#pragma once

#include "PluginProcessor.h"
#include "ui/Displays.h"
#include "ui/Widgets.h"

namespace mbx
{
/** SYNTH tab: oscillator strip, selected-oscillator editor, amp envelope, master + scope. */
class SynthPage final : public juce::Component
{
public:
    explicit SynthPage (MysteryBoxProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void selectOscillator (int index);

    MysteryBoxProcessor& proc;
    juce::AudioProcessorValueTreeState& state;

    std::array<juce::TextButton, params::kNumOscs> oscTabs;
    std::array<std::unique_ptr<ui::ParamToggle>, params::kNumOscs> oscPower;
    int selectedOsc = 0;

    ui::WaveDisplay waveDisplay;
    ui::ParamChoice type { "TYPE" }, shape { "SHAPE" }, table { "TABLE" }, noise { "NOISE" }, octave { "OCTAVE" };
    ui::ParamKnob coarse { "COARSE", ui::colours::cyan, true }, fine { "FINE", ui::colours::cyan, true },
        level { "LEVEL" }, pan { "PAN", ui::colours::cyan, true }, phase { "PHASE" }, phaseRand { "RAND" },
        unison { "UNISON", ui::colours::magenta }, detune { "DETUNE", ui::colours::magenta },
        blend { "BLEND", ui::colours::magenta }, spread { "SPREAD", ui::colours::magenta },
        wtPos { "WT POS", ui::colours::lime }, pw { "PW", ui::colours::lime };

    ui::EnvelopeDisplay envDisplay;
    ui::ParamKnob envDelay { "DELAY", ui::colours::magenta }, envAttack { "ATTACK", ui::colours::magenta },
        envHold { "HOLD", ui::colours::magenta }, envDecay { "DECAY", ui::colours::magenta },
        envSustain { "SUSTAIN", ui::colours::magenta }, envRelease { "RELEASE", ui::colours::magenta },
        envACurve { "A CURVE", ui::colours::magenta, true }, envDCurve { "D CURVE", ui::colours::magenta, true },
        envRCurve { "R CURVE", ui::colours::magenta, true }, envVel { "VELOCITY", ui::colours::magenta },
        envPunch { "PUNCH", ui::colours::amber };

    ui::ScopeDisplay scope;
    ui::ParamKnob master { "MASTER" }, poly { "VOICES" }, bend { "BEND" };
    ui::ParamChoice oversampling { "OVERSAMPLING" };

    juce::Rectangle<int> oscArea, envArea, masterArea, comingArea;
};

/** Placeholder for tabs whose features land in later build phases. */
class PlannedPage final : public juce::Component
{
public:
    PlannedPage (juce::String title, juce::String phase, juce::StringArray items);
    void paint (juce::Graphics&) override;

private:
    juce::String title, phase;
    juce::StringArray items;
};

/** Fixed 1280x800 design surface, scaled as a whole (75%..200%) so the UI stays vector-sharp. */
class MysteryBoxEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit MysteryBoxEditor (MysteryBoxProcessor&);
    ~MysteryBoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int kBaseWidth = 1280, kBaseHeight = 800;

private:
    void timerCallback() override;
    void showTab (int index);
    void rebuildBackground();

    class Content final : public juce::Component
    {
    public:
        explicit Content (MysteryBoxEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        MysteryBoxEditor& owner;
    };
    void paintContent (juce::Graphics&);

    MysteryBoxProcessor& proc;
    ui::NeonLookAndFeel lnf;
    Content content { *this };
    juce::TooltipWindow tooltips { this, 500 };

    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    std::array<juce::TextButton, 6> tabs;
    juce::Label presetLabel, cpuLabel;

    SynthPage synthPage;
    PlannedPage presetsPage, fxPage, advancedPage, xdPage, settingsPage;
    std::array<juce::Component*, 6> pages {};
    juce::Image background;
    int currentTab = 0;
};

} // namespace mbx
