// Per-user settings shared by every instance (stored in the user's app-data folder),
// e.g. ~/Library/Application Support/Raygun/MysteryBox.settings on macOS.
#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace mbx
{
class GlobalSettings
{
public:
    enum Animation { Full = 0, Reduced, Eco };

    static GlobalSettings& get()
    {
        static GlobalSettings instance;
        return instance;
    }

    int theme() const               { return props->getIntValue ("theme", 0); }
    int uiScalePercent() const      { return props->getIntValue ("uiScale", 100); }
    int animation() const           { return props->getIntValue ("animation", Full); }
    bool tooltips() const           { return props->getBoolValue ("tooltips", true); }
    bool openGL() const
    {
       #if JUCE_LINUX
        return props->getBoolValue ("openGL", false);
       #else
        return props->getBoolValue ("openGL", true);
       #endif
    }
    int defaultOversampling() const { return props->getIntValue ("defaultOversampling", 1); }
    juce::File presetFolder() const
    {
        const auto s = props->getValue ("presetFolder");
        return s.isNotEmpty() ? juce::File (s)
                              : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Mystery Box").getChildFile ("Presets");
    }
    juce::String getString (const juce::String& key, const juce::String& def = {}) const { return props->getValue (key, def); }

    void set (const juce::String& key, const juce::var& v)
    {
        props->setValue (key, v);
        props->saveIfNeeded();
    }

    /** Frames per second for animated components at the current animation level. */
    int fps (int full) const
    {
        switch (animation())
        {
            case Reduced: return std::max (5, full / 2);
            case Eco:     return std::max (2, full / 6);
            default:      return full;
        }
    }

private:
    GlobalSettings()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "MysteryBox";
        o.folderName = "Raygun";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        props = std::make_unique<juce::PropertiesFile> (o);
    }
    std::unique_ptr<juce::PropertiesFile> props;
};

} // namespace mbx
