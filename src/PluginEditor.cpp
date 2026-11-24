#include "PluginEditor.h"

namespace mbx
{
using namespace ui;

//==============================================================================
SynthPage::SynthPage (MysteryBoxProcessor& p)
    : proc (p), osc (p), mixVoice (p), filter1 (p, 0), filter2 (p, 1), mod (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &osc, &mixVoice, &filter1, &filter2, &mod })
        addAndMakeVisible (c);

    osc.onAddModulation = [this] (int d) { mod.addModulation (d); };
    for (auto& list : { osc.getModKnobs(), mixVoice.getModKnobs(), filter1.getModKnobs(), filter2.getModKnobs(), mod.getModKnobs() })
        for (auto* k : list)
            modKnobs.push_back (k);
    for (auto* k : modKnobs)
        if (k->slider.onAssignModulation == nullptr)
            k->slider.onAssignModulation = [this, k] { mod.addModulation (k->getModDest()); };
    startTimerHz (15);
}

void SynthPage::timerCallback()
{
    std::array<float, (size_t) dsp::dest::Count> depth {};
    auto& s = proc.getState();
    for (int k = 0; k < dsp::kNumMatrixSlots; ++k)
    {
        const int src = juce::roundToInt (rawParam (s, params::modId (k, params::id::modSrc)));
        const int dst = juce::roundToInt (rawParam (s, params::modId (k, params::id::modDest)));
        const bool byp = rawParam (s, params::modId (k, params::id::modBypass)) >= 0.5f;
        if (src > 0 && juce::isPositiveAndBelow (dst, dsp::dest::Count) && dst > 0 && ! byp)
            depth[(size_t) dst] += rawParam (s, params::modId (k, params::id::modAmt));
    }
    const bool playing = proc.getActiveVoices() > 0;
    for (auto* k : modKnobs)
    {
        const int d = k->getModDest();
        if (d <= 0 || d >= dsp::dest::Count)
        {
            k->setModulationDisplay (0.0f, 0.0f);
            continue;
        }
        k->setModulationDisplay (depth[(size_t) d] * k->getModTravel(), playing ? proc.getLiveModulation (d) * k->getModTravel() : 0.0f);
    }
}

void SynthPage::resized()
{
    osc.setBounds (16, 0, 852, 296);
    mixVoice.setBounds (880, 0, 384, 296);
    filter1.setBounds (16, 304, 620, 168);
    filter2.setBounds (644, 304, 620, 168);
    mod.setBounds (16, 480, 1248, 232);
}

//==============================================================================
PlannedPage::PlannedPage (juce::String t, juce::String ph, juce::StringArray it)
    : title (std::move (t)), phase (std::move (ph)), items (std::move (it))
{
}

void PlannedPage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (16, 0).withTrimmedBottom (4).toFloat();
    drawTechPanel (g, r, title, colours::cyan, "MODULE OFFLINE // AWAITING BUILD");
    auto inner = r.reduced (60.0f, 60.0f);
    g.setFont (fonts::display (40.0f));
    g.setColour (colours::cyan);
    g.drawText (title, inner.removeFromTop (54.0f), juce::Justification::centredLeft);
    g.setFont (fonts::mono (15.0f));
    g.setColour (colours::amber);
    g.drawText ("[!] " + phase, inner.removeFromTop (30.0f), juce::Justification::centredLeft);
    inner.removeFromTop (20.0f);
    g.setColour (colours::text);
    int n = 0;
    for (auto& item : items)
        g.drawText ("0x" + juce::String::toHexString (++n).paddedLeft ('0', 2) + "  > " + item, inner.removeFromTop (26.0f), juce::Justification::centredLeft);
}

//==============================================================================
TopBar::TopBar (MysteryBoxProcessor& p) : proc (p), miniScope (p.getScope(), true)
{
    const char* names[] = { "SYNTH", "PRESETS", "FX", "ADVANCED", "XD", "SETTINGS" };
    for (int i = 0; i < 6; ++i)
    {
        auto& t = tabs[(size_t) i];
        t.setButtonText ("0" + juce::String (i + 1) + " " + names[i]);
        t.setRadioGroupId (2001);
        t.setClickingTogglesState (true);
        t.onClick = [this, i] { if (onTab) onTab (i); };
        addAndMakeVisible (t);
    }
    undoButton.onClick = [this] { proc.getUndoManager().undo(); };
    redoButton.onClick = [this] { proc.getUndoManager().redo(); };
    undoButton.setTooltip ("Undo parameter change");
    redoButton.setTooltip ("Redo");
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);
    addAndMakeVisible (miniScope);
    tabs[0].setToggleState (true, juce::dontSendNotification);
    startTimerHz (30);
}

void TopBar::resized()
{
    undoButton.setBounds (206, 15, 58, 24);
    redoButton.setBounds (268, 15, 58, 24);
    constexpr int tabW = 100, gap = 4;
    const int x0 = (getWidth() - (6 * tabW + 5 * gap)) / 2 + 8;
    for (int i = 0; i < 6; ++i)
        tabs[(size_t) i].setBounds (x0 + i * (tabW + gap), 11, tabW, 30);
    miniScope.setBounds (1120, 9, 96, 28);
}

void TopBar::timerCallback()
{
    sweep += 0.012f;
    if (sweep > 1.3f) sweep = -0.3f;
    const float peak = proc.getOutputPeak();
    meter = std::max (peak, meter * 0.9f);
    if (peak >= meterPeak) { meterPeak = peak; peakHold = 45; }
    else if (--peakHold < 0) meterPeak *= 0.97f;
    cpu = cpu * 0.8f + 0.2f * (float) proc.getCpuLoad();
    undoButton.setEnabled (proc.getUndoManager().canUndo());
    redoButton.setEnabled (proc.getUndoManager().canRedo());
    repaint (0, getHeight() - 8, getWidth(), 8);  // sweep strip
    repaint (958, 0, getWidth() - 958, getHeight()); // readouts
}

void TopBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xe0090c13));
    g.fillRect (r);
    drawHatch (g, { 0.0f, 0.0f, r.getWidth(), 4.0f }, colours::cyan.withAlpha (0.08f), 6.0f);

    // glitch logo: RGB-split copies behind the main title
    const auto logoArea = juce::Rectangle<float> (18.0f, 6.0f, 190.0f, 30.0f);
    g.setFont (fonts::display (21.0f));
    g.setColour (colours::magenta.withAlpha (0.55f));
    g.drawText ("MYSTERY BOX", logoArea.translated (-1.5f, 0.5f), juce::Justification::centredLeft);
    g.setColour (colours::cyan.withAlpha (0.55f));
    g.drawText ("MYSTERY BOX", logoArea.translated (1.5f, -0.5f), juce::Justification::centredLeft);
    g.setColour (colours::text);
    g.drawText ("MYSTERY BOX", logoArea, juce::Justification::centredLeft);
    g.setFont (fonts::mono (9.5f));
    g.setColour (colours::magenta);
    g.drawText ("BY RAYGUN  //  v" MYSTERYBOX_VERSION "  //  6-OSC", juce::Rectangle<float> (19.0f, 34.0f, 190.0f, 12.0f), juce::Justification::centredLeft);

    // preset readout
    auto preset = juce::Rectangle<float> (960.0f, 9.0f, 150.0f, 38.0f);
    g.setColour (colours::screen);
    g.fillPath (chamferedRect (preset, 6.0f));
    g.setColour (colours::cyan.withAlpha (0.5f));
    g.strokePath (chamferedRect (preset, 6.0f), juce::PathStrokeType (1.0f));
    g.setFont (fonts::mono (8.5f));
    g.setColour (colours::textDim);
    g.drawText ("PATCH // 000", preset.reduced (8.0f, 3.0f), juce::Justification::topLeft);
    g.setFont (fonts::mono (14.0f));
    g.setColour (colours::cyan);
    g.drawText ("<  INIT  >", preset.reduced (8.0f, 2.0f), juce::Justification::centredBottom);

    g.setColour (colours::cyan);
    g.fillRect (0.0f, r.getBottom() - 1.0f, r.getWidth(), 1.0f);
}

void TopBar::paintOverChildren (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // moving scan sweep along the bottom edge
    const float sx = r.getWidth() * sweep;
    g.setGradientFill (juce::ColourGradient (colours::cyan.withAlpha (0.0f), sx - 220.0f, 0, colours::cyan, sx, 0, false));
    g.fillRect (sx - 220.0f, r.getBottom() - 2.0f, 220.0f, 2.0f);
    g.setGradientFill (juce::ColourGradient (colours::magenta, sx, 0, colours::magenta.withAlpha (0.0f), sx + 120.0f, 0, false));
    g.fillRect (sx, r.getBottom() - 2.0f, 120.0f, 2.0f);
    drawTickRuler (g, { 0.0f, r.getBottom() - 7.0f, r.getWidth(), 5.0f }, colours::cyan.withAlpha (0.6f), 128);

    // output meter + peak hold under the scope
    auto m = juce::Rectangle<float> (1120.0f, 40.0f, 96.0f, 5.0f);
    g.setColour (colours::track);
    g.fillRect (m);
    const float lvl = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (meter, -60.0f) + 60.0f) / 66.0f);
    g.setGradientFill (juce::ColourGradient (colours::lime, m.getX(), 0, meter > 0.98f ? colours::amber : colours::cyan, m.getRight(), 0, false));
    g.fillRect (m.withWidth (m.getWidth() * lvl));
    const float pk = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (meterPeak, -60.0f) + 60.0f) / 66.0f);
    g.setColour (meterPeak > 0.98f ? colours::amber : colours::text);
    g.fillRect (m.getX() + m.getWidth() * pk - 1.0f, m.getY() - 1.0f, 2.0f, m.getHeight() + 2.0f);

    g.setFont (fonts::mono (9.0f));
    g.setColour (colours::textDim);
    g.drawText ("CPU", juce::Rectangle<float> (1222.0f, 8.0f, 50.0f, 12.0f), juce::Justification::centredLeft);
    g.setFont (fonts::mono (13.0f));
    g.setColour (cpu > 0.7f ? colours::amber : colours::cyan);
    g.drawText (juce::String (cpu * 100.0f, 1) + "%", juce::Rectangle<float> (1222.0f, 20.0f, 54.0f, 16.0f), juce::Justification::centredLeft);
    drawLed (g, { 1226.0f, 42.0f }, 2.5f, colours::lime, proc.getActiveVoices() > 0);
    g.setFont (fonts::mono (9.0f));
    g.setColour (colours::textDim);
    g.drawText ("SIG", juce::Rectangle<float> (1233.0f, 36.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
}

//==============================================================================
StatusBar::StatusBar (MysteryBoxProcessor& p) : proc (p) { startTimerHz (8); }

void StatusBar::timerCallback()
{
    ++ticks;
    juce::Random r ((juce::int64) ticks * 7919);
    hexStream = hexStream.substring (3);
    while (hexStream.length() < 96)
        hexStream << juce::String::toHexString (r.nextInt (256)).paddedLeft ('0', 2).toUpperCase() << " ";
    repaint();
}

void StatusBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xf0070a10));
    g.fillRect (r);
    g.setColour (colours::cyan.withAlpha (0.7f));
    g.fillRect (0.0f, 0.0f, r.getWidth(), 1.0f);
    drawHatch (g, { r.getWidth() - 70.0f, 4.0f, 60.0f, r.getHeight() - 8.0f }, colours::magenta.withAlpha (0.35f), 5.0f);

    g.setFont (fonts::mono (10.0f));
    float x = 14.0f;
    auto item = [&] (const juce::String& label, const juce::String& value, juce::Colour c) {
        g.setColour (colours::textDim);
        const float lw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 4.0f;
        g.drawText (label, juce::Rectangle<float> (x, 0, lw, r.getHeight()), juce::Justification::centredLeft);
        x += lw;
        g.setColour (c);
        const float vw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), value) + 18.0f;
        g.drawText (value, juce::Rectangle<float> (x, 0, vw, r.getHeight()), juce::Justification::centredLeft);
        x += vw;
    };

    drawLed (g, { x + 3.0f, r.getCentreY() }, 3.0f, colours::lime, (ticks / 4) % 2 == 0);
    x += 12.0f;
    item ("", "ENGINE ONLINE", colours::lime);
    item ("SR", juce::String (proc.getSampleRateHz() / 1000.0, 1) + "k", colours::cyan);
    item ("OS", juce::String (proc.getOversamplingFactor()) + "x", colours::cyan);
    item ("LAT", juce::String (proc.getLatencySamples()) + " smp", colours::cyan);
    item ("VOX", juce::String (proc.getActiveVoices()).paddedLeft ('0', 2) + "/32", colours::magenta);
    item ("BPM", juce::String (proc.getHostBpm(), 1), colours::cyan);
    item ("PARAMS", juce::String (proc.getParameters().size()), colours::cyan);

    g.setColour (colours::cyan.withAlpha (0.25f));
    g.drawText (hexStream, juce::Rectangle<float> (x + 10.0f, 0, 420.0f, r.getHeight()), juce::Justification::centredLeft);

    const int secs = (int) ((juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0);
    g.setColour (colours::textDim);
    g.drawText ("UPTIME " + juce::String (secs / 3600).paddedLeft ('0', 2) + ":" + juce::String ((secs / 60) % 60).paddedLeft ('0', 2) + ":"
                    + juce::String (secs % 60).paddedLeft ('0', 2) + "   //   RAYGUN SYS",
                juce::Rectangle<float> (0, 0, r.getWidth() - 82.0f, r.getHeight()), juce::Justification::centredRight);
}

//==============================================================================
MysteryBoxEditor::MysteryBoxEditor (MysteryBoxProcessor& p)
    : AudioProcessorEditor (p), proc (p), topBar (p), statusBar (p), synthPage (p), fxPage (p), slimMeter (p.getOutputMeter(), true),
      presetsPage ("PRESETS", "Arrives in Phase 5",
                   { "Browser with search, tags, favourites, ratings, recently used", "Random preset + audio preview",
                     "1,000 factory presets across 10 categories", "User save / tag / export (JSON)" }),
      advancedPage ("ADVANCED", "Arrives in Phases 3-4",
                    { "Per-module oversampling + quality", "Analog drift, voice stealing modes, per-voice randomisation",
                      "Microtuning (.scl/.kbm), MIDI CC table, MPE zone config", "Velocity / keytrack curve editors",
                      "Full parameter browser with search" }),
      xdPage ("XD // GAMES", "Arrives in Phase 6",
              { "25 original games that play the synth", "Tempo sync, scale & key lock, quantize, high scores",
                "Game outputs as mod-matrix sources", "Performance (auto-play) mode as a generative sequencer" }),
      settingsPage ("SETTINGS", "Arrives in Phase 4",
                    { "UI scale + themes (Neon Cyan, Magenta Night, Acid Green, Mono)", "Animation level / Eco GPU mode",
                      "Quality defaults, MIDI learn management, preset folder", "Tooltips, shortcuts, colorblind-safe palette" })
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    pages = { &synthPage, &presetsPage, &fxPage, &advancedPage, &xdPage, &settingsPage };
    for (auto* pg : pages)
        content.addChildComponent (pg);
    content.addAndMakeVisible (topBar);
    content.addAndMakeVisible (statusBar);
    topBar.onTab = [this] (int i) { showTab (i); };
    content.addAndMakeVisible (slimMeter);
    slimMeter.setTooltip ("Output level  //  click for the full meters");
    slimMeter.onClick = [this] {
        topBar.tabs[2].setToggleState (true, juce::dontSendNotification);
        showTab (2);
        fxPage.showMaster (true);
    };

    content.addMouseListener (this, true); // start an undo transaction per gesture
    showTab (0);

    setResizable (true, true);
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 2, kBaseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) kBaseWidth / kBaseHeight);
    setSize (kBaseWidth, kBaseHeight);
}

MysteryBoxEditor::~MysteryBoxEditor()
{
    setLookAndFeel (nullptr);
}

void MysteryBoxEditor::showTab (int index)
{
    currentTab = index;
    for (int i = 0; i < 6; ++i)
        pages[(size_t) i]->setVisible (i == index);
}

void MysteryBoxEditor::mouseDown (const juce::MouseEvent&)
{
    proc.getUndoManager().beginNewTransaction();
}

void MysteryBoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::base);
}

void MysteryBoxEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseWidth;
    content.setBounds (0, 0, kBaseWidth, kBaseHeight);
    content.setTransform (juce::AffineTransform::scale (scale));

    topBar.setBounds (0, 0, kBaseWidth, kTopBarHeight);
    statusBar.setBounds (0, kBaseHeight - kStatusHeight, kBaseWidth, kStatusHeight);
    for (auto* p : pages)
        p->setBounds (0, kTopBarHeight + 6, kBaseWidth, kBaseHeight - kTopBarHeight - kStatusHeight - 8);
    slimMeter.setBounds (kBaseWidth - 11, kTopBarHeight + 10, 7, kBaseHeight - kTopBarHeight - kStatusHeight - 20);

    rebuildBackground();
}

void MysteryBoxEditor::rebuildBackground()
{
    float dpi = 1.0f;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        dpi = (float) d->scale;
    const float scale = juce::jmax (0.5f, (float) getWidth() / (float) kBaseWidth) * dpi;
    background = juce::Image (juce::Image::ARGB, juce::roundToInt (kBaseWidth * scale), juce::roundToInt (kBaseHeight * scale), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale));

    constexpr float W = (float) kBaseWidth, H = (float) kBaseHeight;
    g.fillAll (colours::base);

    // base grid
    for (int x = 0; x <= kBaseWidth; x += 40)
    {
        g.setColour (colours::cyan.withAlpha (0.03f));
        g.drawVerticalLine (x, 56.0f, H);
    }
    for (int y = 56; y <= kBaseHeight; y += 40)
    {
        g.setColour (colours::cyan.withAlpha (0.02f + 0.03f * (float) y / H));
        g.drawHorizontalLine (y, 0.0f, W);
    }

    // holographic glow blobs
    g.setGradientFill (juce::ColourGradient (colours::magenta.withAlpha (0.09f), 1180, 760, colours::magenta.withAlpha (0.0f), 760, 400, true));
    g.fillRect (0.0f, 0.0f, W, H);
    g.setGradientFill (juce::ColourGradient (colours::cyan.withAlpha (0.08f), 120, 120, colours::cyan.withAlpha (0.0f), 600, 500, true));
    g.fillRect (0.0f, 0.0f, W, H);

    // radar rings
    for (int i = 1; i <= 7; ++i)
    {
        g.setColour (colours::cyan.withAlpha (0.035f));
        g.drawEllipse (juce::Rectangle<float> ((float) i * 110.0f, (float) i * 110.0f).withCentre ({ 1130.0f, 230.0f }), 1.0f);
    }
    for (int i = 0; i < 24; ++i)
    {
        const float a = (float) i / 24.0f * juce::MathConstants<float>::twoPi;
        g.setColour (colours::cyan.withAlpha (0.025f));
        g.drawLine (juce::Line<float> ({ 1130.0f, 230.0f }, juce::Point<float> (1130.0f, 230.0f).getPointOnCircumference (390.0f, a)), 1.0f);
    }

    // circuit traces (deterministic)
    juce::Random rng (0xC0DE);
    for (int t = 0; t < 90; ++t)
    {
        float x = (float) (rng.nextInt (kBaseWidth / 20) * 20);
        float y = 60.0f + (float) (rng.nextInt ((kBaseHeight - 90) / 20) * 20);
        const auto c = (t % 5 == 0 ? colours::magenta : colours::cyan).withAlpha (0.05f + 0.06f * rng.nextFloat());
        juce::Path p;
        p.startNewSubPath (x, y);
        g.setColour (c);
        g.drawEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f, 1.0f);
        const int segs = 2 + rng.nextInt (5);
        for (int s = 0; s < segs; ++s)
        {
            const float len = (float) (1 + rng.nextInt (5)) * 20.0f;
            switch (rng.nextInt (4))
            {
                case 0:  x += len; break;
                case 1:  y += len; break;
                case 2:  x += len; y += len * (rng.nextBool() ? 1.0f : -1.0f); break; // 45 degree jog
                default: x -= len; break;
            }
            p.lineTo (x, y);
        }
        g.strokePath (p, juce::PathStrokeType (1.0f));
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }

    // edge rulers with coordinates
    g.setFont (fonts::mono (7.0f));
    for (int y = 60; y < kBaseHeight - 24; y += 10)
    {
        const bool major = y % 100 == 0;
        g.setColour (colours::cyan.withAlpha (major ? 0.35f : 0.15f));
        g.fillRect (0.0f, (float) y, major ? 9.0f : 4.0f, 1.0f);
        g.fillRect (W - (major ? 9.0f : 4.0f), (float) y, major ? 9.0f : 4.0f, 1.0f);
    }

    // outer HUD frame
    drawCornerBrackets (g, { 3.0f, 59.0f, W - 6.0f, H - 59.0f - 25.0f }, colours::cyan.withAlpha (0.5f), 22.0f, 2.0f);

    // scanlines + grain
    for (int y = 0; y < kBaseHeight; y += 3)
    {
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.drawHorizontalLine (y, 0.0f, W);
    }
    juce::Random grain (1234);
    for (int i = 0; i < 7000; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (grain.nextFloat() * 0.035f));
        g.fillRect ((float) grain.nextInt (kBaseWidth), (float) grain.nextInt (kBaseHeight), 1.0f, 1.0f);
    }
}

void MysteryBoxEditor::paintContent (juce::Graphics& g)
{
    if (background.isValid())
        g.drawImage (background, juce::Rectangle<float> (0, 0, (float) kBaseWidth, (float) kBaseHeight));
}

} // namespace mbx
