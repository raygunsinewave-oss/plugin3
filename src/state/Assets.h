// Non-parameter state shared with the audio thread: drawable shapes (MSEG + custom LFO
// waves), imported wavetables, granular samples and macro names.
//
// Audio-thread access is lock-free:
//  * Shapes use a seqlock: the audio thread copies a shape only when its version changes.
//  * Wavetables/samples are published by atomic pointer swap; replaced objects are retired
//    and freed on the message thread a few seconds later, never on the audio thread.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include "../dsp/ArpSeq.h"
#include "../dsp/SynthEngine.h"

namespace mbx
{
class ShapeStore
{
public:
    static constexpr int kMsegIndex = 0;
    static constexpr int kNumShapes = 1 + dsp::kNumLfos; // MSEG + one custom wave per LFO
    static int lfoIndex (int lfo) noexcept { return 1 + lfo; }

    ShapeStore()
    {
        set (kMsegIndex, dsp::Shape::defaultMseg());
        for (int l = 0; l < dsp::kNumLfos; ++l)
            set (lfoIndex (l), dsp::Shape::defaultCustom());
    }

    /** Writer side (message thread only). */
    void set (int index, dsp::Shape shape) noexcept
    {
        shape.sanitise();
        auto& s = slots[(size_t) index];
        s.seq.fetch_add (1, std::memory_order_acq_rel); // odd: write in progress
        std::atomic_thread_fence (std::memory_order_release);
        s.shape = shape;
        s.seq.fetch_add (1, std::memory_order_release);
        changeCounter.fetch_add (1, std::memory_order_relaxed);
    }

    /** Message-thread read (the only writer is the message thread, so this is race-free). */
    dsp::Shape get (int index) const noexcept { return slots[(size_t) index].shape; }

    /** Audio-thread read. Copies into `out` only if the shape changed since `seenVersion`. */
    void readIfChanged (int index, dsp::Shape& out, uint32_t& seenVersion) const noexcept
    {
        const auto& s = slots[(size_t) index];
        const uint32_t v1 = s.seq.load (std::memory_order_acquire);
        if ((v1 & 1u) != 0 || v1 == seenVersion)
            return;
        dsp::Shape copy = s.shape;
        std::atomic_thread_fence (std::memory_order_acquire);
        if (s.seq.load (std::memory_order_relaxed) == v1)
        {
            out = copy;
            seenVersion = v1;
        }
    }

    uint32_t getChangeCounter() const noexcept { return changeCounter.load (std::memory_order_relaxed); }

    static juce::String serialise (const dsp::Shape& s);
    static dsp::Shape parse (const juce::String& text, const dsp::Shape& fallback);

private:
    struct Slot
    {
        std::atomic<uint32_t> seq { 0 };
        dsp::Shape shape;
    };
    std::array<Slot, kNumShapes> slots;
    std::atomic<uint32_t> changeCounter { 0 };
};

/** Sequencer pattern with the same seqlock scheme as ShapeStore. */
class SeqStore
{
public:
    SeqStore() { set (dsp::SeqPattern::makeDefault()); }
    void set (const dsp::SeqPattern& p) noexcept
    {
        seq.fetch_add (1, std::memory_order_acq_rel);
        std::atomic_thread_fence (std::memory_order_release);
        pattern = p;
        seq.fetch_add (1, std::memory_order_release);
    }
    dsp::SeqPattern get() const noexcept { return pattern; }
    void readIfChanged (dsp::SeqPattern& out, uint32_t& seen) const noexcept
    {
        const uint32_t v1 = seq.load (std::memory_order_acquire);
        if ((v1 & 1u) != 0 || v1 == seen) return;
        dsp::SeqPattern copy = pattern;
        std::atomic_thread_fence (std::memory_order_acquire);
        if (seq.load (std::memory_order_relaxed) == v1) { out = copy; seen = v1; }
    }
    uint32_t version() const noexcept { return seq.load (std::memory_order_relaxed); }

private:
    std::atomic<uint32_t> seq { 0 };
    dsp::SeqPattern pattern;
};

/** Atomically published immutable object with deferred destruction. */
template <typename T>
class AssetSlot
{
public:
    const T* get() const noexcept { return current.load (std::memory_order_acquire); }

    void publish (std::unique_ptr<T> object, const juce::String& source)
    {
        const juce::ScopedLock sl (lock);
        auto* raw = object.get();
        if (owned != nullptr)
            retired.push_back ({ std::move (owned), juce::Time::getMillisecondCounterHiRes() });
        owned = std::move (object);
        current.store (raw, std::memory_order_release);
        sourcePath = source;
    }

    void clear() { publish (nullptr, {}); }

    void collectGarbage()
    {
        const juce::ScopedLock sl (lock);
        const double now = juce::Time::getMillisecondCounterHiRes();
        retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const auto& r) { return now - r.second > 3000.0; }),
                       retired.end());
    }

    juce::String getSourcePath() const
    {
        const juce::ScopedLock sl (lock);
        return sourcePath;
    }

private:
    juce::CriticalSection lock; // never taken by the audio thread
    std::atomic<const T*> current { nullptr };
    std::unique_ptr<T> owned;
    std::vector<std::pair<std::unique_ptr<T>, double>> retired;
    juce::String sourcePath;
};

class Assets final : private juce::Timer
{
public:
    Assets();
    ~Assets() override;

    ShapeStore shapes;
    SeqStore sequence;
    std::array<AssetSlot<dsp::Wavetable>, dsp::kNumOscillators> userTables;
    std::array<AssetSlot<dsp::SampleData>, dsp::kNumOscillators> samples;

    juce::String getMacroName (int m) const;
    void setMacroName (int m, const juce::String& name);

    /** Loads WAV/AIFF/FLAC. Frames of 2048 samples are detected (Serum-style tables);
        short files become a single cycle. Returns an error message or empty on success. */
    juce::String loadWavetable (int osc, const juce::File& file);
    juce::String loadSample (int osc, const juce::File& file);
    /** Builds a one-frame user table from a drawn single cycle. */
    void setDrawnWavetable (int osc, const std::vector<float>& cycle);

    /** Serialises everything into / restores from a child of the plugin state tree. */
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

    static constexpr const char* kTreeType = "MBX_ASSETS";
    static juce::String serialisePattern (const dsp::SeqPattern& pat);

private:
    void timerCallback() override;
    std::unique_ptr<juce::AudioFormatReader> openReader (const juce::File& file, juce::String& error);

    juce::AudioFormatManager formats;
    std::array<std::vector<float>, dsp::kNumOscillators> drawnCycles; // persisted for drawn tables
    mutable juce::CriticalSection nameLock;
    juce::StringArray macroNames;
};

} // namespace mbx
