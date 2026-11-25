// LYNX's own Harmor bank (round 190rh). Designed for LYNX and written from the documented layout of the plugin's state
// (REA_rodada11 §4.1, REA_rodada34 §1: parameters by index, 43 articulators, 26 shaping curves); no FL preset was read for it.
// Harmor is additive and needs no FL data: the presets sound the same with and without FL.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

#include "core/Harmor.h"
#include "core/LynxBanks.h"

namespace lynx::banks
{
namespace
{
using Bytes = std::vector<std::uint8_t>;
using Points = std::vector<std::pair<double, double>>; // step from the point before, value

struct Envelope
{
    bool on = false;
    double attack = 0.0, decay = 0.0, sustain = 1.0, release = 0.1; // seconds, 0…1, seconds
};

struct Part
{
    bool on = true;
    std::array<std::int32_t, 386> p {};
    std::array<std::int32_t, 19> combo {}; // the integers of the 0x4B block
    Envelope volume, filter;               // articulators 1 and 4 (filter 1's envelope: p56 is its amount)
    std::array<Points, 26> curves;

    Part()
    {
        const std::pair<int, int> neutral[] = { { 9, 1024 }, { 11, 1000 }, { 12, 1000 }, { 18, 500 }, { 20, 1000 }, { 28, 500 }, { 29, 256 }, { 34, 1 },
                                                { 37, 128 }, { 38, 128 }, { 40, 10000 }, { 41, 1 }, { 42, 10000 }, { 43, 1 }, { 44, 6000 }, { 46, 500 },
                                                { 49, -200 }, { 52, 8192 }, { 54, 250 }, { 59, 250 }, { 64, 8192 }, { 66, 250 }, { 71, 250 }, { 75, 1000 },
                                                { 78, 5000 }, { 89, 1000 }, { 92, 1000 }, { 94, 1000 } };
        for (const auto& [i, v] : neutral)
            p[(std::size_t) i] = v;
        for (int a = 0; a < 31; ++a) // the knobs of articulators 0…30: the envelope's four, the LFO's four
            for (int k = 0; k < 8; ++k)
                p[(std::size_t) (95 + 8 * a + k)] = k == 2 ? 0 : k < 4 ? 128 : k == 4 ? 40898 : 0;
        combo[2] = 100;             // amplitude glide
        combo[7] = 1;               // harmonics in a row (not octaves)
        combo[10] = 200;            // phaser
        combo[15] = combo[16] = 100; // resonance shapes
        const auto flat = [] (double y) { return Points { { 0.0, y }, { 1.0, y } }; };
        const double level[26] = { 0.25, 0.5, 0.25, 0.25, 0.5, 0, 0.5, 0.5, 0.5, 0.5, 0, 0, 0.5, 0.5, 0, 0, 0.5, 0.25, 1, 1, 1, 1, 1, 1, 1, 1 };
        for (std::size_t i = 0; i < 26; ++i)
            curves[i] = flat (level[i]);
        curves[5] = { { 0.0, 1.0 }, { 0.4, 0.0 }, { 0.6, 0.0 } }; // the first harmonics are the protected ones
        curves[7] = { { 0.0, 0.5 }, { 1.0, 1.0 } };
        curves[10] = curves[11] = { { 0.0, 1.0 }, { 1.0, 0.0 } };
        curves[14] = { { 0.0, 1.0 }, { 1.0, 0.6 } };               // a pluck: high harmonics die first
        curves[15] = { { 0.0, 0.0 }, { 0.5, 1.0 }, { 0.5, 0.0 } };
    }

    // level of harmonic h (1 = its natural 1/h) of timbre t, the first `count` of them; what follows is `rest`
    void timbre (int t, int count, const std::function<double (int)>& level, double rest)
    {
        Points c;
        const auto y = [] (double v) { return 0.25 * std::sqrt (std::clamp (v, 0.0, 16.0)); };
        for (int h = 1; h <= count; ++h)
            c.emplace_back (h == 1 ? 0.0 : 1.0 / 515.0, y (level (h)));
        c.emplace_back (1.0 / 515.0, y (rest));
        c.emplace_back ((515.0 - count) / 515.0, y (rest));
        curves[(std::size_t) (2 + t)] = c;
    }
};

struct Patch
{
    Part a, b;
    std::array<std::int32_t, 53> g {};
    Patch()
    {
        const std::pair<int, int> neutral[] = { { 0, 256 }, { 1, 256 }, { 2, 256 }, { 10, 500 }, { 11, 1000 }, { 12, 1000 }, { 15, 1000 }, { 20, 1000 },
                                                { 21, 1100 }, { 24, 1050 }, { 26, 50 }, { 27, 64 }, { 28, 440 }, { 29, 800 }, { 30, 0 }, { 31, 400 },
                                                { 33, 400 }, { 34, 300 }, { 35, 64 }, { 36, 64 }, { 37, 64 }, { 38, 500 }, { 41, 400 }, { 42, 144 },
                                                { 44, 600 }, { 47, 1050 } };
        for (const auto& [i, v] : neutral)
            g[(std::size_t) i] = v;
        b.on = false;
    }
};

struct Writer
{
    Bytes out;
    template <typename T>
    void put (T v)
    {
        std::uint8_t raw[sizeof (T)];
        std::memcpy (raw, &v, sizeof (T));
        out.insert (out.end(), raw, raw + sizeof (T));
    }
    void i (std::int32_t v) { put (v); }
    // header: flags, on, decay point, loop point, sustain point
    void curve (int kind, const Points& points, std::array<std::int32_t, 5> header)
    {
        i (kind);
        i (3);
        i ((std::int32_t) points.size());
        for (const auto& [dx, y] : points)
        {
            put (dx);
            put (y);
            put (0.0f); // tension
            i (0);      // plain segment, no arpeggio mark
        }
        for (const auto h : header)
            i (h);
    }
    void articulator (int k, const Envelope& e)
    {
        // the neutral value of a map: 1 where articulators multiply, the middle where they add (REA_rodada34 §1.12)
        static const int adding[] = { 0, 4, 5, 12, 13, 15, 17, 18, 24, 27, 29, 33, 36, 40 };
        const auto neutral = std::find (std::begin (adding), std::end (adding), k) != std::end (adding) ? 0.5 : 1.0;
        i (3);
        i (0);
        put (1.0f);
        put (1.0f);
        if (e.on)
        {
            const auto bottom = neutral == 0.5 ? 0.5 : 0.0;
            const auto at = [bottom] (double v) { return bottom + (1.0 - bottom) * v; };
            // a step of 1 lasts half a second
            curve (1, { { 0.0, at (e.attack > 0.0 ? 0.0 : 1.0) }, { 2.0 * e.attack, at (1.0) }, { 2.0 * e.decay, at (e.sustain) }, { 2.0 * e.release, at (0.0) } },
                   { 0, 1, 1, -1, 2 });
        }
        else
            curve (1, { { 0.0, neutral == 0.5 ? 0.5 : 1.0 } }, { 0, 0, -1, -1, -1 });
        for (const int v : { 128, 128, 0, 128 })
            i (v);
        curve (2, { { 0.0, 0.75 } }, { 0, 0, -1, -1, -1 });
        for (const int v : { 40898, 0, 0, 0, 0 })
            i (v);
        for (int m = 0; m < 9; ++m)
            curve (3, { { 0.0, neutral }, { 1.0, neutral } }, { 0, -1, -1, -1, -1 });
    }
    void part (const Part& part)
    {
        const auto sizeAt = out.size();
        i (0);
        for (const auto v : part.p)
            i (v);
        for (int m = 0; m < 12; ++m)
            i (m); // the modules in their natural order
        Bytes combos (0x4b, 0);
        std::memcpy (combos.data(), part.combo.data(), 4 * 17);
        const float eqMix = 1.0f;
        std::memcpy (combos.data() + 0x30, &eqMix, 4);
        combos[0x44] = part.on ? 1 : 0;
        out.insert (out.end(), combos.begin(), combos.end());
        for (int k = 0; k < 43; ++k)
            articulator (k, k == 1 ? part.volume : k == 4 ? part.filter : Envelope {});
        for (const auto& c : part.curves)
            curve (3, c, { 0, -1, -1, -1, -1 });
        // an empty image (REA_rodada75 §1.1): version, mode, loop mode, gain, loop start / end / crossfade, columns, rows, three
        // planes of no pixels; then no file name
        for (int v = 0; v < 3; ++v)
            i (0);
        put (0.0f);
        for (const double v : { -1.0, -1.0, 0.0 })
            put (v);
        for (int v = 0; v < 2 + 3; ++v)
            i (0);
        out.push_back (0);
        const auto size = (std::int32_t) (out.size() - sizeAt - 4);
        std::memcpy (out.data() + sizeAt, &size, 4);
    }
};

Bytes write (const Patch& patch)
{
    Writer w;
    w.i (0x130003);
    w.i (7); // part A, part B, the global block
    w.part (patch.a);
    w.part (patch.b);
    for (const auto v : patch.g)
        w.i (v);
    for (int v = 0; v < 6; ++v)
        w.i (v); // the effects in their natural order
    Bytes rest (0x3d, 0);
    const auto set = [&rest] (int at, std::int32_t v) { std::memcpy (rest.data() + at, &v, 4); };
    set (24, 200); // control rate: 2 ms
    rest[0x39] = 1; // smoothed X / Y / Z
    w.out.insert (w.out.end(), rest.begin(), rest.end());
    w.out.push_back (0); // no text
    w.out.push_back (1);
    return w.out;
}

Preset make (const char* name, const char* category, const std::function<void (Patch&)>& design, std::vector<std::string> tags = {})
{
    Patch patch;
    design (patch);
    Preset out;
    out.name = name;
    out.category = category;
    out.tags = std::move (tags);
    out.apply = [state = write (patch)] (ChannelDef& c) {
        c.pluginState = state;
        c.modelParams.clear(); // (no compressor preset)
        c.samplePath.clear();
    };
    return out;
}

void volume (Part& p, double a, double d, double s, double r) { p.volume = { true, a, d, s, r }; }
// filter 1 closed to `knob` (0…1), opened by its envelope by `amount` (0…1)
void lowpass (Part& p, double knob, double amount, double resonance, double a, double d, double s, double r)
{
    p.p[52] = (int) std::lround (8192.0 * knob);
    p.p[56] = (int) std::lround (1000.0 * amount);
    p.p[57] = (int) std::lround (1000.0 * resonance);
    p.filter = { amount > 0.0, a, d, s, r };
}
void unison (Part& p, int voices, int detune, int pan = 128)
{
    p.p[34] = voices;
    p.p[35] = 1;
    p.p[37] = pan;
    p.p[38] = detune;
    p.p[0] = 0;
    p.p[1] = 1000; // random start
}
void chorus (Patch& p) { p.g[32] = 3; }
void reverb (Patch& p, int wet) { p.g[22] = 1, p.g[31] = wet; }
void delay (Patch& p) { p.g[39] = 1, p.g[40] = 2, p.g[44] = 500; }
double bell (double h, double at, double width) { return std::exp (-0.5 * (h - at) * (h - at) / (width * width)); }
} // namespace

std::vector<Preset> harmorBank()
{
    const auto odd = [] (int h) { return h % 2 != 0 ? 1.0 : 0.0; };
    return {
        make ("Plain Saw", "Init", [] (Patch& p) { volume (p.a, 0.002, 0.2, 1.0, 0.1); p.g[11] = 357; }),
        make ("Soft Square", "Init", [odd] (Patch& p) { p.a.timbre (0, 64, odd, 0.0); volume (p.a, 0.002, 0.2, 1.0, 0.1); p.g[11] = 471; }),
        make ("Sub Weight", "Bass", [] (Patch& p) { p.a.timbre (0, 12, [] (int h) { return 1.0 / h; }, 0.0); p.a.p[5] = 600; volume (p.a, 0.003, 0.4, 0.8, 0.08); p.g[11] = 751; }, { "sub" }),
        make ("Filter Bass", "Bass", [] (Patch& p) { lowpass (p.a, 0.22, 0.35, 0.25, 0.0, 0.35, 0.15, 0.2); volume (p.a, 0.002, 0.5, 0.8, 0.08); p.g[11] = 793; }),
        make ("Rubber Bass", "Bass", [odd] (Patch& p) { p.a.timbre (0, 64, odd, 0.0); lowpass (p.a, 0.2, 0.3, 0.2, 0.0, 0.18, 0.0, 0.1); volume (p.a, 0.002, 0.3, 0.7, 0.06); p.g[11] = 793; }, { "short" }),
        make ("Wide Reese", "Bass", [] (Patch& p) { unison (p.a, 5, 120, 160); lowpass (p.a, 0.45, 0.0, 0.1, 0, 0, 0, 0); volume (p.a, 0.01, 0.3, 1.0, 0.15); p.a.p[13] = 400; p.g[11] = 773; }, { "detuned" }),
        make ("Formant Bass", "Bass", [] (Patch& p) { p.a.timbre (0, 24, [] (int h) { return 0.4 + 3.0 * bell (h, 6, 1.5) + 2.0 * bell (h, 14, 2); }, 0.0); volume (p.a, 0.003, 0.4, 0.8, 0.1); p.g[11] = 720; }),
        make ("Straight Lead", "Lead", [] (Patch& p) { unison (p.a, 3, 60); volume (p.a, 0.004, 0.3, 0.9, 0.12); p.a.p[45] = 60; delay (p); p.g[11] = 471; }),
        make ("Seven Saws", "Lead", [] (Patch& p) { unison (p.a, 7, 150, 192); volume (p.a, 0.005, 0.4, 0.9, 0.2); reverb (p, 350); p.g[11] = 533; }, { "supersaw" }),
        make ("Hollow Lead", "Lead", [odd] (Patch& p) { p.a.timbre (0, 96, odd, 0.0); p.a.p[45] = 80; volume (p.a, 0.01, 0.4, 0.85, 0.15); p.g[11] = 576; }),
        make ("Stretched Lead", "Lead", [] (Patch& p) { p.a.p[26] = 60; unison (p.a, 2, 40); volume (p.a, 0.004, 0.3, 0.9, 0.15); p.g[11] = 468; }, { "prism" }),
        make ("Pluck Saw", "Pluck", [] (Patch& p) { p.a.p[75] = 620; volume (p.a, 0.001, 1.2, 0.0, 0.4); p.g[11] = 601; }),
        make ("Pluck Stack", "Pluck", [] (Patch& p) { p.a.p[75] = 560; unison (p.a, 5, 110, 180); volume (p.a, 0.001, 1.0, 0.0, 0.3); delay (p); p.g[11] = 549; }),
        make ("Glass Pluck", "Pluck", [] (Patch& p) { p.a.timbre (0, 32, [] (int h) { return h == 1 ? 1.0 : h % 5 == 0 ? 2.5 : 0.1; }, 0.0); volume (p.a, 0.001, 0.8, 0.0, 0.5); reverb (p, 450); p.g[11] = 716; }),
        make ("Drawbars", "Keys", [] (Patch& p) { p.a.timbre (0, 16, [] (int h) { return h == 1 || h == 2 || h == 3 || h == 4 || h == 6 || h == 8 ? 0.7 * h : 0.0; }, 0.0); p.a.p[5] = 500; volume (p.a, 0.004, 0.1, 1.0, 0.05); chorus (p); p.g[11] = 190; }, { "organ" }),
        make ("Soft Keys", "Keys", [] (Patch& p) { p.a.timbre (0, 12, [] (int h) { return 1.0 / (h * h) + (h == 7 ? 0.5 : 0.0); }, 0.0); volume (p.a, 0.002, 1.6, 0.1, 0.4); p.g[11] = 793; }),
        make ("Tine Bell", "Keys", [] (Patch& p) { p.a.p[42] = 14100; p.a.timbre (0, 12, [] (int h) { return h == 1 || h == 3 || h == 6 ? 1.0 : 0.05; }, 0.0); volume (p.a, 0.001, 2.0, 0.0, 1.5); reverb (p, 400); p.g[11] = 722; }, { "inharmonic" }),
        make ("Slow Saws", "Pad", [] (Patch& p) { unison (p.a, 7, 110, 192); lowpass (p.a, 0.5, 0.15, 0.05, 1.5, 2.0, 0.6, 1.5); volume (p.a, 0.8, 1.0, 1.0, 1.2); reverb (p, 500); p.g[11] = 791; }),
        make ("Blurred Pad", "Pad", [] (Patch& p) { p.a.p[21] = 1050, p.a.p[22] = 1050, p.a.p[23] = 1500; unison (p.a, 5, 80, 180); volume (p.a, 0.4, 1.0, 1.0, 1.5); reverb (p, 500); p.g[11] = 627; }, { "blur" }),
        make ("Voices Aah", "Pad", [] (Patch& p) { p.a.timbre (0, 40, [] (int h) { return 0.15 + 2.5 * bell (h, 3, 1.5) + 2.0 * bell (h, 6, 1.5) + 1.2 * bell (h, 14, 3); }, 0.02); unison (p.a, 6, 70, 170); volume (p.a, 0.4, 1.0, 1.0, 0.8); reverb (p, 500); p.g[11] = 381; }, { "choir" }),
        make ("Phased Pad", "Pad", [] (Patch& p) { p.a.p[76] = 800; p.a.p[78] = 4000; p.a.p[80] = 1500; unison (p.a, 4, 90, 170); volume (p.a, 0.5, 1.0, 1.0, 1.0); chorus (p); p.g[11] = 458; }, { "phaser" }),
        make ("Two Layers", "Pad", [odd] (Patch& p) { p.b.on = true; unison (p.a, 5, 100, 180); volume (p.a, 0.6, 1.0, 1.0, 1.0); p.b.timbre (0, 64, odd, 0.0); p.b.p[40] = 20000; volume (p.b, 1.2, 1.0, 0.8, 1.2); reverb (p, 400); p.g[11] = 581; }, { "part B" }),
        make ("Bent Metal", "FX", [] (Patch& p) { p.a.p[26] = 500; p.a.timbre (0, 40, [] (int h) { return h % 3 == 1 ? 1.0 : 0.2; }, 0.0); volume (p.a, 0.001, 2.5, 0.0, 2.0); reverb (p, 500); p.g[11] = 637; }, { "prism" }),
        make ("Octave Cloud", "FX", [] (Patch& p) { p.a.combo[7] = 0; p.a.p[42] = 30000; p.a.timbre (0, 24, [] (int h) { return 0.3 * h; }, 0.0); p.a.p[13] = 600; unison (p.a, 4, 60, 180); volume (p.a, 0.2, 1.0, 1.0, 1.0); reverb (p, 500); p.g[11] = 595; }, { "octaves" }),
    };
}
} // namespace lynx::banks
