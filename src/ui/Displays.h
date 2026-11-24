#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"
#include "../dsp/FFT.h"
#include "Theme.h"

namespace mbx::ui
{
/** Oscillator display. Click cycles 3D waterfall -> 2D -> harmonic spectrum. */
class WaveDisplay final : public juce::Component, private juce::Timer
{
public:
    enum class View { Waterfall3D, Flat2D, Spectrum };

    WaveDisplay (juce::AudioProcessorValueTreeState& s, const dsp::WavetableBank& b) : state (s), bank (b)
    {
        startTimerHz (30);
    }

    void setOscillator (int index) { osc = index; repaint(); }
    void setAnimationEnabled (bool on) { on ? startTimerHz (30) : stopTimer(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown())
        {
            view = static_cast<View> (((int) view + 1) % 3);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto frame = chamferedRect (r.reduced (1.0f), 10.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0a1420), r.getX(), r.getY(),
                                                 juce::Colour (0xff140a1c), r.getRight(), r.getBottom(), false));
        g.fillPath (frame);
        drawGrid (g, r);

        const auto type = (dsp::OscType) choice (params::id::oscType);
        const bool on = value (params::id::oscOn) >= 0.5f;

        if (type == dsp::OscType::Granular)
            drawMessage (g, r, "GRANULAR  //  sample loading arrives in Phase 2");
        else if (view == View::Spectrum)
            drawSpectrum (g, r.reduced (14.0f, 22.0f), type);
        else if (view == View::Waterfall3D)
            draw3D (g, r.reduced (14.0f, 18.0f), type);
        else
            draw2D (g, r.reduced (14.0f, 22.0f), type);

        g.setColour (colours::cyan.withAlpha (0.5f));
        g.strokePath (frame, juce::PathStrokeType (1.0f));

        g.setFont (fonts::mono (11.0f));
        g.setColour (colours::textDim);
        const juce::String viewName = view == View::Waterfall3D ? "3D" : view == View::Flat2D ? "2D" : "SPECTRUM";
        g.drawText (viewName + "  [click]", r.reduced (12.0f, 6.0f), juce::Justification::topRight);
        g.setColour (on ? colours::lime : colours::amber);
        g.drawText (on ? params::oscTypeNames()[(int) type].toUpperCase() : "OFF", r.reduced (12.0f, 6.0f),
                    juce::Justification::topLeft);
    }

private:
    float value (const char* suffix) const
    {
        if (auto* p = state.getRawParameterValue (params::oscId (osc, suffix)))
            return p->load();
        return 0.0f;
    }
    int choice (const char* suffix) const { return juce::roundToInt (value (suffix)); }

    /** One display sample of the current oscillator (naive, for drawing only). */
    float sampleAt (dsp::OscType type, float p, float position) const
    {
        p = dsp::wrap01 (p);
        switch (type)
        {
            case dsp::OscType::Wavetable: return bank.get (choice (params::id::oscTable)).read (p, position, 0);
            case dsp::OscType::FM:        return std::sin (dsp::kTwoPi * p);
            case dsp::OscType::Noise:     return noiseRng.nextFloat() * 2.0f - 1.0f;
            case dsp::OscType::Supersaw:  return 2.0f * p - 1.0f;
            case dsp::OscType::Analog:
            case dsp::OscType::Granular:
            default: break;
        }
        const float pw = value (params::id::oscPw);
        switch ((dsp::VaShape) choice (params::id::oscShape))
        {
            case dsp::VaShape::Sine:     return std::sin (dsp::kTwoPi * p);
            case dsp::VaShape::Triangle: return 1.0f - 4.0f * std::abs (p - 0.5f);
            case dsp::VaShape::Saw:      return 2.0f * p - 1.0f;
            case dsp::VaShape::Square:   return p < 0.5f ? 1.0f : -1.0f;
            case dsp::VaShape::Pulse:
            default:                     return p < pw ? 1.0f : -1.0f;
        }
    }

    juce::Path wavePath (dsp::OscType type, float position, juce::Rectangle<float> area, int points) const
    {
        juce::Path p;
        for (int i = 0; i <= points; ++i)
        {
            const float ph = (float) i / (float) points;
            const float y  = area.getCentreY() - sampleAt (type, ph, position) * area.getHeight() * 0.45f;
            const float x  = area.getX() + ph * area.getWidth();
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        return p;
    }

    void draw2D (juce::Graphics& g, juce::Rectangle<float> area, dsp::OscType type)
    {
        auto p = wavePath (type, value (params::id::oscWtPos), area, 256);
        juce::Path fill (p);
        fill.lineTo (area.getRight(), area.getCentreY());
        fill.lineTo (area.getX(), area.getCentreY());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colours::cyan.withAlpha (0.25f), 0, area.getY(),
                                                 colours::magenta.withAlpha (0.05f), 0, area.getBottom(), false));
        g.fillPath (fill);
        strokeGlow (g, p, colours::cyan, 2.0f);
    }

    void draw3D (juce::Graphics& g, juce::Rectangle<float> area, dsp::OscType type)
    {
        constexpr int slices = 28;
        const float pos   = value (params::id::oscWtPos);
        const bool isWt   = type == dsp::OscType::Wavetable;
        const float sway  = std::sin (anim * 0.6f) * 6.0f;
        const auto sliceW = area.getWidth() * 0.62f;
        const auto sliceH = area.getHeight() * 0.42f;
        const int active  = juce::roundToInt (pos * (slices - 1));

        for (int i = slices - 1; i >= 0; --i) // back to front
        {
            const float depth = (float) i / (slices - 1);
            const float x = area.getX() + depth * (area.getWidth() - sliceW) + sway * (1.0f - depth);
            const float y = area.getBottom() - sliceH - depth * (area.getHeight() - sliceH);
            const float slicePos = isWt ? depth : pos;
            auto path = wavePath (type, slicePos, { x, y, sliceW, sliceH }, 96);

            const bool isActive = isWt ? i == active : i == 0;
            if (isActive)
                strokeGlow (g, path, colours::cyan, 2.0f, 1.5f);
            else
            {
                g.setColour (colours::magenta.interpolatedWith (colours::cyan, 1.0f - depth).withAlpha (0.18f + 0.25f * (1.0f - depth)));
                g.strokePath (path, juce::PathStrokeType (1.0f));
            }
        }
    }

    void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area, dsp::OscType type)
    {
        constexpr int n = 1024, bars = 64;
        std::vector<std::complex<double>> buf (n);
        const float pos = value (params::id::oscWtPos);
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = sampleAt (type, (float) i / n, pos);
        dsp::fft (buf, false);

        const float barW = area.getWidth() / bars;
        for (int h = 1; h <= bars; ++h)
        {
            const float mag = (float) std::abs (buf[(size_t) h]) / (n * 0.5f);
            const float db  = juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (mag, -60.0f));
            const float hgt = area.getHeight() * (1.0f + db / 60.0f);
            auto bar = juce::Rectangle<float> (area.getX() + (h - 1) * barW + 1.0f, area.getBottom() - hgt, barW - 2.0f, hgt);
            g.setGradientFill (juce::ColourGradient (colours::cyan, 0, bar.getY(), colours::magenta.withAlpha (0.4f), 0, area.getBottom(), false));
            g.fillRect (bar);
        }
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (colours::cyan.withAlpha (0.05f));
        for (float x = r.getX(); x < r.getRight(); x += 24.0f)
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        for (float y = r.getY(); y < r.getBottom(); y += 24.0f)
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    }

    void drawMessage (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& msg)
    {
        g.setColour (colours::amber);
        g.setFont (fonts::mono (13.0f));
        g.drawText (msg, r, juce::Justification::centred);
    }

    void timerCallback() override
    {
        anim += 1.0f / 30.0f;
        repaint();
    }

    juce::AudioProcessorValueTreeState& state;
    const dsp::WavetableBank& bank;
    mutable juce::Random noiseRng;
    int osc = 0;
    View view = View::Waterfall3D;
    float anim = 0.0f;
};

/** Draws the amp envelope's DAHDSR shape with its stage curves. */
class EnvelopeDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit EnvelopeDisplay (juce::AudioProcessorValueTreeState& s) : state (s) { startTimerHz (20); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto frame = chamferedRect (r.reduced (1.0f), 8.0f);
        g.setColour (juce::Colour (0xff0a111b));
        g.fillPath (frame);

        auto v = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };
        const float d = v (params::id::env1Delay), a = v (params::id::env1Attack), h = v (params::id::env1Hold);
        const float dc = v (params::id::env1Decay), s = v (params::id::env1Sustain), rl = v (params::id::env1Release);
        const float ac = v (params::id::env1ACurve), dcc = v (params::id::env1DCurve), rc = v (params::id::env1RCurve);

        // sqrt time axis so short and long stages are both readable
        const float w[] = { std::sqrt (d), std::sqrt (a), std::sqrt (h), std::sqrt (dc), 0.6f, std::sqrt (rl) };
        float total = 0.0f;
        for (auto x : w) total += x;
        auto area = r.reduced (10.0f, 12.0f);
        const float sx = area.getWidth() / juce::jmax (0.001f, total);
        auto Y = [&] (float lvl) { return area.getBottom() - lvl * area.getHeight(); };

        juce::Path p;
        float x = area.getX();
        p.startNewSubPath (x, Y (0));
        x += w[0] * sx; p.lineTo (x, Y (0));
        auto segment = [&] (float width, float from, float to, float curve) {
            const float x0 = x;
            for (int i = 1; i <= 24; ++i)
            {
                const float t = (float) i / 24.0f;
                p.lineTo (x0 + t * width, Y (from + (to - from) * dsp::curveShape (t, curve)));
            }
            x += width;
        };
        segment (w[1] * sx, 0.0f, 1.0f, ac);
        x += w[2] * sx; p.lineTo (x, Y (1));
        segment (w[3] * sx, 1.0f, s, dcc);
        x += w[4] * sx; p.lineTo (x, Y (s));
        segment (w[5] * sx, s, 0.0f, rc);

        juce::Path fill (p);
        fill.lineTo (x, Y (0));
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colours::magenta.withAlpha (0.3f), 0, area.getY(), colours::magenta.withAlpha (0.02f), 0, area.getBottom(), false));
        g.fillPath (fill);
        strokeGlow (g, p, colours::magenta, 1.8f);
        g.setColour (colours::magenta.withAlpha (0.5f));
        g.strokePath (frame, juce::PathStrokeType (1.0f));
    }

private:
    void timerCallback() override { repaint(); }
    juce::AudioProcessorValueTreeState& state;
};

/** Triggered oscilloscope of the plugin output. */
class ScopeDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit ScopeDisplay (const ScopeTap& t) : tap (t) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto frame = chamferedRect (r.reduced (1.0f), 8.0f);
        g.setColour (juce::Colour (0xff0a111b));
        g.fillPath (frame);
        g.setColour (colours::cyan.withAlpha (0.08f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

        constexpr int show = 600;
        int start = 0;
        for (int i = 1; i < kLen - show; ++i) // rising zero-crossing trigger
            if (samples[(size_t) i - 1] <= 0.0f && samples[(size_t) i] > 0.0f) { start = i; break; }

        juce::Path p;
        auto area = r.reduced (6.0f, 10.0f);
        for (int i = 0; i < show; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (show - 1);
            const float y = area.getCentreY() - juce::jlimit (-1.0f, 1.0f, samples[(size_t) (start + i)]) * area.getHeight() * 0.5f;
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        strokeGlow (g, p, colours::lime, 1.5f);
        g.setColour (colours::cyan.withAlpha (0.5f));
        g.strokePath (frame, juce::PathStrokeType (1.0f));
        g.setFont (fonts::mono (10.0f));
        g.setColour (colours::textDim);
        g.drawText ("SCOPE", r.reduced (10.0f, 4.0f), juce::Justification::topLeft);
    }

private:
    static constexpr int kLen = 2048;
    void timerCallback() override
    {
        tap.copyLatest (samples.data(), kLen);
        repaint();
    }
    const ScopeTap& tap;
    std::array<float, kLen> samples {};
};

} // namespace mbx::ui
