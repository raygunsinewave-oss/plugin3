// Breakpoint editor for the MSEG and custom LFO waveforms.
//   click empty space   -> add point
//   drag point          -> move (end points move vertically only)
//   double-click point  -> delete
//   drag on a segment   -> bend its curve (vertical drag)
//   right-click         -> presets / reset
#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"

namespace mbx::ui
{
class ShapeEditor final : public juce::Component, private juce::Timer
{
public:
    ShapeEditor (Assets& a, int shapeIndex, juce::Colour c, juce::String label)
        : assets (a), index (shapeIndex), accent (c), title (std::move (label))
    {
        shape = assets.shapes.get (index);
        startTimerHz (GlobalSettings::get().fps (10));
    }

    /** Optional playhead in 0..1 supplier (e.g. GUI-side free-running estimate). */
    std::function<float()> playhead;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawScreen (g, r, accent);
        const auto area = plotArea();

        g.setColour (accent.withAlpha (0.12f));
        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 8.0f), area.getY(), area.getBottom());
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) i / 4.0f), area.getX(), area.getRight());

        juce::Path p, fill;
        constexpr int steps = 240;
        for (int i = 0; i <= steps; ++i)
        {
            const float t = (float) i / steps;
            const auto pt = toScreen (t, shape.evaluate (t));
            if (i == 0) { p.startNewSubPath (pt); fill.startNewSubPath (pt.x, area.getBottom()); }
            p.lineTo (pt);
            fill.lineTo (pt);
        }
        fill.lineTo (area.getRight(), area.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.28f), 0, area.getY(), accent.withAlpha (0.02f), 0, area.getBottom(), false));
        g.fillPath (fill);
        strokeGlow (g, p, accent, 1.8f);

        for (int i = 0; i < shape.numPoints; ++i)
        {
            const auto& pt = shape.points[(size_t) i];
            const auto sp = toScreen (pt.x, pt.y);
            const bool hot = i == dragIndex || i == hoverIndex;
            g.setColour (colours::base);
            g.fillRect (juce::Rectangle<float> (9.0f, 9.0f).withCentre (sp));
            g.setColour (hot ? colours::lime : accent);
            g.drawRect (juce::Rectangle<float> (9.0f, 9.0f).withCentre (sp), 1.5f);
        }

        if (playhead)
        {
            const float ph = playhead();
            if (ph >= 0.0f)
            {
                const float x = area.getX() + area.getWidth() * juce::jlimit (0.0f, 1.0f, ph);
                g.setColour (colours::lime.withAlpha (0.6f));
                g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            }
        }

        g.setFont (fonts::mono (10.0f));
        g.setColour (accent);
        g.drawText (title + "  //  " + juce::String (shape.numPoints) + " PTS", r.reduced (10.0f, 4.0f), juce::Justification::topLeft);
        g.setColour (colours::textDim);
        g.drawText ("CLICK add  DBL-CLICK del  DRAG SEGMENT bend  R-CLICK presets", r.reduced (10.0f, 4.0f), juce::Justification::bottomRight);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hitPoint (e.position);
        if (h != hoverIndex) { hoverIndex = h; repaint(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        dragIndex = hitPoint (e.position);
        curveSegment = -1;
        if (dragIndex < 0)
        {
            const auto v = fromScreen (e.position);
            const float onCurve = shape.evaluate (v.x);
            const auto curvePt = toScreen (v.x, onCurve);
            if (std::abs (curvePt.y - e.position.y) < 8.0f) // grab a segment to bend it
            {
                for (int i = 1; i < shape.numPoints; ++i)
                    if (v.x <= shape.points[(size_t) i].x) { curveSegment = i; break; }
                curveStartY = e.position.y;
                curveStartValue = curveSegment > 0 ? shape.points[(size_t) curveSegment].curve : 0.0f;
                return;
            }
            if (shape.numPoints < dsp::Shape::kMaxPoints)
            {
                shape.points[(size_t) shape.numPoints++] = { v.x, v.y, 0.0f };
                shape.sanitise();
                dragIndex = hitPoint (e.position);
                commit();
            }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (curveSegment > 0)
        {
            const auto& a = shape.points[(size_t) curveSegment - 1];
            const auto& b = shape.points[(size_t) curveSegment];
            const float dir = b.y >= a.y ? 1.0f : -1.0f;
            shape.points[(size_t) curveSegment].curve = juce::jlimit (-1.0f, 1.0f, curveStartValue + dir * (curveStartY - e.position.y) / 80.0f);
            commit();
            return;
        }
        if (dragIndex < 0)
            return;
        auto v = fromScreen (e.position);
        auto& pt = shape.points[(size_t) dragIndex];
        if (dragIndex == 0 || dragIndex == shape.numPoints - 1)
            v.x = pt.x; // end points are pinned in time
        else
            v.x = juce::jlimit (shape.points[(size_t) dragIndex - 1].x + 0.001f, shape.points[(size_t) dragIndex + 1].x - 0.001f, v.x);
        if (e.mods.isShiftDown()) // snap to a 1/16 grid
        {
            v.x = std::round (v.x * 16.0f) / 16.0f;
            v.y = std::round (v.y * 8.0f) / 8.0f;
        }
        pt.x = v.x;
        pt.y = v.y;
        commit();
    }

    void mouseUp (const juce::MouseEvent&) override { dragIndex = -1; curveSegment = -1; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int h = hitPoint (e.position);
        if (h > 0 && h < shape.numPoints - 1)
        {
            for (int i = h; i < shape.numPoints - 1; ++i)
                shape.points[(size_t) i] = shape.points[(size_t) i + 1];
            --shape.numPoints;
            commit();
        }
        else if (h < 0) // double-click a segment: reset its curve
        {
            const auto v = fromScreen (e.position);
            for (int i = 1; i < shape.numPoints; ++i)
                if (v.x <= shape.points[(size_t) i].x) { shape.points[(size_t) i].curve = 0.0f; break; }
            commit();
        }
    }

private:
    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (14.0f, 20.0f); }

    juce::Point<float> toScreen (float x, float y) const
    {
        const auto a = plotArea();
        return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() };
    }
    juce::Point<float> fromScreen (juce::Point<float> p) const
    {
        const auto a = plotArea();
        return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()), juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight()) };
    }
    int hitPoint (juce::Point<float> p) const
    {
        for (int i = 0; i < shape.numPoints; ++i)
            if (toScreen (shape.points[(size_t) i].x, shape.points[(size_t) i].y).getDistanceFrom (p) < 7.0f)
                return i;
        return -1;
    }

    void commit()
    {
        shape.sanitise();
        assets.shapes.set (index, shape);
        repaint();
    }

    void showMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader (title);
        auto preset = [this] (std::initializer_list<dsp::Shape::Point> pts) { shape = dsp::Shape::make (pts); commit(); };
        m.addItem ("Ramp Down",  [preset] { preset ({ { 0, 1, 0 }, { 1, 0, 0.5f } }); });
        m.addItem ("Ramp Up",    [preset] { preset ({ { 0, 0, 0 }, { 1, 1, -0.5f } }); });
        m.addItem ("Triangle",   [preset] { preset ({ { 0, 0, 0 }, { 0.5f, 1, 0 }, { 1, 0, 0 } }); });
        m.addItem ("Pluck",      [preset] { preset ({ { 0, 0, 0 }, { 0.02f, 1, 0 }, { 1, 0, 0.85f } }); });
        m.addItem ("Steps x4",   [preset] { preset ({ { 0, 0.25f, 0 }, { 0.249f, 0.25f, 0 }, { 0.25f, 1, 0 }, { 0.499f, 1, 0 }, { 0.5f, 0.5f, 0 },
                                                      { 0.749f, 0.5f, 0 }, { 0.75f, 0.75f, 0 }, { 1, 0.75f, 0 } }); });
        m.addItem ("Sidechain Duck", [preset] { preset ({ { 0, 0, 0 }, { 0.35f, 1, -0.7f }, { 1, 1, 0 } }); });
        m.addSeparator();
        m.addItem ("Reset", [this] {
            shape = index == ShapeStore::kMsegIndex ? dsp::Shape::defaultMseg() : dsp::Shape::defaultCustom();
            commit();
        });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    void timerCallback() override
    {
        // pick up external changes (preset load / state restore)
        const auto counter = assets.shapes.getChangeCounter();
        if (counter != lastCounter && dragIndex < 0 && curveSegment < 0)
        {
            lastCounter = counter;
            shape = assets.shapes.get (index);
        }
        repaint();
    }

    Assets& assets;
    int index;
    juce::Colour accent;
    juce::String title;
    dsp::Shape shape;
    int dragIndex = -1, hoverIndex = -1, curveSegment = -1;
    float curveStartY = 0.0f, curveStartValue = 0.0f;
    uint32_t lastCounter = 0;
};

} // namespace mbx::ui
