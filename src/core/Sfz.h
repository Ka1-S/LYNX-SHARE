#pragma once

// SFZ instruments (round 167o, issue #22; https://sfzformat.com): an .sfz file is read into a LYNX Soundfont font
// (core/SoundFont.h) and played by its voices. FL Studio loads SFZ through DirectWave (its import list has .sfz,
// docs/analise/REA_rodada167o.md); in LYNX SHARE an .sfz opens as a channel of LYNX's own sampler-player.
//
// Supported: the headers <control> <global> <master> <group> <region> with their inheritance, comments, #define and
// #include, default_path / note_offset / octave_offset, note names (c4 = 60), and the opcodes
//   sample (WAV, and what the app's decoder reads: FLAC, OGG, AIFF, MP3…), lokey / hikey / key, lovel / hivel,
//   pitch_keycenter, pitch_keytrack, transpose, tune / pitch, volume / gain, amplitude, pan, offset, end,
//   loop_mode / loopmode (no_loop, one_shot, loop_continuous, loop_sustain), loop_start / loop_end (and the v1
//   loopstart / loopend; the sample's own smpl loop otherwise), ampeg_delay / attack / hold / decay / sustain /
//   release, amp_veltrack, fil_type (low-pass) / cutoff / resonance, fileg_* and pitcheg_* (one modulation envelope),
//   amplfo_*, fillfo_*, pitchlfo_* (delay, freq, depth), group / off_by / off_mode, seq_length / seq_position,
//   lorand / hirand, trigger (attack, release), sw_lokey / sw_hikey / sw_last / sw_default, delay.
// Unknown opcodes and headers are skipped (counted in the report).

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/SoundFont.h"

namespace lynx::sfz
{
// One region after the headers' inheritance (global → master → group → region): its opcodes, the last value wins.
struct ParsedRegion
{
    std::map<std::string, std::string> opcodes;
    std::string get (const std::string& name, const std::string& fallback = {}) const;
};

struct Parsed
{
    std::vector<ParsedRegion> regions;
    std::map<std::string, std::string> control; // <control>
    int unknownHeaders = 0;
};

// Parses SFZ text. `readInclude` returns the text of an #include'd file (relative to the .sfz's folder), or nullopt.
Parsed parse (const std::string& text, const std::function<std::optional<std::string> (const std::string&)>& readInclude = {});

// "c4" → 60, "c#4" / "db4" → 61, "60" → 60; -1 when unreadable
int noteNumber (const std::string& text);

// A decoded sample file: channels of floats, its rate, the loop in its smpl chunk (-1: none)
struct Audio
{
    std::vector<std::vector<float>> channels;
    double sampleRate = 44100.0;
    std::int64_t loopStart = -1, loopEnd = -1; // [start, end)
};
using Decoder = std::function<bool (const std::string& path, Audio& out)>;

// The decoder for every format: the app installs one with its readers (FLAC, OGG, AIFF, MP3…); without it only WAV
// files are read (tests, command-line renders).
void setDecoder (Decoder decoder);
bool decodeWavFile (const std::string& path, Audio& out);

struct Report
{
    int regions = 0, samplesLoaded = 0, samplesMissing = 0, unknownOpcodes = 0;
    std::vector<std::string> missing; // sample paths that could not be read
};

// Reads an .sfz (UTF-8 path) into a font with one preset named after the file. nullptr + `error` when the file can't
// be read or has no playable region.
std::shared_ptr<const sf::Font> loadFile (const std::string& path, std::string* error = nullptr, Report* report = nullptr);
// Same, from text already read (`folder`: where relative sample paths start)
std::shared_ptr<const sf::Font> loadText (const std::string& text, const std::string& folder, const std::string& name,
                                          std::string* error = nullptr, Report* report = nullptr);

// What the preset browser and the Browser need without reading a sample: valid when a region names a sample; one
// preset (bank 0, program 0) named after the file.
sf::FontInfo scanFile (const std::string& path);

bool isSfzPath (const std::string& path);
} // namespace lynx::sfz
