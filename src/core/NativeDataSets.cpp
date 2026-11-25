// LYNX's own data sets for the FL engines it reproduces (core/NativeData.h, round 190rh). Everything here is computed from
// textbook shapes and public literature; docs/analise/REA_rodada190rh.md cites the sources. No constant comes from FL's
// stored data: the only adjustment is one level per set, from rendered audio.

#include "core/NativeData.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>

namespace lynx::nativedata
{
namespace
{
constexpr double pi = 3.14159265358979323846;
}

std::vector<std::vector<float>> bassPulseCycles (double rate, int count)
{
    // The TB-303's square as Open303 models it (R. Schmidt's rosic::MipMappedWaveTable, fillWithSquare303, MIT licence): the
    // saw into a saturating transistor stage, −tanh (G·saw + O) with G = 36.9 dB and O = 4.37. The engine's own falling
    // saw goes in, so the two line up when they are mixed. The edge the shaper gives (the saw's slow side) and the sharp
    // one (the reset) are its own; a coupling high-pass of 10 Hz makes the flat sides sag, more on the low notes.
    constexpr double lowest = 16.351597831287414; // C0
    const double gain = std::pow (10.0, 36.9 / 20.0);
    constexpr double offset = 4.37, coupling = 10.0;
    constexpr int over = 16; // points per sample, averaged under a raised cosine two samples wide
    std::vector<std::vector<float>> out;
    for (int k = 0; k < count; ++k)
    {
        const auto hz = lowest * std::pow (2.0, (double) k / 12.0);
        const auto n = std::max (8, (int) std::lround (rate / hz));
        const auto shape = [&] (double p) {
            p -= std::floor (p);
            return -std::tanh (gain * (1.0 - 2.0 * p) + offset);
        };
        std::vector<double> x ((std::size_t) n);
        for (int i = 0; i < n; ++i)
        {
            double v = 0.0, w = 0.0;
            for (int j = -over + 1; j < over; ++j)
            {
                const auto g = 0.5 + 0.5 * std::cos (pi * (double) j / over);
                v += g * shape (((double) i + (double) j / over) / n);
                w += g;
            }
            x[(std::size_t) i] = v / w;
        }
        // the high-pass in its steady state: run until it repeats
        const auto a = std::exp (-2.0 * pi * coupling / rate);
        const auto turns = 2 + (int) std::ceil (14.0 * rate / (2.0 * pi * coupling) / n);
        std::vector<float> cycle ((std::size_t) n);
        double y = 0.0, last = x.back(), sum = 0.0;
        for (int t = 0; t < turns; ++t)
            for (int i = 0; i < n; ++i)
            {
                y = a * (y + x[(std::size_t) i] - last);
                last = x[(std::size_t) i];
                cycle[(std::size_t) i] = (float) y;
            }
        for (const auto v : cycle)
            sum += v;
        for (auto& v : cycle)
            v = (float) ((v - sum / n) * bassPulseLevel);
        out.push_back (std::move (cycle));
    }
    return out;
}

void fallingSaw (std::vector<float>& amplitude, std::vector<float>& phase)
{
    // ½ − p = Σ sin (2πhp) / (πh): the Fourier series of a falling saw, as cosines (a quarter turn behind)
    amplitude.assign (1024, 0.0f);
    phase.assign (1024, (float) (-0.5 * pi));
    for (int h = 1; h <= 1024; ++h)
        amplitude[(std::size_t) h - 1] = (float) (miniSynthLevel / (pi * h));
}

namespace
{
std::vector<float> makeSawCycle()
{
    // Σ sin (2πhp)·2 / (πh) over the harmonics below a quarter of the table's own, faded by a raised cosine
    constexpr int n = 8192, harmonics = 2048;
    std::vector<double> x ((std::size_t) n, 0.0), sine ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        sine[(std::size_t) i] = std::sin (2.0 * pi * i / n);
    for (int h = 1; h <= harmonics; ++h)
    {
        const auto a = (0.5 + 0.5 * std::cos (pi * h / (harmonics + 1))) * 2.0 / (pi * h);
        for (int i = 0; i < n; ++i)
            x[(std::size_t) i] += a * sine[(std::size_t) (((std::int64_t) h * i) & (n - 1))];
    }
    std::vector<float> out ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        out[(std::size_t) i] = (float) (x[(std::size_t) i] * sawerLevel);
    return out;
}
} // namespace

std::vector<float> sineCycle (int points)
{
    std::vector<float> out ((std::size_t) points);
    for (int i = 0; i < points; ++i)
        out[(std::size_t) i] = (float) (bassDrumLevel * std::sin (2.0 * pi * i / points));
    return out;
}

std::vector<float> drumNoise (const std::string& name)
{
    std::string n;
    for (const auto c : name)
        n.push_back ((char) std::tolower ((unsigned char) c));
    const auto has = [&n] (const char* word) { return n.find (word) != std::string::npos; };
    constexpr double rate = 44100.0;
    const auto length = (std::size_t) (0.8 * rate);
    std::vector<double> x (length);
    std::uint32_t seed = 0x1234567u;
    const auto white = [&seed] {
        seed = seed * 1664525u + 1013904223u; // (Numerical Recipes' generator)
        return (double) (std::int32_t) seed / 2147483648.0;
    };
    if (has ("metal"))
    {
        // six square waves at the TR-808 cymbal oscillators' frequencies (its service notes), high-passed, decaying
        constexpr double hz[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        double last = 0.0, y = 0.0;
        const auto a = std::exp (-2.0 * pi * 3000.0 / rate);
        for (std::size_t i = 0; i < length; ++i)
        {
            const auto t = (double) i / rate;
            double v = 0.0;
            for (const auto f : hz)
                v += std::fmod (t * f, 1.0) < 0.5 ? 1.0 : -1.0;
            y = a * (y + v - last);
            last = v;
            x[i] = y * std::exp (-t / 0.12);
        }
    }
    else if (has ("clap"))
    {
        // noise through a band-pass at 1 kHz under three short bursts 10 ms apart and a tail (the analogue clap circuits')
        double low = 0.0, band = 0.0;
        const auto f = 2.0 * std::sin (pi * 1000.0 / rate);
        for (std::size_t i = 0; i < length; ++i)
        {
            const auto t = (double) i / rate;
            const auto high = white() - low - band;
            band += f * high;
            low += f * band;
            double e = 0.0;
            for (int k = 0; k < 3; ++k)
                if (t >= 0.01 * k)
                    e = std::max (e, std::exp (-(t - 0.01 * k) / 0.004));
            if (t >= 0.03)
                e = std::max (e, 0.8 * std::exp (-(t - 0.03) / 0.06));
            x[i] = band * e;
        }
    }
    else if (has ("crush") || has ("crunch") || has ("bit"))
    {
        double held = 0.0;
        for (std::size_t i = 0; i < length; ++i)
        {
            if (i % 12 == 0) // (3.7 kHz)
                held = std::round (white() * 8.0) / 8.0;
            x[i] = held * std::exp (-(double) i / rate / 0.15);
        }
    }
    else
        for (std::size_t i = 0; i < length; ++i)
            x[i] = white() * std::exp (-(double) i / rate / 0.12);
    double top = 1.0e-9;
    for (const auto v : x)
        top = std::max (top, std::fabs (v));
    std::vector<float> out (length);
    for (std::size_t i = 0; i < length; ++i)
        out[i] = (float) (x[i] / top * 0.9);
    return out;
}

std::vector<float> drumSynthWave (int wave, int points)
{
    constexpr double rate = 22050.0;
    std::vector<double> x ((std::size_t) std::max (points, 1));
    std::uint32_t seed = 0x9e3779b9u + 77u * (std::uint32_t) wave;
    const auto white = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return (double) (std::int32_t) seed / 2147483648.0;
    };
    const auto square = [] (double p) { return p - std::floor (p) < 0.5 ? 1.0 : -1.0; };
    // the TR-808 cymbal circuit's six oscillators and its cowbell's two (service notes; Werner et al., ICMC 2014)
    constexpr double metal[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    double low = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const auto t = (double) i / rate;
        double v = 0.0;
        if (wave == 0 || wave == 2 || wave == 5 || wave == 6)
        {
            const auto shift = wave == 0 ? 1.0 : wave == 2 ? 1.5 : wave == 5 ? 2.1 : 1.8;
            for (const auto f : metal)
                v += square (t * f * shift * (1.0 + 0.03 * wave)) / 6.0;
            v = (wave == 5 ? 0.4 : 0.8) * v + (wave == 5 ? 0.6 : 0.2) * white();
        }
        else if (wave == 1)
            v = 0.5 * square (t * 540.0) + 0.5 * square (t * 800.0);
        else if (wave == 3 || wave == 4)
        {
            const auto n = white();
            low += 0.5 * (n - low);
            v = (n - low) * std::exp (-t / (wave == 3 ? 0.06 : 0.5));
        }
        else
        {
            const auto n = white();
            const auto a = 0.1 + 0.07 * (wave - 7);
            low += a * (n - low);
            v = (wave % 3 == 0 ? low * 2.0 : wave % 3 == 1 ? n - low : n) * std::exp (-t / (0.03 + 0.03 * (wave - 7)));
        }
        x[i] = v;
    }
    double top = 1.0e-9;
    for (const auto v : x)
        top = std::max (top, std::fabs (v));
    std::vector<float> out (x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        out[i] = (float) (0.8 * x[i] / top);
    return out;
}

std::vector<BassZone> bassString()
{
    constexpr double rate = 16000.0, pluckAt = 0.17;
    std::vector<BassZone> zones;
    for (const int semitone : { 11, 16, 23, 28, 35, 40 })
    {
        BassZone z;
        z.semitone = semitone;
        const auto hz = 16.351597831287414 * std::pow (2.0, semitone / 12.0);
        const auto cycles = std::floor (0.7 * hz);
        const auto n = (int) std::lround (cycles * rate / hz);
        const auto top = std::min (40, (int) (0.45 * rate / hz));
        const auto partial = [&] (int h, double t) { // amplitude now: from the pluck to what keeps sounding
            const auto pluck = std::sin (pi * h * pluckAt) / h, rest = std::sin (pi * h * pluckAt) / ((double) h * h) * std::exp (-h / 9.0);
            return rest + (pluck - rest) * std::exp (-t * (4.0 + 1.6 * h));
        };
        z.attack.resize ((std::size_t) n + 1);
        for (int i = 0; i <= n; ++i)
        {
            const auto t = (double) i / rate;
            double v = 0.0;
            for (int h = 1; h <= top; ++h)
                v += partial (h, t) * std::sin (2.0 * pi * h * cycles * i / n);
            z.attack[(std::size_t) i] = (float) (bassStringLevel * v * (1.0 - std::exp (-t / 0.0015)));
        }
        z.loop.resize (2048);
        for (int i = 0; i < 2048; ++i)
        {
            double v = 0.0;
            for (int h = 1; h <= top; ++h)
                v += partial (h, 0.7) * std::sin (2.0 * pi * h * i / 2048.0);
            z.loop[(std::size_t) i] = (float) (bassStringLevel * v);
        }
        zones.push_back (std::move (z));
    }
    return zones;
}

std::vector<std::int16_t> guitarPick (int index)
{
    // a burst of noise through two one-pole low-passes: harder picks are shorter and brighter
    const auto hardness = (double) (index % 7) / 6.0, length = 0.012 + 0.010 * (double) (index / 7);
    const auto n = (int) (44100.0 * length * 2.5);
    std::vector<double> x ((std::size_t) n);
    std::uint32_t seed = 0x51ed27u + 991u * (std::uint32_t) index;
    double a = 0.0, b = 0.0, top = 1.0e-9;
    for (int i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const auto c = 0.12 + 0.7 * hardness;
        a += c * ((double) (std::int32_t) seed / 2147483648.0 - a);
        b += c * (a - b);
        const auto t = (double) i / 44100.0;
        x[(std::size_t) i] = b * std::exp (-t / length) * (1.0 - std::exp (-t / 0.0004));
        top = std::max (top, std::fabs (x[(std::size_t) i]));
    }
    std::vector<std::int16_t> out;
    for (const auto v : x)
        out.push_back ((std::int16_t) std::lround (v / top * 24000.0));
    return out;
}

std::vector<int> guitarChord (int root)
{
    std::vector<int> c;
    for (const int interval : { 0, 7, 12, 16, 19, 24 })
        c.push_back (root + interval);
    return c;
}

std::vector<float> glideLengths (bool dynamic)
{
    // the engine counts twelfths of a beat: sixteenth … whole note; the dynamic mode's table is a speed
    if (dynamic)
        return { 8.0f, 4.0f, 2.5f, 2.0f, 1.3f, 1.0f, 0.5f };
    return { 3.0f, 6.0f, 9.0f, 12.0f, 18.0f, 24.0f, 48.0f };
}

std::vector<float> triangleCycle (int points)
{
    std::vector<float> out ((std::size_t) points);
    for (int i = 0; i < points; ++i)
    {
        const auto p = (double) i / points;
        out[(std::size_t) i] = (float) (p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
    }
    return out;
}

std::vector<std::int16_t> whiteNoise16 (int points)
{
    std::vector<std::int16_t> out ((std::size_t) points);
    std::uint32_t seed = 0x13579bdu;
    for (auto& v : out)
    {
        seed = seed * 1664525u + 1013904223u;
        v = (std::int16_t) std::lround ((double) (std::int32_t) seed / 2147483648.0 * 32767.0 * 0.35 * keplerNoiseLevel);
    }
    return out;
}

std::vector<float> noiseLayer (const std::string& name)
{
    std::string n;
    for (const auto c : name)
        n.push_back ((char) std::tolower ((unsigned char) c));
    for (const auto* word : { "hit", "click", "clap", "pop", "pluck", "metal", "bell", "conga", "castanet", "drop" })
        if (n.find (word) != std::string::npos)
            return drumNoise (name);
    const auto brown = n.find ("brown") != std::string::npos || n.find ("low") != std::string::npos;
    std::vector<float> out (88200);
    std::uint32_t seed = 0x2468aceu;
    double low = 0.0;
    for (auto& v : out)
    {
        seed = seed * 1664525u + 1013904223u;
        const auto white = (double) (std::int32_t) seed / 2147483648.0;
        low += 0.02 * (white - low);
        v = (float) (brown ? 5.0 * low : 0.5 * white);
    }
    return out;
}

std::vector<float> sawCycle()
{
    static const auto cycle = makeSawCycle();
    return cycle;
}
} // namespace lynx::nativedata
