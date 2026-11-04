#pragma once

// Online Sessions, phase 2 (round 169j): the live session's chat window (with "Follow the host's transport" for
// guests) and the host's "People and permissions" dialog. engine/LiveSessions.h does the work.

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class LiveSessionManager;

namespace liveui
{
// Opens (or brings to the front) the session's chat window.
void showChat (LiveSessionManager& live);
// Closes it (the session ended, the app quits).
void closeChat();
// Whether it is open and in front (new lines are then read at once).
bool chatVisible();
// The host: each guest's role and what they keep at the end, changed during the session.
void showPermissions (LiveSessionManager& live);
} // namespace liveui
} // namespace lynx
