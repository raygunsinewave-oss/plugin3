#include "TestHarness.h"

#include "../src/dsp/FFT.h"
#include "../src/dsp/SynthEngine.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace mbx;
using namespace mbx::dsp;

namespace
{
constexpr float kSr = 48000.0f;

const WavetableBank& bank()
{
    static const WavetableBank b;
    return b;
}

/** Ratio (dB) of non-harmonic (alias) energy to harmonic energy. */
double aliasRatioDb (const std::vector<float>& x, double f0, double sr)
{
    const size_t n = x.size();
    std::vector<std::complex<double>> spec (n);
    for (size_t i = 0; i < n; ++i) // Blackman-Harris window
    {
        const double t = 2.0 * 3.141592653589793 * (double) i / (double) (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        spec[i] = x[i] * w;
    }
    fft (spec, false);

    const double binHz = sr / (double) n;
    double harmonic = 0.0, alias = 0.0;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double f   = (double) k * binHz;
        const double pwr = std::norm (spec[k]);
        const double h   = std::round (f / f0);
        if (h >= 1.0 && std::abs (f - h * f0) <= 6.0 * binHz)
            harmonic += pwr;
        else
            alias += pwr;
    }
    return 10.0 * std::log10 ((alias + 1e-30) / (harmonic + 1e-30));
}

std::vector<float> renderOsc (OscSettings s, float hz, int n)
{
    Oscillator osc;
    osc.setBank (&bank());
    osc.setSeed (42);
    osc.noteOn (s);
    std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
    for (int pos = 0; pos < n; pos += 64)
        osc.render (s, hz, kSr, l.data() + pos, r.data() + pos, std::min (64, n - pos));
    return l;
}

OscSettings analog (VaShape shape)
{
    OscSettings s;
    s.on = true;
    s.type = OscType::Analog;
    s.shape = shape;
    s.phaseRand = 0.0f;
    s.level = 1.0f;
    return s;
}
} // namespace

MBX_TEST (wavetable_mips_are_bandlimited)
{
    const auto& wt = bank().get (2); // PWM sweep: rich harmonics
    for (int mip = 0; mip < Wavetable::kNumMips; ++mip)
    {
        std::vector<std::complex<double>> spec (Wavetable::kSize);
        const float* d = wt.mipData (wt.getNumFrames() - 1, mip);
        for (int i = 0; i < Wavetable::kSize; ++i)
            spec[(size_t) i] = d[i];
        fft (spec, false);
        const int maxH = (Wavetable::kSize / 2) >> mip;
        double above = 0.0, total = 0.0;
        for (int k = 1; k < Wavetable::kSize / 2; ++k)
        {
            total += std::norm (spec[(size_t) k]);
            if (k > maxH) above += std::norm (spec[(size_t) k]);
        }
        MBX_CHECK (above / total < 1e-9, "mip %d leaks %.3g of its energy above harmonic %d", mip, above / total, maxH);
    }
}

MBX_TEST (wavetable_frames_are_normalised)
{
    for (int t = 0; t < bank().size(); ++t)
    {
        const auto& wt = bank().get (t);
        for (int f = 0; f < wt.getNumFrames(); f += 7)
        {
            float peak = 0.0f;
            const float* d = wt.mipData (f, 0);
            for (int i = 0; i < Wavetable::kSize; ++i)
                peak = std::max (peak, std::abs (d[i]));
            MBX_CHECK (peak > 0.99f && peak < 1.01f, "table %d frame %d peak %.3f", t, f, peak);
        }
    }
}

MBX_TEST (analog_saw_has_correct_pitch)
{
    const auto x = renderOsc (analog (VaShape::Saw), 440.0f, (int) kSr);
    int crossings = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] > 0.0f && x[i] <= 0.0f) // saw falls once per cycle
            ++crossings;
    MBX_CHECK (std::abs (crossings - 440) <= 1, "got %d cycles", crossings);
}

MBX_TEST (polyblep_saw_aliases_far_less_than_naive)
{
    const float f0 = 2489.0f;
    const auto blep = renderOsc (analog (VaShape::Saw), f0, 16384);
    std::vector<float> naive (16384);
    float p = 0.0f;
    for (auto& v : naive) { v = 2.0f * p - 1.0f; p += f0 / kSr; if (p >= 1.0f) p -= 1.0f; }

    const double a = aliasRatioDb (blep, f0, kSr), b = aliasRatioDb (naive, f0, kSr);
    std::printf ("         saw alias: polyBLEP %.1f dB vs naive %.1f dB\n", a, b);
    MBX_CHECK (a < b - 10.0, "polyBLEP %.1f dB vs naive %.1f dB", a, b);
}

MBX_TEST (polyblamp_triangle_aliases_less_than_naive)
{
    const float f0 = 2489.0f;
    const auto tri = renderOsc (analog (VaShape::Triangle), f0, 16384);
    std::vector<float> naive (16384);
    float p = 0.0f;
    for (auto& v : naive) { v = 1.0f - 4.0f * std::abs (p - 0.5f); p += f0 / kSr; if (p >= 1.0f) p -= 1.0f; }

    const double a = aliasRatioDb (tri, f0, kSr), b = aliasRatioDb (naive, f0, kSr);
    std::printf ("         triangle alias: polyBLAMP %.1f dB vs naive %.1f dB\n", a, b);
    MBX_CHECK (a < b - 10.0, "polyBLAMP %.1f dB vs naive %.1f dB", a, b);
}

MBX_TEST (wavetable_playback_is_alias_free_at_high_pitch)
{
    OscSettings s = analog (VaShape::Saw);
    s.type = OscType::Wavetable;
    s.table = 0;
    s.wtPosition = 0.66f; // pure saw frame
    const float f0 = 3111.0f;
    const double a = aliasRatioDb (renderOsc (s, f0, 16384), f0, kSr);
    std::printf ("         wavetable saw alias: %.1f dB\n", a);
    MBX_CHECK (a < -60.0, "alias %.1f dB", a);
}

MBX_TEST (unison_is_level_normalised)
{
    auto rms = [] (const std::vector<float>& x) {
        double s = 0.0;
        for (size_t i = x.size() / 2; i < x.size(); ++i) s += (double) x[i] * x[i];
        return std::sqrt (s / (double) (x.size() / 2));
    };
    OscSettings one = analog (VaShape::Saw);
    one.spread = 0.0f;
    OscSettings many = one;
    many.unison = 16;
    many.detuneCents = 30.0f;
    const double r1 = rms (renderOsc (one, 220.0f, 48000)), r16 = rms (renderOsc (many, 220.0f, 48000));
    const double diffDb = 20.0 * std::log10 (r16 / r1);
    MBX_CHECK (std::abs (diffDb) < 4.0, "16-voice unison is %.1f dB vs single voice", diffDb);
}

MBX_TEST (envelope_stage_timing)
{
    Envelope env;
    Envelope::Params p;
    p.attack = 0.010f; p.decay = 0.020f; p.sustain = 0.5f; p.release = 0.050f;
    env.setParams (p, kSr);
    env.noteOn();

    int peakAt = -1;
    for (int i = 0; i < 2000 && peakAt < 0; ++i)
        if (env.next() >= 0.999f) peakAt = i;
    MBX_CHECK (std::abs (peakAt - 480) <= 2, "attack peak at sample %d", peakAt);

    for (int i = 0; i < 4800; ++i) env.next();
    MBX_CHECK (std::abs (env.getValue() - 0.5f) < 1e-3f, "sustain %.4f", env.getValue());

    env.noteOff();
    int idleAt = -1;
    for (int i = 0; i < 10000 && idleAt < 0; ++i)
    {
        env.next();
        if (! env.isActive()) idleAt = i;
    }
    MBX_CHECK (std::abs (idleAt - 2400) <= 2, "release finished at %d", idleAt);
}

MBX_TEST (envelope_retrigger_does_not_jump)
{
    Envelope env;
    Envelope::Params p;
    p.attack = 0.050f;
    env.setParams (p, kSr);
    env.noteOn();
    for (int i = 0; i < 4800; ++i) env.next();
    const float before = env.getValue();
    env.noteOn(); // legato retrigger from the current level
    const float after = env.next();
    MBX_CHECK (std::abs (after - before) < 0.01f, "jumped from %.3f to %.3f", before, after);
}

namespace
{
std::unique_ptr<SynthEngine> sineEngine (int polyphony)
{
    auto e = std::make_unique<SynthEngine>();
    e->prepare (&bank(), kSr);
    EngineSettings s;
    s.osc[0] = analog (VaShape::Sine);
    s.osc[0].level = 0.8f;
    s.amp.attack = 0.002f;
    s.amp.sustain = 1.0f;
    s.masterGain = 1.0f;
    s.polyphony = polyphony;
    e->setTarget (s);
    return e;
}

float maxStep (const std::vector<float>& x)
{
    float m = 0.0f;
    for (size_t i = 1; i < x.size(); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}
} // namespace

MBX_TEST (voice_steal_is_click_free)
{
    auto engine = sineEngine (1);
    std::vector<float> l (19200, 0.0f), r (19200, 0.0f);
    engine->noteOn (60, 1.0f);
    engine->render (l.data(), r.data(), 4800);
    engine->noteOn (67, 1.0f);   // forces a steal (polyphony = 1)
    engine->render (l.data() + 4800, r.data() + 4800, 4800);
    engine->noteOn (67, 0.5f);   // same-key retrigger
    engine->render (l.data() + 9600, r.data() + 9600, 9600);

    const float step = maxStep (l);
    // a 392 Hz sine at this level moves at most ~0.03/sample; a hard cut would be ~0.5
    MBX_CHECK (step < 0.06f, "max sample step %.4f (click)", step);
    MBX_CHECK (engine->getActiveVoiceCount() == 1);
}

MBX_TEST (polyphony_limit_is_respected)
{
    auto engine = sineEngine (4);
    std::vector<float> l (512), r (512);
    for (int n = 0; n < 10; ++n)
        engine->noteOn (48 + n, 0.8f);
    engine->render (l.data(), r.data(), 512);
    MBX_CHECK (engine->getActiveVoiceCount() == 4, "active %d", engine->getActiveVoiceCount());
}

MBX_TEST (sustain_pedal_holds_notes)
{
    auto engine = sineEngine (8);
    std::vector<float> l (48000), r (48000);
    engine->setSustainPedal (true);
    engine->noteOn (60, 1.0f);
    engine->render (l.data(), r.data(), 480);
    engine->noteOff (60);
    engine->render (l.data(), r.data(), 48000);
    MBX_CHECK (engine->getActiveVoiceCount() == 1, "note released despite pedal");
    engine->setSustainPedal (false);
    engine->render (l.data(), r.data(), 48000);
    MBX_CHECK (engine->getActiveVoiceCount() == 0, "note stuck after pedal up");
}

MBX_TEST (all_oscillator_types_are_finite_and_bounded)
{
    for (int t = 0; t <= (int) OscType::Noise; ++t)
        for (int nz = 0; nz <= (int) NoiseType::Vinyl; ++nz)
        {
            OscSettings s = analog (VaShape::Pulse);
            s.type = (OscType) t;
            s.noise = (NoiseType) nz;
            s.unison = 16;
            s.pulseWidth = 0.1f;
            s.phaseRand = 1.0f; // default; with 0 all 16 voices start in phase and sum coherently (by design)
            for (float hz : { 20.0f, 440.0f, 12000.0f })
                for (float v : renderOsc (s, hz, 4096))
                    MBX_CHECK (std::isfinite (v) && std::abs (v) < 4.0f, "type %d noise %d hz %.0f -> %f", t, nz, hz, v);
        }
}
