#include "Parameters.h"

namespace mbx::params
{
using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
    template <typename Container>
    juce::StringArray toStringArray (const Container& c)
    {
        juce::StringArray s;
        for (const auto& item : c)
            s.add (juce::String (item));
        return s;
    }
} // namespace

const juce::StringArray& oscTypeNames()      { static const juce::StringArray n { "Wavetable", "Analog", "FM", "Granular", "Supersaw", "Noise" }; return n; }
const juce::StringArray& oscShapeNames()     { static const juce::StringArray n { "Sine", "Triangle", "Saw", "Square", "Pulse" }; return n; }
const juce::StringArray& noiseTypeNames()    { static const juce::StringArray n { "White", "Pink", "Brown", "Digital", "Vinyl" }; return n; }
const juce::StringArray& octaveNames()       { static const juce::StringArray n { "32'", "16'", "8'", "4'", "2'" }; return n; }
const juce::StringArray& oversamplingNames() { static const juce::StringArray n { "1x", "2x", "4x", "8x" }; return n; }
const juce::StringArray& routeNames()        { static const juce::StringArray n { "Filter 1", "Filter 2", "Both", "Bypass" }; return n; }
const juce::StringArray& warpNames()         { static const juce::StringArray n = toStringArray (dsp::kWarpModeNames); return n; }
const juce::StringArray& oscSourceNames()    { static const juce::StringArray n { "Off", "Osc 1", "Osc 2", "Osc 3", "Osc 4", "Osc 5", "Osc 6" }; return n; }
const juce::StringArray& syncDivisionNames() { static const juce::StringArray n = toStringArray (dsp::kSyncDivisionNames); return n; }
const juce::StringArray& scanLoopNames()     { static const juce::StringArray n { "One Shot", "Loop", "Ping-Pong" }; return n; }
const juce::StringArray& lfoShapeNames()     { static const juce::StringArray n { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Rnd", "Custom" }; return n; }
const juce::StringArray& lfoModeNames()      { static const juce::StringArray n { "Retrigger", "Free", "Envelope" }; return n; }
const juce::StringArray& modSourceNames()    { static const juce::StringArray n = toStringArray (dsp::kModSourceNames); return n; }
const juce::StringArray& filterTypeNames()   { static const juce::StringArray n = toStringArray (dsp::kFilterTypeNames); return n; }
const juce::StringArray& filterRoutingNames(){ static const juce::StringArray n { "Serial", "Parallel", "Split L/R" }; return n; }
const juce::StringArray& subShapeNames()     { static const juce::StringArray n { "Sine", "Triangle", "Square", "Saw" }; return n; }
const juce::StringArray& voiceModeNames()    { static const juce::StringArray n { "Poly", "Mono", "Legato", "Unison" }; return n; }
const juce::StringArray& glideModeNames()    { static const juce::StringArray n { "Always", "Legato Only" }; return n; }

const juce::StringArray& tableNames()
{
    static const juce::StringArray names = [] {
        juce::StringArray s;
        for (auto n : dsp::kFactoryTableNames)
            s.add (juce::String (n.data(), n.size()));
        s.add ("User");
        return s;
    }();
    return names;
}

const juce::StringArray& modDestNames()
{
    static const juce::StringArray names = [] {
        juce::StringArray s;
        s.add ("None");
        const char* oscDests[] = { "Pitch", "Level", "Pan", "WT Pos", "Pulse Width", "Detune", "Blend", "Spread", "Warp" };
        for (int o = 0; o < kNumOscs; ++o)
            for (auto* d : oscDests)
                s.add ("Osc " + juce::String (o + 1) + " " + d);
        const char* filtDests[] = { "Cutoff", "Resonance", "Drive", "Mix", "FM" };
        for (int f = 0; f < dsp::kNumFilters; ++f)
            for (auto* d : filtDests)
                s.add ("Filter " + juce::String (f + 1) + " " + d);
        s.addArray ({ "Sub Level", "Noise Level", "Ring Level", "Amp Level", "Amp Pan" });
        for (int l = 0; l < dsp::kNumLfos; ++l)
            s.add ("LFO " + juce::String (l + 1) + " Rate");
        s.add ("Global Pitch");
        jassert (s.size() == dsp::dest::Count);
        return s;
    }();
    return names;
}

namespace
{
    juce::ParameterID pid (const juce::String& s, int version) { return { s, version }; }

    juce::NormalisableRange<float> timeRange (float maxSeconds, float centre = 0.5f)
    {
        juce::NormalisableRange<float> r (0.0f, maxSeconds, 0.0001f);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::String secondsText (float v)
    {
        return v < 1.0f ? juce::String (v * 1000.0f, v < 0.1f ? 1 : 0) + " ms" : juce::String (v, 2) + " s";
    }

    auto secondsAttr()  { return juce::AudioParameterFloatAttributes().withLabel ("s").withStringFromValueFunction ([] (float v, int) { return secondsText (v); }); }
    auto percentAttr()  { return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }); }
    auto bipolarPercentAttr()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            const int p = juce::roundToInt (v * 100.0f);
            return (p > 0 ? "+" : "") + juce::String (p) + "%";
        });
    }
    auto panAttr()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            const int p = juce::roundToInt (v * 100.0f);
            return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
        });
    }
    auto curveAttr()    { return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); }); }
    auto hzAttr()
    {
        return juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, v < 10.0f ? 2 : 0) + " Hz";
        });
    }

    /** Small helper so the layout reads like a table. */
    struct Builder
    {
        juce::AudioProcessorParameterGroup& g;
        int version;

        void boolean (const juce::String& id, const juce::String& name, bool def)
        {
            g.addChild (std::make_unique<juce::AudioParameterBool> (pid (id, version), name, def));
        }
        void choice (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
        {
            g.addChild (std::make_unique<juce::AudioParameterChoice> (pid (id, version), name, items, def));
        }
        void integer (const juce::String& id, const juce::String& name, int lo, int hi, int def, const juce::String& label = {})
        {
            g.addChild (std::make_unique<juce::AudioParameterInt> (pid (id, version), name, lo, hi, def,
                                                                   juce::AudioParameterIntAttributes().withLabel (label)));
        }
        void real (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range, float def,
                   juce::AudioParameterFloatAttributes attr = {})
        {
            g.addChild (std::make_unique<juce::AudioParameterFloat> (pid (id, version), name, range, def, attr));
        }
        void unit (const juce::String& id, const juce::String& name, float def) { real (id, name, { 0.0f, 1.0f }, def, percentAttr()); }
        void bipolar (const juce::String& id, const juce::String& name, float def) { real (id, name, { -1.0f, 1.0f }, def, bipolarPercentAttr()); }
    };
} // namespace

APVTS::ParameterLayout createLayout()
{
    APVTS::ParameterLayout layout;
    constexpr int v1 = kVersionPhase1, v2 = kVersionPhase2;

    // ---------------------------------------------------------------- oscillators
    for (int i = 0; i < kNumOscs; ++i)
    {
        const auto name = "Osc " + juce::String (i + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("osc" + juce::String (i + 1), "OSC " + juce::String (i + 1), "|");
        Builder a { *group, v1 }, b { *group, v2 };

        a.boolean (oscId (i, id::oscOn), name + "On", i == 0);
        a.choice (oscId (i, id::oscType), name + "Type", oscTypeNames(), 1);
        a.choice (oscId (i, id::oscShape), name + "Shape", oscShapeNames(), 2);
        a.choice (oscId (i, id::oscTable), name + "Table", tableNames(), 0);
        a.choice (oscId (i, id::oscNoise), name + "Noise", noiseTypeNames(), 0);
        a.choice (oscId (i, id::oscOctave), name + "Octave", octaveNames(), 2);
        a.integer (oscId (i, id::oscCoarse), name + "Coarse", -24, 24, 0, "st");
        a.real (oscId (i, id::oscFine), name + "Fine", { -100.0f, 100.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("ct"));
        a.unit (oscId (i, id::oscLevel), name + "Level", 0.75f);
        a.real (oscId (i, id::oscPan), name + "Pan", { -1.0f, 1.0f }, 0.0f, panAttr());
        a.real (oscId (i, id::oscPhase), name + "Phase", { 0.0f, 360.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("deg"));
        a.unit (oscId (i, id::oscPhaseRand), name + "Phase Rand", 1.0f);
        a.integer (oscId (i, id::oscUnison), name + "Unison", 1, 16, 1);
        juce::NormalisableRange<float> detune (0.0f, 100.0f, 0.01f);
        detune.setSkewForCentre (20.0f);
        a.real (oscId (i, id::oscDetune), name + "Detune", detune, 15.0f, juce::AudioParameterFloatAttributes().withLabel ("ct"));
        a.unit (oscId (i, id::oscBlend), name + "Blend", 0.75f);
        a.unit (oscId (i, id::oscSpread), name + "Spread", 0.6f);
        a.unit (oscId (i, id::oscWtPos), name + "WT Position", 0.0f);
        a.real (oscId (i, id::oscPw), name + "Pulse Width", { 0.02f, 0.98f }, 0.5f, percentAttr());

        b.choice (oscId (i, id::oscRoute), name + "Route", routeNames(), 0);
        b.choice (oscId (i, id::oscWarp), name + "Warp", warpNames(), 0);
        b.unit (oscId (i, id::oscWarpAmt), name + "Warp Amount", 0.0f);
        b.choice (oscId (i, id::oscModSrc), name + "Mod Source", oscSourceNames(), 0);
        b.boolean (oscId (i, id::oscScanOn), name + "Scan On", false);
        juce::NormalisableRange<float> scanRate (0.01f, 20.0f, 0.001f);
        scanRate.setSkewForCentre (1.0f);
        b.real (oscId (i, id::oscScanRate), name + "Scan Rate", scanRate, 0.5f, hzAttr());
        b.boolean (oscId (i, id::oscScanSync), name + "Scan Sync", false);
        b.choice (oscId (i, id::oscScanDiv), name + "Scan Division", syncDivisionNames(), 16);
        b.unit (oscId (i, id::oscScanStart), name + "Scan Start", 0.0f);
        b.unit (oscId (i, id::oscScanMid), name + "Scan Mid", 0.5f);
        b.unit (oscId (i, id::oscScanEnd), name + "Scan End", 1.0f);
        b.choice (oscId (i, id::oscScanLoop), name + "Scan Loop", scanLoopNames(), 1);
        b.real (oscId (i, id::oscGrainSize), name + "Grain Size", timeRange (1.0f, 0.1f), 0.08f, secondsAttr());
        juce::NormalisableRange<float> density (1.0f, 120.0f, 0.01f);
        density.setSkewForCentre (20.0f);
        b.real (oscId (i, id::oscGrainDensity), name + "Grain Density", density, 30.0f, juce::AudioParameterFloatAttributes().withLabel ("/s"));
        b.unit (oscId (i, id::oscGrainSpray), name + "Grain Spray", 0.1f);
        b.unit (oscId (i, id::oscGrainSpread), name + "Grain Spread", 0.5f);
        layout.add (std::move (group));
    }

    // ---------------------------------------------------------------- mixer strip
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("mixer", "MIXER", "|");
        Builder b { *group, v2 };
        b.boolean (id::subOn, "Sub On", false);
        b.choice (id::subShape, "Sub Shape", subShapeNames(), 0);
        b.choice (id::subOctave, "Sub Octave", { "-1 Oct", "-2 Oct" }, 0);
        b.unit (id::subLevel, "Sub Level", 0.5f);
        b.choice (id::subRoute, "Sub Route", routeNames(), 0);
        b.boolean (id::noiseOn, "Noise On", false);
        b.choice (id::noiseType, "Noise Type", noiseTypeNames(), 1);
        b.unit (id::noiseLevel, "Noise Level", 0.2f);
        b.choice (id::noiseRoute, "Noise Route", routeNames(), 0);
        b.boolean (id::ringOn, "Ring On", false);
        b.choice (id::ringA, "Ring Source A", { "Osc 1", "Osc 2", "Osc 3", "Osc 4", "Osc 5", "Osc 6" }, 0);
        b.choice (id::ringB, "Ring Source B", { "Osc 1", "Osc 2", "Osc 3", "Osc 4", "Osc 5", "Osc 6" }, 1);
        b.unit (id::ringLevel, "Ring Level", 0.5f);
        b.choice (id::ringRoute, "Ring Route", routeNames(), 0);
        layout.add (std::move (group));
    }

    // ---------------------------------------------------------------- filters
    for (int f = 0; f < dsp::kNumFilters; ++f)
    {
        const auto name = "Filter " + juce::String (f + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("filter" + juce::String (f + 1), "FILTER " + juce::String (f + 1), "|");
        Builder b { *group, v2 };
        b.boolean (filterId (f, id::filterOn), name + "On", f == 0);
        b.choice (filterId (f, id::filterType), name + "Type", filterTypeNames(), f == 0 ? 1 : 2);
        juce::NormalisableRange<float> cutoff (20.0f, 20000.0f, 0.01f);
        cutoff.setSkewForCentre (632.0f); // geometric centre: log-feel knob
        b.real (filterId (f, id::filterCutoff), name + "Cutoff", cutoff, f == 0 ? 8000.0f : 200.0f, hzAttr());
        b.unit (filterId (f, id::filterRes), name + "Resonance", 0.15f);
        b.unit (filterId (f, id::filterDrive), name + "Drive", 0.0f);
        b.bipolar (filterId (f, id::filterEnv), name + "Env Amount", 0.0f);
        b.unit (filterId (f, id::filterKey), name + "Keytrack", 0.0f);
        b.unit (filterId (f, id::filterVel), name + "Velocity", 0.0f);
        b.unit (filterId (f, id::filterFm), name + "FM", 0.0f);
        b.unit (filterId (f, id::filterMix), name + "Mix", 1.0f);
        layout.add (std::move (group));
    }
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("filterrouting", "FILTER ROUTING", "|");
        Builder b { *group, v2 };
        b.choice (id::filterRouting, "Filter Routing", filterRoutingNames(), 0);
        layout.add (std::move (group));
    }

    // ---------------------------------------------------------------- envelopes
    for (int e = 0; e < dsp::kNumEnvelopes; ++e)
    {
        const auto name = e == 0 ? juce::String ("Amp ") : "Env " + juce::String (e + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("env" + juce::String (e + 1),
                                                                           e == 0 ? juce::String ("ENV 1 (AMP)") : "ENV " + juce::String (e + 1), "|");
        Builder b { *group, e == 0 ? v1 : v2 };
        b.real (envId (e, id::envDelay), name + "Delay", timeRange (10.0f), 0.0f, secondsAttr());
        b.real (envId (e, id::envAttack), name + "Attack", timeRange (20.0f), e == 0 ? 0.005f : 0.01f, secondsAttr());
        b.real (envId (e, id::envHold), name + "Hold", timeRange (10.0f), 0.0f, secondsAttr());
        b.real (envId (e, id::envDecay), name + "Decay", timeRange (20.0f), e == 0 ? 0.3f : 0.5f, secondsAttr());
        b.unit (envId (e, id::envSustain), name + "Sustain", e == 0 ? 0.8f : 0.0f);
        b.real (envId (e, id::envRelease), name + "Release", timeRange (20.0f), e == 0 ? 0.25f : 0.4f, secondsAttr());
        b.real (envId (e, id::envACurve), name + "Attack Curve", { -1.0f, 1.0f }, 0.0f, curveAttr());
        b.real (envId (e, id::envDCurve), name + "Decay Curve", { -1.0f, 1.0f }, 0.6f, curveAttr());
        b.real (envId (e, id::envRCurve), name + "Release Curve", { -1.0f, 1.0f }, 0.6f, curveAttr());
        b.unit (envId (e, id::envVelocity), name + "Velocity", e == 0 ? 0.5f : 0.0f);
        if (e == 0)
            b.unit (id::env1Punch, "Amp Punch", 0.0f);
        Builder loopB { *group, v2 };
        loopB.boolean (envId (e, id::envLoop), name + "Loop", false);
        layout.add (std::move (group));
    }

    // ---------------------------------------------------------------- MSEG
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("mseg", "MSEG", "|");
        Builder b { *group, v2 };
        b.real (id::msegTime, "MSEG Time", timeRange (30.0f, 1.0f), 1.0f, secondsAttr());
        b.boolean (id::msegSync, "MSEG Sync", false);
        b.choice (id::msegDiv, "MSEG Division", syncDivisionNames(), 16);
        b.boolean (id::msegLoop, "MSEG Loop", false);
        layout.add (std::move (group));
    }

    // ---------------------------------------------------------------- LFOs
    for (int l = 0; l < dsp::kNumLfos; ++l)
    {
        const auto name = "LFO " + juce::String (l + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("lfo" + juce::String (l + 1), "LFO " + juce::String (l + 1), "|");
        Builder b { *group, v2 };
        b.choice (lfoId (l, id::lfoShape), name + "Shape", lfoShapeNames(), 0);
        juce::NormalisableRange<float> rate (0.01f, 50.0f, 0.001f);
        rate.setSkewForCentre (2.0f);
        b.real (lfoId (l, id::lfoRate), name + "Rate", rate, 2.0f, hzAttr());
        b.boolean (lfoId (l, id::lfoSync), name + "Sync", false);
        b.choice (lfoId (l, id::lfoDiv), name + "Division", syncDivisionNames(), 11);
        b.choice (lfoId (l, id::lfoMode), name + "Mode", lfoModeNames(), 0);
        b.unit (lfoId (l, id::lfoPhase), name + "Phase", 0.0f);
        b.real (lfoId (l, id::lfoFade), name + "Fade In", timeRange (10.0f, 1.0f), 0.0f, secondsAttr());
        b.real (lfoId (l, id::lfoSmooth), name + "Smooth", timeRange (1.0f, 0.1f), 0.0f, secondsAttr());
        b.boolean (lfoId (l, id::lfoBipolar), name + "Bipolar", true);
        layout.add (std::move (group));
    }

    // ---------------------------------------------------------------- mod matrix
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("matrix", "MOD MATRIX", "|");
        Builder b { *group, v2 };
        for (int k = 0; k < dsp::kNumMatrixSlots; ++k)
        {
            const auto name = "Mod " + juce::String (k + 1) + " ";
            b.choice (modId (k, id::modSrc), name + "Source", modSourceNames(), 0);
            b.choice (modId (k, id::modVia), name + "Via", modSourceNames(), 0);
            b.bipolar (modId (k, id::modAmt), name + "Amount", 0.0f);
            b.real (modId (k, id::modCurve), name + "Curve", { -1.0f, 1.0f }, 0.0f, curveAttr());
            b.choice (modId (k, id::modDest), name + "Destination", modDestNames(), 0);
            b.boolean (modId (k, id::modBypass), name + "Bypass", false);
        }
        layout.add (std::move (group));
    }

    // ---------------------------------------------------------------- macros
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("macros", "MACROS", "|");
        Builder b { *group, v2 };
        for (int m = 0; m < dsp::kNumMacros; ++m)
            b.unit (macroId (m), "Macro " + juce::String (m + 1), 0.0f);
        layout.add (std::move (group));
    }

    // ---------------------------------------------------------------- voice
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("voice", "VOICE", "|");
        Builder b { *group, v2 };
        b.choice (id::voiceMode, "Voice Mode", voiceModeNames(), 0);
        b.real (id::glideTime, "Glide Time", timeRange (5.0f, 0.3f), 0.0f, secondsAttr());
        b.choice (id::glideMode, "Glide Mode", glideModeNames(), 0);
        b.unit (id::glideCurve, "Glide Curve", 1.0f);
        b.integer (id::stackVoices, "Unison Voices", 1, 8, 2);
        b.real (id::stackDetune, "Unison Detune", { 0.0f, 100.0f, 0.1f }, 20.0f, juce::AudioParameterFloatAttributes().withLabel ("ct"));
        b.boolean (id::mpeOn, "MPE", false);
        b.integer (id::mpeBendRange, "MPE Bend Range", 1, 96, 48, "st");
        layout.add (std::move (group));
    }

    // ---------------------------------------------------------------- global
    auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "GLOBAL", "|");
    {
        Builder b { *global, v1 };
        b.real (id::masterVolume, "Master Volume", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f, 2.0f), -6.0f,
                juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) {
                    return v <= -59.9f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB";
                }));
        b.integer (id::polyphony, "Polyphony", 1, dsp::SynthEngine::kMaxVoices, 16);
        b.integer (id::bendRange, "Pitch Bend Range", 0, 48, 2, "st");
        // Changing oversampling changes latency, so hosts should not automate it.
        global->addChild (std::make_unique<juce::AudioParameterChoice> (pid (id::oversampling, v1), "Oversampling", oversamplingNames(), 1,
                                                                        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    }
    layout.add (std::move (global));

    return layout;
}

//==============================================================================
ParameterCache::ParameterCache (APVTS& apvts)
{
    auto get = [&apvts] (const juce::String& s) {
        auto* p = apvts.getRawParameterValue (s);
        jassert (p != nullptr);
        return p;
    };

    for (int i = 0; i < kNumOscs; ++i)
    {
        auto& o = osc[(size_t) i];
        o.on = get (oscId (i, id::oscOn)); o.type = get (oscId (i, id::oscType)); o.shape = get (oscId (i, id::oscShape));
        o.table = get (oscId (i, id::oscTable)); o.noise = get (oscId (i, id::oscNoise)); o.octave = get (oscId (i, id::oscOctave));
        o.coarse = get (oscId (i, id::oscCoarse)); o.fine = get (oscId (i, id::oscFine)); o.level = get (oscId (i, id::oscLevel));
        o.pan = get (oscId (i, id::oscPan)); o.phase = get (oscId (i, id::oscPhase)); o.phaseRand = get (oscId (i, id::oscPhaseRand));
        o.unison = get (oscId (i, id::oscUnison)); o.detune = get (oscId (i, id::oscDetune)); o.blend = get (oscId (i, id::oscBlend));
        o.spread = get (oscId (i, id::oscSpread)); o.wtPos = get (oscId (i, id::oscWtPos)); o.pw = get (oscId (i, id::oscPw));
        o.route = get (oscId (i, id::oscRoute)); o.warp = get (oscId (i, id::oscWarp)); o.warpAmt = get (oscId (i, id::oscWarpAmt));
        o.modSrc = get (oscId (i, id::oscModSrc)); o.scanOn = get (oscId (i, id::oscScanOn)); o.scanRate = get (oscId (i, id::oscScanRate));
        o.scanSync = get (oscId (i, id::oscScanSync)); o.scanDiv = get (oscId (i, id::oscScanDiv));
        o.scanStart = get (oscId (i, id::oscScanStart)); o.scanMid = get (oscId (i, id::oscScanMid)); o.scanEnd = get (oscId (i, id::oscScanEnd));
        o.scanLoop = get (oscId (i, id::oscScanLoop)); o.grainSize = get (oscId (i, id::oscGrainSize));
        o.grainDensity = get (oscId (i, id::oscGrainDensity)); o.grainSpray = get (oscId (i, id::oscGrainSpray));
        o.grainSpread = get (oscId (i, id::oscGrainSpread));
    }
    for (int e = 0; e < dsp::kNumEnvelopes; ++e)
    {
        auto& v = env[(size_t) e];
        v.delay = get (envId (e, id::envDelay)); v.attack = get (envId (e, id::envAttack)); v.hold = get (envId (e, id::envHold));
        v.decay = get (envId (e, id::envDecay)); v.sustain = get (envId (e, id::envSustain)); v.release = get (envId (e, id::envRelease));
        v.aCurve = get (envId (e, id::envACurve)); v.dCurve = get (envId (e, id::envDCurve)); v.rCurve = get (envId (e, id::envRCurve));
        v.velocity = get (envId (e, id::envVelocity)); v.loop = get (envId (e, id::envLoop));
    }
    for (int l = 0; l < dsp::kNumLfos; ++l)
    {
        auto& v = lfo[(size_t) l];
        v.shape = get (lfoId (l, id::lfoShape)); v.rate = get (lfoId (l, id::lfoRate)); v.sync = get (lfoId (l, id::lfoSync));
        v.div = get (lfoId (l, id::lfoDiv)); v.mode = get (lfoId (l, id::lfoMode)); v.phase = get (lfoId (l, id::lfoPhase));
        v.fade = get (lfoId (l, id::lfoFade)); v.smooth = get (lfoId (l, id::lfoSmooth)); v.bipolar = get (lfoId (l, id::lfoBipolar));
    }
    for (int k = 0; k < dsp::kNumMatrixSlots; ++k)
    {
        auto& v = slot[(size_t) k];
        v.src = get (modId (k, id::modSrc)); v.via = get (modId (k, id::modVia)); v.amt = get (modId (k, id::modAmt));
        v.curve = get (modId (k, id::modCurve)); v.dest = get (modId (k, id::modDest)); v.bypass = get (modId (k, id::modBypass));
    }
    for (int f = 0; f < dsp::kNumFilters; ++f)
    {
        auto& v = filt[(size_t) f];
        v.on = get (filterId (f, id::filterOn)); v.type = get (filterId (f, id::filterType)); v.cutoff = get (filterId (f, id::filterCutoff));
        v.res = get (filterId (f, id::filterRes)); v.drive = get (filterId (f, id::filterDrive)); v.env = get (filterId (f, id::filterEnv));
        v.key = get (filterId (f, id::filterKey)); v.vel = get (filterId (f, id::filterVel)); v.fm = get (filterId (f, id::filterFm));
        v.mix = get (filterId (f, id::filterMix));
    }
    for (int m = 0; m < dsp::kNumMacros; ++m)
        macro[(size_t) m] = get (macroId (m));

    punch = get (id::env1Punch);
    msegTime = get (id::msegTime); msegSync = get (id::msegSync); msegDiv = get (id::msegDiv); msegLoop = get (id::msegLoop);
    routing = get (id::filterRouting);
    subOn = get (id::subOn); subShape = get (id::subShape); subOct = get (id::subOctave); subLevel = get (id::subLevel); subRoute = get (id::subRoute);
    noiseOn = get (id::noiseOn); noiseType = get (id::noiseType); noiseLevel = get (id::noiseLevel); noiseRoute = get (id::noiseRoute);
    ringOn = get (id::ringOn); ringA = get (id::ringA); ringB = get (id::ringB); ringLevel = get (id::ringLevel); ringRoute = get (id::ringRoute);
    voiceMode = get (id::voiceMode); glideTime = get (id::glideTime); glideMode = get (id::glideMode); glideCurve = get (id::glideCurve);
    stackVoices = get (id::stackVoices); stackDetune = get (id::stackDetune); mpeOn = get (id::mpeOn); mpeBend = get (id::mpeBendRange);
    master = get (id::masterVolume); poly = get (id::polyphony); bend = get (id::bendRange); os = get (id::oversampling);
}

void ParameterCache::read (dsp::EngineSettings& out) const noexcept
{
    auto f = [] (const P p) { return p->load (std::memory_order_relaxed); };
    auto i = [] (const P p) { return juce::roundToInt (p->load (std::memory_order_relaxed)); };
    auto b = [] (const P p) { return p->load (std::memory_order_relaxed) >= 0.5f; };

    for (size_t k = 0; k < (size_t) kNumOscs; ++k)
    {
        const auto& o = osc[k];
        auto& s = out.osc[k];
        s.on = b (o.on);
        s.type = static_cast<dsp::OscType> (i (o.type));
        s.shape = static_cast<dsp::VaShape> (i (o.shape));
        s.table = i (o.table);
        s.noise = static_cast<dsp::NoiseType> (i (o.noise));
        s.pitchSemis = (float) ((i (o.octave) - 2) * 12 + i (o.coarse));
        s.fineCents = f (o.fine);
        s.level = f (o.level);
        s.pan = f (o.pan);
        s.phase = f (o.phase) / 360.0f;
        s.phaseRand = f (o.phaseRand);
        s.unison = i (o.unison);
        s.detuneCents = f (o.detune);
        s.blend = f (o.blend);
        s.spread = f (o.spread);
        s.wtPosition = f (o.wtPos);
        s.pulseWidth = f (o.pw);
        s.route = static_cast<dsp::OscRoute> (i (o.route));
        s.warp = static_cast<dsp::WarpMode> (i (o.warp));
        s.warpAmount = f (o.warpAmt);
        s.modSource = i (o.modSrc) - 1;
        s.scanOn = b (o.scanOn);
        s.scanRateHz = f (o.scanRate);
        s.scanSync = b (o.scanSync);
        s.scanDivision = i (o.scanDiv);
        s.scanStart = f (o.scanStart);
        s.scanMid = f (o.scanMid);
        s.scanEnd = f (o.scanEnd);
        s.scanLoop = static_cast<dsp::ScanLoop> (i (o.scanLoop));
        s.grain.sizeSec = std::max (0.005f, f (o.grainSize));
        s.grain.density = f (o.grainDensity);
        s.grain.spray = f (o.grainSpray);
        s.grain.spread = f (o.grainSpread);
    }

    for (size_t e = 0; e < (size_t) dsp::kNumEnvelopes; ++e)
    {
        const auto& v = env[e];
        auto& a = out.env[e];
        a.delay = f (v.delay); a.attack = f (v.attack); a.hold = f (v.hold); a.decay = f (v.decay);
        a.sustain = f (v.sustain); a.release = f (v.release);
        a.attackCurve = f (v.aCurve); a.decayCurve = f (v.dCurve); a.releaseCurve = f (v.rCurve);
        a.loop = b (v.loop);
        a.punch = e == 0 ? f (punch) : 0.0f;
        out.envVelocity[e] = f (v.velocity);
    }

    out.mseg.durationSec = f (msegTime);
    out.mseg.sync = b (msegSync);
    out.mseg.division = i (msegDiv);
    out.mseg.loop = b (msegLoop);

    for (size_t l = 0; l < (size_t) dsp::kNumLfos; ++l)
    {
        const auto& v = lfo[l];
        auto& s = out.lfo[l];
        s.shape = static_cast<dsp::LfoShape> (i (v.shape));
        s.rateHz = f (v.rate);
        s.sync = b (v.sync);
        s.division = i (v.div);
        s.mode = static_cast<dsp::LfoMode> (i (v.mode));
        s.phase = f (v.phase);
        s.fade = f (v.fade);
        s.smooth = f (v.smooth);
        s.bipolar = b (v.bipolar);
    }

    for (size_t k = 0; k < (size_t) dsp::kNumMatrixSlots; ++k)
    {
        const auto& v = slot[k];
        auto& s = out.matrix[k];
        s.source = i (v.src);
        s.via = i (v.via);
        s.amount = f (v.amt);
        s.curve = f (v.curve);
        s.destination = i (v.dest);
        s.bypass = b (v.bypass);
    }

    for (size_t m = 0; m < (size_t) dsp::kNumMacros; ++m)
        out.macros[m] = f (macro[m]);

    for (size_t k = 0; k < (size_t) dsp::kNumFilters; ++k)
    {
        const auto& v = filt[k];
        auto& s = out.filter[k];
        s.dsp.on = b (v.on);
        s.dsp.type = static_cast<dsp::FilterType> (i (v.type));
        s.cutoffHz = f (v.cutoff);
        s.dsp.resonance = f (v.res);
        s.dsp.drive = f (v.drive);
        s.envAmount = f (v.env);
        s.keytrack = f (v.key);
        s.velocity = f (v.vel);
        s.dsp.fmAmount = f (v.fm);
        s.dsp.mix = f (v.mix);
    }
    out.routing = static_cast<dsp::FilterRouting> (i (routing));

    out.sub = { b (subOn), static_cast<dsp::SubShape> (i (subShape)), i (subOct) + 1, f (subLevel), static_cast<dsp::OscRoute> (i (subRoute)) };
    out.noise = { b (noiseOn), static_cast<dsp::NoiseType> (i (noiseType)), f (noiseLevel), static_cast<dsp::OscRoute> (i (noiseRoute)) };
    out.ring = { b (ringOn), i (ringA), i (ringB), f (ringLevel), static_cast<dsp::OscRoute> (i (ringRoute)) };

    out.voiceMode = static_cast<dsp::VoiceMode> (i (voiceMode));
    out.glideTime = f (glideTime);
    out.glideMode = static_cast<dsp::GlideMode> (i (glideMode));
    out.glideCurve = f (glideCurve);
    out.stackVoices = i (stackVoices);
    out.stackDetune = f (stackDetune);
    out.mpe = b (mpeOn);
    out.mpeBendRange = (float) i (mpeBend);
    out.polyphony = i (poly);
    out.bendRange = (float) i (bend);
    out.masterGain = juce::Decibels::decibelsToGain (f (master), -59.9f);
}

int ParameterCache::oversamplingIndex() const noexcept
{
    return juce::jlimit (0, 3, juce::roundToInt (os->load (std::memory_order_relaxed)));
}

} // namespace mbx::params
