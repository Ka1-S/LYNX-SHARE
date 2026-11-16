#pragma once

// GarageBand project (.band) reader, round 190ic (issue #184). Clean-room, read-only, no JUCE.
//
// A .band is a macOS package (a folder; iOS shares it as a zip). GarageBand 10 for Mac and GarageBand for iOS 2.x and
// later keep the song in Logic Pro's project format:
//  - "Alternatives/000/ProjectData" (GarageBand 10 for Mac) holds it as is;
//  - "projectData" (GarageBand for iOS, and GarageBand 10's compatibility copy) is a keyed-archive property list (XML or
//    binary) whose "DfLogicModelLogicSong" data object holds the same bytes;
//  - "Output/metadata.plist" (iOS) / "Alternatives/000/MetaData.plist" (Mac) summarise the song (tempo, signature, key);
//  - "Media/" holds the recordings and imported audio.
// GarageBand 6–11 (2010–2013 and older) wrote an older song layout without the record tags this reader needs: it is
// recognised and refused (`legacyFormat`), never guessed at.
//
// The song bytes: a 24-byte root header (magic 23 47 C0 AB, then u32 LE @+0x10 = size − 24) and a flat run of records,
// each a 36-byte header (reversed FourCC tag, u32 idx @+0x08, u32 payload size @+0x1C) and its payload. Facts used here
// come from public descriptions of Logic's format, checked again on GarageBand files before use (sources and the
// evidence per field: /mnt/e/lynx-work/band190ic/HANDOFF.md):
//  - LogicProFormatWriter's PROJECTDATA_FORMAT.md (MIT, J. Kubis): framing, tempo, signature, region placements,
//    MIDI notes, audio regions (gRuA trim/length), file references (lFuA), channel strips (OCuA fader/pan), track rows
//    (karT), environment objects (ivnE);
//  - band2flp's docs (MIT): the same framing inside GarageBand's keyed archive.
// GarageBand-specific facts found in this round (CC Jingle / Ocean, MPL-2.0, against their own arrange screenshots):
// the arrange track row's byte +0x27 bit 0 = the track's mute; a track's fader is the channel strip its row drives.
// No code of either project is used.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lynx::band
{
constexpr int ppq = 960; // the song's ticks per quarter note

struct Note
{
    std::int64_t tick = 0;   // from the region's start
    int key = 60;
    int velocity = 100;      // 1..127
    std::int64_t length = 0; // ticks
};

struct Region
{
    bool audio = false;
    std::string name;
    std::int64_t start = 0;  // ticks from the song's start (bar 1)
    std::int64_t length = 0; // ticks (MIDI: the region's own length; audio: 0, see lengthSamples)
    // MIDI
    std::vector<Note> notes;   // the notes that sound: inside [0, length)
    int hiddenNotes = 0;       // notes outside the region's bounds (trimmed away in GarageBand: they don't play there)
    int controllerEvents = 0;  // CC / pitch bend / pressure events (not imported)
    bool patternContent = false; // Smart Drums / Drummer / pattern regions: content this reader cannot play
    bool folder = false;         // a folder / take folder region (its inner regions are not read)
    // audio
    std::string file;                // the file's name, as the project references it
    std::int64_t trimSamples = 0;    // the region starts this many samples into the file
    std::int64_t lengthSamples = 0;  // and plays this many
    bool fades = false;              // the region has fades (not converted)
};

struct Track
{
    std::string name;
    bool audio = false;
    int volume = 90; // channel strip fader byte (0..127, 90 = 0 dB)
    int pan = 64;    // 0..127, 64 = centre
    bool volumeKnown = false;
    bool muted = false;
    std::vector<Region> regions;
    // the instrument, from the track's channel strip plugin records (GarageBand for iOS 2.3+; empty when not readable):
    std::string patch;             // the patch's name ("Grand Piano", "808 Flex": its .cst / .patch file without the extension)
    std::string category;          // the patch's category ("Drums", "Smart Drums", "Sampler", "Pianos and Keyboards"...)
    std::string unit;              // the instrument plugin ("Sampler", "Alchemy", "Drum Kits"...)
    std::string samplerInstrument; // the Sampler's instrument file ("Ring.exs"; Media/Sampler/Sampler Instruments holds the user's)
};

struct Project
{
    std::string title;
    double bpm = 120.0;
    int numerator = 4, denominator = 4;
    double sampleRate = 44100.0;
    int keyRoot = -1; // 0 = C … 11 = B; −1 unknown
    bool keyMinor = false;
    std::vector<std::pair<std::int64_t, double>> tempoChanges;         // after the first: (tick, bpm)
    std::vector<std::pair<std::int64_t, std::pair<int, int>>> meters; // signature changes after the first
    std::vector<std::pair<std::int64_t, std::string>> markers;
    std::vector<Track> tracks;
    std::string version; // the song's version bytes as hex ("af06")
};

// Whether these are GarageBand / Logic song bytes (the magic and a walkable record run).
bool isSongData (const std::uint8_t* data, std::size_t size) noexcept;

// The song bytes inside a "projectData" / "ProjectData" file (raw, XML or binary keyed archive). nullopt + `error`
// when there are none or the song is GarageBand 6–11's older layout.
std::optional<std::vector<std::uint8_t>> songFromProjectData (const std::vector<std::uint8_t>& file, std::string* error = nullptr);

// The song's tracks, regions, notes, tempo and signature. nullopt + `error` when the bytes do not walk.
std::optional<Project> parseSong (const std::vector<std::uint8_t>& song, std::string* error = nullptr);

// The summary plist (Output/metadata.plist or Alternatives/000/MetaData.plist, XML or binary): title, tempo,
// signature, key, sample rate. Fills only what the plist has; the song's own values stay otherwise.
void applyMetadata (const std::vector<std::uint8_t>& plist, Project& project);

// Minimal property-list reading (exposed for tests): the top-level dictionary's scalar values as text (strings,
// integers, reals, booleans "true"/"false") and every data object's bytes, in document order.
struct Plist
{
    std::vector<std::pair<std::string, std::string>> values;
    std::vector<std::vector<std::uint8_t>> data;
    std::string value (const std::string& key) const;
};
std::optional<Plist> readPlist (const std::vector<std::uint8_t>& bytes);

// A Sampler instrument file (.exs, little-endian as GarageBand for iOS writes it): its zones, each with its sample's file name.
// Facts from public descriptions of the EXS24 format, checked on GarageBand for iOS files (issue #184's project): chunks of an
// 84-byte header (type @+3, u32 data size @+4, "TBOS" @+16, name @+20) and their data; a zone's data holds the root key @+1,
// the fine tune (cents, signed) @+2, the key range @+6..+7, the velocity range @+9..+10 and its sample's number @+0x5C;
// sample chunks (type 3) are numbered in file order and named by their file. nullopt when the bytes are not such a file.
struct ExsZone
{
    std::string sample;
    int rootKey = 60, lowKey = 0, highKey = 127, lowVelocity = 0, highVelocity = 127;
    int fineTune = 0; // cents
};
std::optional<std::vector<ExsZone>> readExs (const std::vector<std::uint8_t>& bytes);

// "C", "F#", "Bb" → 0..11 (−1 when not a note name)
int keyRootOf (const std::string& name) noexcept;
} // namespace lynx::band
