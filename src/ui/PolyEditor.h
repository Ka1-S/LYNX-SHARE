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
// Round 190lo (Melodyne's / Newtone's conventions in this look):
//  - tools: Notes (pitch / time drag; the end of a note is its stretch handle: length 25 - 400 %, the attack keeps its
//    speed), Modulation and Drift (Melodyne's Pitch Modulation / Pitch Drift tools: drag a note up / down, 0 - 200 %;
//    double-click = 0 % / 100 %), Draw pitch (a drawn pitch line per note; Alt erases, double-click clears);
//  - a rubber band over empty space selects (Shift / Ctrl adds), and the selected notes move together;
//  - Note assignment (Melodyne's Note Assignment Mode): the candidates the note filter rejected are drawn faint; a
//    double-click makes one a note, or a note a candidate (one undo step; the analysis object is made again);
//  - a dragged note is heard while it moves (PitchNet's renderNotes on the worker, the newest state wins; it stops when
//    the mouse is let go);
//  - buffer mode for Edison (its own undo, Apply); the "Prepare edited clips when a project opens" extra.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
namespace PolyEditorWindows
{
// The editor of an audio clip channel (one window per channel)
void open (AudioEngine& engine, int channel);
// Round 190lo: the editor on a buffer (Edison's Tools › Analysis › "Edit notes (polyphonic)..."): its own undo, and Apply
// hands the edited render (same length) to `apply`, then the window closes
void openBuffer (AudioEngine& engine, std::shared_ptr<const SampleData> buffer, const juce::String& title,
                 std::function<void (std::shared_ptr<const SampleData>)> apply);
// Round 190lo, a LYNX extra (off by default; Settings › LYNX extras and the editor's... ui-options.txt "polyPrepareOnOpen"):
// when a project opens, the clips whose notes were already edited get their analysis object and masks built on a
// background thread, so the editor opens ready to play a dragged note. Returns the number of clips queued.
extern bool prepareOnOpen;
int prepareEditedClips (AudioEngine& engine);
bool preparing();
void closeAll();
} // namespace PolyEditorWindows

// --polyeditshot190lk: the editor alone (not shown). `analyseNow` analyses on the calling thread (Basic Pitch evidence
// included when the runtime loads) before returning.
std::unique_ptr<juce::Component> makePolyEditor (AudioEngine& engine, int channel, bool analyseNow);
// round 190lo: the editor in buffer mode (Edison), for the shot route and tests
std::unique_ptr<juce::Component> makePolyBufferEditor (AudioEngine& engine, std::shared_ptr<const SampleData> buffer, bool analyseNow,
                                                       std::function<void (std::shared_ptr<const SampleData>)> apply);
// a state for a picture: "select:k,k", "pitch:k:semitones", "move:k:seconds", "level:k:db", "mute:k", (round 190lo) "stretch:k:ratio",
// "modulation:k:scale", "drift:k:scale", "curve:k:semitones", "assign:on|off", "toggle:k", "tool:main|modulation|drift|draw",
// "band:s0:m0:s1:m1", "band:show|hide", "preview:k" (times a dragged note's render), "view:seconds:topPitch[:rowHeight]",
// "undo" / "redo" / "apply" (buffer mode), "render" (waits for the
// render and commits it to the song), "zoom:pixelsPerSecond"; returns a line for the shot's report
juce::String setPolyEditorShotState (juce::Component& editor, const juce::String& state);
// the editor's notes as text ("k midi start-end [edit]" per line) and the average time of `paints` full paints in ms
juce::String polyEditorNotes (juce::Component& editor);
double polyEditorPaintMs (juce::Component& editor, int paints);
} // namespace lynx
