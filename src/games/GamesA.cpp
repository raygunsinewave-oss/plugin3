// XD games 1-9.
#include "Game.h"
#include "../ui/Theme.h"

namespace mbx::games
{
using K = juce::KeyPress;
using P = juce::Point<float>;
using R = juce::Rectangle<float>;

juce::Colour gameColour (int i)
{
    const juce::Colour c[] = { ui::colours::cyan, ui::colours::magenta, ui::colours::lime, ui::colours::amber,
                               juce::Colour (0xff8a5cff), juce::Colour (0xff3cff9e), juce::Colour (0xffff5a5a) };
    return c[(size_t) (((i % 7) + 7) % 7)];
}

void neonRect (juce::Graphics& g, R r, juce::Colour c, float glow)
{
    g.setColour (c.withAlpha (0.18f * glow));
    g.fillRect (r.expanded (2.0f));
    g.setColour (c.withAlpha (0.75f));
    g.fillRect (r);
    g.setColour (c.brighter (0.6f));
    g.drawRect (r, 1.0f);
}

void neonCircle (juce::Graphics& g, P c, float radius, juce::Colour col, bool fill)
{
    g.setColour (col.withAlpha (0.2f));
    g.fillEllipse (R (radius * 2.6f, radius * 2.6f).withCentre (c));
    g.setColour (col);
    if (fill) g.fillEllipse (R (radius * 2.0f, radius * 2.0f).withCentre (c));
    else g.drawEllipse (R (radius * 2.0f, radius * 2.0f).withCentre (c), 1.5f);
}

void gameText (juce::Graphics& g, const juce::String& t, R r, float size, juce::Colour c, juce::Justification j)
{
    g.setFont (ui::fonts::mono (size));
    g.setColour (c);
    g.drawText (t, r, j);
}

namespace
{
    const juce::Point<int> kDirs[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

    //==========================================================================
    // 1. Block Drop
    class BlockDrop final : public Game
    {
    public:
        const char* name() const override { return "Block Drop"; }
        const char* help() const override { return "LEFT/RIGHT move  UP rotate  DOWN soft drop  SPACE hard drop.\nEach landed piece plays a note, line clears play chords. Mod 1 = stack height."; }
        void reset() override { board.fill (0); score = 0; over = false; spawn(); }
        void update (float) override { mods[0] = height() / 20.0f; mods[1] = (float) linesFlash; if (linesFlash > 0.0f) linesFlash = std::max (0.0f, linesFlash - 0.05f); }
        void tick (int s) override { if (! over && s % 2 == 0) fall(); }
        void keyDown (int k) override
        {
            if (over) { if (k == K::spaceKey) reset(); return; }
            if (k == K::leftKey) tryMove (-1, 0, rot);
            else if (k == K::rightKey) tryMove (1, 0, rot);
            else if (k == K::upKey) tryMove (0, 0, (rot + 1) % 4);
            else if (k == K::downKey) fall();
            else if (k == K::spaceKey) { while (tryMove (0, 1, rot)) {} fall(); }
        }
        void autoplay (float dt) override
        {
            timer += dt;
            if (over) { if (timer > 1.0f) { reset(); timer = 0; } return; }
            if (timer < 0.12f) return;
            timer = 0;
            if (! planned) plan();
            if (rot != targetRot) tryMove (0, 0, (rot + 1) % 4);
            else if (x < targetX) tryMove (1, 0, rot);
            else if (x > targetX) tryMove (-1, 0, rot);
            else { while (tryMove (0, 1, rot)) {} fall(); }
        }
        void draw (juce::Graphics& g, R a) override
        {
            const float cs = std::min (a.getWidth() / 10.0f, a.getHeight() / 20.0f);
            const auto o = a.withSizeKeepingCentre (cs * 10, cs * 20);
            g.setColour (ui::colours::cyan.withAlpha (0.25f));
            g.drawRect (o, 1.0f);
            for (int r = 0; r < 20; ++r)
                for (int c = 0; c < 10; ++c)
                    if (board[(size_t) (r * 10 + c)]) neonRect (g, cell (o, cs, c, r), gameColour (board[(size_t) (r * 10 + c)]));
            if (! over)
                for (auto [cx, cy] : cells (piece, rot)) neonRect (g, cell (o, cs, x + cx, y + cy), gameColour (piece + 1), 1.5f);
            if (over) gameText (g, "STACK OVERFLOW  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        static R cell (R o, float cs, int c, int r) { return { o.getX() + c * cs + 1, o.getY() + r * cs + 1, cs - 2, cs - 2 }; }
        static std::array<std::pair<int, int>, 4> cells (int p, int r)
        {
            static const int shapes[7][4][2] = { { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 } }, { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },
                                                 { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }, { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } },
                                                 { { 2, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }, { { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 } },
                                                 { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 } } };
            std::array<std::pair<int, int>, 4> out {};
            for (int i = 0; i < 4; ++i)
            {
                int cx = shapes[p][i][0], cy = shapes[p][i][1];
                for (int k = 0; k < r; ++k) { const int t = cx; cx = (p == 1 ? cx : 2 - cy); cy = (p == 1 ? cy : t); }
                out[(size_t) i] = { cx, cy };
            }
            return out;
        }
        bool fits (int nx, int ny, int nr) const
        {
            for (auto [cx, cy] : cells (piece, nr))
            {
                const int c = nx + cx, r = ny + cy;
                if (c < 0 || c >= 10 || r >= 20) return false;
                if (r >= 0 && board[(size_t) (r * 10 + c)]) return false;
            }
            return true;
        }
        bool tryMove (int dx, int dy, int nr)
        {
            if (! fits (x + dx, y + dy, nr)) return false;
            x += dx; y += dy; rot = nr;
            return true;
        }
        void fall()
        {
            if (tryMove (0, 1, rot)) return;
            for (auto [cx, cy] : cells (piece, rot))
                if (y + cy >= 0) board[(size_t) ((y + cy) * 10 + x + cx)] = piece + 1;
            note (x + piece, 0.75f, 0.5);
            int cleared = 0;
            for (int r = 19; r >= 0; --r)
            {
                bool full = true;
                for (int c = 0; c < 10; ++c) full &= board[(size_t) (r * 10 + c)] != 0;
                if (! full) continue;
                ++cleared;
                for (int rr = r; rr > 0; --rr) for (int c = 0; c < 10; ++c) board[(size_t) (rr * 10 + c)] = board[(size_t) ((rr - 1) * 10 + c)];
                for (int c = 0; c < 10; ++c) board[(size_t) c] = 0;
                ++r;
            }
            if (cleared > 0) { chord (cleared * 2, 0.85f, 2.0, 2 + cleared); score += 100 * cleared * cleared; linesFlash = 1.0f; }
            score += 5;
            spawn();
        }
        void spawn()
        {
            piece = rnd.nextInt (7); rot = 0; x = 3; y = -1; planned = false;
            if (! fits (x, y, rot)) { over = true; chord (-7, 0.9f, 3.0); }
        }
        int height() const
        {
            for (int r = 0; r < 20; ++r) for (int c = 0; c < 10; ++c) if (board[(size_t) (r * 10 + c)]) return 20 - r;
            return 0;
        }
        void plan()
        {
            int best = -100000;
            for (int r = 0; r < 4; ++r)
                for (int c = -2; c < 10; ++c)
                {
                    if (! fits (c, y, r)) continue;
                    int yy = y;
                    while (fits (c, yy + 1, r)) ++yy;
                    int sc = yy * 10;
                    for (auto [cx, cy] : cells (piece, r)) { const int below = yy + cy + 1; if (below < 20 && ! board[(size_t) (below * 10 + c + cx)]) sc -= 15; }
                    if (sc > best) { best = sc; targetX = c; targetRot = r; }
                }
            planned = true;
        }
        std::array<int, 200> board {};
        int piece = 0, rot = 0, x = 3, y = 0, targetX = 3, targetRot = 0;
        bool planned = false;
        float timer = 0.0f, linesFlash = 0.0f;
    };

    //==========================================================================
    // 2. Neon Snake
    class NeonSnake final : public Game
    {
    public:
        const char* name() const override { return "Neon Snake"; }
        const char* help() const override { return "ARROWS steer. The snake steps on every 8th note; its head row is the pitch.\nFood adds a note (chord). Mod 1 = head height, Mod 2 = length."; }
        void reset() override { body = { { 8, 8 }, { 7, 8 }, { 6, 8 } }; dir = { 1, 0 }; score = 0; over = false; place(); }
        void update (float) override { mods[0] = 1.0f - body.front().y / 15.0f; mods[1] = std::min (1.0f, body.size() / 40.0f); }
        void keyDown (int k) override
        {
            if (over && k == K::spaceKey) { reset(); return; }
            if (k == K::leftKey && dir.x == 0) nd = { -1, 0 };
            if (k == K::rightKey && dir.x == 0) nd = { 1, 0 };
            if (k == K::upKey && dir.y == 0) nd = { 0, -1 };
            if (k == K::downKey && dir.y == 0) nd = { 0, 1 };
        }
        void tick (int s) override
        {
            if (over || s % 2) return;
            if (nd != juce::Point<int>()) { dir = nd; nd = {}; }
            auto head = body.front() + dir;
            head.x = (head.x + 16) % 16; head.y = (head.y + 16) % 16;
            for (auto& b : body) if (b == head) { over = true; chord (-7, 0.9f, 2.0); return; }
            body.insert (body.begin(), head);
            if (head == food) { score += 10; chord (15 - head.y, 0.8f, 1.0); place(); }
            else body.pop_back();
            note (15 - head.y, 0.6f, 0.4);
        }
        void autoplay (float) override
        {
            if (over) { reset(); return; }
            const juce::Point<int> opts[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            int best = 1 << 20; juce::Point<int> pick = dir;
            for (auto o : opts)
            {
                if (o == -dir) continue;
                auto h = body.front() + o; h.x = (h.x + 16) % 16; h.y = (h.y + 16) % 16;
                bool hit = false;
                for (auto& b : body) hit |= b == h;
                if (hit) continue;
                const int d = std::abs (h.x - food.x) + std::abs (h.y - food.y);
                if (d < best) { best = d; pick = o; }
            }
            nd = pick;
        }
        void draw (juce::Graphics& g, R a) override
        {
            const float cs = std::min (a.getWidth(), a.getHeight()) / 16.0f;
            const auto o = a.withSizeKeepingCentre (cs * 16, cs * 16);
            g.setColour (ui::colours::cyan.withAlpha (0.08f));
            for (int i = 0; i <= 16; ++i) { g.drawVerticalLine ((int) (o.getX() + i * cs), o.getY(), o.getBottom()); g.drawHorizontalLine ((int) (o.getY() + i * cs), o.getX(), o.getRight()); }
            for (size_t i = 0; i < body.size(); ++i)
                neonRect (g, { o.getX() + body[i].x * cs + 2, o.getY() + body[i].y * cs + 2, cs - 4, cs - 4 }, i == 0 ? ui::colours::lime : ui::colours::cyan.withAlpha (1.0f - 0.5f * (float) i / (float) body.size()));
            neonCircle (g, { o.getX() + (food.x + 0.5f) * cs, o.getY() + (food.y + 0.5f) * cs }, cs * 0.3f, ui::colours::magenta);
            if (over) gameText (g, "TANGLED  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        void place() { do food = { rnd.nextInt (16), rnd.nextInt (16) }; while (std::find (body.begin(), body.end(), food) != body.end()); }
        std::vector<juce::Point<int>> body;
        juce::Point<int> dir { 1, 0 }, nd, food;
    };

    //==========================================================================
    // 3. Pulse Pong
    class PulsePong final : public Game
    {
    public:
        const char* name() const override { return "Pulse Pong"; }
        const char* help() const override { return "UP/DOWN or mouse move your paddle (left). Paddle hits play notes by hit position.\nMod 1 = your paddle height, Mod 2 = ball X."; }
        void reset() override { ball = { 0.5f, 0.5f }; vel = { 0.45f, 0.25f }; me = cpu = 0.5f; score = 0; over = false; }
        void drag (P p) override { me = juce::jlimit (0.1f, 0.9f, p.y); }
        void click (P p, bool) override { drag (p); }
        void keyDown (int k) override { if (k == K::upKey) mv = -1; if (k == K::downKey) mv = 1; }
        void keyUp (int) override { mv = 0; }
        void autoplay (float) override { me += (ball.y - me) * 0.08f; }
        void update (float dt) override
        {
            me = juce::jlimit (0.1f, 0.9f, me + mv * dt * 1.2f);
            cpu += juce::jlimit (-dt * 0.7f, dt * 0.7f, ball.y - cpu);
            ball += vel * dt;
            if (ball.y < 0.02f || ball.y > 0.98f) { vel.y = -vel.y; ball.y = juce::jlimit (0.02f, 0.98f, ball.y); note (0, 0.4f, 0.25); }
            auto paddle = [&] (float py, float side) {
                const float off = (ball.y - py) / 0.1f;
                if (std::abs (off) <= 1.0f) { vel.x = -vel.x * 1.04f; vel.y += off * 0.25f; note ((int) ((1.0f - ball.y) * 10.0f) + (side > 0 ? 2 : 0), 0.8f, 0.5); return true; }
                return false;
            };
            if (ball.x < 0.05f && vel.x < 0) { if (paddle (me, -1)) ++score; else { chord (-3, 0.8f, 1.0); reset(); } }
            if (ball.x > 0.95f && vel.x > 0) { if (! paddle (cpu, 1)) { score += 5; chord (7, 0.8f, 1.0); ball = { 0.5f, 0.5f }; vel = { -0.45f, 0.2f }; } }
            vel.x = juce::jlimit (-1.6f, 1.6f, vel.x);
            mods[0] = 1.0f - me; mods[1] = ball.x;
        }
        void draw (juce::Graphics& g, R a) override
        {
            auto pt = [&] (float x, float y) { return P (a.getX() + x * a.getWidth(), a.getY() + y * a.getHeight()); };
            g.setColour (ui::colours::cyan.withAlpha (0.2f));
            for (float y = 0; y < 1.0f; y += 0.05f) g.fillRect (R (pt (0.5f, y).x - 1, pt (0.5f, y).y, 2, a.getHeight() * 0.025f));
            neonRect (g, R (8, a.getHeight() * 0.2f).withCentre (pt (0.04f, me)), ui::colours::lime);
            neonRect (g, R (8, a.getHeight() * 0.2f).withCentre (pt (0.96f, cpu)), ui::colours::magenta);
            neonCircle (g, pt (ball.x, ball.y), 6.0f, ui::colours::cyan);
        }

    private:
        P ball, vel;
        float me = 0.5f, cpu = 0.5f, mv = 0.0f;
    };

    //==========================================================================
    // 4. Brick Breaker
    class BrickBreaker final : public Game
    {
    public:
        const char* name() const override { return "Brick Breaker"; }
        const char* help() const override { return "Mouse or LEFT/RIGHT moves the paddle. Each brick row is a scale degree;\nconsecutive bricks before the paddle build an arpeggio. Mod 1 = combo."; }
        void reset() override { for (auto& b : bricks) b = true; ball = { 0.5f, 0.7f }; vel = { 0.3f, -0.45f }; paddle = 0.5f; combo = 0; score = 0; over = false; }
        void drag (P p) override { paddle = juce::jlimit (0.08f, 0.92f, p.x); }
        void click (P p, bool) override { drag (p); }
        void keyDown (int k) override { mv = k == K::leftKey ? -1.0f : k == K::rightKey ? 1.0f : mv; if (over && k == K::spaceKey) reset(); }
        void keyUp (int) override { mv = 0; }
        void autoplay (float) override { paddle += (ball.x - paddle) * 0.15f; if (over) reset(); }
        void update (float dt) override
        {
            if (over) return;
            paddle = juce::jlimit (0.08f, 0.92f, paddle + mv * dt * 1.3f);
            ball += vel * dt;
            if (ball.x < 0.01f || ball.x > 0.99f) vel.x = -vel.x;
            if (ball.y < 0.01f) vel.y = std::abs (vel.y);
            if (ball.y > 0.93f && ball.y < 0.97f && std::abs (ball.x - paddle) < 0.09f && vel.y > 0)
            { vel.y = -std::abs (vel.y); vel.x += (ball.x - paddle) * 3.0f; combo = 0; note (0, 0.5f, 0.25); }
            if (ball.y > 1.0f) { over = true; chord (-5, 0.8f, 2.0); }
            const int c = (int) (ball.x * 10), r = (int) ((ball.y - 0.08f) / 0.05f);
            if (r >= 0 && r < 6 && c >= 0 && c < 10 && bricks[(size_t) (r * 10 + c)])
            {
                bricks[(size_t) (r * 10 + c)] = false;
                vel.y = -vel.y;
                note ((5 - r) * 2 + combo, 0.8f, 0.4);
                ++combo; score += 10 * combo;
                if (std::count (bricks.begin(), bricks.end(), true) == 0) { arp (0, 8); reset(); }
            }
            mods[0] = std::min (1.0f, combo / 8.0f); mods[1] = paddle;
        }
        void draw (juce::Graphics& g, R a) override
        {
            for (int r = 0; r < 6; ++r)
                for (int c = 0; c < 10; ++c)
                    if (bricks[(size_t) (r * 10 + c)])
                        neonRect (g, { a.getX() + c * a.getWidth() / 10 + 2, a.getY() + (0.08f + r * 0.05f) * a.getHeight(), a.getWidth() / 10 - 4, a.getHeight() * 0.04f }, gameColour (r));
            neonRect (g, R (a.getWidth() * 0.18f, 8).withCentre ({ a.getX() + paddle * a.getWidth(), a.getY() + 0.95f * a.getHeight() }), ui::colours::lime);
            neonCircle (g, { a.getX() + ball.x * a.getWidth(), a.getY() + ball.y * a.getHeight() }, 6.0f, ui::colours::cyan);
            if (over) gameText (g, "GAME OVER  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        std::array<bool, 60> bricks {};
        P ball, vel;
        float paddle = 0.5f, mv = 0.0f;
        int combo = 0;
    };

    //==========================================================================
    // 5. Asteroid Drift
    class AsteroidDrift final : public Game
    {
    public:
        const char* name() const override { return "Asteroid Drift"; }
        const char* help() const override { return "LEFT/RIGHT rotate  UP thrust  SPACE fire. Shots play plucks;\nasteroid size sets pitch & octave. Mod 1 = ship speed, Mod 2 = rock count."; }
        void reset() override { ship = { 0.5f, 0.5f }; sv = {}; angle = 0; rocks.clear(); shots.clear(); for (int i = 0; i < 5; ++i) addRock (3, { rnd.nextFloat(), rnd.nextFloat() * 0.3f }); score = 0; over = false; }
        void keyDown (int k) override
        {
            if (k == K::leftKey) turn = -1;
            if (k == K::rightKey) turn = 1;
            if (k == K::upKey) thrust = true;
            if (k == K::spaceKey) { if (over) reset(); else fire(); }
        }
        void keyUp (int k) override { if (k == K::leftKey || k == K::rightKey) turn = 0; if (k == K::upKey) thrust = false; }
        void autoplay (float dt) override
        {
            if (over) { reset(); return; }
            if (rocks.empty()) return;
            const auto& t = rocks.front();
            const float want = std::atan2 (t.p.y - ship.y, t.p.x - ship.x);
            float d = want - angle;
            while (d > 3.14159f) d -= 6.28318f;
            while (d < -3.14159f) d += 6.28318f;
            turn = d > 0.1f ? 1.0f : d < -0.1f ? -1.0f : 0.0f;
            cool -= dt;
            if (std::abs (d) < 0.3f && cool <= 0) { fire(); cool = 0.35f; }
        }
        void update (float dt) override
        {
            if (over) return;
            angle += turn * dt * 3.5f;
            if (thrust) sv += P (std::cos (angle), std::sin (angle)) * dt * 0.6f;
            sv *= 0.99f; ship += sv * dt; wrap (ship);
            for (auto& s : shots) { s.p += s.v * dt; s.life -= dt; wrap (s.p); }
            shots.erase (std::remove_if (shots.begin(), shots.end(), [] (auto& s) { return s.life <= 0; }), shots.end());
            for (auto& r : rocks) { r.p += r.v * dt; wrap (r.p); if (r.p.getDistanceFrom (ship) < r.size * 0.03f) { over = true; chord (-7, 0.9f, 2.0); } }
            for (size_t si = 0; si < shots.size(); ++si)
                for (size_t ri = 0; ri < rocks.size(); ++ri)
                    if (shots[si].p.getDistanceFrom (rocks[ri].p) < rocks[ri].size * 0.03f)
                    {
                        auto r = rocks[ri];
                        note ((3 - r.size) * 7 + rnd.nextInt (3), 0.85f, 0.75);
                        score += 10 * (4 - r.size);
                        rocks.erase (rocks.begin() + (long) ri);
                        shots[si].life = 0;
                        if (r.size > 1) { addRock (r.size - 1, r.p); addRock (r.size - 1, r.p); }
                        break;
                    }
            if (rocks.empty()) { chord (7, 0.8f, 2.0); for (int i = 0; i < 6; ++i) addRock (3, { rnd.nextFloat(), 0.0f }); }
            mods[0] = std::min (1.0f, sv.getDistanceFromOrigin() * 3.0f); mods[1] = std::min (1.0f, rocks.size() / 16.0f);
        }
        void draw (juce::Graphics& g, R a) override
        {
            auto pt = [&] (P p) { return P (a.getX() + p.x * a.getWidth(), a.getY() + p.y * a.getHeight()); };
            for (auto& r : rocks) neonCircle (g, pt (r.p), r.size * 0.03f * a.getWidth(), gameColour (r.size + 2), false);
            for (auto& s : shots) neonCircle (g, pt (s.p), 2.0f, ui::colours::lime);
            juce::Path sh;
            const auto c = pt (ship);
            sh.addTriangle (c + P (std::cos (angle), std::sin (angle)) * 14.0f, c + P (std::cos (angle + 2.5f), std::sin (angle + 2.5f)) * 10.0f, c + P (std::cos (angle - 2.5f), std::sin (angle - 2.5f)) * 10.0f);
            g.setColour (ui::colours::cyan); g.strokePath (sh, juce::PathStrokeType (2.0f));
            if (over) gameText (g, "HULL BREACH  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        struct Rock { P p, v; int size; };
        struct Shot { P p, v; float life; };
        static void wrap (P& p) { p.x -= std::floor (p.x); p.y -= std::floor (p.y); }
        void addRock (int s, P p) { const float a = rnd.nextFloat() * 6.28f; rocks.push_back ({ p, P (std::cos (a), std::sin (a)) * (0.05f + 0.1f / s), s }); }
        void fire() { shots.push_back ({ ship, P (std::cos (angle), std::sin (angle)) * 0.9f, 0.9f }); note (14, 0.4f, 0.125); }
        std::vector<Rock> rocks;
        std::vector<Shot> shots;
        P ship, sv;
        float angle = 0, turn = 0, cool = 0;
        bool thrust = false;
    };

    //==========================================================================
    // 6. Grid Raiders
    class GridRaiders final : public Game
    {
    public:
        const char* name() const override { return "Grid Raiders"; }
        const char* help() const override { return "LEFT/RIGHT move  SPACE fire. The invading rows march as a rhythmic sequence\n(each step plays a column note) and speed up as they thin out. Mod 1 = invasion depth."; }
        void reset() override { for (auto& a : alive) a = true; ox = 0; oy = 0; dir = 1; player = 0.5f; shots.clear(); score = 0; over = false; stepEvery = 4; }
        void keyDown (int k) override { if (k == K::leftKey) mv = -1; if (k == K::rightKey) mv = 1; if (k == K::spaceKey) { if (over) reset(); else shoot(); } }
        void keyUp (int) override { mv = 0; }
        void autoplay (float dt) override
        {
            if (over) { reset(); return; }
            float tx = 0.5f; for (int i = 0; i < 40; ++i) if (alive[(size_t) i]) { tx = colX (i % 8); break; }
            mv = tx > player + 0.02f ? 1.0f : tx < player - 0.02f ? -1.0f : 0.0f;
            cool -= dt; if (cool <= 0 && std::abs (tx - player) < 0.04f) { shoot(); cool = 0.4f; }
        }
        void tick (int s) override
        {
            if (over || s % stepEvery) return;
            ox += dir * 0.02f;
            int col = step++ % 8;
            for (int r = 4; r >= 0; --r) if (alive[(size_t) (r * 8 + col)]) { note (col + (4 - r), 0.55f + 0.1f * (float) (4 - r), 0.25); break; }
            if (ox > 0.12f || ox < -0.12f) { dir = -dir; oy += 0.04f; note (-7, 0.7f, 0.5); }
            if (oy > 0.45f) { over = true; chord (-7, 0.9f, 2.0); }
        }
        void update (float dt) override
        {
            player = juce::jlimit (0.05f, 0.95f, player + mv * dt * 0.8f);
            for (auto& s : shots) s.y -= dt * 1.2f;
            for (auto& s : shots)
                for (int i = 0; i < 40; ++i)
                    if (alive[(size_t) i] && std::abs (s.x - colX (i % 8)) < 0.035f && std::abs (s.y - rowY (i / 8)) < 0.03f)
                    { alive[(size_t) i] = false; s.y = -1; score += 20; note (i % 8 + 7, 0.8f, 0.25); }
            shots.erase (std::remove_if (shots.begin(), shots.end(), [] (auto& s) { return s.y < 0; }), shots.end());
            const int left = (int) std::count (alive.begin(), alive.end(), true);
            stepEvery = left > 25 ? 4 : left > 12 ? 2 : 1;
            if (left == 0) { arp (0, 6); reset(); }
            mods[0] = oy / 0.45f; mods[1] = player;
        }
        void draw (juce::Graphics& g, R a) override
        {
            for (int i = 0; i < 40; ++i)
                if (alive[(size_t) i]) neonRect (g, R (a.getWidth() * 0.06f, a.getHeight() * 0.035f).withCentre ({ a.getX() + colX (i % 8) * a.getWidth(), a.getY() + rowY (i / 8) * a.getHeight() }), gameColour (i / 8));
            for (auto& s : shots) neonRect (g, R (3, 10).withCentre ({ a.getX() + s.x * a.getWidth(), a.getY() + s.y * a.getHeight() }), ui::colours::lime);
            neonRect (g, R (a.getWidth() * 0.07f, 10).withCentre ({ a.getX() + player * a.getWidth(), a.getY() + 0.93f * a.getHeight() }), ui::colours::cyan);
            if (over) gameText (g, "GRID LOST  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        float colX (int c) const { return 0.2f + c * 0.085f + ox; }
        float rowY (int r) const { return 0.1f + r * 0.07f + oy; }
        void shoot() { if (shots.size() < 3) { shots.push_back ({ player, 0.9f }); note (21, 0.3f, 0.125); } }
        std::array<bool, 40> alive {};
        std::vector<P> shots;
        float ox = 0, oy = 0, dir = 1, player = 0.5f, mv = 0, cool = 0;
        int stepEvery = 4, step = 0;
    };

    //==========================================================================
    // 7. Maze Muncher
    class MazeMuncher final : public Game
    {
    public:
        const char* name() const override { return "Maze Muncher"; }
        const char* help() const override { return "ARROWS move. Pellets play rising arpeggio notes; power pellets open the filter\n(Mod 1 jumps to 1 and decays). Avoid the drones. Mod 2 = pellets left."; }
        void reset() override
        {
            static const char* layout[] = { "###############", "#o.....#.....o#", "#.###.#.#.###.#", "#.............#", "#.#.#.###.#.#.#",
                                            "#...#..#..#...#", "###.##.#.##.###", "#......P......#", "#.###.###.###.#", "#o....#.#....o#",
                                            "###############" };
            for (int r = 0; r < 11; ++r) for (int c = 0; c < 15; ++c) { grid[(size_t) r][(size_t) c] = layout[r][c]; if (layout[r][c] == 'P') { me = { c, r }; grid[(size_t) r][(size_t) c] = ' '; } }
            ghosts = { { 1, 3 }, { 13, 3 } }; score = 0; over = false; power = 0; chain = 0;
        }
        void keyDown (int k) override
        {
            if (over && k == K::spaceKey) { reset(); return; }
            if (k == K::leftKey) want = { -1, 0 };
            if (k == K::rightKey) want = { 1, 0 };
            if (k == K::upKey) want = { 0, -1 };
            if (k == K::downKey) want = { 0, 1 };
        }
        void autoplay (float) override
        {
            if (over) { reset(); return; }
            // BFS to nearest pellet
            std::array<std::array<int, 15>, 11> dist {}; for (auto& r : dist) r.fill (-1);
            std::vector<juce::Point<int>> q { me }; dist[(size_t) me.y][(size_t) me.x] = 0;
            juce::Point<int> target = me;
            for (size_t i = 0; i < q.size(); ++i)
            {
                auto p = q[i];
                if (grid[(size_t) p.y][(size_t) p.x] == '.' || grid[(size_t) p.y][(size_t) p.x] == 'o') { target = p; break; }
                for (auto d : kDirs)
                {
                    auto n = p + d;
                    if (grid[(size_t) n.y][(size_t) n.x] != '#' && dist[(size_t) n.y][(size_t) n.x] < 0) { dist[(size_t) n.y][(size_t) n.x] = dist[(size_t) p.y][(size_t) p.x] + 1; q.push_back (n); }
                }
            }
            while (dist[(size_t) target.y][(size_t) target.x] > 1)
                for (auto d : kDirs)
                {
                    auto n = target + d;
                    if (dist[(size_t) n.y][(size_t) n.x] == dist[(size_t) target.y][(size_t) target.x] - 1) { target = n; break; }
                }
            want = target - me;
        }
        void tick (int s) override
        {
            if (over || s % 2) return;
            auto n = me + want;
            if (grid[(size_t) n.y][(size_t) n.x] != '#') me = n;
            char& c = grid[(size_t) me.y][(size_t) me.x];
            if (c == '.') { c = ' '; score += 10; note (chain++ % 10, 0.6f, 0.25); }
            if (c == 'o') { c = ' '; score += 50; power = 1.0f; chord (4, 0.85f, 1.0); }
            for (auto& g : ghosts)
            {
                juce::Point<int> best = g; int bd = 1 << 20;
                for (auto d : kDirs)
                {
                    auto m = g + d;
                    if (grid[(size_t) m.y][(size_t) m.x] == '#') continue;
                    const int dd = std::abs (m.x - me.x) + std::abs (m.y - me.y) + rnd.nextInt (4);
                    if (power > 0 ? -dd < bd : dd < bd) { bd = power > 0 ? -dd : dd; best = m; }
                }
                if (s % 4 == 0) g = best;
                if (g == me) { if (power > 0) { g = { 7, 5 }; score += 200; arp (7, 4); } else { over = true; chord (-7, 0.9f, 2.0); } }
            }
            int left = 0; for (auto& r : grid) for (char ch : r) left += ch == '.' || ch == 'o';
            if (left == 0) { arp (0, 8); reset(); }
            mods[1] = left / 100.0f;
        }
        void update (float dt) override { power = std::max (0.0f, power - dt * 0.15f); mods[0] = power; }
        void draw (juce::Graphics& g, R a) override
        {
            const float cs = std::min (a.getWidth() / 15.0f, a.getHeight() / 11.0f);
            const auto o = a.withSizeKeepingCentre (cs * 15, cs * 11);
            for (int r = 0; r < 11; ++r)
                for (int c = 0; c < 15; ++c)
                {
                    const R cell (o.getX() + c * cs, o.getY() + r * cs, cs, cs);
                    const char ch = grid[(size_t) r][(size_t) c];
                    if (ch == '#') { g.setColour (ui::colours::magenta.withAlpha (0.35f)); g.fillRect (cell.reduced (1)); }
                    else if (ch == '.') neonCircle (g, cell.getCentre(), 2.0f, ui::colours::text);
                    else if (ch == 'o') neonCircle (g, cell.getCentre(), 5.0f, ui::colours::lime);
                }
            neonCircle (g, { o.getX() + (me.x + 0.5f) * cs, o.getY() + (me.y + 0.5f) * cs }, cs * 0.38f, ui::colours::amber);
            for (auto& gh : ghosts) neonCircle (g, { o.getX() + (gh.x + 0.5f) * cs, o.getY() + (gh.y + 0.5f) * cs }, cs * 0.35f, power > 0 ? ui::colours::cyan : ui::colours::magenta, power > 0);
            if (over) gameText (g, "CAUGHT  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        std::array<std::array<char, 15>, 11> grid {};
        juce::Point<int> me, want;
        std::vector<juce::Point<int>> ghosts;
        float power = 0;
        int chain = 0;
    };

    //==========================================================================
    // 8. Wave Rider
    class WaveRider final : public Game
    {
    public:
        const char* name() const override { return "Wave Rider"; }
        const char* help() const override { return "SPACE / click to flap. Height is pitch (a note every beat; Mod 1 = height);\nclearing a gate plays a chord; crashes spike Mod 4 for glitch FX."; }
        void reset() override { y = 0.5f; vy = 0; gates.clear(); score = 0; over = false; spawn = 0; }
        void keyDown (int k) override { if (k == K::spaceKey || k == K::upKey) flap(); }
        void click (P, bool) override { flap(); }
        void autoplay (float) override
        {
            if (over) { reset(); return; }
            float target = 0.5f;
            for (auto& gt : gates) if (gt.x > 0.15f) { target = gt.gap; break; }
            if (y > target + 0.05f && vy > -0.1f) flap();
        }
        void tick (int s) override { if (! over && s % 4 == 0) note ((int) ((1.0f - y) * 14.0f), 0.55f, 0.5); }
        void update (float dt) override
        {
            mods[3] = std::max (0.0f, mods[3] - dt * 2.0f);
            if (over) return;
            vy += dt * 1.4f; y += vy * dt;
            spawn -= dt;
            if (spawn <= 0) { gates.push_back ({ 1.05f, 0.25f + rnd.nextFloat() * 0.5f, false }); spawn = 1.6f; }
            for (auto& gt : gates)
            {
                gt.x -= dt * 0.35f;
                if (! gt.passed && gt.x < 0.2f) { gt.passed = true; score += 10; chord ((int) ((1.0f - gt.gap) * 10.0f), 0.75f, 1.0); }
                if (std::abs (gt.x - 0.2f) < 0.04f && std::abs (y - gt.gap) > 0.12f) crash();
            }
            gates.erase (std::remove_if (gates.begin(), gates.end(), [] (auto& gt) { return gt.x < -0.1f; }), gates.end());
            if (y > 1.0f || y < 0.0f) crash();
            mods[0] = 1.0f - juce::jlimit (0.0f, 1.0f, y);
        }
        void draw (juce::Graphics& g, R a) override
        {
            for (auto& gt : gates)
            {
                const float x = a.getX() + gt.x * a.getWidth();
                neonRect (g, { x - 10, a.getY(), 20, (gt.gap - 0.12f) * a.getHeight() }, ui::colours::magenta);
                neonRect (g, { x - 10, a.getY() + (gt.gap + 0.12f) * a.getHeight(), 20, a.getHeight() * (0.88f - gt.gap) }, ui::colours::magenta);
            }
            neonCircle (g, { a.getX() + 0.2f * a.getWidth(), a.getY() + y * a.getHeight() }, 9.0f, ui::colours::lime);
            if (over) gameText (g, "WIPEOUT  //  SPACE", a, 18.0f, ui::colours::amber);
        }

    private:
        struct Gate { float x, gap; bool passed; };
        void flap() { if (over) { reset(); return; } vy = -0.55f; note (14, 0.3f, 0.125); }
        void crash() { over = true; mods[3] = 1.0f; chord (-7, 0.9f, 2.0); }
        std::vector<Gate> gates;
        float y = 0.5f, vy = 0, spawn = 0;
    };

    //==========================================================================
    // 9. Life Cells
    class LifeCells final : public Game
    {
    public:
        const char* name() const override { return "Life Cells"; }
        const char* help() const override { return "Click cells to toggle. A playhead scans one column per 16th; live cells sing\n(row = pitch). A new generation evolves every bar. Mod 1 = population."; }
        void reset() override { cells.fill (false); for (int i = 0; i < 60; ++i) cells[(size_t) rnd.nextInt (256)] = true; score = 0; over = false; }
        void click (P p, bool) override
        {
            const int c = juce::jlimit (0, 15, (int) (p.x * 16)), r = juce::jlimit (0, 15, (int) (p.y * 16));
            cells[(size_t) (r * 16 + c)] = ! cells[(size_t) (r * 16 + c)];
        }
        void autoplay (float) override { if (std::count (cells.begin(), cells.end(), true) < 12) reset(); }
        void tick (int s) override
        {
            col = s % 16;
            int voices = 0;
            for (int r = 15; r >= 0 && voices < 3; --r)
                if (cells[(size_t) (r * 16 + col)]) { note (15 - r, 0.5f + 0.05f * (float) voices, 0.25); ++voices; }
            if (col == 15) evolve();
        }
        void update (float) override { mods[0] = (float) std::count (cells.begin(), cells.end(), true) / 128.0f; mods[1] = col / 15.0f; }
        void draw (juce::Graphics& g, R a) override
        {
            const float cs = std::min (a.getWidth(), a.getHeight()) / 16.0f;
            const auto o = a.withSizeKeepingCentre (cs * 16, cs * 16);
            g.setColour (ui::colours::lime.withAlpha (0.12f));
            g.fillRect (R (o.getX() + col * cs, o.getY(), cs, o.getHeight()));
            for (int i = 0; i < 256; ++i)
                if (cells[(size_t) i]) neonRect (g, { o.getX() + (i % 16) * cs + 2, o.getY() + (i / 16) * cs + 2, cs - 4, cs - 4 }, (i % 16) == col ? ui::colours::lime : ui::colours::cyan);
            gameText (g, "GEN " + juce::String (score), a.removeFromTop (20), 12.0f, ui::colours::textDim, juce::Justification::topRight);
        }

    private:
        void evolve()
        {
            std::array<bool, 256> next {};
            for (int r = 0; r < 16; ++r)
                for (int c = 0; c < 16; ++c)
                {
                    int n = 0;
                    for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc)
                        if (dr || dc) n += cells[(size_t) (((r + dr + 16) % 16) * 16 + (c + dc + 16) % 16)];
                    const bool alive = cells[(size_t) (r * 16 + c)];
                    next[(size_t) (r * 16 + c)] = alive ? (n == 2 || n == 3) : n == 3;
                }
            cells = next;
            ++score;
        }
        std::array<bool, 256> cells {};
        int col = 0;
    };
} // namespace

std::unique_ptr<Game> createGameA (int i)
{
    switch (i)
    {
        case 0: return std::make_unique<BlockDrop>();
        case 1: return std::make_unique<NeonSnake>();
        case 2: return std::make_unique<PulsePong>();
        case 3: return std::make_unique<BrickBreaker>();
        case 4: return std::make_unique<AsteroidDrift>();
        case 5: return std::make_unique<GridRaiders>();
        case 6: return std::make_unique<MazeMuncher>();
        case 7: return std::make_unique<WaveRider>();
        case 8: return std::make_unique<LifeCells>();
        default: return nullptr;
    }
}

} // namespace mbx::games
