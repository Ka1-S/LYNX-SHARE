#pragma once

// Multisample instruments from other samplers opened as LYNX DirectWave channels (round 190ii, issue #149).
//
// Every format ends as a DirectWave program state (docs/analise/REA_rodada9.md §2, REA_rodada190ii.md) handed to
// dw::importChannel (core/DirectWave.h), the same entry an .flp's DirectWave channel takes, so the channel plays on
// LYNX's DirectWave engine and opens in its editor:
//   .dwp  FL's DirectWave program ("DwPr" + version + chunks): the program as FL saved it, its samples found the way
//         DirectWave finds them (REA_rodada190ii.md §1)
//   .dwb  FL's DirectWave bank ("DwBk" + the state): the bank's first program with zones
//   .gig  GigaStudio / LinuxSampler instruments (core/GigFile.h): regions × dimensions → zones, the samples written
//         out as WAV files
//   .nki  Kontakt 1 / 2 instruments that are not encrypted (core/KontaktFile.h): their zone list
// Pure C++ (no JUCE): files come and go through the callbacks of `Access`.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/FlpImport.h"
#include "core/Song.h"

namespace lynx::msimport
{
// ------------------------------------------------------------------------------------------------ a neutral program
struct Zone
{
    std::string name;
    std::string path;                      // an existing sample file, or empty with `wav`
    std::vector<std::uint8_t> wav;         // audio to store as a WAV file (formats whose samples live in the file)
    int root = 60, lowKey = 0, highKey = 127, lowVel = 0, highVel = 127;
    int coarse = 0;                        // semitones
    int fine = 0;                          // cents
    int keyTrack = 100;                    // cents per key
    double gainDb = 0.0;                   // ≤ 0 (DirectWave's zone gain stops at 0 dB)
    double pan = 0.0;                      // −1 … 1
    int loopMode = 0;                      // DirectWave's: 0 off, 1 one-shot, 2 forward, 3 sustained, 4 bounce
    std::int64_t loopStart = 0, loopEnd = 0; // frames, end exclusive
    std::int64_t offset = 0;               // sample start
    int exclusiveGroup = 0;                // 0 none, else 1 … 255 (a new note of the group ends the others)
    int altGroup = 0;                      // 0 none, else 1 … 99 (Program::groups says how the group alternates)
    // amplitude envelope; < 0 = DirectWave's default for a new zone (A 0, D 0.625 s, S 100 %, R 0.039 s)
    double attack = -1.0, decay = -1.0, release = -1.0; // seconds
    double sustain = -1.0;                 // 0 … 1, amplitude
    // the sample's format (frames 0 = unknown; DirectWave takes the loaded file's own)
    std::int64_t frames = 0;
    int channels = 1, bits = 16;
    double rate = 44100.0;
};

struct Group
{
    int index = 1;                         // 1 … 99
    int mode = 0;                          // DirectWave's: 0 all play, 1 cycle (round robin), 2 random, 3 random, not the previous
};

struct Program
{
    std::string name;
    std::vector<Zone> zones;
    std::vector<Group> groups;
};

// A DirectWave state (version 38) holding `program` as its only program: FL's record of a new zone for every field
// the program does not set (the bytes of FL's own .dwp files), the zones' paths as given. Zones with `wav` must have
// had their path filled first (see importProgram).
std::vector<std::uint8_t> buildState (const Program& program);

// ------------------------------------------------------------------------------------------------ files
struct Access
{
    std::function<bool (const std::string&)> exists;                         // a file exists
    // a file's first min(size, `limit`) bytes (empty: missing or unreadable)
    std::function<std::vector<std::uint8_t> (const std::string&, std::size_t limit)> read;
    // `count` bytes at `offset` of a file (large libraries are read piece by piece); empty = unreadable
    std::function<std::vector<std::uint8_t> (const std::string&, std::uint64_t offset, std::size_t count)> readAt;
    std::function<std::uint64_t (const std::string&)> size;                  // 0 = missing
    // stores audio that only exists inside the imported file as a WAV file; returns its path ("" = failed)
    std::function<std::string (const std::vector<std::uint8_t>& wav, const std::string& name)> saveWav;
    // a file named `fileName` somewhere below `folder` (a bounded search); "" = none. Optional.
    std::function<std::string (const std::string& folder, const std::string& fileName)> findBelow;
    FlPathRoots roots;                                                       // FL's path variables (.dwp)
    char separator = '\\';                                                   // this system's path separator
};

struct Report
{
    std::string format;                    // "DirectWave program", "GigaStudio", …
    std::string name;                      // the program / instrument
    int instruments = 0;                   // in the file (.gig: several)
    int instrument = 0;                    // the one imported
    int zones = 0;                         // zones of the LYNX channel
    int missingSamples = 0;
    std::vector<std::string> skipped;      // what could not be brought over, in words (shown by the import dialog)
    std::string error;                     // why nothing was imported
};

// The formats this importer opens (lower case, without the dot)
bool handles (const std::string& extension);
// The instruments of a file with several (.gig): their names; one entry for the other formats; empty = unreadable
std::vector<std::string> instrumentNames (const std::string& path, const Access& access);
// Opens `path` as a DirectWave channel (instrument `instrument` of a .gig). nullopt + report.error when it can't.
std::optional<ChannelDef> importFile (const std::string& path, const Access& access, Report& report, int instrument = 0);
// A neutral program as a channel: the zones' audio stored through access.saveWav, then dw::importChannel
std::optional<ChannelDef> importProgram (Program program, const Access& access, Report& report);

// ------------------------------------------------------------------------------------------------ .dwp / .dwb
// The state inside a DirectWave program / bank file ("DwPr" / "DwBk", versions 36 … 38), its short chunks completed
// the way DirectWave reads them (a version-36 sample format record has 36 bytes: the bit depth reads as 32);
// nullopt when it is not one.
std::optional<std::vector<std::uint8_t>> stateOfFile (const std::vector<std::uint8_t>& file);
// Where DirectWave looks for a zone's sample when its saved path does not exist (REA_rodada190ii.md §1): the path
// with FL's variables, then "<program file without extension>/<file name>", then (LYNX, for FL's own sample search)
// the program's folder and below it, then DirectWave's library (%ILSharedData%\DirectWave) searched. "" = not found.
std::string findZoneSample (const std::string& savedPath, const std::string& programFile, const Access& access);

// ------------------------------------------------------------------------------------------------ helpers
// A 16- / 24-bit PCM or 32-bit float WAV of interleaved frames (`bits` 8, 16, 24, 32 = PCM, -32 = float)
std::vector<std::uint8_t> wavFile (const std::uint8_t* data, std::size_t bytes, int channels, int bits, double rate,
                                   std::int64_t loopStart = -1, std::int64_t loopEnd = -1, int root = -1);
std::string extensionOf (const std::string& path);   // lower case, without the dot
std::string fileNameOf (const std::string& path);    // after the last '\' or '/'
} // namespace lynx::msimport
