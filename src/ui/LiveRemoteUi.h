#pragma once

// Online Sessions P5-A1 (round 190ai): remote plugins in the window — the host's "Share plugins with guests" list
// (opt-in per plugin, with the licence notice), the shared plugins in a guest's plugin menus ("On Ana's computer"),
// and the guest's parameter editor for a plugin the host plays (names and sliders from the host's parameter list;
// "updating..." while the host renders). engine/LiveSessions.h and core/RemotePlugins.h do the work.

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/RemotePlugins.h"
#include "core/Song.h"

namespace lynx
{
class AudioEngine;
class LiveSessionManager;

namespace liveui
{
// The session these functions work with (liveui::attach sets it; nullptr when the main window goes).
void setRemoteSession (LiveSessionManager* live, AudioEngine* engine);

// The host: which of its plugins guests may use during this session.
void showSharePlugins (LiveSessionManager& live, AudioEngine& engine);

// A guest's plugin menus: the plugins the host shares (instruments or effects), first, under "On {host}'s computer";
// each is marked "played by {host}". Nothing when there is no session, for the host, or for "listen only".
void addSharedPluginItems (juce::PopupMenu& menu, bool instruments, const std::function<void (PluginRef)>& onChosen);

// A guest opens the window of a channel's / slot's plugin: when the host plays that plugin, its parameter editor
// opens and this returns true (false: an ordinary plugin, the caller goes on).
bool showHostedEditor (const remoteplug::Place& place);

// Closes the windows above (the app quits).
void closeRemoteWindows();

// LYNX --remoteshot <prefix>: both panels off-screen with sample data, as <prefix>-share.png, <prefix>-editor.png,
// <prefix>-editor-updating.png and <prefix>-editor-away.png (the coordinator's review).
void saveRemoteShots (const juce::String& prefix);
} // namespace liveui
} // namespace lynx
