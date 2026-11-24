// XD games 10-17.
#include "Game.h"
#include "../ui/Theme.h"

namespace mbx::games
{
using K = juce::KeyPress;
using P = juce::Point<float>;
using R = juce::Rectangle<float>;

namespace
{
    //==========================================================================
    // 10. Echo Memory
    class EchoMemory final : public Game
    {
    public:
        const char* name() const override { return "Echo Memory"; }
        const char* help() const override { return "Listen, then repeat the melody: click the pads or press 1-8.\nGreat ear training: the pads are the scale degrees. Mod 1 = sequence length."; }
        void reset() override { seq.clear(); pos = 0; score = 0; over = false; addStep(); listening = false; playIdx = 0; wait = 1.0f; }
        void click (P p, bool) override { const int c = juce::jlimit (0, 3, (int) (p.x * 4)), r = juce::jlimit (0, 1, (int) (p.y * 2)); press (r * 4 + c); }
        void keyDown (int k) override { if (k >= '1' && k <= '8') press (k - '1'); if (over && k == K::spaceKey) reset(); }
        void autoplay (float dt) override { if (over) { reset(); return; } if (listening) { aiT += dt; if (aiT > 0.35f) { aiT = 0; press (seq[(size_t) pos]); } } }
        void update (float dt) override
        {
            flash = std::max (0.0f, flash - dt * 3.0f);
            mods[0] = std::min (1.0f, seq.size() / 16.0f);
            if (over || listening) return;
            wait -= dt;
            if (wait > 0) return;
            if (playIdx < (int) seq.size()) { lit = seq[(size_t) playIdx++]; flash = 1.0f; note (lit, 0.8f, 0.5); wait = 0.45f; }
            else { listening = true; pos = 0; }
        }
        void draw (juce::Graphics& g, R a) override
        {
            for (int i = 0; i < 8; ++i)
            {
                const R cell (a.getX() + (i % 4) * a.getWidth() / 4 + 8, a.getY() + (i / 4) * a.getHeight() / 2 + 8, a.getWidth() / 4 - 16, a.getHeight() / 2 - 16);
                neonRect (g, cell, gameColour (i), i == lit && flash > 0 ? 3.0f : 0.4f);
                if (i != lit || flash <= 0) { g.setColour (ui::colours::base.withAlpha (0.55f)); g.fillRect (cell.reduced (3)); }
                gameText (g, juce::String (i + 1), cell, 22.0f, ui::colours::text);
            }
            gameText (g, over ? "WRONG NOTE  //  SPACE" : listening ? "YOUR TURN" : "LISTEN...", a.removeFromTop (24), 14.0f, over ? ui::colours::amber : ui::colours::lime);
        }

    private:
        void press (int i)
        {
            if (! listening || over) return;
            lit = i; flash = 1.0f; note (i, 0.8f, 0.4);
            if (seq[(size_t) pos] != i) { over = true; chord (-7, 0.8f, 2.0); return; }
            if (++pos >= (int) seq.size()) { score = (int) seq.size(); addStep(); listening = false; playIdx = 0; wait = 1.0f; chord (0, 0.5f, 0.5); }
        }
        void addStep() { seq.push_back (rnd.nextInt (8)); }
        std::vector<int> seq;
        int pos = 0, playIdx = 0, lit = -1;
        bool listening = false;
        float wait = 1.0f, flash = 0.0f, aiT = 0.0f;
    };

    //==========================================================================
    // 11. Rhythm Lanes
    class RhythmLanes final : public Game
    {
    public:
        const char* name() const override { return "Rhythm Lanes"; }
        const char* help() const override { return "Notes fall on the tempo grid. Hit D F J K as they cross the line;\nhits play the current preset. Mod 1 = accuracy streak, Mod 2 = last lane."; }
        void reset() override { notes.clear(); score = 0; over = false; streak = 0; }
        void keyDown (int k) override
        {
            const char keys[] = { 'D', 'F', 'J', 'K' };
            for (int l = 0; l < 4; ++l) if (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k) == keys[l]) hitLane (l);
        }
        void autoplay (float) override { for (auto& n : notes) if (! n.hit && n.y > 0.84f && n.y < 0.9f) hitLane (n.lane); }
        void tick (int s) override
        {
            static const int pattern[16] = { 1, 0, 2, 0, 3, 1, 0, 2, 1, 0, 3, 0, 2, 1, 0, 3 };
            if (pattern[s % 16] && rnd.nextFloat() < 0.8f) notes.push_back ({ (pattern[s % 16] + rnd.nextInt (2)) % 4, 0.0f, false });
        }
        void update (float dt) override
        {
            const float speed = host != nullptr ? host->bpm / 60.0f * 0.25f : 0.5f;
            for (auto& n : notes) { n.y += dt * speed; if (! n.hit && n.y > 0.97f) { n.hit = true; streak = 0; } }
            notes.erase (std::remove_if (notes.begin(), notes.end(), [] (auto& n) { return n.y > 1.05f; }), notes.end());
            mods[0] = std::min (1.0f, streak / 20.0f);
            for (auto& f : flash) f = std::max (0.0f, f - dt * 4.0f);
        }
        void draw (juce::Graphics& g, R a) override
        {
            const float lw = a.getWidth() / 4.0f;
            for (int l = 0; l < 4; ++l)
            {
                g.setColour (gameColour (l).withAlpha (0.06f + 0.3f * flash[(size_t) l]));
                g.fillRect (R (a.getX() + l * lw + 2, a.getY(), lw - 4, a.getHeight()));
                gameText (g, juce::String::charToString ("DFJK"[l]), R (a.getX() + l * lw, a.getBottom() - 24, lw, 20), 14.0f, ui::colours::textDim);
            }
            g.setColour (ui::colours::text);
            g.fillRect (R (a.getX(), a.getY() + 0.87f * a.getHeight(), a.getWidth(), 2));
            for (auto& n : notes)
                if (! n.hit) neonRect (g, { a.getX() + n.lane * lw + 8, a.getY() + n.y * a.getHeight() - 6, lw - 16, 12 }, gameColour (n.lane));
            gameText (g, "STREAK " + juce::String (streak), a.removeFromTop (20), 12.0f, ui::colours::lime, juce::Justification::topRight);
        }

    private:
        struct Note { int lane; float y; bool hit; };
        void hitLane (int l)
        {
            flash[(size_t) l] = 1.0f;
            for (auto& n : notes)
                if (! n.hit && n.lane == l && std::abs (n.y - 0.87f) < 0.06f)
                {
                    n.hit = true; ++streak; score += 10 + streak; note (l * 2 + (streak % 8 == 0 ? 7 : 0), 0.85f, 0.25);
                    mods[1] = l / 3.0f;
                    return;
                }
            streak = 0;
        }
        std::vector<Note> notes;
        std::array<float, 4> flash {};
        int streak = 0;
    };

    //==========================================================================
    // 12. Signal Sweep
    class SignalSweep final : public Game
    {
    public:
        const char* name() const override { return "Signal Sweep"; }
        const char* help() const override { return "Click to reveal a tile, right-click (or Shift-click) to flag. Revealed numbers\nplay chords built on that number. Mod 1 = cleared area."; }
        void reset() override
        {
            mines.fill (false); open.fill (false); flags.fill (false); score = 0; over = false;
            for (int k = 0; k < 12;) { const int i = rnd.nextInt (81); if (! mines[(size_t) i]) { mines[(size_t) i] = true; ++k; } }
        }
        void click (P p, bool secondary) override
        {
            if (over) { reset(); return; }
            const int c = juce::jlimit (0, 8, (int) (p.x * 9)), r = juce::jlimit (0, 8, (int) (p.y * 9));
            if (secondary || juce::ModifierKeys::currentModifiers.isShiftDown()) { flags[(size_t) (r * 9 + c)] = ! flags[(size_t) (r * 9 + c)]; note (0, 0.3f, 0.125); return; }
            reveal (c, r, true);
        }
        void autoplay (float dt) override
        {
            t += dt; if (t < 0.5f) return; t = 0;
            if (over) { reset(); return; }
            for (int tries = 0; tries < 100; ++tries) { const int i = rnd.nextInt (81); if (! open[(size_t) i] && ! mines[(size_t) i]) { reveal (i % 9, i / 9, true); return; } }
        }
        void update (float) override { mods[0] = (float) std::count (open.begin(), open.end(), true) / 69.0f; }
        void draw (juce::Graphics& g, R a) override
        {
            const float cs = std::min (a.getWidth(), a.getHeight()) / 9.0f;
            const auto o = a.withSizeKeepingCentre (cs * 9, cs * 9);
            for (int i = 0; i < 81; ++i)
            {
                const R cell (o.getX() + (i % 9) * cs + 2, o.getY() + (i / 9) * cs + 2, cs - 4, cs - 4);
                if (! open[(size_t) i]) { neonRect (g, cell, flags[(size_t) i] ? ui::colours::amber : ui::colours::cyan.withAlpha (0.4f), 0.3f); continue; }
                if (mines[(size_t) i]) { neonCircle (g, cell.getCentre(), cs * 0.25f, ui::colours::magenta); continue; }
                g.setColour (ui::colours::track); g.fillRect (cell);
                if (const int n = count (i % 9, i / 9)) gameText (g, juce::String (n), cell, 16.0f, gameColour (n));
            }
            if (over) gameText (g, score > 0 && std::count (open.begin(), open.end(), true) == 69 ? "SIGNAL CLEAR" : "NOISE BURST  //  CLICK", a.removeFromTop (24), 14.0f, ui::colours::amber);
        }

    private:
        int count (int c, int r) const
        {
            int n = 0;
            for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc)
            { const int cc = c + dc, rr = r + dr; if (cc >= 0 && cc < 9 && rr >= 0 && rr < 9 && mines[(size_t) (rr * 9 + cc)]) ++n; }
            return n;
        }
        void reveal (int c, int r, bool sound)
        {
            const auto i = (size_t) (r * 9 + c);
            if (open[i] || flags[i]) return;
            open[i] = true;
            if (mines[i]) { over = true; chord (-7, 0.9f, 2.0); for (int k = 0; k < 81; ++k) if (mines[(size_t) k]) open[(size_t) k] = true; return; }
            ++score;
            const int n = count (c, r);
            if (sound) n > 0 ? chord (n * 2, 0.7f, 1.0) : note (0, 0.5f, 0.25);
            if (n == 0) for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc) if (c + dc >= 0 && c + dc < 9 && r + dr >= 0 && r + dr < 9) reveal (c + dc, r + dr, false);
            if (std::count (open.begin(), open.end(), true) == 69) { over = true; arp (0, 8); }
        }
        std::array<bool, 81> mines {}, open {}, flags {};
        float t = 0;
    };

    //==========================================================================
    // 13. Merge Tones
    class MergeTones final : public Game
    {
    public:
        const char* name() const override { return "Merge Tones"; }
        const char* help() const override { return "ARROWS slide the tiles. Equal tiles merge; each merge plays a harmonic interval\n(tile level = scale degree). Mod 1 = highest tile."; }
        void reset() override { grid.fill (0); score = 0; over = false; add(); add(); }
        void keyDown (int k) override
        {
            if (over) { if (k == K::spaceKey) reset(); return; }
            if (k == K::leftKey) move (0);
            if (k == K::rightKey) move (1);
            if (k == K::upKey) move (2);
            if (k == K::downKey) move (3);
        }
        void autoplay (float dt) override { t += dt; if (t < 0.4f) return; t = 0; if (over) { reset(); return; } const int order[] = { 3, 0, 1, 2 }; for (int d : order) if (move (d)) return; }
        void update (float) override { mods[0] = (float) *std::max_element (grid.begin(), grid.end()) / 11.0f; }
        void draw (juce::Graphics& g, R a) override
        {
            const float cs = std::min (a.getWidth(), a.getHeight()) / 4.0f;
            const auto o = a.withSizeKeepingCentre (cs * 4, cs * 4);
            for (int i = 0; i < 16; ++i)
            {
                const R cell (o.getX() + (i % 4) * cs + 5, o.getY() + (i / 4) * cs + 5, cs - 10, cs - 10);
                if (grid[(size_t) i] == 0) { g.setColour (ui::colours::track); g.fillRect (cell); continue; }
                neonRect (g, cell, gameColour (grid[(size_t) i]), 0.8f);
                gameText (g, juce::String (1 << grid[(size_t) i]), cell, 22.0f, ui::colours::base);
            }
            if (over) gameText (g, "NO MOVES  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        bool move (int dir)
        {
            bool moved = false;
            for (int line = 0; line < 4; ++line)
            {
                std::array<int, 4> idx {};
                for (int k = 0; k < 4; ++k)
                {
                    const int kk = (dir == 1 || dir == 3) ? 3 - k : k;
                    idx[(size_t) k] = dir < 2 ? line * 4 + kk : kk * 4 + line;
                }
                std::vector<int> vals;
                for (int k : idx) if (grid[(size_t) k]) vals.push_back (grid[(size_t) k]);
                std::vector<int> out;
                for (size_t k = 0; k < vals.size(); ++k)
                {
                    if (k + 1 < vals.size() && vals[k] == vals[k + 1])
                    {
                        out.push_back (vals[k] + 1); score += 1 << (vals[k] + 1);
                        note (vals[k] - 1, 0.7f, 0.5); note (vals[k] + 3, 0.6f, 0.5); // interval grows with the tile
                        ++k;
                    }
                    else out.push_back (vals[k]);
                }
                while (out.size() < 4) out.push_back (0);
                for (int k = 0; k < 4; ++k) { if (grid[(size_t) idx[(size_t) k]] != out[(size_t) k]) moved = true; grid[(size_t) idx[(size_t) k]] = out[(size_t) k]; }
            }
            if (moved) add();
            if (! moved && std::count (grid.begin(), grid.end(), 0) == 0) { over = true; chord (-7, 0.8f, 2.0); }
            return moved;
        }
        void add()
        {
            std::vector<int> empty;
            for (int i = 0; i < 16; ++i) if (! grid[(size_t) i]) empty.push_back (i);
            if (! empty.empty()) grid[(size_t) empty[(size_t) rnd.nextInt ((int) empty.size())]] = rnd.nextFloat() < 0.9f ? 1 : 2;
        }
        std::array<int, 16> grid {};
        float t = 0;
    };

    //==========================================================================
    // 14. Neon Pinball
    class NeonPinball final : public Game
    {
    public:
        const char* name() const override { return "Neon Pinball"; }
        const char* help() const override { return "LEFT / RIGHT (or Z / M) flip. Bumpers play notes, walls play low hits.\nSPACE launches a new ball. Mod 1 = ball height, Mod 2 = bumper energy."; }
        void reset() override { ball = { 0.85f, 0.85f }; vel = { 0.0f, -1.2f }; score = 0; over = false; energy = 0; }
        void keyDown (int k) override { if (k == K::leftKey || k == 'Z' || k == 'z') lf = 1; if (k == K::rightKey || k == 'M' || k == 'm') rf = 1; if (k == K::spaceKey && over) reset(); }
        void keyUp (int k) override { if (k == K::leftKey || k == 'Z' || k == 'z') lf = 0; if (k == K::rightKey || k == 'M' || k == 'm') rf = 0; }
        void autoplay (float) override { if (over) reset(); lf = ball.y > 0.78f && ball.x < 0.5f ? 1.0f : 0.0f; rf = ball.y > 0.78f && ball.x >= 0.5f ? 1.0f : 0.0f; }
        void update (float dt) override
        {
            energy = std::max (0.0f, energy - dt);
            if (over) return;
            vel.y += dt * 0.9f;
            ball += vel * dt;
            if (ball.x < 0.03f || ball.x > 0.97f) { vel.x = -vel.x * 0.9f; ball.x = juce::jlimit (0.03f, 0.97f, ball.x); note (-7, 0.5f, 0.25); }
            if (ball.y < 0.03f) { vel.y = std::abs (vel.y); note (-5, 0.5f, 0.25); }
            const P bumpers[] = { { 0.3f, 0.25f }, { 0.7f, 0.25f }, { 0.5f, 0.42f }, { 0.25f, 0.55f }, { 0.75f, 0.55f } };
            for (int b = 0; b < 5; ++b)
            {
                auto d = ball - bumpers[b];
                if (d.getDistanceFromOrigin() < 0.065f)
                {
                    const auto n = d / std::max (0.001f, d.getDistanceFromOrigin());
                    vel = n * std::max (0.9f, vel.getDistanceFromOrigin() * 1.05f);
                    ball = bumpers[b] + n * 0.066f;
                    note (b * 2 + 2, 0.85f, 0.4); score += 25; energy = 1.0f;
                }
            }
            // flippers
            for (int side = 0; side < 2; ++side)
            {
                const float px = side == 0 ? 0.32f : 0.68f;
                if (ball.y > 0.84f && ball.y < 0.9f && std::abs (ball.x - px) < 0.13f && vel.y > 0)
                {
                    const bool up = side == 0 ? lf > 0 : rf > 0;
                    vel.y = up ? -1.5f : -std::abs (vel.y) * 0.5f;
                    vel.x += (ball.x - px) * (up ? 3.0f : 1.0f);
                    if (up) note (0, 0.6f, 0.25);
                }
            }
            if (ball.y > 1.02f) { over = true; chord (-7, 0.8f, 2.0); }
            mods[0] = 1.0f - juce::jlimit (0.0f, 1.0f, ball.y); mods[1] = energy;
        }
        void draw (juce::Graphics& g, R a) override
        {
            auto pt = [&] (P p) { return P (a.getX() + p.x * a.getWidth(), a.getY() + p.y * a.getHeight()); };
            const P bumpers[] = { { 0.3f, 0.25f }, { 0.7f, 0.25f }, { 0.5f, 0.42f }, { 0.25f, 0.55f }, { 0.75f, 0.55f } };
            for (int b = 0; b < 5; ++b) neonCircle (g, pt (bumpers[b]), a.getWidth() * 0.05f, gameColour (b), false);
            for (int side = 0; side < 2; ++side)
            {
                const float px = side == 0 ? 0.32f : 0.68f;
                const bool up = side == 0 ? lf > 0 : rf > 0;
                g.setColour (ui::colours::lime);
                g.drawLine (juce::Line<float> (pt ({ px + (side == 0 ? -0.12f : 0.12f), 0.87f }), pt ({ px, up ? 0.82f : 0.9f })), 5.0f);
            }
            neonCircle (g, pt (ball), 7.0f, ui::colours::text);
            if (over) gameText (g, "TILT  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        P ball, vel;
        float lf = 0, rf = 0, energy = 0;
    };

    //==========================================================================
    // 15. Orbit
    class Orbit final : public Game
    {
    public:
        const char* name() const override { return "Orbit"; }
        const char* help() const override { return "Click to launch a planet into orbit. Three trigger lines radiate from the star;\nevery crossing fires a note (line = degree, orbit radius = octave). Mod 1 = planet count."; }
        void reset() override { planets.clear(); score = 0; over = false; addPlanet ({ 0.5f, 0.25f }); addPlanet ({ 0.5f, 0.85f }); }
        void click (P p, bool) override { addPlanet (p); }
        void autoplay (float dt) override { t += dt; if (t > 4.0f && planets.size() < 7) { t = 0; addPlanet ({ 0.5f + (rnd.nextFloat() - 0.5f) * 0.8f, 0.5f + (rnd.nextFloat() - 0.5f) * 0.8f }); } }
        void update (float dt) override
        {
            const P sun (0.5f, 0.5f);
            for (auto& pl : planets)
            {
                auto d = sun - pl.p;
                const float r = std::max (0.05f, d.getDistanceFromOrigin());
                pl.v += d / r * (0.02f / (r * r)) * dt;
                const float prevA = std::atan2 (pl.p.y - 0.5f, pl.p.x - 0.5f);
                pl.p += pl.v * dt;
                const float a = std::atan2 (pl.p.y - 0.5f, pl.p.x - 0.5f);
                for (int l = 0; l < 3; ++l)
                {
                    const float la = -3.14159f + l * 2.0944f + 0.3f;
                    if ((prevA < la && a >= la) || (prevA > la && a <= la && std::abs (a - prevA) < 1.0f))
                    { note (l * 2 + (int) ((0.5f - r) * 14.0f), 0.65f, 0.5); ++score; pl.flash = 1.0f; }
                }
                pl.flash = std::max (0.0f, pl.flash - dt * 3.0f);
            }
            planets.erase (std::remove_if (planets.begin(), planets.end(), [] (auto& pl) { return pl.p.x < -0.3f || pl.p.x > 1.3f || pl.p.y < -0.3f || pl.p.y > 1.3f; }), planets.end());
            mods[0] = std::min (1.0f, planets.size() / 8.0f);
        }
        void draw (juce::Graphics& g, R a) override
        {
            auto pt = [&] (P p) { return P (a.getX() + p.x * a.getWidth(), a.getY() + p.y * a.getHeight()); };
            for (int l = 0; l < 3; ++l)
            {
                const float la = -3.14159f + l * 2.0944f + 0.3f;
                g.setColour (gameColour (l).withAlpha (0.4f));
                g.drawLine (juce::Line<float> (pt ({ 0.5f, 0.5f }), pt ({ 0.5f + std::cos (la), 0.5f + std::sin (la) })), 1.0f);
            }
            neonCircle (g, pt ({ 0.5f, 0.5f }), 14.0f, ui::colours::amber);
            for (size_t i = 0; i < planets.size(); ++i) neonCircle (g, pt (planets[i].p), 5.0f + 4.0f * planets[i].flash, gameColour ((int) i + 1));
        }

    private:
        struct Planet { P p, v; float flash; };
        void addPlanet (P p)
        {
            auto d = p - P (0.5f, 0.5f);
            const float r = std::max (0.08f, d.getDistanceFromOrigin());
            const P tangent (-d.y / r, d.x / r);
            planets.push_back ({ p, tangent * std::sqrt (0.02f / r) * 1.0f, 0.0f });
            if (planets.size() > 10) planets.erase (planets.begin());
        }
        std::vector<Planet> planets;
        float t = 0;
    };

    //==========================================================================
    // 16. Bounce Box
    class BounceBox final : public Game
    {
    public:
        const char* name() const override { return "Bounce Box"; }
        const char* help() const override { return "Click to drop a ball. Ball speeds are locked to tempo divisions, so wall\nhits form polyrhythms (each wall is a degree). Mod 1 = hit density."; }
        void reset() override { balls.clear(); score = 0; over = false; add ({ 0.3f, 0.3f }); add ({ 0.7f, 0.6f }); }
        void click (P p, bool) override { add (p); }
        void autoplay (float dt) override { t += dt; if (t > 6.0f) { t = 0; if (balls.size() > 6) balls.erase (balls.begin()); add ({ rnd.nextFloat(), rnd.nextFloat() }); } }
        void update (float dt) override
        {
            const float bps = host != nullptr ? host->bpm / 60.0f : 2.0f;
            for (auto& b : balls)
            {
                b.p += b.v * dt * bps;
                for (int axis = 0; axis < 2; ++axis)
                {
                    float& c = axis == 0 ? b.p.x : b.p.y;
                    float& v = axis == 0 ? b.v.x : b.v.y;
                    if (c < 0.0f || c > 1.0f)
                    {
                        const int wall = axis * 2 + (c > 1.0f ? 1 : 0);
                        c = juce::jlimit (0.0f, 1.0f, c); v = -v;
                        note (wall * 2 + b.octave * 7, 0.7f, 0.25); ++score; density += 0.2f; b.flash = 1.0f;
                    }
                }
                b.flash = std::max (0.0f, b.flash - dt * 4.0f);
            }
            density = std::max (0.0f, density - dt * 0.8f);
            mods[0] = std::min (1.0f, density);
        }
        void draw (juce::Graphics& g, R a) override
        {
            for (int w = 0; w < 4; ++w)
            {
                g.setColour (gameColour (w).withAlpha (0.7f));
                const R e = w == 0 ? a.withWidth (3) : w == 1 ? a.withLeft (a.getRight() - 3) : w == 2 ? a.withHeight (3) : a.withTop (a.getBottom() - 3);
                g.fillRect (e);
            }
            for (size_t i = 0; i < balls.size(); ++i)
                neonCircle (g, { a.getX() + balls[i].p.x * a.getWidth(), a.getY() + balls[i].p.y * a.getHeight() }, 8.0f + 6.0f * balls[i].flash, gameColour ((int) i));
        }

    private:
        struct Ball { P p, v; int octave; float flash; };
        void add (P p)
        {
            static const float divs[] = { 1.0f, 0.5f, 0.75f, 1.5f, 0.6667f, 1.3333f }; // crossings per beat: straight, half, dotted, triplet...
            const float sx = divs[rnd.nextInt (6)], sy = divs[rnd.nextInt (6)];
            balls.push_back ({ p, P (sx * (rnd.nextBool() ? 1 : -1), sy * (rnd.nextBool() ? 1 : -1)), rnd.nextInt (2), 0.0f });
        }
        std::vector<Ball> balls;
        float t = 0, density = 0;
    };

    //==========================================================================
    // 17. Lane Cross
    class LaneCross final : public Game
    {
    public:
        const char* name() const override { return "Lane Cross"; }
        const char* help() const override { return "ARROWS hop. Each traffic lane is its own polyrhythmic loop: cars crossing the\ncentre line play that lane's note. Reaching the top plays a chord. Mod 1 = progress."; }
        void reset() override { pos = { 0.5f, 7 }; score = 0; over = false; for (int l = 0; l < 6; ++l) offs[(size_t) l] = rnd.nextFloat(); }
        void keyDown (int k) override
        {
            if (over) { if (k == K::spaceKey) reset(); return; }
            if (k == K::upKey) hop (-1);
            if (k == K::downKey && pos.y < 7) hop (1);
            if (k == K::leftKey) pos.x = std::max (0.05f, pos.x - 0.08f);
            if (k == K::rightKey) pos.x = std::min (0.95f, pos.x + 0.08f);
        }
        void autoplay (float dt) override
        {
            t += dt; if (t < 0.25f) return; t = 0;
            if (over) { reset(); return; }
            const int next = pos.y - 1;
            if (next < 1 || safe (next, pos.x)) hop (-1);
        }
        void update (float dt) override
        {
            const float bps = host != nullptr ? host->bpm / 60.0f : 2.0f;
            for (int l = 0; l < 6; ++l)
            {
                const float prev = offs[(size_t) l];
                offs[(size_t) l] += dt * bps * speeds[l] * 0.25f;
                for (int c = 0; c < 2; ++c)
                {
                    const float a = std::fmod (prev + c * 0.5f, 1.0f), b = std::fmod (offs[(size_t) l] + c * 0.5f, 1.0f);
                    if (a < 0.5f && b >= 0.5f) note (l, 0.45f, 0.25);
                }
            }
            if (pos.y >= 1 && pos.y <= 6 && ! safe (pos.y, pos.x)) { over = true; chord (-7, 0.8f, 2.0); }
            mods[0] = (7 - pos.y) / 7.0f;
        }
        void draw (juce::Graphics& g, R a) override
        {
            const float lh = a.getHeight() / 8.0f;
            for (int l = 1; l <= 6; ++l)
            {
                g.setColour (ui::colours::track); g.fillRect (R (a.getX(), a.getY() + l * lh + 1, a.getWidth(), lh - 2));
                for (int c = 0; c < 2; ++c)
                {
                    const float x = carX (l, c);
                    neonRect (g, R (a.getWidth() * 0.12f, lh * 0.6f).withCentre ({ a.getX() + x * a.getWidth(), a.getY() + (l + 0.5f) * lh }), gameColour (l));
                }
            }
            g.setColour (ui::colours::text.withAlpha (0.2f)); g.drawVerticalLine ((int) a.getCentreX(), a.getY(), a.getBottom());
            neonCircle (g, { a.getX() + pos.x * a.getWidth(), a.getY() + (pos.y + 0.5f) * lh }, lh * 0.3f, ui::colours::lime);
            if (over) gameText (g, "SPLAT  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        static constexpr float speeds[6] = { 1.0f, -0.75f, 1.5f, -0.6667f, 1.25f, -1.3333f };
        float carX (int lane, int c) const { float x = std::fmod (offs[(size_t) (lane - 1)] + c * 0.5f, 1.0f); if (x < 0) x += 1.0f; return speeds[lane - 1] > 0 ? x : 1.0f - x; }
        bool safe (int lane, float x) const { for (int c = 0; c < 2; ++c) if (std::abs (carX (lane, c) - x) < 0.09f) return false; return true; }
        void hop (int d)
        {
            pos.y += d; note (7 - pos.y, 0.5f, 0.125);
            if (pos.y <= 0) { score += 100; chord (7, 0.8f, 1.5); pos.y = 7; }
        }
        struct { float x; int y; } pos { 0.5f, 7 };
        std::array<float, 6> offs {};
        float t = 0;
    };
} // namespace

std::unique_ptr<Game> createGameB (int i)
{
    switch (i)
    {
        case 9:  return std::make_unique<EchoMemory>();
        case 10: return std::make_unique<RhythmLanes>();
        case 11: return std::make_unique<SignalSweep>();
        case 12: return std::make_unique<MergeTones>();
        case 13: return std::make_unique<NeonPinball>();
        case 14: return std::make_unique<Orbit>();
        case 15: return std::make_unique<BounceBox>();
        case 16: return std::make_unique<LaneCross>();
        default: return nullptr;
    }
}

} // namespace mbx::games
