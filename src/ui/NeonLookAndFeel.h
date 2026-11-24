#pragma once

#include "Theme.h"

namespace mbx::ui
{
class NeonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel() { refreshColours(); }

    /** Re-applies the current theme palette to JUCE's colour ids. */
    void refreshColours()
    {
        using namespace colours;
        setColour (juce::ResizableWindow::backgroundColourId, base);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ComboBox::backgroundColourId, track);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, cyan.withAlpha (0.4f));
        setColour (juce::ComboBox::arrowColourId, cyan);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf00b0e16));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, cyan.withAlpha (0.2f));
        setColour (juce::PopupMenu::highlightedTextColourId, cyan);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0080b12));
        setColour (juce::TooltipWindow::textColourId, cyan);
        setColour (juce::TooltipWindow::outlineColourId, cyan.withAlpha (0.6f));
        setColour (juce::TextButton::textColourOffId, textDim);
        setColour (juce::TextButton::textColourOnId, base);
        setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xf0080b12));
        setColour (juce::BubbleComponent::outlineColourId, cyan.withAlpha (0.6f));
    }

    /** Accent used for the value ring; components can override per-slider via properties. */
    static juce::Colour accentFor (const juce::Component& c)
    {
        if (auto v = c.getProperties()["accent"]; v.isInt() || v.isInt64())
            return juce::Colour ((juce::uint32) (juce::int64) v);
        return colours::cyan;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        const auto accent = accentFor (s);
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float ringR = radius - 6.0f;
        const float range = endAngle - startAngle;
        const float angle = startAngle + pos * range;
        const bool hover  = s.isMouseOverOrDragging();
        const bool bipolar = (bool) s.getProperties()["bipolar"];
        const float from   = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        // outer tick ring (lit up to the value)
        constexpr int ticks = 25;
        for (int i = 0; i < ticks; ++i)
        {
            const float a = startAngle + range * (float) i / (float) (ticks - 1);
            const bool lit = a >= juce::jmin (from, angle) - 0.001f && a <= juce::jmax (from, angle) + 0.001f;
            const float inner = radius - (i % 6 == 0 ? 4.5f : 2.5f);
            g.setColour (lit ? accent.withAlpha (0.9f) : colours::textDim.withAlpha (0.28f));
            g.drawLine (juce::Line<float> (centre.getPointOnCircumference (inner, a), centre.getPointOnCircumference (radius - 0.5f, a)), 1.0f);
        }

        // glass body with segmented inner ring
        const float bodyR = ringR - 5.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1e2836), centre.x, centre.y - bodyR,
                                                 juce::Colour (0xff090d14), centre.x, centre.y + bodyR, false));
        g.fillEllipse (juce::Rectangle<float> (bodyR * 2, bodyR * 2).withCentre (centre));
        for (int seg = 0; seg < 12; ++seg)
        {
            juce::Path arc;
            const float a0 = (float) seg / 12.0f * juce::MathConstants<float>::twoPi;
            arc.addCentredArc (centre.x, centre.y, bodyR - 2.5f, bodyR - 2.5f, 0.0f, a0 + 0.08f, a0 + 0.42f, true);
            g.setColour (accent.withAlpha (hover ? 0.35f : 0.14f));
            g.strokePath (arc, juce::PathStrokeType (1.0f));
        }

        // track + value arc
        juce::Path trackArc;
        trackArc.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::track);
        g.strokePath (trackArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        strokeGlow (g, valueArc, accent, 3.0f, hover ? 1.6f : 1.0f);

        // modulation depth (static matrix amount) as a second, inner arc
        const float depth = (float) s.getProperties().getWithDefault ("modDepth", 0.0f);
        if (std::abs (depth) > 0.001f)
        {
            const float end = juce::jlimit (startAngle, endAngle, angle + depth * range);
            juce::Path modArc;
            const float mr = ringR - 4.0f;
            modArc.addCentredArc (centre.x, centre.y, mr, mr, 0.0f, juce::jmin (angle, end), juce::jmax (angle, end), true);
            strokeGlow (g, modArc, colours::magenta, 2.0f, 0.8f);
        }
        // live modulated position (newest voice)
        const float live = (float) s.getProperties().getWithDefault ("modLive", 0.0f);
        if (std::abs (live) > 0.0005f)
        {
            const float la = juce::jlimit (startAngle, endAngle, angle + live * range);
            g.setColour (colours::lime);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (centre.getPointOnCircumference (ringR, la)));
        }

        // pointer + hub
        juce::Path pointer;
        const float pr = bodyR - 2.0f;
        pointer.startNewSubPath (centre.getPointOnCircumference (pr * 0.3f, angle));
        pointer.lineTo (centre.getPointOnCircumference (pr, angle));
        g.setColour (colours::text);
        g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent.withAlpha (0.8f));
        g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (centre));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
            return;
        }
        const auto accent = accentFor (s);
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1.0f, (float) h * 0.28f);
        g.setColour (colours::track);
        g.fillRect (r);
        const bool bipolar = (bool) s.getProperties()["bipolar"];
        const float cx = bipolar ? r.getCentreX() : r.getX();
        const float px = juce::jlimit (r.getX(), r.getRight(), pos);
        auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, px), r.getY(), juce::jmax (cx, px), r.getBottom());
        g.setColour (accent.withAlpha (0.25f));
        g.fillRect (fill.expanded (0.0f, 2.0f));
        g.setColour (accent);
        g.fillRect (fill);
        for (float t = r.getX(); t < r.getRight(); t += 4.0f) // light-bar segmentation
        {
            g.setColour (colours::base.withAlpha (0.55f));
            g.fillRect (t, r.getY(), 1.0f, r.getHeight());
        }
        g.setColour (colours::text);
        g.fillRect (px - 1.0f, r.getY() - 3.0f, 2.0f, r.getHeight() + 6.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto accent = accentFor (b);
        const auto r = b.getLocalBounds().toFloat().reduced (1.5f);
        auto path = chamferedRect (r, juce::jmin (8.0f, r.getHeight() * 0.35f));
        const bool on = b.getToggleState();

        if (on)
        {
            g.setColour (accent.withAlpha (0.15f));
            g.strokePath (path, juce::PathStrokeType (6.0f));
            g.setColour (accent.withAlpha (down ? 0.75f : 0.9f));
            g.fillPath (path);
        }
        else
        {
            g.setColour (colours::glass.withMultipliedAlpha (over ? 2.0f : 1.0f));
            g.fillPath (path);
            g.setColour (accent.withAlpha (over ? 0.7f : 0.35f));
            g.strokePath (path, juce::PathStrokeType (1.0f));
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return fonts::display (juce::jmin (13.0f, (float) h * 0.5f)); }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.getToggleState() ? colours::base : (over ? colours::text : colours::textDim));
        if (! b.isEnabled())
            g.setColour (colours::textDim.withAlpha (0.4f));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
        auto path = chamferedRect (r, 6.0f);
        g.setColour (colours::track);
        g.fillPath (path);
        g.setColour (colours::cyan.withAlpha (box.isMouseOver (true) ? 0.8f : 0.4f));
        g.strokePath (path, juce::PathStrokeType (1.0f));

        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (colours::cyan);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return fonts::mono (13.0f); }
    juce::Font getPopupMenuFont() override { return fonts::mono (14.0f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (colours::cyan.withAlpha (0.5f));
        g.drawRect (0, 0, w, h);
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const int w = juce::jmin (320, juce::GlyphArrangement::getStringWidthInt (fonts::mono (13.0f), tip) + 20);
        return juce::Rectangle<int> (pos.x + 14, pos.y + 18, w, 24).constrainedWithin (parent);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& tip, int w, int h) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRect (r);
        g.setColour (colours::cyan.withAlpha (0.7f));
        g.drawRect (r, 1.0f);
        g.setFont (fonts::mono (13.0f));
        g.setColour (colours::cyan);
        g.drawText (tip, r.reduced (8, 0), juce::Justification::centredLeft);
    }
};

} // namespace mbx::ui
