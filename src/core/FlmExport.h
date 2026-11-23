#pragma once

// FL Studio Mobile project (.flm) export, round 190og: "Save a copy as FL Studio Mobile project (.flm)..." (a LYNX-own
// command; FL Studio has no .flm writer). The song becomes an FLM 5 project the way FLM itself writes one
// (docs/analise/FLM_FORMAT.md; core/FlmFormat.h's writer) and the .flm import (core/FlmImport.h) reads it back:
//  - the song: title, tempo, beats per bar, the key (FLM's key locker), the Chord pad set (KEYB), the credits (META);
//  - one FLM channel (a rack and its track lanes) per instrument channel; sampler channels that share an insert become
//    one drum channel (each a pad of its kit, the step lane playing kit[key]); one audio channel per Playlist track
//    that holds audio clips;
//  - the mixer: an insert used by one channel is that channel's rack (its level, pan, mute, solo, effects); an insert
//    several channels use, or one that receives sends, becomes an FLM bus channel the others send into; the Master's
//    effects and level are FLM's master rack;
//  - instruments FLM has a module for keep their sound: FL Studio Mobile's own engines in LYNX (MiniSynth, SuperSaw,
//    GMSynth, Transistor Bass in FLM's build, the DW Sampler with its zones, 3x Osc) and FL's MiniSynth / Transistor Bass /
//    3x Osc; any other instrument keeps its notes on FLM's MiniSynth and is listed;
//  - effects: every Pocket engine (FLM's own effects in LYNX), with its Mix and the Post Gain a utility after it holds;
//  - automation clips of an insert's level / pan, of Pocket effects and of the DW Sampler's / GMSynth's own values;
//  - the notes: every pattern clip of the Playlist as a clip of its channels' lanes (one FLM content per pattern and
//    channel, so a pattern placed twice stays one linked content); audio clips with their level, pan, pitch, reverse;
//  - what FLM cannot hold is listed (FlmExportLoss, the wording of the "Save a copy for an older LYNX" list).
// Clean-room: written from FLM_FORMAT.md; no Image-Line code or data.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
struct FlmExportOptions
{
    std::string title;          // the song's name ("" = Song::info.title, else "LYNX SHARE")
    std::string samplesFolder;  // the folder beside the .flm the samples are copied to ("" = "<title> Samples")
    std::string targetFolder;   // the .flm's folder (PRST keeps the copied samples' absolute paths there; "" = none)
    double nowSeconds = 0.0;    // the save time, seconds since 1970 UTC (META's last-saved date; 0 = left empty)
    std::string appVersion = "5.0.4"; // HEAD's app version (the newest FLM the import was checked against)
    // FL Studio Mobile's factory content on this computer (the .flm import's resource folders: FL's copy, the user's own):
    // a sample inside one stays a factory reference ("@R@", nothing copied), as FLM itself saves it
    std::vector<std::string> resourceRoots;
    // a sample's length in seconds (audio clips' source length; 0 = unknown, the clip's own length is used)
    std::function<double (const std::string&)> sampleSeconds;
};

struct FlmExportLoss
{
    std::string text;   // English, with {0} for `detail` (translated by the UI)
    std::string detail; // what it applies to (names)
    bool sound = true;  // true: the copy may sound different; false: left out, the sound stays
};

struct FlmExportSample
{
    std::string source;   // the file on this computer
    std::string relative; // where it goes, relative to the .flm's folder ('/' separated)
};

struct FlmExportResult
{
    bool ok = false;
    std::string error;
    std::vector<std::uint8_t> bytes;
    std::vector<FlmExportLoss> lost;
    std::vector<FlmExportSample> samples;
};

FlmExportResult exportFlm (const Song& song, const FlmExportOptions& options = {});

// The LYNX chunk (round 190og): FLM keeps one pattern per track, so the export adds at the end of the file a chunk FLM's
// loader skips (an unknown tag; FLM_FORMAT §1) with what LYNX needs to rebuild its own patterns — which FLM content of
// which channel was which LYNX pattern, and each pattern's name and colour — and a checksum of everything before it.
// The import uses it only while the checksum holds (a song saved again by FLM has no such chunk, or other bytes).
namespace flmexport
{
struct Extras
{
    struct PatternInfo
    {
        std::string name;
        std::uint32_t colour = 0;
    };
    std::vector<PatternInfo> patterns;                                 // by the original pattern index
    std::map<std::pair<int, std::uint32_t>, int> contents;            // (FLM channel index, content id) → pattern
};
std::optional<Extras> readExtras (const std::vector<std::uint8_t>& fileBytes);
// Patterns made per FLM content merged back into the song's patterns, their names and colours restored, and the pattern
// clips the split had made of one clip turned back into one
void regroupPatterns (Song& song, const Extras& extras, const std::map<std::pair<int, std::uint32_t>, int>& contentPatterns);
} // namespace flmexport

// The laws the export inverts (exposed for the tests): FLM's fader position for a linear gain (0 dB = 0.853), the
// module wrapper's Post Gain for a gain (40g − 20 dB), a drum pad's Speed value for whole semitones
float flmFaderValue (double gain) noexcept;
float flmPostGainValue (double gain) noexcept;
float flmSpeedValue (int semitones) noexcept;
} // namespace lynx
