#pragma once

// FL Studio's Fruity Convolver, rebuilt from the behaviour decoded in docs/analise/REA_rodada60.md (clean-room).
// The plugin turns an impulse response (a WAV embedded in its state, or a file of the user's FL installation) into a
// stereo convolution kernel — level normalisation, volume envelope, "self-convolution" blend, speed (resampling),
// EQ curve (a flat level or a linear-phase FIR from the drawn curve), silence trimming and predelay — and runs input
// matrix → convolution → dry + wet matrix with FL's per-block gain ramps. The effect's parameters are FL's 4
// automatable controls (dry, wet, input and wet stereo separation) stored as (raw − min) / (max − min); the impulse
// response travels in the slot's `state` blob, so a saved LYNX project keeps working without the FL installation.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx
{
constexpr int convolverParamCount = 4; // dry level, wet level, input stereo separation, wet stereo separation

// Reads a file of the user's FL installation by its path relative to %FLStudioFactoryData% (e.g.
// "Data\\Patches\\Impulses\\Default.wav"); empty = not available (same contract as FlDataReader)
using ConvolverFileReader = std::function<std::vector<std::uint8_t> (const std::string& relativePath)>;

// FL state → the exact effect, with the impulse response packed into slot.state (an embedded WAV, or the file read
// through `read`). nullopt = an unsupported state or feature, or an impulse response that cannot be read (the
// caller keeps its approximation).
std::optional<EffectSlot> convolverFromFl (const std::vector<std::uint8_t>& state, const ConvolverFileReader& read = {});

std::unique_ptr<Effect> makeConvolver();                                  // no impulse response: dry signal only
std::unique_ptr<Effect> makeConvolver (const std::vector<std::uint8_t>& blob); // blob = EffectSlot::state of a slot above

// Text of a parameter in FL's units ("-6.0dB", "38% separated", "Original", "100% merged")
std::string convolverParamText (int param, float value01);

namespace convolver
{
struct Range
{
    int lo, hi;
};
extern const std::array<Range, convolverParamCount> ranges; // dry 0…1000, wet 0…1000, separations −1000…1000

// One point of an FL envelope map: Δt from the previous point (seconds for the time envelopes, 0…1 for the EQ
// curve), level 0…1, tension, segment mode
struct EnvPoint
{
    double dt = 0.0, y = 0.0;
    float tension = 0.0f;
    std::uint8_t mode = 0;
};

// What the plugin keeps in its state (FUN_00875550, versions 10…15)
struct State
{
    int version = 15;
    std::array<int, 4> params { 1000, 1000, 0, 0 };  // dry, wet, input separation, wet separation (raw)
    bool fromFile = false;                           // true: `path` names the file; false: `wav` holds it
    std::string path;                                // FL path with %FLStudio…% variables
    std::vector<std::uint8_t> wav;                   // embedded RIFF/WAVE bytes
    std::array<std::vector<EnvPoint>, 5> envelopes;  // 4 time envelopes (pan?, volume, ?, ?) + the EQ curve
    std::array<std::uint8_t, 4> flags {};            // [3] = normalise
    int option = 0;                                  // the int after the flags (version > 14), 0 in every known state
    std::array<int, 5> knobs { 0, 0, 0, 0, 1000 };   // page, predelay, blend, speed, EQ amount (raw)
    std::size_t flagsAt = 0, knobsAt = 0;            // byte offsets of the flags and the knobs in the parsed state (0 = absent)
    std::size_t eqAt = 0, eqEnd = 0;                 // byte range of the EQ curve's map in the parsed state
    int knobCount = 0;                               // knobs stored (5 from version 11, else 4)
};
std::optional<State> parseState (const std::vector<std::uint8_t>& state);

// Decoded impulse response: planar float channels (1 or 2 used) and its rate
struct Audio
{
    std::vector<float> l, r;
    double rate = 44100.0;
};
// RIFF/WAVE (PCM 8/16/24/32, float 32/64, extensible) or WavPack; nullopt = not readable
std::optional<Audio> decodeAudio (const std::vector<std::uint8_t>& bytes);

// The convolution kernel FL builds at the host rate `fs` (FUN_0087b750): stereo, starting at time 0 (the predelay
// is part of it). nullopt = a feature that is not decoded (non-default first / third / fourth or a shaped volume
// envelope).
struct Kernel
{
    std::vector<float> l, r;
    int tail = 0; // samples the plugin keeps processing after the input falls silent
};
std::optional<Kernel> buildKernel (const State& s, const Audio& ir, int fs);
bool kernelSupported (const State& s) noexcept; // the features above are all decoded

// The engine on planar stereo buffers, in place.
class Engine
{
public:
    Engine();
    ~Engine();
    void setRate (int fs) noexcept;                        // the host rate (call before setParams)
    void setKernel (const Kernel& k);                      // off the audio thread; also clears the history
    void setParams (const std::array<int, 4>& raw) noexcept; // dry, wet, input separation, wet separation
    // history cleared, ramps at their targets; fresh = the gains start at 0 and glide in, as in a newly created
    // plugin whose first block is not silent
    void reset (bool fresh = false) noexcept;
    void process (float* left, float* right, int n) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// The slot blob: "LXCV" + version 1 + FL state + impulse-response file bytes (empty when embedded)
std::vector<std::uint8_t> packBlob (const std::vector<std::uint8_t>& flState, const std::vector<std::uint8_t>& irFile);
bool unpackBlob (const std::vector<std::uint8_t>& blob, std::vector<std::uint8_t>& flState, std::vector<std::uint8_t>& irFile);

// EQ curve for the editor (REA_rodada60 §7 C.7). The curve is a map over x = 0…1 (the frequency axis): points with Δx
// from the previous point (the first at 0; FL scales the Δx to a sum of 1), level 0…1 (0 = −inf, 0.5 = unchanged,
// 1 = +12 dB), tension −1…1 and segment mode (0 single curve, 1 double curve, 2 hold, 3 stairs, …, FL's envelope modes).
std::optional<std::vector<EnvPoint>> readEqCurve (const std::vector<std::uint8_t>& blob); // from a slot blob
bool writeEqCurve (std::vector<std::uint8_t>& blob, const std::vector<EnvPoint>& points); // replaces the curve in the blob
std::vector<float> eqCurveLevels (const std::vector<EnvPoint>& points, int count); // the curve at count evenly spaced x (0…1)
double eqFrequencyToX (double hz) noexcept; // the display's frequency axis: 130.8 Hz … 22 050 Hz → x (log-like, linear below)
double eqXToFrequency (double x) noexcept;
float eqLevelGain (double level) noexcept; // (3^(2·level) − 1)/2

void toParams (const std::array<int, 4>& raw, EffectParams& params) noexcept;
std::array<int, 4> fromParams (const EffectParams& params) noexcept;
} // namespace convolver
} // namespace lynx
