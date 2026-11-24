// One oscillator slot of one synth voice. Up to 16 unison sub-voices.
// Real-time safe: fixed-size state, no allocation, no locks.
#pragma once

#include "DspCommon.h"
#include "FactoryTables.h"

namespace mbx::dsp
{
/** Choice-list orders below are mirrored in params/Parameters.cpp — append only. */
enum class OscType : int { Wavetable = 0, Analog, FM, Granular, Supersaw, Noise };
enum class VaShape : int { Sine = 0, Triangle, Saw, Square, Pulse };
enum class NoiseType : int { White = 0, Pink, Brown, Digital, Vinyl };

/** Block-rate snapshot of one oscillator's parameters. */
struct OscSettings
{
    bool on            = false;
    OscType type       = OscType::Analog;
    VaShape shape      = VaShape::Saw;
    NoiseType noise    = NoiseType::White;
    int table          = 0;
    float pitchSemis   = 0.0f;  // octave*12 + coarse (discrete, never smoothed)
    float fineCents    = 0.0f;  // -100..100 (smoothed)
    float level        = 0.75f; // linear 0..1
    float pan          = 0.0f;  // -1..1
    float phase        = 0.0f;  // start phase 0..1
    float phaseRand    = 1.0f;  // 0..1
    int unison         = 1;     // 1..16
    float detuneCents  = 15.0f; // total spread of the outermost unison voices
    float blend        = 0.75f; // level of side voices vs centre
    float spread       = 0.6f;  // stereo width of unison
    float wtPosition   = 0.0f;  // 0..1
    float pulseWidth   = 0.5f;  // 0.02..0.98
};

/** Coloured / textured noise source. */
class NoiseGen
{
public:
    void reset (uint32_t seed) noexcept
    {
        rng.seed (seed);
        b0 = b1 = b2 = brown = held = holdPhase = crackle = 0.0f;
    }

    float next (NoiseType type, float holdInc, float crackleProbability) noexcept
    {
        const float w = rng.bipolar();
        switch (type)
        {
            case NoiseType::White: return w;
            case NoiseType::Pink:  return pink (w);
            case NoiseType::Brown:
                brown = (brown + 0.02f * w) * (1.0f / 1.02f);
                return brown * 3.5f;
            case NoiseType::Digital:
                holdPhase += holdInc; // pitched sample-and-hold, quantised to 3 bits
                if (holdPhase >= 1.0f)
                {
                    holdPhase -= std::floor (holdPhase);
                    held = std::round (w * 4.0f) * 0.25f;
                }
                return held;
            case NoiseType::Vinyl:
            default:
                if (rng.uniform() < crackleProbability)
                    crackle = rng.bipolar() * (0.4f + 0.6f * rng.uniform());
                else
                    crackle *= 0.55f;
                return pink (w) * 0.12f + crackle;
        }
    }

private:
    float pink (float w) noexcept // Paul Kellet's economy pink filter
    {
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        return (b0 + b1 + b2 + w * 0.1848f) * 0.22f;
    }

    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0, brown = 0, held = 0, holdPhase = 0, crackle = 0;
};

class Oscillator
{
public:
    static constexpr int kMaxUnison = 16;

    void setBank (const WavetableBank* b) noexcept { bank = b; }
    void setSeed (uint32_t seed) noexcept
    {
        rng.seed (seed);
        noiseL.reset (seed * 3u + 1u);
        noiseR.reset (seed * 7u + 5u);
    }

    /** Called on note start: resets phases (with optional randomisation). */
    void noteOn (const OscSettings& s) noexcept
    {
        for (int v = 0; v < kMaxUnison; ++v)
            phase[(size_t) v] = wrap01 (s.phase + s.phaseRand * rng.uniform());
        prevGainL.fill (0.0f);
        prevGainR.fill (0.0f);
        hasPrev     = false;
        prevVoices  = 0;
        prevPos     = s.wtPosition;
        prevPw      = s.pulseWidth;
    }

    /** Adds n samples into L/R. noteHz is the voice pitch incl. bend/glide. */
    void render (const OscSettings& s, float noteHz, float sampleRate, float* L, float* R, int n) noexcept
    {
        if (n <= 0)
            return;
        if (! s.on && prevVoices == 0)
            return;

        const bool isNoise   = s.type == OscType::Noise;
        const bool isSuper   = s.type == OscType::Supersaw;
        const int targetVox  = ! s.on || s.type == OscType::Granular ? 0
                             : isNoise ? 1
                             : isSuper ? (s.unison == 1 ? 7 : s.unison)
                                       : std::clamp (s.unison, 1, kMaxUnison);

        // --- unison layout (block rate) ---------------------------------------------------
        std::array<float, kMaxUnison> tgtL {}, tgtR {};
        if (targetVox > 0)
        {
            std::array<float, kMaxUnison> gains {};
            float sumSq = 0.0f;
            for (int i = 0; i < targetVox; ++i)
            {
                const float t = targetVox == 1 ? 0.0f : 2.0f * (float) i / (float) (targetVox - 1) - 1.0f;
                const float shaped = isSuper ? std::copysign (std::pow (std::abs (t), 1.6f), t) : t;
                detuneRatio[(size_t) i] = semitonesToRatio (shaped * s.detuneCents * 0.01f);

                const bool centre = targetVox == 1
                                 || ((targetVox & 1) ? i == targetVox / 2 : (i == targetVox / 2 - 1 || i == targetVox / 2));
                const float g = targetVox == 1 ? 1.0f : (centre ? 1.0f - 0.5f * s.blend : s.blend);
                gains[(size_t) i] = g;
                sumSq += g * g;

                const float uniPan = targetVox == 1 ? 0.0f : t * s.spread * ((i & 1) ? -1.0f : 1.0f);
                float pl, pr;
                equalPowerPan (s.pan + uniPan, pl, pr);
                tgtL[(size_t) i] = pl;
                tgtR[(size_t) i] = pr;
            }
            const float norm = sumSq > 0.0f ? s.level / std::sqrt (sumSq) : 0.0f;
            for (int i = 0; i < targetVox; ++i)
            {
                tgtL[(size_t) i] *= gains[(size_t) i] * norm;
                tgtR[(size_t) i] *= gains[(size_t) i] * norm;
            }
        }

        if (! hasPrev) // first block of a note: start at target, the amp envelope handles the fade-in
        {
            prevGainL = tgtL;
            prevGainR = tgtR;
            hasPrev   = true;
        }

        const int voices    = std::max (targetVox, prevVoices); // voices being removed ramp to 0
        const float baseInc = noteHz * semitonesToRatio (s.pitchSemis + s.fineCents * 0.01f) / sampleRate;
        const float invN    = 1.0f / (float) n;

        if (isNoise || (prevType == OscType::Noise && targetVox == 0))
        {
            const float holdInc = std::min (1.0f, baseInc * 8.0f);
            const float crackle = 3.0f / sampleRate; // ~3 crackles per second
            float gl = prevGainL[0], gr = prevGainR[0];
            const float dl = (tgtL[0] - gl) * invN, dr = (tgtR[0] - gr) * invN;
            const float width = s.spread;
            for (int i = 0; i < n; ++i)
            {
                const float a = noiseL.next (s.noise, holdInc, crackle);
                const float b = noiseR.next (s.noise, holdInc, crackle);
                L[i] += a * gl;
                R[i] += (a + (b - a) * width) * gr;
                gl += dl;
                gr += dr;
            }
        }
        else
        {
            const auto& sine = SineTable::get();
            const Wavetable* wt = bank != nullptr ? &bank->get (s.table) : nullptr;
            const float pos0 = prevPos, dPos = (s.wtPosition - prevPos) * invN;
            const float pw0  = prevPw,  dPw  = (s.pulseWidth - prevPw) * invN;

            for (int v = 0; v < voices; ++v)
            {
                const auto vi  = (size_t) v;
                const float inc = std::min (baseInc * detuneRatio[vi], 0.45f);
                const float tl  = v < targetVox ? tgtL[vi] : 0.0f;
                const float tr  = v < targetVox ? tgtR[vi] : 0.0f;

                switch (isSuper ? OscType::Analog : s.type)
                {
                    case OscType::Wavetable:
                    {
                        if (wt == nullptr || wt->isEmpty())
                            break;
                        const int mip = Wavetable::mipForIncrement (inc);
                        runVoice (vi, inc, tl, tr, L, R, n, [&] (float p, int i) {
                            return wt->read (p, pos0 + dPos * (float) i, mip);
                        });
                        break;
                    }
                    case OscType::FM: // operator mode: a pure sine carrier until FM routing lands (phase 2)
                        runVoice (vi, inc, tl, tr, L, R, n, [&] (float p, int) { return sine.lookup (p); });
                        break;

                    case OscType::Analog:
                    case OscType::Supersaw:
                    case OscType::Granular:
                    case OscType::Noise:
                    default:
                        renderAnalog (isSuper ? VaShape::Saw : s.shape, vi, inc, tl, tr, L, R, n, pw0, dPw, sine);
                        break;
                }
            }
        }

        prevGainL  = tgtL;
        prevGainR  = tgtR;
        prevVoices = targetVox;
        prevType   = s.type;
        prevPos    = s.wtPosition;
        prevPw     = s.pulseWidth;
    }

private:
    template <typename SampleFn>
    void runVoice (size_t v, float inc, float tl, float tr, float* L, float* R, int n, SampleFn&& fn) noexcept
    {
        float p  = phase[v];
        float gl = prevGainL[v], gr = prevGainR[v];
        const float invN = 1.0f / (float) n;
        const float dl = (tl - gl) * invN, dr = (tr - gr) * invN;
        for (int i = 0; i < n; ++i)
        {
            const float x = fn (p, i);
            L[i] += x * gl;
            R[i] += x * gr;
            gl += dl;
            gr += dr;
            p += inc;
            if (p >= 1.0f)
                p -= 1.0f;
        }
        phase[v] = p;
    }

    void renderAnalog (VaShape shape, size_t v, float inc, float tl, float tr, float* L, float* R, int n,
                       float pw0, float dPw, const SineTable& sine) noexcept
    {
        switch (shape)
        {
            case VaShape::Sine:
                runVoice (v, inc, tl, tr, L, R, n, [&] (float p, int) { return sine.lookup (p); });
                break;
            case VaShape::Triangle:
                runVoice (v, inc, tl, tr, L, R, n, [&] (float p, int) {
                    float x = 1.0f - 4.0f * std::abs (p - 0.5f);
                    x += 4.0f * inc * polyBlamp (p, inc); // slope change of 8/cycle; residual is two-sided
                    float p2 = p + 0.5f;
                    if (p2 >= 1.0f) p2 -= 1.0f;
                    x -= 4.0f * inc * polyBlamp (p2, inc);
                    return x;
                });
                break;
            case VaShape::Saw:
                runVoice (v, inc, tl, tr, L, R, n, [&] (float p, int) {
                    return 2.0f * p - 1.0f - polyBlep (p, inc);
                });
                break;
            case VaShape::Square:
            case VaShape::Pulse:
            default:
            {
                const bool fixed = shape == VaShape::Square;
                runVoice (v, inc, tl, tr, L, R, n, [&] (float p, int i) {
                    const float pw = fixed ? 0.5f : std::clamp (pw0 + dPw * (float) i, 0.02f, 0.98f);
                    float x = p < pw ? 1.0f : -1.0f;
                    x += polyBlep (p, inc);
                    float p2 = p + 1.0f - pw;
                    if (p2 >= 1.0f) p2 -= 1.0f;
                    x -= polyBlep (p2, inc);
                    return x - (2.0f * pw - 1.0f); // remove DC so PWM doesn't thump
                });
                break;
            }
        }
    }

    const WavetableBank* bank = nullptr;
    Rng rng;
    NoiseGen noiseL, noiseR;
    std::array<float, kMaxUnison> phase {}, prevGainL {}, prevGainR {};
    std::array<float, kMaxUnison> detuneRatio { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                                1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float prevPos = 0.0f, prevPw = 0.5f;
    int prevVoices = 0;
    OscType prevType = OscType::Analog;
    bool hasPrev = false;
};

} // namespace mbx::dsp
