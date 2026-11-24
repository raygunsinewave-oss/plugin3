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
        osc.render (s, hz, kSr, l.data() + pos, r.data() + pos, nullptr, nullptr, false, std::min (64, n - pos));
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
    s.env[0].attack = 0.002f;
    s.env[0].sustain = 1.0f;
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

//==============================================================================
// Phase 2: filters, modulation, voice modes
namespace
{
std::vector<float> sineSignal (float hz, int n)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = 0.5f * std::sin (kTwoPi * hz * (float) i / kSr);
    return x;
}
double rmsOf (const std::vector<float>& x, size_t from = 0)
{
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (x.size() - from));
}
std::vector<float> runFilter (FilterType type, float cutoff, float res, std::vector<float> x, float drive = 0.0f)
{
    FilterUnit f;
    f.prepare (kSr);
    FilterSettings s;
    s.type = type; s.resonance = res; s.drive = drive;
    for (size_t pos = 0; pos < x.size(); pos += 32)
        f.process (s, cutoff, cutoff, nullptr, x.data() + pos, nullptr, (int) std::min<size_t> (32, x.size() - pos), kSr);
    return x;
}
} // namespace

MBX_TEST (lowpass_and_highpass_separate_bands)
{
    const auto hi = sineSignal (8000.0f, 9600), lo = sineSignal (100.0f, 9600);
    const double in = rmsOf (hi, 4800);
    for (auto t : { FilterType::LP12, FilterType::LP24, FilterType::Ladder, FilterType::Diode })
    {
        const double att = 20.0 * std::log10 (rmsOf (runFilter (t, 500.0f, 0.1f, hi), 4800) / in);
        const double pass = 20.0 * std::log10 (rmsOf (runFilter (t, 500.0f, 0.1f, lo), 4800) / rmsOf (lo, 4800));
        std::printf ("         %-12s 8 kHz through 500 Hz LP: %6.1f dB, 100 Hz: %5.1f dB\n", kFilterTypeNames[(int) t], att, pass);
        MBX_CHECK (att < -24.0, "%s only attenuates %.1f dB", kFilterTypeNames[(int) t], att);
        MBX_CHECK (pass > -6.0, "%s passband %.1f dB", kFilterTypeNames[(int) t], pass);
    }
    const double hpAtt = 20.0 * std::log10 (rmsOf (runFilter (FilterType::HP24, 4000.0f, 0.1f, lo), 4800) / rmsOf (lo, 4800));
    MBX_CHECK (hpAtt < -40.0, "HP24 lets 100 Hz through at %.1f dB", hpAtt);
}

MBX_TEST (every_filter_type_is_stable_at_extreme_settings)
{
    Rng rng (7);
    std::vector<float> noise (24000);
    for (auto& v : noise) v = rng.bipolar();
    std::vector<float> fm (32);
    for (int t = 0; t <= (int) FilterType::Acid; ++t)
        for (float cutoff : { 30.0f, 1000.0f, 20000.0f })
        {
            FilterUnit f;
            f.prepare (kSr);
            FilterSettings s;
            s.type = (FilterType) t; s.resonance = 1.0f; s.drive = 1.0f; s.fmAmount = 1.0f;
            auto x = noise;
            for (size_t pos = 0; pos < x.size(); pos += 32)
            {
                for (auto& v : fm) v = rng.bipolar();
                f.process (s, cutoff, cutoff * 0.5f, fm.data(), x.data() + pos, nullptr, 32, kSr);
            }
            for (float v : x)
                MBX_CHECK (std::isfinite (v) && std::abs (v) < 12.0f, "%s @ %.0f Hz -> %f", kFilterTypeNames[t], cutoff, v);
        }
}

MBX_TEST (svf_self_oscillates_at_full_resonance)
{
    std::vector<float> x (48000, 0.0f);
    x[0] = 1.0f;
    const auto y = runFilter (FilterType::LP12, 1000.0f, 1.0f, x);
    const double tail = rmsOf (y, 40000);
    MBX_CHECK (tail > 0.01, "resonance decayed to %.5f rms after 0.8 s", tail);
}

MBX_TEST (lfo_tempo_sync_matches_host_tempo)
{
    LfoSettings s;
    s.sync = true;
    s.division = 11; // 1/4 note
    Lfo lfo;
    lfo.noteOn (s, 1);
    const float hz = syncDivisionHz (s.division, 120.0f);
    MBX_CHECK (std::abs (hz - 2.0f) < 1.0e-4f, "1/4 at 120 BPM = %.4f Hz", hz);
    s.shape = LfoShape::SawUp;
    int wraps = 0;
    float prev = -2.0f;
    for (int c = 0; c < 1875; ++c) // 1.25 s: two full cycles, no wrap on the boundary
    {
        const float v = lfo.advance (s, nullptr, hz, 0.0, 1, 32, kSr);
        if (v < prev - 1.0f) ++wraps;
        prev = v;
    }
    MBX_CHECK (wraps == 2, "%d cycles in 1.25 s", wraps);
}

MBX_TEST (shape_evaluates_curves_and_endpoints)
{
    auto s = Shape::make ({ { 0, 0, 0 }, { 0.5f, 1, 0 }, { 1, 0, 0.8f } });
    MBX_CHECK (std::abs (s.evaluate (0.25f) - 0.5f) < 1e-4f);
    MBX_CHECK (std::abs (s.evaluate (0.5f) - 1.0f) < 1e-4f);
    MBX_CHECK (s.evaluate (0.75f) < 0.4f, "curved segment should fall fast: %.3f", s.evaluate (0.75f));
    MBX_CHECK (std::abs (s.evaluate (1.0f)) < 1e-4f);
}

namespace
{
std::unique_ptr<SynthEngine> engineWith (EngineSettings s)
{
    auto e = std::make_unique<SynthEngine>();
    e->prepare (&bank(), kSr, kSr);
    e->setTarget (s);
    return e;
}
EngineSettings sineSettings()
{
    EngineSettings s;
    s.osc[0] = analog (VaShape::Sine);
    s.osc[0].level = 0.8f;
    s.env[0].attack = 0.002f;
    s.env[0].sustain = 1.0f;
    s.masterGain = 1.0f;
    s.filter[0].dsp.on = false;
    return s;
}
double renderRms (SynthEngine& e, int n)
{
    std::vector<float> l ((size_t) n), r ((size_t) n);
    e.render (l.data(), r.data(), n);
    return rmsOf (l, (size_t) n / 2);
}
} // namespace

MBX_TEST (mod_matrix_routes_velocity_to_level)
{
    auto s = sineSettings();
    s.matrix[0] = { (int) ModSource::Velocity, 0, dest::osc (0, dest::Level), -1.0f, 0.0f, false };
    auto e = engineWith (s);
    e->noteOn (60, 1.0f);
    const double silenced = renderRms (*e, 9600);
    s.matrix[0].bypass = true;
    auto e2 = engineWith (s);
    e2->noteOn (60, 1.0f);
    const double normal = renderRms (*e2, 9600);
    MBX_CHECK (silenced < normal * 0.01 && normal > 0.05, "modulated %.4f vs bypassed %.4f", silenced, normal);
}

MBX_TEST (mod_matrix_via_and_macro_scale_amount)
{
    auto s = sineSettings();
    s.osc[0].level = 0.0f;
    s.matrix[0] = { (int) ModSource::Macro1, (int) ModSource::Macro2, dest::osc (0, dest::Level), 1.0f, 0.0f, false };
    s.macros[0] = 1.0f;
    s.macros[1] = 0.0f; // via = 0 -> no modulation
    auto e = engineWith (s);
    e->noteOn (60, 1.0f);
    MBX_CHECK (renderRms (*e, 9600) < 1e-4);
    s.macros[1] = 1.0f;
    auto e2 = engineWith (s);
    e2->noteOn (60, 1.0f);
    MBX_CHECK (renderRms (*e2, 9600) > 0.05);
}

MBX_TEST (legato_mode_uses_one_voice_and_glides)
{
    auto s = sineSettings();
    s.voiceMode = VoiceMode::Legato;
    s.glideTime = 0.1f;
    s.glideCurve = 0.0f; // linear
    auto e = engineWith (s);
    std::vector<float> l (4800), r (4800);
    e->noteOn (60, 1.0f);
    e->render (l.data(), r.data(), 4800);
    e->noteOn (72, 1.0f);
    e->render (l.data(), r.data(), 2400); // 50 ms into a 100 ms glide
    const float mid = e->getNewestVoice()->getPitch();
    MBX_CHECK (e->getActiveVoiceCount() == 1);
    MBX_CHECK (mid > 63.0f && mid < 69.0f, "pitch half-way through glide = %.2f", mid);
    e->render (l.data(), r.data(), 4800);
    MBX_CHECK (std::abs (e->getNewestVoice()->getPitch() - 72.0f) < 0.01f);
    e->noteOff (72);
    e->render (l.data(), r.data(), 480);
    MBX_CHECK (std::abs (e->getNewestVoice()->getPitch() - 60.0f) < 0.01f || e->getNewestVoice()->getPitch() < 72.0f,
               "falls back to the held key");
}

MBX_TEST (unison_voice_mode_stacks_voices)
{
    auto s = sineSettings();
    s.voiceMode = VoiceMode::Unison;
    s.stackVoices = 4;
    auto e = engineWith (s);
    std::vector<float> l (256), r (256);
    e->noteOn (60, 1.0f);
    e->noteOn (64, 1.0f);
    e->render (l.data(), r.data(), 256);
    MBX_CHECK (e->getActiveVoiceCount() == 8, "%d voices", e->getActiveVoiceCount());
}

MBX_TEST (fm_warp_and_ring_mod_produce_finite_audio)
{
    auto s = sineSettings();
    s.osc[1] = analog (VaShape::Sine);
    s.osc[1].pitchSemis = 7.0f;
    s.osc[1].warp = WarpMode::FM;
    s.osc[1].warpAmount = 0.6f;
    s.osc[1].modSource = 0;
    s.osc[2] = s.osc[1];
    s.osc[2].modSource = 2; // self feedback
    s.ring = { true, 0, 1, 0.5f, OscRoute::Filter1 };
    s.sub = { true, SubShape::Square, 1, 0.5f, OscRoute::Filter2 };
    s.noise = { true, NoiseType::Pink, 0.2f, OscRoute::Bypass };
    for (int w = 0; w <= (int) WarpMode::Ring; ++w)
    {
        s.osc[0].warp = (WarpMode) w;
        s.osc[0].warpAmount = 0.7f;
        s.osc[0].modSource = 1;
        auto e = engineWith (s);
        std::vector<float> l (4800), r (4800);
        e->noteOn (48, 1.0f);
        e->render (l.data(), r.data(), 4800);
        for (float v : l)
            MBX_CHECK (std::isfinite (v) && std::abs (v) < 8.0f, "warp %d -> %f", w, v);
    }
}

MBX_TEST (granular_plays_a_loaded_sample)
{
    SampleData sample;
    sample.sampleRate = kSr;
    sample.data.resize (48000);
    for (size_t i = 0; i < sample.data.size(); ++i) sample.data[i] = std::sin (kTwoPi * 261.6f * (float) i / kSr);
    auto s = sineSettings();
    s.osc[0].type = OscType::Granular;
    s.osc[0].sample = &sample;
    s.osc[0].wtPosition = 0.3f;
    auto e = engineWith (s);
    e->noteOn (60, 1.0f);
    MBX_CHECK (renderRms (*e, 24000) > 0.02);
}
