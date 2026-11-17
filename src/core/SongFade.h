#pragma once

// The Simple layout's "Fade out the end" (round 190ke, owner decision D5 of coverage190jw; GarageBand's Mix › Fade Out on
// the Mac and Song settings › Fade Out on the iPad). GarageBand (Apple's GarageBand User Guide for Mac, "Work with the
// master track"): the command adds four volume points on the master track, 10, 7 and 4 seconds before the end of the
// last region and at that end, and they stay editable as the master track's volume automation; the iPad switch fades the
// last ten seconds to silence and follows the song's end.
//
// LYNX: one automation clip on the Master's volume (AutomationTargetKind::mixerVolume, index 0), named kFadeName, on a
// Playlist track of its own named "Master" under the last used track, over the last `seconds` of the song. It is an
// ordinary automation clip: the Pattern layout edits it as FL edits any clip, the Simple layout shows it on its row, the
// engine plays it and every export renders it. Its first point is the Master's level, so the song plays as before up to
// the fade (before the clip, the Master keeps the first point's value). Applied again (another length, or the song grew
// or shrank), the same clip and automation are reused. No JUCE (tests/SimpleGb190keTests.inc).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "core/Song.h"

namespace lynx::songfade
{
inline constexpr const char* kFadeName = "Song fade-out"; // the automation's name (it identifies the fade)
inline constexpr const char* kTrackName = "Master";        // the Playlist track it sits on (GarageBand's master track)
inline constexpr double defaultSeconds = 10.0;              // GarageBand's
inline constexpr double minSeconds = 1.0, maxSeconds = 60.0;

inline bool isFade (const Automation& a) noexcept
{
    return a.name == kFadeName && a.target.kind == AutomationTargetKind::mixerVolume && a.target.index == 0 && ! a.isLive();
}

// The fade's automation (−1: none) and its clip in the current arrangement (−1: none)
inline int findAutomation (const Song& song) noexcept
{
    for (int i = 0; i < (int) song.automations.size(); ++i)
        if (isFade (song.automations[(std::size_t) i]))
            return i;
    return -1;
}

inline int findClip (const Song& song) noexcept
{
    const auto a = findAutomation (song);
    if (a < 0)
        return -1;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
        if (const auto& c = song.playlist[(std::size_t) i]; c.kind == ClipKind::automation && c.automation == a && c.gridSlot < 0)
            return i;
    return -1;
}

inline bool present (const Song& song) noexcept { return findClip (song) >= 0; }

// Ticks of `seconds` at the project's tempo
inline std::int64_t ticksFor (const Song& song, double seconds) noexcept
{
    const auto bpm = song.bpm > 0.0 ? song.bpm : 120.0;
    return (std::int64_t) std::llround (seconds * bpm / 60.0 * (double) ppq);
}

inline double secondsOf (const Song& song, std::int64_t ticks) noexcept
{
    const auto bpm = song.bpm > 0.0 ? song.bpm : 120.0;
    return (double) ticks / (double) ppq * 60.0 / bpm;
}

// The end of the music: the end of the last clip that is not the fade itself (GarageBand: the end of the last region).
// Clips of the clip grid are left out (they are not on the timeline). 0 = nothing placed.
inline std::int64_t musicEnd (const Song& song) noexcept
{
    const auto fade = findAutomation (song);
    std::int64_t end = 0;
    for (const auto& c : song.playlist)
        if (c.gridSlot < 0 && c.length > 0 && ! (c.kind == ClipKind::automation && c.automation == fade && fade >= 0))
            end = std::max (end, c.end());
    return end;
}

// The fade's length now, in seconds (0 = none)
inline double seconds (const Song& song) noexcept
{
    const auto c = findClip (song);
    return c < 0 ? 0.0 : secondsOf (song, song.playlist[(std::size_t) c].length);
}

// The track the fade's clip goes on: the one it is on already, else the first track under every track in use (one with
// clips, or a Simple / audio / instrument track, a name, lanes — an armed Guitar track without a take yet is in use).
inline int trackFor (const Song& song) noexcept
{
    if (const auto c = findClip (song); c >= 0)
        return song.playlist[(std::size_t) c].track;
    int highest = -1;
    for (const auto& c : song.playlist)
        if (c.gridSlot < 0)
            highest = std::max (highest, c.track);
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        if (const auto& pt = song.playlistTracks[(std::size_t) t]; pt.mode != PlaylistTrackMode::unassigned || ! pt.name.empty() || ! pt.lanes.empty())
            highest = std::max (highest, t);
    return highest + 1;
}

// GarageBand's four points (−10 s, −7 s, −4 s, the end), scaled to the fade's length, from the Master's level `level`
// (its fader's 0…1 travel) to silence. The middle points follow the fader's travel down by 25 % and 55 %.
inline std::vector<AutomationPoint> points (std::int64_t length, float level)
{
    level = std::clamp (level, 0.0f, 1.0f);
    const auto at = [length] (double fraction) { return (std::int64_t) std::llround ((double) length * fraction); };
    std::vector<AutomationPoint> p (4);
    p[0].tick = 0;
    p[0].value = level;
    p[1].tick = at (0.3);
    p[1].value = level * 0.75f;
    p[2].tick = at (0.6);
    p[2].value = level * 0.45f;
    p[3].tick = std::max<std::int64_t> (1, length);
    p[3].value = 0.0f;
    return p;
}

struct Result
{
    bool ok = false;
    int automation = -1, clip = -1, track = -1;
    std::int64_t start = 0, length = 0;
};

// Adds (or moves and resizes) the fade over the last `seconds` of the music. Nothing happens for a song with nothing
// placed. A fade longer than the music starts at its beginning. Returns where it went.
inline Result apply (Song& song, double seconds)
{
    Result r;
    seconds = std::clamp (seconds, minSeconds, maxSeconds);
    const auto end = musicEnd (song);
    if (end <= 0)
        return r;
    const auto length = std::clamp<std::int64_t> (ticksFor (song, seconds), 1, end);
    const auto level = song.mixer.empty() ? 0.8f : song.mixer[0].volume;
    auto a = findAutomation (song);
    if (a < 0)
    {
        Automation fade;
        fade.name = kFadeName;
        fade.target = { AutomationTargetKind::mixerVolume, 0, 0, 0 };
        song.automations.push_back (std::move (fade));
        a = (int) song.automations.size() - 1;
    }
    song.automations[(std::size_t) a].points = points (length, level);
    const auto track = trackFor (song);
    auto c = findClip (song);
    if (c < 0)
    {
        Clip clip;
        clip.kind = ClipKind::automation;
        clip.automation = a;
        clip.pattern = 0;
        song.playlist.push_back (clip);
        c = (int) song.playlist.size() - 1;
    }
    auto& clip = song.playlist[(std::size_t) c];
    clip.track = track;
    clip.start = end - length;
    clip.length = length;
    clip.offset = 0;
    clip.muted = false;
    if ((int) song.playlistTracks.size() <= track)
        song.playlistTracks.resize ((std::size_t) track + 1);
    if (song.playlistTracks[(std::size_t) track].name.empty())
        song.playlistTracks[(std::size_t) track].name = kTrackName;
    r.ok = true;
    r.automation = a;
    r.clip = c;
    r.track = track;
    r.start = clip.start;
    r.length = length;
    return r;
}

// Takes the fade away: its clips and its automation (Song::removeAutomation). The track keeps its name only if
// something else is on it. Returns whether anything was there.
inline bool remove (Song& song)
{
    const auto a = findAutomation (song);
    if (a < 0)
        return false;
    int track = -1;
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::automation && c.automation == a)
            track = c.track;
    song.removeAutomation (a); // (its clips in every arrangement and lane go with it; later indices shift down)
    if (track >= 0 && track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].name == kTrackName
        && std::none_of (song.playlist.begin(), song.playlist.end(), [track] (const Clip& c) { return c.track == track; }))
        song.playlistTracks[(std::size_t) track].name.clear();
    return true;
}
} // namespace lynx::songfade
