#include "Assets.h"

namespace mbx
{
juce::String ShapeStore::serialise (const dsp::Shape& s)
{
    juce::StringArray parts;
    for (int i = 0; i < s.numPoints; ++i)
    {
        const auto& p = s.points[(size_t) i];
        parts.add (juce::String (p.x, 4) + "," + juce::String (p.y, 4) + "," + juce::String (p.curve, 3));
    }
    return parts.joinIntoString (";");
}

dsp::Shape ShapeStore::parse (const juce::String& text, const dsp::Shape& fallback)
{
    auto parts = juce::StringArray::fromTokens (text, ";", "");
    parts.removeEmptyStrings();
    if (parts.size() < 2)
        return fallback;
    dsp::Shape s;
    s.numPoints = 0;
    for (const auto& part : parts)
    {
        auto v = juce::StringArray::fromTokens (part, ",", "");
        if (v.size() != 3 || s.numPoints >= dsp::Shape::kMaxPoints)
            continue;
        s.points[(size_t) s.numPoints++] = { v[0].getFloatValue(), v[1].getFloatValue(), v[2].getFloatValue() };
    }
    if (s.numPoints < 2)
        return fallback;
    s.sanitise();
    return s;
}

//==============================================================================
Assets::Assets()
{
    formats.registerBasicFormats();
    for (int m = 0; m < dsp::kNumMacros; ++m)
        macroNames.add ("MACRO " + juce::String (m + 1));
    startTimer (1000);
}

Assets::~Assets()
{
    stopTimer();
}

void Assets::timerCallback()
{
    for (auto& t : userTables) t.collectGarbage();
    for (auto& s : samples) s.collectGarbage();
}

juce::String Assets::getMacroName (int m) const
{
    const juce::ScopedLock sl (nameLock);
    return macroNames[m];
}

void Assets::setMacroName (int m, const juce::String& name)
{
    const juce::ScopedLock sl (nameLock);
    if (juce::isPositiveAndBelow (m, macroNames.size()))
        macroNames.set (m, name.substring (0, 24).toUpperCase());
}

std::unique_ptr<juce::AudioFormatReader> Assets::openReader (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return nullptr;
    }
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        error = "Unsupported audio file: " + file.getFileName();
    else if (reader->lengthInSamples < 16)
    {
        error = "File is too short";
        reader.reset();
    }
    return reader;
}

static std::vector<float> readMono (juce::AudioFormatReader& reader, juce::int64 maxSamples)
{
    const int len = (int) std::min<juce::int64> (reader.lengthInSamples, maxSamples);
    juce::AudioBuffer<float> buf ((int) reader.numChannels, len);
    reader.read (&buf, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        juce::FloatVectorOperations::addWithMultiply (mono.data(), buf.getReadPointer (c), 1.0f / (float) buf.getNumChannels(), len);
    return mono;
}

juce::String Assets::loadWavetable (int osc, const juce::File& file)
{
    juce::String error;
    auto reader = openReader (file, error);
    if (reader == nullptr)
        return error;

    constexpr int frameLen = dsp::Wavetable::kSize;
    const auto mono = readMono (*reader, (juce::int64) frameLen * 256);
    const int len = (int) mono.size();

    std::vector<std::vector<float>> frames;
    if (len >= frameLen && len % frameLen == 0)
    {
        for (int f = 0; f < len / frameLen; ++f)
            frames.emplace_back (mono.begin() + f * frameLen, mono.begin() + (f + 1) * frameLen);
    }
    else if (len <= 8192)
        frames.push_back (mono); // single cycle of any length
    else
    {
        const int count = std::min (64, len / frameLen);
        const int step  = len / count;
        for (int f = 0; f < count; ++f)
            frames.emplace_back (mono.begin() + f * step, mono.begin() + f * step + frameLen);
    }

    auto table = std::make_unique<dsp::Wavetable>();
    table->build (frames, file.getFileNameWithoutExtension().toStdString());
    userTables[(size_t) osc].publish (std::move (table), file.getFullPathName());
    return {};
}

void Assets::setDrawnWavetable (int osc, const std::vector<float>& cycle)
{
    drawnCycles[(size_t) osc] = cycle;
    auto table = std::make_unique<dsp::Wavetable>();
    table->build ({ cycle }, "Drawn");
    userTables[(size_t) osc].publish (std::move (table), "drawn:" + juce::String (osc));
}

juce::String Assets::loadSample (int osc, const juce::File& file)
{
    juce::String error;
    auto reader = openReader (file, error);
    if (reader == nullptr)
        return error;

    auto sample = std::make_unique<dsp::SampleData>();
    sample->sampleRate = (float) reader->sampleRate;
    sample->data = readMono (*reader, (juce::int64) (reader->sampleRate * 60.0)); // up to 60 s
    float peak = 0.0f;
    for (auto v : sample->data) peak = std::max (peak, std::abs (v));
    if (peak > 1.0e-6f)
        for (auto& v : sample->data) v /= peak;
    sample->name = file.getFileNameWithoutExtension().toStdString();
    samples[(size_t) osc].publish (std::move (sample), file.getFullPathName());
    return {};
}

juce::ValueTree Assets::toValueTree() const
{
    juce::ValueTree t (kTreeType);
    t.setProperty ("mseg", ShapeStore::serialise (shapes.get (ShapeStore::kMsegIndex)), nullptr);
    for (int l = 0; l < dsp::kNumLfos; ++l)
        t.setProperty ("lfo" + juce::String (l + 1) + "_custom", ShapeStore::serialise (shapes.get (ShapeStore::lfoIndex (l))), nullptr);
    for (int m = 0; m < dsp::kNumMacros; ++m)
        t.setProperty ("macro" + juce::String (m + 1) + "_name", getMacroName (m), nullptr);
    for (int o = 0; o < dsp::kNumOscillators; ++o)
    {
        const auto wtSource = userTables[(size_t) o].getSourcePath();
        t.setProperty ("osc" + juce::String (o + 1) + "_wavetable", wtSource, nullptr);
        if (wtSource.startsWith ("drawn:") && ! drawnCycles[(size_t) o].empty())
        {
            juce::MemoryBlock mb (drawnCycles[(size_t) o].data(), drawnCycles[(size_t) o].size() * sizeof (float));
            t.setProperty ("osc" + juce::String (o + 1) + "_drawn", mb.toBase64Encoding(), nullptr);
        }
        t.setProperty ("osc" + juce::String (o + 1) + "_sample", samples[(size_t) o].getSourcePath(), nullptr);
    }
    return t;
}

void Assets::fromValueTree (const juce::ValueTree& t)
{
    if (! t.hasType (kTreeType))
        return;
    shapes.set (ShapeStore::kMsegIndex, ShapeStore::parse (t["mseg"], dsp::Shape::defaultMseg()));
    for (int l = 0; l < dsp::kNumLfos; ++l)
        shapes.set (ShapeStore::lfoIndex (l), ShapeStore::parse (t["lfo" + juce::String (l + 1) + "_custom"], dsp::Shape::defaultCustom()));
    for (int m = 0; m < dsp::kNumMacros; ++m)
        if (t.hasProperty ("macro" + juce::String (m + 1) + "_name"))
            setMacroName (m, t["macro" + juce::String (m + 1) + "_name"]);
    for (int o = 0; o < dsp::kNumOscillators; ++o)
    {
        const juce::String wt = t["osc" + juce::String (o + 1) + "_wavetable"];
        const juce::String sm = t["osc" + juce::String (o + 1) + "_sample"];
        if (wt.startsWith ("drawn:"))
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (t["osc" + juce::String (o + 1) + "_drawn"].toString()) && mb.getSize() >= 16 * sizeof (float))
            {
                std::vector<float> cycle (mb.getSize() / sizeof (float));
                std::memcpy (cycle.data(), mb.getData(), cycle.size() * sizeof (float));
                setDrawnWavetable (o, cycle);
            }
        }
        else if (wt.isNotEmpty() && juce::File::isAbsolutePath (wt))
            loadWavetable (o, juce::File (wt));
        else if (wt.isEmpty())
            userTables[(size_t) o].clear();
        if (sm.isNotEmpty() && juce::File::isAbsolutePath (sm))
            loadSample (o, juce::File (sm));
        else if (sm.isEmpty())
            samples[(size_t) o].clear();
    }
}

} // namespace mbx
