#include "FxPage.h"

namespace mbx::ui
{
using dsp::fx::FxType;

namespace
{
    juce::Colour fxColour (FxType t)
    {
        switch (t)
        {
            case FxType::Distortion: case FxType::Stutter: case FxType::RingMod: case FxType::FreqShift: return colours::amber;
            case FxType::OTT: case FxType::Compressor: case FxType::Transient: case FxType::Limiter: case FxType::Ducker: return colours::lime;
            case FxType::Chorus: case FxType::Flanger: case FxType::Phaser: case FxType::Ensemble: case FxType::Widener: return colours::magenta;
            case FxType::Off: case FxType::Delay: case FxType::Reverb: case FxType::Formant: case FxType::Vocoder:
            case FxType::TiltEq: case FxType::Count:
            default: return colours::cyan;
        }
    }

    //==========================================================================
    /** Per-type animated display for a slot. */
    class SlotDisplay final : public juce::Component, private juce::Timer
    {
    public:
        SlotDisplay (MysteryBoxProcessor& p, int s) : proc (p), slot (s) { startTimerHz (GlobalSettings::get().fps (24)); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            const auto type = proc.getFxRack().getUnitType (slot);
            const auto accent = fxColour (type);
            drawScreen (g, r, accent);
            auto a = r.reduced (8.0f, 14.0f);
            auto prm = [this] (int k) { return rawParam (proc.getState(), params::fxParamId (slot, k)); };
            const float act = proc.getFxRack().getActivity (slot);
            juce::Path path;

            switch (type)
            {
                case FxType::Off:
                case FxType::Count:
                    g.setColour (colours::textDim);
                    g.setFont (fonts::mono (10.0f));
                    g.drawText ("EMPTY", a, juce::Justification::centred);
                    return;
                case FxType::Distortion:
                {
                    const float drive = dsp::dbToGain (prm (0) * 42.0f);
                    const int mode = dsp::fx::stepped (prm (1), 5);
                    for (int i = 0; i <= 60; ++i)
                    {
                        const float x = (float) i / 30.0f - 1.0f;
                        float y = x * drive;
                        y = mode == 0 ? std::tanh (y) : mode == 1 ? y / (1 + std::abs (y)) * 1.4f : mode == 2 ? std::sin (std::clamp (y, -40.0f, 40.0f) * 1.5708f)
                          : mode == 3 ? std::clamp (y, -1.0f, 1.0f) : std::round (std::clamp (y * 0.25f, -1.0f, 1.0f) * 4.0f) / 4.0f;
                        const auto pt = juce::Point<float> (a.getCentreX() + x * a.getWidth() * 0.5f, a.getCentreY() - juce::jlimit (-1.2f, 1.2f, y) * a.getHeight() * 0.42f);
                        i == 0 ? path.startNewSubPath (pt) : path.lineTo (pt);
                    }
                    break;
                }
                case FxType::OTT: case FxType::Compressor: case FxType::Limiter: case FxType::Transient:
                {
                    const float gr = juce::jlimit (-24.0f, 24.0f, act);
                    const float h = a.getHeight() * std::abs (gr) / 24.0f;
                    g.setColour (accent.withAlpha (0.8f));
                    g.fillRect (gr < 0 ? juce::Rectangle<float> (a.getCentreX() - 10, a.getY(), 20, h) : juce::Rectangle<float> (a.getCentreX() - 10, a.getBottom() - h, 20, h));
                    g.setFont (fonts::mono (10.0f));
                    g.setColour (colours::text);
                    g.drawText ((type == FxType::OTT ? "AVG " : "GR ") + juce::String (gr, 1) + " dB", a, juce::Justification::centredBottom);
                    for (float y = a.getY(); y < a.getBottom(); y += 6.0f) { g.setColour (accent.withAlpha (0.15f)); g.fillRect (a.getX(), y, a.getWidth(), 1.0f); }
                    return;
                }
                case FxType::Delay:
                {
                    const float t = std::max (0.01f, act), fb = prm (2);
                    float lvl = 1.0f;
                    for (int k = 0; k < 12 && lvl > 0.03f; ++k, lvl *= std::max (0.05f, fb))
                    {
                        const float x = a.getX() + a.getWidth() * std::fmod ((float) k * t / 2.0f + 0.02f, 1.0f);
                        g.setColour (accent.withAlpha (0.25f + 0.75f * lvl));
                        g.fillRect (x, a.getBottom() - a.getHeight() * lvl, 3.0f, a.getHeight() * lvl);
                    }
                    g.setColour (colours::text);
                    g.setFont (fonts::mono (10.0f));
                    g.drawText (juce::String (t * 1000.0f, 0) + " ms", a, juce::Justification::topRight);
                    return;
                }
                case FxType::Reverb:
                {
                    const float rt = std::max (0.2f, act);
                    for (int i = 0; i <= 80; ++i)
                    {
                        const float t = (float) i / 80.0f;
                        const float y = std::exp (-6.9f * t * 4.0f / rt) * (0.7f + 0.3f * std::sin ((float) i * 2.3f + anim * 3.0f));
                        const auto pt = juce::Point<float> (a.getX() + t * a.getWidth(), a.getBottom() - y * a.getHeight());
                        i == 0 ? path.startNewSubPath (pt) : path.lineTo (pt);
                    }
                    g.setColour (colours::text);
                    g.setFont (fonts::mono (10.0f));
                    g.drawText ("RT60 " + juce::String (rt, 1) + "s", a, juce::Justification::topRight);
                    break;
                }
                case FxType::Ducker:
                {
                    for (int i = 0; i <= 80; ++i)
                    {
                        const float t = (float) i / 80.0f;
                        const float ph = std::fmod (t * 2.0f, 1.0f);
                        const float y = 1.0f - prm (0) * (1.0f - dsp::curveShape (ph, -dsp::fx::bip (prm (2)) * 0.95f));
                        const auto pt = juce::Point<float> (a.getX() + t * a.getWidth(), a.getBottom() - y * a.getHeight());
                        i == 0 ? path.startNewSubPath (pt) : path.lineTo (pt);
                    }
                    g.setColour (colours::lime.withAlpha (0.6f));
                    g.fillRect (a.getX(), a.getBottom() - act * a.getHeight(), 6.0f, act * a.getHeight());
                    break;
                }
                case FxType::Stutter:
                {
                    const bool on = act > 0.5f;
                    g.setColour (on ? colours::amber : colours::textDim);
                    for (int k = 0; k < 8; ++k)
                        g.fillRect (a.getX() + a.getWidth() * (float) k / 8.0f + 2.0f, a.getCentreY() - 8.0f, a.getWidth() / 8.0f - 4.0f,
                                    16.0f * (on ? (0.4f + 0.6f * std::abs (std::sin (anim * 6.0f + (float) k))) : 0.3f));
                    g.setFont (fonts::mono (10.0f));
                    g.drawText (on ? "ENGAGED" : "ARMED", a, juce::Justification::centredBottom);
                    return;
                }
                case FxType::Chorus: case FxType::Flanger: case FxType::Phaser: case FxType::Ensemble: case FxType::RingMod:
                case FxType::FreqShift: case FxType::Formant: case FxType::Vocoder: case FxType::Widener: case FxType::TiltEq:
                default:
                {
                    const float phase = type == FxType::FreqShift || type == FxType::Vocoder || type == FxType::Widener || type == FxType::TiltEq
                                            ? anim * 0.3f : act;
                    for (int i = 0; i <= 80; ++i)
                    {
                        const float t = (float) i / 80.0f;
                        const float y = std::sin ((t * 2.0f + phase) * dsp::kTwoPi) * (type == FxType::RingMod ? std::sin (t * 40.0f) : 1.0f);
                        const auto pt = juce::Point<float> (a.getX() + t * a.getWidth(), a.getCentreY() - y * a.getHeight() * 0.4f);
                        i == 0 ? path.startNewSubPath (pt) : path.lineTo (pt);
                    }
                    break;
                }
            }
            strokeGlow (g, path, accent, 1.5f, 0.8f);
        }

    private:
        void timerCallback() override { anim += 1.0f / 24.0f; repaint(); }
        MysteryBoxProcessor& proc;
        int slot;
        float anim = 0.0f;
    };

    //==========================================================================
    class SlotCard final : public juce::Component, public juce::DragAndDropTarget, private juce::Timer
    {
    public:
        SlotCard (MysteryBoxProcessor& p, int s) : proc (p), slot (s), display (p, s)
        {
            auto& st = p.getState();
            type.bind (st, params::fxId (s, params::id::fxType));
            type.box.addMouseListener (this, false);
            type.box.onChange = [this] {
                if (userPicking) // a type picked by hand loads that effect's defaults
                {
                    userPicking = false;
                    proc.getFxRack().applyDefaults (slot, (FxType) type.box.getSelectedItemIndex());
                }
                refreshLabels();
            };
            on.bind (st, params::fxId (s, params::id::fxOn));
            mix.bind (st, params::fxId (s, params::id::fxMix));
            if (isSend())
                send.bind (st, params::fxId (s, params::id::fxSend));
            for (int k = 0; k < dsp::fx::kParamsPerFx; ++k)
            {
                knobs[(size_t) k] = std::make_unique<ParamKnob> ("P" + juce::String (k + 1), colours::cyan, false);
                knobs[(size_t) k]->bind (st, params::fxParamId (s, k));
                addAndMakeVisible (*knobs[(size_t) k]);
            }
            for (juce::Component* c : std::initializer_list<juce::Component*> { &type, &on, &mix, &display })
                addAndMakeVisible (c);
            if (isSend())
                addAndMakeVisible (send);
            refreshLabels();
            startTimerHz (4);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.eventComponent == &type.box)
                userPicking = true;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (isSend() || e.eventComponent != this || e.getMouseDownY() > 26)
                return;
            if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
                if (! c->isDragAndDropActive())
                    c->startDragging ("fxslot:" + juce::String (slot), this, juce::ScaledImage (createComponentSnapshot ({ 0, 0, getWidth(), 26 })));
        }

        bool isInterestedInDragSource (const SourceDetails& d) override
        {
            return ! isSend() && d.description.toString().startsWith ("fxslot:");
        }
        void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
        void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
        void itemDropped (const SourceDetails& d) override
        {
            dropHover = false;
            const int from = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
            // move: shift the slots in between (keeps the rest of the chain order)
            auto& rack = proc.getFxRack();
            if (from < slot) for (int k = from; k < slot; ++k) rack.swapSlots (k, k + 1);
            else             for (int k = from; k > slot; --k) rack.swapSlots (k, k - 1);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto t = (FxType) juce::jmax (0, type.box.getSelectedItemIndex());
            const auto accent = isSend() ? colours::magenta : fxColour (t);
            const juce::String title = isSend() ? (slot == params::kNumFxSlots ? "SEND A" : "SEND B")
                                                : "FX." + juce::String (slot + 1).paddedLeft ('0', 2);
            drawTechPanel (g, getLocalBounds().toFloat(), title, accent, isSend() ? "PARALLEL BUS" : "DRAG HEADER TO MOVE");
            if (dropHover)
                drawCornerBrackets (g, getLocalBounds().toFloat().reduced (2.0f), colours::lime, 22.0f, 2.5f);
        }

        void resized() override
        {
            type.setBounds (isSend() ? 92 : 84, 4, 132, 20);
            on.setBounds (isSend() ? 228 : 220, 5, 36, 18);
            display.setBounds (10, 32, 118, 128);
            for (int k = 0; k < dsp::fx::kParamsPerFx; ++k)
                knobs[(size_t) k]->setBounds (134 + (k % 3) * 58, 30 + (k / 3) * 68, 58, 68);
            if (isSend())
            {
                send.setBounds (getWidth() - 76, 28, 70, 68);
                mix.setBounds (getWidth() - 76, 96, 70, 68);
            }
            else
                mix.setBounds (getWidth() - 78, 54, 72, 84);
        }

    private:
        bool isSend() const noexcept { return slot >= params::kNumFxSlots; }

        void refreshLabels()
        {
            const auto t = (FxType) juce::jmax (0, type.box.getSelectedItemIndex());
            const auto& info = dsp::fx::fxInfo (t);
            for (int k = 0; k < dsp::fx::kParamsPerFx; ++k)
            {
                const juce::String name = info.params[(size_t) k];
                knobs[(size_t) k]->setVisible (t != FxType::Off && name.isNotEmpty());
                knobs[(size_t) k]->setLabel (name);
                knobs[(size_t) k]->slider.getProperties().set ("accent", (juce::int64) fxColour (t).getARGB());
            }
            mix.setVisible (t != FxType::Off);
            repaint();
        }

        void timerCallback() override
        {
            const int t = type.box.getSelectedItemIndex();
            if (t != lastType) { lastType = t; refreshLabels(); }
        }

        MysteryBoxProcessor& proc;
        int slot;
        SlotDisplay display;
        ParamChoice type { "" };
        ParamToggle on { "ON" };
        ParamKnob mix { "MIX", colours::text }, send { "SEND", colours::magenta };
        std::array<std::unique_ptr<ParamKnob>, dsp::fx::kParamsPerFx> knobs;
        bool userPicking = false, dropHover = false;
        int lastType = -1;
    };

    class RackView final : public juce::Component
    {
    public:
        explicit RackView (MysteryBoxProcessor& p)
        {
            for (int k = 0; k < params::kNumFxSlots + 2; ++k)
            {
                cards.push_back (std::make_unique<SlotCard> (p, k));
                addAndMakeVisible (*cards.back());
            }
        }
        void resized() override
        {
            for (int k = 0; k < (int) cards.size(); ++k)
                cards[(size_t) k]->setBounds (16 + (k % 3) * 418, (k / 3) * 170, 410, 164);
        }

    private:
        std::vector<std::unique_ptr<SlotCard>> cards;
    };

    //==========================================================================
    /** Analyser + master EQ curve with draggable band nodes. */
    class EqDisplay final : public SpectrumAnalyser
    {
    public:
        explicit EqDisplay (MysteryBoxProcessor& p)
            : SpectrumAnalyser (p.getScope(), [&p] { return p.getSampleRateHz(); }), proc (p) {}

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragBand = hitBand (e.position);
            if (dragBand >= 0)
                for (auto* id : { params::id::eqFreq, params::id::eqGain })
                    proc.getState().getParameter (params::eqId (dragBand, id))->beginChangeGesture();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragBand < 0) return;
            const auto a = plotArea();
            set (dragBand, params::id::eqFreq, juce::jlimit (20.0f, 20000.0f, hzForX (e.position.x, a)));
            set (dragBand, params::id::eqGain, juce::jlimit (-18.0f, 18.0f, (a.getCentreY() - e.position.y) / (a.getHeight() * 0.5f) * 18.0f));
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragBand >= 0)
                for (auto* id : { params::id::eqFreq, params::id::eqGain })
                    proc.getState().getParameter (params::eqId (dragBand, id))->endChangeGesture();
            dragBand = -1;
        }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            const int b = hitBand (e.position);
            if (b < 0) return;
            const float q = rawParam (proc.getState(), params::eqId (b, params::id::eqQ));
            set (b, params::id::eqQ, juce::jlimit (0.1f, 12.0f, q * std::exp2 (w.deltaY * 2.0f)));
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int b = hitBand (e.position);
            if (b >= 0) set (b, params::id::eqGain, 0.0f);
        }

    private:
        juce::Point<float> nodePos (int b) const
        {
            const auto a = plotArea();
            const float f = rawParam (proc.getState(), params::eqId (b, params::id::eqFreq));
            const float gdb = rawParam (proc.getState(), params::eqId (b, params::id::eqGain));
            return { xForHz (f, a), a.getCentreY() - gdb / 18.0f * a.getHeight() * 0.5f };
        }
        int hitBand (juce::Point<float> p) const
        {
            for (int b = 0; b < params::kNumEqBands; ++b)
                if (nodePos (b).getDistanceFrom (p) < 10.0f) return b;
            return -1;
        }
        void set (int b, const char* id, float v)
        {
            if (auto* prm = proc.getState().getParameter (params::eqId (b, id)))
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        }

        void paintOverlay (juce::Graphics& g, juce::Rectangle<float> a) override
        {
            g.setColour (colours::lime.withAlpha (0.25f));
            g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
            juce::Path curve;
            for (int i = 0; i <= 200; ++i)
            {
                const float x = a.getX() + a.getWidth() * (float) i / 200.0f;
                const float db = proc.getFxRack().eqMagnitudeDb (hzForX (x, a));
                const float y = a.getCentreY() - juce::jlimit (-18.0f, 18.0f, db) / 18.0f * a.getHeight() * 0.5f;
                i == 0 ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
            }
            strokeGlow (g, curve, colours::lime, 2.0f);
            for (int b = 0; b < params::kNumEqBands; ++b)
            {
                const bool bandOn = rawParam (proc.getState(), params::eqId (b, params::id::eqBandOn)) >= 0.5f;
                const auto p = nodePos (b);
                g.setColour (colours::base);
                g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (p));
                g.setColour (bandOn ? colours::lime : colours::textDim);
                g.drawEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (p), 1.5f);
                g.setFont (fonts::mono (9.0f));
                g.drawText (juce::String (b + 1), juce::Rectangle<float> (16.0f, 16.0f).withCentre (p), juce::Justification::centred);
            }
            g.setColour (colours::textDim);
            g.drawText ("DRAG NODES  //  WHEEL = Q  //  DBL-CLICK = 0 dB", a, juce::Justification::topRight);
        }

        MysteryBoxProcessor& proc;
        int dragBand = -1;
    };

    class MasterView final : public juce::Component
    {
    public:
        explicit MasterView (MysteryBoxProcessor& p)
            : proc (p), eq (p), outMeter (p.getOutputMeter(), false), inMeter (p.getInputMeter(), false),
              loudness (p.getOutputMeter()), gonio (p.getScope())
        {
            auto& st = p.getState();
            eqOn.bind (st, params::id::eqOn);
            for (int b = 0; b < params::kNumEqBands; ++b)
            {
                auto& band = bands[(size_t) b];
                band.on = std::make_unique<ParamToggle> ("B" + juce::String (b + 1));
                band.on->bind (st, params::eqId (b, params::id::eqBandOn));
                band.freq = std::make_unique<ParamKnob> ("FREQ", colours::lime);
                band.gain = std::make_unique<ParamKnob> ("GAIN", colours::lime, true);
                band.q = std::make_unique<ParamKnob> ("Q", colours::lime);
                band.freq->bind (st, params::eqId (b, params::id::eqFreq));
                band.gain->bind (st, params::eqId (b, params::id::eqGain));
                band.q->bind (st, params::eqId (b, params::id::eqQ));
                for (juce::Component* c : std::initializer_list<juce::Component*> { band.on.get(), band.freq.get(), band.gain.get(), band.q.get() })
                    addAndMakeVisible (c);
            }
            limiter.bind (st, params::id::outLimiter);
            ceiling.bind (st, params::id::outCeiling);
            master.bind (st, params::id::masterVolume);
            for (juce::Component* c : std::initializer_list<juce::Component*> { &eq, &eqOn, &outMeter, &loudness, &gonio, &limiter, &ceiling, &master })
                addAndMakeVisible (c);
            addChildComponent (inMeter);
        }

        void paint (juce::Graphics& g) override
        {
            drawTechPanel (g, { 16.0f, 0.0f, 852.0f, 680.0f }, "MASTER EQ  //  ANALYSER", colours::lime, "SYS.EQ // 6 BANDS");
            drawTechPanel (g, { 880.0f, 0.0f, 384.0f, 680.0f }, "OUTPUT", colours::cyan, "SYS.OUT // METERS");
            g.setFont (fonts::mono (9.0f));
            g.setColour (colours::textDim);
            g.drawText ("OUT L/R", 898, 34, 80, 12, juce::Justification::centredLeft);
            if (inMeter.isVisible())
                g.drawText ("SIDECHAIN", 1166, 34, 80, 12, juce::Justification::centredLeft);
        }

        void resized() override
        {
            eqOn.setBounds (300, 5, 50, 18);
            eq.setBounds (30, 32, 824, 448);
            for (int b = 0; b < params::kNumEqBands; ++b)
            {
                auto& band = bands[(size_t) b];
                const int x = 30 + b * 138;
                band.on->setBounds (x + 40, 488, 56, 20);
                band.freq->setBounds (x, 514, 46, 84);
                band.gain->setBounds (x + 46, 514, 46, 84);
                band.q->setBounds (x + 92, 514, 46, 84);
            }
            outMeter.setBounds (896, 48, 90, 400);
            inMeter.setBounds (1164, 48, 86, 200);
            loudness.setBounds (996, 48, 160, 92);
            gonio.setBounds (996, 148, 260, 300);
            limiter.setBounds (900, 470, 100, 22);
            ceiling.setBounds (900, 500, 90, 100);
            master.setBounds (1000, 470, 110, 130);
        }

        void visibilityChanged() override { inMeter.setVisible (proc.isSidechainActive()); }

    private:
        struct Band { std::unique_ptr<ParamToggle> on; std::unique_ptr<ParamKnob> freq, gain, q; };
        MysteryBoxProcessor& proc;
        EqDisplay eq;
        StereoMeter outMeter, inMeter;
        LoudnessReadout loudness;
        Goniometer gonio;
        ParamToggle eqOn { "EQ" }, limiter { "LIMITER", colours::amber };
        ParamKnob ceiling { "CEILING", colours::amber }, master { "MASTER", colours::cyan };
        std::array<Band, params::kNumEqBands> bands;
    };
} // namespace

//==============================================================================
FxPage::FxPage (MysteryBoxProcessor& p) : proc (p)
{
    rack = std::make_unique<RackView> (p);
    master = std::make_unique<MasterView> (p);
    addAndMakeVisible (*rack);
    addChildComponent (*master);
    for (auto* t : { &rackTab, &masterTab })
    {
        t->setRadioGroupId (5001);
        t->setClickingTogglesState (true);
        addAndMakeVisible (t);
    }
    rackTab.onClick = [this] { showMaster (false); };
    masterTab.onClick = [this] { showMaster (true); };
    rackTab.setToggleState (true, juce::dontSendNotification);
}

void FxPage::showMaster (bool m)
{
    (m ? masterTab : rackTab).setToggleState (true, juce::dontSendNotification);
    rack->setVisible (! m);
    master->setVisible (m);
    master->visibilityChanged();
}

void FxPage::paint (juce::Graphics&) {}

void FxPage::resized()
{
    rack->setBounds (0, 0, getWidth(), getHeight());
    master->setBounds (0, 0, getWidth(), getHeight());
    rackTab.setBounds (getWidth() - 200, getHeight() - 24, 90, 22);
    masterTab.setBounds (getWidth() - 106, getHeight() - 24, 90, 22);
}

} // namespace mbx::ui
