#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MidiImport.h"

namespace lynx
{
class AudioEngine;

// "Pitch..." of a sample channel or an audio clip's channel (round 163): semitones + cents and "Keep length" (the
// stretch engine, else resampling: the speed follows). OK applies it as one undo step (clipops::setChannelPitch).
void showSamplePitchDialog (AudioEngine& engine, int channel);
std::unique_ptr<juce::AlertWindow> makeSamplePitchDialog (float pitch, bool keepLength, bool audioClip);
// FL's "Import MIDI data" window (File › Import › MIDI file..., REA rounds 143 / 146b): the project options, which
// tracks / MIDI channels / controller to import, zero-velocity notes; `pianoRoll` (the piano roll's import into a
// channel) shows "Blend with existing data" instead of the project options. `onAccept` gets the chosen options.
void showMidiImportDialog (const juce::String& fileName, const midiimport::ParsedMidi& midi, const midiimport::Options& current,
                           bool pianoRoll, std::function<void (const midiimport::Options&)> onAccept);
// The same window for the Event editor's File › Import MIDI file (REA_rodada178a §11): which tracks / channels / controller
// (any CC), Realign events, Import time signatures, Import zero velocity notes.
void showEventLaneMidiImportDialog (const juce::String& fileName, const midiimport::ParsedMidi& midi, const midiimport::Options& current,
                                    std::function<void (const midiimport::Options&)> onAccept);
std::unique_ptr<juce::AlertWindow> makeMidiImportDialog();

// "Rename and color" dialog for patterns and channels (docs/WORKFLOW_FL.md, P1 item 4): a name field
// and LYNX's palette (plus "automatic"); F2 inside the dialog picks a random palette color.
// `onDone(name, colour)` runs when the user confirms (colour 0 = automatic / theme default).
void showNameAndColourDialog (const juce::String& title, const juce::String& name, std::uint32_t colour,
                              std::function<void (const juce::String&, std::uint32_t)> onDone);

// FL's "Gradient editor" (mixer "Gradient color...", REA_rodada177a §5): First and Last swatches with the blend over
// `count` tracks, copy / swap buttons and LYNX's colour picker for the swatch being edited. Accept gives the two colours;
// Cancel or Escape drops them.
void showGradientColourDialog (juce::Colour first, juce::Colour last, int count, std::function<void (juce::Colour, juce::Colour)> onAccept);
std::unique_ptr<juce::AlertWindow> makeGradientColourDialog (juce::Colour first, juce::Colour last, int count);

// The record button's question (FL's "Recording - What would you like to record?", REA_rodada88): audio into
// the Playlist, notes, everything, or audio into the audio editor. `onChoice(choice, dontAskAgain)`: 1 = audio,
// 2 = notes, 3 = everything, 4 = audio editor.
enum RecordChoice
{
    recordChoiceAudio = 1,
    recordChoiceNotes = 2,
    recordChoiceEverything = 3,
    recordChoiceAudioEditor = 4
};
void showRecordChoiceDialog (std::function<void (int, bool)> onChoice);
std::unique_ptr<juce::AlertWindow> makeRecordChoiceDialog();

// FL's message windows (round 186c; REA FUN_009635b0 / FUN_00964d10): a title, a question icon, a text whose
// |bar-delimited| words show in the accent colour ("This will |delete| 2 selected mixer track(s). Are you sure?"),
// the buttons, and FL's "Remember my choice" box (its 0x20000 flag) when `rememberBox`. `onChoice(button, remember)`:
// button 1, 2, ... in order, 0 = closed / Escape.
struct FlMessage
{
    juce::String title, text;
    juce::StringArray buttons;
    bool rememberBox = false;
};
void showFlMessage (const FlMessage& message, std::function<void (int, bool)> onChoice);
std::unique_ptr<juce::AlertWindow> makeFlMessageDialog (const FlMessage& message);

// FL's remembered message choices (Settings › General › Hints and warnings › "Manage messages", REA round 186c: the
// table at 0x01500260, one entry per message with its id, label and category). A message whose choice was
// remembered is not shown: its action runs as if Yes was clicked (FL remembers Yes only; No asks again).
namespace messages
{
struct Entry
{
    const char* id;       // FL's own value name ("DeleteMixerTrack")
    const char* label;    // the Manage messages item ("Delete mixer track")
    const char* category; // its heading ("File/Data")
};
const std::vector<Entry>& all();
bool shown (const juce::String& id);              // false once "Remember my choice" was ticked with Yes
void setShown (const juce::String& id, bool show);
juce::String hiddenList();                        // for the settings file: the hidden ids, comma-separated
void setHiddenList (const juce::String& list);
inline std::function<void()> onChanged;           // MainComponent saves its settings
juce::PopupMenu manageMenu();                     // FL's "Manage messages" menu: ticked = shown
} // namespace messages

// The dialog itself, for --snapshot mode.
std::unique_ptr<juce::AlertWindow> makeNameAndColourDialog (const juce::String& title, const juce::String& name, std::uint32_t colour);
} // namespace lynx
