#pragma once

// Piano roll scripts (FL Studio's Piano roll › Tools › Scripts: Python ".pyscript" files), run by LYNX's embedded Python
// (core/Python.h) on the Piano roll's notes and time markers (round 190as, docs/analise/REA_rodada190as-pyscript.md).
//
// LYNX's own `flpianoroll` module is written from FL Studio's published "Piano roll script reference" (the text file FL
// ships next to its scripts, and the manual's API page) plus the behaviour read from FL's engine (REA: what the script
// sees, defaults, what is kept when the notes are written back); clean-room, our own code. Scripts written for FL run
// unmodified:
//   score   PPQ, tsnum, tsden, noteCount, markerCount, length, snap_root_note, snap_scale_helper, getNote, addNote,
//           deleteNote, getMarker, addMarker, deleteMarker, clear, clearNotes, clearMarkers, getTimelineSelection,
//           setTimelineSelection, getDefaultNoteProperties, getNextFreeGroupIndex
//   Note    number, time, length, group, pan, velocity, release, color, fcut, fres, pitchofs, slide, porta, muted,
//           selected, repeats, clone()
//   Marker  time, name, mode, tsnum, tsden, scale_root, scale_helper
//   ScriptDialog (core/ScriptDialog.h) and Utils (ShowMessage, ProgressMsg, log)
//
// As in FL: the script sees the notes of the Piano roll's channel, and only the selected ones when notes are selected;
// getNote() hands out the live note (changing it changes the score), addNote() takes the object itself (changes after
// the call still count); integer members refuse floats and the True/False members refuse anything but a bool (FL's
// members are typed). A script either runs straight through or defines createDialog() and apply(form): LYNX shows the
// form and calls apply() with the values (again on a fresh copy of the notes whenever a control changes: the preview).
//
// Sandbox: no files, no programs, no network, no modules from disk (core/Python.h); a script that runs too long is
// stopped (TimeoutError). score.loadScoreFromFile (FL's undocumented file reader) is refused. No JUCE here
// (tests/PianoRollScript190Tests.inc).

#include <cstdint>
#include <string>
#include <vector>

#include "core/ScriptDialog.h"
#include "core/Song.h"

namespace lynx::prscript
{
using Control = scriptform::Control;
using Form = scriptform::Form;

// A note as scripts see it (FL's units)
struct ScriptNote
{
    int number = 0;                 // MIDI note number (60 = FL's C5)
    std::int64_t time = 0, length = 0; // ticks
    int group = 0;
    double pan = 0.5;               // 0..1
    double velocity = 0.78125;      // 0..1 (FL's 100 / 128)
    double release = 0.5;           // 0..1
    int color = 0;                  // 0..15
    double fcut = 128.0 / 255.0;    // 0..1 (Mod X)
    double fres = 128.0 / 255.0;    // 0..1 (Mod Y)
    int pitchofs = 0;               // -120..120 (10 cents each)
    bool slide = false, porta = false, muted = false, selected = false;
    int repeats = 0;                // 0..14 (FL's note repeats; LYNX has none: kept for the script only)
    int origin = -1;                // the source note it came from (Score::source index; clone() keeps it), -1 = new
};

struct ScriptMarker
{
    std::int64_t time = 0;
    std::string name;
    int mode = 0;                   // FL's marker action (Marker::action), 0..12
    int tsnum = 4, tsden = 4;
    int scaleRoot = 0;
    std::string scaleHelper = "0,0,0,0,0,0,0,0,0,0,0,0";
};

struct Score
{
    int ppq = lynx::ppq, tsnum = 4, tsden = 4;
    std::vector<ScriptNote> notes;       // what the script sees
    bool hasSelection = false;           // notes were selected when the script started (then `notes` are those)
    std::vector<ScriptMarker> markers;
    std::int64_t selectionStart = 0, selectionEnd = -1; // the timeline selection (end -1: none)
    ScriptNote defaults;                 // getDefaultNoteProperties(): what the draw tool would write
    std::vector<int> otherGroups;        // groups used by the notes the script does not see (getNextFreeGroupIndex)
    int snapRoot = 0;                    // snap_root_note
    std::string snapScale = "0,0,0,0,0,0,0,0,0,0,0,0"; // snap_scale_helper

    // the pattern's notes behind `notes` (pattern indices and the notes themselves), for apply()
    std::vector<int> sourceIndex;
    std::vector<Note> source;
};

struct Outcome
{
    enum class Status
    {
        done,      // the score holds the result
        needsForm, // show `form`, then call run() again with the answers
        failed     // `error` says why (Python's traceback)
    };
    Status status = Status::failed;
    Form form;
    std::string error;
    std::vector<std::string> messages; // Utils.ShowMessage texts, in order
    std::string log;                   // print() and Utils.log output
};

// Runs `source` on `score` (a copy the caller owns). `answers`: the values of the forms already shown, in order.
// `timeLimitMs`: Python time the run may take before it is stopped.
Outcome run (const std::string& source, Score& score, const std::vector<Form>& answers, double timeLimitMs = 20000.0);

// The score of `channel`'s notes in `pattern` (`selection`: indices into pattern.notes; only they are seen when any of
// the channel's notes is selected). timeFrom/timeTo: the Piano roll's time selection (timeTo <= timeFrom: none).
Score scoreOf (const Pattern& pattern, int channel, const std::vector<int>& selection, int beatsPerBar, int beatUnit,
               std::int64_t timeFrom, std::int64_t timeTo, const Note& drawDefaults);

// Writes a script's result back: the notes the script saw are replaced by the score's (values clamped as FL does when
// it stores them; LYNX-only note data survives through ScriptNote::origin), the markers by its markers. Returns the
// indices (into pattern.notes, sorted) of the notes the script left selected.
std::vector<int> apply (Pattern& pattern, int channel, const Score& score);

// The scripts LYNX ships (its own, written for LYNX): name / source
struct Builtin
{
    const char* name;
    const char* source;
};
const std::vector<Builtin>& builtins();
} // namespace lynx::prscript
