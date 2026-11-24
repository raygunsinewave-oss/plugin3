// Single source of truth for every automatable parameter.
//
// Rules (state-compatibility contract — saved projects depend on these):
//  * Parameter ID strings never change once released.
//  * Choice lists are append-only.
//  * New parameters get a higher ParameterID version hint than the release they ship in.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../dsp/SynthEngine.h"

namespace mbx::params
{
inline constexpr int kVersionHint = 1;
inline constexpr int kNumOscs     = dsp::kNumOscillators;

/** "osc3_level" etc. index is 0-based. */
inline juce::String oscId (int oscIndex, const char* name) { return "osc" + juce::String (oscIndex + 1) + "_" + name; }

namespace id
{
    // per-oscillator suffixes
    inline constexpr const char* oscOn        = "on";
    inline constexpr const char* oscType      = "type";
    inline constexpr const char* oscShape     = "shape";
    inline constexpr const char* oscTable     = "table";
    inline constexpr const char* oscNoise     = "noise";
    inline constexpr const char* oscOctave    = "octave";
    inline constexpr const char* oscCoarse    = "coarse";
    inline constexpr const char* oscFine      = "fine";
    inline constexpr const char* oscLevel     = "level";
    inline constexpr const char* oscPan       = "pan";
    inline constexpr const char* oscPhase     = "phase";
    inline constexpr const char* oscPhaseRand = "phase_rand";
    inline constexpr const char* oscUnison    = "unison";
    inline constexpr const char* oscDetune    = "detune";
    inline constexpr const char* oscBlend     = "blend";
    inline constexpr const char* oscSpread    = "spread";
    inline constexpr const char* oscWtPos     = "wt_pos";
    inline constexpr const char* oscPw        = "pw";

    // amp envelope (ENV 1)
    inline constexpr const char* env1Delay    = "env1_delay";
    inline constexpr const char* env1Attack   = "env1_attack";
    inline constexpr const char* env1Hold     = "env1_hold";
    inline constexpr const char* env1Decay    = "env1_decay";
    inline constexpr const char* env1Sustain  = "env1_sustain";
    inline constexpr const char* env1Release  = "env1_release";
    inline constexpr const char* env1ACurve   = "env1_a_curve";
    inline constexpr const char* env1DCurve   = "env1_d_curve";
    inline constexpr const char* env1RCurve   = "env1_r_curve";
    inline constexpr const char* env1Velocity = "env1_velocity";
    inline constexpr const char* env1Punch    = "env1_punch";

    // global
    inline constexpr const char* masterVolume = "master_volume";
    inline constexpr const char* polyphony    = "polyphony";
    inline constexpr const char* bendRange    = "bend_range";
    inline constexpr const char* oversampling = "oversampling";
} // namespace id

const juce::StringArray& oscTypeNames();
const juce::StringArray& oscShapeNames();
const juce::StringArray& noiseTypeNames();
const juce::StringArray& octaveNames();
const juce::StringArray& tableNames();
const juce::StringArray& oversamplingNames();

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Caches raw atomic pointers once so the audio thread never does string lookups. */
class ParameterCache
{
public:
    explicit ParameterCache (juce::AudioProcessorValueTreeState& apvts);

    /** Audio-thread safe: lock-free atomic loads only. */
    void read (dsp::EngineSettings& out) const noexcept;
    int oversamplingIndex() const noexcept;

private:
    struct Osc
    {
        std::atomic<float>* on {}, *type {}, *shape {}, *table {}, *noise {}, *octave {}, *coarse {}, *fine {},
                          *level {}, *pan {}, *phase {}, *phaseRand {}, *unison {}, *detune {}, *blend {},
                          *spread {}, *wtPos {}, *pw {};
    };
    std::array<Osc, kNumOscs> osc;
    std::atomic<float>* delay {}, *attack {}, *hold {}, *decay {}, *sustain {}, *release {}, *aCurve {}, *dCurve {},
                      *rCurve {}, *velocity {}, *punch {}, *master {}, *poly {}, *bend {}, *os {};
};

} // namespace mbx::params
