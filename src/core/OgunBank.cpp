// LYNX's own Ogun bank (round 190rh). Designed for LYNX and written from the documented layout of the plugin's state
// (REA_rodada37 §1.3); no FL preset was read for it. Ogun reads no data of FL's: the engine is the same with and without FL.

#include <cmath>
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>

#include "core/BankTrims.h"
#include "core/LynxBanks.h"
#include "core/Ogun.h"
#include "core/ZlibPack.h"

namespace lynx::banks
{
namespace
{
struct Bytes
{
    std::vector<std::uint8_t> b;
    template <typename T>
    void put (T v)
    {
        const auto* p = (const std::uint8_t*) &v;
        b.insert (b.end(), p, p + sizeof (v));
    }
    void i (int v) { put ((std::int32_t) v); }
    void f (float v) { put (v); }
    void u8 (int v) { b.push_back ((std::uint8_t) v); }
};
struct Point
{
    double dx, y;
    float tension = 0.0f;
};
void curve (Bytes& s, int type, const std::vector<Point>& points, bool on, int decay = -1, int sustain = -1)
{
    s.i (type), s.i (3), s.i ((int) points.size());
    for (const auto& p : points)
    {
        s.put (p.dx), s.put (p.y), s.f (p.tension);
        for (int k = 0; k < 4; ++k)
            s.u8 (0);
    }
    s.i (0), s.i (on ? -1 : 0), s.i (decay), s.i (-1), s.i (sustain);
}

struct Adsr
{
    float attack = 0.0f, decay = 0.5f, sustain = 1.0f, release = 0.2f;
};
struct Ogun
{
    std::array<std::int32_t, 64> P {};
    Adsr volume;
    bool volumeOn = false, cutoffOn = false, pitchOn = false;
    Adsr cutoff;
    float cutoffAmount = 0.5f, pitchDrop = 0.0f, pitchTime = 0.1f;
    std::array<std::uint8_t, 4> pattern {};
    std::vector<Point> impulse; // the Impulse EQ (empty = flat)
    bool delay = false;

    Ogun()
    {
        P[0] = 100, P[3] = P[4] = 256;
        P[5] = 100, P[6] = 4000, P[7] = 1500, P[8] = 0, P[9] = 0, P[10] = 1, P[11] = 2;
        P[12] = 1, P[13] = 64, P[14] = 128, P[15] = 40, P[16] = 64, P[17] = 256, P[18] = 0;
        P[20] = 400, P[21] = 300, P[22] = 40, P[23] = 64, P[24] = 64, P[25] = 64;
        P[26] = 50, P[27] = 144, P[29] = 64, P[30] = 2;
        P[31] = 120, P[32] = 120, P[33] = 10, P[34] = 55, P[35] = 80, P[36] = 40, P[37] = 100, P[38] = 2, P[39] = 60;
        for (int k : { 43, 44, 45, 46, 47, 48 })
            P[(std::size_t) k] = 32768;
        P[52] = 12, P[54] = 2; // richness (4095 partials), quality
        P[62] = 0x1ae, P[63] = 0;
    }
    void byte (int pluginOffset, int v)
    {
        auto* p = (std::uint8_t*) P.data();
        p[pluginOffset - 0x12c] = (std::uint8_t) v;
    }
    void seeds (int one, int two, int morph = 0) { P[10] = one, P[11] = two, P[9] = morph; }
    void times (int pre, int decay, int release) { P[5] = pre, P[6] = decay, P[7] = release; }
    void unison (int voices, int pitch, int pan = 100) { P[12] = voices, P[15] = pitch, P[13] = pan; }
    void filter (int frequency, int resonance, int order = 2) { P[17] = frequency, P[18] = resonance, P[49] = 0, P[50] = order; }
    void reverb (int wet = 60) { P[53] = 1, P[39] = wet; }

    std::vector<std::uint8_t> state() const
    {
        auto copy = *this;
        copy.byte (0x209, 1); // a key played again cuts its own voice
        copy.byte (0x20b, 1); // the pitch follows the key
        copy.byte (0x20e, 1); // declick
        copy.byte (0x225, 1); // level kept by the Impulse / Decay maps
        for (int k = 0; k < 4; ++k)
            for (int j = 0; j < 4; ++j)
                copy.byte (0x214 + 4 * k + j, j == 0 ? pattern[(std::size_t) k] : 0);
        Bytes s;
        for (const auto v : copy.P)
            s.i (v);
        s.u8 (1), s.u8 (delay ? 1 : 0), s.u8 (0);
        for (int k = 0; k < 3; ++k)
            s.i (k);
        s.u8 (0); // no comment
        for (int k = 0; k < 13; ++k)
        {
            static constexpr int additive[13] { 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1 };
            const auto add = additive[k] != 0;
            const double neutral = add ? 0.5 : 1.0;
            s.i (3);
            s.f (0.0f), s.f (k == 1 ? cutoffAmount : 1.0f), s.f (0.0f);
            const auto on = (k == 0 && volumeOn) || (k == 1 && cutoffOn) || (k == 3 && pitchOn);
            const auto e = k == 1 ? cutoff : volume;
            if (k == 3)
                curve (s, 1, { { 0.0, 0.5 + 0.5 * (double) pitchDrop }, { pitchTime, 0.5, -0.6f } }, on, -1, 1);
            else if (add)
                curve (s, 1, { { 0.0, 0.5 }, { e.attack, 1.0 }, { e.decay, 0.5 + 0.5 * (double) e.sustain, -0.4f }, { e.release, 0.5, -0.3f } }, on, 1, 2);
            else
                curve (s, 1, { { 0.0, 0.0 }, { e.attack, 1.0 }, { e.decay, e.sustain, -0.3f }, { e.release, 0.0, -0.3f } }, on, 1, 2);
            s.i (128), s.i (128), s.i (0), s.i (128);
            curve (s, 2, { { 0.0, 1.0 }, { 0.5, 1.0 } }, false, -1, 1);
            s.i (32768), s.i (0), s.i (0), s.i (0), s.i (0);
            for (int m = 0; m < 4; ++m)
                curve (s, 3, { { 0.0, neutral }, { 1.0, neutral } }, false);
        }
        curve (s, 3, { { 0.0, 0.0 }, { 1.0, 0.0 } }, false);                                  // forced harmonics: none
        curve (s, 3, impulse.empty() ? std::vector<Point> { { 0.0, 0.5 }, { 1.0, 0.5 } } : impulse, true); // Impulse EQ
        curve (s, 3, { { 0.0, 0.5 }, { 1.0, 0.5 } }, true);                                   // Decay EQ
        curve (s, 3, { { 0.0, 0.5 }, { 1.0, 0.5 } }, true);                                   // Release EQ
        const auto zip = zlibPack (s.b);
        Bytes out;
        out.put ((std::uint32_t) 0x00120003u); // version 18
        out.b.insert (out.b.end(), zip.begin(), zip.end());
        return out.b;
    }
};

Preset make (const char* name, const char* category, const std::function<void (Ogun&)>& design, std::vector<std::string> tags = {})
{
    Ogun o;
    design (o);
    // (the level knob's law: (3^(v / 128) - 1) / 2)
    o.P[0] = (int) std::lround (std::clamp (128.0 * std::log (((std::pow (3.0, o.P[0] / 128.0) - 1.0) * std::pow (10.0, trimDb ("Ogun", name) / 20.0)) + 1.0) / std::log (3.0), 1.0, 128.0));
    Preset p;
    p.name = name;
    p.category = category;
    p.tags = std::move (tags);
    p.apply = [o] (ChannelDef& c) {
        c.pluginState = o.state();
        c.modelParams.clear();
        c.samplePath.clear();
    };
    return p;
}
const std::vector<Point> dark { { 0.0, 0.9 }, { 0.35, 0.5, -0.3f }, { 0.65, 0.1 } }, bright { { 0.0, 0.4 }, { 1.0, 0.75 } }, middle { { 0.0, 0.2 }, { 0.4, 0.8 }, { 0.6, 0.15 } };
} // namespace

std::vector<Preset> ogunBank()
{
    return {
        make ("Iron Bell", "Metal", [] (Ogun& o) { o.seeds (11, 29, 400), o.times (0, 6000, 4000), o.P[8] = 200; o.reverb(); }, { "bell" }),
        make ("Glass Rod", "Metal", [] (Ogun& o) { o.seeds (57, 0), o.times (0, 5000, 3000), o.impulse = bright; o.reverb (50); }, { "bell" }),
        make ("Gong", "Metal", [] (Ogun& o) { o.seeds (204, 77, 600), o.times (300, 9000, 6000), o.P[8] = 500, o.P[52] = 14, o.impulse = dark; }, { "long" }),
        make ("Small Plate", "Metal", [] (Ogun& o) { o.seeds (830, 12), o.times (0, 1800, 900), o.impulse = bright; }, { "short" }),
        make ("Spring Rod", "Metal", [] (Ogun& o) { o.seeds (91, 402, 500), o.times (0, 3500, 1500), o.unison (3, 60); }, { "detuned" }),
        make ("Dark Chime", "Metal", [] (Ogun& o) { o.seeds (6, 88, 300), o.times (100, 7000, 5000), o.impulse = dark; o.reverb (80); }, { "bell" }),
        make ("Saw Choir", "Pad", [] (Ogun& o) { o.seeds (3, 0), o.times (1000, 10000, 5000), o.unison (5, 50), o.P[8] = -600; o.filter (150, 20); o.reverb(); }, { "wide" }),
        make ("Frost Pad", "Pad", [] (Ogun& o) { o.seeds (41, 310, 500), o.times (1000, 9000, 6000), o.unison (4, 40), o.volumeOn = true, o.volume = { 0.7f, 1.0f, 0.9f, 1.2f }; o.reverb (90); }, { "slow" }),
        make ("Deep Air", "Pad", [] (Ogun& o) { o.seeds (900, 45, 700), o.times (1000, 10000, 6000), o.unison (3, 30), o.impulse = dark, o.volumeOn = true, o.volume = { 1.0f, 1.0f, 1.0f, 1.5f }; o.reverb (80); }, { "slow" }),
        make ("Wire Pad", "Pad", [] (Ogun& o) { o.seeds (512, 513, 300), o.times (800, 8000, 4000), o.unison (7, 70), o.P[19] = 4, o.volumeOn = true, o.volume = { 0.5f, 1.0f, 0.8f, 1.0f }; }, { "chorus" }),
        make ("Bright Sweep", "Pad", [] (Ogun& o) { o.seeds (70, 7), o.times (1000, 9000, 4000), o.unison (4, 50), o.filter (70, 40), o.cutoffOn = true, o.cutoffAmount = 1.0f, o.cutoff = { 2.0f, 2.0f, 0.4f, 1.0f }; o.reverb(); }, { "filter" }),
        make ("Metal Bass", "Bass", [] (Ogun& o) { o.P[1] = -12; o.seeds (15, 0), o.times (0, 1500, 500), o.P[8] = -700, o.P[52] = 9, o.impulse = dark; }, { "short" }),
        make ("Scrape Bass", "Bass", [] (Ogun& o) { o.P[1] = -12; o.seeds (620, 34, 600), o.times (0, 3000, 800), o.unison (3, 40), o.filter (110, 60), o.cutoffOn = true, o.cutoffAmount = 0.8f, o.cutoff = { 0.01f, 0.4f, 0.2f, 0.2f }; }, { "filter" }),
        make ("Sub Plate", "Bass", [] (Ogun& o) { o.P[1] = -24; o.seeds (2, 5), o.times (0, 4000, 700), o.P[52] = 8, o.impulse = dark; }, { "sub" }),
        make ("Steel Pluck", "Pluck", [] (Ogun& o) { o.seeds (33, 0), o.times (0, 1200, 700), o.P[8] = -400; o.delay = true; }, { "short" }),
        make ("Harp Wire", "Pluck", [] (Ogun& o) { o.seeds (18, 240, 250), o.times (0, 2500, 1500), o.P[8] = -800, o.impulse = middle; o.reverb (50); }, { "string" }),
        make ("Muted Tine", "Pluck", [] (Ogun& o) { o.seeds (77, 0), o.times (0, 700, 300), o.impulse = dark; }, { "short" }),
        make ("Koto Rust", "Pluck", [] (Ogun& o) { o.seeds (441, 9, 350), o.times (0, 1800, 800), o.pitchOn = true, o.pitchDrop = 0.06f, o.pitchTime = 0.05f; o.delay = true; }, { "string" }),
        make ("Trash Hat", "Drum", [] (Ogun& o) { o.seeds (1234, 4321, 500), o.times (0, 500, 200), o.P[8] = 900, o.P[52] = 15, o.impulse = bright, o.byte (0x20b, 0); }, { "noise" }),
        make ("Clank", "Drum", [] (Ogun& o) { o.seeds (999, 0), o.times (0, 350, 150), o.P[8] = 400, o.impulse = middle; }, { "short" }),
        make ("Sheet Hit", "Drum", [] (Ogun& o) { o.seeds (321, 654, 700), o.times (0, 1500, 900), o.P[8] = 800, o.P[52] = 14; o.reverb (70); }, { "long" }),
        make ("Tank Drop", "FX", [] (Ogun& o) { o.seeds (808, 80, 400), o.times (0, 6000, 4000), o.pitchOn = true, o.pitchDrop = 0.8f, o.pitchTime = 1.5f; o.reverb (90); }, { "pitch" }),
        make ("Rail Drone", "FX", [] (Ogun& o) { o.P[1] = -12; o.seeds (64, 4096, 500), o.times (1000, 10000, 8000), o.unison (9, 120), o.P[52] = 14; o.reverb (100); }, { "long" }),
        make ("Ghost Metal", "FX", [] (Ogun& o) { o.seeds (27, 72, 900), o.times (1000, 9000, 7000), o.unison (5, 200), o.volumeOn = true, o.volume = { 1.5f, 2.0f, 0.6f, 2.0f }, o.delay = true; o.reverb (100); }, { "slow" }),
    };
}

// Autogun is Ogun's engine on the same state (REA_rodada57), at a quarter of its level: the same sounds, the level knob up
std::vector<Preset> autogunBank()
{
    auto bank = ogunBank();
    for (auto& p : bank)
        p.apply = [ogun = p.apply] (ChannelDef& c) {
            const auto path = c.samplePath; // (FL's SG.dat: the Magic Dust stage)
            ogun (c);
            c.samplePath = path;
        };
    return bank;
}
} // namespace lynx::banks
