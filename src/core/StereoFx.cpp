#include "core/StereoFx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64) // x86 SSE, or ARM64 through core/Simd.h (sse2neon)
 #include "core/Simd.h"
#endif

// Clean-room implementation of docs/analise/REA_rodada54.md. Comments name the behaviour, not FL's code.
// The engines are single-precision code whose results depend on the order of operations: no FMA contraction.
#if defined(__clang__)
 #pragma clang fp contract(off)
#endif

namespace lynx::stereofx
{
namespace
{
std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t pos) noexcept
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + pos, 4);
    return v;
}

constexpr int flBlock = 512; // FL's usual block size: longer host blocks are split into calls of this size
constexpr float tiny = 5.9604644775390625e-08f; // 2⁻²⁴

float toUnit (int raw, int lo, int hi) noexcept { return (float) (std::clamp (raw, lo, hi) - lo) / (float) (hi - lo); }
int fromUnit (float v, int lo, int hi) noexcept
{
    return lo + (int) std::lround (std::clamp ((double) v, 0.0, 1.0) * (double) (hi - lo));
}

// flush-to-zero / denormals-are-zero while an engine runs, as the plugins' own render calls do
class Ftz
{
public:
    Ftz() noexcept
    {
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64) // x86 SSE, or ARM64 through core/Simd.h (sse2neon)
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040u);
#endif
    }
    ~Ftz()
    {
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64) // x86 SSE, or ARM64 through core/Simd.h (sse2neon)
        _mm_setcsr (saved);
#endif
    }
    Ftz (const Ftz&) = delete;
    Ftz& operator= (const Ftz&) = delete;

private:
    unsigned int saved = 0;
};

// ---- the shared plugin framework of Stereo Shaper and Stereo Enhancer (REA_rodada54 §1) ------------------------

// per-block linear ramp of a gain towards its target, slope limited to `maxSlope` per sample
struct Ramp
{
    float current = 0.0f, target = 0.0f;

    void next (int n, float maxSlope, float& start, float& inc) noexcept
    {
        start = current;
        inc = target - start;
        std::uint32_t bits = 0;
        std::memcpy (&bits, &inc, 4);
        if (bits == 0)
            return;
        current = target;
        inc = inc / (float) n;
        if (std::fabs (inc) > maxSlope)
        {
            inc = std::copysign (maxSlope, inc);
            current = start + inc * (float) n;
            if (std::fabs (current) < tiny)
            {
                current = 0.0f;
                inc = -start / (float) n;
            }
        }
    }
};

// all-pass biquad (direct form I, double state, float in/out); state values below 2⁻²⁴ are zeroed after each block
struct AllPass
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    void design (double alpha, double cosw) noexcept
    {
        const auto d = 1.0 / (alpha + 1.0);
        a1 = d * cosw * -2.0;
        a2 = d * (1.0 - alpha);
        b1 = a1;
        b0 = a2;
        b2 = 1.0;
    }
    void clear() noexcept { x1 = x2 = y1 = y2 = 0.0; }
    void run (float* x, int n, int stride) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            auto acc = x1 * b1 - y2 * a2;
            acc = acc + x2 * b2;
            acc = acc - y1 * a1;
            const auto in = (double) x[i * stride];
            const auto y = in * b0 + acc;
            x[i * stride] = (float) y;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = y;
        }
        for (auto* v : { &x1, &x2, &y1, &y2 })
            if (std::fabs (*v) < (double) tiny)
                *v = 0.0;
    }
};

// knob law shared by the delay controls: time in units of 10 µs, (1001^x − 1)·50 (0…500 ms)
float delayUnits (double x) noexcept
{
    const auto xf = (float) x;
    return (float) ((double) (float) (std::exp ((double) xf * std::log (1001.0)) - 1.0) * 50.0);
}

// a 2×2 gain matrix with ramped gains, a delay on one channel and an optional all-pass on one channel; the delay and
// the all-pass run before or after the matrix
struct MatrixCore
{
    float maxSlope = 0.001f, maxHz = 22045.59f, rateScale = 0.441f;
    double fs = 44100.0;
    std::array<Ramp, 4> gains {}; // L←L, R←R, L←R, R←L
    std::vector<float> ring;
    int ringPos = 0;
    bool delayRight = false;
    int phaseSide = 0; // 0 off, < 0 left, > 0 right
    AllPass ap;
    bool post = false;

    void prepare (double sampleRate) noexcept
    {
        const auto rate = (int) std::lround (sampleRate > 0.0 ? sampleRate : 44100.0);
        fs = rate;
        maxSlope = (float) ((44100.0 / rate) * 0.001);
        maxHz = (float) ((double) rate * 0.4999);
        rateScale = (float) ((double) rate / 100000.0);
        // the delay's longest setting (50000 units: 0.5 s), so a delay change never allocates on the audio thread
        ring.reserve ((std::size_t) std::nearbyint (50000.0 * (double) rateScale) + 2);
        ap.clear();
        for (auto& g : gains)
            g.current = 0.0f; // the plugins fade the matrix in when they are created
    }
    void setDelay (float units, bool right)
    {
        const auto samples = std::max (0, (int) std::nearbyint ((double) (units * rateScale)));
        ring.assign ((std::size_t) samples, 0.0f);
        ringPos = 0;
        delayRight = right;
    }
    void setPhase (int side, float hz) noexcept
    {
        phaseSide = side;
        if (side == 0)
        {
            ap.clear();
            return;
        }
        if (maxHz <= hz && hz != maxHz)
            hz = maxHz;
        const auto w = (double) hz * ((1.0 / fs) * 6.283185307179586);
        ap.design (std::sin (w), std::cos (w));
    }
    void snap() noexcept
    {
        ap.clear();
        for (auto& g : gains)
            g.current = g.target;
    }

    void effects (float* x, int n) noexcept // interleaved
    {
        if (! ring.empty())
        {
            auto* c = x + (delayRight ? 1 : 0);
            const auto len = (int) ring.size();
            for (int i = 0; i < n; ++i)
            {
                std::swap (c[2 * i], ring[(std::size_t) ringPos]);
                if (++ringPos >= len)
                    ringPos = 0;
            }
        }
        if (phaseSide != 0)
            ap.run (x + (phaseSide > 0 ? 1 : 0), n, 2);
    }

    void matrix (float* x, int n) noexcept
    {
        float c[4], inc[4];
        bool ramping = false;
        for (int k = 0; k < 4; ++k)
        {
            gains[(std::size_t) k].next (n, maxSlope, c[k], inc[k]);
            ramping = ramping || inc[k] != 0.0f;
        }
        if (! ramping && c[2] == 0.0f && c[3] == 0.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                x[2 * i] *= c[0];
                x[2 * i + 1] *= c[1];
            }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const auto l = x[2 * i], r = x[2 * i + 1];
            x[2 * i] = l * c[0] + r * c[2];
            x[2 * i + 1] = r * c[1] + l * c[3];
            if (ramping)
                for (int k = 0; k < 4; ++k)
                    c[k] += inc[k];
        }
    }

    void block (float* x, int n) noexcept
    {
        if (! post)
            effects (x, n);
        matrix (x, n);
        if (post)
            effects (x, n);
    }
};

// stereo, in place, split into FL-sized blocks; `difference` outputs input − output
void runMatrix (MatrixCore& core, float* left, float* right, int n, bool difference) noexcept
{
    Ftz ftz;
    float buf[2 * flBlock], dry[2 * flBlock];
    for (int done = 0; done < n;)
    {
        const auto m = std::min (flBlock, n - done);
        for (int i = 0; i < m; ++i)
        {
            buf[2 * i] = left[done + i];
            buf[2 * i + 1] = right[done + i];
        }
        if (difference)
            std::copy (buf, buf + 2 * m, dry);
        core.block (buf, m);
        for (int i = 0; i < m; ++i)
        {
            left[done + i] = difference ? dry[2 * i] - buf[2 * i] : buf[2 * i];
            right[done + i] = difference ? dry[2 * i + 1] - buf[2 * i + 1] : buf[2 * i + 1];
        }
        done += m;
    }
}
} // namespace

// ------------------------------------------------------------------------------------------- Fruity Stereo Shaper

namespace
{
// knob law of the matrix gains: x = raw / 12800; |g| = (3^|x| − 1) / 2, exactly 1 at |x| = 1, sign of x
float matrixGain (int raw) noexcept
{
    const auto x = (float) ((double) raw * 7.8125e-05);
    const auto a = std::fabs (x);
    float g = a;
    if (a != 1.0f)
    {
        static const auto ln3 = (float) std::log (3.0);
        g = (float) ((std::exp ((double) (a * ln3)) - 1.0) * (double) 0.5f);
    }
    const float s = x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f);
    return g * s;
}
float shaperDelayUnits (int raw) noexcept { return delayUnits ((double) std::abs (raw) * 0.000244140625); }
// phase knob: all-pass frequency (5001^(1 − |v|/4096) − 1)·4.398 + 10 Hz (22 kHz … 10 Hz)
float shaperPhaseHz (int raw) noexcept
{
    const auto x = (float) (1.0 - std::fabs ((double) raw * 0.000244140625));
    return (float) ((double) (float) (std::exp ((double) x * std::log (5001.0)) - 1.0) * 4.398 + 10.0);
}
} // namespace

std::optional<StereoShaperState> parseStereoShaper (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 36) // the leading version word (0 in every known state) is read and ignored by FL
        return std::nullopt;
    StereoShaperState s;
    int* f[8] = { &s.rightToLeft, &s.left, &s.right, &s.leftToRight, &s.delay, &s.phase, &s.send, &s.post };
    for (int i = 0; i < 8; ++i)
        *f[i] = i32At (d, 4 + 4 * (std::size_t) i);
    return s;
}

void stereoShaperToParams (const StereoShaperState& s, float* p) noexcept
{
    p[0] = toUnit (s.rightToLeft, -25600, 25600);
    p[1] = toUnit (s.left, -25600, 25600);
    p[2] = toUnit (s.right, -25600, 25600);
    p[3] = toUnit (s.leftToRight, -25600, 25600);
    p[4] = toUnit (s.delay, -4096, 4096);
    p[5] = toUnit (s.phase, -4096, 4096);
    p[6] = s.post > 0 ? 1.0f : 0.0f;
    p[7] = 0.0f;
}

StereoShaperState stereoShaperFromParams (const float* p) noexcept
{
    StereoShaperState s;
    s.rightToLeft = fromUnit (p[0], -25600, 25600);
    s.left = fromUnit (p[1], -25600, 25600);
    s.right = fromUnit (p[2], -25600, 25600);
    s.leftToRight = fromUnit (p[3], -25600, 25600);
    s.delay = fromUnit (p[4], -4096, 4096);
    s.phase = fromUnit (p[5], -4096, 4096);
    s.post = p[6] >= 0.5f ? 1 : 0;
    return s;
}

std::string stereoShaperParamText (int param, float v)
{
    std::array<float, 8> p {};
    p.fill (v);
    const auto s = stereoShaperFromParams (p.data());
    char t[48];
    switch (param)
    {
        case 0: case 1: case 2: case 3:
        {
            const auto raw = param == 0 ? s.rightToLeft : param == 1 ? s.left : param == 2 ? s.right : s.leftToRight;
            std::snprintf (t, sizeof (t), "%d%%", (int) std::lround (matrixGain (raw) * 100.0f));
            return t;
        }
        case 4:
            if (s.delay == 0)
                return "off";
            std::snprintf (t, sizeof (t), "%s %.1f ms", s.delay > 0 ? "R" : "L", shaperDelayUnits (s.delay) / 100.0);
            return t;
        case 5:
            if (s.phase == 0)
                return "off";
            std::snprintf (t, sizeof (t), "%s %.0f Hz", s.phase > 0 ? "R" : "L", (double) shaperPhaseHz (s.phase));
            return t;
        case 6: return s.post ? "post" : "pre";
        case 7: return v >= 0.5f ? "difference" : "output";
        default: return {};
    }
}

namespace
{
class StereoShaperEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        core.prepare (sampleRate);
        if (have)
            apply (st, true);
    }
    void reset() noexcept override { core.snap(); } // FL's Flush: filter state cleared, gains jump to their targets
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        apply (stereoShaperFromParams (params.data()), ! have);
        runMatrix (core, left, right, n, params[7] >= 0.5f);
    }

private:
    MatrixCore core;
    StereoShaperState st;
    bool have = false;

    void apply (const StereoShaperState& s, bool all)
    {
        const auto old = st;
        st = s;
        have = true;
        if (all || old.delay != s.delay)
            core.setDelay (shaperDelayUnits (s.delay), s.delay > 0);
        if (all || old.phase != s.phase)
            core.setPhase (s.phase > 0 ? 1 : (s.phase < 0 ? -1 : 0), shaperPhaseHz (s.phase));
        core.post = s.post > 0;
        core.gains[0].target = matrixGain (s.left);
        core.gains[1].target = matrixGain (s.right);
        core.gains[2].target = matrixGain (s.rightToLeft);
        core.gains[3].target = matrixGain (s.leftToRight);
    }
};
} // namespace

std::unique_ptr<Effect> makeStereoShaper() { return std::make_unique<StereoShaperEffect>(); }

// ----------------------------------------------------------------------------------------- Fruity Stereo Enhancer

std::optional<StereoEnhancerState> parseStereoEnhancer (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 24)
        return std::nullopt;
    StereoEnhancerState s;
    int* f[6] = { &s.pan, &s.volume, &s.separation, &s.delay, &s.post, &s.invert };
    for (int i = 0; i < 6; ++i)
        *f[i] = i32At (d, 4 * (std::size_t) i);
    return s;
}

void stereoEnhancerToParams (const StereoEnhancerState& s, float* p) noexcept
{
    p[0] = toUnit (s.pan, -128, 128);
    p[1] = toUnit (s.volume, 0, 320);
    p[2] = toUnit (s.separation, -96, 96);
    p[3] = toUnit (s.delay, -512, 512);
    p[4] = s.post > 0 ? 1.0f : 0.0f;
    p[5] = toUnit (s.invert, 0, 2);
}

StereoEnhancerState stereoEnhancerFromParams (const float* p) noexcept
{
    StereoEnhancerState s;
    s.pan = fromUnit (p[0], -128, 128);
    s.volume = fromUnit (p[1], 0, 320);
    s.separation = fromUnit (p[2], -96, 96);
    s.delay = fromUnit (p[3], -512, 512);
    s.post = p[4] >= 0.5f ? 1 : 0;
    s.invert = fromUnit (p[5], 0, 2);
    return s;
}

namespace
{
// volume knob: x = raw/256, (11^x − 1)/10, exactly 1 at x = 1
float enhancerVolume (int raw) noexcept
{
    const auto x = (float) raw * 0.00390625f;
    if (x == 1.0f)
        return 1.0f;
    return (float) ((std::exp ((double) (x * 2.39789534f)) - 1.0) * (double) 0.1f);
}
float enhancerDelayUnits (int raw) noexcept { return delayUnits ((double) std::abs (raw) * 0.001953125); }

std::array<float, 4> enhancerGains (const StereoEnhancerState& s) noexcept
{
    const auto pan = (float) s.pan * 0.0078125f;
    const auto sep = (float) s.separation * 0.0078125f;
    const auto a = enhancerVolume (s.volume) * (1.0f - sep * 0.5f);
    std::array<float, 4> c { a, a, a * sep, a * sep }; // L←L, R←R, L←R, R←L
    if (pan < 0.0f)
    {
        const auto f = pan + 1.0f;
        c[1] *= f;
        c[3] *= f;
    }
    else
    {
        const auto f = 1.0f - pan;
        c[0] *= f;
        c[2] *= f;
    }
    if (s.invert == 1)
    {
        c[0] = -c[0];
        c[2] = -c[2];
    }
    else if (s.invert == 2)
    {
        c[1] = -c[1];
        c[3] = -c[3];
    }
    return c;
}
} // namespace

std::string stereoEnhancerParamText (int param, float v)
{
    std::array<float, 6> p {};
    p.fill (v);
    const auto s = stereoEnhancerFromParams (p.data());
    char t[48];
    switch (param)
    {
        case 0: std::snprintf (t, sizeof (t), "%d%% %s", std::abs (s.pan) * 100 / 128, s.pan < 0 ? "L" : (s.pan > 0 ? "R" : "")); return t;
        case 1: std::snprintf (t, sizeof (t), "%.1f dB", 20.0 * std::log10 (std::max (1e-9f, enhancerVolume (s.volume)))); return t;
        case 2: std::snprintf (t, sizeof (t), "%d%%", s.separation * 100 / 96); return t;
        case 3:
            if (s.delay == 0)
                return "off";
            std::snprintf (t, sizeof (t), "%s %.1f ms", s.delay > 0 ? "R" : "L", enhancerDelayUnits (s.delay) / 100.0);
            return t;
        case 4: return s.post ? "post" : "pre";
        case 5: return s.invert == 1 ? "invert L" : (s.invert == 2 ? "invert R" : "none");
        default: return {};
    }
}

namespace
{
class StereoEnhancerEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        core.prepare (sampleRate);
        if (have)
            apply (st, true);
    }
    void reset() noexcept override { core.snap(); }
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        apply (stereoEnhancerFromParams (params.data()), ! have);
        runMatrix (core, left, right, n, false);
    }

private:
    MatrixCore core;
    StereoEnhancerState st;
    bool have = false;

    void apply (const StereoEnhancerState& s, bool all)
    {
        const auto old = st;
        st = s;
        have = true;
        if (all || old.delay != s.delay)
            core.setDelay (enhancerDelayUnits (s.delay), s.delay > 0);
        core.post = s.post > 0; // without a delay the order does not matter
        const auto c = enhancerGains (s);
        for (int k = 0; k < 4; ++k)
            core.gains[(std::size_t) k].target = c[(std::size_t) k];
    }
};
} // namespace

std::unique_ptr<Effect> makeStereoEnhancer() { return std::make_unique<StereoEnhancerEffect>(); }

// -------------------------------------------------------------------------------------------- Transient Processor

std::optional<TransientProcessorState> parseTransientProcessor (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 40 || i32At (d, 0) >= 2) // FL reads nothing from versions ≥ 2
        return std::nullopt;
    TransientProcessorState s;
    int* f[9] = { &s.attack, &s.drive, &s.release, &s.gain, &s.attackShape, &s.releaseShape, &s.split, &s.balance, &s.on };
    for (int i = 0; i < 9; ++i)
        *f[i] = i32At (d, 4 + 4 * (std::size_t) i);
    return s;
}

namespace
{
constexpr int tpMax[9] = { 160, 160, 160, 160, 2, 2, 160, 160, 1 };
}

void transientProcessorToParams (const TransientProcessorState& s, float* p) noexcept
{
    const int raw[9] = { s.attack, s.drive, s.release, s.gain, s.attackShape, s.releaseShape, s.split, s.balance, s.on };
    for (int i = 0; i < 9; ++i)
        p[i] = toUnit (raw[i], 0, tpMax[i]);
}

TransientProcessorState transientProcessorFromParams (const float* p) noexcept
{
    TransientProcessorState s;
    int* f[9] = { &s.attack, &s.drive, &s.release, &s.gain, &s.attackShape, &s.releaseShape, &s.split, &s.balance, &s.on };
    for (int i = 0; i < 9; ++i)
        *f[i] = fromUnit (p[i], 0, tpMax[i]);
    return s;
}

std::string transientProcessorParamText (int param, float v)
{
    if (param < 0 || param > 8)
        return {};
    const auto raw = fromUnit (v, 0, tpMax[param]);
    const auto x = (double) (float) ((double) raw / tpMax[param]);
    char t[48];
    switch (param)
    {
        case 0: case 2: std::snprintf (t, sizeof (t), "%.1f%%", x * 200.0 - 100.0); return t;
        case 1: std::snprintf (t, sizeof (t), "%.1f %%", x * 100.0); return t;
        case 3: std::snprintf (t, sizeof (t), "%.1f dB", (x - 0.5) * 18.0); return t;
        case 4: case 5: return raw == 0 ? "Sharp" : (raw == 1 ? "Mid" : "Slow");
        case 6: std::snprintf (t, sizeof (t), "%.1f Hz", x * x * 995.0 + 5.0); return t;
        case 7:
        {
            const auto dry = std::min (1.0, 2.0 * (1.0 - x)), wet = std::min (1.0, 2.0 * x);
            std::snprintf (t, sizeof (t), "%d%% Dry/%d%% Wet", (int) (dry * 100.0), (int) (wet * 100.0));
            return t;
        }
        case 8: return raw ? "on" : "off";
        default: return {};
    }
}

namespace
{
float powf_ (double a, double b) noexcept { return (float) std::pow (a, b); }

// the transient engine (REA_rodada54 §4): a one-pole split, a detector driving a gain on the high band, a soft
// saturator, all in single precision; parameters glide every 8 samples
class TransientEngine
{
public:
    TransientEngine()
    {
        detectorCoefficients (44100.0f);
        for (int i = 0; i < 8; ++i)
            apply (i, 0.5f);
    }

    // the host's parameter call: smoothed parameters are queued, the shapes apply at once
    void setParameter (int i, float x) noexcept
    {
        if (i == 4 || i == 5)
            apply (i, x);
        else if (i >= 0 && i < 8)
        {
            bool found = false;
            for (int k = 0; k < queued; ++k)
                if (queue[k].index == i)
                {
                    queue[k].target = x;
                    found = true;
                    break;
                }
            if (! found && queued < 30)
                queue[queued++] = { i, x };
        }
        if (i >= 0 && i < 8)
            shown[i] = x;
    }
    void applyAll() noexcept // the wrapper's call after a state load: every value at once
    {
        for (int i = 0; i < 8; ++i)
            apply (i, shown[i]);
    }
    void setSampleRate (float fs) noexcept
    {
        k = 1.0f - (float) std::pow (0.1, 200.0 / (double) fs);
        detectorCoefficients (fs);
        inv = 1.0f / fs;
        actual[6] = shown[6];
        crossover (shown[6]);
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            if (++counter == 8)
            {
                counter = 0;
                for (int j = 0; j < queued;)
                {
                    const auto idx = queue[j].index;
                    const auto target = queue[j].target;
                    const auto cur = actual[idx];
                    const auto next = cur + (target - cur) * k;
                    if (std::fabs (next - target) < 9.999999747378752e-05f)
                    {
                        apply (idx, target);
                        for (int m = j; m + 1 < queued; ++m)
                            queue[m] = queue[m + 1];
                        --queued;
                    }
                    else
                    {
                        apply (idx, next);
                        ++j;
                    }
                }
            }
            const auto l = inL[i], r = inR[i];
            const auto yR = lp[0] * r + lp[1] * lx1R + lp[2] * ly1R;
            const auto yL = lp[0] * l + lp[1] * lx1L + lp[2] * ly1L;
            lx1L = l;
            lx1R = r;
            ly1L = yL;
            ly1R = yR;
            const auto hR = hp[0] * r + hp[1] * hx1R + hp[2] * hy1R;
            const auto hL = hp[1] * hx1L + hp[0] * l + hp[2] * hy1L;
            hx1L = l;
            hx1R = r;
            hy1L = hL;
            hy1R = hR;
            float pa = 0.0f, pb = 0.0f;
            detector (hL, hR, pa, pb);
            outL[i] = (g1 * yL + g2 * pa) * gain;
            outR[i] = (g1 * yR + g2 * pb) * gain;
        }
    }

private:
    struct Pending
    {
        int index = 0;
        float target = 0.0f;
    };
    Pending queue[30] {};
    int queued = 0, counter = 0;
    float shown[8] { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    float actual[8] {};
    float k = 1e-05f, inv = 0.0f, fsD = 44100.0f;
    float lp[3] {}, hp[3] {};
    float lx1L = 0, lx1R = 0, ly1L = 0, ly1R = 0, hx1L = 0, hx1R = 0, hy1L = 0, hy1R = 0;
    float gain = 1.0f, g1 = 1.0f, g2 = 1.0f;
    // detector parameters (current / target pairs advance once per sample) and coefficients
    float drive = 0.01f, driveTarget = 0.1f, attackAmt = 0.0f, attackTarget = 0.0f, releaseAmt = 0.0f, releaseTarget = 0.0f;
    float outScale = 1.0f, outScaleTarget = 1.0f, outUnit = 1.0f;
    float attackShape = 1.0f, releaseShape = 1.0f;
    float decay = 0, envAttack = 0, relBase = 0, relRange = 0, holdDecay = 0, peakDecay = 0;
    float pk = 0, env = 0, hold = 0, slow = 0, peak = 0;

    void detectorCoefficients (float fs) noexcept
    {
        fsD = fs;
        const auto d = (double) fs;
        decay = (float) std::pow (0.1, 5.0 / d);
        envAttack = 1.0f - (float) std::pow (0.1, 1.0 / ((double) attackShape * 0.01 * d));
        relRange = (float) std::pow (0.1, 1.0 / ((double) releaseShape * 0.02 * d));
        relBase = 1.0f - relRange;
        holdDecay = (float) std::pow (0.1, 1.0 / ((double) releaseShape * 0.1 * d));
        peakDecay = (float) std::pow (0.1, 1.0 / (double) (fs * 4.0f));
    }
    void crossover (float v) noexcept
    {
        const auto w = v * v * inv * 3125.884765625f + inv * 15.707963943481445f;
        const auto t = (float) std::tan ((double) w);
        const auto d = 1.0f / (t + 1.0f);
        hp[0] = d;
        hp[1] = -d;
        hp[2] = -((t - 1.0f) * d);
        lp[0] = lp[1] = d * t;
        lp[2] = -((t - 1.0f) * d);
    }
    void apply (int i, float x) noexcept
    {
        actual[i] = x;
        switch (i)
        {
            case 0: case 2:
            {
                const auto u = ((x - 0.5f) + x) - 0.5f;
                auto q = u * u;
                if (! (u >= 0.0f))
                    q = -q;
                if (i == 0)
                    attackTarget = (q >= 0.0f ? 0.4000000059604645f : 0.6000000238418579f) * q;
                else
                    releaseTarget = q >= 0.0f ? q * 0.6000000238418579f : q;
                break;
            }
            case 1:
                driveTarget = x < 0.10000000149011612f ? x * 9.0f + 0.10000000149011612f
                                                       : powf_ (15.0, (double) ((x - 0.10000000149011612f) * 1.111111044883728f));
                break;
            case 3: gain = powf_ (10.0, (double) ((x - 0.5f) * 0.9000000357627869f)); break;
            case 4: case 5:
            {
                const auto shape = (double) x < 0.333 ? 0.5f : ((double) x < 0.666 ? 1.0f : 2.0f);
                (i == 4 ? attackShape : releaseShape) = shape;
                detectorCoefficients (fsD);
                break;
            }
            case 6: crossover (x); break;
            case 7:
                g1 = std::min ((1.0f - x) + (1.0f - x), 1.0f);
                g2 = std::min (x + x, 1.0f);
                break;
            default: break;
        }
    }
    static float saturate (float v) noexcept
    {
        if (v < -3.0f)
            return -1.0f;
        if (v > 3.0f)
            return 1.0f;
        return ((v * v + 27.0f) * v) / (v * 9.0f * v + 27.0f);
    }
    void detector (float a, float b, float& oa, float& ob) noexcept
    {
        const auto dScale = outUnit * outScaleTarget - outScale;
        const auto dAttack = attackTarget - attackAmt;
        const auto dRelease = releaseTarget - releaseAmt;
        const auto dDrive = driveTarget - drive;
        const auto m = (std::fabs (b) + std::fabs (a)) * 0.5f;
        pk = m > pk ? m : pk * decay;
        env = m > env ? (m - env) * envAttack + env : env * decay;
        float r = 1.0f;
        if (env > 0.0f)
        {
            r = pk / env;
            if (r >= 1.0f)
            {
                if (attackAmt >= 0.0f)
                    r = r > 10.0f ? 6.599999904632568f : r * (0.9600000381469727f - r * 0.003000000026077032f * r);
            }
            else
                r = 1.0f;
        }
        peak = m >= peak ? m : peak * peakDecay;
        hold = m >= hold ? m : hold * holdDecay;
        const auto x6 = hold - slow;
        float q = 1.0f;
        if (releaseAmt >= 0.0f && hold > 0.0f)
        {
            const auto h = hold + 9.999999747378752e-05f;
            q = (h - slow) / h;
        }
        q = std::min (std::max (q * 3.0f, 0.0f), 1.0f);
        slow = (q * relRange + relBase) * x6 + slow;
        float t = 1.0f;
        if (slow > 9.999999974752427e-07f)
        {
            t = peak / slow;
            if (t >= 1.0f)
            {
                if (releaseAmt >= 0.0f)
                    t = t > 20.0f ? 12.0f : t * 0.8399999737739563f - (t * t) * (t * 0.0006000000284984708f);
            }
            else
                t = 1.0f;
        }
        auto g = (t - 1.0f) * releaseAmt + (r - 1.0f) * attackAmt;
        g = g >= 0.0f ? g + 1.0f : 1.0f / (1.0f - g);
        const auto ag = a * g, bg = b * g;
        oa = saturate (drive * ag) / drive * outScale;
        ob = saturate (drive * bg) / drive * outScale;
        outScale = dScale + outScale;
        attackAmt = dAttack + attackAmt;
        releaseAmt = dRelease + releaseAmt;
        drive = dDrive + drive;
    }
};

class TransientProcessorEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        tail = (int) std::nearbyint (std::lround (fs) * 1.5) + 88;
        fresh = true;
    }
    void reset() noexcept override {} // FL's Flush showed no effect on this plugin (REA_rodada54 §4.6)
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        const auto s = transientProcessorFromParams (params.data());
        if (fresh)
            start (s);
        else
            update (s);
        Ftz ftz;
        for (int done = 0; done < n;)
        {
            const auto m = std::min (flBlock, n - done);
            block (left + done, right + done, m);
            done += m;
        }
    }

private:
    double fs = 44100.0;
    int tail = 66238, quiet = 0;
    bool fresh = true;
    TransientProcessorState st;
    std::unique_ptr<TransientEngine> engine;
    float outL[flBlock] {}, outR[flBlock] {};

    static float unit (int raw, int i) noexcept
    {
        const auto v = std::clamp ((double) raw * (1.0 / tpMax[i]), 0.0, 1.0);
        return (float) v;
    }
    void start (const TransientProcessorState& s)
    {
        engine = std::make_unique<TransientEngine>();
        const int raw[9] = { s.attack, s.drive, s.release, s.gain, s.attackShape, s.releaseShape, s.split, s.balance, s.on };
        for (int i = 0; i < 8; ++i)
            engine->setParameter (i, unit (raw[i], i));
        engine->applyAll();
        engine->setSampleRate ((float) std::lround (fs));
        st = s;
        quiet = 0;
        fresh = false;
    }
    void update (const TransientProcessorState& s) noexcept
    {
        const int now[9] = { s.attack, s.drive, s.release, s.gain, s.attackShape, s.releaseShape, s.split, s.balance, s.on };
        const int was[9] = { st.attack, st.drive, st.release, st.gain, st.attackShape, st.releaseShape, st.split, st.balance, st.on };
        for (int i = 0; i < 8; ++i)
            if (now[i] != was[i])
                engine->setParameter (i, unit (now[i], i));
        st = s;
    }
    void block (float* left, float* right, int n) noexcept
    {
        // after 1.5 s (+88 samples) of input within ±10⁻⁶ the plugin passes its input and the engine stops
        bool silent = true;
        for (int i = 0; i < n && silent; ++i)
            silent = std::fabs (left[i]) <= 9.999999974752427e-07f && std::fabs (right[i]) <= 9.999999974752427e-07f;
        quiet = silent ? quiet - n : tail;
        if (! (-n < quiet))
        {
            quiet = -0x3fffffff;
            return;
        }
        if (st.on == 0)
            return;
        engine->process (left, right, outL, outR, n);
        std::copy (outL, outL + n, left);
        std::copy (outR, outR + n, right);
    }
};
} // namespace

std::unique_ptr<Effect> makeTransientProcessor() { return std::make_unique<TransientProcessorEffect>(); }

// ------------------------------------------------------------------------------------------------------- Spreader

std::optional<SpreaderState> parseSpreader (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 24)
        return std::nullopt;
    const auto version = (std::uint32_t) i32At (d, 0);
    if (version != 0x10002 && version != 0x10003) // older layouts occur in no installation state
        return std::nullopt;
    SpreaderState s;
    int* f[5] = { &s.width, &s.separation, &s.lowBypass, &s.monoBass, &s.enable };
    for (int i = 0; i < 5; ++i)
        *f[i] = i32At (d, 4 + 4 * (std::size_t) i);
    return s;
}

namespace
{
constexpr int spLo[5] = { 0, -10000, 1000, 0, 0 }, spHi[5] = { 10000, 10000, 1000000, 1, 1 };

// FL's normalised control value (double, clamped), as the plugin hands it on
double spreaderUnit (int raw, int i) noexcept
{
    return std::clamp ((double) (raw - spLo[i]) * (1.0 / (spHi[i] - spLo[i])), 0.0, 1.0);
}
// low bypass control → Hz: 0…500 Hz over the first half, 500 Hz…10 kHz over the second, at least 10 Hz
float spreaderLowHz (float unit) noexcept
{
    auto x = (double) unit;
    double lo = 0.0, hi = 1000.0;
    if (! (0.5 >= x))
    {
        x = (x - 0.5) * 2.0;
        lo = 500.0;
        hi = 10000.0;
    }
    const auto v = x > 0.0 ? std::exp (std::log (x)) : 0.0;
    const auto hz = (float) (lo + (hi - lo) * v);
    return std::isnan (hz) || hz < 10.0f ? 10.0f : hz;
}
} // namespace

void spreaderToParams (const SpreaderState& s, float* p) noexcept
{
    const int raw[5] = { s.width, s.separation, s.lowBypass, s.monoBass, s.enable };
    for (int i = 0; i < 5; ++i)
        p[i] = toUnit (raw[i], spLo[i], spHi[i]);
}

SpreaderState spreaderFromParams (const float* p) noexcept
{
    SpreaderState s;
    int* f[5] = { &s.width, &s.separation, &s.lowBypass, &s.monoBass, &s.enable };
    for (int i = 0; i < 5; ++i)
        *f[i] = fromUnit (p[i], spLo[i], spHi[i]);
    return s;
}

std::string spreaderParamText (int param, float v)
{
    if (param < 0 || param > 4)
        return {};
    const auto raw = fromUnit (v, spLo[param], spHi[param]);
    char t[48];
    switch (param)
    {
        case 0: std::snprintf (t, sizeof (t), "%.0f%%", raw / 100.0); return t;
        case 1: std::snprintf (t, sizeof (t), "%.0f%%", raw / 100.0); return t;
        case 2: std::snprintf (t, sizeof (t), "%.0f Hz", (double) spreaderLowHz ((float) spreaderUnit (raw, 2))); return t;
        default: return raw ? "on" : "off";
    }
}

namespace
{
// the Spreader engine (REA_rodada54 §5): a Faust-built single-precision stereo widener with smoothed controls
class SpreaderEngine
{
public:
    float width = 1.0f, separation = 0.5f, lowBypass = 10.0f, monoBass = 0.0f, enable = 0.0f; // the engine's controls

    void constants (int sampleRate) noexcept
    {
        const auto r = (float) sampleRate;
        const auto c0 = std::min (r > 1.0f ? r : 1.0f, 192000.0f);
        const auto c1 = 1.0f / c0;
        k = (float) std::exp ((double) (c1 * -500.0f));
        k1 = 1.0f - k;
        wc = c1 * 3.1415927410125732f;
        maxDelay = (int) (c0 * 0.0063492064364254475f);
        maxDelayF = (float) maxDelay;
        fadeStep = 1.0f / (float) (int) (c0 * 0.02321995422244072f);
        const auto t = (float) std::tan ((double) (c1 * 47123.890625f));
        const auto f = 1.0f / t;
        cb4 = 1.0f - f;
        cc4 = f * 0.5f;
        cb8 = 1.0f / (f + 1.0f);
    }
    void clear() noexcept
    {
        lb = en = w = fade = fadeStepNow = 0.0f;
        std::fill (std::begin (s), std::end (s), 0.0f);
        b0 = bc = c8 = d0 = d8 = e0 = sepNow = mbNow = 0.0f;
        idx0 = idx1 = 0;
        iota = 0;
        std::fill (std::begin (lineA), std::end (lineA), 0.0f);
        std::fill (std::begin (lineB), std::end (lineB), 0.0f);
    }

    void compute (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        constexpr float sq2 = 1.4142135381698608f;
        constexpr float c796 = 0.7962143421173096f, c398 = 0.3981071710586548f;
        const auto aLb = k1 * lowBypass, aEn = k1 * enable, aW = k1 * width, aSep = k1 * separation, aMb = k1 * monoBass;
        for (int i = 0; i < n; ++i)
        {
            lb = lb * k + aLb;
            const auto t = (float) std::tan ((double) (lb * wc));
            const auto t2 = t + sq2;
            const auto g = 1.0f / (t2 * t + 1.0f);
            en = en * k + aEn;
            const auto xL = inL[i], xR = inR[i];
            float hpL, lowL, hpR, lowR;
            chain (en * xL, t, t2, g, s, hpL, lowL);
            chain (en * xR, t, t2, g, s + 4, hpR, lowR);
            const auto io = iota & 2047;
            lineA[io] = (lowR + hpL) - (hpR + lowL);
            w = w * k + aW;
            const auto iv = (int) (w * maxDelayF);
            float x = 0.0f;
            if (fadeStepNow != 0.0f)
                x = (fade > 0.0f && fade < 1.0f) ? fadeStepNow : 0.0f;
            else if (fade == 0.0f)
                x = iv != idx0 ? fadeStep : 0.0f;
            else if (fade == 1.0f)
                x = iv != idx1 ? -fadeStep : 0.0f;
            fadeStepNow = x;
            const auto y = fade + x;
            const auto f = std::max (y >= 1.0f ? 1.0f : y, 0.0f);
            const auto n0 = (fade >= 1.0f && idx1 != iv) ? iv : idx0;
            const auto n1 = (fade <= 0.0f && idx0 != iv) ? iv : idx1;
            fade = f;
            const auto d1 = std::min (n0 > 0 ? n0 : 0, maxDelay), d2 = std::min (n1 > 0 ? n1 : 0, maxDelay);
            const auto a = lineA[(iota - d1) & 2047], b = lineA[(iota - d2) & 2047];
            const auto x10 = ((a - b) * f - a) * w;
            const auto nbc = -(((b0 + x10) * 0.5f + cb4 * bc) * cb8);
            const auto nc8 = -(((x10 - b0) * cc4 + cb4 * c8) * cb8);
            const auto hps = hpR + hpL, lows = lowR + lowL;
            const auto hx = nc8 * c796 + hps;
            lineB[io] = hps - lows;
            const auto c = lineB[(iota - d1) & 2047], d = lineB[(iota - d2) & 2047];
            const auto nd0 = (c - (c - d) * f) * w;
            const auto mb = k * mbNow + aMb;
            const auto sep = k * sepNow + aSep;
            const auto ne0 = ((nd0 - d0) * cc4 - cb4 * e0) * cb8;
            const auto nd8 = ((d0 + nd0) * 0.5f - cb4 * d8) * cb8;
            const auto mid = (((mb * lows + nbc) + (hx + nbc)) - lows) * 0.5f;
            const auto side0 = (lows - ((nbc - (hpL - lowL)) * 2.0f + hx)) * 0.5f;
            const auto side = ((side0 + (ne0 + nc8) * c398) + (nd8 + nbc)) * (sep + sep);
            const auto dry = 1.0f - en, mono = 1.0f - mb;
            outL[i] = (dry * xL + mid) + (mono * lowL + side);
            outR[i] = ((mono * lowR + dry * xR) + mid) - side;
            b0 = x10;
            bc = nbc;
            c8 = nc8;
            d0 = nd0;
            d8 = nd8;
            e0 = ne0;
            mbNow = mb;
            sepNow = sep;
            idx0 = n0;
            idx1 = n1;
            ++iota;
        }
    }

private:
    float k = 0, k1 = 0, wc = 0, maxDelayF = 0, fadeStep = 0, cb4 = 0, cc4 = 0, cb8 = 0;
    int maxDelay = 0;
    float lb = 0, en = 0, w = 0, fade = 0, fadeStepNow = 0;
    float s[8] {}; // two cascaded 2-pole sections per channel
    float b0 = 0, bc = 0, c8 = 0, d0 = 0, d8 = 0, e0 = 0, sepNow = 0, mbNow = 0;
    int idx0 = 0, idx1 = 0;
    unsigned iota = 0;
    float lineA[2048] {}, lineB[2048] {};

    static void chain (float e, float t, float t2, float g, float* st, float& hp, float& low) noexcept
    {
        const auto v1 = ((e - (st[0] * t2 + st[1])) * t) * g;
        const auto na = (v1 + v1) + st[0];
        const auto u1 = st[0] + v1;
        hp = e - u1 * sq8c;
        const auto w1 = u1 * t;
        const auto nb = (w1 + w1) + st[1];
        const auto bb = w1 + st[1];
        const auto v2 = ((bb - (st[2] * t2 + st[3])) * t) * g;
        const auto nc = (v2 + v2) + st[2];
        const auto u2 = st[2] + v2;
        const auto w2 = u2 * t;
        const auto nd = (w2 + w2) + st[3];
        low = st[3] + w2;
        st[0] = na;
        st[1] = nb;
        st[2] = nc;
        st[3] = nd;
    }
    static constexpr float sq8c = 2.8284270763397217f;
};

class SpreaderEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        rate = (int) std::lround (sampleRate > 0.0 ? sampleRate : 44100.0);
        engine.constants (rate);
        engine.clear(); // the plugin clears its engine when the sample rate is set
    }
    void reset() noexcept override { engine.clear(); } // FL's Flush clears the engine too
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        const auto s = spreaderFromParams (params.data());
        engine.width = (float) spreaderUnit (s.width, 0);
        engine.separation = (float) spreaderUnit (s.separation, 1);
        engine.lowBypass = spreaderLowHz ((float) spreaderUnit (s.lowBypass, 2));
        engine.monoBass = (float) s.monoBass;
        engine.enable = (float) s.enable;
        Ftz ftz;
        for (int done = 0; done < n;)
        {
            const auto m = std::min (flBlock, n - done);
            engine.compute (left + done, right + done, outL, outR, m);
            // FL's engine diverges where its fixed 15 kHz section passes Nyquist (22.05 kHz): keep NaN/inf out of
            // the mixer (REA_rodada54 §5.2)
            for (int i = 0; i < m; ++i)
            {
                left[done + i] = std::isfinite (outL[i]) ? outL[i] : 0.0f;
                right[done + i] = std::isfinite (outR[i]) ? outR[i] : 0.0f;
            }
            done += m;
        }
    }

private:
    int rate = 44100;
    SpreaderEngine engine;
    float outL[flBlock] {}, outR[flBlock] {};
};
} // namespace

std::unique_ptr<Effect> makeSpreader() { return std::make_unique<SpreaderEffect>(); }
} // namespace lynx::stereofx
