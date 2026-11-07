#pragma once

// FL Studio's EQUO (morphing graphic equalizer) as an exact LYNX mixer effect, rebuilt in docs/analise/REA_rodada61.md
// (clean-room: layouts, formulas and behaviour in our own words; no Image-Line code or data in this repository).
//
// EQUO splits the signal into 31 band-pass bands (20 Hz · 2^(b/3)), gives each band a volume, a pan and a send level
// drawn in one of eight banks plus a "master" bank, morphs between the banks and sums the bands back.
//
// The effect slot (EffectKind::equo) keeps FL's own saved state (plugin record 0xD5) in EffectSlot::state; the
// knobs live in the slot parameters so they can be edited and automated:
//   [0] band shift (FL −12000…12000), [1] band mix (−128…128), [2] morph (0…7000), [3] motion smoothness (0…1024),
//   [4] output volume (0…256): each (raw − min) / range;
//   [5] bandwidth (80…500, (raw − 80) / 420), [6] 2× oversampling, [7] add mode, [8] send only (on ≥ 0.5).

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

namespace lynx::equofx
{
constexpr int numBands = 31, numTargets = 3, numBanks = 9; // targets: volume, panning, send; bank 8 = master
constexpr int numParams = 9;

struct State
{
    struct Target
    {
        float value = 1.0f;
        int mode = 0; // 0 = on (drawn banks), 1 = off, 2 = locked to `value`
    };
    int version = 4;
    std::array<std::array<Target, numTargets>, numBands> target {};
    std::array<float, numBanks * numBands * numTargets> table {}; // [bank][band][target], normalised 0…2
    std::array<int, 5> knob { 0, 128, 0, 170, 128 };               // shift, mix, morph, smoothness, volume (raw)
    int oversample = 0;  // ×2^oversample around the whole EQ
    int bandwidth = 150; // percent of an octave … (raw 80…500)
    int sendTrack = -1;  // the "send" levels' mixer insert minus 1 (−1 = none; FL hands the send to insert sendTrack + 1)
    bool sendOnly = false, addMode = false;

    float& cell (int bank, int band, int t) noexcept { return table[(std::size_t) ((bank * numBands + band) * numTargets + t)]; }
    float cell (int bank, int band, int t) const noexcept { return table[(std::size_t) ((bank * numBands + band) * numTargets + t)]; }
};

// FL's saved state; nullopt = not an EQUO state (versions 3 and 4 are read)
std::optional<State> parse (const std::vector<std::uint8_t>& state);

// the state's send track as a mixer insert (1…; 0 = none) and back (FL keeps the insert minus 1, −1 = none: §2.1)
int sendInsert (const std::vector<std::uint8_t>& state) noexcept;
bool setSendInsert (std::vector<std::uint8_t>& state, int insert) noexcept; // false = not an EQUO state

// slot parameters ⇄ the knobs and options of a state
void toParams (const State& s, EffectParams& params) noexcept;
void applyParams (const EffectParams& params, State& s) noexcept;
std::string paramText (int param, float v);

class Engine
{
public:
    Engine();
    ~Engine();
    // hqRender: FL's "HQ non-realtime" process mode (the plugin then designs its bands for 2× the rate)
    void prepare (double sampleRate, bool hqRender = false) noexcept;
    void set (const State& s) noexcept; // a newly loaded state (as after FL's state load)
    void setKnobs (const State& s) noexcept; // knob / option changes on the running engine
    void setBands (const State& s) noexcept; // band table / band mode changes on the running engine (values glide)
    void reset() noexcept;              // as freshly loaded: filters cleared, gains ramp in from zero (smoother kept)
    void flush() noexcept;              // FL's Flush message: filters cleared, gains jump to their targets
    // stereo, in place, any n (FL blocks of 512); the send (when a send track is set) is added into sendLeft / sendRight
    void process (float* left, float* right, int n, float* sendLeft = nullptr, float* sendRight = nullptr) noexcept;

    // motion smoothing: FL calls the plugin once per song tick (samples per tick; 0 = no ticks); tick() = one call
    void setSamplesPerTick (double samplesPerTick) noexcept;
    void tick() noexcept;
    // the smoother's states as FL's constructor leaves them (1, 0, 1). A new engine starts there and set() / reset() do
    // not touch them (FL's render of the render test, REA_rodada170i §8); this puts them back (EXEC checks)
    void startFromConstruction() noexcept;

    // band b's current volume / pan / send values (after mix, shift, morph and the master bank)
    float bandValue (int b, int t) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& flState);
} // namespace lynx::equofx
