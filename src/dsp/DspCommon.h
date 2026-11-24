// Mystery Box by Raygun — shared DSP utilities.
// Everything in src/dsp is plain C++20 with no JUCE dependency so it can be
// unit-tested in isolation and is guaranteed allocation-free on the audio path.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>

namespace mbx::dsp
{
inline constexpr float kPi    = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;

inline float semitonesToRatio (float semis) noexcept { return std::exp2 (semis * (1.0f / 12.0f)); }
inline float midiNoteToHz (float note) noexcept       { return 440.0f * std::exp2 ((note - 69.0f) * (1.0f / 12.0f)); }
inline float dbToGain (float db) noexcept             { return db <= -100.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }

/** Tiny, fast, deterministic RNG (xorshift32). Safe to use on the audio thread. */
class Rng
{
public:
    explicit Rng (uint32_t seed = 0x9E3779B9u) noexcept : state (seed != 0 ? seed : 1u) {}
    void seed (uint32_t s) noexcept { state = s != 0 ? s : 1u; }

    uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    /** Uniform in [0, 1). */
    float uniform() noexcept { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
    /** Uniform in [-1, 1). */
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }

private:
    uint32_t state;
};

/** PolyBLEP residual for a unit step discontinuity. t = phase in [0,1), dt = phase increment. */
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/** PolyBLAMP residual for a unit change of slope (used for the triangle). */
inline float polyBlamp (float t, float dt) noexcept
{
    if (t < dt)
    {
        t = t / dt - 1.0f;
        return -(1.0f / 3.0f) * t * t * t;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt + 1.0f;
        return (1.0f / 3.0f) * t * t * t;
    }
    return 0.0f;
}

/** Exact float comparison for change-detection caches (intentional; avoids -Wfloat-equal noise). */
inline bool exactlyEqual (float a, float b) noexcept { return std::equal_to<float>() (a, b); }

inline float wrap01 (float p) noexcept
{
    p -= std::floor (p);
    return p >= 1.0f ? 0.0f : p;
}

/** Cheap rational tanh approximation (exact at +-3, where it saturates to +-1). */
inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Soft limiter that is transparent below |x| = 1 and bounded to +-2. */
inline float softLimit (float x) noexcept
{
    const float a = std::abs (x);
    if (a <= 1.0f)
        return x;
    const float over = a - 1.0f;
    return std::copysign (1.0f + over / (1.0f + over), x);
}

/** Pade approximation of tan(x) for 0 <= x < ~1.45 (used for per-sample filter FM). */
inline float fastTan (float x) noexcept
{
    const float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

/** Integer hash -> uniform [-1, 1). Used for stateless, voice-synchronous random LFOs. */
inline float hashToBipolar (uint64_t v) noexcept
{
    v ^= v >> 33; v *= 0xff51afd7ed558ccdULL;
    v ^= v >> 33; v *= 0xc4ceb9fe1a85ec53ULL;
    v ^= v >> 33;
    return static_cast<float> (v >> 40) * (2.0f / 16777216.0f) - 1.0f;
}

/** -3 dB equal-power pan law. pan in [-1, 1]. */
inline void equalPowerPan (float pan, float& left, float& right) noexcept
{
    const float a = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    left  = std::cos (a);
    right = std::sin (a);
}

/** Shared 4096-point sine lookup (with guard point), built once at static-init time. */
class SineTable
{
public:
    static constexpr int kSize = 4096;

    static const SineTable& get() noexcept
    {
        static const SineTable instance;
        return instance;
    }

    /** phase in [0,1). */
    float lookup (float phase) const noexcept
    {
        const float idx = phase * static_cast<float> (kSize);
        const int i     = static_cast<int> (idx);
        const float f   = idx - static_cast<float> (i);
        return table[(size_t) i] + f * (table[(size_t) i + 1] - table[(size_t) i]);
    }

private:
    SineTable() noexcept
    {
        for (int i = 0; i <= kSize; ++i)
            table[(size_t) i] = std::sin (kTwoPi * static_cast<float> (i) / static_cast<float> (kSize));
    }
    std::array<float, kSize + 1> table {};
};

/**
    Rational curve used by envelope stages. x in [0,1], curve in [-1,1].
    curve = 0 is linear, > 0 is fast-start (log-like), < 0 is slow-start (exp-like).
    Costs one division, so it is cheap enough to evaluate per sample.
*/
inline float curveShape (float x, float curve) noexcept
{
    const float a = curve >= 0.0f ? curve * 15.0f : curve * 0.94f;
    return (1.0f + a) * x / (1.0f + a * x);
}

} // namespace mbx::dsp
