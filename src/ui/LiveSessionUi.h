#pragma once

// Online Sessions, phase 2 (round 169b): the live session's commands (File › Online session), its dialogs and the
// people of the session in the toolbar (engine/LiveSessions.h does the work).

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
class LiveSessionManager;

namespace liveui
{
struct Context
{
    AudioEngine& engine;
    LiveSessionManager& live;
    juce::File projectFile; // the open project's .lynx (none: not saved yet)
    // runs `then` once unsaved changes are saved or let go (MainComponent::confirmDiscard)
    std::function<void (std::function<void()> then)> confirmDiscard;
};

// The File › Online session items of live sessions (start / join / invite code / leave or end).
void addMenuItems (juce::PopupMenu& menu, Context context);
void start (Context context);
void join (Context context);
void showInvite (Context context);
void leave (Context context);

// The manager's questions and messages as windows and hints (the host's "let them in?", the end of the session).
void attach (LiveSessionManager& live);

// The people of the session from the left of `area` (a dot of their colour, their name, "rec" while they record);
// returns the width used (0: no session).
int paintPresence (juce::Graphics& g, juce::Rectangle<int> area, const LiveSessionManager& live);

// The display name the person used last time (Online sessions P1 and P2 share it).
juce::String myName();
} // namespace liveui
} // namespace lynx
