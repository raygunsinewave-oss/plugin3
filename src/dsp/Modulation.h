// Modulation building blocks: source/destination tables, drawable shapes (MSEG and
// custom LFO), and the LFO itself. Everything is JUCE-free and allocation-free.
#pragma once

#include "DspCommon.h"

namespace mbx::dsp
{
inline constexpr int kNumOscillators   = 6;
inline constexpr int kNumEnvelopes     = 4; // ENV 1 = amp
inline constexpr int kNumLfos          = 6;
inline constexpr int kNumMacros        = 8;
inline constexpr int kNumMatrixSlots   = 32;
inline constexpr int kNumFilters       = 2;

//==============================================================================
// Sources — APPEND ONLY (saved projects store the index).
enum class ModSource : int
{
    None = 0,
    Env1, Env2, Env3, Env4,
    Mseg,
    Lfo1, Lfo2, Lfo3, Lfo4, Lfo5, Lfo6,
    Velocity, Aftertouch, ModWheel, PitchBend, Keytrack, Random,
    Macro1, Macro2, Macro3, Macro4, Macro5, Macro6, Macro7, Macro8,
    MpePressure, MpeSlide, MpeGlide,
    SeqLane1, SeqLane2,
    Game1, Game2, Game3, Game4,
    Count
};

inline constexpr const char* kModSourceNames[] = {
    "None", "Env 1", "Env 2", "Env 3", "Env 4", "MSEG",
    "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6",
    "Velocity", "Aftertouch", "Mod Wheel", "Pitch Bend", "Keytrack", "Random",
    "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8",
    "MPE Pressure", "MPE Slide", "MPE Glide",
    "Seq Lane 1", "Seq Lane 2",
    "Game 1", "Game 2", "Game 3", "Game 4"
};
static_assert (std::size (kModSourceNames) == (size_t) ModSource::Count);

//==============================================================================
// Destinations — computed layout, APPEND ONLY at the end.
namespace dest
{
    enum OscDest : int { Pitch = 0, Level, Pan, WtPos, PulseWidth, Detune, Blend, Spread, WarpAmount, NumOscDests };
    enum FilterDest : int { Cutoff = 0, Resonance, Drive, Mix, FilterFM, NumFilterDests };

    inline constexpr int None        = 0;
    inline constexpr int OscBase     = 1;
    inline constexpr int FilterBase  = OscBase + kNumOscillators * NumOscDests;  // 55
    inline constexpr int SubLevel    = FilterBase + kNumFilters * NumFilterDests; // 65
    inline constexpr int NoiseLevel  = SubLevel + 1;
    inline constexpr int RingLevel   = SubLevel + 2;
    inline constexpr int AmpLevel    = SubLevel + 3;
    inline constexpr int AmpPan      = SubLevel + 4;
    inline constexpr int LfoRateBase = SubLevel + 5;                              // 70..75
    inline constexpr int GlobalPitch = LfoRateBase + kNumLfos;                    // 76
    // Phase 5: global FX destinations (driven by global sources such as macros / mod wheel)
    inline constexpr int FxMixBase   = GlobalPitch + 1;                           // 77..86 rack slot mix
    inline constexpr int SendALevel  = FxMixBase + 10;                            // 87
    inline constexpr int SendBLevel  = SendALevel + 1;                            // 88
    inline constexpr int Count       = SendBLevel + 1;
    inline constexpr bool isGlobalFx (int d) { return d >= FxMixBase && d < Count; }

    inline constexpr int osc (int oscIndex, OscDest d) { return OscBase + oscIndex * NumOscDests + d; }
    inline constexpr int filter (int f, FilterDest d)  { return FilterBase + f * NumFilterDests + d; }

    /** Natural-unit range covered by amount = 1 for each destination. */
    inline float scale (int d) noexcept
    {
        if (d >= OscBase && d < FilterBase)
        {
            switch ((d - OscBase) % NumOscDests)
            {
                case Pitch:      return 24.0f;  // semitones
                case PulseWidth: return 0.48f;
                case Detune:     return 50.0f;  // cents
                default:         return 1.0f;
            }
        }
        if (d >= FilterBase && d < SubLevel)
            return (d - FilterBase) % NumFilterDests == Cutoff ? 96.0f : 1.0f; // semitones
        if (d >= LfoRateBase && d < GlobalPitch)
            return 4.0f; // octaves
        if (d == GlobalPitch)
            return 24.0f;
        return 1.0f;
    }
} // namespace dest

/** One mod-matrix slot (block-rate snapshot). */
struct ModSlot
{
    int source = 0, via = 0, destination = 0;
    float amount = 0.0f; // -1..1
    float curve  = 0.0f; // -1..1, shapes the source response
    bool bypass  = false;

    bool isActive() const noexcept { return ! bypass && source != 0 && destination != 0 && ! exactlyEqual (amount, 0.0f); }
};

/** Source response curve: 0 = linear, >0 = exponential-ish, <0 = logarithmic-ish. Sign preserving. */
inline float applyModCurve (float x, float curve) noexcept
{
    if (exactlyEqual (curve, 0.0f))
        return x;
    const float a = std::abs (x);
    const float e = std::exp2 (curve * 2.0f);
    return std::copysign (std::pow (a, e), x);
}

//==============================================================================
/** Breakpoint shape used by the MSEG and custom LFO waves. x and y are 0..1. */
struct Shape
{
    static constexpr int kMaxPoints = 32;
    struct Point { float x = 0.0f, y = 0.0f, curve = 0.0f; }; // curve applies to the segment ending at this point

    int numPoints = 2;
    std::array<Point, kMaxPoints> points { Point { 0.0f, 0.0f, 0.0f }, Point { 1.0f, 1.0f, 0.0f } };

    float evaluate (float x) const noexcept
    {
        x = std::clamp (x, 0.0f, 1.0f);
        for (int i = 1; i < numPoints; ++i)
        {
            const auto& b = points[(size_t) i];
            if (x <= b.x || i == numPoints - 1)
            {
                const auto& a = points[(size_t) i - 1];
                const float w = b.x - a.x;
                const float t = w > 1.0e-6f ? std::clamp ((x - a.x) / w, 0.0f, 1.0f) : 1.0f;
                return a.y + (b.y - a.y) * curveShape (t, b.curve);
            }
        }
        return points[0].y;
    }

    /** Keeps points sorted, clamped, with fixed end points at x = 0 and x = 1. */
    void sanitise() noexcept
    {
        numPoints = std::clamp (numPoints, 2, kMaxPoints);
        std::sort (points.begin(), points.begin() + numPoints, [] (const Point& a, const Point& b) { return a.x < b.x; });
        for (int i = 0; i < numPoints; ++i)
        {
            auto& p = points[(size_t) i];
            p.x = std::clamp (p.x, 0.0f, 1.0f);
            p.y = std::clamp (p.y, 0.0f, 1.0f);
            p.curve = std::clamp (p.curve, -1.0f, 1.0f);
        }
        points[0].x = 0.0f;
        points[(size_t) numPoints - 1].x = 1.0f;
    }

    static Shape make (std::initializer_list<Point> pts)
    {
        Shape s;
        s.numPoints = 0;
        for (const auto& p : pts)
            if (s.numPoints < kMaxPoints)
                s.points[(size_t) s.numPoints++] = p;
        s.sanitise();
        return s;
    }

    static Shape defaultMseg()  { return make ({ { 0, 0, 0 }, { 0.08f, 1, -0.5f }, { 0.4f, 0.45f, 0.6f }, { 0.75f, 0.6f, 0 }, { 1, 0, 0.5f } }); }
    static Shape defaultCustom() { return make ({ { 0, 0.5f, 0 }, { 0.2f, 1, 0.5f }, { 0.45f, 0.15f, -0.4f }, { 0.7f, 0.8f, 0.3f }, { 1, 0.5f, 0 } }); }
};

//==============================================================================
enum class LfoShape : int { Sine = 0, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom, Custom };
enum class LfoMode : int { Retrigger = 0, Free, Envelope };

/** Tempo divisions shared by LFOs and wavetable scan. Values are in quarter-note beats. APPEND ONLY. */
inline constexpr const char* kSyncDivisionNames[] = {
    "1/64", "1/32T", "1/32", "1/32D", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
    "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1 bar", "2 bars", "4 bars", "8 bars"
};
inline constexpr float kSyncDivisionBeats[] = {
    0.0625f, 0.083333f, 0.125f, 0.1875f, 0.166667f, 0.25f, 0.375f, 0.333333f, 0.5f, 0.75f,
    0.666667f, 1.0f, 1.5f, 1.333333f, 2.0f, 3.0f, 4.0f, 8.0f, 16.0f, 32.0f
};
inline float syncDivisionHz (int division, float bpm) noexcept
{
    const int d = std::clamp (division, 0, (int) std::size (kSyncDivisionBeats) - 1);
    return (std::max (bpm, 1.0f) / 60.0f) / kSyncDivisionBeats[(size_t) d];
}

struct LfoSettings
{
    LfoShape shape = LfoShape::Sine;
    float rateHz   = 2.0f;
    bool sync      = false;
    int division   = 11; // 1/4
    LfoMode mode   = LfoMode::Retrigger;
    float phase    = 0.0f; // 0..1 start phase
    float fade     = 0.0f; // seconds
    float smooth   = 0.0f; // seconds of slew
    bool bipolar   = true;
};

/** Raw (bipolar -1..1) LFO waveform at an unwrapped phase in cycles. */
inline float lfoWave (LfoShape shape, double cycles, const Shape* custom, uint64_t seed) noexcept
{
    const auto cycle = static_cast<int64_t> (std::floor (cycles));
    const float p    = static_cast<float> (cycles - static_cast<double> (cycle));
    switch (shape)
    {
        case LfoShape::Sine:         return SineTable::get().lookup (p);
        case LfoShape::Triangle:     return 1.0f - 4.0f * std::abs (p - 0.5f) ; // starts at -1
        case LfoShape::SawUp:        return 2.0f * p - 1.0f;
        case LfoShape::SawDown:      return 1.0f - 2.0f * p;
        case LfoShape::Square:       return p < 0.5f ? 1.0f : -1.0f;
        case LfoShape::SampleHold:   return hashToBipolar (seed + (uint64_t) cycle);
        case LfoShape::SmoothRandom:
        {
            const float a = hashToBipolar (seed + (uint64_t) cycle), b = hashToBipolar (seed + (uint64_t) cycle + 1);
            const float t = 0.5f - 0.5f * SineTable::get().lookup (wrap01 (p * 0.5f + 0.25f)); // cosine ease
            return a + (b - a) * t;
        }
        case LfoShape::Custom:
        default:                     return custom != nullptr ? custom->evaluate (p) * 2.0f - 1.0f : 0.0f;
    }
}

/** Per-voice LFO, advanced at control rate. */
class Lfo
{
public:
    void noteOn (const LfoSettings& s, uint64_t voiceSeed) noexcept
    {
        cycles   = s.phase;
        elapsed  = 0.0f;
        seed     = voiceSeed;
        smoothed = 0.0f;
        first    = true;
        done     = false;
    }

    /** Advances by n samples and returns the output for this chunk.
        freeCycles is the engine's global phase (used in Free mode so all voices agree). */
    float advance (const LfoSettings& s, const Shape* custom, float rateHz, double freeCycles, uint64_t globalSeed,
                   int n, float sampleRate, bool needed = true) noexcept
    {
        const float dt = (float) n / sampleRate;
        double ph;
        uint64_t sd = seed;
        if (s.mode == LfoMode::Free)
        {
            ph = freeCycles + s.phase;
            sd = globalSeed;
        }
        else
        {
            if (! done)
                cycles += (double) rateHz * dt;
            if (s.mode == LfoMode::Envelope && cycles >= 1.0 + s.phase)
            {
                cycles = 1.0 + s.phase - 1.0e-6;
                done   = true;
            }
            ph = cycles;
        }

        if (! needed) // keep the phase running, skip the waveform maths
        {
            elapsed += (float) n / sampleRate;
            return 0.0f;
        }
        float v = lfoWave (s.shape, ph, custom, sd);
        if (! s.bipolar)
            v = 0.5f * (v + 1.0f);

        elapsed += dt;
        if (s.fade > 0.0f)
            v *= std::min (1.0f, elapsed / s.fade);

        if (s.smooth > 0.0f && ! first)
            smoothed += (v - smoothed) * (1.0f - std::exp (-dt / s.smooth));
        else
            smoothed = v;
        first = false;
        return smoothed;
    }

    float getValue() const noexcept { return smoothed; }

private:
    double cycles = 0.0;
    float elapsed = 0.0f, smoothed = 0.0f;
    uint64_t seed = 1;
    bool first = true, done = false;
};

} // namespace mbx::dsp
