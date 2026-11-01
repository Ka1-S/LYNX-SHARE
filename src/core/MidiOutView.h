#pragma once

// How the MIDI Out editor draws one of its 3 x 3 knobs (round 167p, issue #17). Pure logic, no JUCE (unit-tested in
// tests/tests.cpp). FL draws the cog at the knob's top-right and the name under the knob; LYNX keeps the cog there
// for every knob (it always opens "Configure..."), the name under it (a click switches the control on / off), and
// makes the three states look different: unassigned (dimmed, no value), assigned but off, sending (value arc in the
// accent colour).

#include <algorithm>
#include <string>

#include "core/MidiOut.h"

namespace lynx::midiout::view
{
struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    int right() const noexcept { return x + w; }
    int bottom() const noexcept { return y + h; }
    bool contains (int px, int py) const noexcept { return px >= x && py >= y && px < right() && py < bottom(); }
    bool intersects (const Rect& o) const noexcept { return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom(); }
    bool operator== (const Rect&) const = default;
};

struct Cell
{
    Rect knob, gear, label;
};

constexpr int labelHeight = 18, gearSize = 16, rowGap = 8, maxKnob = 64;

// The knob (a square), its cog (top-right, outside the knob's circle) and its name (under it) inside one grid cell;
// a gap of at least `rowGap` px stays under the name for the next row.
inline Cell layout (Rect cell)
{
    Cell c;
    const auto d = std::max (16, std::min ({ maxKnob, cell.h - labelHeight - rowGap - 8, cell.w - 2 * gearSize }));
    const auto top = cell.y + std::max (4, (cell.h - d - labelHeight - rowGap) / 2);
    c.knob = { cell.x + (cell.w - d) / 2, top, d, d };
    c.gear = { c.knob.right() - gearSize / 4, c.knob.y - gearSize / 4, gearSize, gearSize };
    c.label = { cell.x + 4, c.knob.bottom() + 3, cell.w - 8, labelHeight };
    return c;
}

enum class Look
{
    unassigned, // dimmed knob, no arc, no value, no name
    off,        // assigned but deactivated: grey arc, value and name greyed
    sending     // value arc in the accent colour, bold name
};

inline Look look (const Control& c) { return ! c.assigned() ? Look::unassigned : c.active ? Look::sending : Look::off; }

// The value shown inside the knob ("" when unassigned: an unset knob has no value to show)
inline std::string readout (const Control& c, int value) { return c.assigned() ? std::to_string (value) : std::string(); }
} // namespace lynx::midiout::view
