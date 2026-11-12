#pragma once

// Warp commands on a channel's markers (round 190cg; the commands of Ableton Live's sample editor, the method LYNX's own:
// /mnt/e/lynx-work/warp190cg/HANDOFF.md lists the evidence per command). They work on SamplerSettings::warp (file
// seconds → content beats, core/WarpEdit.h) after warpedit::ensureMarkers; the caller ends with warpedit::syncLength.
// Everything here stays piecewise linear, which is what the offline stretcher renders (core/TimeStretch warpStretchMap),
// so each command plays exactly as its markers say. JUCE-free and header-only.

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "core/WarpEdit.h"

namespace lynx::warptools
{
// The segment (index of its left marker) that holds file time `seconds`; the end segments reach beyond the end markers.
inline int segmentAt (const std::vector<WarpPoint>& w, double seconds) noexcept
{
    if (w.size() < 2)
        return -1;
    int i = 0;
    while (i + 2 < (int) w.size() && seconds >= w[(std::size_t) i + 1].seconds)
        ++i;
    return i;
}

// The tempo the audio of segment `index` is taken to have (Live's "Seg. BPM" / "BPM" box): its beats per minute of file
// time. The last marker answers with the segment that ends on it. 0 when there are no markers.
inline double segmentBpm (const std::vector<WarpPoint>& w, int index) noexcept
{
    if (w.size() < 2)
        return 0.0;
    const auto i = (std::size_t) std::clamp (index, 0, (int) w.size() - 2);
    const auto seconds = w[i + 1].seconds - w[i].seconds;
    return seconds > 0.0 ? 60.0 * (w[i + 1].beats - w[i].beats) / seconds : 0.0;
}

// Segment `index` set to `bpm`: the markers after it keep their own tempos (they move in beats with the segment's end).
inline bool setSegmentBpm (SamplerSettings& s, int index, double bpm)
{
    if (s.warp.size() < 2 || index < 0 || index + 1 >= (int) s.warp.size() || ! (bpm > 0.0))
        return false;
    const auto i = (std::size_t) index;
    const auto beats = std::max (warpedit::minBeatGap, (s.warp[i + 1].seconds - s.warp[i].seconds) * bpm / 60.0);
    const auto shift = s.warp[i].beats + beats - s.warp[i + 1].beats;
    for (auto k = i + 1; k < s.warp.size(); ++k)
        s.warp[k].beats += shift;
    return true;
}

// Live's ×2 and ÷2: the audio's tempo is read as `factor` times what it was, so every marker sits on `factor` times its
// beat (beat 0 stays). ×2 plays the audio half as fast, ÷2 twice as fast.
inline bool scaleTempo (SamplerSettings& s, double factor)
{
    if (s.warp.size() < 2 || ! (factor > 0.0))
        return false;
    for (auto& m : s.warp)
        m.beats *= factor;
    return true;
}

// ÷2 / ×2 on the clip the markers were scaled for (round 190cy; Live 12.4.16 live check 190cw shot 02: ÷2 on a 2-bar
// loop at 120 BPM took the segment to 60 BPM, End 3.1.1 → 2.1.1 and Loop Length 2.0.0 → 1.0.0): the clip's length (its
// end, and its loop length in a slot) and where it starts in the audio scale by the same factor, so it holds the same
// audio. Fades stay inside the clip.
inline void scaleClip (Clip& c, double factor)
{
    if (! (factor > 0.0))
        return;
    c.length = std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) c.length * factor));
    const auto exact = ((double) c.offset + (double) c.offsetFraction) * factor;
    const auto whole = std::floor (exact + 0.5);
    c.offset = std::max<std::int64_t> (0, (std::int64_t) whole);
    c.offsetFraction = c.offset > 0 ? (float) (exact - whole) : 0.0f;
    c.fadeIn = std::min (c.fadeIn, c.length);
    c.fadeOut = std::min (c.fadeOut, c.length - c.fadeIn);
}

// Live's warp marker drags, one surface (round 190cy; Live 12.4.16 live check 190cw shot 09): a plain drag moves the
// marker with its audio to another beat, so the audio on both sides stretches; Shift + drag keeps the marker on its
// beat and slides the audio under it (the marker is tied to another point of the audio). In an editor drawn in file
// time the plain drag's beat is the one the pointer's place played on before the drag (`before`: the markers then).
inline double stretchDragBeat (const std::vector<WarpPoint>& before, double pointerSeconds)
{
    return warpedit::beatAt (before, pointerSeconds);
}
inline bool slideMarker (SamplerSettings& s, int index, double seconds)
{
    if (index <= 0 || index >= (int) s.warp.size())
        return false;
    const auto lo = s.warp[(std::size_t) index - 1].seconds + warpedit::minSecondGap;
    const auto hi = index + 1 < (int) s.warp.size() ? s.warp[(std::size_t) index + 1].seconds - warpedit::minSecondGap : 1.0e9;
    s.warp[(std::size_t) index].seconds = std::clamp (seconds, lo, std::max (lo, hi));
    return true;
}

// "Set 1.1.1 Here": the audio at `seconds` becomes beat 0 (every marker's beat shifts by the same amount; the tempos
// stay). Returns the shift in beats (what the point's beat was).
inline double setOneOneOne (SamplerSettings& s, double seconds)
{
    if (s.warp.size() < 2)
        return 0.0;
    const auto shift = warpedit::beatAt (s.warp, seconds);
    for (auto& m : s.warp)
        m.beats -= shift;
    return shift;
}

// Where content beat 0 plays, in beats from the file's start as the channel plays it (warpedit::playedMarkers'
// normalisation): the offset a clip needs to start on 1.1.1. Negative when beat 0 lies before the file.
inline double playedBeatOfZero (const SamplerSettings& s, double fileSeconds) noexcept
{
    if (s.warp.size() < 2 || ! (fileSeconds > 0.0))
        return 0.0;
    const auto b0 = warpedit::beatAt (s.warp, 0.0), b1 = warpedit::beatAt (s.warp, fileSeconds);
    if (! (b1 > b0))
        return 0.0;
    const auto scale = s.stretchBeats > 0.0f ? (double) s.stretchBeats / (b1 - b0) : 1.0;
    return -b0 * scale;
}

// The markers up to `seconds` and one at `seconds` on the beat it plays on now: the part a "from here" command keeps.
inline std::vector<WarpPoint> headUpTo (const std::vector<WarpPoint>& w, double seconds)
{
    std::vector<WarpPoint> head;
    for (const auto& m : w)
        if (m.seconds < seconds - warpedit::minSecondGap)
            head.push_back (m);
    head.push_back ({ seconds, warpedit::beatAt (w, seconds) });
    return head;
}

// "Warp <tempo> BPM From Here": the markers after `seconds` go; from there the audio is taken to be at `bpm`, so it
// plays at its own speed while the song is at that tempo. False when `seconds` is not inside the file.
inline bool warpTempoFromHere (SamplerSettings& s, double seconds, double fileSeconds, double bpm)
{
    if (s.warp.size() < 2 || ! (bpm > 0.0) || seconds < 0.0 || ! (fileSeconds > seconds + warpedit::minSecondGap))
        return false;
    auto out = headUpTo (s.warp, seconds);
    out.push_back ({ fileSeconds, out.back().beats + (fileSeconds - seconds) * bpm / 60.0 });
    s.warp = std::move (out);
    return true;
}

// "Warp From Here" and its variants: the markers after `seconds` are replaced by `tail`, the markers an analysis of the
// audio from `seconds` on gave (their seconds count from `seconds`, their beats from anywhere). The audio at `seconds`
// stays on its beat. False when the tail has nothing to offer.
inline bool warpTailFromHere (SamplerSettings& s, double seconds, double fileSeconds, const std::vector<WarpPoint>& tail)
{
    if (s.warp.size() < 2 || tail.size() < 2 || seconds < 0.0 || ! (fileSeconds > seconds + warpedit::minSecondGap))
        return false;
    auto out = headUpTo (s.warp, seconds);
    const auto here = out.back().beats, zero = warpedit::beatAt (tail, 0.0);
    for (const auto& m : tail)
    {
        const WarpPoint p { seconds + m.seconds, here + (m.beats - zero) };
        if (p.seconds > out.back().seconds + warpedit::minSecondGap && p.beats > out.back().beats + warpedit::minBeatGap
            && p.seconds <= fileSeconds + warpedit::minSecondGap)
            out.push_back (p);
    }
    if (out.size() < 2 || out.back().seconds < seconds + warpedit::minSecondGap * 2.0)
        return false;
    s.warp = std::move (out);
    return true;
}

// The beats "Warp as N-Bar Loop" offers for a file of `fileSeconds`: whole bars, a power of two of them, nearest its
// length at the song's tempo (the choice round 175a measured for unnamed loops of the reference's library).
inline double loopBars (double fileSeconds, double songBpm, double beatsPerBar) noexcept
{
    const auto bar = std::max (1.0, beatsPerBar) * 60.0 / std::clamp (songBpm, 10.0, 999.0);
    return std::exp2 (std::max (0.0, std::round (std::log2 (std::max (1.0e-9, fileSeconds / bar)))));
}

// "Warp as N-Bar Loop": one tempo, the whole file lasts `beats`.
inline bool warpAsLoop (SamplerSettings& s, double fileSeconds, double beats)
{
    if (! (fileSeconds > 0.0) || ! (beats > 0.0))
        return false;
    s.warp = { { 0.0, 0.0 }, { fileSeconds, beats } };
    s.keepPitch = true;
    return true;
}

// Markers at each of `seconds` (file times), each on the beat it plays on now: nothing moves. Returns how many were
// added ("Insert warp markers" at the edges of a selection or on its transients).
inline int insertMarkers (SamplerSettings& s, const std::vector<double>& seconds)
{
    int added = 0;
    for (const auto t : seconds)
        added += warpedit::addMarker (s, t) >= 0;
    return added;
}

// Quantize audio: each of `points` (file times: the attacks) between `from` and `to` gets a marker and moves towards
// the nearest line of a grid of `gridBeats`, by `amount` (0…1). Markers already inside the range are treated like
// attacks; two points that want the same grid line: the nearer one gets it, the other stays free. The markers outside
// the range hold. Returns the number of markers placed.
inline int quantize (SamplerSettings& s, const std::vector<double>& points, double from, double to, double gridBeats, double amount)
{
    if (s.warp.size() < 2 || ! (gridBeats > 0.0) || ! (to > from))
        return 0;
    amount = std::clamp (amount, 0.0, 1.0);
    const auto first = s.warp.front().seconds, last = s.warp.back().seconds;
    std::vector<double> times;
    for (const auto t : points)
        if (t >= from && t <= to && t > first + warpedit::minSecondGap && t < last - warpedit::minSecondGap)
            times.push_back (t);
    std::vector<WarpPoint> kept;
    for (std::size_t k = 0; k < s.warp.size(); ++k)
    {
        const auto& m = s.warp[k];
        const auto inside = k > 0 && k + 1 < s.warp.size() && m.seconds >= from && m.seconds <= to;
        if (! inside)
            kept.push_back (m);
        else if (std::none_of (times.begin(), times.end(), [&] (double t) { return std::abs (t - m.seconds) < warpedit::minSecondGap; }))
            times.push_back (m.seconds);
    }
    std::sort (times.begin(), times.end());
    // one point per grid line: the nearest
    struct Pick
    {
        double seconds, beat, distance;
    };
    std::map<long long, Pick> lines;
    for (const auto t : times)
    {
        const auto beat = warpedit::beatAt (s.warp, t);
        const auto line = std::llround (beat / gridBeats);
        const auto distance = std::abs (beat - (double) line * gridBeats);
        if (const auto it = lines.find (line); it == lines.end() || distance < it->second.distance)
            lines[line] = { t, beat, distance };
    }
    int placed = 0;
    for (const auto& [line, pick] : lines)
    {
        const auto target = amount >= 1.0 ? (double) line * gridBeats : pick.beat + amount * ((double) line * gridBeats - pick.beat);
        const auto at = std::lower_bound (kept.begin(), kept.end(), pick.seconds, [] (const WarpPoint& p, double t) { return p.seconds < t; });
        if (at == kept.begin() || at == kept.end())
            continue;
        if (pick.seconds - (at - 1)->seconds < warpedit::minSecondGap || at->seconds - pick.seconds < warpedit::minSecondGap
            || target < (at - 1)->beats + warpedit::minBeatGap || target > at->beats - warpedit::minBeatGap)
            continue;
        kept.insert (at, WarpPoint { pick.seconds, target });
        ++placed;
    }
    s.warp = std::move (kept);
    return placed;
}
} // namespace lynx::warptools
