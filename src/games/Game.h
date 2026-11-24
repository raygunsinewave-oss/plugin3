// XD games framework. Games run on the GUI thread, speak in scale degrees, and are turned
// into tempo-synced, key-locked, optionally quantized notes by the GameHost.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace mbx::games
{
inline const juce::StringArray& scaleNames()
{
    static const juce::StringArray n { "Major", "Minor", "Dorian", "Mixolydian", "Lydian", "Phrygian", "Harmonic Minor",
                                       "Major Pentatonic", "Minor Pentatonic", "Blues", "Chromatic" };
    return n;
}

class GameHost
{
public:
    std::function<void (int note, float velocity, bool on)> send;
    int scale = 1, key = 9, octave = 3;   // A minor, from A3
    float quantize = 0.5f;                // 0 = immediate, 1 = snap to the next 16th
    double beat = 0.0;                    // continuous beat clock
    float bpm = 120.0f;

    int degreeToNote (int degree) const
    {
        static const std::vector<std::vector<int>> scales {
            { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 5, 7, 9, 10 },
            { 0, 2, 4, 6, 7, 9, 11 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 4, 7, 9 },
            { 0, 3, 5, 7, 10 }, { 0, 3, 5, 6, 7, 10 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
        const auto& s = scales[(size_t) juce::jlimit (0, (int) scales.size() - 1, scale)];
        const int n = (int) s.size();
        const int oct = (int) std::floor ((double) degree / n);
        const int idx = degree - oct * n;
        return juce::jlimit (0, 127, 12 * (octave + 1) + key + 12 * oct + s[(size_t) idx]);
    }

    /** Schedules a note on (quantized toward the 16th grid) and its note-off `lengthBeats` later. */
    void play (int degree, float velocity, double lengthBeats)
    {
        const int n = degreeToNote (degree);
        const double grid = 0.25;
        const double next = std::ceil (beat / grid - 1.0e-9) * grid;
        const double at = beat + (next - beat) * quantize;
        events.push_back ({ at, n, juce::jlimit (0.05f, 1.0f, velocity), true });
        events.push_back ({ at + std::max (0.05, lengthBeats), n, 0.0f, false });
    }

    /** Advances the clock and fires due events. */
    void advance (double newBeat)
    {
        beat = newBeat;
        std::stable_sort (events.begin(), events.end(), [] (const Ev& a, const Ev& b) { return a.at < b.at; });
        size_t k = 0;
        for (; k < events.size() && events[k].at <= beat; ++k)
        {
            const auto& e = events[k];
            if (e.on)
            {
                if (held[(size_t) e.note] > 0) send (e.note, 0.0f, false); // retrigger cleanly
                ++held[(size_t) e.note];
                send (e.note, e.velocity, true);
            }
            else if (held[(size_t) e.note] > 0 && --held[(size_t) e.note] == 0)
                send (e.note, 0.0f, false);
        }
        events.erase (events.begin(), events.begin() + (long) k);
    }

    void allOff()
    {
        events.clear();
        for (int n = 0; n < 128; ++n)
            if (held[(size_t) n] > 0) { held[(size_t) n] = 0; if (send) send (n, 0.0f, false); }
    }

private:
    struct Ev { double at; int note; float velocity; bool on; };
    std::vector<Ev> events;
    std::array<int, 128> held {};
};

class Game
{
public:
    virtual ~Game() = default;
    virtual const char* name() const = 0;
    virtual const char* help() const = 0;
    virtual void reset() = 0;
    virtual void update (float dt) = 0;                       // seconds
    virtual void draw (juce::Graphics& g, juce::Rectangle<float> area) = 0;
    virtual void keyDown (int) {}
    virtual void keyUp (int) {}
    virtual void click (juce::Point<float>, bool /*secondary*/) {} // normalised 0..1 coordinates
    virtual void drag (juce::Point<float>) {}
    virtual void autoplay (float) {}                          // performance mode AI
    virtual void tick (int /*sixteenth*/) {}                  // host-synced 16th-note clock

    int score = 0;
    bool over = false;
    std::array<float, 4> mods {};                             // "Mod Out" 0..1 -> mod matrix sources Game 1..4
    GameHost* host = nullptr;

protected:
    void note (int degree, float vel = 0.8f, double beats = 0.5) { if (host) host->play (degree, vel, beats); }
    void chord (int degree, float vel = 0.7f, double beats = 1.0, int size = 3)
    {
        for (int k = 0; k < size; ++k) note (degree + 2 * k, vel, beats);
    }
    void arp (int degree, int count, float vel = 0.7f)
    {
        for (int k = 0; k < count; ++k) note (degree + 2 * k, vel, 0.25);
    }
    juce::Random rnd;
};

/** Number of games and factory. */
int numGames();
std::unique_ptr<Game> createGame (int index);

// ---------------------------------------------------------------- shared drawing helpers
juce::Colour gameColour (int i);
void neonRect (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float glow = 1.0f);
void neonCircle (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour col, bool fill = true);
void gameText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float size, juce::Colour c,
               juce::Justification j = juce::Justification::centred);

} // namespace mbx::games
