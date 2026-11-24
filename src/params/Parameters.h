// Single source of truth for every automatable parameter.
//
// State-compatibility contract (saved projects depend on these):
//  * Parameter ID strings never change once released.
//  * Choice lists are append-only.
//  * New parameters get the version hint of the release they ship in.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../dsp/SynthEngine.h"

namespace mbx::params
{
inline constexpr int kVersionPhase1 = 1;
inline constexpr int kVersionPhase2 = 2;
inline constexpr int kNumOscs       = dsp::kNumOscillators;

inline juce::String oscId (int oscIndex, const char* name)    { return "osc" + juce::String (oscIndex + 1) + "_" + name; }
inline juce::String envId (int envIndex, const char* name)    { return "env" + juce::String (envIndex + 1) + "_" + name; }
inline juce::String lfoId (int lfoIndex, const char* name)    { return "lfo" + juce::String (lfoIndex + 1) + "_" + name; }
inline juce::String modId (int slot, const char* name)        { return "mod" + juce::String (slot + 1) + "_" + name; }
inline juce::String filterId (int f, const char* name)        { return "filter" + juce::String (f + 1) + "_" + name; }
inline juce::String macroId (int m)                           { return "macro" + juce::String (m + 1); }

namespace id
{
    // per-oscillator suffixes (Phase 1)
    inline constexpr const char* oscOn = "on", *oscType = "type", *oscShape = "shape", *oscTable = "table", *oscNoise = "noise",
                                 *oscOctave = "octave", *oscCoarse = "coarse", *oscFine = "fine", *oscLevel = "level",
                                 *oscPan = "pan", *oscPhase = "phase", *oscPhaseRand = "phase_rand", *oscUnison = "unison",
                                 *oscDetune = "detune", *oscBlend = "blend", *oscSpread = "spread", *oscWtPos = "wt_pos",
                                 *oscPw = "pw";
    // per-oscillator (Phase 2)
    inline constexpr const char* oscRoute = "route", *oscWarp = "warp", *oscWarpAmt = "warp_amt", *oscModSrc = "mod_src",
                                 *oscScanOn = "scan_on", *oscScanRate = "scan_rate", *oscScanSync = "scan_sync",
                                 *oscScanDiv = "scan_div", *oscScanStart = "scan_start", *oscScanMid = "scan_mid",
                                 *oscScanEnd = "scan_end", *oscScanLoop = "scan_loop", *oscGrainSize = "grain_size",
                                 *oscGrainDensity = "grain_density", *oscGrainSpray = "grain_spray", *oscGrainSpread = "grain_spread";

    // envelopes: envN_<suffix>; ENV 1 is the amp envelope
    inline constexpr const char* envDelay = "delay", *envAttack = "attack", *envHold = "hold", *envDecay = "decay",
                                 *envSustain = "sustain", *envRelease = "release", *envACurve = "a_curve",
                                 *envDCurve = "d_curve", *envRCurve = "r_curve", *envVelocity = "velocity",
                                 *envLoop = "loop";
    inline constexpr const char* env1Punch = "env1_punch";
    // Phase 1 full IDs kept for readability at call sites
    inline constexpr const char* env1Delay = "env1_delay", *env1Attack = "env1_attack", *env1Hold = "env1_hold",
                                 *env1Decay = "env1_decay", *env1Sustain = "env1_sustain", *env1Release = "env1_release",
                                 *env1ACurve = "env1_a_curve", *env1DCurve = "env1_d_curve", *env1RCurve = "env1_r_curve",
                                 *env1Velocity = "env1_velocity";

    inline constexpr const char* msegTime = "mseg_time", *msegSync = "mseg_sync", *msegDiv = "mseg_div", *msegLoop = "mseg_loop";

    inline constexpr const char* lfoShape = "shape", *lfoRate = "rate", *lfoSync = "sync", *lfoDiv = "div", *lfoMode = "mode",
                                 *lfoPhase = "phase", *lfoFade = "fade", *lfoSmooth = "smooth", *lfoBipolar = "bipolar";

    inline constexpr const char* modSrc = "src", *modVia = "via", *modAmt = "amt", *modCurve = "curve", *modDest = "dest",
                                 *modBypass = "bypass";

    inline constexpr const char* filterOn = "on", *filterType = "type", *filterCutoff = "cutoff", *filterRes = "res",
                                 *filterDrive = "drive", *filterEnv = "env", *filterKey = "key", *filterVel = "vel",
                                 *filterFm = "fm", *filterMix = "mix";
    inline constexpr const char* filterRouting = "filter_routing";

    inline constexpr const char* subOn = "sub_on", *subShape = "sub_shape", *subOctave = "sub_octave", *subLevel = "sub_level", *subRoute = "sub_route";
    inline constexpr const char* noiseOn = "noise_on", *noiseType = "noise_type", *noiseLevel = "noise_level", *noiseRoute = "noise_route";
    inline constexpr const char* ringOn = "ring_on", *ringA = "ring_a", *ringB = "ring_b", *ringLevel = "ring_level", *ringRoute = "ring_route";

    inline constexpr const char* voiceMode = "voice_mode", *glideTime = "glide_time", *glideMode = "glide_mode",
                                 *glideCurve = "glide_curve", *stackVoices = "stack_voices", *stackDetune = "stack_detune",
                                 *mpeOn = "mpe_on", *mpeBendRange = "mpe_bend_range";

    // global (Phase 1)
    inline constexpr const char* masterVolume = "master_volume", *polyphony = "polyphony", *bendRange = "bend_range",
                                 *oversampling = "oversampling";
} // namespace id

const juce::StringArray& oscTypeNames();
const juce::StringArray& oscShapeNames();
const juce::StringArray& noiseTypeNames();
const juce::StringArray& octaveNames();
const juce::StringArray& tableNames();
const juce::StringArray& oversamplingNames();
const juce::StringArray& routeNames();
const juce::StringArray& warpNames();
const juce::StringArray& oscSourceNames();   // "Off", "Osc 1".."Osc 6"
const juce::StringArray& syncDivisionNames();
const juce::StringArray& scanLoopNames();
const juce::StringArray& lfoShapeNames();
const juce::StringArray& lfoModeNames();
const juce::StringArray& modSourceNames();
const juce::StringArray& modDestNames();
const juce::StringArray& filterTypeNames();
const juce::StringArray& filterRoutingNames();
const juce::StringArray& subShapeNames();
const juce::StringArray& voiceModeNames();
const juce::StringArray& glideModeNames();

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
    using P = std::atomic<float>*;
    struct Osc
    {
        P on {}, type {}, shape {}, table {}, noise {}, octave {}, coarse {}, fine {}, level {}, pan {}, phase {}, phaseRand {},
          unison {}, detune {}, blend {}, spread {}, wtPos {}, pw {}, route {}, warp {}, warpAmt {}, modSrc {}, scanOn {},
          scanRate {}, scanSync {}, scanDiv {}, scanStart {}, scanMid {}, scanEnd {}, scanLoop {}, grainSize {},
          grainDensity {}, grainSpray {}, grainSpread {};
    };
    struct Env { P delay {}, attack {}, hold {}, decay {}, sustain {}, release {}, aCurve {}, dCurve {}, rCurve {}, velocity {}, loop {}; };
    struct Lfo { P shape {}, rate {}, sync {}, div {}, mode {}, phase {}, fade {}, smooth {}, bipolar {}; };
    struct Slot { P src {}, via {}, amt {}, curve {}, dest {}, bypass {}; };
    struct Filt { P on {}, type {}, cutoff {}, res {}, drive {}, env {}, key {}, vel {}, fm {}, mix {}; };

    std::array<Osc, kNumOscs> osc;
    std::array<Env, dsp::kNumEnvelopes> env;
    std::array<Lfo, dsp::kNumLfos> lfo;
    std::array<Slot, dsp::kNumMatrixSlots> slot;
    std::array<Filt, dsp::kNumFilters> filt;
    std::array<P, dsp::kNumMacros> macro {};
    P punch {}, msegTime {}, msegSync {}, msegDiv {}, msegLoop {}, routing {};
    P subOn {}, subShape {}, subOct {}, subLevel {}, subRoute {}, noiseOn {}, noiseType {}, noiseLevel {}, noiseRoute {};
    P ringOn {}, ringA {}, ringB {}, ringLevel {}, ringRoute {};
    P voiceMode {}, glideTime {}, glideMode {}, glideCurve {}, stackVoices {}, stackDetune {}, mpeOn {}, mpeBend {};
    P master {}, poly {}, bend {}, os {};
};

} // namespace mbx::params
