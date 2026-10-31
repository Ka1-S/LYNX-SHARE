#pragma once

// FL Studio's Fruity Reeverb 2, rebuilt from the behaviour decoded in docs/analise/REA_rodada49.md (clean-room). The
// plugin is a thin host around the "full" process of FL's shared plate reverb (Shared/Reverb_x64.dll): mono input
// (L + R, or L − R with the Mid/Side option), the plate network of core/PlateReverb.h plus a modulated delay in each
// tank half and an early-reflection tap line, then dry / early-reflection / wet gains with a stereo-separation
// matrix and FL's per-block gain ramps. The effect's parameters are FL's own 15 controls plus the two option flags,
// each stored as (raw − min) / (max − min), so the slot stays editable and maps back to FL's integers exactly.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx
{
constexpr int reeverb2ParamCount = 17; // 15 controls + tempo-synced predelay + Mid/Side input

std::unique_ptr<Effect> makeReeverb2();

// FL state (version 10000/10001 or the old unversioned layout) → the effect's parameters; nullopt if not supported
std::optional<EffectSlot> reeverb2FromFl (const std::vector<std::uint8_t>& state);

// Text of a parameter in FL's units
std::string reeverb2ParamText (int param, float value01);

// Fruity Reeverb (the first version, REA_rodada49 §6): its own Delphi copy of the plate without early reflections or
// modulation — 10 controls (low cut, high cut, predelay, room, diffusion, colour, decay, high damping, dry, wet) kept
// as raw / max.
constexpr int reeverb1ParamCount = 10;
std::unique_ptr<Effect> makeReeverb1();
std::optional<EffectSlot> reeverb1FromFl (const std::vector<std::uint8_t>& state);
std::string reeverb1ParamText (int param, float value01);

namespace reeverb1
{
extern const std::array<int, reeverb1ParamCount> maxima; // FL's control ranges (all start at 0)
using Raw = std::array<int, reeverb1ParamCount>;
Raw fromParams (const EffectParams& params) noexcept;

class Engine
{
public:
    Engine();
    ~Engine();
    void prepare (int sampleRate, const Raw& raw); // creation, rate, state and FL's flush
    void reserve (int sampleRate, int maxBlock); // the buffers for that rate, ahead of prepare (UI thread)
    void flush() noexcept;
    void set (const Raw& raw);
    void process (float* left, float* right, int n) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace reeverb1

namespace reeverb2
{
struct Range
{
    int lo, hi;
};
extern const std::array<Range, reeverb2ParamCount> ranges;

struct Settings
{
    std::array<int, 15> p { 75, 40, 0, 50, 100, 15, 40, 100, 500, 0, 128, 64, 64, 684, 0 };
    bool tempoPredelay = false, midSide = false;

    bool operator== (const Settings&) const = default;
};

Settings fromParams (const EffectParams& params) noexcept;
void toParams (const Settings& s, EffectParams& params) noexcept;

// The plugin (FL's Eff_Render path) on interleaved-free stereo buffers, in place.
class Engine
{
public:
    Engine();
    ~Engine();
    // creation + state load, then FL's flush (without it the gains fade in from 0 as in a freshly created plugin)
    void prepare (int sampleRate, const Settings& s, double bpm, bool flushed = true);
    // The memory prepare() / a predelay change would take, reserved now (off the audio thread; round 167e): the first
    // block and predelay automation then never allocate (predelay up to 2 s: tempo-synced at 60 BPM and above)
    void reserve (int sampleRate, int maxBlock);
    void flush() noexcept;                                         // FL's flush: clears the reverb, ends the ramps
    void set (const Settings& s, double bpm);                      // parameter / tempo changes (FL's ProcessParam)
    void process (float* left, float* right, int n) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace reeverb2
} // namespace lynx
