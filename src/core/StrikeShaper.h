#pragma once

// Strike Shaper (round trans189d, issue #94): LYNX's level-independent transient designer, in the role of the classic
// two-knob "differential envelope" transient designer. Written from the public description of that method (no third-
// party code): two envelopes of the same signal are compared, and the gain follows their RATIO, so the result does
// not depend on the input level and there is no threshold.
//
//   env  : the signal's envelope — the peak of |x| (both channels when linked, through the optional detector
//          high-pass), held for 15 ms so a 35 Hz kick's cycles don't ripple it, then falling with a 40 ms time constant,
//          lightly smoothed (0.3 ms).
//   ATTACK : a slow-rising copy of env (12 ms rise, falls with env at once). a = 1 − slow / env is 0 on a steady or
//          falling signal and jumps toward 1 at an onset (1 − 10^(−Δ/20) for a step of Δ dB), decaying as the slow
//          copy catches up. Attack gain = Attack dB × a (−15 … +15 dB).
//   SUSTAIN: a slow-falling copy of env (rises with env at once, 300 ms fall). s = 1 − env / slow is 0 while the sound
//          holds and grows toward 1 as a tail decays. Sustain gain = Sustain dB × s (−24 … +24 dB).
//   Both gains add in dB and are applied to the (undelayed) signal: no look-ahead, no latency. A tiny level floor
//   (−120 dBFS) keeps digital silence quiet; everything above it is level independent. A 3 % dead zone on a and s
//   keeps the held peak's ripple on steady tones from moving the gain.
//   Output −20 … +6 dB, Mix (the dry signal blended back), Guard (a soft clipper with a 0 dBFS ceiling) and Link (one
//   gain for both channels, so the image doesn't move). Attack 0 and Sustain 0 at Mix 100 % and Output 0 dB pass the
//   signal bit for bit.
//
// Header-only, no allocation: the Effect wrapper lives in core/MixSuite.cpp (core/StrikeSuite.inc).

#include <algorithm>
#include <array>
#include <cmath>

namespace lynx::strike
{
enum Param { attack, sustain, output, guard, mix, link, detectHp, count };

// The knob laws follow the two-knob panel's printed scales: the stops sit at equal angles, finer near 0 dB
//   Attack  -15 -13 -10 -6 -3 0 3 6 10 13 15 dB, Sustain -24 -18 -12 -6 -3 0 3 6 12 18 24 dB,
//   Output  -20 -15 -10 -6 -3 0 1 2 3 4.5 6 dB (0 dB at the top, like the attack and sustain knobs)
namespace law
{
constexpr double attackStops[] { 0.0, 3.0, 6.0, 10.0, 13.0, 15.0 };
constexpr double sustainStops[] { 0.0, 3.0, 6.0, 12.0, 18.0, 24.0 };
constexpr double outputLow[] { 0.0, 3.0, 6.0, 10.0, 15.0, 20.0 };
constexpr double outputHigh[] { 0.0, 1.0, 2.0, 3.0, 4.5, 6.0 };
// v 0..1 (0.5 = 0 dB) through five equal steps each side
inline double fromStops (float v, const double* below, const double* above) noexcept
{
    const auto x = ((double) std::clamp (v, 0.0f, 1.0f) - 0.5) * 10.0; // -5 .. 5
    const auto* t = x < 0.0 ? below : above;
    const auto ax = std::abs (x);
    const auto i = std::min (4, (int) ax);
    const auto db = t[i] + (t[i + 1] - t[i]) * (ax - (double) i);
    return x < 0.0 ? -db : db;
}
inline float toStops (double db, const double* below, const double* above) noexcept
{
    const auto* t = db < 0.0 ? below : above;
    const auto a = std::min (std::abs (db), t[5]);
    int i = 0;
    while (i < 4 && a > t[i + 1]) ++i;
    const auto x = (double) i + (a - t[i]) / (t[i + 1] - t[i]);
    return (float) std::clamp (0.5 + (db < 0.0 ? -x : x) / 10.0, 0.0, 1.0);
}
} // namespace law
inline double attackDb (float v) noexcept { return law::fromStops (v, law::attackStops, law::attackStops); }     // ±15 dB
inline double sustainDb (float v) noexcept { return law::fromStops (v, law::sustainStops, law::sustainStops); }  // ±24 dB
inline double outputDb (float v) noexcept { return law::fromStops (v, law::outputLow, law::outputHigh); }        // -20 .. +6 dB
inline float attackValue (double db) noexcept { return law::toStops (db, law::attackStops, law::attackStops); }
inline float sustainValue (double db) noexcept { return law::toStops (db, law::sustainStops, law::sustainStops); }
inline float outputValue (double db) noexcept { return law::toStops (db, law::outputLow, law::outputHigh); }
// the detector's high-pass: 0 = off, else 20 Hz … 1 kHz (log)
inline double detectHpHz (float v) noexcept { return v <= 0.0f ? 0.0 : 20.0 * std::pow (50.0, (double) std::clamp (v, 0.0f, 1.0f)); }
inline float detectHpValueOf (double hz) noexcept { return hz <= 0.0 ? 0.0f : (float) std::clamp (std::log (hz / 20.0) / std::log (50.0), 0.0, 1.0); }
inline bool on (float v) noexcept { return v >= 0.5f; }

// the time constants (seconds)
constexpr double holdS = 0.015, fallS = 0.040, smoothS = 0.0003, attackRiseS = 0.012, sustainFallS = 0.300;
constexpr double floorLevel = 1.0e-6; // −120 dBFS
// a small dead zone on both measures: the held peak's ripple on steady tones (a fraction of a dB) moves nothing
constexpr double deadZone = 0.03;

// Guard: unity below 0.7 (−3.1 dBFS), then a tanh knee that never passes ±1
inline float guardClip (float x) noexcept
{
    constexpr float t = 0.7f, room = 1.0f - t;
    const auto a = std::abs (x);
    if (a <= t) return x;
    const auto y = t + room * std::tanh ((a - t) / room);
    return x < 0.0f ? -y : y;
}

class Shaper
{
public:
    void prepare (double rate) noexcept
    {
        sr = rate > 0.0 ? rate : 48000.0;
        holdN = (int) std::lround (holdS * sr);
        fall = std::exp (-1.0 / (fallS * sr));
        smooth = 1.0 - std::exp (-1.0 / (smoothS * sr));
        riseA = 1.0 - std::exp (-1.0 / (attackRiseS * sr));
        fallS_ = std::exp (-1.0 / (sustainFallS * sr));
        gainSmooth = 1.0 - std::exp (-1.0 / (0.0002 * sr));
        hpHz = -1.0;
        reset();
    }
    void reset() noexcept
    {
        for (auto& c : ch)
            c = Channel {};
    }

    // last block's extremes, for the editor's lamps (dB, signed)
    double lastAttackDb = 0.0, lastSustainDb = 0.0;

    void process (float* l, float* r, int n, const float* p) noexcept
    {
        const auto aDb = attackDb (p[attack]), sDb = sustainDb (p[sustain]);
        const auto outGain = (float) std::pow (10.0, outputDb (p[output]) / 20.0);
        const auto wet = std::clamp (p[mix], 0.0f, 1.0f);
        const bool linked = on (p[link]), clip = on (p[guard]);
        designHp (detectHpHz (p[detectHp]));
        lastAttackDb = lastSustainDb = 0.0;
        // the neutral setting passes the signal untouched (detectors keep running so a change starts clean)
        const bool neutral = aDb == 0.0 && sDb == 0.0;
        double peakA = 0.0, peakS = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto dl = l[i], dr = r[i];
            const auto xl = std::abs (hpOn ? ch[0].hp.run (dl, hpc) : dl);
            const auto xr = std::abs (hpOn ? ch[1].hp.run (dr, hpc) : dr);
            double gl, gr;
            if (linked)
            {
                gl = gr = detect (ch[0], std::max (xl, xr), aDb, sDb, peakA, peakS);
                ch[1].copyEnv (ch[0]);
            }
            else
            {
                gl = detect (ch[0], xl, aDb, sDb, peakA, peakS);
                gr = detect (ch[1], xr, aDb, sDb, peakA, peakS);
            }
            if (neutral) continue;
            auto ol = (float) (dl * gl), orr = (float) (dr * gr);
            ol = dl + wet * (ol - dl);
            orr = dr + wet * (orr - dr);
            l[i] = ol;
            r[i] = orr;
        }
        lastAttackDb = peakA;
        lastSustainDb = peakS;
        if (outGain != 1.0f)
            for (int i = 0; i < n; ++i)
            {
                l[i] *= outGain;
                r[i] *= outGain;
            }
        if (clip)
            for (int i = 0; i < n; ++i)
            {
                l[i] = guardClip (l[i]);
                r[i] = guardClip (r[i]);
            }
    }

private:
    struct Hp // a 2nd-order high-pass (transposed direct form II), for the detector only
    {
        double z1 = 0.0, z2 = 0.0;
        double run (double x, const std::array<double, 5>& c) noexcept
        {
            const auto y = c[0] * x + z1;
            z1 = c[1] * x - c[3] * y + z2;
            z2 = c[2] * x - c[4] * y;
            return y;
        }
    };
    struct Channel
    {
        double peak = 0.0, env = 0.0, slowA = 0.0, slowS = 0.0, gainDb = 0.0;
        int hold = 0;
        Hp hp;
        void copyEnv (const Channel& o) noexcept
        {
            peak = o.peak; env = o.env; slowA = o.slowA; slowS = o.slowS; gainDb = o.gainDb; hold = o.hold;
        }
    };

    double detect (Channel& c, double x, double aDb, double sDb, double& peakA, double& peakS) noexcept
    {
        // the held peak, falling after the hold, then smoothed
        if (x >= c.peak)
        {
            c.peak = x;
            c.hold = holdN;
        }
        else if (c.hold > 0)
            --c.hold;
        else
            c.peak = std::max (x, c.peak * fall);
        c.env += (c.peak - c.env) * smooth;
        const auto e = c.env;
        // attack: a slow-rising copy (down with env at once)
        c.slowA = e > c.slowA ? c.slowA + (e - c.slowA) * riseA : e;
        // sustain: a slow-falling copy (up with env at once)
        c.slowS = e > c.slowS ? e : std::max (e, c.slowS * fallS_);
        const auto a = std::max (0.0, 1.0 - (c.slowA + floorLevel) / (e + floorLevel) - deadZone) / (1.0 - deadZone);
        const auto s = std::max (0.0, 1.0 - (e + floorLevel) / (c.slowS + floorLevel) - deadZone) / (1.0 - deadZone);
        const auto ga = aDb * a, gs = sDb * s;
        if (std::abs (ga) > std::abs (peakA)) peakA = ga;
        if (std::abs (gs) > std::abs (peakS)) peakS = gs;
        // the gain in dB, very lightly smoothed (0.2 ms) so a full-scale step can't click
        c.gainDb += (ga + gs - c.gainDb) * gainSmooth;
        if (std::abs (c.gainDb) < 1.0e-12) c.gainDb = 0.0; // no denormals
        return std::exp (c.gainDb * 0.11512925464970229); // 10^(dB / 20)
    }

    void designHp (double hz) noexcept
    {
        if (hz == hpHz) return;
        hpHz = hz;
        hpOn = hz > 0.0 && hz < sr * 0.45;
        if (! hpOn) return;
        constexpr double q = 0.7071067811865476;
        const auto w = 2.0 * 3.141592653589793 * hz / sr, cw = std::cos (w), al = std::sin (w) / (2.0 * q);
        const auto a0 = 1.0 + al;
        hpc = { (1.0 + cw) / 2.0 / a0, -(1.0 + cw) / a0, (1.0 + cw) / 2.0 / a0, -2.0 * cw / a0, (1.0 - al) / a0 };
    }

    double sr = 48000.0, fall = 0.0, smooth = 1.0, riseA = 1.0, fallS_ = 0.0, gainSmooth = 1.0, hpHz = -1.0;
    int holdN = 0;
    bool hpOn = false;
    std::array<double, 5> hpc {};
    std::array<Channel, 2> ch {};
};
} // namespace lynx::strike
