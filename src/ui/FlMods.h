#pragma once

// JUCE side of core/ModifierMap.h: FL's modifier roles from a juce::ModifierKeys. Use these in the editors instead of
// isCtrlDown() / isPopupMenu() wherever an FL Ctrl gesture exists (on macOS JUCE turns Control+click into a popup
// click and never maps Cmd to Ctrl). On Windows and Linux they return exactly isCtrlDown() / isPopupMenu().

#include "core/EditorGestures.h"
#include "core/ModifierMap.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::flmods
{
inline modmap::Raw raw (const juce::ModifierKeys& m) noexcept
{
    modmap::Raw r;
    r.shift = m.isShiftDown();
    r.control = m.isCtrlDown();
#if JUCE_MAC
    r.command = m.isCommandDown();
#endif
    r.alt = m.isAltDown();
    r.left = m.isLeftButtonDown();
    r.right = m.isRightButtonDown();
    r.middle = m.isMiddleButtonDown();
    return r;
}

inline modmap::Fl of (const juce::ModifierKeys& m) noexcept { return modmap::map (raw (m), modmap::hostOs()); }

// FL's Ctrl (Cmd or Control on a Mac)
inline bool ctrl (const juce::ModifierKeys& m) noexcept { return of (m).ctrl; }

// a right click (never Control+left, which is FL's Ctrl role on a Mac)
inline bool popup (const juce::ModifierKeys& m) noexcept { return of (m).popup; }

// The wheel event with Windows touchpad pieces of a notch turned into smooth deltas (core/EditorGestures.h,
// gesture::normaliseWheel; round 170e). Other platforms: unchanged.
inline juce::MouseWheelDetails normalisedWheel (const juce::MouseWheelDetails& wheel) noexcept
{
    const auto w = gesture::normaliseWheel (wheel.deltaX, wheel.deltaY, wheel.isSmooth, JUCE_WINDOWS != 0);
    auto out = wheel;
    out.deltaX = (float) w.x;
    out.deltaY = (float) w.y;
    out.isSmooth = w.smooth;
    return out;
}

// key names of hint / shortcut texts for this platform (⌘ / ⌥ on macOS)
inline juce::String keyNames (const juce::String& text)
{
    if (modmap::hostOs() != modmap::Os::mac)
        return text;
    return juce::String::fromUTF8 (modmap::keyNames (text.toStdString(), modmap::hostOs()).c_str());
}
} // namespace lynx::flmods
