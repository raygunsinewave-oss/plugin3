// Presets: JSON files (.mbxpreset) holding only non-default parameter values plus assets
// (shapes, sequencer pattern, macro names). Factory presets ship embedded as one JSON array.
// Message thread only.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Assets.h"

namespace mbx
{
class PresetManager
{
public:
    struct Entry
    {
        juce::String name, category, author, description, key;
        juce::StringArray tags;
        bool factory = false;
        int factoryIndex = -1;
        juce::File file;
    };

    static inline const juce::StringArray categories { "Bass", "Leads", "Pads", "Plucks & Keys", "Winds", "Ambient", "Drones", "FX",
                                                       "Arps & Sequences", "Misc" };
    static constexpr const char* kExtension = ".mbxpreset";

    PresetManager (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s, Assets& a);

    /** Rebuilds the list (factory + user folder). */
    void refresh();
    const std::vector<Entry>& getEntries() const noexcept { return entries; }

    bool load (const Entry& e);
    bool loadIndex (int index) { return juce::isPositiveAndBelow (index, (int) entries.size()) && load (entries[(size_t) index]); }
    /** Step through `order` (indices into getEntries()) from the current preset. */
    void step (const std::vector<int>& order, int delta);
    juce::String getCurrentName() const { return currentName; }
    int getCurrentIndex() const noexcept { return currentIndex; }
    void setCurrentName (const juce::String& n) { currentName = n; }

    /** Current state as a preset JSON object. */
    juce::var capture (const juce::String& name, const juce::String& category, const juce::StringArray& tags,
                       const juce::String& description, const juce::String& author) const;
    void apply (const juce::var& preset);

    juce::File saveUser (const juce::String& name, const juce::String& category, const juce::StringArray& tags, const juce::String& description);
    bool exportCurrent (const juce::File& file) const;
    bool importFile (const juce::File& file);

    // A/B compare
    void storeAB (int slot);
    void recallAB (int slot);
    bool hasAB (int slot) const { return ! ab[(size_t) slot].isVoid(); }
    int getActiveAB() const noexcept { return activeAB; }
    void toggleAB();

    // favourites / ratings / recents (stored per user)
    bool isFavourite (const Entry& e) const { return meta.getProperty ("fav", {}).getDynamicObject() != nullptr && (bool) meta["fav"][juce::Identifier (safeKey (e))]; }
    void setFavourite (const Entry& e, bool f);
    int getRating (const Entry& e) const;
    void setRating (const Entry& e, int stars);
    juce::StringArray getRecents() const;

    static juce::String safeKey (const Entry& e);

    /** The browser's current filtered ordering (used by the top-bar prev / next arrows). */
    std::vector<int> browserOrder;
    void stepBrowser (int delta)
    {
        if (browserOrder.empty())
            for (int i = 0; i < (int) entries.size(); ++i) browserOrder.push_back (i);
        step (browserOrder, delta);
    }
    /** Preset object for an entry (factory or user file). */
    juce::var getPresetData (const Entry& e) const;

private:
    void loadFactory();
    void scanUser();
    void saveMeta();
    void touchRecent (const Entry& e);

    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    Assets& assets;
    std::vector<Entry> entries;
    juce::var factoryData; // parsed embedded array
    juce::String currentName { "INIT" };
    int currentIndex = -1, activeAB = 0;
    std::array<juce::var, 2> ab;
    juce::var meta;
};

} // namespace mbx
