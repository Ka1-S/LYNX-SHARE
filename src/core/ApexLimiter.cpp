#include "core/ApexLimiter.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

#include "core/Loudness.h"
#include "core/MixSuite.h"

namespace lynx::apex
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int interpTaps = 8;   // per phase of the 4× inter-sample peak estimator
constexpr int interpDelay = interpTaps / 2;

// 4× interpolation filter (windowed sinc), phases 1..3 (phase 0 is the sample itself)
struct Interp
{
    std::array<std::array<double, interpTaps>, 3> h {};
    Interp()
    {
        for (int ph = 1; ph <= 3; ++ph)
        {
            double sum = 0.0;
            for (int k = 0; k < interpTaps; ++k)
            {
                const double t = (k - (interpTaps / 2 - 1)) - ph / 4.0; // tap position relative to the interpolated point
                const double x = pi * t;
                const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (x) / x;
                const double w = 0.5 + 0.5 * std::cos (pi * t / (interpTaps / 2 + 0.5));
                h[(std::size_t) ph - 1][(std::size_t) k] = sinc * w;
                sum += sinc * w;
            }
            for (auto& c : h[(std::size_t) ph - 1]) c /= sum;
        }
    }
};
const Interp& interp() { static const Interp i; return i; }

// per style, the two release stages (round tp172b [EXEC], fitted to the reference's gain recovery after 1.6 / 9.6 /
// 50 / 200 ms bursts 9.6 dB over the ceiling, 12 settings, rms error 0.2 … 0.8 dB): the slow stage charges toward
// the gain reduction the programme needs over attackFactor × Attack and recovers over releaseFactor × Release + 12 ms;
// the fast stage follows every drop at once and recovers in fastMs, so a short transient is limited without pumping
// what follows it (the old law recovered half of every reduction on a stage as slow as Release)
// round tp172b3 [EXEC] (the reference's stair, 0.5 s steps 3 … 15 dB over the ceiling and back down, releases 10 ms …
// 3 s): while the programme still needs limiting, the slow stage recovers only at holdRelease × its speed (Modern
// holds the deepest reduction until the level falls under the ceiling; Transparent and Dynamic recover to the
// requirement), and it reaches its full speed once the peaks are rampDb under the ceiling
struct StyleLaw { double attackFactor, releaseFactor, fastMs, holdRelease = 1.0, rampDb = 0.0; };
// round tp172b3 [EXEC]: the table refitted on the whole Pro-L 2 bank (154 settings × stair, bursts, drums, music, pilot;
// 5 ms gain trajectories, rms error before → after per style: Transparent 0.63 → 0.49, Allround 1.08 → 0.92, Punchy
// 1.85 → 1.63, Dynamic 0.72 → 0.66, Modern 1.77 → 0.96, Aggressive 1.20 → 0.93, Bus 1.71 → 1.13, Safe 2.63 → 1.18 dB)
constexpr StyleLaw laws[styleCount] = {
    { 0.214, 0.436, 0.080 },              // Clean (the reference's Transparent): recovers to the requirement
    { 0.325, 0.958, 0.146, 0.365 },       // Glue (Allround)
    { 0.178, 0.876, 0.512, 0.522 },       // Punch (Punchy)
    { 0.086, 0.191, 0.183 },              // Dynamic: the shortest stages, recovers to the requirement
    { 0.251, 0.684, 0.327, 0.008, 5.74 }, // Dense (Modern): holds the deepest reduction while it limits
    { 0.320, 0.281, 0.048, 0.128 },       // Loud (Aggressive)
    { 0.014, 0.448, 0.027, 0.885 },       // Bus: charges fastest
    { 0.0115, 0.642, 73.9, 0.165, 0.52 }, // Safe: a slow fast stage — the least distortion
};

// the slow stage's release speed (0 … 1) at the detected peak level `peak` (round tp172b3)
double holdSpeed (double peak, double ceiling, const StyleLaw& law) noexcept
{
    if (law.holdRelease >= 1.0) return 1.0;
    if (peak >= ceiling) return law.holdRelease;
    if (law.rampDb <= 0.0) return 1.0;
    const double h = 20.0 * std::log10 (ceiling / std::max (peak, 1e-9));
    return law.holdRelease + (1.0 - law.holdRelease) * std::min (1.0, h / law.rampDb);
}

// error-feedback noise shaping filters (on the quantisation error of the last samples)
constexpr double shapers[4][3] = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 2.0, -1.0, 0.0 }, { 1.623, -0.982, 0.109 } };

// running minimum over the last `len` values (monotonic queue in a ring allocated by setCapacity, never on the audio thread)
class SlidingMin
{
public:
    void setCapacity (int n) { idx.assign ((std::size_t) n + 2, 0); val.assign ((std::size_t) n + 2, 0.0); setLength (1); }
    void setLength (int n) noexcept { len = std::clamp (n, 1, (int) idx.size() - 1); head = tail = 0; index = 0; }
    double push (double v) noexcept
    {
        const int cap = (int) idx.size();
        while (tail != head && val[(std::size_t) ((tail - 1 + cap) % cap)] >= v) tail = (tail - 1 + cap) % cap;
        idx[(std::size_t) tail] = index; val[(std::size_t) tail] = v; tail = (tail + 1) % cap;
        while (idx[(std::size_t) head] <= index - len) head = (head + 1) % cap;
        ++index;
        return val[(std::size_t) head];
    }
private:
    std::vector<long long> idx;
    std::vector<double> val;
    int head = 0, tail = 0, len = 1;
    long long index = 0;
};

// one limiter at one sample rate (the base rate, or an oversampled rate)
struct Core
{
    // `extra`: a plain delay added to the output (an oversampled core pads its latency to whole base-rate samples)
    void prepare (double sr, int extra = 0)
    {
        sampleRate = sr;
        maxLook = (int) std::ceil (maxLookaheadMs * 0.001 * sr);
        extraDelay = std::max (0, extra);
        const auto ring = (std::size_t) (maxLook + extraDelay + 2);
        inL.assign (ring, 0.0f); inR.assign (ring, 0.0f);
        outL.assign (ring, 0.0f); outR.assign (ring, 0.0f);
        boxL.assign ((std::size_t) maxLook + 1, 1.0); boxR.assign ((std::size_t) maxLook + 1, 1.0);
        minAll.setCapacity (maxLook + 1); minL.setCapacity (maxLook + 1); minR.setCapacity (maxLook + 1);
        reset();
    }
    int latency() const noexcept { return maxLook + interpDelay + extraDelay; }

    void reset() noexcept
    {
        std::fill (inL.begin(), inL.end(), 0.0f); std::fill (inR.begin(), inR.end(), 0.0f);
        std::fill (outL.begin(), outL.end(), 0.0f); std::fill (outR.begin(), outR.end(), 0.0f);
        histL.fill (0.0); histR.fill (0.0); scHistL.fill (0.0); scHistR.fill (0.0);
        pos = 0;
        fastL = fastR = slowL = slowR = 0.0; // dB
        peakL = peakR = 0.0;
        dcXL = dcXR = dcYL = dcYR = 0.0;
        err = {};
        rng = 0x9e3779b97f4a7c15ull;
        look = -1; // rebuilt by the next run()
    }

    // `scl` / `scr`: the external side chain (round 135) — when given, the required gain is computed from it (after
    // the same gain) instead of the programme; the safety clip still holds the programme at the ceiling.
    // Returns the lowest gain of the block.
    double run (float* left, float* right, int n, const EffectParams& p, const float* scl, const float* scr, bool ditherHere) noexcept
    {
        const double gain = std::pow (10.0, gainDb (p[0]) / 20.0);
        const double ceiling = std::pow (10.0, ceilingDb (p[1]) / 20.0);
        const int newLook = std::clamp ((int) std::lround (lookaheadMs (p[2]) * 0.001 * sampleRate), 1, maxLook);
        if (newLook != look)
        {
            look = newLook;
            minAll.setLength (look + 1); minL.setLength (look + 1); minR.setLength (look + 1);
            std::fill (boxL.begin(), boxL.end(), 1.0); std::fill (boxR.begin(), boxR.end(), 1.0);
            boxSumL = boxSumR = (double) look; boxPos = 0;
        }
        const auto law = laws[(int) style (p[4])];
        const double rel = releaseMs (p[3]) * 0.001 * sampleRate;
        const double kFast = 1.0 - std::exp (-1.0 / std::max (1.0, law.fastMs * 0.001 * sampleRate));
        const double kSlow = 1.0 - std::exp (-1.0 / std::max (1.0, rel * law.releaseFactor + 0.012 * sampleRate));
        const double kPeak = std::exp (-1.0 / (0.01 * sampleRate));
        const bool truePeak = p[5] >= 0.5f;
        const double link = std::clamp ((double) p[6], 0.0, 1.0);
        const double attackSamples = attackMs (p[pAttack]) * 0.001 * sampleRate;
        const double kAttack = attackSamples * law.attackFactor >= 1.0 ? 1.0 - std::exp (-1.0 / (attackSamples * law.attackFactor)) : 1.0;
        const double linkRelease = std::clamp ((double) p[pLinkRelease], 0.0, 1.0);
        const bool unity = p[pUnity] >= 0.5f, audition = p[pAudition] >= 0.5f, dcFilter = p[pDcFilter] >= 0.5f;
        if (! dcFilter) dcXL = dcXR = dcYL = dcYR = 0.0;
        dcR = std::exp (-2.0 * pi * 5.0 / sampleRate);
        bits = ditherHere ? ditherBits (p[pDither]) : 0;
        shaping = noiseShaping (p[pShaping]);
        const auto& ip = interp();
        const int len = (int) inL.size();
        const int pad = maxLook - look + extraDelay;
        const bool external = scl != nullptr && scr != nullptr;
        double minGain = 1.0;

        for (int i = 0; i < n; ++i)
        {
            // history for the inter-sample estimate; the detection point is `interpDelay` samples back
            double xl = left[i], xr = right[i];
            if (dcFilter) // one-pole high pass at ~5 Hz
            {
                const double yl = xl - dcXL + dcR * dcYL, yr = xr - dcXR + dcR * dcYR;
                dcXL = xl; dcXR = xr; dcYL = yl; dcYR = yr;
                xl = yl; xr = yr;
            }
            std::rotate (histL.begin(), histL.begin() + 1, histL.end()); histL.back() = xl * gain;
            std::rotate (histR.begin(), histR.begin() + 1, histR.end()); histR.back() = xr * gain;
            const double cl = histL[(std::size_t) interpTaps / 2 - 1], cr = histR[(std::size_t) interpTaps / 2 - 1];
            const auto& dl = external ? scHistL : histL;
            const auto& dr = external ? scHistR : histR;
            if (external)
            {
                std::rotate (scHistL.begin(), scHistL.begin() + 1, scHistL.end()); scHistL.back() = scl[i] * gain;
                std::rotate (scHistR.begin(), scHistR.begin() + 1, scHistR.end()); scHistR.back() = scr[i] * gain;
            }
            double pl = std::abs (dl[(std::size_t) interpTaps / 2 - 1]), pr = std::abs (dr[(std::size_t) interpTaps / 2 - 1]);
            if (truePeak)
                for (int ph = 0; ph < 3; ++ph)
                {
                    double sl = 0.0, sr = 0.0;
                    for (int k = 0; k < interpTaps; ++k)
                    {
                        sl += ip.h[(std::size_t) ph][(std::size_t) k] * dl[(std::size_t) k];
                        sr += ip.h[(std::size_t) ph][(std::size_t) k] * dr[(std::size_t) k];
                    }
                    pl = std::max (pl, std::abs (sl)); pr = std::max (pr, std::abs (sr));
                }

            // required gain, held over look + 1 samples, then a box average of `look` samples: the result is at or
            // below the requirement of the sample `look` samples back, the one leaving the delay now
            const double reqAll = std::min (1.0, ceiling / std::max (std::max (pl, pr), 1e-12));
            const double reqL = std::min (1.0, ceiling / std::max (pl, 1e-12));
            const double reqR = std::min (1.0, ceiling / std::max (pr, 1e-12));
            const double hAll = minAll.push (reqAll), hL = minL.push (reqL), hR = minR.push (reqR);
            const double tL = link * hAll + (1.0 - link) * hL, tR = link * hAll + (1.0 - link) * hR;
            boxSumL += tL - boxL[(std::size_t) boxPos]; boxL[(std::size_t) boxPos] = tL;
            boxSumR += tR - boxR[(std::size_t) boxPos]; boxR[(std::size_t) boxPos] = tR;
            boxPos = (boxPos + 1) % look;
            const double aL = std::min (1.0, boxSumL / look), aR = std::min (1.0, boxSumR / look);

            // release: the gain follows a drop at once and recovers on a fast and a slow stage (mixed by style)
            // release: both stages in dB; the gain is the deeper of the requirement and the two stages
            const double dL = 20.0 * std::log10 (std::max (aL, 1e-9)), dR = 20.0 * std::log10 (std::max (aR, 1e-9));
            fastL = dL < fastL ? dL : fastL + (dL - fastL) * kFast;
            fastR = dR < fastR ? dR : fastR + (dR - fastR) * kFast;
            peakL = std::max (pl, peakL * kPeak); peakR = std::max (pr, peakR * kPeak);
            const double tLink = link * std::max (peakL, peakR);
            slowL += (dL - slowL) * (dL < slowL ? kAttack : kSlow * holdSpeed (tLink + (1.0 - link) * peakL, ceiling, law));
            slowR += (dR - slowR) * (dR < slowR ? kAttack : kSlow * holdSpeed (tLink + (1.0 - link) * peakR, ceiling, law));
            double gL = std::min (aL, std::pow (10.0, std::min (fastL, slowL) / 20.0));
            double gR = std::min (aR, std::pow (10.0, std::min (fastR, slowR) / 20.0));
            if (linkRelease > link) // the release stage linked further than the transients (round 128)
            {
                const double shared = std::min (gL, gR), f = (linkRelease - link) / (1.0 - link);
                gL += f * (shared - gL);
                gR += f * (shared - gR);
            }

            minGain = std::min (minGain, std::min (gL, gR));
            inL[(std::size_t) pos] = (float) cl; inR[(std::size_t) pos] = (float) cr;
            const int back = (pos - look + len) % len;
            double yl = std::clamp (inL[(std::size_t) back] * gL, -ceiling, ceiling); // safety clip
            double yr = std::clamp (inR[(std::size_t) back] * gR, -ceiling, ceiling);
            if (audition) { yl = inL[(std::size_t) back] - yl; yr = inR[(std::size_t) back] - yr; } // what is removed
            if (unity) { yl /= gain; yr /= gain; }
            if (bits > 0) { yl = dither (yl, 0); yr = dither (yr, 1); }
            // the rest of the maximum look-ahead as plain delay, so the latency never changes
            outL[(std::size_t) pos] = (float) yl; outR[(std::size_t) pos] = (float) yr;
            const int rd = (pos - pad + len) % len;
            left[i] = outL[(std::size_t) rd]; right[i] = outR[(std::size_t) rd];
            pos = (pos + 1) % len;
        }
        return minGain;
    }

    // TPDF dither at the chosen word length with error-feedback noise shaping
    double dither (double x, int ch) noexcept
    {
        const double scale = std::ldexp (1.0, bits - 1);
        const auto& h = shapers[shaping];
        auto& e = err[(std::size_t) ch];
        const double v = x - (h[0] * e[0] + h[1] * e[1] + h[2] * e[2]);
        const double tpdf = (rand01() - rand01()); // ±1 LSB, triangular
        const double q = std::round (v * scale + tpdf) / scale;
        e[2] = e[1]; e[1] = e[0]; e[0] = q - v;
        return q;
    }
    double rand01() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        return (double) (rng >> 11) * (1.0 / 9007199254740992.0);
    }

    double sampleRate = 48000.0;
    int maxLook = 240, look = -1, extraDelay = 0;
    int bits = 0, shaping = 0;
    std::array<std::array<double, 3>, 2> err {};
    unsigned long long rng = 0x9e3779b97f4a7c15ull;
    double dcXL = 0.0, dcXR = 0.0, dcYL = 0.0, dcYR = 0.0, dcR = 0.9993;
    std::vector<float> inL, inR, outL, outR;
    std::array<double, interpTaps> histL {}, histR {}, scHistL {}, scHistR {};
    std::vector<double> boxL, boxR;
    double boxSumL = 1.0, boxSumR = 1.0;
    int boxPos = 0, pos = 0;
    SlidingMin minAll, minL, minR;
    double fastL = 0.0, fastR = 0.0, slowL = 0.0, slowR = 0.0; // release stages, dB
    double peakL = 0.0, peakR = 0.0; // round tp172b3: the detected peaks over ~10 ms (the headroom under the ceiling)
};

class ApexLimiterEffect final : public Effect
{
public:
    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        base.prepare (sr);
        for (int k = 0; k < 3; ++k)
        {
            // the high-rate core's latency padded to the base core's latency × factor (whole base-rate samples)
            const int f = 2 << k;
                const int overLatency = (int) std::ceil (maxLookaheadMs * 0.001 * sr * f) + interpDelay;
            over[(std::size_t) k].prepare (sr * f, f * base.latency() - overLatency);
        }
        loud.prepare (sr);
        latency.store (latencyFor (sr, knownFactor), std::memory_order_relaxed);
        reset();
    }

    void reset() noexcept override
    {
        base.reset();
        for (auto& c : over) c.reset();
        for (auto& o : os) o.reset();
        for (auto& o : osSide) o.reset();
        for (auto& o : osSc) o.reset();
        loud.reset();
    }

    int latencySamples() const noexcept override { return latency.load (std::memory_order_relaxed); }

    void setSidechain (const float* l, const float* r) noexcept override { scL = l; scR = r; }
    void presetLatency (const EffectParams& p) noexcept
    {
        knownFactor = oversamplingFactor (p[pOversampling]);
        latency.store (latencyFor (sampleRate, knownFactor), std::memory_order_relaxed);
    }

    // live meters for the editor (round 114): 0 = gain reduction over 0 … 30 dB (0..1), 2 = input peak, 3 = output
    // peak (true peak when the peak meter is set to it, round 135); round 135's loudness meter (LUFS / LU / dB, as
    // they are): 1 = momentary, 4 = short term, 5 = integrated, 6 = loudness range, 7 = the highest output peak so far
    float controlOutput (int k) const noexcept override
    {
        return k >= 0 && k < (int) meters.size() ? meters[(std::size_t) k].load (std::memory_order_relaxed) : 0.0f;
    }

    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        if (base.inL.empty()) prepare (sampleRate);
        const auto factor = oversamplingFactor (p[pOversampling]);
        if (factor != osFactor)
        {
            osFactor = factor;
            for (auto& c : over) c.reset();
            for (auto& o : os) { o.setFactor (std::max (2, factor)); o.reset(); }
            for (auto& o : osSide) { o.setFactor (std::max (2, factor)); o.reset(); }
            for (auto& o : osSc) { o.setFactor (std::max (2, factor)); o.reset(); }
        }
        knownFactor = factor;
        latency.store (latencyFor (sampleRate, factor), std::memory_order_relaxed);
        // the loudness meter's reset button flips its value (a "momentary" switch)
        if (p[pLoudnessReset] != lastReset) { lastReset = p[pLoudnessReset]; loud.reset(); }
        const bool external = p[pSidechain] >= 0.5f && scL != nullptr && scR != nullptr;

        float inPeak = 0.0f;
        for (int i = 0; i < n; ++i) inPeak = std::max (inPeak, std::max (std::abs (left[i]), std::abs (right[i])));
        double minGain = 1.0;
        if (factor == 1)
            minGain = base.run (left, right, n, p, external ? scL : nullptr, external ? scR : nullptr, true);
        else
        {
            auto& core = over[(std::size_t) (factor == 2 ? 0 : factor == 4 ? 1 : 2)];
            for (int start = 0; start < n; start += chunk)
            {
                const int m = std::min (chunk, n - start), up = m * factor;
                // upsample (the audio, and the side chain on the same path so both stay aligned), limit at the high
                // rate, then the down path returns the limited high-rate samples
                for (int c = 0; c < 2; ++c)
                {
                    std::copy (c == 0 ? left + start : right + start, (c == 0 ? left : right) + start + m, copy.begin());
                    auto* dst = c == 0 ? upL.data() : upR.data();
                    int k = 0;
                    osSide[(std::size_t) c].process (copy.data(), m, [&] (float v) noexcept { dst[std::min (k++, up - 1)] = v; return 0.0f; });
                    if (external)
                    {
                        std::copy (c == 0 ? scL + start : scR + start, (c == 0 ? scL : scR) + start + m, copy.begin());
                        auto* sdst = c == 0 ? upScL.data() : upScR.data();
                        int j = 0;
                        osSc[(std::size_t) c].process (copy.data(), m, [&] (float v) noexcept { sdst[std::min (j++, up - 1)] = v; return 0.0f; });
                    }
                }
                minGain = std::min (minGain, core.run (upL.data(), upR.data(), up, p, external ? upScL.data() : nullptr, external ? upScR.data() : nullptr, false));
                for (int c = 0; c < 2; ++c)
                {
                    const auto* src = c == 0 ? upL.data() : upR.data();
                    int k = 0;
                    os[(std::size_t) c].process (c == 0 ? left + start : right + start, m, [&] (float) noexcept { return src[std::min (k++, up - 1)]; });
                }
            }
            // the down path's ripple may pass the ceiling by a hair: the final clip (and the dither at the base rate)
            const double ceiling = std::pow (10.0, ceilingDb (p[1]) / 20.0);
            const int bits = ditherBits (p[pDither]);
            base.bits = bits;
            base.shaping = noiseShaping (p[pShaping]);
            const bool audition = p[pAudition] >= 0.5f;
            for (int i = 0; i < n; ++i)
            {
                double yl = left[i], yr = right[i];
                if (! audition && p[pUnity] < 0.5f) { yl = std::clamp (yl, -ceiling, ceiling); yr = std::clamp (yr, -ceiling, ceiling); }
                if (bits > 0) { yl = base.dither (yl, 0); yr = base.dither (yr, 1); }
                left[i] = (float) yl; right[i] = (float) yr;
            }
        }
        scL = scR = nullptr; // a side chain is given again before every block that has one

        loud.process (left, right, n);
        const bool tpMeter = p[pPeakMeter] >= 0.5f;
        const auto blockTp = loud.takeBlockTruePeak(), blockSp = loud.takeBlockSamplePeak();
        const auto outPeak = (float) (tpMeter ? blockTp : blockSp);
        store (0, (float) std::clamp (-20.0 * std::log10 (std::max (minGain, 1e-9)) / 30.0, 0.0, 1.0));
        store (2, std::min (inPeak, 1.0f));
        store (3, std::min (outPeak, 2.0f));
        store (1, (float) loud.momentary());
        store (4, (float) loud.shortTermLoudness());
        store (5, (float) loud.integratedLoudness());
        store (6, (float) loud.loudnessRange());
        store (7, (float) (tpMeter ? loud.truePeakDb() : loud.samplePeakDb()));
    }

private:
    void store (int k, float v) noexcept { meters[(std::size_t) k].store (v, std::memory_order_relaxed); }

    static constexpr int chunk = 1024, maxFactor = 8;
    double sampleRate = 48000.0;
    Core base;
    std::array<Core, 3> over; // 2x, 4x, 8x
    std::array<mixsuite::Oversampler, 2> os, osSide, osSc;
    int osFactor = 1, knownFactor = 1;
    std::array<float, chunk> copy {};
    std::array<float, chunk * maxFactor> upL {}, upR {}, upScL {}, upScR {};
    const float* scL = nullptr;
    const float* scR = nullptr;
    loudness::Meter loud;
    float lastReset = 0.0f;
    std::atomic<int> latency { latencyAt (48000.0) };
    std::array<std::atomic<float>, 8> meters {};
};
} // namespace

int oversamplingFactor (float v) noexcept { return 1 << std::clamp ((int) std::lround (v * 3.0f), 0, 3); }
int latencyFor (double sr, int factor) noexcept { return latencyAt (sr) + (factor > 1 ? mixsuite::Oversampler::latency() : 0); }
int displayMode (float v) noexcept { return std::clamp ((int) std::lround (v * 4.0f), 0, 4); }
double loudnessTarget (float v) noexcept
{
    static constexpr double t[] { -23.0, -18.0, -16.0, -14.0, -11.0, -9.0 };
    return t[std::clamp ((int) std::lround (v * 5.0f), 0, 5)];
}

double gainDb (float v) noexcept { return (double) std::clamp (v, 0.0f, 1.0f) * 30.0; }
double ceilingDb (float v) noexcept { return ((double) std::clamp (v, 0.0f, 1.0f) - 1.0) * 30.0; }
double lookaheadMs (float v) noexcept { return (double) std::clamp (v, 0.0f, 1.0f) * maxLookaheadMs; }
double releaseMs (float v) noexcept { return std::pow (1000.0, (double) std::clamp (v, 0.0f, 1.0f)); }
float releaseValue (double ms) noexcept { return (float) std::clamp (std::log (ms) / std::log (1000.0), 0.0, 1.0); }
Style style (float v) noexcept { return (Style) std::clamp ((int) std::lround (v * (styleCount - 1)), 0, styleCount - 1); }
const char* styleName (Style s) noexcept
{
    static const char* names[] = { "Clean", "Glue", "Punch", "Dynamic", "Dense", "Loud", "Bus", "Safe" };
    return names[(int) s];
}
double attackMs (float v) noexcept { const double x = std::clamp ((double) v, 0.0, 1.0); return 10000.0 * x * x * x * x; }
int ditherBits (float v) noexcept
{
    static constexpr int b[] { 0, 16, 18, 20, 22, 24 };
    return b[std::clamp ((int) std::lround (v * 5.0f), 0, 5)];
}
int noiseShaping (float v) noexcept { return std::clamp ((int) std::lround (v * 3.0f), 0, 3); }
int meterScale (float v) noexcept { return std::clamp ((int) std::lround (v * 5.0f), 0, 5); }

const EffectInfo& info()
{
    static const EffectInfo i { "Apex Limiter",
                                { { "Gain", 0.0f }, { "Ceiling", 1.0f - 0.1f / 30.0f }, { "Lookahead", 0.2f }, { "Release", releaseValue (150.0) },
                                  { "Style", 0.0f }, { "True Peak", 1.0f }, { "Link", 1.0f },
                                  // round 128
                                  { "Attack", 0.0f }, { "Link release", 0.0f }, { "Dither", 0.0f }, { "Noise shaping", 2.0f / 3.0f },
                                  { "Unity gain", 0.0f }, { "Audition", 0.0f }, { "DC filter", 0.0f }, { "Meter scale", 0.4f },
                                  // round 135
                                  { "Oversampling", 0.0f }, { "Peak meter", 1.0f }, { "Display", 0.0f }, { "Loudness reset", 0.0f },
                                  { "Side chain", 0.0f }, { "Loudness meter", 0.0f }, { "Loudness target", 0.6f } } };
    return i;
}

std::string paramText (int p, float v)
{
    char t[40];
    switch (p)
    {
        case 0: std::snprintf (t, sizeof (t), "+%.1f dB", gainDb (v)); break;
        case 1: std::snprintf (t, sizeof (t), "%.1f dB", ceilingDb (v)); break;
        case 2: std::snprintf (t, sizeof (t), "%.2f ms", lookaheadMs (v)); break;
        case 3: // (1 s at the top, never "1000 ms": round tp172b5, the scale's end under the knob reads "1 s")
            if (releaseMs (v) >= 999.5) std::snprintf (t, sizeof (t), "%.2f s", releaseMs (v) / 1000.0);
            else std::snprintf (t, sizeof (t), releaseMs (v) < 100 ? "%.1f ms" : "%.0f ms", releaseMs (v));
            break;
        case 4: return styleName (style (v));
        case 5: case pUnity: case pAudition: case pDcFilter: case pSidechain: case pLoudnessShown: return v >= 0.5f ? "On" : "Off";
        case pOversampling: return oversamplingFactor (v) == 1 ? "Off" : std::to_string (oversamplingFactor (v)) + "x";
        case pPeakMeter: return v >= 0.5f ? "True peak" : "Sample peak";
        case pDisplayMode: { static const char* n[] = { "Fast", "Slow", "Slow down", "Infinite", "Off" }; return n[displayMode (v)]; }
        case pLoudnessReset: return "Reset";
        case pLoudnessTarget: std::snprintf (t, sizeof (t), "%.0f LUFS", loudnessTarget (v)); break;
        case pAttack:
        {
            const auto ms = attackMs (v);
            if (ms < 0.05) return "0 ms";
            std::snprintf (t, sizeof (t), ms < 10.0 ? "%.2f ms" : ms < 1000.0 ? "%.0f ms" : "%.2f s", ms < 1000.0 ? ms : ms / 1000.0);
            break;
        }
        case pDither:
            if (ditherBits (v) == 0) return "Off";
            std::snprintf (t, sizeof (t), "%d bits", ditherBits (v));
            break;
        case pShaping: { static const char* n[] = { "None", "Basic", "Optimized", "Weighted" }; return n[noiseShaping (v)]; }
        case pMeterScale: { static const char* n[] = { "-16 dB", "-32 dB", "-48 dB", "K-12", "K-14", "K-20" }; return n[meterScale (v)]; }
        default: std::snprintf (t, sizeof (t), "%.0f%%", v * 100.0f); break;
    }
    return t;
}

int latencyAt (double sr) noexcept { return (int) std::ceil (maxLookaheadMs * 0.001 * sr) + interpDelay; }

std::unique_ptr<Effect> make (const EffectParams* initial)
{
    auto fx = std::make_unique<ApexLimiterEffect>();
    if (initial != nullptr) fx->presetLatency (*initial);
    return fx;
}
} // namespace lynx::apex
