#pragma once

#include "Displays.h"
#include "Widgets.h"

namespace mbx::ui
{
class PresetsPage final : public juce::Component, private juce::Timer
{
public:
    explicit PresetsPage (MysteryBoxProcessor& p);
    ~PresetsPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onPresetLoaded;

private:
    void timerCallback() override;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace mbx::ui
