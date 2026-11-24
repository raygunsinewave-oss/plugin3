// ADVANCED and SETTINGS tabs.
#pragma once

#include "Displays.h"
#include "Widgets.h"

namespace mbx::ui
{
class AdvancedPage final : public juce::Component
{
public:
    explicit AdvancedPage (MysteryBoxProcessor& p);
    ~AdvancedPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class SettingsPage final : public juce::Component
{
public:
    explicit SettingsPage (MysteryBoxProcessor& p);
    ~SettingsPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called (asynchronously) when a change needs the editor to rebuild (theme, GPU, scale...). */
    std::function<void()> onNeedsRebuild;
    std::function<void (int percent)> onScaleChanged;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace mbx::ui
