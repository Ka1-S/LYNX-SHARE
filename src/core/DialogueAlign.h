#pragma once

// Dialogue alignment (round 174d, ADR): time-aligns a recorded take to the guide (the original line), offline — LYNX's
// own algorithm, in the spirit of the alignment tools post-production uses. Both signals become frames of log band
// energies (25 ms Hann windows every 10 ms, 24 bands 80 Hz…8 kHz, each band normalised over the utterance); dynamic
// time warping inside a band around the diagonal finds the cheapest path; the path, smoothed and kept monotonic with
// its slope limited, maps each guide time to a take time. The take is then rendered along that map with WSOLA (overlap-
// add of 30 ms grains, each placed where it best continues the previous one), so the pitch and the voice stay.
// No JUCE here (tests/Video174dTests.inc).

#include <cstddef>
#include <utility>
#include <vector>

namespace lynx::align
{
struct Options
{
    double maxShift = 1.0;   // seconds the take may run ahead of / behind the guide anywhere (the DTW band)
    double maxRate = 2.0;    // the warp's local speed stays in [1 / maxRate, maxRate]
    double smoothing = 0.05; // seconds of path smoothing
    double strength = 1.0;   // 0 = no change, 1 = follow the guide fully
};

struct Map
{
    std::vector<std::pair<double, double>> points; // (guide seconds, take seconds), increasing in both
    double takeAt (double guideSeconds) const noexcept;
    bool empty() const noexcept { return points.size() < 2; }
};

// Mono signals at `sampleRate`
Map align (const std::vector<float>& guide, const std::vector<float>& take, double sampleRate, const Options& options = {});
// The take rendered along the map: `outLength` samples (the guide's length), sample i plays the take at map.takeAt (i / sr)
std::vector<float> warp (const std::vector<float>& take, double sampleRate, const Map& map, std::size_t outLength);

// (exposed for the tests) the frame features: frames × 24 values
std::vector<std::vector<float>> features (const std::vector<float>& signal, double sampleRate);
constexpr double hopSeconds = 0.010;
} // namespace lynx::align
