#pragma once

// Names for undo steps (docs/WORKFLOW_FL.md P2: history panel). The undo stack keeps whole Song
// snapshots; this compares two of them and says what changed, in a few words the UI translates.

#include <string>

#include "core/Song.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx
{
struct ChangeLabel
{
    const char* text = "Edit"; // English, may contain {0}; translated by the UI
    std::string arg;           // what {0} stands for (a pattern, channel or insert name)
};

// What turned `before` into `after` (the first difference found, from the most specific).
ChangeLabel describeChange (const Song& before, const Song& after);

// Same, already formatted in English ("Notes: Pattern 1"); used by the tests and logs.
std::string describeChangeText (const Song& before, const Song& after);

// Round 190re (0.1.32 regression sweep): the project's clock is not part of the history. Undo and redo put back a whole
// earlier Song, whose working time was the count of that moment: every undo took the time since that edit off the
// project's working time (Settings › Info, the .flp's work days), and an undo past the first save cleared "Created".
// A history step keeps the clock of the song it leaves (as "Reset working time", which is no undo step either).
inline void keepProjectClock (ProjectInfo& restored, const ProjectInfo& current) noexcept
{
    restored.created = current.created;
    restored.workSeconds = current.workSeconds;
}
} // namespace lynx
