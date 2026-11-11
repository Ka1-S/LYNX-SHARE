#pragma once

// A number field dragged up and down (round 190s; the value boxes of Live's Clip View panels): Ctrl = fine, wheel,
// double-click / Alt-click = default. LYNX's control contract through ui::ValueSpec / ui::ValueDrag.

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/HintBar.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx::ui
{
// A number field dragged up and down (Live's value boxes): Ctrl = fine, wheel, double-click / Alt-click = default.
class NumBox final : public juce::Component
{
public:
    NumBox (ValueSpec s, std::function<double()> get, std::function<void (double)> set)
        : spec (std::move (s)), getter (std::move (get)), setter (std::move (set))
    {
        setRepaintsOnMouseActivity (true);
        setHint (*this, spec.name, true, false);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg0);
        g.fillRect (getLocalBounds());
        g.setColour (isMouseOverOrDragging() ? theme::colour::accent : theme::colour::borderStrong);
        g.drawRect (getLocalBounds());
        g.setColour (isEnabled() ? theme::colour::textPrimary : theme::colour::textDisabled);
        g.setFont (theme::font (theme::type::caption));
        g.drawText (spec.text (getter()), getLocalBounds().reduced (2, 0), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (isResetClick (e))
        {
            setter (spec.defaultValue);
            repaint();
            return;
        }
        drag.begin (e, getter());
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drag.isActive())
            return;
        setter (drag.update (e, spec));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { drag.end(); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        setter (spec.defaultValue);
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (const auto v = wheelValue (getter(), e, wheel, spec))
        {
            setter (*v);
            repaint();
        }
    }

private:
    ValueSpec spec;
    std::function<double()> getter;
    std::function<void (double)> setter;
    ValueDrag drag;
};
} // namespace lynx::ui
