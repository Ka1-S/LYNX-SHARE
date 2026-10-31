#pragma once

// FL Studio's Wave Traveller ("spline-controlled scratching") rebuilt from docs/analise/REA_rodada167f-wavetraveller.md
// (clean-room; checked against the installation's Wave Traveller_x64.dll run unmodified in our harness).
//
// Each of the 120 keys (C0 … B9) holds its own "patch": a position curve (the sample's position between Start and End
// over the note's time, a spline of points with a segment type each), a volume curve, mute switches along the curve,
// Speed (tempo based: the curve lasts 0.125 … 8 beats; or absolute: ×0.25 … ×4 of the sample's own speed), Attack
// (1 … 500 ms) and Release (1 … 1000 ms). A note plays its key's patch; the key does not transpose (the channel's pitch
// changes the curve's speed). The curve is turned into a table of about 1023 values when it is loaded; the voice reads
// the table at its own speed, places the read position in the sample and interpolates it (6-point polynomial live,
// windowed sinc with FL's render quality when exporting), through a DC blocker, the mute gate's smoothed gains and an
// attack / release envelope; at the curve's end the last 256 output samples are played backwards with a fade out.
//
// The channel keeps FL's own state bytes in ChannelDef::pluginState (the importer copies them as they are; the editor
// rewrites them in FL's layout) and the sample in ChannelDef::samplePath.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Instruments.h"

namespace lynx::wavetrav
{
constexpr int keyCount = 120;
constexpr int tableCapacity = 4096; // FL's tables hold 1024 values; extra room for curves that round past it

// A spline point (FL's 60-byte record, WarpTypes.TCSplinePt): position, value, the two Bezier handles (relative), the
// tension and the type of the segment that starts at this point (1 linear, 2 cosine, 3 Bezier, 4 sine, 5 ease-in
// cosine, 6 tension, 8 zero; others write nothing). The other bytes are kept as they were for the round trip.
struct Point
{
    float x = 0.0f, y = 0.0f;
    float c1x = -0.042f, c1y = 0.0f, c2x = 0.042f, c2y = 0.0f;
    float tension = 0.5f;
    std::int32_t type = 1;
    std::array<std::uint8_t, 60> raw {};
};

struct Mute
{
    bool on = true;   // the switch's state from `at` on (true = sound)
    float at = 0.0f;  // position along the curve, 0 … 1
};

struct Patch
{
    float speedBeats = 4.0f;  // tempo-based length of the curve, in beats (0.125 … 8)
    float absSpeed = 1.0f;    // absolute speed factor (0.25 … 4)
    float attackMs = 1.0f, releaseMs = 1.0f;
    float start = 0.0f, end = 1.0f; // the part of the sample the curve spans
    std::int32_t splineType = 0;     // 0 tempo based, 1 absolute speed
    std::string name;                // preset name (the .spln it came from)
    std::vector<Mute> mutes { Mute {} };
    std::vector<Point> wave, vol;

    // built from the curves (FUN_007558c0): values 0 … 1, n = the table's length
    std::array<float, tableCapacity> waveTable {}, volTable {};
    int waveN = 0, volN = 0;

    void rebuild() noexcept;
};

struct State
{
    std::int32_t selectedKey = 60; // the editor's key (FL keeps it in the state)
    std::string samplePath;        // as FL saved it (may hold %FLStudioFactoryData%)
    std::array<Patch, keyCount> patches;
};

// a new point as FL's editor makes one (linear segment, default handles)
Point newPoint (float x, float y);
Patch defaultPatch();
State defaultState();

// FL's 0xD5 state (tagged records 1000 / 2000 / 3000); false when it is not a Wave Traveller state
bool parseState (const std::vector<std::uint8_t>& bytes, State& state);
std::vector<std::uint8_t> writeState (const State& state);
// a .spln preset (FL's Presets folder) applied to a patch, as the editor's preset menu does
bool applyPreset (const std::vector<std::uint8_t>& spln, Patch& patch);

// the curve → table builder (FUN_007558c0), exposed for the tests
void buildTable (const std::vector<Point>& points, float* table, int& n) noexcept;

// the editor's knobs (0 … 1) and their values (FUN_007579e0 / 757b10 / 757b60)
inline float speedFromKnob (float k, bool absolute) noexcept { return (float) (absolute ? (double) k * 3.75 + 0.25 : (double) k * 7.875 + 0.125); }
inline float knobFromSpeed (float v, bool absolute) noexcept { return (float) (absolute ? ((double) v - 0.25) / 3.75 : ((double) v - 0.125) / 7.875); }
inline float attackFromKnob (float k) noexcept { return k * 499.0f + 1.0f; }
inline float releaseFromKnob (float k) noexcept { return k * 999.0f + 1.0f; }

// The engine's voice inputs (FL's TVoiceParams as the host fills them)
struct NoteParams
{
    int key = 60;
    float initPan = 0.0f;   // the note's own pan
    float finalPan = 0.0f, finalVol = 1.0f;
    float finalPitch = 0.0f; // cents, key 60 = 0 (key, channel pitch and the note's fine pitch)
};

// The sample as the plugin holds it: FL hands it as 16-bit stereo at 44.1 kHz (LoadSample's NeededFormat), the plugin
// keeps it as float pairs (value / 32768) plus one guard frame.
struct Sample
{
    std::vector<float> frames; // interleaved L/R, (length + 1) frames
    int length = 0;
};
std::shared_ptr<const Sample> sampleFrom (const SampleData& data);

class Engine
{
public:
    Engine();
    ~Engine();
    void setRate (int sampleRate) noexcept;
    void setTempo (float bpm) noexcept { tempo = bpm; }
    // 0 = the plugin's live mode (6-point polynomial); 64 / 128 / 256 = render mode with that many sinc taps per side
    void setRenderTaps (int taps) noexcept { renderTaps = taps; }
    void setState (std::shared_ptr<const State> s) noexcept { state = std::move (s); }
    void setSample (std::shared_ptr<const Sample> s) noexcept { sample = std::move (s); }

    int trigger (const NoteParams& note) noexcept; // voice slot, −1 when refused (no sample)
    void release (int slot) noexcept;
    void kill (int slot) noexcept;
    // the host's Final.Vol / Final.Pan of a sounding voice (FL's voice parameters are live: the plugin reads them on
    // every render)
    void setVoiceLevels (int slot, float finalVol, float finalPan) noexcept;
    void killAll() noexcept;
    int voiceKey (int slot) const noexcept;
    std::uint32_t voiceSerial (int slot) const noexcept; // changes when the slot is reused
    bool voiceActive (int slot) const noexcept;
    bool idle() const noexcept;
    // adds n frames into the interleaved stereo buffer
    void render (float* interleaved, int n) noexcept;

    static constexpr int maxVoices = 64;

private:
    struct Voice;
    void renderVoice (Voice& v, float* out, int n) noexcept;
    std::shared_ptr<const State> state;
    std::shared_ptr<const Sample> sample;
    std::unique_ptr<Voice[]> voices;
    std::array<int, maxVoices> order {}; // active slots, oldest first
    int count = 0;
    int rate = 44100;
    float samplesPerMinute = 44100.0f * 60.0f, dcCoef = 0.99f, tempo = 0.0f;
    double rateConv = 1.0; // the host's SmpRateConv for the 44.1 kHz sample [HYP: 44100 / rate] (absolute speed only)
    int renderTaps = 0;
    std::uint32_t serials = 0;
};

// Renders `notes` (start frame, release frame or −1, the note) like the DLL in our harness: interleaved stereo, length
// frames, block-independent.
struct RefNote
{
    int start = 0, release = -1;
    NoteParams note;
};
std::vector<float> renderReference (const State& state, const Sample& sample, const std::vector<RefNote>& notes, int sampleRate,
                                    float tempo, int renderTaps, int length);

} // namespace lynx::wavetrav

namespace lynx
{
bool waveTravellerStateUsable (const std::vector<std::uint8_t>& state);
// The instrument; `sample` is the channel's sample (silent without one, as FL: the plugin refuses notes)
std::unique_ptr<Instrument> makeWaveTravellerInstrument (std::shared_ptr<const SampleData> sample);
} // namespace lynx
