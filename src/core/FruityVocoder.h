#pragma once

// FL Studio's Fruity Vocoder as an exact LYNX mixer effect (docs/analise/REA_rodada70.md, completing REA_rodada20 §4.1;
// clean-room: rebuilt from the observed behaviour of the plugin, no Image-Line code).
//
// The left input is the modulator and the right input the carrier (swapped by the "swap" option). The modulator goes
// through an analysis bank of 4…128 band-passes (1…3 identical biquads in cascade), each followed by an envelope
// follower; the carrier goes through a synthesis bank built from the same band edges (shifted by Formant, mirrored by
// Invert); the output is the sum of the filtered carrier bands times their envelopes, plus the modulator and carrier
// levels of the MIX section. It is mono: the same signal leaves on both sides.
//
// The LYNX slot (EffectKind::vocoder) keeps the controls as its parameters (see Effects.cpp: 0 bands, 1 order,
// 2 listen, 3 swap, 4…15 FL's twelve automatable controls, 16 LYNX's side-input mode) and the per-band levels of the
// analysis bank in EffectSlot::state as FL's own saved state (0xD5 record).

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace lynx::fvocoder
{
constexpr int maxBands = 128;
constexpr int maxOrder = 3;
constexpr int controls = 12;  // FL's automatable controls (raw integers)
constexpr int blockFrames = 512; // longer host blocks are split into calls of this size

enum Control
{
    freqMin = 0,   // 0…65536
    freqMax = 1,   // 0…65536
    freqScale = 2, // 0…128
    invert = 3,    // 0/1
    formant = 4,   // −1200…1200 cents
    bandwidth = 5, // 5…200 %
    attack = 6,    // 10…1000 ms
    release = 7,   // 10…1000 ms
    hold = 8,      // 0/1
    modLevel = 9,  // 0…160
    carLevel = 10, // 0…160
    wetLevel = 11  // 0…160
};

int controlMin (int c) noexcept;
int controlMax (int c) noexcept;
int controlDefault (int c) noexcept;
// The raw control a 0…1 knob (the LYNX slot parameter, (raw − min)/(max − min) within the knob range) stands for.
// FL's state may hold raw values outside the knob range, and the plugin uses them as they are (REA_rodada78 §1): while
// the knob still shows the saved value (clamped to its range), the saved raw value is kept.
int rawFromKnob (float knob, int control, int savedRaw) noexcept;

struct State
{
    int bands = 16, order = 2, listen = 2; // bands 4·2^k (4…128), order 1…3, listen 0 modulator / 1 carrier / 2 mix
    bool swap = false;
    std::array<float, maxBands> levels {}; // analysis band levels (1 = 0 dB); FL resets them to 1 when the band count changes
    std::array<int, controls> c { 0, 65536, 64, 0, 0, 50, 20, 100, 0, 0, 0, 128 };
    State() { levels.fill (1.0f); }
};

// FL's saved state: i32 version (≥ 2), i32 bands, i32 order, i32 listen, u8 swap, bands × f32 levels, 12 × i32 controls.
std::optional<State> parse (const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> serialise (const State& s);
bool validBands (int n) noexcept;

// Frequency (Hz) of FreqMin / FreqMax for a raw control value, as the plugin computes it.
float edgeHz (int raw) noexcept;
// Level of the MIX section (modulator / carrier / wet) for a raw 0…160 value.
float mixLevel (int raw) noexcept;

class Engine
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;                 // states and envelopes to zero (a freshly loaded plugin)
    void setState (const State& s) noexcept; // configuration + controls (states kept unless the band count changes)
    const State& state() const noexcept { return st; }

    // mono modulator and carrier in, mono out (out may alias neither input); any n
    void process (const float* mod, const float* car, float* out, int n) noexcept;
    // FL's own I/O: stereo in place, L = modulator and R = carrier (or swapped), mono result on both sides
    void processStereo (float* left, float* right, int n) noexcept;
    // round 190ld (DP-32): analysis band k's envelope over the bank's common gain (the modulator's band level times the
    // band's own level; 1 = 0 dB), as the editor's band display shows it. Frozen while HOLD is on, as the envelopes are.
    float bandMeter (int k) const noexcept
    {
        return k >= 0 && k < st.bands && bandGain > 0.0f ? analysis[(std::size_t) k].env / bandGain : 0.0f;
    }

private:
    struct Biquad
    {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };
    struct Band
    {
        float hz = 0.0f, octaves = 0.0f;
        Biquad k;
        std::array<std::array<double, 4>, maxOrder> z {}; // per stage: x1, x2, y1, y2
        float env = 0.0f, gain = 0.0f;
        bool silent = false, active = false;
    };

    void configure() noexcept;
    void buildBank (std::array<Band, maxBands>& bank, bool synthesis) noexcept;
    void design (Band& b) const noexcept;
    void filter (Band& b, const float* in, float* out, int n) noexcept;
    void block (const float* mod, const float* car, float* out, int n) noexcept;

    State st;
    int rate = 44100;
    double radPerHz = 0.0;
    float fMin = 10.0f, fMax = 16000.0f, scale = 100.0f, top = 20000.0f, bwPerCent = 0.0f, bandGain = 30.0f;
    float gMod = 0.0f, gCar = 0.0f, gWet = 1.0f;
    std::array<float, 2> coef { 0.0f, 0.0f }; // attack, release (per-sample pole)
    std::array<Band, maxBands> analysis {}, synthesis {};
    std::array<float, blockFrames + 1> work {}, carWork {}, acc {};
    std::array<float, blockFrames> modBuf {}, carBuf {};
};
} // namespace lynx::fvocoder
