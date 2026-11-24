#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace mbx
{
MysteryBoxProcessor::MysteryBoxProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "MysteryBoxState", params::createLayout()),
      paramCache (apvts)
{
    for (auto& v : shapeVersions) v = 0xffffffffu;
    engine.prepare (&wavetables, 48000.0f, 48000.0f * 8.0f);
}

MysteryBoxProcessor::~MysteryBoxProcessor()
{
    cancelPendingUpdate();
}

void MysteryBoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    loadMeasurer.reset (sampleRate, samplesPerBlock);

    for (size_t k = 1; k < oversamplers.size(); ++k)
    {
        oversamplers[k] = std::make_unique<juce::dsp::Oversampling<float>> (
            2, k, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversamplers[k]->initProcessing ((size_t) samplesPerBlock);
    }

    currentOs = -1; // forces engine sample-rate refresh on the next block
    const int os = paramCache.oversamplingIndex();
    setLatencySamples (os > 0 ? juce::roundToInt (oversamplers[(size_t) os]->getLatencyInSamples()) : 0);
    engine.prepare (&wavetables, (float) sampleRate, (float) sampleRate * 8.0f);
    paramCache.read (settings);
    engine.setTarget (settings);
}

void MysteryBoxProcessor::releaseResources()
{
    engine.allSoundOff();
}

bool MysteryBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && layouts.getMainInputChannelSet().isDisabled();
}

void MysteryBoxProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    const int ch = juce::jlimit (0, 15, m.getChannel() - 1);
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity(), ch);
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber(), ch);
    else if (m.isPitchWheel())
        engine.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f, ch);
    else if (m.isChannelPressure())
        engine.setChannelPressure ((float) m.getChannelPressureValue() / 127.0f, ch);
    else if (m.isAftertouch())
        engine.setPolyPressure (m.getNoteNumber(), (float) m.getAfterTouchValue() / 127.0f);
    else if (m.isSustainPedalOn())
        engine.setSustainPedal (true);
    else if (m.isSustainPedalOff())
        engine.setSustainPedal (false);
    else if (m.isAllSoundOff())
        engine.allSoundOff();
    else if (m.isAllNotesOff() || m.isResetAllControllers())
        engine.allNotesOff();
    else if (m.isController())
    {
        const float v = (float) m.getControllerValue() / 127.0f;
        switch (m.getControllerNumber())
        {
            case 1:  engine.setModWheel (v); break;
            case 74: engine.setTimbre (v, ch); break; // MPE "slide"
            default: break;
        }
    }
}

void MysteryBoxProcessor::renderRange (float* l, float* r, int start, int end, int factor) noexcept
{
    if (end > start)
        engine.render (l + start * factor, r + start * factor, (end - start) * factor);
}

void MysteryBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, numSamples);

    buffer.clear();
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    const int os = paramCache.oversamplingIndex();
    if (os != currentOs)
    {
        currentOs = os;
        if (os > 0)
            oversamplers[(size_t) os]->reset();
        engine.setSampleRate ((float) (hostSampleRate * (1 << os)));
        triggerAsyncUpdate(); // latency report happens on the message thread
    }

    paramCache.read (settings);
    auto& shapes = assets.shapes;
    for (int k = 0; k < ShapeStore::kNumShapes; ++k)
        shapes.readIfChanged (k, audioShapes[(size_t) k], shapeVersions[(size_t) k]);
    settings.msegShape = &audioShapes[ShapeStore::kMsegIndex];
    for (int l = 0; l < dsp::kNumLfos; ++l)
        settings.lfoCustom[(size_t) l] = &audioShapes[(size_t) ShapeStore::lfoIndex (l)];
    for (int o = 0; o < dsp::kNumOscillators; ++o)
    {
        settings.osc[(size_t) o].userTable = assets.userTables[(size_t) o].get();
        settings.osc[(size_t) o].sample    = assets.samples[(size_t) o].get();
    }
    engine.setTarget (settings);

    double bpm = 120.0, ppq = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            bpm = pos->getBpm().orFallback (120.0);
            if (pos->getIsPlaying())
                if (auto p = pos->getPpqPosition())
                    ppq = *p;
        }
    engine.setTransport ((float) bpm, ppq);
    hostBpm.store (bpm, std::memory_order_relaxed);

    const int factor = 1 << os;
    juce::dsp::AudioBlock<float> hostBlock (buffer);
    juce::dsp::AudioBlock<float> renderBlock = hostBlock;
    if (os > 0)
        renderBlock = oversamplers[(size_t) os]->processSamplesUp (hostBlock); // silent input -> zeroed OS buffer

    float* l = renderBlock.getChannelPointer (0);
    float* r = renderBlock.getChannelPointer (1);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int evPos = juce::jlimit (0, numSamples, meta.samplePosition);
        renderRange (l, r, pos, evPos, factor);
        handleMidi (meta.getMessage());
        pos = evPos;
    }
    renderRange (l, r, pos, numSamples, factor);

    if (os > 0)
        oversamplers[(size_t) os]->processSamplesDown (hostBlock);

    scope.push (buffer.getReadPointer (0), buffer.getReadPointer (1), numSamples);
    activeVoices.store (engine.getActiveVoiceCount(), std::memory_order_relaxed);
    outputPeak.store (buffer.getMagnitude (0, numSamples), std::memory_order_relaxed);
    if (const auto* v = engine.getNewestVoice())
    {
        const auto& mod = v->getLastModulation();
        for (size_t d = 0; d < liveMod.size(); ++d)
            liveMod[d].store (mod[d], std::memory_order_relaxed);
        for (int k = 0; k < dsp::kNumLfos; ++k)
            liveLfo[(size_t) k].store (v->getLfoValue (k), std::memory_order_relaxed);
    }
}

void MysteryBoxProcessor::handleAsyncUpdate()
{
    const int os = currentOs;
    const int latency = os > 0 && oversamplers[(size_t) os] != nullptr
                          ? juce::roundToInt (oversamplers[(size_t) os]->getLatencyInSamples())
                          : 0;
    setLatencySamples (latency);
}

void MysteryBoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", kStateVersion, nullptr);
    state.removeChild (state.getChildWithName (Assets::kTreeType), nullptr);
    state.appendChild (assets.toValueTree(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MysteryBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            auto extra = tree.getChildWithName (Assets::kTreeType);
            if (extra.isValid())
            {
                assets.fromValueTree (extra);
                tree.removeChild (extra, nullptr);
            }
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* MysteryBoxProcessor::createEditor()
{
    return new MysteryBoxEditor (*this);
}

} // namespace mbx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mbx::MysteryBoxProcessor();
}
