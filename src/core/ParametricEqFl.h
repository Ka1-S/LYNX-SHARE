#pragma once

// FL Studio's Fruity Parametric EQ 2 and Fruity Parametric EQ (v1) as exact engines, rebuilt in
// docs/analise/REA_rodada50.md (clean-room: layouts, formulas and behaviour in our own words; no Image-Line code or
// data in this repository).
//
// Both run inside LYNX's 7-band parametric EQ (EffectKind::parametricEq) so its editor keeps working. A slot imported
// from FL carries an "FL law" marker in parameter [36]; the band parameters then hold FL's own values:
//   [4b+0] band type (peq::BandType, a one-to-one image of FL's types)
//   [4b+1] frequency: FL's raw value / 65536 (Parametric EQ 2: the same 20 Hz…20 kHz law as LYNX's EQ)
//   [4b+2] gain: raw / 3600 + 0.5 (±18 dB, the same law as LYNX's EQ)
//   [4b+3] width ("bandwidth"): raw / 65536
//   [28]   main level: raw / 3600 + 0.5 (Parametric EQ 2)
//   [29+b] order: (order + 3) / 6, order −3…+3 = Steep 8 … 2 … Gentle 8 (Parametric EQ 2)
//   [36]   law: 0 = LYNX's own EQ, 0.5 = Parametric EQ 2, 1 = Parametric EQ (v1)
//   [37]   processing: 0 = plain, 1/3 = HQ (×2 oversampling), 2/3 = HQ through Frequency Splitter's resampler,
//          1 = HQ "alternative design" (REA_rodada50 §4)
//   [38]   linear phase (Parametric EQ 2 option; REA_rodada50 §4)
//   [39]   band enable mask / 127 (each band's mute button)
//   [40]   Parametric EQ 2, linear phase: FFT quality / 7;  [41] cross-fade time index / 3
//   [40+b] Parametric EQ (v1): FL's raw frequency / 65536 (its own 10 Hz…16 kHz law), used while [4b+1] still equals
//          the LYNX value of that frequency (i.e. until the band is moved in the editor)
//   [47]   Parametric EQ (v1): FL's raw main level / 256 (used while [28] still matches it)

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/Song.h"

namespace lynx::peqfl
{
class LinearPhase;
}

namespace lynx::peqfl
{
enum class Law { lynx = 0, peq2 = 1, peq1 = 2 };
enum class Processing { plain = 0, hq = 1, hqExternal = 2, hqAlternative = 3 };

constexpr int lawParam = 36, processingParam = 37, linearParam = 38, maskParam = 39, v1FreqParam = 40, v1MainParam = 47;
constexpr int linearQualityParam = 40, linearFadeParam = 41; // Parametric EQ 2 law only

// FL's controls as integers (the plugin's own saved values)
struct Settings
{
    Law law = Law::peq2;
    std::array<int, 7> gain {}, freq {}, width {}, type {}, order {}; // type = FL's UI type 0…7, order −3…3
    int main = 0;                                                     // PEQ2 −1800…1800; PEQ1 raw (128 = 0 dB)
    Processing processing = Processing::plain;
    bool linearPhase = false;
    int linearQuality = 2, linearFade = 0; // linear phase: FFT size 512·2^quality, cross-fade time index (§5)
    std::uint32_t mask = 0x7F;
};

// FL's saved state (plugin record 0xD5) of each plugin; nullopt = not a state of that plugin
std::optional<Settings> parsePeq2 (const std::vector<std::uint8_t>& state);
std::optional<Settings> parsePeq1 (const std::vector<std::uint8_t>& state);

Law lawOf (const EffectParams& params) noexcept;
void toParams (const Settings& s, EffectParams& params) noexcept;
Settings fromParams (const EffectParams& params) noexcept; // law must not be Law::lynx

// One biquad stage of a band: FL's internal type, coefficients (a0 = 1) and Direct-Form-I state
// (x1 L/R, x2 L/R, y1 L/R, y2 L/R)
struct Stage
{
    int type = 0;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double s[8] {};
};

struct Band
{
    int type = 0;   // FL's internal type (0 off, 1 LP, 2 BP, 3 notch, 4 HP, 6 low shelf, 7 peaking, 8 high shelf)
    int active = 0; // 0 = not processed, −1 = always, > 0 = frames left (a peaking/shelf band at 0 dB)
    int stages = 0;
    std::array<Stage, 4> stage {};
};

// Designs band b (coefficients only; the stage states are left as they are) for the given processing rate
void designBand (const Settings& s, int b, double rate, Band& band);

// The plugin's main-level stage, interleaved stereo in place: a change glides over the block by at most maxStep per
// frame (FL's float ramp); snap = start at the target (first block after a reset)
void mainLevel (float* io, int n, float target, float& now, float maxStep, bool snap) noexcept;

// Magnitude response of the whole EQ in dB at frequency hz, as the engine runs it at the host rate sr
double responseDb (const Settings& s, double sr, double hz);

class Engine
{
public:
    Engine();
    ~Engine();
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    // new settings; states are kept unless a band's type or order changed (as FL does)
    void set (const Settings& s) noexcept;
    // stereo, in place; any n (split into FL-sized blocks of 512 frames)
    void process (float* left, float* right, int n) noexcept;
    // delay the plugin reports to its host (the external resampler's FIR)
    int latencySamples() const noexcept;

private:
    void block (float* io, int n) noexcept; // interleaved, n ≤ 512
    void flushFilters() noexcept;
    double processingRate() const noexcept;

    Settings settings;
    bool configured = false;
    double sampleRate = 44100.0;
    int silenceFrames = 8820;         // FL: rate / 5
    int silenceCount = (int) 0xC0000001;
    float mainGain = 1.0f, gainNow = 1.0f;
    bool gainSnap = true;
    std::unique_ptr<LinearPhase> linear; // linear-phase mode (core/ParametricEqLinear.h)
    std::array<Band, 7> bands {};
    double up[24] {}, down[24] {};    // half-band states (×2 oversampling)
    float ring[2][256] {};            // external decimator's input history per channel (§2.9)
    int ringIndex[2] {};
    std::vector<float> buffer, wide;
};
// Round 190kr test hook: the engine's sample-by-sample cascade of stages renders exactly what the plain per-stage
// loop renders (random stages, states and blocks); true when every output and state is bit-identical
bool stagesMatchReference (std::uint32_t seed) noexcept;
} // namespace lynx::peqfl
