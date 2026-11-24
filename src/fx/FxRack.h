// FX rack (10 serial slots + 2 send buses), master EQ, output limiter and metering.
// Effect objects are created on the message thread (timer or prepare) and published with
// an atomic pointer swap; replaced objects are freed later, never on the audio thread.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../dsp/FxCommon.h"
#include "../params/Parameters.h"

namespace mbx
{
class FxRack final : private juce::Timer
{
public:
    static constexpr int kTotalSlots = params::kNumFxSlots + 2;

    explicit FxRack (juce::AudioProcessorValueTreeState& state);
    ~FxRack() override;

    /** Message thread: (re)creates every unit for the current types at this rate. */
    void prepare (double sampleRate, int maxBlock);
    /** Message thread: create/replace units whose type parameter changed. */
    void syncUnits();

    /** Audio thread. Processes the rack, sends, EQ and output limiter in place. */
    void process (float* L, float* R, int n, const dsp::fx::FxContext& ctx) noexcept;

    dsp::fx::FxType getUnitType (int slot) const noexcept;
    float getActivity (int slot) const noexcept;

    /** Message thread: swaps every parameter of two rack slots (drag-to-reorder). */
    void swapSlots (int a, int b);
    /** Message thread: loads a type's default parameter values into a slot. */
    void applyDefaults (int slot, dsp::fx::FxType type);

    /** Combined master-EQ magnitude at hz (UI curve). */
    float eqMagnitudeDb (float hz) const noexcept;

private:
    void timerCallback() override { syncUnits(); }

    using P = std::atomic<float>*;
    struct Slot
    {
        P type {}, on {}, mix {}, send {};
        std::array<P, dsp::fx::kParamsPerFx> p {};
        std::atomic<dsp::fx::FxUnit*> unit { nullptr };
        std::unique_ptr<dsp::fx::FxUnit> owned;
        float mixSmoothed = 0.0f;
    };

    juce::AudioProcessorValueTreeState& state;
    std::array<Slot, kTotalSlots> slots;
    std::vector<std::pair<std::unique_ptr<dsp::fx::FxUnit>, double>> retired;
    juce::CriticalSection createLock; // message-thread only

    // master EQ
    P eqOn {};
    std::array<P, params::kNumEqBands> eqBandOn {}, eqFreq {}, eqGain {}, eqQ {};
    std::array<dsp::fx::Biquad, params::kNumEqBands * 2> eq;
    std::array<dsp::fx::Biquad, params::kNumEqBands> eqUi; // mirror for the UI curve
    std::array<float, params::kNumEqBands * 3> eqCache {};
    P outLimiter {}, outCeiling {};
    std::unique_ptr<dsp::fx::FxUnit> limiter;

    std::vector<float> dryL, dryR, sendL, sendR, accL, accR;
    double rate = 48000.0;
    int blockSize = 512;
};

//==============================================================================
/** Output metering: peak, RMS, LUFS (momentary / short-term), approx true peak, clip latch. */
class Meter
{
public:
    void prepare (double sampleRate);
    void process (const float* L, const float* R, int n) noexcept;

    struct Snapshot
    {
        float peakL = 0, peakR = 0, rmsL = 0, rmsR = 0, truePeak = 0;
        float lufsM = -100.0f, lufsS = -100.0f;
        bool clip = false;
    };
    Snapshot read() const noexcept;
    void resetClip() noexcept { clip.store (false); }

private:
    double rate = 48000.0;
    dsp::fx::Biquad kShelf[2], kHigh[2];
    float rmsStateL = 0, rmsStateR = 0, rmsCoef = 0.001f, lastL = 0, lastR = 0, prevL = 0, prevR = 0;
    // 100 ms blocks of K-weighted mean square, 30 blocks = 3 s
    std::array<double, 30> blocks {};
    int blockIndex = 0, blockFill = 0, blockLen = 4800, blocksValid = 0;
    double blockAcc = 0.0;

    std::atomic<float> peakL { 0 }, peakR { 0 }, rmsL { 0 }, rmsR { 0 }, truePeak { 0 }, lufsM { -100 }, lufsS { -100 };
    std::atomic<bool> clip { false };
};

} // namespace mbx
