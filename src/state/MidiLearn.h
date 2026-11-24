// MIDI CC -> parameter mapping with learn mode.
// Audio thread: lock-free capture of CC values. Message thread: applies them to parameters
// (with proper host gestures) on a timer, so the host is never notified from the audio thread.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mbx
{
class MidiLearn;

/** Implemented by the processor so UI widgets can reach the MIDI-learn map without knowing the processor type. */
struct MidiLearnHost
{
    virtual ~MidiLearnHost() = default;
    virtual MidiLearn& getMidiLearn() noexcept = 0;
};

class MidiLearn final : private juce::Timer
{
public:
    explicit MidiLearn (juce::AudioProcessor& p) : processor (p)
    {
        for (auto& m : map) m.store (-1);
        for (auto& v : pending) v.store (-1.0f);
        startTimerHz (60);
    }
    ~MidiLearn() override { stopTimer(); }

    /** Audio thread. Returns true if the CC was consumed by a mapping or learn. */
    bool handleCc (int cc, int value) noexcept
    {
        if (cc < 0 || cc > 127)
            return false;
        const int learn = learnTarget.load (std::memory_order_acquire);
        if (learn >= 0)
        {
            for (auto& m : map) // one CC per parameter
                if (m.load (std::memory_order_relaxed) == learn) m.store (-1, std::memory_order_relaxed);
            map[(size_t) cc].store (learn, std::memory_order_release);
            learnTarget.store (-1, std::memory_order_release);
            changed.store (true);
        }
        if (map[(size_t) cc].load (std::memory_order_acquire) < 0)
            return false;
        pending[(size_t) cc].store ((float) value / 127.0f, std::memory_order_release);
        return true;
    }

    void startLearn (int paramIndex) noexcept { learnTarget.store (paramIndex); }
    int learningParam() const noexcept { return learnTarget.load(); }
    void clearParam (int paramIndex) noexcept
    {
        for (auto& m : map)
            if (m.load() == paramIndex) m.store (-1);
        changed.store (true);
    }
    void clearAll() noexcept { for (auto& m : map) m.store (-1); changed.store (true); }

    int ccForParam (int paramIndex) const noexcept
    {
        for (int cc = 0; cc < 128; ++cc)
            if (map[(size_t) cc].load (std::memory_order_relaxed) == paramIndex) return cc;
        return -1;
    }
    int paramForCc (int cc) const noexcept { return map[(size_t) cc].load (std::memory_order_relaxed); }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree t ("MBX_MIDIMAP");
        for (int cc = 0; cc < 128; ++cc)
            if (const int p = map[(size_t) cc].load(); p >= 0)
                if (auto* prm = dynamic_cast<juce::AudioProcessorParameterWithID*> (processor.getParameters()[p]))
                    t.setProperty ("cc" + juce::String (cc), prm->paramID, nullptr);
        return t;
    }

    void fromValueTree (const juce::ValueTree& t)
    {
        clearAll();
        if (! t.hasType ("MBX_MIDIMAP"))
            return;
        const auto& params = processor.getParameters();
        for (int cc = 0; cc < 128; ++cc)
        {
            const juce::String id = t["cc" + juce::String (cc)];
            if (id.isEmpty()) continue;
            for (int i = 0; i < params.size(); ++i)
                if (auto* prm = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]); prm != nullptr && prm->paramID == id)
                    map[(size_t) cc].store (i);
        }
    }

private:
    void timerCallback() override
    {
        const auto& params = processor.getParameters();
        for (int cc = 0; cc < 128; ++cc)
        {
            const float v = pending[(size_t) cc].exchange (-1.0f, std::memory_order_acq_rel);
            const int p = map[(size_t) cc].load();
            if (v < 0.0f || p < 0 || p >= params.size())
                continue;
            auto* prm = params[p];
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (v);
            prm->endChangeGesture();
        }
    }

    juce::AudioProcessor& processor;
    std::array<std::atomic<int>, 128> map;
    std::array<std::atomic<float>, 128> pending;
    std::atomic<int> learnTarget { -1 };
    std::atomic<bool> changed { false };
};

} // namespace mbx
