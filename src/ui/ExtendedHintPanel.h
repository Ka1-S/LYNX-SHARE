#pragma once

// Round 190mr (DF-49): FL's Extended Hint Panel (FL 2026: View › Toolbars › "Hint bar", capture 190mr A2; manual
// toolbar_panels "Extended Hint Panel"): the hint bar's text in a larger floating panel at the lower left of the desktop,
// transparent to mouse clicks so the controls under it stay usable. Only its round grip takes the mouse: hovering it
// fades the panel, dragging it moves the panel. Drawn from FL's measures at 100 %: a 400 x 82 dark translucent pill with a
// round right end, the hint on the first line, the value in grey italics on the second, a red dot for an automatable
// control and the grip circle (r 34) on the right. LYNX vectors only; repaints only when the hint changes.

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/Theme.h"
#include "ui/HintBar.h"
#include "ui/ValueControl.h"

namespace lynx::ui
{
class ExtendedHintPanel final : public juce::Component
{
public:
    static constexpr int panelWidth = 400, panelHeight = 82;

    ExtendedHintPanel()
    {
        setSize (panelWidth, panelHeight);
        setOpaque (false);
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    // the hint shown (the same one the hint bar shows); `live`: a value moving under the mouse
    void setHint (const Hint& hint, bool live)
    {
        if (hint.text == shown.text && hint.value == shown.value && hint.automatable == shown.automatable && live == shownLive)
            return;
        shown = hint;
        shownLive = live;
        repaint();
    }
    const Hint& getHint() const noexcept { return shown; }

    // where the grip sits: its centre
    juce::Point<float> gripCentre() const noexcept { return { (float) getWidth() - 41.0f, (float) getHeight() * 0.5f }; }
    bool gripContains (juce::Point<float> p) const noexcept { return p.getDistanceFrom (gripCentre()) <= 34.0f; }

    // click-through: only the grip is part of the panel for the mouse
    bool hitTest (int x, int y) override { return gripContains ({ (float) x, (float) y }); }

    std::function<void (juce::Point<int>)> onMoved; // the new top-left (parent coordinates) after a drag

    void mouseEnter (const juce::MouseEvent&) override { setFaded (true); }
    void mouseExit (const juce::MouseEvent&) override { setFaded (false); }
    void mouseDown (const juce::MouseEvent& e) override { grabbedAt = e.getPosition(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* parent = getParentComponent();
        if (parent == nullptr)
            return;
        auto at = getPosition() + e.getPosition() - grabbedAt;
        at.x = juce::jlimit (-getWidth() / 2, parent->getWidth() - getWidth() / 2, at.x);
        at.y = juce::jlimit (0, parent->getHeight() - getHeight(), at.y);
        setTopLeftPosition (at);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (onMoved)
            onMoved (getPosition());
    }

    void paint (juce::Graphics& g) override
    {
        const auto alpha = faded ? 0.22f : 1.0f;
        const auto b = getLocalBounds().toFloat();
        const auto radius = b.getHeight() * 0.5f;
        juce::Path pill;
        pill.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight(), radius, radius, false, true, false, true);
        // FL: near-black over what is behind, darkest around the grip ((19,27,31) at the left, (5,6,7) at the grip)
        const auto c = gripCentre();
        juce::ColourGradient shade (juce::Colours::black.withAlpha (0.86f * alpha), c.x, c.y, juce::Colours::black.withAlpha (0.42f * alpha), 0.0f, c.y, true);
        g.setGradientFill (shade);
        g.fillPath (pill);

        auto text = b.withTrimmedLeft (5.0f).withTrimmedRight (radius * 2.0f + 22.0f);
        const auto first = text.removeFromTop (b.getHeight() * 0.5f).withTrimmedTop (2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.95f * alpha));
        g.setFont (theme::font (15.0f));
        g.drawFittedText (plainHint (shown.text), first.toNearestInt(), juce::Justification::topLeft, 2, 0.85f);
        if (shown.value.isNotEmpty())
        {
            g.setColour ((shownLive ? theme::colour::accent : juce::Colour (0xffa9b0b3)).withAlpha (alpha));
            g.setFont (theme::font (15.0f).italicised());
            g.drawFittedText (shown.value, text.withTrimmedBottom (6.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
        }
        if (shown.automatable) // FL's record dot: the control can be automated
        {
            g.setColour (juce::Colour (0xffff5f60).withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ c.x - 52.0f, c.y + 8.0f }));
        }
        // the grip: a thin ring with four dots
        g.setColour (juce::Colour (0xff20282c).withAlpha (alpha));
        g.drawEllipse (juce::Rectangle<float> (68.0f, 68.0f).withCentre (c), 1.5f);
        g.setColour (juce::Colour (0xffe6eaec).withAlpha (alpha));
        for (const auto dx : { -3.5f, 3.5f })
            for (const auto dy : { -3.5f, 3.5f })
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ c.x + dx, c.y + dy }));
    }

private:
    void setFaded (bool on)
    {
        if (faded != on)
        {
            faded = on;
            repaint();
        }
    }

    Hint shown;
    bool shownLive = false, faded = false;
    juce::Point<int> grabbedAt;
};
} // namespace lynx::ui
