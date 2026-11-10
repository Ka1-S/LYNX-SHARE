#include "core/FruityDynamics.h"

#include "core/Effects.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

// Rebuilt from the plugins' observed behaviour (docs/analise/REA_rodada48.md). Precision notes that matter for
// matching FL sample for sample:
// - control values are computed in float from the raw integers, with the transcendental steps in double;
// - the one-pole "curve" stages keep their state in x87 extended precision inside a block and as a double between
//   blocks (LYNX: long double, 80-bit on every x86-64 target it is built for); the limiter's RMS running sum likewise;
// - comparisons on levels are made on the float bit patterns (non-negative values), as the plugin does;
// - the IPP vector functions FL uses (log10, exp, ln, sqrt on floats) are computed here correctly rounded; FL's IPP
//   build differs from that by 1–2 ulps on some values, which is the only residual against FL.

namespace lynx::fldyn
{
const std::array<int, limiterControls> limiterMin { 0, 0, 0, 0, 1, 0, 1, 0, 0, -1000, -1000, 0, 0, 1, 0, 0, 0, 0 };
const std::array<int, limiterControls> limiterMax { 2000, 1000, 2000, 10000, 8, 10000, 8, 10000, 1000, 1000, 1000, 10000, 10000, 8, 10000, 1000, 1000, 10000 };
const std::array<int, limiterControls> limiterDefault { 1000, 1000, 1000, 1115, 3, 5000, 3, 2882, 1000, 0, 0, 0, 6666, 1, 650, 1000, 100, 4187 };
const std::array<int, compressorControls> compressorMin { -600, 4, -300, 0, 1, 0 };
const std::array<int, compressorControls> compressorMax { 0, 300, 300, 4000, 4000, 7 };
const std::array<int, compressorControls> compressorDefault { 0, 10, 0, 150, 200, 0 };

namespace
{
std::int32_t i32 (const std::vector<std::uint8_t>& d, std::size_t at)
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + at, 4);
    return v;
}

inline std::int32_t fbits (float f) noexcept
{
    std::int32_t b;
    std::memcpy (&b, &f, 4);
    return b;
}

inline int roundEven (double x) noexcept { return (int) std::nearbyint (x); } // cvtsd2si, round to nearest even

const double ln501 = std::log (501.0);
const double lnHundredth = std::log (0.01);
const float ln7f = (float) std::log (7.0);
const float ln3f = (float) std::log (3.0);

// FL's normalised exponential laws on a raw control (scale 1000): (k^x − 1)/(k − 1)
float law7 (int v) noexcept
{
    const auto x = (float) v * 0.001f;
    if (x == 1.0f)
        return 1.0f;
    return (float) ((std::exp ((double) (x * ln7f)) - 1.0) * (double) (1.0f / 6.0f));
}

float law3 (int v) noexcept
{
    const auto x = (float) v * 0.001f;
    if (x == 1.0f)
        return 1.0f;
    return (float) ((std::exp ((double) (x * ln3f)) - 1.0) * (double) 0.5f);
}

// Limiter time law in samples: 2·(501^(v/10000) − 1) ms
double timeSamples (int v, int sampleRate) noexcept
{
    const auto e = std::exp ((double) ((float) v * 1e-4f) * ln501);
    return (double) (float) (e - 1.0) * 2.0 * 0.001 * (double) sampleRate;
}

// --- one-pole curve stage (attack coefficient when rising, release when falling) ------------------------------------

struct Stage
{
    double attack = 0.0, release = 0.0, state = 0.0;
    bool single = false; // both coefficients equal: one coefficient for both directions

    void set (double a, double b) noexcept
    {
        attack = a <= 0.0 ? 0.0 : std::exp (lnHundredth / std::max (a, 1.0));
        release = b <= 0.0 ? 0.0 : std::exp (lnHundredth / std::max (b, 1.0));
        single = attack == release;
    }

    void run (float* buf, int n) noexcept
    {
        const long double a = attack, r = release;
        long double y = state;
        for (int i = 0; i < n; ++i)
        {
            const long double x = buf[i];
            const auto c = (single || x >= y) ? a : r;
            y = x + (y - x) * c;
            buf[i] = (float) y;
        }
        state = std::fabs (y) > (long double) 5.960464477539063e-08f ? (double) y : 0.0;
    }
};

// A chain of `count` stages over the block, the same as running them one after another (each stage's float output is
// the next one's input), but sample by sample: the stages' recurrences overlap instead of running back to back.
template <int count>
void runChainN (Stage* st, float* buf, int n) noexcept
{
    // coefficient table per stage: [falling, rising] (both the attack for a single-coefficient stage), picked by
    // index rather than by a branch on the signal
    long double y[count];
    double c[count][2];
    for (int s = 0; s < count; ++s)
    {
        y[s] = st[s].state;
        c[s][0] = st[s].single ? st[s].attack : st[s].release;
        c[s][1] = st[s].attack;
    }
    for (int i = 0; i < n; ++i)
    {
        float v = buf[i];
        for (int s = 0; s < count; ++s)
        {
            const long double x = v;
            const long double k = c[s][x >= y[s] ? 1 : 0];
            y[s] = x + (y[s] - x) * k;
            v = (float) y[s];
        }
        buf[i] = v;
    }
    for (int s = 0; s < count; ++s)
        st[s].state = std::fabs (y[s]) > (long double) 5.960464477539063e-08f ? (double) y[s] : 0.0;
}

// A block whose samples all equal every stage's state (a gain curve resting at a constant gain) is a fixed point of
// the chain: each step gives y = x + (x − x)·c = x, the float output is the input and the states stay as they are.
bool chainAtRest (const Stage* st, int count, const float* buf, int n) noexcept
{
    if (n <= 0)
        return false;
    const float v = buf[0];
    if (! (std::fabs ((long double) v) > (long double) 5.960464477539063e-08f)) // (the states would be flushed to 0)
        return false;
    for (int s = 0; s < count; ++s)
        if (! ((long double) st[s].state == (long double) v))
            return false;
    const auto bits = fbits (v);
    for (int i = 1; i < n; ++i)
        if (fbits (buf[i]) != bits)
            return false;
    return true;
}

void runChain (Stage* st, int count, float* buf, int n) noexcept
{
    if (count > 0 && chainAtRest (st, count, buf, n))
        return;
    switch (count)
    {
        case 0: return;
        case 1: st[0].run (buf, n); return;
        case 2: runChainN<2> (st, buf, n); return;
        case 3: runChainN<3> (st, buf, n); return;
        case 4: runChainN<4> (st, buf, n); return;
        default:
            runChainN<4> (st, buf, n);
            runChain (st + 4, count - 4, buf, n);
            return;
    }
}

// --- limiter peak window: running maximum over the window, kept on the bit patterns ---------------------------------

struct PeakWindow
{
    std::vector<std::int32_t> ring, queue;
    int size = 0, pos = 0, head = 1, tail = 0, count = 0;

    void allocate (int maxSize)
    {
        ring.assign ((std::size_t) std::max (maxSize, 1), 0);
        queue.assign (ring.size(), 0);
    }

    void setSize (int w) noexcept
    {
        w = std::clamp (w, 1, (int) ring.size());
        if (w == size)
            return;
        size = w;
        std::fill (ring.begin(), ring.begin() + w, 0); // fresh memory
        std::fill (queue.begin(), queue.begin() + w, 0);
        reset();
    }

    void reset() noexcept
    {
        ring[0] = 0;
        pos = 0;
        head = 1;
        tail = 0;
        count = 0;
    }

    void run (float* buf, int n) noexcept
    {
        const int w = size;
        if (w < 2)
            return;
        for (int i = 0; i < n; ++i)
        {
            const auto x = fbits (buf[i]);
            const int prev = pos;
            if (++pos >= w)
                pos = 0;
            const auto ref = count > 0 ? ring[(std::size_t) queue[(std::size_t) head]] : ring[(std::size_t) prev];
            if (x > ring[(std::size_t) prev])
            {
                while (count > 0)
                {
                    if (x > ring[(std::size_t) queue[(std::size_t) tail]])
                    {
                        --count;
                        if (--tail < 0)
                            tail = w - 1;
                        continue;
                    }
                    const auto t = queue[(std::size_t) head] - 1;
                    if (pos == t || pos == t + w)
                    {
                        --count;
                        if (++head >= w)
                            head = 0;
                    }
                    break;
                }
            }
            else
            {
                if (++tail >= w)
                    tail = 0;
                queue[(std::size_t) tail] = prev;
                const auto t = queue[(std::size_t) head] - 1;
                if (pos == t || pos == t + w)
                {
                    if (++head >= w)
                        head = 0;
                }
                else
                    ++count;
            }
            ring[(std::size_t) pos] = x;
            const auto y = x < ref ? ref : x;
            std::memcpy (buf + i, &y, 4);
        }
    }
};

// --- limiter compression detector: RMS of L² + R² over the window -------------------------------------------------

struct RmsWindow
{
    std::vector<float> ring;
    int size = 0, pos = 0;
    float scale = 1.0f;
    double sum = 0.0;

    void allocate (int maxSize) { ring.assign ((std::size_t) std::max (maxSize, 1), 0.0f); }

    void setSize (int w) noexcept
    {
        w = std::clamp (w, 1, (int) ring.size());
        if (w == size)
            return;
        size = w;
        scale = (float) (1.0 / (double) (w * 2));
        reset();
    }

    void reset() noexcept
    {
        std::fill (ring.begin(), ring.begin() + std::max (size, 1), 0.0f);
        pos = 0;
        sum = 0.0;
    }

    void run (const float* stereo, float* out, int n) noexcept
    {
        long double s = sum;
        const long double k = scale;
        for (int i = 0; i < n; ++i)
        {
            const auto l = stereo[2 * i], r = stereo[2 * i + 1];
            const auto sq = l * l + r * r;
            if (++pos >= size)
                pos = 0;
            s = (s - (long double) ring[(std::size_t) pos]) + (long double) sq;
            ring[(std::size_t) pos] = sq;
            const auto v = (float) (k * s);
            out[i] = std::sqrt (v < 0.0f ? 0.0f : v);
        }
        sum = (double) s;
    }
};

// ===================================================================================================================
// Fruity Limiter

class LimiterEngine
{
public:
    static constexpr int maxBlock = 512;

    void prepare (int rate)
    {
        sampleRate = std::max (rate, 1);
        const auto maxWindow = sampleRate + 2; // the time law tops at one second of samples
        peak.allocate (maxWindow);
        rms.allocate (maxWindow);
        delay.assign ((std::size_t) maxWindow * 2, 0.0f);
        level.assign (maxBlock, 0.0f);
        gain.assign (maxBlock, 0.0f);
        detect.assign (maxBlock, 0.0f);
        work.assign (maxBlock * 2, 0.0f);
        side.assign (maxBlock * 2, 0.0f);
        lookahead = -1;
        peak.size = rms.size = 0;
        configured = false;
    }

    void setControls (const std::array<int, limiterControls>& p, bool exact) noexcept
    {
        if (configured && p == raw && exact == exactCeiling)
            return;
        // ProcessParam groups: gains / times / release & compressor curve / threshold / ratio / knee
        const auto changed = [&] (std::initializer_list<int> idx) {
            if (! configured)
                return true;
            for (auto i : idx)
                if (p[(std::size_t) i] != raw[(std::size_t) i])
                    return true;
            return false;
        };
        const auto gainsChanged = changed ({ 0, 1, 2, 15, 16 }) || exact != exactCeiling;
        const auto timesChanged = changed ({ 3, 4, 7, 14 });
        const auto curvesChanged = changed ({ 5, 6, 11, 12, 13, 17 });
        const auto thresholdChanged = changed ({ 8 });
        const auto ratioChanged = changed ({ 9 });
        const auto kneeChanged = changed ({ 10 });
        raw = p;
        exactCeiling = exact;
        if (gainsChanged)
            setGains();
        if (thresholdChanged)
            setThreshold();
        if (ratioChanged)
            setRatio();
        if (kneeChanged)
            setKnee();
        if (timesChanged || ! configured)
            setTimes(); // includes the curves
        else if (curvesChanged)
            setCurves();
        if (! configured)
            reset();
        configured = true;
    }

    void reset() noexcept
    {
        for (int i = 0; i < gainStages; ++i)
            gainCurve[(std::size_t) i].state = 1.0;
        for (int i = 0; i < releaseStages; ++i)
            releaseCurve[(std::size_t) i].state = 0.0;
        for (int i = 0; i < compStages; ++i)
            compCurve[(std::size_t) i].state = 0.0;
        gateGain = 1.0f;
        std::fill (delay.begin(), delay.end(), 0.0f);
        delayPos = 0;
        silentFor = std::numeric_limits<int>::min() / 2 + 1; // 0xC0000001
    }

    int latency() const noexcept { return std::max (lookahead, 0); }

    // interleaved stereo in place; sideIn = interleaved side input or nullptr (own input)
    void process (float* io, const float* sideIn, int n) noexcept
    {
        // silence: after `holdSamples` of input at or below 1e-6 the plugin passes the input through untouched
        bool silent = true;
        for (int i = 0; i < 2 * n && silent; ++i)
            silent = std::fabs (io[i]) <= 1e-6f; // NaN counts as signal
        if (silent)
            silentFor -= n;
        else
            silentFor = holdSamples;
        if (silentFor <= -n)
        {
            if (silentFor > minSilent)
                reset();
            silentFor = minSilent;
            return;
        }

        auto* lv = level.data();
        for (int i = 0; i < n; ++i)
            lv[i] = std::max (std::fabs (io[2 * i]), std::fabs (io[2 * i + 1]));
        peak.run (lv, n);

        std::fill (gain.begin(), gain.begin() + n, gainLin);
        if (compOn)
        {
            rms.run (sideIn != nullptr ? sideIn : io, detect.data(), n);
            runChain (compCurve.data(), compStages, detect.data(), n);
            const auto thr = fbits (compThreshold);
            for (int i = 0; i < n; ++i)
                if (fbits (detect[(std::size_t) i]) > thr)
                    gain[(std::size_t) i] = compressorGain (detect[(std::size_t) i]);
        }

        // look-ahead: the audio is delayed by the attack time
        if (lookahead > 0)
            for (int i = 0; i < n; ++i)
            {
                std::swap (io[2 * i], delay[(std::size_t) delayPos * 2]);
                std::swap (io[2 * i + 1], delay[(std::size_t) delayPos * 2 + 1]);
                if (++delayPos >= lookahead)
                    delayPos = 0;
            }

        runChain (releaseCurve.data(), releaseStages, lv, n);

        const auto limitBits = fbits (limitLevel), gateBits = fbits (gateThreshold);
        for (int i = 0; i < n; ++i)
        {
            const auto l = lv[i];
            const auto g = gain[(std::size_t) i];
            auto r = fbits (l * g) > limitBits ? limitLevel / l : g;
            if (fbits (l) < gateBits)
                r = gateFloor + (gateGain - gateFloor) * gateRelease;
            gateGain = r;
            lv[i] = r;
        }
        runChain (gainCurve.data(), gainStages, lv, n);

        for (int i = 0; i < n; ++i)
        {
            io[2 * i] *= lv[i];
            io[2 * i + 1] *= lv[i];
        }

        if (saturate)
        {
            const long double t = satThreshold, r = satRange, k = satScale;
            const auto tb = fbits (satThreshold);
            for (int i = 0; i < 2 * n; ++i)
            {
                const auto a = std::fabs (io[i]);
                if (fbits (a) <= tb)
                    continue;
                const auto e = std::exp ((t - (long double) a) * k);
                const auto y = (float) ((1.0L - e) * r + t);
                io[i] = std::signbit (io[i]) ? -y : y;
            }
        }
        else
            for (int i = 0; i < 2 * n; ++i)
                if (std::fabs (io[i]) > ceiling)
                    io[i] = std::signbit (io[i]) ? -ceiling : ceiling;
    }

    std::vector<float> work, side;

private:
    static constexpr int minSilent = std::numeric_limits<int>::min() / 2 + 1;

    float compressorGain (float level) const noexcept
    {
        const auto v = level > compClamp ? compClamp : level;
        auto over = (float) std::log10 ((double) v);
        over *= 20.0f;
        over += -compThresholdDb;
        float d;
        if (knee != 0)
        {
            auto c = over + 1.0f;
            if (knee > 0)
            {
                c *= kneeAmount;
                c += 1.0f;
                c = (float) std::log ((double) c);
                c *= kneeScale;
            }
            else
            {
                c *= kneeScale;
                c = (float) std::exp ((double) c);
                c += -1.0f;
                c *= kneeInverse;
            }
            c += -1.0f;
            c *= ratioFactor;
            d = c - over;
        }
        else
            d = over * ratioSlope;
        const auto e = d * 0.115129255f; // ln 10 / 20
        const auto g = (float) std::exp ((double) e) * gainLin;
        // (a negative knee with a tiny amount overflows the curve to inf; at Gain −inf that was inf · 0 = NaN: round 111)
        return std::isfinite (g) ? g : (gainLin > 0.0f ? std::numeric_limits<float>::max() : 0.0f);
    }

    void setGains() noexcept
    {
        gainLin = law7 (raw[0]);
        ceiling = law3 (raw[2]);
        limitLevel = ceiling * (exactCeiling ? 1.0f : 0.975f);
        gateFloor = law7 (raw[15]);
        const auto gateOpen = law7 (raw[16]);
        gateThreshold = limitLevel <= gateOpen ? limitLevel : gateOpen;
        const auto sat = law3 (raw[1]);
        saturate = (double) sat < 1.0;
        if (saturate)
        {
            const auto c = ceiling < 1.0f ? ceiling : 1.0f;
            satThreshold = sat;
            satRange = c - sat;
            if (! (satRange > 0.0f))
                satRange = 1.52587890625e-05f;
            satScale = 1.0f / satRange;
        }
    }

    void setThreshold() noexcept
    {
        compOn = raw[8] < 1000;
        if (compOn)
        {
            const auto t = law7 (raw[8]);
            compThreshold = t <= 0.001f ? 0.001f : t; // floor −60 dB
        }
        else
            compThreshold = 100.0f;
        compThresholdDb = fbits (compThreshold) > 0 ? (float) (std::log10 ((double) compThreshold) * 20.0)
                                                     : -std::numeric_limits<float>::infinity();
        compClamp = compThreshold * 100.0f;
    }

    void setRatio() noexcept
    {
        const auto v = raw[9];
        const auto e = std::exp ((double) ((float) std::abs (v) * 0.001f) * std::log (51.0));
        ratioFactor = v < 1 ? (float) ((double) (float) (e - 1.0) * 0.06 + 1.0) : (float) (1.0 / ((double) (float) (e - 1.0) * 0.38 + 1.0));
        ratioSlope = ratioFactor - 1.0f;
    }

    void setKnee() noexcept
    {
        const auto v = raw[10];
        knee = v == 0 ? 0 : (v > 0 ? 1 : -1);
        const auto e = std::exp ((double) std::fabs ((float) v * 0.001f) * std::log (201.0));
        if (v < 1)
        {
            kneeAmount = (float) ((double) (float) (e - 1.0) * 0.001);
            kneeScale = (float) std::log ((double) (kneeAmount + 1.0f));
            kneeInverse = 1.0f / kneeAmount;
        }
        else
        {
            kneeAmount = (float) ((double) (float) (e - 1.0) * 0.005);
            kneeScale = (float) (1.0 / std::log ((double) (kneeAmount + 1.0f)));
        }
    }

    void setTimes() noexcept
    {
        const auto la = std::clamp (roundEven (timeSamples (raw[3], sampleRate)), 0, (int) delay.size() / 2);
        if (la != lookahead)
        {
            lookahead = la;
            std::fill (delay.begin(), delay.end(), 0.0f);
            delayPos = 0;
        }
        const auto window = std::max ({ la, 1, roundEven (timeSamples (raw[7], sampleRate)) });
        peak.setSize (window);
        rms.setSize (std::max (roundEven (timeSamples (raw[14], sampleRate)), 1));
        if (la < 1)
            gainStages = 0;
        else
        {
            const auto count = std::clamp (raw[4], 1, 8);
            if (count != gainStages)
            {
                gainStages = count;
                for (int i = 0; i < count; ++i)
                    gainCurve[(std::size_t) i].state = 1.0;
            }
            const auto c = (double) ((float) la * 0.8f) / std::sqrt ((double) count);
            for (int i = 0; i < count; ++i)
                gainCurve[(std::size_t) i].set (c, c);
        }
        setCurves();
        holdSamples = lookahead + roundEven ((double) sampleRate * 1.5);
    }

    void setCurves() noexcept
    {
        const auto release = timeSamples (raw[5], sampleRate);
        if (release <= 0.0)
            releaseStages = 0;
        else
        {
            const auto count = std::clamp (raw[6], 1, 8);
            if (count != releaseStages)
            {
                releaseStages = count;
                for (int i = 0; i < count; ++i)
                    releaseCurve[(std::size_t) i].state = 0.0;
            }
            const auto b = release * 2.0 / std::sqrt ((double) count);
            for (int i = 0; i < count; ++i)
                releaseCurve[(std::size_t) i].set (0.0, b);
        }
        const auto count = std::clamp (raw[13], 1, 8);
        if (count != compStages)
        {
            compStages = count;
            for (int i = 0; i < count; ++i)
                compCurve[(std::size_t) i].state = 0.0;
        }
        const auto k = 1.0 / std::sqrt ((double) count);
        const auto a = timeSamples (raw[11], sampleRate) * k;
        const auto b = timeSamples (raw[12], sampleRate) * 2.0 * k;
        for (int i = 0; i < count; ++i)
            compCurve[(std::size_t) i].set (a, b);
        const auto gate = std::max (timeSamples (raw[17], sampleRate), 1.0) * 2.0;
        gateRelease = (float) std::exp (lnHundredth / gate);
        gateGain = 1.0f;
    }

    int sampleRate = 44100;
    std::array<int, limiterControls> raw = limiterDefault;
    bool exactCeiling = false, configured = false;

    float gainLin = 1.0f, ceiling = 1.0f, limitLevel = 0.975f, gateFloor = 1.0f, gateThreshold = 0.0f;
    bool saturate = false;
    float satThreshold = 1.0f, satRange = 1.0f, satScale = 1.0f;
    bool compOn = false;
    float compThreshold = 100.0f, compThresholdDb = 40.0f, compClamp = 1e4f;
    float ratioFactor = 1.0f, ratioSlope = 0.0f;
    int knee = 0;
    float kneeAmount = 0.0f, kneeScale = 0.0f, kneeInverse = 0.0f;
    float gateRelease = 0.0f, gateGain = 1.0f;

    int lookahead = -1, delayPos = 0, holdSamples = 0, silentFor = minSilent;
    PeakWindow peak;
    RmsWindow rms;
    std::array<Stage, 8> gainCurve {}, releaseCurve {}, compCurve {};
    int gainStages = 0, releaseStages = 0, compStages = 0;
    std::vector<float> delay, gain, detect, level;
};

// ===================================================================================================================
// Fruity Compressor

class CompressorEngine
{
public:
    void prepare (int rate) noexcept
    {
        sampleRate = std::max (rate, 1);
        reset();
    }

    void setControls (const std::array<int, compressorControls>& p) noexcept
    {
        if (p != raw)
        {
            raw = p;
            dirty = true;
        }
    }

    void reset() noexcept // also what the plugin does when it is created
    {
        outPos = gainPos = 0;
        releaseNow = 0.5f;
        gainRead = 1.0f;
        follower = 0.0f;
        smooth = 1.0f;
        limitGain = 1.0f;
        hold = 0;
        gainRing.fill (1.0f);
        outRing.fill (0.0f);
        ap.fill (1e-30f);
        dirty = true;
    }

    // interleaved stereo; side = interleaved side input or nullptr (own signal)
    void process (const float* in, const float* side, float* out, int n) noexcept
    {
        if (dirty)
            setup();
        const auto slopeK = slope;
        const auto inv = (float) (1.0 / (double) (kneeHigh - kneeLow));
        const auto vintageTop = (kneeHigh - lnThreshold) * slope;
        auto mixK = lnThreshold - kneeLow;
        if (mixK > 1.0f)
            mixK = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            const auto xl = (float) ((double) in[2 * i] + 1e-30);
            const auto xr = (float) ((double) in[2 * i + 1] + 1e-30);
            ap[0] = (xl - ap[0]) * apCoef + ap[1];
            ap[1] = xl;
            const auto hl = (float) ((double) (xl - ap[0]) * 0.5);
            ap[2] = (xr - ap[2]) * apCoef + ap[3];
            ap[3] = xr;
            const auto hr = (float) ((double) (xr - ap[2]) * 0.5);

            float a = hl, b = hr;
            if (side != nullptr)
            {
                a = (float) ((double) side[2 * i] + 1e-30);
                b = (float) ((double) side[2 * i + 1] + 1e-30);
            }
            a = std::fabs (a);
            b = std::fabs (b);
            auto m = b > a ? b : a;
            m = (float) ((double) m + 1e-30);
            follower = m + (follower - m) * (m > follower ? fastAttack : fastRelease);

            auto g = 1.0f;
            const auto l = (float) std::log ((double) follower);
            if (l >= kneeLow)
            {
                if (vintage)
                {
                    if (l >= kneeHigh)
                        g = vintageCeiling;
                    else
                    {
                        const auto u = (l - kneeLow) * inv;
                        const auto t = (float) ((double) (u * u) * (3.0 - (double) u * 2.0));
                        const auto w = u + (t - u) * mixK;
                        g = (float) std::exp ((double) (vintageTop * w));
                    }
                }
                else if (l >= kneeHigh)
                    g = (float) std::exp ((double) ((l - lnThreshold) * slopeK));
                else
                {
                    const auto dd = l - kneeLow;
                    g = (float) std::exp ((double) (slopeK * dd * dd * kneeScale));
                }
            }
            g = g + (smooth - g) * (g > smooth ? releaseNow : attack);
            smooth = g;
            peakHold = g + (peakHold - g) * releaseSlow;
            if (g > peakHold)
                peakHold = g;
            auto r = peakHold - g;
            if (vintage)
                r = (float) ((0.707 - (double) g) * 1.414);
            auto rel = releaseBase + (releaseFast - releaseBase) * r;
            if (0.0f > rel)
                rel = 0.0f;
            releaseNow = rel;
            g *= makeup;
            const auto ol = hl * g, orr = hr * g;
            const auto mx = std::max (std::fabs (ol), std::fabs (orr));

            if ((double) mx > 1.0)
            {
                const auto lg = (float) (0.9885531 / (double) mx);
                if (limitGain > lg)
                {
                    limitGain = lg;
                    const auto from = gainRead;
                    if (hold < 1)
                        gainRing.fill (gainRead);
                    hold = 64;
                    limitSpeed = 0.0f;
                    for (int j = 0; j < 128; ++j)
                    {
                        auto idx = gainPos + j;
                        if (idx >= 128)
                            idx -= 128;
                        auto w = (float) ((double) (j + 1) / 64.0);
                        if ((double) w > 1.0)
                            w = 1.0f;
                        gainRead = from + (lg - from) * w * w * (3.0f - w * 2.0f);
                        if (gainRing[(std::size_t) idx] > gainRead)
                            gainRing[(std::size_t) idx] = gainRead;
                    }
                }
                else if (hold <= 0)
                {
                    limitSpeed = (float) ((double) limitSpeed + (0.09 - (double) limitSpeed) * 5e-05);
                    limitGain = limitGain + (lg - limitGain) * limitSpeed;
                }
            }
            else if (hold <= 0)
            {
                limitSpeed = (float) ((double) limitSpeed + (0.09 - (double) limitSpeed) * 5e-05);
                limitGain = (float) ((double) limitGain + (1.0 - (double) limitGain) * (double) limitSpeed);
            }
            if (hold > 0)
                --hold;

            gainRead = gainRing[(std::size_t) gainPos];
            gainRing[(std::size_t) gainPos] = limitGain;
            out[2 * i] = outRing[(std::size_t) outPos] * gainRead;
            out[2 * i + 1] = outRing[(std::size_t) outPos + 1] * gainRead;
            outRing[(std::size_t) outPos] = ol;
            outRing[(std::size_t) outPos + 1] = orr;
            if ((outPos += 2) >= 128)
                outPos = 0;
            if (++gainPos >= 128)
                gainPos = 0;
        }
    }

private:
    float coef (float ms) const noexcept // 0.1^(1/(ms·rate/1000))
    {
        if (ms == 0.0f)
            return 0.0f;
        auto t = ms * (float) sampleRate / 1000.0f;
        t = 1.0f / t;
        return (float) std::pow (0.1, (double) t);
    }

    void setup() noexcept
    {
        static constexpr int kneeDb[4] { 0, 6, 7, 15 };
        const auto [thr, ratio, gainRaw, att, rel, type] = raw;
        peakHold = 1.0f;
        fastAttack = coef (0.01f);
        fastRelease = coef (1.0f);
        attack = coef ((float) ((double) att / 10.0));
        const auto t3 = type & 3;
        vintage = t3 == 2;
        releaseFast = type >= 4 ? coef ((float) ((double) rel * 0.1)) : coef ((float) (double) rel);
        releaseBase = coef ((float) rel);
        releaseSlow = coef ((float) rel);
        threshold = (float) std::pow (10.0, (double) thr / 200.0);
        slope = (float) ((double) ratio / 10.0);
        makeup = (float) std::pow (10.0, (double) gainRaw / 200.0);
        const auto kneeLin = (float) std::pow (10.0, (double) -kneeDb[t3] / 20.0);
        slope = (float) (1.0 / (double) slope + -1.0);
        lnThreshold = (float) std::log ((double) threshold);
        kneeLow = (float) std::log ((double) (threshold * kneeLin));
        kneeHigh = (float) std::log ((double) (threshold / kneeLin));
        kneeScale = (float) (0.5 / (double) (kneeHigh - kneeLow));
        if (vintage)
        {
            kneeHigh = (float) ((double) lnThreshold + std::pow ((double) ratio / 10.0, 0.25) * 1.25);
            kneeScale = (float) (0.5 / (double) (kneeHigh - kneeLow));
            vintageCeiling = (float) std::exp ((double) ((kneeHigh - lnThreshold) * slope));
            kneeHigh = (float) std::log (std::exp ((double) kneeHigh) / (double) kneeLin);
            slope = (float) (std::log ((double) vintageCeiling) / (double) (kneeHigh - lnThreshold));
        }
        limitGain = 1.0f;
        hold = 0;
        limitSpeed = 0.01f;
        const auto t = (float) std::tan (9.42477796076938 / (double) sampleRate); // 1st-order all-pass at 1.5 Hz
        apCoef = (float) (((double) t - 1.0) / ((double) t + 1.0));
        dirty = false;
    }

    int sampleRate = 44100;
    std::array<int, compressorControls> raw = compressorDefault;
    bool dirty = true, vintage = false;
    float fastAttack = 0, fastRelease = 0, attack = 0, releaseFast = 0, releaseBase = 0, releaseSlow = 0;
    float threshold = 1, slope = 0, makeup = 1, lnThreshold = 0, kneeLow = 0, kneeHigh = 0, kneeScale = 0, vintageCeiling = 0;
    float apCoef = 0;
    std::array<float, 4> ap {};
    float follower = 0, smooth = 1, peakHold = 1, releaseNow = 0.5f;
    float limitGain = 1, limitSpeed = 0.01f, gainRead = 1;
    int hold = 0, outPos = 0, gainPos = 0;
    std::array<float, 128> gainRing {}, outRing {};
};

// ===================================================================================================================
// LYNX effects

void readLimiter (const EffectParams& params, std::array<int, limiterControls>& p, bool& exact) noexcept
{
    for (int i = 0; i < limiterControls; ++i)
        p[(std::size_t) i] = raw (params[(std::size_t) i], limiterMin[(std::size_t) i], limiterMax[(std::size_t) i]);
    exact = params[limiterControls] >= 0.5f;
}

class FruityLimiterEffect final : public Effect
{
public:
    // `initial` (may be null): the slot's parameters, so the look-ahead (latency) is known after prepare(), before the
    // first block (issue #113: a Patcher plans its delay compensation right after building its nodes)
    explicit FruityLimiterEffect (const EffectParams* initial = nullptr)
    {
        if (initial != nullptr)
            params0 = *initial;
        hasParams0 = initial != nullptr;
    }

    void prepare (double rate) noexcept override
    {
        engine.prepare ((int) std::lround (rate)); // allocates; called off the audio thread by the host
        prepared = true;
        if (hasParams0)
        {
            std::array<int, limiterControls> p {};
            bool exact = false;
            readLimiter (params0, p, exact);
            engine.setControls (p, exact);
        }
        lastLatency.store (engine.latency(), std::memory_order_relaxed);
    }

    void reset() noexcept override { engine.reset(); }

    void setSidechain (const float* left, const float* right) noexcept override
    {
        sideL = left;
        sideR = right;
        sideSet = true;
    }

    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        if (! prepared)
            return;
        std::array<int, limiterControls> p {};
        bool exact = false;
        readLimiter (params, p, exact);
        engine.setControls (p, exact);
        lastLatency.store (engine.latency(), std::memory_order_relaxed);

        for (int off = 0; off < n; off += LimiterEngine::maxBlock)
        {
            const auto m = std::min (LimiterEngine::maxBlock, n - off);
            auto* io = engine.work.data();
            for (int i = 0; i < m; ++i)
            {
                io[2 * i] = left[off + i];
                io[2 * i + 1] = right[off + i];
            }
            const float* side = nullptr;
            if (sideSet) // a sidechained slot: a silent source track is a zero signal, not the own input
            {
                auto* s = engine.side.data();
                for (int i = 0; i < m; ++i)
                {
                    s[2 * i] = sideL != nullptr ? sideL[off + i] : 0.0f;
                    s[2 * i + 1] = sideR != nullptr ? sideR[off + i] : 0.0f;
                }
                side = s;
            }
            engine.process (io, side, m);
            for (int i = 0; i < m; ++i)
            {
                left[off + i] = io[2 * i];
                right[off + i] = io[2 * i + 1];
            }
        }
        sideSet = false;
    }

    int latencySamples() const noexcept override { return lastLatency.load (std::memory_order_relaxed); }

private:
    LimiterEngine engine;
    EffectParams params0 {};
    bool hasParams0 = false, prepared = false, sideSet = false;
    const float* sideL = nullptr;
    const float* sideR = nullptr;
    std::atomic<int> lastLatency { 0 }; // (read by the UI thread)
};

class FruityCompressorEffect final : public Effect
{
public:
    // `initial` (may be null): the slot's parameters, so the reported look-ahead latency is known before the first block
    explicit FruityCompressorEffect (const EffectParams* initial = nullptr)
    {
        if (initial != nullptr)
            lookahead = (*initial)[compressorControls] >= 0.5f;
    }

    void prepare (double rate) noexcept override
    {
        engine.prepare ((int) std::lround (rate));
        io.assign (1024, 0.0f);
        side.assign (1024, 0.0f);
    }

    void reset() noexcept override { engine.reset(); }

    void setSidechain (const float* left, const float* right) noexcept override
    {
        sideL = left;
        sideR = right;
        sideSet = true;
    }

    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        if (io.empty())
            return;
        std::array<int, compressorControls> p {};
        for (int i = 0; i < compressorControls; ++i)
            p[(std::size_t) i] = raw (params[(std::size_t) i], compressorMin[(std::size_t) i], compressorMax[(std::size_t) i]);
        engine.setControls (p);
        lookahead.store (params[compressorControls] >= 0.5f, std::memory_order_relaxed);

        for (int off = 0; off < n; off += 512)
        {
            const auto m = std::min (512, n - off);
            for (int i = 0; i < m; ++i)
            {
                io[(std::size_t) (2 * i)] = left[off + i];
                io[(std::size_t) (2 * i + 1)] = right[off + i];
            }
            const float* s = nullptr;
            if (sideSet)
            {
                for (int i = 0; i < m; ++i)
                {
                    side[(std::size_t) (2 * i)] = sideL != nullptr ? sideL[off + i] : 0.0f;
                    side[(std::size_t) (2 * i + 1)] = sideR != nullptr ? sideR[off + i] : 0.0f;
                }
                s = side.data();
            }
            engine.process (io.data(), s, io.data(), m);
            for (int i = 0; i < m; ++i)
            {
                left[off + i] = io[(std::size_t) (2 * i)];
                right[off + i] = io[(std::size_t) (2 * i + 1)];
            }
        }
        sideSet = false;
    }

    // the output is always 64 frames late; FL only reports it (and so compensates it) with the look-ahead option
    int latencySamples() const noexcept override { return lookahead.load (std::memory_order_relaxed) ? 64 : 0; }

private:
    CompressorEngine engine;
    std::vector<float> io, side;
    bool sideSet = false;
    std::atomic<bool> lookahead { true }; // (read by the UI thread)
    const float* sideL = nullptr;
    const float* sideR = nullptr;
};
} // namespace

// --- states ----------------------------------------------------------------------------------------------------

std::optional<LimiterState> parseLimiter (const std::vector<std::uint8_t>& d)
{
    // FUN_006d3760: v ≥ 6 reads 0xA4 bytes after the version (v5: 0x9D); v ≤ 4 keeps the defaults; v ≥ 7 one option byte
    if (d.size() < 4)
        return std::nullopt;
    LimiterState s;
    s.version = i32 (d, 0);
    if (s.version > 4)
    {
        if (d.size() < 4 + limiterControls * 4)
            return std::nullopt;
        for (int i = 0; i < limiterControls; ++i)
            s.p[(std::size_t) i] = std::clamp (i32 (d, 4 + 4 * (std::size_t) i), limiterMin[(std::size_t) i], limiterMax[(std::size_t) i]);
        if (s.version >= 6 && d.size() >= 0xA8)
            s.sidechain = i32 (d, 0xA4);
    }
    if (s.version > 6 && d.size() > 0xA8)
        s.exactCeiling = (d[0xA8] & 1) != 0;
    return s;
}

std::optional<CompressorState> parseCompressor (const std::vector<std::uint8_t>& d)
{
    // FUN_006bd9a0: versions below 4: 6 controls, then the flags word and (v ≥ 3) the side input
    if (d.size() < 4 + compressorControls * 4)
        return std::nullopt;
    CompressorState s;
    s.version = i32 (d, 0);
    if (s.version >= 4)
        return std::nullopt;
    for (int i = 0; i < compressorControls; ++i)
        s.p[(std::size_t) i] = std::clamp (i32 (d, 4 + 4 * (std::size_t) i), compressorMin[(std::size_t) i], compressorMax[(std::size_t) i]);
    s.lookahead = d.size() >= 32 && (i32 (d, 28) & 1) != 0;
    s.sidechain = s.version > 2 && d.size() >= 36 ? i32 (d, 32) : 0;
    return s;
}

float normalised (int r, int min, int max) noexcept
{
    return max > min ? (float) (std::clamp (r, min, max) - min) / (float) (max - min) : 0.0f;
}

int raw (float v, int min, int max) noexcept
{
    return min + (int) std::lround (std::clamp ((double) v, 0.0, 1.0) * (double) (max - min));
}

void limiterToParams (const LimiterState& s, float* params) noexcept
{
    for (int i = 0; i < limiterControls; ++i)
        params[i] = normalised (s.p[(std::size_t) i], limiterMin[(std::size_t) i], limiterMax[(std::size_t) i]);
    params[limiterControls] = s.exactCeiling ? 1.0f : 0.0f;
}

void compressorToParams (const CompressorState& s, float* params) noexcept
{
    for (int i = 0; i < compressorControls; ++i)
        params[i] = normalised (s.p[(std::size_t) i], compressorMin[(std::size_t) i], compressorMax[(std::size_t) i]);
    params[compressorControls] = s.lookahead ? 1.0f : 0.0f;
}

const char* limiterParamName (int i) noexcept
{
    static const char* names[limiterParams] { "Gain", "Saturation", "Ceiling", "Attack", "Attack curve", "Release",
                                              "Release curve", "Sustain", "Comp. threshold", "Ratio", "Knee",
                                              "Comp. attack", "Comp. release", "Comp. curve", "RMS window",
                                              "Gate gain", "Gate threshold", "Gate release", "Exact ceiling" };
    return i >= 0 && i < limiterParams ? names[i] : "";
}

const char* compressorParamName (int i) noexcept
{
    static const char* names[compressorParams] { "Threshold", "Ratio", "Gain", "Attack", "Release", "Type", "Look-ahead" };
    return i >= 0 && i < compressorParams ? names[i] : "";
}

std::string limiterParamText (int i, float v)
{
    char t[48];
    if (i == limiterControls)
        return v >= 0.5f ? "on" : "off";
    if (i < 0 || i > limiterControls)
        return {};
    const auto r = raw (v, limiterMin[(std::size_t) i], limiterMax[(std::size_t) i]);
    auto db = [&] (float g) {
        if (g <= 0.0f)
            std::snprintf (t, sizeof (t), "-inf dB");
        else
            std::snprintf (t, sizeof (t), "%+.1f dB", 20.0 * std::log10 ((double) g));
    };
    auto ms = [&] (double factor) { std::snprintf (t, sizeof (t), "%.1f ms", factor * 2.0 * (std::pow (501.0, r / 10000.0) - 1.0)); };
    switch (i)
    {
        case 0: case 8: case 15: case 16: db (law7 (r)); break;
        case 1: case 2: db (law3 (r)); break;
        case 3: case 7: case 11: case 14: ms (1.0); break;
        case 5: case 12: case 17: ms (2.0); break;
        case 4: case 6: case 13: std::snprintf (t, sizeof (t), "%d", r); break;
        case 9:
            if (r > 0)
                std::snprintf (t, sizeof (t), "%.1f:1", 1.0 + 0.38 * (std::pow (51.0, r / 1000.0) - 1.0));
            else
                std::snprintf (t, sizeof (t), "1:%.2f", 1.0 + 0.06 * (std::pow (51.0, -r / 1000.0) - 1.0));
            break;
        case 10: std::snprintf (t, sizeof (t), "%.1f%%", r / 10.0); break;
        default: return {};
    }
    return t;
}

std::string compressorParamText (int i, float v)
{
    char t[48];
    if (i == compressorControls)
        return v >= 0.5f ? "on" : "off";
    if (i < 0 || i > compressorControls)
        return {};
    const auto r = raw (v, compressorMin[(std::size_t) i], compressorMax[(std::size_t) i]);
    static const char* types[8] { "Hard", "Medium", "Vintage", "Soft", "Hard/R", "Medium/R", "Vintage/R", "Soft/R" };
    switch (i)
    {
        case 0: case 2: std::snprintf (t, sizeof (t), "%+.1f dB", r / 10.0); break;
        case 1: std::snprintf (t, sizeof (t), "%.1f:1", r / 10.0); break;
        case 3: std::snprintf (t, sizeof (t), "%.1f ms", r / 10.0); break;
        case 4: std::snprintf (t, sizeof (t), "%d ms", r); break;
        case 5: return types[std::clamp (r, 0, 7)];
        default: return {};
    }
    return t;
}

std::unique_ptr<Effect> makeLimiter (const EffectParams* initial) { return std::make_unique<FruityLimiterEffect> (initial); }
std::unique_ptr<Effect> makeCompressor (const EffectParams* initial) { return std::make_unique<FruityCompressorEffect> (initial); }
} // namespace lynx::fldyn
