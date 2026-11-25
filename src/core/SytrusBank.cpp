// LYNX's own Sytrus bank (round 190rh). Designed for LYNX and written from the documented layout of the plugin's state
// (REA_rodada11 §1, corrected in REA_rodada31 §0, §4 and §7); no FL preset was read for it. Sytrus reads no data of FL's: the engine is
// the same with and without FL.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>

#include "core/LynxBanks.h"
#include "core/SytrusEngine.h"
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

struct Adsr
{
    float attack = 0.0f, decay = 0.5f, sustain = 1.0f, release = 0.1f;
};
// the six harmonics of an additive table: amplitude of harmonic 1…
using Table = std::vector<float>;

struct Operator
{
    float ratio = 1.0f;
    int shape = 0, volume = 256, tension = 0, pitchRange = 1200;
    Table table;
    bool envOn = false;
    Adsr env;
    float pitchStart = 0.5f, pitchTime = 0.0f; // a pitch drop from this level (0.5 = none)
    float velocity = 0.0f;                     // how much the velocity map takes from the volume (0…1)
    float lfoDepth = 0.0f;                     // vibrato (pitch articulator's LFO)
    bool pluck = false;
};
struct Filter
{
    int order = 0, type = 3, cutoff = 256, resonance = 0, envAmount = 128;
    bool envOn = false;
    Adsr env;
    float lfoDepth = 0.0f;
    int lfoSpeed = 45000;
};

struct Sytrus
{
    std::array<Operator, 6> op;
    std::array<Filter, 3> filter;
    std::array<int, 81> fm {}, rm {}; // [row * 9 + column]
    int master = 100, pitch = 0;
    int unison = 1, uniPan = 64, uniPitch = 24, uniPhase = 64;
    int chorus = 0;
    bool delay = false, reverb = false;
    int reverbWet = 60, reverbDecay = 30, delayTime = 144, delayFeedback = 50, delayVolume = 64;

    // ±256: how much operator / filter `from` goes to `to`
    void modulate (int from, int to, int amount) { fm[(std::size_t) (to * 9 + from)] = amount; }
    void ring (int from, int to, int amount) { rm[(std::size_t) (to * 9 + from)] = amount; }
    void out (int unit, int amount = 256) { fm[(std::size_t) (unit * 9 + 8)] = amount; }
    void fx (int unit, int amount = 200) { fm[(std::size_t) (unit * 9 + 7)] = amount; }
    void pan (int unit, int amount) { fm[(std::size_t) (unit * 9 + 6)] = amount; }
    void toFilter (int opIndex, int filterIndex, int amount = 256) { modulate (opIndex, 6 + filterIndex, amount); }

    struct Point
    {
        double dt, y;
        float tension = 0.0f;
    };
    // an envelope part: kind (1 envelope, 2 LFO, 3 map), version 3, points, header
    static void part (Bytes& s, int kind, const std::vector<Point>& points, bool on, int decay = -1, int sustain = -1)
    {
        s.i (kind), s.i (3), s.i ((int) points.size());
        for (const auto& p : points)
        {
            s.put (p.dt), s.put (p.y), s.f (p.tension);
            for (int k = 0; k < 4; ++k)
                s.u8 (0);
        }
        s.i (0), s.i (on ? -1 : 0), s.i (decay), s.i (-1), s.i (sustain);
    }
    static std::vector<Point> adsr (const Adsr& e) { return { { 0.0, 0.0 }, { e.attack, 1.0 }, { e.decay, e.sustain, -0.3f }, { e.release, 0.0, -0.3f } }; }

    struct Art
    {
        bool additive = false, envOn = false;
        std::vector<Point> env;
        int sustain = 2, decay = 1;
        float lfoDepth = 0.0f, velocity = 0.0f;
        int lfoSpeed = 45000;
    };
    static void articulator (Bytes& s, const Art& a)
    {
        s.i (3);
        s.f (0.0f), s.f (1.0f), s.f (a.lfoDepth); // base, envelope depth, LFO depth
        part (s, 1, a.env.empty() ? adsr ({ 0.75f, 1.5f, 0.5f, 0.25f }) : a.env, a.envOn, a.decay, a.sustain);
        s.i (128), s.i (128), s.i (0), s.i (128); // the articulator's A / D / S / R knobs
        // the LFO's own amplitude envelope: there from the start
        part (s, 2, { { 0.0, 1.0 }, { 0.5, 1.0 } }, a.lfoDepth != 0.0f, -1, 1);
        s.i (a.lfoSpeed), s.i (0), s.i (0), s.i (0), s.i (0);
        const double flat = a.additive ? 0.5 : 1.0;
        for (int m = 0; m < 6; ++m) // keyboard, velocity, Mod X, Mod Y, random, unison: neutral
            if (m == 1 && a.velocity > 0.0f)
                part (s, 3, { { 0.0, flat * (1.0 - (double) a.velocity) }, { 1.0, flat } }, true);
            else
                part (s, 3, { { 0.0, flat }, { 1.0, flat } }, false);
    }

    std::vector<std::uint8_t> state() const
    {
        Bytes s;
        std::array<std::int32_t, 200> main {};
        main[0] = master, main[2] = pitch;
        for (std::size_t k = 0; k < 81; ++k)
            main[3 + k] = fm[k], main[84 + k] = rm[k];
        const int global[8] { 128, 128, 0, 128, 128, 128, 0, 128 };
        for (std::size_t k = 0; k < 8; ++k)
            main[165 + k] = global[k];
        const int uni[7] { unison, uniPan, 128, uniPitch, 0, uniPhase, 0 };
        for (std::size_t k = 0; k < 7; ++k)
            main[173 + k] = uni[k];
        const int cho[9] { 0, chorus, 400, 300, 40, 64, 64, 100, 0 };
        for (std::size_t k = 0; k < 9; ++k)
            main[180 + k] = cho[k];
        main[189] = -1, main[190] = main[191] = 256;
        main[195] = 0, main[196] = 1; // oversampling: ×1 live, ×2 export
        for (const auto v : main)
            s.i (v);
        s.b.resize (962, 0); // 800 bytes of integers, then the 162 mutes
        s.i (0);
        s.u8 (0), s.u8 (0), s.u8 (0);
        for (int k = 0; k < 9; ++k)
            s.u8 (0);
        for (int k = 0; k < 3; ++k)
        {
            s.i (delayFeedback), s.i (delayTime), s.i (0), s.i (delayVolume), s.i (2);
            s.u8 (1), s.u8 (0), s.u8 (k == 0 && delay ? 1 : 0); // tempo, serial, on
        }
        for (const int v : { 120, 120, 10, 55, 80, reverbDecay, 100, 2, reverbWet })
            s.i (v);
        s.u8 (reverb ? 1 : 0), s.u8 (0);
        s.i (0); // no EQ
        for (int k = 0; k < 3; ++k)
            s.i (0), s.i (32768), s.i (32768), s.i (k);
        s.u8 (0); // no text
        for (int u = 0; u < 10; ++u)
        {
            if (u < 6)
                operatorUnit (s, op[(std::size_t) u]);
            else if (u < 9)
                filterUnit (s, filter[(std::size_t) (u - 6)]);
            else
            {
                for (int k = 0; k < 10; ++k)
                    s.i (0);
                Art a;
                a.additive = true;
                articulator (s, a);
                articulator (s, {});
            }
        }
        s.u8 (0), s.u8 (0);
        auto zip = zlibPack (s.b);
        Bytes out;
        out.put ((std::uint32_t) 0x00190003u); // version 25
        out.b.insert (out.b.end(), zip.begin(), zip.end());
        return out.b;
    }

    static void operatorUnit (Bytes& s, const Operator& o)
    {
        for (const int v : { 0, o.volume, o.pitchRange, 0, 0, 0, 0, 0, 0, 0 })
            s.i (v);
        s.i ((int) std::lround (o.ratio * 20000.0f)), s.i (0), s.i (128);
        for (std::size_t h = 0; h < 256; ++h)
            s.f (o.table.empty() ? (h == 0 ? 1.0f : 0.0f) : h < o.table.size() ? o.table[h] : 0.0f);
        for (int h = 0; h < 256; ++h)
            s.f (0.0f);
        s.i (o.shape), s.i (o.tension), s.i (0), s.i (0), s.i (128);
        for (int k = 0; k < 8; ++k)
            s.u8 (k == 6 && o.pluck ? 1 : 0);
        // 0 panning, 1 volume, 2 input modulation, 3 pitch, 4 phase, 5 plucked damp
        for (int k = 0; k < 6; ++k)
        {
            Art a;
            a.additive = k == 0 || k == 3 || k == 4;
            if (k == 1)
            {
                a.envOn = o.envOn;
                if (o.envOn)
                    a.env = adsr (o.env);
                a.velocity = o.velocity;
            }
            if (k == 3)
            {
                if (o.pitchTime > 0.0f)
                {
                    a.envOn = true;
                    a.env = { { 0.0, o.pitchStart }, { o.pitchTime, 0.5, -0.6f } };
                    a.decay = -1, a.sustain = 1;
                }
                a.lfoDepth = o.lfoDepth;
            }
            articulator (s, a);
        }
    }

    static void filterUnit (Bytes& s, const Filter& f)
    {
        for (const int v : { f.order, f.envAmount, f.cutoff, f.resonance, 128, 0, 0, 128, 0, 0 })
            s.i (v);
        s.i (0);
        for (int k = 0; k < 9; ++k)
            s.f (0.0f);
        s.i (0), s.i (f.type); // no waveshaper; the filter's type
        part (s, 3, { { 0.0, 0.0 }, { 1.0, 1.0 } }, true);
        s.u8 (0);
        // 0 panning, 1 volume, 2 cutoff, 3 resonance, 4 low / gain, 5 band, 6 high, 7 waveshaper
        for (int k = 0; k < 8; ++k)
        {
            Art a;
            a.additive = k == 0 || k == 2 || k == 3;
            if (k == 2)
            {
                a.envOn = f.envOn;
                if (f.envOn)
                    a.env = { { 0.0, 0.5 }, { f.env.attack, 1.0 }, { f.env.decay, 0.5 + 0.5 * (double) f.env.sustain, -0.4f }, { f.env.release, 0.5, -0.3f } };
                a.lfoDepth = f.lfoDepth;
                a.lfoSpeed = f.lfoSpeed;
            }
            articulator (s, a);
        }
    }
};

constexpr int saw = 256, square = 384, triangle = 128, pulse = 470;

Preset make (const char* name, const char* category, const std::function<void (Sytrus&)>& design, std::vector<std::string> tags = {})
{
    Sytrus s;
    design (s);
    Preset p;
    p.name = name;
    p.category = category;
    p.tags = std::move (tags);
    p.apply = [s] (ChannelDef& c) {
        c.pluginState = s.state();
        c.modelParams.clear();
        c.samplePath.clear();
    };
    return p;
}
// operator `k`: shape, ratio, volume and its volume envelope
Operator voice (int shape, float ratio = 1.0f, Adsr env = { 0.002f, 0.6f, 0.8f, 0.15f }, int volume = 256)
{
    Operator o;
    o.shape = shape, o.ratio = ratio, o.volume = volume, o.envOn = true, o.env = env;
    return o;
}
Operator modulator (float ratio, Adsr env, int volume = 256, float velocity = 0.5f)
{
    auto o = voice (0, ratio, env, volume);
    o.velocity = velocity;
    return o;
}
Filter lowPass (int cutoff, int resonance = 20, int order = 2, int envAmount = 0, Adsr env = { 0.002f, 0.3f, 0.2f, 0.2f })
{
    Filter f;
    f.order = order, f.type = 3, f.cutoff = cutoff, f.resonance = resonance, f.envAmount = envAmount, f.envOn = envAmount > 0, f.env = env;
    return f;
}
} // namespace

std::vector<Preset> sytrusBank()
{
    const Adsr pad { 0.6f, 1.0f, 0.9f, 1.2f }, pluck { 0.001f, 0.45f, 0.0f, 0.3f }, bell { 0.001f, 2.2f, 0.0f, 1.5f }, keys { 0.001f, 1.4f, 0.2f, 0.3f },
        bass { 0.001f, 0.35f, 0.6f, 0.08f }, organ { 0.004f, 0.1f, 1.0f, 0.06f };
    return {
        make ("Pure Sine", "Init", [] (Sytrus& s) { s.op[0] = voice (0); s.out (0); }),
        make ("Warm Saws", "Lead", [] (Sytrus& s) {
            s.op[0] = voice (saw), s.op[1] = voice (saw, 1.004f);
            s.filter[0] = lowPass (170, 20, 2, 40);
            s.toFilter (0, 0, 190), s.toFilter (1, 0, 190), s.out (6);
        }, { "saw" }),
        make ("Nine Saws", "Lead", [] (Sytrus& s) {
            s.op[0] = voice (saw);
            s.unison = 9, s.uniPitch = 40, s.uniPan = 100;
            s.out (0), s.fx (0, 120), s.delay = true, s.master = 90;
        }, { "supersaw" }),
        make ("Square Solo", "Lead", [] (Sytrus& s) {
            s.op[0] = voice (square), s.op[1] = voice (square, 0.5f, { 0.002f, 0.6f, 0.8f, 0.15f }, 128);
            s.op[0].lfoDepth = 0.02f;
            s.filter[0] = lowPass (190, 30);
            s.toFilter (0, 0), s.toFilter (1, 0), s.out (6, 220), s.fx (6, 140), s.delay = true;
        }, { "square" }),
        make ("FM Brass", "Lead", [] (Sytrus& s) {
            s.op[0] = voice (0, 1.0f, { 0.04f, 0.4f, 0.85f, 0.15f }), s.op[1] = modulator (1.0f, { 0.07f, 0.5f, 0.6f, 0.15f });
            s.modulate (1, 0, 150), s.modulate (1, 1, 90), s.out (0);
        }, { "fm", "brass" }),
        make ("Ring Lead", "Lead", [] (Sytrus& s) {
            s.op[0] = voice (saw), s.op[1] = voice (0, 2.5f);
            s.ring (1, 0, 160), s.out (0, 230), s.fx (0, 120), s.reverb = true;
        }, { "ring" }),
        make ("Electric Tines", "Keys", [keys] (Sytrus& s) {
            s.op[0] = voice (0, 1.0f, keys), s.op[1] = modulator (14.0f, { 0.001f, 0.25f, 0.0f, 0.1f }), s.op[2] = voice (0, 1.0f, keys, 160), s.op[3] = modulator (1.0f, keys, 200);
            s.modulate (1, 0, 70), s.modulate (3, 2, 110), s.out (0), s.out (2), s.fx (0, 120), s.chorus = 3;
        }, { "fm", "electric piano" }),
        make ("Glass Bell", "Keys", [bell] (Sytrus& s) {
            s.op[0] = voice (0, 1.0f, bell), s.op[1] = modulator (3.5f, { 0.001f, 1.6f, 0.0f, 1.0f }), s.op[2] = voice (0, 2.0f, bell, 110), s.op[3] = modulator (7.01f, { 0.001f, 0.8f, 0.0f, 0.5f });
            s.modulate (1, 0, 110), s.modulate (3, 2, 90), s.out (0), s.out (2), s.fx (0, 200), s.fx (2, 200), s.reverb = true;
        }, { "fm", "bell" }),
        make ("Tubular", "Keys", [bell] (Sytrus& s) {
            s.op[0] = voice (0, 1.0f, bell), s.op[1] = modulator (1.414f, { 0.001f, 1.8f, 0.1f, 1.2f });
            s.modulate (1, 0, 150), s.out (0), s.fx (0, 200), s.reverb = true, s.reverbDecay = 50;
        }, { "fm", "bell" }),
        make ("Drawbars", "Keys", [organ] (Sytrus& s) {
            s.op[0] = voice (0, 1.0f, organ);
            s.op[0].table = { 1.0f, 0.8f, 0.6f, 0.4f, 0.0f, 0.3f, 0.0f, 0.35f };
            s.op[1] = voice (0, 0.5f, organ, 180);
            s.out (0, 200), s.out (1, 190), s.fx (0, 160), s.chorus = 3;
        }, { "additive", "organ" }),
        make ("Clav Bite", "Keys", [] (Sytrus& s) {
            s.op[0] = voice (pulse, 1.0f, { 0.001f, 0.6f, 0.1f, 0.06f });
            s.filter[0] = lowPass (110, 70, 2, 110, { 0.001f, 0.15f, 0.1f, 0.1f });
            s.toFilter (0, 0), s.out (6);
        }, { "pulse" }),
        make ("Sub Sine", "Bass", [bass] (Sytrus& s) { s.op[0] = voice (0, 0.5f, bass), s.op[1] = modulator (0.5f, bass, 200, 0.2f); s.modulate (1, 0, 60), s.out (0); }, { "sub" }),
        make ("FM Growl", "Bass", [bass] (Sytrus& s) {
            s.op[0] = voice (0, 0.5f, bass), s.op[1] = modulator (0.5f, { 0.001f, 0.25f, 0.5f, 0.1f }), s.op[2] = modulator (1.0f, { 0.001f, 0.2f, 0.4f, 0.1f });
            s.modulate (1, 0, 170), s.modulate (2, 1, 130), s.modulate (2, 2, 80), s.out (0);
        }, { "fm" }),
        make ("Saw Bass", "Bass", [bass] (Sytrus& s) {
            s.op[0] = voice (saw, 0.5f, bass), s.op[1] = voice (square, 0.25f, bass, 140);
            s.filter[0] = lowPass (90, 60, 2, 90, { 0.001f, 0.2f, 0.2f, 0.1f });
            s.toFilter (0, 0), s.toFilter (1, 0, 180), s.out (6);
        }, { "saw" }),
        make ("Reese", "Bass", [bass] (Sytrus& s) {
            s.op[0] = voice (saw, 0.5f, bass), s.op[1] = voice (saw, 0.503f, bass);
            s.unison = 3, s.uniPitch = 50;
            s.filter[0] = lowPass (110, 10);
            s.toFilter (0, 0, 200), s.toFilter (1, 0, 200), s.out (6);
        }, { "detuned" }),
        make ("Wobble", "Bass", [bass] (Sytrus& s) {
            s.op[0] = voice (saw, 0.5f, bass);
            s.filter[0] = lowPass (90, 80);
            s.filter[0].lfoDepth = 0.25f, s.filter[0].lfoSpeed = 42000;
            s.toFilter (0, 0), s.out (6);
        }, { "lfo" }),
        make ("Wide Pad", "Pad", [pad] (Sytrus& s) {
            s.op[0] = voice (saw, 1.0f, pad), s.op[1] = voice (triangle, 0.5f, pad, 180);
            s.unison = 7, s.uniPitch = 45, s.uniPan = 110;
            s.filter[0] = lowPass (120, 10, 2, 40, { 0.8f, 1.5f, 0.5f, 1.0f });
            s.toFilter (0, 0, 190), s.toFilter (1, 0, 190), s.out (6, 220), s.fx (6, 200), s.reverb = true, s.reverbDecay = 50;
        }, { "slow" }),
        make ("Choral Sines", "Pad", [pad] (Sytrus& s) {
            s.op[0] = voice (0, 1.0f, pad);
            s.op[0].table = { 1.0f, 0.5f, 0.35f, 0.5f, 0.2f, 0.1f, 0.15f, 0.08f };
            s.unison = 5, s.uniPitch = 30, s.uniPan = 100;
            s.out (0, 200), s.fx (0, 220), s.chorus = 4, s.reverb = true;
        }, { "additive" }),
        make ("FM Haze", "Pad", [pad] (Sytrus& s) {
            s.op[0] = voice (0, 1.0f, pad), s.op[1] = modulator (2.0f, { 1.5f, 2.0f, 0.5f, 1.0f }, 256, 0.0f), s.op[2] = voice (0, 1.003f, pad), s.op[3] = modulator (3.0f, { 2.0f, 2.0f, 0.4f, 1.0f }, 256, 0.0f);
            s.modulate (1, 0, 90), s.modulate (3, 2, 80), s.pan (0, -120), s.pan (2, 120), s.out (0), s.out (2), s.fx (0, 180), s.fx (2, 180), s.reverb = true;
        }, { "fm" }),
        make ("Saw Pluck", "Pluck", [pluck] (Sytrus& s) {
            s.op[0] = voice (saw, 1.0f, pluck), s.op[1] = voice (saw, 2.006f, pluck, 120);
            s.filter[0] = lowPass (90, 30, 2, 120, { 0.001f, 0.18f, 0.0f, 0.15f });
            s.toFilter (0, 0, 220), s.toFilter (1, 0, 220), s.out (6), s.fx (6, 140), s.delay = true;
        }, { "short" }),
        make ("FM Pluck", "Pluck", [pluck] (Sytrus& s) {
            s.op[0] = voice (0, 1.0f, pluck), s.op[1] = modulator (3.0f, { 0.001f, 0.12f, 0.0f, 0.1f });
            s.modulate (1, 0, 160), s.out (0), s.fx (0, 140), s.delay = true;
        }, { "fm" }),
        make ("Marimba Wood", "Pluck", [] (Sytrus& s) {
            s.op[0] = voice (0, 1.0f, { 0.001f, 0.35f, 0.0f, 0.2f }), s.op[1] = voice (0, 4.0f, { 0.001f, 0.08f, 0.0f, 0.05f }, 130), s.op[2] = modulator (10.0f, { 0.001f, 0.03f, 0.0f, 0.02f });
            s.modulate (2, 0, 80), s.out (0), s.out (1), s.fx (0, 120), s.reverb = true;
        }, { "percussive" }),
        make ("Laser Drop", "FX", [] (Sytrus& s) {
            s.op[0] = voice (square, 1.0f, { 0.001f, 0.8f, 0.0f, 0.3f });
            s.op[0].pitchRange = 4800, s.op[0].pitchStart = 1.0f, s.op[0].pitchTime = 0.6f;
            s.out (0, 200), s.fx (0, 180), s.delay = true, s.reverb = true;
        }, { "pitch" }),
        make ("Metal Storm", "FX", [] (Sytrus& s) {
            s.op[0] = voice (0, 1.0f, { 0.4f, 2.0f, 0.4f, 1.5f }), s.op[1] = modulator (2.73f, { 0.6f, 2.0f, 0.7f, 1.0f }), s.op[2] = modulator (5.13f, { 1.0f, 2.0f, 0.8f, 1.0f });
            s.modulate (1, 0, 220), s.modulate (2, 1, 200), s.modulate (1, 2, 120), s.out (0, 190), s.fx (0, 200), s.reverb = true, s.reverbDecay = 70;
        }, { "noise" }),
    };
}
} // namespace lynx::banks
