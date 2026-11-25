// LYNX's own oscillator waves for GMS (core/NativeData.h, round 190rh). GMS names its 29 waves by file; a state keeps the
// names. The plain ones are the textbook waves, the others are LYNX's own idea of what the name says, made from harmonic
// recipes, pulses and waveshapers: nothing of FL's wave files was used for them, and only Saw / Square / Sine / Triangle
// are meant to sound like FL's.

#include "core/NativeData.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>

namespace lynx::nativedata
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int points = 1024, top = 256;
using Cycle = std::vector<double>;

Cycle harmonics (const std::function<double (int)>& amplitude, int highest = top)
{
    Cycle x ((std::size_t) points, 0.0);
    for (int h = 1; h <= highest; ++h)
    {
        const auto a = amplitude (h) * (0.5 + 0.5 * std::cos (pi * h / (highest + 1)));
        if (a != 0.0)
            for (int i = 0; i < points; ++i)
                x[(std::size_t) i] += a * std::sin (2.0 * pi * (double) ((h * i) % points) / points);
    }
    return x;
}
Cycle shape (Cycle x, const std::function<double (double)>& f)
{
    for (auto& v : x)
        v = f (v);
    return x;
}
Cycle function (const std::function<double (double)>& f) // f (phase 0…1)
{
    Cycle x;
    for (int i = 0; i < points; ++i)
        x.push_back (f ((double) i / points));
    return x;
}
double odd (int h) { return (h & 1) != 0 ? 1.0 : 0.0; }
Cycle saw() { return harmonics ([] (int h) { return 2.0 / (pi * h); }); }
Cycle sine() { return harmonics ([] (int h) { return h == 1 ? 1.0 : 0.0; }, 1); }
Cycle pulse (double duty) { return harmonics ([duty] (int h) { return 4.0 / (pi * h) * std::sin (pi * h * duty); }); }
// a resonance of `width` harmonics around harmonic `centre`
double formant (double h, double centre, double width) { return std::exp (-(h - centre) * (h - centre) / (2.0 * width * width)); }

const std::map<std::string, std::function<Cycle()>>& recipes()
{
    static const std::map<std::string, std::function<Cycle()>> r = {
        { "sine", sine },
        { "triangle", [] { return harmonics ([] (int h) { return odd (h) * 8.0 / (pi * pi * h * h) * ((h / 2) % 2 == 0 ? 1.0 : -1.0); }); } },
        { "saw", saw },
        { "square", [] { return harmonics ([] (int h) { return odd (h) * 4.0 / (pi * h); }); } },
        { "saw retro", [] { return harmonics ([] (int h) { return 2.0 / (pi * h) * std::exp (-h / 40.0); }); } },
        { "saw mean", [] { return shape (saw(), [] (double v) { return std::tanh (2.5 * v + 0.4); }); } },
        { "square retro", [] { return harmonics ([] (int h) { return 4.0 / (pi * h) * std::sin (pi * h * 0.45) * std::exp (-h / 50.0); }); } },
        { "square smooth", [] { return harmonics ([] (int h) { return odd (h) * 4.0 / (pi * h) * std::exp (-h / 12.0); }); } },
        { "smooth sub", [] { return harmonics ([] (int h) { return h == 1 ? 1.0 : h == 2 ? 0.25 : h == 3 ? 0.1 : 0.0; }, 3); } },
        { "keys", [] { return harmonics ([] (int h) { return h <= 8 ? std::pow (0.55, h - 1) : 0.0; }, 8); } },
        { "rhodes", [] { return harmonics ([] (int h) { return h == 1 ? 1.0 : h == 2 ? 0.3 : h == 14 ? 0.08 : 0.0; }, 14); } },
        { "vocal", [] { return harmonics ([] (int h) { return (formant (h, 4, 1.5) + 0.6 * formant (h, 9, 2) + 0.4 * formant (h, 1, 1)) / std::sqrt ((double) h); }, 40); } },
        { "throat", [] { return harmonics ([] (int h) { return (formant (h, 2, 1) + 0.7 * formant (h, 6, 1.2)) / std::sqrt ((double) h); }, 40); } },
        { "string", [] { return harmonics ([] (int h) { return (1.0 + 0.5 * std::cos (h * 1.7)) / std::pow ((double) h, 1.2); }); } },
        { "strat", [] { return harmonics ([] (int h) { return std::sin (pi * h * 0.22) / h; }); } },
        { "silk", [] { return harmonics ([] (int h) { return 1.0 / std::pow ((double) h, 1.8); }); } },
        { "liquid", [] { return harmonics ([] (int h) { return (h % 3 == 0 ? 1.6 : 1.0) / ((double) h * h); }); } },
        { "digital", [] { return shape (sine(), [] (double v) { return std::round (v * 4.0) / 4.0; }); } },
        { "dirt", [] { return shape (harmonics ([] (int h) { return h == 1 ? 1.0 : h == 5 ? 0.35 : 0.0; }, 5), [] (double v) { return std::tanh (4.0 * v); }); } },
        { "crunch", [] { return shape (sine(), [] (double v) { return std::clamp (3.0 * v + 0.5, -1.0, 1.0); }); } },
        { "electro", [] { return pulse (0.25); } },
        { "fox", [] { return pulse (0.12); } },
        { "filth", [] { return shape (harmonics ([] (int h) { return (h == 2 ? 2.0 : 1.0) / h; }), [] (double v) { return std::tanh (3.0 * v); }); } },
        { "funky", [] { return harmonics ([] (int h) { return h == 1 ? 1.0 : h == 2 ? 0.8 : h == 4 ? 0.6 : h == 8 ? 0.4 : 0.0; }, 8); } },
        { "grubby", [] { return function ([] (double p) { return (1.0 - 2.0 * p) * std::sin (6.0 * pi * p); }); } },
        { "ogre", [] { return shape (harmonics ([] (int h) { return h == 1 ? 1.0 : h == 3 ? 0.5 : 0.0; }, 3), [] (double v) { return std::clamp (2.0 * v, -1.0, 0.8); }); } },
        { "troll", [] { return shape (harmonics ([] (int h) { return h == 1 ? 1.0 : h == 2 ? 0.7 : h == 7 ? 0.3 : 0.0; }, 7), [] (double v) { return std::tanh (2.0 * v); }); } },
        { "beast", [] { return function ([] (double p) { return (1.0 - p) * std::sin (2.0 * pi * 2.7 * p); }); } },
        { "double spike", [] { return harmonics ([] (int h) { return (1.0 + std::cos (pi * h * 0.6)) / std::sqrt ((double) h); }, 64); } },
    };
    return r;
}
} // namespace

std::vector<float> gmsWave (const std::string& name)
{
    std::string key;
    for (const auto c : name)
        key.push_back ((char) std::tolower ((unsigned char) c));
    const auto& all = recipes();
    const auto it = all.find (key);
    const auto x = it != all.end() ? it->second() : saw(); // (a wave of the user's own that is not here: the plain one)
    double mean = 0.0, peak = 1.0e-9;
    for (const auto v : x)
        mean += v / points;
    for (const auto v : x)
        peak = std::max (peak, std::fabs (v - mean));
    std::vector<float> out;
    for (const auto v : x)
        out.push_back ((float) ((v - mean) / peak * gmsLevel));
    return out;
}
} // namespace lynx::nativedata
