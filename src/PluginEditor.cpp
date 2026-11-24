#include "PluginEditor.h"

namespace mbx
{
using namespace ui;

//==============================================================================
SynthPage::SynthPage (MysteryBoxProcessor& p)
    : proc (p), state (p.getState()), waveDisplay (p.getState(), p.getWavetables()), envDisplay (p.getState()), scope (p.getScope())
{
    for (int i = 0; i < params::kNumOscs; ++i)
    {
        auto& tab = oscTabs[(size_t) i];
        tab.setButtonText ("OSC " + juce::String (i + 1));
        tab.setRadioGroupId (1001);
        tab.setClickingTogglesState (true);
        tab.onClick = [this, i] { selectOscillator (i); };
        addAndMakeVisible (tab);

        auto& power = oscPower[(size_t) i];
        power = std::make_unique<ParamToggle> ("", colours::lime);
        power->setTooltip ("Oscillator " + juce::String (i + 1) + " on/off");
        power->bind (state, params::oscId (i, params::id::oscOn));
        addAndMakeVisible (*power);
    }

    addAndMakeVisible (waveDisplay);
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &type, &shape, &table, &noise, &octave, &coarse, &fine, &level, &pan, &phase, &phaseRand, &unison, &detune,
             &blend, &spread, &wtPos, &pw, &envDisplay, &envDelay, &envAttack, &envHold, &envDecay, &envSustain, &envRelease,
             &envACurve, &envDCurve, &envRCurve, &envVel, &envPunch, &scope, &master, &poly, &bend, &oversampling })
        addAndMakeVisible (c);

    envDelay.bind (state, params::id::env1Delay);
    envAttack.bind (state, params::id::env1Attack);
    envHold.bind (state, params::id::env1Hold);
    envDecay.bind (state, params::id::env1Decay);
    envSustain.bind (state, params::id::env1Sustain);
    envRelease.bind (state, params::id::env1Release);
    envACurve.bind (state, params::id::env1ACurve);
    envDCurve.bind (state, params::id::env1DCurve);
    envRCurve.bind (state, params::id::env1RCurve);
    envVel.bind (state, params::id::env1Velocity);
    envPunch.bind (state, params::id::env1Punch);

    master.bind (state, params::id::masterVolume);
    poly.bind (state, params::id::polyphony);
    bend.bind (state, params::id::bendRange);
    oversampling.bind (state, params::id::oversampling);

    type.box.onChange = [this] { resized(); };
    oscTabs[0].setToggleState (true, juce::dontSendNotification);
    selectOscillator (0);
}

void SynthPage::selectOscillator (int i)
{
    selectedOsc = i;
    waveDisplay.setOscillator (i);
    type.bind (state, params::oscId (i, params::id::oscType));
    shape.bind (state, params::oscId (i, params::id::oscShape));
    table.bind (state, params::oscId (i, params::id::oscTable));
    noise.bind (state, params::oscId (i, params::id::oscNoise));
    octave.bind (state, params::oscId (i, params::id::oscOctave));
    coarse.bind (state, params::oscId (i, params::id::oscCoarse));
    fine.bind (state, params::oscId (i, params::id::oscFine));
    level.bind (state, params::oscId (i, params::id::oscLevel));
    pan.bind (state, params::oscId (i, params::id::oscPan));
    phase.bind (state, params::oscId (i, params::id::oscPhase));
    phaseRand.bind (state, params::oscId (i, params::id::oscPhaseRand));
    unison.bind (state, params::oscId (i, params::id::oscUnison));
    detune.bind (state, params::oscId (i, params::id::oscDetune));
    blend.bind (state, params::oscId (i, params::id::oscBlend));
    spread.bind (state, params::oscId (i, params::id::oscSpread));
    wtPos.bind (state, params::oscId (i, params::id::oscWtPos));
    pw.bind (state, params::oscId (i, params::id::oscPw));
    resized();
    repaint();
}

void SynthPage::paint (juce::Graphics& g)
{
    drawPanel (g, oscArea.toFloat(), "");
    drawPanel (g, envArea.toFloat(), "ENV 1  //  AMP", colours::magenta);
    drawPanel (g, masterArea.toFloat(), "OUTPUT  //  GLOBAL");
    drawPanel (g, comingArea.toFloat(), "FILTERS  //  LFO  //  MOD", colours::amber);

    auto c = comingArea.reduced (20, 34);
    g.setFont (fonts::mono (12.5f));
    g.setColour (colours::textDim);
    g.drawFittedText ("Arriving in Phase 2:\n\n"
                      "- 2 filters (ladder, SVF, diode, comb, formant, acid)\n"
                      "- ENV 2-4 + drawable MSEG\n"
                      "- 6 LFOs with tempo sync\n"
                      "- 32-slot mod matrix + 8 macros\n"
                      "- FM routing, warp modes, granular, WT scan\n"
                      "- Mono / legato / glide, MPE",
                      c, juce::Justification::topLeft, 12);
}

void SynthPage::resized()
{
    oscArea    = { 16, 0, 860, 450 };
    masterArea = { 892, 0, 372, 450 };
    envArea    = { 16, 462, 860, 262 };
    comingArea = { 892, 462, 372, 262 };

    // oscillator tabs + power LEDs
    for (int i = 0; i < params::kNumOscs; ++i)
    {
        const int x = 32 + i * 138;
        oscPower[(size_t) i]->setBounds (x, 16, 26, 28);
        oscTabs[(size_t) i].setBounds (x + 30, 16, 100, 28);
    }

    waveDisplay.setBounds (32, 60, 400, 374);

    const auto t = (dsp::OscType) type.box.getSelectedItemIndex();
    const bool isAnalog = t == dsp::OscType::Analog, isWt = t == dsp::OscType::Wavetable, isNoise = t == dsp::OscType::Noise;

    const int cx = 450;
    type.setBounds (cx, 60, 140, 40);
    octave.setBounds (cx + 150, 60, 80, 40);
    shape.setVisible (isAnalog);
    table.setVisible (isWt);
    noise.setVisible (isNoise);
    shape.setBounds (cx + 240, 60, 160, 40);
    table.setBounds (cx + 240, 60, 160, 40);
    noise.setBounds (cx + 240, 60, 160, 40);

    juce::Component* knobs[] = { &coarse, &fine, &level, &pan, &phase, &phaseRand, &unison, &detune, &blend, &spread, &wtPos, &pw };
    for (int k = 0; k < 12; ++k)
        knobs[k]->setBounds (cx + (k % 4) * 102, 110 + (k / 4) * 108, 96, 104);
    wtPos.setAlpha (isWt ? 1.0f : 0.35f);
    pw.setAlpha (isAnalog ? 1.0f : 0.35f);

    // amp envelope
    envDisplay.setBounds (32, 494, 300, 214);
    juce::Component* env[] = { &envDelay, &envAttack, &envHold, &envDecay, &envSustain, &envRelease,
                               &envACurve, &envDCurve, &envRCurve, &envVel, &envPunch };
    for (int k = 0; k < 11; ++k)
        env[k]->setBounds (350 + (k % 6) * 86, 490 + (k / 6) * 112, 84, 104);

    // master
    scope.setBounds (908, 34, 340, 190);
    master.setBounds (908, 240, 110, 110);
    poly.setBounds (1024, 240, 110, 110);
    bend.setBounds (1140, 240, 110, 110);
    oversampling.setBounds (908, 370, 160, 40);
}

//==============================================================================
PlannedPage::PlannedPage (juce::String t, juce::String ph, juce::StringArray it)
    : title (std::move (t)), phase (std::move (ph)), items (std::move (it))
{
}

void PlannedPage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (16, 0).withTrimmedBottom (12).toFloat();
    drawPanel (g, r, "");
    auto inner = r.reduced (60.0f, 50.0f);
    g.setFont (fonts::display (40.0f));
    g.setColour (colours::cyan);
    g.drawText (title, inner.removeFromTop (54.0f), juce::Justification::centredLeft);
    g.setFont (fonts::mono (15.0f));
    g.setColour (colours::amber);
    g.drawText (phase, inner.removeFromTop (30.0f), juce::Justification::centredLeft);
    inner.removeFromTop (20.0f);
    g.setColour (colours::text);
    for (auto& item : items)
        g.drawText ("> " + item, inner.removeFromTop (26.0f), juce::Justification::centredLeft);
}

//==============================================================================
MysteryBoxEditor::MysteryBoxEditor (MysteryBoxProcessor& p)
    : AudioProcessorEditor (p), proc (p), synthPage (p),
      presetsPage ("PRESETS", "Arrives in Phase 5",
                   { "Browser with search, tags, favourites, ratings, recently used", "Random preset + audio preview",
                     "1,000 factory presets across 10 categories", "User save / tag / export (JSON)" }),
      fxPage ("FX RACK", "Arrives in Phase 3",
              { "10 reorderable slots with bypass / mix / animated displays", "Distortion, OTT, compressor, transient shaper",
                "Chorus, flanger, phaser, ensemble, delay, reverb (hall/plate/room/shimmer)",
                "Granular freeze, formant & frequency shift, vocoder bank, EQ, widener, ducker, limiter",
                "Send buses A/B" }),
      advancedPage ("ADVANCED", "Arrives in Phases 2-4",
                    { "Per-module oversampling + quality", "Analog drift, voice stealing modes, per-voice randomisation",
                      "Microtuning (.scl/.kbm), MIDI CC table, MPE config", "Velocity / keytrack curve editors",
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

    const char* names[] = { "SYNTH", "PRESETS", "FX", "ADVANCED", "XD", "SETTINGS" };
    pages = { &synthPage, &presetsPage, &fxPage, &advancedPage, &xdPage, &settingsPage };
    for (int i = 0; i < 6; ++i)
    {
        auto& t = tabs[(size_t) i];
        t.setButtonText (names[i]);
        t.setRadioGroupId (2001);
        t.setClickingTogglesState (true);
        t.onClick = [this, i] { showTab (i); };
        content.addAndMakeVisible (t);
        content.addChildComponent (pages[(size_t) i]);
    }

    undoButton.onClick = [this] { proc.getUndoManager().undo(); };
    redoButton.onClick = [this] { proc.getUndoManager().redo(); };
    undoButton.setTooltip ("Undo (parameter changes)");
    redoButton.setTooltip ("Redo");
    content.addAndMakeVisible (undoButton);
    content.addAndMakeVisible (redoButton);

    presetLabel.setText ("INIT", juce::dontSendNotification);
    presetLabel.setFont (fonts::mono (15.0f));
    presetLabel.setColour (juce::Label::textColourId, colours::cyan);
    presetLabel.setJustificationType (juce::Justification::centred);
    cpuLabel.setFont (fonts::mono (12.0f));
    cpuLabel.setColour (juce::Label::textColourId, colours::textDim);
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    content.addAndMakeVisible (presetLabel);
    content.addAndMakeVisible (cpuLabel);

    content.addMouseListener (this, true); // start an undo transaction per gesture
    tabs[0].setToggleState (true, juce::dontSendNotification);
    showTab (0);

    setResizable (true, true);
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 2, kBaseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) kBaseWidth / kBaseHeight);
    setSize (kBaseWidth, kBaseHeight);
    startTimerHz (4);
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

void MysteryBoxEditor::timerCallback()
{
    cpuLabel.setText ("VOICES " + juce::String (proc.getActiveVoices()) + "   CPU "
                          + juce::String (proc.getCpuLoad() * 100.0, 1) + "%",
                      juce::dontSendNotification);
    undoButton.setEnabled (proc.getUndoManager().canUndo());
    redoButton.setEnabled (proc.getUndoManager().canRedo());
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

    undoButton.setBounds (206, 15, 60, 26);
    redoButton.setBounds (270, 15, 60, 26);
    constexpr int tabW = 96, gap = 6;
    const int tabsX = (kBaseWidth - (6 * tabW + 5 * gap)) / 2;
    for (int i = 0; i < 6; ++i)
        tabs[(size_t) i].setBounds (tabsX + i * (tabW + gap), 12, tabW, 32);
    presetLabel.setBounds (960, 12, 140, 32);
    cpuLabel.setBounds (1104, 12, 160, 32);
    for (auto* p : pages)
        p->setBounds (0, 64, kBaseWidth, kBaseHeight - 64);

    rebuildBackground();
}

void MysteryBoxEditor::rebuildBackground()
{
    const float scale = juce::jmax (0.5f, (float) getWidth() / (float) kBaseWidth)
                      * (float) juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;
    background = juce::Image (juce::Image::ARGB, juce::roundToInt (kBaseWidth * scale), juce::roundToInt (kBaseHeight * scale), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale));

    g.fillAll (colours::base);
    // perspective grid fading towards the top
    for (int x = 0; x <= kBaseWidth; x += 40)
    {
        g.setColour (colours::cyan.withAlpha (0.035f));
        g.drawVerticalLine (x, 56.0f, (float) kBaseHeight);
    }
    for (int y = 56; y <= kBaseHeight; y += 40)
    {
        g.setColour (colours::cyan.withAlpha (0.02f + 0.03f * (float) y / kBaseHeight));
        g.drawHorizontalLine (y, 0.0f, (float) kBaseWidth);
    }
    // holographic glow blobs
    g.setGradientFill (juce::ColourGradient (colours::magenta.withAlpha (0.08f), 1180, 760, colours::magenta.withAlpha (0.0f), 760, 400, true));
    g.fillRect (0, 0, kBaseWidth, kBaseHeight);
    g.setGradientFill (juce::ColourGradient (colours::cyan.withAlpha (0.07f), 120, 120, colours::cyan.withAlpha (0.0f), 600, 500, true));
    g.fillRect (0, 0, kBaseWidth, kBaseHeight);
    // scanlines + grain
    for (int y = 0; y < kBaseHeight; y += 3)
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.drawHorizontalLine (y, 0.0f, (float) kBaseWidth);
    }
    juce::Random rng (1234);
    for (int i = 0; i < 6000; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (rng.nextFloat() * 0.035f));
        g.fillRect ((float) rng.nextInt (kBaseWidth), (float) rng.nextInt (kBaseHeight), 1.0f, 1.0f);
    }
    // top bar
    auto bar = juce::Rectangle<float> (0, 0, (float) kBaseWidth, 56);
    g.setColour (juce::Colour (0xe0090c13));
    g.fillRect (bar);
    g.setGradientFill (juce::ColourGradient (colours::cyan.withAlpha (0.0f), 0, 56, colours::cyan, 640, 56, false));
    g.fillRect (0.0f, 55.0f, 640.0f, 1.0f);
    g.setGradientFill (juce::ColourGradient (colours::magenta, 640, 56, colours::magenta.withAlpha (0.0f), 1280, 56, false));
    g.fillRect (640.0f, 55.0f, 640.0f, 1.0f);

    // logo
    g.setFont (fonts::display (21.0f));
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (colours::cyan.withAlpha (0.08f));
        g.drawText ("MYSTERY BOX", juce::Rectangle<float> (18.0f - i, 7.0f, 190.0f, 30.0f), juce::Justification::centredLeft);
    }
    g.setColour (colours::cyan);
    g.drawText ("MYSTERY BOX", juce::Rectangle<float> (18.0f, 7.0f, 190.0f, 30.0f), juce::Justification::centredLeft);
    g.setFont (fonts::mono (10.0f));
    g.setColour (colours::magenta);
    g.drawText ("BY RAYGUN  //  v" MYSTERYBOX_VERSION, juce::Rectangle<float> (19.0f, 33.0f, 190.0f, 14.0f), juce::Justification::centredLeft);
}

void MysteryBoxEditor::paintContent (juce::Graphics& g)
{
    if (background.isValid())
        g.drawImage (background, juce::Rectangle<float> (0, 0, (float) kBaseWidth, (float) kBaseHeight));
}

} // namespace mbx
