#pragma once

#include "../games/Game.h"
#include "Displays.h"
#include "Widgets.h"

namespace mbx::ui
{
/** XD tab: 25 playable games that drive the synth with notes and Mod Out signals. */
class XdPage final : public juce::Component, private juce::Timer
{
public:
    explicit XdPage (MysteryBoxProcessor& p);
    ~XdPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;

private:
    void timerCallback() override;
    void selectGame (int index);
    void recordScore();
    juce::StringArray highScores (const juce::String& game) const;

    class Canvas;
    MysteryBoxProcessor& proc;
    games::GameHost host;
    std::unique_ptr<games::Game> game;
    std::unique_ptr<Canvas> canvas;
    std::vector<std::unique_ptr<juce::TextButton>> gameButtons;
    juce::TextButton playBtn { "PLAY" }, autoBtn { "PERFORMANCE MODE" }, resetBtn { "RESTART" };
    juce::ComboBox scale, key, octave, target;
    juce::Slider quantize;
    int current = 0, lastSixteenth = -1;
    bool playing = false, wasOver = false;
    double lastMs = 0.0, beat = 0.0;
    std::vector<int> downKeys;
};

} // namespace mbx::ui
