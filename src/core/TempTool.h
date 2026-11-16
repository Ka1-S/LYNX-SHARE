#pragma once

// Issue #191 point 1 (round 190iu), a LYNX extra: temporary tools. In the Playlist and the Piano roll a tool key held
// down switches to that tool while it is down; letting go goes back to the tool that was active before. A quick tap
// still switches for good. FL Studio 2026 has no such switch (docs/analise/REA_rodada190iu.md: its tool keys only select;
// its temporary tools are the right button, Ctrl = select, Shift = paint/draw, the right button + wheel, and 26.1's
// "hold A" resize/stretch for audio clips), so it is off by default (Settings › LYNX extras, the Playlist menu); an app
// setting kept in ui-options.txt ("tempTools").
//
// The state of one hold, driven by the key's press (and its auto-repeats) and its release. Times are milliseconds from
// any steady clock. No JUCE: tested in tests/Workflow190iuTests.inc.

#include <atomic>
#include <cstdint>

#include "core/EditorGestures.h"

namespace lynx::temptool
{
inline std::atomic<bool>& enabledFlag() noexcept
{
    static std::atomic<bool> on { false };
    return on;
}
inline bool enabled() noexcept { return enabledFlag().load (std::memory_order_relaxed); }
inline void setEnabled (bool on) noexcept { enabledFlag().store (on, std::memory_order_relaxed); }

// A tool key held at least this long, or held while the mouse was used, is a temporary switch; a shorter tap is a
// permanent one. (The typing auto-repeat starts after ~500 ms, so a tap never repeats.)
inline constexpr std::uint32_t holdMs = 400;

enum class Editor { none, playlist, pianoRoll };

struct Hold
{
    Editor editor = Editor::none;
    int keyCode = 0;
    EditTool previous = EditTool::pencil, held = EditTool::pencil;
    std::uint32_t downAt = 0;
    bool usedMouse = false;

    bool active() const noexcept { return editor != Editor::none; }

    // A tool key ran `chosen` in `editor`; `current` was the tool before. A repeat of the key being held changes
    // nothing (the auto-repeat runs the action again). Another key starts a new hold that goes back to the tool from
    // before the first one (holding C, then D: letting D go returns to the original tool, not to C). Returns true when a
    // new hold started.
    bool press (Editor in, int code, EditTool current, EditTool chosen, std::uint32_t now) noexcept
    {
        if (in == Editor::none || code == 0)
            return false;
        if (active() && editor == in && keyCode == code)
            return false; // auto-repeat
        const auto before = active() && editor == in ? previous : current;
        editor = in;
        keyCode = code;
        previous = before;
        held = chosen;
        downAt = now;
        usedMouse = false;
        return previous != chosen;
    }

    // the mouse was pressed in the editor while the key is down: a temporary use, however short
    void mouseUsed() noexcept
    {
        if (active())
            usedMouse = true;
    }

    // The key went up. Returns true (and `restore`) when the editor goes back to the previous tool: a hold, or a press
    // during which the mouse was used. The hold ends either way.
    bool release (std::uint32_t now, EditTool toolNow, EditTool& restore) noexcept
    {
        if (! active())
            return false;
        const auto longEnough = now - downAt >= holdMs;
        // the user picked another tool meanwhile (a click on the tool strip): that choice stays
        const auto stillHeld = toolNow == held;
        const auto back = stillHeld && previous != held && (longEnough || usedMouse);
        restore = previous;
        *this = {};
        return back;
    }

    void cancel() noexcept { *this = {}; }
};
} // namespace lynx::temptool
