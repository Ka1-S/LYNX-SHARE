#pragma once

// The warp mode's own settings in the Clip View (round 190fm; Live 12.4's Clip View Audio panel shows, under the warp
// mode, Beats' Preserve chooser, its Transient Loop Mode and Transient Envelope, Tones' and Texture's Grain Size; facts in
// /mnt/e/lynx-work/realive190fm/LIVE-FACTS.md §1, docs/analise/ALS_FORMAT.md §21). The values are the channel's
// SamplerSettings::warpModes, rendered offline by core/TimeStretch. Header only, no JUCE.
//
// Owner decision O16 (taken as recommended): the warp mode stays on the channel, as in LYNX's model, and an edit made
// from one grid clip's Clip View first gives that clip its own copy of the channel when other clips play it too (like
// "Make unique"), so only that clip changes — what Live's per-clip mode does.

#include <algorithm>
#include <array>
#include <string>

#include "core/Song.h"

namespace lynx::warpmodes
{
// Beats › Preserve, index = WarpModeSettings::beatsPreserve (0…5 the note grid, 6 Transients). [Live: 7 values with
// Transients = 6, the default; the order of the grid values is inferred — live check]
inline const std::array<const char*, 7>& preserveNames()
{
    static const std::array<const char*, 7> names { "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/32", "Transients" };
    return names;
}
// Beats › Transient Loop Mode, index = WarpModeSettings::beatsLoop [Live's labels]
inline const std::array<const char*, 3>& loopNames()
{
    static const std::array<const char*, 3> names { "Loop Off", "Loop Forward", "Loop Back-and-Forth" };
    return names;
}

// The .lynx form of a channel's warpModes (engine/ProjectFile.cpp "warpModes"): nine numbers in the struct's order; a
// channel with the defaults writes nothing. fromList clamps to the ranges LYNX keeps; fewer than nine numbers: defaults.
inline std::array<double, 9> toList (const WarpModeSettings& w)
{
    return { (double) w.beatsPreserve, (double) w.beatsLoop, (double) w.beatsEnvelope, (double) w.tonesGrain, (double) w.textureGrain,
             (double) w.textureFlux, (double) w.proFormants, (double) w.proEnvelope, w.complexPro ? 1.0 : 0.0 };
}
template <typename List>
WarpModeSettings fromList (const List& m, std::size_t size)
{
    WarpModeSettings w;
    if (size < 9)
        return w;
    const auto at = [&] (std::size_t k) { return (double) m[k]; };
    w.beatsPreserve = (std::uint8_t) std::clamp ((int) at (0), 0, WarpModeSettings::preserveTransients);
    w.beatsLoop = (std::uint8_t) std::clamp ((int) at (1), 0, 2);
    w.beatsEnvelope = (float) std::clamp (at (2), 0.0, 100.0);
    w.tonesGrain = (float) std::clamp (at (3), WarpModeSettings::tonesGrainMin, WarpModeSettings::tonesGrainMax);
    w.textureGrain = (float) std::clamp (at (4), WarpModeSettings::textureGrainMin, WarpModeSettings::textureGrainMax);
    w.textureFlux = (float) std::clamp (at (5), 0.0, 100.0);
    w.proFormants = (float) std::clamp (at (6), 0.0, 100.0);
    w.proEnvelope = (float) std::clamp (at (7), 8.0, 256.0);
    w.complexPro = at (8) != 0.0;
    return w;
}

// How many clips (the Playlist's and the clip grid's) play audio channel `channel`.
inline int clipsOfChannel (const Song& song, int channel)
{
    int n = 0;
    for (const auto& c : song.playlist)
        n += c.kind == ClipKind::audio && c.channel == channel ? 1 : 0;
    if (song.clipGrid)
        for (const auto& c : song.clipGrid->clips)
            n += c.kind == ClipKind::audio && c.channel == channel ? 1 : 0;
    return n;
}

// The audio clip of the clip grid in (track, scene), or nullptr.
inline Clip* gridAudioClip (Song& song, int track, int scene)
{
    if (! song.clipGrid)
        return nullptr;
    auto& grid = *song.clipGrid;
    for (auto& c : grid.clips)
        if (c.track == track && c.kind == ClipKind::audio && c.gridSlot >= 0 && c.gridSlot < (int) grid.slots.size()
            && grid.slots[(std::size_t) c.gridSlot].scene == scene)
            return &c;
    return nullptr;
}

// O16: the channel an edit of the grid clip in (track, scene) changes. When other clips play its channel too, the clip
// moves to a copy of it named "<name> #2" (the first free number, as Make unique) and that copy is returned. -1: no
// audio clip there.
inline int ownChannelForGridClip (Song& song, int track, int scene)
{
    auto* clip = gridAudioClip (song, track, scene);
    if (clip == nullptr || clip->channel < 0 || clip->channel >= (int) song.channels.size())
        return -1;
    const auto source = clip->channel;
    if (clipsOfChannel (song, source) <= 1)
        return source;
    auto copy = song.channels[(std::size_t) source];
    const auto base = copy.name;
    for (int n = 2;; ++n)
    {
        const auto candidate = base + " #" + std::to_string (n);
        if (std::none_of (song.channels.begin(), song.channels.end(), [&] (const ChannelDef& c) { return c.name == candidate; }))
        {
            copy.name = candidate;
            break;
        }
    }
    song.channels.push_back (std::move (copy));
    clip = gridAudioClip (song, track, scene); // (the push may not move clips, but stay safe)
    clip->channel = (int) song.channels.size() - 1;
    return clip->channel;
}

// The warp mode of channel `channel`: `method` = a StretchMethod, or −1 for Re-Pitch (speed and pitch together).
inline bool setMode (Song& song, int channel, int method)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return false;
    auto& s = song.channels[(std::size_t) channel].sampler;
    s.keepPitch = method >= 0;
    if (method >= 0)
        s.stretchMethod = (StretchMethod) std::clamp (method, 0, stretchMethodCount - 1);
    s.warpModes.complexPro = false; // (a mode chosen in LYNX is LYNX's own Complex, not an imported Complex Pro)
    return true;
}

// The settings shown for each mode, clamped to the ranges LYNX keeps.
inline bool setPreserve (Song& song, int channel, int index)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return false;
    song.channels[(std::size_t) channel].sampler.warpModes.beatsPreserve = (std::uint8_t) std::clamp (index, 0, WarpModeSettings::preserveTransients);
    return true;
}
inline bool setLoopMode (Song& song, int channel, int index)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return false;
    song.channels[(std::size_t) channel].sampler.warpModes.beatsLoop = (std::uint8_t) std::clamp (index, 0, 2);
    return true;
}
inline bool setEnvelope (Song& song, int channel, double value)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return false;
    song.channels[(std::size_t) channel].sampler.warpModes.beatsEnvelope = (float) std::clamp (value, 0.0, 100.0);
    return true;
}
// Tones' or Texture's Grain Size (the one of the channel's current mode), in Live's ranges (Tones 12…100, Texture 2…263)
inline bool setGrain (Song& song, int channel, double value)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return false;
    auto& s = song.channels[(std::size_t) channel].sampler;
    if (s.stretchMethod == StretchMethod::tones)
        s.warpModes.tonesGrain = (float) std::clamp (value, WarpModeSettings::tonesGrainMin, WarpModeSettings::tonesGrainMax);
    else if (s.stretchMethod == StretchMethod::texture)
        s.warpModes.textureGrain = (float) std::clamp (value, WarpModeSettings::textureGrainMin, WarpModeSettings::textureGrainMax);
    else
        return false;
    return true;
}
} // namespace lynx::warpmodes
