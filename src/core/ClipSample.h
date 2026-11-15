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
#include <cstdlib>
#include <string>
#include <vector>

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

// Position (Live's Loop Position): the clip's part of its content moves to start at `tick`, keeping its length. LYNX's
// loop starts where the clip starts, so this is the start marker moved together with the end. False for a negative tick.
inline bool setPositionAt (Clip& c, std::int64_t tick) noexcept
{
    if (tick < 0)
        return false;
    c.offset = tick;
    c.offsetFraction = 0.0f;
    return true;
}

// --- the Clip section's fields, as Live writes them (round 190dr-2) ----------------------------------------------------
// A place in the content: "bar. beat. sixteenth" from 1 ("1. 1. 1"); a length from 0 ("8. 0. 0") [SHOT
// live-clipview-audio-panel.png: Start 1. 1. 1, End 9. 1. 1, Length 8. 0. 0]. An unwarped audio clip shows seconds
// instead, "m:ss:mmm" (its content has no beats).
inline std::string positionText (std::int64_t ticks, Meter meter, bool asLength = false)
{
    // (round 190fg: the meter's bars and beats: in 6/8 the beats are eighths, as Live counts them)
    const auto barLength = meter.bar(), beatLength = meter.beat();
    const auto sixteenth = std::max<std::int64_t> (1, ppq / 4);
    ticks = std::max<std::int64_t> (0, ticks);
    const auto bar = ticks / barLength, beat = (ticks % barLength) / beatLength, six = (ticks % beatLength) / sixteenth;
    const auto base = asLength ? 0 : 1;
    char text[48];
    std::snprintf (text, sizeof (text), "%lld. %lld. %lld", (long long) (bar + base), (long long) (beat + base), (long long) (six + base));
    return text;
}
inline std::string secondsText (std::int64_t ticks, double bpm)
{
    const auto ms = (long long) std::llround ((double) std::max<std::int64_t> (0, ticks) / ppq * 60.0 / std::max (1.0, bpm) * 1000.0);
    char text[48];
    std::snprintf (text, sizeof (text), "%lld:%02lld:%03lld", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return text;
}
// The ticks a typed field means (-1: not readable). Bars-beats-sixteenths: "9. 1. 1", "9.1.1", "9 1 1" or "9" (missing
// parts are the first beat / sixteenth); seconds: "m:ss:mmm", "ss:mmm" or plain seconds "1.25".
inline std::int64_t parsePosition (const std::string& text, Meter meter, bool asLength = false)
{
    long long part[3] = { 0, 0, 0 };
    int count = 0;
    std::string digits;
    const auto flush = [&] {
        if (! digits.empty() && count < 3)
            part[count++] = std::atoll (digits.c_str());
        digits.clear();
    };
    for (const auto ch : text)
    {
        if (ch >= '0' && ch <= '9')
            digits += ch;
        else if (ch == '.' || ch == ' ' || ch == ':' || ch == ',')
            flush();
        else if (ch == '-')
            return -1;
        else
            return -1;
    }
    flush();
    if (count == 0)
        return -1;
    const auto base = asLength ? 0 : 1;
    for (int i = count; i < 3; ++i)
        part[i] = base;
    if (! asLength && (part[0] < 1 || part[1] < 1 || part[2] < 1))
        return -1;
    return (part[0] - base) * meter.bar() + (part[1] - base) * meter.beat() + (part[2] - base) * std::max<std::int64_t> (1, ppq / 4);
}
inline std::int64_t parseSeconds (const std::string& text, double bpm)
{
    double seconds = 0.0;
    if (text.find (':') == std::string::npos)
    {
        char* end = nullptr;
        seconds = std::strtod (text.c_str(), &end);
        if (end == text.c_str() || seconds < 0.0)
            return -1;
    }
    else
    {
        std::vector<double> parts;
        std::size_t from = 0;
        while (from <= text.size())
        {
            const auto to = text.find (':', from);
            const auto piece = text.substr (from, to == std::string::npos ? std::string::npos : to - from);
            char* end = nullptr;
            const auto v = std::strtod (piece.c_str(), &end);
            if (end == piece.c_str() || v < 0.0)
                return -1;
            parts.push_back (v);
            if (to == std::string::npos)
                break;
            from = to + 1;
        }
        if (parts.size() == 2)
            seconds = parts[0] + parts[1] / 1000.0;
        else if (parts.size() == 3)
            seconds = parts[0] * 60.0 + parts[1] + parts[2] / 1000.0;
        else
            return -1;
    }
    return (std::int64_t) std::llround (seconds * std::max (1.0, bpm) / 60.0 * ppq);
}

// --- the Audio section's BPM box and its ÷2 / ×2 (round 190dr-2; warp190cg's commands) -------------------------------
// The tempo the warped sample is read at: its beats per minute of file time (0 while it is not warped).
inline double fileBpm (const SamplerSettings& s, double fileSeconds) noexcept
{
    return isWarped (s) && fileSeconds > 0.0 ? (double) s.stretchBeats * 60.0 / fileSeconds : 0.0;
}
// The sample read `factor` times as fast in beats (÷2 = 0.5: the BPM halves and the clip plays it in half the beats):
// the warp markers' beats (or the channel's length in beats) and the grid clip (track, scene) scale together, as the
// audio editor's ÷2 / ×2 do. False when it is not warped.
inline bool scaleWarpedTempo (Song& song, int channel, int track, int scene, double fileSeconds, double factor)
{
    if (channel < 0 || channel >= (int) song.channels.size() || ! (factor > 0.0) || ! (fileSeconds > 0.0))
        return false;
    auto& sampler = song.channels[(std::size_t) channel].sampler;
    if (! isWarped (sampler))
        return false;
    if (sampler.warp.size() >= 2)
    {
        for (auto& m : sampler.warp)
            m.beats *= factor;
        warpedit::syncLength (sampler, fileSeconds);
    }
    else
        sampler.stretchBeats = (float) std::max (warpedit::minBeatGap, (double) sampler.stretchBeats * factor);
    if (song.clipGrid)
        for (auto& c : song.clipGrid->clips)
            if (c.track == track && c.kind == ClipKind::audio && c.channel == channel && c.gridSlot >= 0 && c.gridSlot < (int) song.clipGrid->slots.size()
                && song.clipGrid->slots[(std::size_t) c.gridSlot].scene == scene)
            {
                c.length = std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) c.length * factor));
                const auto exact = ((double) c.offset + (double) c.offsetFraction) * factor;
                c.offset = std::max<std::int64_t> (0, (std::int64_t) std::floor (exact));
                c.offsetFraction = (float) (exact - (double) c.offset);
                c.fadeIn = std::min (c.fadeIn, c.length);
                c.fadeOut = std::min (c.fadeOut, c.length - c.fadeIn);
            }
    return true;
}
// The BPM box: the sample read at `bpm`.
inline bool setFileBpm (Song& song, int channel, int track, int scene, double fileSeconds, double bpm)
{
    if (channel < 0 || channel >= (int) song.channels.size() || ! (bpm > 0.0))
        return false;
    const auto now = fileBpm (song.channels[(std::size_t) channel].sampler, fileSeconds);
    return now > 0.0 && std::abs (bpm - now) > 1.0e-9 && scaleWarpedTempo (song, channel, track, scene, fileSeconds, bpm / now);
}

// --- the Fade switch (Live's clip Fade: short fades at the clip's edges) ------------------------------------------------
// LYNX uses the clip's own fade-in and fade-out, `ms` long at the song tempo (at least one tick).
constexpr double edgeFadeMs = 4.0;
inline bool hasEdgeFades (const Clip& c) noexcept { return c.fadeIn > 0 || c.fadeOut > 0; }
inline void setEdgeFades (Clip& c, bool on, double bpm)
{
    const auto ticks = std::max<std::int64_t> (1, (std::int64_t) std::llround (edgeFadeMs / 1000.0 * std::max (1.0, bpm) / 60.0 * ppq));
    c.fadeIn = on ? std::min (ticks, c.length / 2) : 0;
    c.fadeOut = on ? std::min (ticks, c.length / 2) : 0;
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
