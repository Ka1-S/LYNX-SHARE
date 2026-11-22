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
// Round 190pl: the name of the virtual MIDI input --demo adds ("LYNX demo keyboard"): it is listed in Options › MIDI
// like a device that is plugged in, and midi.input plays it (engine/AudioEngine.h injectMidiInput with its name), so its
// notes go through the MIDI settings (on / off, MIDI channel, what it plays), recording and Map Mode as a keyboard's do.
juce::String virtualKeyboardName();
// Round 190pl: the file the Export window's Export... button writes instead of asking with a file dialog (ui.export:
// a take shows the window, never the system's file dialog with the computer's folders). Taken (and cleared) once.
void setExportTarget (const juce::File& file);
juce::File takeExportTarget();
// A relay URL a demo session may use: loopback only (http://127.0.0.1:port, http://localhost:port, http://[::1]:port)
bool isLocalRelay (const juce::String& url);

// A control made of cells (the Simple Drum Machine's steps): ui.find / ui.click with "cell": { "row", "column" } reach one
// cell of it (its rectangle in the control's coordinates; empty: no such cell)
struct CellSource
{
    virtual ~CellSource() = default;
    virtual juce::Rectangle<int> demoCellBounds (int row, int column) const = 0;
    // Round 190pl: "cell": { "row", "name" } — the rectangle of the cell in row `row` that shows `name` (a tile, a list's
    // row); empty: none
    virtual juce::Rectangle<int> demoCellNamed (int row, const juce::String& name) const
    {
        juce::ignoreUnused (row, name);
        return {};
    }
};
// Round 190pl: a component that paints several knobs itself (a channel editor's designed page) says where the knob of a
// parameter is (an AutomationTarget's kind, index, slot and param), for "param:..." targets; empty: not here
struct ParamSource
{
    virtual ~ParamSource() = default;
    virtual juce::Rectangle<int> demoParamBounds (int kind, int index, int slot, int param) const = 0;
};
} // namespace lynx::demo
