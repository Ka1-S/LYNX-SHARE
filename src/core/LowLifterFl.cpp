#include "core/LowLifterFl.h"
#include "core/BassHarmonics.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/DelphiMath.h"
#include "core/Denormals.h"
#include "core/IldspMath.h"

// Clean-room rebuild of FL's Low Lifter engine (docs/analise/REA_rodada154.md §5). Float32 like the library; the order
// of every sum and product follows the library's build (the two channels were compiled with different groupings in a
// few places, kept here).

#pragma clang fp contract(off)

namespace lynx::lowlifter
{
namespace
{
constexpr float sqrt2 = 1.4142135f, twoSqrt2 = 2.828427f;

// the library's tanf for |x| ≤ π/4 (the engine's arguments): x below 2⁻²⁷, x + x³/3 below 2⁻¹³, else a rational fit,
// evaluated in double with fused multiply-adds when the CPU has them (as the library dispatches), then rounded
inline float tanf_ (float xf) noexcept
{
    const double x = xf;
    const double ax = std::fabs (x);
    if (ax > 0.7853981633974483)
        return (float) std::tan (x);
    if (ax < 7.450580596923828e-09)
        return xf;
    const bool fma = ildsp::useFma();
    if (ax < 1.220703125e-04)
    {
        const double x3 = (x * x) * x;
        return ildsp::d2f (fma ? ildsp::fmaD (x3, 0.3333333333333333, x) : x3 * 0.3333333333333333 + x);
    }
    const double x2 = x * x;
    double p, q;
    if (fma)
    {
        p = ildsp::fmaD (-0.017203248047148168, x2, 0.3852960712639954);
        q = ildsp::fmaD (ildsp::fmaD (0.01844239256901656, x2, -0.5139650547885454), x2, 1.1558882143468838);
    }
    else
    {
        p = -0.017203248047148168 * x2 + 0.3852960712639954;
        q = (0.01844239256901656 * x2 + -0.5139650547885454) * x2 + 1.1558882143468838;
    }
    const double r = p / q;
    const double x3 = x2 * x;
    return ildsp::d2f (fma ? ildsp::fmaD (x3, r, x) : x3 * r + x);
}

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o)
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}

// one TPT state-variable step: returns v = (x − (s1·k + s2))·g·inv; bp = s1 + v; the states advance
struct Step
{
    float v, bp, t, lp;
};
inline Step svf (Engine::Svf& s, float x, float g, float k, float inv) noexcept
{
    Step r;
    r.v = ((x - (s.s1 * k + s.s2)) * g) * inv;
    r.bp = s.s1 + r.v;
    r.t = r.bp * g;
    r.lp = r.t + s.s2;
    s.s1 = (r.v + r.v) + s.s1;
    s.s2 = (r.t + r.t) + s.s2;
    return r;
}
inline float smooth (Engine::Smooth& s, float target, float k, float k2) noexcept
{
    const float d = target - s.s;
    const float o = k * d + s.s;
    s.s = k2 * d + s.s;
    return o;
}
inline float apLength (float xoInv, float len) noexcept
{
    float r = xoInv * len - 1.0f;
    if (r <= 0.0f)
        r = 0.0f;
    if (48000.0f <= r)
        r = 48000.0f;
    return r;
}
} // namespace

float engineValue (int k, int raw) noexcept
{
    const auto x = (float) std::clamp ((double) raw * (1.0 / rawMax[(std::size_t) std::clamp (k, 0, numParams - 1)]), 0.0, 1.0);
    auto lerp = [&] (double a, double b, double c) {
        const double p = x > 0.0f ? dmath::exp (dmath::ln ((double) x) / c) : 0.0;
        return (float) (a + (b - a) * p);
    };
    switch (k)
    {
        case 1: case 7: return x;
        case 2: return lerp (-120.0, 12.0, 7.275);
        case 3: return lerp (-60.0, 0.0, 3.105);
        case 6: return lerp (0.0, 18.0, 1.0);
        case 8: return lerp (-0.99, 0.99, 1.0);
        case 9: return lerp (-120.0, 0.0, 5.0);
        case 10: case 11: return lerp (20.0, 120.0, 0.498918);
        default: return (float) raw;
    }
}

Engine::Engine() { init (44100); }

void Engine::init (int sampleRate)
{
    float r = (float) sampleRate;
    if (r <= 1.0f)
        r = 1.0f;
    fs = r < 192000.0f ? r : 192000.0f;
    const float inv = 1.0f / fs;
    g0 = tanf_ (inv * 62.831852f);
    kG = g0 / (g0 + 1.0f);
    kG2 = kG + kG;
    piFs = inv * 3.1415927f;
    quarterPiFs = inv * 0.7853982f;
    halfPiFs = inv * 1.5707964f;
    k0 = g0 + sqrt2;
    const float d0 = k0 * g0 + 1.0f;
    g0Inv = 1.0f / d0;
    g0Half = g0Inv * g0;
    g0Twice = g0Half + g0Half;
    g0Two = g0 + g0;
    holdBase = fs * 0.25f;
    kRelease = ildsp::expf_ (inv * -144.44786f);
    attackBase = inv * 14.444787f;
    {
        const float t = tanf_ (inv * 15.707963f);
        kSlope = t / (t + 1.0f);
        kSlope2 = kSlope + kSlope;
    }
    {
        const float t = tanf_ (piFs);
        kFade = t / (t + 1.0f);
        kFade2 = kFade + kFade;
    }
    apLen[0] = fs * 0.5f;
    apLen[1] = fs * 0.20710678f;
    apLen[2] = fs * 0.15891862f;
    apLen[3] = fs * 0.1339746f;
    // all states cleared
    for (auto* s : { &mix, &lowCut, &hiSw, &hiGain, &mb, &xover, &pol, &loSw, &loGain, &dMix, &dDepth, &preGain, &slope, &fade })
        s->s = 0.0f;
    index = 0;
    for (auto& c : ch)
        c = Channel();
}

void Engine::setControl (int k, float v) noexcept
{
    switch (k)
    {
        case 0: on = v; break;
        case 1: dryWet = v; break;
        case 2: lowGainDb = v; break;
        case 3: slopeDb = v; break;
        case 4: lookahead = v; break;
        case 5: invert = v; break;
        case 6: preGainDb = v; break;
        case 7: diffMix = v; break;
        case 8: diffDepth = v; break;
        case 9: highGainDb = v; break;
        case 10: crossoverHz = v; break;
        case 11: lowCutHz = v; break;
        case 12: multiband = v; break;
        case 13: highSwitch = v; break;
        case 14: lowSwitch = v; break;
        default: break;
    }
}

float Engine::runHarmonics (Harmonics& h, float in, int delay, float holdLen, float kAttack, float a, float b, float norm) noexcept
{
    h.buf[(std::size_t) (index & 0x7ff)] = in;
    float x = std::fabs (in);
    const float xd = h.buf[(std::size_t) ((index - delay) & 0x7ff)];
    for (int k = 0; k < 4; ++k)
    {
        const bool keep = (float) h.count[k] < holdLen && x < h.hold[k];
        h.count[k] = keep ? h.count[k] + 1 : 0;
        if (keep)
            x = h.hold[k];
        h.hold[k] = x;
    }
    if (x <= 1.0f)
        x = 1.0f;
    for (int k = 0; k < 4; ++k)
    {
        const float c = x <= h.env[k] ? kRelease : kAttack;
        h.env[k] = (1.0f - c) * x + c * h.env[k];
        x = h.env[k];
    }
    const float e = x;
    float t2 = (xd * xd) / (e * e);
    t2 = (t2 + t2) - 1.0f;
    const float y = (((((t2 + t2) - 1.0f) * b + 1.0f) * xd) / e + t2 * a) * norm;
    const float w = y - (h.dc.s1 * k0 + h.dc.s2);
    const float s1 = h.dc.s1;
    h.dc.s1 = w * g0Twice + s1;
    h.dc.s2 = (w * g0Half + s1) * g0Two + h.dc.s2;
    return w;
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    ScopedFlushDenormals ftz;
    const float tMix = (int) on == 0 ? 1.0f : 1.0f - dryWet;
    const float tLowCut = lowCutHz, tHiSw = highSwitch;
    const float tHiGain = ildsp::powf_ (10.0f, highGainDb * 0.05f);
    const float tMb = multiband, tXover = crossoverHz;
    const float la = (int) lookahead == 0 ? 0.0f : 0.01f;
    const int delay = (int) (la * fs);
    const float tPol = (int) invert == 0 ? 1.0f : -1.0f;
    const float tLoSw = lowSwitch;
    const float tLoGain = ildsp::powf_ (10.0f, lowGainDb * 0.05f);
    const float tDMix = diffMix, tDDepth = diffDepth;
    const float tPre = ildsp::powf_ (10.0f, preGainDb * 0.05f);
    const float holdLen = std::nearbyint ((la + 0.05f) * holdBase);
    const float kAttack = ildsp::expf_ (-(attackBase / la));
    const float tSlope = slopeDb;

    auto& L = ch[0];
    auto& R = ch[1];
    for (int i = 0; i < n; ++i)
    {
        // --- shared controls
        const float dryG = smooth (mix, tMix, kG, kG2);
        const float wetG = 1.0f - dryG;
        const float wLc = tanf_ (smooth (lowCut, tLowCut, kG, kG2) * piFs);
        const float hs = smooth (hiSw, tHiSw, kG, kG2);
        const float hg = smooth (hiGain, tHiGain, kG, kG2);
        const float hiG = hg * hs;
        const float mbO = smooth (mb, tMb, kG, kG2);
        const float mb1 = 1.0f - mbO;
        float xo = smooth (xover, tXover, kG, kG2);
        if (xo <= 20.0f)
            xo = 20.0f;
        const float g1 = tanf_ (piFs * xo);
        const float k1 = g1 + sqrt2;
        const float d1 = k1 * g1 + 1.0f;
        const float inv1 = 1.0f / d1;
        const float g2 = tanf_ (xo * quarterPiFs);
        const float g3 = tanf_ (xo * halfPiFs);
        const float k3 = g3 + sqrt2;
        const float d3 = k3 * g3 + 1.0f;
        const float inv3 = 1.0f / d3;
        const float k2 = g2 + sqrt2;
        const float d2 = k2 * g2 + 1.0f;
        const float inv2 = 1.0f / d2;
        const float polO = smooth (pol, tPol, kG, kG2);
        const float lsO = smooth (loSw, tLoSw, kG, kG2);
        const float lgO = smooth (loGain, tLoGain, kG, kG2);
        const float lowChain = lgO * (lsO * polO);
        const float dm = smooth (dMix, tDMix, kG, kG2);
        float c = smooth (dDepth, tDDepth, kG, kG2);
        if (0.99f <= c)
            c = 0.99f;
        if (c <= -0.99f)
            c = -0.99f;
        const float s = std::sqrt (1.0f - c * c);
        const float pre = smooth (preGain, tPre, kG, kG2);
        const float slO = smooth (slope, tSlope, kSlope, kSlope2);
        const float aCh = ildsp::powf_ (10.0f, slO * 0.05f);
        const float bCh = ildsp::powf_ (10.0f, slO * 0.1f);
        const float norm = 1.0f / ((aCh + aCh) + 1.0f);
        const float xoInv = 1.0f / xo;
        const float fd = smooth (fade, 1.0f, kFade, kFade2);
        const int len[4] { (int) apLength (xoInv, apLen[0]), (int) apLength (xoInv, apLen[1]), (int) apLength (xoInv, apLen[2]),
                           (int) apLength (xoInv, apLen[3]) };
        const float kLc = wLc + sqrt2;
        const float invLc = 1.0f / (wLc * kLc + 1.0f);
        const float wLc1 = wLc + 1.0f;

        for (int side = 0; side < 2; ++side)
        {
            auto& C = side == 0 ? L : R;
            const float x = side == 0 ? inL[i] : inR[i];
            // crossover: 24 dB low band (A → B), and the split around ½ / ¼ of it (C, D, E) and back (F, G)
            const auto A = svf (C.a, x, g1, k1, inv1);
            const auto B = svf (C.b, A.lp, g1, k1, inv1);
            const auto Cc = svf (C.c, x, g3, k3, inv3);
            const float hC = x - Cc.bp * twoSqrt2;
            const auto D = svf (C.d, Cc.lp, g3, k3, inv3);
            Step E;
            {
                E.v = ((hC - ((C.e.s1 * k2 + D.lp) + C.e.s2)) * g2) * inv2;
                E.bp = C.e.s1 + E.v;
                E.t = E.bp * g2;
                C.e.s1 = (E.v + E.v) + C.e.s1;
                C.e.s2 = (E.t + E.t) + C.e.s2;
            }
            const float hE = hC - (E.bp * twoSqrt2 + D.lp);
            const auto F = svf (C.f, hE, g1, k1, inv1);
            const auto G = svf (C.g, F.lp, g1, k1, inv1);
            C.high[(std::size_t) (index & 0x7ff)] = ((hE - F.bp * twoSqrt2) - G.lp) * mbO + ((x - A.bp * twoSqrt2) - B.lp) * mb1;
            // the four bands into the enhancers
            const auto H = svf (C.h, D.lp, g1, k1, inv1);
            const float hH = D.lp - H.bp * twoSqrt2;
            const auto I = svf (C.i, hH, g2, k2, inv2);
            const auto J = svf (C.j, I.lp, g2, k2, inv2);
            const float y1 = runHarmonics (C.band[0], pre * J.lp, delay, holdLen, kAttack, aCh, bCh, norm);
            const float y2 = runHarmonics (C.band[1], ((hH - I.bp * twoSqrt2) - J.lp) * pre, delay, holdLen, kAttack, aCh, bCh, norm);
            const float y3 = runHarmonics (C.band[2], pre * G.lp, delay, holdLen, kAttack, aCh, bCh, norm);
            const float y4 = runHarmonics (C.band[3], pre * B.lp, delay, holdLen, kAttack, aCh, bCh, norm);
            const float sum = ((y2 * g0Inv + y1 * g0Inv) + y3 * g0Inv) * mbO;
            float out0, lowBand;
            if (side == 0)
            {
                const float hf = (sum + mb1 * (y4 * g0Inv)) * fd;
                C.ap0[(std::size_t) (index & 0x1fff)] = s * hf - c * C.z[0];
                const float z0 = C.ap0[(std::size_t) ((index - len[0]) & 0x1fff)];
                out0 = c * hf + s * C.z[0];
                C.z[0] = z0;
                float o = out0;
                for (int k = 1; k < 4; ++k)
                {
                    auto& buf = k == 1 ? C.ap1 : (k == 2 ? C.ap2 : C.ap3);
                    buf[(std::size_t) (index & 0x7ff)] = s * o - c * C.z[k];
                    const float zk = buf[(std::size_t) ((index - len[k]) & 0x7ff)];
                    o = c * o + s * C.z[k];
                    C.z[k] = zk;
                }
                lowBand = ((o * dm + (1.0f - dm) * hf) * lowChain) + hiG * C.high[(std::size_t) ((index - delay) & 0x7ff)];
            }
            else
            {
                const float s0 = sum + (y4 * g0Inv) * mb1;
                C.ap0[(std::size_t) (index & 0x1fff)] = (s0 * fd) * s - c * C.z[0];
                const float z0 = C.ap0[(std::size_t) ((index - len[0]) & 0x1fff)];
                out0 = (c * fd) * s0 + s * C.z[0];
                C.z[0] = z0;
                float o = out0;
                for (int k = 1; k < 4; ++k)
                {
                    auto& buf = k == 1 ? C.ap1 : (k == 2 ? C.ap2 : C.ap3);
                    buf[(std::size_t) (index & 0x7ff)] = s * o - c * C.z[k];
                    const float zk = buf[(std::size_t) ((index - len[k]) & 0x7ff)];
                    o = c * o + s * C.z[k];
                    C.z[k] = zk;
                }
                lowBand = ((o * dm + ((1.0f - dm) * fd) * s0) * lowChain) + hiG * C.high[(std::size_t) ((index - delay) & 0x7ff)];
            }
            // low cut (TPT high-pass)
            const float e = lowBand - (C.lowCut.s1 * kLc + C.lowCut.s2);
            {
                const float v = (wLc * e) * invLc;
                const float s1 = C.lowCut.s1;
                C.lowCut.s1 = (v + v) + s1;
                const float t = (v + s1) * wLc;
                C.lowCut.s2 = (t + t) + C.lowCut.s2;
            }
            const float wetOut = invLc * e;
            // dry path: the matching all-passes
            float ap;
            {
                const float s1 = C.dryA.s1;
                const float v = side == 0 ? ((((s1 * k1 + x) + C.dryA.s2) * g1) * inv1) : ((((s1 * k1 + x) + C.dryA.s2) * g1) / d1);
                C.dryA.s1 = s1 - (v + v);
                const float t = (s1 - v) * g1;
                C.dryA.s2 = (t + t) + C.dryA.s2;
                ap = -x - (s1 - v) * twoSqrt2;
            }
            {
                float w = ((ap - C.dry1) * wLc) / wLc1;
                w = w + w;
                const float z = C.dry1;
                C.dry1 = w + z;
                ap = ((z + z) + w) - ap;
            }
            float ap3;
            {
                const float s1 = C.dryB.s1;
                const float v = side == 0 ? (((ap - (k2 * s1 + C.dryB.s2)) * g2) * inv2) : (((ap - (s1 * k2 + C.dryB.s2)) * g2) / d2);
                C.dryB.s1 = (v + v) + s1;
                const float t = (v + s1) * g2;
                ap3 = ap - (v + s1) * twoSqrt2;
                C.dryB.s2 = (t + t) + C.dryB.s2;
            }
            {
                const float s1 = C.dryC.s1;
                const float v = side == 0 ? (((ap3 - (s1 * k3 + C.dryC.s2)) * g3) * inv3) : (((ap3 - (s1 * k3 + C.dryC.s2)) * g3) / d3);
                C.dryC.s1 = (v + v) + s1;
                const float bp2 = (v + s1) * twoSqrt2;
                const float t = (v + s1) * g3;
                C.dryC.s2 = (t + t) + C.dryC.s2;
                C.dryBuf[(std::size_t) (index & 0x7ff)] = (ap3 - bp2) * mbO + ap * mb1;
            }
            const float out = dryG * C.dryBuf[(std::size_t) ((index - delay) & 0x7ff)] + wetOut * wetG;
            (side == 0 ? outL : outR)[i] = out;
        }
        ++index;
    }
}

// --- state, texts, effect -------------------------------------------------------------------------------------------

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 4 + 4 * numParams)
        return std::nullopt;
    const auto v = i32At (d, 0);
    if (v < 6 || v > 6)
        return std::nullopt;
    State s;
    for (int k = 0; k < numParams; ++k)
        s.raw[(std::size_t) k] = std::clamp (i32At (d, 4 + 4 * (std::size_t) k), 0, rawMax[(std::size_t) k]);
    return s;
}

void toParams (const State& s, float* params) noexcept
{
    for (int k = 0; k < numParams; ++k)
        params[k] = (float) ((double) s.raw[(std::size_t) k] / rawMax[(std::size_t) k]);
}

std::string paramText (int param, float v01)
{
    const int raw = (int) std::lround (std::clamp (v01, 0.0f, 1.0f) * rawMax[(std::size_t) std::clamp (param, 0, numParams - 1)]);
    char b[40];
    switch (param)
    {
        case 1: case 7: std::snprintf (b, sizeof b, "%.0f%%", raw / 100.0); return b;
        case 2: case 3: case 6: case 9: std::snprintf (b, sizeof b, "%.1f dB", (double) engineValue (param, raw)); return b;
        case 8: std::snprintf (b, sizeof b, "%.2f", (double) engineValue (param, raw)); return b;
        case 10: case 11: std::snprintf (b, sizeof b, "%.0f Hz", (double) engineValue (param, raw)); return b;
        default: return raw != 0 ? "On" : "Off";
    }
}

namespace
{
class LowLifterEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        rate = (int) std::lround (sampleRate);
        ready = false;
        bassOn = false;
    }
    void reset() noexcept override {}
    // what the plug-in reports to FL's PDC: the look-ahead, rint(rate · 0.01)
    explicit LowLifterEffect (bool la) : lookahead (la) {}
    int latencySamples() const noexcept override { return lookahead.load() ? (int) std::nearbyint ((double) rate * 0.01) : 0; }
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        if (params[(std::size_t) bassLaw] >= 0.5f)
        {
            // round 190mm (dyn): the psycho-acoustic bass law (set only by the Renaissance Bass / MaxxBass translators)
            bassharm::Settings st;
            st.freqHz = bassFreqHz (params[(std::size_t) bassFreq]);
            st.inputDb = std::max (-72.0, bassLevelDb (params[(std::size_t) bassInput]));
            st.origGain = std::clamp ((double) params[(std::size_t) bassOrig], 0.0, 4.0);
            st.harmDb = bassLevelDb (params[(std::size_t) bassHarm]);
            st.outDb = std::max (-72.0, bassLevelDb (params[(std::size_t) bassOut]));
            if (! bassOn)
            {
                bass.prepare ((double) rate);
                bassOn = true;
            }
            bass.process (left, right, n, st);
            return;
        }
        bassOn = false;
        std::array<int, numParams> raw {};
        for (int k = 0; k < numParams; ++k)
            raw[(std::size_t) k] = (int) std::lround (std::clamp (params[(std::size_t) k], 0.0f, 1.0f) * rawMax[(std::size_t) k]);
        if (! ready)
        {
            if (! engine)
                engine = std::make_unique<Engine>();
            engine->init (rate);
            for (int k = 0; k < numParams; ++k)
                engine->setControl (k, engineValue (k, raw[(std::size_t) k]));
            current = raw;
            lookahead = raw[4] != 0;
            silent = 0;
            tail = rate;
            ready = true;
        }
        else
            for (int k = 0; k < numParams; ++k)
                if (raw[(std::size_t) k] != current[(std::size_t) k])
                {
                    engine->setControl (k, engineValue (k, raw[(std::size_t) k]));
                    current[(std::size_t) k] = raw[(std::size_t) k];
                }
        lookahead = raw[4] != 0;
        for (int done = 0; done < n;)
        {
            const int m = std::min (512, n - done);
            run (left + done, right + done, m);
            done += m;
        }
    }

private:
    // FL's host gate: after a second of input below 10⁻⁶ the engine is skipped (the input passes)
    void run (float* l, float* r, int m) noexcept
    {
        bool quiet = true;
        for (int i = 0; i < m && quiet; ++i)
            quiet = std::fabs (l[i]) <= 1.0e-6f && std::fabs (r[i]) <= 1.0e-6f;
        if (! quiet)
            silent = tail;
        else
            silent -= m;
        if (-m < silent)
            engine->process (l, r, l, r, m);
        else
            silent = -0x3fffffff;
    }

    std::unique_ptr<Engine> engine;
    bassharm::Engine bass; // round 190mm (dyn)
    bool bassOn = false;
    std::array<int, numParams> current {};
    int rate = 44100, silent = 0, tail = 44100;
    bool ready = false;
    std::atomic<bool> lookahead { true };
};
} // namespace

std::unique_ptr<Effect> make (bool lookahead) { return std::make_unique<LowLifterEffect> (lookahead); }
float bassLevelValue (double db) noexcept { return db <= -71.9 ? 0.0f : (float) std::clamp ((db + 72.0) / 96.0, 1.0e-4, 1.0); }
double bassLevelDb (float v) noexcept { return v <= 0.0f ? -200.0 : (double) std::clamp (v, 0.0f, 1.0f) * 96.0 - 72.0; }
float bassFreqValue (double hz) noexcept { return (float) std::clamp (std::log (std::max (hz, 16.0) / 16.0) / std::log (1000.0 / 16.0), 1.0e-4, 1.0); }
double bassFreqHz (float v) noexcept { return 16.0 * std::pow (1000.0 / 16.0, (double) std::clamp (v, 0.0f, 1.0f)); }
} // namespace lynx::lowlifter
