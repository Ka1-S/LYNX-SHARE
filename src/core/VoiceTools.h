#pragma once

// LYNX's voice tools (REA_rodada119): two native processors for vocal chains, written from published DSP literature
// and designed against our own black-box measurements of the plugins vocal projects use in these roles (official
// trials, docs/analise/REA_rodada119.md). Own names, own code, own parameter laws; the laws follow the measured ones,
// so a saved setting of those plugins translates one to one when a project uses them on a machine without them.
//
//   Quell Denoiser   one-knob broadband noise suppressor: a phase-coherent 6-band Linkwitz-Riley split, a downward
//                    expander per band whose threshold, depth and slope follow the Amount knob, fast opening and a
//                    slower close, linked stereo; Listen plays what is taken away
//   Gemini Doubler   two-voice doubler: each voice is a pitch-shifting delay (two crossfaded grains reading a delay
//                    line) with gain, pan, delay, feedback, detune, a triangle LFO on the pitch, an octave-down switch
//                    and its source (left, sum, difference); the direct signal can be rotated in the stereo field and
//                    aligned with the voices; low / high shelves on the voices, a low cut in the feedback paths
//
// Parameters are 0..1 in the model (EffectParams). Delay lines are allocated in prepare() (off the audio thread),
// process() never allocates. Neither effect reports latency: Gemini's "Align" delays the direct signal inside the
// effect, as the measured reference does (it reports none either).
//
// Meters (Effect::controlOutput): Quell: 0 = mean reduction 0..1 over 0..30 dB, 1..6 = each band's reduction / 30.

#include <memory>
#include <string>

#include "core/Song.h"

namespace lynx
{
struct EffectInfo;
class Effect;

namespace voicetools
{
bool isVoiceKind (EffectKind kind) noexcept;
std::unique_ptr<Effect> make (EffectKind kind);
std::string paramText (EffectKind kind, int param, float v);
const EffectInfo& quellInfo();
const EffectInfo& geminiInfo();

namespace quell
{
// `matched` (appended, round 124): the engine fitted to the measured NS1 trial (REA_rodada124 §6) — a zero-latency
// chain whose open state is exactly the reference's all-pass (60 Hz / 16 kHz), a cascade of peaking cuts on seven
// octave bands and gain-reduced low / high bands; off (older projects) = the 6-band expander of round 119
// `law` (appended, round 190dp; Measured for new instances, Classic for projects saved before): the matched engine's
// noise-floor tracker (below). Off = round 124's fixed thresholds
enum Param { amount, listen, output, matched, law, paramCount };
constexpr int bands = 6;
// crossover frequencies (Hz) between the six bands
constexpr double split[bands - 1] { 105.0, 311.0, 921.0, 2727.0, 8075.0 };
double outputDb (float v) noexcept; // −12 .. +12 dB

// The expander's settings per band at an Amount (0..1). Fitted to the measurements (REA_rodada119 §3).
struct Tuning
{
    double thr0[bands] {};        // threshold (dB of band power, sine-calibrated) at Amount 0
    double thr1[bands] {};        // ... at Amount 1 (linear in between)
    double range0[bands] {};      // depth (dB) at Amount 0
    double range1[bands] {};      // ... at Amount 1
    double slope = 1.0;           // dB of reduction per dB below the threshold
    double knee = 6.0;            // soft knee width (dB)
    double floorCut0[bands] {};   // constant reduction (dB) at Amount 0 (the top band's steady trim)
    double floorCut1[bands] {};
    double detAttackMs = 2.0, detReleaseMs = 30.0; // power detector
    double openMs = 3.0, closeMs = 60.0;           // gain smoothing (opening / closing)
};
const Tuning& defaultTuning() noexcept;
// steady-state reduction (dB, ≥ 0) of band b for a band power `levelDb` at `amount`
double reductionDb (const Tuning& t, int band, double levelDb, double amount) noexcept;
// an engine with explicit tuning (tests and the fitting harness)
std::unique_ptr<Effect> make (const Tuning& t);

// Round 190dp: the matched engine's noise-floor tracker. [black-box, the NS1 trial's renders] the reference does not
// work from fixed thresholds: at a high setting a steady programme is taken down more and more over the first seconds
// (a dense mix by 10 … 24 dB at 100 %, nothing at 35 … 50 %), the signature of a noise floor estimated from the
// signal itself. Here (minimum statistics, R. Martin, "Noise power spectral density estimation based on optimal
// smoothing and minimum statistics", IEEE TSAP 9(5), 2001 — the minimum of the smoothed band power over a sliding
// window, kept as the minima of `subWindows` sub-windows) each band's floor sets a second expander threshold, `off`
// dB above it; the larger of its reduction and the fixed law's is applied. Latency stays 0.
// The constants below are fitted (round 190dp, /mnt/e/lynx-work/vqe190dp/qfit) on the reference's programme renders at
// 0 / 20 / 35 / 50 / 100 % and its stimulus captures at 0 … 100 %: band-envelope distance 4.15 → 2.95 dB; then the
// tracker got its own Q 8.5 bands (a tone sweeping by is not taken for its own floor), on the channels' mean and
// evaluated every 16 samples (its cost: within 10 % of the fixed law's), with off1 17 → 16 dB to match.
struct Track
{
    double smoothMs = 22.5;   // band power smoothing ahead of the minimum (ms)
    double windowS = 1.1;     // the minimum's window (s)
    double off0 = -30.0, off1 = 16.0, offPow = 2.0; // threshold above the floor: off0 + (off1 − off0) · amount^offPow
    double slope = 0.8875, knee = 32.5;             // dB of reduction per dB under it, soft knee (dB)
    double range0 = 0.0, range1 = 17.0;             // the tracker's largest reduction (dB) at amount 0 / 1
    double bias = 1.5;                              // the minimum's bias: added to it (dB; a minimum reads low)
    double q = 8.5;   // the tracker's own band-pass Q on the seven middle bands (0 = the expander's detectors): a
                      // narrower view, so that a tone sweeping past is not taken for its own floor
};
constexpr int subWindows = 8;
const Track& measuredTrack() noexcept;
// the matched engine with an explicit tracker (tests and the fitting harness); `params` as the Effect sees them
std::unique_ptr<Effect> makeTracked (const Track& t);
} // namespace quell

namespace gemini
{
// round 135w: four voices (the four-voice doubler's voices 3 and 4, appended after voice 2, off by default) and a
// modulation RESET (momentary: each flip of its value restarts every voice's LFO, in sync)
constexpr int voices = 4;
enum Param
{
    output, directOn, directGain, directPan, align, lowOn, lowGain, lowFreq, highOn, highGain, highFreq, feedbackCut, range,
    voice1, // first parameter of voice 1; each voice has `voiceParams` parameters (VoiceParam order)
    voice3 = voice1 + 2 * 10, // round 135w (appended)
    modReset = voice1 + 4 * 10, // round 135w (appended)
    // round 165w (appended): the shelves' law — 1 = measured on the 17.1 trials (gain −88 … +12 dB, two first-order
    // sections), 0 = round 124's (−30 … +12 dB, one first-order section): a project saved before keeps it
    eqLaw,
    // round 190do (appended): the voices' read head with Align off — 1 = measured (one head that jumps back by a fixed
    // range), 0 = two crossfaded taps half a window apart (projects saved before)
    headLaw,
    paramCount
};
enum VoiceParam { vOn, vGain, vPan, vDelay, vFeedback, vDetune, vDepth, vRate, vOctave, vSource, voiceParams };
inline int voiceParam (int voice, VoiceParam p) noexcept { return voice1 + voice * voiceParams + (int) p; }

double outputDb (float v) noexcept;      // −24 .. 0 dB
double gainDb (float v) noexcept;        // voice / direct gain: −48 .. 0 dB
double panDegrees (float v) noexcept;    // −45 .. +45 (voices: equal-power pan; direct: stereo rotation)
double delayMs (float v) noexcept;       // 0 .. 100 ms
double feedback (float v) noexcept;      // 0 .. 1
double detuneCents (float v) noexcept;   // −100 .. +100
double depthCents (float v) noexcept;    // −200 .. +200 (triangle LFO on the pitch)
double rateValue (float v) noexcept;     // 0.1 .. 400 (log); the LFO runs at rate / 2 Hz
double shelfDb (float v, bool measured = true) noexcept; // measured law: −88 .. +12 dB; round 124's: −30 .. +12 dB
float shelfValue (double db, bool measured = true) noexcept;
bool measuredEq (const EffectParams& p) noexcept;
// the shelf's magnitude (dB) at f for a corner f0 and a gain: the engine's response at 48 kHz (editor display)
double shelfResponseDb (double f, double f0, double db, bool high, bool measured) noexcept;
double freqHz (float v) noexcept;        // 16 .. 21357 Hz (log)
float freqValue (double hz) noexcept;
int sourceOf (float v) noexcept;         // 0 own side, 1 sum, 2 difference
double windowMs (float rangeV) noexcept; // grain window: 13.3 ms (80 Hz range), 53.3 ms (20 Hz range)
} // namespace gemini
} // namespace voicetools
} // namespace lynx
