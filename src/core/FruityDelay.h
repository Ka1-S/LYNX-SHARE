#pragma once

// FL Studio's Fruity Delay 2 and Fruity Delay 3 as exact LYNX mixer effects (docs/analise/REA_rodada52.md; clean-room:
// rebuilt from the observed behaviour of the plugins, no Image-Line code). Both effects keep FL's raw controls as
// their parameters (value over the control's range, like the FL import reads them from the saved state), so the
// LYNX editor can still change them.
//
// Delay 2: tempo-synced stereo delay (time in 1/192 of a beat), input pan/volume, dry level, feedback with a
// one-pole low-pass in the loop, normal / swapped / ping-pong routing and a stereo time offset.
// Delay 3: the third-party "Delay VST" engine FL hosts (send, delay model, time with or without tempo sync, time
// smoothing or pitch-keeping crossfades, modulation LFO, diffusion all-passes, feedback filter, distortion,
// sample-rate and bit reduction, output colour, stereo spread, wet and dry).

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/Effects.h"

namespace lynx::fldelay
{
// --- Fruity Delay 2 ---------------------------------------------------------------------------------------------------
constexpr int delay2Controls = 8;
extern const std::array<int, delay2Controls> delay2Min, delay2Max, delay2Default;
const char* delay2ParamName (int i) noexcept;

struct Delay2State
{
    std::array<int, delay2Controls> v {}; // input pan, input volume, dry, feedback, time, stereo offset, mode, cutoff
};
std::optional<Delay2State> parseDelay2 (const std::vector<std::uint8_t>& state);

class Delay2Engine
{
public:
    // maxBlock: the longest block process() will be given (it sizes the delay line like FL's host block).
    // A new instance fades its input in (FL's gain ramps start at 0); `settled` starts at the targets instead.
    Delay2Engine (const Delay2State& s, double sampleRate, double bpm, int maxBlock = 512, bool settled = false);
    // New controls / tempo: gains, feedback, cutoff and mode change in place (ramped like FL); a new delay length
    // or stereo offset rebuilds the line (FL resizes it; its content after a resize is not confirmed: cleared here)
    void set (const Delay2State& s, double bpm);
    void process (float* left, float* right, int n) noexcept; // in place, n <= maxBlock
    // Room for the line at tempos down to minBpm (UI thread), so a later restart() or set() does not allocate.
    void reserve (double minBpm);
    // As if newly made with these values, reusing the line's memory (the audio thread's first block).
    void restart (const Delay2State& s, double bpm, bool settled);

private:
    double rate = 44100.0;
    int block = 512, delaySamples = 0, offsetSamples = 0, offsetSign = 0;
    std::vector<float> line; // interleaved L/R
    int frames = 0, readPos = 0, writePos = 0, offL = 0, offR = 0;
    int mode = 0;
    float dry = 1.0f, fbTarget = 0.0f, fbCur = 0.0f, coef = 1.0f, step = 0.001f;
    float gTarget[4] {}, gCur[4] {};
    double stL = 0.0, stR = 0.0;
    std::vector<float> tmp;
};

// --- Fruity Delay 3 ---------------------------------------------------------------------------------------------------
constexpr int delay3Controls = 26;
extern const std::array<int, delay3Controls> delay3Min, delay3Max, delay3Default;
const char* delay3ParamName (int i) noexcept;

struct Delay3State
{
    std::array<int, delay3Controls> v {};
};
std::optional<Delay3State> parseDelay3 (const std::vector<std::uint8_t>& state);
// The value FL's wrapper hands the engine for a raw control (0…1; controls 2, 3 and 13 go through raw)
float delay3EngineValue (int control, int raw) noexcept;

class Delay3Engine
{
public:
    explicit Delay3Engine (double sampleRate);
    ~Delay3Engine();
    Delay3Engine (const Delay3Engine&) = delete;
    Delay3Engine& operator= (const Delay3Engine&) = delete;

    // Host tempo (read at the start of each block, like the plugin's time info)
    void setTempo (double bpm) noexcept { hostTempo = bpm; }
    // A parameter change from the host (0…1): the plugin's smoothed controls glide toward it, the others apply now
    void setParameter (int index, float value) noexcept;
    // The host's sample-rate set-up (clears the lines; the delay times jump to their current targets)
    void setSampleRate (double sampleRate) noexcept;
    // LYNX start-up: lets every glide (parameter smoothing, time smoothing, modulation depth) reach its target, as
    // after FL has been running for a while
    void settle() noexcept;
    void process (float* left, float* right, int n) noexcept; // in place

    struct Dsp;

private:
    std::unique_ptr<Dsp> d;
    double hostTempo = 120.0;
    std::vector<std::pair<int, float>> smooth; // smoothed parameters: (index, target)
    float applied[delay3Controls] {};
    float smoothCoef = 1.0e-5f;
    int smoothCounter = 0;
    void applyNow (int index, float value) noexcept;
    void hostSet (int index, float value) noexcept;
};

// The LYNX effects (parameters = FL's raw controls over their ranges)
std::unique_ptr<Effect> makeDelay2();
std::unique_ptr<Effect> makeDelay3();
void delay2ToParams (const Delay2State& s, float* params) noexcept;
void delay3ToParams (const Delay3State& s, float* params) noexcept;
std::string delay2ParamText (int i, float v);
std::string delay3ParamText (int i, float v, double bpm);
} // namespace lynx::fldelay
