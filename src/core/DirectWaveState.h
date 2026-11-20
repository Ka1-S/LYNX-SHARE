#pragma once

// DirectWave's saved state as an editable document (round 190hz, issue #157; the layout: docs/analise/REA_rodada9.md §2,
// REA_rodada190hz_dw.md). The state (the channel's 0xD5 record, a .dwb bank after "DwBk", a .dwp program after "DwPr")
// is a version word and a list of chunks (u32 id, u64 size, body); programs (id 1) and their zones (id 3) hold chunks of
// their own. The document keeps every chunk as read, so writing it back gives the same bytes; the editor changes a
// program through setProgram, which rewrites only the fields that changed and keeps every byte it does not know.
// Pure C++ (no JUCE).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/DirectWave.h"
#include "core/Sfz.h"

namespace lynx::dw
{
struct Node
{
    std::uint32_t id = 0;
    std::vector<std::uint8_t> body; // leaf chunks
    std::vector<Node> kids;         // programs (1) and zones (3)
    bool container = false;
};

struct Doc
{
    std::uint32_t version = 38;     // FL 2026 writes 38 (DirectWave_x64.dll 0x180040750)
    std::vector<Node> top;          // flags (6), options (0x206), 16 MIDI slots (5), the programs (1)
};

std::optional<Doc> readDoc (const std::vector<std::uint8_t>& state);
std::vector<std::uint8_t> writeDoc (const Doc& doc);
// Gives every zone record shorter than LYNX reads its full size, as DirectWave reads it (zeros; the sample format
// record's bit depth 32; zoneRecordSize), e.g. the 36-byte sample format records of FL's version-36 programs, for a
// release whose reader skipped such records (File › "Save a copy for an older LYNX", rollup 0.1.30). Returns how many.
int completeShortZoneRecords (Doc& doc);

// A new DirectWave's state: no flags, 16 MIDI slots on programs 0…15, 128 empty programs named "---"
Doc freshDoc();

// ---- programs
int programCount (const Doc& doc);                 // the program records in order (128 in a bank)
Program program (const Doc& doc, int index);       // index: the record's place; zones carry their origin
// Writes `edited` over program record `index`: changed fields only; zones by their origin (−1: a new zone record).
void setProgram (Doc& doc, int index, const Program& edited);
// The program MIDI slot `slot` (0…15) plays, and setting it (the editor's program arrows on that MIDI channel)
int slotProgram (const Doc& doc, int slot);
void setSlotProgram (Doc& doc, int slot, int program);

// A zone as FL's editor makes it for a new sample (Add sample(s), a drop): the file's format, keys and FL's defaults
Zone newZone (const std::string& path, const std::string& name, std::int64_t frames, int channels, float rate, int bits,
              int root, int lowKey, int highKey);

// ---- files
// .dwp: "DwPr", version, flags chunk, one program record (DirectWave_x64.dll 0x180014140)
std::vector<std::uint8_t> writeProgramFile (const Doc& doc, int index);
// The program of a .dwp (versions 36…38), or nullopt
std::optional<Program> readProgramFile (const std::vector<std::uint8_t>& file);
// Puts a loaded program into record `index` (keeps the record's index; the zones become new records)
void loadProgram (Doc& doc, int index, Program loaded);
// .dwb: "DwBk" + the whole state (0x180014020)
std::vector<std::uint8_t> writeBankFile (const Doc& doc);
std::optional<Doc> readBankFile (const std::vector<std::uint8_t>& file);

// ---- the zone menu (DirectWave_x64.dll's command handler 0x180018c17; REA_rodada190hz_dw.md §3)
// "Automap zone(s)" (0x180013d50): each zone with a sample spans from its root to below the next higher root (127 at
// the top); then the zones starting lowest reach down to key 0. `hasSample[i]`: zone i has audio.
void automap (std::vector<Zone>& zones, const std::vector<bool>& hasSample);
// "Remap zone(s) as drumkit": zone i's root becomes key 60 + i (mod 128), then Automap
void remapAsDrumkit (std::vector<Zone>& zones, const std::vector<bool>& hasSample);
// "Copy to all zones" / "Copy to selected zones": one field of zone `from` into every other zone that is not locked
// (header byte 6) and, when `targets` is not empty, is one of the targets
enum CopyField
{
    CopyKeyTracking, CopyFineTune, CopyTicks, CopyTranspose, CopyBeatSync, CopyWaveSlice, CopyMuteGroup, CopySampleRate,
    CopyRootKey, CopyLowKey, CopyHighKey, CopyLowVelocity, CopyHighVelocity
};
void copyToZones (std::vector<Zone>& zones, int from, int what, const std::vector<bool>& targets);

// ---- loading a sample into a zone (0x18001ddb0, 0x180043d40)
// What a WAV's own chunks set: 'smpl' (root = the unity note; the first loop → Forward, A = start, B = end + 1) and
// 'inst' (root, fine tune, key and velocity range). Fields stay −1 when the file does not hold them.
struct WavInfo
{
    int root = -1, fine = 0, lowKey = -1, highKey = -1, lowVel = -1, highVel = -1;
    int loopMode = -1, loopA = 0, loopB = 0;
    bool hasInst = false;
};
std::optional<WavInfo> readWavInfo (const std::vector<std::uint8_t>& wav);
// The zone's name for a file (0x180003c70): the path up to its last '.', after its last '\' or '/'
std::string zoneNameOf (const std::string& path);

// ---- importing (issue #157 asks for SoundFont / SFZ programs; FL converts them with its own importers, which were not
// read: LYNX's mapping, approximate). An .sfz's regions as zones: sample (with default_path, relative to `folder`),
// lokey / hikey / key, lovel / hivel, pitch_keycenter, pitch_keytrack, transpose, tune, volume / gain, pan, offset,
// loop_mode, loop_start / loop_end. Each zone's format (frames, channels, rate, bits) is left for the caller to fill
// from the file; `resolve` turns the joined path into an existing file ("" = missing: the region is skipped).
Program programFromSfz (const sfz::Parsed& parsed, const std::string& folder, const std::function<std::string (const std::string&)>& resolve);
} // namespace lynx::dw
