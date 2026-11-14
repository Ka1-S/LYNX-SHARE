#pragma once

// Pitch-preserving time-stretch, done off the audio thread once per (sample, ratio, method), like FL's "offline"
// modes (docs/analise/REA_rodada3.md §5). Round 160 (docs/analise/REA_rodada160.md) replaced the earlier basic WSOLA
// with Signalsmith Stretch (MIT, third_party/signalsmith) plus LYNX's own transient handling:
//  - ratios within 0.1 % of 1 (e.g. FL's recorded takes, whose stretch time is their own length) are resampled with a
//    windowed sinc instead — at most 1.7 cents of pitch, no stretch artefacts at all;
//  - otherwise the spectral stretcher runs, and the attacks it detects are
//    spliced back unstretched (no smeared or doubled hits, no pre-echo).

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// A warp mode's own settings for one stretch (round 190fm): the channel's WarpModeSettings and, for Beats cut on a grid,
// how many beats the whole source spans (its stretchBeats). Default-constructed = the render before 190fm.
struct StretchOptions
{
    WarpModeSettings modes;
    double sourceBeats = 0.0;
    bool operator== (const StretchOptions&) const = default;
};
// The options a sampler's stretch uses (its warpModes and stretchBeats).
StretchOptions stretchOptionsOf (const SamplerSettings& sampler);
// The part of `options` that changes what `method` renders, as text for cache keys: empty when it renders as the
// defaults do (so every key made before 190fm stays the same).
std::string stretchOptionsKey (const StretchOptions& options, StretchMethod method);

// `ratio` = output duration / input duration (2 = twice as long, same pitch).
// `semitones` (Beats only, else ignored): each piece is also resampled by that much, offline (Live's clip transpose in
// Beats mode); the pieces keep their places on the grid.
std::shared_ptr<SampleData> timeStretch (const SampleData& input, double ratio, StretchMethod method = StretchMethod::general,
                                         double semitones = 0.0, const StretchOptions& options = {});

// A sample FL's precomputed effects and Trim already cut (core/SamplePrecompute.h) stretched by `ratio`: FL stretches that
// span to TIME, not the whole file (REA round 180d, FUN_00ff0db0 runs before the stretch). Keeps what the sampler needs to
// map its knobs (cropped, cropFrom, cropTo) and scales the span's loop.
std::shared_ptr<SampleData> timeStretchSpan (const SampleData& span, double ratio, StretchMethod method = StretchMethod::general,
                                             double semitones = 0.0, const StretchOptions& options = {});

// A warp map (round 175a, warp markers M1): source frame → output frame, piecewise linear between its points. Both
// coordinates increase; the first point is (0, 0) and the last (the source's frames, the output's frames). Each segment
// is stretched by its own ratio, with no seam between them.
using StretchMap = std::vector<std::pair<double, double>>;
// The stretch along a warp map (fewer than 3 points, or a segment outside 0.05…20×: timeStretch with the overall ratio).
std::shared_ptr<SampleData> timeStretchMapped (const SampleData& input, const StretchMap& map, StretchMethod method = StretchMethod::general,
                                               double semitones = 0.0, const StretchOptions& options = {});
// A channel's warp markers (sample seconds ↔ content beats) as the map that stretches a source of `frames` frames at
// `sampleRate` to `outFrames`: each sample time lands at its beat, scaled so the whole file fills `outFrames` (beats
// before the first marker and after the last one follow the nearest segment). Empty when the markers give one tempo.
StretchMap warpStretchMap (const std::vector<WarpPoint>& warp, double frames, double sampleRate, double outFrames);
// Where source frame `x` lands on a map (and back); outside it the end segments continue.
double stretchMapOutput (const StretchMap& map, double x) noexcept;
double stretchMapSource (const StretchMap& map, double y) noexcept;

// FL's stretch mode (channel record 0xD7 +0x6C, REA_rodada3 §5.2) → LYNX's method. 0 (Resample) has no
// pitch-preserving stretch; callers keep resampling for it.
StretchMethod stretchMethodForFlMode (int flMode) noexcept;

// A voice's real-time pitch shifter (round 162, issue #9: a sampler stretched keeping its pitch follows the keys like
// FL's Stretch modes — the note transposes the sample and its length stays). The voice reads its source `factor` times
// faster (an exact transposition, as a resampler) and Signalsmith Stretch stretches that back to the source's length
// (pitch kept): measured within 0.05 cent from 55 Hz up, where Signalsmith's own spectral transposition was off by up to
// 30 cents with real-time block sizes. The voice primes it with primeLength (factor) input frames when the note starts,
// so the output is aligned with the source (no latency); then each process() takes the input frames read for n output
// frames (n / factor of them, accumulated by the caller). allocate() on the UI thread; start()/process() never allocate.
class VoicePitchShifter
{
public:
    static constexpr double minFactor = 0.25, maxFactor = 4.0; // ±2 octaves

    VoicePitchShifter();
    ~VoicePitchShifter();
    VoicePitchShifter (const VoicePitchShifter&) = delete;
    VoicePitchShifter& operator= (const VoicePitchShifter&) = delete;

    void allocate (double sampleRate, StretchMethod method);
    bool ready() const noexcept;
    int primeLength (double factor) const noexcept;
    int maxPrimeLength() const noexcept { return primeLength (minFactor); } // the longest: the lowest transposition
    // `l`/`r` (r may equal l): primeLength (factor) frames of the transposed source from the note's start
    void start (const float* l, const float* r, int frames) noexcept;
    void process (const float* inL, const float* inR, int inFrames, float* outL, float* outR, int outFrames) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

namespace stretchdetail
{
// Exposed for the tests: attack positions (frames) found in a mono signal, strongest-first filtering already applied.
std::vector<long> detectAttacks (const float* mono, long frames, double sampleRate, double minRiseDb);
// Where LYNX's Beats warp mode cuts the audio (round 175a, N4): the attacks, each moved to where it rises fastest.
std::vector<long> beatsCuts (const float* mono, long frames, double sampleRate);
// The ratio band that is resampled instead of stretched.
constexpr double resampleTolerance = 0.001;

// The spectral stretcher's analysis block and interval, and the attack splicing (0 dB = none).
struct Preset
{
    double blockSeconds, intervalSeconds, attackRiseDb, holdSeconds;
};
Preset presetFor (StretchMethod method) noexcept;
// The same with a channel's grain sizes (round 190fm): Tones and Texture scale the block by size / Live's default size
// (clamped to 10…250 ms); every other method, and the default sizes, give presetFor (method) exactly.
Preset presetFor (StretchMethod method, const WarpModeSettings& modes) noexcept;
// Beats cut on a grid (round 190fm, Preserve 1 bar … 1/32): the source frames where the output reaches every multiple
// of `divisionBeats` (output frame = beats × outFrames / sourceBeats), mapped back through `toSource`; strictly
// increasing, inside (0, frames).
std::vector<long> gridCuts (long frames, long outFrames, double sourceBeats, double divisionBeats, const std::function<double (double)>& toSource);
// The note value of Preserve index 0…5 in beats (1 bar = 4 beats … 1/32 = 0.125); 0 for Transients.
double preserveDivisionBeats (int preserve) noexcept;
// Beats' Transient Envelope (round 190fm): the gain at output frame i of a piece that sounds for `span` frames,
// envelope 0…100 (100 = 1 everywhere). LYNX's own curve [needs live render]: (1 − i/span)^k, k = 4 (1 − e) / max (e, 0.02).
float beatsEnvelopeGain (float envelope, long i, long span) noexcept;
// The spectral stretch with a given preset (no near-unity shortcut).
std::shared_ptr<SampleData> stretchWith (const SampleData& input, double ratio, const Preset& preset);
} // namespace stretchdetail
} // namespace lynx
