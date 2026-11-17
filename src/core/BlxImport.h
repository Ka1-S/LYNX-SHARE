#pragma once

// BandLab project (.blx) import, round 190jr (B15). A LYNX-own import (FL Studio has none), clean-room.
//
// What a .blx is: Cakewalk / Sonar's "BandLab Interchange file", written next to the project's downloaded audio when a
// BandLab project is opened in the desktop DAW (Documents\BandLab\Downloads\<project>\). Cakewalk users describe it as
// a JSON file with the project and its tracks; the BandLab web Studio itself has no .blx export (only Download ›
// Tracks / Mixdown). No .blx sample is public, so the reader takes BandLab's revision JSON — the project tree the web
// Studio saves and loads — wherever it sits in the file (the top object, or an object inside it that holds "tracks"),
// and the zip variant is unpacked by the engine before it gets here. Field names and units are facts read from
// public sources only: BandLab's own Android app's revision model (com.bandlab.revision.objects: Revision, Track,
// Region, Sample, AutomationPoint, Metronome.Signature) and the measurements published with the MIT-licensed
// bandlab-mcp project (region positions in SECONDS, automation positions in BEATS, volume linear 0..2 with 1 = unity,
// pan -1..1, effects = an ordered `effects[]` of { slug, bypass, params }). No code was copied.
//
// The song becomes a Simple-workflow project (core/SimpleTracks.h), one LYNX track per BandLab track:
//  - tempo, signature, key, title;
//  - an audio track ("voice", or regions of audio samples) → an Audio track; each region an audio clip of the sample
//    file found next to the .blx (by its sample id, its file name or the region's name), with its offset, gain, fades,
//    pitch and playback rate (the channel's stretch, pitch kept); a looped region (loopLength) repeats as clips;
//  - a MIDI track → an Instrument track (LYNX's built-in synth, preset by the BandLab sound's name) or a Drums track
//    (built-in kick / snare / clap / hat by General MIDI key); each region a pattern clip of its MIDI sample's notes
//    (a .mid next to the .blx); a looped region loops its pattern;
//  - volume, pan, mute on the track's insert / track; volume and pan automation as automation lanes; the send to
//    BandLab's shared reverb as the Simple layout's shared reverb send;
//  - effects that have a LYNX equivalent (compressor, 3-band EQ, graphic EQ, reverbs, delays, chorus, distortion,
//    limiter); everything else is listed in the import report.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/FlpImport.h"
#include "core/Song.h"

namespace lynx
{
namespace blx
{
struct Sample
{
    std::string id, name, file;
    bool midi = false;
    double duration = 0.0; // seconds (0 = unknown)
};

struct Region
{
    std::string id, name, sampleId;
    double start = 0.0, end = 0.0;   // seconds in the song
    double offset = 0.0;             // seconds skipped at the sample's start
    double loopLength = 0.0;         // seconds of one pass when the region loops (0 = no loop)
    double playbackRate = 1.0;       // 1 = the sample's own speed
    double pitchShift = 0.0;         // semitones
    double gain = 1.0;               // linear
    double fadeIn = 0.0, fadeOut = 0.0; // seconds
};

struct AutoPoint
{
    double beat = 0.0;  // quarter notes from the song's start
    double value = 0.0; // volume: linear 0..2; pan: -1..1
};

struct Effect
{
    std::string slug;
    bool bypass = false;
    std::map<std::string, double> params;    // numbers only
    std::map<std::string, std::string> text; // strings (irType...)
    bool automated = false;
};

struct Track
{
    std::string id, name, type, soundbank, preset, colorName;
    int order = 0;
    double volume = 1.0, pan = 0.0;
    bool muted = false, solo = false, frozen = false;
    std::vector<Region> regions;
    std::vector<Effect> effects;
    std::vector<AutoPoint> volumeAutomation, panAutomation;
    double reverbSend = 0.0; // the send to the shared reverb aux (sendLevel)
    bool hasPatterns = false, hasSamplerKit = false, hasEffectAutomation = false;
};

struct Project
{
    std::string title, key;
    double bpm = 120.0;
    int numerator = 4, denominator = 4;
    double masterVolume = 1.0;
    std::string mastering; // the mastering preset ("" = none)
    std::vector<Track> tracks;
    std::vector<Sample> samples;
    int auxChannels = 0;
    bool hasLyrics = false;
};

// Reads a revision from a .blx's bytes (JSON, UTF-8, optional BOM). nullopt + `error` (English) when it is not one.
std::optional<Project> parse (const std::vector<std::uint8_t>& bytes, std::string* error = nullptr);

// A Standard MIDI File's notes in quarter notes (format 0 or 1, every track and channel merged). nullopt when the bytes
// are not a MIDI file.
struct MidiNote
{
    double beat = 0.0, length = 0.0;
    int key = 60, velocity = 100;
};
std::optional<std::vector<MidiNote>> readMidi (const std::vector<std::uint8_t>& bytes);

// "D min", "Dm", "F# Major", "Bb minor", "C" → root 0..11 and minor; root −1 when not readable.
std::pair<int, bool> parseKey (const std::string& key) noexcept;

// BandLab's colour names (Red, Blue, Violet, Pink, Green, Yellow) → 0xFFRRGGBB, 0 for others.
std::uint32_t colourOf (const std::string& colorName) noexcept;
} // namespace blx

struct BlxImportOptions
{
    // fileExists, readFile, sampleSeconds, relocations and report are used (the others are FL's)
    FlpImportOptions common;
    // the .blx's folder: the sample files are looked for in it and its sub-folders
    std::string folder;
    // the files under `folder` ('/'-separated paths relative to it); empty = only the direct names are tried
    std::vector<std::string> files;
};

// The project as a LYNX song.
Song importBlx (const blx::Project& project, const BlxImportOptions& options);

// Parses `bytes` and imports them. nullopt + `error` (English) when it is not a BandLab project.
std::optional<Song> importBlxBytes (const std::vector<std::uint8_t>& bytes, const BlxImportOptions& options, std::string* error = nullptr);

// The file a region's sample plays (absolute path, or "" when not found): `sampleId.*`, the sample's own file name,
// then the region's name, among options.files (audio extensions for audio, .mid / .midi for MIDI). Exposed for tests.
std::string blxSampleFile (const blx::Sample* sample, const std::string& sampleId, const std::string& regionName, bool midi,
                           const BlxImportOptions& options);
// The path the import report lists for a region whose file is not found (and the key of its relocation).
std::string blxMissingPath (const BlxImportOptions& options, const std::string& sampleId, const std::string& regionName, bool midi);
} // namespace lynx
