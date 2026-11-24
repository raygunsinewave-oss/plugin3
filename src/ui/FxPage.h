#pragma once

#include "Displays.h"
#include "Meters.h"
#include "Widgets.h"

namespace mbx::ui
{
class FxPage final : public juce::Component, public juce::DragAndDropContainer
{
public:
    explicit FxPage (MysteryBoxProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void showMaster (bool master);

private:
    MysteryBoxProcessor& proc;
    juce::TextButton rackTab { "RACK" }, masterTab { "MASTER" };
    std::unique_ptr<juce::Component> rack, master;
};

} // namespace mbx::ui
