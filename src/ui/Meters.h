// Meters and visualisers: stereo peak/RMS bars with peak hold + clip LED, LUFS readout,
// goniometer with phase correlation, and an FFT spectrum analyser with peak hold.
#pragma once

#include "../PluginProcessor.h"
#include "../dsp/FFT.h"
#include "Theme.h"

namespace mbx::ui
{
inline float meterPos (float gain, float floorDb = -60.0f, float topDb = 6.0f)
{
    return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (gain, floorDb) - floorDb) / (topDb - floorDb));
}

/** Vertical stereo bar meter (peak bars + RMS shading + peak hold + clip LED). */
class StereoMeter final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    StereoMeter (Meter& m, bool slimStyle) : meter (m), slim (slimStyle) { startTimerHz (GlobalSettings::get().fps (30)); }
    std::function<void()> onClick;

    void mouseUp (const juce::MouseEvent&) override
    {
        meter.resetClip();
        if (onClick) onClick();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (! slim)
            drawScreen (g, r, colours::cyan);
        auto area = slim ? r : r.reduced (10.0f, 18.0f).withTrimmedBottom (4.0f);
        const float gap = slim ? 1.0f : 6.0f;
        const float w = (area.getWidth() - gap) * 0.5f;
        const float pk[2] = { peak[0], peak[1] }, rm[2] = { rms[0], rms[1] }, hd[2] = { hold[0], hold[1] };
        for (int c = 0; c < 2; ++c)
        {
            auto bar = juce::Rectangle<float> (area.getX() + (float) c * (w + gap), area.getY(), w, area.getHeight());
            g.setColour (colours::track);
            g.fillRect (bar);
            const float ph = bar.getHeight() * meterPos (pk[c]);
            const float rh = bar.getHeight() * meterPos (rm[c]);
            g.setGradientFill (juce::ColourGradient (colours::amber, 0, bar.getY(), colours::lime, 0, bar.getBottom(), false));
            g.fillRect (bar.withTop (bar.getBottom() - ph));
            g.setColour (colours::cyan.withAlpha (0.65f));
            g.fillRect (bar.withTop (bar.getBottom() - rh).reduced (bar.getWidth() * 0.25f, 0.0f));
            if (! slim) // segment gaps
                for (float y = bar.getY(); y < bar.getBottom(); y += 4.0f)
                {
                    g.setColour (colours::base.withAlpha (0.5f));
                    g.fillRect (bar.getX(), y, bar.getWidth(), 1.0f);
                }
            const float hy = bar.getBottom() - bar.getHeight() * meterPos (hd[c]);
            g.setColour (hd[c] >= 1.0f ? colours::amber : colours::text);
            g.fillRect (bar.getX(), hy - 1.0f, bar.getWidth(), 2.0f);
        }
        if (! slim)
        {
            g.setFont (fonts::mono (9.0f));
            for (int db : { 0, -6, -12, -24, -48 })
            {
                const float y = area.getBottom() - area.getHeight() * meterPos (juce::Decibels::decibelsToGain ((float) db));
                g.setColour (colours::textDim);
                g.drawText (juce::String (db), juce::Rectangle<float> (r.getX(), y - 5.0f, 10.0f, 10.0f), juce::Justification::centredLeft);
            }
            drawLed (g, { r.getCentreX(), r.getY() + 9.0f }, 3.5f, colours::amber, clipped);
        }
        else if (clipped)
        {
            g.setColour (colours::amber);
            g.fillRect (r.removeFromTop (4.0f));
        }
    }

private:
    void timerCallback() override
    {
        const auto s = meter.read();
        const float p[2] = { s.peakL, s.peakR }, rr[2] = { s.rmsL, s.rmsR };
        for (int c = 0; c < 2; ++c)
        {
            peak[c] = std::max (p[c], peak[c] * 0.86f);
            rms[c] = rr[c];
            if (p[c] >= hold[c]) { hold[c] = p[c]; holdTicks[c] = 45; }
            else if (--holdTicks[c] < 0) hold[c] *= 0.95f;
        }
        clipped = s.clip;
        repaint();
    }

    Meter& meter;
    bool slim;
    float peak[2] {}, rms[2] {}, hold[2] {};
    int holdTicks[2] {};
    bool clipped = false;
};

/** LUFS / true-peak numeric readout. */
class LoudnessReadout final : public juce::Component, private juce::Timer
{
public:
    explicit LoudnessReadout (Meter& m) : meter (m) { startTimerHz (GlobalSettings::get().fps (10)); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawScreen (g, r, colours::magenta);
        const auto s = meter.read();
        auto row = [&] (const juce::String& label, const juce::String& value, juce::Colour c) {
            auto line = r.reduced (10.0f, 0.0f).removeFromTop (18.0f).translated (0.0f, yy);
            g.setFont (fonts::mono (10.0f));
            g.setColour (colours::textDim);
            g.drawText (label, line, juce::Justification::centredLeft);
            g.setFont (fonts::mono (13.0f));
            g.setColour (c);
            g.drawText (value, line, juce::Justification::centredRight);
            yy += 20.0f;
        };
        yy = 8.0f;
        auto fmt = [] (float v) { return v <= -99.0f ? juce::String ("-inf") : juce::String (v, 1); };
        row ("LUFS MOMENTARY", fmt (s.lufsM), colours::cyan);
        row ("LUFS SHORT-TERM", fmt (s.lufsS), colours::cyan);
        row ("TRUE PEAK dBTP", fmt (juce::Decibels::gainToDecibels (s.truePeak, -100.0f)), s.truePeak >= 1.0f ? colours::amber : colours::lime);
        row ("PEAK dBFS", fmt (juce::Decibels::gainToDecibels (std::max (s.peakL, s.peakR), -100.0f)), colours::text);
    }

private:
    void timerCallback() override { repaint(); }
    Meter& meter;
    float yy = 0.0f;
};

/** Mid/side goniometer (Lissajous) + correlation bar. */
class Goniometer final : public juce::Component, private juce::Timer
{
public:
    explicit Goniometer (const ScopeTap& t) : tap (t) { startTimerHz (GlobalSettings::get().fps (30)); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawScreen (g, r, colours::lime);
        auto sq = r.reduced (12.0f, 20.0f).withTrimmedBottom (12.0f);
        sq = sq.withSizeKeepingCentre (std::min (sq.getWidth(), sq.getHeight()), std::min (sq.getWidth(), sq.getHeight()));
        const auto c = sq.getCentre();
        g.setColour (colours::lime.withAlpha (0.15f));
        g.drawLine (sq.getX(), c.y, sq.getRight(), c.y);
        g.drawLine (c.x, sq.getY(), c.x, sq.getBottom());
        g.drawLine (sq.getX(), sq.getY(), sq.getRight(), sq.getBottom());
        g.drawLine (sq.getRight(), sq.getY(), sq.getX(), sq.getBottom());
        g.setFont (fonts::mono (9.0f));
        g.drawText ("M", juce::Rectangle<float> (c.x - 5, sq.getY() - 12, 10, 10), juce::Justification::centred);

        const float scale = sq.getWidth() * 0.5f * gain;
        for (int i = 0; i < kN; i += 2)
        {
            const float side = (l[(size_t) i] - r2[(size_t) i]) * 0.7071f, mid = (l[(size_t) i] + r2[(size_t) i]) * 0.7071f;
            const float x = c.x + juce::jlimit (-1.0f, 1.0f, side) * scale, y = c.y - juce::jlimit (-1.0f, 1.0f, mid) * scale;
            g.setColour (colours::lime.withAlpha (0.35f));
            g.fillRect (x, y, 1.5f, 1.5f);
        }
        // correlation
        auto bar = r.reduced (14.0f, 0.0f).removeFromBottom (14.0f).translated (0.0f, -4.0f);
        g.setColour (colours::track);
        g.fillRect (bar);
        const float cx = bar.getCentreX() + corr * bar.getWidth() * 0.5f;
        g.setColour (corr < 0.0f ? colours::amber : colours::lime);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (cx, bar.getCentreX()), bar.getY(), std::max (cx, bar.getCentreX()), bar.getBottom()));
        g.setColour (colours::textDim);
        g.drawText ("CORR " + juce::String (corr, 2), r.reduced (10.0f, 4.0f), juce::Justification::topLeft);
    }

private:
    static constexpr int kN = 1024;
    void timerCallback() override
    {
        tap.copyLatestStereo (l.data(), r2.data(), kN);
        double lr = 0, ll = 0, rr = 0;
        float pk = 0.0f;
        for (int i = 0; i < kN; ++i)
        {
            lr += (double) l[(size_t) i] * r2[(size_t) i];
            ll += (double) l[(size_t) i] * l[(size_t) i];
            rr += (double) r2[(size_t) i] * r2[(size_t) i];
            pk = std::max ({ pk, std::abs (l[(size_t) i]), std::abs (r2[(size_t) i]) });
        }
        const float target = ll > 1e-9 && rr > 1e-9 ? (float) (lr / std::sqrt (ll * rr)) : 0.0f;
        corr += (target - corr) * 0.2f;
        gain += (juce::jlimit (1.0f, 8.0f, 0.7f / std::max (pk, 1.0e-3f)) - gain) * 0.1f; // auto-zoom
        repaint();
    }
    const ScopeTap& tap;
    std::array<float, kN> l {}, r2 {};
    float corr = 0.0f, gain = 1.0f;
};

/** Log-frequency FFT spectrum of the output with peak hold. Optional overlay callback (EQ curve). */
class SpectrumAnalyser : public juce::Component, private juce::Timer
{
public:
    SpectrumAnalyser (const ScopeTap& t, std::function<double()> sampleRate) : tap (t), sr (std::move (sampleRate))
    {
        smoothDb.fill (kMinDb);
        peakDb.fill (kMinDb);
        startTimerHz (GlobalSettings::get().fps (30));
    }

    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f, kMinDb = -84.0f, kMaxDb = 12.0f;
    float xForHz (float hz, juce::Rectangle<float> a) const { return a.getX() + a.getWidth() * std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz); }
    float hzForX (float x, juce::Rectangle<float> a) const { return kMinHz * std::pow (kMaxHz / kMinHz, (x - a.getX()) / a.getWidth()); }
    float yForDb (float db, juce::Rectangle<float> a, float lo = kMinDb, float hi = kMaxDb) const
    {
        return a.getY() + a.getHeight() * (1.0f - (juce::jlimit (lo, hi, db) - lo) / (hi - lo));
    }
    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (12.0f, 20.0f); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawScreen (g, r, colours::cyan);
        const auto a = plotArea();
        g.setFont (fonts::mono (9.0f));
        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xForHz (hz, a);
            g.setColour (colours::cyan.withAlpha (0.12f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (colours::textDim);
            g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz),
                        juce::Rectangle<float> (x + 2, a.getBottom() - 11, 30, 10), juce::Justification::centredLeft);
        }

        juce::Path spec, fill, peaks;
        for (int i = 0; i < kPoints; ++i)
        {
            const float x = a.getX() + a.getWidth() * (float) i / (kPoints - 1);
            const float y = yForDb (smoothDb[(size_t) i], a);
            const float yp = yForDb (peakDb[(size_t) i], a);
            if (i == 0) { spec.startNewSubPath (x, y); fill.startNewSubPath (x, a.getBottom()); peaks.startNewSubPath (x, yp); }
            spec.lineTo (x, y);
            fill.lineTo (x, y);
            peaks.lineTo (x, yp);
        }
        fill.lineTo (a.getRight(), a.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colours::cyan.withAlpha (0.25f), 0, a.getY(), colours::magenta.withAlpha (0.05f), 0, a.getBottom(), false));
        g.fillPath (fill);
        g.setColour (colours::cyan.withAlpha (0.8f));
        g.strokePath (spec, juce::PathStrokeType (1.0f));
        g.setColour (colours::magenta.withAlpha (0.5f));
        g.strokePath (peaks, juce::PathStrokeType (1.0f));
        paintOverlay (g, a);
        g.setColour (colours::textDim);
        g.drawText ("ANALYSER  //  FFT 4096  //  PEAK HOLD", r.reduced (10.0f, 4.0f), juce::Justification::topLeft);
    }

protected:
    virtual void paintOverlay (juce::Graphics&, juce::Rectangle<float>) {}

private:
    static constexpr int kFft = 4096, kPoints = 240;
    void timerCallback() override
    {
        std::vector<float> l ((size_t) kFft), r ((size_t) kFft);
        tap.copyLatestStereo (l.data(), r.data(), kFft);
        std::vector<std::complex<double>> spec ((size_t) kFft);
        for (int i = 0; i < kFft; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * i / (kFft - 1));
            spec[(size_t) i] = 0.5 * (l[(size_t) i] + r[(size_t) i]) * w;
        }
        dsp::fft (spec, false);
        const float rate = (float) std::max (8000.0, sr());
        for (int i = 0; i < kPoints; ++i)
        {
            const float f0 = kMinHz * std::pow (kMaxHz / kMinHz, (float) i / kPoints);
            const float f1 = kMinHz * std::pow (kMaxHz / kMinHz, (float) (i + 1) / kPoints);
            int b0 = std::max (1, (int) (f0 / rate * kFft)), b1 = std::max (b0, (int) (f1 / rate * kFft));
            b1 = std::min (b1, kFft / 2 - 1);
            double mx = 0.0;
            for (int b = b0; b <= b1; ++b) mx = std::max (mx, std::abs (spec[(size_t) b]));
            const float db = juce::Decibels::gainToDecibels ((float) (mx / (kFft * 0.25)), -120.0f) + 3.0f * std::log2 (std::max (f0, 20.0f) / 1000.0f); // +3 dB/oct tilt
            smoothDb[(size_t) i] = db > smoothDb[(size_t) i] ? db : smoothDb[(size_t) i] * 0.85f + db * 0.15f;
            peakDb[(size_t) i] = std::max (peakDb[(size_t) i] - 0.25f, smoothDb[(size_t) i]);
        }
        repaint();
    }
    const ScopeTap& tap;
    std::function<double()> sr;
    std::array<float, kPoints> smoothDb {}, peakDb {};
};

} // namespace mbx::ui
