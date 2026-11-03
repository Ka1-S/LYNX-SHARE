#pragma once

#include <optional>

#include <juce_audio_basics/juce_audio_basics.h>

#include "core/MidiImport.h"
#include "core/Song.h"

namespace lynx
{
// MIDI files (.mid): import as a project, import notes into a channel, and export patterns.
namespace MidiFileIo
{
// New project: one channel (built-in synth) per MIDI track with notes, all in one pattern.
std::optional<Song> loadSong (const juce::File& file, juce::String& error);

// FL's File › Import › "MIDI file..." (REA round 143: the "Import MIDI data" window, TMIDIImportForm): the file's
// notes into a new pattern of `song` (it becomes the current one), one new channel per MIDI track, or per MIDI
// channel. FL's options and defaults: Start new project (off), Create one channel per track (on), Realign events
// (off: removes the silence at the start), Import time signatures (on; LYNX: the project's time signature, new
// project only), Set mixer tracks for new channels (on), Channel type (LYNX: its synth, or MIDI Out on the note's
// MIDI channel).
// (REA round 146b: also the tracks / MIDI channels / controller to import and zero-velocity notes; core/MidiImport.h)
using ImportOptions = midiimport::Options;

// The file's raw events (notes, controllers; ticks at LYNX's PPQ), track names, first tempo and time signature.
std::optional<midiimport::ParsedMidi> parseFile (const juce::File& file, juce::String& error);
// The same from a Standard MIDI File in memory (the Piano roll's "Paste from MIDI clipboard", round 168g).
std::optional<midiimport::ParsedMidi> parseBytes (const void* data, std::size_t size, juce::String& error);
// A Standard MIDI File of `notes` (muted ones left out): a tempo / time signature track and one track named
// `trackName` (FL's "Copy to MIDI clipboard": the channel's notes, FUN_01214510 → FUN_010d5e70).
juce::MemoryBlock writeNotes (const Song& song, const std::vector<Note>& notes, const std::string& trackName);

bool importInto (Song& song, const juce::File& file, const ImportOptions& options, juce::String& error);

// All notes in the file (tracks merged) into `channel`, starting at tick 0.
std::optional<std::vector<Note>> loadNotes (const juce::File& file, int channel, juce::String& error);

// One track per channel with notes in the pattern. Song tempo at the start.
bool savePattern (const Song& song, int pattern, const juce::File& file, juce::String& error);

// FL's File › Export › "MIDI file..." (REA round 139): the arrangement's notes (every pattern clip as it plays in the
// Playlist: its part of the pattern, cut at the clip's end; muted clips and notes left out), one track per channel;
// in pattern mode or with an empty Playlist, the current pattern. Tempo and time signature at the start.
bool saveSong (const Song& song, const juce::File& file, juce::String& error);
} // namespace MidiFileIo
} // namespace lynx
