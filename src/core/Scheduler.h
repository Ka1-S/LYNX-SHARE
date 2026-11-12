#pragma once

// Converts a pattern's notes into events with an exact sample offset within
// an audio block. Runs on the audio thread: no allocation, no locks.

#include <array>
#include <bitset>
#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Audio clip events carry the clip's own "key" (above the MIDI range): the end of a clip stops only the voice that clip
// started. Two clips of the same channel overlapping (FL's crossfaded vocal comps) both play, as in FL (REA_rodada107 §5).
constexpr int audioClipKeyBase = 128;
constexpr int audioClipKey (std::size_t playlistIndex) noexcept { return audioClipKeyBase + (int) (playlistIndex % 1000000u); }

struct NoteEvent
{
    enum class Type : std::uint8_t { on, off, slide, choke, control, voiceProp }; // slide: glides the sounding notes to `key`; choke: cut group
    // voiceProp (round 165c): a VFX's voice property changing while its note sounds (on a port: `port`, `colour`),
    // `key` the note's key, `prop` the vfx::Prop, `velocity` the value
    // control (live MIDI input only, round 162): `key` = CC number 0…127, controlPitchBend or controlPressure;
    // `velocity` = the value, 0..1 (the pitch wheel: -1..1, 0 = centre)
    // controlProgram (round 165c): a program change, `velocity` = program / 127
    static constexpr int controlPitchBend = 128, controlPressure = 129, controlProgram = 130;

    int sampleOffset = 0;   // 0 .. numSamples-1 within the block
    Type type = Type::on;
    int channel = 0;
    int key = 60;           // audio clips: audioClipKey (the clip's playlist index), so a clip's end stops only its voice
    float velocity = 0.0f;
    double startSeconds = 0.0; // audio clips: where the sample starts playing from
    float pan = 0.0f;          // -1..1, from the note
    float cents = 0.0f;        // note's fine pitch
    float slideSamples = 0.0f; // slide: glide duration
    bool porta = false;        // the note inverts the channel's porta
    bool echo = false;         // a repeat of the channel's echo delay (FL's generated voices: not in the Mono list, round 168l)
    // audio clips: fades (in samples) and the position within the clip when the note starts
    float clipElapsed = 0.0f, clipRemaining = 0.0f, fadeIn = 0.0f, fadeOut = 0.0f;
    // bit 0: the fade-in is a crossfade (FL's S curve, clipops::crossfadeCurve), bit 1: the fade-out (round 166e);
    // bit 2: the audio clip itself is reversed (FL 2026's clip reverse, round 167k)
    // bits 3 / 4: the fade-in / fade-out is an equal-power comp crossfade (Clip::fadeCurve, round 177c)
    // bits 5 / 6 (round 189i, FL's per-clip edge declicker): no declick at the clip's start / end (FL's declicker choice
    // Disabled, or Automatic where the clip joins its neighbour seamlessly); bits 7 / 8: the start / end is a manual fade
    // (FL's fade flags bits 0 / 1: no declick there, a sharp cut when its fade is 0)
    std::uint16_t fadeShape = 0;
    float fadeInTension = 0.0f, fadeOutTension = 0.0f; // FL's raw fade tensions (clipops::fadeTensionCurve)
    // audio clips: FL 2026's clip pitch in semitones (keys 7/8): a pitch shift that keeps the length (round 167k)
    float clipPitch = 0.0f;
    // audio clips: FL 2026's clip stretch (the audio plays that many times longer; round 167l)
    float clipStretch = 1.0f;
    // the note's Mod X / Y (0..1, 0.5 = none) and, on its "off", its release velocity (0..1) — REA round 151
    float modX = 0.5f, modY = 0.5f, release = 0.5f;
    // the note's level factor (a Layer's VOL and crossfade, round 164): the instrument scales the voice when it can,
    // else the velocity is scaled
    float gain = 1.0f;
    std::uint8_t prop = 0;  // voiceProp: the vfx::Prop
    // a live note from an MPE controller (issue #108, core/Mpe.h): its "on" brings its own `cents`, `modX` and `modY`
    // (the live queue ignores them otherwise), and a live voiceProp is that note's expression changing as it sounds
    bool expression = false;
    std::int8_t colour = 0; // the note's colour group 0…15 (FL's note colours: VFX Color Mapper, round 164b)
    // A VFX channel (round 164b): −1 = an ordinary note; −2 = one its processor takes (it goes nowhere else); N ≥ 0 =
    // what the processor sent on MIDI port N (only the routes on that port take it)
    std::int16_t port = -1;
    // a scheduled note's "on" (round 169d): the ticks from it to the note's "off" (0 = not known), for ScheduledNotes
    float lengthTicks = 0.0f;
};

// Fixed-capacity buffer. Events beyond capacity are dropped
// (and counted), never allocate.
class EventBuffer
{
public:
    static constexpr int capacity = 2048;

    void clear() noexcept { count = 0; dropped = 0; }
    void add (const NoteEvent& e) noexcept;
    void sortByOffset() noexcept;

    int size() const noexcept { return count; }
    void resize (int n) noexcept { count = n < 0 ? 0 : n < count ? n : count; } // (shrinks only)
    int droppedCount() const noexcept { return dropped; }
    const NoteEvent& operator[] (int i) const noexcept { return events[(std::size_t) i]; }
    NoteEvent& at (int i) noexcept { return events[(std::size_t) i]; }
    const NoteEvent* begin() const noexcept { return events.data(); }
    const NoteEvent* end() const noexcept { return events.data() + count; }

private:
    std::array<NoteEvent, capacity> events {};
    int count = 0;
    int dropped = 0;
};

// The song's notes that started and the end each one started with (round 169d, issue #32 follow-up 2). FL keeps a
// note's "off" once the note starts: a note deleted, moved or made longer while it sounds, or left behind when the
// pattern changes, still ends where it was going to (FL 2026 [SHOT] r169d f13–f16: C5 deleted while playing, still
// sounding at +150 ms and +550 ms, gone after its end; f17–f20: a new pattern added while it sounds, the same).
// LYNX's schedulers emit each "off" from the notes as they are now, so such a note got none and hung until Stop.
// Audio thread: process() takes a block's scheduled events (sorted). An "off" the scheduler emits ends the sounding
// note of its channel and key that ends there; a note whose "off" has not come two samples after its end (the
// schedulers' rounding) gets one there; any other "off" (a note edited in behind the play head, or made shorter or
// longer while it sounds) is dropped, so it cannot end another voice of that key. Audio clips (keys ≥ 128) pass.
class ScheduledNotes
{
public:
    static constexpr int capacity = 4096;
    // the schedulers round a note's "on" and "off" to samples: an "off" within this of a note's end is that note's,
    // and a note whose "off" has not come this long after its end gets its own
    static constexpr double matchSamples = 3.0, dueSamples = 2.0;

    void clear() noexcept
    {
        count = 0;
        overflow = false;
        clock = 0.0;
    }
    void process (EventBuffer& events, int numSamples, double tps) noexcept;
    // The sounding notes of (channel, key) end `offsetTicks` into the next process() block at the latest. False when
    // none sounds. (A launched clip cut off, core/ClipLauncher.h.)
    bool shorten (int channel, int key, double offsetTicks) noexcept
    {
        auto found = false;
        for (int k = 0; k < count; ++k)
            if (auto& entry = entries[(std::size_t) k]; entry.channel == channel && entry.key == key)
            {
                entry.end = std::min (entry.end, clock + offsetTicks);
                found = true;
            }
        return found;
    }
    // channels inserted, removed or moved: from[c] = the old index of channel c (−1 = a new channel)
    void moveChannels (const std::vector<int>& from) noexcept;
    int sounding() const noexcept { return count; }

private:
    struct Entry
    {
        int channel = 0, key = 0;
        double end = 0.0; // on `clock`; +inf = its "off" comes from the scheduler only
    };
    std::array<Entry, capacity> entries {};
    int count = 0;
    bool overflow = false; // a note could not be kept: every "off" passes until the next clear()
    double clock = 0.0;    // ticks played since clear()
};

inline double ticksPerSample (double bpm, double sampleRate) noexcept
{
    return bpm * ppq / (60.0 * sampleRate);
}

// Generates the pattern's events (playing in a loop) in the interval [startTick, startTick + numSamples * tps).
// A note starting at t generates "on" at t and "off" at t + length.
void schedulePatternLoop (const Pattern& pattern,
                          std::int64_t patternLength,
                          double startTick,
                          int numSamples,
                          double ticksPerSampleValue,
                          EventBuffer& out,
                          float swing = 0.0f,
                          const std::vector<ChannelDef>* channels = nullptr,
                          bool chase = false,
                          double linearOrigin = -1.0,
                          std::uint32_t chanceSeed = 0) noexcept;
// `chanceSeed` (round 187c): Song::chanceSeed, for the notes with Chance or a Velocity Range (core/NoteChance.h).
// `linearOrigin` >= 0 (round 170e5, Loop record off in Pattern mode): the pattern plays once from that transport tick,
// every note (also those past `patternLength`) and no repeat; before it nothing plays.
// `chase` (both schedulers): playback starts at startTick and FL's Settings › Audio › "Play truncated notes on
// transport" is on (FLEngine FUN_011e7470, REA_rodada167j §2): the notes that started before startTick and still
// sound there start on the block's first sample, with what is left of their length.
// The range the transport loops over: in Song mode the Playlist's loop region, or else the whole song (FL plays the
// song in a loop: at its end the position goes back to the start, issue #4). Pattern mode: none (end <= start); the
// pattern loop is the position modulo the pattern length.
struct PlaybackLoop
{
    double start = 0.0, end = 0.0;
    bool active() const noexcept { return end > start; }
};
// (songStart > 0: Performance mode, the song loops back to its Start marker)
PlaybackLoop playbackLoop (bool songMode, double loopStart, double loopEnd, double songLength, double songStart = 0.0) noexcept;

// The position hosted plugins see (their play head): within the pattern in Pattern mode, within the song in Song
// mode, so plugins synced to the host follow the loop like in FL instead of counting on forever.
double hostPositionTick (const Song& song, double tick, std::int64_t songLength) noexcept; // songLength = song.songLength()

// Index built once per snapshot so scheduleSong doesn't scan every note on every block:
// song length and the longest note of each pattern. Requires notes sorted by start.
struct ScheduleIndex
{
    std::int64_t songLength = 0;
    std::vector<std::int64_t> maxNoteLength; // per pattern
    std::vector<std::int64_t> patternLength; // per pattern: effectiveLength, the period of a long clip's repeats
    std::int64_t maxShift = 0;               // the largest channel time shift (FL's Misc › Shift), ticks
    // per Playlist clip: the next clip of the same pattern by start (the same start: later in the list), -1 = none or
    // not a pattern clip (Song::patternClipTakeover, issue #158)
    std::vector<int> nextOfPattern;
};

// FL's pattern clip takeover (Song::patternClipTakeover, issue #158): the song tick from which Playlist clip `clipIndex`
// starts no more notes — the start of the first later clip of its pattern that plays (on any track; the same start:
// the clip later in the list wins). Muted clips, clips on muted tracks and Performance-mode zone clips take nothing
// over. Max = never (the option off, not a pattern clip, no later clip). With an index built from this song, O(1) in
// the usual case; without one, a scan of the Playlist.
std::int64_t patternClipStopsAt (const Song& song, std::size_t clipIndex, const ScheduleIndex* index = nullptr) noexcept;

// When a note plays (ticks within its pattern) and for how long: the swing (with the channel's swing mix), FL's
// Misc › Time › Shift and Gate (REA round 146b). c = the note's channel (nullptr: only the swing).
struct PlayedTiming
{
    std::int64_t start = 0, length = 1;
};
PlayedTiming playedTiming (const Note& note, float swing, const ChannelDef* c) noexcept;

// The largest Misc › Shift of the channels, in ticks.
std::int64_t maxChannelShift (const std::vector<ChannelDef>& channels) noexcept;

ScheduleIndex makeScheduleIndex (const Song& song);

// Generates the Playlist's events (playing the whole song in a loop). Each pattern clip
// plays the pattern's notes that fall in [offset, offset + length), the pattern repeating along a clip longer than
// it when stretchedClipsLoop() (LYNX extra, issue #25; off = FL: once); notes are cut at the end of the clip. With Song::playTruncatedNotes, a note that started before the offset plays from the clip's start. Audio clips trigger the channel's sample at the start and stop at the end.
// `resume` = playback started or jumped to startTick: audio clips that were already
// in progress at that point start in the middle, on the first sample of the block.
void scheduleSong (const Song& song,
                   double startTick,
                   int numSamples,
                   double ticksPerSampleValue,
                   EventBuffer& out,
                   bool resume = false,
                   const ScheduleIndex* index = nullptr,
                   bool wrap = true,
                   bool chase = false) noexcept;
// The Playlist while clips of the grid hold tracks (Clip-launch workflow, core/ClipLauncher.h). As scheduleSong, except: on a
// `taken` track nothing starts (the ends of what was sounding still come), and on a `givenBack` track what is in
// progress at startTick starts there, in the middle.
constexpr int scheduleMaskTracks = 500;
using TrackMask = std::bitset<scheduleMaskTracks>;
void scheduleSongAround (const Song& song, const TrackMask& taken, const TrackMask& givenBack, double startTick, int numSamples,
                         double ticksPerSampleValue, EventBuffer& out, bool resume, const ScheduleIndex* index, bool wrap,
                         bool chase) noexcept;

// One clip played once where it is (no song loop), its events in the block (not sorted): Performance mode's passes.
void scheduleSingleClip (const Song& song, const Clip& clip, std::size_t clipIndex, double startTick, int numSamples, double tps,
                         EventBuffer& out, const ScheduleIndex* index = nullptr) noexcept;
// (wrap = false: the song plays once and is silent past its end — audio recording without a loop region, round 163)
} // namespace lynx
