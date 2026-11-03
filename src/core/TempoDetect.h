#pragma once

// Tempo detection of audio (FL's audio clip "Detect tempo", behaviour from its manual; the method is LYNX's own,
// REA round 153): an onset-strength curve from band energies, its autocorrelation over the tempo range, and a final
// snap to the sample's length when the sample is a loop of whole beats.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lynx
{
struct SampleData;
}

namespace lynx::tempodetect
{
struct Options
{
    double minBpm = 70.0, maxBpm = 180.0; // the range the result is folded into
    bool wholeBpm = false;                // FL 2024's "ignore decimals": round the result to a whole BPM
    bool snapToLength = true;             // a loop of whole beats: the tempo that fits its length exactly
};

struct Result
{
    double bpm = 0.0;        // 0 = no tempo found (silence, too short, no pulse)
    double confidence = 0.0; // 0..1: how much the winning period stands out
    double beats = 0.0;      // the sample's length in beats at `bpm`
};

// `left` / `right` (right may be null = mono) with `frames` frames at `sampleRate`.
Result detect (const float* left, const float* right, std::size_t frames, double sampleRate, const Options& options = {});

// The onset-strength curve (one value per hop of `hopSeconds`), exposed for tests.
std::vector<float> onsetCurve (const float* left, const float* right, std::size_t frames, double sampleRate, double hopSeconds);

// "Fit to tempo" of a whole sample (issue #32 #7): clipops::fitBeats with the WAV's ACID beats, else the tempo detected
// in it (samples up to 2 minutes), else its power-of-two length at `bpm`. 0 for no sample.
float fitBeatsOf (const SampleData* sample, double bpm);
} // namespace lynx::tempodetect
