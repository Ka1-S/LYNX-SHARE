#pragma once

// FL Studio's sampler interpolation (REA_rodada69 §1): the read-position interpolators FL's native sampler channels and
// audio clips use (and that FPC, Fruity Slicer and Slicex carry in their own copies): 2-point linear, 6-point
// fifth-order polynomial ("6-point Hermite") and windowed sinc with 16…512 points. Quality = FL's interpolation index
// 1…9 (1 linear, 2 Hermite, 3…9 sinc 16, 24, 32, 64, 128, 256, 512 points); FL plays with index 4 (24-point sinc) and
// renders with index 5 (32-point sinc) unless the user changes them. The kernels reproduce FL's arithmetic (x87
// extended precision for the linear/Hermite kernels and the sinc seeds, SSE single precision with the reciprocal
// estimate for the sinc sums), so on the same CPU they give FL's bits; the sine of the seeds is the C library's (a
// guarded polynomial that gives the same float seeds, round 190kz).

#include <cstdint>

namespace lynx::flrs
{
constexpr int liveQuality = 4;   // FL's playback default (24-point sinc)
constexpr int renderQuality = 5; // FL's render dialog default (32-point sinc)

// half-width of the kernel for an interpolation index (1 = linear, 3 = Hermite, ≥ 8 = sinc with 2·N points)
int halfWidthFor (int quality) noexcept;

// Per-voice kernel set-up for a read speed (frames of the sample per output frame). FL widens the sinc to
// round(|speed|) (rounded up to a multiple of 4, at most 512) when that exceeds the base width, and lowers the cutoff
// to π/|speed| when the speed is above 1.
struct Kernel
{
    int baseHalfWidth = 12;
    int halfWidth = 12;
    double speed = -1.0;
    double cutoff = 3.141592653589793, twoCosCutoff = -2.0; // w and 2·cos w
    double step = 0.2617993877991494, twoCosStep = 0.0;     // π/N and 2·cos(π/N)

    void setQuality (int quality) noexcept;
    void setSpeed (double speed) noexcept;
};

// Source frames around a read position: taps t = 0 … 2N−1 hold frames i−N+1 … i+N (i = the integer part of the
// position, N = the kernel's half-width), already clamped/wrapped by the caller.
// Writes the interpolated left/right frame for the fractional part `frac` (0 ≤ frac < 1). Call it with the x87 unit at
// 64-bit precision (flvp::X87Extended in scope), as FL runs it.
void interpolate (const Kernel& k, const float* tapsL, const float* tapsR, double frac, float& outL, float& outR) noexcept;

// The sinc kernel's eight float seeds from the sines at a0…a3 (round 190kz): a guarded polynomial sine whose seeds are the
// library sines' bit for bit, or (plain) the library sines themselves — the two give the same floats (tests)
void sincSeeds (const Kernel& k, double a0, double a1, double a2, double a3, float* seed, bool plain = false) noexcept;

// The read speed FL computes for a pitch in cents and a rate ratio (sample rate / output rate):
// exactly `ratio` at 0 cents, else exp(cents · ln2/1200) · ratio.
double speedFor (float cents, double ratio) noexcept;

// FL's integer part of a read position (truncation) and its fraction.
inline std::int64_t wholeFrames (double position) noexcept { return (std::int64_t) position; }
} // namespace lynx::flrs
