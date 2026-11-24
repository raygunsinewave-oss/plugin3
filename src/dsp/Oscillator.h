// One oscillator slot of one synth voice: up to 16 unison sub-voices, warp modes,
// phase/amplitude modulation from another oscillator, wavetable scanning and granular.
// Real-time safe: fixed-size state, no allocation, no locks.
#pragma once

#include "DspCommon.h"
#include "FactoryTables.h"
#include "Granular.h"
#include "Modulation.h"

namespace mbx::dsp
{
/** Choice-list orders below are mirrored in params/Parameters.cpp — append only. */
enum class OscType : int { Wavetable = 0, Analog, FM, Granular, Supersaw, Noise };
enum class VaShape : int { Sine = 0, Triangle, Saw, Square, Pulse };
enum class NoiseType : int { White = 0, Pink, Brown, Digital, Vinyl };
enum class WarpMode : int { None = 0, BendPlus, BendMinus, Sync, Mirror, Pwm, Formant, Quantize, Bitcrush, FM, AM, Ring };
enum class OscRoute : int { Filter1 = 0, Filter2, Both, Bypass };
enum class ScanLoop : int { OneShot = 0, Loop, PingPong };

inline constexpr const char* kWarpModeNames[] = { "None", "Bend +", "Bend -", "Sync", "Mirror", "PWM",
                                                  "Formant", "Quantize", "Bit Crush", "FM", "AM", "Ring" };
inline constexpr int kUserTableIndex = (int) kFactoryTableNames.size(); // "User" entry after the factory tables

/** Block-rate snapshot of one oscillator's (modulated) parameters. */
struct OscSettings
{
    bool on            = false;
    OscType type       = OscType::Analog;
    VaShape shape      = VaShape::Saw;
    NoiseType noise    = NoiseType::White;
    int table          = 0;
    float pitchSemis   = 0.0f;  // octave*12 + coarse (discrete, never smoothed)
    float fineCents    = 0.0f;  // -100..100 (smoothed)
    float level        = 0.75f;
    float pan          = 0.0f;
    float phase        = 0.0f;  // start phase 0..1
    float phaseRand    = 1.0f;
    int unison         = 1;
    float detuneCents  = 15.0f;
    float blend        = 0.75f;
    float spread       = 0.6f;
    float wtPosition   = 0.0f;
    float pulseWidth   = 0.5f;
    OscRoute route     = OscRoute::Filter1;

    WarpMode warp      = WarpMode::None;
    float warpAmount   = 0.0f;
    int modSource      = -1;    // -1 off, 0..5 oscillator index (self = feedback)

    bool scanOn        = false;
    float scanRateHz   = 0.5f;
    bool scanSync      = false;
    int scanDivision   = 16;    // 1 bar
    float scanStart = 0.0f, scanMid = 0.5f, scanEnd = 1.0f;
    ScanLoop scanLoop  = ScanLoop::Loop;

    GrainSettings grain {};

    const Wavetable* userTable = nullptr; // imported table (if any)
    const SampleData* sample   = nullptr; // granular source (if any)
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
                holdPhase += holdInc;
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
        grainSeed = seed * 13u + 7u;
    }

    /** Called on note start: resets phases (with optional randomisation), scan and grains. */
    void noteOn (const OscSettings& s) noexcept
    {
        for (int v = 0; v < kMaxUnison; ++v)
            phase[(size_t) v] = wrap01 (s.phase + s.phaseRand * rng.uniform());
        prevGainL.fill (0.0f);
        prevGainR.fill (0.0f);
        lastOut.fill (0.0f);
        layoutVox  = -1;
        hasPrev    = false;
        prevVoices = 0;
        prevPos    = s.wtPosition;
        prevPw     = s.pulseWidth;
        scanCycles = 0.0;
        grains.reset (grainSeed++);
    }

    /** Wavetable/grain position from the scanner (call once per chunk before render when scanOn). */
    float advanceScan (const OscSettings& s, float bpm, int n, float sampleRate) noexcept
    {
        const float hz = s.scanSync ? syncDivisionHz (s.scanDivision, bpm) : s.scanRateHz;
        scanCycles += (double) hz * n / sampleRate;
        float ph;
        switch (s.scanLoop)
        {
            case ScanLoop::OneShot:  ph = (float) std::min (scanCycles, 1.0); break;
            case ScanLoop::PingPong: { const float f = (float) std::fmod (scanCycles, 2.0); ph = f < 1.0f ? f : 2.0f - f; break; }
            case ScanLoop::Loop:
            default:                 ph = (float) (scanCycles - std::floor (scanCycles)); break;
        }
        return ph < 0.5f ? s.scanStart + (s.scanMid - s.scanStart) * ph * 2.0f
                         : s.scanMid + (s.scanEnd - s.scanMid) * (ph - 0.5f) * 2.0f;
    }

    /** Adds n samples into L/R. raw (optional) receives the un-panned first unison voice (pre-level),
        used as an FM/AM/ring/filter-FM source by other oscillators. modIn is another oscillator's raw
        signal (or null); selfMod = this oscillator modulates itself (feedback). */
    void render (const OscSettings& s, float noteHz, float sampleRate, float* L, float* R, float* raw,
                 const float* modIn, bool selfMod, int n) noexcept
    {
        if (n <= 0)
            return;
        if (raw != nullptr)
            std::fill_n (raw, n, 0.0f);
        if (! s.on && prevVoices == 0)
            return;

        const bool isNoise  = s.type == OscType::Noise;
        const bool isGrain  = s.type == OscType::Granular;
        const bool isSuper  = s.type == OscType::Supersaw;
        const int targetVox = ! s.on ? 0
                            : (isNoise || isGrain) ? 1
                            : isSuper ? (s.unison == 1 ? 7 : s.unison)
                                      : std::clamp (s.unison, 1, kMaxUnison);

        // --- unison layout (recomputed only when its inputs change) ------------------------
        const bool layoutChanged = targetVox != layoutVox || ! exactlyEqual (s.detuneCents, layoutDetune) || ! exactlyEqual (s.blend, layoutBlend)
                                || ! exactlyEqual (s.spread, layoutSpread) || ! exactlyEqual (s.pan, layoutPan)
                                || ! exactlyEqual (s.level, layoutLevel) || isSuper != layoutSuper;
        auto& tgtL = layoutL;
        auto& tgtR = layoutR;
        if (layoutChanged)
        {
            layoutVox = targetVox; layoutDetune = s.detuneCents; layoutBlend = s.blend;
            layoutSpread = s.spread; layoutPan = s.pan; layoutLevel = s.level; layoutSuper = isSuper;
            tgtL.fill (0.0f);
            tgtR.fill (0.0f);
        }
        if (targetVox > 0 && layoutChanged)
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
            const float norm = sumSq > 0.0f ? std::max (0.0f, s.level) / std::sqrt (sumSq) : 0.0f;
            for (int i = 0; i < targetVox; ++i)
            {
                tgtL[(size_t) i] *= gains[(size_t) i] * norm;
                tgtR[(size_t) i] *= gains[(size_t) i] * norm;
            }
        }

        if (! hasPrev)
        {
            prevGainL = tgtL;
            prevGainR = tgtR;
            hasPrev   = true;
        }

        const int voices    = std::max (targetVox, prevVoices);
        const float baseInc = noteHz * semitonesToRatio (s.pitchSemis + s.fineCents * 0.01f) / sampleRate;
        const float invN    = 1.0f / (float) n;

        if (isNoise || (prevType == OscType::Noise && targetVox == 0))
        {
            const float holdInc = std::min (1.0f, baseInc * 8.0f);
            const float crackle = 3.0f / sampleRate;
            float gl = prevGainL[0], gr = prevGainR[0];
            const float dl = (tgtL[0] - gl) * invN, dr = (tgtR[0] - gr) * invN;
            for (int i = 0; i < n; ++i)
            {
                const float a = noiseL.next (s.noise, holdInc, crackle);
                const float b = noiseR.next (s.noise, holdInc, crackle);
                L[i] += a * gl;
                R[i] += (a + (b - a) * s.spread) * gr;
                if (raw != nullptr) raw[i] = a;
                gl += dl;
                gr += dr;
            }
        }
        else if (isGrain || (prevType == OscType::Granular && targetVox == 0))
        {
            GrainSettings g = s.grain;
            g.position = std::clamp (s.wtPosition, 0.0f, 1.0f);
            const float hz = baseInc * sampleRate;
            grains.render (s.sample, g, hz, sampleRate, L, R, raw, prevGainL[0], (tgtL[0] - prevGainL[0]) * invN,
                           prevGainR[0], (tgtR[0] - prevGainR[0]) * invN, n);
        }
        else
        {
            const Wavetable* wt = resolveTable (s);
            const bool warped = s.warp != WarpMode::None && s.warpAmount > 0.0f;
            const float pos0 = prevPos, dPos = (s.wtPosition - prevPos) * invN;
            const float pw0  = prevPw,  dPw  = (s.pulseWidth - prevPw) * invN;

            for (int v = 0; v < voices; ++v)
            {
                const auto vi   = (size_t) v;
                const float inc = std::min (baseInc * detuneRatio[vi], 0.45f);
                const float tl  = v < targetVox ? tgtL[vi] : 0.0f;
                const float tr  = v < targetVox ? tgtR[vi] : 0.0f;
                float* rawV     = v == 0 ? raw : nullptr;

                if (warped)
                    renderWarped (s, wt, vi, inc, tl, tr, L, R, rawV, modIn, selfMod, n, pos0, dPos, pw0, dPw);
                else
                    renderClean (s, wt, vi, inc, tl, tr, L, R, rawV, n, pos0, dPos, pw0, dPw);
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
    const Wavetable* resolveTable (const OscSettings& s) const noexcept
    {
        if (s.table == kUserTableIndex && s.userTable != nullptr && ! s.userTable->isEmpty())
            return s.userTable;
        return bank != nullptr ? &bank->get (s.table) : nullptr;
    }

    template <typename SampleFn>
    void runVoice (size_t v, float inc, float tl, float tr, float* L, float* R, float* raw, int n, SampleFn&& fn) noexcept
    {
        if (raw != nullptr)
            runVoiceT<true> (v, inc, tl, tr, L, R, raw, n, fn);
        else
            runVoiceT<false> (v, inc, tl, tr, L, R, raw, n, fn);
    }

    template <bool WriteRaw, typename SampleFn>
    void runVoiceT (size_t v, float inc, float tl, float tr, float* L, float* R, float* raw, int n, SampleFn& fn) noexcept
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
            if constexpr (WriteRaw) raw[i] = x;
            gl += dl;
            gr += dr;
            p += inc;
            if (p >= 1.0f)
                p -= 1.0f;
        }
        phase[v] = p;
    }

    /** Fast path: band-limited (polyBLEP / mip-mapped) with no warp. */
    void renderClean (const OscSettings& s, const Wavetable* wt, size_t v, float inc, float tl, float tr, float* L, float* R,
                      float* raw, int n, float pos0, float dPos, float pw0, float dPw) noexcept
    {
        const auto& sine = SineTable::get();
        const OscType type = s.type == OscType::Supersaw ? OscType::Analog : s.type;
        const VaShape shape = s.type == OscType::Supersaw ? VaShape::Saw : s.shape;

        if (type == OscType::Wavetable)
        {
            if (wt == nullptr || wt->isEmpty())
                return;
            const int mip = Wavetable::mipForIncrement (inc);
            if (exactlyEqual (dPos, 0.0f)) // static position: resolve the frame pair once per chunk
            {
                const auto fr = wt->frames (pos0, mip);
                runVoice (v, inc, tl, tr, L, R, raw, n, [&fr] (float p, int) { return Wavetable::readPair (fr, p); });
            }
            else
                runVoice (v, inc, tl, tr, L, R, raw, n, [&] (float p, int i) { return wt->read (p, pos0 + dPos * (float) i, mip); });
            return;
        }
        if (type == OscType::FM)
        {
            runVoice (v, inc, tl, tr, L, R, raw, n, [&] (float p, int) { return sine.lookup (p); });
            return;
        }

        switch (shape)
        {
            case VaShape::Sine:
                runVoice (v, inc, tl, tr, L, R, raw, n, [&] (float p, int) { return sine.lookup (p); });
                break;
            case VaShape::Triangle:
                runVoice (v, inc, tl, tr, L, R, raw, n, [&] (float p, int) {
                    float x = 1.0f - 4.0f * std::abs (p - 0.5f);
                    x += 4.0f * inc * polyBlamp (p, inc); // slope change of 8/cycle; residual is two-sided
                    float p2 = p + 0.5f;
                    if (p2 >= 1.0f) p2 -= 1.0f;
                    x -= 4.0f * inc * polyBlamp (p2, inc);
                    return x;
                });
                break;
            case VaShape::Saw:
                runVoice (v, inc, tl, tr, L, R, raw, n, [&] (float p, int) { return 2.0f * p - 1.0f - polyBlep (p, inc); });
                break;
            case VaShape::Square:
            case VaShape::Pulse:
            default:
            {
                const bool fixed = shape == VaShape::Square;
                runVoice (v, inc, tl, tr, L, R, raw, n, [&] (float p, int i) {
                    const float pw = fixed ? 0.5f : std::clamp (pw0 + dPw * (float) i, 0.02f, 0.98f);
                    float x = p < pw ? 1.0f : -1.0f;
                    x += polyBlep (p, inc);
                    float p2 = p + 1.0f - pw;
                    if (p2 >= 1.0f) p2 -= 1.0f;
                    x -= polyBlep (p2, inc);
                    return x - (2.0f * pw - 1.0f);
                });
                break;
            }
        }
    }

    /** Naive waveform at an arbitrary (warped) phase. Oversampling keeps aliasing in check. */
    float naiveAt (const OscSettings& s, const Wavetable* wt, float p, float pos, float pw, int mip) const noexcept
    {
        switch (s.type)
        {
            case OscType::Wavetable: return wt != nullptr && ! wt->isEmpty() ? wt->read (p, pos, mip) : 0.0f;
            case OscType::FM:        return SineTable::get().lookup (p);
            case OscType::Supersaw:  return 2.0f * p - 1.0f;
            case OscType::Analog:
            case OscType::Granular:
            case OscType::Noise:
            default: break;
        }
        switch (s.shape)
        {
            case VaShape::Sine:     return SineTable::get().lookup (p);
            case VaShape::Triangle: return 1.0f - 4.0f * std::abs (p - 0.5f);
            case VaShape::Saw:      return 2.0f * p - 1.0f;
            case VaShape::Square:   return p < 0.5f ? 1.0f : -1.0f;
            case VaShape::Pulse:
            default:                return (p < pw ? 1.0f : -1.0f) - (2.0f * pw - 1.0f);
        }
    }

    void renderWarped (const OscSettings& s, const Wavetable* wt, size_t v, float inc, float tl, float tr, float* L, float* R,
                       float* raw, const float* modIn, bool selfMod, int n, float pos0, float dPos, float pw0, float dPw) noexcept
    {
        const float amt   = std::clamp (s.warpAmount, 0.0f, 1.0f);
        const float ratio = 1.0f + 7.0f * amt;
        const auto& sine  = SineTable::get();
        const WarpMode w  = s.warp;
        const float bandwidth = (w == WarpMode::Sync || w == WarpMode::Formant) ? ratio : (w == WarpMode::FM ? 1.0f + 6.0f * amt : 1.0f);
        const int mip     = Wavetable::mipForIncrement (std::min (inc * bandwidth, 0.5f));
        const float pwmW  = 0.5f - 0.48f * amt;
        const float qSteps = 2.0f + (1.0f - amt) * (1.0f - amt) * 126.0f;
        const float crushLevels = 2.0f + (1.0f - amt) * (1.0f - amt) * (1.0f - amt) * 254.0f;
        const float fbScale = selfMod ? 0.35f : 1.0f; // feedback FM is tamed to stay musical

        float p  = phase[v];
        float gl = prevGainL[v], gr = prevGainR[v];
        const float invN = 1.0f / (float) n;
        const float dl = (tl - gl) * invN, dr = (tr - gr) * invN;
        float fb = lastOut[v];

        for (int i = 0; i < n; ++i)
        {
            const float m = selfMod ? fb : (modIn != nullptr ? modIn[i] : 0.0f);
            float rp = p, win = 1.0f;
            switch (w)
            {
                case WarpMode::BendPlus:  rp = curveShape (p, amt); break;
                case WarpMode::BendMinus: rp = curveShape (p, -amt); break;
                case WarpMode::Sync:      rp = wrap01 (p * ratio); break;
                case WarpMode::Mirror:    { const float mp = p < 0.5f ? p * 2.0f : (1.0f - p) * 2.0f; rp = wrap01 (p + (mp - p) * amt); break; }
                case WarpMode::Pwm:       rp = p < pwmW ? p * 0.5f / pwmW : 0.5f + (p - pwmW) * 0.5f / (1.0f - pwmW); break;
                case WarpMode::Formant:   rp = wrap01 (p * ratio); win = sine.lookup (p * 0.5f); break;
                case WarpMode::Quantize:  rp = std::floor (p * qSteps) / qSteps; break;
                case WarpMode::FM:        rp = wrap01 (p + amt * 2.0f * fbScale * m); break;
                case WarpMode::None:
                case WarpMode::Bitcrush:
                case WarpMode::AM:
                case WarpMode::Ring:
                default: break;
            }
            rp = std::clamp (rp, 0.0f, 0.99999f);

            float x = naiveAt (s, wt, rp, pos0 + dPos * (float) i, std::clamp (pw0 + dPw * (float) i, 0.02f, 0.98f), mip) * win;

            if (w == WarpMode::Bitcrush)  x = std::round (x * crushLevels) / crushLevels;
            else if (w == WarpMode::AM)   x *= 1.0f - amt * 0.5f * (1.0f - m);
            else if (w == WarpMode::Ring) x *= 1.0f - amt + amt * m;

            fb = x;
            L[i] += x * gl;
            R[i] += x * gr;
            if (raw != nullptr) raw[i] = x;
            gl += dl;
            gr += dr;
            p += inc;
            if (p >= 1.0f)
                p -= 1.0f;
        }
        phase[v]   = p;
        lastOut[v] = fb;
    }

    const WavetableBank* bank = nullptr;
    Rng rng;
    NoiseGen noiseL, noiseR;
    GrainCloud grains;
    uint32_t grainSeed = 1;
    std::array<float, kMaxUnison> phase {}, prevGainL {}, prevGainR {}, lastOut {};
    std::array<float, kMaxUnison> detuneRatio { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                                1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float prevPos = 0.0f, prevPw = 0.5f;
    double scanCycles = 0.0;
    std::array<float, kMaxUnison> layoutL {}, layoutR {};
    int layoutVox = -1;
    float layoutDetune = 0, layoutBlend = 0, layoutSpread = 0, layoutPan = 0, layoutLevel = 0;
    bool layoutSuper = false;
    int prevVoices = 0;
    OscType prevType = OscType::Analog;
    bool hasPrev = false;
};

} // namespace mbx::dsp
