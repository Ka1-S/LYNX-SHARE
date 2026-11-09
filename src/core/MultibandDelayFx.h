#pragma once

// FL Studio's Multiband Delay as an exact LYNX mixer effect, rebuilt in docs/analise/REA_rodada71.md (clean-room:
// layouts, formulas and behaviour in our own words; no Image-Line code or data in this repository).
//
// The plugin splits the signal into 16 bands (crossover "gentle" / "steep" IIR or linear phase), sends each band
// through its own delay line with feedback and a volume / pan stage, and sums the bands back through a 10 Hz…1 kHz
// high pass and an optional compressor / saturator / clipper, then mixes with the (latency-aligned) dry signal.
// Eight banks hold a delay time, a volume and a pan per band; "morph" interpolates between neighbouring banks.
//
// The effect slot (EffectKind::multibandDelay) keeps FL's own saved state (plugin record 0xD5) in EffectSlot::state;
// the twelve global controls live in the slot parameters ((raw − min) / range) so they can be edited and automated:
//   [0] delay scale (−6000…6000)  [1] delay ×0.1 (0/1)  [2] feedback (0…6000)  [3] smoothing time (0…6000)
//   [4] filter mode (0 gentle, 1 steep, 2 linear phase)  [5] keep pitch (0/1)  [6] morph (0…7000)
//   [7] wet/dry mix (−128…128)  [8] harmonic scale (0/1)  [9] weighted feedback (0/1)
//   [10] high-pass frequency (1000…100000)  [11] wet saturation (0 comp, 1 saturate, 2 clip)

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
class Effect;
}

namespace lynx::mbdelay
{
constexpr int numBands = 16, numBanks = 8;
constexpr int numEngineParams = 396; // 8 banks × (16 delays, 16 volumes, 16 pans) + 12 global controls
constexpr int numGlobals = 12;       // slot parameters (see above)

struct State
{
    int version = 7;
    std::array<int, numEngineParams> raw {}; // FL's raw control values: bank cells ×2^30, globals in their own ranges
    int targetSel = 0, bank = 7;             // editor: selected target and bank (FL re-sends that bank's cells clamped)
    std::uint8_t flagA = 1, flagB = 1, flagC = 1;
    std::array<std::array<int, 3>, numBands> mode {}; // per band and target (delay, volume, pan): editor mode, 0 = on

    State();
};

// FL's saved state (versions 1…7 are read); nullopt = not a Multiband Delay state
std::optional<State> parse (const std::vector<std::uint8_t>& state);
std::vector<std::uint8_t> serialize (const State& s);

// the value FL's front end hands the engine for parameter i after a state load (bank cells raw / 2^30, those of the
// selected bank clamped to 0…1; globals (raw − min) / range)
float engineValue (const State& s, int i) noexcept;

// slot parameters ⇄ the global controls of a state
void toParams (const State& s, EffectParams& params) noexcept;
void applyParams (const EffectParams& params, State& s) noexcept;
std::string paramText (int param, float v);

class Engine
{
public:
    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    void setSampleRate (double sampleRate);           // allocates; as FL's sample-rate message
    void setParameter (int index, float value) noexcept; // engine parameter 0…395, 0…1
    void load (const State& s) noexcept;              // every parameter of a state, in order (as FL's state load)
    void suspend() noexcept;                          // clears the delay lines, the linear-phase buffers and the high pass
    void process (float* left, float* right, int n) noexcept; // stereo, in place
    int latencySamples() const noexcept;              // 1536 in linear-phase mode, else 0

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// `params` (may be null): the slot's parameters, so the mode's latency is known before the first block (issue #113)
std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& flState, const EffectParams* params = nullptr);
} // namespace lynx::mbdelay
