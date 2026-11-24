#include "Sections.h"
#include "ArpSeqPage.h"

namespace mbx::ui
{
namespace
{
    juce::String modSlotSummary (juce::AudioProcessorValueTreeState& s, int slot, int& sourceOut, bool& active)
    {
        sourceOut = juce::roundToInt (rawParam (s, params::modId (slot, params::id::modSrc)));
        const int dst = juce::roundToInt (rawParam (s, params::modId (slot, params::id::modDest)));
        const float amt = rawParam (s, params::modId (slot, params::id::modAmt));
        const bool byp = rawParam (s, params::modId (slot, params::id::modBypass)) >= 0.5f;
        active = sourceOut > 0 && dst > 0 && std::abs (amt) > 0.0001f && ! byp;
        const int pct = juce::roundToInt (amt * 100.0f);
        return "-> " + params::modDestNames()[dst].toUpperCase().paddedRight (' ', 18) + (pct > 0 ? "+" : "") + juce::String (pct) + "%";
    }

    void bindOsc (ParamKnob& k, juce::AudioProcessorValueTreeState& s, int osc, const char* id) { k.bind (s, params::oscId (osc, id)); }
    void bindOsc (ParamChoice& c, juce::AudioProcessorValueTreeState& s, int osc, const char* id) { c.bind (s, params::oscId (osc, id)); }
} // namespace

//==============================================================================
void RoutedList::timerCallback()
{
    juce::StringArray next;
    for (int k = 0; k < dsp::kNumMatrixSlots; ++k)
    {
        int src = 0;
        bool active = false;
        const auto text = modSlotSummary (state, k, src, active);
        if (active && src == source)
            next.add (juce::String (k + 1).paddedLeft ('0', 2) + " " + text);
    }
    if (next != lines)
    {
        lines = next;
        repaint();
    }
}

void RoutedList::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r, accent);
    auto area = r.reduced (10.0f, 6.0f);
    g.setFont (fonts::mono (10.0f));
    g.setColour (accent);
    g.drawText ("ROUTED TO  //  " + juce::String (lines.size()) + " SLOT" + (lines.size() == 1 ? "" : "S"), area.removeFromTop (14.0f), juce::Justification::centredLeft);
    area.removeFromTop (4.0f);
    g.setColour (colours::text);
    if (lines.isEmpty())
    {
        g.setColour (colours::textDim);
        g.drawText ("NOT ROUTED  -  use MATRIX or right-click a knob", area.removeFromTop (14.0f), juce::Justification::centredLeft);
    }
    for (const auto& l : lines)
    {
        if (area.getHeight() < 12.0f)
            break;
        g.drawText (l, area.removeFromTop (13.0f), juce::Justification::centredLeft);
    }
}

//==============================================================================
OscSection::OscSection (MysteryBoxProcessor& p) : proc (p), state (p.getState()), display (p)
{
    for (int i = 0; i < 6; ++i)
    {
        auto& t = tabs[(size_t) i];
        t.setButtonText ("OSC.0" + juce::String (i + 1));
        t.setRadioGroupId (1001);
        t.setClickingTogglesState (true);
        t.onClick = [this, i] { selectOscillator (i); };
        addAndMakeVisible (t);
        power[(size_t) i] = std::make_unique<ParamToggle> ("", colours::lime);
        power[(size_t) i]->setTooltip ("Oscillator " + juce::String (i + 1) + " on/off");
        power[(size_t) i]->bind (state, params::oscId (i, params::id::oscOn));
        addAndMakeVisible (*power[(size_t) i]);
    }

    addAndMakeVisible (display);
    display.onDrawFinished = [this] {
        if (auto* t = state.getParameter (params::oscId (selected, params::id::oscTable)))
            t->setValueNotifyingHost (t->convertTo0to1 ((float) dsp::kUserTableIndex));
        if (auto* t = state.getParameter (params::oscId (selected, params::id::oscType)))
            t->setValueNotifyingHost (t->convertTo0to1 (0.0f));
        status = "DRAWN CYCLE -> USER TABLE";
    };

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &type, &shape, &table, &noise, &octave, &route, &warp, &modSrc, &scanDiv, &scanLoop, &scanOn, &scanSync, &loadButton,
             &drawButton, &coarse, &fine, &level, &pan, &phase, &rand, &unison, &detune, &blend, &spread, &wtPos, &pw, &warpAmt,
             &scanRate, &scanStart, &scanMid, &scanEnd, &gSize, &gDensity, &gSpray, &gSpread })
        addAndMakeVisible (c);

    loadButton.setTooltip ("Load a wavetable (Wavetable type) or a sample (Granular type). You can also drag & drop a file.");
    loadButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Load audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) {
                                  if (fc.getResult().existsAsFile())
                                      loadFile (fc.getResult());
                              });
    };
    drawButton.setClickingTogglesState (true);
    drawButton.getProperties().set ("accent", (juce::int64) colours::lime.getARGB());
    drawButton.setTooltip ("Draw a single-cycle waveform into this oscillator's User wavetable");
    drawButton.onClick = [this] { display.setDrawMode (drawButton.getToggleState()); };

    tabs[0].setToggleState (true, juce::dontSendNotification);
    selectOscillator (0);
    startTimerHz (8);
}

void OscSection::selectOscillator (int i)
{
    selected = i;
    display.setOscillator (i);
    drawButton.setToggleState (false, juce::dontSendNotification);
    display.setDrawMode (false);
    status.clear();

    bindOsc (type, state, i, params::id::oscType);       bindOsc (shape, state, i, params::id::oscShape);
    bindOsc (table, state, i, params::id::oscTable);     bindOsc (noise, state, i, params::id::oscNoise);
    bindOsc (octave, state, i, params::id::oscOctave);   bindOsc (route, state, i, params::id::oscRoute);
    bindOsc (warp, state, i, params::id::oscWarp);       bindOsc (modSrc, state, i, params::id::oscModSrc);
    bindOsc (scanDiv, state, i, params::id::oscScanDiv); bindOsc (scanLoop, state, i, params::id::oscScanLoop);
    scanOn.bind (state, params::oscId (i, params::id::oscScanOn));
    scanSync.bind (state, params::oscId (i, params::id::oscScanSync));

    bindOsc (coarse, state, i, params::id::oscCoarse);   bindOsc (fine, state, i, params::id::oscFine);
    bindOsc (level, state, i, params::id::oscLevel);     bindOsc (pan, state, i, params::id::oscPan);
    bindOsc (phase, state, i, params::id::oscPhase);     bindOsc (rand, state, i, params::id::oscPhaseRand);
    bindOsc (unison, state, i, params::id::oscUnison);   bindOsc (detune, state, i, params::id::oscDetune);
    bindOsc (blend, state, i, params::id::oscBlend);     bindOsc (spread, state, i, params::id::oscSpread);
    bindOsc (wtPos, state, i, params::id::oscWtPos);     bindOsc (pw, state, i, params::id::oscPw);
    bindOsc (warpAmt, state, i, params::id::oscWarpAmt); bindOsc (scanRate, state, i, params::id::oscScanRate);
    bindOsc (scanStart, state, i, params::id::oscScanStart); bindOsc (scanMid, state, i, params::id::oscScanMid);
    bindOsc (scanEnd, state, i, params::id::oscScanEnd); bindOsc (gSize, state, i, params::id::oscGrainSize);
    bindOsc (gDensity, state, i, params::id::oscGrainDensity); bindOsc (gSpray, state, i, params::id::oscGrainSpray);
    bindOsc (gSpread, state, i, params::id::oscGrainSpread);

    using namespace dsp::dest;
    coarse.setModTarget (osc (i, Pitch), 0.5f);
    level.setModTarget (osc (i, Level), 1.0f);
    pan.setModTarget (osc (i, Pan), 0.5f);
    wtPos.setModTarget (osc (i, WtPos), 1.0f);
    pw.setModTarget (osc (i, PulseWidth), 0.5f);
    detune.setModTarget (osc (i, Detune), 0.5f);
    blend.setModTarget (osc (i, Blend), 1.0f);
    spread.setModTarget (osc (i, Spread), 1.0f);
    warpAmt.setModTarget (osc (i, WarpAmount), 1.0f);
    for (auto* k : getModKnobs())
        k->slider.onAssignModulation = [this, k] { if (onAddModulation) onAddModulation (k->getModDest()); };

    lastType = -1;
    layoutForType();
    repaint();
}

std::vector<ParamKnob*> OscSection::getModKnobs() { return { &coarse, &level, &pan, &wtPos, &pw, &detune, &blend, &spread, &warpAmt }; }

void OscSection::timerCallback()
{
    const int t = juce::roundToInt (rawParam (state, params::oscId (selected, params::id::oscType)));
    if (t != lastType)
        layoutForType();
}

void OscSection::layoutForType()
{
    const auto t = (dsp::OscType) juce::roundToInt (rawParam (state, params::oscId (selected, params::id::oscType)));
    lastType = (int) t;
    const bool isWt = t == dsp::OscType::Wavetable, isGrain = t == dsp::OscType::Granular;
    const bool isAnalog = t == dsp::OscType::Analog, isNoise = t == dsp::OscType::Noise;
    shape.setVisible (isAnalog);
    table.setVisible (isWt);
    noise.setVisible (isNoise);
    loadButton.setVisible (isWt || isGrain);
    drawButton.setVisible (isWt);
    if (! isWt && display.isDrawMode())
    {
        drawButton.setToggleState (false, juce::dontSendNotification);
        display.setDrawMode (false);
    }
    const bool scanUi = isWt || isGrain;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &scanOn, &scanSync, &scanDiv, &scanLoop })
        c->setVisible (scanUi);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &scanRate, &scanStart, &scanMid, &scanEnd })
        c->setVisible (isWt);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &gSize, &gDensity, &gSpray, &gSpread })
        c->setVisible (isGrain);
    wtPos.setAlpha (isWt || isGrain ? 1.0f : 0.35f);
    pw.setAlpha (isAnalog ? 1.0f : 0.35f);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &unison, &detune, &blend, &spread, &phase, &rand })
        c->setAlpha (isNoise || isGrain ? 0.35f : 1.0f);
    resized();
}

void OscSection::paint (juce::Graphics& g)
{
    drawTechPanel (g, getLocalBounds().toFloat(), "", colours::cyan, {}, false);
    if (dragHover)
    {
        g.setColour (colours::lime.withAlpha (0.12f));
        g.fillRect (display.getBounds());
        drawCornerBrackets (g, display.getBounds().toFloat().expanded (3.0f), colours::lime, 16.0f, 2.0f);
    }
    g.setFont (fonts::mono (9.5f));
    g.setColour (status.startsWith ("ERR") ? colours::amber : colours::textDim);
    g.drawText (status.isNotEmpty() ? status : "SYS.OSC // 0x0" + juce::String (selected + 1) + "  DRAG AUDIO ONTO THE DISPLAY",
                juce::Rectangle<int> (342, getHeight() - 14, getWidth() - 356, 12), juce::Justification::centredRight);
}

void OscSection::resized()
{
    for (int i = 0; i < 6; ++i)
    {
        const int x = 14 + i * 138;
        power[(size_t) i]->setBounds (x, 8, 22, 22);
        tabs[(size_t) i].setBounds (x + 26, 8, 104, 22);
    }
    display.setBounds (14, 38, 316, 246);

    constexpr int cx = 342;
    type.setBounds (cx, 36, 108, 34);
    shape.setBounds (cx + 114, 36, 124, 34);
    table.setBounds (cx + 114, 36, 124, 34);
    noise.setBounds (cx + 114, 36, 124, 34);
    octave.setBounds (cx + 244, 36, 54, 34);
    route.setBounds (cx + 304, 36, 86, 34);
    loadButton.setBounds (cx + 396, 49, 46, 21);
    drawButton.setBounds (cx + 446, 49, 46, 21);

    warp.setBounds (cx, 76, 108, 34);
    modSrc.setBounds (cx + 114, 76, 74, 34);
    scanOn.setBounds (cx + 194, 89, 50, 21);
    scanSync.setBounds (cx + 248, 89, 50, 21);
    scanDiv.setBounds (cx + 304, 76, 70, 34);
    scanLoop.setBounds (cx + 380, 76, 112, 34);

    ParamKnob* row1[] = { &coarse, &fine, &level, &pan, &phase, &rand, &unison, &detune, &blend };
    for (int k = 0; k < 9; ++k)
        row1[k]->setBounds (cx + k * 55, 114, 55, 84);
    ParamKnob* row2a[] = { &spread, &wtPos, &pw, &warpAmt };
    for (int k = 0; k < 4; ++k)
        row2a[k]->setBounds (cx + k * 55, 198, 55, 84);
    ParamKnob* scanK[] = { &scanRate, &scanStart, &scanMid, &scanEnd };
    ParamKnob* grainK[] = { &gSize, &gDensity, &gSpray, &gSpread };
    for (int k = 0; k < 4; ++k)
    {
        scanK[k]->setBounds (cx + (k + 4) * 55 + 6, 198, 55, 84);
        grainK[k]->setBounds (cx + (k + 4) * 55 + 6, 198, 55, 84);
    }
}

bool OscSection::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac"))
            return true;
    return false;
}

void OscSection::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    if (! files.isEmpty())
        loadFile (juce::File (files[0]));
    repaint();
}

void OscSection::loadFile (const juce::File& f)
{
    const auto t = (dsp::OscType) juce::roundToInt (rawParam (state, params::oscId (selected, params::id::oscType)));
    juce::String err;
    if (t == dsp::OscType::Granular)
        err = proc.getAssets().loadSample (selected, f);
    else
    {
        err = proc.getAssets().loadWavetable (selected, f);
        if (err.isEmpty())
        {
            if (auto* p = state.getParameter (params::oscId (selected, params::id::oscType)))
                p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
            if (auto* p = state.getParameter (params::oscId (selected, params::id::oscTable)))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) dsp::kUserTableIndex));
        }
    }
    status = err.isEmpty() ? "LOADED " + f.getFileName().toUpperCase() : "ERR " + err.toUpperCase();
    repaint();
}

//==============================================================================
MixVoiceSection::MixVoiceSection (MysteryBoxProcessor& p) : state (p.getState())
{
    for (auto* t : { &mixTab, &voiceTab })
    {
        t->setRadioGroupId (3001);
        t->setClickingTogglesState (true);
        addAndMakeVisible (t);
    }
    mixTab.onClick = [this] { showPage (0); };
    voiceTab.onClick = [this] { showPage (1); };

    subOn.bind (state, params::id::subOn);         subShape.bind (state, params::id::subShape);
    subOct.bind (state, params::id::subOctave);    subRoute.bind (state, params::id::subRoute);
    subLevel.bind (state, params::id::subLevel);
    noiseOn.bind (state, params::id::noiseOn);     noiseType.bind (state, params::id::noiseType);
    noiseRoute.bind (state, params::id::noiseRoute); noiseLevel.bind (state, params::id::noiseLevel);
    ringOn.bind (state, params::id::ringOn);       ringA.bind (state, params::id::ringA);
    ringB.bind (state, params::id::ringB);         ringRoute.bind (state, params::id::ringRoute);
    ringLevel.bind (state, params::id::ringLevel);
    subLevel.setModTarget (dsp::dest::SubLevel, 1.0f);
    noiseLevel.setModTarget (dsp::dest::NoiseLevel, 1.0f);
    ringLevel.setModTarget (dsp::dest::RingLevel, 1.0f);

    voiceMode.bind (state, params::id::voiceMode); glideMode.bind (state, params::id::glideMode);
    oversampling.bind (state, params::id::oversampling);
    mpe.bind (state, params::id::mpeOn);
    poly.bind (state, params::id::polyphony);      glide.bind (state, params::id::glideTime);
    glideCurve.bind (state, params::id::glideCurve); stackVoices.bind (state, params::id::stackVoices);
    stackDetune.bind (state, params::id::stackDetune); bend.bind (state, params::id::bendRange);
    mpeRange.bind (state, params::id::mpeBendRange); master.bind (state, params::id::masterVolume);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &subOn, &noiseOn, &ringOn, &subShape, &subOct, &subRoute, &noiseType, &noiseRoute, &ringA, &ringB, &ringRoute,
             &subLevel, &noiseLevel, &ringLevel, &voiceMode, &glideMode, &oversampling, &mpe, &poly, &glide, &glideCurve,
             &stackVoices, &stackDetune, &bend, &mpeRange, &master })
        addChildComponent (c);

    mixTab.setToggleState (true, juce::dontSendNotification);
    showPage (0);
}

void MixVoiceSection::showPage (int p)
{
    page = p;
    const bool mix = p == 0;
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &subOn, &noiseOn, &ringOn, &subShape, &subOct, &subRoute, &noiseType, &noiseRoute, &ringA, &ringB, &ringRoute,
             &subLevel, &noiseLevel, &ringLevel })
        c->setVisible (mix);
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &voiceMode, &glideMode, &oversampling, &mpe, &poly, &glide, &glideCurve, &stackVoices, &stackDetune, &bend, &mpeRange, &master })
        c->setVisible (! mix);
    repaint();
}

void MixVoiceSection::paint (juce::Graphics& g)
{
    drawTechPanel (g, getLocalBounds().toFloat(), page == 0 ? "MIXER" : "VOICE", colours::magenta, "SYS.MIX // 0x07");
    if (page == 0)
    {
        g.setColour (colours::magenta.withAlpha (0.2f));
        for (int r = 1; r < 3; ++r)
            g.fillRect (14, 32 + r * 86 - 3, getWidth() - 28, 1);
    }
}

void MixVoiceSection::resized()
{
    mixTab.setBounds (getWidth() - 182, 28, 80, 18);
    voiceTab.setBounds (getWidth() - 98, 28, 80, 18);

    struct Row { ParamToggle* on; ParamChoice* a; ParamChoice* b; ParamChoice* route; ParamKnob* knob; };
    Row rows[] = { { &subOn, &subShape, &subOct, &subRoute, &subLevel }, { &noiseOn, &noiseType, nullptr, &noiseRoute, &noiseLevel },
                   { &ringOn, &ringA, &ringB, &ringRoute, &ringLevel } };
    for (int r = 0; r < 3; ++r)
    {
        const int y = 48 + r * 82;
        rows[r].on->setBounds (14, y + 26, 58, 22);
        rows[r].a->setBounds (80, y + 2, 96, 32);
        if (rows[r].b != nullptr) rows[r].b->setBounds (80, y + 40, 96, 32);
        rows[r].route->setBounds (184, y + 2, 92, 32);
        rows[r].knob->setBounds (288, y - 2, 80, 80);
    }

    voiceMode.setBounds (14, 50, 110, 32);
    glideMode.setBounds (130, 50, 110, 32);
    oversampling.setBounds (246, 50, 124, 32);
    ParamKnob* row1[] = { &poly, &glide, &glideCurve, &stackVoices, &stackDetune };
    for (int k = 0; k < 5; ++k)
        row1[k]->setBounds (14 + k * 71, 92, 70, 92);
    ParamKnob* row2[] = { &bend, &mpeRange, &master };
    for (int k = 0; k < 3; ++k)
        row2[k]->setBounds (14 + k * 71, 190, 70, 92);
    mpe.setBounds (14 + 3 * 71 + 10, 226, 70, 22);
}

//==============================================================================
FilterSection::FilterSection (MysteryBoxProcessor& p, int f)
    : state (p.getState()), index (f), accent (f == 0 ? colours::cyan : colours::magenta), display (p.getState(), f)
{
    on.bind (state, params::filterId (f, params::id::filterOn));
    type.bind (state, params::filterId (f, params::id::filterType));
    if (f == 0)
    {
        routing.bind (state, params::id::filterRouting);
        routing.box.setTooltip ("Serial: F1 -> F2   Parallel: F1 + F2   Split: F1 left / F2 right");
        addAndMakeVisible (routing);
    }
    cutoff.bind (state, params::filterId (f, params::id::filterCutoff));
    res.bind (state, params::filterId (f, params::id::filterRes));
    drive.bind (state, params::filterId (f, params::id::filterDrive));
    env.bind (state, params::filterId (f, params::id::filterEnv));
    key.bind (state, params::filterId (f, params::id::filterKey));
    vel.bind (state, params::filterId (f, params::id::filterVel));
    fm.bind (state, params::filterId (f, params::id::filterFm));
    mix.bind (state, params::filterId (f, params::id::filterMix));
    env.slider.setTooltip ("Envelope 2 amount (+-96 semitones)");
    fm.slider.setTooltip ("Audio-rate cutoff modulation from OSC 1");

    using namespace dsp::dest;
    cutoff.setModTarget (filter (f, Cutoff), 0.8f);
    res.setModTarget (filter (f, Resonance), 1.0f);
    drive.setModTarget (filter (f, Drive), 1.0f);
    mix.setModTarget (filter (f, Mix), 1.0f);
    fm.setModTarget (filter (f, FilterFM), 1.0f);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &display, &on, &type, &cutoff, &res, &drive, &env, &key, &vel, &fm, &mix })
        addAndMakeVisible (c);
    on.getProperties().set ("accent", (juce::int64) accent.getARGB());
}

void FilterSection::paint (juce::Graphics& g)
{
    drawTechPanel (g, getLocalBounds().toFloat(), "FILTER " + juce::String (index + 1), accent, "SYS.VCF // 0x1" + juce::String (index + 1));
}

void FilterSection::resized()
{
    on.setBounds (186, 6, 40, 18);
    type.setBounds (232, 4, 132, 22);
    if (index == 0)
        routing.setBounds (370, 4, 104, 22);
    display.setBounds (14, 30, 186, 130);
    ParamKnob* knobs[] = { &cutoff, &res, &drive, &env, &key, &vel, &fm, &mix };
    for (int k = 0; k < 8; ++k)
        knobs[k]->setBounds (206 + k * 50, 34, 50, 124);
}

//==============================================================================
namespace
{
    class EnvPage final : public juce::Component
    {
    public:
        EnvPage (MysteryBoxProcessor& p, int e)
            : display (p.getState(), e, e == 0 ? colours::magenta : colours::cyan),
              routed (p.getState(), (int) dsp::ModSource::Env1 + e, colours::cyan), isAmp (e == 0)
        {
            auto& s = p.getState();
            const auto accent = e == 0 ? colours::magenta : colours::cyan;
            const char* ids[] = { params::id::envDelay, params::id::envAttack, params::id::envHold, params::id::envDecay, params::id::envSustain,
                                  params::id::envRelease, params::id::envACurve, params::id::envDCurve, params::id::envRCurve, params::id::envVelocity };
            const char* names[] = { "DELAY", "ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE", "A CURVE", "D CURVE", "R CURVE", "VELOCITY" };
            for (int k = 0; k < 10; ++k)
            {
                knobs.push_back (std::make_unique<ParamKnob> (names[k], accent, k >= 6 && k <= 8));
                knobs.back()->bind (s, params::envId (e, ids[k]));
                addAndMakeVisible (*knobs.back());
            }
            if (isAmp)
            {
                knobs.push_back (std::make_unique<ParamKnob> ("PUNCH", colours::amber));
                knobs.back()->bind (s, params::id::env1Punch);
                addAndMakeVisible (*knobs.back());
            }
            loop.bind (s, params::envId (e, params::id::envLoop));
            chip = std::make_unique<ModSourceChip> ((int) dsp::ModSource::Env1 + e, "ENV " + juce::String (e + 1), accent);
            addAndMakeVisible (*chip);
            addAndMakeVisible (display);
            addAndMakeVisible (loop);
            addAndMakeVisible (routed);
        }
        void resized() override
        {
            display.setBounds (0, 0, 340, getHeight());
            for (size_t k = 0; k < knobs.size(); ++k)
                knobs[k]->setBounds (352 + (int) (k % 6) * 72, (int) (k / 6) * 94, 70, 90);
            loop.setBounds (352 + 5 * 72 + 6, 94 + 34, 58, 22);
            chip->setBounds (352 + 5 * 72 + 6, 94 + 64, 96, 22);
            routed.setBounds (868, 0, getWidth() - 868, getHeight());
        }

    private:
        std::unique_ptr<ModSourceChip> chip;
        EnvelopeDisplay display;
        RoutedList routed;
        std::vector<std::unique_ptr<ParamKnob>> knobs;
        ParamToggle loop { "LOOP" };
        bool isAmp;
    };

    class MsegPage final : public juce::Component
    {
    public:
        explicit MsegPage (MysteryBoxProcessor& p)
            : editor (p.getAssets(), ShapeStore::kMsegIndex, colours::amber, "MSEG"),
              routed (p.getState(), (int) dsp::ModSource::Mseg, colours::amber)
        {
            auto& s = p.getState();
            time.bind (s, params::id::msegTime);
            sync.bind (s, params::id::msegSync);
            loop.bind (s, params::id::msegLoop);
            div.bind (s, params::id::msegDiv);
            for (juce::Component* c : std::initializer_list<juce::Component*> { &editor, &time, &sync, &loop, &div, &routed, &chip })
                addAndMakeVisible (c);
        }
        void resized() override
        {
            editor.setBounds (0, 0, 700, getHeight());
            time.setBounds (712, 0, 74, 92);
            sync.setBounds (792, 14, 64, 22);
            loop.setBounds (792, 44, 64, 22);
            div.setBounds (712, 100, 144, 34);
            chip.setBounds (712, 146, 144, 24);
            routed.setBounds (868, 0, getWidth() - 868, getHeight());
        }

    private:
        ModSourceChip chip { (int) dsp::ModSource::Mseg, "MSEG", colours::amber };
        ShapeEditor editor;
        RoutedList routed;
        ParamKnob time { "TIME", colours::amber };
        ParamToggle sync { "SYNC", colours::amber }, loop { "LOOP", colours::amber };
        ParamChoice div { "DIVISION" };
    };

    class LfoPage final : public juce::Component, private juce::Timer
    {
    public:
        LfoPage (MysteryBoxProcessor& p, int l)
            : state (p.getState()), lfo (l), display (p, l),
              editor (p.getAssets(), ShapeStore::lfoIndex (l), colours::lime, "LFO " + juce::String (l + 1) + " CUSTOM"),
              routed (p.getState(), (int) dsp::ModSource::Lfo1 + l, colours::lime)
        {
            shape.bind (state, params::lfoId (l, params::id::lfoShape));
            mode.bind (state, params::lfoId (l, params::id::lfoMode));
            div.bind (state, params::lfoId (l, params::id::lfoDiv));
            sync.bind (state, params::lfoId (l, params::id::lfoSync));
            bipolar.bind (state, params::lfoId (l, params::id::lfoBipolar));
            rate.bind (state, params::lfoId (l, params::id::lfoRate));
            phase.bind (state, params::lfoId (l, params::id::lfoPhase));
            fade.bind (state, params::lfoId (l, params::id::lfoFade));
            smooth.bind (state, params::lfoId (l, params::id::lfoSmooth));
            rate.setModTarget (dsp::dest::LfoRateBase + l, 0.33f);
            chip = std::make_unique<ModSourceChip> ((int) dsp::ModSource::Lfo1 + l, "LFO " + juce::String (l + 1), colours::lime);
            for (juce::Component* c : std::initializer_list<juce::Component*> { &display, &shape, &mode, &div, &sync, &bipolar, &rate, &phase, &fade, &smooth, &routed, chip.get() })
                addAndMakeVisible (c);
            addChildComponent (editor);
            startTimerHz (8);
        }
        void resized() override
        {
            display.setBounds (0, 0, 520, getHeight());
            editor.setBounds (0, 0, 520, getHeight());
            shape.setBounds (532, 0, 120, 34);
            mode.setBounds (658, 0, 100, 34);
            div.setBounds (764, 0, 92, 34);
            sync.setBounds (532, 44, 60, 22);
            bipolar.setBounds (598, 44, 76, 22);
            chip->setBounds (680, 44, 110, 22);
            ParamKnob* k[] = { &rate, &phase, &fade, &smooth };
            for (int i = 0; i < 4; ++i)
                k[i]->setBounds (532 + i * 82, 78, 78, 104);
            routed.setBounds (868, 0, getWidth() - 868, getHeight());
        }
        ParamKnob* rateKnob() { return &rate; }

    private:
        void timerCallback() override
        {
            const bool custom = juce::roundToInt (rawParam (state, params::lfoId (lfo, params::id::lfoShape))) == (int) dsp::LfoShape::Custom;
            editor.setVisible (custom);
            display.setVisible (! custom);
            const bool synced = rawParam (state, params::lfoId (lfo, params::id::lfoSync)) >= 0.5f;
            rate.setAlpha (synced ? 0.35f : 1.0f);
            div.setAlpha (synced ? 1.0f : 0.35f);
        }

        juce::AudioProcessorValueTreeState& state;
        int lfo;
        std::unique_ptr<ModSourceChip> chip;
        LfoDisplay display;
        ShapeEditor editor;
        RoutedList routed;
        ParamChoice shape { "SHAPE" }, mode { "MODE" }, div { "DIVISION" };
        ParamToggle sync { "SYNC" }, bipolar { "BIPOLAR" };
        ParamKnob rate { "RATE", colours::lime }, phase { "PHASE", colours::lime }, fade { "FADE IN", colours::lime },
            smooth { "SMOOTH", colours::lime };
    };

    class MatrixRow final : public juce::Component
    {
    public:
        MatrixRow (juce::AudioProcessorValueTreeState& s, int slotIndex) : slot (slotIndex)
        {
            bypass.bind (s, params::modId (slot, params::id::modBypass));
            bypass.setTooltip ("Bypass this slot");
            src.bind (s, params::modId (slot, params::id::modSrc));
            via.bind (s, params::modId (slot, params::id::modVia));
            amount.bind (s, params::modId (slot, params::id::modAmt));
            curve.bind (s, params::modId (slot, params::id::modCurve));
            dest.bind (s, params::modId (slot, params::id::modDest));
            src.box.setTooltip ("Source");
            via.box.setTooltip ("Via: scales the amount by a second source");
            dest.box.setTooltip ("Destination");
            curve.slider.setTooltip ("Source response curve");
            for (juce::Component* c : std::initializer_list<juce::Component*> { &bypass, &src, &via, &amount, &curve, &dest })
                addAndMakeVisible (c);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour ((slot & 1) ? colours::cyan.withAlpha (0.03f) : juce::Colours::transparentBlack);
            g.fillRect (getLocalBounds());
            g.setFont (fonts::mono (10.0f));
            g.setColour (colours::textDim);
            g.drawText (juce::String (slot + 1).paddedLeft ('0', 2), 0, 0, 22, getHeight(), juce::Justification::centred);
        }
        void resized() override
        {
            const int h = getHeight() - 4;
            bypass.setBounds (24, 2, 30, h);
            src.setBounds (58, 2, 116, h);
            via.setBounds (178, 2, 98, h);
            amount.setBounds (282, 2, 158, h);
            curve.setBounds (446, 2, 50, h);
            dest.setBounds (500, 2, 104, h);
        }

    private:
        int slot;
        ParamToggle bypass { "BYP", colours::amber };
        ParamChoice src { "" }, via { "" }, dest { "" };
        ParamBar amount { colours::magenta, true };
        ParamBar curve { colours::cyan, true, false };
    };

    class MatrixPage final : public juce::Component
    {
    public:
        explicit MatrixPage (juce::AudioProcessorValueTreeState& s)
        {
            for (int k = 0; k < dsp::kNumMatrixSlots; ++k)
            {
                rows.push_back (std::make_unique<MatrixRow> (s, k));
                content.addAndMakeVisible (*rows.back());
            }
            viewport.setViewedComponent (&content, false);
            viewport.setScrollBarsShown (true, false);
            viewport.setScrollBarThickness (6);
            addAndMakeVisible (viewport);
        }
        void paint (juce::Graphics& g) override
        {
            g.setFont (fonts::display (9.0f));
            g.setColour (colours::textDim);
            for (int col = 0; col < 2; ++col)
            {
                const int x = col * 612;
                g.drawText ("#", x, 0, 22, 12, juce::Justification::centred);
                g.drawText ("BYP", x + 24, 0, 30, 12, juce::Justification::centredLeft);
                g.drawText ("SOURCE", x + 58, 0, 110, 12, juce::Justification::centredLeft);
                g.drawText ("VIA", x + 178, 0, 90, 12, juce::Justification::centredLeft);
                g.drawText ("AMOUNT", x + 282, 0, 120, 12, juce::Justification::centredLeft);
                g.drawText ("CURVE", x + 446, 0, 50, 12, juce::Justification::centredLeft);
                g.drawText ("DESTINATION", x + 500, 0, 104, 12, juce::Justification::centredLeft);
            }
        }
        void resized() override
        {
            viewport.setBounds (0, 14, getWidth(), getHeight() - 14);
            constexpr int rowH = 24;
            content.setSize (getWidth() - 8, 16 * rowH);
            for (int k = 0; k < (int) rows.size(); ++k)
                rows[(size_t) k]->setBounds ((k / 16) * 612, (k % 16) * rowH, 606, rowH);
        }
        void scrollToSlot (int slot) { viewport.setViewPosition (0, (slot % 16) * 24); }

    private:
        juce::Viewport viewport;
        juce::Component content;
        std::vector<std::unique_ptr<MatrixRow>> rows;
    };

    class MacroPage final : public juce::Component
    {
    public:
        explicit MacroPage (MysteryBoxProcessor& p) : assets (p.getAssets())
        {
            for (int m = 0; m < dsp::kNumMacros; ++m)
            {
                auto& k = knobs[(size_t) m];
                k = std::make_unique<ParamKnob> ("", m % 2 == 0 ? colours::cyan : colours::magenta);
                k->bind (p.getState(), params::macroId (m));
                addAndMakeVisible (*k);

                auto& n = names[(size_t) m];
                n.setText (assets.getMacroName (m), juce::dontSendNotification);
                n.setEditable (false, true, false);
                n.setJustificationType (juce::Justification::centred);
                n.setFont (fonts::display (11.0f));
                n.setColour (juce::Label::textColourId, colours::text);
                n.setColour (juce::Label::backgroundWhenEditingColourId, colours::track);
                n.setColour (juce::Label::textWhenEditingColourId, colours::cyan);
                n.setTooltip ("Double-click to rename");
                n.onTextChange = [this, m] {
                    assets.setMacroName (m, names[(size_t) m].getText());
                    names[(size_t) m].setText (assets.getMacroName (m), juce::dontSendNotification);
                };
                addAndMakeVisible (n);

                routed[(size_t) m] = std::make_unique<RoutedList> (p.getState(), (int) dsp::ModSource::Macro1 + m, colours::cyan);
                addAndMakeVisible (*routed[(size_t) m]);
                chips[(size_t) m] = std::make_unique<ModSourceChip> ((int) dsp::ModSource::Macro1 + m, "M" + juce::String (m + 1), colours::cyan);
                addAndMakeVisible (*chips[(size_t) m]);
            }
        }
        void resized() override
        {
            const int w = getWidth() / dsp::kNumMacros;
            for (int m = 0; m < dsp::kNumMacros; ++m)
            {
                names[(size_t) m].setBounds (m * w + 4, 0, w - 8, 18);
                knobs[(size_t) m]->setBounds (m * w + (w - 96) / 2, 16, 96, 92);
                chips[(size_t) m]->setBounds (m * w + 20, 110, w - 40, 20);
                routed[(size_t) m]->setBounds (m * w + 4, 134, w - 8, getHeight() - 134);
            }
        }

    private:
        Assets& assets;
        std::array<std::unique_ptr<ParamKnob>, dsp::kNumMacros> knobs;
        std::array<juce::Label, dsp::kNumMacros> names;
        std::array<std::unique_ptr<RoutedList>, dsp::kNumMacros> routed;
        std::array<std::unique_ptr<ModSourceChip>, dsp::kNumMacros> chips;
    };
} // namespace

//==============================================================================
ModSection::ModSection (MysteryBoxProcessor& p) : proc (p)
{
    juce::StringArray names { "ENV 1", "ENV 2", "ENV 3", "ENV 4", "MSEG", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "MATRIX", "MACROS", "ARP/SEQ" };
    for (int i = 0; i < names.size(); ++i)
    {
        auto t = std::make_unique<juce::TextButton> (names[i]);
        t->setRadioGroupId (4001);
        t->setClickingTogglesState (true);
        const auto accent = i < 4 || i == 13 ? colours::magenta : i == 4 ? colours::amber : i < 11 ? colours::lime : colours::cyan;
        t->getProperties().set ("accent", (juce::int64) accent.getARGB());
        t->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (*t);
        tabs.push_back (std::move (t));
    }
    for (int e = 0; e < dsp::kNumEnvelopes; ++e)
        pages.push_back (std::make_unique<EnvPage> (p, e));
    pages.push_back (std::make_unique<MsegPage> (p));
    for (int l = 0; l < dsp::kNumLfos; ++l)
        pages.push_back (std::make_unique<LfoPage> (p, l));
    pages.push_back (std::make_unique<MatrixPage> (p.getState()));
    pages.push_back (std::make_unique<MacroPage> (p));
    pages.push_back (std::make_unique<ArpSeqPage> (p));
    for (auto& pg : pages)
        addChildComponent (*pg);

    tabs[0]->setToggleState (true, juce::dontSendNotification);
    showTab (0);
}

std::vector<ParamKnob*> ModSection::getModKnobs()
{
    std::vector<ParamKnob*> v;
    for (int l = 0; l < dsp::kNumLfos; ++l)
        if (auto* page = dynamic_cast<LfoPage*> (pages[(size_t) (5 + l)].get()))
            v.push_back (page->rateKnob());
    return v;
}

void ModSection::showTab (int index)
{
    current = index;
    tabs[(size_t) index]->setToggleState (true, juce::dontSendNotification);
    for (size_t i = 0; i < pages.size(); ++i)
        pages[i]->setVisible ((int) i == index);
    repaint();
}

void ModSection::addModulation (int destination, int source)
{
    auto& s = proc.getState();
    for (int k = 0; k < dsp::kNumMatrixSlots; ++k)
    {
        const int src = juce::roundToInt (rawParam (s, params::modId (k, params::id::modSrc)));
        const int dst = juce::roundToInt (rawParam (s, params::modId (k, params::id::modDest)));
        if (src == 0 && dst == 0)
        {
            auto set = [&s, k] (const char* id, float v) {
                if (auto* p = s.getParameter (params::modId (k, id)))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 (v));
                    p->endChangeGesture();
                }
            };
            set (params::id::modSrc, (float) source);
            set (params::id::modDest, (float) destination);
            set (params::id::modAmt, 0.25f);
            showTab (kMatrixTab);
            if (auto* mp = dynamic_cast<MatrixPage*> (pages[(size_t) kMatrixTab].get()))
                mp->scrollToSlot (k);
            return;
        }
    }
}

void ModSection::paint (juce::Graphics& g)
{
    const juce::StringArray titles { "ENV 1  //  AMP", "ENV 2  //  FILTER", "ENV 3", "ENV 4", "MSEG", "LFO 1", "LFO 2", "LFO 3", "LFO 4",
                                     "LFO 5", "LFO 6", "MATRIX // 32", "MACROS", "ARP / SEQ" };
    const auto accent = current < 4 || current == 13 ? colours::magenta : current == 4 ? colours::amber : current < 11 ? colours::lime : colours::cyan;
    drawTechPanel (g, getLocalBounds().toFloat(), "", accent, {}, false);
    g.setFont (fonts::mono (9.5f));
    g.setColour (accent.withAlpha (0.7f));
    g.drawText ("SYS.MOD // " + titles[current], getLocalBounds().reduced (18, 8).removeFromTop (14), juce::Justification::centredRight);
}

void ModSection::resized()
{
    for (size_t i = 0; i < tabs.size(); ++i)
        tabs[i]->setBounds (14 + (int) i * 70, 7, 66, 20);
    for (auto& pg : pages)
        pg->setBounds (14, 32, getWidth() - 28, getHeight() - 40);
}

} // namespace mbx::ui
