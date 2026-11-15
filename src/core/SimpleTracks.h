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
    audio,      // imported audio or a dropped loop (audio track, no input)
    pads        // round 190fo: 16 pads, one sample each (the pads' Sampler channels in one group, one insert; core/SimplePads.h)
};

// "Voice", "Instrument", "Drums", "Sampler", "Guitar", "Bass", "Audio", "Sampler" (pads) (English, for tr())
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
    // Pads (round 190fo): the 16 pads' channels (simplepads::emptyKit) and the same pattern rules as Drums.
    // Instrument / Sampler (slice S3: an imported MIDI track): notes for the channel (their `channel` is ignored),
    // placed at the song's start as a pattern named after the track; nullopt: nothing placed.
    std::vector<ChannelDef> kit;
    std::optional<Pattern> pattern;
    double bpm = 0.0; // Drums: > 0 sets the project's tempo too (a starter beat's suggested tempo), in the same edit
    // Drums (round 190t): > 0 repeats the pattern's clip from the song's start until it covers this many bars, so Play
    // gives a loop long enough to build on (a one-bar starter: four clips). 0: one clip.
    int fillBars = 0;
    // Audio (slice S3: an imported file or a dropped loop): the file, played from the song's start by an audio clip
    // channel routed to the track's insert, `audioLength` ticks long (> 0). Empty: no clip.
    std::string audioFile;
    std::int64_t audioLength = 0;
    // Audio and Instrument with notes (slice S4: a file dropped on the timeline): where the clip starts, in ticks (>= 0)
    std::int64_t start = 0;
    // Audio (round 190p: a loop keeps time): > 0 makes the file last this many beats at any tempo (its channel's
    // stretch, pitch kept), and the clip is that long whatever `audioLength` says. 0: the file plays at its own speed.
    float audioBeats = 0.0f;
    // Instrument / Sampler without notes (round 190ds): an empty pattern of `emptyBars` bars placed on the track from
    // `start`, so the docked editor has a pattern to write into (0: none, as before)
    int emptyBars = 0;
    // Audio (round 190ds, core/KeyFollow.h): the file's key (root 0..11, −1 unknown), how it was found (1 name,
    // 2 detected) and whether the clip follows the project key (moved to it in the same edit)
    int audioKeyRoot = -1;
    bool audioKeyMinor = false;
    int audioKeySource = 0;
    bool followKey = false;
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

// The pattern a Simple Instrument / Sampler track's notes are written into (round 190ds, the dock's Editor tab): the
// pattern of the track's first pattern clip; with none, an empty `bars`-bar pattern named after the track is made and
// placed at the song's start (`*made` = true). -1 for a track that is not an Instrument / Sampler track.
int ensurePattern (Song& song, int track, int bars = 4, bool* made = nullptr);
// The pattern ensurePattern would give without making one (-1: none yet)
int patternOf (const Song& song, int track);

// Renames a Simple track everywhere: the Playlist track, its insert and its channel (Drums: its group and its pattern
// when they carry the old name). An empty name is ignored. Returns the name given (made unique among the tracks).
std::string rename (Song& song, int track, const std::string& name);

// What a track is, read back from the song (its track mode and what plays through its insert): the families the
// Simple workflow's dock needs. Voice, Guitar, Bass and imported audio all read back as `audio`. nullopt for a track
// that is none of these (an FL track without a mode, or a mode whose insert or channel is gone).
std::optional<Kind> kindOf (const Song& song, int track);

// Gives a Simple track, its insert and its channel (Drums: its kit) the colour `colour` (0xFFRRGGBB; 0 = the default).
void recolour (Song& song, int track, std::uint32_t colour);

// The insert a Simple track plays through, or -1 (the same rule as kindOf).
int insertOf (const Song& song, int track);

// A loop keeps time (round 190p). How many beats an imported or dropped audio file should last so that it follows the
// project's tempo, or 0 when it is not a loop made for a tempo (a voice take, a whole song, a one-shot): it then plays
// at its own speed, as before. `name` is the file's name without folder (the extension is ignored), `seconds` its
// length, `acidBeats` the WAV's ACID beat count (0 = none).
//  - an ACID beat count is taken as it is;
//  - else the tempo written in the name: "... 98 bpm ..." (autowarp::bpmInName), or a number from 60 to 200 standing
//    alone between separators ("DL_102_Dm_Stack", "Funky Guitar 98"). With it the file must come out as whole bars
//    (within a quarter of a beat: 1, 2, 3 ... bars of `meter`), or as whole beats when shorter than a bar; a name
//    that says "bpm" also keeps a loop with a tail (its exact beats).
//  - files longer than 70 seconds are never taken as loops.
float loopBeats (const std::string& name, double seconds, float acidBeats, Meter meter) noexcept;
// The tempo `loopBeats` reads in a name (0 = none) and whether the name says "bpm" (`*explicitBpm`, may be null).
double tempoInName (const std::string& name, bool* explicitBpm = nullptr);

// Whether a project has nothing in it yet (the Simple workflow then opens on the New track chooser, slice S3): no clip
// in any clip list, no note in any pattern, no Playlist track with a mode or a name. Channels without notes (a
// template's empty drum rows) and inserts don't count.
bool isEmptySong (const Song& song);

// Round 190fo: a folder of stems and MIDI files brought in at once (owner decision O6: BandLab's "Download Stems",
// one file per track named "NN_name", and LYNX's own export for other apps, "Stems/NN name.wav" + "MIDI/NN name.mid").
// `paths` are the folder's files (any order, sub-folders allowed, '/' separated); the plan keeps the audio and MIDI files
// in name order (the number first), names each track after its file without the number, skips the "(all tracks)" MIDI
// file and the "<name> - mixdown" audio file of LYNX's export, and marks a MIDI file whose track name an audio file also has (`muted`: its notes come in on a
// muted track, since the stem already plays them).
struct FolderItem
{
    std::string path;
    std::string name;
    bool midi = false;
    bool muted = false;
};
std::vector<FolderItem> folderPlan (std::vector<std::string> paths);
// Round 190fo: the Simple layout's shared reverb (the original's per-track "Reverb" knob is a send into one shared aux
// channel). It is the insert named "Reverb" whose first effect is Atrium; `ensureSharedReverb` makes it the first time (a
// free insert, Atrium fully wet with a medium room, to the Master). −1: none (or no free insert). A track's send to it is
// an ordinary FL send from the track's insert (linear gain 0…1 here: up to 0 dB); level 0 removes the send.
// Round 190fo ("Reset all tracks", the original's mobile app): every Simple track's insert back to unity level and centre
// pan (its effects, sends and channels stay). Returns how many inserts changed.
int resetLevels (Song& song);
int sharedReverb (const Song& song);
int ensureSharedReverb (Song& song);
float reverbSend (const Song& song, int insert);
bool setReverbSend (Song& song, int insert, float level);

// "03_Lead Vox" → "Lead Vox", "01 Drums" → "Drums", "12-Bass" → "Bass"; anything else stays (a bare number too).
std::string stemTrackName (const std::string& fileStem);
} // namespace lynx::simpletracks
