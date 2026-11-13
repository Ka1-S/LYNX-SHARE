#pragma once

// Audio editor window (round 86; the feature set follows FL Studio's Edison, docs/analise/REA_rodada86.md — the
// window and the drawing are LYNX's own). Waveform or spectrogram of one piece of audio with zoom/scroll, a
// selection, markers/regions and a loop; edits from core/AudioEdit (cut/copy/paste, delete, trim, silence, fades
// with curves, normalize, amp, reverse, polarity, DC removal, time stretch / pitch shift, resample, channels);
// an undo history; recording from the sound card input, the Master or a mixer insert; saving as WAV (markers and
// loop kept) and sending the result to the Playlist as an audio clip, to a new Sampler channel or back to the
// channel it came from.

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/AudioEdit.h"
#include "engine/AudioEngine.h"

namespace lynx
{
// An empty editor (Tools menu): record into it or open a file.
void openAudioEditor (AudioEngine& engine);
// A file (the Browser).
void openAudioEditor (AudioEngine& engine, const juce::File& file);
// The sample of a channel (a Sampler channel or the channel of a Playlist audio clip); the edit can go back to it.
// `clip`: the Playlist audio clip it was opened from (its warp markers are then edited for that clip alone).
void openAudioEditorForChannel (AudioEngine& engine, int channel, int clip = -1);
// The same editor as a panel, without a window (round 175a, for 183a's Clip View): the sample of `channel`, opened from
// Playlist audio clip `clip` (-1: none). `showWarp`: the warp markers lane starts open.
std::unique_ptr<juce::Component> makeAudioEditorPanel (AudioEngine& engine, int channel, int clip, bool showWarp = true);
// Round 190cy: the panel shows the sample of the grid's clip in (track, scene): ÷2 / ×2 scale that clip with the markers.
void setAudioEditorGridClip (juce::Component& panel, int track, int scene);
// --warpshot (round 190cy): ÷2 (0.5) / ×2 (2) as the panel's buttons do it; a warp marker held in the middle of a drag
// (plain: to `beatOrSeconds` as a beat; `slides` (Shift): to `beatOrSeconds` as a point of the audio).
void scaleAudioEditorTempoForSnapshot (juce::Component& panel, double factor);
void holdAudioEditorWarpDragForSnapshot (juce::Component& panel, int marker, double beatOrSeconds, bool slides);
// --warpshot (round 190db): the beat-time view of a warped clip: -1 as the workflow (beats in the Clip-launch
// workflow), 0 file time, 1 beats (the Warp markers menu's "Show the audio in beats")
void setAudioEditorBeatTimeForSnapshot (juce::Component& panel, int choice);
// The record button's "Audio, into the audio editor" choice: an empty editor recording the audio input while the
// song plays (it starts with the playback and stops with it, like FL's Edison record target).
void openAudioEditorRecording (AudioEngine& engine);
// A mixer insert (0 = Master): opens with the recording source set to it, like an editor sitting in a mixer slot.
void openAudioEditorForInsert (AudioEngine& engine, int insert);

// An Edison effect slot (round 190cd, core/EdisonFx.h): the editor as that plugin's window. It records the slot's
// input, plays into the slot and edits the slot's own audio, which is kept with the project. One window per slot;
// `record`: Record is switched on too. The window follows its slot when the slot moves and closes when it is deleted.
void openEdisonSlot (AudioEngine& engine, int track, int slot, bool record = false);
// The same editor without a window (thumbnails, snapshots); nullptr when the slot is not an Edison.
std::unique_ptr<juce::Component> makeEdisonSlotEditor (AudioEngine& engine, int track, int slot);
// Loads an Edison into the first empty slot of mixer track `track` (FL's "Open audio editor" of a mixer track).
// `openAndRecord`: FL's "Open audio editor and record" settings (edisonfx::setOpenAndRecord: On play, 5 min, Append off,
// Slave on); otherwise a fresh Edison. Returns the effect's index, or -1 when the track is full.
int addEdisonSlot (AudioEngine& engine, int track, bool openAndRecord = false);
// Tools › Macros › "Remove all Edison instances": FL's question first (title "Remove Edison instances", Yes / No; live FL
// check 190cq, shot edison190cd-07b), then every Edison slot is emptied on Yes. LYNX keeps this as one undo step
// (coordinator decision, rollup 190cv: an undo is strictly better), so its text leaves out FL's second line "(No undo)".
// The question alone and the Yes action alone, for --edisonslottest.
juce::MessageBoxOptions removeAllEdisonInstancesQuestion();
void removeAllEdisonInstances (AudioEngine& engine);
void askRemoveAllEdisonInstances (AudioEngine& engine);

// --snapshot: the editor showing `doc` (selection = the second quarter), in the waveform or spectrogram view.
std::unique_ptr<juce::Component> makeAudioEditorForSnapshot (AudioEngine& engine, audioedit::Document doc, bool spectrum);
// --snapshot: the items of the editor's main menu, one per line ("Section > Item"), for the menu check.
juce::StringArray audioEditorMenuForSnapshot (juce::Component& editor);
// round 190ee: a marker's menu as lines ("Trigger note > C5  [x]"), and FL's snap mode set (snapping on) with where
// `probeFrame` snaps to (the tooltip first)
juce::StringArray audioEditorMarkerMenuForSnapshot (juce::Component& editor, int marker);
juce::String setAudioEditorSnapForSnapshot (juce::Component& editor, int mode, std::int64_t probeFrame);
// --edisonshot (round 165l): a tool window of `editor` (tool = audiotools::Tool as int; envelopeMode 0 wave,
// 1 volume, 2 panning, 3 stereo separation: what the envelope tools work on), its English title, and the editor's mode.
std::unique_ptr<juce::Component> makeAudioToolPanelForSnapshot (juce::Component& editor, int tool, int envelopeMode = 0);
const char* audioToolNameForSnapshot (int tool); // nullptr past the last tool
void setAudioEditorEnvelopeModeForSnapshot (juce::Component& editor, int mode);
} // namespace lynx
