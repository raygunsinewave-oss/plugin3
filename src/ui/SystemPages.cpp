#include "SystemPages.h"

namespace mbx::ui
{
namespace
{
    /** Shows a response curve y = x^(2^(-curve*k)) driven by a parameter. */
    class CurveView final : public juce::Component, private juce::Timer
    {
    public:
        CurveView (juce::AudioProcessorValueTreeState& s, juce::String paramId, juce::String label, bool bipolar)
            : state (s), id (std::move (paramId)), title (std::move (label)), bip (bipolar) { startTimerHz (10); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawScreen (g, r, colours::magenta);
            auto a = r.reduced (10.0f, 16.0f);
            const float c = rawParam (state, id);
            juce::Path p;
            for (int i = 0; i <= 64; ++i)
            {
                const float x = bip ? (float) i / 32.0f - 1.0f : (float) i / 64.0f;
                const float y = bip ? dsp::applyModCurve (x, c) : std::pow (x, std::exp2 (-c * 1.5f));
                const float px = a.getX() + a.getWidth() * (float) i / 64.0f;
                const float py = bip ? a.getCentreY() - y * a.getHeight() * 0.5f : a.getBottom() - y * a.getHeight();
                i == 0 ? p.startNewSubPath (px, py) : p.lineTo (px, py);
            }
            g.setColour (colours::magenta.withAlpha (0.2f));
            g.drawLine (a.getX(), bip ? a.getCentreY() + a.getHeight() * 0.5f : a.getBottom(), a.getRight(), a.getY());
            strokeGlow (g, p, colours::magenta, 1.6f);
            g.setFont (fonts::mono (9.5f));
            g.setColour (colours::textDim);
            g.drawText (title, r.reduced (8.0f, 3.0f), juce::Justification::topLeft);
        }

    private:
        void timerCallback() override { repaint(); }
        juce::AudioProcessorValueTreeState& state;
        juce::String id, title;
        bool bip;
    };

    //==========================================================================
    class ParamRow final : public juce::Component
    {
    public:
        ParamRow (MysteryBoxProcessor& p) : proc (p)
        {
            addAndMakeVisible (bar);
            learn.onClick = [this] {
                if (paramIndex < 0) return;
                auto& ml = proc.getMidiLearn();
                if (ml.ccForParam (paramIndex) >= 0) ml.clearParam (paramIndex);
                else ml.startLearn (paramIndex);
                repaint();
            };
            learn.getProperties().set ("accent", (juce::int64) colours::amber.getARGB());
            addAndMakeVisible (learn);
        }
        void setParam (int index, bool odd)
        {
            alt = odd;
            if (index == paramIndex) return;
            paramIndex = index;
            if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.getParameters()[index]))
                bar.bind (proc.getState(), prm->paramID);
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            if (alt) { g.setColour (colours::cyan.withAlpha (0.04f)); g.fillRect (getLocalBounds()); }
            if (paramIndex < 0) return;
            auto* prm = proc.getParameters()[paramIndex];
            g.setFont (fonts::mono (10.5f));
            g.setColour (colours::text);
            g.drawText (prm->getName (48), 6, 0, 210, getHeight(), juce::Justification::centredLeft);
            const int cc = proc.getMidiLearn().ccForParam (paramIndex);
            const bool learning = proc.getMidiLearn().learningParam() == paramIndex;
            g.setColour (learning ? colours::amber : cc >= 0 ? colours::lime : colours::textDim);
            g.drawText (learning ? "MOVE A CC..." : cc >= 0 ? "CC " + juce::String (cc) : "--", getWidth() - 170, 0, 90, getHeight(), juce::Justification::centredLeft);
            learn.setButtonText (cc >= 0 ? "CLEAR" : "LEARN");
        }
        void resized() override
        {
            bar.setBounds (220, 3, getWidth() - 400, getHeight() - 6);
            learn.setBounds (getWidth() - 74, 3, 68, getHeight() - 6);
        }

    private:
        MysteryBoxProcessor& proc;
        ParamBar bar { colours::cyan, false };
        juce::TextButton learn { "LEARN" };
        int paramIndex = -1;
        bool alt = false;
    };

    class BrowserModel final : public juce::ListBoxModel
    {
    public:
        explicit BrowserModel (MysteryBoxProcessor& p) : proc (p) { filter ({}, false); }
        void filter (const juce::String& text, bool mappedOnly)
        {
            rows.clear();
            const auto& ps = proc.getParameters();
            for (int i = 0; i < ps.size(); ++i)
            {
                if (mappedOnly && proc.getMidiLearn().ccForParam (i) < 0) continue;
                if (text.isNotEmpty() && ! ps[i]->getName (64).containsIgnoreCase (text)) continue;
                rows.push_back (i);
            }
        }
        int getNumRows() override { return (int) rows.size(); }
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
        juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override
        {
            auto* r = dynamic_cast<ParamRow*> (existing);
            if (row < 0 || row >= (int) rows.size()) { delete existing; return nullptr; }
            if (r == nullptr) { delete existing; r = new ParamRow (proc); }
            r->setParam (rows[(size_t) row], row % 2 == 1);
            return r;
        }

    private:
        MysteryBoxProcessor& proc;
        std::vector<int> rows;
    };
} // namespace

//==============================================================================
struct AdvancedPage::Impl : private juce::Timer
{
    explicit Impl (MysteryBoxProcessor& p, juce::Component& owner)
        : proc (p), model (p),
          velView (p.getState(), params::id::velCurve, "VELOCITY CURVE", false),
          keyView (p.getState(), params::id::keyCurve, "KEYTRACK CURVE", true)
    {
        auto& s = p.getState();
        drift.bind (s, params::id::analogDrift); vrand.bind (s, params::id::voiceRandom); smooth.bind (s, params::id::modSmooth);
        velCurve.bind (s, params::id::velCurve); keyCurve.bind (s, params::id::keyCurve); mpeRange.bind (s, params::id::mpeBendRange);
        steal.bind (s, params::id::stealMode); sat.bind (s, params::id::satModel); os.bind (s, params::id::oversampling);
        dc.bind (s, params::id::dcBlock); mpe.bind (s, params::id::mpeOn); poly.bind (s, params::id::polyphony);

        loadScl.onClick = [this] { pick ("*.scl", true); };
        loadKbm.onClick = [this] { pick ("*.kbm", false); };
        resetTuning.onClick = [this] { proc.getTuning().reset(); tuningStatus = "12-TET"; owner2->repaint(); };

        search.setTextToShowWhenEmpty ("SEARCH PARAMETERS...", colours::textDim);
        search.setFont (fonts::mono (12.0f));
        search.setColour (juce::TextEditor::backgroundColourId, colours::track);
        search.setColour (juce::TextEditor::textColourId, colours::cyan);
        search.setColour (juce::TextEditor::outlineColourId, colours::cyan.withAlpha (0.4f));
        search.onTextChange = [this] { refilter(); };
        mappedOnly.setClickingTogglesState (true);
        mappedOnly.onClick = [this] { refilter(); };
        clearMaps.onClick = [this] { proc.getMidiLearn().clearAll(); refilter(); };
        list.setModel (&model);
        list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.getViewport()->setScrollBarThickness (7);

        owner2 = &owner;
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &drift, &vrand, &smooth, &velCurve, &keyCurve, &mpeRange, &poly, &steal, &sat, &os, &dc, &mpe, &velView, &keyView,
                 &loadScl, &loadKbm, &resetTuning, &search, &mappedOnly, &clearMaps, &list })
            owner.addAndMakeVisible (c);
        tuningStatus = proc.getTuning().getName();
        startTimerHz (4);
    }

    void pick (const juce::String& pattern, bool scl)
    {
        chooser = std::make_unique<juce::FileChooser> (scl ? "Load Scala scale" : "Load keyboard mapping", juce::File(), pattern);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, scl] (const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            if (scl) sclText = f.loadFileAsString(); else kbmText = f.loadFileAsString();
            if (sclText.isEmpty()) sclText = proc.getTuning().getScl();
            const auto err = sclText.isNotEmpty() ? proc.getTuning().load (sclText, kbmText) : juce::String ("Load a .scl first");
            tuningStatus = err.isEmpty() ? proc.getTuning().getName() : "ERR " + err;
            owner2->repaint();
        });
    }

    void refilter()
    {
        model.filter (search.getText(), mappedOnly.getToggleState());
        list.updateContent();
        list.repaint();
    }

    void timerCallback() override { list.repaint(); }

    MysteryBoxProcessor& proc;
    juce::Component* owner2 = nullptr;
    BrowserModel model;
    juce::ListBox list;
    juce::TextEditor search;
    juce::TextButton mappedOnly { "MAPPED ONLY" }, clearMaps { "CLEAR ALL CC" }, loadScl { "LOAD .SCL" }, loadKbm { "LOAD .KBM" },
        resetTuning { "12-TET" };
    ParamKnob drift { "DRIFT", colours::amber }, vrand { "VOICE RND", colours::amber }, smooth { "MOD SMOOTH", colours::cyan },
        velCurve { "VEL CURVE", colours::magenta, true }, keyCurve { "KEY CURVE", colours::magenta, true }, mpeRange { "MPE BEND", colours::magenta },
        poly { "VOICES", colours::cyan };
    ParamChoice steal { "VOICE STEALING" }, sat { "FILTER SATURATION" }, os { "OVERSAMPLING" };
    ParamToggle dc { "DC BLOCK" }, mpe { "MPE", colours::magenta };
    CurveView velView, keyView;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String sclText, kbmText, tuningStatus;
};

AdvancedPage::AdvancedPage (MysteryBoxProcessor& p) : impl (std::make_unique<Impl> (p, *this)) {}
AdvancedPage::~AdvancedPage() = default;

void AdvancedPage::paint (juce::Graphics& g)
{
    drawTechPanel (g, { 16.0f, 0.0f, 620.0f, 420.0f }, "ENGINE", colours::amber, "SYS.ADV // VOICE + QUALITY");
    drawTechPanel (g, { 16.0f, 430.0f, 620.0f, 250.0f }, "MICROTUNING", colours::magenta, "SCALA .SCL / .KBM");
    drawTechPanel (g, { 648.0f, 0.0f, 616.0f, 680.0f }, "PARAMETERS + MIDI CC", colours::cyan, juce::String (impl->proc.getParameters().size()) + " AUTOMATABLE");
    g.setFont (fonts::mono (12.0f));
    g.setColour (impl->tuningStatus.startsWith ("ERR") ? colours::amber : colours::lime);
    g.drawText ("ACTIVE: " + impl->tuningStatus, 36, 520, 580, 18, juce::Justification::centredLeft);
    g.setFont (fonts::mono (10.0f));
    g.setColour (colours::textDim);
    g.drawFittedText ("Load a Scala scale (.scl) and optionally a keyboard mapping (.kbm). Without a .kbm the scale's 1/1 sits on "
                      "middle C and A4 stays 440 Hz. Tunings are saved with the project.\n\nMIDI CC: press LEARN on any row (or "
                      "right-click a knob > MIDI Learn), then move a controller.",
                      juce::Rectangle<int> (36, 548, 580, 110), juce::Justification::topLeft, 8);
}

void AdvancedPage::resized()
{
    auto& i = *impl;
    i.steal.setBounds (36, 34, 170, 34);
    i.sat.setBounds (216, 34, 170, 34);
    i.os.setBounds (396, 34, 110, 34);
    i.dc.setBounds (520, 46, 90, 22);
    ParamKnob* k[] = { &i.drift, &i.vrand, &i.smooth, &i.poly, &i.mpeRange };
    for (int n = 0; n < 5; ++n)
        k[n]->setBounds (36 + n * 86, 84, 84, 96);
    i.mpe.setBounds (36 + 5 * 86 + 6, 122, 70, 22);
    i.velCurve.setBounds (36, 196, 84, 96);
    i.velView.setBounds (126, 196, 180, 200);
    i.keyCurve.setBounds (326, 196, 84, 96);
    i.keyView.setBounds (416, 196, 200, 200);
    i.loadScl.setBounds (36, 470, 110, 26);
    i.loadKbm.setBounds (154, 470, 110, 26);
    i.resetTuning.setBounds (272, 470, 90, 26);
    i.search.setBounds (668, 32, 300, 26);
    i.mappedOnly.setBounds (980, 32, 120, 26);
    i.clearMaps.setBounds (1110, 32, 130, 26);
    i.list.setBounds (664, 68, 584, 600);
}

//==============================================================================
struct SettingsPage::Impl
{
    explicit Impl (MysteryBoxProcessor& p, SettingsPage& owner) : proc (p), page (owner)
    {
        auto& gs = GlobalSettings::get();
        theme.addItemList (themeNames(), 1);
        theme.setSelectedItemIndex (gs.theme(), juce::dontSendNotification);
        theme.onChange = [this] {
            GlobalSettings::get().set ("theme", theme.getSelectedItemIndex());
            applyTheme (theme.getSelectedItemIndex());
            if (page.onNeedsRebuild) juce::MessageManager::callAsync ([cb = page.onNeedsRebuild] { cb(); });
        };
        const int scales[] = { 75, 100, 125, 150, 175, 200 };
        for (int k = 0; k < 6; ++k) scale.addItem (juce::String (scales[k]) + "%", scales[k]);
        scale.setSelectedId (gs.uiScalePercent(), juce::dontSendNotification);
        scale.onChange = [this] {
            GlobalSettings::get().set ("uiScale", scale.getSelectedId());
            if (page.onScaleChanged) page.onScaleChanged (scale.getSelectedId());
        };
        animation.addItemList ({ "Full", "Reduced", "Eco / Low GPU" }, 1);
        animation.setSelectedItemIndex (gs.animation(), juce::dontSendNotification);
        animation.onChange = [this] {
            GlobalSettings::get().set ("animation", animation.getSelectedItemIndex());
            if (page.onNeedsRebuild) juce::MessageManager::callAsync ([cb = page.onNeedsRebuild] { cb(); });
        };
        defaultOs.addItemList (params::oversamplingNames(), 1);
        defaultOs.setSelectedItemIndex (gs.defaultOversampling(), juce::dontSendNotification);
        defaultOs.onChange = [this] { GlobalSettings::get().set ("defaultOversampling", defaultOs.getSelectedItemIndex()); };

        for (auto* t : { &tooltips, &gpu })
            t->setClickingTogglesState (true);
        tooltips.setToggleState (gs.tooltips(), juce::dontSendNotification);
        gpu.setToggleState (gs.openGL(), juce::dontSendNotification);
        tooltips.onClick = [this] {
            GlobalSettings::get().set ("tooltips", tooltips.getToggleState());
            if (page.onNeedsRebuild) juce::MessageManager::callAsync ([cb = page.onNeedsRebuild] { cb(); });
        };
        gpu.onClick = [this] {
            GlobalSettings::get().set ("openGL", gpu.getToggleState());
            if (page.onNeedsRebuild) juce::MessageManager::callAsync ([cb = page.onNeedsRebuild] { cb(); });
        };
        folder.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser> ("Default preset folder", GlobalSettings::get().presetFolder());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
                if (fc.getResult().isDirectory())
                {
                    GlobalSettings::get().set ("presetFolder", fc.getResult().getFullPathName());
                    page.repaint();
                }
            });
        };
        updates.onClick = [] { juce::URL ("https://github.com/raygunsinewave-oss/plugin3/releases").launchInDefaultBrowser(); };
        clearMidi.onClick = [this] { proc.getMidiLearn().clearAll(); };
        for (juce::ComboBox* c : { &theme, &scale, &animation, &defaultOs })
            page.addAndMakeVisible (c);
        for (juce::Button* b : std::initializer_list<juce::Button*> { &tooltips, &gpu, &folder, &updates, &clearMidi })
            page.addAndMakeVisible (b);
    }

    MysteryBoxProcessor& proc;
    SettingsPage& page;
    juce::ComboBox theme, scale, animation, defaultOs;
    juce::TextButton tooltips { "TOOLTIPS" }, gpu { "GPU (OPENGL)" }, folder { "CHOOSE..." }, updates { "CHECK FOR UPDATES" },
        clearMidi { "CLEAR MIDI LEARN" };
    std::unique_ptr<juce::FileChooser> chooser;
};

SettingsPage::SettingsPage (MysteryBoxProcessor& p) : impl (std::make_unique<Impl> (p, *this)) {}
SettingsPage::~SettingsPage() = default;

void SettingsPage::paint (juce::Graphics& g)
{
    drawTechPanel (g, { 16.0f, 0.0f, 620.0f, 680.0f }, "SETTINGS", colours::cyan, "SAVED PER USER");
    drawTechPanel (g, { 648.0f, 0.0f, 616.0f, 330.0f }, "SHORTCUTS", colours::magenta, "KEYBOARD + MOUSE");
    drawTechPanel (g, { 648.0f, 340.0f, 616.0f, 340.0f }, "ABOUT", colours::lime, "v" MYSTERYBOX_VERSION);

    g.setFont (fonts::display (10.5f));
    g.setColour (colours::textDim);
    const char* labels[] = { "THEME", "UI SCALE", "ANIMATION", "DEFAULT OVERSAMPLING (NEW INSTANCES)", "INTERFACE", "DEFAULT PRESET FOLDER", "MIDI" };
    const int ys[] = { 36, 96, 156, 216, 276, 336, 416 };
    for (int k = 0; k < 7; ++k)
        g.drawText (labels[k], 36, ys[k], 560, 14, juce::Justification::centredLeft);
    g.setFont (fonts::mono (11.0f));
    g.setColour (colours::cyan);
    g.drawText (GlobalSettings::get().presetFolder().getFullPathName(), 150, 356, 470, 26, juce::Justification::centredLeft, true);
    g.setColour (colours::textDim);
    g.drawFittedText ("Theme, animation, tooltips and GPU changes rebuild the interface instantly; audio is never interrupted.",
                      juce::Rectangle<int> (36, 600, 580, 50), juce::Justification::topLeft, 3);

    juce::StringArray shortcuts {
        "Double-click knob ........ reset to default",
        "Ctrl / Cmd + drag ........ fine adjustment",
        "Alt + click .............. reset to default",
        "Right-click knob ......... reset, randomize, MIDI learn, host menu",
        "Drag a MOD chip onto a knob  add a mod-matrix slot",
        "Tab / Shift+Tab .......... move keyboard focus",
        "Arrow keys ............... adjust focused control (Shift = fine)",
        "A W S E D F T G Y H U J K  play notes (computer keyboard)",
        "Z / X .................... octave down / up",
        "Ctrl/Cmd + Z / Y ......... undo / redo",
        "Click wavetable display .. 3D / 2D / spectrum" };
    g.setFont (fonts::mono (11.0f));
    for (int k = 0; k < shortcuts.size(); ++k)
    {
        g.setColour (k % 2 == 0 ? colours::text : colours::text.withAlpha (0.8f));
        g.drawText (shortcuts[k], 668, 34 + k * 24, 580, 20, juce::Justification::centredLeft);
    }

    juce::StringArray about {
        "MYSTERY BOX by Raygun  //  6-oscillator synthesizer",
        "Version " MYSTERYBOX_VERSION "  //  " + juce::String (impl->proc.getParameters().size()) + " parameters",
        "",
        "Built with JUCE 8 (commercial use requires a JUCE licence",
        "or AGPLv3 compliance).",
        "VST is a registered trademark of Steinberg Media Technologies GmbH.",
        "Fonts: Orbitron & JetBrains Mono, SIL Open Font License 1.1.",
        "Factory wavetables, presets and games are original content." };
    for (int k = 0; k < about.size(); ++k)
    {
        g.setColour (k == 0 ? colours::lime : colours::textDim);
        g.drawText (about[k], 668, 374 + k * 22, 580, 20, juce::Justification::centredLeft);
    }
}

void SettingsPage::resized()
{
    auto& i = *impl;
    i.theme.setBounds (36, 52, 240, 28);
    i.scale.setBounds (36, 112, 140, 28);
    i.animation.setBounds (36, 172, 200, 28);
    i.defaultOs.setBounds (36, 232, 140, 28);
    i.tooltips.setBounds (36, 292, 120, 26);
    i.gpu.setBounds (164, 292, 140, 26);
    i.folder.setBounds (36, 356, 104, 26);
    i.clearMidi.setBounds (36, 432, 170, 26);
    i.updates.setBounds (668, 620, 200, 28);
}

} // namespace mbx::ui
