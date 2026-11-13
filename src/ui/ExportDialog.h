#pragma once

#include "core/QuickRender.h"
#include "engine/AudioEngine.h"

namespace lynx
{
// "Export audio" window: format, sample rate and what to export; then asks for the file and
// renders on a background thread with a progress bar. `suggestedName` = name without extension. `format`: −1 = the
// format(s) of the last export (FL's File › Export › "Last exported format(s)"), 0 WAV, 1 MP3, 2 OGG, 3 FLAC = that
// format alone (FL's "Wave file...", "MP3 file"...; REA round 139).
void showExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& suggestedName, int format = -1);
// File › Export › "Master..." (Ctrl+Alt+M; round 168e): the Export window with the render mastered on the computer
// (core/Mastering.h: a loudness target, −1 dBTP ceiling) where FL sends it to FL Cloud; "<project> (mastered)".
// File › Export › "Project bones..." (REA round 168e, FUN_011edb50): the project's parts as files in a sub-folder of a
// chosen folder named after the project (core/QuickRender.h projectBones: Mixer *.lynxtrack, Effects *.lynxpreset,
// Scores *.mid). `writeProjectBones` writes them into `dir` (returns how many; the failed ones in `failed`).
void exportProjectBones (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& projectName);
int writeProjectBones (const Song& song, const juce::File& dir, juce::StringArray& failed);
void showMasterDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& projectName);
// FL's Mixer › Disk recording › "Render armed / selected tracks to wave file(s)..." (REA round 165f-mixer): the same
// window, set to one file per insert for `tracks` only (the Master in the list = also the full mix).
// `afterRender` (optional) runs on the message thread once the files are written, with the files (FL's "Auto-unarm",
// round 166e; "Auto-create audio clip", round 167a). Inserts with a "Record to" file render to it, and when every one
// has one there is no file dialog, as in FL (round 167a).
void showExportDialogForTracks (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& suggestedName,
                                const std::vector<int>& tracks, std::function<void (const std::vector<juce::File>&)> afterRender = {});

// "Render to audio" (freeze): the insert becomes an audio clip on a new insert with the same
// routing, and the original is muted. Runs on a background thread with progress.
void bounceInsertToAudio (AudioEngine& engine, int track);
// Freeze (round 190ay; core/StandIns.h lynx::freeze — LYNX's own, FL has none): the channel's own sound (after its
// instrument, before its volume, pan and insert) is rendered next to the project and plays in the channel's place while
// the instrument / plugin is not loaded at all: its CPU and RAM are free, the channel's insert effects stay live. The
// channel keeps its plugin and settings as they are, so Unfreeze brings back the same sound. A frozen channel is
// rendered again from its instrument ("Re-freeze"). Close the channel's editor first (a plugin window holds its plugin).
// Runs on a background thread with progress; `done` (optional, message thread) gets whether it froze.
void freezeChannel (AudioEngine& engine, int channel, std::function<void (bool)> done = {});
// Why `channel` cannot be frozen, as a translated sentence; empty = it can (round 190bj: "Freeze selected channels"
// names the channels it leaves out and freezes the others).
juce::String freezeRefusal (AudioEngine& engine, int channel);
// The instrument plays again and the frozen audio is deleted.
void unfreezeChannel (AudioEngine& engine, int channel);
// Live's Freeze Track (round 190ff, the Clip-launch workflow; core/StandIns.h lynx::freeze track freeze): the channel
// with its volume and pan through its insert's effects (the track's devices) is rendered and plays in their place; the
// instrument and the insert's effects are not loaded; the insert's EQ, fader, pan and sends stay live. Unfreeze with
// unfreezeChannel. Refused, with the reason, when something else plays into the insert or an effect reaches out of it.
void freezeTrack (AudioEngine& engine, int channel, std::function<void (bool)> done = {});
// Live's Bounce Track in Place (12.2 on; it also stands for Flatten and Freeze and Flatten there): the track's sound
// becomes audio clips at its clips' places for good (freeze::flatten), its notes and devices gone; a frozen track whose
// audio is current is flattened from that audio without a new render. One undo step.
void bounceTrackInPlace (AudioEngine& engine, int channel);
// The same without a window on the calling (message) thread, for the command-line modes and the tests. Empty = done.
juce::String freezeTrackNow (AudioEngine& engine, int channel, bool flattenAfter = false, const std::function<bool (double)>& progress = {});
juce::String flattenChannelNow (AudioEngine& engine, int channel);
// Live's Bounce to New Track (round 190ff, freeze::planBounceToNewTrack) and Consolidate (freeze::planConsolidate) for the
// selected Playlist clips of one track. In the background with progress; `now`: on the calling (message) thread with
// no window or alert (command-line modes, tests). Returns why not (translated), empty = done / started.
juce::String bounceToNewTrack (AudioEngine& engine, const std::vector<int>& clips, bool now = false);
juce::String consolidateClips (AudioEngine& engine, const std::vector<int>& clips, bool now = false);
// The same without a window, on the calling (message) thread: the command-line modes and the tests. Empty = frozen.
juce::String freezeChannelNow (AudioEngine& engine, int channel, const std::function<bool (double)>& progress = {});
// FL's renders that come back into the project as audio clips (REA round 168e, core/QuickRender.h). Each opens the
// render window in its audio-clip mode (WAV, no file dialog), renders in the background and then edits the song.
// Playlist track menu "Consolidate this track" (From track start / From song start; Shift held = choose the folder): a
// new track above the track holds the render, the track joins its group and its clips are muted.
void consolidatePlaylistTrack (AudioEngine& engine, int track, bool fromSongStart);
// Tools › "Consolidate playlist selection" (Ctrl+Alt+C From selection start, Ctrl+Shift+Alt+C From song start): the same
// for the selected clips (`selectedClips`: indices into the Playlist), the new track above the highest one.
void consolidatePlaylistSelection (AudioEngine& engine, const std::vector<int>& selectedClips, bool fromSongStart);
// Patterns › "Render as audio clip..." (how 0), "Quick render as audio clip" (1: the last choices, no window), "Render and
// replace..." (2: the pattern's Playlist clips become clips of the render): a new audio clip channel named after it.
void renderPatternAsAudioClip (AudioEngine& engine, int pattern, int how);
// The same for several patterns (FL, from the Playlist picker's selection, round 169k): rendered one after the other in
// that order, the window only for the first, empty ones skipped with FL's warning, all results in one undo step.
void renderPatternsAsAudioClips (AudioEngine& engine, const std::vector<int>& patterns, int how);
// File › Export › "All playlist tracks" / "All playlist track groups" (and the Playlist's Tools): a folder, the render
// window once, then one WAV per track ("<project> - <track>"), from the song start, the track's start or the time
// selection (quickrender::Start).
void exportAllPlaylistTracks (AudioEngine& engine, const juce::String& projectName, quickrender::Start start, bool groups);

// Panel only (for --snapshot mode; `format`: 0 WAV, 1 MP3, 2 OGG, 3 FLAC with loop/stem options set). Nothing is saved.
std::unique_ptr<juce::Component> makeExportPanel (AudioEngine& engine, int format = 0);
// The panel of showExportDialogForTracks (for --snapshot).
// The panel with round 190ap's additions in use (--versionsshot): two presets, one chosen, "One file per marker section" on.
std::unique_ptr<juce::Component> makeExportPanelWithAdditions (AudioEngine& engine);
std::unique_ptr<juce::Component> makeExportPanelForTracks (AudioEngine& engine, const std::vector<int>& tracks);
} // namespace lynx
