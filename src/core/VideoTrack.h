#pragma once

// Video tracks (round 174d, docs/analise/REA_rodada174d.md §2): LYNX's own (FL has none). Lanes above the Playlist's
// clip tracks, each holding video clips placed in song time. A video clip is locked to real time (seconds from the song's
// start): tempo changes never change its speed or where it starts (the post-production rule; FL's Video Player keeps
// FL's own clock, core/VideoPlayer.h). The project has a frame rate and a start timecode (song time 0 =
// 01:00:00:00 in film / TV work). No JUCE here (tests/Video174dTests.inc).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/VideoTime.h"

namespace lynx
{
struct Song;

struct VideoClip
{
    std::string path;      // the video file (UTF-8)
    double start = 0.0;    // song seconds where the clip starts
    double offset = 0.0;   // seconds into the file at the clip's start (trimmed off its head)
    double length = 0.0;   // seconds the clip shows
    double duration = 0.0; // the file's own length (seconds; 0 = unknown): limits trimming
    std::uint64_t uid = 0; // stable id (thumbnail cache, selection)
    bool operator== (const VideoClip&) const = default;

    double end() const noexcept { return start + length; }
};

struct VideoTrack
{
    std::string name;          // empty = "Video N"
    bool muted = false;        // a muted track shows nothing
    int height = 0;            // the lane's height in px (0 = the default)
    std::uint32_t colour = 0;  // 0xFFRRGGBB, 0 = the default
    std::vector<VideoClip> clips; // sorted by start, not overlapping
    bool operator== (const VideoTrack&) const = default;
};

struct VideoSettings
{
    vtime::Rate rate { 25, 1, false }; // the project's frame rate (timecode ruler, nudges, ADR cues)
    double startTimecode = 0.0;        // the timecode of song time 0, in seconds (3600 = 01:00:00:00)
    bool rateFromVideo = true;         // the first imported video sets `rate` (until the user picks one)
    bool showTimecodeRuler = true;     // the Playlist's timecode row while there is a video track
    bool burnIn = false;               // the video window draws the timecode over the picture
    bool operator== (const VideoSettings&) const = default;
};

namespace videotrack
{
// The frame a track shows at song time `songSeconds`: the clip and the file's time (nullopt: none / muted)
struct Showing
{
    int clip = -1;
    double fileSeconds = 0.0;
};
std::optional<Showing> showing (const VideoTrack& track, double songSeconds) noexcept;

// The timecode (seconds on the timecode clock) of a song time, and back
inline double timecodeOf (const VideoSettings& v, double songSeconds) noexcept { return v.startTimecode + songSeconds; }
inline double songOf (const VideoSettings& v, double timecodeSeconds) noexcept { return timecodeSeconds - v.startTimecode; }

// Song ticks ↔ song seconds with the song's tempo automation (the inverse of Song::secondsForTicks)
double ticksForSeconds (const Song& song, double seconds);
// The same both ways, for many lookups (drawing): built once per song, tabulated in the same 1/16 steps as
// Song::secondsForTicks up to `maxSeconds` (linear past it at the last tempo); constant tempo: no table
class TempoMap
{
public:
    TempoMap() = default;
    TempoMap (const Song& song, double maxSeconds);
    double ticks (double seconds) const noexcept;
    double seconds (double ticks) const noexcept;

private:
    double secondsPerTick = 60.0 / (130.0 * 960.0);
    std::vector<double> stepStarts; // seconds at tick k · step (empty: constant tempo)
};

// Clip edits (the track's clips stay sorted and do not overlap: a moved / added clip cuts what it covers)
// Adds a clip of the whole file (`duration` seconds) at `start`; returns its index
int addClip (VideoTrack& track, const std::string& path, double start, double duration, std::uint64_t uid);
// Moves clip `index` to start at `start` (≥ 0); returns its new index
int moveClip (VideoTrack& track, int index, double start);
// Trims the head to `newStart` (the file's offset follows) / the tail to `newEnd`; limited by the file's length
void trimStart (VideoClip& clip, double newStart, double minLength) noexcept;
void trimEnd (VideoClip& clip, double newEnd, double minLength) noexcept;
// Splits clip `index` at song time `at` (inside it): the right part is inserted after it with `uid`
bool splitClip (VideoTrack& track, int index, double at, std::uint64_t uid);
// Snaps a song time to the project's frame grid (on the timecode clock)
double snapToFrame (const VideoSettings& v, double songSeconds) noexcept;
// The end of the last video clip (seconds), 0 without any
double videoEnd (const std::vector<VideoTrack>& tracks) noexcept;
} // namespace videotrack
} // namespace lynx
