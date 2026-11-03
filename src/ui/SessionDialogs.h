#pragma once

// Online Sessions, phase 1 (docs/design/ONLINE_SESSIONS.md §9 P1, round 168a): the File menu's session commands —
// Send project..., Send my part back..., Merge a part... — and opening a received .lynxpack (engine/SessionPack.h).

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

namespace sessionui
{
struct Context
{
    AudioEngine& engine;
    juce::File projectFile;  // the open project's .lynx (none: not saved yet)
    juce::String projectName;
    // opens a .lynx file as the current project (whatever "save changes?" there was is already answered)
    std::function<void (const juce::File&)> openProject;
};

// The host: invites a friend (name, what he may keep) and writes the package for him (the stand-ins of the parts with
// third-party plugins rendered first).
void sendProject (Context context);
// A guest: his own area of the project he received, as a package for the host (his stand-ins rendered first). When the
// host did not let him keep a full copy, his copy is reduced to his part afterwards (the user's decision 1).
void sendPartBack (Context context);
// The host: brings a guest's part into the open project (one undo step).
void mergePart (Context context);
// File › Open... / a double-click on a .lynxpack: a project sent to us is opened (after a summary), a part is merged.
void openPackage (Context context, const juce::File& file);
} // namespace sessionui
} // namespace lynx
