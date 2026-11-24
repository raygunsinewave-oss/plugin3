#include "TestHarness.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdlib>

#include "../src/PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

using namespace mbx;

namespace
{
void setParam (MysteryBoxProcessor& p, const juce::String& id, float realValue)
{
    auto* param = p.getState().getParameter (id);
    if (param == nullptr)
        throw test::Failure (("unknown parameter " + id).toStdString());
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

struct Rendered
{
    float peak = 0.0f;
    double rms = 0.0;
    bool finite = true;
};

Rendered renderNote (MysteryBoxProcessor& p, int blocks, int blockSize, int noteAtSample = 0)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    Rendered out;
    double sum = 0.0;
    long count = 0;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), noteAtSample);
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buffer.getSample (ch, i);
                out.finite &= std::isfinite (v);
                out.peak = std::max (out.peak, std::abs (v));
                sum += (double) v * v;
                ++count;
            }
    }
    out.rms = std::sqrt (sum / (double) count);
    return out;
}
} // namespace

MBX_TEST (processor_renders_audio_at_every_oversampling_factor)
{
    for (int os = 0; os < 4; ++os)
    {
        auto pPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
        auto& p = *pPtr;
        setParam (p, params::id::oversampling, (float) os);
        p.prepareToPlay (48000.0, 512);
        const auto r = renderNote (p, 40, 512);
        MBX_CHECK (r.finite, "NaN/Inf at %dx", 1 << os);
        MBX_CHECK (r.rms > 0.01 && r.peak < 1.0f, "%dx: rms %.4f peak %.3f", 1 << os, r.rms, r.peak);
        const int expectedLatency = os == 0 ? 0 : 1;
        MBX_CHECK ((p.getLatencySamples() > 0) == (expectedLatency > 0), "%dx latency %d", 1 << os, p.getLatencySamples());
    }
}

MBX_TEST (note_on_is_sample_accurate)
{
    auto pPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& p = *pPtr;
    setParam (p, params::id::oversampling, 0.0f);
    setParam (p, params::id::env1Attack, 0.0f);
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 300);
    p.processBlock (buffer, midi);
    float before = 0.0f, after = 0.0f;
    for (int i = 0; i < 300; ++i) before = std::max (before, std::abs (buffer.getSample (0, i)));
    for (int i = 300; i < 512; ++i) after = std::max (after, std::abs (buffer.getSample (0, i)));
    MBX_CHECK (before < 1.0e-9f && after > 0.01f, "before %.4f after %.4f", before, after);
}

MBX_TEST (state_round_trips_every_parameter)
{
    auto aPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& a = *aPtr;
    juce::Random rng (99);
    for (auto* param : a.getParameters())
        param->setValueNotifyingHost (rng.nextFloat());

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    auto bPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB

    auto& b = *bPtr;
    b.setStateInformation (blob.getData(), (int) blob.getSize());

    const auto pa = a.getParameters(), pb = b.getParameters();
    MBX_CHECK (pa.size() == pb.size());
    for (int i = 0; i < pa.size(); ++i)
    {
        // compare in plain units so bool/choice/int params are judged on their snapped value
        auto* ra = dynamic_cast<juce::RangedAudioParameter*> (pa[i]);
        auto* rb = dynamic_cast<juce::RangedAudioParameter*> (pb[i]);
        MBX_CHECK (ra != nullptr && rb != nullptr);
        const float va = ra->convertFrom0to1 (ra->getValue()), vb = rb->convertFrom0to1 (rb->getValue());
        MBX_CHECK (std::abs (va - vb) <= 1e-4f * std::max (1.0f, std::abs (va)), "%s: %.5f vs %.5f", pa[i]->getName (64).toRawUTF8(), va, vb);
    }
}

MBX_TEST (process_block_is_allocation_free)
{
    auto pPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& p = *pPtr;
    setParam (p, params::oscId (0, params::id::oscUnison), 16.0f);
    setParam (p, params::oscId (1, params::id::oscOn), 1.0f);
    setParam (p, params::oscId (1, params::id::oscType), 0.0f); // wavetable
    setParam (p, params::oscId (2, params::id::oscOn), 1.0f);
    setParam (p, params::oscId (2, params::id::oscType), 5.0f); // noise
    p.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (1024);
    for (int warm = 0; warm < 4; ++warm)
        p.processBlock (buffer, midi);

    long allocs = 0;
    for (int b = 0; b < 64; ++b)
    {
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 40 + b % 30, 0.8f), b % 256);
        midi.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + b * 40), 10);
        if (b % 3 == 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, 40 + (b + 15) % 30), 128);

        test::AllocationScope scope;
        p.processBlock (buffer, midi);
        allocs += scope.count();
    }
    MBX_CHECK (allocs == 0, "processBlock allocated %ld times", allocs);
}

MBX_TEST (cpu_budget_six_osc_eight_voices)
{
    // Quality bar: 6 oscillators, 8 voices, 48 kHz, 2x oversampling < 5% of one core.
    auto pPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& p = *pPtr;
    for (int o = 0; o < params::kNumOscs; ++o)
    {
        setParam (p, params::oscId (o, params::id::oscOn), 1.0f);
        setParam (p, params::oscId (o, params::id::oscType), o % 2 == 0 ? 1.0f : 0.0f); // analog / wavetable
    }
    setParam (p, params::id::oversampling, 1.0f);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int n = 0; n < 8; ++n)
        midi.addEvent (juce::MidiMessage::noteOn (1, 48 + n * 3, 0.8f), 0);
    p.processBlock (buffer, midi);
    midi.clear();

    // best of 5 runs of ~2 s: the minimum is the robust estimator on a shared / noisy machine
    constexpr int blocks = 188;
    double best = 1.0e9;
    for (int trial = 0; trial < 5; ++trial)
    {
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int b = 0; b < blocks; ++b)
            p.processBlock (buffer, midi);
        best = std::min (best, juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0));
    }
    const double load = best / (blocks * 512.0 / 48000.0) * 100.0;
    std::printf ("         6 osc x 8 voices @48k/2x: %.2f%% of one core (%d voices active)\n", load, p.getActiveVoices());
    MBX_CHECK (p.getActiveVoices() == 8);
#if defined(NDEBUG) || ! JUCE_DEBUG
    if (std::getenv ("MBX_SKIP_PERF") == nullptr) // shared CI runners are too noisy for a hard budget
        MBX_CHECK (load < 5.0, "%.2f%% exceeds the 5%% budget", load);
#endif
}

namespace mbx::test
{
/** Renders a short demo phrase exercising Phase 1 features to a 24-bit WAV. */
int renderDemo (const juce::File& file)
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    auto pPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& p = *pPtr;
    setParam (p, params::id::oversampling, 1.0f);
    setParam (p, params::id::masterVolume, -9.0f);
    p.prepareToPlay (sr, block);

    juce::AudioBuffer<float> out (2, (int) (sr * 42.0));
    out.clear();
    juce::AudioBuffer<float> buffer (2, block);
    int written = 0;

    auto play = [&] (std::vector<int> notes, double seconds, std::function<void (double)> automate = {}) {
        const int total = (int) (seconds * sr);
        const int holdFor = (int) (total * 0.8);
        for (int pos = 0; pos < total && written + block <= out.getNumSamples(); pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                for (int n : notes) midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.85f), 0);
            if (pos <= holdFor && pos + block > holdFor)
                for (int n : notes) midi.addEvent (juce::MidiMessage::noteOff (1, n), holdFor - pos);
            if (automate) automate ((double) pos / total);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, written, buffer, ch, 0, block);
            written += block;
        }
    };

    // 1. plucky saw arpeggio
    setParam (p, params::id::env1Decay, 0.18f);
    setParam (p, params::id::env1Sustain, 0.0f);
    setParam (p, params::id::env1Release, 0.2f);
    setParam (p, params::id::env1Punch, 0.6f);
    for (int n : { 57, 60, 64, 69, 64, 60, 57, 52 })
        play ({ n }, 0.25);

    // 2. supersaw chord stab, 9 voices, wide
    setParam (p, params::oscId (0, params::id::oscType), 4.0f);
    setParam (p, params::oscId (0, params::id::oscUnison), 9.0f);
    setParam (p, params::oscId (0, params::id::oscDetune), 28.0f);
    setParam (p, params::oscId (0, params::id::oscSpread), 1.0f);
    setParam (p, params::id::env1Attack, 0.01f);
    setParam (p, params::id::env1Decay, 0.6f);
    setParam (p, params::id::env1Sustain, 0.6f);
    setParam (p, params::id::env1Release, 0.6f);
    setParam (p, params::id::env1Punch, 0.0f);
    play ({ 53, 57, 60, 64 }, 2.0);
    play ({ 50, 53, 57, 62 }, 2.0);

    // 3. vowel wavetable pad with a position sweep + sub
    setParam (p, params::oscId (0, params::id::oscType), 0.0f);
    setParam (p, params::oscId (0, params::id::oscTable), 3.0f);
    setParam (p, params::oscId (0, params::id::oscUnison), 5.0f);
    setParam (p, params::oscId (0, params::id::oscDetune), 12.0f);
    setParam (p, params::oscId (1, params::id::oscOn), 1.0f);
    setParam (p, params::oscId (1, params::id::oscShape), 0.0f);
    setParam (p, params::oscId (1, params::id::oscOctave), 1.0f);
    setParam (p, params::oscId (1, params::id::oscLevel), 0.5f);
    setParam (p, params::id::env1Attack, 0.6f);
    setParam (p, params::id::env1Sustain, 0.9f);
    setParam (p, params::id::env1Release, 1.2f);
    play ({ 45, 52, 57, 60 }, 5.0, [&] (double t) {
        setParam (p, params::oscId (0, params::id::oscWtPos), (float) (0.5 - 0.5 * std::cos (t * 6.2831853)));
    });

    // 4. PWM bass line with vinyl noise layer
    setParam (p, params::oscId (0, params::id::oscType), 1.0f);
    setParam (p, params::oscId (0, params::id::oscShape), 4.0f);
    setParam (p, params::oscId (0, params::id::oscUnison), 2.0f);
    setParam (p, params::oscId (0, params::id::oscOctave), 1.0f);
    setParam (p, params::oscId (2, params::id::oscOn), 1.0f);
    setParam (p, params::oscId (2, params::id::oscType), 5.0f);
    setParam (p, params::oscId (2, params::id::oscNoise), 4.0f);
    setParam (p, params::oscId (2, params::id::oscLevel), 0.25f);
    setParam (p, params::id::env1Attack, 0.003f);
    setParam (p, params::id::env1Decay, 0.25f);
    setParam (p, params::id::env1Sustain, 0.5f);
    setParam (p, params::id::env1Release, 0.12f);
    int step = 0;
    for (int n : { 45, 45, 57, 45, 48, 45, 55, 43, 41, 41, 53, 41, 43, 43, 55, 50 })
    {
        play ({ n }, 0.22, [&] (double t) {
            setParam (p, params::oscId (0, params::id::oscPw), (float) (0.5 + 0.4 * std::sin ((step + t) * 0.7)));
        });
        ++step;
    }
    play ({}, 0.5);

    // ---------------- Phase 2 ----------------
    auto reset = [&] {
        for (int o = 1; o < 6; ++o) setParam (p, params::oscId (o, params::id::oscOn), 0.0f);
        setParam (p, params::oscId (0, params::id::oscOctave), 2.0f);
        setParam (p, params::oscId (0, params::id::oscUnison), 1.0f);
        setParam (p, params::id::subOn, 0.0f);
        setParam (p, params::id::noiseOn, 0.0f);
        for (int k = 0; k < 4; ++k) { setParam (p, params::modId (k, params::id::modSrc), 0.0f); setParam (p, params::modId (k, params::id::modDest), 0.0f); }
        setParam (p, params::id::voiceMode, 0.0f);
        setParam (p, params::id::glideTime, 0.0f);
        setParam (p, params::filterId (0, params::id::filterEnv), 0.0f);
    };

    // 5. wobble bass: ladder filter swept by a tempo-synced LFO, legato + glide, sub
    reset();
    setParam (p, params::oscId (0, params::id::oscType), 4.0f); // supersaw
    setParam (p, params::oscId (0, params::id::oscUnison), 5.0f);
    setParam (p, params::oscId (0, params::id::oscDetune), 18.0f);
    setParam (p, params::oscId (0, params::id::oscOctave), 1.0f);
    setParam (p, params::id::subOn, 1.0f);
    setParam (p, params::id::subLevel, 0.7f);
    setParam (p, params::filterId (0, params::id::filterType), 8.0f); // ladder
    setParam (p, params::filterId (0, params::id::filterCutoff), 180.0f);
    setParam (p, params::filterId (0, params::id::filterRes), 0.6f);
    setParam (p, params::filterId (0, params::id::filterDrive), 0.4f);
    setParam (p, params::lfoId (0, params::id::lfoSync), 1.0f);
    setParam (p, params::lfoId (0, params::id::lfoDiv), 7.0f); // 1/8T
    setParam (p, params::lfoId (0, params::id::lfoBipolar), 0.0f);
    setParam (p, params::modId (0, params::id::modSrc), (float) dsp::ModSource::Lfo1);
    setParam (p, params::modId (0, params::id::modDest), (float) dsp::dest::filter (0, dsp::dest::Cutoff));
    setParam (p, params::modId (0, params::id::modAmt), 0.55f);
    setParam (p, params::id::voiceMode, 2.0f); // legato
    setParam (p, params::id::glideTime, 0.08f);
    setParam (p, params::id::env1Attack, 0.005f);
    setParam (p, params::id::env1Sustain, 1.0f);
    setParam (p, params::id::env1Release, 0.1f);
    for (int n : { 38, 38, 41, 36 })
        play ({ n }, 1.0);

    // 6. FM bell: sine carrier phase-modulated by osc 2 a fifth + octave above
    reset();
    setParam (p, params::oscId (0, params::id::oscType), 2.0f);      // FM operator
    setParam (p, params::oscId (0, params::id::oscWarp), 9.0f);      // FM
    setParam (p, params::oscId (0, params::id::oscWarpAmt), 0.35f);
    setParam (p, params::oscId (0, params::id::oscModSrc), 2.0f);    // osc 2
    setParam (p, params::oscId (1, params::id::oscType), 2.0f);
    setParam (p, params::oscId (1, params::id::oscCoarse), 19.0f);
    setParam (p, params::filterId (0, params::id::filterType), 1.0f);
    setParam (p, params::filterId (0, params::id::filterCutoff), 12000.0f);
    setParam (p, params::filterId (0, params::id::filterRes), 0.1f);
    setParam (p, params::filterId (0, params::id::filterDrive), 0.0f);
    setParam (p, params::modId (0, params::id::modSrc), (float) dsp::ModSource::Env2); // index decays with ENV 2
    setParam (p, params::modId (0, params::id::modDest), (float) dsp::dest::osc (0, dsp::dest::WarpAmount));
    setParam (p, params::modId (0, params::id::modAmt), 0.5f);
    setParam (p, params::envId (1, params::id::envDecay), 0.6f);
    setParam (p, params::id::env1Attack, 0.001f);
    setParam (p, params::id::env1Decay, 2.0f);
    setParam (p, params::id::env1Sustain, 0.0f);
    setParam (p, params::id::env1Release, 1.5f);
    for (auto chord : std::vector<std::vector<int>> { { 72 }, { 76 }, { 79 }, { 84, 72 } })
        play (chord, 0.6);
    play ({}, 1.0);

    // 7. acid line: 18 dB acid filter, ENV 2 sweep, accents via velocity
    reset();
    setParam (p, params::oscId (0, params::id::oscType), 1.0f);
    setParam (p, params::oscId (0, params::id::oscShape), 2.0f);
    setParam (p, params::oscId (0, params::id::oscWarp), 0.0f);
    setParam (p, params::oscId (0, params::id::oscOctave), 1.0f);
    setParam (p, params::filterId (0, params::id::filterType), 14.0f);
    setParam (p, params::filterId (0, params::id::filterCutoff), 220.0f);
    setParam (p, params::filterId (0, params::id::filterRes), 0.82f);
    setParam (p, params::filterId (0, params::id::filterDrive), 0.3f);
    setParam (p, params::filterId (0, params::id::filterEnv), 0.45f);
    setParam (p, params::envId (1, params::id::envDecay), 0.18f);
    setParam (p, params::id::voiceMode, 1.0f); // mono
    setParam (p, params::id::glideTime, 0.04f);
    setParam (p, params::id::glideMode, 1.0f); // legato only
    setParam (p, params::id::env1Decay, 0.3f);
    setParam (p, params::id::env1Sustain, 0.7f);
    setParam (p, params::id::env1Release, 0.05f);
    for (int n : { 36, 48, 36, 39, 36, 51, 43, 36, 46, 36, 48, 36, 41, 36, 53, 39 })
        play ({ n }, 0.16);

    // 8. pad: wavetable scan + LFO on pan/blend + parallel LP/BP filters
    reset();
    setParam (p, params::oscId (0, params::id::oscType), 0.0f);
    setParam (p, params::oscId (0, params::id::oscTable), 1.0f);
    setParam (p, params::oscId (0, params::id::oscUnison), 6.0f);
    setParam (p, params::oscId (0, params::id::oscDetune), 22.0f);
    setParam (p, params::oscId (0, params::id::oscScanOn), 1.0f);
    setParam (p, params::oscId (0, params::id::oscScanRate), 0.4f);
    setParam (p, params::oscId (0, params::id::oscScanLoop), 2.0f);
    setParam (p, params::oscId (0, params::id::oscRoute), 2.0f); // both
    setParam (p, params::id::filterRouting, 1.0f);
    setParam (p, params::filterId (0, params::id::filterType), 1.0f);
    setParam (p, params::filterId (0, params::id::filterCutoff), 2500.0f);
    setParam (p, params::filterId (0, params::id::filterRes), 0.3f);
    setParam (p, params::filterId (0, params::id::filterEnv), 0.0f);
    setParam (p, params::filterId (1, params::id::filterOn), 1.0f);
    setParam (p, params::filterId (1, params::id::filterType), 12.0f); // formant
    setParam (p, params::filterId (1, params::id::filterCutoff), 400.0f);
    setParam (p, params::lfoId (1, params::id::lfoRate), 0.15f);
    setParam (p, params::modId (1, params::id::modSrc), (float) dsp::ModSource::Lfo2);
    setParam (p, params::modId (1, params::id::modDest), (float) dsp::dest::filter (1, dsp::dest::Cutoff));
    setParam (p, params::modId (1, params::id::modAmt), 0.4f);
    setParam (p, params::id::env1Attack, 0.8f);
    setParam (p, params::id::env1Sustain, 0.9f);
    setParam (p, params::id::env1Release, 1.5f);
    play ({ 50, 57, 62, 65, 69 }, 5.0);
    play ({}, 1.5);

    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    if (stream == nullptr)
        return 1;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0));
    if (writer == nullptr)
        return 1;
    stream.release(); // owned by writer now
    writer->writeFromAudioSampleBuffer (out, 0, written);
    std::printf ("Rendered %.1f s to %s (peak %.2f dBFS)\n", written / sr, file.getFullPathName().toRawUTF8(),
                 juce::Decibels::gainToDecibels (out.getMagnitude (0, written)));
    return 0;
}
} // namespace mbx::test

namespace mbx::test
{
/** Renders the editor (after playing a chord so the scope has signal) to a PNG at 2x. */
int screenshot (const juce::File& file, int tab, int osc, const juce::String& modTab)
{
    auto pPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& p = *pPtr;
    setParam (p, params::oscId (0, params::id::oscType), 0.0f);
    setParam (p, params::oscId (0, params::id::oscTable), 3.0f);
    setParam (p, params::oscId (0, params::id::oscWtPos), 0.4f);
    setParam (p, params::oscId (0, params::id::oscUnison), 7.0f);
    setParam (p, params::oscId (1, params::id::oscOn), 1.0f);
    setParam (p, params::oscId (1, params::id::oscType), 4.0f);
    setParam (p, params::id::env1Attack, 0.08f);
    setParam (p, params::id::env1Punch, 0.4f);
    // a few routings so the knobs show modulation rings
    setParam (p, params::modId (0, params::id::modSrc), (float) dsp::ModSource::Lfo1);
    setParam (p, params::modId (0, params::id::modDest), (float) dsp::dest::osc (0, dsp::dest::WtPos));
    setParam (p, params::modId (0, params::id::modAmt), 0.35f);
    setParam (p, params::modId (1, params::id::modSrc), (float) dsp::ModSource::Env2);
    setParam (p, params::modId (1, params::id::modDest), (float) dsp::dest::filter (0, dsp::dest::Cutoff));
    setParam (p, params::modId (1, params::id::modAmt), -0.4f);
    setParam (p, params::modId (2, params::id::modSrc), (float) dsp::ModSource::Macro1);
    setParam (p, params::modId (2, params::id::modDest), (float) dsp::dest::osc (0, dsp::dest::Detune));
    setParam (p, params::modId (2, params::id::modAmt), 0.5f);
    setParam (p, params::filterId (0, params::id::filterCutoff), 2200.0f);
    setParam (p, params::filterId (0, params::id::filterRes), 0.55f);
    setParam (p, params::filterId (1, params::id::filterOn), 1.0f);
    setParam (p, params::id::subOn, 1.0f);
    p.prepareToPlay (48000.0, 512);
    renderNote (p, 30, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (MysteryBoxEditor::kBaseWidth, MysteryBoxEditor::kBaseHeight);

    // drive the tab/osc buttons the same way a user would
    static const char* tabs[] = { "SYNTH", "PRESETS", "FX", "ADVANCED", "XD", "SETTINGS" };
    const juce::String wantTab = tabs[juce::jlimit (0, 5, tab)];
    const juce::String wantOsc = "OSC.0" + juce::String (osc + 1);
    std::function<void (juce::Component&)> click = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (child))
            {
                const auto t = b->getButtonText();
                if (t.endsWith (" " + wantTab) || t == wantOsc || (modTab.isNotEmpty() && t == modTab))
                    b->triggerClick();
            }
            click (*child);
        }
    };
    click (*editor);
    juce::Timer::callAfterDelay (400, [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop(); // let display timers tick once

    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream os (file);
    juce::PNGImageFormat png;
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    std::printf ("%s %s\n", ok ? "Wrote" : "FAILED", file.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}
} // namespace mbx::test

MBX_TEST (state_round_trips_assets)
{
    auto aPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& a = *aPtr;
    auto shape = dsp::Shape::make ({ { 0, 0.2f, 0 }, { 0.3f, 0.9f, 0.4f }, { 1, 0.1f, -0.3f } });
    a.getAssets().shapes.set (ShapeStore::kMsegIndex, shape);
    a.getAssets().setMacroName (2, "Brightness");
    a.getAssets().setDrawnWavetable (1, std::vector<float> (256, 0.5f));
    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    auto bPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB

    auto& b = *bPtr;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    const auto got = b.getAssets().shapes.get (ShapeStore::kMsegIndex);
    MBX_CHECK (got.numPoints == 3 && std::abs (got.points[1].y - 0.9f) < 1e-3f && std::abs (got.points[1].curve - 0.4f) < 1e-3f);
    MBX_CHECK (b.getAssets().getMacroName (2) == "BRIGHTNESS");
    MBX_CHECK (b.getAssets().userTables[1].get() != nullptr, "drawn wavetable not restored");
}

MBX_TEST (phase2_processor_is_allocation_free_with_everything_on)
{
    auto pPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& p = *pPtr;
    for (int o = 0; o < 6; ++o)
    {
        setParam (p, params::oscId (o, params::id::oscOn), 1.0f);
        setParam (p, params::oscId (o, params::id::oscWarp), (float) (o * 2 % 12));
        setParam (p, params::oscId (o, params::id::oscWarpAmt), 0.5f);
        setParam (p, params::oscId (o, params::id::oscModSrc), (float) ((o + 1) % 6 + 1));
        setParam (p, params::oscId (o, params::id::oscScanOn), 1.0f);
    }
    for (int k = 0; k < 32; ++k)
    {
        setParam (p, params::modId (k, params::id::modSrc), (float) (1 + k % 28));
        setParam (p, params::modId (k, params::id::modDest), (float) (1 + (k * 7) % 76));
        setParam (p, params::modId (k, params::id::modAmt), 0.3f);
        setParam (p, params::modId (k, params::id::modVia), (float) (k % 3));
    }
    setParam (p, params::filterId (1, params::id::filterOn), 1.0f);
    setParam (p, params::filterId (0, params::id::filterType), 11.0f); // comb -
    setParam (p, params::filterId (1, params::id::filterType), 13.0f); // phaser
    setParam (p, params::filterId (0, params::id::filterFm), 0.5f);
    setParam (p, params::id::filterRouting, 2.0f);
    setParam (p, params::id::subOn, 1.0f);
    setParam (p, params::id::noiseOn, 1.0f);
    setParam (p, params::id::ringOn, 1.0f);
    setParam (p, params::id::voiceMode, 3.0f);
    setParam (p, params::id::glideTime, 0.1f);
    setParam (p, params::id::mpeOn, 1.0f);
    p.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (2048);
    for (int warm = 0; warm < 4; ++warm)
        p.processBlock (buffer, midi);
    long allocs = 0;
    bool finite = true;
    for (int b = 0; b < 48; ++b)
    {
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1 + b % 4, 40 + b % 24, 0.8f), b % 256);
        midi.addEvent (juce::MidiMessage::pitchWheel (2, 8192 + b * 60), 10);
        midi.addEvent (juce::MidiMessage::channelPressureChange (2, b * 2), 20);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, b * 2), 30);
        midi.addEvent (juce::MidiMessage::controllerEvent (3, 74, b), 40);
        if (b % 3 == 0)
            midi.addEvent (juce::MidiMessage::noteOff (1 + (b + 2) % 4, 40 + (b + 10) % 24), 128);
        test::AllocationScope scope;
        p.processBlock (buffer, midi);
        allocs += scope.count();
        for (int i = 0; i < 256; ++i)
            finite &= std::isfinite (buffer.getSample (0, i)) && std::isfinite (buffer.getSample (1, i));
    }
    MBX_CHECK (allocs == 0, "processBlock allocated %ld times", allocs);
    MBX_CHECK (finite, "non-finite output");
}

//==============================================================================
// Phase 3: FX rack, arp / sequencer, metering
MBX_TEST (every_fx_type_is_finite_and_bounded)
{
    using namespace dsp::fx;
    dsp::Rng rng (3);
    std::vector<float> l (4096), r (4096), side (4096);
    for (int t = 1; t < (int) FxType::Count; ++t)
        for (int trial = 0; trial < 6; ++trial)
        {
            auto fx = createFx ((FxType) t);
            MBX_CHECK (fx != nullptr, "type %d", t);
            fx->prepare (48000.0f, 512);
            FxParams p;
            for (auto& v : p) v = trial == 0 ? 0.0f : trial == 1 ? 1.0f : rng.uniform();
            FxContext ctx;
            ctx.sideL = side.data();
            ctx.sideR = side.data();
            for (int i = 0; i < 4096; ++i) { l[(size_t) i] = rng.bipolar() * 0.5f; r[(size_t) i] = rng.bipolar() * 0.5f; side[(size_t) i] = rng.bipolar(); }
            for (int pos = 0; pos < 4096; pos += 512)
                fx->process (l.data() + pos, r.data() + pos, 512, p, ctx);
            for (int i = 0; i < 4096; ++i)
                MBX_CHECK (std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]) && std::abs (l[(size_t) i]) < 40.0f,
                           "%s trial %d -> %f", fxInfo ((FxType) t).name, trial, l[(size_t) i]);
        }
}

MBX_TEST (limiter_holds_its_ceiling)
{
    auto fx = dsp::fx::createFx (dsp::fx::FxType::Limiter);
    fx->prepare (48000.0f, 512);
    std::vector<float> l (48000), r (48000);
    for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 1.8f * std::sin (0.05f * (float) i);
    for (size_t pos = 0; pos < l.size(); pos += 512)
        fx->process (l.data() + pos, r.data() + pos, (int) std::min<size_t> (512, l.size() - pos), { 0.0f, 0.75f, 0.3f, 0, 0, 0 }, {});
    const float ceiling = dsp::dbToGain (-3.0f);
    float peak = 0.0f;
    for (float v : l) peak = std::max (peak, std::abs (v));
    MBX_CHECK (peak <= ceiling + 1.0e-4f, "peak %.4f > ceiling %.4f", peak, ceiling);
}

MBX_TEST (fx_rack_with_every_slot_is_allocation_free)
{
    auto pPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& p = *pPtr;
    for (int k = 0; k < params::kNumFxSlots + 2; ++k)
    {
        setParam (p, params::fxId (k, params::id::fxType), (float) (1 + (k * 2) % ((int) dsp::fx::FxType::Count - 1)));
        if (k >= params::kNumFxSlots)
            setParam (p, params::fxId (k, params::id::fxSend), 0.5f);
    }
    setParam (p, params::id::arpMode, 1.0f);
    setParam (p, params::id::arpOctaves, 3.0f);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (2048);
    for (int warm = 0; warm < 4; ++warm)
        p.processBlock (buffer, midi);
    long allocs = 0;
    bool finite = true;
    double energy = 0.0;
    for (int b = 0; b < 160; ++b) // ~0.85 s: long enough for the first synced echo to arrive
    {
        midi.clear();
        if (b == 0) // hold a chord for the whole run so the free-running arp has steps to play
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 3);
            midi.addEvent (juce::MidiMessage::noteOn (1, 55, 0.9f), 3);
        }
        test::AllocationScope scope;
        p.processBlock (buffer, midi);
        allocs += scope.count();
        for (int i = 0; i < 256; ++i)
        {
            finite &= std::isfinite (buffer.getSample (0, i)) && std::isfinite (buffer.getSample (1, i));
            energy += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
        }
    }
    MBX_CHECK (allocs == 0, "processBlock allocated %ld times", allocs);
    MBX_CHECK (finite);
    MBX_CHECK (energy > 0.01, "rack produced silence");
}

MBX_TEST (arpeggiator_plays_up_pattern_on_the_grid)
{
    dsp::ArpSeq arp;
    arp.prepare (48000.0f);
    dsp::ArpSettings s;
    s.mode = dsp::ArpMode::Arp;
    s.pattern = dsp::ArpPattern::Up;
    s.rateDivision = 5; // 1/16 = 0.25 beat = 6000 samples at 120 BPM
    s.gate = 0.5f;
    dsp::NoteEvent in[3] = { { 0, true, 64, 0.8f, 0 }, { 0, true, 60, 0.8f, 0 }, { 0, true, 67, 0.8f, 0 } };
    std::array<dsp::NoteEvent, 64> out {};
    std::vector<int> ons, onTimes;
    long t0 = 0;
    for (int block = 0; block < 60; ++block)
    {
        const int n = arp.process (s, dsp::SeqPattern::makeDefault(), block == 0 ? in : nullptr, block == 0 ? 3 : 0, out.data(), 64, 512,
                                   (double) block * 512 / 48000.0 * 2.0, 120.0f);
        for (int i = 0; i < n; ++i)
            if (out[(size_t) i].on) { ons.push_back (out[(size_t) i].note); onTimes.push_back ((int) (t0 + out[(size_t) i].sample)); }
        t0 += 512;
    }
    MBX_CHECK (ons.size() >= 5, "%d notes", (int) ons.size());
    MBX_CHECK (ons[0] == 60 && ons[1] == 64 && ons[2] == 67 && ons[3] == 60, "order %d %d %d %d", ons[0], ons[1], ons[2], ons[3]);
    MBX_CHECK (std::abs ((onTimes[1] - onTimes[0]) - 6000) <= 1, "step spacing %d", onTimes[1] - onTimes[0]);
}

MBX_TEST (sequencer_applies_pitch_and_ratchets)
{
    dsp::ArpSeq arp;
    arp.prepare (48000.0f);
    dsp::ArpSettings s;
    s.mode = dsp::ArpMode::Seq;
    s.seqLength = 2;
    s.seqRateDivision = 11; // 1/4 note = 24000 samples at 120 BPM
    auto pat = dsp::SeqPattern::makeDefault();
    pat.steps[0] = { true, 0.0f, 0.9f, 0.5f, 1.0f, 1, false, 0.25f, 0.0f };
    pat.steps[1] = { true, 7.0f, 0.9f, 0.5f, 1.0f, 3, false, 0.75f, 1.0f };
    dsp::NoteEvent in[1] = { { 0, true, 48, 1.0f, 0 } };
    std::array<dsp::NoteEvent, 64> out {};
    std::vector<int> ons;
    for (int block = 0; block < 100; ++block) // ~1.07 s
    {
        const int n = arp.process (s, pat, block == 0 ? in : nullptr, block == 0 ? 1 : 0, out.data(), 64, 512, -1.0, 120.0f);
        for (int i = 0; i < n; ++i)
            if (out[(size_t) i].on) ons.push_back (out[(size_t) i].note);
    }
    // step 0: one C3, step 1: three G3 ratchets, step 0 again...
    MBX_CHECK (ons.size() >= 4 && ons[0] == 48 && ons[1] == 55 && ons[2] == 55 && ons[3] == 55, "got %d notes", (int) ons.size());
}

MBX_TEST (lufs_meter_matches_reference)
{
    Meter m;
    m.prepare (48000.0);
    std::vector<float> l (48000 * 4), r (48000 * 4);
    for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.1f * std::sin (dsp::kTwoPi * 1000.0f * (float) i / 48000.0f);
    for (size_t pos = 0; pos < l.size(); pos += 512)
        m.process (l.data() + pos, r.data() + pos, 512);
    const auto s = m.read();
    // BS.1770: two channels of a -20 dBFS-peak 1 kHz sine -> about -20.7 LUFS (K-weighting adds ~+0.7 dB at 1 kHz)
    std::printf ("         LUFS-S %.2f  LUFS-M %.2f  true peak %.3f\n", s.lufsS, s.lufsM, s.truePeak);
    MBX_CHECK (s.lufsS > -21.5f && s.lufsS < -19.0f, "short-term %.2f", s.lufsS);
    MBX_CHECK (std::abs (s.truePeak - 0.1f) < 0.01f && ! s.clip);
}
