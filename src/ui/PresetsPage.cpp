#include "PresetsPage.h"

namespace mbx::ui
{
namespace
{
    enum class View { All, Category, Favourites, Recent, User };

    /** One row of the preset list. */
    class PresetRow final : public juce::Component
    {
    public:
        PresetRow (PresetManager& pm, std::function<void (int)> onLoad, std::function<void (int)> onSelect)
            : presets (pm), load (std::move (onLoad)), select (std::move (onSelect)) {}
        void set (int entryIndex, bool isSelected, bool odd) { index = entryIndex; selected = isSelected; alt = odd; repaint(); }

        void paint (juce::Graphics& g) override
        {
            const auto& e = presets.getEntries()[(size_t) index];
            auto r = getLocalBounds().toFloat();
            if (selected) { g.setColour (colours::cyan.withAlpha (0.18f)); g.fillRect (r); g.setColour (colours::cyan); g.fillRect (r.removeFromLeft (3.0f)); }
            else if (alt) { g.setColour (colours::cyan.withAlpha (0.035f)); g.fillRect (r); }
            const bool fav = presets.isFavourite (e);
            g.setFont (fonts::mono (14.0f));
            g.setColour (fav ? colours::amber : colours::textDim.withAlpha (0.5f));
            g.drawText (fav ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x86")), 6, 0, 20, getHeight(), juce::Justification::centred);
            g.setFont (fonts::mono (12.0f));
            g.setColour (presets.getCurrentIndex() == index ? colours::lime : colours::text);
            g.drawText (e.name, 30, 0, 250, getHeight(), juce::Justification::centredLeft, true);
            g.setFont (fonts::mono (10.5f));
            g.setColour (colours::magenta);
            g.drawText (e.category.toUpperCase(), 286, 0, 120, getHeight(), juce::Justification::centredLeft, true);
            g.setColour (colours::textDim);
            g.drawText (e.tags.joinIntoString (" / "), 410, 0, getWidth() - 78 - 416, getHeight(), juce::Justification::centredLeft, true);
            const int rating = presets.getRating (e);
            for (int s = 0; s < 5; ++s)
            {
                g.setColour (s < rating ? colours::lime : colours::textDim.withAlpha (0.35f));
                g.fillRect (juce::Rectangle<float> ((float) (getWidth() - 74 + s * 13), r.getCentreY() - 3.0f, 9.0f, 6.0f));
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto& entry = presets.getEntries()[(size_t) index];
            if (e.x < 28) { presets.setFavourite (entry, ! presets.isFavourite (entry)); repaint(); return; }
            if (e.x > getWidth() - 78) { presets.setRating (entry, juce::jlimit (0, 5, (e.x - (getWidth() - 78)) / 13 + 1)); repaint(); return; }
            select (index);
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override { if (e.x >= 28 && e.x <= getWidth() - 78) load (index); }

    private:
        PresetManager& presets;
        std::function<void (int)> load, select;
        int index = 0;
        bool selected = false, alt = false;
    };

    class ListModel final : public juce::ListBoxModel
    {
    public:
        ListModel (PresetManager& pm, std::function<void (int)> onLoad, std::function<void (int)> onSelect)
            : presets (pm), load (std::move (onLoad)), select (std::move (onSelect)) {}
        std::vector<int> rows;
        int selectedEntry = -1;
        int getNumRows() override { return (int) rows.size(); }
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
        juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override
        {
            if (row < 0 || row >= (int) rows.size()) { delete existing; return nullptr; }
            auto* r = dynamic_cast<PresetRow*> (existing);
            if (r == nullptr) { delete existing; r = new PresetRow (presets, load, select); }
            r->set (rows[(size_t) row], rows[(size_t) row] == selectedEntry, row % 2 == 1);
            return r;
        }

    private:
        PresetManager& presets;
        std::function<void (int)> load, select;
    };
} // namespace

struct PresetsPage::Impl
{
    Impl (MysteryBoxProcessor& p, PresetsPage& o)
        : proc (p), pm (p.getPresets()), owner (o),
          model (pm, [this] (int i) { loadEntry (i); }, [this] (int i) { model.selectedEntry = i; list.updateContent(); list.repaint(); owner.repaint(); })
    {
        if (pm.getEntries().empty())
            pm.refresh();

        auto styleEditor = [] (juce::TextEditor& t, const juce::String& hint) {
            t.setTextToShowWhenEmpty (hint, colours::textDim);
            t.setFont (fonts::mono (12.0f));
            t.setColour (juce::TextEditor::backgroundColourId, colours::track);
            t.setColour (juce::TextEditor::textColourId, colours::cyan);
            t.setColour (juce::TextEditor::outlineColourId, colours::cyan.withAlpha (0.4f));
        };
        styleEditor (search, "SEARCH NAME, TAG, DESCRIPTION...");
        styleEditor (saveName, "NAME");
        styleEditor (saveTags, "TAGS (comma separated)");
        styleEditor (saveDesc, "DESCRIPTION");
        search.onTextChange = [this] { refilter(); };

        list.setModel (&model);
        list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.getViewport()->setScrollBarThickness (7);

        // category / view buttons
        auto addView = [this] (const juce::String& label, View v, const juce::String& cat) {
            auto b = std::make_unique<juce::TextButton> (label);
            b->setRadioGroupId (7001);
            b->setClickingTogglesState (true);
            b->onClick = [this, v, cat] { view = v; category = cat; refilter(); };
            owner.addAndMakeVisible (*b);
            viewButtons.push_back (std::move (b));
        };
        addView ("ALL PRESETS", View::All, {});
        for (const auto& c : PresetManager::categories)
            addView (c.toUpperCase(), View::Category, c);
        addView ("FAVOURITES", View::Favourites, {});
        addView ("RECENT", View::Recent, {});
        addView ("USER", View::User, {});
        viewButtons[0]->setToggleState (true, juce::dontSendNotification);

        // tag filters
        juce::StringArray tags;
        for (const auto& e : pm.getEntries())
            for (const auto& t : e.tags)
                tags.addIfNotAlreadyThere (t);
        tags.sort (true);
        for (const auto& t : tags)
        {
            auto b = std::make_unique<juce::TextButton> (t.toUpperCase());
            b->setClickingTogglesState (true);
            b->getProperties().set ("accent", (juce::int64) colours::magenta.getARGB());
            b->onClick = [this] { refilter(); };
            b->getProperties().set ("tag", t);
            owner.addAndMakeVisible (*b);
            tagButtons.push_back (std::move (b));
        }

        loadBtn.onClick = [this] { if (model.selectedEntry >= 0) loadEntry (model.selectedEntry); };
        previewBtn.onClick = [this] { if (model.selectedEntry >= 0) startPreview (model.selectedEntry); };
        randomBtn.onClick = [this] {
            if (model.rows.empty()) return;
            const int pick = model.rows[(size_t) juce::Random::getSystemRandom().nextInt ((int) model.rows.size())];
            model.selectedEntry = pick;
            loadEntry (pick);
        };
        saveBtn.onClick = [this] {
            const auto name = saveName.getText().trim().isNotEmpty() ? saveName.getText().trim() : pm.getCurrentName();
            pm.saveUser (name, saveCategory.getText().isNotEmpty() ? saveCategory.getText() : juce::String ("User"),
                         juce::StringArray::fromTokens (saveTags.getText(), ",", ""), saveDesc.getText());
            status = "SAVED " + name.toUpperCase();
            refilter();
        };
        exportBtn.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser> ("Export preset", GlobalSettings::get().presetFolder().getChildFile (pm.getCurrentName() + PresetManager::kExtension),
                                                           juce::String ("*") + PresetManager::kExtension);
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc) {
                if (fc.getResult() != juce::File())
                    status = pm.exportCurrent (fc.getResult().withFileExtension (PresetManager::kExtension)) ? "EXPORTED" : "EXPORT FAILED";
                owner.repaint();
            });
        };
        importBtn.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser> ("Import preset", juce::File(), juce::String ("*") + PresetManager::kExtension);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
                if (fc.getResult().existsAsFile())
                    status = pm.importFile (fc.getResult()) ? "IMPORTED" : "NOT A MYSTERY BOX PRESET";
                refilter();
            });
        };
        saveCategory.addItemList (PresetManager::categories, 1);
        saveCategory.addItem ("User", 100);
        saveCategory.setSelectedId (100, juce::dontSendNotification);

        for (juce::Component* c : std::initializer_list<juce::Component*> { &search, &list, &loadBtn, &previewBtn, &randomBtn, &saveBtn, &exportBtn,
                                                                             &importBtn, &saveName, &saveTags, &saveDesc, &saveCategory })
            owner.addAndMakeVisible (c);
        refilter();
    }

    void refilter()
    {
        const auto text = search.getText().trim();
        juce::StringArray activeTags;
        for (auto& b : tagButtons)
            if (b->getToggleState()) activeTags.add (b->getProperties()["tag"].toString());
        const auto recents = pm.getRecents();
        model.rows.clear();
        const auto& entries = pm.getEntries();
        auto accept = [&] (size_t i) {
            const auto& e = entries[i];
            if (view == View::Category && e.category != category) return false;
            if (view == View::Favourites && ! pm.isFavourite (e)) return false;
            if (view == View::User && e.factory) return false;
            for (auto& t : activeTags) if (! e.tags.contains (t)) return false;
            if (text.isNotEmpty() && ! (e.name.containsIgnoreCase (text) || e.tags.joinIntoString (" ").containsIgnoreCase (text)
                                        || e.description.containsIgnoreCase (text) || e.category.containsIgnoreCase (text)))
                return false;
            return true;
        };
        if (view == View::Recent)
        {
            for (const auto& key : recents)
                for (size_t i = 0; i < entries.size(); ++i)
                    if (entries[i].key == key && accept (i)) model.rows.push_back ((int) i);
        }
        else
            for (size_t i = 0; i < entries.size(); ++i)
                if (accept (i)) model.rows.push_back ((int) i);
        pm.browserOrder = model.rows;
        list.updateContent();
        list.repaint();
        owner.repaint();
    }

    void loadEntry (int i)
    {
        model.selectedEntry = i;
        pm.loadIndex (i);
        proc.getFxRack().syncUnits();
        status = "LOADED " + pm.getCurrentName().toUpperCase();
        list.repaint();
        owner.repaint();
        if (owner.onPresetLoaded) owner.onPresetLoaded();
    }

    void startPreview (int i)
    {
        loadEntry (i);
        const auto& cat = pm.getEntries()[(size_t) i].category;
        preview.clear();
        auto add = [this] (int ms, std::vector<int> notes) { preview.push_back ({ ms, std::move (notes) }); };
        if (cat == "Bass") { add (0, { 36 }); add (450, {}); add (500, { 43 }); add (950, {}); add (1000, { 41 }); add (1600, {}); }
        else if (cat == "Pads" || cat == "Ambient" || cat == "Drones") { add (0, { 48, 55, 60, 64 }); add (2600, {}); }
        else if (cat == "Arps & Sequences") { add (0, { 48, 52, 55 }); add (2400, {}); }
        else if (cat == "FX") { add (0, { 60 }); add (2500, {}); }
        else { add (0, { 60, 64, 67 }); add (700, {}); add (800, { 62 }); add (1300, {}); add (1400, { 67 }); add (2000, {}); }
        previewStart = juce::Time::getMillisecondCounter();
        previewIndex = 0;
        status = "PREVIEW " + pm.getCurrentName().toUpperCase();
    }

    void tickPreview()
    {
        if (previewIndex >= preview.size()) return;
        const auto now = (int) (juce::Time::getMillisecondCounter() - previewStart);
        while (previewIndex < preview.size() && preview[previewIndex].first <= now)
        {
            for (int n : held) proc.injectNote (n, 0.0f, false);
            held = preview[previewIndex].second;
            for (int n : held) proc.injectNote (n, 0.85f, true);
            ++previewIndex;
        }
    }

    MysteryBoxProcessor& proc;
    PresetManager& pm;
    PresetsPage& owner;
    ListModel model;
    juce::ListBox list;
    juce::TextEditor search, saveName, saveTags, saveDesc;
    juce::ComboBox saveCategory;
    juce::TextButton loadBtn { "LOAD" }, previewBtn { "PREVIEW" }, randomBtn { "RANDOM" }, saveBtn { "SAVE USER PRESET" },
        exportBtn { "EXPORT" }, importBtn { "IMPORT" };
    std::vector<std::unique_ptr<juce::TextButton>> viewButtons, tagButtons;
    std::unique_ptr<juce::FileChooser> chooser;
    View view = View::All;
    juce::String category, status;
    std::vector<std::pair<int, std::vector<int>>> preview;
    size_t previewIndex = 0;
    juce::uint32 previewStart = 0;
    std::vector<int> held;
};

PresetsPage::PresetsPage (MysteryBoxProcessor& p) : impl (std::make_unique<Impl> (p, *this)) { startTimerHz (50); }
PresetsPage::~PresetsPage()
{
    stopTimer();
    for (int n : impl->held) impl->proc.injectNote (n, 0.0f, false);
}

void PresetsPage::timerCallback() { impl->tickPreview(); }

void PresetsPage::paint (juce::Graphics& g)
{
    auto& i = *impl;
    drawTechPanel (g, { 16.0f, 0.0f, 300.0f, 680.0f }, "LIBRARY", colours::magenta, juce::String ((int) i.pm.getEntries().size()) + " PRESETS");
    drawTechPanel (g, { 328.0f, 0.0f, 640.0f, 680.0f }, "BROWSER", colours::cyan, juce::String ((int) i.model.rows.size()) + " MATCHES");
    drawTechPanel (g, { 980.0f, 0.0f, 284.0f, 680.0f }, "INFO", colours::lime, i.pm.getCurrentName().toUpperCase().substring (0, 22));

    g.setFont (fonts::display (9.5f));
    g.setColour (colours::textDim);
    g.drawText ("TAGS", 34, 404, 100, 12, juce::Justification::centredLeft);
    g.drawText ("SAVE AS", 998, 440, 200, 12, juce::Justification::centredLeft);

    auto info = juce::Rectangle<int> (998, 34, 250, 300);
    if (i.model.selectedEntry >= 0)
    {
        const auto& e = i.pm.getEntries()[(size_t) i.model.selectedEntry];
        g.setFont (fonts::display (14.0f));
        g.setColour (colours::lime);
        g.drawFittedText (e.name.toUpperCase(), info.removeFromTop (40), juce::Justification::topLeft, 2);
        g.setFont (fonts::mono (11.0f));
        g.setColour (colours::magenta);
        g.drawText (e.category.toUpperCase() + "  //  " + e.author, info.removeFromTop (18), juce::Justification::centredLeft);
        g.setColour (colours::text);
        g.drawFittedText (e.description, info.removeFromTop (110), juce::Justification::topLeft, 8);
        g.setColour (colours::cyan);
        g.drawFittedText ("TAGS: " + e.tags.joinIntoString (", "), info.removeFromTop (36), juce::Justification::topLeft, 2);
        const auto data = i.pm.getPresetData (e);
        juce::StringArray macros;
        for (int m = 1; m <= 4; ++m)
            if (const auto n = data["assets"][juce::Identifier ("macro" + juce::String (m) + "_name")].toString(); n.isNotEmpty())
                macros.add ("M" + juce::String (m) + " " + n);
        g.setColour (colours::textDim);
        g.drawFittedText (macros.joinIntoString ("   "), info.removeFromTop (36), juce::Justification::topLeft, 2);
    }
    else
    {
        g.setFont (fonts::mono (11.0f));
        g.setColour (colours::textDim);
        g.drawFittedText ("Select a preset. Double-click to load.\nClick the star to favourite, the bars to rate.", info, juce::Justification::topLeft, 4);
    }
    g.setFont (fonts::mono (10.5f));
    g.setColour (i.status.startsWith ("NOT") || i.status.contains ("FAILED") ? colours::amber : colours::lime);
    g.drawText (i.status, 998, 646, 250, 16, juce::Justification::centredLeft, true);
}

void PresetsPage::resized()
{
    auto& i = *impl;
    int y = 34;
    for (auto& b : i.viewButtons)
    {
        b->setBounds (34, y, 264, 22);
        y += 26;
    }
    int tx = 34, ty = 420;
    for (auto& b : i.tagButtons)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (fonts::display (9.0f), b->getButtonText()) + 22;
        if (tx + w > 300) { tx = 34; ty += 24; }
        b->setBounds (tx, ty, w, 20);
        tx += w + 4;
    }
    i.search.setBounds (346, 32, 604, 26);
    i.list.setBounds (342, 66, 614, 600);
    i.loadBtn.setBounds (998, 340, 120, 26);
    i.previewBtn.setBounds (1126, 340, 120, 26);
    i.randomBtn.setBounds (998, 372, 120, 26);
    i.importBtn.setBounds (1126, 372, 120, 26);
    i.exportBtn.setBounds (998, 404, 120, 26);
    i.saveName.setBounds (998, 456, 248, 24);
    i.saveCategory.setBounds (998, 486, 248, 24);
    i.saveTags.setBounds (998, 516, 248, 24);
    i.saveDesc.setBounds (998, 546, 248, 52);
    i.saveBtn.setBounds (998, 606, 248, 28);
}

} // namespace mbx::ui
