#pragma once

// File › "Save a copy for an older LYNX..." (round 190hq, slice B20; community complaint #35 "a project from a newer
// version won't open in an older one", FL's KB 515). LYNX extra: FL Studio and Live 12 save only for themselves.
//
// A .lynx keeps format version 1, so every release reads every newer file and skips what it does not know. That is not
// enough on its own: a newer effect the older release does not know is dropped, which moves every later slot of that
// insert (automation, side-chains and links point at slots by position), and the older release cannot say what it lost.
// This writes the project as the chosen release reads it, step by step from the newest format down: what the release
// cannot hold is flattened into something it can (a newer effect becomes a switched-off Utility in its slot, so the slots
// keep their places; its automation is detached) or dropped, and each change is listed, so nothing is lost silently.
// The copy is stamped as saved by that release (no "newer version" notice there); the open project is not changed.
//
// Adding a release: a new step at the front of `steps` in ProjectDowngrade.cpp (newest first) that turns the next
// release's file into the previous one's, with a Loss for everything it drops; tests in the app's --selftest.

#include <juce_core/juce_core.h>

#include "core/Song.h"

namespace lynx::olderlynx
{
// The releases a copy can be saved for, newest first ("0.1.28", "0.1.27").
juce::StringArray releases();

// One thing the copy loses or changes. `text` is an English UI string (TR_NOOP) with an optional "{0}" filled by
// `detail` (translate with tr (text, { detail })); `sound` = the copy may sound different there, else only the view,
// a setting or an editing aid is lost.
struct Loss
{
    const char* text = "";
    juce::String detail;
    bool sound = false;
};

struct Copy
{
    bool ok = false;
    juce::String error; // (an unknown release)
    juce::String json;  // the copy, as the release reads it
    std::vector<Loss> lost;
};

// The copy of `song` for release `release` (one of releases()).
Copy forRelease (const Song& song, const juce::String& release);
// The same from a project's text (what this build would save), e.g. a file on disk.
Copy forRelease (const juce::String& projectJson, const juce::String& release);

// "<name> (for LYNX 0.1.27)": the copy's suggested file name, without extension.
juce::String copyName (const juce::String& projectName, const juce::String& release);
} // namespace lynx::olderlynx
