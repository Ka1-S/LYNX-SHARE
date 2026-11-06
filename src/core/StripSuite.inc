// Round tp172c8: Fret Channel (a guitar / bass channel strip) and Shell Channel (a drum channel strip), LYNX's own
// plugins for those two roles. Included by core/MixSuite.cpp at namespace mixsuite level (it uses Ridge's engine, the
// DRIVE section's measured amp models and the suite's helpers). The originals (SSL Guitarstrip, SSL Native Drumstrip)
// were measured black-box on their trials (REA-free: their outputs only, GS_bank / GS_x / GS_y, DS_bank / DS_y in the
// round's report); every law below is a fit to those measurements.

namespace
{
inline float linV (double x, double lo, double hi) noexcept { return (float) std::clamp ((x - lo) / (hi - lo), 0.0, 1.0); }
inline float logV (double x, double lo, double hi) noexcept
{
    return (float) std::clamp (std::log (std::max (x, 1e-9) / lo) / std::log (hi / lo), 0.0, 1.0);
}
// first-order sections by the bilinear transform (a shelf as the analog (s + wz) / (s + wp))
void firstOrderShelf (Biquad& b, double sr, double hz, double db, bool high) noexcept
{
    // low: (s + wc sqrt g) / (s + wc / sqrt g) — g at DC, 1 above; high: g (s + wc / sqrt g) / (s + wc sqrt g) — 1 at DC, g
    // above; the half gain (in dB) at hz; s by the bilinear transform with the corner prewarped
    const auto g = std::pow (10.0, db / 20.0), sg = std::sqrt (g);
    const auto wc = 2.0 * sr * std::tan (3.141592653589793 * std::min (hz, sr * 0.45) / sr);
    const auto wz = high ? wc / sg : wc * sg, wp = high ? wc * sg : wc / sg, k = high ? g : 1.0;
    const auto c = 2.0 * sr, a0 = c + wp;
    b.set (k * (c + wz) / a0, k * (wz - c) / a0, 0.0, 1.0, (wp - c) / a0, 0.0);
}
void firstOrderLowPass (Biquad& b, double sr, double hz) noexcept
{
    const auto k = std::tan (3.141592653589793 * std::min (hz, sr * 0.45) / sr);
    b.set (k / (1.0 + k), k / (1.0 + k), 0.0, 1.0, (k - 1.0) / (k + 1.0), 0.0);
}
void firstOrderHighPass (Biquad& b, double sr, double hz) noexcept
{
    const auto k = std::tan (3.141592653589793 * std::min (hz, sr * 0.45) / sr);
    b.set (1.0 / (1.0 + k), -1.0 / (1.0 + k), 0.0, 1.0, (k - 1.0) / (k + 1.0), 0.0);
}
} // namespace

// ================================================================== Fret Channel: laws
namespace fret
{
double gainDb (float v) noexcept { return lin (v, -24.0, 24.0); }
double delayMs (float v) noexcept { return lin (v, -20.0, 20.0); }
double allPassHz (float v) noexcept { return logMap (v, 20.0, 20000.0); }
double allPassQOf (float v) noexcept { return logMap (v, 0.1, 6.0); }
double hpfHz (float v) noexcept { return v <= 0.0f ? 0.0 : logMap (v, 20.0, 300.0); }
double lpfKHz (float v) noexcept { return v >= 0.9999f ? 0.0 : logMap (v, 3.0, 20.0); }
double shelfDb (float v) noexcept { return lin (v, -12.0, 12.0); }
double midKHz (float v) noexcept
{
    const auto x = (double) std::clamp (v, 0.0f, 1.0f);
    return x < 0.5 ? 0.5 * std::pow (2.0, x / 0.5) : std::pow (7.0, (x - 0.5) / 0.5);
}
double thresholdDb (float v) noexcept { return lin (v, -36.0, 0.0); }
double preAmpDb (float v) noexcept { return lin (v, -6.0, 30.0); }
double ampDrive (float v) noexcept { return lin (v, 0.0, 11.0); }
double voicePct (float v) noexcept { return lin (v, -100.0, 100.0); }
double voiceSteps (float v) noexcept { return lin (v, 0.0, 10.0); }
int alignOf (float v) noexcept { return std::clamp ((int) std::floor (std::clamp (v, 0.0f, 1.0f) * 2.0f + 0.5f), 0, 2); }
int modeOf (float v) noexcept { return std::clamp ((int) std::floor (std::clamp (v, 0.0f, 1.0f) * 4.0f + 0.5f), 0, 4); }
float modeValue (int mode) noexcept { return (float) std::clamp (mode, 0, 4) / 4.0f; }
const char* modeName (int mode) noexcept
{
    static const char* names[5] { "Fastest", "Fast", "Medium", "Slow", "Slowest" };
    return names[std::clamp (mode, 0, 4)];
}
const char* alignName (int align) noexcept
{
    static const char* names[3] { "Side chain", "Left", "Right" };
    return names[std::clamp (align, 0, 2)];
}
float midValue (double kHz) noexcept
{
    return kHz < 1.0 ? 0.5f * logV (kHz, 0.5, 1.0) : 0.5f + 0.5f * logV (kHz, 1.0, 7.0);
}
float hpfValue (double hz) noexcept { return hz <= 20.5 ? 0.0f : std::max (1e-4f, logV (hz, 20.0, 300.0)); }
float lpfValue (double kHz) noexcept { return kHz >= 19.95 ? 1.0f : std::min (0.9998f, logV (kHz, 3.0, 20.0)); }
int baseDelaySamples (double sampleRate) noexcept { return (int) std::lround (0.020 * sampleRate); }

// the compressor's ratio per mode (Fastest .. Slowest = 2 : 1 .. 4 : 1, measured), its auto gain: the threshold's distance
// under -18 dB times (1 - 1 / ratio) (cauto: +6 dB at -27 dB, Medium; 0 at -18 dB and above)
static double modeRatio (int mode) noexcept { return 2.0 + 0.5 * mode; }
double autoGainDb (const EffectParams& p) noexcept
{
    if (! on (p[compIn]) || ! on (p[compAutoGain])) return 0.0;
    return std::max (0.0, -18.0 - thresholdDb (p[compThreshold])) * (1.0 - 1.0 / modeRatio (modeOf (p[compMode])));
}

EffectParams ridgeParams (const EffectParams& p)
{
    namespace r = ridge;
    EffectParams q {};
    const auto& info = ridgeInfo();
    for (std::size_t i = 0; i < info.params.size() && i < q.size(); ++i)
        q[i] = info.params[i].defaultValue;
    q[r::modern] = q[r::console4k] = q[r::voicing] = q[r::vocal] = 0.0f;
    q[r::curve] = q[r::analog] = q[r::autoMakeup] = 0.0f;
    q[r::eqToSidechain] = q[r::filtersToSidechain] = 0.0f;
    q[r::gateRange] = 0.0f;
    q[r::driveOn] = 0.0f;
    q[r::input] = q[r::output] = 0.5f;
    q[r::phase] = 0.0f;
    // EQ: the first-order shelves as Ridge's S 1 shelves at their best-fitting corner and gain (low cut 327 Hz x0.93,
    // boost 450 Hz x1.0; high 4262 Hz x0.87), the Q 0.5 mid bell on the low-mid band below 600 Hz, else the high-mid
    const bool eqIn = on (p[fret::eqIn]);
    q[r::eqOn] = eqIn ? 1.0f : 0.0f;
    const auto loDb = shelfDb (p[lowGain]), hiDb = shelfDb (p[highGain]);
    q[r::lfBell] = q[r::hfBell] = 0.0f;
    q[r::lfGain] = linV (loDb * (loDb < 0.0 ? 0.928 : 1.0), -15.0, 15.0);
    q[r::lfFreq] = logV (loDb < 0.0 ? 327.5 : 450.0, 30.0, 450.0);
    q[r::hfGain] = linV (hiDb * 0.867, -15.0, 15.0);
    q[r::hfFreq] = logV (4262.0, 1500.0, 16000.0);
    const auto midHz = midKHz (p[midFreq]) * 1000.0, midDb = shelfDb (p[midGain]);
    const bool lowMid = midHz < 600.0;
    q[r::lmfGain] = linV (lowMid ? midDb : 0.0, -15.0, 15.0);
    q[r::lmfFreq] = logV (lowMid ? midHz : 1000.0, 200.0, 2500.0);
    q[r::lmfQ] = 0.0f;
    q[r::hmfGain] = linV (lowMid ? 0.0 : midDb, -15.0, 15.0);
    q[r::hmfFreq] = logV (lowMid ? 3000.0 : midHz, 600.0, 7000.0);
    q[r::hmfQ] = 0.0f;
    // filters: the 24 dB/oct high-pass on Ridge's 18 dB/oct at ~1.25x its corner, the steep low-pass on Ridge's
    // 12 dB/oct at 0.52x (3.6 kHz) .. 0.74x (11.3 kHz)
    const auto hp = hpfHz (p[fret::hpf]), lpk = lpfKHz (p[fret::lpf]);
    const auto hpK = hp < 30.0 ? 1.03 : 1.25;
    q[r::hpf] = eqIn && hp > 20.5 ? std::max (1e-4f, logV (hp * hpK, 16.0, 350.0)) : 0.0f;
    const auto lp = lpk > 0.0 ? lpk : 20.0;
    const auto lpK = std::clamp (0.523 + (0.74 - 0.523) * std::log (std::max (lp, 3.0) / 3.6) / std::log (11.3 / 3.6), 0.5, 0.8);
    q[r::lpf] = eqIn && lpk > 0.0 && lp < 19.5 ? std::max (1e-4f, logV (std::max (3000.0, lp * 1000.0 * lpK), 22000.0, 3000.0)) : 0.0f;
    // the compressor: threshold at the sine's RMS (Ridge reads the peak: +3 dB), the mode's ratio and times
    const bool compOn = on (p[compIn]);
    const auto mode = modeOf (p[compMode]);
    q[r::dynOn] = compOn ? 1.0f : 0.0f;
    q[r::compRatio] = compOn ? (float) std::sqrt (std::clamp ((modeRatio (mode) - 1.0) / 19.0, 0.0, 0.996)) : 0.0f;
    q[r::compThreshold] = linV (thresholdDb (p[compThreshold]) + 3.0, -40.0, 0.0);
    q[r::compFastAttack] = mode < 1 ? 1.0f : 0.0f;
    q[r::compRelease] = logV (std::exp (std::log (0.1) + (std::log (0.3) - std::log (0.1)) * mode / 4.0), 0.1, 4.0);
    q[r::compMix] = std::clamp (p[compMix], 0.0f, 1.0f);
    q[r::order] = on (p[eqPrePost]) ? 1.0f : 0.0f; // Pre = the EQ before the compressor
    return q;
}
} // namespace fret

// ================================================================== Shell Channel: laws
namespace shell
{
double gainDb (float v) noexcept { return lin (v, -20.0, 20.0); }
double attackS (float v) noexcept { return lin (v, 0.0, 0.1); }
double holdMs (float v) noexcept { return lin (v, 0.0, 4000.0); }
double releaseS (float v) noexcept { return logMap (v, 0.01, 1.0); }
double thresholdDb (float v) noexcept { return lin (v, -30.0, 0.0); }
double rangeDb (float v) noexcept { return lin (v, 0.0, 80.0); }
double hfHz (float v) noexcept { return logMap (v, 2000.0, 20000.0); }
double lfHz (float v) noexcept { return logMap (v, 20.0, 250.0); }
double driveSteps (float v) noexcept { return lin (v, 0.0, 10.0); }
int orderIndex (float v) noexcept { return std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 119.0f), 0, 119); }
float orderValue (int index) noexcept { return (float) std::clamp (index, 0, 119) / 119.0f; }
// the 120 orders in lexicographic order of (gate, shaper, low enhancer, high enhancer, listen mic): 0 = that order, 119 = reversed
std::array<int, modules> orderOf (int index) noexcept
{
    std::array<int, modules> left { gate, shaper, lowEnhancer, highEnhancer, listenMic }, out {};
    int n = modules, rest = std::clamp (index, 0, 119);
    int fact = 24;
    for (int i = 0; i < modules; ++i)
    {
        const auto k = fact > 0 ? rest / fact : 0;
        out[(std::size_t) i] = left[(std::size_t) k];
        for (int j = k; j < n - 1; ++j)
            left[(std::size_t) j] = left[(std::size_t) j + 1];
        --n;
        rest -= k * fact;
        fact = n > 1 ? fact / n : 0;
    }
    return out;
}
int indexOf (const std::array<int, modules>& order) noexcept
{
    std::array<int, modules> left { gate, shaper, lowEnhancer, highEnhancer, listenMic };
    int n = modules, index = 0, fact = 24;
    for (int i = 0; i < modules; ++i)
    {
        int k = 0;
        while (k < n && left[(std::size_t) k] != order[(std::size_t) i]) ++k;
        if (k == n) return 0;
        index += k * fact;
        for (int j = k; j < n - 1; ++j)
            left[(std::size_t) j] = left[(std::size_t) j + 1];
        --n;
        fact = n > 1 ? fact / n : 0;
    }
    return index;
}
const char* moduleName (int module) noexcept
{
    static const char* names[modules] { "Gate", "Transient shaper", "Low freq enhancer", "High freq enhancer", "Listen mic compressor" };
    return names[std::clamp (module, 0, (int) modules - 1)];
}
std::string orderText (int index)
{
    static const char* shortNames[modules] { "Gate", "TS", "LF", "HF", "LMC" };
    std::string s;
    for (const auto m : orderOf (index))
        s += (s.empty() ? "" : " > ") + std::string (shortNames[(std::size_t) m]);
    return s;
}
float thresholdValue (double db) noexcept { return linV (db, -30.0, 0.0); }
float releaseValue (double s) noexcept { return logV (s, 0.01, 1.0); }
float hfValue (double hz) noexcept { return logV (hz, 2000.0, 20000.0); }
float lfValue (double hz) noexcept { return logV (hz, 20.0, 250.0); }
int latencySamples (double sampleRate) noexcept { return (int) std::lround (199.0 * sampleRate / 48000.0); }
} // namespace shell

namespace
{
// ================================================================== Fret Channel: the effect
class FretChannel final : public Effect
{
public:
    void prepare (double rate) noexcept override
    {
        sr = rate;
        eqComp.prepare (rate);
        amp.prepare (rate);
        reset();
    }
    void reset() noexcept override
    {
        eqComp.reset();
        amp.reset();
        for (auto& line : phaseLine) line.fill (0.0f);
        for (auto& line : dryLine) line.fill (0.0f);
        pos = 0;
        for (auto* b : { &ap1, &ap2, &boost, &punch, &edgeBroad, &edgeHigh, &bassA, &bassB, &shapeBell, &shapeHigh })
            b->reset();
        sxy = sxx = syy = 0.0;
        designed = false;
    }
    int latencySamples() const noexcept override { return fret::baseDelaySamples (sr) + ridge::DriveStage::latency(); }
    float controlOutput (int k) const noexcept override
    {
        if (k >= 4 && k < 8) return sides[(std::size_t) k - 4].load (std::memory_order_relaxed);
        if (k == 1) return correlation.load (std::memory_order_relaxed);
        if (k == 0) return eqComp.controlOutput (0);
        return meters.read (k);
    }
    void setSidechain (const float* l, const float* r) noexcept override { scL = l; scR = r; }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        namespace f = fret;
        const float* sideL = scL;
        const float* sideR = scR;
        scL = scR = nullptr; // the host sets it again before every block that has one
        const auto inGain = (float) dbToGain (f::gainDb (p[f::input]));
        for (int i = 0; i < n; ++i)
        {
            l[i] *= inGain;
            r[i] *= inGain;
        }
        const auto inPeak = Meters::peak (l, r, n);
        sides[0].store (Meters::peak (l, l, n), std::memory_order_relaxed);
        sides[1].store (Meters::peak (r, r, n), std::memory_order_relaxed);
        design (p);

        // the phase correction: every channel goes through the fixed 20 ms (the delay's centre); the moving channel(s)
        // take the delay, the polarity and the all-pass; Align to Left / Right keeps that channel as the reference
        const auto base = f::baseDelaySamples (sr);
        const bool pc = on (p[f::phaseCorrection]);
        const auto align = f::alignOf (p[f::alignMode]);
        const auto shift = pc && on (p[f::delayIn]) ? (int) std::lround (f::delayMs (p[f::delay]) * 0.001 * sr) : 0;
        const bool moveL = align != 1, moveR = align != 2;
        const bool invert = pc && on (p[f::phaseInvert]);
        const bool allPass = pc && on (p[f::allPassIn]);
        const bool second = on (p[f::allPassOrder]);
        const auto totalLatency = base + ridge::DriveStage::latency();
        double cxy = 0.0, cxx = 0.0, cyy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto w = pos & (ringSize - 1);
            phaseLine[0][(std::size_t) w] = l[i];
            phaseLine[1][(std::size_t) w] = r[i];
            dryLine[0][(std::size_t) w] = l[i];
            dryLine[1][(std::size_t) w] = r[i];
            auto readAt = [&] (int c, int d) { return phaseLine[(std::size_t) c][(std::size_t) ((pos - std::clamp (d, 0, ringSize - 1)) & (ringSize - 1))]; };
            float a = readAt (0, moveL ? base + shift : base), b = readAt (1, moveR ? base + shift : base);
            if (allPass)
            {
                if (moveL) a = second ? ap2.process (a, 0) : ap1.process (a, 0);
                if (moveR) b = second ? ap2.process (b, 1) : ap1.process (b, 1);
            }
            if (invert)
            {
                if (moveL) a = -a;
                if (moveR) b = -b;
            }
            l[i] = a;
            r[i] = b;
            // the correlation meter: the moving signal against its reference (the side chain, or the other channel)
            double x, y;
            if (align == 0)
            {
                x = 0.5 * (a + b);
                y = sideL != nullptr ? 0.5 * ((double) sideL[i] + (sideR != nullptr ? sideR[i] : sideL[i])) : x;
            }
            else
            {
                x = align == 1 ? b : a;
                y = align == 1 ? a : b;
            }
            cxy += x * y;
            cxx += x * x;
            cyy += y * y;
            ++pos;
        }
        {
            const auto k = std::exp (-(double) n / (0.3 * sr));
            sxy = k * sxy + cxy;
            sxx = k * sxx + cxx;
            syy = k * syy + cyy;
            const auto c = sxx > 1e-12 && syy > 1e-12 ? sxy / std::sqrt (sxx * syy) : 0.0;
            correlation.store ((float) std::clamp (0.5 + 0.5 * c, 0.0, 1.0), std::memory_order_relaxed);
        }

        // the EQ and the compressor on Ridge's engine (no latency: its drive and voicings stay out), the auto gain after
        eqComp.process (l, r, n, ridgeSettings, bpm);
        if (const auto mk = f::autoGainDb (p); mk > 0.0)
        {
            const auto g = (float) dbToGain (mk);
            for (int i = 0; i < n; ++i)
            {
                l[i] *= g;
                r[i] *= g;
            }
        }

        // the amp: always running (a constant latency), mixed out while the drive is off
        const bool driveIn = on (p[f::driveIn]);
        const bool bass = on (p[f::driveModel]);
        const auto model = bass ? ridge::driveBassAmp : ridge::driveGuitarAmp;
        const auto amount = ridge::driveAmountValue (model, f::ampDrive (p[bass ? f::bassDrive : f::guitarDrive]));
        const auto pre = bass ? 1.0f : (float) dbToGain (f::preAmpDb (p[f::preAmp]));
        amp.process (l, r, n, model, amount, driveIn ? pre : 1.0f, 1.0f, driveIn ? std::clamp (p[f::driveMix], 0.0f, 1.0f) : 0.0f);
        if (driveIn)
        {
            if (! bass)
            {
                if (boostOn) boost.processBlock (l, r, n);
                if (punchOn) punch.processBlock (l, r, n);
                if (edgeOn)
                {
                    edgeBroad.processBlock (l, r, n);
                    edgeHigh.processBlock (l, r, n);
                }
            }
            else
            {
                if (bassPunchOn)
                {
                    bassA.processBlock (l, r, n);
                    bassB.processBlock (l, r, n);
                }
                if (shapeOn)
                {
                    shapeBell.processBlock (l, r, n);
                    shapeHigh.processBlock (l, r, n);
                }
            }
        }

        // the output mix (the dry signal waits for the strip's latency), the output gain, the listen switches
        const auto mix = std::clamp (p[f::outputMix], 0.0f, 1.0f);
        const auto outGain = (float) dbToGain (f::gainDb (p[f::output]));
        const bool mono = on (p[f::monoListen]), scListen = on (p[f::scListen]) && sideL != nullptr;
        // EQ Auto listen: while the editor moves an EQ band's knob, that band alone (a low-pass under the low shelf, a
        // band-pass round the mid bell, a high-pass over the high shelf); nothing otherwise, as the original renders
        const auto band = on (p[f::autoListen]) ? (int) std::lround (std::clamp (p[f::listenBand], 0.0f, 1.0f) * 3.0f) : 0;
        if (band > 0)
        {
            const auto hz = band == 1 ? 450.0 : band == 2 ? f::midKHz (p[f::midFreq]) * 1000.0 : 4262.0;
            if (band != listenShown || hz != listenHz)
            {
                if (band != listenShown) listen.reset();
                listen.filter (sr, band == 1 ? 0 : band == 3 ? 1 : 2, hz, band == 2 ? 1.4 : 0.707);
                listenShown = band;
                listenHz = hz;
            }
        }
        else
            listenShown = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto rd = (pos - n + i - totalLatency) & (ringSize - 1);
            auto a = mix * l[i] + (1.0f - mix) * dryLine[0][(std::size_t) rd];
            auto b = mix * r[i] + (1.0f - mix) * dryLine[1][(std::size_t) rd];
            a *= outGain;
            b *= outGain;
            if (scListen)
            {
                a = sideL[i];
                b = sideR != nullptr ? sideR[i] : sideL[i];
            }
            if (mono)
                a = b = 0.5f * (a + b);
            l[i] = a;
            r[i] = b;
        }
        if (band > 0)
            listen.processBlock (l, r, n);
        meters.set (0.0f, 0.0f, inPeak, Meters::peak (l, r, n));
        sides[2].store (Meters::peak (l, l, n), std::memory_order_relaxed);
        sides[3].store (Meters::peak (r, r, n), std::memory_order_relaxed);
    }

private:
    // the filters follow the settings (designed again only when one of them moves)
    void design (const EffectParams& p) noexcept
    {
        namespace f = fret;
        std::array<float, 10> now { p[f::allPassFreq], p[f::allPassQ], p[f::guitarBoost], p[f::guitarPunch], p[f::guitarEdge],
                                    p[f::bassPunch], p[f::bassShape], p[f::eqIn], p[f::compThreshold], p[f::lowGain] };
        bool same = designed;
        for (std::size_t i = 0; i < now.size() && same; ++i)
            same = now[i] == last[i];
        // the EQ / compressor settings are cheap to rebuild and change with any of their controls: every block
        ridgeSettings = f::ridgeParams (p);
        if (same) return;
        designed = true;
        last = now;
        // the all-pass: first order inverted at DC (180 degrees low, 0 high), second order the cookbook's
        const auto apHz = f::allPassHz (p[f::allPassFreq]);
        {
            const auto k = std::tan (3.141592653589793 * std::min (apHz, sr * 0.45) / sr);
            const auto a = (k - 1.0) / (k + 1.0);
            ap1.set (-a, -1.0, 0.0, 1.0, a, 0.0);
            const auto w = 2.0 * 3.141592653589793 * std::min (apHz, sr * 0.45) / sr;
            const auto alpha = std::sin (w) / (2.0 * f::allPassQOf (p[f::allPassQ]));
            ap2.set (1.0 - alpha, -2.0 * std::cos (w), 1.0 + alpha, 1.0 + alpha, -2.0 * std::cos (w), 1.0 - alpha);
        }
        // the guitar voicings (measured at drive 0, where the amp is linear: GS_bank / GS_x)
        // Boost: a first-order low shelf, half gain at 250 Hz, ±15 dB at ±100 %
        const auto boostPct = f::voicePct (p[f::guitarBoost]) / 100.0;
        boostOn = std::abs (boostPct) > 1e-4;
        firstOrderShelf (boost, sr, 250.0, 15.0 * boostPct, false);
        // Punch: a wide mid bell, a deeper and narrower cut (-27 dB at 710 Hz) than the very broad lift (+12 dB round 840 Hz)
        const auto punchPct = f::voicePct (p[f::guitarPunch]) / 100.0;
        punchOn = std::abs (punchPct) > 1e-4;
        if (punchPct < 0.0)
            punch.peaking (sr, 712.0, 0.51, 27.3 * punchPct);
        else
            punch.peaking (sr, 843.0, 0.15, 11.7 * punchPct);
        // Edge (0 .. 10): a broad first-order high shelf from ~200 Hz plus a steeper presence shelf from ~6 kHz
        const auto e = f::voiceSteps (p[f::guitarEdge]);
        edgeOn = e > 1e-3;
        auto nodes = [] (double x, std::initializer_list<double> v) {
            const auto t = std::clamp (x / 2.5, 0.0, 4.0);
            const auto i = std::min ((int) t, 3);
            const auto* a = v.begin();
            return a[i] + (a[i + 1] - a[i]) * (t - i);
        };
        firstOrderShelf (edgeBroad, sr, 206.0, nodes (e, { 0.0, 0.3, 2.5, 4.5, 12.5 }), true);
        edgeHigh.shelf (sr, std::min (7100.0, sr * 0.45), nodes (e, { 0.0, 0.3, 5.0, 14.0, 18.9 }), true, 1.0);
        // the bass Punch: a cut is a high shelf (-12.6 dB from ~800 Hz at -100 %), a lift a broad mid bell (+5.8 dB at 720 Hz)
        // with a little air
        const auto bp = f::voicePct (p[f::bassPunch]) / 100.0;
        bassPunchOn = std::abs (bp) > 1e-4;
        if (bp < 0.0)
        {
            bassA.shelf (sr, 813.0, 12.6 * bp, true, 0.55);
            bassB.set (1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        }
        else
        {
            bassA.peaking (sr, 717.0, 0.335, 5.79 * bp);
            bassB.shelf (sr, std::min (6350.0, sr * 0.45), 3.04 * bp, true, 0.7);
        }
        // the bass Shape (0 .. 10): a mid scoop at 700 Hz, -2.2 dB per step, and a slight lift above 1.3 kHz
        const auto sh = f::voiceSteps (p[f::bassShape]);
        shapeOn = sh > 1e-3;
        shapeBell.peaking (sr, 699.0, 0.71, -2.22 * sh);
        shapeHigh.shelf (sr, 1311.0, 0.307 * sh, true, 0.7);
    }

    static constexpr int ringSize = 16384; // the 20 ms ± 20 ms + the amp's latency at up to 192 kHz
    double sr = 44100.0;
    ridge::Strip eqComp;
    ridge::DriveStage amp;
    EffectParams ridgeSettings {};
    std::array<std::array<float, ringSize>, 2> phaseLine {}, dryLine {};
    int pos = 0;
    Biquad ap1, ap2, boost, punch, edgeBroad, edgeHigh, bassA, bassB, shapeBell, shapeHigh, listen;
    int listenShown = 0;
    double listenHz = 0.0;
    bool boostOn = false, punchOn = false, edgeOn = false, bassPunchOn = false, shapeOn = false, designed = false;
    std::array<float, 10> last {};
    double sxy = 0.0, sxx = 0.0, syy = 0.0;
    std::atomic<float> correlation { 0.5f };
    const float* scL = nullptr;
    const float* scR = nullptr;
    Meters meters;
    std::array<std::atomic<float>, 4> sides {};
};

// ================================================================== Shell Channel: the effect
class ShellChannel final : public Effect
{
public:
    void prepare (double rate) noexcept override
    {
        sr = rate;
        reset();
    }
    void reset() noexcept override
    {
        for (auto& line : inLine) line.fill (0.0f);
        pos = 0;
        gateEnv = 0.0;
        gateGain = 1.0;
        gateOpen = false;
        holdLeft = 0;
        ef = es = 0.0;
        lmcGr = 0.0;
        for (auto* b : { &lf1, &lf2, &lf3, &hf1, &hf2, &nbHp, &nbLp })
            b->reset();
        designed = false;
    }
    int latencySamples() const noexcept override { return shell::latencySamples (sr); }
    // 0 the transient shaper's lift (0..1 over 0..20 dB), 1 the gate's reduction (0..1 over 0..80 dB), 2 / 3 input /
    // output peaks (the gate meter reads its input from 2), 4..7 input L / R and output L / R
    float controlOutput (int k) const noexcept override
    {
        if (k >= 4 && k < 8) return sides[(std::size_t) k - 4].load (std::memory_order_relaxed);
        if (k == 0) return shaperLift.load (std::memory_order_relaxed);
        if (k == 1) return std::clamp (gateGrDb.load (std::memory_order_relaxed) / 80.0f, 0.0f, 1.0f);
        return meters.read (k);
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        namespace s = shell;
        design (p);
        const auto inGain = (float) dbToGain (s::gainDb (p[s::input]));
        const auto lat = s::latencySamples (sr);
        // the input, delayed by the strip's fixed latency (every path, the dry one too, stays aligned)
        for (int i = 0; i < n; ++i)
        {
            const auto w = (pos + i) & (ringSize - 1);
            inLine[0][(std::size_t) w] = l[i] * inGain;
            inLine[1][(std::size_t) w] = r[i] * inGain;
            const auto rd = (pos + i - lat) & (ringSize - 1);
            l[i] = inLine[0][(std::size_t) rd];
            r[i] = inLine[1][(std::size_t) rd];
        }
        pos = (pos + n) & (ringSize - 1);
        const auto inPeak = Meters::peak (l, r, n);
        sides[0].store (Meters::peak (l, l, n), std::memory_order_relaxed);
        sides[1].store (Meters::peak (r, r, n), std::memory_order_relaxed);
        std::array<float, maxBlock> dryL {}, dryR {};
        for (int start = 0; start < n; start += maxBlock)
        {
            const auto m = std::min (maxBlock, n - start);
            auto* a = l + start;
            auto* b = r + start;
            std::copy (a, a + m, dryL.begin());
            std::copy (b, b + m, dryR.begin());
            for (const auto module : s::orderOf (s::orderIndex (p[s::processOrder])))
            {
                switch (module)
                {
                    case s::gate: if (on (p[s::gateOn])) runGate (a, b, m, p); else gateGrDb.store (0.0f, std::memory_order_relaxed); break;
                    case s::shaper: if (on (p[s::tsOn])) runShaper (a, b, m, p); break;
                    case s::lowEnhancer: if (on (p[s::lfOn]) && lfK > 0.0) runEnhancer (a, b, m, lfK * p[s::lfAmount], true); break;
                    case s::highEnhancer: if (on (p[s::hfOn]) && hfK > 0.0) runEnhancer (a, b, m, -hfK * p[s::hfAmount], false); break;
                    case s::listenMic: if (on (p[s::lmcOn])) runListenMic (a, b, m, p); break;
                    default: break;
                }
            }
            const auto mix = std::clamp (p[s::outputMix], 0.0f, 1.0f);
            const auto outGain = (float) dbToGain (s::gainDb (p[s::output]));
            for (int i = 0; i < m; ++i)
            {
                a[i] = (mix * a[i] + (1.0f - mix) * dryL[(std::size_t) i]) * outGain;
                b[i] = (mix * b[i] + (1.0f - mix) * dryR[(std::size_t) i]) * outGain;
            }
        }
        meters.set (lmcPeakGr, 0.0f, inPeak, Meters::peak (l, r, n));
        lmcPeakGr = 0.0f;
        sides[2].store (Meters::peak (l, l, n), std::memory_order_relaxed);
        sides[3].store (Meters::peak (r, r, n), std::memory_order_relaxed);
    }

private:
    void design (const EffectParams& p) noexcept
    {
        namespace s = shell;
        std::array<float, 6> now { p[s::lfFreq], p[s::lfDrive], p[s::hfFreq], p[s::hfDrive], p[s::lmcNarrow], 0.0f };
        if (designed && now == last) return;
        designed = true;
        last = now;
        // the low enhancer: a sixth-order Butterworth low-pass at 0.72x the frequency, added at K (drive 5: 6.5x,
        // drive 10: 7.35x; below 5 in proportion) — LF bank, sweep fits
        const auto lfd = s::driveSteps (p[s::lfDrive]);
        lfK = lfd <= 5.0 ? 1.3 * lfd : 6.5 + 0.17 * (lfd - 5.0);
        const auto lfc = 0.72 * s::lfHz (p[s::lfFreq]);
        lf1.filter (sr, 0, lfc, 0.5176);
        lf2.filter (sr, 0, lfc, 0.7071);
        lf3.filter (sr, 0, lfc, 1.9319);
        // the high enhancer: a third-order Butterworth high-pass at the frequency, subtracted at K (drive 5: 4.1x; above,
        // the corner moves down with the cube root of the drive; below, K in proportion) — HF bank, sweep fits
        const auto hfd = s::driveSteps (p[s::hfDrive]);
        auto hfc = s::hfHz (p[s::hfFreq]) * (hfd > 5.0 ? std::cbrt (5.0 / hfd) : 1.0);
        hfK = hfd <= 5.0 ? 0.82 * hfd : 4.1 + 0.02 * (hfd - 5.0);
        if (const auto cap = sr * 0.45; hfc > cap) // above the band: the same skirt from a corner the rate allows
        {
            hfK *= std::pow (cap / hfc, 3.0);
            hfc = cap;
        }
        firstOrderHighPass (hf1, sr, hfc);
        hf2.filter (sr, 1, hfc, 1.0);
        // the listen-mic compressor's narrow band: first-order 300 Hz high-pass and 8 kHz low-pass before it
        firstOrderHighPass (nbHp, sr, 300.0);
        firstOrderLowPass (nbLp, sr, 8000.0);
    }

    // the gate: a peak detector (instant rise, 5 ms fall) opens at Open and closes under Close, holds, then the gain falls
    // linearly to the range's floor over the release time and rises over the attack (one pole, attack x 0.03)
    void runGate (float* a, float* b, int n, const EffectParams& p) noexcept
    {
        namespace s = shell;
        const auto openDb = s::thresholdDb (p[s::gateOpen]);
        const auto closeDb = std::min (openDb, s::thresholdDb (p[s::gateClose]));
        const auto floor = dbToGain (-s::rangeDb (p[s::gateRange]));
        const auto fall = coef (0.005, sr);
        const auto atk = coef (s::attackS (p[s::gateAttack]) * 0.03, sr);
        const auto relStep = (1.0 - floor) / std::max (1.0, s::releaseS (p[s::gateRelease]) * sr);
        const auto hold = (int) (s::holdMs (p[s::gateHold]) * 0.001 * sr);
        const auto openLin = dbToGain (openDb), closeLin = dbToGain (closeDb);
        for (int i = 0; i < n; ++i)
        {
            const auto x = (double) std::max (std::abs (a[i]), std::abs (b[i]));
            gateEnv = x > gateEnv ? x : fall * gateEnv;
            if (gateEnv >= openLin)
            {
                gateOpen = true;
                holdLeft = hold;
            }
            else if (gateEnv < closeLin && gateOpen)
            {
                if (holdLeft > 0) --holdLeft;
                else gateOpen = false;
            }
            if (gateOpen)
                gateGain = atk * gateGain + (1.0 - atk);
            else
                gateGain = std::max (floor, gateGain - relStep);
            a[i] *= (float) gateGain;
            b[i] *= (float) gateGain;
        }
        gateGrDb.store ((float) -gainToDb (gateGain), std::memory_order_relaxed);
    }

    // the transient shaper (DS_bank ts* / tsInv / tsG10 fits): two followers of the peak — a fast one (0.14 ms rise) and a
    // slow one (82 .. 122 ms rise with Speed), both 38 ms fall — their ratio in dB over a small floor (Gain moves the
    // floor and the depth), less 1.1 dB, times the depth, is the lift G; out = x (1 ± Amount (G - 1)); Listen = the change
    void runShaper (float* a, float* b, int n, const EffectParams& p) noexcept
    {
        namespace s = shell;
        const auto speed = (double) std::clamp (p[s::tsSpeed], 0.0f, 1.0f);
        const auto g = s::gainDb (p[s::tsGain]);
        const auto depth = std::max (0.0, (2.435 - 0.838 * speed - 0.772 * speed * speed) * (1.0 + 0.0258 * g));
        const auto eps = dbToGain (-8.0 - 0.48 * g);
        const auto aF = 1.0 - std::exp (-1.0 / (0.136e-3 * sr));
        const auto aS = 1.0 - std::exp (-1.0 / (121.6e-3 * std::pow (81.5 / 121.6, speed) * sr));
        const auto rel = std::exp (-1.0 / (38.3e-3 * sr));
        const auto amount = (double) std::clamp (p[s::tsAmount], 0.0f, 1.0f);
        const auto sign = on (p[s::tsInvert]) ? -1.0 : 1.0;
        const bool listen = on (p[s::tsListen]);
        double maxLift = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto x = (double) std::max (std::abs (a[i]), std::abs (b[i]));
            ef = x > ef ? ef + (x - ef) * aF : ef * rel;
            es = x > es ? es + (x - es) * aS : es * rel;
            const auto liftDb = depth * std::max (0.0, 20.0 * std::log10 ((ef + eps) / (es + eps)) - 1.1);
            maxLift = std::max (maxLift, liftDb);
            const auto change = sign * amount * (dbToGain (liftDb) - 1.0);
            const auto gain = (float) (listen ? change : 1.0 + change);
            a[i] *= gain;
            b[i] *= gain;
        }
        shaperLift.store ((float) std::clamp (maxLift / 20.0, 0.0, 1.0), std::memory_order_relaxed);
    }

    // an enhancer: the band (its filters) times k added to the signal
    void runEnhancer (float* a, float* b, int n, double k, bool low) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float u = a[i], v = b[i];
            if (low)
            {
                u = lf3.process (lf2.process (lf1.process (u, 0), 0), 0);
                v = lf3.process (lf2.process (lf1.process (v, 1), 1), 1);
            }
            else
            {
                u = hf2.process (hf1.process (u, 0), 0);
                v = hf2.process (hf1.process (v, 1), 1);
            }
            a[i] += (float) k * u;
            b[i] += (float) k * v;
        }
    }

    // the listen-mic compressor (lmc fits): the narrow band's filters (when on) then a fast peak compressor, 4.4 : 1, its
    // threshold -7.4 dB less 40 dB x Comp, 0.26 ms attack / 11 ms release, the make-up, then the dry / wet mix
    void runListenMic (float* a, float* b, int n, const EffectParams& p) noexcept
    {
        namespace s = shell;
        const auto thr = -7.4 - 40.0 * (double) std::clamp (p[s::lmcComp], 0.0f, 1.0f);
        const auto slope = 1.0 - 1.0 / 4.41;
        const auto att = coef (0.26e-3, sr), rel = coef (11.3e-3, sr);
        const auto makeup = dbToGain (s::gainDb (p[s::lmcMakeup]));
        const auto mix = (double) std::clamp (p[s::lmcMix], 0.0f, 1.0f);
        const bool narrow = on (p[s::lmcNarrow]);
        for (int i = 0; i < n; ++i)
        {
            double u = a[i], v = b[i];
            if (narrow)
            {
                u = nbLp.process (nbHp.process ((float) u, 0), 0);
                v = nbLp.process (nbHp.process ((float) v, 1), 1);
            }
            const auto lvl = gainToDb (std::max (std::abs (u), std::abs (v)));
            const auto want = std::max (0.0, (lvl - thr) * slope);
            lmcGr = want > lmcGr ? att * lmcGr + (1.0 - att) * want : rel * lmcGr + (1.0 - rel) * want;
            lmcPeakGr = std::max (lmcPeakGr, (float) lmcGr);
            const auto g = dbToGain (-lmcGr) * makeup;
            a[i] = (float) (mix * u * g + (1.0 - mix) * a[i]);
            b[i] = (float) (mix * v * g + (1.0 - mix) * b[i]);
        }
    }

    static constexpr int ringSize = 2048, maxBlock = 256;
    double sr = 44100.0;
    std::array<std::array<float, ringSize>, 2> inLine {};
    int pos = 0;
    double gateEnv = 0.0, gateGain = 1.0;
    bool gateOpen = false;
    int holdLeft = 0;
    double ef = 0.0, es = 0.0, lmcGr = 0.0;
    float lmcPeakGr = 0.0f;
    double lfK = 0.0, hfK = 0.0;
    Biquad lf1, lf2, lf3, hf1, hf2, nbHp, nbLp;
    std::array<float, 6> last {};
    bool designed = false;
    std::atomic<float> gateGrDb { 0.0f }, shaperLift { 0.0f };
    Meters meters;
    std::array<std::atomic<float>, 4> sides {};
};
} // namespace

std::unique_ptr<Effect> makeFret() { return std::make_unique<FretChannel>(); }
std::unique_ptr<Effect> makeShell() { return std::make_unique<ShellChannel>(); }

const EffectInfo& fretInfo()
{
    // the reference's defaults (its own parameter values at a new instance)
    static const EffectInfo info { "Fret Channel",
                                   { { "Input", 0.5f }, { "Output", 0.5f }, { "Output mix", 1.0f }, { "Phase correction", 1.0f },
                                     { "Align to", 0.0f }, { "Delay in", 1.0f }, { "Delay", 0.5f }, { "Phase invert", 0.0f },
                                     { "All-pass in", 0.0f }, { "All-pass freq.", 2.0f / 3.0f }, { "All-pass Q", 0.562382f },
                                     { "All-pass order", 1.0f }, { "High-pass", 0.0f }, { "EQ in", 1.0f }, { "EQ post", 0.0f },
                                     { "Low-pass", 1.0f }, { "Low gain", 0.5f }, { "Mid freq.", 0.5f }, { "Mid gain", 0.5f },
                                     { "High gain", 0.5f }, { "Compressor", 1.0f }, { "Threshold", 1.0f }, { "Comp. mix", 1.0f },
                                     { "Auto gain", 0.0f }, { "Comp. mode", 0.5f }, { "Drive in", 0.0f }, { "Amp model", 0.0f },
                                     { "Pre-amp", 1.0f / 6.0f }, { "Guitar drive", 0.0f }, { "Boost", 0.5f }, { "Punch", 0.5f },
                                     { "Edge", 0.0f }, { "Bass drive", 0.0f }, { "Bass punch", 0.5f }, { "Bass shape", 0.0f },
                                     { "Drive mix", 1.0f }, { "Mono listen", 0.0f }, { "Side-chain listen", 0.0f }, { "EQ auto listen", 0.0f } } };
    return info;
}

const EffectInfo& shellInfo()
{
    static const EffectInfo info { "Shell Channel",
                                   { { "Input", 0.5f }, { "Gate", 0.0f }, { "Gate attack", 0.5f }, { "Gate hold", 0.0f },
                                     { "Gate release", 0.494949f }, { "Gate open", 0.5f }, { "Gate close", 0.2f }, { "Gate range", 1.0f },
                                     { "Shaper gain", 0.5f }, { "Shaper speed", 0.5f }, { "Shaper amount", 0.0f }, { "Transient shaper", 0.0f },
                                     { "Shaper invert", 0.0f }, { "Shaper listen", 0.0f }, { "High enhancer", 0.0f },
                                     { "High freq.", 0.493109f }, { "High drive", 0.5f }, { "High amount", 0.0f }, { "Low enhancer", 0.0f },
                                     { "Low freq.", 0.496001f }, { "Low drive", 0.5f }, { "Low amount", 0.0f }, { "Listen mic", 0.0f },
                                     { "Narrow band", 0.0f }, { "LMC comp", 0.0f }, { "LMC make-up", 0.5f }, { "LMC mix", 1.0f },
                                     { "Output", 0.5f }, { "Output mix", 1.0f }, { "Process order", 0.0f } } };
    return info;
}

std::string fretParamText (int param, float v)
{
    namespace f = fret;
    switch (param)
    {
        case f::input: case f::output: return fmt ("%+.1f dB", f::gainDb (v));
        case f::outputMix: case f::compMix: case f::driveMix: return fmt ("%.1f %%", v * 100.0);
        case f::alignMode: return f::alignName (f::alignOf (v));
        case f::delay: return fmt ("%+.2f ms", f::delayMs (v));
        case f::allPassFreq: return fmt ("%.0f Hz", f::allPassHz (v));
        case f::allPassQ: return fmt ("%.2f", f::allPassQOf (v));
        case f::allPassOrder: return on (v) ? "2nd" : "1st";
        case f::hpf: return v <= 0.0f ? std::string ("Off") : fmt ("%.0f Hz", f::hpfHz (v));
        case f::lpf: return v >= 0.9999f ? std::string ("Off") : fmt ("%.1f kHz", f::lpfKHz (v));
        case f::lowGain: case f::midGain: case f::highGain: return fmt ("%+.1f dB", f::shelfDb (v));
        case f::midFreq: return fmt ("%.2f kHz", f::midKHz (v));
        case f::compThreshold: return fmt ("%.1f dB", f::thresholdDb (v));
        case f::compMode: return f::modeName (f::modeOf (v));
        case f::driveModel: return on (v) ? "Bass amp" : "Guitar amp";
        case f::preAmp: return fmt ("%+.1f dB", f::preAmpDb (v));
        case f::guitarDrive: case f::bassDrive: return fmt ("%.2f", f::ampDrive (v));
        case f::guitarBoost: case f::guitarPunch: case f::bassPunch: return fmt ("%+.0f %%", f::voicePct (v));
        case f::guitarEdge: case f::bassShape: return fmt ("%.2f", f::voiceSteps (v));
        case f::eqPrePost: return on (v) ? "Post" : "Pre";
        default: return v >= 0.5f ? "On" : "Off";
    }
}

std::string shellParamText (int param, float v)
{
    namespace s = shell;
    switch (param)
    {
        case s::input: case s::output: case s::tsGain: case s::lmcMakeup: return fmt ("%+.1f dB", s::gainDb (v));
        case s::gateAttack: return fmt ("%.3f s", s::attackS (v));
        case s::gateHold: return fmt ("%.0f ms", s::holdMs (v));
        case s::gateRelease: return fmt ("%.2f s", s::releaseS (v));
        case s::gateOpen: case s::gateClose: return fmt ("%.1f dB", s::thresholdDb (v));
        case s::gateRange: return fmt ("%.1f dB", s::rangeDb (v));
        case s::tsSpeed: return fmt ("%.2f", (double) v);
        case s::tsAmount: case s::hfAmount: case s::lfAmount: case s::lmcComp: case s::lmcMix: case s::outputMix: return fmt ("%.1f %%", v * 100.0);
        case s::hfFreq: return fmt ("%.0f Hz", s::hfHz (v));
        case s::lfFreq: return fmt ("%.1f Hz", s::lfHz (v));
        case s::hfDrive: case s::lfDrive: return fmt ("%.2f", s::driveSteps (v));
        case s::processOrder: return s::orderText (s::orderIndex (v));
        default: return v >= 0.5f ? "On" : "Off";
    }
}
