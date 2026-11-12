#pragma once

// The Clip page's Sample panel for an audio clip of the clip grid (round 190dr; Live's Clip View: the Audio panel's
// Warp switch, the Clip panel's Start / End "Set" buttons, the sample editor's title "loop16.wav 48.0 kHz · 16-Bit ·
// 2 Ch" [SHOT warp190cg/shots/live-clipview-audio-panel.png]; docs/analise/REA_rodada190dr.md §2). Every field is the
// Song's own: Warp is the channel's tempo following (SamplerSettings::stretchBeats > 0), Start / End are the clip's
// offset and end. Header only, no JUCE.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "core/Song.h"
#include "core/WarpEdit.h"

namespace lynx::clipsample
{
// Warp on: the sample follows the song's tempo (it lasts stretchBeats beats). Off: it plays at its own speed.
inline bool isWarped (const SamplerSettings& s) noexcept { return s.stretchBeats > 0.0f; }

// The beats the file lasts when warped: its markers' span when it has them, else its length at `bpm` (no audible
// change at the current tempo; from then on it follows the tempo).
inline double warpedBeats (const SamplerSettings& s, double fileSeconds, double bpm)
{
    if (s.warp.size() >= 2)
    {
        auto copy = s;
        warpedit::syncLength (copy, fileSeconds);
        return copy.stretchBeats;
    }
    return fileSeconds * std::max (1.0, bpm) / 60.0;
}

// The Warp switch on channel `channel`'s sample (a file of `fileSeconds`). The warp markers stay while it is off, as
// Live keeps them. LYNX's warp belongs to the channel, so every clip of the channel (Playlist, other arrangements, the
// grid) keeps playing the same stretch of the file: its offset and length scale with the content's new length.
// Returns false when nothing changed.
inline bool setWarp (Song& song, int channel, bool on, double fileSeconds)
{
    if (channel < 0 || channel >= (int) song.channels.size() || ! (fileSeconds > 0.0))
        return false;
    auto& sampler = song.channels[(std::size_t) channel].sampler;
    if (isWarped (sampler) == on)
        return false;
    const auto unwarped = fileSeconds * std::max (1.0, song.bpm) / 60.0; // beats at its own speed
    const auto warped = on ? warpedBeats (sampler, fileSeconds, song.bpm) : (double) sampler.stretchBeats;
    if (! (warped > 0.0))
        return false;
    const auto scale = on ? warped / unwarped : unwarped / warped; // content ticks after / before
    sampler.stretchBeats = on ? (float) warped : 0.0f;
    if (std::abs (scale - 1.0) > 1.0e-9)
        song.forEachClipList ([channel, scale] (std::vector<Clip>& clips) {
            for (auto& c : clips)
                if (c.kind == ClipKind::audio && c.channel == channel)
                {
                    const auto offset = ((double) c.offset + (double) c.offsetFraction) * scale;
                    c.offset = (std::int64_t) std::floor (offset);
                    c.offsetFraction = (float) (offset - (double) c.offset);
                    c.length = std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) c.length * scale));
                    c.fadeIn = std::min (c.fadeIn, c.length);
                    c.fadeOut = std::min (c.fadeOut, c.length - c.fadeIn);
                }
        });
    return true;
}

// --- Start / End "Set" -------------------------------------------------------------------------------------------------
// Live's Set buttons put the marker where the playing clip is now (they are dim while the clip does not play). A grid
// clip's place in its content: its offset plus the played part of its length (`progress` 0..1, the launcher's view).
inline std::int64_t playPosition (const Clip& c, float progress) noexcept
{
    return c.offset + (std::int64_t) std::llround ((double) std::clamp (progress, 0.0f, 1.0f) * (double) c.length);
}
// Start here: the clip starts at `tick` of its content and still ends where it did. False when `tick` is not before the end.
inline bool setStartAt (Clip& c, std::int64_t tick) noexcept
{
    const auto end = c.offset + c.length;
    if (tick < 0 || tick >= end)
        return false;
    c.offset = tick;
    c.offsetFraction = 0.0f;
    c.length = end - tick;
    c.fadeIn = std::min (c.fadeIn, c.length);
    c.fadeOut = std::min (c.fadeOut, c.length - c.fadeIn);
    return true;
}
// End here: the clip ends at `tick` of its content. False when `tick` is not after the start.
inline bool setEndAt (Clip& c, std::int64_t tick) noexcept
{
    if (tick <= c.offset)
        return false;
    c.length = tick - c.offset;
    c.fadeIn = std::min (c.fadeIn, c.length);
    c.fadeOut = std::min (c.fadeOut, c.length - c.fadeIn);
    return true;
}

// --- the sample's title -----------------------------------------------------------------------------------------------
// "loop16.wav  48.0 kHz · 16-Bit · 2 Ch" as Live's sample editor titles it (bits 0 = unknown: left out).
inline std::string infoText (const std::string& fileName, double sampleRate, int bits, int channels)
{
    std::string text = fileName;
    if (sampleRate > 0.0)
    {
        char rate[32];
        std::snprintf (rate, sizeof (rate), "%.1f kHz", sampleRate / 1000.0);
        text += "  ";
        text += rate;
    }
    if (bits > 0)
        text += " \xc2\xb7 " + std::to_string (bits) + "-Bit";
    if (channels > 0)
        text += " \xc2\xb7 " + std::to_string (channels) + " Ch";
    return text;
}
} // namespace lynx::clipsample
