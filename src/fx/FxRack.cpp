#include "FxRack.h"

namespace mbx
{
using namespace dsp::fx;

FxRack::FxRack (juce::AudioProcessorValueTreeState& s) : state (s)
{
    auto get = [&s] (const juce::String& id) { auto* p = s.getRawParameterValue (id); jassert (p != nullptr); return p; };
    for (int k = 0; k < kTotalSlots; ++k)
    {
        auto& sl = slots[(size_t) k];
        sl.type = get (params::fxId (k, params::id::fxType));
        sl.on = get (params::fxId (k, params::id::fxOn));
        sl.mix = get (params::fxId (k, params::id::fxMix));
        if (k >= params::kNumFxSlots)
            sl.send = get (params::fxId (k, params::id::fxSend));
        for (int p = 0; p < kParamsPerFx; ++p)
            sl.p[(size_t) p] = get (params::fxParamId (k, p));
    }
    eqOn = get (params::id::eqOn);
    for (int b = 0; b < params::kNumEqBands; ++b)
    {
        eqBandOn[(size_t) b] = get (params::eqId (b, params::id::eqBandOn));
        eqFreq[(size_t) b] = get (params::eqId (b, params::id::eqFreq));
        eqGain[(size_t) b] = get (params::eqId (b, params::id::eqGain));
        eqQ[(size_t) b] = get (params::eqId (b, params::id::eqQ));
    }
    outLimiter = get (params::id::outLimiter);
    outCeiling = get (params::id::outCeiling);
    eqCache.fill (-1.0f);
    startTimerHz (15);
}

FxRack::~FxRack()
{
    stopTimer();
}

void FxRack::prepare (double sampleRate, int maxBlock)
{
    const juce::ScopedLock sl (createLock);
    rate = sampleRate;
    blockSize = std::max (1, maxBlock);
    for (auto* v : { &dryL, &dryR, &sendL, &sendR, &accL, &accR })
        v->assign ((size_t) blockSize, 0.0f);
    for (auto& s : slots)
    {
        s.unit.store (nullptr);
        if (s.owned != nullptr)
            retired.push_back ({ std::move (s.owned), 0.0 });
        s.mixSmoothed = 0.0f;
    }
    retired.clear(); // audio is stopped during prepare, so immediate destruction is safe
    limiter = createFx (FxType::Limiter);
    limiter->prepare ((float) rate, blockSize);
    for (auto& b : eq) b.reset();
    eqCache.fill (-1.0f);
    syncUnits();
}

void FxRack::syncUnits()
{
    const juce::ScopedLock sl (createLock);
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto& s : slots)
    {
        const auto want = (FxType) juce::jlimit (0, (int) FxType::Count - 1, juce::roundToInt (s.type->load()));
        auto* current = s.unit.load();
        const auto have = current != nullptr ? current->getType() : FxType::Off;
        if (want == have)
            continue;
        std::unique_ptr<FxUnit> fresh = createFx (want);
        if (fresh != nullptr)
            fresh->prepare ((float) rate, blockSize);
        s.unit.store (fresh.get());
        if (s.owned != nullptr)
            retired.push_back ({ std::move (s.owned), now });
        s.owned = std::move (fresh);
    }
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const auto& r) { return now - r.second > 2000.0; }), retired.end());
}

FxType FxRack::getUnitType (int slot) const noexcept
{
    auto* u = slots[(size_t) slot].unit.load();
    return u != nullptr ? u->getType() : FxType::Off;
}

float FxRack::getActivity (int slot) const noexcept
{
    auto* u = slots[(size_t) slot].unit.load();
    return u != nullptr ? u->getActivity() : 0.0f;
}

void FxRack::swapSlots (int a, int b)
{
    if (a == b || a < 0 || b < 0 || a >= params::kNumFxSlots || b >= params::kNumFxSlots)
        return;
    juce::StringArray ids { params::id::fxType, params::id::fxOn, params::id::fxMix };
    for (int p = 0; p < kParamsPerFx; ++p)
        ids.add ("p" + juce::String (p + 1));
    for (const auto& suffix : ids)
    {
        auto* pa = state.getParameter (params::fxPrefix (a) + "_" + suffix);
        auto* pb = state.getParameter (params::fxPrefix (b) + "_" + suffix);
        const float va = pa->getValue(), vb = pb->getValue();
        pa->beginChangeGesture(); pa->setValueNotifyingHost (vb); pa->endChangeGesture();
        pb->beginChangeGesture(); pb->setValueNotifyingHost (va); pb->endChangeGesture();
    }
    syncUnits();
}

void FxRack::applyDefaults (int slot, FxType type)
{
    const auto& info = fxInfo (type);
    if (slot < params::kNumFxSlots)
        if (auto* mix = state.getParameter (params::fxId (slot, params::id::fxMix)))
        {
            mix->beginChangeGesture();
            mix->setValueNotifyingHost (info.defaultMix);
            mix->endChangeGesture();
        }
    for (int p = 0; p < kParamsPerFx; ++p)
        if (auto* prm = state.getParameter (params::fxParamId (slot, p)))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (info.defaults[(size_t) p]);
            prm->endChangeGesture();
        }
}

float FxRack::eqMagnitudeDb (float hz) const noexcept
{
    if (eqOn->load() < 0.5f)
        return 0.0f;
    float g = 1.0f;
    for (int b = 0; b < params::kNumEqBands; ++b)
        if (eqBandOn[(size_t) b]->load() >= 0.5f)
            g *= eqUi[(size_t) b].magnitude (hz, (float) rate);
    return gainToDb (g);
}

void FxRack::process (float* L, float* R, int n, const FxContext& ctx) noexcept
{
    n = std::min (n, blockSize);
    const float mixStep = 1.0f / std::max (1.0f, 0.02f * (float) rate); // 20 ms bypass crossfades

    // ---------------------------------------------------------------- serial rack
    for (int k = 0; k < params::kNumFxSlots; ++k)
    {
        auto& s = slots[(size_t) k];
        auto* u = s.unit.load (std::memory_order_acquire);
        const float target = (u != nullptr && s.on->load (std::memory_order_relaxed) >= 0.5f)
                                 ? std::clamp (s.mix->load (std::memory_order_relaxed) + modOffsets[(size_t) k], 0.0f, 1.0f) : 0.0f;
        if (u == nullptr || (target <= 0.0f && s.mixSmoothed <= 0.0f))
        {
            s.mixSmoothed = 0.0f;
            continue;
        }
        FxParams p;
        for (int i = 0; i < kParamsPerFx; ++i)
            p[(size_t) i] = s.p[(size_t) i]->load (std::memory_order_relaxed);
        std::copy (L, L + n, dryL.begin());
        std::copy (R, R + n, dryR.begin());
        u->process (L, R, n, p, ctx);
        float m = s.mixSmoothed;
        for (int i = 0; i < n; ++i)
        {
            m += std::clamp (target - m, -mixStep, mixStep);
            L[i] = dryL[(size_t) i] + (L[i] - dryL[(size_t) i]) * m;
            R[i] = dryR[(size_t) i] + (R[i] - dryR[(size_t) i]) * m;
        }
        s.mixSmoothed = m;
    }

    // ---------------------------------------------------------------- send buses (parallel)
    std::fill_n (accL.begin(), n, 0.0f);
    std::fill_n (accR.begin(), n, 0.0f);
    bool anySend = false;
    for (int k = params::kNumFxSlots; k < kTotalSlots; ++k)
    {
        auto& s = slots[(size_t) k];
        auto* u = s.unit.load (std::memory_order_acquire);
        const float send = std::clamp (s.send->load (std::memory_order_relaxed) + modOffsets[(size_t) k], 0.0f, 1.0f);
        const float target = (u != nullptr && s.on->load() >= 0.5f) ? s.mix->load() : 0.0f;
        if (u == nullptr || send <= 0.0f || (target <= 0.0f && s.mixSmoothed <= 0.0f))
        {
            s.mixSmoothed = 0.0f;
            continue;
        }
        for (int i = 0; i < n; ++i)
        {
            sendL[(size_t) i] = L[i] * send;
            sendR[(size_t) i] = R[i] * send;
        }
        FxParams p;
        for (int i = 0; i < kParamsPerFx; ++i)
            p[(size_t) i] = s.p[(size_t) i]->load (std::memory_order_relaxed);
        u->process (sendL.data(), sendR.data(), n, p, ctx);
        float m = s.mixSmoothed;
        for (int i = 0; i < n; ++i)
        {
            m += std::clamp (target - m, -mixStep, mixStep);
            accL[(size_t) i] += sendL[(size_t) i] * m;
            accR[(size_t) i] += sendR[(size_t) i] * m;
        }
        s.mixSmoothed = m;
        anySend = true;
    }
    if (anySend)
        for (int i = 0; i < n; ++i)
        {
            L[i] += accL[(size_t) i];
            R[i] += accR[(size_t) i];
        }

    // ---------------------------------------------------------------- master EQ
    if (eqOn->load (std::memory_order_relaxed) >= 0.5f)
    {
        for (int b = 0; b < params::kNumEqBands; ++b)
        {
            if (eqBandOn[(size_t) b]->load() < 0.5f)
                continue;
            const float f = eqFreq[(size_t) b]->load(), g = eqGain[(size_t) b]->load(), q = eqQ[(size_t) b]->load();
            auto* c = &eqCache[(size_t) b * 3];
            if (! dsp::exactlyEqual (c[0], f) || ! dsp::exactlyEqual (c[1], g) || ! dsp::exactlyEqual (c[2], q))
            {
                c[0] = f; c[1] = g; c[2] = q;
                const auto kind = b == 0 ? Biquad::Kind::LowShelf : b == params::kNumEqBands - 1 ? Biquad::Kind::HighShelf : Biquad::Kind::Peak;
                eq[(size_t) b * 2].set (kind, f, q, g, (float) rate);
                eq[(size_t) b * 2 + 1].set (kind, f, q, g, (float) rate);
                eqUi[(size_t) b].set (kind, f, q, g, (float) rate);
            }
            if (std::abs (g) < 0.01f)
                continue;
            auto& bl = eq[(size_t) b * 2];
            auto& br = eq[(size_t) b * 2 + 1];
            for (int i = 0; i < n; ++i)
            {
                L[i] = bl.process (L[i]);
                R[i] = br.process (R[i]);
            }
        }
    }

    // ---------------------------------------------------------------- output safety limiter
    if (outLimiter->load (std::memory_order_relaxed) >= 0.5f && limiter != nullptr)
    {
        const float ceilingNorm = (outCeiling->load() + 12.0f) / 12.0f; // maps -12..0 dB onto the limiter's ceiling range
        limiter->process (L, R, n, { 0.0f, std::clamp (ceilingNorm, 0.0f, 1.0f), 0.25f, 0, 0, 0 }, ctx);
    }
}

//==============================================================================
void Meter::prepare (double sampleRate)
{
    rate = sampleRate;
    // ITU-R BS.1770 K-weighting approximated with RBJ sections at the given rate
    for (int c = 0; c < 2; ++c)
    {
        kShelf[c].set (Biquad::Kind::HighShelf, 1681.97f, 0.7071f, 3.99984f, (float) rate);
        kHigh[c].set (Biquad::Kind::HighPass, 38.1355f, 0.5003f, 0.0f, (float) rate);
        kShelf[c].reset();
        kHigh[c].reset();
    }
    rmsCoef = 1.0f - std::exp (-1.0f / (0.3f * (float) rate));
    blockLen = std::max (1, (int) (0.1 * rate));
    blocks.fill (0.0);
    blockIndex = blockFill = blocksValid = 0;
    blockAcc = 0.0;
    rmsStateL = rmsStateR = 0.0f;
}

void Meter::process (const float* L, const float* R, int n) noexcept
{
    float pl = 0.0f, pr = 0.0f, tp = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float l = L[i], r = R[i];
        pl = std::max (pl, std::abs (l));
        pr = std::max (pr, std::abs (r));
        rmsStateL += (l * l - rmsStateL) * rmsCoef;
        rmsStateR += (r * r - rmsStateR) * rmsCoef;

        // approximate inter-sample peaks: Catmull-Rom midpoint between the previous two samples
        const float midL = 0.5625f * (prevL + lastL) - 0.0625f * (l + (2.0f * prevL - lastL));
        const float midR = 0.5625f * (prevR + lastR) - 0.0625f * (r + (2.0f * prevR - lastR));
        tp = std::max ({ tp, std::abs (midL), std::abs (midR), std::abs (l), std::abs (r) });
        prevL = lastL; lastL = l; prevR = lastR; lastR = r;

        const float kl = kHigh[0].process (kShelf[0].process (l));
        const float kr = kHigh[1].process (kShelf[1].process (r));
        blockAcc += (double) kl * kl + (double) kr * kr;
        if (++blockFill >= blockLen)
        {
            blocks[(size_t) blockIndex] = blockAcc / blockLen;
            blockIndex = (blockIndex + 1) % (int) blocks.size();
            blocksValid = std::min (blocksValid + 1, (int) blocks.size());
            blockAcc = 0.0;
            blockFill = 0;

            auto loudness = [this] (int count) {
                double sum = 0.0;
                const int c = std::min (count, blocksValid);
                for (int b = 0; b < c; ++b)
                    sum += blocks[(size_t) ((blockIndex - 1 - b + (int) blocks.size()) % (int) blocks.size())];
                const double ms = c > 0 ? sum / c : 0.0;
                return ms > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (ms)) : -100.0f;
            };
            lufsM.store (loudness (4), std::memory_order_relaxed);   // 400 ms
            lufsS.store (loudness (30), std::memory_order_relaxed);  // 3 s
        }
    }
    peakL.store (pl, std::memory_order_relaxed);
    peakR.store (pr, std::memory_order_relaxed);
    rmsL.store (std::sqrt (rmsStateL), std::memory_order_relaxed);
    rmsR.store (std::sqrt (rmsStateR), std::memory_order_relaxed);
    truePeak.store (tp, std::memory_order_relaxed);
    if (tp >= 1.0f)
        clip.store (true, std::memory_order_relaxed);
}

Meter::Snapshot Meter::read() const noexcept
{
    Snapshot s;
    s.peakL = peakL.load(); s.peakR = peakR.load(); s.rmsL = rmsL.load(); s.rmsR = rmsR.load();
    s.truePeak = truePeak.load(); s.lufsM = lufsM.load(); s.lufsS = lufsS.load(); s.clip = clip.load();
    return s;
}

} // namespace mbx
