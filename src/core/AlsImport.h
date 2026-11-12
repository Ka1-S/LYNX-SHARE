#pragma once

// Live set (.als) import, round 175a (format: docs/analise/ALS_FORMAT.md). Clean-room: from the sets Live ships and the
// member names in its binary, nothing copied. The set becomes a LYNX song the way an .flp does:
//  - a MIDI track → a channel (its instrument) + a mixer insert + a Playlist track; each MIDI clip → its own pattern;
//  - an audio track → an audio-clip channel per sample + audio clips (warp → the clip's stretch);
//  - group tracks → an insert the members route to (their Playlist tracks grouped under a header);
//  - return tracks → inserts fed by the tracks' sends;
//  - tempo, time signature, locators, mixer volume / pan / mute / solo / send levels and their automation;
//  - Session clips and scenes → the clip grid (round 181c, the default by the user's choice of 2026-11-08: launch mode,
//    quantisation, legato, loop, follow actions, stop buttons, scene names / tempo / meter); or, with
//    `sessionOnPlaylist`, the Playlist after the arrangement, one section per scene (a marker per scene);
//  - third-party VST2 / VST3 plugins → PluginRef::foreign (resolved like an .flp's, state carried over);
//  - Live's own devices → the closest LYNX instrument (Simpler / Sampler / Drum rack / Instrument rack); the rest is
//    listed in the report (unsupported / droppedEffects), as for an .flp.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/FlPluginDb.h"
#include "core/FlpImport.h"
#include "core/Song.h"

namespace lynx
{
namespace xml
{
class Document;
}

struct AlsImportOptions
{
    // The .flp import's options that apply here: projectFolder (the set's folder), fileExists, relocations, report
    // (FlpImportReport: the same report and Missing files window), sampleSeconds. FL's path roots are not used.
    FlpImportOptions common;
    // Folders the library-relative sample paths are relative to (RelativePathType 3 / 5 / 6): Live's Core Library,
    // installed packs, the user library. Searched in order.
    std::vector<std::string> libraryRoots;
    // Session clips: false (default, the user's choice of 2026-11-08) → LYNX's clip grid, scene for scene (round 181c);
    // true → the Playlist after the arrangement, one section per scene (the default before).
    bool sessionOnPlaylist = false;
    // Drum Racks (round 190ec): false (default) → a LYNX Drum Rack with the pads' chains, choke groups, returns and
    // sends; true → one sampler channel per pad, as an FL drum kit (the import before 190ec)
    bool drumPadsAsChannels = false;
};

// True for a gzip file whose XML root is a Live set (also for plain XML starting with <?xml … <Ableton).
bool isAlsData (const std::uint8_t* data, std::size_t size);

// The set as a LYNX song, or nullopt with `error` (not a Live set, corrupt, unsupported version).
std::optional<Song> importAls (const std::vector<std::uint8_t>& fileBytes, const AlsImportOptions& options = {}, std::string* error = nullptr);
std::optional<Song> importAlsXml (const xml::Document& document, const AlsImportOptions& options = {}, std::string* error = nullptr);

// The set's third-party plugins (no import): for the targeted scan before an open, as flpForeignPlugins for an .flp.
std::vector<FlpPluginUse> alsForeignPlugins (const std::vector<std::uint8_t>& fileBytes);

// Live's time signature code: (numerator − 1) + 99 × log2(denominator); false for an invalid code.
bool alsTimeSignature (int code, int& numerator, int& denominator) noexcept;
int alsTimeSignatureCode (int numerator, int denominator) noexcept;

// A VST3 class id from Live's four Uid fields (the FUID's four 32-bit words), in ForeignPlugin::classId's layout
// (the TUID as Windows' COM-compatible FUID lays it out in memory), and back.
std::array<std::uint8_t, 16> alsVst3ClassId (const std::array<std::int32_t, 4>& fields) noexcept;
std::array<std::int32_t, 4> alsVst3Fields (const std::array<std::uint8_t, 16>& classId) noexcept;

// Live's hex blobs (plugin states): pairs of hex digits, whitespace ignored; empty on a bad digit.
std::vector<std::uint8_t> alsHexDecode (const std::string& text);
std::string alsHexEncode (const std::vector<std::uint8_t>& bytes, std::size_t lineBytes = 40);

// Live's 70-colour clip/track palette index → 0xFFRRGGBB (LYNX's own approximation of each hue; 0 for unknown).
std::uint32_t alsColour (int index) noexcept;
int alsColourIndex (std::uint32_t argb) noexcept; // the nearest palette index (for the export)
} // namespace lynx
