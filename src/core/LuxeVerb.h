#pragma once

// FL Studio's LuxeVerb, rebuilt from the behaviour decoded in docs/analise/REA_rodada60.md (clean-room). The plugin
// (Plugins/Fruity/Effects/LuxeVerb) is a Delphi host around the "shimmer reverb" engine of FL's shared DSP library
// (Shared/ildsp_x64.dll): input gain and filters, an envelope follower, a modulated predelay with a two-stage
// all-pass diffuser, a 16-line feedback delay network with a rotation matrix and per-line damping, a feedback path
// through a granular pitch shifter (or two feed-forward shifters), an output peak filter, a soft limiter, stereo
// width and dry/wet. The effect's parameters are FL's 49 controls stored as (raw − min) / (max − min) plus the four
// tempo-sync switches, so the slot stays editable and maps back to FL's integers exactly.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx
{
constexpr int luxeVerbControlCount = 49;
constexpr int luxeVerbParamCount = luxeVerbControlCount + 4; // + sync of predelay, feedback delay, feed-forward 1 / 2
// Round tp172b: the "Space" engine's controls follow FL's (core/SpaceVerb.h, the Pro-R role). They are off by default,
// so the FL-exact sound and FL's state mapping (the first luxeVerbParamCount values) are unchanged.
constexpr int luxeVerbSpaceFirst = luxeVerbParamCount;
constexpr int luxeVerbSpaceCount = 15;
constexpr int luxeVerbAllParamCount = luxeVerbSpaceFirst + luxeVerbSpaceCount;

std::unique_ptr<Effect> makeLuxeVerb();

// FL state (versions 7…9) → the effect's parameters; nullopt if the layout is not recognised
std::optional<EffectSlot> luxeVerbFromFl (const std::vector<std::uint8_t>& state);

// Text of a parameter in FL's units
std::string luxeVerbParamText (int param, float value01);

namespace luxeverb
{
// the Space engine's parameters (positions from luxeVerbSpaceFirst) and their laws
namespace sp
{
enum : int { engine, style, space, rate, distance, brightness, character, thickness, width, predelay, mix, lowMult, lowFreq, highMult, highFreq };
extern const std::array<const char*, luxeVerbSpaceCount> names;
extern const std::array<float, luxeVerbSpaceCount> defaults;
double decaySeconds (float v) noexcept;   // "Space": 0.3 … 10 s (mid-band RT60 at 100 % rate)
float decayValue (double seconds) noexcept;
double rateOf (float v) noexcept;         // 25 … 400 % (×0.25 … ×4)
double predelayMs (float v) noexcept;     // 0 … 500 ms (cubic)
double multOf (float v) noexcept;         // decay-EQ multiplier ×0.25 … ×4
double lowHz (float v) noexcept;          // 20 … 2000 Hz
double highHz (float v) noexcept;         // 500 … 20000 Hz
std::string text (int index, float v);
} // namespace sp

struct Range
{
    int lo, hi;
};
extern const std::array<Range, luxeVerbControlCount> ranges; // FL's control ranges
extern const std::array<const char*, luxeVerbParamCount> names;

struct Settings
{
    std::array<int, luxeVerbControlCount> raw {};
    std::array<bool, 4> sync {}; // predelay, feedback delay, feed-forward delay 1, feed-forward delay 2

    Settings();
    bool operator== (const Settings&) const = default;
};

Settings fromParams (const EffectParams& params) noexcept;
void toParams (const Settings& s, EffectParams& params) noexcept;
std::optional<Settings> parseState (const std::vector<std::uint8_t>& state);

// The plugin's mapping of a control's integer to the engine parameter it sends (REA_rodada60 §3.2)
float engineValue (int control, int raw) noexcept;

// The plugin with its engine, on separate stereo buffers in place. The sidechain (envelope source when the
// "Envelope sidechain" switch is on) defaults to the input.
class Engine
{
public:
    Engine();
    ~Engine();
    // creation, sample rate, state (FL's order: rate, all 49 controls, tempo with the sync choices)
    void prepare (int sampleRate, const Settings& s, double bpm);
    // the same in two steps: create() allocates (not for the audio thread), load() only computes
    void create (int sampleRate);
    void load (const Settings& s, double bpm);
    void flush() noexcept; // FL's reset: clears the lines, filters and the feedback shifter
    void set (const Settings& s, double bpm); // control / switch / tempo changes (FL's ProcessParam / tempo event)
    void process (float* left, float* right, int n, const float* scLeft = nullptr, const float* scRight = nullptr) noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> d;
    Settings cur;
    double curBpm = 0.0;
};
} // namespace luxeverb
} // namespace lynx
