// Factory preset tests + the level-matching tool used after generating presets.
#include "TestHarness.h"

#include "../src/PluginProcessor.h"

using namespace mbx;

namespace
{
/** Plays a short category-appropriate phrase and returns the output peak (linear). */
float renderPhrase (MysteryBoxProcessor& p, const juce::String& category, double& rmsOut, bool& finite)
{
    constexpr int block = 512;
    juce::AudioBuffer<float> buf (2, block);
    std::vector<std::pair<double, std::vector<int>>> events; // (seconds, notes) ; empty = release all
    if (category == "Bass")                             events = { { 0.0, { 36 } }, { 0.6, {} }, { 0.7, { 43 } }, { 1.3, {} }, { 1.4, { 41 } }, { 2.0, {} } };
    else if (category == "Pads" || category == "Ambient" || category == "Drones")
                                                        events = { { 0.0, { 48, 55, 60, 64 } }, { 2.6, {} } };
    else if (category == "Arps & Sequences")            events = { { 0.0, { 48, 52, 55 } }, { 2.4, {} } };
    else if (category == "FX")                          events = { { 0.0, { 60 } }, { 2.5, {} } };
    else                                                events = { { 0.0, { 60, 64, 67 } }, { 0.8, {} }, { 1.0, { 62 } }, { 1.6, {} }, { 1.7, { 67 } }, { 2.3, {} } };

    const double length = 3.2;
    const int blocks = (int) (length * 48000.0 / block);
    float peak = 0.0f;
    double sum = 0.0;
    long count = 0;
    std::vector<int> held;
    size_t next = 0;
    finite = true;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        const double t0 = b * block / 48000.0, t1 = (b + 1) * block / 48000.0;
        while (next < events.size() && events[next].first < t1)
        {
            const int pos = juce::jlimit (0, block - 1, (int) ((events[next].first - t0) * 48000.0));
            for (int n : held) midi.addEvent (juce::MidiMessage::noteOff (1, n), pos);
            held = events[next].second;
            for (int n : held) midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.85f), pos);
            ++next;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float v = buf.getSample (ch, i);
                finite &= std::isfinite (v);
                peak = std::max (peak, std::abs (v));
                sum += (double) v * v;
                ++count;
            }
    }
    rmsOut = std::sqrt (sum / (double) std::max (1L, count));
    return peak;
}

float auditionPreset (MysteryBoxProcessor& p, const juce::var& preset, double& rms, bool& finite)
{
    p.getPresets().apply (preset);
    p.getFxRack().syncUnits();
    p.prepareToPlay (48000.0, 512);
    return renderPhrase (p, preset["category"].toString(), rms, finite);
}
} // namespace

namespace mbx::test
{
/** Renders every preset and rewrites master_volume so the demo phrase peaks near -6 dBFS. */
int levelPresets (const juce::File& bundle)
{
    auto data = juce::JSON::parse (bundle);
    auto* arr = data.getArray();
    if (arr == nullptr) { std::printf ("cannot parse %s\n", bundle.getFullPathName().toRawUTF8()); return 1; }
    auto pPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& p = *pPtr;
    int silent = 0;
    for (int i = 0; i < arr->size(); ++i)
    {
        auto& preset = arr->getReference (i);
        auto* prm = preset["params"].getDynamicObject();
        float vol = prm->hasProperty ("master_volume") ? (float) (double) prm->getProperty ("master_volume") : -6.0f;
        float peakDb = -100.0f;
        for (int pass = 0; pass < 6; ++pass) // iterate: compressors / distortion make the gain non-linear
        {
            prm->setProperty ("master_volume", vol);
            double rms = 0.0;
            bool finite = true;
            const float peak = auditionPreset (p, preset, rms, finite);
            peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
            if (peakDb < -80.0f) break;
            const float err = -6.0f - peakDb;
            if (std::abs (err) < 0.5f) break;
            const float next = juce::jlimit (-36.0f, 6.0f, vol + err * (pass < 3 ? 1.0f : 0.6f));
            if (std::abs (next - vol) < 0.01f) break; // clamped: can't get any closer
            vol = next;
        }
        prm->setProperty ("master_volume", std::round (vol * 100.0f) / 100.0f);
        if (peakDb < -80.0f) ++silent;
        if (i % 50 == 0)
            std::printf ("%4d  %-28s %-16s peak %6.1f dBFS  vol %6.1f dB\n", i, preset["name"].toString().toRawUTF8(),
                         preset["category"].toString().toRawUTF8(), peakDb, vol);
    }
    bundle.replaceWithText (juce::JSON::toString (data, true));
    std::printf ("levelled %d presets (%d silent in the audition phrase)\n", arr->size(), silent);
    return 0;
}
} // namespace mbx::test

MBX_TEST (factory_bank_has_1000_presets_in_the_specified_categories)
{
    auto pPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& p = *pPtr;
    p.getPresets().refresh();
    std::map<juce::String, int> counts;
    int factory = 0;
    for (const auto& e : p.getPresets().getEntries())
        if (e.factory) { ++counts[e.category]; ++factory; }
    MBX_CHECK (factory == 1000, "%d factory presets", factory);
    const std::pair<const char*, int> expected[] = { { "Bass", 180 }, { "Leads", 160 }, { "Pads", 150 }, { "Plucks & Keys", 110 }, { "Winds", 60 },
                                                     { "Ambient", 90 }, { "Drones", 60 }, { "FX", 90 }, { "Arps & Sequences", 60 }, { "Misc", 40 } };
    for (auto& [cat, n] : expected)
        MBX_CHECK (counts[cat] == n, "%s: %d (expected %d)", cat, counts[cat], n);
}

MBX_TEST (factory_presets_load_render_and_are_level_matched)
{
    auto pPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& p = *pPtr;
    p.getPresets().refresh();
    const auto& entries = p.getPresets().getEntries();
    int checked = 0, outOfRange = 0;
    for (size_t i = 0; i < entries.size(); i += 37) // a spread across every category
    {
        if (! entries[i].factory) continue;
        MBX_CHECK (p.getPresets().load (entries[i]), "load %s", entries[i].name.toRawUTF8());
        p.getFxRack().syncUnits();
        p.prepareToPlay (48000.0, 512);
        double rms = 0.0;
        bool finite = true;
        const float peakDb = juce::Decibels::gainToDecibels (renderPhrase (p, entries[i].category, rms, finite), -100.0f);
        MBX_CHECK (finite, "%s produced NaN/Inf", entries[i].name.toRawUTF8());
        if (peakDb < -12.0f || peakDb > -3.0f)
        {
            ++outOfRange;
            std::printf ("         %-28s peak %.1f dBFS\n", entries[i].name.toRawUTF8(), peakDb);
        }
        ++checked;
    }
    std::printf ("         %d presets audited, %d outside -12..-3 dBFS\n", checked, outOfRange);
    MBX_CHECK (outOfRange <= checked / 10, "%d of %d presets are not level matched", outOfRange, checked);
}

MBX_TEST (preset_capture_apply_round_trips)
{
    auto aPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& a = *aPtr;
    juce::Random rng (11);
    for (auto* prm : a.getParameters())
        if (dynamic_cast<juce::AudioParameterFloat*> (prm) != nullptr)
            prm->setValueNotifyingHost (rng.nextFloat());
    a.getAssets().setMacroName (0, "Brightness");
    const auto preset = a.getPresets().capture ("Test", "Bass", { "A" }, "desc", "me");
    const auto json = juce::JSON::toString (preset);
    auto bPtr = std::make_unique<MysteryBoxProcessor>(); // heap: the processor is several MB
    auto& b = *bPtr;
    b.getPresets().apply (juce::JSON::parse (json));
    for (int i = 0; i < a.getParameters().size(); ++i)
    {
        auto* ra = dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[i]);
        auto* rb = dynamic_cast<juce::RangedAudioParameter*> (b.getParameters()[i]);
        if (ra->paramID == "oversampling") continue;
        const float va = ra->convertFrom0to1 (ra->getValue()), vb = rb->convertFrom0to1 (rb->getValue());
        MBX_CHECK (std::abs (va - vb) <= 2.0e-4f * std::max (1.0f, std::abs (va)), "%s: %f vs %f", ra->paramID.toRawUTF8(), va, vb);
    }
    MBX_CHECK (b.getAssets().getMacroName (0) == "BRIGHTNESS");
}
