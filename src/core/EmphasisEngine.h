#pragma once

// The look-ahead limiter of FL Studio's shared DSP library (Shared/ildsp_x64.dll, class "Limiter"), the engine under
// the Emphasis plug-in, rebuilt from docs/analise/REA_rodada167f.md §1.2 (clean-room: the library's structure,
// formulas and constants described in our own words; no Image-Line code or data in this repository). Output is meant
// to be bit-identical to the library for the same parameter calls, block sizes and input.
//
// Signal path per block: optional DC high-pass (5 Hz) on the input → (modes 0/1) a spectral-balance analysis that
// steers the emphasis knee and, in mode 0, the release envelope's attack → integer dry delay (= latency) → input
// gain (L/R or M/S) → |x|, stereo linking, knee → per channel: program-dependent release envelope (GRAdaptiveRel) →
// look-ahead delay (interpolated) → 8-stage peak hold + envelope at the oversampled rate (halfband polyphase up/down,
// 1…16x) → M/S decode → (M/S) 1-stage limiter → true-peak (4x FIR) or sample-peak limiter (4 ms delay) → output
// ceiling gain → level matching → delta → bypass cross-fade → fade in / out around deferred parameter changes.
//
// Engine parameter indices and units are the library's (what the Emphasis front end sends):
//   0 bypass 0/1        1 delta 0/1 (rounded)      2 envelope order − 1 (0…3)     3 true peak 0/1
//   4 channels 0 L/R, 1 M/S   5 mode 0…3 (versatile, loud, transient, steady)       6 input gain dB
//   7 stereo linking 0…1   8 emphasis amount 0…1   9 output ceiling dB   10 level matching 0/1
//   11 oversampling 0…4 (1, 2, 4, 8, 16x)   12 hardness 0…9   13 loudness meter off (> 0.5; meter only)
//   25 look-ahead off: when nonzero and instant recovery is off, the main look-ahead delay (F2 − F1 samples) is
//      skipped and the latency drops by it   26 instant recovery (> 0.5: release fixed at 5 ms)   27 DC block (> 0.5)
// Parameters 1, 3, 4, 11, 25 and 26 are "deferred": while the output is audible they fade the engine out (100 Hz
// two-pole), then reset the delays / halfbands, re-send all parameters and fade back in, like the library.
//
// Not reproduced: the loudness / RMS meters (they never touch the audio). process() with n <= 0 does nothing (the
// library would update a few envelope fields from an empty block); n > maxBlock is processed in maxBlock pieces.

#include <array>
#include <cstdint>
#include <vector>

namespace lynx::emphasis
{
namespace detail
{
struct Lp1 // one-pole low-pass s = (s − x) c + x
{
    float sr = 0.0f, isr = 0.0f, c = 0.0f, s = 0.0f, cut = 20.0f;
    void setRate (float r) noexcept;
    void prepare (float r) noexcept;
    void setCoef (float hz, double k) noexcept; // cut = hz, c = exp (isr · k)
    void process (const float* in, float* out, int n) noexcept;
};

struct Hp1 // the high-pass twin (output x − s), with the library's rounding of every fourth sample
{
    float sr = 0.0f, isr = 0.0f, c = 0.0f, s = 0.0f, cut = 20.0f;
    void setRate (float r) noexcept;
    void prepare (float r) noexcept;
    void process (const float* in, float* out, int n) noexcept;
};

struct Es1 // one-stage attack / release smoother, double-precision coefficients
{
    float sr = 0.0f, isr = 0.0f, c40 = 0.0f, c44 = 0.0f, s = 0.0f, att = 0.0f, rel = 0.0f;
    void setRate (float r) noexcept;
    void process (const float* in, float* out, int n) noexcept;
};

struct EsN // 1…8-stage smoother with order-dependent time scaling, float coefficients
{
    float sr = 0.0f, isr = 0.0f, c40 = 0.0f, c44 = 0.0f;
    std::array<float, 8> st {};
    std::uint64_t order = 1;
    float factor = 0.0f, k = 0.0f, att = 0.0f, rel = 0.0f;
    void setRate (float r) noexcept;
    void setOrder (std::uint64_t n) noexcept;
    void prepare (float r) noexcept;
};

struct Es8 // 8-stage smoother (fixed time scaling)
{
    float sr = 0.0f, isr = 0.0f, c40 = 0.0f, c44 = 0.0f;
    std::array<float, 8> st {};
    float factor = 0.0f, att = 0.0f, rel = 0.0f;
    void setRate (float r) noexcept;
    void process (const float* in, float* out, int n) noexcept;
};

struct Ph1 // peak hold, 1 stage (used here only as an activity detector)
{
    float sr = 0.0f, len = 0.0f, v = 0.0f, hold = 0.0f;
    std::uint64_t cnt = 0;
    void setRate (float r) noexcept;
};

struct Ph8 // peak hold, 8 stages of hold/8 each
{
    float sr = 0.0f, len = 0.0f, hold = 0.0f;
    std::array<std::uint64_t, 8> cnt {};
    std::array<float, 8> v {};
    void setRate (float r) noexcept;
    void process (const float* in, float* out, int n) noexcept;
    void reset() noexcept;
};

template <typename Smoother>
struct GrStatic // peak hold → max 1 → smoother → min (1/x, 1), plus a block activity measure
{
    float sr = 0.0f;
    Ph1 act;
    Ph8 ph;
    Smoother es;
    float att = 0.0f, hold = 0.0f, rel = 0.0f, last = 0.0f, activity = 0.0f;
    void setRate (float r) noexcept;
    void process (const float* in, float* out, int n) noexcept;
};

struct GrAdaptive // program-dependent release envelope (the knee-detector branch)
{
    float sr = 0.0f, isr = 0.0f;
    Ph8 ph;
    EsN es;
    Es1 es1;
    Lp1 lp;
    float g = 0.0f, a = 0.0f, b = 0.0f, rel1 = 0.15f, rel2 = 0.6f, thr = 0.0f, scale = 1.0f, mult = 10.0f;
    bool flag = true;
    float att = 10.05f, hold = 0.05f, w = 1.0f;
    void setRate (float r) noexcept;
    void prepare (float r) noexcept;
    void process (float* buf, int n) noexcept;
};

constexpr int ringSize = 0x40000; // the library's delay rings (2^18 floats)

struct DelayInt
{
    float sr = 0.0f, isr = 0.0f, t = 0.0f;
    std::uint64_t d = 0, w = 0;
    std::vector<float> buf;
    void setRate (float r) noexcept;
    void process (const float* in, float* out, int n) noexcept;
    void reset() noexcept;
};

struct DelayLerp // delay with a 5 Hz glide of the length and linear interpolation
{
    float sr = 0.0f, isr = 0.0f, t = 0.0f, target = 0.0f, cur = 0.0f, c = 0.0f;
    std::uint64_t w = 0;
    std::vector<float> buf;
    void setRate (float r) noexcept;
    void process (const float* in, float* out, int n) noexcept;
    void reset() noexcept;
};

struct HbSection
{
    float c = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};

struct Halfband // two all-pass chains, output ½ (A(x) + previous B(x))
{
    std::array<HbSection, 6> a {}, b {};
    int na = 0, nb = 0;
    float prev = 0.0f;
    void init (int coefs) noexcept; // 12, 6 or 4 coefficients
    void reset() noexcept;
};

struct Oversampler // the halfband cascades of one direction for 2, 4, 8 and 16x
{
    std::array<Halfband, 10> hb {}; // 2x: [0]; 4x: [1, 2]; 8x: [3…5]; 16x: [6…9]
    void init() noexcept;
    void reset() noexcept;
    // 1 or 2 channels at once (interleaved for speed); scratch[c]: os · n floats; down destroys in
    static void up (Oversampler* const* ovs, const float* const* in, float* const* out, float* const* scratch, int nCh,
                    int n, int os) noexcept;
    static void down (Oversampler* const* ovs, float* const* in, float* const* out, int nCh, int n, int os) noexcept;
};

struct TruePeak // 4x FIR interpolator, max |sample| of each group of 4
{
    static constexpr int factor = 4, taps = 96 * factor + 1, bufLen = 98 * factor;
    std::array<float, taps> coef {};
    std::array<float, bufLen> acc {};
    int pos = 0;
    void init() noexcept;
    void process (const float* in, float* out, float* scratch, int n) noexcept;
};
} // namespace detail

class Engine
{
public:
    Engine();

    // Allocates (the only place); the library's state after its own prepare (sample rate, then all parameters
    // re-sent). Parameters set before stay; call setParam afterwards like the front end.
    void prepare (float sampleRate, int maxBlock);
    void setParam (int k, float v) noexcept;
    float getParam (int k) const noexcept { return k >= 0 && k < 28 ? p[(std::size_t) k] : 0.0f; }
    // in and out may alias
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;
    int latency() const noexcept;
    // The library's reset: clears the delays and the halfband filters (not the envelopes)
    void reset();

    // latency() of an engine prepared at sampleRate and then sent engine values ep[0…27] in the front end's order
    // (0…9, 11, 12, 10, 26, 27, 25); no allocation
    static int latencyFor (float sampleRate, const float* engineParams) noexcept;

private:
    void set (int k, float v) noexcept;
    void defer() noexcept;
    void updateLatency (bool enable) noexcept;
    void resendAll() noexcept;
    void defaults() noexcept;
    void processBlock (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;
    std::uint64_t latencyRaw() const noexcept;

    float sr = 44100.0f, isr = 1.0f / 44100.0f;
    int maxBlock = 0;
    bool prepared = false;

    std::array<float, 28> p {};
    float pOs = 4.0f, pModeLow = 1.0f, pGin = 1.0f, pGinInv = 1.0f, pOut = 0.99966854f, p19 = 0.0f, p20 = 1.0f,
          pAtt = 10.05f, p22 = 0.0f, kneeT = 1.0f, kneeR = 1.0f;
    float aDelta = 0.0f, aTp = 0.0f, aMs = 0.0f, aOs = 2.0f, aHard = 7.0f, aOsF = 4.0f, aLa = 0.0f, aInstant = 1.0f;
    bool enable = true, muted = true, pending = false;
    std::uint64_t fadeCount = 0;

    detail::GrAdaptive grar[2];
    detail::GrStatic<detail::Es8> grOs[2], grTp[2];
    detail::GrStatic<detail::Es1> grMs[2];
    detail::DelayLerp look[2], lookOs[2];
    detail::DelayInt msDelay[2], tpDelay[2], dry[2];
    detail::Lp1 lvlIn[2], lvlOut[2], eLo[2], eHi[2], split[2], fade[2], meter[2];
    detail::Hp1 dc[2];
    detail::Lp1 lpBypass, lpGain, lpOut, lpAtt;
    detail::Es1 esBal;
    detail::Oversampler upMain[2], downMain[2], upDry[2], downDry[2];
    detail::TruePeak tp[2];

    // work buffers (maxBlock, or 16 · maxBlock for the oversampled ones)
    std::vector<float> xin[2], b8[2], b10[2], b12[2], b14[2], b16[2], b18[2], b22[2], b24[2], b26[2], b20, b21, tpScratch;
};
} // namespace lynx::emphasis
