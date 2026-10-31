#pragma once

// Edison's audio scripts (Tools › Run script / Edit last script), run by LYNX's embedded Python (core/Python.h) on the
// audio editor's document (round 167c, docs/analise/REA_rodada167c.md §2).
//
// LYNX's own `enveditor` module is written from FL Studio's published "Edison Script Reference" (the Reference.txt FL
// ships in System\Config\Audio scripts: names, signatures and behaviour; clean-room, our own code): the globals Editor
// (SelectionStartS / SelectionEndS), EditorSample and Utils (ShowMessage, ProgressMsg); the classes Sample (Length,
// NumChans, SampleRate, RegionCount, GetSampleAt, SetSampleAt, NormalizeFromTo, AmpFromTo, SilenceFromTo, SineFromTo,
// NoiseFromTo, CenterFromTo, ReverseFromTo, ReversePolarityFromTo, SwapChannelsFromTo, InsertSilence, DeleteFromTo,
// TrimFromTo, PasteFromTo, LoadFromClipboard, LoadFromFile, LoadFromFile_Ask, NormalizeFormat, ConvertToMono,
// MSToSamples, SamplesToMS, GetRegion, AddRegion, DeleteRegion), Region (SampleStart, SampleEnd, Name, Info, Time,
// KeyNum, Copy) and ScriptDialog (AddInput, AddInputKnob, AddInputKnobInt, AddInputCombo, AddInputCheckbox,
// AddInputText, AddInputSurface, AddGroup, EndGroup, GetInputValue, SetNormalizedValue, SetText, Execute; the same
// names starting in lower case work too), the constants NF_NumChannels, NF_Format, NF_Samplerate, NF_All.
//
// A script either calls form.Execute() itself or defines createDialog() and apply(form) (FL's "preview" style): LYNX
// then shows createDialog()'s form and calls apply() with the values. Positions are frames, both ends included, as in
// FL. With no selection Editor.SelectionStartS / SelectionEndS span the whole sample (what Edison's tools do).
//
// The Python thread never waits for the user: the dialog is shown between two runs. A script that calls Execute() is
// run once to read its form (its changes dropped), then again from the start with the answers. A script runs on a
// copy of the document: the editor takes the result as one undo step, or nothing when the script fails or the form is
// cancelled. No JUCE here (tests/EdisonScript167Tests.inc).

#include <functional>
#include <string>
#include <vector>

#include "core/AudioEdit.h"

namespace lynx::audioedit::script
{
struct Control
{
    enum class Type
    {
        value,    // AddInput: a number (0..1 by default)
        knob,     // AddInputKnob: a float in min..max
        knobInt,  // AddInputKnobInt
        combo,    // AddInputCombo: the option's index
        checkbox, // AddInputCheckbox
        text,     // AddInputText
        surface   // AddInputSurface (a Control Surface preset): not available in LYNX, shown as a note
    };
    Type type = Type::value;
    std::string name, hint; // name: "Group: Label" inside a group
    double value = 0.0, minimum = 0.0, maximum = 1.0;
    std::vector<std::string> options;
    std::string text;
};

struct Form
{
    std::string title, description;
    std::vector<Control> controls;
    bool restoreValues = true; // ScriptDialog.restoreFormValues: the last run's values come back
};

// What the editor provides (all called on the Python thread; they must not wait for the user)
struct Host
{
    std::function<bool (Audio&)> clipboard;                          // LoadFromClipboard (false: empty)
    std::function<void (const Audio&)> setClipboard;                 // DeleteFromTo (..., Copy = True)
    std::function<bool (const std::string& path, Audio&)> loadFile; // LoadFromFile
};

struct Outcome
{
    enum class Status
    {
        done,      // `doc` holds the result
        needsForm, // show `form`, then call run() again with the answers
        failed     // `error` says why (Python's traceback)
    };
    Status status = Status::failed;
    Form form;
    std::string error;
    std::vector<std::string> messages; // Utils.ShowMessage texts, in order
    std::string log;                   // print() output
};

// Runs `source` on `doc` (a copy the caller owns). `answers`: the values of the forms already shown, in order (each
// Form's controls with their chosen values). `scriptPath` names the script (relative imports are refused: sandbox).
Outcome run (const std::string& source, Document& doc, Range selection, const std::vector<Form>& answers, const Host& host);

// The last values a form had (per script path), for restoreFormValues; remember() after the user confirms
void remember (const std::string& scriptKey, const Form& form);
void restore (const std::string& scriptKey, Form& form);

// The scripts LYNX ships (its own, written for LYNX): folder / name / source
struct Builtin
{
    const char* folder;
    const char* name;
    const char* source;
};
const std::vector<Builtin>& builtins();
} // namespace lynx::audioedit::script
