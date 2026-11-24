#include "Parameters.h"

namespace mbx::params
{
using APVTS = juce::AudioProcessorValueTreeState;

const juce::StringArray& oscTypeNames()
{
    static const juce::StringArray names { "Wavetable", "Analog", "FM", "Granular", "Supersaw", "Noise" };
    return names;
}
const juce::StringArray& oscShapeNames()
{
    static const juce::StringArray names { "Sine", "Triangle", "Saw", "Square", "Pulse" };
    return names;
}
const juce::StringArray& noiseTypeNames()
{
    static const juce::StringArray names { "White", "Pink", "Brown", "Digital", "Vinyl" };
    return names;
}
const juce::StringArray& octaveNames()
{
    static const juce::StringArray names { "32'", "16'", "8'", "4'", "2'" };
    return names;
}
const juce::StringArray& tableNames()
{
    static const juce::StringArray names = [] {
        juce::StringArray s;
        for (auto n : dsp::kFactoryTableNames)
            s.add (juce::String (n.data(), n.size()));
        return s;
    }();
    return names;
}
const juce::StringArray& oversamplingNames()
{
    static const juce::StringArray names { "1x", "2x", "4x", "8x" };
    return names;
}

namespace
{
    juce::ParameterID pid (const juce::String& s) { return { s, kVersionHint }; }

    juce::NormalisableRange<float> timeRange (float maxSeconds)
    {
        juce::NormalisableRange<float> r (0.0f, maxSeconds, 0.0001f);
        r.setSkewForCentre (0.5f);
        return r;
    }

    auto secondsAttr()
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel ("s")
            .withStringFromValueFunction ([] (float v, int) {
                return v < 1.0f ? juce::String (v * 1000.0f, v < 0.1f ? 1 : 0) + " ms" : juce::String (v, 2) + " s";
            });
    }

    auto percentAttr()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; });
    }

    auto bipolarAttr()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            const int p = juce::roundToInt (v * 100.0f);
            return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
        });
    }

    auto curveAttr()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String (v, 2); });
    }
} // namespace

APVTS::ParameterLayout createLayout()
{
    APVTS::ParameterLayout layout;

    for (int i = 0; i < kNumOscs; ++i)
    {
        const auto name = "Osc " + juce::String (i + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("osc" + juce::String (i + 1), "OSC " + juce::String (i + 1), "|");

        group->addChild (std::make_unique<juce::AudioParameterBool> (pid (oscId (i, id::oscOn)), name + "On", i == 0));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (pid (oscId (i, id::oscType)), name + "Type", oscTypeNames(), 1));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (pid (oscId (i, id::oscShape)), name + "Shape", oscShapeNames(), 2));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (pid (oscId (i, id::oscTable)), name + "Table", tableNames(), 0));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (pid (oscId (i, id::oscNoise)), name + "Noise", noiseTypeNames(), 0));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (pid (oscId (i, id::oscOctave)), name + "Octave", octaveNames(), 2));
        group->addChild (std::make_unique<juce::AudioParameterInt> (pid (oscId (i, id::oscCoarse)), name + "Coarse", -24, 24, 0,
                                                                    juce::AudioParameterIntAttributes().withLabel ("st")));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (pid (oscId (i, id::oscFine)), name + "Fine",
                                                                      juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                                                      juce::AudioParameterFloatAttributes().withLabel ("ct")));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (pid (oscId (i, id::oscLevel)), name + "Level",
                                                                      juce::NormalisableRange<float> (0.0f, 1.0f), 0.75f, percentAttr()));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (pid (oscId (i, id::oscPan)), name + "Pan",
                                                                      juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipolarAttr()));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (pid (oscId (i, id::oscPhase)), name + "Phase",
                                                                      juce::NormalisableRange<float> (0.0f, 360.0f, 0.1f), 0.0f,
                                                                      juce::AudioParameterFloatAttributes().withLabel ("deg")));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (pid (oscId (i, id::oscPhaseRand)), name + "Phase Rand",
                                                                      juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, percentAttr()));
        group->addChild (std::make_unique<juce::AudioParameterInt> (pid (oscId (i, id::oscUnison)), name + "Unison", 1, 16, 1));

        juce::NormalisableRange<float> detune (0.0f, 100.0f, 0.01f);
        detune.setSkewForCentre (20.0f);
        group->addChild (std::make_unique<juce::AudioParameterFloat> (pid (oscId (i, id::oscDetune)), name + "Detune", detune, 15.0f,
                                                                      juce::AudioParameterFloatAttributes().withLabel ("ct")));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (pid (oscId (i, id::oscBlend)), name + "Blend",
                                                                      juce::NormalisableRange<float> (0.0f, 1.0f), 0.75f, percentAttr()));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (pid (oscId (i, id::oscSpread)), name + "Spread",
                                                                      juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f, percentAttr()));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (pid (oscId (i, id::oscWtPos)), name + "WT Position",
                                                                      juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttr()));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (pid (oscId (i, id::oscPw)), name + "Pulse Width",
                                                                      juce::NormalisableRange<float> (0.02f, 0.98f), 0.5f, percentAttr()));
        layout.add (std::move (group));
    }

    auto env = std::make_unique<juce::AudioProcessorParameterGroup> ("env1", "ENV 1 (AMP)", "|");
    env->addChild (std::make_unique<juce::AudioParameterFloat> (pid (id::env1Delay), "Amp Delay", timeRange (10.0f), 0.0f, secondsAttr()));
    env->addChild (std::make_unique<juce::AudioParameterFloat> (pid (id::env1Attack), "Amp Attack", timeRange (20.0f), 0.005f, secondsAttr()));
    env->addChild (std::make_unique<juce::AudioParameterFloat> (pid (id::env1Hold), "Amp Hold", timeRange (10.0f), 0.0f, secondsAttr()));
    env->addChild (std::make_unique<juce::AudioParameterFloat> (pid (id::env1Decay), "Amp Decay", timeRange (20.0f), 0.3f, secondsAttr()));
    env->addChild (std::make_unique<juce::AudioParameterFloat> (pid (id::env1Sustain), "Amp Sustain",
                                                                juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f, percentAttr()));
    env->addChild (std::make_unique<juce::AudioParameterFloat> (pid (id::env1Release), "Amp Release", timeRange (20.0f), 0.25f, secondsAttr()));
    env->addChild (std::make_unique<juce::AudioParameterFloat> (pid (id::env1ACurve), "Amp Attack Curve",
                                                                juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, curveAttr()));
    env->addChild (std::make_unique<juce::AudioParameterFloat> (pid (id::env1DCurve), "Amp Decay Curve",
                                                                juce::NormalisableRange<float> (-1.0f, 1.0f), 0.6f, curveAttr()));
    env->addChild (std::make_unique<juce::AudioParameterFloat> (pid (id::env1RCurve), "Amp Release Curve",
                                                                juce::NormalisableRange<float> (-1.0f, 1.0f), 0.6f, curveAttr()));
    env->addChild (std::make_unique<juce::AudioParameterFloat> (pid (id::env1Velocity), "Amp Velocity",
                                                                juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, percentAttr()));
    env->addChild (std::make_unique<juce::AudioParameterFloat> (pid (id::env1Punch), "Amp Punch",
                                                                juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttr()));
    layout.add (std::move (env));

    auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "GLOBAL", "|");
    global->addChild (std::make_unique<juce::AudioParameterFloat> (
        pid (id::masterVolume), "Master Volume", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f, 2.0f), -6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) {
            return v <= -59.9f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB";
        })));
    global->addChild (std::make_unique<juce::AudioParameterInt> (pid (id::polyphony), "Polyphony", 1, dsp::SynthEngine::kMaxVoices, 16));
    global->addChild (std::make_unique<juce::AudioParameterInt> (pid (id::bendRange), "Pitch Bend Range", 0, 48, 2,
                                                                 juce::AudioParameterIntAttributes().withLabel ("st")));
    // Changing oversampling changes latency, so hosts should not automate it.
    global->addChild (std::make_unique<juce::AudioParameterChoice> (pid (id::oversampling), "Oversampling", oversamplingNames(), 1,
                                                                    juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::move (global));

    return layout;
}

ParameterCache::ParameterCache (APVTS& apvts)
{
    auto get = [&apvts] (const juce::String& s) {
        auto* p = apvts.getRawParameterValue (s);
        jassert (p != nullptr);
        return p;
    };

    for (int i = 0; i < kNumOscs; ++i)
    {
        auto& o     = osc[(size_t) i];
        o.on        = get (oscId (i, id::oscOn));
        o.type      = get (oscId (i, id::oscType));
        o.shape     = get (oscId (i, id::oscShape));
        o.table     = get (oscId (i, id::oscTable));
        o.noise     = get (oscId (i, id::oscNoise));
        o.octave    = get (oscId (i, id::oscOctave));
        o.coarse    = get (oscId (i, id::oscCoarse));
        o.fine      = get (oscId (i, id::oscFine));
        o.level     = get (oscId (i, id::oscLevel));
        o.pan       = get (oscId (i, id::oscPan));
        o.phase     = get (oscId (i, id::oscPhase));
        o.phaseRand = get (oscId (i, id::oscPhaseRand));
        o.unison    = get (oscId (i, id::oscUnison));
        o.detune    = get (oscId (i, id::oscDetune));
        o.blend     = get (oscId (i, id::oscBlend));
        o.spread    = get (oscId (i, id::oscSpread));
        o.wtPos     = get (oscId (i, id::oscWtPos));
        o.pw        = get (oscId (i, id::oscPw));
    }
    delay    = get (id::env1Delay);
    attack   = get (id::env1Attack);
    hold     = get (id::env1Hold);
    decay    = get (id::env1Decay);
    sustain  = get (id::env1Sustain);
    release  = get (id::env1Release);
    aCurve   = get (id::env1ACurve);
    dCurve   = get (id::env1DCurve);
    rCurve   = get (id::env1RCurve);
    velocity = get (id::env1Velocity);
    punch    = get (id::env1Punch);
    master   = get (id::masterVolume);
    poly     = get (id::polyphony);
    bend     = get (id::bendRange);
    os       = get (id::oversampling);
}

void ParameterCache::read (dsp::EngineSettings& out) const noexcept
{
    constexpr auto relaxed = std::memory_order_relaxed;
    auto idx = [] (const std::atomic<float>* p) { return juce::roundToInt (p->load (std::memory_order_relaxed)); };

    for (size_t i = 0; i < (size_t) kNumOscs; ++i)
    {
        const auto& o = osc[i];
        auto& s       = out.osc[i];
        s.on          = o.on->load (relaxed) >= 0.5f;
        s.type        = static_cast<dsp::OscType> (idx (o.type));
        s.shape       = static_cast<dsp::VaShape> (idx (o.shape));
        s.table       = idx (o.table);
        s.noise       = static_cast<dsp::NoiseType> (idx (o.noise));
        s.pitchSemis  = (float) ((idx (o.octave) - 2) * 12 + idx (o.coarse));
        s.fineCents   = o.fine->load (relaxed);
        s.level       = o.level->load (relaxed);
        s.pan         = o.pan->load (relaxed);
        s.phase       = o.phase->load (relaxed) / 360.0f;
        s.phaseRand   = o.phaseRand->load (relaxed);
        s.unison      = idx (o.unison);
        s.detuneCents = o.detune->load (relaxed);
        s.blend       = o.blend->load (relaxed);
        s.spread      = o.spread->load (relaxed);
        s.wtPosition  = o.wtPos->load (relaxed);
        s.pulseWidth  = o.pw->load (relaxed);
    }

    auto& a        = out.amp;
    a.delay        = delay->load (relaxed);
    a.attack       = attack->load (relaxed);
    a.hold         = hold->load (relaxed);
    a.decay        = decay->load (relaxed);
    a.sustain      = sustain->load (relaxed);
    a.release      = release->load (relaxed);
    a.attackCurve  = aCurve->load (relaxed);
    a.decayCurve   = dCurve->load (relaxed);
    a.releaseCurve = rCurve->load (relaxed);
    a.punch        = punch->load (relaxed);
    out.ampVelocity = velocity->load (relaxed);
    out.polyphony   = idx (poly);
    out.bendRange   = (float) idx (bend);
    out.masterGain  = juce::Decibels::decibelsToGain (master->load (relaxed), -59.9f);
}

int ParameterCache::oversamplingIndex() const noexcept
{
    return juce::jlimit (0, 3, juce::roundToInt (os->load (std::memory_order_relaxed)));
}

} // namespace mbx::params
