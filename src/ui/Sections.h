// SYNTH-tab sections. Each is a self-contained, parameter-bound component.
#pragma once

#include "Displays.h"
#include "ShapeEditor.h"
#include "Widgets.h"

namespace mbx::ui
{
/** Text readout listing which matrix slots a modulation source feeds. */
class RoutedList final : public juce::Component, private juce::Timer
{
public:
    RoutedList (juce::AudioProcessorValueTreeState& s, int sourceIndex, juce::Colour c) : state (s), source (sourceIndex), accent (c)
    {
        startTimerHz (5);
    }
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;
    juce::AudioProcessorValueTreeState& state;
    int source;
    juce::Colour accent;
    juce::StringArray lines;
};

//==============================================================================
class OscSection final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit OscSection (MysteryBoxProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }

    std::vector<ParamKnob*> getModKnobs();
    int getSelectedOscillator() const noexcept { return selected; }
    std::function<void (int destination)> onAddModulation;

private:
    void selectOscillator (int index);
    void loadFile (const juce::File& f);
    void timerCallback() override;
    void layoutForType();

    MysteryBoxProcessor& proc;
    juce::AudioProcessorValueTreeState& state;
    int selected = 0;
    bool dragHover = false;
    juce::String status;
    std::unique_ptr<juce::FileChooser> chooser;

    std::array<juce::TextButton, 6> tabs;
    std::array<std::unique_ptr<ParamToggle>, 6> power;
    WaveDisplay display;
    ParamChoice type { "TYPE" }, shape { "SHAPE" }, table { "TABLE" }, noise { "NOISE" }, octave { "OCT" }, route { "ROUTE" },
        warp { "WARP" }, modSrc { "SOURCE" }, scanDiv { "DIV" }, scanLoop { "LOOP" };
    ParamToggle scanOn { "SCAN", colours::lime }, scanSync { "SYNC", colours::lime };
    juce::TextButton loadButton { "LOAD" }, drawButton { "DRAW" };
    ParamKnob coarse { "COARSE", colours::cyan, true }, fine { "FINE", colours::cyan, true }, level { "LEVEL" },
        pan { "PAN", colours::cyan, true }, phase { "PHASE" }, rand { "RAND" }, unison { "UNISON", colours::magenta },
        detune { "DETUNE", colours::magenta }, blend { "BLEND", colours::magenta }, spread { "SPREAD", colours::magenta },
        wtPos { "WT POS", colours::lime }, pw { "PW", colours::lime }, warpAmt { "WARP", colours::amber },
        scanRate { "RATE", colours::lime }, scanStart { "START", colours::lime }, scanMid { "MID", colours::lime },
        scanEnd { "END", colours::lime }, gSize { "SIZE", colours::lime }, gDensity { "DENSITY", colours::lime },
        gSpray { "SPRAY", colours::lime }, gSpread { "WIDTH", colours::lime };
    int lastType = -1;
};

//==============================================================================
class MixVoiceSection final : public juce::Component
{
public:
    explicit MixVoiceSection (MysteryBoxProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::vector<ParamKnob*> getModKnobs() { return { &subLevel, &noiseLevel, &ringLevel }; }

private:
    void showPage (int page);
    juce::AudioProcessorValueTreeState& state;
    juce::TextButton mixTab { "MIXER" }, voiceTab { "VOICE" };
    int page = 0;

    ParamToggle subOn { "SUB" }, noiseOn { "NOISE" }, ringOn { "RING" };
    ParamChoice subShape { "SHAPE" }, subOct { "OCTAVE" }, subRoute { "ROUTE" }, noiseType { "TYPE" }, noiseRoute { "ROUTE" },
        ringA { "SOURCE A" }, ringB { "SOURCE B" }, ringRoute { "ROUTE" };
    ParamKnob subLevel { "LEVEL" }, noiseLevel { "LEVEL" }, ringLevel { "LEVEL" };

    ParamChoice voiceMode { "VOICE MODE" }, glideMode { "GLIDE MODE" }, oversampling { "OVERSAMPLING" };
    ParamToggle mpe { "MPE", colours::magenta };
    ParamKnob poly { "VOICES" }, glide { "GLIDE", colours::magenta }, glideCurve { "CURVE", colours::magenta },
        stackVoices { "UNI VOX", colours::magenta }, stackDetune { "UNI DET", colours::magenta }, bend { "BEND" },
        mpeRange { "MPE RNG", colours::magenta }, master { "MASTER", colours::lime };
};

//==============================================================================
class FilterSection final : public juce::Component
{
public:
    FilterSection (MysteryBoxProcessor& p, int filterIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::vector<ParamKnob*> getModKnobs() { return { &cutoff, &res, &drive, &mix, &fm }; }

private:
    juce::AudioProcessorValueTreeState& state;
    int index;
    juce::Colour accent;
    FilterDisplay display;
    ParamToggle on { "ON" };
    ParamChoice type { "" }, routing { "" };
    ParamKnob cutoff { "CUTOFF" }, res { "RESO" }, drive { "DRIVE" }, env { "ENV", colours::magenta, true }, key { "KEY" },
        vel { "VEL" }, fm { "FM", colours::amber }, mix { "MIX" };
};

//==============================================================================
class ModSection final : public juce::Component
{
public:
    explicit ModSection (MysteryBoxProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void showTab (int index);
    std::vector<ParamKnob*> getModKnobs();

    /** Puts `destination` into the first free matrix slot and shows the matrix. */
    void addModulation (int destination, int source = (int) dsp::ModSource::Lfo1);

    static constexpr int kMatrixTab = 11, kMacroTab = 12;

private:
    MysteryBoxProcessor& proc;
    std::vector<std::unique_ptr<juce::TextButton>> tabs;
    std::vector<std::unique_ptr<juce::Component>> pages;
    int current = 0;
};

} // namespace mbx::ui
