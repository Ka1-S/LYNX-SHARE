#pragma once

// The Export window's choices (FL's "WAV rendering" window, interpreted; docs/analise/REA_rodada84.md) and the parts of
// exporting that are plain logic: what song copy is rendered (song / pattern / selection, repeated N times, with a tail),
// what happens to the tail (cut / leave / wrap into the start), normalising, mono, which inserts or channels become
// stems, and how the options are remembered. No JUCE here: the engine side is engine/Exporter.*.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
class DelayLine;
}

namespace lynx::exportopts
{
enum class Format : int
{
    wav,
    mp3,
    ogg,
    flac
};

enum class TailMode : int
{
    cut,   // stop exactly at the end (loops)
    leave, // keep the tail after the end until it is silent (songs)
    wrap   // render the tail and mix it into the start, keeping the exact length (seamless loops)
};

enum class Range : int
{
    song,      // the Playlist arrangement
    pattern,   // the current pattern
    selection  // the Playlist's time selection (its loop region)
};

enum class Stems : int
{
    none,
    mixerTracks, // one file per insert that gets sound (FL's "Split mixer tracks")
    channels,    // one file per Channel Rack channel that plays something (each heard through its mixer path)
    channelsDry  // round 177d (pain point 35, LYNX's own): the same, but each channel's insert goes straight to the
                 // Master: no sends, no group/bus inserts after it (the "instruments only" stems)
};

enum class DitherMode : int
{
    none,
    tpdf,   // ±1 LSB triangular noise
    shaped  // the same with 2nd-order noise shaping (pushes the noise to the highs)
};

enum class Channels : int
{
    stereo,
    monoMerged,
    monoLeft,
    monoRight
};

struct Options
{
    Format format = Format::wav;
    int wavBits = 24;          // 16, 24 (integer) or 32 (float)
    int flacBits = 24;         // 16 or 24
    int flacLevel = 5;         // compression 0 (fastest) .. 8 (smallest)
    int mp3Kbps = 320;         // constant bitrate
    int oggKbps = 256;         // nominal bitrate
    double sampleRate = 44100.0;
    DitherMode dither = DitherMode::shaped;
    TailMode tail = TailMode::leave;
    double maxTailSeconds = 12.0; // leave / wrap: the tail stops earlier when the sound ends
    Range range = Range::song;
    int loops = 1;                 // how many times the range plays (1..16)
    Stems stems = Stems::none;
    bool stemsIncludeMaster = false; // with stems: also the full mix
    bool normalize = false;          // peak to 0 dBFS
    bool masterEffects = true;       // the Master insert's effects
    // FL's "Enable insert effects" (on by default; WAVRenderForm InputFXBox, REA round 167j §4): off = the inserts'
    // effect slots (not the Master's) are bypassed for the render
    bool insertEffects = true;
    bool trimPdc = true;             // drop the silence the delay compensation adds at the start
    // FL's "HQ for all plugins" (REA round 134): engines with a separate render quality use it and HQ switches are
    // forced on; off = the export uses the same (draft) quality as playback, so it matches what was heard
    bool hqAllPlugins = true;
    // "Plugin warm-up" (round 155): hosted plugins get silent, discarded audio before the render until they have
    // settled (fresh instances: bridged plugins / background loading apply their state late), so the file does not
    // start with half-loaded effects
    bool pluginWarmUp = true;
    bool showWhenDone = false;       // FL's "Show file(s) when complete": the file manager opens on the result
    // FL's "Disable maximum polyphony" (on by default; REA round 139, FLEngine FUN_00d1a…: every channel's Max
    // polyphony is set to 0 for the render): the channels' polyphony limits are lifted in the exported song
    bool disableMaxPolyphony = true;
    // more formats written from the same render (FL's WAV / MP3 / OGG / FLAC boxes can be ticked together): a mask of
    // `1 << Format`, besides `format` (whose settings window shows); 0 = only `format`
    std::uint32_t moreFormats = 0;
    Channels channels = Channels::stereo;
    // FL's "Extra data" (WAVRenderForm AcidBox / LoopMarkersBox / PlaylistMarkersBox, REA_rodada167l §3), WAV files
    // only: "Tempo information" (an acid chunk: tempo, beats, meter; off by default), "Loop markers" (a smpl chunk
    // with the song as one loop; on), "Playlist markers" (the Playlist's markers as cue points with their names; on)
    bool saveTempo = false;
    bool loopMarkers = true;
    bool playlistMarkers = true;
    // File › Export › "Master..." (Ctrl+Alt+M; round 168e: LYNX's own mastering where FL sends the render to FL Cloud):
    // the render is brought to `masterLufs` integrated under a −1 dBTP ceiling (core/Mastering.h). `master` is set by
    // that item only (not remembered); the target is remembered.
    bool master = false;
    double masterLufs = -14.0;
    // Round 190ap (LYNX's own, off by default): "One file per marker section" — the song is cut at its Playlist
    // markers and each section becomes a file named after its marker ("<name> - 02 Verse"); with stems, each section
    // gets its stems. Song range only (markerSections).
    bool sections = false;

    bool operator== (const Options&) const = default;
};

// Choices offered by the window.
const std::vector<int>& mp3Bitrates();  // kbps
const std::vector<int>& oggBitrates();  // kbps (JUCE's Vorbis encoder's settings)
const std::vector<double>& sampleRates();

// ".wav", ".mp3", ".ogg", ".flac"
const char* extension (Format format);

// Every format the export writes, `format` first, then the others of `moreFormats` in WAV, MP3, OGG, FLAC order.
std::vector<Format> formatsOf (const Options& o);

// FL's "LastExportedFormats" (REA round 139, FLEngine FUN_011dd840 / the render window's close): a mask of the ticked
// format boxes, bit 3 WAV, 4 MP3, 5 OGG, 6 FLAC, 7 MIDI, 9 M4A; 8 (WAV) when nothing valid was saved (< 8 or > 0x3FF).
// The File › Export entries open the window with one format (WAV 3, MP3 4, OGG 5, FLAC 6, MIDI 7, M4A 9) or, with
// "Last exported format(s)", the boxes of that mask.
constexpr std::uint32_t flFormatBit (Format f) noexcept
{
    return f == Format::wav ? 0x8u : f == Format::mp3 ? 0x10u : f == Format::ogg ? 0x20u : 0x40u;
}
std::uint32_t flLastExportedMask (const Options& o) noexcept; // the mask of the formats `o` writes
Options withFlLastExported (Options o, std::uint32_t mask);    // `o` with the formats of an FL mask (invalid → WAV)
Options withSingleFormat (Options o, Format f);                // what an FL File › Export › "<format> file..." entry opens

// Bits per sample written to the file (32 = float WAV; MP3/OGG encode floats and report 16).
int fileBits (const Options& o) noexcept;

// Whether dither is applied: integer output of 16 or 24 bits with dither on (MP3/OGG/float never).
bool usesDither (const Options& o) noexcept;

// MP3 is limited to MPEG-1 rates: higher project rates are encoded at 48 kHz (44.1 kHz family → 44.1 kHz).
double mp3SampleRate (double rate) noexcept;

// What the render window shows before the export (FL: "Length: 144 bars  Total time: 3'57''  Disk space: 38.76 MB"):
// the music's length in bars and the file size the options give for `seconds` (one file; the tail not counted;
// FLAC and the lossy formats are estimates).
double lengthInBars (double seconds, double bpm, int beatsPerBar) noexcept;
std::int64_t estimatedBytes (const Options& o, double seconds) noexcept;

// The notes FL's File › Export › "MIDI file..." writes (REA round 139): in song mode, each pattern clip's part of its
// pattern placed at the clip (notes from `offset` to `offset + length`, cut at the clip's end; muted clips / notes /
// playlist tracks left out; with Song::patternClipTakeover, none from where a later clip of the same pattern starts —
// issue #158, as playback), sorted; in pattern mode or with no pattern clip, the current pattern's notes.
std::vector<Note> midiExportNotes (const Song& song);

// Remembered between sessions as "key=value" lines; unknown or bad values fall back to the defaults (clamped).
std::string toText (const Options& o);
Options fromText (const std::string& text);
Options clamped (Options o);

// --- the song that is rendered ------------------------------------------------------------------------------------

// Copy of `song` ready to render (always song mode, no loop region), with the range repeated `loops` times and, when
// `tailTicks` > 0, an empty tail after it. `musicTicks` receives the length of the music (all the loops, no tail);
// `loopTicks` one pass. Selection = song.loopStart..loopEnd (falls back to the whole song when there is none).
// Clips crossing the selection's edges are cut there (audio and pattern offsets follow).
Song makeRenderSong (const Song& song, const Options& o, std::int64_t tailTicks, std::int64_t& musicTicks, std::int64_t& loopTicks);

// The selection exists (loop region set and inside the song).
bool hasSelection (const Song& song) noexcept;

// Inserts that get sound: a channel goes to them or another insert sends to them (Master excluded). Round 190gh (B7):
// also the inserts fed only by extra outputs — a hosted multi-output instrument's Connections (as the engine plays
// them, "Process inactive inputs and outputs" included), FPC pads with their own mixer offset, a generator Patcher's
// "To LYNX" outputs, and an effect slot's sends (Fruity Send, Transmitter, a hosted effect's Connections).
std::vector<int> stemTracks (const Song& song);
// FL's Mixer › Disk recording › "Render armed / selected tracks to wave file(s)" (REA round 165f-mixer): the inserts
// of `only` (each once, in mixer order, the Master excluded) whether or not they get sound; empty = stemTracks (song).
std::vector<int> stemTracks (const Song& song, const std::vector<int>& only);

// Channels that play something in the range: notes in a pattern placed in the Playlist (or the current pattern in
// pattern range), or an audio clip.
std::vector<int> stemChannels (const Song& song, Range range);

// Copy of the song where only `channel` plays (every other channel muted); the mixer stays as it is. `dry` (Stems::
// channelsDry): the channel's insert loses its sends and goes to the Master at its route level, so what the buses
// and the send effects add is left out (a channel on the Master itself is unchanged).
Song makeChannelStemSong (const Song& song, int channel, bool dry = false);

// "<base> - <number> <name><ext>" with characters that are illegal in file names replaced.
std::string stemFileName (const std::string& base, int number, const std::string& name, const char* ext);

// --- "Split mixer tracks" in one render (round 190gj, P9) -----------------------------------------------------------
// FL renders the split mixer tracks in one pass (TRENDERFORM, REA_rodada84 §1). LYNX's stem of an insert is the render
// of makeBounceSong (song, insert): that insert alone (soloed with its sources), straight to a neutral Master. The same
// samples come out of one render of the whole song when that render's insert outputs are tapped: the stem render's
// Master adds up its own channels and the routes that carry sound into it — the insert, and whatever feeds it that
// also goes to the Master — each delayed by that render's delay compensation, and its neutral Master passes the sum
// as it is. planStemTap says how (or why not: then the stem is rendered alone, as before).
struct StemTap
{
    bool ok = false;
    std::string why; // when not ok: what the stem's own render does differently
    struct Input
    {
        int track = 0;      // an insert whose output is tapped
        float level = 1.0f; // its route level into the Master (the stem render's)
        int delay = 0;      // the stem render's delay on that route (samples)
    };
    std::vector<Input> inputs; // in the order the stem render's Master adds them (after its own channels)
    int latency = 0;           // the stem render's delay compensation total (its export trims that much)
};

// The stem of insert `track` from a render of `shared` (the song that render plays: solos cleared), whose instruments
// and effects report these latencies (the layout of computePdc; an engine's after its plugin warm-up).
StemTap planStemTap (const Song& shared, int track, const std::vector<int>& channelLatency,
                     const std::vector<std::vector<int>>& slotLatency, double sampleRate);

// What a stem render's Master adds up, made from the taps of the shared render, one block after another (the blocks in
// order, the same sizes as that render's): its own channels' sum (`masterL/R`, the shared render's Master input before
// its routes; nullptr = silence) then each input of the plan through its delay line, exactly as the stem render's Master
// (RackRenderer::pullInputs) does.
class StemMixer
{
public:
    explicit StemMixer (const StemTap& plan);
    StemMixer (StemMixer&&) noexcept;
    ~StemMixer();
    // `tapLeft / tapRight (input index)`: that input's tap for this block (n samples)
    void mix (const float* masterL, const float* masterR, const float* const* tapLeft, const float* const* tapRight, float* outL,
              float* outR, int n) noexcept;

private:
    StemTap plan;
    std::vector<std::unique_ptr<DelayLine>> lines; // per input (nullptr = no delay)
};

// --- one file per marker section (round 190ap) ----------------------------------------------------------------------
// A stretch of the song between two Playlist markers.
struct Section
{
    std::string name;            // the marker's name ("Start" before the first marker; "Marker 3" for a nameless one)
    std::int64_t start = 0, end = 0; // ticks
    bool operator== (const Section&) const = default;
};

// The song's sections: the plain Playlist markers (not the actions, not the time signatures: the same ones that become
// cue points) cut the arrangement from its start to its end. What lies before the first marker is the section "Start";
// markers at the same place count once; an empty stretch and markers at or after the song's end give no section.
// No marker: empty (there is nothing to split by).
std::vector<Section> markerSections (const Song& song);

// The song with `section` as its time selection, to be rendered with Range::selection (so the clips that cross its
// edges are cut there, as for any selection).
Song makeSectionSong (const Song& song, const Section& section);
// `o` as each section is rendered with: the selection range, once (a section is not looped).
Options sectionOptions (Options o);
// Whether an export with `o` is split by sections: the option is on, the range is the song and it has markers.
bool exportsSections (const Song& song, const Options& o);

// "<base> - <NN> <name>" (no extension: the stems of a section add theirs): the number is the section's place, two
// digits (three from 100 sections on) so the files sort in song order; illegal file name characters replaced.
std::string sectionBaseName (const std::string& base, int number, int count, const std::string& name);

// How many files an export writes with `o`: one per format, times the stems (and the full mix), times the sections.
int fileCount (const Song& song, const Options& o, const std::vector<int>& onlyTracks = {});

// --- export presets (round 190ap) -------------------------------------------------------------------------------------
// The window's choices under a name ("Stems for mixing", "MP3 demo"), kept between sessions.
struct Preset
{
    std::string name;
    Options options;
    bool operator== (const Preset&) const = default;
};
// A preset's name as it is kept: one line, no brackets at the ends, trimmed, at most 60 characters ("" = not a name).
std::string cleanPresetName (const std::string& name);
// The presets' file: "[name]" then the options' "key=value" lines, one block per preset, in the list's order.
std::string presetsToText (const std::vector<Preset>& presets);
std::vector<Preset> presetsFromText (const std::string& text);
// `presets` with `preset` added, or replacing the one of the same name (compared without case); the list stays sorted
// by name.
std::vector<Preset> withPreset (std::vector<Preset> presets, Preset preset);
std::vector<Preset> withoutPreset (std::vector<Preset> presets, const std::string& name);

// "Same as last time": what the last export was made with and where it went (the full path of the file chosen then,
// extension included). `file` empty = nothing was exported yet.
struct LastExport
{
    Options options;
    std::string file;
    bool operator== (const LastExport&) const = default;
};
std::string lastExportToText (const LastExport& last);
LastExport lastExportFromText (const std::string& text);
// The file the same export writes for a project named `projectName` now: the last folder, the project's name, the
// last format's extension ("" when there is no last export or no name).
std::string sameAsLastFile (const LastExport& last, const std::string& projectName);

// --- post-processing of the rendered audio --------------------------------------------------------------------------

// Tail handling on the whole render (`frames` per channel): cut drops what follows `loopFrames`; wrap adds it back
// from the start (wrapping more than once if the tail is longer than the loop) and then cuts; leave does nothing.
void applyTail (std::vector<float>& left, std::vector<float>& right, std::int64_t loopFrames, TailMode mode);

// Gain that brings `peak` to full scale (1 when silent).
float normalizeGain (float peak) noexcept;

// Mono choices: the result goes to `left` (right is left untouched for stereo).
void toChannels (float* left, const float* right, int n, Channels mode) noexcept;
inline int channelCount (Channels mode) noexcept { return mode == Channels::stereo ? 2 : 1; }

// "m:ss" (or "h:mm:ss") for the total time line.
std::string formatDuration (double seconds);

// --- FL's extra data in exported WAV files (REA_rodada167l §3) -----------------------------------------------------

struct WavExtras
{
    bool loop = false;              // smpl: one loop over the music
    std::int64_t musicFrames = 0;   // the music's length (no tail): the loop is [0, musicFrames − 1]
    bool acid = false;              // acid: tempo information
    float tempo = 120.0f;
    int numerator = 4, denominator = 4;
    struct Cue
    {
        std::int64_t frame = 0;
        std::string name;           // empty: FL's "Marker <index>"
    };
    std::vector<Cue> markers;       // cue + LIST adtl labl + tlst
};

// What `o` asks for, for the export of `song` (the range the export plays: the selection's markers move to its start)
// at `sampleRate`. Plain Playlist markers only (the actions and the time signatures are not cue points).
WavExtras wavExtras (const Song& song, const Options& o, double sampleRate);

// The chunks FL writes after "data", in its order: smpl, acid, cue , LIST adtl (labl), tlst. Empty when nothing is on.
// `dataFrames` = the frames in the file (the acid's beat count). Byte layout from FL 2026's renders (§3).
std::vector<std::uint8_t> wavExtraChunks (const WavExtras& x, double sampleRate, std::int64_t dataFrames);

// Appends `chunks` to the RIFF/WAVE file at `path` and fixes the RIFF size (a "data" chunk of odd size gets its pad
// byte first). False if the file is not a RIFF/WAVE file or cannot be written.
bool appendWavChunks (const std::string& path, const std::vector<std::uint8_t>& chunks);
// The frames in the "data" chunk of the WAV file at `path` (fmt's block align), −1 if it is not one.
std::int64_t wavDataFrames (const std::string& path);
// What the export does after writing a WAV file: the chunks of `x` appended (nothing to do = true).
bool appendWavExtras (const std::string& path, const WavExtras& x, double sampleRate);
} // namespace lynx::exportopts
