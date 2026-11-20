#pragma once

// LYNX Model's modal core (round 190ni): a bank of damped modes, each a rotating phasor z <- p z (+ d f), |p| < 1,
// whose imaginary part is the mode's output. Structure of arrays, 8 modes per lane group, run with SSE (x86-64
// baseline; NEON through sse2neon on ARM64) or AVX where the CPU has it (core/CpuFeatures.h). Both paths do the same
// IEEE operations in the same order per mode (no FMA, a fixed lane-sum order), so they render the same bits.

#include <cstdint>

namespace lynx::lm
{
constexpr int maxModes = 48;  // per voice: six groups of eight (round 190nl; 32 before: the bells' and the kalimba's
                              // long, quiet upper partials did not fit)
constexpr int laneGroup = 8;
constexpr int maxBlock = 256; // samples per runModes call

struct alignas (32) ModalBank
{
    float re[maxModes] {}, im[maxModes] {};    // the state (z)
    float pr[maxModes] {}, pi[maxModes] {};    // the pole p = r e^(i w)
    float drive[maxModes] {};                  // the force's gain into Re z
    int groups = 0;                            // groups of 8 modes in use (unused lanes have p = 0, drive = 0)
};

// Adds the modes' summed output (sum of Im z after each step) to out[0..n), n <= maxBlock. With `force` (n values)
// each mode is driven: z <- p z + drive * force[i].
void runModes (ModalBank& bank, const float* force, float* out, int n) noexcept;
// The same on the portable path only (tests: both paths must give the same bits)
void runModesBaseline (ModalBank& bank, const float* force, float* out, int n) noexcept;
// Whether runModes takes the AVX path on this CPU
bool modesUseAvx() noexcept;
// The bank's energy (sum of |z|^2), in a fixed order
float modalEnergy (const ModalBank& bank) noexcept;
} // namespace lynx::lm
