// DAHDSR envelope with per-stage curves, optional loop and a "punch" transient.
// Stage progress is normalised (0..1) so times can be changed mid-stage without jumps.
#pragma once

#include "DspCommon.h"

namespace mbx::dsp
{
class Envelope
{
public:
    struct Params
    {
        float delay = 0.0f, attack = 0.005f, hold = 0.0f, decay = 0.3f, sustain = 0.8f, release = 0.25f; // seconds / level
        float attackCurve = 0.0f, decayCurve = 0.6f, releaseCurve = 0.6f; // -1..1
        float punch = 0.0f; // 0..1, amp envelope only
        bool loop = false;  // cycles A-H-D while the key is held
    };

    enum class Stage { Idle, Delay, Attack, Hold, Decay, Sustain, Release };

    /** Call at control rate (every chunk). Cheap. */
    void setParams (const Params& p, float sampleRate) noexcept
    {
        params = p;
        const float sr = std::max (1.0f, sampleRate);
        incDelay   = increment (p.delay, 0.0f, sr);
        incAttack  = increment (p.attack, 0.0003f, sr); // tiny minimums de-click "zero" times
        incHold    = increment (p.hold, 0.0f, sr);
        incDecay   = increment (p.decay, 0.001f, sr);
        incRelease = increment (p.release, 0.001f, sr);
        punchDecay = std::exp (-1.0f / (0.018f * sr)); // ~18 ms punch tail
        sustainCoef = 1.0f - std::exp (-1.0f / (0.005f * sr));
    }

    /** Starts (or re-starts, legato-safe) the envelope from its current value. */
    void noteOn() noexcept
    {
        startValue = value;
        punchValue = 0.0f;
        enter (params.delay > 0.0f ? Stage::Delay : Stage::Attack);
    }

    void noteOff() noexcept
    {
        if (stage != Stage::Idle && stage != Stage::Release)
        {
            startValue = value;
            enter (Stage::Release);
        }
    }

    void reset() noexcept
    {
        stage = Stage::Idle;
        value = punchValue = 0.0f;
    }

    float next() noexcept
    {
        switch (stage)
        {
            case Stage::Idle: return 0.0f;

            case Stage::Delay:
                if (advance (incDelay)) enter (Stage::Attack);
                break;

            case Stage::Attack:
                if (advance (incAttack))
                {
                    value      = 1.0f;
                    punchValue = params.punch;
                    enter (params.hold > 0.0f ? Stage::Hold : Stage::Decay);
                }
                else
                    value = startValue + (1.0f - startValue) * curveShape (x, params.attackCurve);
                break;

            case Stage::Hold:
                value = 1.0f;
                if (advance (incHold)) enter (Stage::Decay);
                break;

            case Stage::Decay:
                if (advance (incDecay))
                {
                    value = params.sustain;
                    if (params.loop) { startValue = value; enter (Stage::Attack); }
                    else             enter (Stage::Sustain);
                }
                else
                    value = 1.0f + (params.sustain - 1.0f) * curveShape (x, params.decayCurve);
                break;

            case Stage::Sustain:
                value += (params.sustain - value) * sustainCoef; // follows sustain changes smoothly
                break;

            case Stage::Release:
                if (advance (incRelease))
                {
                    value = 0.0f;
                    stage = Stage::Idle;
                }
                else
                    value = startValue * (1.0f - curveShape (x, params.releaseCurve));
                break;
        }

        punchValue *= punchDecay;
        return value * (1.0f + punchValue);
    }

    bool isActive() const noexcept   { return stage != Stage::Idle; }
    bool isReleasing() const noexcept { return stage == Stage::Release; }
    Stage getStage() const noexcept  { return stage; }
    float getValue() const noexcept  { return value; }

private:
    static float increment (float seconds, float minSeconds, float sr) noexcept
    {
        const float t = std::max (seconds, minSeconds);
        return t <= 0.0f ? 1.0f : 1.0f / (t * sr);
    }

    bool advance (float inc) noexcept
    {
        x += inc;
        return x >= 1.0f;
    }

    void enter (Stage s) noexcept
    {
        stage = s;
        x     = 0.0f;
    }

    Params params;
    Stage stage = Stage::Idle;
    float x = 0.0f, value = 0.0f, startValue = 0.0f, punchValue = 0.0f;
    float incDelay = 1, incAttack = 1, incHold = 1, incDecay = 1, incRelease = 1;
    float punchDecay = 0.99f, sustainCoef = 0.01f;
};

} // namespace mbx::dsp
