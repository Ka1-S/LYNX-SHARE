#pragma once

// Round 190ik (BUILD-LIST 8): the warp engines LYNX lacked, measured black-box against Ableton Live 12.4's renders
// (/mnt/e/lynx-work/warp190ik, sets W1 of round 190gu and W2 of this round; docs/analise/REA_rodada190ik.md). LYNX's own
// code (clean room: Live's renders were only listened to by numbers, no code or content of Live is used):
//  - Complex Pro: the clip's transposition rendered offline (a spectral stretch to the transposed length, then a band-limited
//    resampling) followed by a formant correction that moves the spectral envelope back (Formants 0…100 % of the shift;
//    Envelope = the cepstral resolution of that envelope);
//  - Texture: granular overlap-add — grains of Grain Size ms at the source's own speed, two grains overlapping, each
//    starting where the warp map puts its anchor, Flux scattering the grains' source positions;
//  - Beats: a pitch-aware transient detector — legato pitch steps of a tonal sound are onsets; glides and vibrato are not.
// Everything here runs offline (the stretch cache, core/TimeStretch): never on the audio thread. Header only declares,
// no JUCE.

#include <cstdint>
#include <functional>
#include <vector>

namespace lynx::warpengine
{
using Channels = std::vector<std::vector<float>>;

// Band-limited resampling of every channel to `outFrames` frames (Kaiser-windowed sinc, cut-off lowered when the signal
// is read faster, so a transposition up does not alias).
Channels resampleTo (const Channels& in, long outFrames);

// Complex Pro's formant correction (in place): `y` was transposed by `factor` (its spectral envelope moved with it); the
// envelope of each 46 ms frame is estimated (cepstral "true envelope", its lifter set by `envelope` = Live's Envelope
// 8…256, see formantLifter) and moved back by factor^amount (amount = Formants / 100), attenuating only. One gain for all
// channels.
void keepFormants (Channels& y, double sampleRate, double factor, double amount, double envelope);
// The lifter length (samples) Complex Pro's Envelope value gives at `sampleRate`.
int formantLifter (double envelope, double sampleRate) noexcept;

// Texture: the granular render of `in` to `outFrames` frames. `toSource` maps an output frame to the source frame its
// grain anchor reads (the warp map, or output / ratio). grainMs = Live's Grain Size (2…263), flux = Live's Flux (0…100);
// `seed` makes the flux scatter repeatable (the same render every time). `factor` transposes: each grain reads its
// audio that much faster (Live: a transposed Texture clip's grains are resampled, its formants move).
Channels texture (const Channels& in, long outFrames, double sampleRate, const std::function<double (double)>& toSource, double grainMs,
                  double flux, std::uint32_t seed, double factor = 1.0);
// The grain period (ms) for a Grain Size, and the flux scatter (± ms of source position) at a flux value.
double textureGrainPeriodMs (double grainMs) noexcept;
double textureFluxSpreadMs (double grainMs, double flux) noexcept;

// The pitch track the Beats detector uses: one value per `hop` frames, semitones (MIDI) or NaN where the sound is not
// clearly pitched (YIN, 60…1000 Hz).
struct PitchTrack
{
    long hop = 0;
    std::vector<float> semitones;
};
PitchTrack pitchTrack (const float* mono, long frames, double sampleRate);

// The pitch-aware onsets: `levelCuts` (the level detector's cuts, already refined) minus the ones a pitch glide or vibrato
// explains, plus the legato pitch steps of pitched sound (a step of ≥ 0.8 semitone that happens within 60 ms and lasts).
// Sorted, at least 50 ms apart, inside (0, frames).
std::vector<long> pitchAwareCuts (const float* mono, long frames, double sampleRate, const std::vector<long>& levelCuts);
} // namespace lynx::warpengine
