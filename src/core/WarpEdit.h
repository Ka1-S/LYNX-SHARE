#pragma once

// Editing a channel's warp markers (round 175a, warp markers M3): the core of the Playlist's marker drags and the audio
// editor's Warp markers mode. A channel's markers map its file's seconds to content beats (SamplerSettings::warp). They
// always include the file's start (beat 0, never moved) and its end. The channel's stretchBeats is kept equal to the
// end's beat, so the file still fills that many beats. Editing one clip's markers first gives that clip its own copy of
// the channel when other clips share it (clipops::makeAudioUnique), so the other clips keep their timing.

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/ClipOps.h"
#include "core/TimeStretch.h"
#include "core/Song.h"

namespace lynx::warpedit
{
constexpr double minBeatGap = 1.0 / 64.0; // markers stay at least a 64th of a beat apart
constexpr double minSecondGap = 0.001;

// The content beat at file time `seconds` (straight beyond the end markers)
inline double beatAt (const std::vector<WarpPoint>& w, double seconds)
{
    if (w.size() < 2)
        return seconds;
    std::size_t i = 1;
    while (i + 1 < w.size() && seconds > w[i].seconds)
        ++i;
    const auto& a = w[i - 1];
    const auto& b = w[i];
    return a.beats + (seconds - a.seconds) * (b.beats - a.beats) / std::max (1.0e-12, b.seconds - a.seconds);
}

// Markers to edit: the existing ones, or the start and the end of a file of `fileSeconds` at the channel's tempo (its
// stretchBeats, else `bpm`). Returns false for no file length.
inline bool ensureMarkers (SamplerSettings& s, double fileSeconds, double bpm)
{
    if (! (fileSeconds > 0.0))
        return false;
    if (s.warp.size() >= 2)
        return true;
    const auto beats = s.stretchBeats > 0.0f ? (double) s.stretchBeats : fileSeconds * std::max (1.0, bpm) / 60.0;
    s.warp = { { 0.0, 0.0 }, { fileSeconds, beats } };
    s.stretchBeats = (float) beats;
    s.keepPitch = true;
    return true;
}

// stretchBeats follows the markers: the beat at the file's end (`fileSeconds`; 0 = the last marker's beat)
inline void syncLength (SamplerSettings& s, double fileSeconds = 0.0)
{
    if (s.warp.size() >= 2)
        s.stretchBeats = (float) std::max (minBeatGap, fileSeconds > 0.0 ? beatAt (s.warp, fileSeconds) - beatAt (s.warp, 0.0) : s.warp.back().beats);
}

// A marker at file time `seconds`, on the beat it plays on now. Returns its index, or -1 (too close to another).
inline int addMarker (SamplerSettings& s, double seconds)
{
    if (s.warp.size() < 2 || seconds <= s.warp.front().seconds + minSecondGap || seconds >= s.warp.back().seconds - minSecondGap)
        return -1;
    const auto at = std::lower_bound (s.warp.begin(), s.warp.end(), seconds, [] (const WarpPoint& p, double t) { return p.seconds < t; });
    if (std::abs (at->seconds - seconds) < minSecondGap || std::abs ((at - 1)->seconds - seconds) < minSecondGap)
        return -1;
    const auto index = (int) (at - s.warp.begin());
    s.warp.insert (at, WarpPoint { seconds, beatAt (s.warp, seconds) });
    return index;
}

// Marker `index` moved to beat `beats` (the audio around it stretches); kept between its neighbours. The start
// marker never moves. Returns the beat it got.
inline double moveMarker (SamplerSettings& s, int index, double beats, double fileSeconds = 0.0)
{
    if (index <= 0 || index >= (int) s.warp.size())
        return index == 0 && ! s.warp.empty() ? s.warp.front().beats : 0.0;
    const auto lo = s.warp[(std::size_t) index - 1].beats + minBeatGap;
    const auto hi = index + 1 < (int) s.warp.size() ? s.warp[(std::size_t) index + 1].beats - minBeatGap : 1.0e6;
    auto& m = s.warp[(std::size_t) index];
    m.beats = std::clamp (beats, lo, std::max (lo, hi));
    syncLength (s, fileSeconds);
    return m.beats;
}

// Marker `index` removed (not the start or the end). Returns false when it can't be.
inline bool removeMarker (SamplerSettings& s, int index)
{
    if (index <= 0 || index + 1 >= (int) s.warp.size())
        return false;
    s.warp.erase (s.warp.begin() + index);
    return true;
}

// All markers between the start and the end removed: one tempo again.
inline void clearMarkers (SamplerSettings& s)
{
    if (s.warp.size() > 2)
        s.warp = { s.warp.front(), s.warp.back() };
}

// The markers as the channel plays them: content beats scaled so the file fills stretchBeats from beat 0 (the same
// normalisation as the render, core/TimeStretch warpStretchMap). Empty unless they bend the tempo.
inline std::vector<WarpPoint> playedMarkers (const SamplerSettings& s, double fileSeconds)
{
    if (! s.keepPitch || s.stretchBeats <= 0.0f || s.warp.size() < 3 || ! (fileSeconds > 0.0)
        || warpStretchMap (s.warp, fileSeconds * 1000.0, 1000.0, (double) s.stretchBeats * 1000.0).empty())
        return {};
    const auto b0 = beatAt (s.warp, 0.0), b1 = beatAt (s.warp, fileSeconds);
    if (! (b1 > b0))
        return {};
    const auto scale = (double) s.stretchBeats / (b1 - b0);
    std::vector<WarpPoint> out;
    for (const auto& p : s.warp)
        out.push_back ({ p.seconds, (p.beats - b0) * scale });
    return out;
}

// Round 190gi: the markers to show and edit on a Playlist clip — playedMarkers, or, for a warped channel whose markers
// keep one tempo (playedMarkers is then empty), the same normalisation of its own markers (the end marker, and markers
// added on the beat they play on, show as flags and can be dragged). Empty when the channel is not warped.
inline std::vector<WarpPoint> editableMarkers (const SamplerSettings& s, double fileSeconds)
{
    if (auto played = playedMarkers (s, fileSeconds); ! played.empty())
        return played;
    if (! s.keepPitch || s.stretchBeats <= 0.0f || s.warp.size() < 2 || ! (fileSeconds > 0.0))
        return {};
    const auto b0 = beatAt (s.warp, 0.0), b1 = beatAt (s.warp, fileSeconds);
    if (! (b1 > b0))
        return {};
    const auto scale = (double) s.stretchBeats / (b1 - b0);
    std::vector<WarpPoint> out;
    for (const auto& p : s.warp)
        out.push_back ({ p.seconds, (p.beats - b0) * scale });
    return out;
}

// The file time played at content beat `beats` (the inverse of beatAt, straight beyond the end markers)
inline double secondsAt (const std::vector<WarpPoint>& w, double beats)
{
    if (w.size() < 2)
        return beats;
    std::size_t i = 1;
    while (i + 1 < w.size() && beats > w[i].beats)
        ++i;
    const auto& a = w[i - 1];
    const auto& b = w[i];
    return a.seconds + (beats - a.beats) * (b.seconds - a.seconds) / std::max (1.0e-12, b.beats - a.beats);
}

// The Playlist tick where content beat `beats` of audio clip `c` plays (the clip's own stretch included)
inline double clipTickOfBeat (const Clip& c, double beats)
{
    return (double) c.start - ((double) c.offset + (double) c.offsetFraction) + beats * ppq * std::clamp (c.stretch, 0.01, 100.0);
}

// The channel to edit for audio clip `clip`: its own, or a copy of it when other clips play that channel too (the
// clip moves to the copy). -1 for no audio clip.
inline int channelForClipEdit (Song& song, int clip)
{
    if (clip < 0 || clip >= (int) song.playlist.size() || song.playlist[(std::size_t) clip].kind != ClipKind::audio)
        return -1;
    const auto channel = song.playlist[(std::size_t) clip].channel;
    const auto shared = std::count_if (song.playlist.begin(), song.playlist.end(), [&] (const Clip& c) {
        return c.kind == ClipKind::audio && c.channel == channel;
    });
    return shared > 1 ? clipops::makeAudioUnique (song, { clip }) : channel;
}
} // namespace lynx::warpedit
