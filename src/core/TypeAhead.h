#pragma once

// Type-to-jump in lists and menus (issue #182, round 190ib). Typing while a list or a popup menu has the keyboard
// selects the row whose text starts with what was typed: the first letter jumps, more letters typed quickly (within
// `timeoutMs` of each other) narrow it to a longer prefix, and the same letter again goes on to the next row that starts
// with it (as the file lists of Windows, macOS and GNOME do). Pure and header-only: the UI passes the rows' texts. Used by
// JUCE's PopupMenu and ListBox through cmake/LynxPatchJuce.cmake (fix 37) and by LYNX's own lists (ui/TypeAhead.h).

#include <cstdint>
#include <functional>
#include <string>

namespace lynx::typeahead
{
constexpr std::uint32_t timeoutMs = 1000; // a pause longer than this starts a new prefix

struct State
{
    const void* owner = nullptr; // the list or menu typed into last
    std::u32string typed;        // the prefix so far (lower case)
    std::uint32_t lastMs = 0;
};

constexpr char32_t lower (char32_t c) noexcept
{
    if (c >= U'A' && c <= U'Z')
        return c - U'A' + U'a';
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) // Latin-1 capitals (the UI lower-cases the rest before)
        return c + 0x20;
    return c;
}

// A row's text as it is matched: lower case, without LYNX's menu accelerator marks (U+0001, core/MenuMnemonic.h),
// cut at a tab (a menu item's key text), leading spaces dropped.
inline std::u32string key (const std::u32string& text)
{
    std::u32string out;
    for (const auto c : text)
    {
        if (c == U'\t')
            break;
        if (c == 1 || (out.empty() && (c == U' ' || c == 0xA0)))
            continue;
        out.push_back (lower (c));
    }
    return out;
}

// The row to select after `typed` was typed into `owner` (a list of `count` rows whose selected row is `current`, -1 for
// none) at `nowMs`; -1 when nothing starts with the prefix, or when the key is not one that types (a control character,
// or a space that would start a prefix).
inline int find (State& state, const void* owner, char32_t typed, std::uint32_t nowMs, int current, int count,
                 const std::function<std::u32string (int)>& textOf)
{
    if (count <= 0 || typed < U' ' || typed == 0x7F)
        return -1;
    if (owner != state.owner || nowMs - state.lastMs > timeoutMs)
        state.typed.clear();
    state.owner = owner;
    state.lastMs = nowMs;
    if (state.typed.empty() && typed == U' ')
        return -1;
    state.typed.push_back (lower (typed));

    // the same letter again (or once): the next row that starts with it, after the selected one
    const auto& t = state.typed;
    const bool oneLetter = t.find_first_not_of (t[0]) == std::u32string::npos;
    const auto prefix = oneLetter ? t.substr (0, 1) : t;
    const auto start = oneLetter ? (current + 1) : (current < 0 ? 0 : current);
    for (int i = 0; i < count; ++i)
    {
        const auto row = ((start % count) + count + i) % count;
        if (key (textOf (row)).compare (0, prefix.size(), prefix) == 0)
            return row;
    }
    return -1;
}
} // namespace lynx::typeahead
