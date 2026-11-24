#pragma once

#include "Widgets.h"
#include "Displays.h"

namespace mbx::ui
{
/** Step grid: shows one lane at a time for 16 steps (4 pages = 64 steps). Drag to paint values. */
class StepGrid final : public juce::Component, private juce::Timer
{
public:
    enum Lane { Pitch = 0, Velocity, Gate, Probability, Ratchet, Slide, On, Mod1, Mod2, NumLanes };
    static inline const char* laneNames[NumLanes] = { "PITCH", "VEL", "GATE", "PROB", "RATCH", "SLIDE", "ON", "MOD 1", "MOD 2" };

    explicit StepGrid (MysteryBoxProcessor& p) : proc (p) { startTimerHz (GlobalSettings::get().fps (20)); }

    void setLane (int l) { lane = l; repaint(); }
    void setPage (int p) { page = p; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawScreen (g, r, colours::magenta);
        const auto pat = proc.getAssets().sequence.get();
        const int length = juce::roundToInt (rawParam (proc.getState(), params::id::seqLength));
        const bool seqMode = juce::roundToInt (rawParam (proc.getState(), params::id::arpMode)) == 2;
        const int playing = seqMode && proc.getActiveVoices() > 0 ? proc.getSeqStep() : -1;
        auto a = gridArea();
        const float w = a.getWidth() / 16.0f;
        for (int i = 0; i < 16; ++i)
        {
            const int idx = page * 16 + i;
            const auto& st = pat.steps[(size_t) idx];
            auto cell = juce::Rectangle<float> (a.getX() + (float) i * w + 2.0f, a.getY(), w - 4.0f, a.getHeight());
            const bool inRange = idx < length;
            g.setColour ((i / 4) % 2 == 0 ? colours::magenta.withAlpha (0.06f) : colours::cyan.withAlpha (0.04f));
            g.fillRect (cell);
            const float v = valueOf (st);
            const auto col = ! inRange ? colours::textDim.withAlpha (0.3f) : ! st.on ? colours::textDim : colours::magenta;
            g.setColour (col.withAlpha (idx == playing ? 1.0f : 0.75f));
            if (lane == Pitch)
            {
                const float cy = cell.getCentreY(), h = st.pitch / 24.0f * cell.getHeight() * 0.5f;
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (cell.getX(), std::min (cy, cy - h), cell.getRight(), std::max (cy, cy - h) + 2.0f));
                g.setFont (fonts::mono (9.0f));
                g.setColour (colours::text);
                g.drawText ((st.pitch > 0 ? "+" : "") + juce::String ((int) std::round (st.pitch)), cell.removeFromBottom (12.0f), juce::Justification::centred);
            }
            else
                g.fillRect (cell.withTop (cell.getBottom() - cell.getHeight() * v));
            if (idx == playing)
                drawCornerBrackets (g, cell.expanded (1.0f), colours::lime, 6.0f, 1.5f);
            g.setFont (fonts::mono (8.5f));
            g.setColour (colours::textDim);
            g.drawText (juce::String (idx + 1), juce::Rectangle<float> (cell.getX(), r.getY() + 3.0f, cell.getWidth(), 10.0f), juce::Justification::centred);
        }
        g.setColour (colours::magenta);
        g.setFont (fonts::mono (10.0f));
        g.drawText (juce::String (laneNames[lane]) + " LANE  //  STEPS " + juce::String (page * 16 + 1) + "-" + juce::String (page * 16 + 16),
                    r.reduced (10.0f, 2.0f), juce::Justification::bottomLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override { edit (e.position, true); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e.position, false); }

private:
    juce::Rectangle<float> gridArea() const { return getLocalBounds().toFloat().reduced (8.0f, 16.0f); }

    float valueOf (const dsp::SeqStep& s) const
    {
        switch (lane)
        {
            case Velocity: return s.velocity;
            case Gate: return s.gate;
            case Probability: return s.probability;
            case Ratchet: return (float) s.ratchet / 4.0f;
            case Slide: return s.slide ? 1.0f : 0.0f;
            case On: return s.on ? 1.0f : 0.0f;
            case Mod1: return s.mod1;
            case Mod2: return s.mod2;
            case Pitch:
            default: return (s.pitch + 24.0f) / 48.0f;
        }
    }

    void edit (juce::Point<float> p, bool isDown)
    {
        const auto a = gridArea();
        const int i = juce::jlimit (0, 15, (int) ((p.x - a.getX()) / (a.getWidth() / 16.0f)));
        const float v = juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight());
        auto pat = proc.getAssets().sequence.get();
        auto& st = pat.steps[(size_t) (page * 16 + i)];
        switch (lane)
        {
            case Pitch:       st.pitch = std::round (juce::jlimit (-24.0f, 24.0f, (a.getCentreY() - p.y) / (a.getHeight() * 0.5f) * 24.0f)); break;
            case Velocity:    st.velocity = v; break;
            case Gate:        st.gate = juce::jmax (0.05f, v); break;
            case Probability: st.probability = v; break;
            case Ratchet:     st.ratchet = juce::jlimit (1, 4, 1 + (int) (v * 4.0f)); break;
            case Mod1:        st.mod1 = v; break;
            case Mod2:        st.mod2 = v; break;
            case Slide:
            case On:
            default:
                if (isDown) toggleValue = ! (lane == Slide ? st.slide : st.on);
                (lane == Slide ? st.slide : st.on) = toggleValue;
                break;
        }
        proc.getAssets().sequence.set (pat);
        repaint();
    }

    void timerCallback() override { repaint(); }

    MysteryBoxProcessor& proc;
    int lane = Pitch, page = 0;
    bool toggleValue = true;
};

class ArpSeqPage final : public juce::Component
{
public:
    explicit ArpSeqPage (MysteryBoxProcessor& p) : grid (p)
    {
        auto& s = p.getState();
        mode.bind (s, params::id::arpMode);
        pattern.bind (s, params::id::arpPattern);
        rate.bind (s, params::id::arpRate);
        seqRate.bind (s, params::id::seqRate);
        gate.bind (s, params::id::arpGate);
        swing.bind (s, params::id::arpSwing);
        octaves.bind (s, params::id::arpOctaves);
        length.bind (s, params::id::seqLength);
        seqSwing.bind (s, params::id::seqSwing);
        hold.bind (s, params::id::arpHold);
        retrig.bind (s, params::id::arpRetrigger);
        follow.bind (s, params::id::seqFollow);
        midiOut.bind (s, params::id::midiOut);
        midiOut.setTooltip ("Also send arp / sequencer / XD notes to the host's MIDI output");
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &mode, &pattern, &rate, &seqRate, &gate, &swing, &octaves, &length, &seqSwing, &hold, &retrig, &follow, &midiOut, &grid })
            addAndMakeVisible (c);

        for (int l = 0; l < StepGrid::NumLanes; ++l)
        {
            auto b = std::make_unique<juce::TextButton> (StepGrid::laneNames[l]);
            b->setRadioGroupId (6001);
            b->setClickingTogglesState (true);
            b->getProperties().set ("accent", (juce::int64) colours::magenta.getARGB());
            b->onClick = [this, l] { grid.setLane (l); };
            addAndMakeVisible (*b);
            laneButtons.push_back (std::move (b));
        }
        for (int pg = 0; pg < 4; ++pg)
        {
            auto b = std::make_unique<juce::TextButton> (juce::String (pg * 16 + 1) + "-" + juce::String (pg * 16 + 16));
            b->setRadioGroupId (6002);
            b->setClickingTogglesState (true);
            b->onClick = [this, pg] { grid.setPage (pg); };
            addAndMakeVisible (*b);
            pageButtons.push_back (std::move (b));
        }
        laneButtons[0]->setToggleState (true, juce::dontSendNotification);
        pageButtons[0]->setToggleState (true, juce::dontSendNotification);
    }

    void resized() override
    {
        mode.setBounds (0, 0, 110, 34);
        pattern.setBounds (116, 0, 96, 34);
        rate.setBounds (218, 0, 70, 34);
        seqRate.setBounds (294, 0, 70, 34);
        ParamKnob* k[] = { &gate, &swing, &octaves, &length, &seqSwing };
        for (int i = 0; i < 5; ++i)
            k[i]->setBounds (i * 74, 40, 72, 92);
        hold.setBounds (0, 140, 62, 22);
        retrig.setBounds (66, 140, 66, 22);
        follow.setBounds (136, 140, 70, 22);
        midiOut.setBounds (210, 140, 80, 22);

        const int gx = 384;
        for (size_t l = 0; l < laneButtons.size(); ++l)
            laneButtons[l]->setBounds (gx + (int) l * 58, 0, 56, 20);
        for (size_t pg = 0; pg < pageButtons.size(); ++pg)
            pageButtons[pg]->setBounds (gx + 9 * 58 + 6 + (int) pg * 76, 0, 72, 20);
        grid.setBounds (gx, 26, getWidth() - gx, getHeight() - 26);
    }

private:
    StepGrid grid;
    ParamChoice mode { "MODE" }, pattern { "PATTERN" }, rate { "ARP RATE" }, seqRate { "SEQ RATE" };
    ParamKnob gate { "GATE", colours::magenta }, swing { "SWING", colours::magenta }, octaves { "OCTAVES", colours::magenta },
        length { "STEPS", colours::magenta }, seqSwing { "SEQ SWING", colours::magenta };
    ParamToggle hold { "HOLD", colours::magenta }, retrig { "RETRIG", colours::magenta }, follow { "FOLLOW", colours::magenta },
        midiOut { "MIDI OUT", colours::amber };
    std::vector<std::unique_ptr<juce::TextButton>> laneButtons, pageButtons;
};

} // namespace mbx::ui
