#pragma once

// LYNX's editors for a Newtone / Newtime mixer slot (round 142, issue #5): load a sample, analyse it, edit it, hear it.
//  - Newtone (FL's workflow, round 165g): the sample's notes on a pitch grid with a keyboard ruler. Drag a note up or
//    down to change its pitch (snapped to semitones or the chosen scale; Alt = free), Ctrl+drag to move it in time;
//    Shift+click / a drag on empty space selects several (Ctrl+A all); a click on the ruler moves the selected notes
//    to that key; double-click snaps; Cut mode (C) slices a note where clicked, Ctrl+click glues it to the one before.
//    The Center / Variation / Transition knobs act on the selection (or every note); the note panel (FL's Advanced
//    Edit) sets the selected notes' pitch, formant, volume, variation, ramps, vibrato and tremolo. Score: send the
//    notes to a channel's piano roll or save them as MIDI.
//  - Newtime: the sample's waveform with warp markers. Double-click adds a marker, dragging it moves where that point
//    of the sample plays (the sample stretches around it, pitch kept); right-click a marker to remove it.
// The edits are the slot's state (LYNX's own format); "Play with the song" is the plugin's slave mode, Preview plays
// the edited sample alone. Imported FL Newtone states open with their notes; editing one saves it in LYNX's format.

#include "engine/AudioEngine.h"

namespace lynx
{
namespace NewtoneWindows
{
bool handles (EffectKind kind); // newtone, newtime
void open (AudioEngine& engine, int track, int slot);
void closeAll();
} // namespace NewtoneWindows

// Round 190gw: the audio clip menu's "Pitch-correct sample" / "Time-warp sample" and the Sampler waveform menu's "Edit in
// pitch corrector" / "Edit in time warper" (FL: Newtone / Newtime on the Master with the sample, live check 190gu B3):
// LYNX's Newtone / Newtime on the Master (core mixerops::openSampleInMasterEditor) with `path` loaded, its window opened.
void openSampleInMasterEditor (AudioEngine& engine, EffectKind kind, const std::string& path);

// The editor alone (snapshots, tests)
std::unique_ptr<juce::Component> makeNewtoneEditor (AudioEngine& engine, int track, int slot);
// Round 190kj (--newtoneshot): put an editor made above in a state for a picture: "select:k,k...", "advanced", "cut",
// "timesel:a,b" (output samples), "zoom:z", "lynxlook" (the round-165g layout, for the paint before / after); and the
// average time of `paints` full paints in ms
void setNewtoneShotState (juce::Component& editor, const juce::String& state);
double newtonePaintMs (juce::Component& editor, int paints);
// Round 190li (--newtoneshot190li): the editor's Edit menu, for a picture
juce::PopupMenu newtoneEditMenuForSnapshot (juce::Component& editor);
} // namespace lynx
