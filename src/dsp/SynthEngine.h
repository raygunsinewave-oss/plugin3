// Voices + polyphonic voice manager. JUCE-free and real-time safe: everything is
// pre-allocated in prepare(); rendering runs in fixed control-rate chunks where
// modulation is evaluated, then audio is generated sample-by-sample.
#pragma once

#include "Envelope.h"
#include "Filter.h"
#include "Oscillator.h"

namespace mbx::dsp
{
enum class VoiceMode : int { Poly = 0, Mono, Legato, Unison };
enum class GlideMode : int { Always = 0, LegatoOnly };
enum class FilterRouting : int { Serial = 0, Parallel, Split };
enum class SubShape : int { Sine = 0, Triangle, Square, Saw };

struct SubSettings        { bool on = false; SubShape shape = SubShape::Sine; int octavesDown = 1; float level = 0.5f; OscRoute route = OscRoute::Filter1; };
struct NoiseLayerSettings { bool on = false; NoiseType type = NoiseType::White; float level = 0.2f; OscRoute route = OscRoute::Filter1; };
struct RingSettings       { bool on = false; int oscA = 0, oscB = 1; float level = 0.5f; OscRoute route = OscRoute::Filter1; };
struct MsegSettings       { float durationSec = 1.0f; bool sync = false; int division = 16; bool loop = false; };

struct FilterParams
{
    FilterSettings dsp {};
    float cutoffHz  = 8000.0f;
    float envAmount = 0.0f; // -1..1 of ENV 2, +-96 semitones
    float keytrack  = 0.0f; // 0..1 (1 = 100%)
    float velocity  = 0.0f; // 0..1, up to +48 semitones at full velocity
};

struct EngineSettings
{
    std::array<OscSettings, kNumOscillators> osc {};
    std::array<Envelope::Params, kNumEnvelopes> env {};          // [0] = amp
    std::array<float, kNumEnvelopes> envVelocity { 0.5f, 0.0f, 0.0f, 0.0f };
    MsegSettings mseg {};
    std::array<LfoSettings, kNumLfos> lfo {};
    std::array<ModSlot, kNumMatrixSlots> matrix {};
    std::array<float, kNumMacros> macros {};
    std::array<FilterParams, kNumFilters> filter {};
    FilterRouting routing = FilterRouting::Serial;
    SubSettings sub {};
    NoiseLayerSettings noise {};
    RingSettings ring {};

    VoiceMode voiceMode   = VoiceMode::Poly;
    float glideTime       = 0.0f;  // seconds
    GlideMode glideMode   = GlideMode::Always;
    float glideCurve      = 1.0f;  // 0 = linear (constant time), 1 = exponential
    int stackVoices       = 1;     // voices per note in Unison voice mode
    float stackDetune     = 20.0f; // cents spread of the voice stack
    bool mpe              = false;
    float mpeBendRange    = 48.0f;
    int polyphony         = 16;
    float bendRange       = 2.0f;
    float masterGain      = 0.5f;

    // Advanced (Phase 4)
    float analogDrift   = 0.0f;  // 0..1, slow per-voice pitch wander (up to +-12 cents)
    float voiceRandom   = 0.0f;  // 0..1, per-note random pitch / cutoff / pan offsets
    int stealMode       = 0;     // 0 quietest, 1 oldest, 2 lowest note, 3 highest note
    float modSmoothMs   = 0.0f;  // smoothing applied to every matrix destination
    float velocityCurve = 0.0f;  // -1..1 (positive = more sensitive)
    float keytrackCurve = 0.0f;  // -1..1
    const float* tuning = nullptr; // 128 per-note semitone offsets (microtuning), or null for 12-TET

    // Resolved by the host layer each block (pointers stay valid for the block).
    const Shape* msegShape = nullptr;
    std::array<const Shape*, kNumLfos> lfoCustom {};

    // Back-compat alias for the amp envelope velocity amount
    float& ampVelocity() noexcept { return envVelocity[0]; }
};

/** MIDI-driven and transport state shared by all voices. */
struct PerformanceState
{
    float modWheel = 0.0f, pitchBend = 0.0f, aftertouch = 0.0f;
    std::array<float, 16> chBend {}, chPressure {}, chTimbre {};
    float bpm = 120.0f;
    std::array<double, kNumLfos> lfoFreeCycles {};
    std::array<float, 2> seqLanes {};
    std::array<float, 4> gameMods {};
};

/** Which oscillator raw signals are needed and in which order to render them (per block). */
struct RenderPlan
{
    std::array<int, kNumOscillators> order { 0, 1, 2, 3, 4, 5 };
    uint32_t rawNeeded = 0;
    uint64_t usedSources = ~0ull; // bit per ModSource that some active slot / filter env reads
    bool uses (ModSource s) const noexcept { return (usedSources >> (int) s) & 1ull; }
};

class Voice
{
public:
    static constexpr int kMaxChunk = 64;

    void prepare (const WavetableBank* bank, uint32_t seed, float maxSampleRate)
    {
        voiceSeed = seed;
        rng.seed (seed * 2654435761u + 17u);
        for (int i = 0; i < kNumOscillators; ++i)
        {
            osc[(size_t) i].setBank (bank);
            osc[(size_t) i].setSeed (seed * 31u + (uint32_t) i * 977u + 1u);
        }
        for (auto& f : filters)
            f.prepare (maxSampleRate);
        noiseL.reset (seed * 11u + 3u);
        noiseR.reset (seed * 19u + 5u);
    }

    void start (int midiNote, float velocity, int midiChannel, const EngineSettings& s, uint64_t order,
                float detuneSemis, float panOffset, float glideFromNote) noexcept
    {
        note       = midiNote;
        vel        = std::pow (std::clamp (velocity, 0.0f, 1.0f), std::exp2 (-s.velocityCurve * 1.5f));
        channel    = std::clamp (midiChannel, 0, 15);
        startOrder = order;
        keyDown    = true;
        sustained  = false;
        active     = true;
        stealing   = false;
        stackDetune = detuneSemis;
        stackPan    = panOffset;
        pitchTarget = (float) midiNote;
        pitchNow    = glideFromNote >= 0.0f ? glideFromNote : pitchTarget;
        glideStart  = pitchNow;
        randomValue = rng.bipolar();
        randPitch   = rng.bipolar() * 0.2f;  // up to +-20 cents at full "per-voice random"
        randCutoff  = rng.bipolar() * 12.0f; // up to +-12 semitones
        randPan     = rng.bipolar() * 0.35f;
        driftPhase  = rng.uniform();
        driftValue  = 0.0f;
        smoothedDest.fill (0.0f);
        firstMod    = true;
        polyPressure = 0.0f;
        msegTime    = 0.0f;
        firstChunk  = true;
        subPhase    = 0.0f;
        noteSerial++;

        for (int i = 0; i < kNumOscillators; ++i)
            osc[(size_t) i].noteOn (s.osc[(size_t) i]);
        for (auto& e : env)
        {
            e.reset();
            e.noteOn();
        }
        for (int k = 0; k < kNumLfos; ++k)
            lfo[(size_t) k].noteOn (s.lfo[(size_t) k], (uint64_t) voiceSeed * (uint64_t) 1000003 + noteSerial * (uint64_t) 7919 + (uint64_t) k);
        for (auto& f : filters)
            f.reset();
    }

    /** Mono mode: new note re-triggers the envelopes (from their current level) and glides. */
    void retrigger (int midiNote, float velocity, const EngineSettings& s, uint64_t order, bool glide) noexcept
    {
        note = midiNote;
        vel = velocity;
        startOrder = order;
        keyDown = true;
        sustained = false;
        setPitchTarget ((float) midiNote, glide);
        for (auto& e : env)
            e.noteOn();
        for (int k = 0; k < kNumLfos; ++k)
            if (s.lfo[(size_t) k].mode != LfoMode::Free)
                lfo[(size_t) k].noteOn (s.lfo[(size_t) k], (uint64_t) voiceSeed * (uint64_t) 1000003 + (++noteSerial) * (uint64_t) 7919 + (uint64_t) k);
        msegTime = 0.0f;
    }

    /** Legato mode: change pitch only. */
    void legatoTo (int midiNote, bool glide) noexcept
    {
        note = midiNote;
        keyDown = true;
        setPitchTarget ((float) midiNote, glide);
    }

    void steal (int midiNote, float velocity, int midiChannel, uint64_t order, float glideFrom,
                float detuneSemis = 0.0f, float panOffset = 0.0f) noexcept
    {
        pendingDetune  = detuneSemis;
        pendingPan     = panOffset;
        if (! active)
            return;
        stealing       = true;
        pendingNote    = midiNote;
        pendingVel     = velocity;
        pendingChannel = midiChannel;
        pendingOrder   = order;
        pendingGlide   = glideFrom;
        pendingKeyUp   = false;
        fadeGain       = 1.0f;
        keyDown        = true;
        sustained      = false;
    }

    void release (bool sustainPedalDown) noexcept
    {
        if (stealing)
        {
            pendingKeyUp = true;
            return;
        }
        keyDown = false;
        if (sustainPedalDown)
            sustained = true;
        else
            for (auto& e : env)
                e.noteOff();
    }

    void releaseSustained() noexcept
    {
        if (sustained && ! keyDown)
        {
            sustained = false;
            for (auto& e : env)
                e.noteOff();
        }
    }

    void kill() noexcept
    {
        active = stealing = keyDown = sustained = false;
        for (auto& e : env)
            e.reset();
    }

    void setPolyPressure (float p) noexcept { polyPressure = p; }

    void render (const EngineSettings& s, const PerformanceState& perf, const RenderPlan& plan, float sr,
                 float* outL, float* outR, int n) noexcept
    {
        if (! active || n <= 0)
            return;

        for (int k = 0; k < kNumEnvelopes; ++k)
            env[(size_t) k].setParams (s.env[(size_t) k], sr);

        // ---------------------------------------------------------------- modulation sources
        std::array<float, (size_t) ModSource::Count> src {};
        src[(size_t) ModSource::Env1] = env[0].getValue();
        for (int k = 1; k < kNumEnvelopes; ++k)
        {
            const float v = env[(size_t) k].next ((float) n); // mod envelopes run at control rate
            const float vs = s.envVelocity[(size_t) k];
            src[(size_t) ModSource::Env1 + (size_t) k] = v * (1.0f - vs + vs * vel);
        }

        if (plan.uses (ModSource::Mseg))
        {
            const float dur = s.mseg.sync ? 60.0f / std::max (perf.bpm, 1.0f) * kSyncDivisionBeats[(size_t) std::clamp (s.mseg.division, 0, 19)]
                                          : std::max (0.005f, s.mseg.durationSec);
            msegTime += (float) n / sr;
            float x = msegTime / dur;
            x = s.mseg.loop ? x - std::floor (x) : std::min (x, 1.0f);
            src[(size_t) ModSource::Mseg] = s.msegShape != nullptr ? s.msegShape->evaluate (x) : 0.0f;
        }

        for (int k = 0; k < kNumLfos; ++k)
        {
            const auto& ls = s.lfo[(size_t) k];
            const float base = ls.sync ? syncDivisionHz (ls.division, perf.bpm) : ls.rateHz;
            const float rmod = lastLfoRateMod[(size_t) k];
            const float rate = exactlyEqual (rmod, 0.0f) ? base : base * std::exp2 (rmod * 4.0f);
            const auto srcId = (ModSource) ((int) ModSource::Lfo1 + k);
            src[(size_t) srcId] = lfo[(size_t) k].advance (ls, s.lfoCustom[(size_t) k], rate, perf.lfoFreeCycles[(size_t) k],
                                                           (uint64_t) 0x5eed + (uint64_t) k * (uint64_t) 977, n, sr, plan.uses (srcId));
        }

        src[(size_t) ModSource::Velocity]   = vel;
        src[(size_t) ModSource::Aftertouch] = std::max ({ polyPressure, perf.aftertouch, s.mpe ? perf.chPressure[(size_t) channel] : 0.0f });
        src[(size_t) ModSource::ModWheel]   = perf.modWheel;
        src[(size_t) ModSource::PitchBend]  = perf.pitchBend;
        src[(size_t) ModSource::Keytrack]   = applyModCurve (std::clamp (((float) note - 60.0f) / 60.0f, -1.0f, 1.0f), s.keytrackCurve);
        src[(size_t) ModSource::Random]     = randomValue;
        for (int m = 0; m < kNumMacros; ++m)
            src[(size_t) ModSource::Macro1 + (size_t) m] = s.macros[(size_t) m];
        src[(size_t) ModSource::MpePressure] = perf.chPressure[(size_t) channel];
        src[(size_t) ModSource::MpeSlide]    = perf.chTimbre[(size_t) channel];
        src[(size_t) ModSource::MpeGlide]    = perf.chBend[(size_t) channel];
        src[(size_t) ModSource::SeqLane1]    = perf.seqLanes[0];
        src[(size_t) ModSource::SeqLane2]    = perf.seqLanes[1];
        for (int k = 0; k < 4; ++k)
            src[(size_t) ModSource::Game1 + (size_t) k] = perf.gameMods[(size_t) k];

        // ---------------------------------------------------------------- matrix
        std::array<float, (size_t) dest::Count> d {};
        for (const auto& slot : s.matrix)
        {
            if (! slot.isActive() || slot.source >= (int) ModSource::Count || slot.destination >= dest::Count)
                continue;
            float v = applyModCurve (src[(size_t) slot.source], slot.curve) * slot.amount;
            if (slot.via > 0 && slot.via < (int) ModSource::Count)
                v *= src[(size_t) slot.via];
            d[(size_t) slot.destination] += v;
        }
        if (s.modSmoothMs > 0.05f && ! firstMod) // optional smoothing of every destination
        {
            const float a = 1.0f - std::exp (-(float) n / (s.modSmoothMs * 0.001f * sr));
            for (size_t k = 0; k < d.size(); ++k)
                d[k] = smoothedDest[k] += (d[k] - smoothedDest[k]) * a;
        }
        else
            smoothedDest = d;
        firstMod = false;
        for (int k = 0; k < kNumLfos; ++k)
            lastLfoRateMod[(size_t) k] = d[(size_t) (dest::LfoRateBase + k)];
        lastDest = d;

        // ---------------------------------------------------------------- pitch / glide
        updateGlide (s, n, sr);
        if (s.analogDrift > 0.0f) // smoothed random walk, ~0.3 Hz
        {
            driftPhase += 0.3f * (float) n / sr;
            if (driftPhase >= 1.0f) { driftPhase -= 1.0f; driftTarget = rng.bipolar(); }
            driftValue += (driftTarget - driftValue) * std::min (1.0f, 2.0f * (float) n / sr);
        }
        const float tune = s.tuning != nullptr ? s.tuning[std::clamp (note, 0, 127)] : 0.0f;
        float pitch = pitchNow + tune + stackDetune + driftValue * s.analogDrift * 0.12f + randPitch * s.voiceRandom + perf.pitchBend * s.bendRange + d[(size_t) dest::GlobalPitch] * dest::scale (dest::GlobalPitch);
        if (s.mpe)
            pitch += perf.chBend[(size_t) channel] * s.mpeBendRange;
        const float hz = midiNoteToHz (pitch);

        // ---------------------------------------------------------------- oscillators
        clear (bus1L, n); clear (bus1R, n); clear (bus2L, n); clear (bus2R, n); clear (dirL, n); clear (dirR, n);
        bus2Used = false;

        for (int idx = 0; idx < kNumOscillators; ++idx)
        {
            const int i = plan.order[(size_t) idx];
            const auto ui = (size_t) i;
            OscSettings e = s.osc[ui];
            const bool audible = e.on;
            const bool needRaw = (plan.rawNeeded >> i) & 1u;
            if (! audible && needRaw)
            {
                e.on = true;      // render as a silent operator (FM/ring/filter-FM source)
                e.level = 0.0f;
            }

            auto D = [&] (dest::OscDest od) { return d[(size_t) dest::osc (i, od)] * dest::scale (dest::osc (i, od)); };
            e.pitchSemis += D (dest::Pitch);
            if (audible) e.level = std::clamp (e.level + D (dest::Level), 0.0f, 1.0f);
            e.pan         = std::clamp (e.pan + D (dest::Pan), -1.0f, 1.0f);
            e.pulseWidth  = std::clamp (e.pulseWidth + D (dest::PulseWidth), 0.02f, 0.98f);
            e.detuneCents = std::clamp (e.detuneCents + D (dest::Detune), 0.0f, 100.0f);
            e.blend       = std::clamp (e.blend + D (dest::Blend), 0.0f, 1.0f);
            e.spread      = std::clamp (e.spread + D (dest::Spread), 0.0f, 1.0f);
            e.warpAmount  = std::clamp (e.warpAmount + D (dest::WarpAmount), 0.0f, 1.0f);
            const float basePos = e.scanOn ? osc[ui].advanceScan (e, perf.bpm, n, sr) : e.wtPosition;
            e.wtPosition = std::clamp (basePos + D (dest::WtPos), 0.0f, 1.0f);

            const bool usesMod = e.warp == WarpMode::FM || e.warp == WarpMode::AM || e.warp == WarpMode::Ring;
            const bool selfMod = usesMod && e.modSource == i;
            const float* modIn = usesMod && e.modSource >= 0 && e.modSource < kNumOscillators && e.modSource != i
                                   ? raw[(size_t) e.modSource].data() : nullptr;

            float* rawOut = needRaw ? raw[ui].data() : nullptr; // only produce what another stage reads
            float* outBusL; float* outBusR;
            const bool direct = busFor (e.route, s.routing, outBusL, outBusR);
            if (direct) // render straight into the destination bus (common case, no copy)
                osc[ui].render (e, hz, sr, outBusL, outBusR, rawOut, modIn, selfMod, n);
            else
            {
                clear (tmpL, n); clear (tmpR, n);
                osc[ui].render (e, hz, sr, tmpL.data(), tmpR.data(), rawOut, modIn, selfMod, n);
                routeInto (e.route, s.routing, tmpL.data(), tmpR.data(), n, 1.0f);
            }
        }

        // ---------------------------------------------------------------- sub / noise / ring
        {
            const float tgt = s.sub.on ? std::clamp (s.sub.level + d[(size_t) dest::SubLevel], 0.0f, 1.0f) : 0.0f;
            if (tgt > 0.0f || subLevelPrev > 0.0f)
            {
                const float inc = std::min (0.45f, hz / (float) (1 << std::clamp (s.sub.octavesDown, 1, 2)) / sr);
                float g = subLevelPrev;
                const float dg = (tgt - g) / (float) n;
                for (int i = 0; i < n; ++i)
                {
                    const float p = subPhase;
                    float x;
                    switch (s.sub.shape)
                    {
                        case SubShape::Triangle: x = 1.0f - 4.0f * std::abs (p - 0.5f); break;
                        case SubShape::Square:
                        {
                            x = p < 0.5f ? 1.0f : -1.0f;
                            x += polyBlep (p, inc);
                            float p2 = p + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
                            x -= polyBlep (p2, inc);
                            break;
                        }
                        case SubShape::Saw:  x = 2.0f * p - 1.0f - polyBlep (p, inc); break;
                        case SubShape::Sine:
                        default:             x = SineTable::get().lookup (p); break;
                    }
                    tmpL[(size_t) i] = tmpR[(size_t) i] = x * g * 0.7071f;
                    g += dg;
                    subPhase += inc;
                    if (subPhase >= 1.0f) subPhase -= 1.0f;
                }
                subLevelPrev = tgt;
                routeInto (s.sub.route, s.routing, tmpL.data(), tmpR.data(), n, 1.0f);
            }
        }
        {
            const float tgt = s.noise.on ? std::clamp (s.noise.level + d[(size_t) dest::NoiseLevel], 0.0f, 1.0f) : 0.0f;
            if (tgt > 0.0f || noiseLevelPrev > 0.0f)
            {
                float g = noiseLevelPrev;
                const float dg = (tgt - g) / (float) n;
                const float crackle = 3.0f / sr;
                for (int i = 0; i < n; ++i)
                {
                    tmpL[(size_t) i] = noiseL.next (s.noise.type, 0.05f, crackle) * g * 0.7071f;
                    tmpR[(size_t) i] = noiseR.next (s.noise.type, 0.05f, crackle) * g * 0.7071f;
                    g += dg;
                }
                noiseLevelPrev = tgt;
                routeInto (s.noise.route, s.routing, tmpL.data(), tmpR.data(), n, 1.0f);
            }
        }
        {
            const float tgt = s.ring.on ? std::clamp (s.ring.level + d[(size_t) dest::RingLevel], 0.0f, 1.0f) : 0.0f;
            if (tgt > 0.0f || ringLevelPrev > 0.0f)
            {
                const auto& a = raw[(size_t) std::clamp (s.ring.oscA, 0, kNumOscillators - 1)];
                const auto& b = raw[(size_t) std::clamp (s.ring.oscB, 0, kNumOscillators - 1)];
                float g = ringLevelPrev;
                const float dg = (tgt - g) / (float) n;
                for (int i = 0; i < n; ++i)
                {
                    tmpL[(size_t) i] = tmpR[(size_t) i] = a[(size_t) i] * b[(size_t) i] * g * 0.7071f;
                    g += dg;
                }
                ringLevelPrev = tgt;
                routeInto (s.ring.route, s.routing, tmpL.data(), tmpR.data(), n, 1.0f);
            }
        }

        // ---------------------------------------------------------------- filters
        float hzEnd[kNumFilters];
        FilterSettings fs[kNumFilters];
        for (int f = 0; f < kNumFilters; ++f)
        {
            const auto& fp = s.filter[(size_t) f];
            auto D = [&] (dest::FilterDest fd) { return d[(size_t) dest::filter (f, fd)] * dest::scale (dest::filter (f, fd)); };
            const float semis = 69.0f + 12.0f * std::log2 (std::max (fp.cutoffHz, 1.0f) / 440.0f)
                              + fp.envAmount * 96.0f * src[(size_t) ModSource::Env2]
                              + fp.keytrack * ((float) note - 60.0f)
                              + fp.velocity * vel * 48.0f
                              + randCutoff * s.voiceRandom
                              + D (dest::Cutoff);
            hzEnd[f] = midiNoteToHz (std::clamp (semis, 0.0f, 140.0f));
            fs[f] = fp.dsp;
            fs[f].resonance = std::clamp (fs[f].resonance + D (dest::Resonance), 0.0f, 1.0f);
            fs[f].drive     = std::clamp (fs[f].drive + D (dest::Drive), 0.0f, 1.0f);
            fs[f].mix       = std::clamp (fs[f].mix + D (dest::Mix), 0.0f, 1.0f);
            fs[f].fmAmount  = std::clamp (fs[f].fmAmount + D (dest::FilterFM), 0.0f, 1.0f);
            if (firstChunk)
                cutoffPrev[(size_t) f] = hzEnd[f];
        }
        const float* fmSig = raw[0].data(); // filter FM source = OSC 1

        switch (s.routing)
        {
            case FilterRouting::Parallel:
                filters[0].process (fs[0], cutoffPrev[0], hzEnd[0], fmSig, bus1L.data(), bus1R.data(), n, sr);
                filters[1].process (fs[1], cutoffPrev[1], hzEnd[1], fmSig, bus2L.data(), bus2R.data(), n, sr);
                for (int i = 0; i < n; ++i) { bus1L[(size_t) i] += bus2L[(size_t) i]; bus1R[(size_t) i] += bus2R[(size_t) i]; }
                break;
            case FilterRouting::Split: // F1 = left, F2 = right
                for (int i = 0; i < n; ++i) { bus1L[(size_t) i] += bus2L[(size_t) i]; bus1R[(size_t) i] += bus2R[(size_t) i]; }
                filters[0].process (fs[0], cutoffPrev[0], hzEnd[0], fmSig, bus1L.data(), nullptr, n, sr);
                filters[1].process (fs[1], cutoffPrev[1], hzEnd[1], fmSig, bus1R.data(), nullptr, n, sr);
                break;
            case FilterRouting::Serial:
            default:
                filters[0].process (fs[0], cutoffPrev[0], hzEnd[0], fmSig, bus1L.data(), bus1R.data(), n, sr);
                if (bus2Used)
                    for (int i = 0; i < n; ++i) { bus1L[(size_t) i] += bus2L[(size_t) i]; bus1R[(size_t) i] += bus2R[(size_t) i]; }
                filters[1].process (fs[1], cutoffPrev[1], hzEnd[1], fmSig, bus1L.data(), bus1R.data(), n, sr);
                break;
        }
        for (int f = 0; f < kNumFilters; ++f)
            cutoffPrev[(size_t) f] = hzEnd[f];

        // ---------------------------------------------------------------- amp (VCA)
        const float vs       = s.envVelocity[0];
        const float velGain  = 1.0f - vs + vs * vel;
        const float ampTgt   = std::clamp (1.0f + d[(size_t) dest::AmpLevel], 0.0f, 2.0f);
        const float panPos = std::clamp (stackPan + randPan * s.voiceRandom + d[(size_t) dest::AmpPan], -1.0f, 1.0f);
        if (! exactlyEqual (panPos, cachedPanPos))
        {
            equalPowerPan (panPos, cachedPanL, cachedPanR);
            cachedPanL *= 1.41421356f; // balance law: centre = unity
            cachedPanR *= 1.41421356f;
            cachedPanPos = panPos;
        }
        const float panL = cachedPanL, panR = cachedPanR;
        if (firstChunk) { ampPrev = ampTgt; panPrevL = panL; panPrevR = panR; }
        const float invN = 1.0f / (float) n;
        float ag = ampPrev, pl = panPrevL, pr = panPrevR;
        const float dag = (ampTgt - ampPrev) * invN, dpl = (panL - panPrevL) * invN, dpr = (panR - panPrevR) * invN;
        const float fadeStep = 1.0f / (0.003f * sr);

        for (int i = 0; i < n; ++i)
        {
            float g = env[0].next() * velGain * ag;
            if (stealing)
            {
                g *= fadeGain;
                fadeGain = std::max (0.0f, fadeGain - fadeStep);
            }
            outL[i] += (bus1L[(size_t) i] + dirL[(size_t) i]) * g * pl;
            outR[i] += (bus1R[(size_t) i] + dirR[(size_t) i]) * g * pr;
            ag += dag; pl += dpl; pr += dpr;
        }
        ampPrev = ampTgt; panPrevL = panL; panPrevR = panR;
        firstChunk = false;

        if (stealing && fadeGain <= 0.0f)
        {
            const bool keyUp = pendingKeyUp;
            start (pendingNote, pendingVel, pendingChannel, s, pendingOrder, pendingDetune, pendingPan, pendingGlide);
            if (keyUp)
                release (false);
        }
        else if (! env[0].isActive())
        {
            active = false;
        }
    }

    bool isActive() const noexcept      { return active; }
    bool isStealing() const noexcept    { return stealing; }
    bool isKeyDown() const noexcept     { return keyDown; }
    bool isReleasing() const noexcept   { return active && ! stealing && env[0].isReleasing(); }
    int  getKeyNote() const noexcept    { return stealing ? pendingNote : note; }
    int  getChannel() const noexcept    { return stealing ? pendingChannel : channel; }
    uint64_t getOrder() const noexcept  { return stealing ? pendingOrder : startOrder; }
    float getEnvelopeLevel() const noexcept { return env[0].getValue(); }
    float getPitch() const noexcept     { return pitchNow; }
    /** Last computed per-destination modulation (for UI readouts). */
    const std::array<float, (size_t) dest::Count>& getLastModulation() const noexcept { return lastDest; }
    float getLfoValue (int k) const noexcept { return lfo[(size_t) k].getValue(); }

private:
    using Buf = std::array<float, kMaxChunk>;
    static void clear (Buf& b, int) noexcept { b.fill (0.0f); } // fixed size: inlined vector stores, no memset call

    void setPitchTarget (float target, bool glide) noexcept
    {
        pitchTarget = target;
        glideStart  = pitchNow;
        if (! glide)
            pitchNow = target;
    }

    void updateGlide (const EngineSettings& s, int n, float sr) noexcept
    {
        if (s.glideTime <= 0.0005f || exactlyEqual (pitchNow, pitchTarget))
        {
            pitchNow = pitchTarget;
            return;
        }
        const float samples = s.glideTime * sr;
        const float expNext = pitchNow + (pitchTarget - pitchNow) * (1.0f - std::exp (-(float) n * 4.0f / samples));
        const float rate    = std::abs (pitchTarget - glideStart) / samples * (float) n;
        const float linNext = pitchNow < pitchTarget ? std::min (pitchTarget, pitchNow + rate) : std::max (pitchTarget, pitchNow - rate);
        pitchNow = linNext + (expNext - linNext) * std::clamp (s.glideCurve, 0.0f, 1.0f);
        if (std::abs (pitchNow - pitchTarget) < 0.001f)
            pitchNow = pitchTarget;
    }

    /** Single destination bus for a route (false when the route feeds two buses). */
    bool busFor (OscRoute route, FilterRouting routing, float*& l, float*& r) noexcept
    {
        switch (route)
        {
            case OscRoute::Filter1: l = bus1L.data(); r = bus1R.data(); return true;
            case OscRoute::Filter2: l = bus2L.data(); r = bus2R.data(); bus2Used = true; return true;
            case OscRoute::Both:
                if (routing != FilterRouting::Serial) return false;
                l = bus1L.data(); r = bus1R.data(); return true;
            case OscRoute::Bypass:
            default: l = dirL.data(); r = dirR.data(); return true;
        }
    }

    /** Adds a stereo source to the bus chosen by its route. */
    void routeInto (OscRoute route, FilterRouting routing, const float* l, const float* r, int n, float gain) noexcept
    {
        auto add = [n] (Buf& dl, Buf& dr, const float* sl, const float* sr2, float g) {
            for (int i = 0; i < n; ++i) { dl[(size_t) i] += sl[i] * g; dr[(size_t) i] += sr2[i] * g; }
        };
        switch (route)
        {
            case OscRoute::Filter1: add (bus1L, bus1R, l, r, gain); break;
            case OscRoute::Filter2: add (bus2L, bus2R, l, r, gain); bus2Used = true; break;
            case OscRoute::Both:
                if (routing == FilterRouting::Serial)
                    add (bus1L, bus1R, l, r, gain); // enters at F1 and passes through F2
                else
                {
                    add (bus1L, bus1R, l, r, gain * 0.7071f);
                    add (bus2L, bus2R, l, r, gain * 0.7071f);
                    bus2Used = true;
                }
                break;
            case OscRoute::Bypass:
            default: add (dirL, dirR, l, r, gain); break;
        }
    }

    std::array<Oscillator, kNumOscillators> osc;
    std::array<Envelope, kNumEnvelopes> env;
    std::array<Lfo, kNumLfos> lfo;
    std::array<FilterUnit, kNumFilters> filters;
    NoiseGen noiseL, noiseR;
    Rng rng;

    Buf bus1L {}, bus1R {}, bus2L {}, bus2R {}, dirL {}, dirR {}, tmpL {}, tmpR {};
    std::array<Buf, kNumOscillators> raw {};
    std::array<float, (size_t) dest::Count> lastDest {};
    std::array<float, kNumLfos> lastLfoRateMod {};
    std::array<float, kNumFilters> cutoffPrev { 1000.0f, 1000.0f };

    int note = 60, channel = 0;
    float vel = 1.0f, randomValue = 0.0f, polyPressure = 0.0f;
    float pitchNow = 60.0f, pitchTarget = 60.0f, glideStart = 60.0f, stackDetune = 0.0f, stackPan = 0.0f;
    float msegTime = 0.0f, subPhase = 0.0f, subLevelPrev = 0.0f, noiseLevelPrev = 0.0f, ringLevelPrev = 0.0f;
    float ampPrev = 1.0f, panPrevL = 1.0f, panPrevR = 1.0f;
    float randPitch = 0.0f, randCutoff = 0.0f, randPan = 0.0f, driftPhase = 0.0f, driftValue = 0.0f, driftTarget = 0.0f;
    std::array<float, (size_t) dest::Count> smoothedDest {};
    bool firstMod = true;
    float cachedPanPos = -9.0f, cachedPanL = 1.0f, cachedPanR = 1.0f;
    uint64_t startOrder = 0, noteSerial = 0;
    uint32_t voiceSeed = 1;
    bool active = false, keyDown = false, sustained = false, firstChunk = true, bus2Used = false;

    bool stealing = false, pendingKeyUp = false;
    int pendingNote = 60, pendingChannel = 0;
    float pendingVel = 1.0f, fadeGain = 1.0f, pendingGlide = -1.0f, pendingDetune = 0.0f, pendingPan = 0.0f;
    uint64_t pendingOrder = 0;
};

class SynthEngine
{
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kChunk     = 32;
    static_assert (kChunk <= Voice::kMaxChunk);

    /** maxSampleRate = highest (oversampled) rate the engine will run at — sizes comb filters. */
    void prepare (const WavetableBank* bank, float newSampleRate, float maxSampleRate = 0.0f)
    {
        const float maxSr = std::max (newSampleRate, maxSampleRate);
        for (int i = 0; i < kMaxVoices; ++i)
            voices[(size_t) i].prepare (bank, (uint32_t) i + 1u, maxSr);
        setSampleRate (newSampleRate);
        allSoundOff();
        perf = PerformanceState {}; // deterministic restart: LFO phases, controllers
        bendTarget = 0.0f;
        hostPpq = -1.0;
        hasCurrent = false;
    }

    void setSampleRate (float newSampleRate) noexcept { sampleRate = std::max (1000.0f, newSampleRate); }
    float getSampleRate() const noexcept { return sampleRate; }

    void setTarget (const EngineSettings& t) noexcept
    {
        target = t;
        if (! hasCurrent)
        {
            current    = t;
            prevMaster = t.masterGain;
            hasCurrent = true;
        }
        buildPlan();
    }

    /** Host transport. bpm <= 0 keeps the previous tempo. ppq < 0 = not playing. */
    void setTransport (float bpm, double ppqPosition) noexcept
    {
        if (bpm > 0.0f)
            perf.bpm = bpm;
        hostPpq = ppqPosition;
    }

    //==========================================================================
    void noteOn (int note, float velocity, int channel = 0) noexcept
    {
        const uint64_t order = ++noteCounter;
        const auto mode = target.voiceMode;

        if (mode == VoiceMode::Mono || mode == VoiceMode::Legato)
        {
            const bool overlapping = heldCount > 0;
            pushHeld (note, velocity, channel);
            Voice& v = voices[0];
            const bool glide = target.glideTime > 0.0f && (target.glideMode == GlideMode::Always || overlapping);
            if (! v.isActive())
                v.start (note, velocity, channel, current, order, 0.0f, 0.0f, glide && lastNote >= 0 ? (float) lastNote : -1.0f);
            else if (mode == VoiceMode::Legato && overlapping)
                v.legatoTo (note, glide);
            else
                v.retrigger (note, velocity, current, order, glide);
            lastNote = note;
            return;
        }

        const int stack = mode == VoiceMode::Unison ? std::clamp (target.stackVoices, 1, 8) : 1;
        const bool glide = target.glideTime > 0.0f && lastNote >= 0 && (target.glideMode == GlideMode::Always || heldCount > 0);
        pushHeld (note, velocity, channel);
        for (int k = 0; k < stack; ++k)
        {
            const float t = stack == 1 ? 0.0f : 2.0f * (float) k / (float) (stack - 1) - 1.0f;
            allocate (note, velocity, channel, order, t * target.stackDetune * 0.005f, t * 0.8f,
                      glide ? (float) lastNote : -1.0f, stack > 1);
        }
        lastNote = note;
    }

    void noteOff (int note, int channel = 0) noexcept
    {
        removeHeld (note, channel);
        const auto mode = target.voiceMode;
        if (mode == VoiceMode::Mono || mode == VoiceMode::Legato)
        {
            Voice& v = voices[0];
            if (! v.isActive() || v.getKeyNote() != note)
                return;
            if (heldCount > 0)
            {
                const auto& h = held[(size_t) heldCount - 1]; // last-note priority: fall back to the previous key
                v.legatoTo (h.note, target.glideTime > 0.0f);
                lastNote = h.note;
            }
            else
                v.release (sustainDown);
            return;
        }

        for (auto& v : voices)
            if (v.isActive() && v.isKeyDown() && v.getKeyNote() == note && (! target.mpe || v.getChannel() == channel))
                v.release (sustainDown);
    }

    void setSustainPedal (bool down) noexcept
    {
        sustainDown = down;
        if (! down)
            for (auto& v : voices)
                v.releaseSustained();
    }

    /** normalised -1..1. With MPE enabled, channels 2-16 are per-note; channel 1 is global. */
    void setPitchBend (float norm, int channel = 0) noexcept
    {
        norm = std::clamp (norm, -1.0f, 1.0f);
        if (target.mpe && channel > 0)
            perf.chBend[(size_t) (channel & 15)] = norm;
        else
            bendTarget = norm;
    }
    void setModWheel (float v) noexcept { perf.modWheel = std::clamp (v, 0.0f, 1.0f); }
    void setChannelPressure (float v, int channel = 0) noexcept
    {
        if (target.mpe && channel > 0) perf.chPressure[(size_t) (channel & 15)] = v;
        else                           perf.aftertouch = v;
    }
    void setPolyPressure (int note, float v) noexcept
    {
        for (auto& voice : voices)
            if (voice.isActive() && voice.getKeyNote() == note)
                voice.setPolyPressure (v);
    }
    void setTimbre (float v, int channel = 0) noexcept { perf.chTimbre[(size_t) (channel & 15)] = v; }
    void setSeqLanes (float a, float b) noexcept { perf.seqLanes = { a, b }; }
    void setGameMods (const std::array<float, 4>& m) noexcept { perf.gameMods = m; }

    void allNotesOff() noexcept
    {
        sustainDown = false;
        heldCount = 0;
        for (auto& v : voices)
            if (v.isActive())
            {
                v.release (false);
                v.releaseSustained();
            }
    }

    void allSoundOff() noexcept
    {
        for (auto& v : voices)
            v.kill();
        sustainDown = false;
        heldCount = 0;
        lastNote = -1;
    }

    /** Adds n samples to L/R. Split calls at MIDI event positions for sample accuracy. */
    void render (float* L, float* R, int n) noexcept
    {
        while (n > 0)
        {
            const int c = std::min (n, kChunk);
            smooth (c);
            advanceFreeLfos (c);

            for (auto& v : voices)
                v.render (current, perf, plan, sampleRate, L, R, c);

            float g = prevMaster;
            const float dg = (current.masterGain - prevMaster) / (float) c;
            for (int i = 0; i < c; ++i)
            {
                L[i] *= g;
                R[i] *= g;
                g += dg;
            }
            prevMaster = current.masterGain;

            L += c;
            R += c;
            n -= c;
        }
    }

    int getActiveVoiceCount() const noexcept
    {
        int count = 0;
        for (const auto& v : voices)
            count += v.isActive() ? 1 : 0;
        return count;
    }

    /** Most recently started active voice (for UI modulation readouts), or nullptr. */
    const Voice* getNewestVoice() const noexcept
    {
        const Voice* best = nullptr;
        for (const auto& v : voices)
            if (v.isActive() && (best == nullptr || v.getOrder() > best->getOrder()))
                best = &v;
        return best;
    }
    const PerformanceState& getPerformance() const noexcept { return perf; }

private:
    struct HeldKey { int note; float velocity; int channel; };

    void pushHeld (int note, float velocity, int channel) noexcept
    {
        removeHeld (note, channel);
        if (heldCount < (int) held.size())
            held[(size_t) heldCount++] = { note, velocity, channel };
    }
    void removeHeld (int note, int channel) noexcept
    {
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].note == note && (! target.mpe || held[(size_t) i].channel == channel))
            {
                for (int j = i; j < heldCount - 1; ++j)
                    held[(size_t) j] = held[(size_t) j + 1];
                --heldCount;
                return;
            }
    }

    void allocate (int note, float velocity, int channel, uint64_t order, float detune, float pan, float glideFrom, bool stacked) noexcept
    {
        const int limit = std::clamp (target.polyphony, 1, kMaxVoices);

        if (! stacked) // same key re-struck: retrigger that voice (declicked)
            for (auto& v : voices)
                if (v.isActive() && v.getKeyNote() == note && (! target.mpe || v.getChannel() == channel))
                {
                    v.steal (note, velocity, channel, order, glideFrom, detune, pan);
                    return;
                }

        int activeCount = 0;
        Voice* freeVoice = nullptr;
        for (auto& v : voices)
        {
            if (v.isActive()) ++activeCount;
            else if (freeVoice == nullptr) freeVoice = &v;
        }

        if (activeCount < limit && freeVoice != nullptr)
        {
            freeVoice->start (note, velocity, channel, current, order, detune, pan, glideFrom);
            return;
        }

        Voice* victim = nullptr;
        if (target.stealMode == 2 || target.stealMode == 3) // lowest / highest note
        {
            for (auto& v : voices)
                if (v.isActive() && v.getOrder() < order
                    && (victim == nullptr || (target.stealMode == 2 ? v.getKeyNote() < victim->getKeyNote() : v.getKeyNote() > victim->getKeyNote())))
                    victim = &v;
        }
        else if (target.stealMode == 0)
            for (auto& v : voices)
                if (v.isActive() && v.isReleasing()
                    && (victim == nullptr || v.getEnvelopeLevel() < victim->getEnvelopeLevel()))
                    victim = &v;
        if (victim == nullptr)
            for (auto& v : voices)
                if (v.isActive() && v.getOrder() < order && (victim == nullptr || v.getOrder() < victim->getOrder()))
                    victim = &v;
        if (victim != nullptr)
            victim->steal (note, velocity, channel, order, glideFrom, detune, pan);
    }

    void buildPlan() noexcept
    {
        uint32_t needed = 0;
        std::array<int, kNumOscillators> dep;
        for (int i = 0; i < kNumOscillators; ++i)
        {
            const auto& o = target.osc[(size_t) i];
            const bool usesMod = (o.on && (o.warp == WarpMode::FM || o.warp == WarpMode::AM || o.warp == WarpMode::Ring)
                                  && o.modSource >= 0 && o.modSource < kNumOscillators && o.modSource != i);
            dep[(size_t) i] = usesMod ? o.modSource : -1;
            if (usesMod)
                needed |= 1u << o.modSource;
        }
        if (target.ring.on)
            needed |= (1u << std::clamp (target.ring.oscA, 0, 5)) | (1u << std::clamp (target.ring.oscB, 0, 5));
        bool filterFm = target.filter[0].dsp.fmAmount > 0.0f || target.filter[1].dsp.fmAmount > 0.0f;
        for (const auto& slot : target.matrix)
            if (slot.isActive() && (slot.destination == dest::filter (0, dest::FilterFM) || slot.destination == dest::filter (1, dest::FilterFM)))
                filterFm = true;
        if (filterFm)
            needed |= 1u;

        uint64_t used = 0;
        for (const auto& slot : target.matrix)
            if (slot.isActive())
            {
                used |= 1ull << std::clamp (slot.source, 0, 63);
                used |= 1ull << std::clamp (slot.via, 0, 63);
            }
        if (! exactlyEqual (target.filter[0].envAmount, 0.0f) || ! exactlyEqual (target.filter[1].envAmount, 0.0f))
            used |= 1ull << (int) ModSource::Env2;
        plan.usedSources = used;

        // dependency order: modulators before carriers (cycles fall back to index order)
        std::array<bool, kNumOscillators> placed {};
        int count = 0;
        for (int pass = 0; pass < kNumOscillators && count < kNumOscillators; ++pass)
            for (int i = 0; i < kNumOscillators; ++i)
                if (! placed[(size_t) i] && (dep[(size_t) i] < 0 || placed[(size_t) dep[(size_t) i]] || pass == kNumOscillators - 1))
                {
                    placed[(size_t) i] = true;
                    plan.order[(size_t) count++] = i;
                }
        plan.rawNeeded = needed;
    }

    void advanceFreeLfos (int n) noexcept
    {
        const double dt = (double) n / sampleRate;
        if (hostPpq >= 0.0)
            hostPpq += dt * perf.bpm / 60.0;
        for (int k = 0; k < kNumLfos; ++k)
        {
            const auto& ls = current.lfo[(size_t) k];
            if (ls.sync && hostPpq >= 0.0)
                perf.lfoFreeCycles[(size_t) k] = hostPpq / kSyncDivisionBeats[(size_t) std::clamp (ls.division, 0, 19)];
            else
            {
                const float rate = ls.sync ? syncDivisionHz (ls.division, perf.bpm) : ls.rateHz;
                perf.lfoFreeCycles[(size_t) k] += rate * dt;
            }
        }
    }

    void smooth (int numSamples) noexcept
    {
        const float a  = 1.0f - std::exp (-(float) numSamples / (0.015f * sampleRate)); // ~15 ms
        const float ab = 1.0f - std::exp (-(float) numSamples / (0.004f * sampleRate));
        auto glide = [a] (float& cur, float tgt) { cur += (tgt - cur) * a; };

        // copy everything discrete, then smooth the continuous fields
        auto prev = current;
        current = target;
        for (size_t i = 0; i < (size_t) kNumOscillators; ++i)
        {
            auto& c = current.osc[i];
            const auto& p = prev.osc[i];
            const auto& t = target.osc[i];
            c.fineCents = p.fineCents; glide (c.fineCents, t.fineCents);
            c.level = p.level;         glide (c.level, t.level);
            c.pan = p.pan;             glide (c.pan, t.pan);
            c.detuneCents = p.detuneCents; glide (c.detuneCents, t.detuneCents);
            c.blend = p.blend;         glide (c.blend, t.blend);
            c.spread = p.spread;       glide (c.spread, t.spread);
            c.wtPosition = p.wtPosition; glide (c.wtPosition, t.wtPosition);
            c.pulseWidth = p.pulseWidth; glide (c.pulseWidth, t.pulseWidth);
            c.warpAmount = p.warpAmount; glide (c.warpAmount, t.warpAmount);
        }
        for (size_t f = 0; f < (size_t) kNumFilters; ++f)
        {
            auto& c = current.filter[f];
            const auto& p = prev.filter[f];
            const auto& t = target.filter[f];
            c.cutoffHz = p.cutoffHz * std::pow (t.cutoffHz / std::max (p.cutoffHz, 1.0f), a); // log-domain smoothing
            c.dsp.resonance = p.dsp.resonance; glide (c.dsp.resonance, t.dsp.resonance);
            c.dsp.drive = p.dsp.drive;         glide (c.dsp.drive, t.dsp.drive);
            c.dsp.mix = p.dsp.mix;             glide (c.dsp.mix, t.dsp.mix);
            c.envAmount = p.envAmount;         glide (c.envAmount, t.envAmount);
        }
        for (size_t m = 0; m < (size_t) kNumMacros; ++m)
        {
            current.macros[m] = prev.macros[m];
            glide (current.macros[m], target.macros[m]);
        }
        for (size_t k = 0; k < (size_t) kNumMatrixSlots; ++k)
        {
            current.matrix[k].amount = prev.matrix[k].amount;
            glide (current.matrix[k].amount, target.matrix[k].amount);
        }
        current.sub.level   = prev.sub.level;   glide (current.sub.level, target.sub.level);
        current.noise.level = prev.noise.level; glide (current.noise.level, target.noise.level);
        current.ring.level  = prev.ring.level;  glide (current.ring.level, target.ring.level);
        current.masterGain  = prev.masterGain;  glide (current.masterGain, target.masterGain);

        perf.pitchBend += (bendTarget - perf.pitchBend) * ab;
    }

    std::array<Voice, kMaxVoices> voices;
    EngineSettings target, current;
    PerformanceState perf;
    RenderPlan plan;
    std::array<HeldKey, 32> held {};
    int heldCount = 0, lastNote = -1;
    bool hasCurrent = false, sustainDown = false;
    float sampleRate = 48000.0f, prevMaster = 0.5f, bendTarget = 0.0f;
    double hostPpq = -1.0;
    uint64_t noteCounter = 0;
};

} // namespace mbx::dsp
