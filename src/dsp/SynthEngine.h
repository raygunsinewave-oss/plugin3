// Polyphonic voice manager + voices. JUCE-free and real-time safe: all voices are
// pre-allocated, rendering happens in fixed control-rate chunks with smoothed params.
#pragma once

#include "Envelope.h"
#include "Oscillator.h"

namespace mbx::dsp
{
inline constexpr int kNumOscillators = 6;

struct EngineSettings
{
    std::array<OscSettings, kNumOscillators> osc {};
    Envelope::Params amp {};
    float ampVelocity = 0.5f;   // 0 = ignore velocity, 1 = full range
    int polyphony     = 16;     // 1..32
    float bendRange   = 2.0f;   // semitones
    float masterGain  = 0.5f;   // linear
};

class Voice
{
public:
    static constexpr int kMaxChunk = 64;

    void prepare (const WavetableBank* bank, uint32_t seed) noexcept
    {
        for (int i = 0; i < kNumOscillators; ++i)
        {
            osc[(size_t) i].setBank (bank);
            osc[(size_t) i].setSeed (seed * 31u + (uint32_t) i * 977u + 1u);
        }
    }

    void start (int midiNote, float velocity, const EngineSettings& s, uint64_t order) noexcept
    {
        note       = midiNote;
        vel        = velocity;
        startOrder = order;
        keyDown    = true;
        sustained  = false;
        active     = true;
        stealing   = false;
        for (int i = 0; i < kNumOscillators; ++i)
            osc[(size_t) i].noteOn (s.osc[(size_t) i]);
        amp.reset();
        amp.noteOn();
    }

    /** Fast-fades the current note (~3 ms) and then starts the new one: no clicks on steal. */
    void steal (int midiNote, float velocity, uint64_t order) noexcept
    {
        if (! active)
            return; // caller should use start() instead
        stealing       = true;
        pendingNote    = midiNote;
        pendingVel     = velocity;
        pendingOrder   = order;
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
            amp.noteOff();
    }

    void releaseSustained() noexcept
    {
        if (sustained && ! keyDown)
        {
            sustained = false;
            amp.noteOff();
        }
    }

    void kill() noexcept
    {
        active = stealing = keyDown = sustained = false;
        amp.reset();
    }

    void render (const EngineSettings& s, float sampleRate, float bendSemis, float* L, float* R, int n) noexcept
    {
        if (! active || n <= 0)
            return;

        amp.setParams (s.amp, sampleRate);
        std::fill_n (scratchL.data(), n, 0.0f);
        std::fill_n (scratchR.data(), n, 0.0f);

        const float hz = midiNoteToHz ((float) note + bendSemis);
        for (int i = 0; i < kNumOscillators; ++i)
            osc[(size_t) i].render (s.osc[(size_t) i], hz, sampleRate, scratchL.data(), scratchR.data(), n);

        const float velGain = 1.0f - s.ampVelocity + s.ampVelocity * vel;
        const float fadeStep = 1.0f / (0.003f * sampleRate);

        for (int i = 0; i < n; ++i)
        {
            float g = amp.next() * velGain;
            if (stealing)
            {
                g *= fadeGain;
                fadeGain = std::max (0.0f, fadeGain - fadeStep);
            }
            L[i] += scratchL[(size_t) i] * g;
            R[i] += scratchR[(size_t) i] * g;
        }

        if (stealing && fadeGain <= 0.0f)
        {
            const bool keyUp = pendingKeyUp;
            start (pendingNote, pendingVel, s, pendingOrder);
            if (keyUp)
                release (false);
        }
        else if (! amp.isActive())
        {
            active = false;
        }
    }

    bool isActive() const noexcept      { return active; }
    bool isStealing() const noexcept    { return stealing; }
    bool isKeyDown() const noexcept     { return keyDown; }
    bool isReleasing() const noexcept   { return active && ! stealing && amp.isReleasing(); }
    int  getKeyNote() const noexcept    { return stealing ? pendingNote : note; }
    uint64_t getOrder() const noexcept  { return stealing ? pendingOrder : startOrder; }
    float getEnvelopeLevel() const noexcept { return amp.getValue(); }

private:
    std::array<Oscillator, kNumOscillators> osc;
    Envelope amp;
    std::array<float, kMaxChunk> scratchL {}, scratchR {};

    int note = 60;
    float vel = 1.0f;
    uint64_t startOrder = 0;
    bool active = false, keyDown = false, sustained = false;

    bool stealing = false, pendingKeyUp = false;
    int pendingNote = 60;
    float pendingVel = 1.0f, fadeGain = 1.0f;
    uint64_t pendingOrder = 0;
};

class SynthEngine
{
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kChunk     = 32;
    static_assert (kChunk <= Voice::kMaxChunk);

    void prepare (const WavetableBank* bank, float newSampleRate) noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
            voices[(size_t) i].prepare (bank, (uint32_t) i + 1u);
        setSampleRate (newSampleRate);
        allSoundOff();
    }

    void setSampleRate (float newSampleRate) noexcept { sampleRate = std::max (1000.0f, newSampleRate); }
    float getSampleRate() const noexcept { return sampleRate; }

    /** Block-rate parameter update. Continuous values are smoothed internally. */
    void setTarget (const EngineSettings& t) noexcept
    {
        target = t;
        if (! hasCurrent)
        {
            current     = t;
            prevMaster  = t.masterGain;
            hasCurrent  = true;
        }
    }

    void noteOn (int note, float velocity) noexcept
    {
        const uint64_t order = ++noteCounter;
        const int limit = std::clamp (target.polyphony, 1, kMaxVoices);

        // Same key re-struck: retrigger that voice (declicked)
        for (auto& v : voices)
            if (v.isActive() && v.getKeyNote() == note)
            {
                v.steal (note, velocity, order);
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
            freeVoice->start (note, velocity, current, order);
            return;
        }

        // Steal: quietest releasing voice first, otherwise the oldest note.
        Voice* victim = nullptr;
        for (auto& v : voices)
            if (v.isActive() && v.isReleasing()
                && (victim == nullptr || v.getEnvelopeLevel() < victim->getEnvelopeLevel()))
                victim = &v;
        if (victim == nullptr)
            for (auto& v : voices)
                if (v.isActive() && (victim == nullptr || v.getOrder() < victim->getOrder()))
                    victim = &v;
        if (victim != nullptr)
            victim->steal (note, velocity, order);
    }

    void noteOff (int note) noexcept
    {
        for (auto& v : voices)
            if (v.isActive() && v.isKeyDown() && v.getKeyNote() == note)
                v.release (sustainDown);
    }

    void setSustainPedal (bool down) noexcept
    {
        sustainDown = down;
        if (! down)
            for (auto& v : voices)
                v.releaseSustained();
    }

    /** normalised -1..1 */
    void setPitchBend (float norm) noexcept { bendTarget = std::clamp (norm, -1.0f, 1.0f); }

    void allNotesOff() noexcept
    {
        sustainDown = false;
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
    }

    /** Adds n samples to L/R. Split calls at MIDI event positions for sample accuracy. */
    void render (float* L, float* R, int n) noexcept
    {
        while (n > 0)
        {
            const int c = std::min (n, kChunk);
            smooth (c);

            const float bendSemis = bendCurrent * current.bendRange;
            for (auto& v : voices)
                v.render (current, sampleRate, bendSemis, L, R, c);

            // master gain ramp
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
        int n = 0;
        for (const auto& v : voices)
            n += v.isActive() ? 1 : 0;
        return n;
    }

private:
    void smooth (int numSamples) noexcept
    {
        const float a  = 1.0f - std::exp (-(float) numSamples / (0.015f * sampleRate)); // ~15 ms
        const float ab = 1.0f - std::exp (-(float) numSamples / (0.004f * sampleRate)); // pitch bend ~4 ms
        auto glide = [a] (float& cur, float tgt) { cur += (tgt - cur) * a; };

        for (size_t i = 0; i < (size_t) kNumOscillators; ++i)
        {
            auto& c = current.osc[i];
            const auto& t = target.osc[i];
            c.on = t.on; c.type = t.type; c.shape = t.shape; c.noise = t.noise; c.table = t.table;
            c.pitchSemis = t.pitchSemis; c.unison = t.unison; c.phase = t.phase; c.phaseRand = t.phaseRand;
            glide (c.fineCents, t.fineCents);
            glide (c.level, t.level);
            glide (c.pan, t.pan);
            glide (c.detuneCents, t.detuneCents);
            glide (c.blend, t.blend);
            glide (c.spread, t.spread);
            glide (c.wtPosition, t.wtPosition);
            glide (c.pulseWidth, t.pulseWidth);
        }
        current.amp         = target.amp;
        current.ampVelocity = target.ampVelocity;
        current.polyphony   = target.polyphony;
        current.bendRange   = target.bendRange;
        glide (current.masterGain, target.masterGain);
        bendCurrent += (bendTarget - bendCurrent) * ab;
    }

    std::array<Voice, kMaxVoices> voices;
    EngineSettings target, current;
    bool hasCurrent = false, sustainDown = false;
    float sampleRate = 48000.0f, prevMaster = 0.5f, bendTarget = 0.0f, bendCurrent = 0.0f;
    uint64_t noteCounter = 0;
};

} // namespace mbx::dsp
