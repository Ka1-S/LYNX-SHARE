#pragma once

#include <functional>
#include <vector>

#include <juce_core/juce_core.h>

// Issue #232, stage 2 (round 190nz, docs/design/PROJECT_TABS.md; a LYNX extra): the LYNX SHARE windows open on this
// computer know of each other. Each window (its own process since File › Open in new window, stage 1) writes an entry
// in the settings folder's "Windows" folder (its project, the audio device it plays through) and has an inbox the
// others write one-line messages to: a FIFO on Linux and macOS, a named pipe on Windows. Nothing runs while nothing is
// sent (the inbox thread waits in poll / ReadFile; no timer). Uses: the Window menu (the open windows, switch to one),
// and the audio device that only one window may use (ASIO and other drivers one program at a time): "Make this window
// the one that plays" asks the window that has it to let it go.
namespace lynx::windows
{
int processId();
// Off in test runs (no entry, no inbox) unless LYNX_WINDOWS=1 (the two-window test).
bool enabled();

struct Entry
{
    int pid = 0;
    juce::String project;     // the project's name as the window shows it ("" = untitled)
    juce::String inbox;       // where messages to this window go
    juce::String deviceType;  // the audio device it plays through ("" = none open)
    juce::String deviceName;
    double started = 0.0;     // when the window started (seconds since 1970)
};

// The other windows' messages: "<verb> <from pid> <argument>" (verbs: hello, bye, changed, activate, release-audio,
// audio-released). Called on the message thread.
using Receiver = std::function<void (const juce::String& verb, int from, const juce::String& argument)>;

// This window's entry and inbox (`mine` with pid / inbox / started filled in here), a "hello" to the others. False
// when the inbox could not be made (the window then works alone, as before).
bool start (Entry mine, Receiver receiver);
// "bye" to the others; the entry and the inbox go away. Safe to call twice.
void stop();
bool started();
// This window's entry now (pid 0 when not started).
Entry mine();
// Changes this window's entry (its project, its audio device) and tells the others ("changed") when it changed.
void update (const std::function<void (Entry&)>& change);
// The other live windows, oldest first. An entry whose process is gone is removed.
std::vector<Entry> others();
// One message to window `pid`; false when it cannot be delivered (that window is gone: its entry is removed).
bool send (int pid, const juce::String& verb, const juce::String& argument = {});
// The same message to every other window.
void broadcast (const juce::String& verb, const juce::String& argument = {});
// Before asking window `pid` to come to the front: lets it (Windows' foreground rule; nothing elsewhere).
void allowToFront (int pid);

// The folder of the entries (the settings folder's "Windows").
juce::File folder();
} // namespace lynx::windows
