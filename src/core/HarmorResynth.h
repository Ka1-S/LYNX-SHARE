#pragma once

// Harmor's resynthesis (the IMG section with a sample loaded, image object mode 1 / part +0x5c = 2), round
// natives190kj (docs/analise/REA_rodada190kj-harmor.md): the sample analysis FL's Harmor_x64.dll runs when it loads
// the sample (FUN_00794510: resampled so that 2048 points span C0, Kaiser windows of two widths, magnitudes and phases
// per 5.8 ms column, the frequency axis decimated to 255 harmonics of C2, pixel law; FUN_00795720: peaks, their
// regions, onsets and the frequency plane), JUCE-free. The voice side lives in Harmor.cpp.
//
// The sample itself comes through a loader the host installs (the app resolves FL's path variables and decodes the
// file); without one, plain WAV files are read here. Analyses are cached per path.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lynx::harmor
{
struct ResynthPlanes
{
    int cols = 0, rows = 0, stride = 0, hop = 0;
    double rate = 0.0;   // analysis rate (+0x140): 2048 · C0
    double msCol = 0.0;  // milliseconds per column (+0x130)
    double k148 = 0.0;   // pitch of harmonic 1 relative to C5, cents (+0x148)
    double durMs = 0.0;  // the sample's length (+0xd8)
    float k38 = 1.0f;    // the pixel law's reference (+0x38: the weighted magnitude maximum)
    float k104 = 1.0f;   // frequency plane scale (+0x104)
    std::vector<float> gain;    // plane 0: pixels 0…255, cols × stride
    std::vector<float> freqRaw; // plane 1 before its normalisation (the pitch track reads it)
    std::vector<float> freq;    // plane 1: frequency deviation, −1…1
    std::vector<float> adv;     // +0x168: the phase advance of harmonic i over one column, cycles (stride entries)
    std::vector<float> phase;   // plane 2: code + phase (codes: 2 + the peak a bin follows, 522 onset, 523 return, 524 peak)
};

// The part's resynthesis options (part misc +0x20 → part +0x240, FUN_00794260; misc +0x24 → part +0x244)
struct ResynthOptions
{
    int mode = 0;   // 0: harmonics of C2 (every 4th bin, Kaiser windows); 1: harmonics of C1 (every 2nd bin, Kaiser + Hamming)
    int soft = 250; // soft knee: image +0x15c = (soft / 250)²
    bool operator== (const ResynthOptions& o) const noexcept { return mode == o.mode && soft == o.soft; }
};

// The analysis of a mono sample at `rate` Hz (FUN_00794510 + the resynthesis branch of FUN_00795720).
std::shared_ptr<const ResynthPlanes> analyseResynthesis (const float* mono, std::size_t frames, double rate,
                                                         const ResynthOptions& options = {});

struct SampleData
{
    std::vector<float> mono; // channels mixed as FL does (mean)
    double rate = 0.0;
};

// path as saved in the state (FL's path variables included) → the sample; false when it cannot be found / read
using SampleLoader = std::function<bool (const std::string& savedPath, SampleData& out)>;
void setSampleLoader (SampleLoader loader);

// The cached analysis of the sample a state names (null when there is no loader result).
std::shared_ptr<const ResynthPlanes> resynthesisFor (const std::string& savedPath, const ResynthOptions& options = {});

// Plain RIFF/WAVE reader (8/16/24/32-bit PCM, 32-bit float), channels mixed as FL does.
bool readWavMono (const std::string& path, SampleData& out);
bool readWavMono (const std::vector<std::uint8_t>& bytes, SampleData& out);
} // namespace lynx::harmor
