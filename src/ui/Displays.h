#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"
#include "../dsp/FFT.h"
#include "Theme.h"

namespace mbx::ui
{
inline float rawParam (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    if (auto* p = s.getRawParameterValue (id))
        return p->load();
    return 0.0f;
}

//==============================================================================
/** Oscillator display. Click cycles 3D waterfall -> 2D -> harmonic spectrum.
    In draw mode the user paints a single cycle that becomes the osc's "User" wavetable. */
class WaveDisplay final : public juce::Component, private juce::Timer
{
public:
    enum class View { Waterfall3D, Flat2D, Spectrum };

    explicit WaveDisplay (MysteryBoxProcessor& p) : proc (p), state (p.getState()), bank (p.getWavetables())
    {
        drawn.assign (128, 0.0f);
        startTimerHz (GlobalSettings::get().fps (30));
    }

    void setOscillator (int index) { osc = index; repaint(); }
    void setDrawMode (bool on)
    {
        drawMode = on;
        if (on)
            for (size_t i = 0; i < drawn.size(); ++i)
                drawn[i] = sampleAt (currentType(), (float) i / (float) drawn.size(), value (params::id::oscWtPos));
        repaint();
    }
    bool isDrawMode() const noexcept { return drawMode; }
    std::function<void()> onDrawFinished;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (drawMode) { paintAt (e.position, e.position); lastDraw = e.position; }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drawMode) { paintAt (lastDraw, e.position); lastDraw = e.position; repaint(); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (drawMode)
        {
            std::vector<float> cycle (drawn.begin(), drawn.end());
            proc.getAssets().setDrawnWavetable (osc, cycle);
            if (onDrawFinished) onDrawFinished();
            return;
        }
        if (! e.mouseWasDraggedSinceMouseDown())
        {
            view = static_cast<View> (((int) view + 1) % 3);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawScreen (g, r, drawMode ? colours::lime : colours::cyan);

        const auto type = currentType();
        const bool on = value (params::id::oscOn) >= 0.5f;
        auto area = r.reduced (14.0f, 24.0f);

        if (drawMode)
            drawCycleEditor (g, area);
        else if (type == dsp::OscType::Granular)
            drawGranular (g, area);
        else if (view == View::Spectrum)
            drawSpectrum (g, area, type);
        else if (view == View::Waterfall3D)
            draw3D (g, r.reduced (14.0f, 20.0f), type);
        else
            draw2D (g, area, type);

        // HUD readouts
        g.setFont (fonts::mono (10.0f));
        g.setColour (on ? colours::lime : colours::amber);
        g.drawText ((on ? params::oscTypeNames()[(int) type].toUpperCase() : juce::String ("OFFLINE")) + "  //  OSC.0" + juce::String (osc + 1),
                    r.reduced (12.0f, 6.0f), juce::Justification::topLeft);
        g.setColour (colours::textDim);
        const juce::String viewName = drawMode ? "DRAW MODE" : view == View::Waterfall3D ? "3D" : view == View::Flat2D ? "2D" : "SPECTRUM";
        g.drawText (viewName + (drawMode ? "" : "  [CLICK]"), r.reduced (12.0f, 6.0f), juce::Justification::topRight);
        g.drawText ("POS " + juce::String (value (params::id::oscWtPos) * 100.0f, 1) + "%   FRM " + juce::String (frameCounter % 10000).paddedLeft ('0', 4),
                    r.reduced (12.0f, 6.0f), juce::Justification::bottomLeft);
        g.drawText (tableLabel(), r.reduced (12.0f, 6.0f), juce::Justification::bottomRight);
    }

private:
    float value (const char* suffix) const { return rawParam (state, params::oscId (osc, suffix)); }
    int choice (const char* suffix) const { return juce::roundToInt (value (suffix)); }
    dsp::OscType currentType() const { return (dsp::OscType) choice (params::id::oscType); }

    juce::String tableLabel() const
    {
        const auto t = currentType();
        if (t == dsp::OscType::Wavetable)
        {
            const int idx = choice (params::id::oscTable);
            if (idx == dsp::kUserTableIndex)
                if (auto* ut = proc.getAssets().userTables[(size_t) osc].get())
                    return "USER: " + juce::String (ut->getName()).toUpperCase();
            return params::tableNames()[idx].toUpperCase();
        }
        if (t == dsp::OscType::Granular)
            if (auto* sm = proc.getAssets().samples[(size_t) osc].get())
                return juce::String (sm->name).toUpperCase();
        return {};
    }

    const dsp::Wavetable* table() const
    {
        const int idx = choice (params::id::oscTable);
        if (idx == dsp::kUserTableIndex)
            if (auto* ut = proc.getAssets().userTables[(size_t) osc].get())
                return ut;
        return &bank.get (idx);
    }

    float sampleAt (dsp::OscType type, float p, float position) const
    {
        p = dsp::wrap01 (p);
        switch (type)
        {
            case dsp::OscType::Wavetable: return table()->read (p, position, 0);
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
            const float y  = area.getCentreY() - juce::jlimit (-1.2f, 1.2f, sampleAt (type, ph, position)) * area.getHeight() * 0.45f;
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
        g.setGradientFill (juce::ColourGradient (colours::cyan.withAlpha (0.25f), 0, area.getY(), colours::magenta.withAlpha (0.05f), 0, area.getBottom(), false));
        g.fillPath (fill);
        g.setColour (colours::cyan.withAlpha (0.2f));
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
        strokeGlow (g, p, colours::cyan, 2.0f);
        // scanning playhead
        const float sx = area.getX() + area.getWidth() * dsp::wrap01 (anim * 0.35f);
        g.setGradientFill (juce::ColourGradient (colours::lime.withAlpha (0.0f), sx - 30.0f, 0, colours::lime.withAlpha (0.25f), sx, 0, false));
        g.fillRect (sx - 30.0f, area.getY(), 30.0f, area.getHeight());
    }

    void draw3D (juce::Graphics& g, juce::Rectangle<float> area, dsp::OscType type)
    {
        constexpr int slices = 30;
        const float pos   = value (params::id::oscWtPos);
        const bool isWt   = type == dsp::OscType::Wavetable;
        const float sway  = std::sin (anim * 0.6f) * 6.0f;
        const auto sliceW = area.getWidth() * 0.62f;
        const auto sliceH = area.getHeight() * 0.40f;
        const int active  = juce::roundToInt (pos * (slices - 1));

        // floor grid in perspective
        g.setColour (colours::cyan.withAlpha (0.08f));
        for (int i = 0; i <= 6; ++i)
        {
            const float t = (float) i / 6.0f;
            g.drawLine (area.getX() + t * sliceW, area.getBottom(), area.getRight() - sliceW + t * sliceW, area.getY() + sliceH, 1.0f);
        }

        for (int i = slices - 1; i >= 0; --i)
        {
            const float depth = (float) i / (slices - 1);
            const float x = area.getX() + depth * (area.getWidth() - sliceW) + sway * (1.0f - depth);
            const float y = area.getBottom() - sliceH - depth * (area.getHeight() - sliceH);
            const float slicePos = isWt ? depth : pos;
            auto path = wavePath (type, slicePos, { x, y, sliceW, sliceH }, 96);

            const bool isActive = isWt ? i == active : i == 0;
            if (isActive)
            {
                strokeGlow (g, path, colours::cyan, 2.0f, 1.5f);
                g.setColour (colours::cyan.withAlpha (0.6f));
                g.drawText (">", juce::Rectangle<float> (x - 14.0f, y, 12.0f, sliceH), juce::Justification::centred);
            }
            else
            {
                g.setColour (colours::magenta.interpolatedWith (colours::cyan, 1.0f - depth).withAlpha (0.16f + 0.25f * (1.0f - depth)));
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
        g.setFont (fonts::mono (9.0f));
        g.setColour (colours::textDim);
        for (int h : { 1, 8, 16, 32, 64 })
            g.drawText ("H" + juce::String (h), juce::Rectangle<float> (area.getX() + (h - 1) * barW - 6.0f, area.getBottom() + 2.0f, 26.0f, 10.0f),
                        juce::Justification::centredLeft);
    }

    void drawGranular (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto* sample = proc.getAssets().samples[(size_t) osc].get();
        if (sample == nullptr || sample->data.empty())
        {
            g.setColour (colours::amber);
            g.setFont (fonts::mono (12.0f));
            g.drawFittedText ("NO SAMPLE LOADED\n\nDrop a WAV / AIFF / FLAC here\nor press LOAD", area.toNearestInt(), juce::Justification::centred, 4);
            return;
        }
        const auto& d = sample->data;
        const int cols = (int) area.getWidth();
        for (int c = 0; c < cols; ++c)
        {
            const size_t a = (size_t) ((double) c / cols * (double) d.size());
            const size_t b = std::min (d.size(), (size_t) ((double) (c + 1) / cols * (double) d.size()) + 1);
            float lo = 0.0f, hi = 0.0f;
            for (size_t k = a; k < b; k += std::max<size_t> (1, (b - a) / 32))
            {
                lo = std::min (lo, d[k]);
                hi = std::max (hi, d[k]);
            }
            g.setColour (colours::cyan.withAlpha (0.55f));
            g.drawVerticalLine ((int) area.getX() + c, area.getCentreY() - hi * area.getHeight() * 0.45f,
                                area.getCentreY() - lo * area.getHeight() * 0.45f + 1.0f);
        }
        const float pos   = value (params::id::oscWtPos);
        const float spray = value (params::id::oscGrainSpray);
        const float px    = area.getX() + pos * area.getWidth();
        const float sw    = spray * 0.5f * area.getWidth();
        g.setColour (colours::magenta.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (px - sw, area.getY(), sw * 2.0f, area.getHeight()).getIntersection (area));
        g.setColour (colours::lime);
        g.fillRect (px - 1.0f, area.getY(), 2.0f, area.getHeight());
        // animated grain sparks
        juce::Random r ((juce::int64) (anim * 20.0f));
        for (int k = 0; k < 14; ++k)
        {
            const float gx = px + (r.nextFloat() * 2.0f - 1.0f) * sw;
            const float gy = area.getY() + r.nextFloat() * area.getHeight();
            g.setColour (colours::lime.withAlpha (0.25f + 0.5f * r.nextFloat()));
            g.fillEllipse (gx - 1.5f, gy - 1.5f, 3.0f, 3.0f);
        }
    }

    void drawCycleEditor (juce::Graphics& g, juce::Rectangle<float> area)
    {
        editArea = area;
        juce::Path p;
        for (size_t i = 0; i <= drawn.size(); ++i)
        {
            const float v = drawn[i % drawn.size()];
            const float x = area.getX() + area.getWidth() * (float) i / (float) drawn.size();
            const float y = area.getCentreY() - v * area.getHeight() * 0.45f;
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        g.setColour (colours::lime.withAlpha (0.25f));
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
        strokeGlow (g, p, colours::lime, 2.0f);
        g.setFont (fonts::mono (10.0f));
        g.setColour (colours::lime);
        g.drawText ("PAINT A SINGLE CYCLE  //  RELEASE TO APPLY", area.withTrimmedTop (area.getHeight() - 12.0f), juce::Justification::centred);
    }

    void paintAt (juce::Point<float> a, juce::Point<float> b)
    {
        if (editArea.isEmpty()) return;
        const int n = (int) drawn.size();
        auto toIdx = [&] (float x) { return juce::jlimit (0, n - 1, (int) ((x - editArea.getX()) / editArea.getWidth() * (float) n)); };
        auto toVal = [&] (float y) { return juce::jlimit (-1.0f, 1.0f, (editArea.getCentreY() - y) / (editArea.getHeight() * 0.45f)); };
        const int i0 = toIdx (a.x), i1 = toIdx (b.x);
        const float v0 = toVal (a.y), v1 = toVal (b.y);
        const int lo = std::min (i0, i1), hi = std::max (i0, i1);
        for (int i = lo; i <= hi; ++i)
        {
            const float t = hi == lo ? 1.0f : (float) (i - lo) / (float) (hi - lo);
            drawn[(size_t) i] = i0 <= i1 ? v0 + (v1 - v0) * t : v1 + (v0 - v1) * t;
        }
    }

    void timerCallback() override
    {
        anim += 1.0f / 30.0f;
        ++frameCounter;
        repaint();
    }

    MysteryBoxProcessor& proc;
    juce::AudioProcessorValueTreeState& state;
    const dsp::WavetableBank& bank;
    mutable juce::Random noiseRng;
    int osc = 0, frameCounter = 0;
    View view = View::Waterfall3D;
    float anim = 0.0f;
    bool drawMode = false;
    std::vector<float> drawn;
    juce::Rectangle<float> editArea;
    juce::Point<float> lastDraw;
};

//==============================================================================
/** Live frequency response of one filter, measured by running the real DSP on an impulse. */
class FilterDisplay final : public juce::Component, private juce::Timer
{
public:
    FilterDisplay (juce::AudioProcessorValueTreeState& s, int filterIndex) : state (s), index (filterIndex)
    {
        unit.prepare (kRate);
        startTimerHz (GlobalSettings::get().fps (20));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto accent = index == 0 ? colours::cyan : colours::magenta;
        drawScreen (g, r, accent);
        auto area = r.reduced (8.0f, 14.0f);

        // dB / frequency graticule
        g.setFont (fonts::mono (8.5f));
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = xFor (hz, area);
            g.setColour (accent.withAlpha (0.15f));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            g.setColour (colours::textDim);
            g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz), juce::Rectangle<float> (x + 2, area.getBottom() - 10, 26, 10),
                        juce::Justification::centredLeft);
        }
        g.setColour (accent.withAlpha (0.2f));
        g.drawHorizontalLine ((int) yFor (0.0f, area), area.getX(), area.getRight());

        if (! enabled)
        {
            g.setColour (colours::amber);
            g.setFont (fonts::mono (11.0f));
            g.drawText ("BYPASSED", area, juce::Justification::centred);
            return;
        }

        juce::Path curve, fill;
        for (int i = 0; i < kPoints; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (kPoints - 1);
            const float y = yFor (responseDb[(size_t) i], area);
            if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, area.getBottom()); }
            curve.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (area.getRight(), area.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.3f), 0, area.getY(), accent.withAlpha (0.02f), 0, area.getBottom(), false));
        g.fillPath (fill);
        strokeGlow (g, curve, accent, 1.6f);

        const float cx = xFor (cutoffHz, area);
        g.setColour (colours::lime.withAlpha (0.7f));
        g.drawVerticalLine ((int) cx, area.getY(), area.getBottom());
        g.setColour (colours::lime);
        g.drawText (cutoffHz >= 1000.0f ? juce::String (cutoffHz / 1000.0f, 2) + "k" : juce::String ((int) cutoffHz),
                    juce::Rectangle<float> (cx + 3, area.getY(), 40, 10), juce::Justification::centredLeft);
    }

private:
    static constexpr float kRate = 48000.0f;
    static constexpr int kFft = 4096, kPoints = 160;

    static float xFor (float hz, juce::Rectangle<float> a) { return a.getX() + a.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); }
    static float yFor (float db, juce::Rectangle<float> a) { return a.getY() + a.getHeight() * (1.0f - (juce::jlimit (-36.0f, 24.0f, db) + 36.0f) / 60.0f); }

    void timerCallback() override
    {
        auto v = [this] (const char* id) { return rawParam (state, params::filterId (index, id)); };
        dsp::FilterSettings s;
        s.on = v (params::id::filterOn) >= 0.5f;
        s.type = (dsp::FilterType) juce::roundToInt (v (params::id::filterType));
        s.resonance = v (params::id::filterRes);
        s.drive = v (params::id::filterDrive);
        s.mix = v (params::id::filterMix);
        const float hz = v (params::id::filterCutoff);
        const juce::String key = juce::String ((int) s.on) + "|" + juce::String ((int) s.type) + "|" + juce::String (s.resonance, 3) + "|"
                               + juce::String (s.drive, 3) + "|" + juce::String (s.mix, 3) + "|" + juce::String (hz, 1);
        if (key == lastKey)
            return;
        lastKey = key;
        enabled = s.on;
        cutoffHz = hz;

        std::vector<float> l ((size_t) kFft, 0.0f);
        l[0] = 0.05f; // small impulse keeps the nonlinear models in their linear range
        unit.reset();
        for (int pos = 0; pos < kFft; pos += 64)
            unit.process (s, hz, hz, nullptr, l.data() + pos, nullptr, 64, kRate);

        std::vector<std::complex<double>> spec ((size_t) kFft);
        for (int i = 0; i < kFft; ++i)
            spec[(size_t) i] = l[(size_t) i] / 0.05f;
        dsp::fft (spec, false);
        for (int i = 0; i < kPoints; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / (float) (kPoints - 1));
            const float fb = juce::jlimit (1.0f, (float) (kFft / 2 - 2), f / kRate * (float) kFft);
            const auto b0 = (size_t) fb;
            const float fr = fb - (float) b0; // interpolate between bins: smooth low-frequency curve
            const float mag = (float) std::abs (spec[b0]) * (1.0f - fr) + (float) std::abs (spec[b0 + 1]) * fr;
            responseDb[(size_t) i] = juce::Decibels::gainToDecibels (mag, -60.0f);
        }
        repaint();
    }

    juce::AudioProcessorValueTreeState& state;
    int index;
    dsp::FilterUnit unit;
    std::array<float, kPoints> responseDb {};
    juce::String lastKey;
    float cutoffHz = 1000.0f;
    bool enabled = true;
};

//==============================================================================
/** DAHDSR shape of one envelope with a live "level" readout. */
class EnvelopeDisplay final : public juce::Component, private juce::Timer
{
public:
    EnvelopeDisplay (juce::AudioProcessorValueTreeState& s, int envIndex, juce::Colour c) : state (s), env (envIndex), accent (c)
    {
        startTimerHz (GlobalSettings::get().fps (20));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawScreen (g, r, accent);

        auto v = [this] (const char* id) { return rawParam (state, params::envId (env, id)); };
        const float d = v (params::id::envDelay), a = v (params::id::envAttack), h = v (params::id::envHold);
        const float dc = v (params::id::envDecay), s = v (params::id::envSustain), rl = v (params::id::envRelease);
        const float ac = v (params::id::envACurve), dcc = v (params::id::envDCurve), rc = v (params::id::envRCurve);

        const float w[] = { std::sqrt (d), std::sqrt (a), std::sqrt (h), std::sqrt (dc), 0.6f, std::sqrt (rl) };
        float total = 0.0f;
        for (auto x : w) total += x;
        auto area = r.reduced (12.0f, 18.0f);
        const float sx = area.getWidth() / juce::jmax (0.001f, total);
        auto Y = [&] (float lvl) { return area.getBottom() - lvl * area.getHeight(); };

        juce::Path p;
        float x = area.getX();
        p.startNewSubPath (x, Y (0));
        std::array<float, 6> marks {};
        x += w[0] * sx; p.lineTo (x, Y (0)); marks[0] = x;
        auto segment = [&] (float width, float from, float to, float curve) {
            const float x0 = x;
            for (int i = 1; i <= 24; ++i)
            {
                const float t = (float) i / 24.0f;
                p.lineTo (x0 + t * width, Y (from + (to - from) * dsp::curveShape (t, curve)));
            }
            x += width;
        };
        segment (w[1] * sx, 0.0f, 1.0f, ac); marks[1] = x;
        x += w[2] * sx; p.lineTo (x, Y (1)); marks[2] = x;
        segment (w[3] * sx, 1.0f, s, dcc); marks[3] = x;
        x += w[4] * sx; p.lineTo (x, Y (s)); marks[4] = x;
        segment (w[5] * sx, s, 0.0f, rc); marks[5] = x;

        juce::Path fill (p);
        fill.lineTo (x, Y (0));
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.3f), 0, area.getY(), accent.withAlpha (0.02f), 0, area.getBottom(), false));
        g.fillPath (fill);
        strokeGlow (g, p, accent, 1.8f);

        const char* names[] = { "D", "A", "H", "D", "S", "R" };
        g.setFont (fonts::mono (9.0f));
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.drawVerticalLine ((int) marks[(size_t) i], area.getY(), area.getBottom());
            g.setColour (colours::textDim);
            g.drawText (names[i], juce::Rectangle<float> (marks[(size_t) i] - 10, r.getBottom() - 14, 10, 10), juce::Justification::centredRight);
        }
        g.setColour (accent);
        g.setFont (fonts::mono (10.0f));
        g.drawText ("ENV." + juce::String (env + 1) + (env == 0 ? "  VCA" : "") + (v (params::id::envLoop) >= 0.5f ? "  LOOP" : ""),
                    r.reduced (10.0f, 4.0f), juce::Justification::topLeft);
    }

private:
    void timerCallback() override { repaint(); }
    juce::AudioProcessorValueTreeState& state;
    int env;
    juce::Colour accent;
};

//==============================================================================
/** LFO waveform preview with a running phase cursor and the live output value. */
class LfoDisplay final : public juce::Component, private juce::Timer
{
public:
    LfoDisplay (MysteryBoxProcessor& p, int lfoIndex) : proc (p), state (p.getState()), lfo (lfoIndex) { startTimerHz (GlobalSettings::get().fps (30)); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawScreen (g, r, colours::lime);
        auto area = r.reduced (12.0f, 20.0f);
        auto v = [this] (const char* id) { return rawParam (state, params::lfoId (lfo, id)); };
        const auto shape = (dsp::LfoShape) juce::roundToInt (v (params::id::lfoShape));
        const bool bipolar = v (params::id::lfoBipolar) >= 0.5f;
        const auto custom = proc.getAssets().shapes.get (ShapeStore::lfoIndex (lfo));

        juce::Path p;
        constexpr int pts = 200;
        for (int i = 0; i <= pts; ++i)
        {
            const double c = 2.0 * (double) i / pts; // two cycles
            const float y = dsp::lfoWave (shape, c, &custom, 1234);
            const float px = area.getX() + area.getWidth() * (float) i / pts;
            const float py = area.getCentreY() - y * area.getHeight() * 0.45f;
            i == 0 ? p.startNewSubPath (px, py) : p.lineTo (px, py);
        }
        g.setColour (colours::lime.withAlpha (0.15f));
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
        strokeGlow (g, p, colours::lime, 1.8f);

        const float cx = area.getX() + area.getWidth() * (float) std::fmod (cursor, 2.0) * 0.5f;
        const float cy = area.getCentreY() - dsp::lfoWave (shape, cursor, &custom, 1234) * area.getHeight() * 0.45f;
        g.setColour (colours::magenta.withAlpha (0.4f));
        g.drawVerticalLine ((int) cx, area.getY(), area.getBottom());
        drawLed (g, { cx, cy }, 3.0f, colours::magenta, true);

        const float live = proc.getLiveLfo (lfo);
        g.setFont (fonts::mono (10.0f));
        g.setColour (colours::lime);
        g.drawText ("LFO." + juce::String (lfo + 1) + "  " + params::lfoShapeNames()[(int) shape].toUpperCase() + (bipolar ? "  BIPOLAR" : "  UNIPOLAR"),
                    r.reduced (10.0f, 4.0f), juce::Justification::topLeft);
        g.setColour (colours::textDim);
        g.drawText ("OUT " + juce::String (live, 3), r.reduced (10.0f, 4.0f), juce::Justification::topRight);
    }

private:
    void timerCallback() override
    {
        auto v = [this] (const char* id) { return rawParam (state, params::lfoId (lfo, id)); };
        const float rate = v (params::id::lfoSync) >= 0.5f ? dsp::syncDivisionHz (juce::roundToInt (v (params::id::lfoDiv)), (float) proc.getHostBpm())
                                                            : v (params::id::lfoRate);
        cursor += (double) rate / 30.0;
        if (cursor > 1.0e6) cursor = 0.0;
        repaint();
    }

    MysteryBoxProcessor& proc;
    juce::AudioProcessorValueTreeState& state;
    int lfo;
    double cursor = 0.0;
};

//==============================================================================
/** Triggered oscilloscope of the plugin output. */
class ScopeDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit ScopeDisplay (const ScopeTap& t, bool compactStyle = false) : tap (t), compact (compactStyle) { startTimerHz (GlobalSettings::get().fps (30)); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (compact)
        {
            g.setColour (colours::screen);
            g.fillRect (r);
            g.setColour (colours::cyan.withAlpha (0.35f));
            g.drawRect (r, 1.0f);
        }
        else
            drawScreen (g, r, colours::cyan);

        const int show = compact ? 300 : 600;
        int start = 0;
        for (int i = 1; i < kLen - show; ++i)
            if (samples[(size_t) i - 1] <= 0.0f && samples[(size_t) i] > 0.0f) { start = i; break; }

        juce::Path p;
        auto area = r.reduced (compact ? 2.0f : 6.0f, compact ? 3.0f : 10.0f);
        for (int i = 0; i < show; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (show - 1);
            const float y = area.getCentreY() - juce::jlimit (-1.0f, 1.0f, samples[(size_t) (start + i)] * (compact ? 1.6f : 1.0f)) * area.getHeight() * 0.5f;
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        strokeGlow (g, p, colours::lime, compact ? 1.0f : 1.5f, compact ? 0.6f : 1.0f);
        if (! compact)
        {
            g.setFont (fonts::mono (10.0f));
            g.setColour (colours::textDim);
            g.drawText ("SCOPE", r.reduced (10.0f, 4.0f), juce::Justification::topLeft);
        }
    }

private:
    static constexpr int kLen = 2048;
    void timerCallback() override
    {
        tap.copyLatest (samples.data(), kLen);
        repaint();
    }
    const ScopeTap& tap;
    bool compact;
    std::array<float, kLen> samples {};
};

} // namespace mbx::ui
