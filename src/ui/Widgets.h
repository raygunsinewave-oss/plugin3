// Parameter-bound controls. Each can be re-bound to a different parameter at runtime
// (used by the oscillator panel when switching OSC 1..6).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "NeonLookAndFeel.h"
#include "../state/MidiLearn.h"

namespace mbx::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

/** Rotary slider with live-value tooltip, double-click reset, Ctrl/Cmd fine drag
    and a right-click menu (reset / randomise / host automation menu). */
class NeonSlider final : public juce::Slider
{
public:
    NeonSlider() : juce::Slider (RotaryHorizontalVerticalDrag, NoTextBox)
    {
        setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        // Ctrl (Win) / Cmd (mac) switches to slow velocity-based dragging for fine adjustment
        setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::commandModifier);
        setMouseDragSensitivity (220);
        setPopupMenuEnabled (false);
        setWantsKeyboardFocus (true);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        const int code = k.getKeyCode();
        const bool up = code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey;
        const bool down = code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey;
        if (! up && ! down)
            return juce::Slider::keyPressed (k);
        const double range = getMaximum() - getMinimum();
        double step = std::max (getInterval(), range * (k.getModifiers().isShiftDown() ? 0.001 : 0.01));
        if (getInterval() >= 1.0) step = getInterval(); // integer / choice-like parameters move one step
        setValue (getValue() + (up ? step : -step), juce::sendNotificationSync);
        return true;
    }

    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    void setParameter (juce::RangedAudioParameter* p) { param = p; }

    /** Set by owners that know which mod destination this control maps to. */
    std::function<void()> onAssignModulation;

    juce::String getTooltip() override
    {
        return param != nullptr ? param->getName (64) + ": " + param->getCurrentValueAsText() : juce::String();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && param != nullptr)
        {
            showContextMenu();
            return;
        }
        juce::Slider::mouseDown (e);
    }

private:
    void showContextMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader (param->getName (64));
        m.addItem ("Reset to default", [this] { setParam (param->getDefaultValue()); });
        m.addItem ("Randomize", [this] { setParam (juce::Random::getSystemRandom().nextFloat()); });
        if (auto* host = findLearnHost())
        {
            auto& ml = host->getMidiLearn();
            const int idx = param->getParameterIndex();
            const int cc = ml.ccForParam (idx);
            m.addItem (cc >= 0 ? "MIDI Learn (mapped to CC " + juce::String (cc) + ")" : "MIDI Learn", [&ml, idx] { ml.startLearn (idx); });
            if (cc >= 0)
                m.addItem ("Clear MIDI CC", [&ml, idx] { ml.clearParam (idx); });
        }
        if (onAssignModulation != nullptr)
            m.addItem ("Add modulation slot...", [this] { onAssignModulation(); });

        if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
            if (auto* ctx = editor->getHostContext())
                if (auto hostMenu = ctx->getContextMenuForParameter (param))
                {
                    m.addSeparator();
                    m.addSubMenu ("Host / Automation", hostMenu->getEquivalentPopupMenu());
                }

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    MidiLearnHost* findLearnHost()
    {
        if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
            return dynamic_cast<MidiLearnHost*> (editor->getAudioProcessor());
        return nullptr;
    }

    void setParam (float normalised)
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (normalised);
        param->endChangeGesture();
    }

    juce::RangedAudioParameter* param = nullptr;
};

class ParamKnob final : public juce::Component, public juce::DragAndDropTarget
{
public:
    /** Called with a ModSource index when a modulation chip is dropped on this knob. */
    std::function<void (int source)> onModulationDropped;

    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return modDest > 0 && d.description.toString().startsWith ("modsrc:");
    }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHover = false;
        repaint();
        if (onModulationDropped)
            onModulationDropped (d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue());
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (dropHover)
            drawCornerBrackets (g, getLocalBounds().toFloat().reduced (1.0f), colours::lime, 10.0f, 2.0f);
        if (slider.hasKeyboardFocus (false))
            drawCornerBrackets (g, getLocalBounds().toFloat().reduced (1.0f), colours::text, 6.0f, 1.0f);
    }

    /** Mod-matrix destination this knob represents (0 = none) and how one unit of
        modulation maps onto the knob's travel (used to draw the depth arc). */
    void setModTarget (int destination, float travelPerUnit) { modDest = destination; modTravel = travelPerUnit; }
    int getModDest() const noexcept { return modDest; }
    void setLabel (const juce::String& text) { label = text; repaint(); }
    float getModTravel() const noexcept { return modTravel; }

    void setModulationDisplay (float depth, float live)
    {
        auto& props = slider.getProperties();
        if (std::abs ((float) props.getWithDefault ("modDepth", 0.0f) - depth) > 1.0e-4f
            || std::abs ((float) props.getWithDefault ("modLive", 0.0f) - live) > 1.0e-3f)
        {
            props.set ("modDepth", depth);
            props.set ("modLive", live);
            slider.repaint();
        }
    }

    explicit ParamKnob (juce::String labelText, juce::Colour accent = colours::cyan, bool bipolar = false)
        : label (std::move (labelText))
    {
        slider.getProperties().set ("accent", (juce::int64) accent.getARGB());
        slider.getProperties().set ("bipolar", bipolar);
        addAndMakeVisible (slider);
        slider.onValueChange = [this] { refreshValueText(); };
    }

    void bind (APVTS& state, const juce::String& paramId)
    {
        attachment.reset();
        auto* p = state.getParameter (paramId);
        jassert (p != nullptr);
        slider.setParameter (p);
        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        param = p;
        refreshValueText();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::textDim);
        g.setFont (fonts::display (9.5f));
        g.drawText (label, r.removeFromTop (12.0f), juce::Justification::centred);
        g.setColour (slider.isMouseOverOrDragging() ? colours::cyan : colours::text);
        g.setFont (fonts::mono (10.5f));
        g.drawText (valueText, r.removeFromBottom (12.0f), juce::Justification::centred);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds().withTrimmedTop (12).withTrimmedBottom (12));
    }

    NeonSlider slider;

private:
    void refreshValueText()
    {
        if (param != nullptr)
        {
            valueText = param->getCurrentValueAsText();
            repaint();
        }
    }

    juce::String label, valueText;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
    int modDest = 0;
    float modTravel = 1.0f;
    bool dropHover = false;
};

/** Draggable modulation-source chip: drop it on any modulatable knob to create a matrix slot. */
class ModSourceChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModSourceChip (int sourceIndex, juce::String name, juce::Colour c) : source (sourceIndex), label (std::move (name)), accent (c)
    {
        setTooltip ("Drag onto a knob to modulate it with " + label);
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (accent.withAlpha (isMouseOver() ? 0.35f : 0.18f));
        g.fillPath (chamferedRect (r, 5.0f));
        g.setColour (accent);
        g.strokePath (chamferedRect (r, 5.0f), juce::PathStrokeType (1.0f));
        drawLed (g, { r.getX() + 9.0f, r.getCentreY() }, 3.0f, accent, true);
        g.setFont (fonts::mono (10.0f));
        g.drawText ("DRAG " + label, r.withTrimmedLeft (16.0f), juce::Justification::centredLeft);
    }
    void mouseDrag (const juce::MouseEvent&) override
    {
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! c->isDragAndDropActive())
                c->startDragging ("modsrc:" + juce::String (source), this);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    int source;
    juce::String label;
    juce::Colour accent;
};

/** Thin horizontal light-bar slider (mod-matrix amounts etc.). */
class ParamBar final : public juce::Component
{
public:
    explicit ParamBar (juce::Colour accent = colours::magenta, bool bipolar = true, bool showValue = true) : withText (showValue)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.getProperties().set ("accent", (juce::int64) accent.getARGB());
        slider.getProperties().set ("bipolar", bipolar);
        slider.setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::commandModifier);
        addAndMakeVisible (slider);
        slider.onValueChange = [this] { repaint(); };
    }

    void bind (APVTS& state, const juce::String& paramId)
    {
        attachment.reset();
        param = state.getParameter (paramId);
        slider.setParameter (param);
        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
        if (param != nullptr)
            slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }

    void paint (juce::Graphics& g) override
    {
        if (param == nullptr || ! withText) return;
        g.setFont (fonts::mono (10.5f));
        g.setColour (colours::text);
        g.drawText (param->getCurrentValueAsText(), getLocalBounds().removeFromRight (44), juce::Justification::centredRight);
    }

    void resized() override { slider.setBounds (getLocalBounds().withTrimmedRight (withText ? 46 : 0)); }

    NeonSlider slider;

private:
    bool withText = true;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

class ParamChoice final : public juce::Component
{
public:
    explicit ParamChoice (juce::String labelText) : label (std::move (labelText)) { addAndMakeVisible (box); }

    void bind (APVTS& state, const juce::String& paramId)
    {
        attachment.reset();
        box.clear (juce::dontSendNotification);
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            box.addItemList (p->choices, 1);
        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, box);
    }

    void paint (juce::Graphics& g) override
    {
        if (label.isEmpty()) return;
        g.setColour (colours::textDim);
        g.setFont (fonts::display (9.5f));
        g.drawText (label, getLocalBounds().removeFromTop (12), juce::Justification::centredLeft);
    }

    void resized() override { box.setBounds (getLocalBounds().withTrimmedTop (label.isEmpty() ? 0 : 13)); }

    juce::ComboBox box;

private:
    juce::String label;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

class ParamToggle final : public juce::TextButton
{
public:
    explicit ParamToggle (const juce::String& buttonText, juce::Colour accent = colours::lime) : juce::TextButton (buttonText)
    {
        setClickingTogglesState (true);
        getProperties().set ("accent", (juce::int64) accent.getARGB());
    }

    void bind (APVTS& state, const juce::String& paramId)
    {
        attachment.reset();
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
    }

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

} // namespace mbx::ui
