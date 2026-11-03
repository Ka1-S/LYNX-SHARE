#pragma once

// FL Studio's mouse / key modifiers on each platform, without JUCE (unit-tested in tests/Sel168tTests.inc).
//
// FL's gestures are written for Windows: Ctrl+drag selects, right-drag deletes, and so on. FL Studio for Mac uses
// Cmd where Windows uses Ctrl and Opt where it uses Alt (FL manual, "Shortcuts": "Ctrl -> command (Cmd), Alt ->
// option (Opt)"), and in FL Studio 2026 for Mac a Control+click is NOT a right click (Image-Line forum, 2026-07:
// "FL Studio 2026 does not respect [macOS's Ctrl+click = right click], and instead left clicks").
//
// JUCE, on the other hand, reports a Control+left click on macOS as a popup-menu click (ModifierKeys::isPopupMenu()
// is "right button or Ctrl" there), so a LYNX editor that asked isPopupMenu() opened a menu where the user wanted
// FL's Ctrl gesture, and nothing ever looked at Cmd (round 168d, the YouTube review on a Mac: "the double click to
// highlight this bar is ... hard to do"). Docs: docs/analise/REA_rodada168t.md.
//
// The editors (Playlist, Piano Roll, Channel Rack, Mixer, event editors) ask this map instead:
//   ctrl  = FL's Ctrl role: Ctrl on Windows / Linux; Cmd or Control on macOS (Control kept so a Mac user who reads
//           "Ctrl" in a hint and presses Control still gets the gesture instead of a menu);
//   popup = the right button only, on every platform (Control+click on a Mac is a left click with FL's Ctrl role,
//           as in FL 2026 for Mac; a two-finger click / right button stays the right click).
// On Windows and Linux the result is exactly JUCE's isCtrlDown() / isPopupMenu().

#include <string>

namespace lynx::modmap
{
enum class Os
{
    windows,
    linuxOs, // not "linux": GNU mode predefines linux as a macro
    mac
};

constexpr Os hostOs() noexcept
{
#if defined(__APPLE__)
    return Os::mac;
#elif defined(_WIN32)
    return Os::windows;
#else
    return Os::linuxOs;
#endif
}

// What the hardware reports (JUCE: isCtrlDown() = the Control key on every platform, isCommandDown() = Cmd on a Mac).
struct Raw
{
    bool shift = false, control = false, command = false, alt = false;
    bool left = false, right = false, middle = false;
};

// What the gesture means in FL's (Windows) terms.
struct Fl
{
    bool shift = false, ctrl = false, alt = false;
    bool left = false, right = false, middle = false;
    bool popup = false; // a right click (menus, the right-button gestures)
};

constexpr Fl map (const Raw& r, Os os) noexcept
{
    Fl f;
    f.shift = r.shift;
    f.alt = r.alt;
    f.ctrl = os == Os::mac ? (r.command || r.control) : r.control;
    f.left = r.left;
    f.right = r.right;
    f.middle = r.middle;
    f.popup = r.right;
    return f;
}

// Hint / shortcut texts are written with Windows key names ("Ctrl+drag", "[Ctrl]", "Ctrl+B", "Alt"). On macOS they
// show the Mac keys FL for Mac uses: Ctrl -> ⌘ (Cmd), Alt -> ⌥ (Opt). Only whole words are replaced ("Ctrl" and the
// German "Strg", "Alt"; not "Alternate" or "Control"), after translation. Other platforms: unchanged.
inline std::string keyNames (const std::string& text, Os os)
{
    if (os != Os::mac)
        return text;

    auto isWordChar = [] (char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; };
    static const char* const cmd = "\xe2\x8c\x98"; // U+2318 ⌘
    static const char* const opt = "\xe2\x8c\xa5"; // U+2325 ⌥

    std::string out;
    out.reserve (text.size() + 8);
    for (std::size_t i = 0; i < text.size();)
    {
        const auto before = i == 0 ? ' ' : text[i - 1];
        auto matches = [&] (const char* word, std::size_t n) {
            return text.compare (i, n, word) == 0 && ! isWordChar (before) && (i + n >= text.size() || ! isWordChar (text[i + n]));
        };
        if (matches ("Ctrl", 4) || matches ("Strg", 4)) // "Strg": the German tables' Ctrl
        {
            out += cmd;
            i += 4;
        }
        else if (matches ("Alt", 3))
        {
            out += opt;
            i += 3;
        }
        else
            out += text[i++];
    }
    return out;
}
} // namespace lynx::modmap
