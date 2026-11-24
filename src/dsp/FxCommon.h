// FX rack building blocks: the effect interface, type table and small DSP primitives.
// JUCE-free. Effect objects are created/destroyed off the audio thread; process() is
// allocation- and lock-free.
#pragma once

#include "DspCommon.h"

#include <memory>
#include <vector>

namespace mbx::dsp::fx
{
/** APPEND ONLY — the slot "type" parameter stores this index. */
enum class FxType : int
{
    Off = 0, Distortion, OTT, Compressor, Transient, Chorus, Flanger, Phaser, Ensemble, Delay, Reverb,
    Stutter, Formant, RingMod, FreqShift, Vocoder, Widener, Ducker, Limiter, TiltEq, Count
};

inline constexpr int kParamsPerFx = 6;
using FxParams = std::array<float, kParamsPerFx>; // all normalised 0..1

struct FxInfo
{
    const char* name;
    std::array<const char*, kParamsPerFx> params;
    FxParams defaults;
    float defaultMix = 1.0f;
};

inline const FxInfo& fxInfo (FxType t) noexcept
{
    static const FxInfo table[] = {
        { "Off",          { "", "", "", "", "", "" },                                   { 0, 0, 0, 0, 0, 0 } },
        { "Distortion",   { "DRIVE", "MODE", "TONE", "BIAS", "CRUSH", "OUTPUT" },       { 0.35f, 0.0f, 0.7f, 0.5f, 0.0f, 0.5f } },
        { "OTT",          { "DEPTH", "TIME", "UPWARD", "DOWNWARD", "TILT", "OUTPUT" },  { 1.0f, 0.4f, 1.0f, 1.0f, 0.5f, 0.5f } },
        { "Compressor",   { "THRESH", "RATIO", "ATTACK", "RELEASE", "KNEE", "MAKEUP" }, { 0.6f, 0.3f, 0.3f, 0.4f, 0.3f, 0.2f } },
        { "Transient",    { "ATTACK", "SUSTAIN", "SPEED", "OUTPUT", "", "" },           { 0.7f, 0.5f, 0.5f, 0.5f, 0, 0 } },
        { "Chorus",       { "RATE", "DEPTH", "DELAY", "FEEDBACK", "VOICES", "WIDTH" },  { 0.3f, 0.5f, 0.4f, 0.1f, 0.34f, 0.8f }, 0.5f },
        { "Flanger",      { "RATE", "DEPTH", "DELAY", "FEEDBACK", "", "WIDTH" },        { 0.2f, 0.7f, 0.3f, 0.7f, 0, 0.5f }, 0.5f },
        { "Phaser",       { "RATE", "DEPTH", "STAGES", "CENTER", "FEEDBACK", "WIDTH" }, { 0.2f, 0.7f, 0.3f, 0.5f, 0.65f, 0.5f }, 0.6f },
        { "Ensemble",     { "RATE", "DEPTH", "TONE", "WIDTH", "", "" },                 { 0.4f, 0.6f, 0.7f, 0.9f, 0, 0 }, 0.6f },
        { "Delay",        { "TIME", "SPREAD", "FEEDBACK", "MODE", "TAPE", "SYNC" },     { 0.58f, 0.5f, 0.4f, 0.5f, 0.2f, 1.0f }, 0.3f },
        { "Reverb",       { "SIZE", "DECAY", "DAMP", "PREDELAY", "TYPE", "WIDTH" },     { 0.6f, 0.5f, 0.4f, 0.1f, 0.0f, 0.8f }, 0.25f },
        { "Stutter",      { "MODE", "LENGTH", "ENGAGE", "PITCH", "GRAIN", "SPRAY" },    { 0.0f, 0.5f, 0.0f, 0.5f, 0.3f, 0.3f }, 1.0f },
        { "Formant",      { "VOWEL", "SHIFT", "RESO", "DRIVE", "LFO RATE", "LFO AMT" }, { 0.0f, 0.5f, 0.5f, 0.1f, 0.3f, 0.0f }, 0.8f },
        { "Ring Mod",     { "FREQ", "LFO RATE", "LFO AMT", "WAVE", "SPREAD", "" },      { 0.4f, 0.2f, 0.0f, 0.0f, 0.2f, 0 }, 0.5f },
        { "Freq Shift",   { "SHIFT", "FEEDBACK", "STEREO", "DIRECTION", "", "" },       { 0.55f, 0.2f, 0.5f, 1.0f, 0, 0 }, 0.5f },
        { "Vocoder",      { "SOURCE", "Q", "SPEED", "SHIFT", "VOWEL", "OUTPUT" },       { 0.0f, 0.5f, 0.4f, 0.5f, 0.3f, 0.5f }, 1.0f },
        { "Widener",      { "WIDTH", "MONO BASS", "HAAS", "BALANCE", "", "" },          { 0.75f, 0.3f, 0.0f, 0.5f, 0, 0 } },
        { "Ducker",       { "DEPTH", "RATE", "SHAPE", "ATTACK", "PHASE", "" },          { 0.8f, 0.5f, 0.5f, 0.1f, 0.0f, 0 } },
        { "Limiter",      { "GAIN", "CEILING", "RELEASE", "", "", "" },                 { 0.0f, 0.95f, 0.3f, 0, 0, 0 } },
        { "Tilt EQ",      { "LOW", "MID", "HIGH", "LOW FREQ", "HIGH FREQ", "OUTPUT" },  { 0.5f, 0.5f, 0.5f, 0.3f, 0.6f, 0.5f } },
    };
    static_assert (std::size (table) == (size_t) FxType::Count);
    return table[(size_t) std::clamp ((int) t, 0, (int) FxType::Count - 1)];
}

struct FxContext
{
    float sampleRate = 48000.0f;
    float bpm        = 120.0f;
    double ppq       = -1.0;       // < 0: transport stopped (effects free-run)
    const float* sideL = nullptr;  // optional sidechain (vocoder modulator, etc.)
    const float* sideR = nullptr;
};

class FxUnit
{
public:
    virtual ~FxUnit() = default;
    /** NOT real-time safe (may allocate). */
    virtual void prepare (float sampleRate, int maxBlock) = 0;
    virtual void reset() noexcept = 0;
    virtual void process (float* L, float* R, int n, const FxParams& p, const FxContext& ctx) noexcept = 0;
    /** UI activity readout (e.g. gain reduction dB, LFO phase); written by the audio thread. */
    float getActivity() const noexcept { return activity; }
    FxType getType() const noexcept { return type; }

protected:
    explicit FxUnit (FxType t) : type (t) {}
    float activity = 0.0f;

private:
    FxType type;
};

std::unique_ptr<FxUnit> createFx (FxType type);

//==============================================================================
// Parameter mapping helpers
inline float lin (float p, float a, float b) noexcept  { return a + (b - a) * p; }
inline float expo (float p, float a, float b) noexcept { return a * std::pow (b / a, std::clamp (p, 0.0f, 1.0f)); }
inline int stepped (float p, int count) noexcept       { return std::clamp ((int) (p * (float) count), 0, count - 1); }
inline float bip (float p) noexcept                    { return p * 2.0f - 1.0f; }

//==============================================================================
/** Fractional delay line with linear interpolation. */
class DelayLine
{
public:
    void allocate (int maxSamples) { buf.assign ((size_t) std::max (4, maxSamples + 4), 0.0f); w = 0; }
    void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); }
    int size() const noexcept { return (int) buf.size(); }

    void push (float x) noexcept
    {
        buf[(size_t) w] = x;
        if (++w >= (int) buf.size()) w = 0;
    }
    /** delay in samples behind the most recently pushed sample (>= 0). */
    float read (float delay) const noexcept
    {
        const int len = (int) buf.size();
        delay = std::clamp (delay, 0.0f, (float) len - 3.0f);
        float rp = (float) (w - 1) - delay;
        while (rp < 0.0f) rp += (float) len;
        int i0 = (int) rp;
        const float fr = rp - (float) i0;
        if (i0 >= len) i0 -= len; // float rounding can land exactly on len
        const float a = buf[(size_t) i0], b = buf[(size_t) ((i0 + 1) % len)];
        return a + (b - a) * fr;
    }
    float readInt (int delay) const noexcept
    {
        const int len = (int) buf.size();
        int rp = w - 1 - delay;
        while (rp < 0) rp += len;
        return buf[(size_t) rp];
    }

private:
    std::vector<float> buf;
    int w = 0;
};

struct OnePole
{
    float z = 0.0f;
    float lp (float x, float coef) noexcept { z += (x - z) * coef; return z; }
    static float coefFor (float hz, float sr) noexcept { return 1.0f - std::exp (-kTwoPi * std::min (hz, sr * 0.49f) / sr); }
};

/** RBJ biquad (transposed direct form II). */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    enum class Kind { LowPass, HighPass, BandPass, Peak, LowShelf, HighShelf, Notch };

    void set (Kind k, float hz, float q, float gainDb, float sr) noexcept
    {
        hz = std::clamp (hz, 10.0f, sr * 0.49f);
        q  = std::max (q, 0.05f);
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = kTwoPi * hz / sr, cw = std::cos (w0), sw = std::sin (w0);
        const float alpha = sw / (2.0f * q);
        float B0, B1, B2, A0, A1, A2;
        switch (k)
        {
            case Kind::LowPass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Kind::HighPass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Kind::BandPass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Kind::Notch:    B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Kind::Peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case Kind::LowShelf:
            {
                const float sa = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + sa); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sa);
                A0 = (A + 1) + (A - 1) * cw + sa; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - sa;
                break;
            }
            case Kind::HighShelf:
            default:
            {
                const float sa = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + sa); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sa);
                A0 = (A + 1) - (A - 1) * cw + sa; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - sa;
                break;
            }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0.0f; }

    /** |H(e^jw)| at hz (for UI curves). */
    float magnitude (float hz, float sr) const noexcept
    {
        const float w = kTwoPi * hz / sr;
        const float c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const float nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const float dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / std::max (1.0e-12f, dr * dr + di * di));
    }
};

/** Peak/RMS-ish envelope follower with separate attack/release. */
struct EnvFollower
{
    float env = 0.0f, att = 0.01f, rel = 0.001f;
    void setTimes (float attackMs, float releaseMs, float sr) noexcept
    {
        att = 1.0f - std::exp (-1.0f / (std::max (0.01f, attackMs) * 0.001f * sr));
        rel = 1.0f - std::exp (-1.0f / (std::max (0.1f, releaseMs) * 0.001f * sr));
    }
    float process (float x) noexcept
    {
        const float a = std::abs (x);
        env += (a - env) * (a > env ? att : rel);
        return env;
    }
};

inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-6f)); }

/** Block-rate host-synced phase helper (beats -> cycles). Free-runs when the transport is stopped. */
struct SyncPhase
{
    double cycles = 0.0;
    /** Returns phase (cycles) at the start of this block and advances. */
    double advance (const FxContext& c, float beatsPerCycle, int n) noexcept
    {
        if (c.ppq >= 0.0)
            cycles = c.ppq / std::max (0.001f, beatsPerCycle);
        const double start = cycles;
        if (c.ppq < 0.0)
            cycles += (double) n / c.sampleRate * c.bpm / 60.0 / std::max (0.001f, beatsPerCycle);
        return start;
    }
};

} // namespace mbx::dsp::fx
