#pragma once

// FL Studio's Pitch Shifter effect, rebuilt from its behaviour (docs/analise/REA_rodada61.md; clean-room, no Image-Line
// code or data). The plug-in hands its audio to one of two engines of FL's shared DSP library: a pitch-synchronous
// overlap-add shifter with a spectral pitch detector (the UI's "FFT" mode, formant shift and correction) and a granular
// shifter. Both run per sample in single precision, so the output does not depend on the host block size.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
class Effect;
}

namespace lynx::psfl
{
// The plug-in's saved state (record 0xD5: 75 bytes for version 7, 69 for version 6)
struct State
{
    int version = 7;
    // 0 pitch (cents, −1200…1200), 1 formant (cents), 2 analysis channel (0 L, 1 R, 2 L+R), 3 mix (0…2400 = 0…100 %),
    // 4 grain duration (2000…15000 = 20…150 ms), 5 grain density (100…500 %), 6 jitter (0…100 %), 7 random (0…100 %),
    // 8 delay (1…768: 1…1000 ms, or 1/48 steps with tempo sync), 9 delay feedback (0…100 %), 10 delay mode (0 off,
    // 1 post, 2 pre, 3 delay only), 11 formant correction (0…65536 = 0…100 %), 12 pitch mode (0 relative, 1…3 = C4…C6)
    std::array<int, 13> params { 0, 0, 0, 2400, 8500, 200, 0, 0, 768, 0, 0, 65536, 0 };
    bool tempoSync = false;
    int engine = 0;   // 0 = PSOLA ("FFT"), 1 = granular
    int detector = 0; // pitch detector: 0 = fast, 1 = accurate
};

bool parsePitchShifter (const std::uint8_t* data, std::size_t size, State& out);

// Normalised parameter value of the plug-in's knob i (what FL automation and the engine see), from the raw value
float normalised (int index, int raw) noexcept;
// Range of parameter i's raw value
int rawMin (int index) noexcept;
int rawMax (int index) noexcept;

class Engine
{
public:
    Engine();
    ~Engine();

    void setState (const State& s);     // as the plug-in's state load: every parameter re-sent
    void setRaw (int index, int raw);   // one parameter change (automation)
    void setTempo (double bpm);         // host tempo (tempo-synced delay); 120 until set
    void prepare (double sampleRate);   // as the host's sample-rate change: engines re-initialised
    void reset();                       // clears the audio history (keeps the parameters)
    void process (float* left, float* right, int numFrames); // in place, stereo
    int latencyFrames() const noexcept; // what the plug-in reports to the host

private:
    struct Psola;
    struct Granular;
    void sendParam (int index);
    State st;
    double rate = 44100.0, tempo = 120.0;
    std::unique_ptr<Psola> psola;
    std::unique_ptr<Granular> gran;
    std::vector<float> io;
};

// The LYNX mixer effect (EffectKind::flPitchShifter): slot parameters [0…12] = FL's 13 knobs (raw − min) / range,
// [13] engine (≥ 0.5 = granular), [14] accurate detector, [15] delay tempo sync
constexpr int numParams = 16;
void toParams (const State& s, EffectParams& p) noexcept;
State fromParams (const EffectParams& p) noexcept;
std::string paramText (int param, float v);
// `params` (may be null): the slot's parameters, so the latency is known before the first block (issue #113)
std::unique_ptr<Effect> make (const EffectParams* params = nullptr);

// FL's MSVC run-time tanhf (the engines' soft-clip table is built from it); exposed for the tests
float tanhfCrt (float x) noexcept;
} // namespace lynx::psfl
