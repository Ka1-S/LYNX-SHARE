#include "core/EdisonView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lynx::edisonview
{
namespace
{
// Delphi's Round (FL's 0x40c3e0): to the nearest, halves to even — the default FPU rounding
std::int64_t flRound (double v) noexcept { return (std::int64_t) std::nearbyint (v); }
} // namespace

Layers layers (bool spectrumView, bool dualView, double samplesPerPixel) noexcept
{
    if (! dualView)
        return spectrumView ? Layers { 256, 0 } : Layers { 0, 256 };
    const auto f = (float) std::clamp (samplesPerPixel > 0.0 ? 2.0 / samplesPerPixel : 1.0, 0.0, 1.0);
    if (spectrumView)
        return { 256 - (int) flRound ((double) (f * 256.0f)), (int) flRound ((double) (f * 164.0f)) + 92 };
    return { 92 - (int) flRound ((double) (f * 92.0f)), 256 };
}

bool beatMode (const audioedit::SampleProps& props) noexcept
{
    return props.tempoSync && props.tempo >= 10.0 && props.tempo <= 999.0;
}

double framesPerUnit (bool beats, double sampleRate, double tempo) noexcept
{
    // seconds: a unit is 500 ms (0x855850: × 500, then ms → frames); beats: 60 / tempo seconds (0x62c060)
    if (beats && tempo > 0.0)
        return sampleRate * 60.0 / tempo;
    return sampleRate * 0.5;
}

GridBase secondsBase() noexcept
{
    return { 2.0 / 10.0, 2.0, 0.0 };
}

GridBase beatsBase (int stepsPerBar, int stepsPerBeat) noexcept
{
    stepsPerBeat = std::max (1, stepsPerBeat);
    return { 1.0 / stepsPerBeat, 1.0, (double) (std::max (0, stepsPerBar) / stepsPerBeat) };
}

Grid grid (const GridBase& base, double pixelsPerUnit, double minPixels) noexcept
{
    Grid g;
    if (! (base.small > 0.0) || ! (pixelsPerUnit > 0.0))
    {
        g.step = base.small > 0.0 ? base.small : 65536.0;
        return g;
    }
    const auto s = base.small;
    const auto mediumCount = (int) flRound (base.medium / s);
    const auto largeCount = (int) flRound (base.large / s);
    const auto most = std::max (largeCount, mediumCount);
    const auto medium = std::max (1, mediumCount), large = std::max (1, largeCount);
    // 0x84c7e0: m divides or is a multiple of both counts, and m steps are at least minPixels wide
    const auto fits = [&] (int m) {
        return (medium % m == 0 || m % medium == 0) && (large % m == 0 || m % large == 0) && s * m * pixelsPerUnit >= minPixels;
    };
    auto m = most;
    if (most != 0)
    {
        if (! fits (most))
        {
            for (int guard = 0; guard < 40 && ! fits (m); ++guard)
                m *= 2;
        }
        else
        {
            m = 1;
            while (m < most && ! fits (m))
                ++m;
        }
    }
    g.step = s * (double) std::max (1, m);
    if (! (g.step > 0.0))
        g.step = 65536.0;
    g.mediumEvery = (int) std::max<std::int64_t> (1, flRound (base.medium / g.step));
    g.largeEvery = base.large == 0.0 ? 0 : (int) std::max<std::int64_t> (1, flRound (base.large / g.step));
    return g;
}

int lineClass (std::int64_t i, const Grid& g) noexcept
{
    if (g.largeEvery != 0 && i % g.largeEvery == 0)
        return 2;
    return g.mediumEvery > 0 && i % g.mediumEvery == 0 ? 1 : 0;
}

double snapToStep (double pos, double step, double offset) noexcept
{
    if (! (step > 0.0))
        return pos;
    return (double) flRound ((pos - offset) / step) * step + offset;
}

double pitchPeriodFrames (double sampleRate, int middleNote, double fineCents) noexcept
{
    const auto hz = 440.0 * std::exp2 (((double) middleNote + fineCents / 100.0 - 57.0) / 12.0);
    return hz > 0.0 ? 1000.0 / hz * sampleRate * 0.001 : 0.0;
}

std::int64_t snapToPeriod (std::int64_t frame, double periodFrames) noexcept
{
    if (! (periodFrames > 0.0))
        return frame;
    return flRound ((double) flRound ((double) frame / periodFrames) * periodFrames);
}

double sliceStep (const GridBase& base, int size) noexcept
{
    const auto step = size == 0 ? base.small : size == 1 ? base.medium : base.large;
    return step <= 0.0 ? base.medium : step;
}

std::vector<std::int64_t> gridSlicePositions (std::int64_t start, std::int64_t end, double stepFrames)
{
    std::vector<std::int64_t> out;
    if (! (stepFrames > 0.0) || end < start)
        return out;
    for (auto p = (double) start; p <= (double) end && out.size() < 1000000; p += stepFrames)
        out.push_back (flRound (p));
    return out;
}

int gridSlice (audioedit::Document& doc, std::int64_t start, std::int64_t end, double stepFrames)
{
    const auto positions = gridSlicePositions (start, end, stepFrames);
    if (positions.empty())
        return 0;
    // (FL removes its slice markers that lie in the range; regions and the loop stay)
    std::erase_if (doc.markers, [&] (const audioedit::Marker& m) { return ! m.isRegion() && m.start >= start && m.start <= end; });
    for (const auto p : positions)
        audioedit::addMarker (doc, p, {});
    return (int) positions.size();
}

double scrubSpeed (double speed, double pixelsRight, double uiScale) noexcept
{
    const auto scale = uiScale > 0.0 ? uiScale : 1.0;
    return std::clamp (speed + pixelsRight * 0.025 / scale, -scrubMaxSpeed, scrubMaxSpeed);
}

std::string scrubText (double speed)
{
    if (std::abs (speed) <= 1.0e-7)
        return "Paused";
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%.1f x", std::abs (speed));
    return std::string (speed >= 0.0 ? "\xe2\x96\xb6 " : "\xe2\x97\x80 ") + buf;
}

std::string regionNoteName (int note)
{
    static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto n = std::max (0, note);
    return std::string (names[n % 12]) + std::to_string ((n + 1200) / 12 - 100);
}

int pitchRegions (audioedit::Document& doc, const std::vector<PitchSegment>& segments)
{
    doc.markers.clear();
    int n = 0;
    for (const auto& s : segments)
    {
        const auto note = flRound (s.pitch);
        if (note < 0 || note >= 128 || s.end <= s.start)
            continue;
        audioedit::addRegion (doc, { s.start, s.end }, regionNoteName ((int) note));
        ++n;
    }
    return n;
}
} // namespace lynx::edisonview
