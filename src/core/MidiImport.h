#pragma once

// FL's "Import MIDI data" window as LYNX applies it (REA rounds 143, 146b; FL's TMIDIImportForm): which tracks, which
// MIDI channels and which controller to import, zero-velocity notes, and the options of File › Import › MIDI file.
// Platform-neutral: the MIDI file is read by engine/MidiFileIo (JUCE) into the raw events below.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::midiimport
{
struct RawEvent
{
    int track = 0;           // the file's track (0-based, every track of the file)
    std::int64_t tick = 0;   // LYNX ticks
    std::uint8_t status = 0; // 0x80 note off, 0x90 note on, 0xB0 controller, 0xC0 program, 0xE0 pitch bend (low nibble = MIDI channel)
    std::uint8_t data1 = 0, data2 = 0;
};

struct ParsedMidi
{
    std::vector<std::string> trackNames; // per file track ("" = none)
    std::vector<RawEvent> events;        // in time order per track
    double bpm = 120.0;                  // the first Set Tempo (FL's whole BPM, 10..522), 120 without one
    int numerator = 0, denominator = 4;  // the first time signature (0 = none)
    // every Set Tempo, in time order (issue #86): LYNX ticks, FL's BPM (round half even of 60 000 000 / µs per quarter,
    // 10..522, FUN_009864f0); File › Import writes them as the new pattern's Tempo events, as FL does
    std::vector<PatternEvent> tempos;    // value = valueFromTempo (bpm)
};

// FL's MIDI tempo (REA_rodada178a §5): a whole BPM, rounded half to even, 10..522.
double midiTempoBpm (double secondsPerQuarter) noexcept;

// The pattern's Tempo lane from a file's Set Tempo events (moved earlier by `shift` ticks, the realign), as FL's
// whole-file MIDI import writes them (FUN_00e1d090: the destination pattern's Tempo events are replaced).
void setTempoEvents (Pattern& pattern, const std::vector<PatternEvent>& tempos, std::int64_t shift);

struct Options
{
    bool newProject = false;      // Start new project
    bool perTrack = true;         // Create one channel per track (else per MIDI channel)
    bool realign = false;         // Realign events (removes the silence before the first note)
    bool timeSignatures = true;   // Import time signatures
    bool mixerTracks = true;      // Set mixer tracks for new channels
    bool midiOut = false;         // Channel type: MIDI Out (else LYNX's synth)
    int track = -1;               // Which tracks to import? −1 = all, else the file's track
    std::uint16_t channels = 0xFFFF; // Which channels to import? bit c = MIDI channel c + 1
    int controller = -1;          // Which controller to import? −1 = none, else CC 0..127 (7 / 11 volume, 10 pan)
    bool zeroVelocityNotes = false; // Import zero velocity notes: a note on with velocity 0 is a note, not a note off
    bool blend = false;           // Blend with existing data (piano roll import: keep the channel's notes)

    bool operator== (const Options&) const = default;
};

// The tracks that hold notes or the chosen controller, for the "Which tracks" list: file track indices.
std::vector<int> tracksWithContent (const ParsedMidi& midi);

// The notes of the selection (track, channels, zero velocity), each with `channel` = its group's index; `groups` gets
// the group names and their first MIDI channel (one per track with notes, or per MIDI channel when ! perTrack).
struct Group
{
    std::string name;
    int midiChannel = 0;
    int track = -1;          // per track: the file track; per channel: −1
    std::vector<Note> notes; // channel = 0
    std::vector<PatternEvent> controller; // the chosen controller's values (0..1)
    // with a MIDI Out channel type (FL's import, REA_rodada167i §1.6): every lane FL imports — CC 7 / 10 as the
    // channel's volume / pan, CC 1, 2, 4, 5, 91, 93, 64…69 onto the MIDI Out's knobs, program changes onto its patch,
    // the pitch bend
    std::vector<PatternEvents> midiOutLanes;
};
std::vector<Group> groups (const ParsedMidi& midi, const Options& options);

// File › Import › MIDI file: new channels (one per group) and a new pattern that becomes current; with
// `newProject`, the song is replaced first. False (and `error`) when the selection has no notes.
bool importInto (Song& song, const ParsedMidi& midi, const Options& options, const std::string& patternName, std::string& error);

// The piano roll's import into `channel` of the current pattern: the selection's notes (all groups merged), the
// chosen controller into the pattern's event lane of the channel's volume / pan; without `blend`, the channel's
// notes in the pattern are deleted first. Returns the indices of the imported notes in the pattern.
std::vector<int> importIntoChannel (Song& song, int channel, const ParsedMidi& midi, const Options& options, std::string& error);

// The Event editor's File › Import MIDI file / Paste from MIDI clipboard (seen in FL 2026, REA_rodada178a §11): the chosen
// controller of the selected tracks and MIDI channels as one lane, value = data / 127, at the file's ticks; with no controller
// chosen (FL's "---"), every controller merged in time order. With `realign`, moved earlier by the selection's first note (its
// first event without notes). Empty when the file has none: FL then empties the lane.
std::vector<PatternEvent> controllerEvents (const ParsedMidi& midi, const Options& options);

// The automation target for controller `cc` of `channel` (7 / 11 volume, 10 pan); kind none for the others.
AutomationTarget controllerTarget (int cc, int channel) noexcept;
} // namespace lynx::midiimport
