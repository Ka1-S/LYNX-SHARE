#pragma once

// Round 190rp (0.1.32 Q4, FIX-LIST item 3): the crash-recovery round trip, tested as hard as the save. LYNX is ended
// on purpose in a child process (a crash, or a kill as after a power cut) at the moments users lose work, and what the
// next start offers is opened and compared with what was there:
//  1. crash mid-session: LYNX's crash handler writes "recovered <time>.lynx"; it is the file the recovery box offers,
//     and it opens as the song that was playing (the whole project, compared field by field);
//  2. killed while an autosave writes: the half-written temporary file ("..._temp<hex>.lynx") must never be offered
//     (it was, before round 190rp: the newest file of the session); the newest complete backup is;
//  3. killed while File › Save writes over the project: the project on disk is the old or the new version, whole;
//  4. killed mid-take: the take's WAV is repaired from its journal and holds all but the last second at most (the
//     writer's flush period; Ardour 9.8's bar is 5 s), reported as the seconds lost.
// The real-app version (a window, Remote Control, the box answered, PipeWire recording) is
// /mnt/e/lynx-work/stab_190rp/s/recovery.py; this one runs in --selftest and --recoverytest.

#include <juce_core/juce_core.h>

namespace lynx
{
// --selftest "recovery190rp" / --recoverytest: "OK (...)" or "ERROR ...", the details line by line into `log`.
juce::String recoveryRoundTripSelfTest (juce::String& log);
// The children (LYNX --recoverychild autosave|save|take <folder>): they write until they are killed. Returns false
// when the command line is not one of them.
bool runRecoveryChild (const juce::StringArray& args);
} // namespace lynx
