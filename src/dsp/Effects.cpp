// All rack effects. Each class maps the six normalised parameters to musical ranges.
#include "FxCommon.h"
#include "Modulation.h" // sync division tables

namespace mbx::dsp::fx
{
namespace
{
    constexpr float kHalfPi = kPi * 0.5f;

    /** Smooths a block-rate value per sample (linear ramp across the block). */
    struct Ramp
    {
        float cur = 0.0f;
        bool init = false;
        float start (float target, int n, float& step) noexcept
        {
            if (! init) { cur = target; init = true; }
            step = (target - cur) / (float) std::max (1, n);
            const float s = cur;
            cur = target;
            return s;
        }
    };

    float beatsForDivision (int idx) noexcept
    {
        return kSyncDivisionBeats[(size_t) std::clamp (idx, 0, (int) std::size (kSyncDivisionBeats) - 1)];
    }

    //==========================================================================
    class Distortion final : public FxUnit
    {
    public:
        Distortion() : FxUnit (FxType::Distortion) {}
        void prepare (float sr, int) override { rate = sr; reset(); }
        void reset() noexcept override { tone[0] = tone[1] = {}; held[0] = held[1] = 0.0f; holdCount = 0.0f; }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float drive = dbToGain (p[0] * 42.0f);
            const int mode = stepped (p[1], 5); // tube, tape, fold, hard, crush
            const float toneC = OnePole::coefFor (expo (p[2], 600.0f, 20000.0f), rate);
            const float bias = (p[3] - 0.5f) * 0.8f;
            const float bits = lin (p[4], 16.0f, 2.0f);
            const float levels = std::exp2 (bits) * 0.5f;
            const float dsFactor = 1.0f + p[4] * p[4] * 31.0f;
            const float comp = 1.0f / std::pow (drive, mode == 3 ? 0.6f : 0.45f);
            const float out = dbToGain ((p[5] - 0.5f) * 24.0f);
            const float biasOff = std::tanh (bias);
            float* ch[2] = { L, R };
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float x = ch[c][i] * drive;
                    float y;
                    switch (mode)
                    {
                        case 0:  y = std::tanh (x + bias) - biasOff; break;                         // tube (asymmetric)
                        case 1:  y = (x + bias * 0.5f) / (1.0f + std::abs (x + bias * 0.5f)) * 1.4f; break; // tape-ish
                        case 2:  y = std::sin (std::clamp (x + bias, -40.0f, 40.0f) * kHalfPi); break; // wavefolder
                        case 3:  y = std::clamp (x + bias, -1.0f, 1.0f); break;                     // hard clip
                        default:
                        {
                            if (c == 0 && (holdCount += 1.0f) >= dsFactor) holdCount -= dsFactor; // sample-rate reduction
                            if (holdCount < 1.0f) held[c] = std::round (std::clamp (x * 0.25f + bias, -1.0f, 1.0f) * levels) / levels;
                            y = held[c];
                            break;
                        }
                    }
                    y = tone[c].lp (y, toneC);
                    ch[c][i] = y * comp * (mode == 4 ? 1.0f / comp * 0.9f : 1.0f) * out;
                }
            activity = drive;
        }

    private:
        float rate = 48000.0f, holdCount = 0.0f;
        OnePole tone[2];
        float held[2] {};
    };

    //==========================================================================
    /** Three-band upward + downward compressor (OTT-style). */
    class Ott final : public FxUnit
    {
    public:
        Ott() : FxUnit (FxType::OTT) {}
        void prepare (float sr, int) override { rate = sr; reset(); }
        void reset() noexcept override
        {
            for (auto& b : xo) b.reset();
            for (auto& g : gain) g = 1.0f;
            for (auto& e : env) e.env = 0.0f;
        }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float depth = p[0];
            const float timeScale = expo (p[1], 0.2f, 5.0f);
            const float up = p[2], down = p[3];
            const float tilt = bip (p[4]) * 6.0f;
            const float out = dbToGain ((p[5] - 0.5f) * 24.0f);
            for (int c = 0; c < 2; ++c)
            {
                xo[c * 4 + 0].set (Biquad::Kind::LowPass, 120.0f, 0.7071f, 0, rate);
                xo[c * 4 + 1].set (Biquad::Kind::HighPass, 120.0f, 0.7071f, 0, rate);
                xo[c * 4 + 2].set (Biquad::Kind::LowPass, 2500.0f, 0.7071f, 0, rate);
                xo[c * 4 + 3].set (Biquad::Kind::HighPass, 2500.0f, 0.7071f, 0, rate);
            }
            const float bandTrim[3] = { dbToGain (-tilt), 1.0f, dbToGain (tilt) };
            for (int b = 0; b < 3; ++b)
                env[b].setTimes (3.0f * timeScale * (b == 0 ? 2.0f : 1.0f), 80.0f * timeScale, rate);
            const float gs = 1.0f - std::exp (-1.0f / (0.002f * rate));
            float grSum = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                float bandL[3], bandR[3];
                const float xl = L[i], xr = R[i];
                bandL[0] = xo[0].process (xl); const float hl = xo[1].process (xl);
                bandL[1] = xo[2].process (hl); bandL[2] = xo[3].process (hl);
                bandR[0] = xo[4].process (xr); const float hr = xo[5].process (xr);
                bandR[1] = xo[6].process (hr); bandR[2] = xo[7].process (hr);
                float yl = 0.0f, yr = 0.0f;
                for (int b = 0; b < 3; ++b)
                {
                    const float lvl = gainToDb (env[b].process (std::max (std::abs (bandL[b]), std::abs (bandR[b]))));
                    float gDb = 0.0f;
                    if (lvl > -24.0f) gDb -= (lvl + 24.0f) * (1.0f - 1.0f / 66.0f) * down;
                    if (lvl < -40.0f && lvl > -90.0f) gDb += std::min ((-40.0f - lvl) * 0.75f, 30.0f) * up;
                    gain[b] += (dbToGain (gDb) - gain[b]) * gs;
                    grSum += gDb;
                    yl += bandL[b] * gain[b] * bandTrim[b];
                    yr += bandR[b] * gain[b] * bandTrim[b];
                }
                L[i] = (xl + (yl - xl) * depth) * out;
                R[i] = (xr + (yr - xr) * depth) * out;
            }
            activity = grSum / (float) (3 * std::max (1, n));
        }

    private:
        float rate = 48000.0f;
        Biquad xo[8];
        EnvFollower env[3];
        float gain[3] { 1, 1, 1 };
    };

    //==========================================================================
    class Compressor final : public FxUnit
    {
    public:
        Compressor() : FxUnit (FxType::Compressor) {}
        void prepare (float sr, int) override { rate = sr; reset(); }
        void reset() noexcept override { envDb = -120.0f; }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float thr = lin (p[0], -60.0f, 0.0f);
            const float ratio = expo (p[1], 1.0f, 20.0f);
            const float att = 1.0f - std::exp (-1.0f / (expo (p[2], 0.1f, 100.0f) * 0.001f * rate));
            const float rel = 1.0f - std::exp (-1.0f / (expo (p[3], 10.0f, 1000.0f) * 0.001f * rate));
            const float knee = p[4] * 18.0f;
            const float makeup = dbToGain (p[5] * 24.0f);
            float maxGr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float lvl = gainToDb (std::max (std::abs (L[i]), std::abs (R[i])));
                envDb += (lvl - envDb) * (lvl > envDb ? att : rel);
                const float over = envDb - thr;
                float gr = 0.0f;
                if (knee > 0.0f && std::abs (over) <= knee * 0.5f)
                    gr = (1.0f / ratio - 1.0f) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
                else if (over > 0.0f)
                    gr = (1.0f / ratio - 1.0f) * over;
                const float g = dbToGain (gr) * makeup;
                L[i] *= g;
                R[i] *= g;
                maxGr = std::min (maxGr, gr);
            }
            activity = maxGr;
        }

    private:
        float rate = 48000.0f, envDb = -120.0f;
    };

    //==========================================================================
    class Transient final : public FxUnit
    {
    public:
        Transient() : FxUnit (FxType::Transient) {}
        void prepare (float sr, int) override { rate = sr; reset(); }
        void reset() noexcept override { fast.env = slow.env = 0.0f; }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float attackAmt = bip (p[0]), sustainAmt = bip (p[1]);
            const float speed = expo (p[2], 0.4f, 2.5f);
            fast.setTimes (0.5f * speed, 40.0f * speed, rate);
            slow.setTimes (25.0f * speed, 250.0f * speed, rate);
            const float out = dbToGain ((p[3] - 0.5f) * 24.0f);
            for (int i = 0; i < n; ++i)
            {
                const float x = std::max (std::abs (L[i]), std::abs (R[i]));
                const float d = gainToDb (fast.process (x)) - gainToDb (slow.process (x));
                const float gDb = std::clamp (d > 0.0f ? attackAmt * d * 1.5f : sustainAmt * -d * 1.5f, -24.0f, 18.0f);
                const float g = dbToGain (gDb) * out;
                L[i] *= g;
                R[i] *= g;
            }
        }

    private:
        float rate = 48000.0f;
        EnvFollower fast, slow;
    };

    //==========================================================================
    /** Shared engine for chorus / flanger / ensemble: modulated delay taps. */
    class ModDelay : public FxUnit
    {
    public:
        explicit ModDelay (FxType t) : FxUnit (t) {}
        void prepare (float sr, int) override
        {
            rate = sr;
            for (auto& d : line) d.allocate ((int) (sr * 0.06f));
            reset();
        }
        void reset() noexcept override
        {
            for (auto& d : line) d.clear();
            fb[0] = fb[1] = 0.0f;
            tone[0] = tone[1] = {};
        }

    protected:
        float rate = 48000.0f;
        DelayLine line[2];
        float fb[2] {};
        double phase = 0.0, phase2 = 0.0;
        OnePole tone[2];
    };

    class Chorus final : public ModDelay
    {
    public:
        Chorus() : ModDelay (FxType::Chorus) {}
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float hz = expo (p[0], 0.05f, 6.0f);
            const float depthMs = p[1] * 6.0f;
            const float baseMs = lin (p[2], 5.0f, 30.0f);
            const float feedback = p[3] * 0.7f;
            const int voices = 1 + stepped (p[4], 4);
            const float width = p[5];
            const auto& sine = SineTable::get();
            const float inc = hz / rate;
            for (int i = 0; i < n; ++i)
            {
                line[0].push (L[i] + fb[0] * feedback);
                line[1].push (R[i] + fb[1] * feedback);
                float wl = 0.0f, wr = 0.0f;
                for (int v = 0; v < voices; ++v)
                {
                    const float off = (float) v / (float) voices;
                    const float ml = sine.lookup (wrap01 ((float) phase + off));
                    const float mr = sine.lookup (wrap01 ((float) phase + off + 0.25f * width));
                    wl += line[0].read ((baseMs + depthMs * (0.5f + 0.5f * ml)) * 0.001f * rate);
                    wr += line[1].read ((baseMs + depthMs * (0.5f + 0.5f * mr)) * 0.001f * rate);
                }
                const float norm = 1.0f / std::sqrt ((float) voices);
                wl *= norm; wr *= norm;
                fb[0] = wl; fb[1] = wr;
                const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
                L[i] = mid + side;
                R[i] = mid - side;
                phase += inc;
                if (phase >= 1.0) phase -= 1.0;
            }
            activity = (float) phase;
        }
    };

    class Flanger final : public ModDelay
    {
    public:
        Flanger() : ModDelay (FxType::Flanger) {}
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float hz = expo (p[0], 0.02f, 5.0f);
            const float depthMs = p[1] * 5.0f;
            const float baseMs = lin (p[2], 0.2f, 6.0f);
            const float feedback = bip (p[3]) * 0.95f;
            const float spread = p[5] * 0.5f;
            const auto& sine = SineTable::get();
            for (int i = 0; i < n; ++i)
            {
                const float ml = 0.5f + 0.5f * sine.lookup ((float) phase);
                const float mr = 0.5f + 0.5f * sine.lookup (wrap01 ((float) phase + spread));
                const float dl = line[0].read ((baseMs + depthMs * ml) * 0.001f * rate);
                const float dr = line[1].read ((baseMs + depthMs * mr) * 0.001f * rate);
                line[0].push (softLimit (L[i] + dl * feedback));
                line[1].push (softLimit (R[i] + dr * feedback));
                L[i] = 0.7071f * (L[i] + dl);
                R[i] = 0.7071f * (R[i] + dr);
                phase += hz / rate;
                if (phase >= 1.0) phase -= 1.0;
            }
            activity = (float) phase;
        }
    };

    class Ensemble final : public ModDelay
    {
    public:
        Ensemble() : ModDelay (FxType::Ensemble) {}
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float slowHz = lin (p[0], 0.2f, 1.5f), fastHz = slowHz * 9.0f;
            const float depth = p[1];
            const float toneC = OnePole::coefFor (expo (p[2], 2000.0f, 16000.0f), rate);
            const float width = p[3];
            const auto& sine = SineTable::get();
            for (int i = 0; i < n; ++i)
            {
                const float mono = 0.5f * (L[i] + R[i]);
                line[0].push (mono);
                float taps[3];
                for (int t = 0; t < 3; ++t)
                {
                    const float off = (float) t / 3.0f;
                    const float m = 0.7f * sine.lookup (wrap01 ((float) phase + off)) + 0.3f * sine.lookup (wrap01 ((float) phase2 + off));
                    taps[t] = line[0].read ((10.0f + depth * 4.0f * (1.0f + m)) * 0.001f * rate);
                }
                const float wl = tone[0].lp (taps[0] + 0.5f * taps[1], toneC) * 0.8f;
                const float wr = tone[1].lp (taps[2] + 0.5f * taps[1], toneC) * 0.8f;
                const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width * 1.5f;
                L[i] = mid + side;
                R[i] = mid - side;
                phase += slowHz / rate;  if (phase >= 1.0) phase -= 1.0;
                phase2 += fastHz / rate; if (phase2 >= 1.0) phase2 -= 1.0;
            }
            activity = (float) phase;
        }
    };

    //==========================================================================
    class Phaser final : public FxUnit
    {
    public:
        Phaser() : FxUnit (FxType::Phaser) {}
        void prepare (float sr, int) override { rate = sr; reset(); }
        void reset() noexcept override { for (auto& c : ap) for (auto& s : c) s = 0.0f; fb[0] = fb[1] = 0.0f; }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float hz = expo (p[0], 0.02f, 6.0f);
            const float depth = p[1];
            const int stages = 2 + 2 * stepped (p[2], 6); // 2..12
            const float centre = expo (p[3], 200.0f, 4000.0f);
            const float feedback = bip (p[4]) * 0.9f;
            const float spread = p[5] * 0.5f;
            const auto& sine = SineTable::get();
            float* ch[2] = { L, R };
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < 2; ++c)
                {
                    const float m = sine.lookup (wrap01 ((float) phase + (c == 1 ? spread : 0.0f)));
                    const float f = std::clamp (centre * std::exp2 (m * depth * 2.0f), 30.0f, rate * 0.45f);
                    const float g = fastTan (kPi * f / rate);
                    const float G = g / (1.0f + g);
                    float u = ch[c][i] + fb[c] * feedback;
                    for (int s = 0; s < stages; ++s)
                    {
                        const float v = (u - ap[c][s]) * G;
                        const float lp = v + ap[c][s];
                        ap[c][s] = lp + v;
                        u = 2.0f * lp - u;
                    }
                    fb[c] = softLimit (u);
                    ch[c][i] = 0.5f * (ch[c][i] + u);
                }
                phase += hz / rate;
                if (phase >= 1.0) phase -= 1.0;
            }
            activity = (float) phase;
        }

    private:
        float rate = 48000.0f;
        float ap[2][12] {};
        float fb[2] {};
        double phase = 0.0;
    };

    //==========================================================================
    class Delay final : public FxUnit
    {
    public:
        Delay() : FxUnit (FxType::Delay) {}
        void prepare (float sr, int) override
        {
            rate = sr;
            line[0].allocate ((int) (sr * 4.2f));
            line[1].allocate ((int) (sr * 4.2f));
            reset();
        }
        void reset() noexcept override
        {
            line[0].clear(); line[1].clear();
            tone[0] = tone[1] = {};
            hp[0] = hp[1] = {};
            smoothL = smoothR = -1.0f;
        }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext& ctx) noexcept override
        {
            const bool sync = p[5] >= 0.5f;
            float tL;
            if (sync)
            {
                static constexpr int divs[] = { 2, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }; // 1/32 .. 1 bar
                const int d = divs[stepped (p[0], (int) std::size (divs))];
                tL = beatsForDivision (d) * 60.0f / std::max (ctx.bpm, 20.0f);
            }
            else
                tL = expo (p[0], 0.001f, 2.0f);
            const float spreadRatio = lin (p[1], 0.5f, 1.5f);
            const float tR = std::min (4.0f, tL * spreadRatio);
            const float feedback = p[2] * 1.02f;
            const int mode = stepped (p[3], 3); // 0 mono, 1 stereo, 2 ping-pong
            const float tape = p[4];
            const float toneC = OnePole::coefFor (expo (1.0f - tape * 0.8f, 1500.0f, 16000.0f), rate);
            const float hpC = OnePole::coefFor (80.0f, rate);
            const auto& sine = SineTable::get();
            const float tgtL = tL * rate, tgtR = (mode == 0 ? tL : tR) * rate;
            if (smoothL < 0.0f) { smoothL = tgtL; smoothR = tgtR; }
            const float glide = 1.0f - std::exp (-1.0f / (0.08f * rate)); // tape-like time glide, no zipper
            for (int i = 0; i < n; ++i)
            {
                smoothL += (tgtL - smoothL) * glide;
                smoothR += (tgtR - smoothR) * glide;
                const float wow = tape * (0.0025f * sine.lookup ((float) wowPhase) + 0.0006f * sine.lookup ((float) flutterPhase)) * rate;
                float dl = line[0].read (smoothL + wow);
                float dr = line[1].read (smoothR + wow * 0.9f);
                dl = tone[0].lp (dl, toneC); dr = tone[1].lp (dr, toneC);
                dl -= hp[0].lp (dl, hpC);    dr -= hp[1].lp (dr, hpC);
                if (tape > 0.01f) { dl = fastTanh (dl * (1.0f + tape)) / (1.0f + tape * 0.5f); dr = fastTanh (dr * (1.0f + tape)) / (1.0f + tape * 0.5f); }
                const float inL = L[i], inR = R[i];
                if (mode == 2) // ping-pong: input enters left, feedback crosses channels
                {
                    line[0].push (softLimit (0.5f * (inL + inR) + dr * feedback));
                    line[1].push (softLimit (dl * feedback));
                }
                else if (mode == 0)
                {
                    const float m = 0.5f * (inL + inR);
                    line[0].push (softLimit (m + dl * feedback));
                    line[1].push (softLimit (m + dl * feedback));
                }
                else
                {
                    line[0].push (softLimit (inL + dl * feedback));
                    line[1].push (softLimit (inR + dr * feedback));
                }
                L[i] = dl;
                R[i] = dr;
                wowPhase += 0.6 / rate;     if (wowPhase >= 1.0) wowPhase -= 1.0;
                flutterPhase += 7.0 / rate; if (flutterPhase >= 1.0) flutterPhase -= 1.0;
            }
            activity = tL;
        }

    private:
        float rate = 48000.0f, smoothL = -1.0f, smoothR = -1.0f;
        DelayLine line[2];
        OnePole tone[2], hp[2];
        double wowPhase = 0.0, flutterPhase = 0.0;
    };

    //==========================================================================
    /** Delay-line pitch shifter (two crossfaded read heads). */
    struct PitchShifter
    {
        DelayLine line;
        float pos = 0.0f;
        void allocate (int n) { line.allocate (n); }
        void clear() noexcept { line.clear(); pos = 0.0f; }
        float process (float x, float ratio, float windowSamples) noexcept
        {
            line.push (x);
            pos += 1.0f - ratio;
            if (pos < 0.0f) pos += windowSamples;
            if (pos >= windowSamples) pos -= windowSamples;
            const float p2 = std::fmod (pos + windowSamples * 0.5f, windowSamples);
            const float w1 = SineTable::get().lookup (pos / windowSamples * 0.5f);
            const float w2 = SineTable::get().lookup (p2 / windowSamples * 0.5f);
            return line.read (pos) * w1 * w1 + line.read (p2) * w2 * w2;
        }
    };

    /** 8-line feedback delay network with input diffusion, modulation, damping and shimmer. */
    class Reverb final : public FxUnit
    {
    public:
        Reverb() : FxUnit (FxType::Reverb) {}
        void prepare (float sr, int) override
        {
            rate = sr;
            for (auto& d : tank) d.allocate ((int) (sr * 0.2f));
            for (auto& d : diff) d.allocate ((int) (sr * 0.02f));
            pre.allocate ((int) (sr * 0.26f));
            shift.allocate ((int) (sr * 0.1f));
            reset();
        }
        void reset() noexcept override
        {
            for (auto& d : tank) d.clear();
            for (auto& d : diff) d.clear();
            pre.clear();
            shift.clear();
            for (auto& s : state) s = 0.0f;
            for (auto& d : damp) d = {};
        }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            static constexpr float baseMs[8] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.3f, 67.1f, 73.3f };
            static constexpr float diffMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
            const int kind = stepped (p[4], 4); // hall, plate, room, shimmer
            const float typeScale = kind == 1 ? 0.55f : kind == 2 ? 0.32f : 1.0f;
            const float size = lin (p[0], 0.35f, 2.2f) * typeScale;
            const float rt60 = expo (p[1], 0.2f, 20.0f);
            const float dampC = OnePole::coefFor (expo (1.0f - p[2], 1500.0f, 18000.0f), rate);
            const float preSamples = p[3] * 0.25f * rate;
            const float width = p[5];
            const float modDepth = (kind == 0 || kind == 3) ? 0.0012f * rate : 0.0003f * rate;
            float len[8], g[8];
            for (int k = 0; k < 8; ++k)
            {
                len[k] = std::min (baseMs[k] * size * 0.001f * rate, (float) tank[k].size() - modDepth - 4.0f);
                g[k] = std::pow (10.0f, -3.0f * len[k] / (rt60 * rate));
            }
            const auto& sine = SineTable::get();
            for (int i = 0; i < n; ++i)
            {
                pre.push (0.5f * (L[i] + R[i]));
                float x = pre.read (preSamples);
                const int nd = kind == 1 ? 4 : 2; // plates get denser diffusion
                for (int d = 0; d < nd; ++d)
                {
                    const float dl = diff[d].read (diffMs[d] * 0.001f * rate);
                    const float v = x + 0.6f * dl;
                    diff[d].push (v);
                    x = dl - 0.6f * v;
                }
                float out[8];
                for (int k = 0; k < 8; ++k)
                {
                    const float m = modDepth * sine.lookup (wrap01 ((float) modPhase + (float) k * 0.125f));
                    out[k] = damp[k].lp (tank[k].read (len[k] + m), dampC);
                }
                // Householder feedback matrix
                float sum = 0.0f;
                for (float v : out) sum += v;
                sum *= 0.25f;
                float shimmer = 0.0f;
                if (kind == 3)
                    shimmer = shift.process (0.5f * (out[0] + out[5]), 2.0f, 0.05f * rate) * 0.35f;
                for (int k = 0; k < 8; ++k)
                    tank[k].push (softLimit ((out[k] - sum) * g[k] + x * (k % 2 == 0 ? 0.6f : -0.6f) + shimmer));
                const float wl = 0.6f * (out[0] - out[2] + out[4] - out[6]);
                const float wr = 0.6f * (out[1] - out[3] + out[5] - out[7]);
                const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
                L[i] = mid + side;
                R[i] = mid - side;
                modPhase += 0.3 / rate;
                if (modPhase >= 1.0) modPhase -= 1.0;
            }
            activity = rt60;
        }

    private:
        float rate = 48000.0f;
        DelayLine tank[8], diff[4], pre;
        PitchShifter shift;
        OnePole damp[8];
        float state[8] {};
        double modPhase = 0.0;
    };

    //==========================================================================
    class Stutter final : public FxUnit
    {
    public:
        Stutter() : FxUnit (FxType::Stutter) {}
        void prepare (float sr, int) override
        {
            rate = sr;
            line[0].allocate ((int) (sr * 4.5f));
            line[1].allocate ((int) (sr * 4.5f));
            reset();
        }
        void reset() noexcept override
        {
            line[0].clear(); line[1].clear();
            engaged = false; playPos = 0.0f; xfade = 0.0f;
            for (auto& g : grains) g.active = false;
        }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext& ctx) noexcept override
        {
            const bool freeze = p[0] >= 0.5f;
            static constexpr int divs[] = { 2, 5, 8, 11, 14, 16 }; // 1/32, 1/16, 1/8, 1/4, 1/2, 1 bar
            const float loopLen = std::min (4.0f * rate, beatsForDivision (divs[stepped (p[1], 6)]) * 60.0f / std::max (ctx.bpm, 20.0f) * rate);
            const bool wantOn = p[2] > 0.5f; // strictly above centre: a neutral 0.5 must not engage
            const float pitch = std::exp2 (bip (p[3]));
            const float grainLen = lin (p[4], 0.02f, 0.25f) * rate;
            const float spray = p[5];
            if (wantOn && ! engaged) { engaged = true; playPos = 0.0f; frozenLen = loopLen; }
            if (! wantOn && engaged) engaged = false;
            const float fadeStep = 1.0f / (0.004f * rate);
            for (int i = 0; i < n; ++i)
            {
                if (! engaged || xfade < 1.0f) { line[0].push (L[i]); line[1].push (R[i]); }
                xfade = std::clamp (xfade + (engaged ? fadeStep : -fadeStep), 0.0f, 1.0f);
                if (xfade <= 0.0f) continue;
                float wl = 0.0f, wr = 0.0f;
                if (! freeze)
                {
                    // loop the captured window (behind the write head)
                    const float back = frozenLen - playPos;
                    const float edge = std::min ({ playPos, frozenLen - playPos, 0.003f * rate }) / (0.003f * rate);
                    wl = line[0].read (back) * edge;
                    wr = line[1].read (back) * edge;
                    playPos += pitch;
                    if (playPos >= frozenLen) playPos -= frozenLen;
                }
                else
                {
                    if (++spawn >= grainLen * 0.25f)
                    {
                        spawn = 0;
                        for (auto& g : grains)
                            if (! g.active)
                            {
                                g = { true, 0.0f, (0.1f + rng.uniform() * spray * 0.8f) * std::min (frozenLen, 2.0f * rate) + grainLen };
                                break;
                            }
                    }
                    for (auto& g : grains)
                    {
                        if (! g.active) continue;
                        const float w = SineTable::get().lookup (g.age / grainLen * 0.5f);
                        wl += line[0].read (g.start - g.age * pitch) * w * w * 0.5f;
                        wr += line[1].read (g.start - g.age * pitch) * w * w * 0.5f;
                        if ((g.age += 1.0f) >= grainLen) g.active = false;
                    }
                }
                L[i] += (wl - L[i]) * xfade;
                R[i] += (wr - R[i]) * xfade;
            }
            activity = engaged ? 1.0f : 0.0f;
        }

    private:
        struct Grain { bool active = false; float age = 0.0f, start = 0.0f; };
        float rate = 48000.0f, playPos = 0.0f, frozenLen = 1.0f, xfade = 0.0f;
        int spawn = 0;
        bool engaged = false;
        DelayLine line[2];
        std::array<Grain, 8> grains {};
        Rng rng { 99 };
    };

    //==========================================================================
    class Formant final : public FxUnit
    {
    public:
        Formant() : FxUnit (FxType::Formant) {}
        void prepare (float sr, int) override { rate = sr; reset(); }
        void reset() noexcept override { for (auto& b : bp) b.reset(); }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            static constexpr float F[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
            static constexpr float G[3] = { 1.0f, 0.6f, 0.35f };
            const float lfo = p[5] * SineTable::get().lookup ((float) phase);
            phase += expo (p[4], 0.05f, 8.0f) * (float) n / rate;
            phase -= std::floor (phase);
            const float pos = std::clamp (p[0] + lfo * 0.5f, 0.0f, 1.0f) * 4.0f;
            const int v0 = std::min (3, (int) pos);
            const float fr = pos - (float) v0;
            const float shift = expo (p[1], 0.5f, 2.0f);
            const float q = lin (p[2], 2.0f, 20.0f);
            const float drive = 1.0f + p[3] * 8.0f;
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < 3; ++f)
                    bp[c * 3 + f].set (Biquad::Kind::BandPass, (F[v0][f] + (F[v0 + 1][f] - F[v0][f]) * fr) * shift, q, 0, rate);
            float* ch[2] = { L, R };
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float x = fastTanh (ch[c][i] * drive) / std::sqrt (drive);
                    float y = 0.0f;
                    for (int f = 0; f < 3; ++f)
                        y += bp[c * 3 + f].process (x) * G[f];
                    ch[c][i] = y * 2.0f;
                }
            activity = pos / 4.0f;
        }

    private:
        float rate = 48000.0f;
        double phase = 0.0;
        Biquad bp[6];
    };

    //==========================================================================
    class RingMod final : public FxUnit
    {
    public:
        RingMod() : FxUnit (FxType::RingMod) {}
        void prepare (float sr, int) override { rate = sr; reset(); }
        void reset() noexcept override { ph[0] = ph[1] = 0.0; lfo = 0.0; }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float base = expo (p[0], 1.0f, 5000.0f);
            const float lfoHz = expo (p[1], 0.05f, 20.0f);
            const float lfoAmt = p[2];
            const float shape = p[3];
            const float spread = 1.0f + p[4] * 0.1f;
            const auto& sine = SineTable::get();
            float* ch[2] = { L, R };
            for (int i = 0; i < n; ++i)
            {
                const float m = std::exp2 (lfoAmt * 2.0f * sine.lookup ((float) lfo));
                for (int c = 0; c < 2; ++c)
                {
                    const float s = sine.lookup ((float) ph[c]);
                    const float sq = s >= 0.0f ? 1.0f : -1.0f;
                    ch[c][i] *= s + (sq - s) * shape;
                    ph[c] += base * m * (c == 1 ? spread : 1.0f) / rate;
                    if (ph[c] >= 1.0) ph[c] -= 1.0;
                }
                lfo += lfoHz / rate;
                if (lfo >= 1.0) lfo -= 1.0;
            }
            activity = (float) ph[0];
        }

    private:
        float rate = 48000.0f;
        double ph[2] {}, lfo = 0.0;
    };

    //==========================================================================
    /** Hilbert-transform frequency shifter (Olli Niemitalo's 90-degree allpass pairs). */
    class FreqShift final : public FxUnit
    {
    public:
        FreqShift() : FxUnit (FxType::FreqShift) {}
        void prepare (float sr, int) override { rate = sr; reset(); }
        void reset() noexcept override
        {
            for (auto& c : st) for (auto& s : c) s = {};
            fb[0] = fb[1] = 0.0f;
            ph = 0.0;
        }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float s = bip (p[0]);
            const float hz = std::copysign (s * s * 2000.0f, s);
            const float feedback = p[1] * 0.9f;
            const float stereo = p[2];
            const float dir = bip (p[3]);
            const auto& sine = SineTable::get();
            float* ch[2] = { L, R };
            for (int i = 0; i < n; ++i)
            {
                const float cosv = sine.lookup (wrap01 ((float) ph + 0.25f));
                const float sinv = sine.lookup ((float) ph);
                for (int c = 0; c < 2; ++c)
                {
                    const float x = ch[c][i] + fb[c] * feedback;
                    float re, im;
                    hilbert (c, x, re, im);
                    const float d = c == 1 ? dir * (1.0f - 2.0f * stereo * 0.5f) : dir;
                    const float y = re * cosv - d * im * sinv;
                    fb[c] = softLimit (y);
                    ch[c][i] = y;
                }
                ph += hz / rate;
                ph -= std::floor (ph);
            }
            activity = hz;
        }

    private:
        struct AP { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
        static float apStage (AP& s, float x, float a) noexcept
        {
            const float y = a * (x + s.y2) - s.x2;
            s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
            return y;
        }
        void hilbert (int c, float x, float& re, float& im) noexcept
        {
            static constexpr float A[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
            static constexpr float B[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
            float a = x, b = x;
            for (int k = 0; k < 4; ++k)
            {
                a = apStage (st[c][k], a, A[k] * A[k]);
                b = apStage (st[c][4 + k], b, B[k] * B[k]);
            }
            re = a;
            im = delayed[c];
            delayed[c] = b; // one-sample delay aligns the two branches
        }
        float rate = 48000.0f;
        AP st[2][8] {};
        float delayed[2] {}, fb[2] {};
        double ph = 0.0;
    };

    //==========================================================================
    class Vocoder final : public FxUnit
    {
    public:
        static constexpr int kBands = 16;
        Vocoder() : FxUnit (FxType::Vocoder) {}
        void prepare (float sr, int) override { rate = sr; reset(); }
        void reset() noexcept override
        {
            for (auto& b : car) b.reset();
            for (auto& b : mod) b.reset();
            for (auto& e : env) e.env = 0.0f;
        }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext& ctx) noexcept override
        {
            const bool useSide = p[0] < 0.5f && ctx.sideL != nullptr;
            const float q = lin (p[1], 2.0f, 18.0f);
            const float speed = expo (p[2], 2.0f, 60.0f);
            const float shift = expo (p[3], 0.5f, 2.0f);
            const float vowel = p[4] * 4.0f;
            const float out = dbToGain ((p[5] - 0.5f) * 24.0f + 6.0f);
            static constexpr float F[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
            const int v0 = std::min (3, (int) vowel);
            const float vf = vowel - (float) v0;
            for (int b = 0; b < kBands; ++b)
            {
                const float hz = 100.0f * std::pow (80.0f, (float) b / (kBands - 1));
                car[b].set (Biquad::Kind::BandPass, hz * shift, q, 0, rate);
                car[kBands + b].set (Biquad::Kind::BandPass, hz * shift, q, 0, rate);
                mod[b].set (Biquad::Kind::BandPass, hz, q, 0, rate);
                env[b].setTimes (1000.0f / speed * 0.2f, 1000.0f / speed, rate);
                // built-in "talk box" pattern when no sidechain is used
                float g = 0.05f;
                for (int f = 0; f < 3; ++f)
                {
                    const float fhz = F[v0][f] + (F[v0 + 1][f] - F[v0][f]) * vf;
                    g += std::exp (-std::pow (std::log2 (hz / fhz) * 3.0f, 2.0f)) * (f == 0 ? 1.0f : 0.6f);
                }
                pattern[b] = g;
            }
            for (int i = 0; i < n; ++i)
            {
                const float m = useSide ? 0.5f * (ctx.sideL[i] + (ctx.sideR != nullptr ? ctx.sideR[i] : ctx.sideL[i])) : 0.0f;
                float yl = 0.0f, yr = 0.0f;
                for (int b = 0; b < kBands; ++b)
                {
                    const float gain = useSide ? env[b].process (mod[b].process (m)) * 4.0f : pattern[b];
                    yl += car[b].process (L[i]) * gain;
                    yr += car[kBands + b].process (R[i]) * gain;
                }
                L[i] = yl * out;
                R[i] = yr * out;
            }
            activity = useSide ? 1.0f : 0.0f;
        }

    private:
        float rate = 48000.0f;
        Biquad car[kBands * 2], mod[kBands];
        EnvFollower env[kBands];
        float pattern[kBands] {};
    };

    //==========================================================================
    class Widener final : public FxUnit
    {
    public:
        Widener() : FxUnit (FxType::Widener) {}
        void prepare (float sr, int) override { rate = sr; haas.allocate ((int) (sr * 0.03f)); reset(); }
        void reset() noexcept override { haas.clear(); lpS = {}; }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float width = p[0] * 2.0f;
            const float monoHz = p[1] < 0.02f ? 0.0f : expo (p[1], 20.0f, 500.0f);
            const float haasMs = p[2] * 20.0f;
            const float bal = bip (p[3]);
            const float c = monoHz > 0.0f ? OnePole::coefFor (monoHz, rate) : 0.0f;
            float gl, gr;
            equalPowerPan (bal, gl, gr);
            gl *= 1.41421f; gr *= 1.41421f;
            for (int i = 0; i < n; ++i)
            {
                float l = L[i], r = R[i];
                haas.push (r);
                if (haasMs > 0.05f) r = haas.read (haasMs * 0.001f * rate);
                const float mid = 0.5f * (l + r);
                float side = 0.5f * (l - r);
                if (c > 0.0f) side -= lpS.lp (side, c); // remove low-frequency side content
                side *= width;
                L[i] = (mid + side) * gl;
                R[i] = (mid - side) * gr;
            }
            activity = width;
        }

    private:
        float rate = 48000.0f;
        DelayLine haas;
        OnePole lpS;
    };

    //==========================================================================
    class Ducker final : public FxUnit
    {
    public:
        Ducker() : FxUnit (FxType::Ducker) {}
        void prepare (float sr, int) override { rate = sr; reset(); }
        void reset() noexcept override { sync.cycles = 0.0; smoothed = 1.0f; }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext& ctx) noexcept override
        {
            static constexpr int divs[] = { 5, 8, 11, 14, 16 }; // 1/16, 1/8, 1/4, 1/2, 1 bar
            const float beats = beatsForDivision (divs[stepped (p[1], 5)]);
            const double start = sync.advance (ctx, beats, n);
            const float cyclesPerSample = ctx.bpm / 60.0f / beats / ctx.sampleRate;
            const float depth = p[0];
            const float shape = bip (p[2]) * 0.95f;
            const float attack = p[3] * 0.25f; // portion of the cycle spent ducking down
            const float phaseOff = p[4];
            const float sm = 1.0f - std::exp (-1.0f / (0.0015f * rate));
            for (int i = 0; i < n; ++i)
            {
                const float ph = wrap01 ((float) start + cyclesPerSample * (float) i + phaseOff);
                float curve; // 0 = fully ducked, 1 = full volume
                if (ph < attack) curve = 1.0f - ph / std::max (attack, 1.0e-4f);
                else             curve = curveShape ((ph - attack) / (1.0f - attack), -shape);
                const float g = 1.0f - depth * (1.0f - curve);
                smoothed += (g - smoothed) * sm;
                L[i] *= smoothed;
                R[i] *= smoothed;
            }
            activity = smoothed;
        }

    private:
        float rate = 48000.0f, smoothed = 1.0f;
        SyncPhase sync;
    };

    //==========================================================================
    /** Look-ahead brick-wall limiter (1.5 ms). */
    class Limiter final : public FxUnit
    {
    public:
        Limiter() : FxUnit (FxType::Limiter) {}
        void prepare (float sr, int) override
        {
            rate = sr;
            look = std::max (1, (int) (0.0015f * sr));
            line[0].allocate (look + 4);
            line[1].allocate (look + 4);
            reset();
        }
        void reset() noexcept override { line[0].clear(); line[1].clear(); gain = 1.0f; holdCount = 0; holdTarget = 1.0f; }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float in = dbToGain (p[0] * 24.0f);
            const float ceiling = dbToGain (lin (p[1], -12.0f, 0.0f));
            const float rel = 1.0f - std::exp (-1.0f / (expo (p[2], 10.0f, 500.0f) * 0.001f * rate));
            const float att = 1.0f - std::exp (-1.0f / ((float) look * 0.35f));
            float minG = 1.0f;
            for (int i = 0; i < n; ++i)
            {
                const float l = L[i] * in, r = R[i] * in;
                line[0].push (l);
                line[1].push (r);
                const float peak = std::max (std::abs (l), std::abs (r));
                const float need = peak > ceiling ? ceiling / peak : 1.0f;
                if (need < holdTarget || holdCount <= 0) { holdTarget = need; holdCount = look; }
                else --holdCount;
                gain += (holdTarget - gain) * (holdTarget < gain ? att : rel);
                const float g = std::min (gain, 1.0f);
                L[i] = std::clamp (line[0].readInt (look) * g, -ceiling, ceiling);
                R[i] = std::clamp (line[1].readInt (look) * g, -ceiling, ceiling);
                minG = std::min (minG, g);
            }
            activity = gainToDb (minG);
        }

    private:
        float rate = 48000.0f, gain = 1.0f, holdTarget = 1.0f;
        int look = 64, holdCount = 0;
        DelayLine line[2];
    };

    //==========================================================================
    class TiltEq final : public FxUnit
    {
    public:
        TiltEq() : FxUnit (FxType::TiltEq) {}
        void prepare (float sr, int) override { rate = sr; reset(); }
        void reset() noexcept override { for (auto& b : band) b.reset(); }
        void process (float* L, float* R, int n, const FxParams& p, const FxContext&) noexcept override
        {
            const float lowF = expo (p[3], 40.0f, 600.0f), highF = expo (p[4], 1500.0f, 16000.0f);
            const float midF = std::sqrt (lowF * highF);
            for (int c = 0; c < 2; ++c)
            {
                band[c * 3 + 0].set (Biquad::Kind::LowShelf, lowF, 0.7f, bip (p[0]) * 15.0f, rate);
                band[c * 3 + 1].set (Biquad::Kind::Peak, midF, 0.7f, bip (p[1]) * 15.0f, rate);
                band[c * 3 + 2].set (Biquad::Kind::HighShelf, highF, 0.7f, bip (p[2]) * 15.0f, rate);
            }
            const float out = dbToGain ((p[5] - 0.5f) * 24.0f);
            float* ch[2] = { L, R };
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    ch[c][i] = band[c * 3 + 2].process (band[c * 3 + 1].process (band[c * 3].process (ch[c][i]))) * out;
        }

    private:
        float rate = 48000.0f;
        Biquad band[6];
    };
} // namespace

std::unique_ptr<FxUnit> createFx (FxType type)
{
    switch (type)
    {
        case FxType::Distortion: return std::make_unique<Distortion>();
        case FxType::OTT:        return std::make_unique<Ott>();
        case FxType::Compressor: return std::make_unique<Compressor>();
        case FxType::Transient:  return std::make_unique<Transient>();
        case FxType::Chorus:     return std::make_unique<Chorus>();
        case FxType::Flanger:    return std::make_unique<Flanger>();
        case FxType::Phaser:     return std::make_unique<Phaser>();
        case FxType::Ensemble:   return std::make_unique<Ensemble>();
        case FxType::Delay:      return std::make_unique<Delay>();
        case FxType::Reverb:     return std::make_unique<Reverb>();
        case FxType::Stutter:    return std::make_unique<Stutter>();
        case FxType::Formant:    return std::make_unique<Formant>();
        case FxType::RingMod:    return std::make_unique<RingMod>();
        case FxType::FreqShift:  return std::make_unique<FreqShift>();
        case FxType::Vocoder:    return std::make_unique<Vocoder>();
        case FxType::Widener:    return std::make_unique<Widener>();
        case FxType::Ducker:     return std::make_unique<Ducker>();
        case FxType::Limiter:    return std::make_unique<Limiter>();
        case FxType::TiltEq:     return std::make_unique<TiltEq>();
        case FxType::Off:
        case FxType::Count:
        default:                 return nullptr;
    }
}

} // namespace mbx::dsp::fx
