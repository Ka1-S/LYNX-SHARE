#pragma once

// Adding a track in the Simple workflow (round 184a, slice S2; design: /mnt/e/lynx-work/simple184a/DESIGN.md §2.1).
// In the Simple workflow a track is one Playlist track, one mixer insert and (for an instrument) one channel, made and
// named together, so the user never meets FL's channel / pattern / insert triangle. Everything is built from FL's own
// track modes (core/PlaylistOps.h `trackmodes`), so the project stays an ordinary FL-style project that every layout,
// the .flp export and the .als export read as they are:
//  - Voice / Audio, Guitar, Bass, Import (audio): an FL audio track linked to a free insert, with the insert's input set;
//  - Instrument, Sampler: the caller's channel on an FL instrument track, playing through a free insert of its own;
//  - Drums: the kit's channels in one Channel Rack group, all playing through ONE free insert, with the starter pattern
//    on an audio track linked to that insert (decision D3: one insert per track; the drum track is FL's "audio track"
//    so that it keeps its insert even while its pattern is empty — pattern clips play on such a track as on any other).
// No JUCE. The record arm is the engine's state, not the song's: the UI arms the returned insert.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::simpletracks
{
enum class Kind : std::uint8_t
{
    voice,      // a microphone or any sound (audio track, mono input)
    instrument, // a sound played on keys (instrument track)
    drums,      // a step-grid beat (kit channels in one group, one insert)
    sampler,    // one sample played over the keyboard (instrument track with a sampler channel)
    guitar,     // an instrument cable (audio track, mono input)
    bass,       // an instrument cable (audio track, mono input)
    audio       // imported audio or a dropped loop (audio track, no input)
};

// "Voice", "Instrument", "Drums", "Sampler", "Guitar", "Bass", "Audio" (English, for tr())
const char* defaultName (Kind kind) noexcept;
// The colour a new track of this kind takes (0xFFRRGGBB), the same for its track, insert and channels.
std::uint32_t defaultColour (Kind kind) noexcept;
bool isAudioKind (Kind kind) noexcept; // voice, guitar, bass, audio

// The first Playlist track nothing uses yet: no clip on it in any clip list (every arrangement and the clip grid), no
// track mode, no name. -1 when all of FL's 500 tracks are used.
int nextEmptyTrack (const Song& song);

struct Request
{
    Kind kind = Kind::instrument;
    std::string name; // empty: defaultName (kind), numbered when taken ("Voice 2")
    // Instrument and Sampler: the channel to put on the track, built by the caller (plugin, preset or sample). Its
    // name and colour are replaced by the track's; its mixer track by the track's insert.
    std::optional<ChannelDef> channel;
    // Drums: the kit's channels (each with its sound), in row order, and the starter beat whose notes' `channel` is the
    // index into `kit` (nullopt: an empty one-bar pattern). Its name becomes the track's.
    // Instrument / Sampler (slice S3: an imported MIDI track): notes for the channel (their `channel` is ignored),
    // placed at the song's start as a pattern named after the track; nullopt: nothing placed.
    std::vector<ChannelDef> kit;
    std::optional<Pattern> pattern;
    double bpm = 0.0; // Drums: > 0 sets the project's tempo too (a starter beat's suggested tempo), in the same edit
    // Audio (slice S3: an imported file or a dropped loop): the file, played from the song's start by an audio clip
    // channel routed to the track's insert, `audioLength` ticks long (> 0). Empty: no clip.
    std::string audioFile;
    std::int64_t audioLength = 0;
    // Voice, Guitar, Bass: the insert's audio input (the device's input channel, 0-based; -1 none) and whether the
    // next channel is taken too. Audio (imports) has no input.
    int input = 0;
    bool inputStereo = false;
};

enum class Error : std::uint8_t
{
    none,
    noFreeTrack,   // all 500 Playlist tracks are used
    mixerFull,     // FL: "Can't create new audio or instrument track: no available mixer tracks"
    noChannel,     // Instrument / Sampler without a channel, Drums without a kit
    notAnInstrument // the channel is an audio clip or controller channel (FL: "Only generators can be used as instrument tracks.")
};

struct Result
{
    Error error = Error::none;
    int track = -1;            // the Playlist track
    int insert = -1;           // its mixer insert
    std::vector<int> channels; // the channels made (Instrument/Sampler: one; Drums: the kit, in row order; Audio: the clip's)
    int pattern = -1;          // Drums, or an Instrument with notes: the pattern placed at the song's start
    int group = -1;            // Drums: the Channel Rack group (index into Song::channelGroups)

    bool ok() const noexcept { return error == Error::none; }
};

// Adds a track of `request.kind` to the first empty Playlist track. On an error the song is left as it was.
Result addTrack (Song& song, const Request& request);

// Renames a Simple track everywhere: the Playlist track, its insert and its channel (Drums: its group and its pattern
// when they carry the old name). An empty name is ignored. Returns the name given (made unique among the tracks).
std::string rename (Song& song, int track, const std::string& name);

// What a track is, read back from the song (its track mode and what plays through its insert): the families the
// Simple workflow's dock needs. Voice, Guitar, Bass and imported audio all read back as `audio`. nullopt for a track
// that is none of these (an FL track without a mode, or a mode whose insert or channel is gone).
std::optional<Kind> kindOf (const Song& song, int track);

// The insert a Simple track plays through, or -1 (the same rule as kindOf).
int insertOf (const Song& song, int track);

// Whether a project has nothing in it yet (the Simple workflow then opens on the New track chooser, slice S3): no clip
// in any clip list, no note in any pattern, no Playlist track with a mode or a name. Channels without notes (a
// template's empty drum rows) and inserts don't count.
bool isEmptySong (const Song& song);
} // namespace lynx::simpletracks
