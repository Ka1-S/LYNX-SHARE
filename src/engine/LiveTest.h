#pragma once

// Online Sessions, phase 2 (round 169b): the checks of live sessions inside the real app.
//
// selfTest(): no network — the .lynx JSON round trip of every entity a live session sends (what is lost on the way),
// that it is stable (sent twice = sent once) and that replicas fed through it converge.
// run(): LYNX --livetest report.txt [--relay URL] — a host and three guests (one of them late) in one process, over
// the real relay: media transfer, edits from everyone, an edit that is not allowed, undo per user, a guest leaving and
// the host ending the session.

#include <juce_core/juce_core.h>

#include "core/Song.h"

namespace lynx::livetest
{
// "OK (...)" or "ERROR ..."; `rich` = a project with as much in it as possible (the selftest's demo project)
juce::String selfTest (const Song* rich);
// the --livetest command (message thread, no message loop needed); returns the exit code
int run (const juce::StringArray& args);
// LYNX --livepeer host|guest|rejoin <folder> ... (round 190ah): one side of a session between two real computers,
// scripted, with no window (the two-computer check; see the comment at the function)
int runPeer (const juce::StringArray& args);
} // namespace lynx::livetest
