// Granular cloud for the Granular oscillator type.
#pragma once

#include "DspCommon.h"

#include <string>
#include <vector>

namespace mbx::dsp
{
/** Immutable mono sample. Created on a background thread, swapped in atomically, freed late. */
struct SampleData
{
    std::vector<float> data;
    float sampleRate = 48000.0f;
    float rootHz     = 261.6256f; // the pitch the sample plays back unshifted at (C4)
    std::string name;
};

struct GrainSettings
{
    float position = 0.0f;  // 0..1 through the sample
    float sizeSec  = 0.08f; // grain length
    float density  = 30.0f; // grains per second
    float spray    = 0.1f;  // random position jitter 0..1
    float spread   = 0.5f;  // random stereo placement 0..1
};

class GrainCloud
{
public:
    static constexpr int kMaxGrains = 32;

    void reset (uint32_t seed) noexcept
    {
        rng.seed (seed);
        for (auto& g : grains) g.active = false;
        spawnCounter = 1.0e9f; // spawn immediately on the first sample
    }

    /** Adds n samples. Returns via raw (mono, may be null). gl/gr ramps applied by caller-provided start/step. */
    void render (const SampleData* sample, const GrainSettings& s, float noteHz, float sr, float* L, float* R, float* raw,
                 float gl, float dl, float gr, float dr, int n) noexcept
    {
        if (sample == nullptr || sample->data.size() < 64)
            return;

        const float len       = (float) sample->data.size();
        const float interval  = sr / std::clamp (s.density, 0.5f, 200.0f);
        const float grainLen  = std::max (16.0f, s.sizeSec * sr);
        const float inc       = (noteHz / sample->rootHz) * (sample->sampleRate / sr);
        const float* src      = sample->data.data();
        const auto& sine      = SineTable::get();
        const float normGain  = 1.0f / std::sqrt (std::max (1.0f, grainLen / interval)); // overlap compensation

        for (int i = 0; i < n; ++i)
        {
            if (++spawnCounter >= interval)
            {
                spawnCounter = 0.0f;
                spawn (s, len, grainLen, inc);
            }

            float l = 0.0f, r = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active)
                    continue;
                const float t = g.age / g.length;
                const float w = sine.lookup (t * 0.5f);
                const int p0 = (int) g.pos;
                const float fr = (float) (g.pos - p0);
                const float a = src[p0], b = src[std::min (p0 + 1, (int) len - 1)];
                const float v = (a + (b - a) * fr) * w * w; // Hann window
                l += v * g.panL;
                r += v * g.panR;
                g.pos += g.inc;
                g.age += 1.0f;
                if (g.age >= g.length || g.pos >= (double) len - 2.0 || g.pos < 0.0)
                    g.active = false;
            }
            l *= normGain;
            r *= normGain;
            L[i] += l * gl;
            R[i] += r * gr;
            if (raw != nullptr)
                raw[i] = 0.5f * (l + r);
            gl += dl;
            gr += dr;
        }
    }

private:
    struct Grain
    {
        bool active = false;
        double pos = 0.0, inc = 1.0;
        float age = 0.0f, length = 1.0f, panL = 0.7f, panR = 0.7f;
    };

    void spawn (const GrainSettings& s, float len, float grainLen, float inc) noexcept
    {
        for (auto& g : grains)
        {
            if (g.active)
                continue;
            const float start = std::clamp (s.position + s.spray * 0.5f * rng.bipolar(), 0.0f, 1.0f);
            g.active = true;
            g.pos    = (double) (start * (len - grainLen * inc - 2.0f));
            if (g.pos < 0.0) g.pos = 0.0;
            g.inc    = inc;
            g.age    = 0.0f;
            g.length = grainLen;
            equalPowerPan (s.spread * rng.bipolar(), g.panL, g.panR);
            return;
        }
    }

    std::array<Grain, kMaxGrains> grains {};
    float spawnCounter = 0.0f;
    Rng rng;
};

} // namespace mbx::dsp
