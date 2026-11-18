#pragma once

// Round 190lf: the Limiter's L2 law (docs/analise/REA_rodada190lf.md §2). The REA of the Waves L2 DSP showed its
// structure (a look-ahead brickwall: the gain each sample needs, a running minimum over the look-ahead window, a
// slower follower of that minimum, a release stage whose speed follows the depth and the follower — ARC — and a
// trapezoid smoothing of the gain over the window); every number below is LYNX's own, fitted on our black-box renders
// of the Waves demo (cap/l2d probes: gain trajectories after bursts of 5 ms … 4 s at 3 … 20 dB of reduction).
// The law sits behind a hidden parameter: 0 = the classic Limiter, exactly as every project saved before has it.

namespace lynx::limiterlaw
{
constexpr int lawParam = 3;   // hidden: 0 = classic, l2LawValue = the L2 law
constexpr int arcParam = 4;   // hidden, L2 law only: >= 0.5 = the programme-dependent release (else the Release knob)
constexpr int trimParam = 5;  // hidden, L2 law only: v × 24 dB taken off the Gain knob (L2 thresholds above the ceiling)
constexpr float l2LawValue = 1.0f / 16.0f;
inline bool isL2 (float v) noexcept { return v >= 0.5f / 16.0f; }

// structure at 48 kHz (scaled with the rate): window = latency = 64 samples, the smoothing = two moving averages of 27
// and 38 samples (window = 27 + 38 − 1, so the smoothed gain reaches the needed one when the peak comes out)
constexpr int windowAt48k = 64, boxAAt48k = 27, boxBAt48k = 38;
// the make-up is min (ceiling, 0.999) / threshold (at Ceiling 0 dB the small-signal gain is 0.0087 dB below the threshold's)
constexpr double ceilingMax = 0.999;
// manual release: the Release time is the 90 % recovery of the linear gain (time constant = ms / ln 10)
constexpr double manualTimeScale = 0.43429448190325176;
// ARC: a follower of the window's minimum (attack / release time constants, linear gain) and the release rate of the
// gain d (per second) exp (q0 + q1 u + q2 v + q3 u v + q4 u² + q5 v²), u = ln d (clamped at −24 dB), v = (follower − d)
// / follower: fast after short peaks (the follower stayed high), slow after sustained limiting (lab/fitarc.py on 8
// probes: 0.061 dB rms over every sample after the 1.5 s lead; the scored refcap182 programme was not used)
constexpr double arcFollowAttackS = 0.10743, arcFollowReleaseS = 0.21765;
constexpr double arcLaw[6] = { 9.3181, 9.6655, 6.9906, -6.9001, 0.9427, -1.2230 };
constexpr double arcDepthFloor = -2.7631021115928547; // ln (10^(−24/20))
} // namespace lynx::limiterlaw
