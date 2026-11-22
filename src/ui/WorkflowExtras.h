#pragma once

// Round 190iu (issues #186 and #191): the workflow pieces that hook into editors other rounds own (Playlist, clip grid,
// picker panel, Browser, main window). Each editor calls one function from here at one place; the rules live in core:
//   - core/WheelZones.h   #186   wheel: scroll over the track headers, zoom over the arrangement (LYNX extra)
//   - core/TempTool.h     #191.1 temporary tools while a tool key is held (FL)
//   - core/MidiDrop.h     #191.2 .mid files dropped on the Playlist, the Clip-launch arrangement and the clip grid
//   - core/TakeCleanup.h  #191.6 deleting takes and recorded envelopes from the picker panel and the Browser
//   - core/LaneView.h     #191.3 automation clips shown as lanes under their instrument's track (view option)
//   - core/TrackOnly.h    #191.5 a pattern per track while working linearly; Track-only mode

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/EditorGestures.h"
#include "core/Song.h"
#include "core/TempTool.h"
#include "core/TrackOnly.h"

namespace lynx
{
class AudioEngine;

namespace workflowextras
{
// --- the settings: ui-options.txt keys and the Settings › LYNX extras switches ------------------------------------------
// The main window's loader hands every key here first: true when it is one of these (and it was applied).
bool readUiOption (const juce::String& key, bool on);
// The keys with a number (round 190jq: headerWidthExtra); true when it is one of them.
bool readUiValue (const juce::String& key, const juce::String& value);
// The lines the main window appends to ui-options.txt ("\nkey=1"...).
juce::String uiOptionsText();
// Settings › LYNX extras: the switches these own (nullopt: not one of them).
std::optional<bool> extraOn (std::string_view id);
// Sets one (false: not one of them). `changed` runs after (the main window saves; the views repaint).
bool setExtra (std::string_view id, bool on);
// The main window's hook: runs after any of these switches changes (save the UI options, repaint).
inline std::function<void()> onChanged;
// The key map actions of these switches ("playlist.wheelZones"...): toggles one; false when `id` is not one of them.
bool runAction (const std::string& id);
// The Playlist menu's items of these switches and commands, after "Automation lanes": add (action id, text, enabled, ticked).
void addPlaylistMenuItems (const std::function<void (const char*, const juce::String&, bool, bool)>& add);

// The engine the switches act on (the main window binds it once; the Track-only mode's offer to give each instrument a
// track of its own edits the song).
void bind (AudioEngine* engine);

// --- #191.5 Track-only mode (core/TrackOnly.h) --------------------------------------------------------------------------
// Issue #246 (round 190oq): the Channel rack was opened; in Track-only mode the first time a hint says what it lists.
// (Before 0.1.31 the mode refused to show the rack.) Returns whether the hint was shown.
bool channelRackOpened();
// Whether Track-only mode has just come on (the main window hides the rack then, and only then)
bool trackOnlySwitchedOn();
// View › Layout style: the mode's switch under the three layouts (enabled in the Pattern layout).
void addLayoutItems (juce::PopupMenu& menu, bool patternLayout);

// --- #191.1 temporary tools -----------------------------------------------------------------------------------------------
// The main window's: a tool key ran a tool action. `keyCode` is the key being handled (0: not from a key, e.g. a menu);
// `before` and `chosen` the editor's tool before and after; `restore` puts a tool back (called on release).
class TempToolHold final : private juce::Timer
{
public:
    void toolKey (temptool::Editor editor, int keyCode, EditTool before, EditTool chosen, std::function<EditTool()> toolNow,
                  std::function<void (EditTool)> restore);
    // a mouse press anywhere while a tool key is held (the main window's global mouse listener, or the editors)
    void mouseUsed() { hold.mouseUsed(); }
    bool holding() const noexcept { return hold.active(); }
    // a key press being handled: the code the next toolKey call came from (cleared after the dispatch)
    struct Scope
    {
        Scope (TempToolHold& h, int code) : owner (h) { owner.currentKey = code; }
        ~Scope() { owner.currentKey = 0; }
        TempToolHold& owner;
    };
    int keyBeingHandled() const noexcept { return currentKey; }
    void releaseNow (std::uint32_t now); // (tests and the poll)

private:
    void timerCallback() override; // polls the key while a hold is active only (no timer otherwise)
    temptool::Hold hold;
    std::function<EditTool()> toolNow;
    std::function<void (EditTool)> restore;
    int currentKey = 0;
};
TempToolHold& tempTools();

// --- #191.2 MIDI files dropped -----------------------------------------------------------------------------------------
bool isMidiFile (const juce::String& path);
bool anyMidiFile (const juce::StringArray& files);
// The Playlist (also the Clip-launch arrangement, `liveStyle`): each .mid of `files` at (`track`, `start`), one below
// the other. True when any was placed; refusals show in the hint bar.
bool dropMidiOnPlaylist (AudioEngine& engine, const juce::StringArray& files, int track, std::int64_t start, bool liveStyle);
// A clip-grid slot: each .mid in the next slot down.
bool dropMidiOnSlot (AudioEngine& engine, const juce::StringArray& files, int track, int scene);

// --- #191.6 takes and recorded envelopes ------------------------------------------------------------------------------
// The picker panel's right-click on an audio clip or automation clip row (patterns keep their own menu). `edited` runs
// after a delete (the picker clears its selection).
void showPickerItemMenu (AudioEngine& engine, ClipKind kind, int index, std::function<void()> edited);
// The picker's Tools menu (Playlist menu › Picker panel, under FL's Enable): "Purge unused audio clips" (FL's, also in the
// Playlist's Tools) and "Delete unused automation clips", with their counts.
void addPickerCleanupItems (juce::PopupMenu& menu, AudioEngine& engine, std::function<void()> edited);
// The Browser's Current project: a "Delete" item for an audio clip's audio file row (its channel) or an automation row.
void addBrowserDeleteItem (juce::PopupMenu& menu, AudioEngine& engine, bool automation, int index);
} // namespace workflowextras
} // namespace lynx
