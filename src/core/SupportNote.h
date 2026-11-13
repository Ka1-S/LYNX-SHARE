#pragma once

// The start-up support note (round 190fq, LYNX's own; FL has nothing like it): after the 10th start, once, a small
// card in the main window's bottom-right corner says LYNX SHARE is free and open source and that it can be supported
// on Ko-fi (core/Links.h). "Not now" shows it again 30 starts later; "Don't show again" (and Settings › General ›
// "Show the support note" off) never. Runs that are not a person's start (LYNX_TEST_MODE, CI, the command-line test
// and snapshot modes) neither count nor show it. Donating never unlocks anything.
//
// The state is a small text file in the settings folder (ui/SupportLynx.cpp): "launches=", "next=", "show=" lines.
// This header holds the rules only (no JUCE), so the core tests check them.

#include <charconv>
#include <string>
#include <string_view>

namespace lynx::supportnote
{
inline constexpr int firstStart = 10; // the note may show from the 10th start on
inline constexpr int laterStarts = 30; // "Not now" (or a note left without an answer): 30 more starts

struct State
{
    int launches = 0;        // the person's starts counted so far
    int nextAt = firstStart; // the start from which the note may show
    bool show = true;        // Settings › General › "Show the support note"; "Don't show again" turns it off

    bool operator== (const State&) const = default;
};

enum class Choice
{
    support, // "Support": the Support window opens; the note has done its job and doesn't come back
    notNow,  // "Not now": again after 30 more starts
    never    // "Don't show again": off (the Settings box shows it unticked)
};

// A start of the app. `quietRun` = not a person's start (test mode, CI, a test / snapshot mode): nothing is counted
// and nothing shown. Otherwise the start is counted; true = the note is due at this start.
inline bool countStart (State& s, bool quietRun) noexcept
{
    if (quietRun)
        return false;
    if (s.launches < 1000000000)
        ++s.launches;
    return s.show && s.launches >= s.nextAt;
}

// The card was put on screen: if it is left without an answer (the app closed meanwhile, playback started), it comes
// back as after "Not now".
inline void noteShown (State& s) noexcept { s.nextAt = s.launches + laterStarts; }

inline void choose (State& s, Choice c) noexcept
{
    switch (c)
    {
        case Choice::notNow: s.nextAt = s.launches + laterStarts; break;
        case Choice::support:
        case Choice::never: s.show = false; break;
    }
}

// The settings box: on again counts from now (the note doesn't jump up at the next start because the counter passed
// an old mark while it was off).
inline void setShow (State& s, bool on) noexcept
{
    if (on && ! s.show && s.nextAt <= s.launches)
        s.nextAt = s.launches + laterStarts;
    s.show = on;
}

// The card may come up only at a quiet moment: no dialog or overlay in front, nothing playing or recording.
struct Moment
{
    bool dialogOpen = false;   // a modal window, the setup wizard, the Simple workflow's Welcome screen or tour card
    bool playing = false;
    bool recording = false;
    bool windowShowing = true; // the main window is on screen (not minimised)
};
inline bool canShowNow (const Moment& m) noexcept { return m.windowShowing && ! m.dialogOpen && ! m.playing && ! m.recording; }

inline std::string toText (const State& s)
{
    return "launches=" + std::to_string (s.launches) + "\nnext=" + std::to_string (s.nextAt) + "\nshow=" + (s.show ? "1" : "0") + "\n";
}

// Unknown lines are skipped; a missing or broken file gives the defaults (a first start).
inline State fromText (std::string_view text)
{
    State s;
    const auto number = [] (std::string_view v, int fallback) {
        int n = 0;
        const auto r = std::from_chars (v.data(), v.data() + v.size(), n);
        return r.ec == std::errc() && n >= 0 ? n : fallback;
    };
    while (! text.empty())
    {
        const auto end = text.find ('\n');
        auto line = text.substr (0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr (end + 1);
        while (! line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.remove_suffix (1);
        const auto eq = line.find ('=');
        if (eq == std::string_view::npos)
            continue;
        const auto key = line.substr (0, eq), value = line.substr (eq + 1);
        if (key == "launches")
            s.launches = number (value, 0);
        else if (key == "next")
            s.nextAt = number (value, firstStart);
        else if (key == "show")
            s.show = value != "0";
    }
    return s;
}

// Not a person's start: LYNX_TEST_MODE, a CI machine (CI / GITHUB_ACTIONS set) or LYNX_NO_SUPPORT_NOTE. The caller
// reads the environment (and calls this only on the normal start path: the command-line modes return earlier).
inline bool quietEnvironment (bool testMode, std::string_view ci, std::string_view githubActions, std::string_view noNote) noexcept
{
    const auto on = [] (std::string_view v) { return ! v.empty() && v != "0" && v != "false"; };
    return testMode || on (ci) || on (githubActions) || on (noNote);
}
} // namespace lynx::supportnote
