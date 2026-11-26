#pragma once

// Round 190qk (owner's policy, 2026-11-23): which LYNX extras (core/LynxExtras.h) are on by default, and how an existing
// profile follows a new default.
//
// The policy: an extra may be on by default when it is visual or pure QoL, does not change what FL users' keys, clicks
// or gestures do, and does not change saved files, .flp / .als / .dawproject round-trips or sound. Every extra keeps its
// switch in Settings › LYNX extras, and the page's "FL-exact" button turns every extra off (the ones FL Studio itself
// does stay on: Esc closes the focused window, the start page).
//
// Existing profiles: an extra the user never set follows the default of the LYNX that runs; one the user set keeps the
// user's choice. ui-options.txt keeps the ids the user set ("extrasUserSet=id,id,..."): when it is written, every extra
// whose value differs from its default joins the list (whatever switched it: the page, a menu, a key, the palette), and
// only the page's "Recommended" button empties it. A file written before 0.1.32 has no list: there an extra counts as
// set when its saved value differs from the default it had until 0.1.31.
//
// The values here must match each preference's own initial value (the variable the feature reads): tests check both.
// No JUCE: tests/Extras190qkTests.inc.

#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/LynxExtras.h"

namespace lynx::extras
{
// --- the defaults ----------------------------------------------------------------------------------------------------
// On by default since 0.1.32 (round 190qk; EXTRAS.md of the round has the reasons): visual only, no gesture, file or
// sound change, and no paint or idle cost over the gates (Shade faded audio was measured +1.3 % on Thief's Playlist, the
// value beside the mouse +0.6-1 % idle CPU in --uiperf: both stay off). Round 190ro (EXTRAS-2.md): the Mixer's peak values and
// correlation bars off again, the gain handle that stays in view and the Clip-launch strips of new projects on.
inline constexpr std::string_view onSince0132[] = {
    "pianoRoll.chordHighlight", // the Piano roll's rows light the Chord lane's chord (only a song with chords)
    "playlist.trackInserts",    // the effects column on linked track headers (only while tracks are linked)
    "simple.projectName",       // the project's name at the top of the Simple layout (BandLab)
    "controlBar.liveScale",     // Live 12's scale switch, root and mode in the Clip-launch control bar (Live)
    // rollup 0.1.32 step 2 (the owner's policy 11-23): visual only, on from the start
    "playlist.dimUnselectedTracks", // PR #267: the other tracks step back while tracks are selected
    "playlist.darkenMutedTracks",   // PR #268: a switched-off track's whole row steps back
    // (round 190qh's "mixer.peakValues" and "mixer.correlation" were on here in steps 2-4; round 190ro put them back off:
    // they shorten every FL Mixer fader and meter by 19 px and cost +1.6 % a playhead column, EXTRAS-2.md OFF-1)
    // rollup 0.1.32 step 3, the owner's decisions: #287 (FL ignores a .mid dropped on its Playlist, so no FL gesture
    // changes; on since 0.1.31.2's new profiles, here for every profile that kept 0.1.31's off), #238's track colors
    // (kept in the theme) and round 190qi's LYNX-only plugin / Browser lists, which were always on
    "playlist.midiDrop",            // #287: a .mid dropped on the Playlist becomes a pattern clip
    "theme.tracksFromAccent",       // #238 (PR #261-#266, round 190qj): tracks take a muted shade of the accent
    "plugins.recent",               // round 190qi: Recent in the plugin lists
    "plugins.oneRow",               // round 190qi: one row per plugin across VST / VST3 / CLAP
    "browser.midiPreview",          // round 190qi: a .mid clicked in the Browser plays
    "browser.tabCompletes",         // round 190qi: Tab completes the Browser's search
    // round 190ro (EXTRAS-2.md ON-1): the gain handle moves only while the clip's middle is out of view, where FL shows no
    // handle; a press next to its half circle still moves the clip (--extras2_190roshot checks it)
    "playlist.gainHandleFollowsView", // issue #274: the clip gain handle stays in view
    // round 190ro (EXTRAS-2.md ON-2): new, empty Clip-launch projects get a strip per track, as a new Live set; the
    // Pattern and Simple layouts and every existing project are unchanged (tests/Extras190roTests.inc)
    "project.clipLaunchStrips",       // round 190oo CS O-2
};
// On by default before 0.1.32 too: FL Studio's own behaviour (Esc, the start page), the Simple layout's loops (BandLab),
// and the stretched clips' loop (issue #25, the owner's choice)
inline constexpr std::string_view onBefore0132[] = { "general.escCloses", "general.startPage", "simple.loopDetect", "general.stretchedClipsLoop" };
// What FL Studio does: on for the two that are FL's own; every other extra off
inline constexpr std::string_view onInFlExact[] = { "general.escCloses", "general.startPage" };

constexpr bool listed (std::string_view id, const std::string_view* first, const std::string_view* last) noexcept
{
    for (auto* i = first; i != last; ++i)
        if (*i == id)
            return true;
    return false;
}
// the default of a new profile (and of an extra the user never set)
constexpr bool defaultOn (std::string_view id) noexcept
{
    return listed (id, std::begin (onSince0132), std::end (onSince0132)) || listed (id, std::begin (onBefore0132), std::end (onBefore0132));
}
// the default through 0.1.31 (files without the user-set list)
constexpr bool defaultOnBefore0132 (std::string_view id) noexcept { return listed (id, std::begin (onBefore0132), std::end (onBefore0132)); }
// the page's "FL-exact" button
constexpr bool flExactOn (std::string_view id) noexcept { return listed (id, std::begin (onInFlExact), std::end (onInFlExact)); }

// --- ui-options.txt ----------------------------------------------------------------------------------------------------
inline constexpr std::string_view userSetKey = "extrasUserSet";

// The ui-options.txt key of an extra ("ui-options.txt:shadeFadedAudio" -> "shadeFadedAudio"); empty when the
// preference lives in another file (the game controller: midi-settings.txt, never on by default).
constexpr std::string_view uiKeyOf (const Item& item) noexcept
{
    constexpr std::string_view file = "ui-options.txt:";
    return item.settingsKey.substr (0, file.size()) == file ? item.settingsKey.substr (file.size()) : std::string_view {};
}

// The ids the user set, this session (message thread only): read with the file, written with it
inline std::set<std::string>& userSet()
{
    static std::set<std::string> ids;
    return ids;
}

inline std::set<std::string> parseUserSet (std::string_view text)
{
    std::set<std::string> ids;
    while (! text.empty())
    {
        const auto comma = text.find (',');
        auto id = text.substr (0, comma);
        while (! id.empty() && (id.front() == ' ' || id.front() == '\t'))
            id.remove_prefix (1);
        while (! id.empty() && (id.back() == ' ' || id.back() == '\t' || id.back() == '\r'))
            id.remove_suffix (1);
        if (! id.empty())
            ids.insert (std::string (id));
        if (comma == std::string_view::npos)
            break;
        text.remove_prefix (comma + 1);
    }
    return ids;
}

inline std::string userSetText (const std::set<std::string>& ids)
{
    std::string text;
    for (const auto& id : ids)
        text += (text.empty() ? "" : ",") + id;
    return text;
}

namespace detail
{
inline std::string_view trimmed (std::string_view s) noexcept
{
    while (! s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix (1);
    while (! s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix (1);
    return s;
}
// "key=value": the key and the value, trimmed (no '=': an empty key)
inline std::pair<std::string_view, std::string_view> keyValue (std::string_view line) noexcept
{
    const auto eq = line.find ('=');
    if (eq == std::string_view::npos)
        return {};
    return { trimmed (line.substr (0, eq)), trimmed (line.substr (eq + 1)) };
}
} // namespace detail

// Loading ui-options.txt: reads the user-set list into userSet() (or, from a file written before 0.1.32, works it out
// from the values that differ from the old defaults), then puts each extra the user never set at its default: its line
// gets the default's value. `lines` are the file's lines, changed in place before the loader reads them. A new profile
// (no file, no lines) keeps every preference's own initial value, which is the default.
inline void followDefaults (std::vector<std::string>& lines)
{
    auto& set = userSet();
    set.clear();
    bool known = false;
    for (const auto& line : lines)
        if (const auto [key, value] = detail::keyValue (line); key == userSetKey)
        {
            set = parseUserSet (value);
            known = true;
        }
    if (! known)
        for (const auto& item : items)
            if (const auto key = uiKeyOf (item); ! key.empty())
                for (const auto& line : lines)
                    if (const auto [k, value] = detail::keyValue (line); k == key && (value == "1") != defaultOnBefore0132 (item.id))
                        set.insert (std::string (item.id));
    for (const auto& item : items)
    {
        const auto key = uiKeyOf (item);
        if (key.empty() || set.count (std::string (item.id)) > 0)
            continue;
        for (auto& line : lines)
            if (const auto [k, value] = detail::keyValue (line); k == key && (value == "1") != defaultOn (item.id))
                line = std::string (key) + "=" + (defaultOn (item.id) ? "1" : "0");
    }
}

// Writing ui-options.txt: `text` is what is about to be written ("\nkey=value" lines); every extra whose value there
// differs from its default joins userSet(). Returns the line to append ("\nextrasUserSet=...").
inline std::string noteUserChoices (std::string_view text)
{
    std::map<std::string_view, std::string_view> values;
    for (std::string_view rest = text; ! rest.empty();)
    {
        const auto end = rest.find ('\n');
        if (const auto [k, value] = detail::keyValue (rest.substr (0, end)); ! k.empty())
            values[k] = value;
        if (end == std::string_view::npos)
            break;
        rest.remove_prefix (end + 1);
    }
    auto& set = userSet();
    for (const auto& item : items)
        if (const auto key = uiKeyOf (item); ! key.empty())
            if (const auto v = values.find (key); v != values.end() && (v->second == "1") != defaultOn (item.id))
                set.insert (std::string (item.id));
    return "\n" + std::string (userSetKey) + "=" + userSetText (set);
}
} // namespace lynx::extras
