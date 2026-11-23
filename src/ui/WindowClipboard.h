#pragma once

#include <optional>

#include "engine/CrossClipboard.h"

namespace lynx
{
class AudioEngine;
}

// Issue #232, stage 2 (round 190nz, docs/design/PROJECT_TABS.md; a LYNX extra): the in-window clipboards (notes, clips,
// steps, channels, mixer track states, automation points) stay as they are, and every copy also goes to the system
// clipboard as LYNX's own text (engine/CrossClipboard.h), so a paste in another LYNX SHARE window, or after a restart,
// takes it. A paste first asks incoming(): the newest LYNX copy on the system clipboard when another window made it,
// else nothing and the window's own clipboard pastes as before.
namespace lynx::windowclip
{
// After an in-window copy: the copy on the system clipboard (stamp, process and project filled in here). Big copies
// go through a file in the settings folder's "Clipboard" folder.
void publish (crossclipboard::Payload payload);
// What another LYNX window copied last, when it is of `kind` and newer than this window's own last copy (nothing when
// the system clipboard holds something else: text copied in another program stays text). When the system clipboard is
// empty (on X11 a copy goes with the window that made it), the last copy any window made is read from the folder.
std::optional<crossclipboard::Payload> incoming (crossclipboard::Kind kind);
// Whether incoming (kind) would give something (cheap: the copy itself is not read), for menus' Paste items.
bool hasIncoming (crossclipboard::Kind kind);

// The project name a copy says it came from ("" = untitled); MainComponent keeps it current.
void setProjectName (const juce::String& name);
// The hint after a paste from another window: "Pasted from <project>".
juce::String pastedFrom (const crossclipboard::Payload& payload);

// A copy for another project carries what the plugins hold now, not what the project last saved: the live state of
// channel `channel`'s plugin into `into` / of mixer track `track`'s plugin effects into `into` (a plugin that crashed,
// or one a live session plays elsewhere, keeps the state it had).
void captureChannel (AudioEngine& engine, int channel, ChannelDef& into);
void captureMixerTrack (AudioEngine& engine, int track, MixerTrack& into);

// Off in test runs (the system clipboard belongs to the person at the computer) unless LYNX_SYSTEM_CLIPBOARD=1.
bool enabled();
// The folder big copies go through.
juce::File sideFolder();
} // namespace lynx::windowclip
