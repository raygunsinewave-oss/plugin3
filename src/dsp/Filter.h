// Multi-mode filter: zero-delay-feedback SVF / ladder / diode / acid, plus comb,
// formant, and phaser types. Stereo, per-voice, real-time safe (comb buffers are
// allocated in prepare()).
#pragma once

#include "DspCommon.h"

#include <vector>

namespace mbx::dsp
{
/** APPEND ONLY. */
enum class FilterType : int
{
    LP12 = 0, LP24, HP12, HP24, BP12, BP24, Notch12, Notch24,
    Ladder, Diode, CombPlus, CombMinus, Formant, Phaser, Acid
};

inline constexpr const char* kFilterTypeNames[] = {
    "SVF LP 12", "SVF LP 24", "SVF HP 12", "SVF HP 24", "SVF BP 12", "SVF BP 24", "SVF Notch 12", "SVF Notch 24",
    "Ladder 24", "Diode 24", "Comb +", "Comb -", "Formant", "Phaser", "Acid 18"
};

struct FilterSettings
{
    bool on          = true;
    FilterType type  = FilterType::LP24;
    float resonance  = 0.2f; // 0..1 (self-oscillates near 1)
    float drive      = 0.0f; // 0..1
    float mix        = 1.0f; // 0..1
    float fmAmount   = 0.0f; // 0..1, audio-rate cutoff FM depth (+-4 octaves)
    int satModel     = 0;    // drive character: 0 tanh, 1 tube (asymmetric), 2 diode (hard knee), 3 tape (soft)
};

class FilterUnit
{
public:
    /** Allocates comb delay lines for the highest sample rate that will be used. NOT real-time safe. */
    void prepare (float maxSampleRate)
    {
        const size_t len = (size_t) (maxSampleRate / 20.0f) + 4;
        for (auto& d : comb)
            d.assign (len, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : ch)
            c = Channel {};
        for (auto& d : comb)
            std::fill (d.begin(), d.end(), 0.0f);
    }

    /** Processes L (and R if non-null) in place. Cutoff glides linearly from hzStart to hzEnd
        over the chunk. fm (may be null) is an audio-rate signal in -1..1. */
    void process (const FilterSettings& s, float hzStart, float hzEnd, const float* fm,
                  float* L, float* R, int n, float sampleRate) noexcept
    {
        if (! s.on || n <= 0)
            return;
        if ((int) s.type <= (int) FilterType::Notch24)
        {
            processSvf (s, hzStart, hzEnd, fm, L, R, n, sampleRate);
            return;
        }
        processChannel (0, s, hzStart, hzEnd, fm, L, n, sampleRate);
        if (R != nullptr)
            processChannel (1, s, hzStart, hzEnd, fm, R, n, sampleRate);
    }

private:
    struct Svf { float ic1 = 0, ic2 = 0; };
    struct Channel
    {
        Svf a, b, f1, f2, f3;
        float s[4] {};         // ladder / diode / acid stages
        float hpLp = 0.0f;     // acid feedback high-pass state
        float ap[6] {};        // phaser allpass states
        float phaserFb = 0.0f;
        int combWrite = 0;
    };

    static inline void svf (Svf& st, float v0, float g, float k, float& lp, float& bp, float& hp) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = v0 - st.ic2;
        const float v1 = a1 * st.ic1 + a2 * v3;
        const float v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
        st.ic1 = softLimit (2.0f * v1 - st.ic1); // bounded self-oscillation
        st.ic2 = 2.0f * v2 - st.ic2;
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }

    /** All SVF modes, both channels in one pass, specialised per mode so the inner loop is branch-free.
        Coefficients are recomputed per sample only while the cutoff moves or FM is active. */
    void processSvf (const FilterSettings& s, float hz0, float hz1, const float* fm, float* L, float* R, int n, float sr) noexcept
    {
        switch (s.type)
        {
            case FilterType::LP12:    svfLoop<0, false> (s, hz0, hz1, fm, L, R, n, sr); break;
            case FilterType::LP24:    svfLoop<0, true>  (s, hz0, hz1, fm, L, R, n, sr); break;
            case FilterType::HP12:    svfLoop<1, false> (s, hz0, hz1, fm, L, R, n, sr); break;
            case FilterType::HP24:    svfLoop<1, true>  (s, hz0, hz1, fm, L, R, n, sr); break;
            case FilterType::BP12:    svfLoop<2, false> (s, hz0, hz1, fm, L, R, n, sr); break;
            case FilterType::BP24:    svfLoop<2, true>  (s, hz0, hz1, fm, L, R, n, sr); break;
            case FilterType::Notch12: svfLoop<3, false> (s, hz0, hz1, fm, L, R, n, sr); break;
            case FilterType::Notch24:
            case FilterType::Ladder: case FilterType::Diode: case FilterType::CombPlus: case FilterType::CombMinus:
            case FilterType::Formant: case FilterType::Phaser: case FilterType::Acid:
            default:                  svfLoop<3, true>  (s, hz0, hz1, fm, L, R, n, sr); break;
        }
    }

    struct SvfCoefs { float a1, a2, a3; };
    static inline SvfCoefs svfCoefs (float g, float k) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        return { a1, g * a1, g * g * a1 };
    }

    template <int Kind>
    static inline float svfTick (Svf& st, float v0, const SvfCoefs& c, float k) noexcept
    {
        const float v3 = v0 - st.ic2;
        const float v1 = c.a1 * st.ic1 + c.a2 * v3;
        const float v2 = st.ic2 + c.a2 * st.ic1 + c.a3 * v3;
        st.ic1 = softLimit (2.0f * v1 - st.ic1);
        st.ic2 = 2.0f * v2 - st.ic2;
        if constexpr (Kind == 0) return v2;
        else if constexpr (Kind == 1) return v0 - k * v1 - v2;
        else if constexpr (Kind == 2) return v1 * k;
        else return v0 - k * v1;
    }

    template <int Kind, bool Four>
    void svfLoop (const FilterSettings& s, float hz0, float hz1, const float* fm, float* L, float* R, int n, float sr) noexcept
    {
        const float nyq = 0.45f * sr;
        hz0 = std::clamp (hz0, 15.0f, nyq);
        hz1 = std::clamp (hz1, 15.0f, nyq);
        const bool useFm  = fm != nullptr && s.fmAmount > 0.0f;
        const bool moving = useFm || std::abs (hz1 - hz0) > 1.0e-3f * hz0;
        const float res   = std::clamp (s.resonance, 0.0f, 1.0f);
        const float k     = 2.0f - 2.02f * res;
        const float kPick = std::max (k, 0.05f);
        constexpr float kFlat = 1.41421356f;
        const float mix   = std::clamp (s.mix, 0.0f, 1.0f);
        const bool wetOnly = mix >= 0.9999f;
        const bool drive  = s.drive > 0.001f;
        const float g0 = std::tan (kPi * hz0 / sr), g1 = std::tan (kPi * hz1 / sr);
        const float invN = 1.0f / (float) n;

        SvfCoefs c1 = svfCoefs (g0, k), c2 = svfCoefs (g0, kFlat);
        auto& A = ch[0];
        auto& B = ch[1];
        auto one = [&] (Channel& C, float x) noexcept {
            const float in = drive ? preDrive (x, s.drive, s.satModel) : x;
            float y = svfTick<Kind> (C.a, in, c1, kPick);
            if constexpr (Four) y = svfTick<Kind> (C.b, y, c2, kFlat);
            return wetOnly ? y : x + (y - x) * mix;
        };

        for (int i = 0; i < n; ++i)
        {
            if (moving)
            {
                const float t = (float) i * invN;
                float g;
                if (useFm)
                {
                    const float hz = std::clamp ((hz0 + (hz1 - hz0) * t) * std::exp2 (fm[i] * s.fmAmount * 4.0f), 15.0f, nyq);
                    g = fastTan (kPi * hz / sr);
                }
                else
                    g = g0 + (g1 - g0) * t;
                c1 = svfCoefs (g, k);
                if constexpr (Four) c2 = svfCoefs (g, kFlat);
            }
            L[i] = one (A, L[i]);
            if (R != nullptr)
                R[i] = one (B, R[i]);
        }
    }

    static inline float preDrive (float x, float drive, int model = 0) noexcept
    {
        if (drive <= 0.001f)
            return x;
        const float d = 1.0f + 15.0f * drive * drive;
        const float y = x * d;
        float s;
        switch (model)
        {
            case 1:  s = fastTanh (y + 0.25f) - 0.2449f; break;                 // tube: asymmetric, even harmonics
            case 2:  s = std::clamp (y * 1.2f, -1.0f, 1.0f) * 0.92f + 0.08f * fastTanh (y); break; // diode: hard knee
            case 3:  s = y / (1.0f + std::abs (y)) * 1.3f; break;              // tape: very soft
            default: s = fastTanh (y); break;
        }
        return s / std::sqrt (d);
    }

    void processChannel (int c, const FilterSettings& s, float hz0, float hz1, const float* fm, float* x, int n,
                         float sr) noexcept
    {
        auto& st = ch[(size_t) c];
        const float nyq  = 0.45f * sr;
        hz0 = std::clamp (hz0, 15.0f, nyq);
        hz1 = std::clamp (hz1, 15.0f, nyq);
        const float g0 = std::tan (kPi * hz0 / sr), g1 = std::tan (kPi * hz1 / sr);
        const float invN = 1.0f / (float) n;
        const bool useFm = fm != nullptr && s.fmAmount > 0.0f;
        const float res  = std::clamp (s.resonance, 0.0f, 1.0f);
        const float mix  = std::clamp (s.mix, 0.0f, 1.0f);

        auto gainAt = [&] (int i) noexcept {
            const float t = (float) i * invN;
            if (! useFm)
                return g0 + (g1 - g0) * t;
            const float hz = std::clamp ((hz0 + (hz1 - hz0) * t) * std::exp2 (fm[i] * s.fmAmount * 4.0f), 15.0f, nyq);
            return fastTan (kPi * hz / sr);
        };
        auto hzAt = [&] (int i) noexcept {
            const float t = (float) i * invN;
            float hz = hz0 + (hz1 - hz0) * t;
            if (useFm)
                hz *= std::exp2 (fm[i] * s.fmAmount * 4.0f);
            return std::clamp (hz, 15.0f, nyq);
        };

        switch (s.type)
        {
            case FilterType::LP12: case FilterType::HP12: case FilterType::BP12: case FilterType::Notch12:
            case FilterType::LP24: case FilterType::HP24: case FilterType::BP24: case FilterType::Notch24:
            {
                const int kind  = (int) s.type / 2;          // 0 LP, 1 HP, 2 BP, 3 Notch
                const bool four = ((int) s.type & 1) != 0;   // odd entries are the 24 dB versions
                const float k   = 2.0f - 2.02f * res;        // resonant stage (k < 0 => self-oscillation)
                constexpr float kFlat = 1.41421356f;         // Butterworth second stage
                for (int i = 0; i < n; ++i)
                {
                    const float g  = gainAt (i);
                    const float in = preDrive (x[i], s.drive, s.satModel);
                    float lp, bp, hp;
                    svf (st.a, in, g, k, lp, bp, hp);
                    float y = pick (kind, lp, bp, hp, in, std::max (k, 0.05f));
                    if (four)
                    {
                        const float in2 = y;
                        svf (st.b, in2, g, kFlat, lp, bp, hp);
                        y = pick (kind, lp, bp, hp, in2, kFlat);
                    }
                    x[i] = x[i] + (y - x[i]) * mix;
                }
                break;
            }

            case FilterType::Ladder:
            case FilterType::Diode:
            {
                const bool diode = s.type == FilterType::Diode;
                const float k = res * (diode ? 4.6f : 4.15f);
                const float comp = 1.0f + k * (diode ? 0.45f : 0.35f);
                const float drv  = 1.0f + 6.0f * s.drive * s.drive;
                for (int i = 0; i < n; ++i)
                {
                    const float g = gainAt (i);
                    const float G = g / (1.0f + g);
                    const float b = 1.0f - G;
                    const float S = G * G * G * b * st.s[0] + G * G * b * st.s[1] + G * b * st.s[2] + b * st.s[3];
                    const float G4 = G * G * G * G;
                    const float in = x[i] * drv;
                    const float y4est = (G4 * in + S) / (1.0f + k * G4);
                    float u = fastTanh (in - k * y4est + (diode ? 0.08f * y4est * y4est : 0.0f));
                    for (int stage = 0; stage < 4; ++stage)
                    {
                        if (diode && stage > 0)
                            u = fastTanh (u * 1.2f) * 0.8333f;
                        const float v = (u - st.s[stage]) * G;
                        const float y = v + st.s[stage];
                        st.s[stage] = y + v;
                        u = y;
                    }
                    const float y = u * comp / std::sqrt (drv);
                    x[i] = x[i] + (y - x[i]) * mix;
                }
                break;
            }

            case FilterType::Acid:
            {
                const float k   = res * 8.6f; // 3-pole: self-oscillation at k = 8
                const float drv = 1.0f + 10.0f * s.drive * s.drive;
                const float hpCoef = 1.0f - std::exp (-kTwoPi * 120.0f / sr); // feedback HPF (tames bass resonance)
                for (int i = 0; i < n; ++i)
                {
                    const float g = gainAt (i);
                    const float G = g / (1.0f + g);
                    const float b = 1.0f - G;
                    const float S = G * G * b * st.s[0] + G * b * st.s[1] + b * st.s[2];
                    const float G3 = G * G * G;
                    const float in = x[i] * drv;
                    const float y3est = (G3 * in + S) / (1.0f + k * G3);
                    const float fb = y3est - st.hpLp;
                    st.hpLp += (y3est - st.hpLp) * hpCoef;
                    float u = fastTanh (in - k * fb);
                    for (int stage = 0; stage < 3; ++stage)
                    {
                        const float v = (u - st.s[stage]) * G;
                        const float y = v + st.s[stage];
                        st.s[stage] = y + v;
                        u = y;
                    }
                    const float y = u * (1.0f + k * 0.12f) / std::sqrt (drv);
                    x[i] = x[i] + (y - x[i]) * mix;
                }
                break;
            }

            case FilterType::CombPlus:
            case FilterType::CombMinus:
            {
                auto& buf = comb[(size_t) c];
                if (buf.size() < 8)
                    break;
                const int len = (int) buf.size();
                const float fb = (s.type == FilterType::CombPlus ? 1.0f : -1.0f) * res * 0.97f;
                const float norm = 1.0f - std::abs (fb) * 0.5f;
                for (int i = 0; i < n; ++i)
                {
                    const float delay = std::clamp (sr / hzAt (i), 2.0f, (float) len - 3.0f);
                    float rp = (float) st.combWrite - delay;
                    if (rp < 0.0f) rp += (float) len;
                    const int r0 = (int) rp;
                    const float fr = rp - (float) r0;
                    const float d0 = buf[(size_t) r0], d1 = buf[(size_t) ((r0 + 1) % len)];
                    const float delayed = d0 + (d1 - d0) * fr;
                    const float in = preDrive (x[i], s.drive, s.satModel);
                    const float y = in + fb * delayed;
                    buf[(size_t) st.combWrite] = fastTanh (y * 0.5f) * 2.0f; // saturating feedback
                    st.combWrite = (st.combWrite + 1) % len;
                    x[i] = x[i] + (y * norm - x[i]) * mix;
                }
                break;
            }

            case FilterType::Formant:
            {
                // cutoff sweeps vowels a-e-i-o-u; resonance sets Q
                static constexpr float F[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                                   { 570, 840, 2410 }, { 300, 870, 2240 } };
                const float hzMid = std::sqrt (hz0 * hz1);
                const float pos = std::clamp (std::log2 (hzMid / 20.0f) / std::log2 (1000.0f), 0.0f, 1.0f) * 4.0f;
                const int v0 = std::min (3, (int) pos);
                const float fr = pos - (float) v0;
                float g[3];
                for (int f = 0; f < 3; ++f)
                {
                    const float hz = std::min (F[v0][f] + (F[v0 + 1][f] - F[v0][f]) * fr, nyq);
                    g[f] = std::tan (kPi * hz / sr);
                }
                const float q = 2.0f + res * 16.0f;
                const float k = 1.0f / q;
                for (int i = 0; i < n; ++i)
                {
                    const float in = preDrive (x[i], s.drive, s.satModel);
                    float lp, bp, hp, y = 0.0f;
                    svf (st.f1, in, g[0], k, lp, bp, hp); y += bp * k * 1.0f;
                    svf (st.f2, in, g[1], k, lp, bp, hp); y += bp * k * 0.55f;
                    svf (st.f3, in, g[2], k, lp, bp, hp); y += bp * k * 0.3f;
                    y *= 2.2f;
                    x[i] = x[i] + (y - x[i]) * mix;
                }
                break;
            }

            case FilterType::Phaser:
            default:
            {
                const float fbAmt = res * 0.92f;
                for (int i = 0; i < n; ++i)
                {
                    const float g = gainAt (i);
                    const float G = g / (1.0f + g);
                    float u = preDrive (x[i], s.drive, s.satModel) + fbAmt * st.phaserFb;
                    for (int a = 0; a < 6; ++a)
                    {
                        const float v = (u - st.ap[a]) * G;
                        const float lp = v + st.ap[a];
                        st.ap[a] = lp + v;
                        u = 2.0f * lp - u; // first-order allpass
                    }
                    st.phaserFb = softLimit (u);
                    const float y = 0.5f * (x[i] + u);
                    x[i] = x[i] + (y - x[i]) * mix;
                }
                break;
            }
        }
    }

    static inline float pick (int kind, float lp, float bp, float hp, float in, float k) noexcept
    {
        switch (kind)
        {
            case 0:  return lp;
            case 1:  return hp;
            case 2:  return bp * k; // constant peak gain
            default: return in - k * bp; // notch
        }
    }

    std::array<Channel, 2> ch {};
    std::array<std::vector<float>, 2> comb;
};

} // namespace mbx::dsp
