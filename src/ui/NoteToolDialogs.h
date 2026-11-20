#pragma once

// Piano Roll note tool dialogs (docs/WORKFLOW_FL.md P2, R23; round 83): Strum, Arpeggiate, Flip, Randomise,
// Scale levels, Limit, Quantize, Chop, Flam, LFO, Claw machine, Articulate, Riff machine and Chord progression.
// Each is a small window whose controls preview live on the pattern (not in the history); Accept makes the change
// one undo step, Cancel / closing puts the notes back.
// The note maths lives in core/NoteTools and core/PianoRollTools.

#include <cstdint>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
enum class NoteTool
{
    strum, arpeggiate, flip, randomise, scaleLevels, limit,
    quantize, chop, flam, lfo, claw, articulate, riffMachine, chordProgression,
    euclidean, humanize, noteRepeat // LYNX's versions of FL's piano roll scripts (REA round 143)
};

const char* noteToolTitle (NoteTool tool); // English, for tr()
// Tools that write new notes and can open without any (riff machine, chord progression).
bool noteToolGenerates (NoteTool tool) noexcept;

// The panel (for --snapshot and tests); `indices` are notes of the current pattern, `grid` the editor's grid.
std::unique_ptr<juce::Component> makeNoteToolPanel (AudioEngine& engine, NoteTool tool, std::vector<int> indices, std::int64_t grid);

// round 190jv (--prshot190jv): chooses the item starting with `item` of the panel's choice `control` (its label), as a
// click would; false when there is none (e.g. no FL install for the Arpeggiate tool's "Pattern")
bool noteToolChooseForSnapshot (juce::Component& panel, const juce::String& control, const juce::String& item);
// The right-click menu of the panel's first slider that has one (the LFO tool's Period steps), for --curveshot.
juce::PopupMenu noteToolPresetMenuForSnapshot (juce::Component& panel);
// The panel's choice box with the most items (the Limit tool's chord / scale list), for --curveshot; nullptr if none.
juce::ComboBox* noteToolLongestChoiceForSnapshot (juce::Component& panel);

// round 190kd (--prchord190kd): the Chord progression dialog's FL chord tool state and strip (ui/ChordStripView.h),
// nullptr for the other tools; and its Generate button
struct ChordToolState;
class ChordStripView;
ChordToolState* noteToolChordStateForSnapshot (juce::Component& panel);
ChordStripView* noteToolChordViewForSnapshot (juce::Component& panel);
void noteToolChordGenerateForSnapshot (juce::Component& panel);

// Opens the tool in its own window. `onFinished` runs after Accept or Revert (the note indices changed).
void showNoteTool (AudioEngine& engine, NoteTool tool, std::vector<int> indices, std::int64_t grid, std::function<void()> onFinished = {});
} // namespace lynx
