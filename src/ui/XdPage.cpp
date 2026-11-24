#include "XdPage.h"

namespace mbx::ui
{
class XdPage::Canvas final : public juce::Component
{
public:
    explicit Canvas (XdPage& o) : owner (o) { setWantsKeyboardFocus (true); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawScreen (g, r, colours::cyan);
        if (owner.game != nullptr)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (getLocalBounds().reduced (6));
            owner.game->draw (g, r.reduced (14.0f, 26.0f));
        }
        if (! owner.playing)
        {
            g.setColour (colours::base.withAlpha (0.6f));
            g.fillRect (r.reduced (6.0f));
            games::gameText (g, "PAUSED  //  PRESS PLAY OR CLICK", r, 20.0f, colours::lime);
        }
        g.setFont (fonts::mono (11.0f));
        g.setColour (colours::cyan);
        g.drawText ("XD." + juce::String (owner.current + 1).paddedLeft ('0', 2) + "  //  " + juce::String (owner.game ? owner.game->name() : "").toUpperCase()
                        + "     SCORE " + juce::String (owner.game ? owner.game->score : 0),
                    r.reduced (12.0f, 6.0f), juce::Justification::topLeft);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        if (! owner.playing) { owner.playBtn.triggerClick(); return; }
        if (owner.game) owner.game->click (norm (e.position), e.mods.isPopupMenu());
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (owner.game && owner.playing) owner.game->drag (norm (e.position)); }
    void mouseMove (const juce::MouseEvent& e) override { if (owner.game && owner.playing) owner.game->drag (norm (e.position)); }
    bool keyPressed (const juce::KeyPress& k) override { return owner.keyPressed (k); }
    bool keyStateChanged (bool d) override { return owner.keyStateChanged (d); }

private:
    juce::Point<float> norm (juce::Point<float> p) const
    {
        const auto a = getLocalBounds().toFloat().reduced (14.0f, 26.0f);
        return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()), juce::jlimit (0.0f, 1.0f, (p.y - a.getY()) / a.getHeight()) };
    }
    XdPage& owner;
};

XdPage::XdPage (MysteryBoxProcessor& p) : proc (p)
{
    host.send = [this] (int note, float vel, bool on) { proc.injectNote (note, vel, on); };
    canvas = std::make_unique<Canvas> (*this);
    addAndMakeVisible (*canvas);

    for (int i = 0; i < games::numGames(); ++i)
    {
        auto g = games::createGame (i);
        auto b = std::make_unique<juce::TextButton> (juce::String (i + 1).paddedLeft ('0', 2) + "  " + juce::String (g->name()).toUpperCase());
        b->setRadioGroupId (8001);
        b->setClickingTogglesState (true);
        b->onClick = [this, i] { selectGame (i); };
        addAndMakeVisible (*b);
        gameButtons.push_back (std::move (b));
    }

    playBtn.setClickingTogglesState (true);
    playBtn.onClick = [this] { playing = playBtn.getToggleState(); playBtn.setButtonText (playing ? "PAUSE" : "PLAY"); if (! playing) host.allOff(); canvas->grabKeyboardFocus(); };
    autoBtn.setClickingTogglesState (true);
    autoBtn.getProperties().set ("accent", (juce::int64) colours::magenta.getARGB());
    autoBtn.setTooltip ("The game plays itself: a generative sequencer driven by the game AI");
    resetBtn.onClick = [this] { host.allOff(); if (game) game->reset(); wasOver = false; canvas->grabKeyboardFocus(); };

    scale.addItemList (games::scaleNames(), 1);
    scale.setSelectedItemIndex (1, juce::dontSendNotification);
    scale.onChange = [this] { host.scale = scale.getSelectedItemIndex(); };
    key.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    key.setSelectedItemIndex (9, juce::dontSendNotification);
    key.onChange = [this] { host.key = key.getSelectedItemIndex(); };
    octave.addItemList ({ "Oct 1", "Oct 2", "Oct 3", "Oct 4", "Oct 5" }, 1);
    octave.setSelectedItemIndex (2, juce::dontSendNotification);
    octave.onChange = [this] { host.octave = octave.getSelectedItemIndex() + 1; };
    target.addItem ("Current Patch", 1);
    target.addItem ("Patch + MIDI Out", 2);
    const juce::StringArray curated { "Raindrop Kalimba", "Pixel Arcade Hero", "Glass Hallway", "Rubber Gutter Bass", "Velvet Tine Keys", "Neon Conveyor" };
    for (int k = 0; k < curated.size(); ++k)
        target.addItem ("Load: " + curated[k], 10 + k);
    target.setSelectedId (1, juce::dontSendNotification);
    target.onChange = [this, curated] {
        const int id = target.getSelectedId();
        if (auto* midiOut = proc.getState().getParameter (params::id::midiOut))
            midiOut->setValueNotifyingHost (id == 2 ? 1.0f : midiOut->getValue());
        if (id >= 10)
        {
            auto& pm = proc.getPresets();
            if (pm.getEntries().empty()) pm.refresh();
            for (int i = 0; i < (int) pm.getEntries().size(); ++i)
                if (pm.getEntries()[(size_t) i].name == curated[id - 10]) { pm.loadIndex (i); proc.getFxRack().syncUnits(); break; }
        }
    };
    quantize.setSliderStyle (juce::Slider::LinearHorizontal);
    quantize.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    quantize.setRange (0.0, 1.0);
    quantize.setValue (0.5);
    quantize.onValueChange = [this] { host.quantize = (float) quantize.getValue(); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &playBtn, &autoBtn, &resetBtn, &scale, &key, &octave, &target, &quantize })
        addAndMakeVisible (c);

    gameButtons[0]->setToggleState (true, juce::dontSendNotification);
    selectGame (0);
}

XdPage::~XdPage()
{
    stopTimer();
    host.allOff();
    for (int i = 0; i < 4; ++i) proc.setGameMod (i, 0.0f);
}

void XdPage::visibilityChanged()
{
    if (isVisible())
    {
        lastMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
    }
    else
    {
        stopTimer();
        host.allOff();
    }
}

void XdPage::selectGame (int index)
{
    if (game && game->score > 0) recordScore();
    host.allOff();
    current = index;
    game = games::createGame (index);
    game->host = &host;
    game->reset();
    wasOver = false;
    repaint();
}

juce::StringArray XdPage::highScores (const juce::String& name) const
{
    juce::StringArray out;
    const auto all = juce::JSON::parse (GlobalSettings::get().getString ("hiscores", "{}"));
    if (auto* arr = all[juce::Identifier (name.replaceCharacter (' ', '_'))].getArray())
        for (auto& v : *arr) out.add (v.toString());
    return out;
}

void XdPage::recordScore()
{
    if (game == nullptr || game->score <= 0) return;
    auto all = juce::JSON::parse (GlobalSettings::get().getString ("hiscores", "{}"));
    if (! all.isObject()) all = juce::var (new juce::DynamicObject());
    const juce::Identifier id (juce::String (game->name()).replaceCharacter (' ', '_'));
    std::vector<int> scores;
    if (auto* arr = all[id].getArray()) for (auto& v : *arr) scores.push_back ((int) v);
    scores.push_back (game->score);
    std::sort (scores.rbegin(), scores.rend());
    scores.erase (std::unique (scores.begin(), scores.end()), scores.end());
    if (scores.size() > 5) scores.resize (5);
    juce::Array<juce::var> arr;
    for (int s : scores) arr.add (s);
    all.getDynamicObject()->setProperty (id, arr);
    GlobalSettings::get().set ("hiscores", juce::JSON::toString (all, true));
}

void XdPage::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastMs) / 1000.0);
    lastMs = now;
    host.bpm = (float) proc.getHostBpm();
    if (playing && game)
    {
        beat += dt * host.bpm / 60.0;
        host.advance (beat);
        const int sixteenth = (int) std::floor (beat * 4.0);
        if (sixteenth != lastSixteenth) { lastSixteenth = sixteenth; game->tick (((sixteenth % 64) + 64) % 64); }
        if (autoBtn.getToggleState()) game->autoplay (dt);
        game->update (dt);
        for (int i = 0; i < 4; ++i) proc.setGameMod (i, game->mods[(size_t) i]);
        if (game->over && ! wasOver) recordScore();
        wasOver = game->over;
    }
    canvas->repaint();
    repaint (juce::Rectangle<int> (980, 0, 284, getHeight()));
}

bool XdPage::keyPressed (const juce::KeyPress& k)
{
    if (! playing || ! game) return false;
    const int code = k.getKeyCode();
    if (std::find (downKeys.begin(), downKeys.end(), code) == downKeys.end())
    {
        downKeys.push_back (code);
        game->keyDown (code);
    }
    return true;
}

bool XdPage::keyStateChanged (bool)
{
    if (! game) return false;
    for (auto it = downKeys.begin(); it != downKeys.end();)
        if (! juce::KeyPress::isKeyCurrentlyDown (*it)) { game->keyUp (*it); it = downKeys.erase (it); }
        else ++it;
    return playing;
}

void XdPage::paint (juce::Graphics& g)
{
    drawTechPanel (g, { 16.0f, 0.0f, 220.0f, 680.0f }, "XD GAMES", colours::magenta, "25 TITLES");
    drawTechPanel (g, { 980.0f, 0.0f, 284.0f, 680.0f }, "CONTROL", colours::lime, "SYNCED " + juce::String (host.bpm, 1) + " BPM");
    g.setFont (fonts::display (9.5f));
    g.setColour (colours::textDim);
    const char* labels[] = { "SCALE", "KEY / OCTAVE", "QUANTIZE", "SOUND TARGET", "MOD OUT  (MATRIX SOURCES GAME 1-4)", "HIGH SCORES", "HOW TO PLAY" };
    const int ys[] = { 120, 172, 224, 262, 318, 438, 538 };
    for (int k = 0; k < 7; ++k)
        g.drawText (labels[k], 998, ys[k], 250, 12, juce::Justification::centredLeft);

    if (game)
    {
        for (int m = 0; m < 4; ++m)
        {
            const auto bar = juce::Rectangle<float> (998.0f, 336.0f + m * 22.0f, 248.0f, 14.0f);
            g.setColour (colours::track); g.fillRect (bar);
            g.setColour (games::gameColour (m)); g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, game->mods[(size_t) m])));
            g.setFont (fonts::mono (9.5f)); g.setColour (colours::text);
            g.drawText ("GAME " + juce::String (m + 1), bar.reduced (4, 0), juce::Justification::centredLeft);
        }
        const auto hs = highScores (game->name());
        g.setFont (fonts::mono (11.0f));
        for (int k = 0; k < 5; ++k)
        {
            g.setColour (k == 0 ? colours::lime : colours::text.withAlpha (0.8f));
            g.drawText (juce::String (k + 1) + ".  " + (k < hs.size() ? hs[k].paddedLeft (' ', 7) : juce::String ("   ---")), 998, 456 + k * 16, 250, 16, juce::Justification::centredLeft);
        }
        g.setColour (colours::textDim);
        g.setFont (fonts::mono (10.5f));
        g.drawFittedText (game->help(), juce::Rectangle<int> (998, 556, 252, 110), juce::Justification::topLeft, 8);
    }
}

void XdPage::resized()
{
    for (size_t i = 0; i < gameButtons.size(); ++i)
        gameButtons[i]->setBounds (28, 30 + (int) i * 25, 196, 22);
    canvas->setBounds (248, 0, 720, 680);
    playBtn.setBounds (998, 34, 120, 28);
    resetBtn.setBounds (1126, 34, 120, 28);
    autoBtn.setBounds (998, 70, 248, 28);
    scale.setBounds (998, 134, 248, 26);
    key.setBounds (998, 186, 120, 26);
    octave.setBounds (1126, 186, 120, 26);
    quantize.setBounds (994, 236, 256, 22);
    target.setBounds (998, 276, 248, 26);
}

} // namespace mbx::ui
