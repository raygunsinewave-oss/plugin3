// Cyberpunk palette + drawing helpers shared by every UI component.
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
} // namespace colours

namespace fonts
{
    /** Geometric display face for headers (bundled techno font arrives with the Phase 4 UI pass). */
    inline juce::Font display (float height)
    {
        return juce::Font (juce::FontOptions (height, juce::Font::bold)).withExtraKerningFactor (0.12f);
    }
    /** Monospaced face for values. */
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

/** Smoked-glass panel with chamfered corners, glowing outline and a section title. */
inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour accent = colours::cyan)
{
    auto path = chamferedRect (r.reduced (1.0f), 12.0f);
    g.setGradientFill (juce::ColourGradient (colours::glassHi, r.getX(), r.getY(), colours::glass.withAlpha (0.05f), r.getX(), r.getBottom(), false));
    g.fillPath (path);
    g.setColour (accent.withAlpha (0.08f));
    g.strokePath (path, juce::PathStrokeType (4.0f));
    g.setColour (accent.withAlpha (0.55f));
    g.strokePath (path, juce::PathStrokeType (1.0f));

    if (title.isNotEmpty())
    {
        g.setFont (fonts::display (12.0f));
        g.setColour (accent);
        g.drawText (title, r.reduced (16.0f, 6.0f).removeFromTop (16.0f), juce::Justification::topLeft);
    }
}

} // namespace mbx::ui
