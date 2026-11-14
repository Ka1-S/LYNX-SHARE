#pragma once

// Warp from the Playlist (round 190gi, slice B6 of /mnt/e/lynx-work/pain190gc/REPORT.md; complaint #7: "no audio warping
// in the Playlist"). LYNX's own: FL 2026 warps in Newtime / its audio editor only. The audio clip menu's LYNX extras
// "Auto-warp" and "Quantize audio" run the audio editor's own commands (ui/AudioEditor: autoWarp, quantizeWarp) on the
// clicked clip; on a clip that has warp markers a double-click on its flag strip adds one, and Alt + drag near a marker's
// line anywhere on the clip moves it (ui/PlaylistComponent; Alt + double-click stays the clip's Properties, round 153).
// Each is one undoable edit; a clip whose channel other clips also play gets its own channel first (warpedit).

#include "core/AutoWarp.h"
#include "core/Instruments.h"
#include "core/TimeStretch.h"
#include "core/WarpEdit.h"
#include "core/WarpTools.h"

#include <string>
#include <vector>

namespace lynx::clipwarp
{
inline double fileSeconds (const SampleData& sample) noexcept { return sample.sampleRate > 0.0 ? (double) sample.frames() / sample.sampleRate : 0.0; }

// Auto-warp (the audio editor's: markers from the tempo — the file name's, else its length — and, in a long recording,
// on every bar). Returns the markers placed (0: nothing to lock to, the song unchanged).
inline int autoWarp (Song& song, int clip, const SampleData& sample, const std::string& fileName)
{
    const auto seconds = fileSeconds (sample);
    if (! (seconds > 0.0))
        return 0;
    autowarp::Options o;
    o.songBpm = song.bpm;
    o.beatsPerBar = std::max (1, song.beatsPerBar);
    o.knownBpm = autowarp::bpmInName (fileName);
    const auto result = autowarp::analyse (sample, o);
    if (result.markers.size() < 2)
        return 0;
    const auto channel = warpedit::channelForClipEdit (song, clip);
    if (channel < 0 || channel >= (int) song.channels.size())
        return 0;
    auto& sampler = song.channels[(std::size_t) channel].sampler;
    warpedit::ensureMarkers (sampler, seconds, song.bpm);
    sampler.warp = result.markers;
    sampler.keepPitch = true;
    warpedit::syncLength (sampler, seconds);
    return (int) result.markers.size();
}

// The attacks of the file (seconds), as the audio editor finds them for Quantize audio
inline std::vector<double> attacks (const SampleData& sample)
{
    std::vector<double> out;
    if (sample.left.empty() || ! (sample.sampleRate > 0.0))
        return out;
    std::vector<float> mono (sample.left);
    if (! sample.right.empty())
        for (std::size_t i = 0; i < mono.size() && i < sample.right.size(); ++i)
            mono[i] = 0.5f * (mono[i] + sample.right[i]);
    for (const auto a : stretchdetail::detectAttacks (mono.data(), (long) mono.size(), sample.sampleRate, 6.0))
        out.push_back ((double) a / sample.sampleRate);
    return out;
}

// Quantize audio over the part of the file the clip plays, to the audio editor's default grid (1/16, 100 %). Returns the
// markers placed.
inline int quantize (Song& song, int clip, const SampleData& sample, double gridBeats = 0.25, double amount = 1.0)
{
    const auto seconds = fileSeconds (sample);
    if (! (seconds > 0.0))
        return 0;
    const auto points = attacks (sample);
    const auto channel = warpedit::channelForClipEdit (song, clip);
    if (channel < 0 || channel >= (int) song.channels.size())
        return 0;
    auto& sampler = song.channels[(std::size_t) channel].sampler;
    warpedit::ensureMarkers (sampler, seconds, song.bpm);
    const auto placed = warptools::quantize (sampler, points, 0.0, seconds, gridBeats, amount);
    warpedit::syncLength (sampler, seconds);
    return placed;
}

// A double-click on the warp flag strip of a Playlist clip: a warp marker where the clip plays file second `seconds` (on the beat it plays
// now: nothing moves until the marker is dragged). Returns its index, −1 when none could be added.
inline int addMarker (Song& song, int clip, double seconds, double fileSeconds)
{
    if (! (fileSeconds > 0.0) || seconds <= 0.0 || seconds >= fileSeconds)
        return -1;
    const auto channel = warpedit::channelForClipEdit (song, clip);
    if (channel < 0 || channel >= (int) song.channels.size())
        return -1;
    auto& sampler = song.channels[(std::size_t) channel].sampler;
    // (the markers as the clip plays them, as the flag drag edits them)
    if (auto played = warpedit::playedMarkers (sampler, fileSeconds); played.size() >= 2)
        sampler.warp = std::move (played);
    warpedit::ensureMarkers (sampler, fileSeconds, song.bpm);
    const auto added = warpedit::addMarker (sampler, seconds);
    warpedit::syncLength (sampler, fileSeconds);
    return added;
}
} // namespace lynx::clipwarp
