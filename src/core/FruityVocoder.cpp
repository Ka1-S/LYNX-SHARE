#include "core/FruityVocoder.h"

#include "core/DelphiMath.h"
#include "core/Denormals.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

// The plugin's operation order and roundings are reproduced exactly: no fused multiply-adds (the native-CPU builds
// would contract them otherwise).
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

// Fruity Vocoder, exact engine (docs/analise/REA_rodada70.md). Precision notes, all observed in the plugin:
// - the set-up mixes float and double exactly as written below (every cast is deliberate);
// - the biquads run in double with float buffers between the cascaded stages; at the end of every block each state
//   value below 2^-24 in magnitude is set to zero;
// - the envelope follower runs on the x87 FPU: the envelope is carried in extended precision inside a block (long
//   double here, under a 64-bit precision control word) and stored as a float between blocks;
// - the carrier × envelope products are summed on the x87 as well (one extended rounding, then float); the scalar
//   mixes (hold, modulator and carrier levels) are plain float multiply-adds.

namespace lynx::fvocoder
{
namespace
{
using ld = long double;

#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
struct X87Extended // 64-bit precision, round to nearest, for the follower and the product sums
{
    unsigned short saved = 0;
    X87Extended() noexcept
    {
        asm volatile ("fnstcw %0" : "=m"(saved));
        unsigned short cw = (unsigned short) ((saved & ~0x0F00) | 0x300);
        asm volatile ("fldcw %0" : : "m"(cw) : "memory");
    }
    ~X87Extended() { asm volatile ("fldcw %0" : : "m"(saved) : "memory"); }
};
#else
struct X87Extended
{
};
#endif

constexpr std::array<int, controls> minimum { 0, 0, 0, 0, -1200, 5, 10, 10, 0, 0, 0, 0 };
constexpr std::array<int, controls> maximum { 65536, 65536, 128, 1, 1200, 200, 1000, 1000, 1, 160, 160, 160 };
// The plugin keeps whatever raw value a saved state or ProcessParam gives it (no clamp to the knob ranges above,
// REA_rodada78 §1). LYNX does the same inside these wider bounds, which only keep the arithmetic finite and defined
// (a zero band width or time divides by zero, a huge scale overflows the integer rounding); invert and hold are
// tests (> 0, != 0) and take any value.
constexpr std::array<int, controls> guardMin { 0, 0, 0, INT_MIN, -120000, 1, 1, 1, INT_MIN, -1280, -1280, -1280 };
constexpr std::array<int, controls> guardMax { 262144, 262144, 512, INT_MAX, 120000, 2000, 1000000, 1000000, INT_MAX, 1280, 1280, 1280 };

const double ln2Per1200 = dmath::ln (2.0) / 1200.0; // cents ↔ ln
const double halfLn2 = dmath::ln (2.0) / 2.0;
const float ln11f = (float) dmath::ln (11.0);
constexpr float inv128 = 0.0078125f;
constexpr std::uint32_t silentBits = 0x33800000u; // 2^-24 as float bits
constexpr double stateFloor = 5.9604644775390625e-08; // 2^-24

std::int32_t i32 (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    std::int32_t v;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}

void put32 (std::vector<std::uint8_t>& d, std::int32_t v)
{
    std::uint8_t b[4];
    std::memcpy (b, &v, 4);
    d.insert (d.end(), b, b + 4);
}

// FL's band edge (Hz) → cents (0 = C at 16.35 Hz) and back
float centsOf (float hz) noexcept { return (float) (dmath::ln ((double) (hz / 440.0f)) / ln2Per1200 + 5700.0); }
float hzOf (float cents) noexcept { return (float) (dmath::exp ((double) (cents - 5700.0f) * ln2Per1200) * 440.0); }

} // namespace

int controlMin (int c) noexcept { return c >= 0 && c < controls ? minimum[(std::size_t) c] : 0; }
int controlMax (int c) noexcept { return c >= 0 && c < controls ? maximum[(std::size_t) c] : 0; }
int rawFromKnob (float knob, int c, int savedRaw) noexcept
{
    const auto lo = controlMin (c), hi = controlMax (c);
    const auto raw = lo + (int) std::lround (std::clamp (knob, 0.0f, 1.0f) * (float) (hi - lo));
    return raw == std::clamp (savedRaw, lo, hi) ? savedRaw : raw;
}

int controlDefault (int c) noexcept { return c >= 0 && c < controls ? State().c[(std::size_t) c] : 0; }

bool validBands (int n) noexcept
{
    return n == 4 || n == 8 || n == 16 || n == 32 || n == 64 || n == 128;
}

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 17 || i32 (d, 0) < 2)
        return std::nullopt;
    State s;
    s.bands = i32 (d, 4);
    if (! validBands (s.bands) || d.size() < 17 + 4 * (std::size_t) s.bands + 4 * controls)
        return std::nullopt;
    s.order = std::clamp ((int) i32 (d, 8), 1, maxOrder);
    s.listen = std::clamp ((int) i32 (d, 12), 0, 2);
    s.swap = d[16] != 0;
    for (int k = 0; k < s.bands; ++k)
    {
        std::memcpy (&s.levels[(std::size_t) k], d.data() + 17 + 4 * (std::size_t) k, 4);
        if (! std::isfinite (s.levels[(std::size_t) k]))
            s.levels[(std::size_t) k] = 1.0f;
    }
    for (int c = 0; c < controls; ++c)
        s.c[(std::size_t) c] = i32 (d, 17 + 4 * (std::size_t) s.bands + 4 * (std::size_t) c);
    return s;
}

std::vector<std::uint8_t> serialise (const State& s)
{
    std::vector<std::uint8_t> d;
    put32 (d, 2);
    put32 (d, s.bands);
    put32 (d, s.order);
    put32 (d, s.listen);
    d.push_back (s.swap ? 1 : 0);
    for (int k = 0; k < s.bands; ++k)
    {
        std::uint8_t b[4];
        std::memcpy (b, &s.levels[(std::size_t) k], 4);
        d.insert (d.end(), b, b + 4);
    }
    for (int c = 0; c < controls; ++c)
        put32 (d, s.c[(std::size_t) c]);
    return d;
}

float bandHz (const State& s, int band) noexcept
{
    auto lo = edgeHz (s.c[freqMin]), hi = edgeHz (s.c[freqMax]);
    if (lo > hi)
        lo = hi - 1.0f;
    const auto sv = (float) s.c[freqScale] / 128.0f;
    const auto scale = (float) (std::llrint ((double) (float) (dmath::exp ((double) sv * dmath::ln (10000.0)) - 1.0)) + 1);
    const auto lnScale = dmath::ln ((double) (scale + 1.0f));
    const auto edge = [&] (int i) {
        const auto t = (float) ((double) i / (double) std::max (1, s.bands));
        const auto u = (float) (dmath::exp ((double) t * lnScale) - 1.0) / scale;
        return centsOf (lo + u * (hi - lo));
    };
    return hzOf ((edge (band) + edge (band + 1)) * 0.5f);
}

float edgeHz (int raw) noexcept
{
    const auto t = (float) ((double) raw * (1.0 / 65536.0));
    const auto e = dmath::exp ((double) t * dmath::ln (101.0));
    return (float) ((double) (float) (e - 1.0) * 159.9 + 10.0);
}

float mixLevel (int raw) noexcept
{
    const auto x = (float) raw * inv128;
    if (x == 1.0f)
        return 1.0f;
    return (float) ((dmath::exp ((double) (x * ln11f)) - 1.0) * (double) 0.1f);
}

void Engine::prepare (double sampleRate) noexcept
{
    rate = std::max (1, (int) std::lround (sampleRate));
    radPerHz = 2.0 * 3.141592653589793 / (double) rate;
    configure();
}

void Engine::reset() noexcept
{
    for (auto* bank : { &analysis, &synthesis })
        for (auto& b : *bank)
        {
            for (auto& z : b.z)
                z.fill (0.0);
            b.env = 0.0f;
        }
}

void Engine::setState (const State& s) noexcept
{
    const auto clear = s.bands != st.bands || s.order != st.order;
    st = s;
    st.bands = validBands (s.bands) ? s.bands : 16;
    st.order = std::clamp (s.order, 1, maxOrder);
    st.listen = std::clamp (s.listen, 0, 2);
    for (int c = 0; c < controls; ++c)
        st.c[(std::size_t) c] = std::clamp (s.c[(std::size_t) c], guardMin[(std::size_t) c], guardMax[(std::size_t) c]);
    if (clear)
        reset();
    configure();
}

void Engine::design (Band& b) const noexcept
{
    const auto w = (double) b.hz * radPerHz;
    double s, c;
    dmath::sinCos (w, s, c);
    const auto alpha = s * dmath::sinh (halfLn2 * (double) b.octaves * w / s);
    const auto d = 1.0 / (alpha + 1.0);
    b.k.a1 = d * c * -2.0;
    b.k.a2 = d * (1.0 - alpha);
    b.k.b0 = d * alpha;
    b.k.b1 = 0.0;
    b.k.b2 = d * -alpha;
}

void Engine::buildBank (std::array<Band, maxBands>& bank, bool synth) noexcept
{
    const auto n = st.bands;
    const auto lnScale = dmath::ln ((double) (scale + 1.0f));
    const auto shift = (float) st.c[formant];
    const auto mirrored = st.c[invert] > 0;
    float prev = 0.0f;
    for (int i = 0; i <= n; ++i)
    {
        const auto t = (float) ((double) i / (double) n);
        const auto u = (float) (dmath::exp ((double) t * lnScale) - 1.0) / scale;
        auto edge = centsOf (fMin + u * (fMax - fMin));
        auto index = i - 1;
        if (synth)
        {
            edge += shift;
            if (mirrored)
                index = n - index - 1;
        }
        if (i > 0)
        {
            auto& b = bank[(std::size_t) index];
            b.hz = hzOf ((prev + edge) * 0.5f);
            b.active = b.hz >= 10.0f && top > b.hz;
            if (! synth && ! synthesis[(std::size_t) index].active)
                b.active = false;
            if (! b.active)
            {
                b.silent = true;
                b.gain = 0.0f;
                b.env = 0.0f;
                for (auto& z : b.z)
                    z.fill (0.0);
                for (auto& z : synthesis[(std::size_t) index].z)
                    z.fill (0.0);
            }
            else
            {
                b.octaves = bwPerCent * (edge - prev);
                design (b);
                b.gain = (synth ? 1.0f : st.levels[(std::size_t) index]) * bandGain;
            }
        }
        prev = edge;
    }
}

void Engine::configure() noexcept
{
    if (st.listen == 0)
    {
        gMod = 1.0f;
        gCar = 0.0f;
        gWet = 0.0f;
    }
    else if (st.listen == 1)
    {
        gMod = 0.0f;
        gCar = 1.0f;
        gWet = 0.0f;
    }
    else
    {
        gMod = mixLevel (st.c[modLevel]);
        gCar = mixLevel (st.c[carLevel]);
        gWet = mixLevel (st.c[wetLevel]);
    }
    fMin = edgeHz (st.c[freqMin]);
    fMax = edgeHz (st.c[freqMax]);
    if (fMin > fMax)
        fMin = fMax - 1.0f;
    const auto bw = (float) st.c[bandwidth];
    bandGain = (gWet * 1500.0f) / bw;
    bwPerCent = (bw * (float) st.order) / 120000.0f;
    const auto sv = (float) st.c[freqScale] * inv128;
    const auto e = dmath::exp ((double) sv * dmath::ln (10000.0));
    scale = (float) (std::llrint ((double) (float) (e - 1.0)) + 1);
    top = (float) ((double) rate * 20000.0 / 44100.0); // (in double: rate · 20000 overflows an int above 107 kHz)
    buildBank (synthesis, true);
    buildBank (analysis, false);
    const auto msScale = (float) ((double) rate * 0.001);
    const auto ln001 = (float) dmath::ln (0.01);
    for (int i = 0; i < 2; ++i)
        coef[(std::size_t) i] = (float) dmath::exp ((double) (ln001 / ((float) st.c[(std::size_t) (attack + i)] * msScale)));
}

void Engine::filter (Band& b, const float* in, float* out, int n) noexcept
{
    const auto& k = b.k;
    for (int s = 0; s < st.order; ++s)
    {
        auto& z = b.z[(std::size_t) s];
        double x1 = z[0], x2 = z[1], y1 = z[2], y2 = z[3];
        const float* src = s == 0 ? in : out;
        for (int i = 0; i < n; ++i)
        {
            const auto x = (double) src[i];
            const auto y = x * k.b0 + (((x1 * k.b1 - y2 * k.a2) + x2 * k.b2) - y1 * k.a1);
            out[i] = (float) y;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
        }
        z = { x1, x2, y1, y2 };
        for (auto& v : z)
            if (std::abs (v) < stateFloor)
                v = 0.0;
    }
}

void Engine::block (const float* mod, const float* car, float* out, int n) noexcept
{
    X87Extended fpu;
    std::fill (acc.begin(), acc.begin() + n, 0.0f);
    const auto holding = st.c[hold] != 0;
    const ld att = coef[0], rel = coef[1];
    for (int k = 0; k < st.bands; ++k)
    {
        auto& a = analysis[(std::size_t) k];
        if (a.silent && ! (a.gain > 0.0f))
            continue;
        auto& s = synthesis[(std::size_t) k];
        if (! holding)
        {
            filter (a, mod, work.data(), n);
            work[(std::size_t) n] = 0.0f; // the word after the block (FL's scratch is the output buffer; zero at EXEC)
            // envelope follower (x87, extended precision inside the block)
            const ld g = a.gain;
            ld env = a.env;
            int count = n;
            for (int i = 0; i < n; ++i)
            {
                const ld x = (ld) std::abs (work[(std::size_t) i]) * g;
                env = (env - x) * (x > env ? att : rel) + x;
                work[(std::size_t) i] = (float) env;
                std::uint32_t lo, hi;
                std::memcpy (&lo, &work[(std::size_t) i], 4);
                std::memcpy (&hi, &work[(std::size_t) i + 1], 4);
                if (hi == 0 && lo < silentBits)
                {
                    work[(std::size_t) i] = 0.0f;
                    work[(std::size_t) i + 1] = 0.0f;
                    env = 0.0L;
                    --count;
                }
            }
            a.env = (float) env;
            a.silent = count == 0;
            if (! a.silent)
            {
                filter (s, car, carWork.data(), n);
                for (int i = 0; i < n; ++i)
                    acc[(std::size_t) i] = (float) ((ld) carWork[(std::size_t) i] * (ld) work[(std::size_t) i] + (ld) acc[(std::size_t) i]);
            }
        }
        else if (! a.silent)
        {
            filter (s, car, carWork.data(), n);
            const auto e = a.env;
            for (int i = 0; i < n; ++i)
                acc[(std::size_t) i] = carWork[(std::size_t) i] * e + acc[(std::size_t) i];
        }
    }
    for (int i = 0; i < n; ++i)
        acc[(std::size_t) i] = mod[i] * gMod + acc[(std::size_t) i];
    for (int i = 0; i < n; ++i)
        acc[(std::size_t) i] = car[i] * gCar + acc[(std::size_t) i];
    std::copy (acc.begin(), acc.begin() + n, out);
}

void Engine::process (const float* mod, const float* car, float* out, int n) noexcept
{
    const ScopedFlushDenormals ftz; // the plugin runs with flush-to-zero and denormals-are-zero
    for (int done = 0; done < n;)
    {
        const auto len = std::min (blockFrames, n - done);
        block (mod + done, car + done, out + done, len);
        done += len;
    }
}

void Engine::processStereo (float* left, float* right, int n) noexcept
{
    const ScopedFlushDenormals ftz;
    for (int done = 0; done < n;)
    {
        const auto len = std::min (blockFrames, n - done);
        const float* l = left + done;
        const float* r = right + done;
        std::copy (l, l + len, modBuf.begin());
        std::copy (r, r + len, carBuf.begin());
        if (st.swap)
            block (carBuf.data(), modBuf.data(), left + done, len);
        else
            block (modBuf.data(), carBuf.data(), left + done, len);
        std::copy (left + done, left + done + len, right + done);
        done += len;
    }
}
} // namespace lynx::fvocoder
