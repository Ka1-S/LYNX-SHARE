#pragma once

// Round 190ik: a labelled onset bank made by LYNX's own code (additive synthesis and noise, nothing recorded), for the Beats
// transient detector's gate (before / after F-measure, per category). Each case knows exactly where its onsets are:
//  drums    kick / snare / hat patterns at changing levels;
//  drumbass drums over a plucked bass line;
//  plucks   plucked harmonic notes (fast attack, exponential decay);
//  legato   one sustained tone whose pitch steps (5…20 ms transitions) with light vibrato — tonal onsets, no level change;
//  glides   one sustained tone with slow glides and wide vibrato — no onset after the first;
//  lowglide the same in a bass range (70…130 Hz), where the level detector's envelope wobbles;
//  sung     steps, glides, re-articulations (a 40 ms dip) and slow vibrato together;
//  chords   polyphonic plucked chords.
// Deterministic (fixed seeds), 44.1 and 48 kHz.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace onsetbank190ik
{
struct Case
{
    std::string name, category;
    double sampleRate = 44100.0;
    std::vector<float> signal;
    std::vector<double> onsets; // seconds
};

struct Lcg
{
    std::uint32_t s;
    double next() { s = s * 1664525u + 1013904223u; return (double) (s >> 8) / 16777216.0; } // 0…1
    double range (double a, double b) { return a + (b - a) * next(); }
};

constexpr double pi = 3.141592653589793;

// a harmonic tone along a pitch track (Hz per frame) and a level track, with a fixed vowel-like spectral envelope
inline void addTone (std::vector<float>& y, double sr, const std::vector<double>& f0, const std::vector<double>& amp, double brightness = 1.0)
{
    double phase = 0.0;
    const auto n = y.size();
    std::vector<double> acc (n, 0.0);
    std::vector<double> ph (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        phase += 2.0 * pi * f0[i] / sr;
        if (phase > 2.0 * pi * 1e6)
            phase = std::fmod (phase, 2.0 * pi);
        ph[i] = phase;
    }
    for (int k = 1; k <= 40; ++k)
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto f = k * f0[i];
            if (f > 0.45 * sr || amp[i] == 0.0)
                continue;
            const auto taper = std::clamp ((0.45 * sr - f) / (0.1 * sr), 0.0, 1.0); // (no partial pops in at the top)
            const auto formant = 1.0 / (1.0 + std::pow ((f - 800.0) / 500.0, 2.0)) + 0.4 / (1.0 + std::pow ((f - 2400.0) / 700.0, 2.0)) + 0.05;
            acc[i] += amp[i] * taper * std::pow (1.0 / k, brightness) * formant * std::sin (k * ph[i]);
        }
    for (std::size_t i = 0; i < n; ++i)
        y[i] += (float) acc[i];
}

inline void addPluck (std::vector<float>& y, double sr, double at, double hz, double level, double decay, int harmonics = 20)
{
    const auto start = (std::size_t) (at * sr);
    for (std::size_t i = start; i < y.size(); ++i)
    {
        const auto t = (double) (i - start) / sr;
        if (t > decay * 7.0)
            break;
        const auto env = level * std::exp (-t / decay) * std::min (1.0, t / 0.002);
        double v = 0.0;
        for (int k = 1; k <= harmonics && k * hz < 0.45 * sr; ++k)
            v += std::exp (-t * 1.5 * k / decay * 0.1) / k * std::sin (2.0 * pi * k * hz * t);
        y[i] += (float) (env * v);
    }
}

inline void addDrum (std::vector<float>& y, double sr, double at, int kind, double level, Lcg& rnd)
{
    const auto start = (std::size_t) (at * sr);
    double lp = 0.0, prev = 0.0, phase = 0.0;
    for (std::size_t i = start; i < y.size(); ++i)
    {
        const auto t = (double) (i - start) / sr;
        if (t > 0.6)
            break;
        double v = 0.0;
        const auto noise = rnd.range (-1.0, 1.0);
        if (kind == 0) // kick: falling sine
        {
            phase += 2.0 * pi * (50.0 + 90.0 * std::exp (-t / 0.03)) / sr;
            v = std::sin (phase) * std::exp (-t / 0.18) + 0.3 * noise * std::exp (-t / 0.004);
        }
        else if (kind == 1) // snare: tone + noise
        {
            lp += 0.5 * (noise - lp);
            v = 0.5 * std::sin (2.0 * pi * 190.0 * t) * std::exp (-t / 0.06) + 0.8 * lp * std::exp (-t / 0.12);
        }
        else // hat: high-passed noise
        {
            const auto hp = noise - prev;
            prev = noise;
            v = 0.5 * hp * std::exp (-t / 0.035);
        }
        y[i] += (float) (level * v * std::min (1.0, t / 0.0005));
    }
}

inline std::vector<Case> bank()
{
    std::vector<Case> cases;
    for (int variant = 0; variant < 4; ++variant)
    {
        const auto sr = variant % 2 == 0 ? 44100.0 : 48000.0;
        Lcg rnd { 190u + 977u * (std::uint32_t) variant };
        const auto seconds = 6.0;
        const auto n = (std::size_t) (seconds * sr);
        const auto tag = std::to_string (variant);

        { // drums
            Case c { "drums" + tag, "drums", sr, std::vector<float> (n, 0.0f), {} };
            const auto step = 60.0 / rnd.range (90.0, 140.0) / 2.0;
            for (double t = 0.1; t < seconds - 0.6; t += step)
            {
                const auto beat = (int) std::lround ((t - 0.1) / step);
                const auto kind = beat % 4 == 0 ? 0 : beat % 4 == 2 ? 1 : 2;
                if (kind == 2 && rnd.next() < 0.3)
                    continue;
                addDrum (c.signal, sr, t, kind, rnd.range (0.25, 0.8), rnd);
                c.onsets.push_back (t);
            }
            cases.push_back (std::move (c));
        }
        { // drums over a plucked bass line
            Case c { "drumbass" + tag, "drumbass", sr, std::vector<float> (n, 0.0f), {} };
            const auto step = 60.0 / rnd.range (90.0, 130.0) / 2.0;
            for (double t = 0.1; t < seconds - 0.6; t += step)
            {
                const auto beat = (int) std::lround ((t - 0.1) / step);
                const auto hit = beat % 2 == 0, bass = hit || rnd.next() < 0.5;
                if (hit)
                    addDrum (c.signal, sr, t, beat % 4 == 0 ? 0 : 1, rnd.range (0.3, 0.7), rnd);
                if (bass)
                {
                    addPluck (c.signal, sr, t, 55.0 * std::pow (2.0, (double) (int) rnd.range (0.0, 12.0) / 12.0), 0.35, 0.25, 8);
                    c.onsets.push_back (t);
                }
            }
            cases.push_back (std::move (c));
        }
        { // plucks
            Case c { "plucks" + tag, "plucks", sr, std::vector<float> (n, 0.0f), {} };
            for (double t = 0.1; t < seconds - 0.8; t += rnd.range (0.15, 0.5))
            {
                // (rollup 0.1.30: the draws in a fixed order — g++'s, which made the round's bank: as function arguments their
                // order was the compiler's, and clang's bank differed, failing the gate on CI's clang job)
                const auto decay = rnd.range (0.2, 0.6);
                const auto level = rnd.range (0.15, 0.4);
                const auto hz = 220.0 * std::pow (2.0, (double) (int) rnd.range (-7.0, 12.0) / 12.0);
                addPluck (c.signal, sr, t, hz, level, decay);
                c.onsets.push_back (t);
            }
            cases.push_back (std::move (c));
        }
        { // legato steps
            Case c { "legato" + tag, "legato", sr, std::vector<float> (n, 0.0f), { 0.1 } };
            std::vector<double> f0 (n), amp (n, 0.0);
            auto note = rnd.range (-5.0, 5.0);
            double t = 0.1, next = t + rnd.range (0.3, 0.7), from = note, to = note, moveStart = 0.0, moveLen = 0.0;
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto ti = (double) i / sr;
                if (ti >= next && next < seconds - 0.4)
                {
                    from = to;
                    {
                        const auto dir = (from > 5.0 ? -1.0 : from < -7.0 ? 1.0 : rnd.next() < 0.5 ? -1.0 : 1.0); // (rollup 0.1.30: drawn before the step, in one order on every compiler)
                        to = from + dir * (double) (int) rnd.range (1.0, 8.0);
                    }
                    moveStart = ti;
                    moveLen = rnd.range (0.005, 0.020);
                    c.onsets.push_back (ti + 0.5 * moveLen);
                    next = ti + rnd.range (0.3, 0.7);
                }
                const auto u = moveLen > 0.0 ? std::clamp ((ti - moveStart) / moveLen, 0.0, 1.0) : 1.0;
                const auto st = from + (to - from) * u + 0.15 * std::sin (2.0 * pi * 5.5 * ti);
                f0[i] = 220.0 * std::pow (2.0, st / 12.0);
                amp[i] = ti < t ? 0.0 : 0.1 * std::min (1.0, (ti - t) / 0.01) * std::min (1.0, (seconds - ti) / 0.05);
            }
            addTone (c.signal, sr, f0, amp);
            cases.push_back (std::move (c));
        }
        { // glides and vibrato (only the first onset)
            Case c { "glides" + tag, "glides", sr, std::vector<float> (n, 0.0f), { 0.1 } };
            std::vector<double> f0 (n), amp (n, 0.0);
            double from = rnd.range (-4.0, 4.0), to = from, moveStart = 0.0, moveLen = 1.0, next = 0.5;
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto ti = (double) i / sr;
                if (ti >= next && next < seconds - 1.0)
                {
                    from = to;
                    {
                        const auto dir = (from > 2.0 ? -1.0 : from < -2.0 ? 1.0 : rnd.next() < 0.5 ? -1.0 : 1.0); // (rollup 0.1.30: drawn before the step, in one order on every compiler)
                        to = from + dir * rnd.range (2.0, 9.0);
                    }
                    moveStart = ti;
                    moveLen = rnd.range (0.15, 0.5);
                    next = ti + moveLen + rnd.range (0.4, 0.9);
                }
                const auto u = std::clamp ((ti - moveStart) / moveLen, 0.0, 1.0);
                const auto shape = 0.5 - 0.5 * std::cos (pi * u);
                const auto vib = (std::fmod (ti, 2.0) > 1.0 ? 0.5 : 0.2) * std::sin (2.0 * pi * 5.0 * ti);
                f0[i] = 220.0 * std::pow (2.0, (from + (to - from) * shape + vib) / 12.0);
                amp[i] = ti < 0.1 ? 0.0 : 0.1 * std::min (1.0, (ti - 0.1) / 0.01) * std::min (1.0, (seconds - ti) / 0.05);
            }
            addTone (c.signal, sr, f0, amp);
            cases.push_back (std::move (c));
        }
        { // low glides: a bass voice / cello range (70…130 Hz) gliding with vibrato — the level detector's derivative
          // envelope wobbles with the waveform here (only the first onset counts)
            Case c { "lowglide" + tag, "lowglide", sr, std::vector<float> (n, 0.0f), { 0.1 } };
            std::vector<double> f0 (n), amp (n, 0.0);
            double from = rnd.range (-2.0, 2.0), to = from, moveStart = 0.0, moveLen = 1.0, next = 0.5;
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto ti = (double) i / sr;
                if (ti >= next && next < seconds - 1.0)
                {
                    from = to;
                    {
                        const auto dir = (from > 2.0 ? -1.0 : from < -2.0 ? 1.0 : rnd.next() < 0.5 ? -1.0 : 1.0); // (rollup 0.1.30: drawn before the step, in one order on every compiler)
                        to = from + dir * rnd.range (2.0, 7.0);
                    }
                    moveStart = ti;
                    moveLen = rnd.range (0.15, 0.5);
                    next = ti + moveLen + rnd.range (0.4, 0.9);
                }
                const auto u = std::clamp ((ti - moveStart) / moveLen, 0.0, 1.0);
                const auto shape = 0.5 - 0.5 * std::cos (pi * u);
                const auto vib = 0.4 * std::sin (2.0 * pi * 5.0 * ti);
                f0[i] = 98.0 * std::pow (2.0, (from + (to - from) * shape + vib) / 12.0);
                amp[i] = ti < 0.1 ? 0.0 : 0.1 * std::min (1.0, (ti - 0.1) / 0.01) * std::min (1.0, (seconds - ti) / 0.05);
            }
            addTone (c.signal, sr, f0, amp);
            cases.push_back (std::move (c));
        }
        { // sung: steps, glides, re-articulations, vibrato
            Case c { "sung" + tag, "sung", sr, std::vector<float> (n, 0.0f), { 0.1 } };
            std::vector<double> f0 (n), amp (n, 0.0);
            double from = rnd.range (-3.0, 3.0), to = from, moveStart = 0.0, moveLen = 1.0, next = 0.6, dip = -1.0;
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto ti = (double) i / sr;
                if (ti >= next && next < seconds - 0.5)
                {
                    const auto kind = (int) rnd.range (0.0, 3.0);
                    from = to;
                    if (kind == 0) // step
                    {
                        {
                            const auto dir = (from > 3.0 ? -1.0 : from < -3.0 ? 1.0 : rnd.next() < 0.5 ? -1.0 : 1.0); // (rollup 0.1.30: drawn before the step, in one order on every compiler)
                            to = from + dir * (double) (int) rnd.range (1.0, 6.0);
                        }
                        moveStart = ti;
                        moveLen = rnd.range (0.006, 0.02);
                        c.onsets.push_back (ti + 0.5 * moveLen);
                    }
                    else if (kind == 1) // glide
                    {
                        {
                            const auto dir = (from > 3.0 ? -1.0 : from < -3.0 ? 1.0 : rnd.next() < 0.5 ? -1.0 : 1.0); // (rollup 0.1.30: drawn before the step, in one order on every compiler)
                            to = from + dir * rnd.range (2.0, 7.0);
                        }
                        moveStart = ti;
                        moveLen = rnd.range (0.15, 0.35);
                    }
                    else // re-articulation: a 40 ms dip, same pitch
                    {
                        dip = ti;
                        c.onsets.push_back (ti + 0.040);
                    }
                    next = ti + std::max (moveLen, 0.05) + rnd.range (0.35, 0.7);
                }
                const auto u = std::clamp ((ti - moveStart) / moveLen, 0.0, 1.0);
                const auto shape = moveLen < 0.05 ? u : 0.5 - 0.5 * std::cos (pi * u);
                f0[i] = 220.0 * std::pow (2.0, (from + (to - from) * shape + 0.2 * std::sin (2.0 * pi * 5.2 * ti)) / 12.0);
                double a = ti < 0.1 ? 0.0 : 0.1 * std::min (1.0, (ti - 0.1) / 0.01) * std::min (1.0, (seconds - ti) / 0.05);
                if (dip >= 0.0 && ti >= dip && ti < dip + 0.050)
                    a *= ti < dip + 0.030 ? 1.0 - 0.95 * (ti - dip) / 0.030 : 0.05 + 0.95 * (ti - dip - 0.030) / 0.020;
                amp[i] = a;
            }
            addTone (c.signal, sr, f0, amp);
            cases.push_back (std::move (c));
        }
        { // chords
            Case c { "chords" + tag, "chords", sr, std::vector<float> (n, 0.0f), {} };
            for (double t = 0.1; t < seconds - 1.0; t += rnd.range (0.5, 1.1))
            {
                const auto root = (double) (int) rnd.range (-6.0, 6.0);
                for (const auto interval : { 0.0, 4.0, 7.0, 12.0 })
                    addPluck (c.signal, sr, t, 196.0 * std::pow (2.0, (root + interval) / 12.0), 0.12, rnd.range (0.4, 0.9));
                c.onsets.push_back (t);
            }
            cases.push_back (std::move (c));
        }
    }
    return cases;
}

// one-to-one matching within ±tolerance: true positives, false positives, false negatives
struct Score
{
    int tp = 0, fp = 0, fn = 0;
    double f() const { return tp == 0 ? 0.0 : 2.0 * tp / (2.0 * tp + fp + fn); }
};
inline Score score (const std::vector<double>& truth, const std::vector<double>& found, double tolerance = 0.030)
{
    Score s;
    std::vector<bool> used (found.size(), false);
    for (const auto t : truth)
    {
        int best = -1;
        for (std::size_t j = 0; j < found.size(); ++j)
            if (! used[j] && std::abs (found[j] - t) <= tolerance && (best < 0 || std::abs (found[j] - t) < std::abs (found[(std::size_t) best] - t)))
                best = (int) j;
        if (best >= 0)
        {
            used[(std::size_t) best] = true;
            ++s.tp;
        }
        else
            ++s.fn;
    }
    for (const auto u : used)
        s.fp += u ? 0 : 1;
    return s;
}
} // namespace onsetbank190ik
