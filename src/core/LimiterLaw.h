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
// Round 190mg: IDR, the L2's output requantizer (docs/analise/REA_rodada190mg_dyn.md §2), as LYNX measured it on the demo:
// the output rounded to the Quantize word length (24 / 22 / 20 / 18 / 16 bits) through an error-feedback noise shaper
// (none, moderate, normal, ultra: our minimum-phase fits of the measured noise spectra, below) with, at Type 1, a
// triangular (TPDF) random dither of ±1 LSB — the white error of every Type 1 render is 0.250 LSB² (TPDF 1/6 + the
// rounding's 1/12); with no shaping, digital silence stays silent. The other two dither settings add no random dither
// (the error follows the signal, as the reference's does: measured, the exact sequence is not modelled). Hidden, L2
// law only, all 0 = no requantizer (every project before): [6] bits / 32, [7] (dither setting + 1) / 4 (the state's
// value: 1 = Type 1), [8] (shaping + 1) / 8 (0 none … 3 ultra).
constexpr int ditherBitsParam = 6, ditherTypeParam = 7, ditherShapeParam = 8;
// l2dither:begin (lab/gen_l2d.py: moderate: white 0.2493 LSB², shape 0.26 dB rms (0.87 max), total 1.02 LSB²; normal: white 0.2501 LSB², shape 0.24 dB rms (0.56 max), total 3.95 LSB²; ultra: white 0.2496 LSB², shape 0.56 dB rms (1.15 max), total 15.25 LSB²)
constexpr int ditherTaps = 48;
constexpr double ditherShaping[3][ditherTaps] {
    { -1.347935, 0.992230, -0.116241, -0.279292, 0.316107, 0.102850, -0.157444, 0.087655, 0.132179, -0.068916, 0.030200, 0.079960, 0.015810, -0.028028, 0.069495, 0.022048, -0.012503, 0.035305, 0.031419, -0.006215, 0.017981, 0.026832, 0.003705, 0.007864, 0.019372, 0.005687, 0.008052, 0.009367, 0.011225, 0.005144, 0.008158, 0.005998, 0.006577, 0.004279, 0.009545, 0.000806, 0.007997, 0.002974, 0.004263, 0.002318, 0.007274, 0.001397, 0.001150, 0.009072, -0.002451, 0.002069, 0.007534, -0.001816 },
    { -2.073634, 2.470766, -1.669353, 0.453585, 0.587751, -0.691509, 0.250813, 0.312762, -0.378049, 0.151909, 0.192677, -0.227364, 0.094193, 0.125159, -0.126110, 0.033184, 0.116648, -0.104000, 0.031480, 0.089975, -0.087631, 0.041953, 0.047592, -0.047160, 0.020036, 0.042958, -0.041477, 0.025833, 0.022400, -0.022099, 0.012374, 0.024581, -0.026181, 0.020137, 0.010454, -0.012182, 0.008645, 0.015154, -0.014002, 0.010074, 0.008983, -0.006297, 0.003760, 0.011954, -0.010215, 0.010587, 0.000474, 0.000855 },
    { -2.625055, 4.105793, -4.315016, 3.161046, -1.045332, -0.763382, 1.542042, -1.060474, 0.064682, 0.747228, -0.796364, 0.299137, 0.351381, -0.614178, 0.463573, -0.055607, -0.201121, 0.197032, 0.046870, -0.243420, 0.267176, -0.070874, -0.145313, 0.255851, -0.165816, -0.003768, 0.146967, -0.142302, 0.040398, 0.092127, -0.138584, 0.097787, 0.005437, -0.073265, 0.078824, -0.019211, -0.035018, 0.054904, -0.017167, -0.032074, 0.065334, -0.047781, 0.007240, 0.031350, -0.032114, 0.007900, 0.026275, -0.038465 },
};
// l2dither:end

} // namespace lynx::limiterlaw
