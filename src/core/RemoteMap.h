#pragma once

// Round 190ef: the reference DAW's MIDI Map Mode and Key Map Mode for the Clip-launch (Ableton-style) layout. What the
// built-in lesson "Creating Manual MIDI Mappings" of Live 12 and the program's string table say (every statement and its
// source: docs/analise/REA_rodada190ef.md):
//   - MIDI Map Mode (Ctrl+M) and Key Map Mode (Ctrl+K) highlight every element that can be remote-controlled; a click
//     picks one, the next MIDI key / button / knob moved (or computer key pressed) is assigned to it;
//   - mappings do not act while the map mode is on;
//   - a mapping belongs to the clip SLOT, not to the clip in it;
//   - one MIDI control may launch several slots only when they are on different tracks (the program warns otherwise);
//   - a control already assigned to a clip slot cannot be assigned to a scene;
//   - the Mapping Browser lists every mapping (MIDI channel, note or controller, path, name, Min, Max); Min and Max
//     refine a mapping's range at any time, Min above Max (or "Invert Range") inverts it; Delete removes a mapping.
//
// A parameter's MIDI mapping is FL's MidiLink (Song::midiLinks: the same storage as "Link to controller", with the new
// min / max); everything else is a RemoteMapping (Song::remoteMappings). No JUCE here: the UI and the engine call these.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Preferences.h"
#include "core/Song.h"

namespace lynx::remotemap
{
enum class Mode : std::uint8_t { off, midi, key };

// --- what a mapping drives -------------------------------------------------------------------------------------------
struct Target
{
    RemoteAction action = RemoteAction::parameter;
    AutomationTarget param;     // parameter
    int track = -1, scene = -1; // clipSlot: both; trackStop: track; sceneLaunch: scene
    bool operator== (const Target&) const = default;

    static Target parameter (const AutomationTarget& t) { return { RemoteAction::parameter, t, -1, -1 }; }
    static Target slot (int track, int scene) { return { RemoteAction::clipSlot, {}, track, scene }; }
    static Target sceneLaunch (int scene) { return { RemoteAction::sceneLaunch, {}, -1, scene }; }
    static Target trackStop (int track) { return { RemoteAction::trackStop, {}, track, -1 }; }
    static Target of (RemoteAction a) { return { a, {}, -1, -1 }; }
    static Target selectedClip (int track) { return { RemoteAction::launchSelectedClip, {}, track, -1 }; } // (round 190kc)
};
bool valid (const Target& t) noexcept;
Target targetOf (const RemoteMapping& m);
// a button: its press / release acts (a parameter moves to a value instead)
inline bool isButton (const Target& t) noexcept { return t.action != RemoteAction::parameter; }

// --- ranges ------------------------------------------------------------------------------------------------------------
// The control's position for a controller at `x` (0..1): Min + (Max − Min) · x (Min above Max: inverted).
float applyRange (float x, float min, float max) noexcept;
// A key (or a button) mapped to a parameter: each press toggles it between Min and Max (round 190em, as Live 12.4.16
// does: Min first, Max when it is at Min).
float keyToggle (float current, float min, float max) noexcept;

// --- buttons -----------------------------------------------------------------------------------------------------------
enum class Edge : std::uint8_t { none, press, release };
// A MIDI key (number ≥ linkNoteBase): a velocity above 0 presses, 0 releases. A controller presses when it goes to 64 or
// above and releases when it goes below (`previous` < 0: nothing seen yet, as if released).
Edge midiEdge (int number, int value, int previous) noexcept;

// --- assigning (the map modes) -----------------------------------------------------------------------------------------
struct MidiControl
{
    int channel = -1, number = -1, port = -1;
    bool operator== (const MidiControl&) const = default;
};
enum class Refusal : std::uint8_t
{
    none,
    sameTrack,     // the control already launches another slot of the same track
    usedBySlot,    // the control already launches a clip slot: not for a scene too
    invalid        // nothing to map
};
// The next MIDI control for `target`: a parameter gets a MidiLink (the target's earlier links go, as MIDI learn does),
// anything else a RemoteMapping (its earlier MIDI mapping goes). A control may drive several targets (the lesson maps one
// knob to several volumes), except the two refusals above.
Refusal assignMidi (Song& song, const Target& target, MidiControl control);
// The same for a computer key (`key`: an upper-case letter / digit or another key's code).
Refusal assignKey (Song& song, const Target& target, int key);
// Removes the target's mappings of one kind (MIDI: its links too). Returns how many went.
int unmap (Song& song, const Target& target, Mode kind);

// --- what is mapped (the overlay's labels) -------------------------------------------------------------------------------
struct Assigned
{
    bool found = false;
    int channel = -1, number = -1, key = 0;
};
Assigned midiOf (const Song& song, const Target& target);
Assigned keyOf (const Song& song, const Target& target);

// --- the Mapping Browser -------------------------------------------------------------------------------------------------
struct Row
{
    bool link = false; // a MidiLink (Song::midiLinks[index]) or a RemoteMapping (Song::remoteMappings[index])
    int index = -1;
    int channel = -1, number = -1, key = 0;
    Target target;
    float min = 0.0f, max = 1.0f;
    bool hasRange = false; // parameters: Min and Max can be edited
    // round 190hp: a mapping kept for every project (index into prefs::MidiPrefs::globalLinks when `link`, else
    // globalMappings); `overridden`: this project maps the same control or target itself, so it does not act here
    bool global = false, overridden = false;
    bool operator== (const Row&) const = default;
};
// The rows of one map mode, in the order they were made (MIDI: the links of the project, then the other MIDI mappings).
std::vector<Row> rows (const Song& song, Mode kind);
bool setRange (Song& song, const Row& row, float min, float max);
bool invertRange (Song& song, const Row& row); // Min and Max swap
bool removeRow (Song& song, const Row& row);
// MIDI channel as the list shows it (1..16, "All" for any is the caller's text), a controller "CC 16", a key "C3".
std::string controlText (int number);
std::string noteText (int note); // the reference's octave numbering: middle C (60) = C3

// --- scene rows move (ClipLauncher insertScene / removeScene keep the mappings on their slots and scenes) --------------------
void scenesInserted (Song& song, int at) noexcept;
void sceneRemoved (Song& song, int scene);

// --- acting --------------------------------------------------------------------------------------------------------------
// The RemoteMappings a MIDI event fires (parameters excluded: their MIDI mappings are links), with the edge it makes.
struct Fire
{
    Target target;
    Edge edge = Edge::none;
    float min = 0.0f, max = 1.0f;
};
std::vector<Fire> midiFires (const Song& song, int channel, int number, int value, int previous, int port);
// The mappings of a computer key (every kind of target; parameters toggle on a press).
std::vector<Fire> keyFires (const Song& song, int key, bool down);
// The MIDI numbers the mappings take (bits as AudioEngine::linkedCcs / linkedNotes): their messages go to the mappings
// instead of the instruments.
void usedNumbers (const Song& song, std::uint64_t ccs[2], std::uint64_t notes[2]) noexcept;

// --- for every project (round 190hp, slice B9 of the 0.1.28 pain report; LYNX's own) ------------------------------------
// The reference keeps MIDI mappings with each set; the Mapping Browser's "In every project (LYNX)" moves a MIDI mapping
// into the settings. A parameter's mapping becomes FL's global link (prefs::GlobalLink, the same list FL's "Override
// global link..." makes, so both layouts share them); the transport switches, Stop all clips and a scene's launch
// button become prefs::GlobalMapping. Clip slots and track stop buttons belong to one project's tracks: not offered.
// A project's own mapping of the same control or target wins (as FL's project links over global ones). A global
// mapping acts where its target exists: a scene launch only while the project has that scene; a parameter (a channel,
// an insert, a slot) only while it exists (the engine ignores a link to a missing one).
bool canBeGlobal (const Target& t) noexcept;
// The MIDI rows of the global mappings (after the project's in the Browser), `overridden` set from the song.
std::vector<Row> globalRows (const Song& song, const prefs::MidiPrefs& prefs);
// The project's MIDI row into the settings (it leaves the project; a global entry of the same target or control is
// replaced), or a global row back into this project only. False when the row cannot move.
bool canMove (const Song& song, const prefs::MidiPrefs& prefs, const Row& row, bool toEveryProject);
bool moveToEveryProject (Song& song, prefs::MidiPrefs& prefs, const Row& row);
bool moveToThisProject (Song& song, prefs::MidiPrefs& prefs, const Row& row);
bool removeGlobalRow (prefs::MidiPrefs& prefs, const Row& row);
bool setGlobalRange (prefs::MidiPrefs& prefs, const Row& row, float min, float max);
// The global mappings the project does not override (the ones that act in it).
std::vector<RemoteMapping> activeGlobalMappings (const Song& song, const prefs::MidiPrefs& prefs);
// What a mapped element shows: its project mapping, else its global one.
Assigned midiOf (const Song& song, const prefs::MidiPrefs& prefs, const Target& target);
// midiFires / usedNumbers with the global mappings that act in this project too.
std::vector<Fire> midiFires (const Song& song, const std::vector<RemoteMapping>& global, int channel, int number, int value, int previous, int port);
void usedNumbers (const std::vector<RemoteMapping>& global, std::uint64_t ccs[2], std::uint64_t notes[2]) noexcept;
} // namespace lynx::remotemap
