// LYNX's own Morphine bank (round 190rh). Designed for LYNX and written from the documented chunks of the plugin's state
// (REA_rodada39 §2.2, the layout of core/Morphine.cpp's reader); no FL preset was read for it. Morphine is additive: the
// presets are spectra over time, so they sound the same with and without FL. The few with a noise layer use LYNX's noise
// (core/NativeData.h) unless the channel plays FL's.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>

#include "core/LynxBanks.h"
#include "core/Morphine.h"
#include "core/NativeData.h"

namespace lynx::banks
{
namespace
{
using Spectrum = std::function<double (int)>; // amplitude of partial h (1…128)
struct Frame
{
    int duration = 100; // (100 = one second at rate 0.5)
    Spectrum amp, detune;
};
struct Generator
{
    std::vector<Frame> frames;
    int loopStart = 0, loopEnd = 0, loopMode = 0; // (time units; 0 none, 1 forward, 2 mirror, 3 ping-pong)
    float rate = 0.5f;
    int pwmMode = 0;
    float pwmRate = 0.3f, detuneRange = 1.0f, panRange = 0.0f;
};

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
    void f (double v) { put ((float) v); }
    void tag (const char* t) { for (int k = 3; k >= 0; --k) b.push_back ((std::uint8_t) t[k]); }
    void add (const std::vector<std::uint8_t>& other) { b.insert (b.end(), other.begin(), other.end()); }
    void sized (const std::vector<std::uint8_t>& other) { i ((int) other.size()), add (other); }
};

struct Morphine
{
    std::array<Generator, 4> gen;
    std::vector<std::array<float, 3>> line { { 0.0f, 1.0f, 100.0f } }; // the morph path: X, Y, duration (A = (0, 1), B = (1, 1), C = (1, 0))
    bool lineLoops = false;
    std::array<float, 40> A {};
    std::string noise;

    Morphine()
    {
        A[0] = 0.62f, A[3] = -1.0f, A[4] = 0.2f, A[5] = 0.5f, A[6] = 0.5f, A[8] = 0.2f;
        A[9] = 0.1f, A[10] = 0.4f, A[11] = 0.9f, A[12] = 0.3f;
        A[13] = 0.4f, A[14] = 0.5f, A[15] = 0.15f, A[17] = 0.5f, A[18] = 0.6f, A[19] = 0.1f, A[20] = 0.45f, A[21] = 0.5f;
        A[23] = 0.5f, A[24] = 0.4f, A[25] = 0.6f;
        for (std::size_t k = 27; k < 35; ++k)
            A[k] = 0.5f;
        A[37] = 0.5f;
        for (auto& g : gen)
            g.frames = { { 100, [] (int h) { return h == 1 ? 1.0 : 0.0; }, {} } };
    }
    void envelope (float attack, float decay, float sustain, float release) { A[9] = attack, A[10] = decay, A[11] = sustain, A[12] = release; }
    void chorus() { A[16] = 1.0f; }
    void delay() { A[22] = 1.0f; }
    void reverb (float mix = 0.6f) { A[26] = 1.0f, A[25] = mix; }

    // `gain` brings the loudest frame of a generator to the same energy in every preset
    static std::vector<std::uint8_t> partials (const Frame& f, double gain)
    {
        Bytes s;
        s.tag ("MBP0");
        const auto array = [&s] (const std::function<float (int)>& value) {
            for (int k = 0; k < 128; k += 8)
            {
                int mask = 0;
                for (int j = 0; j < 8; ++j)
                    if (std::fabs (value (k + j)) > 1.0e-6f)
                        mask |= 1 << j;
                s.b.push_back ((std::uint8_t) mask);
                for (int j = 0; j < 8; ++j)
                    if ((mask >> j) & 1)
                        s.f (value (k + j));
            }
        };
        array ([&f] (int k) { return (float) std::cbrt (std::max (0.0, gain * f.amp (k + 1))); }); // (amplitudes as cube roots)
        array ([] (int) { return 0.0f; });                                                   // pan
        array ([&f] (int k) { return f.detune ? (float) f.detune (k + 1) : 0.0f; });          // detune, in harmonic numbers
        return s.b;
    }
    static std::vector<std::uint8_t> generator (const Generator& g)
    {
        Bytes sp;
        sp.tag ("MSP0");
        sp.i ((int) g.frames.size());
        double loudest = 1.0e-9;
        for (const auto& f : g.frames)
        {
            double e = 0.0;
            for (int h = 1; h <= 128; ++h)
                e += std::max (0.0, f.amp (h)) * std::max (0.0, f.amp (h));
            loudest = std::max (loudest, std::sqrt (e));
        }
        const auto gain = std::min (0.5 / loudest, 1.0 / 0.35);
        for (const auto& f : g.frames)
            sp.i (f.duration), sp.f (1.0), sp.sized (partials (f, gain));
        Bytes opt;
        opt.i (0x424), opt.i (g.loopStart), opt.i (g.loopEnd), opt.i (0), opt.i (g.loopMode), opt.f (g.rate), opt.f (0.5), opt.f (0.5), opt.i (0);
        for (int k = 0; k < 128; ++k)
            opt.f (0.3); // velocity sensitivity per partial
        for (int k = 0; k < 128; ++k)
            opt.f (1.0); // gain per partial
        sp.add (opt.b);
        Bytes zh;
        zh.tag ("MZH0");
        zh.i (1), zh.i (128), zh.f (1.0), zh.sized (sp.b);
        Bytes vc;
        vc.tag ("MVC0");
        vc.sized (zh.b);
        Bytes o;
        o.i (28), o.f (g.pwmRate), o.i (g.pwmMode), o.f (0.0), o.i (0), o.f (g.detuneRange), o.f (g.panRange);
        vc.add (o.b);
        return vc.b;
    }
    static std::vector<std::uint8_t> egFlat()
    {
        Bytes s;
        s.tag ("MEG0");
        s.i (2), s.i (8), s.f (1.0), s.i (100), s.f (1.0), s.i (100);
        Bytes o;
        o.i (0x2c), o.i (0), o.i (0), o.i (0), o.f (0.0), o.i (0), o.f (0.5), o.i (0), o.f (0.5), o.f (0.5), o.i (0);
        s.add (o.b);
        return s.b;
    }

    std::vector<std::uint8_t> state (const std::string& name) const
    {
        Bytes s;
        s.tag ("Mor0"), s.tag ("MPR0");
        for (const auto& g : gen)
            s.sized (generator (g));
        Bytes ml;
        ml.tag ("MML0");
        ml.i ((int) line.size()), ml.i (12);
        int total = 0;
        for (const auto& p : line)
            ml.f (p[0]), ml.f (p[1]), ml.i ((int) p[2]), total += (int) p[2];
        Bytes mo;
        mo.i (0x54), mo.i (0), mo.i (lineLoops ? total - (int) line.back()[2] : 0), mo.i (lineLoops ? 3 : 0), mo.i (0);
        for (int k = 0; k < 4; ++k)
            mo.f (1.0); // mixes
        for (int k = 0; k < 4; ++k)
            mo.f (0.5); // pans
        for (int k = 0; k < 4; ++k)
            mo.i (0); // mutes
        mo.f (0.5), mo.f (0.5), mo.f (0.5), mo.i (0);
        ml.add (mo.b);
        s.sized (ml.b);
        Bytes md;
        md.tag ("MMD0");
        md.i (4);
        for (int k = 0; k < 4; ++k)
            md.sized (egFlat());
        Bytes mx;
        mx.i (0x94);
        for (int k = 0; k < 24; ++k)
            mx.i (0); // sources, destinations: none
        for (int k = 0; k < 12; ++k)
            mx.f (0.5);
        md.add (mx.b);
        s.sized (md.b);
        Bytes g;
        g.i (0x5c4);
        for (const auto v : A)
            g.f (v);
        g.f (0.5), g.f (3.0); // noise ratio, noise loop mode
        const auto text = [&g] (const std::string& t, std::size_t size) {
            for (std::size_t k = 0; k < size; ++k)
                g.b.push_back (k < t.size() && k + 1 < size ? (std::uint8_t) t[k] : (std::uint8_t) 0);
        };
        text (noise, 1024);
        g.b.resize (1200, 0);
        text (name, 256);
        g.f (0.0), g.f (1.0), g.f (0.0), g.f (0.0), g.f (0.0); // noise filter off
        s.add (g.b);
        Bytes out;
        for (const auto c : { 'S', 'S', 'L', 'F' })
            out.b.push_back ((std::uint8_t) c);
        out.sized (s.b);
        return out.b;
    }
};

Preset make (const char* name, const char* category, const std::function<void (Morphine&)>& design, std::vector<std::string> tags = {})
{
    Morphine m;
    design (m);
    Preset p;
    p.name = name;
    p.category = category;
    p.tags = std::move (tags);
    p.apply = [m, n = std::string (name)] (ChannelDef& c) {
        c.pluginState = m.state (n);
        c.modelParams.clear();
        c.samplePath = m.noise.empty() ? std::string() : nativedata::ownPath (InstrumentKind::morphine);
    };
    return p;
}

double formant (double h, double centre, double width) { return std::exp (-(h - centre) * (h - centre) / (2.0 * width * width)); }
Spectrum power (double slope, double cut = 1.0e9) { return [=] (int h) { return std::pow ((double) h, -slope) * std::exp (-h / cut); }; }
Spectrum odd (double slope, double cut = 1.0e9) { return [=] (int h) { return (h & 1) != 0 ? std::pow ((double) h, -slope) * std::exp (-h / cut) : 0.0; }; }
Spectrum vowel (double a, double b) { return [=] (int h) { return (formant (h, a, 1.6) + 0.6 * formant (h, b, 2.2) + 0.3 * formant (h, 1, 0.8)) / std::sqrt ((double) h); }; }
Spectrum list (std::vector<double> v) { return [v] (int h) { return (std::size_t) (h - 1) < v.size() ? v[(std::size_t) (h - 1)] : 0.0; }; }
Spectrum scaled (Spectrum s, double g) { return [=] (int h) { return g * s (h); }; }
Spectrum inharmonic (double amount) { return [=] (int h) { return amount * (h - 1) * (h - 1); }; }
} // namespace

std::vector<Preset> morphineBank()
{
    return {
        make ("Sine to Saw", "Pad", [] (Morphine& m) {
            m.gen[0].frames = { { 150, power (4.0), {} }, { 150, power (1.4, 30), {} }, { 100, power (1.1, 60), {} } };
            m.gen[0].loopStart = 150, m.gen[0].loopEnd = 300, m.gen[0].loopMode = 3;
            m.envelope (0.35f, 0.5f, 1.0f, 0.5f), m.chorus(), m.reverb();
        }, { "evolving" }),
        make ("Vowel Walk", "Pad", [] (Morphine& m) {
            m.gen[0].frames = { { 120, vowel (3, 8), {} }, { 120, vowel (5, 11), {} }, { 120, vowel (2, 14), {} }, { 60, vowel (4, 9), {} } };
            m.gen[0].loopEnd = 360, m.gen[0].loopMode = 3;
            m.envelope (0.3f, 0.5f, 1.0f, 0.45f), m.chorus(), m.reverb();
        }, { "voice" }),
        make ("Two Worlds", "Pad", [] (Morphine& m) {
            m.gen[0].frames = { { 100, odd (1.2, 24), {} } }, m.gen[1].frames = { { 100, power (1.1, 40), {} } };
            m.line = { { 0.0f, 1.0f, 250.0f }, { 1.0f, 1.0f, 250.0f }, { 0.0f, 1.0f, 100.0f } }, m.lineLoops = true;
            m.envelope (0.35f, 0.5f, 1.0f, 0.5f), m.chorus(), m.reverb();
        }, { "morph" }),
        make ("Glass Drift", "Pad", [] (Morphine& m) {
            m.gen[0].frames = { { 200, list ({ 1, 0, 0.3, 0, 0.25, 0, 0.12 }), inharmonic (0.004) }, { 200, list ({ 1, 0.2, 0, 0.3, 0, 0.2, 0, 0.15 }), inharmonic (0.008) } };
            m.gen[0].loopEnd = 200, m.gen[0].loopMode = 3;
            m.envelope (0.4f, 0.5f, 1.0f, 0.55f), m.reverb (0.8f);
        }, { "glass" }),
        make ("PWM Strings", "Pad", [] (Morphine& m) {
            m.gen[0].frames = { { 100, power (1.0, 45), {} } };
            m.gen[0].pwmMode = 2, m.gen[0].pwmRate = 0.25f;
            m.envelope (0.3f, 0.5f, 1.0f, 0.4f), m.chorus(), m.reverb();
        }, { "strings" }),
        make ("Breath Choir", "Pad", [] (Morphine& m) {
            m.gen[0].frames = { { 150, vowel (2, 6), {} }, { 150, vowel (3, 7), {} } };
            m.gen[0].loopEnd = 150, m.gen[0].loopMode = 3;
            m.noise = "LYNX Air", m.A[36] = 0.35f;
            m.envelope (0.35f, 0.5f, 1.0f, 0.5f), m.reverb (0.75f);
        }, { "voice", "noise" }),
        make ("Organ Full", "Keys", [] (Morphine& m) {
            m.gen[0].frames = { { 100, list ({ 1, 0.9, 0.7, 0.6, 0, 0.5, 0, 0.5, 0, 0, 0, 0.2, 0, 0, 0, 0.3 }), {} } };
            m.envelope (0.05f, 0.3f, 1.0f, 0.12f), m.chorus();
        }, { "organ" }),
        make ("Tine Piano", "Keys", [] (Morphine& m) {
            m.gen[0].frames = { { 8, list ({ 1, 0.3, 0.1, 0.05, 0.02, 0.02, 0.2, 0.1 }), inharmonic (0.003) }, { 40, list ({ 1, 0.15, 0.05 }), {} }, { 250, list ({ 0.7, 0.06 }), {} }, { 100, list ({ 0.25 }), {} } };
            m.envelope (0.0f, 0.55f, 0.3f, 0.35f), m.chorus();
        }, { "electric piano" }),
        make ("Glass Bell", "Keys", [] (Morphine& m) {
            m.gen[0].frames = { { 30, list ({ 1, 0, 0.6, 0, 0, 0.5, 0, 0, 0, 0.3 }), inharmonic (0.011) }, { 300, list ({ 0.7, 0, 0.25, 0, 0, 0.1 }), inharmonic (0.011) }, { 100, list ({ 0.2 }), {} } };
            m.envelope (0.0f, 0.65f, 0.0f, 0.6f), m.reverb (0.8f);
        }, { "bell" }),
        make ("Wooden Bars", "Pluck", [] (Morphine& m) {
            m.gen[0].frames = { { 4, list ({ 1, 0, 0, 0.6, 0, 0, 0, 0, 0, 0.35 }), {} }, { 25, list ({ 0.8, 0, 0, 0.15 }), {} }, { 60, list ({ 0.3 }), {} } };
            m.envelope (0.0f, 0.42f, 0.0f, 0.3f), m.reverb (0.5f);
        }, { "marimba" }),
        make ("Harp Glow", "Pluck", [] (Morphine& m) {
            m.gen[0].frames = { { 10, power (1.2, 20), {} }, { 60, power (2.0, 12), {} }, { 200, power (3.0), {} } };
            m.envelope (0.0f, 0.55f, 0.0f, 0.45f), m.reverb();
        }, { "string" }),
        make ("Digital Pluck", "Pluck", [] (Morphine& m) {
            m.gen[0].frames = { { 5, odd (0.7, 40), {} }, { 25, odd (1.4, 12), {} }, { 60, odd (2.5), {} } };
            m.envelope (0.0f, 0.4f, 0.0f, 0.3f), m.delay();
        }, { "square" }),
        make ("Saw Lead", "Lead", [] (Morphine& m) { m.gen[0].frames = { { 100, power (1.0, 60), {} } }; m.A[3] = 0.0f, m.A[1] = 0.35f; m.envelope (0.05f, 0.4f, 1.0f, 0.25f), m.delay(); }, { "mono" }),
        make ("Hollow Lead", "Lead", [] (Morphine& m) { m.gen[0].frames = { { 40, odd (1.0, 30), {} }, { 100, odd (1.4, 18), {} } }; m.envelope (0.08f, 0.4f, 1.0f, 0.25f), m.delay(); }, { "square" }),
        make ("Talking Lead", "Lead", [] (Morphine& m) { m.gen[0].frames = { { 20, vowel (2, 9), {} }, { 25, vowel (6, 12), {} }, { 60, vowel (3, 7), {} } }; m.gen[0].loopEnd = 45, m.gen[0].loopMode = 3; m.envelope (0.05f, 0.4f, 1.0f, 0.25f); }, { "voice" }),
        make ("Fifth Lead", "Lead", [] (Morphine& m) { m.gen[0].frames = { { 100, [] (int h) { return (h % 2 == 0 ? 1.0 / h : 0.0) + (h % 3 == 0 ? 0.8 / h : 0.0); }, {} } }; m.envelope (0.06f, 0.4f, 1.0f, 0.25f), m.chorus(); }, { "fifth" }),
        make ("Round Bass", "Bass", [] (Morphine& m) { m.gen[0].frames = { { 12, power (1.1, 14), {} }, { 60, power (2.0, 6), {} } }; m.A[5] = 0.25f; m.envelope (0.0f, 0.45f, 0.7f, 0.2f); }, { "soft" }),
        make ("Reed Bass", "Bass", [] (Morphine& m) { m.gen[0].frames = { { 100, odd (1.1, 16), {} } }; m.gen[0].pwmMode = 2, m.gen[0].pwmRate = 0.15f; m.A[5] = 0.25f; m.envelope (0.0f, 0.4f, 0.9f, 0.2f); }, { "pwm" }),
        make ("Driven Bass", "Bass", [] (Morphine& m) { m.gen[0].frames = { { 30, power (1.0, 24), {} }, { 60, power (1.4, 12), {} } }; m.A[5] = 0.25f, m.A[7] = 0.35f; m.envelope (0.0f, 0.4f, 0.8f, 0.2f); }, { "overdrive" }),
        make ("Gong Cloud", "FX", [] (Morphine& m) {
            m.gen[0].frames = { { 60, power (0.6, 40), inharmonic (0.02) }, { 300, power (1.0, 20), inharmonic (0.023) }, { 200, power (2.0), inharmonic (0.02) } };
            m.envelope (0.1f, 0.75f, 0.0f, 0.7f), m.reverb (0.9f);
        }, { "metal" }),
        make ("Wind Tone", "FX", [] (Morphine& m) { m.gen[0].frames = { { 200, scaled (power (3.0), 0.5), {} } }; m.noise = "LYNX Air", m.A[36] = 0.8f; m.envelope (0.4f, 0.5f, 1.0f, 0.5f), m.reverb (0.8f); }, { "noise" }),
        make ("Spectral Rise", "FX", [] (Morphine& m) {
            for (int k = 0; k < 8; ++k)
                m.gen[0].frames.push_back ({ 60, [k] (int h) { return formant (h, 1.0 + 3.0 * k, 1.5); }, {} });
            m.gen[0].frames.erase (m.gen[0].frames.begin());
            m.envelope (0.2f, 0.5f, 1.0f, 0.5f), m.delay(), m.reverb();
        }, { "evolving" }),
        make ("Morph Triangle", "Keys", [] (Morphine& m) {
            m.gen[0].frames = { { 100, power (1.1, 30), {} } }, m.gen[1].frames = { { 100, odd (1.1, 20), {} } }, m.gen[2].frames = { { 100, vowel (3, 9), {} } };
            m.line = { { 0.0f, 1.0f, 80.0f }, { 1.0f, 1.0f, 80.0f }, { 1.0f, 0.0f, 80.0f }, { 0.0f, 1.0f, 50.0f } }, m.lineLoops = true;
            m.envelope (0.1f, 0.4f, 1.0f, 0.35f), m.chorus();
        }, { "morph" }),
        make ("Detuned Choir", "Pad", [] (Morphine& m) {
            m.gen[0].frames = { { 150, vowel (2, 7), [] (int h) { return 0.02 * std::sin (h * 2.1); } }, { 150, vowel (3, 9), [] (int h) { return -0.02 * std::sin (h * 1.7); } } };
            m.gen[0].loopEnd = 150, m.gen[0].loopMode = 3;
            m.envelope (0.35f, 0.5f, 1.0f, 0.5f), m.chorus(), m.reverb();
        }, { "voice" }),
    };
}
} // namespace lynx::banks
