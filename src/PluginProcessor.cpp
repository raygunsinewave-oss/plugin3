#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace mbx
{
MysteryBoxProcessor::MysteryBoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "MysteryBoxState", params::createLayout()),
      paramCache (apvts)
{
    for (auto& v : shapeVersions) v = 0xffffffffu;
    const char* ids[] = { params::id::arpMode, params::id::arpPattern, params::id::arpRate, params::id::arpGate, params::id::arpSwing,
                          params::id::arpOctaves, params::id::arpHold, params::id::arpRetrigger, params::id::seqLength,
                          params::id::seqRate, params::id::seqSwing, params::id::seqFollow, params::id::midiOut };
    for (size_t i = 0; i < arpP.size(); ++i)
        arpP[i] = apvts.getRawParameterValue (ids[i]);
    engine.prepare (&wavetables, 48000.0f, 48000.0f * 8.0f);
}

MysteryBoxProcessor::~MysteryBoxProcessor()
{
    cancelPendingUpdate();
}

void MysteryBoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    maxBlock = std::max (1, samplesPerBlock);
    loadMeasurer.reset (sampleRate, samplesPerBlock);

    for (size_t k = 1; k < oversamplers.size(); ++k)
    {
        oversamplers[k] = std::make_unique<juce::dsp::Oversampling<float>> (
            2, k, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversamplers[k]->initProcessing ((size_t) maxBlock);
    }

    currentOs = -1;
    const int os = paramCache.oversamplingIndex();
    setLatencySamples (os > 0 ? juce::roundToInt (oversamplers[(size_t) os]->getLatencyInSamples()) : 0);
    engine.prepare (&wavetables, (float) sampleRate, (float) sampleRate * 8.0f);
    paramCache.read (settings);
    engine.setTarget (settings);
    arpSeq.prepare ((float) sampleRate);
    fxRack.prepare (sampleRate, maxBlock);
    outMeter.prepare (sampleRate);
    inMeter.prepare (sampleRate);
    processedMidi.ensureSize (8192);
    sideL.assign ((size_t) maxBlock, 0.0f);
    sideR.assign ((size_t) maxBlock, 0.0f);
}

void MysteryBoxProcessor::releaseResources()
{
    engine.allSoundOff();
}

bool MysteryBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void MysteryBoxProcessor::injectNote (int note, float velocity, bool on) noexcept
{
    const auto w = injectFifo.write (1);
    if (w.blockSize1 > 0)
        injectBuf[(size_t) w.startIndex1] = { note, velocity, on };
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

dsp::ArpSettings MysteryBoxProcessor::readArpSettings() const noexcept
{
    auto f = [this] (size_t i) { return arpP[i]->load (std::memory_order_relaxed); };
    auto r = [&f] (size_t i) { return juce::roundToInt (f (i)); };
    dsp::ArpSettings s;
    s.mode = (dsp::ArpMode) r (0);
    s.pattern = (dsp::ArpPattern) r (1);
    s.rateDivision = r (2);
    s.gate = f (3);
    s.swing = f (4);
    s.octaves = r (5);
    s.hold = f (6) >= 0.5f;
    s.retrigger = f (7) >= 0.5f;
    s.seqLength = r (8);
    s.seqRateDivision = r (9);
    s.seqSwing = f (10);
    s.seqFollowKey = f (11) >= 0.5f;
    return s;
}

void MysteryBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = std::min (buffer.getNumSamples(), maxBlock);
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, numSamples);

    // ---------------------------------------------------------------- sidechain capture
    const float* sl = nullptr;
    const float* sr = nullptr;
    {
        auto in = getBusBuffer (buffer, true, 0);
        const bool active = in.getNumChannels() > 0 && numSamples > 0;
        sidechainActive.store (active, std::memory_order_relaxed);
        if (active)
        {
            std::copy (in.getReadPointer (0), in.getReadPointer (0) + numSamples, sideL.begin());
            const int c1 = in.getNumChannels() > 1 ? 1 : 0;
            std::copy (in.getReadPointer (c1), in.getReadPointer (c1) + numSamples, sideR.begin());
            inMeter.process (sideL.data(), sideR.data(), numSamples);
            sl = sideL.data();
            sr = sideR.data();
        }
    }

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
        triggerAsyncUpdate();
    }

    // ---------------------------------------------------------------- parameters + assets
    paramCache.read (settings);
    for (int k = 0; k < ShapeStore::kNumShapes; ++k)
        assets.shapes.readIfChanged (k, audioShapes[(size_t) k], shapeVersions[(size_t) k]);
    settings.msegShape = &audioShapes[ShapeStore::kMsegIndex];
    for (int l = 0; l < dsp::kNumLfos; ++l)
        settings.lfoCustom[(size_t) l] = &audioShapes[(size_t) ShapeStore::lfoIndex (l)];
    for (int o = 0; o < dsp::kNumOscillators; ++o)
    {
        settings.osc[(size_t) o].userTable = assets.userTables[(size_t) o].get();
        settings.osc[(size_t) o].sample    = assets.samples[(size_t) o].get();
    }
    engine.setTarget (settings);
    assets.sequence.readIfChanged (audioPattern, patternVersion);

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

    // ---------------------------------------------------------------- MIDI: UI notes, arp / sequencer
    const auto arp = readArpSettings();
    processedMidi.clear();
    int nIn = 0;
    {
        const auto rd = injectFifo.read (injectFifo.getNumReady());
        auto take = [&] (int start, int size) {
            for (int i = 0; i < size; ++i)
            {
                const auto& e = injectBuf[(size_t) (start + i)];
                if (arp.mode != dsp::ArpMode::Off && nIn < (int) inNotes.size())
                    inNotes[(size_t) nIn++] = { 0, e.on, e.note, e.velocity, 0 };
                else
                    processedMidi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.velocity) : juce::MidiMessage::noteOff (1, e.note), 0);
            }
        };
        take (rd.startIndex1, rd.blockSize1);
        take (rd.startIndex2, rd.blockSize2);
    }
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int pos = juce::jlimit (0, numSamples - 1, meta.samplePosition);
        if (arp.mode != dsp::ArpMode::Off && (m.isNoteOn() || m.isNoteOff()) && nIn < (int) inNotes.size())
            inNotes[(size_t) nIn++] = { pos, m.isNoteOn(), m.getNoteNumber(), m.getFloatVelocity(), m.getChannel() - 1 };
        else
            processedMidi.addEvent (m, pos);
    }
    if (arp.mode != lastArpMode)
    {
        const int n = arpSeq.flush (outNotes.data(), (int) outNotes.size());
        for (int i = 0; i < n; ++i)
            processedMidi.addEvent (juce::MidiMessage::noteOff (outNotes[(size_t) i].channel + 1, outNotes[(size_t) i].note), 0);
        lastArpMode = arp.mode;
    }
    if (arp.mode != dsp::ArpMode::Off)
    {
        const int n = arpSeq.process (arp, audioPattern, inNotes.data(), nIn, outNotes.data(), (int) outNotes.size(), numSamples, ppq, (float) bpm);
        for (int i = 0; i < n; ++i)
        {
            const auto& e = outNotes[(size_t) i];
            const int ch = juce::jlimit (1, 16, e.channel + 1);
            processedMidi.addEvent (e.on ? juce::MidiMessage::noteOn (ch, e.note, juce::jlimit (0.0f, 1.0f, e.velocity))
                                         : juce::MidiMessage::noteOff (ch, e.note), juce::jlimit (0, numSamples - 1, e.sample));
        }
        engine.setSeqLanes (arpSeq.laneValue (0), arpSeq.laneValue (1));
        seqStep.store (arpSeq.currentStep(), std::memory_order_relaxed);
    }

    // ---------------------------------------------------------------- voices (oversampled)
    const int factor = 1 << os;
    juce::dsp::AudioBlock<float> hostBlock (buffer.getArrayOfWritePointers(), 2, (size_t) numSamples);
    juce::dsp::AudioBlock<float> renderBlock = hostBlock;
    if (os > 0)
        renderBlock = oversamplers[(size_t) os]->processSamplesUp (hostBlock);

    float* l = renderBlock.getChannelPointer (0);
    float* r = renderBlock.getChannelPointer (1);
    int pos = 0;
    for (const auto meta : processedMidi)
    {
        const int evPos = juce::jlimit (0, numSamples, meta.samplePosition);
        renderRange (l, r, pos, evPos, factor);
        handleMidi (meta.getMessage());
        pos = evPos;
    }
    renderRange (l, r, pos, numSamples, factor);
    if (os > 0)
        oversamplers[(size_t) os]->processSamplesDown (hostBlock);

    // ---------------------------------------------------------------- FX rack, EQ, limiter, meters
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);
    dsp::fx::FxContext ctx;
    ctx.sampleRate = (float) hostSampleRate;
    ctx.bpm = (float) bpm;
    ctx.ppq = ppq;
    ctx.sideL = sl;
    ctx.sideR = sr;
    fxRack.process (outL, outR, numSamples, ctx);
    outMeter.process (outL, outR, numSamples);

    scope.push (outL, outR, numSamples);
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

    // ---------------------------------------------------------------- MIDI out
    if (arpP[12]->load (std::memory_order_relaxed) >= 0.5f)
        midi.swapWith (processedMidi); // generated notes (arp / seq / UI) go to the host
    else
        midi.clear();
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
            if (juce::MessageManager::existsAndIsCurrentThread())
                fxRack.syncUnits();
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
