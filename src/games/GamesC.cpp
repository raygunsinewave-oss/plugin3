// XD games 18-25 and the game factory.
#include "Game.h"
#include "../ui/Theme.h"

namespace mbx::games
{
using K = juce::KeyPress;
using P = juce::Point<float>;
using R = juce::Rectangle<float>;

std::unique_ptr<Game> createGameA (int i);
std::unique_ptr<Game> createGameB (int i);

namespace
{
    //==========================================================================
    // 18. Stack Four
    class StackFour final : public Game
    {
    public:
        const char* name() const override { return "Stack Four"; }
        const char* help() const override { return "Click a column (or keys 1-7) to drop your disc; connect four to win.\nColumns are chord degrees: every drop plays that column's chord. Mod 1 = board fill."; }
        void reset() override { board.fill (0); score = 0; over = false; turn = 1; aiDelay = 0; }
        void click (P p, bool) override { if (over) { reset(); return; } if (turn == 1) drop (juce::jlimit (0, 6, (int) (p.x * 7)), 1); }
        void keyDown (int k) override { if (k >= '1' && k <= '7' && turn == 1 && ! over) drop (k - '1', 1); if (over && k == K::spaceKey) reset(); }
        void autoplay (float dt) override { aiT += dt; if (aiT > 0.6f && turn == 1 && ! over) { aiT = 0; drop (bestMove (1), 1); } if (over && aiT > 2.0f) reset(); }
        void update (float dt) override
        {
            mods[0] = (float) std::count_if (board.begin(), board.end(), [] (int v) { return v != 0; }) / 42.0f;
            if (turn == 2 && ! over) { aiDelay += dt;
            if (aiDelay > 0.5f) { aiDelay = 0; drop (bestMove (2), 2); } }
        }
        void draw (juce::Graphics& g, R a) override
        {
            const float cs = std::min (a.getWidth() / 7.0f, a.getHeight() / 6.0f);
            const auto o = a.withSizeKeepingCentre (cs * 7, cs * 6);
            g.setColour (ui::colours::cyan.withAlpha (0.12f)); g.fillRect (o);
            for (int i = 0; i < 42; ++i)
            {
                const P c (o.getX() + (i % 7 + 0.5f) * cs, o.getY() + (i / 7 + 0.5f) * cs);
                if (board[(size_t) i]) neonCircle (g, c, cs * 0.38f, board[(size_t) i] == 1 ? ui::colours::lime : ui::colours::magenta);
                else { g.setColour (ui::colours::base); g.fillEllipse (R (cs * 0.76f, cs * 0.76f).withCentre (c)); }
            }
            if (over) gameText (g, winner == 1 ? "YOU WIN  //  CLICK" : winner == 2 ? "MACHINE WINS  //  CLICK" : "DRAW  //  CLICK", a.removeFromTop (24), 14.0f, ui::colours::amber);
        }

    private:
        int rowFor (int c) const { for (int r = 5; r >= 0; --r) if (! board[(size_t) (r * 7 + c)]) return r; return -1; }
        bool wins (int who) const
        {
            const int dirs[4][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, -1 } };
            for (int r = 0; r < 6; ++r) for (int c = 0; c < 7; ++c) for (auto& d : dirs)
            {
                int n = 0;
                for (int k = 0; k < 4; ++k) { const int rr = r + d[1] * k, cc = c + d[0] * k; if (rr < 0 || rr >= 6 || cc >= 7 || board[(size_t) (rr * 7 + cc)] != who) break; ++n; }
                if (n == 4) return true;
            }
            return false;
        }
        void drop (int c, int who)
        {
            const int r = rowFor (c);
            if (r < 0) return;
            board[(size_t) (r * 7 + c)] = who;
            chord (c, who == 1 ? 0.75f : 0.55f, 1.0);
            if (wins (who)) { over = true; winner = who;
            if (who == 1) { score += 100; }
            arp (c, 6);
            return; }
            if (std::count (board.begin(), board.end(), 0) == 0) { over = true; winner = 0; return; }
            turn = 3 - who;
        }
        int bestMove (int who)
        {
            for (int pass = 0; pass < 2; ++pass) // win if possible, else block
            {
                const int test = pass == 0 ? who : 3 - who;
                for (int c = 0; c < 7; ++c)
                {
                    const int r = rowFor (c);
                    if (r < 0) continue;
                    board[(size_t) (r * 7 + c)] = test;
                    const bool w = wins (test);
                    board[(size_t) (r * 7 + c)] = 0;
                    if (w) return c;
                }
            }
            const int pref[] = { 3, 2, 4, 1, 5, 0, 6 };
            for (int k = 0; k < 7; ++k) { const int c = pref[(k + rnd.nextInt (2)) % 7]; if (rowFor (c) >= 0) return c; }
            return 3;
        }
        std::array<int, 42> board {};
        int turn = 1, winner = 0;
        float aiDelay = 0, aiT = 0;
    };

    //==========================================================================
    // 19. Gem Chords
    class GemChords final : public Game
    {
    public:
        const char* name() const override { return "Gem Chords"; }
        const char* help() const override { return "Click a gem, then a neighbour, to swap. Lines of 3+ play chords (gem colour =\ndegree); cascades play arpeggios. Mod 1 = cascade depth."; }
        void reset() override { for (auto& v : gems) v = rnd.nextInt (6); while (resolve (false)) {} score = 0; over = false; sel = -1; cascade = 0; }
        void click (P p, bool) override
        {
            const int c = juce::jlimit (0, 7, (int) (p.x * 8)), r = juce::jlimit (0, 7, (int) (p.y * 8)), i = r * 8 + c;
            if (sel < 0) { sel = i; return; }
            const int sc = sel % 8, sr = sel / 8;
            if (std::abs (sc - c) + std::abs (sr - r) == 1) trySwap (sel, i);
            sel = -1;
        }
        void autoplay (float dt) override
        {
            t += dt; if (t < 0.8f) return; t = 0;
            for (int i = 0; i < 64; ++i)
                for (int d : { 1, 8 })
                {
                    const int j = i + d;
                    if (j >= 64 || (d == 1 && i % 8 == 7)) continue;
                    std::swap (gems[(size_t) i], gems[(size_t) j]);
                    const bool ok = hasMatch();
                    std::swap (gems[(size_t) i], gems[(size_t) j]);
                    if (ok) { trySwap (i, j); return; }
                }
            reset();
        }
        void update (float dt) override { mods[0] = std::min (1.0f, cascade / 4.0f); cascadeT -= dt; if (cascadeT <= 0 && pending) { pending = resolve (true); cascadeT = 0.25f; } }
        void draw (juce::Graphics& g, R a) override
        {
            const float cs = std::min (a.getWidth(), a.getHeight()) / 8.0f;
            const auto o = a.withSizeKeepingCentre (cs * 8, cs * 8);
            for (int i = 0; i < 64; ++i)
            {
                const P c (o.getX() + (i % 8 + 0.5f) * cs, o.getY() + (i / 8 + 0.5f) * cs);
                if (gems[(size_t) i] >= 0)
                {
                    juce::Path d; d.addPolygon (c, 4 + gems[(size_t) i] % 3, cs * 0.36f, 0.3f * gems[(size_t) i]);
                    g.setColour (gameColour (gems[(size_t) i]).withAlpha (0.85f)); g.fillPath (d);
                }
                if (i == sel) { g.setColour (ui::colours::text); g.drawRect (R (cs, cs).withCentre (c), 2.0f); }
            }
        }

    private:
        void trySwap (int a, int b)
        {
            std::swap (gems[(size_t) a], gems[(size_t) b]);
            if (! hasMatch()) { std::swap (gems[(size_t) a], gems[(size_t) b]); note (-3, 0.4f, 0.25); return; }
            cascade = 0; pending = resolve (true); cascadeT = 0.25f;
        }
        bool hasMatch() const
        {
            for (int r = 0; r < 8; ++r) for (int c = 0; c < 8; ++c)
            {
                const int v = gems[(size_t) (r * 8 + c)];
                if (c < 6 && v == gems[(size_t) (r * 8 + c + 1)] && v == gems[(size_t) (r * 8 + c + 2)]) return true;
                if (r < 6 && v == gems[(size_t) ((r + 1) * 8 + c)] && v == gems[(size_t) ((r + 2) * 8 + c)]) return true;
            }
            return false;
        }
        bool resolve (bool sound)
        {
            std::array<bool, 64> kill {};
            bool any = false;
            for (int r = 0; r < 8; ++r) for (int c = 0; c < 8; ++c)
            {
                const int v = gems[(size_t) (r * 8 + c)];
                if (c < 6 && v == gems[(size_t) (r * 8 + c + 1)] && v == gems[(size_t) (r * 8 + c + 2)]) { kill[(size_t) (r * 8 + c)] = kill[(size_t) (r * 8 + c + 1)] = kill[(size_t) (r * 8 + c + 2)] = any = true; }
                if (r < 6 && v == gems[(size_t) ((r + 1) * 8 + c)] && v == gems[(size_t) ((r + 2) * 8 + c)]) { kill[(size_t) (r * 8 + c)] = kill[(size_t) ((r + 1) * 8 + c)] = kill[(size_t) ((r + 2) * 8 + c)] = any = true; }
            }
            if (! any) return false;
            int colour = 0, count = 0;
            for (int i = 0; i < 64; ++i) if (kill[(size_t) i]) { colour = gems[(size_t) i]; ++count; }
            if (sound) { cascade > 0 ? arp (colour + cascade, 3 + cascade) : chord (colour, 0.8f, 1.0); score += count * 10 * (cascade + 1); ++cascade; }
            for (int c = 0; c < 8; ++c)
            {
                int write = 7;
                for (int r = 7; r >= 0; --r) if (! kill[(size_t) (r * 8 + c)]) gems[(size_t) (write-- * 8 + c)] = gems[(size_t) (r * 8 + c)];
                for (; write >= 0; --write) gems[(size_t) (write * 8 + c)] = rnd.nextInt (6);
            }
            return true;
        }
        std::array<int, 64> gems {};
        int sel = -1, cascade = 0;
        bool pending = false;
        float t = 0, cascadeT = 0;
    };

    //==========================================================================
    // 20. Neon Run
    class NeonRun final : public Game
    {
    public:
        const char* name() const override { return "Neon Run"; }
        const char* help() const override { return "SPACE / UP / click to jump. Obstacles arrive on the beat and the world scrolls\nat tempo. Jumps play notes. Mod 1 = height, Mod 2 = speed."; }
        void reset() override { y = 0; vy = 0; obs.clear(); score = 0; over = false; }
        void keyDown (int k) override { if (k == K::spaceKey || k == K::upKey) jump(); }
        void click (P, bool) override { jump(); }
        void autoplay (float) override { for (auto& o : obs) if (o > 0.2f && o < 0.3f && y <= 0) jump(); if (over) reset(); }
        void tick (int s) override { if (! over && s % 4 == 0 && rnd.nextFloat() < 0.55f) obs.push_back (1.05f); if (! over && s % 8 == 0) note (-7, 0.35f, 0.5); }
        void update (float dt) override
        {
            if (over) return;
            const float speed = host != nullptr ? host->bpm / 120.0f * 0.5f : 0.5f;
            vy -= dt * 3.0f; y = std::max (0.0f, y + vy * dt); if (y <= 0) vy = 0;
            for (auto& o : obs) o -= dt * speed;
            for (auto o : obs) if (std::abs (o - 0.2f) < 0.025f && y < 0.08f) { over = true; chord (-7, 0.8f, 2.0); }
            const auto before = obs.size();
            obs.erase (std::remove_if (obs.begin(), obs.end(), [] (float o) { return o < -0.05f; }), obs.end());
            score += (int) (before - obs.size());
            mods[0] = y / 0.4f; mods[1] = std::min (1.0f, speed);
        }
        void draw (juce::Graphics& g, R a) override
        {
            const float ground = a.getY() + a.getHeight() * 0.8f;
            g.setColour (ui::colours::magenta); g.fillRect (R (a.getX(), ground, a.getWidth(), 2));
            for (int i = 0; i < 12; ++i) { g.setColour (ui::colours::magenta.withAlpha (0.2f)); g.drawLine (a.getCentreX(), ground, a.getX() + i * a.getWidth() / 11, a.getBottom()); }
            for (float o : obs) neonRect (g, { a.getX() + o * a.getWidth() - 8, ground - 30, 16, 30 }, ui::colours::amber);
            neonRect (g, { a.getX() + 0.2f * a.getWidth() - 10, ground - 24 - y * a.getHeight(), 20, 24 }, ui::colours::lime);
            if (over) gameText (g, "DOWN  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        void jump() { if (over) { reset(); return; } if (y <= 0) { vy = 1.3f; note (4 + rnd.nextInt (5), 0.75f, 0.25); } }
        std::vector<float> obs;
        float y = 0, vy = 0;
    };

    //==========================================================================
    // 21. Glitch Whack
    class GlitchWhack final : public Game
    {
    public:
        const char* name() const override { return "Glitch Whack"; }
        const char* help() const override { return "Click the lit targets (or keypad 1-9). Hits trigger stutter-style FX:\nMod 4 pulses on every hit (route it to an FX mix). Mod 1 = combo."; }
        void reset() override { lit.fill (0.0f); score = 0; over = false; combo = 0; }
        void click (P p, bool) override { whack (juce::jlimit (0, 2, (int) (p.y * 3)) * 3 + juce::jlimit (0, 2, (int) (p.x * 3))); }
        void keyDown (int k) override { if (k >= '1' && k <= '9') whack (k - '1'); }
        void autoplay (float dt) override { t += dt; if (t < 0.25f) return; t = 0; for (int i = 0; i < 9; ++i) if (lit[(size_t) i] > 0.3f) { whack (i); return; } }
        void tick (int s) override { if (s % 2 == 0) lit[(size_t) rnd.nextInt (9)] = 1.0f; }
        void update (float dt) override
        {
            for (auto& l : lit) { if (l > 0 && l - dt * 0.8f <= 0) combo = 0; l = std::max (0.0f, l - dt * 0.8f); }
            mods[3] = std::max (0.0f, mods[3] - dt * 4.0f);
            mods[0] = std::min (1.0f, combo / 10.0f);
        }
        void draw (juce::Graphics& g, R a) override
        {
            for (int i = 0; i < 9; ++i)
            {
                const R cell (a.getX() + (i % 3) * a.getWidth() / 3 + 10, a.getY() + (i / 3) * a.getHeight() / 3 + 10, a.getWidth() / 3 - 20, a.getHeight() / 3 - 20);
                g.setColour (ui::colours::track); g.fillRect (cell);
                if (lit[(size_t) i] > 0) neonCircle (g, cell.getCentre(), std::min (cell.getWidth(), cell.getHeight()) * 0.35f * lit[(size_t) i], gameColour (i));
            }
            if (mods[3] > 0) { g.setColour (ui::colours::text.withAlpha (mods[3] * 0.15f)); for (int k = 0; k < 6; ++k) g.fillRect (R (a.getX(), a.getY() + rnd.nextFloat() * a.getHeight(), a.getWidth(), 3)); }
            gameText (g, "COMBO " + juce::String (combo), a.removeFromTop (20), 12.0f, ui::colours::lime, juce::Justification::topRight);
        }

    private:
        void whack (int i)
        {
            if (lit[(size_t) i] > 0) { lit[(size_t) i] = 0; ++combo; score += 10 * combo; note (i, 0.85f, 0.125); note (i + 7, 0.6f, 0.125); mods[3] = 1.0f; }
            else { combo = 0; note (-5, 0.3f, 0.125); }
        }
        std::array<float, 9> lit {};
        int combo = 0;
        float t = 0;
    };

    //==========================================================================
    // 22. Synthwave Racer
    class SynthwaveRacer final : public Game
    {
    public:
        const char* name() const override { return "Synthwave Racer"; }
        const char* help() const override { return "LEFT/RIGHT steer (Mod 1 = steering, for pitch bend / pan), UP/DOWN throttle\n(Mod 2 = speed, for filter). Passing cars play notes; stripes mark the beat."; }
        void reset() override { x = 0.5f; speed = 0.5f; cars.clear(); score = 0; over = false; dist = 0; }
        void keyDown (int k) override { if (k == K::leftKey) steer = -1; if (k == K::rightKey) steer = 1; if (k == K::upKey) thr = 1; if (k == K::downKey) thr = -1; if (over && k == K::spaceKey) reset(); }
        void keyUp (int k) override { if (k == K::leftKey || k == K::rightKey) steer = 0; if (k == K::upKey || k == K::downKey) thr = 0; }
        void autoplay (float) override
        {
            if (over) { reset(); return; }
            float target = 0.5f;
            for (auto& c : cars) if (c.z < 0.6f && std::abs (c.lane - x) < 0.25f) target = c.lane > 0.5f ? 0.2f : 0.8f;
            steer = target > x + 0.03f ? 1.0f : target < x - 0.03f ? -1.0f : 0.0f; thr = 0.3f;
        }
        void tick (int s) override { if (! over && s % 4 == 0 && rnd.nextFloat() < 0.6f) cars.push_back ({ 0.2f + 0.3f * rnd.nextInt (3), 1.0f, rnd.nextInt (5) }); }
        void update (float dt) override
        {
            if (over) return;
            x = juce::jlimit (0.1f, 0.9f, x + steer * dt * 0.9f);
            speed = juce::jlimit (0.2f, 1.0f, speed + thr * dt * 0.5f);
            dist += dt * speed;
            for (auto& c : cars)
            {
                const float before = c.z;
                c.z -= dt * speed * 0.6f;
                if (before >= 0.1f && c.z < 0.1f) { if (std::abs (c.lane - x) < 0.12f) { over = true; chord (-7, 0.8f, 2.0); } else { note (c.colour * 2, 0.7f, 0.5); score += 10; } }
            }
            cars.erase (std::remove_if (cars.begin(), cars.end(), [] (auto& c) { return c.z < -0.1f; }), cars.end());
            mods[0] = x; mods[1] = speed;
        }
        void draw (juce::Graphics& g, R a) override
        {
            const float hz = a.getY() + a.getHeight() * 0.35f;
            g.setGradientFill (juce::ColourGradient (ui::colours::magenta.withAlpha (0.5f), a.getCentreX(), hz, ui::colours::base, a.getCentreX(), a.getY(), false));
            g.fillRect (a.withBottom (hz));
            neonCircle (g, { a.getCentreX(), hz - 20 }, 40.0f, ui::colours::amber);
            auto road = [&] (float z, float lane) { const float w = 0.1f + 0.9f * (1.0f - z); return P (a.getCentreX() + (lane - 0.5f) * w * a.getWidth(), hz + (1.0f - z) * (a.getBottom() - hz)); };
            for (float lane : { 0.0f, 1.0f }) { g.setColour (ui::colours::cyan); g.drawLine (juce::Line<float> (road (1.0f, lane), road (0.0f, lane)), 2.0f); }
            for (int s = 0; s < 8; ++s)
            {
                const float z = std::fmod (s / 8.0f - dist * 2.0f + 100.0f, 1.0f);
                g.setColour (ui::colours::magenta.withAlpha (0.6f)); g.drawLine (juce::Line<float> (road (z, 0.0f), road (z, 1.0f)), 1.0f);
            }
            for (auto& c : cars) if (c.z > 0) { const auto p = road (c.z, c.lane); const float sz = 6.0f + 30.0f * (1.0f - c.z); neonRect (g, R (sz * 1.6f, sz).withCentre (p), gameColour (c.colour)); }
            neonRect (g, R (50, 22).withCentre (road (0.05f, x)), ui::colours::lime);
            if (over) gameText (g, "CRASHED  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        struct Car { float lane, z; int colour; };
        std::vector<Car> cars;
        float x = 0.5f, speed = 0.5f, steer = 0, thr = 0, dist = 0;
    };

    //==========================================================================
    // 23. Frequency Defense
    class FrequencyDefense final : public Game
    {
    public:
        const char* name() const override { return "Frequency Defense"; }
        const char* help() const override { return "Click empty ground to build a tower (max 8). Each tower is a voice: it plays its\nnote (row = pitch) whenever it fires. Stop the noise blobs. Mod 1 = threat level."; }
        void reset() override { towers.clear(); enemies.clear(); score = 0; over = false; lives = 10; spawn = 0; }
        void click (P p, bool) override { if (over) { reset(); return; } build (p); }
        void autoplay (float dt) override { t += dt; if (t > 3.0f && towers.size() < 8) { t = 0; const float k = (float) towers.size(); build ({ 0.15f + std::fmod (k * 0.37f, 0.7f), (int) k % 2 == 0 ? 0.35f : 0.65f }); } if (over) reset(); }
        void tick (int s) override
        {
            if (over) return;
            if (s % 4 == 0) enemies.push_back ({ 0.0f, 1.0f + (float) score / 400.0f });
            if (s % 2 == 0)
                for (auto& tw : towers)
                    for (auto& e : enemies)
                        if (e.hp > 0 && pathPos (e.d).getDistanceFrom (tw.p) < 0.22f) { e.hp -= 0.5f; tw.flash = 1.0f; note (tw.degree, 0.6f, 0.25);
                        if (e.hp <= 0) { score += 10; }
                        break; }
        }
        void update (float dt) override
        {
            if (over) return;
            for (auto& e : enemies) { e.d += dt * 0.08f; if (e.d >= 1.0f && e.hp > 0) { e.hp = 0; if (--lives <= 0) { over = true; chord (-7, 0.8f, 2.0); } } }
            enemies.erase (std::remove_if (enemies.begin(), enemies.end(), [] (auto& e) { return e.hp <= 0; }), enemies.end());
            for (auto& tw : towers) tw.flash = std::max (0.0f, tw.flash - dt * 4.0f);
            float threat = 0; for (auto& e : enemies) threat = std::max (threat, e.d);
            mods[0] = threat; mods[1] = lives / 10.0f;
        }
        void draw (juce::Graphics& g, R a) override
        {
            auto pt = [&] (P p) { return P (a.getX() + p.x * a.getWidth(), a.getY() + p.y * a.getHeight()); };
            juce::Path path;
            for (int i = 0; i <= 40; ++i) { const auto p = pt (pathPos (i / 40.0f)); i == 0 ? path.startNewSubPath (p) : path.lineTo (p); }
            g.setColour (ui::colours::magenta.withAlpha (0.25f)); g.strokePath (path, juce::PathStrokeType (18.0f));
            for (auto& e : enemies) neonCircle (g, pt (pathPos (e.d)), 7.0f * std::max (0.4f, e.hp), ui::colours::amber);
            for (auto& tw : towers) { neonRect (g, R (16, 16).withCentre (pt (tw.p)), gameColour (tw.degree), 1.0f + 2.0f * tw.flash); g.setColour (ui::colours::cyan.withAlpha (0.1f)); g.drawEllipse (R (0.44f * a.getWidth(), 0.44f * a.getHeight()).withCentre (pt (tw.p)), 1.0f); }
            gameText (g, "LIVES " + juce::String (lives), a.removeFromTop (20), 12.0f, ui::colours::lime, juce::Justification::topRight);
            if (over) gameText (g, "OVERRUN  //  CLICK", a, 18.0f, ui::colours::amber);
        }

    private:
        struct Tower { P p; int degree; float flash; };
        struct Enemy { float d, hp; };
        static P pathPos (float d) { return { d, 0.5f + 0.3f * std::sin (d * 6.28318f * 1.5f) }; }
        void build (P p)
        {
            if (towers.size() >= 8) return;
            for (int i = 0; i <= 20; ++i) if (pathPos (i / 20.0f).getDistanceFrom (p) < 0.06f) return; // not on the path
            towers.push_back ({ p, (int) ((1.0f - p.y) * 12.0f), 0.0f });
            chord (towers.back().degree, 0.5f, 0.5);
        }
        std::vector<Tower> towers;
        std::vector<Enemy> enemies;
        int lives = 10;
        float spawn = 0, t = 0;
    };

    //==========================================================================
    // 24. Beat Typer
    class BeatTyper final : public Game
    {
    public:
        const char* name() const override { return "Beat Typer"; }
        const char* help() const override { return "Type the falling words. Each letter maps to a scale note (A = root ... Z),\nfinishing a word plays its chord. A new word drops every bar. Mod 1 = typing speed."; }
        void reset() override { words.clear(); score = 0; over = false; typedInBar = 0; }
        void keyDown (int k) override
        {
            if (over) { if (k == K::spaceKey) reset(); return; }
            const auto ch = (juce::juce_wchar) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k);
            if (ch < 'A' || ch > 'Z' || words.empty()) return;
            auto& w = words.front();
            if (w.text[w.typed] == ch)
            {
                note ((int) (ch - 'A') % 15, 0.7f, 0.25); ++w.typed; ++typedInBar;
                if (w.typed >= w.text.length()) { chord ((int) (w.text[0] - 'A') % 7, 0.8f, 1.0); score += 10 * w.text.length(); words.erase (words.begin()); }
            }
            else note (-6, 0.3f, 0.125);
        }
        void autoplay (float dt) override
        {
            t += dt; if (t < 0.18f) return; t = 0;
            if (over) { reset(); return; }
            if (! words.empty()) keyDown ((int) words.front().text[words.front().typed]);
        }
        void tick (int s) override
        {
            if (over) return;
            if (s % 16 == 0)
            {
                static const char* bank[] = { "NEON", "PULSE", "WAVE", "SYNTH", "GLOW", "BASS", "CHORD", "DRIFT", "LASER", "ORBIT", "GRID", "SIGNAL",
                                              "ECHO", "PIXEL", "VOLT", "PRISM", "SPARK", "TEMPO", "RIFF", "GROOVE" };
                words.push_back ({ juce::String (bank[rnd.nextInt (20)]), 0, 0.0f, 0.15f + rnd.nextFloat() * 0.7f });
                mods[0] = std::min (1.0f, typedInBar / 10.0f); typedInBar = 0;
            }
        }
        void update (float dt) override
        {
            if (over) return;
            for (auto& w : words) { w.y += dt * 0.06f; if (w.y > 0.92f) { over = true; chord (-7, 0.8f, 2.0); } }
        }
        void draw (juce::Graphics& g, R a) override
        {
            for (size_t i = 0; i < words.size(); ++i)
            {
                const auto& w = words[i];
                const R r (a.getX() + w.x * a.getWidth() - 80, a.getY() + w.y * a.getHeight(), 160, 30);
                g.setFont (ui::fonts::display (22.0f));
                g.setColour (ui::colours::lime); g.drawText (w.text.substring (0, w.typed), r, juce::Justification::centredLeft);
                const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), w.text.substring (0, w.typed));
                g.setColour (i == 0 ? ui::colours::cyan : ui::colours::textDim); g.drawText (w.text.substring (w.typed), r.withTrimmedLeft ((float) tw), juce::Justification::centredLeft);
            }
            g.setColour (ui::colours::amber.withAlpha (0.4f)); g.fillRect (R (a.getX(), a.getY() + 0.92f * a.getHeight() + 30, a.getWidth(), 2));
            if (over) gameText (g, "OVERFLOW  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        struct Word { juce::String text; int typed; float y, x; };
        std::vector<Word> words;
        int typedInBar = 0;
        float t = 0;
    };

    //==========================================================================
    // 25. Push Box Chords
    class PushBoxChords final : public Game
    {
    public:
        const char* name() const override { return "Push Box Chords"; }
        const char* help() const override { return "ARROWS push the boxes. Each bar the boxes, read left to right, play a chord\nprogression: a box's column is its chord degree. Fill the targets to solve. Mod 1 = solved."; }
        void reset() override
        {
            static const char* level[] = { "##########", "#........#", "#.B..T...#", "#..##..B.#", "#.T....P.#", "#....B...#", "#.##...T.#", "#........#", "##########" };
            boxes.clear(); targets.clear();
            for (int r = 0; r < 9; ++r)
                for (int c = 0; c < 10; ++c)
                {
                    walls[(size_t) (r * 10 + c)] = level[r][c] == '#';
                    if (level[r][c] == 'B') boxes.push_back ({ c, r });
                    if (level[r][c] == 'T') targets.push_back ({ c, r });
                    if (level[r][c] == 'P') me = { c, r };
                }
            score = 0; over = false;
        }
        void keyDown (int k) override
        {
            if (k == K::leftKey) push ({ -1, 0 });
            if (k == K::rightKey) push ({ 1, 0 });
            if (k == K::upKey) push ({ 0, -1 });
            if (k == K::downKey) push ({ 0, 1 });
            if (k == K::spaceKey && over) reset();
        }
        void autoplay (float dt) override
        {
            t += dt; if (t < 0.4f) return; t = 0;
            if (over) { reset(); return; }
            const juce::Point<int> dirs[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            push (dirs[rnd.nextInt (4)]);
        }
        void tick (int s) override
        {
            if (s % 16 != 0) return;
            auto sorted = boxes;
            std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.x < b.x; });
            for (size_t i = 0; i < sorted.size(); ++i) prog[i] = sorted[i].x - 1;
            chordIdx = 0;
        }
        void update (float dt) override
        {
            beatAcc += dt;
            const float beatLen = host != nullptr ? 60.0f / host->bpm : 0.5f;
            if (chordIdx < 3 && beatAcc >= beatLen) { beatAcc = 0; chord (prog[(size_t) chordIdx], 0.6f, 0.9); ++chordIdx; }
            mods[0] = (float) solvedCount() / (float) targets.size();
        }
        void draw (juce::Graphics& g, R a) override
        {
            const float cs = std::min (a.getWidth() / 10.0f, a.getHeight() / 9.0f);
            const auto o = a.withSizeKeepingCentre (cs * 10, cs * 9);
            auto cell = [&] (juce::Point<int> p) { return R (o.getX() + p.x * cs + 2, o.getY() + p.y * cs + 2, cs - 4, cs - 4); };
            for (int i = 0; i < 90; ++i) if (walls[(size_t) i]) { g.setColour (ui::colours::magenta.withAlpha (0.35f)); g.fillRect (cell ({ i % 10, i / 10 })); }
            for (auto& tg : targets) { g.setColour (ui::colours::lime.withAlpha (0.5f)); g.drawRect (cell (tg), 2.0f); }
            for (size_t i = 0; i < boxes.size(); ++i) neonRect (g, cell (boxes[i]).reduced (3), std::find (targets.begin(), targets.end(), boxes[i]) != targets.end() ? ui::colours::lime : gameColour ((int) i + 3));
            neonCircle (g, cell (me).getCentre(), cs * 0.3f, ui::colours::cyan);
            if (over) gameText (g, "SOLVED  //  SPACE", a.removeFromTop (24), 14.0f, ui::colours::lime);
        }

    private:
        int solvedCount() const { int n = 0; for (auto& b : boxes) n += std::find (targets.begin(), targets.end(), b) != targets.end(); return n; }
        void push (juce::Point<int> d)
        {
            if (over) return;
            const auto n = me + d;
            if (walls[(size_t) (n.y * 10 + n.x)]) return;
            auto it = std::find (boxes.begin(), boxes.end(), n);
            if (it != boxes.end())
            {
                const auto nn = n + d;
                if (walls[(size_t) (nn.y * 10 + nn.x)] || std::find (boxes.begin(), boxes.end(), nn) != boxes.end()) return;
                *it = nn; note (nn.x - 1, 0.6f, 0.25); ++score;
                if (solvedCount() == (int) targets.size()) { over = true; score += 100; arp (0, 8); }
            }
            me = n;
        }
        std::array<bool, 90> walls {};
        std::vector<juce::Point<int>> boxes, targets;
        juce::Point<int> me;
        std::array<int, 8> prog {};
        int chordIdx = 3;
        float t = 0, beatAcc = 0;
    };
} // namespace

int numGames() { return 25; }

std::unique_ptr<Game> createGame (int i)
{
    std::unique_ptr<Game> g;
    if (i < 9) g = createGameA (i);
    else if (i < 17) g = createGameB (i);
    else
        switch (i)
        {
            case 17: g = std::make_unique<StackFour>(); break;
            case 18: g = std::make_unique<GemChords>(); break;
            case 19: g = std::make_unique<NeonRun>(); break;
            case 20: g = std::make_unique<GlitchWhack>(); break;
            case 21: g = std::make_unique<SynthwaveRacer>(); break;
            case 22: g = std::make_unique<FrequencyDefense>(); break;
            case 23: g = std::make_unique<BeatTyper>(); break;
            case 24: g = std::make_unique<PushBoxChords>(); break;
            default: break;
        }
    return g;
}

} // namespace mbx::games
