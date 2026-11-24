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
        MysteryBoxProcessor p;
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
    MysteryBoxProcessor p;
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
    MysteryBoxProcessor a;
    juce::Random rng (99);
    for (auto* param : a.getParameters())
        param->setValueNotifyingHost (rng.nextFloat());

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    MysteryBoxProcessor b;
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
    MysteryBoxProcessor p;
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
    MysteryBoxProcessor p;
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

    constexpr int blocks = 940; // ~10 s of audio
    const auto t0 = juce::Time::getHighResolutionTicks();
    for (int b = 0; b < blocks; ++b)
        p.processBlock (buffer, midi);
    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const double load = secs / (blocks * 512.0 / 48000.0) * 100.0;
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
    MysteryBoxProcessor p;
    setParam (p, params::id::oversampling, 1.0f);
    setParam (p, params::id::masterVolume, -9.0f);
    p.prepareToPlay (sr, block);

    juce::AudioBuffer<float> out (2, (int) (sr * 22.0));
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
int screenshot (const juce::File& file, int tab, int osc)
{
    MysteryBoxProcessor p;
    setParam (p, params::oscId (0, params::id::oscType), 0.0f);
    setParam (p, params::oscId (0, params::id::oscTable), 3.0f);
    setParam (p, params::oscId (0, params::id::oscWtPos), 0.4f);
    setParam (p, params::oscId (0, params::id::oscUnison), 7.0f);
    setParam (p, params::oscId (1, params::id::oscOn), 1.0f);
    setParam (p, params::oscId (1, params::id::oscType), 4.0f);
    setParam (p, params::id::env1Attack, 0.08f);
    setParam (p, params::id::env1Punch, 0.4f);
    p.prepareToPlay (48000.0, 512);
    renderNote (p, 30, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (MysteryBoxEditor::kBaseWidth, MysteryBoxEditor::kBaseHeight);

    // drive the tab/osc buttons the same way a user would
    std::function<void (juce::Component&)> click = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (child))
            {
                const auto t = b->getButtonText();
                static const char* tabs[] = { "SYNTH", "PRESETS", "FX", "ADVANCED", "XD", "SETTINGS" };
                if (t == tabs[juce::jlimit (0, 5, tab)] || t == "OSC " + juce::String (osc + 1))
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
