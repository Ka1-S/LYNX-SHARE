#pragma once

// Fruity Scratcher (EffectKind::scratcher), rebuilt clean-room from the behaviour decoded from FL Studio's
// Fruity Scratcher_x64.dll (docs/analise/REA_rodada20.md §2, REA_rodada165g-flscratch165.md). No Image-Line code here.
//
// A turntable over a sample, added to the slot's input (output = input + the turntable's sound). The sample is one
// "turn" of the platter and loops. Two modes:
//  - motor (Hold off): the platter turns at 2 · Speed × the sample's rate (Speed 0.5 = 1×) in the Transport's direction
//    (backwards / stopped / forwards), reaching that speed through the Acceleration's inertia;
//  - hold (Hold on): the platter follows Wave position (+ whole turns from Rotations) through FL's 2nd-order smoother
//    (Smooth 1…4); a still platter is silent (the DC blocker), only its motion sounds.
// The sample is held as FL's host hands it to the plugin: 16-bit stereo at 44.1 kHz. The engine works per sample, like
// FL's: at another project rate the platter turns that much faster (FL does not compensate either).
//
// LYNX's parameters (0…1 each) and FL's controls (their raw integer ranges, what FL's pattern events hold):
//  [0] Wave position 0…30000 · [1] Mute 0/1 (1 = muted) · [2] Speed 0…100 · [3] Acceleration 0…100 · [4] Pan −128…128 ·
//  [5] Volume 0…255 (linear gain) · [6] Hold 0/1 · [7] Sensitivity 0…100 (the editor's drag speed) ·
//  [8] Transport −1/0/+1 (LYNX 0 / 0.5 / 1) · [9…12] Smooth 1…4 0…1000 · [13] Rotations 0…18 (9 = none).
// Every value is quantized to its control's steps, as FL's controls are.
//
// No JUCE here (tests/FlScratch165Tests.inc).

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Dvs.h"
#include "core/Song.h"

namespace lynx
{
class Effect;
struct SampleData;
} // namespace lynx

namespace lynx::scratcher
{
enum Param
{
    wavePos = 0,
    mute = 1,
    speed = 2,
    accel = 3,
    pan = 4,
    volume = 5,
    hold = 6,
    sensitivity = 7,
    transport = 8,
    smooth1 = 9,
    smooth2 = 10,
    smooth3 = 11,
    smooth4 = 12,
    rotations = 13,
    count = 14, // FL's 14 controls (pattern events, automation)
    // LYNX parameters for FL's two DVS settings (stored in FL's state, not controls): Timecode type (FL's list entry
    // / 8: 0 = Off … 1 = Timecode CD, core/Dvs.h) and RIAA preamp (0/1)
    timecodeType = 14,
    riaaPreamp = 15,
    paramCount = 16
};
// the state's timecode index (−1 = off) for the Timecode type parameter, and back
int timecodeOf (float value) noexcept;
float timecodeValue (int state) noexcept;

// FL's control range of each parameter (the raw integer of its knob / switch / selector)
struct Range
{
    int lo = 0, hi = 1;
};
Range controlRange (int param) noexcept;
// LYNX's value (0…1) for FL's raw control integer, and back (rounded to the nearest step)
float valueOfRaw (int param, int raw) noexcept;
int rawOfValue (int param, float value) noexcept;
// FL's control step of a value (round to the control's integer, then back)
float quantize (int param, float value) noexcept;

int transportOf (float value) noexcept;         // −1 backwards, 0 stopped, +1 forwards
float transportValue (int direction) noexcept;  // the value that selects it
int rotationsOf (float value) noexcept;         // 0…18 (9 = the platter's own turn)
std::string paramText (int param, float value);
// FL's defaults (DFM): Wave position 690, Speed 50, Acceleration 25, Pan 0, Volume 199, Sensitivity 50, transport
// stopped, Smooth 500 / 908 / 999 / 400, Rotations 9; not muted, Hold off.
std::array<float, count> defaults() noexcept;

// FL's state (record 0xD5), what fromFl reads [DECOMP 0x728d80]:
//   u32 version (0xF4628 / 0xF462A written by FL 2026), 14 f32 = the plugin's internal values (Wave position 0…1, Mute
//   1 = sound, Speed, Acceleration, Pan −1…1, Volume, Hold, Sensitivity, Transport −1/0/1, Smooth 1…4, Rotations
//   0…18), i32 n + n bytes = the sample's path (ANSI, with FL's %FLStudioFactoryData% style macros), then for
//   version > 0xF4628 an editor byte and for ≥ 0xF462A the timecode index (i32, −1 = off) and the RIAA byte.
//   Versions before 0xF4628 hold integers (the old reader, not seen in any file: rejected).
struct State
{
    std::uint32_t version = 0;
    std::array<float, count> values {}; // LYNX's parameters, quantized like FL's controls after a load
    std::string samplePath;             // as stored (macros not expanded)
    int timecode = -1;                  // FL's DVS type: the state's timecode index (−1 = off, core/Dvs.h)
    bool riaa = false;                  // FL's RIAA preamp switch
};
std::optional<State> parse (const std::vector<std::uint8_t>& state);
// FL's state for these parameters and path (version 0xF462A) with the DVS settings
std::vector<std::uint8_t> toFl (const std::array<float, count>& values, const std::string& samplePath, int timecode = -1,
                                bool riaa = false);

// The sample as FL's host hands it to the plugin [DECOMP 0x72be20]: 16-bit stereo at 44.1 kHz, as floats (k / 32768),
// plus one extra frame for the interpolation (FL fills it with frame 0's right channel on both sides). Mono is
// doubled; floats that are not already on the 16-bit grid (Ogg, 24-bit, float files) are rounded as round(x · 32767)
// (FL's sample class, REA_rodada77); another rate is resampled linearly to 44.1 kHz (FL's converter: not confirmed).
struct Turntable
{
    std::vector<float> frames; // interleaved L R, (n + 1) frames
    int n = 0;                 // the sample's frames
};
std::shared_ptr<const Turntable> prepareSample (const SampleData& sample);
std::shared_ptr<const Turntable> prepareSample (const float* left, const float* right, std::size_t frames, double sampleRate);

// FL's RIAA preamp [DECOMP 0x72cdf0]: b0, b1, a1, a2 of y = b0 x + b1 x₋₁ − a1 y₋₁ − a2 y₋₂ (poles 3180 µs / 75 µs, zero
// 318 µs, gain 20)
std::array<double, 4> riaaCoefficients (double sampleRate) noexcept;

// The engine, as FL's plugin renders [DECOMP 0x72add0]. Parameter changes take effect as in FL: Wave position, Volume
// and Pan are read from the previous block's values (FL copies them at the end of each block), the others at once.
class Engine
{
public:
    Engine();
    // the sample (nullptr = none: the input passes). Like FL, a new sample takes over at the end of the next block,
    // with the volume aimed at 0 for one block.
    void setSample (std::shared_ptr<const Turntable> sample) noexcept;
    void setParam (int param, float value) noexcept; // LYNX's value; quantized here
    void reset() noexcept;                           // FL's reset: platter at 0, the defaults' gains
    void process (float* left, float* right, int n) noexcept;
    // the project's rate (the RIAA preamp, the DVS decoder and the record → sample mapping use it)
    void setSampleRate (double sampleRate) noexcept;
    // DVS [REA_rodada167c §1]: the state's timecode index (−1 = off, core/Dvs.h) and the RIAA preamp. With a type set
    // the input drives the platter: the output is the sample at the record's place (the motor, Hold and the output
    // stage are not used, as in FL). Call dvs::warm (state) off the audio thread first.
    void setTimecode (int state, bool riaa) noexcept;
    int timecodeState() const noexcept { return timecode; }

    // the platter, for the editor: its position in turns (0…1, wrapped) and whether a sample plays
    float position() const noexcept { return lastPos; }
    bool loaded() const noexcept { return playing != nullptr; }

private:
    std::shared_ptr<const Turntable> playing, pending;
    bool swapPending = false;

    // FL's fields (the DLL's offsets in brackets)
    float wavePosIn = 0.0f, wavePosUsed = 0.0f; // [0x134] set by the parameter (and by the motor), [0x130] the block's
    float volTarget = 0.0f, volSet = 0.0f;      // [0x138] the block's target, [0x13c] the Volume
    float panSet = 0.0f, panTarget = 0.0f;      // [0x140] the Pan (−1…1), [0x144] the block's target
    float spd = 0.5f, acc = 0.25f, sen = 0.5f;  // [0x14c] [0x150] [0x148]
    float vol = 0.0f, panNow = 0.0f;            // [0x158] [0x154] smoothed gain and pan
    float smooth[4] {};                         // [0x15c…0x168]
    bool audible = true, holdOn = false;        // [0x16c] [0x16d]
    int direction = 0, rotationCount = 9;       // [0x170] [0x174]
    float phi = 0.0f, velocity = 0.0f;          // [0x180] position (turns) [0x184]
    float lastPos = 0.0f;                       // [0x190]
    float dcL = 0.0f, dcLIn = 0.0f, dcR = 0.0f, dcRIn = 0.0f; // [0x120…0x12c]
    // [0x194…0x1a8]: 1/(n−1), n−1, 2/(n−1) of the playing sample
    float invSpan = 0.0f, motorScale = 0.0f;
    int span = 0;

    // DVS [0x200 on, 0x204 type, 0x220 / 0x230 the RIAA filter, 0x298 fade, 0x29c hold limit, 0x2a0 / 0x2a4 hold]
    void setupDvs() noexcept;
    void processDvs (float* left, float* right, int n) noexcept;
    double rate = 44100.0;
    int timecode = -1;
    bool riaaOn = false;
    dvs::Decoder decoder;
    std::array<double, 4> riaa {};                    // b0, b1, a1, a2
    std::array<std::array<double, 3>, 2> riaaState {}; // x1, y1, y2 per side
    int holdLimit = 4410, dvsStuck = 0;
    float holdDecay = 1.0f, dvsOutL = 0.0f, dvsOutR = 0.0f;
    double dvsLast = 0.0;
};

// Whether a file dropped on the editor (an OS file drop or a drag from LYNX's Browser) is loaded onto the platter:
// LYNX's sample files (core/SampleFiles.h, the list SampleLibrary::isSampleFile and the Browser use), by extension
bool loadsDroppedFile (std::string_view path) noexcept;

// The slot's effect: `sample` may be null (no sample / not found → the input passes)
std::unique_ptr<Effect> make (std::shared_ptr<const SampleData> sample);
} // namespace lynx::scratcher
