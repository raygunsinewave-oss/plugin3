#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/ArpSeq.h"
#include "dsp/SynthEngine.h"
#include "fx/FxRack.h"
#include "params/Parameters.h"
#include "state/Assets.h"
#include "state/GlobalSettings.h"
#include "state/MidiLearn.h"
#include "state/Tuning.h"

namespace mbx
{
/** Lock-free single-writer ring of recent output samples for GUI visualisers. */
class ScopeTap
{
public:
    static constexpr int kSize = 8192;

    void push (const float* l, const float* r, int n) noexcept
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            left[(size_t) w].store (l[i], std::memory_order_relaxed);
            right[(size_t) w].store (r[i], std::memory_order_relaxed);
            w = (w + 1) & (kSize - 1);
        }
        writePos.store (w, std::memory_order_release);
    }

    /** Newest n samples, mono sum (oldest first). GUI thread. */
    void copyLatest (float* dest, int n) const noexcept
    {
        n = std::min (n, kSize);
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const auto idx = (size_t) ((w - n + i) & (kSize - 1));
            dest[i] = 0.5f * (left[idx].load (std::memory_order_relaxed) + right[idx].load (std::memory_order_relaxed));
        }
    }

    void copyLatestStereo (float* l, float* r, int n) const noexcept
    {
        n = std::min (n, kSize);
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const auto idx = (size_t) ((w - n + i) & (kSize - 1));
            l[i] = left[idx].load (std::memory_order_relaxed);
            r[i] = right[idx].load (std::memory_order_relaxed);
        }
    }

private:
    std::array<std::atomic<float>, kSize> left {}, right {};
    std::atomic<int> writePos { 0 };
};

/** Factory wavetables: built once per process (thread-safe static init), shared by all instances. */
inline const dsp::WavetableBank& factoryWavetables()
{
    static const dsp::WavetableBank bank;
    return bank;
}

class MysteryBoxProcessor final : public juce::AudioProcessor,
                                  public MidiLearnHost,
                                  private juce::AsyncUpdater
{
public:
    MysteryBoxProcessor();
    ~MysteryBoxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mystery Box"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    const dsp::WavetableBank& getWavetables() const noexcept { return wavetables; }
    const ScopeTap& getScope() const noexcept { return scope; }
    double getCpuLoad() const { return loadMeasurer.getLoadAsProportion(); }
    int getActiveVoices() const noexcept { return activeVoices.load (std::memory_order_relaxed); }
    Assets& getAssets() noexcept { return assets; }
    FxRack& getFxRack() noexcept { return fxRack; }
    Meter& getOutputMeter() noexcept { return outMeter; }
    MidiLearn& getMidiLearn() noexcept override { return midiLearn; }
    Tuning& getTuning() noexcept { return tuning; }
    Meter& getInputMeter() noexcept { return inMeter; }
    bool isSidechainActive() const noexcept { return sidechainActive.load (std::memory_order_relaxed); }
    int getSeqStep() const noexcept { return seqStep.load (std::memory_order_relaxed); }
    double getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }
    double getSampleRateHz() const noexcept { return hostSampleRate; }
    int getOversamplingFactor() const noexcept { return 1 << std::max (0, currentOs); }

    /** Live modulation of the newest voice, per destination — UI only. */
    float getLiveModulation (int destination) const noexcept
    {
        return juce::isPositiveAndBelow (destination, dsp::dest::Count) ? liveMod[(size_t) destination].load (std::memory_order_relaxed) : 0.0f;
    }
    float getLiveLfo (int lfo) const noexcept { return liveLfo[(size_t) lfo].load (std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outputPeak.load (std::memory_order_relaxed); }

    /** Notes injected from the UI (on-screen keyboard, XD games). Thread-safe, lock-free (single producer). */
    void injectNote (int note, float velocity, bool on) noexcept;

    static constexpr int kStateVersion = 4;

private:
    void handleAsyncUpdate() override;
    void handleMidi (const juce::MidiMessage& m) noexcept;
    void renderRange (float* l, float* r, int start, int end, int factor) noexcept;
    dsp::ArpSettings readArpSettings() const noexcept;

    Assets assets; // declared before the APVTS so it outlives parameter listeners
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    params::ParameterCache paramCache;
    const dsp::WavetableBank& wavetables = factoryWavetables();
    FxRack fxRack { apvts };
    MidiLearn midiLearn { *this };
    Tuning tuning;
    std::atomic<float>* dcBlockParam = nullptr;
    float dcStateL = 0.0f, dcStateR = 0.0f, dcPrevL = 0.0f, dcPrevR = 0.0f;
    Meter outMeter, inMeter;

    dsp::SynthEngine engine;
    dsp::EngineSettings settings;
    dsp::ArpSeq arpSeq;
    dsp::SeqPattern audioPattern = dsp::SeqPattern::makeDefault();
    uint32_t patternVersion = 0xffffffffu;
    dsp::ArpMode lastArpMode = dsp::ArpMode::Off;
    std::array<dsp::NoteEvent, 512> inNotes {}, outNotes {};
    juce::MidiBuffer processedMidi;
    std::vector<float> sideL, sideR;

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 4> oversamplers; // [0] unused (1x)
    int currentOs = -1;
    double hostSampleRate = 48000.0;
    int maxBlock = 512;

    std::array<dsp::Shape, ShapeStore::kNumShapes> audioShapes {};
    std::array<uint32_t, ShapeStore::kNumShapes> shapeVersions {};

    struct Injected { int note; float velocity; bool on; };
    juce::AbstractFifo injectFifo { 256 };
    std::array<Injected, 256> injectBuf {};

    std::array<std::atomic<float>*, 13> arpP {};

    ScopeTap scope;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::atomic<int> activeVoices { 0 }, seqStep { 0 };
    std::atomic<bool> sidechainActive { false };
    std::atomic<double> hostBpm { 120.0 };
    std::array<std::atomic<float>, (size_t) dsp::dest::Count> liveMod {};
    std::array<std::atomic<float>, dsp::kNumLfos> liveLfo {};
    std::atomic<float> outputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MysteryBoxProcessor)
};

} // namespace mbx
