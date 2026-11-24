// Cyberpunk / HUD palette + drawing helpers shared by every UI component.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mbx::ui
{
namespace colours
{
    inline const juce::Colour base     { 0xff07080d }; // near-black graphite
    inline const juce::Colour glass    { 0x1a9fdcff }; // smoked glass panel fill
    inline const juce::Colour glassHi  { 0x26c8f4ff };
    inline const juce::Colour cyan     { 0xff00f0ff }; // primary accent
    inline const juce::Colour magenta  { 0xffff2bd6 }; // secondary accent
    inline const juce::Colour lime     { 0xffb6ff00 }; // active / modulated
    inline const juce::Colour amber    { 0xffffb020 }; // warnings
    inline const juce::Colour text     { 0xffd9f7ff };
    inline const juce::Colour textDim  { 0xff6f8c9c };
    inline const juce::Colour track    { 0xff1a2230 };
    inline const juce::Colour screen   { 0xff081019 }; // display background
} // namespace colours

namespace fonts
{
    /** Geometric display face for headers (bundled techno font arrives with the Phase 4 UI pass). */
    inline juce::Font display (float height)
    {
        return juce::Font (juce::FontOptions (height, juce::Font::bold)).withExtraKerningFactor (0.12f);
    }
    /** Monospaced face for values and HUD readouts. */
    inline juce::Font mono (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
    }
} // namespace fonts

/** Rectangle with angled (chamfered) top-left and bottom-right corners. */
inline juce::Path chamferedRect (juce::Rectangle<float> r, float cut)
{
    cut = juce::jmin (cut, r.getWidth() * 0.5f, r.getHeight() * 0.5f);
    juce::Path p;
    p.startNewSubPath (r.getX() + cut, r.getY());
    p.lineTo (r.getRight(), r.getY());
    p.lineTo (r.getRight(), r.getBottom() - cut);
    p.lineTo (r.getRight() - cut, r.getBottom());
    p.lineTo (r.getX(), r.getBottom());
    p.lineTo (r.getX(), r.getY() + cut);
    p.closeSubPath();
    return p;
}

/** Thin neon stroke with a soft multi-pass glow. */
inline void strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float thickness, float glow = 1.0f)
{
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (c.withAlpha (0.07f * glow));
        g.strokePath (p, juce::PathStrokeType (thickness + (float) i * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

/** HUD-style L-shaped brackets on each corner. */
inline void drawCornerBrackets (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float len = 10.0f, float thick = 1.5f)
{
    g.setColour (c);
    const float x0 = r.getX(), y0 = r.getY(), x1 = r.getRight(), y1 = r.getBottom();
    g.fillRect (x0, y0, len, thick);          g.fillRect (x0, y0, thick, len);
    g.fillRect (x1 - len, y0, len, thick);    g.fillRect (x1 - thick, y0, thick, len);
    g.fillRect (x0, y1 - thick, len, thick);  g.fillRect (x0, y1 - len, thick, len);
    g.fillRect (x1 - len, y1 - thick, len, thick); g.fillRect (x1 - thick, y1 - len, thick, len);
}

/** Diagonal warning-tape hatching clipped to r. */
inline void drawHatch (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float spacing = 6.0f)
{
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (c);
    for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += spacing)
        g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 1.0f);
}

/** Small status LED. */
inline void drawLed (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour c, bool on)
{
    if (on)
    {
        g.setColour (c.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (radius * 4.0f, radius * 4.0f).withCentre (centre));
    }
    g.setColour (on ? c : c.withAlpha (0.2f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
}

/** Tick ruler along the bottom edge of r (used on displays and panel headers). */
inline void drawTickRuler (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, int ticks = 40)
{
    for (int i = 0; i <= ticks; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) ticks;
        const float h = (i % 10 == 0) ? r.getHeight() : (i % 5 == 0 ? r.getHeight() * 0.6f : r.getHeight() * 0.35f);
        g.setColour (c.withAlpha (i % 5 == 0 ? 0.5f : 0.25f));
        g.fillRect (x, r.getBottom() - h, 1.0f, h);
    }
}

/**
    Smoked-glass panel with chamfered corners, glowing outline, HUD brackets, a header tag
    ("[01] OSCILLATORS"), hatching, and a right-aligned hex section code.
*/
inline void drawTechPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title,
                           juce::Colour accent = colours::cyan, const juce::String& code = {}, bool header = true)
{
    auto path = chamferedRect (r.reduced (1.0f), 12.0f);
    g.setGradientFill (juce::ColourGradient (colours::glassHi, r.getX(), r.getY(), colours::glass.withAlpha (0.04f), r.getX(), r.getBottom(), false));
    g.fillPath (path);

    // faint inner micro-grid
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (path);
        g.setColour (accent.withAlpha (0.025f));
        for (float x = r.getX() + 8.0f; x < r.getRight(); x += 16.0f)
            g.fillRect (x, r.getY(), 1.0f, r.getHeight());
        for (float y = r.getY() + 8.0f; y < r.getBottom(); y += 16.0f)
            g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
    }

    g.setColour (accent.withAlpha (0.08f));
    g.strokePath (path, juce::PathStrokeType (4.0f));
    g.setColour (accent.withAlpha (0.5f));
    g.strokePath (path, juce::PathStrokeType (1.0f));
    drawCornerBrackets (g, r.reduced (4.0f), accent.withAlpha (0.9f), 9.0f, 1.5f);

    if (header && title.isNotEmpty())
    {
        const auto font = fonts::display (11.5f);
        const float tw = (float) juce::GlyphArrangement::getStringWidthInt (font, title) + 26.0f;
        auto tag = juce::Rectangle<float> (r.getX() + 14.0f, r.getY() + 6.0f, tw, 16.0f);
        juce::Path tp;
        tp.startNewSubPath (tag.getX(), tag.getY());
        tp.lineTo (tag.getRight() + 8.0f, tag.getY());
        tp.lineTo (tag.getRight(), tag.getBottom());
        tp.lineTo (tag.getX(), tag.getBottom());
        tp.closeSubPath();
        g.setColour (accent.withAlpha (0.16f));
        g.fillPath (tp);
        g.setColour (accent);
        g.fillRect (tag.getX(), tag.getY(), 3.0f, tag.getHeight());
        g.setFont (font);
        g.drawText (title, tag.withTrimmedLeft (10.0f), juce::Justification::centredLeft);

        auto hatch = juce::Rectangle<float> (tag.getRight() + 14.0f, tag.getY() + 4.0f, 46.0f, 8.0f);
        drawHatch (g, hatch, accent.withAlpha (0.35f), 5.0f);

        if (code.isNotEmpty())
        {
            g.setFont (fonts::mono (9.5f));
            g.setColour (accent.withAlpha (0.55f));
            g.drawText (code, r.reduced (18.0f, 6.0f).removeFromTop (16.0f), juce::Justification::centredRight);
        }
    }
}

/** Phase-1 style panel, kept for simple pages. */
inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour accent = colours::cyan)
{
    drawTechPanel (g, r, title, accent);
}

/** Recessed display "screen" with brackets and scanlines. */
inline void drawScreen (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent)
{
    auto frame = chamferedRect (r.reduced (0.5f), 8.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0a1522), r.getX(), r.getY(), juce::Colour (0xff120a1a),
                                             r.getRight(), r.getBottom(), false));
    g.fillPath (frame);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (frame);
        g.setColour (accent.withAlpha (0.045f));
        for (float x = r.getX(); x < r.getRight(); x += 20.0f) g.fillRect (x, r.getY(), 1.0f, r.getHeight());
        for (float y = r.getY(); y < r.getBottom(); y += 20.0f) g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        for (float y = r.getY(); y < r.getBottom(); y += 3.0f) g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
    }
    g.setColour (accent.withAlpha (0.45f));
    g.strokePath (frame, juce::PathStrokeType (1.0f));
    drawCornerBrackets (g, r.reduced (3.0f), accent.withAlpha (0.8f), 7.0f, 1.2f);
}

} // namespace mbx::ui
