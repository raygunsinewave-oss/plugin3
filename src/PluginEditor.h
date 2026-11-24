#pragma once

#include "PluginProcessor.h"
#include "ui/FxPage.h"
#include "ui/Sections.h"

namespace mbx
{
/** SYNTH tab: oscillators, mixer/voice, two filters, and the modulation section. */
class SynthPage final : public juce::Component, private juce::Timer
{
public:
    explicit SynthPage (MysteryBoxProcessor& p);
    void resized() override;

private:
    void timerCallback() override; // modulation rings on every knob

    MysteryBoxProcessor& proc;
    ui::OscSection osc;
    ui::MixVoiceSection mixVoice;
    ui::FilterSection filter1, filter2;
    ui::ModSection mod;
    std::vector<ui::ParamKnob*> modKnobs;
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

/** Top bar strip with animated sweep, mini scope and output meter. */
class TopBar final : public juce::Component, private juce::Timer
{
public:
    explicit TopBar (MysteryBoxProcessor& p);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    std::array<juce::TextButton, 6> tabs;
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    std::function<void (int)> onTab;

private:
    void timerCallback() override;
    MysteryBoxProcessor& proc;
    ui::ScopeDisplay miniScope;
    float sweep = 0.0f, meter = 0.0f, meterPeak = 0.0f, cpu = 0.0f;
    int peakHold = 0;
};

/** Bottom HUD status bar. */
class StatusBar final : public juce::Component, private juce::Timer
{
public:
    explicit StatusBar (MysteryBoxProcessor& p);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    MysteryBoxProcessor& proc;
    int ticks = 0;
    juce::String hexStream;
    double startMs = juce::Time::getMillisecondCounterHiRes();
};

/** Fixed 1280x800 design surface, scaled as a whole (75%..200%) so the UI stays vector-sharp. */
class MysteryBoxEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MysteryBoxEditor (MysteryBoxProcessor&);
    ~MysteryBoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int kBaseWidth = 1280, kBaseHeight = 800;
    static constexpr int kTopBarHeight = 56, kStatusHeight = 22;

private:
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

    TopBar topBar;
    StatusBar statusBar;
    SynthPage synthPage;
    ui::FxPage fxPage;
    ui::StereoMeter slimMeter;
    PlannedPage presetsPage, advancedPage, xdPage, settingsPage;
    std::array<juce::Component*, 6> pages {};
    juce::Image background;
    int currentTab = 0;
};

} // namespace mbx
