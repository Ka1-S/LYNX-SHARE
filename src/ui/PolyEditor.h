#pragma once

// Round 190lk: "Edit notes (polyphonic)" — LYNX's polyphonic note editor for an audio clip channel (core/PolyEdit.h, on
// PitchNet's engine in third_party/pitchnet-poly). A LYNX extra: opened from the audio clip menu's LYNX extras column.
//  - The clip's sample is analysed once on a low-priority worker (progress and Cancel; Basic Pitch's posteriorgram from
//    LYNX's ONNX Runtime session as the note filter's evidence when it can run, else the DSP-only filter). The notes are
//    saved with the project: opening the editor again, or the project, does not analyse again.
//  - The notes are drawn as blobs along their pitch tracks, chords stacked, in Newtone's note view (its keyboard, rows,
//    grid and blob colours): drag a note up / down for its pitch (whole semitones; Alt = cents), left / right to move it
//    in time; Shift+click adds to the selection; the Level slider sets the selected notes' level; Del mutes / unmutes;
//    R (or a double-click) resets. Edited notes are drawn in Newtone's orange with their old place outlined.
//  - Each edit is rendered on the worker (only the edited notes' blocks), then becomes one undo step of the song
//    (ChannelDef::polyEdit): the channel's clips play the render (cached), Play / Original preview it here.

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
namespace PolyEditorWindows
{
// The editor of an audio clip channel (one window per channel)
void open (AudioEngine& engine, int channel);
void closeAll();
} // namespace PolyEditorWindows

// --polyeditshot190lk: the editor alone (not shown). `analyseNow` analyses on the calling thread (Basic Pitch evidence
// included when the runtime loads) before returning.
std::unique_ptr<juce::Component> makePolyEditor (AudioEngine& engine, int channel, bool analyseNow);
// a state for a picture: "select:k,k", "pitch:k:semitones", "move:k:seconds", "level:k:db", "mute:k", "render" (waits for the
// render and commits it to the song), "zoom:pixelsPerSecond"; returns a line for the shot's report
juce::String setPolyEditorShotState (juce::Component& editor, const juce::String& state);
// the editor's notes as text ("k midi start-end [edit]" per line) and the average time of `paints` full paints in ms
juce::String polyEditorNotes (juce::Component& editor);
double polyEditorPaintMs (juce::Component& editor, int paints);
} // namespace lynx
