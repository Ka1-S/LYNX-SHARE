#pragma once

// FL Studio Harmless's built-in effects rack, rebuilt from docs/analise/REA_rodada36.md (clean-room; a port of our own
// reference model, which was checked against the installation's Harmless_x64.dll, Reverb_x64.dll and SG_x64.dll).
//
// Order on the mix of all voices (Gen_Render): chorus → delay → reverb → compressor. The chorus, delay and reverb
// are fixed presets selected by the rack's type combos (tables read in Harmless_x64.dll); the compressor is FL's
// Maximus engine (hosted through Shared/SG_x64.dll) loaded with Harmless's own preset
// Plugins/Fruity/Generators/Harmless/Data/Compression/<type>.fst, whose processed/original band mix is the rack's
// "Mix" knob. The preset files are read at run time from the user's FL installation; nothing of them is stored here.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace lynx::hlfx
{
// the Maximus state (plugin record 0xD5) inside an FL preset file (.fst); nullopt if there is none
std::optional<std::vector<std::uint8_t>> maximusStateFromFst (const std::vector<std::uint8_t>& fst);

// Everything of a Maximus state that does not depend on the sample rate (parameters, envelope gain tables,
// saturation tables). Built off the audio thread; shared read-only by the instruments that use it.
struct MaximusPreset;
std::shared_ptr<const MaximusPreset> makeMaximusPreset (const std::vector<std::uint8_t>& state);
// A Maximus state of version 1…2 in the version-3 layout (FUN_0083ef50 reads both into the same object); others as they are
std::vector<std::uint8_t> maximusStateAsV3 (const std::vector<std::uint8_t>& state);

// Which build of the engine runs the state: Shared/SG_x64.dll (= Maximus_x64.dll, bit-identical on the demo states)
// or the older copy inside Soundgoodizer_x64.dll (16-bit saturation tables, another curve end; REA_rodada40 §3)
enum class MaximusBuild { shared, soundgoodizer };
std::shared_ptr<const MaximusPreset> makeMaximusPreset (const std::vector<std::uint8_t>& state, MaximusBuild build);

class Maximus;

// The Maximus engine on its own, as FL's mixer Maximus, Soundgoodizer and Vocodex's SG stage run it (Eff_Render on
// the host's blocks; blocks longer than 512 frames are split). mixParameter: parameter 5 (band mix, 0…1000).
class MaximusProcessor
{
public:
    MaximusProcessor();
    ~MaximusProcessor();
    MaximusProcessor (const MaximusProcessor&) = delete;
    MaximusProcessor& operator= (const MaximusProcessor&) = delete;

    void prepare (std::shared_ptr<const MaximusPreset> preset, double sampleRate, int mixParameter);
    void process (float* left, float* right, int n) noexcept;
    void reset() noexcept;
    int latencySamples() const noexcept; // reported to the host (FL compensates it)

private:
    std::unique_ptr<Maximus> engine;
};

// parameter 5 (band mix) stored in the state
int maximusMixParameter (const MaximusPreset& preset) noexcept;

// The whole rack of one Harmless instance (state lives here; the presets are shared)
class Rack
{
public:
    Rack();
    ~Rack();
    Rack (const Rack&) = delete;
    Rack& operator= (const Rack&) = delete;

    // p: the 100 parameters, t: the 7 combo integers; comp: the compression preset (null: compressor off)
    void prepare (const std::array<int, 100>& p, const std::array<int, 7>& t, double fs, double bpm,
                  std::shared_ptr<const MaximusPreset> comp);
    bool enabled() const noexcept { return on; }

    // in place on the float mix of one host block (n ≤ maxBlock); voices: records existed at the start of the block
    void process (float* left, float* right, int n, bool voices) noexcept;

    // no voices and the rack has gone quiet: only its free-running counters and phases advance
    void advance (int n) noexcept;

    // true once the rack's output has stayed below −140 dB for longer than its longest internal delay
    bool quiet() const noexcept { return ! on || quietFor >= quietNeeded; }

    static constexpr int maxBlock = 512;

private:
    struct Chorus;
    struct Delay;
    struct Reverb;
    std::unique_ptr<Chorus> chorus;
    std::unique_ptr<Delay> delay;
    std::unique_ptr<Reverb> reverb;
    std::unique_ptr<Maximus> comp;
    std::shared_ptr<const MaximusPreset> compPreset;
    float wet = 0.0f;
    int tail = 0, tailLength = 0;
    long long quietFor = 0, quietNeeded = 0;
    bool on = false;
    std::array<float, maxBlock> scratchL {}, scratchR {}, mono {};
};
// Harmor's effects rack (docs/analise/REA_rodada38.md): the same library units as Harmless's (chorus, delay, shared
// reverb, the hosted Maximus) fed from Harmor's global parameters instead of fixed records, plus Harmor's own
// distortion; the six units run in the state's order and the records' FX-send mix joins the chain at unit 4.
struct HarmorRackSettings
{
    std::array<int, 53> g {};                      // Harmor's global parameters
    std::array<int, 6> order { 0, 1, 2, 3, 4, 5 }; // unit order (state's 6-integer block): 0 distortion, 1 chorus,
                                                   // 2 delay, 3 reverb, 4 FX-send join, 5 compressor
    int distortion = 0;                            // distortion type (global misc +48), 0 = off
    int precision = 0;                             // global misc +28 (oversampling 2^(precision+1), max 8×)
    double fs = 44100.0, bpm = 120.0;
    std::shared_ptr<const MaximusPreset> comp;     // compressor preset with globals 49–52 applied; null = off
    int compMix = 1000;                            // global 49 (Maximus parameter 5)
};

class HarmorRack
{
public:
    HarmorRack();
    ~HarmorRack();
    HarmorRack (const HarmorRack&) = delete;
    HarmorRack& operator= (const HarmorRack&) = delete;

    void prepare (const HarmorRackSettings& settings);
    bool enabled() const noexcept;

    // one host block (n ≤ maxBlock): left/right hold the records' dry mix and receive the rack's output; fxL/fxR are
    // the records' FX-send mix; voices: records existed in this block
    void process (float* left, float* right, const float* fxL, const float* fxR, int n, bool voices) noexcept;

    // no records and the rack is quiet: only the free-running phases, ramps and counters advance
    void advance (int n) noexcept;
    bool quiet() const noexcept;

    static constexpr int maxBlock = 512;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// a Maximus state with some parameters replaced (index, value): Harmor sends its globals 49–52 as the hosted
// plugin's parameters 5, 8, 21 and 34 after loading the preset
// the 8 envelopes of a Maximus state (per band: the compression curve, then the second envelope) as (Δx, y)
// points; empty when the state cannot be read
std::vector<std::vector<std::pair<double, float>>> maximusEnvelopes (const std::vector<std::uint8_t>& state);

// --- editing a Maximus curve (round 168x, issue #40) ---
// One point of a Maximus envelope as the state stores it (FL's map stream: Δx f64, level f64, tension f32, type)
struct MaximusCurvePoint
{
    double dx = 0.0;
    float y = 0.0f, tension = 0.0f;
    std::uint8_t type = 0;
};
// envelope 0…7 (2·band = the band's compression curve); empty when the state cannot be read
std::vector<MaximusCurvePoint> maximusCurve (const std::vector<std::uint8_t>& state, int envelope);
// the state with envelope `envelope` replaced by these points (FL's layout: the map's header and trailer kept, the
// direction cache byte recomputed); the state unchanged when it cannot be read or the points are empty
std::vector<std::uint8_t> withMaximusCurve (const std::vector<std::uint8_t>& state, int envelope, const std::vector<MaximusCurvePoint>& points);
// the curve as the engine steps it (FUN_007885d0), sampled at n + 1 evenly spaced x over 0…1; empty when a segment
// type is not played by LYNX's engine (only FL's types 0 "single curve" and 7 "single curve 2" are)
std::vector<float> maximusCurveShape (const std::vector<MaximusCurvePoint>& points, int n);
bool maximusCurveTypeSupported (int type) noexcept;

std::vector<std::uint8_t> withMaximusParams (std::vector<std::uint8_t> state, const std::vector<std::pair<int, int>>& params);
} // namespace lynx::hlfx
