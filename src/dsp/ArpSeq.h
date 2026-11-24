// Arpeggiator + 16-64 step sequencer. Consumes the block's incoming note events and
// produces the notes the synth (and optionally the host) should hear. Locked to the
// host transport when it plays, free-running at the host tempo otherwise.
// Real-time safe: fixed-capacity arrays only.
#pragma once

#include "Modulation.h"

namespace mbx::dsp
{
struct NoteEvent
{
    int sample = 0;
    bool on = true;
    int note = 60;
    float velocity = 0.8f;
    int channel = 0;
};

struct SeqStep
{
    bool on = true;
    float pitch = 0.0f;   // semitones -24..24
    float velocity = 0.8f;
    float gate = 0.5f;    // fraction of a step 0.05..1
    float probability = 1.0f;
    int ratchet = 1;      // 1..4 repeats inside the step
    bool slide = false;   // overlap into the next step (legato / glide)
    float mod1 = 0.0f, mod2 = 0.0f; // per-step modulation lanes 0..1
};

struct SeqPattern
{
    static constexpr int kMaxSteps = 64;
    std::array<SeqStep, kMaxSteps> steps {};

    static SeqPattern makeDefault()
    {
        SeqPattern p;
        static constexpr float pitches[16] = { 0, 0, 12, 0, 7, 0, 10, 12, 0, 0, 12, 3, 7, 0, 5, 7 };
        for (int i = 0; i < kMaxSteps; ++i)
        {
            auto& s = p.steps[(size_t) i];
            s.pitch = pitches[i % 16];
            s.velocity = (i % 4 == 0) ? 1.0f : 0.7f;
            s.gate = 0.5f;
            s.mod1 = (float) (i % 16) / 15.0f;
            s.mod2 = (i % 2 == 0) ? 1.0f : 0.0f;
        }
        return p;
    }
};

enum class ArpMode : int { Off = 0, Arp, Seq };
enum class ArpPattern : int { Up = 0, Down, UpDown, Random, AsPlayed, Chord };

struct ArpSettings
{
    ArpMode mode = ArpMode::Off;
    ArpPattern pattern = ArpPattern::Up;
    int rateDivision = 5;   // 1/16
    float gate = 0.6f;
    float swing = 0.0f;
    int octaves = 1;
    bool hold = false;
    bool retrigger = true;
    int seqLength = 16;
    int seqRateDivision = 5;
    float seqSwing = 0.0f;
    bool seqFollowKey = true;
};

class ArpSeq
{
public:
    static constexpr int kMaxHeld = 32, kMaxActive = 64;

    void prepare (float sampleRate) noexcept { sr = sampleRate; reset(); }

    void reset() noexcept
    {
        heldCount = 0;
        activeCount = 0;
        stepIndex = 0;
        nextStepBeat = -1.0;
        freeBeat = 0.0;
        latched = false;
        arpPos = 0;
        arpDir = 1;
    }

    float laneValue (int lane) const noexcept { return lane == 0 ? lane1 : lane2; }
    int currentStep() const noexcept { return lastStepPlayed; }

    /** in: this block's note events (sorted by sample). out: generated notes. Returns count written. */
    int process (const ArpSettings& s, const SeqPattern& pattern, const NoteEvent* in, int nIn,
                 NoteEvent* out, int maxOut, int numSamples, double hostPpq, float bpm) noexcept
    {
        int nOut = 0;
        auto emit = [&] (const NoteEvent& e) { if (nOut < maxOut) out[nOut++] = e; };

        bpm = std::max (bpm, 20.0f);
        const double beatsPerSample = (double) bpm / 60.0 / (double) sr;
        const bool synced = hostPpq >= 0.0;
        const double blockStartBeat = synced ? hostPpq : freeBeat;

        const bool isSeq = s.mode == ArpMode::Seq;
        const double stepBeats = kSyncDivisionBeats[(size_t) std::clamp (isSeq ? s.seqRateDivision : s.rateDivision, 0, 19)];
        const float swing = isSeq ? s.seqSwing : s.swing;

        int evIdx = 0;
        int cursor = 0;
        while (cursor <= numSamples)
        {
            // next scheduled thing: incoming event, pending note-off, or step boundary
            const int nextEvent = evIdx < nIn ? std::clamp (in[evIdx].sample, 0, numSamples) : numSamples + 1;

            if (nextStepBeat < 0.0)
                nextStepBeat = std::ceil ((blockStartBeat + cursor * beatsPerSample) / stepBeats - 1.0e-9) * stepBeats;
            const double swingOffset = (stepIndex % 2 == 1) ? swing * 0.33 * stepBeats : 0.0;
            const double stepSampleD = (nextStepBeat + swingOffset - blockStartBeat) / beatsPerSample;
            const int nextStep = stepSampleD < 0.0 ? cursor : (int) std::min<double> (stepSampleD, numSamples + 1.0);

            int nextOff = numSamples + 1;
            for (int a = 0; a < activeCount; ++a)
                nextOff = std::min (nextOff, active[(size_t) a].started ? active[(size_t) a].offSample : active[(size_t) a].onSample);

            const int t = std::min ({ nextEvent, nextStep, nextOff });
            if (t > numSamples)
                break;
            cursor = t;

            if (t == nextOff)
            {
                for (int a = 0; a < activeCount;)
                {
                    auto& act = active[(size_t) a];
                    if (! act.started && act.onSample <= t)
                    {
                        emit ({ t, true, act.note, act.velocity, act.channel });
                        act.started = true;
                    }
                    if (act.started && act.offSample <= t)
                    {
                        emit ({ t, false, act.note, 0.0f, act.channel });
                        act = active[(size_t) --activeCount];
                    }
                    else ++a;
                }
                continue;
            }
            if (t == nextEvent)
            {
                handleInput (s, in[evIdx], out, nOut, maxOut, t);
                ++evIdx;
                continue;
            }
            // step boundary
            currentSample = t;
            if (heldCount > 0)
                fireStep (s, pattern, t, stepBeats / beatsPerSample, emit);
            ++stepIndex;
            nextStepBeat += stepBeats;
        }

        // shift pending offs into the next block
        for (int a = 0; a < activeCount; ++a)
        {
            active[(size_t) a].offSample -= numSamples;
            active[(size_t) a].onSample -= numSamples;
        }
        if (! synced)
            freeBeat += numSamples * beatsPerSample;
        if (synced && std::abs ((hostPpq + numSamples * beatsPerSample) - nextStepBeat) > stepBeats * 2.0)
            nextStepBeat = -1.0; // host jumped (loop / relocate): re-align on the next block
        lastSynced = synced;
        return nOut;
    }

    /** Releases everything (e.g. when the mode is switched off). */
    int flush (NoteEvent* out, int maxOut) noexcept
    {
        int n = 0;
        for (int a = 0; a < activeCount && n < maxOut; ++a)
            if (active[(size_t) a].started)
                out[n++] = { 0, false, active[(size_t) a].note, 0.0f, active[(size_t) a].channel };
        activeCount = 0;
        heldCount = 0;
        latched = false;
        return n;
    }

private:
    struct Held { int note; float velocity; int channel; };
    struct Active { int note; int channel; int onSample; int offSample; float velocity; bool started; };

    void handleInput (const ArpSettings& s, const NoteEvent& e, NoteEvent*, int&, int, int) noexcept
    {
        if (e.on)
        {
            if (s.hold && latched && physicalDown == 0)
                heldCount = 0; // new chord replaces the latched one
            if (heldCount == 0 && s.retrigger)
            {
                stepIndex = 0;
                arpPos = 0;
                arpDir = 1;
            }
            ++physicalDown;
            for (int i = 0; i < heldCount; ++i)
                if (held[(size_t) i].note == e.note) return;
            if (heldCount < kMaxHeld)
                held[(size_t) heldCount++] = { e.note, e.velocity, e.channel };
            latched = s.hold;
        }
        else
        {
            physicalDown = std::max (0, physicalDown - 1);
            if (s.hold)
                return; // latched notes keep playing
            for (int i = 0; i < heldCount; ++i)
                if (held[(size_t) i].note == e.note)
                {
                    for (int j = i; j < heldCount - 1; ++j) held[(size_t) j] = held[(size_t) j + 1];
                    --heldCount;
                    break;
                }
        }
    }

    template <typename Emit>
    void fireStep (const ArpSettings& s, const SeqPattern& pattern, int at, double samplesPerStep, Emit&& emit) noexcept
    {
        if (s.mode == ArpMode::Seq)
        {
            const int len = std::clamp (s.seqLength, 1, SeqPattern::kMaxSteps);
            const int idx = stepIndex % len;
            const auto& st = pattern.steps[(size_t) idx];
            lastStepPlayed = idx;
            lane1 = st.mod1;
            lane2 = st.mod2;
            if (! st.on || rng.uniform() >= st.probability)
                return;
            const int base = s.seqFollowKey ? held[(size_t) heldCount - 1].note : 60;
            const int note = std::clamp (base + (int) std::round (st.pitch), 0, 127);
            const int r = std::clamp (st.ratchet, 1, 4);
            const double sub = samplesPerStep / r;
            for (int k = 0; k < r; ++k)
            {
                const int on = at + (int) (k * sub);
                const double len2 = st.slide && k == r - 1 ? sub * 1.05 : sub * std::clamp (st.gate, 0.05f, 1.0f);
                startNote (note, st.velocity, held[(size_t) heldCount - 1].channel, on, on + std::max (1, (int) len2), emit);
            }
            return;
        }

        // arpeggiator
        std::array<int, kMaxHeld * 4> notes {};
        std::array<float, kMaxHeld * 4> vels {};
        int count = 0;
        std::array<Held, kMaxHeld> sorted {};
        for (int i = 0; i < heldCount; ++i) sorted[(size_t) i] = held[(size_t) i];
        if (s.pattern != ArpPattern::AsPlayed)
            std::sort (sorted.begin(), sorted.begin() + heldCount, [] (const Held& a, const Held& b) { return a.note < b.note; });
        const int octs = std::clamp (s.octaves, 1, 4);
        for (int o = 0; o < octs; ++o)
            for (int i = 0; i < heldCount; ++i)
            {
                notes[(size_t) count] = std::min (127, sorted[(size_t) i].note + 12 * o);
                vels[(size_t) count++] = sorted[(size_t) i].velocity;
            }
        if (count == 0) return;
        const int offAt = at + std::max (1, (int) (samplesPerStep * std::clamp (s.gate, 0.05f, 1.0f)));
        const int ch = held[0].channel;

        if (s.pattern == ArpPattern::Chord)
        {
            for (int i = 0; i < count; ++i)
                startNote (notes[(size_t) i], vels[(size_t) i], ch, at, offAt, emit);
            return;
        }
        int pick = 0;
        switch (s.pattern)
        {
            case ArpPattern::Down:   pick = count - 1 - (arpPos % count); ++arpPos; break;
            case ArpPattern::Random: pick = (int) (rng.uniform() * (float) count) % count; break;
            case ArpPattern::UpDown:
            {
                if (count == 1) { pick = 0; break; }
                const int cycle = 2 * count - 2;
                const int p = arpPos % cycle;
                pick = p < count ? p : cycle - p;
                ++arpPos;
                break;
            }
            case ArpPattern::Up:
            case ArpPattern::AsPlayed:
            case ArpPattern::Chord:
            default:                 pick = arpPos % count; ++arpPos; break;
        }
        lastStepPlayed = pick;
        startNote (notes[(size_t) pick], vels[(size_t) pick], ch, at, offAt, emit);
    }

    template <typename Emit>
    void startNote (int note, float vel, int ch, int on, int off, Emit&& emit) noexcept
    {
        for (int a = 0; a < activeCount; ++a) // retriggering an already sounding note: end it first
            if (active[(size_t) a].note == note && active[(size_t) a].started && on <= currentSample)
            {
                emit ({ on, false, note, 0.0f, active[(size_t) a].channel });
                active[(size_t) a] = active[(size_t) --activeCount];
                break;
            }
        if (on <= currentSample)
        {
            emit ({ on, true, note, vel, ch });
            if (activeCount < kMaxActive)
                active[(size_t) activeCount++] = { note, ch, on, off, vel, true };
        }
        else if (activeCount < kMaxActive) // ratchet repeats later in the step: schedule
            active[(size_t) activeCount++] = { note, ch, on, off, vel, false };
    }

    float sr = 48000.0f;
    std::array<Held, kMaxHeld> held {};
    std::array<Active, kMaxActive> active {};
    int heldCount = 0, activeCount = 0, physicalDown = 0;
    long stepIndex = 0;
    int arpPos = 0, arpDir = 1, lastStepPlayed = 0, currentSample = 0;
    double nextStepBeat = -1.0, freeBeat = 0.0;
    bool latched = false, lastSynced = false;
    float lane1 = 0.0f, lane2 = 0.0f;
    Rng rng { 4242 };
};

} // namespace mbx::dsp
