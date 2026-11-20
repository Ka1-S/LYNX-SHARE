#pragma once

// LYNX --demo (round 190np; docs/DEMO_MODE.md): the automation mode the tutorial-video pipeline drives. A developer
// feature, off by default and in no menu. With it the Remote Control API (docs/REMOTE_CONTROL.md) is on, its
// "Remote control active" strip is hidden (it would be in every frame), the update check and the support note stay
// quiet, and the API gains view operations: ui.find / ui.click / ui.list / ui.window (controls by component ID or
// name, with their screen rectangles), ui.layout, ui.select_track, ui.show, ui.welcome, and session.start /
// session.status (an Online Session on a local test relay only). MainComponent::handleDemoOp runs them.

#include <juce_graphics/juce_graphics.h>

namespace lynx::demo
{
bool active() noexcept;
void setActive (bool on) noexcept;
// The operations --demo adds to the Remote Control API (also listed by api.describe while it is on)
juce::StringArray operations();
// A relay URL a demo session may use: loopback only (http://127.0.0.1:port, http://localhost:port, http://[::1]:port)
bool isLocalRelay (const juce::String& url);

// A control made of cells (the Simple Drum Machine's steps): ui.find / ui.click with "cell": { "row", "column" } reach one
// cell of it (its rectangle in the control's coordinates; empty: no such cell)
struct CellSource
{
    virtual ~CellSource() = default;
    virtual juce::Rectangle<int> demoCellBounds (int row, int column) const = 0;
};
} // namespace lynx::demo
