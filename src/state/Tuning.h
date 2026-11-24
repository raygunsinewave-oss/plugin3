// Scala microtuning: .scl scale + optional .kbm keyboard mapping -> 128 per-note
// semitone offsets relative to 12-TET (consumed lock-free by the engine).
#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace mbx
{
class Tuning
{
public:
    Tuning() { reset(); }

    /** Audio thread: pointer to 128 offsets (stable storage, values updated atomically per note). */
    const float* offsets() const noexcept { return active.load (std::memory_order_acquire) ? table.data() : nullptr; }

    void reset()
    {
        active.store (false);
        for (auto& v : table) v = 0.0f;
        sclText.clear();
        kbmText.clear();
        name = "12-TET";
    }

    /** Parses and applies. Returns an error string (empty on success). Message thread. */
    juce::String load (const juce::String& scl, const juce::String& kbm)
    {
        std::vector<double> cents; // degrees 1..N (last = period)
        juce::String desc;
        if (auto err = parseScl (scl, cents, desc); err.isNotEmpty())
            return err;

        int mapSize = (int) cents.size(), first = 0, last = 127, middle = 60, refNote = 69, octaveDegree = (int) cents.size();
        double refFreq = 440.0;
        std::vector<int> mapping;
        if (kbm.isNotEmpty())
        {
            auto v = dataLines (kbm);
            if (v.size() < 7)
                return "KBM: expected at least 7 values";
            mapSize = v[0].getIntValue(); first = v[1].getIntValue(); last = v[2].getIntValue(); middle = v[3].getIntValue();
            refNote = v[4].getIntValue(); refFreq = v[5].getDoubleValue(); octaveDegree = v[6].getIntValue();
            for (size_t i = 7; i < v.size() && (int) mapping.size() < mapSize; ++i)
                mapping.push_back (v[i].trim().startsWithIgnoreCase ("x") ? -1 : v[i].getIntValue());
        }
        else
        {
            middle = 60; // 1/1 on middle C, A4 stays at 440 Hz
            refNote = 69;
            refFreq = 440.0;
        }

        active.store (false, std::memory_order_release); // engine falls back to 12-TET while the table is rewritten
        const int n = (int) cents.size();
        auto degreeCents = [&] (int degree) {
            const int oct = (int) std::floor ((double) degree / n);
            const int idx = degree - oct * n;
            return oct * cents.back() + (idx == 0 ? 0.0 : cents[(size_t) idx - 1]);
        };
        auto noteCents = [&] (int note, bool& ok) {
            ok = true;
            if (mapping.empty() || mapSize <= 0)
                return degreeCents (note - middle);
            const int rel = note - middle;
            const int oct = (int) std::floor ((double) rel / mapSize);
            const int idx = rel - oct * mapSize;
            const int deg = idx < (int) mapping.size() ? mapping[(size_t) idx] : -1;
            if (deg < 0) { ok = false; return 0.0; }
            const double period = octaveDegree > 0 && octaveDegree <= n ? (octaveDegree == n ? cents.back() : cents[(size_t) octaveDegree - 1]) : cents.back();
            return oct * period + (deg == 0 ? 0.0 : degreeCents (deg));
        };

        bool ok = true;
        const double refCents = noteCents (refNote, ok);
        for (int note = 0; note < 128; ++note)
        {
            bool mapped = true;
            const double c = noteCents (note, mapped);
            if (! mapped || note < first || note > last)
            {
                table[(size_t) note] = 0.0f;
                continue;
            }
            const double hz = refFreq * std::pow (2.0, (c - refCents) / 1200.0);
            table[(size_t) note] = (float) (12.0 * std::log2 (hz / 440.0) + 69.0 - note);
        }
        sclText = scl;
        kbmText = kbm;
        name = desc.isNotEmpty() ? desc : juce::String (n) + "-note scale";
        active.store (true, std::memory_order_release);
        return {};
    }

    juce::String getName() const { return name; }
    juce::String getScl() const { return sclText; }
    juce::String getKbm() const { return kbmText; }

private:
    static std::vector<juce::String> dataLines (const juce::String& text)
    {
        std::vector<juce::String> out;
        for (auto line : juce::StringArray::fromLines (text))
        {
            line = line.trim();
            if (line.isEmpty() || line.startsWith ("!"))
                continue;
            out.push_back (line.upToFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf ("\t", false, false));
        }
        return out;
    }

    static juce::String parseScl (const juce::String& text, std::vector<double>& cents, juce::String& desc)
    {
        juce::StringArray lines;
        for (auto line : juce::StringArray::fromLines (text))
            if (! line.trim().startsWith ("!"))
                lines.add (line);
        if (lines.size() < 2)
            return "SCL: file too short";
        desc = lines[0].trim();
        const int count = lines[1].trim().getIntValue();
        if (count <= 0 || count > 1024)
            return "SCL: bad note count";
        for (int i = 2; i < lines.size() && (int) cents.size() < count; ++i)
        {
            const auto tok = lines[i].trim().upToFirstOccurrenceOf (" ", false, false);
            if (tok.isEmpty())
                continue;
            if (tok.containsChar ('.'))
                cents.push_back (tok.getDoubleValue());
            else
            {
                const double num = tok.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
                const double den = tok.containsChar ('/') ? tok.fromFirstOccurrenceOf ("/", false, false).getDoubleValue() : 1.0;
                if (num <= 0.0 || den <= 0.0)
                    return "SCL: bad ratio '" + tok + "'";
                cents.push_back (1200.0 * std::log2 (num / den));
            }
        }
        if ((int) cents.size() != count)
            return "SCL: expected " + juce::String (count) + " pitches";
        return {};
    }

    std::array<float, 128> table {};
    std::atomic<bool> active { false };
    juce::String sclText, kbmText, name;
};

} // namespace mbx
