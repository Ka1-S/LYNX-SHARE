#pragma once
// Slicex's deck Snap menu (round 190jd; live check 190iw Q2, the magnet button of the deck toolbar): a radio list
//   Snap to grid (1, the default) · Snap to regions (2) · Snap to samples (3) · Snap to zero-crossing (4) ·
//   Snap to pitch period (5)
// (the digits are its shortcuts). It acts on the deck's selection edges; a region tag still moves by the mouse's
// distance exactly (REA_rodada190ac §2). Alt held: no snap while dragging (LYNX; FL's Alt-bypass convention).
//
// What each mode does here:
//  - grid: the selection follows the mouse to the pixel. FL's deck draws no beat grid, and its own selection drags with
//    "Snap to grid" on (the default) came out unsnapped, to the pixel (REA_rodada190aj §2, aj_4 / aj_12…20): kept so;
//  - regions: the nearest region start or end within 12 px (as LYNX's Edison);
//  - samples: whole samples;
//  - zero-crossing: the nearest frame where the (mono sum of the) wave changes sign;
//  - pitch period: the nearest rising zero crossing (where a period of a pitched sound starts), looked for within the
//    local pitch period (autocorrelation, 40 Hz…1 kHz); without a pitch, the nearest zero crossing. FL's own period
//    detector is not decoded: this is the closest honest behaviour.
// The last four were not measured in FL's deck (the manual: the Snap menu acts on selections).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lynx::slicex
{
enum class DeckSnap
{
    grid = 0,
    regions,
    samples,
    zeroCrossing,
    pitchPeriod
};
constexpr int deckSnapCount = 5;

// a mono reader of the deck's sample (left, right or null = mono)
struct DeckWave
{
    const float* left = nullptr;
    const float* right = nullptr;
    std::int64_t frames = 0;
    double sampleRate = 44100.0;
    float at (std::int64_t i) const noexcept { return right != nullptr ? left[i] + right[i] : left[i]; }
};

// a crossing "at i": the sign changes between i − 1 and i (or the sample is exactly 0); rising: from below 0 to 0 or above
inline bool crossesAt (const DeckWave& w, std::int64_t i, bool risingOnly) noexcept
{
    if (i <= 0 || i >= w.frames)
        return i == 0 || i == w.frames;
    const auto a = w.at (i - 1), b = w.at (i);
    if (risingOnly)
        return a < 0.0f && b >= 0.0f;
    return b == 0.0f || (a < 0.0f) != (b < 0.0f);
}

// the nearest crossing within `radius` frames; `frame` itself when there is none
inline std::int64_t nearestCrossing (const DeckWave& w, std::int64_t frame, std::int64_t radius, bool risingOnly) noexcept
{
    if (w.left == nullptr || w.frames < 2)
        return frame;
    frame = std::clamp<std::int64_t> (frame, 0, w.frames);
    for (std::int64_t d = 0; d <= radius; ++d)
    {
        if (frame - d >= 0 && crossesAt (w, frame - d, risingOnly))
            return frame - d;
        if (frame + d <= w.frames && crossesAt (w, frame + d, risingOnly))
            return frame + d;
    }
    return frame;
}

// the local pitch period around `at` in frames (normalised autocorrelation over ~23 ms, lags for 40 Hz…1 kHz,
// every 2nd frame); 0 = no clear pitch
inline std::int64_t localPitchPeriod (const DeckWave& w, std::int64_t at) noexcept
{
    if (w.left == nullptr || w.frames < 64)
        return 0;
    const auto sr = w.sampleRate > 0.0 ? w.sampleRate : 44100.0;
    const auto lo = std::max<std::int64_t> (2, (std::int64_t) (sr / 1000.0)), hi = (std::int64_t) (sr / 40.0);
    const auto span = (std::int64_t) (sr * 0.023);
    const auto from = std::clamp<std::int64_t> (at - span / 2, 0, std::max<std::int64_t> (0, w.frames - span - hi));
    if (from + span + hi > w.frames)
        return 0;
    double energy = 0.0;
    for (std::int64_t i = 0; i < span; i += 2)
        energy += (double) w.at (from + i) * w.at (from + i);
    if (energy <= 1.0e-9)
        return 0;
    double best = 0.0;
    std::int64_t lag = 0;
    for (auto l = lo; l <= hi; ++l)
    {
        double acc = 0.0, e2 = 0.0;
        for (std::int64_t i = 0; i < span; i += 2)
        {
            const auto b = (double) w.at (from + i + l);
            acc += (double) w.at (from + i) * b;
            e2 += b * b;
        }
        const auto r = e2 > 0.0 ? acc / std::sqrt (energy * e2) : 0.0;
        if (r > best + 1.0e-6)
            best = r, lag = l;
    }
    return best > 0.6 ? lag : 0;
}

// where a dragged selection edge lands. regionEdges: frames of the region starts / ends; framesPerPixel: the zoom shown
inline double snapDeckFrame (DeckSnap mode, double frame, const DeckWave& w, double framesPerPixel, const std::vector<std::int64_t>& regionEdges) noexcept
{
    const auto px = framesPerPixel > 0.0 ? framesPerPixel : 1.0;
    switch (mode)
    {
        case DeckSnap::grid:
            return frame; // (FL: to the pixel, see above)
        case DeckSnap::regions:
        {
            auto best = frame;
            auto bestPx = 12.0;
            for (const auto e : regionEdges)
                if (const auto d = std::abs ((double) e - frame) / px; d <= bestPx)
                    best = (double) e, bestPx = d;
            return best;
        }
        case DeckSnap::samples:
            return std::round (frame);
        case DeckSnap::zeroCrossing:
            return (double) nearestCrossing (w, (std::int64_t) std::llround (frame), (std::int64_t) std::max (8.0, px * 6.0), false);
        case DeckSnap::pitchPeriod:
        {
            const auto f = (std::int64_t) std::llround (frame);
            if (const auto period = localPitchPeriod (w, f); period > 0)
                return (double) nearestCrossing (w, f, period, true);
            return (double) nearestCrossing (w, f, (std::int64_t) std::max (8.0, px * 6.0), false);
        }
    }
    return frame;
}
} // namespace lynx::slicex
