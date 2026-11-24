#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace mbx
{
MysteryBoxProcessor::MysteryBoxProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "MysteryBoxState", params::createLayout()),
      paramCache (apvts)
{
    engine.prepare (&wavetables, 48000.0f);
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
    engine.prepare (&wavetables, (float) sampleRate);
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
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        engine.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isSustainPedalOn())
        engine.setSustainPedal (true);
    else if (m.isSustainPedalOff())
        engine.setSustainPedal (false);
    else if (m.isAllSoundOff())
        engine.allSoundOff();
    else if (m.isAllNotesOff() || m.isResetAllControllers())
        engine.allNotesOff();
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
    engine.setTarget (settings);

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
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MysteryBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
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
