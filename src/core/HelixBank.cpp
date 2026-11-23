// LYNX's own Helix presets (round 190mi): designed for LYNX on Helix (core/WavetableSynth.h) and its Helix+ block
// (second warps, unison stacking, noise, comb / formant / phaser filters, macros, drawn LFOs, the effects rack); round
// 190ml adds presets for Helix+ 2 (oscillator 3, the sample oscillator with LYNX-made samples, user tables made from
// formulas and harmonics, macros 5…8, chaos sources, the shifter / EQ / multiband); round 190mt adds presets for the
// granular and spectral oscillators (Helix round 3); round 190oc two for their unison and reverse grains (Helix round 4).
// Clean-room: no Serum, Live or other preset was read or converted; LYNX's own tables (core/WavetableTables.cpp).
// The first preset ("Init") is Helix's own default, so a new Helix channel sounds as before.

#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <utility>

#include "core/LynxBanks.h"
#include "core/WavetableSynth.h"

namespace lynx::banks
{
namespace
{
namespace w = wt;

// a preset's state, from Helix's new-channel values (Helix+ live)
struct Patch
{
    std::vector<float> v = helixDefaults();
    Patch& set (int index, double value)
    {
        v[(std::size_t) index] = (float) value;
        return *this;
    }
    Patch& x (int extIndex, double value) { return set (w::extBase + extIndex, value); }
    Patch& x2 (int ext2Index, double value) { return set (w::ext2Base + ext2Index, value); }
    Patch& x3 (int ext3Index, double value) { return set (w::ext3Base + ext3Index, value); }
    Patch& x4 (int ext4Index, double value) { return set (w::ext4Base + ext4Index, value); }
    Patch& osc3 (int param, double value) { return x2 (w::ext2::osc3 + param, value); }
    Patch& table3 (const char* name)
    {
        const auto t = w::tableByName (name);
        return x2 (w::ext2::osc3Table, t < 0 ? 0 : t);
    }
    Patch& cell (int target, int source, double amount) { return set (w::cellIndex (target, source), amount); }
    Patch& table (int osc, const char* name)
    {
        const auto t = w::tableByName (name);
        return set (w::tableBase + osc, t < 0 ? 0 : t);
    }
    Patch& osc (int k, int param, double value) { return set (w::oscBase (k) + param, value); }
    Patch& filter (int k, int param, double value) { return set (w::filterBase (k) + param, value); }
    Patch& env (int k, double a, double d, double s, double r)
    {
        const auto b = w::envBase (k);
        return set (b + w::env::attack, a).set (b + w::env::decay, d).set (b + w::env::sustain, s).set (b + w::env::release, r);
    }
    Patch& lfo (int k, int type, double rate, bool synced = false, int syncedRate = 15)
    {
        const auto b = w::lfoBase (k);
        return set (b + w::lfo::type, type).set (b + w::lfo::rate, rate).set (b + w::lfo::sync, synced ? 1.0 : 0.0).set (b + w::lfo::syncedRate, syncedRate);
    }
    // LFO k's drawn shape: (x, y, curve) points, x rising
    Patch& draw (int k, std::initializer_list<std::array<double, 3>> points)
    {
        const auto b = w::ext::points + k * w::ext::lfoPointsSize;
        x (w::ext::lfoDraw + k, 1.0).x (b, (double) points.size());
        int i = 0;
        for (const auto& p : points)
        {
            for (int j = 0; j < 3; ++j)
                x (b + 1 + i * w::ext::pointStride + j, p[(std::size_t) j]);
            ++i;
        }
        return *this;
    }
};

// a preset's own data (user tables, a sample), made when the preset is applied (Helix+ 2, round 190ml)
using DataMaker = std::function<w::HelixUserData()>;

Preset make (const char* name, const char* category, std::vector<std::string> tags, const std::function<void (Patch&)>& build, DataMaker data = {})
{
    Patch p;
    if (build)
        build (p);
    return { name, category, std::move (tags), [state = p.v, data] (ChannelDef& c) {
                c.modelParams = state;
                c.pluginState = data ? w::encodeUserData (data()) : std::vector<std::uint8_t> {}; // (a preset's data replaces the channel's)
            } };
}

// --- LYNX-made material for the presets (190ml): tables from formulas and partial lists, samples synthesised here
w::UserTable formulaTable (const char* formula, const char* name)
{
    w::UserTable t;
    std::string error;
    (void) w::userTableFromFormula (formula, t, error);
    t.name = name;
    return t;
}
// partial amplitudes per frame from a rule (h 1…partials, frame position 0…1)
w::UserTable partialTable (const char* name, int partials, const std::function<double (int h, double t)>& amp)
{
    w::UserTable t;
    t.name = name;
    t.partials = partials;
    t.amp.assign ((std::size_t) w::frameCount * (std::size_t) partials, 0.0f);
    t.phase.assign (t.amp.size(), 0.0f);
    for (int f = 0; f < w::frameCount; ++f)
        for (int h = 1; h <= partials; ++h)
            t.amp[(std::size_t) (f * partials + h - 1)] = (float) amp (h, (double) f / (w::frameCount - 1));
    return t;
}
std::uint32_t mix (std::uint32_t x)
{
    x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
    return x;
}
// a sung "ah" → "oh" at 110 Hz (a glottal-like pulse through three formants), 1.2 s at 22.05 kHz, mono
w::SampleBuffer vowelSample()
{
    w::SampleBuffer b;
    b.rate = 22050.0;
    const int n = (int) (1.2 * b.rate);
    b.l.resize ((std::size_t) n);
    double ph = 0.0;
    double z[3][2] {};
    for (int i = 0; i < n; ++i)
    {
        const auto t = (double) i / n;
        ph += 110.0 * (1.0 + 0.004 * std::sin (2.0 * 3.14159265 * 5.0 * i / b.rate)) / b.rate;
        ph -= std::floor (ph);
        const auto src = ph < 0.6 ? std::sin (3.14159265 * ph / 0.6) : 0.0; // (open phase, then closed)
        const double f[3] { 730.0 + (570.0 - 730.0) * t, 1090.0 + (840.0 - 1090.0) * t, 2440.0 };
        const double g[3] { 1.0, 0.5, 0.25 };
        double y = 0.0;
        for (int k = 0; k < 3; ++k) // (two-pole resonators, 80 Hz wide)
        {
            const auto rr = std::exp (-3.14159265 * 80.0 / b.rate), c = 2.0 * rr * std::cos (2.0 * 3.14159265 * f[k] / b.rate);
            const auto v = src * (1.0 - rr) + c * z[k][0] - rr * rr * z[k][1];
            z[k][1] = z[k][0], z[k][0] = v;
            y += g[k] * v;
        }
        const auto env = std::min (1.0, i / (0.05 * b.rate)) * std::min (1.0, (n - i) / (0.1 * b.rate));
        b.l[(std::size_t) i] = (float) (0.9 * y * env);
    }
    float peak = 0.0f;
    for (const auto v : b.l)
        peak = std::max (peak, std::abs (v));
    for (auto& v : b.l)
        v *= peak > 0.0f ? 0.9f / peak : 1.0f;
    return b;
}
// a short metallic noise hit (seeded noise through a decaying ring), 0.4 s at 44.1 kHz, stereo
w::SampleBuffer hitSample()
{
    w::SampleBuffer b;
    b.rate = 44100.0;
    const int n = (int) (0.4 * b.rate);
    b.l.resize ((std::size_t) n), b.r.resize ((std::size_t) n);
    double lp[2] {};
    for (int i = 0; i < n; ++i)
    {
        const auto e = std::exp (-(double) i / (0.06 * b.rate));
        for (int s = 0; s < 2; ++s)
        {
            const auto nz = (double) (mix ((std::uint32_t) i * 2u + (std::uint32_t) s + 190u) >> 8) / 8388608.0 - 1.0;
            lp[s] += 0.35 * (nz - lp[s]);
            const auto ring = std::sin (2.0 * 3.14159265 * (s == 0 ? 1870.0 : 1910.0) * i / b.rate) * std::exp (-(double) i / (0.15 * b.rate));
            (s == 0 ? b.l : b.r)[(std::size_t) i] = (float) (0.6 * lp[s] * e + 0.35 * ring);
        }
    }
    return b;
}
// (190mt) a struck glass: five inharmonic partials over A4 (440 Hz), each with its own decay, 1.5 s at 44.1 kHz, mono
w::SampleBuffer glassSample()
{
    w::SampleBuffer b;
    b.rate = 44100.0;
    const int n = (int) (1.5 * b.rate);
    b.l.resize ((std::size_t) n);
    const double ratio[5] { 1.0, 2.32, 4.25, 6.63, 9.38 }, level[5] { 0.5, 0.3, 0.2, 0.12, 0.08 }, decay[5] { 1.2, 0.7, 0.45, 0.3, 0.2 };
    for (int i = 0; i < n; ++i)
    {
        const auto t = (double) i / b.rate;
        double y = 0.0;
        for (int k = 0; k < 5; ++k)
            y += level[k] * std::exp (-t / decay[k]) * std::sin (2.0 * 3.14159265 * 440.0 * ratio[k] * t);
        b.l[(std::size_t) i] = (float) (y * std::min (1.0, t / 0.002));
    }
    return b;
}
} // namespace

std::vector<Preset> helixBank()
{
    using namespace w;
    std::vector<Preset> b;
    b.push_back (make ("Init", "Init", { "init", "default" }, {}));
    b.push_back (make ("Wide Supersaw", "Lead", { "supersaw", "unison", "trance", "wide" }, [] (Patch& p) {
        p.table (0, "Analog Saw").osc (0, osc::effectMode, effectNone);
        p.set (unisonMode, unisonClassic).set (unisonAmount, 0.35).x (ext::unisonCount, 12).x (ext::unisonBlend, 0.6);
        p.filter (0, flt::freq, 9000).filter (0, flt::slope, 1).env (0, 0.005, 0.4, 0.8, 0.35).set (volume, 0.3);
        p.cell (tgt::filterFreq, src::macro1, 0.4).cell (tgt::amp, src::velocity, 0.3);
        p.x (ext::reverbOn, 1).x (ext::reverbMix, 0.2).x (ext::delayOn, 1).x (ext::delayMix, 0.15);
    }));
    b.push_back (make ("Reese Bend", "Bass", { "reese", "warp", "dnb", "dark" }, [] (Patch& p) {
        p.table (0, "Analog Saw").table (1, "Analog Saw").osc (0, osc::effectMode, effectNone);
        p.osc (1, osc::on, 1).osc (1, osc::detune, 0.12).osc (1, osc::gain, 0.8);
        p.x (ext::warpMode, warp::bendBoth).x (ext::warpAmount, 0.65);
        p.set (unisonMode, unisonClassic).set (unisonVoices, 4).set (unisonAmount, 0.2).x (ext::unisonWidth, 0.4);
        p.filter (0, flt::freq, 700).filter (0, flt::slope, 1).filter (0, flt::res, 0.3).env (0, 0.003, 0.6, 1.0, 0.25);
        p.set (subOn, 1).set (subGain, 0.45).set (volume, 0.3);
        p.lfo (0, 0, 0.3).cell (tgt::warpAmount, src::lfo1, 0.25);
    }));
    b.push_back (make ("Growl FM", "Bass", { "growl", "fm", "dubstep", "drawn lfo" }, [] (Patch& p) {
        p.table (0, "Shapes").osc (0, osc::position, 0.66).osc (0, osc::effectMode, effectNone);
        p.table (1, "Shapes").osc (1, osc::position, 0.0).osc (1, osc::transpose, 12).osc (1, osc::gain, 0.0);
        p.x (ext::warpMode, warp::fm).x (ext::warpAmount, 0.35);
        p.lfo (0, 0, 1.0, true, 15).draw (0, { { 0.0, -1.0, 0.6 }, { 0.3, 1.0, -0.4 }, { 0.55, -0.4, 0.0 }, { 0.75, 0.8, 0.5 } });
        p.cell (tgt::warpAmount, src::lfo1, 0.35).cell (tgt::filterFreq, src::lfo1, 0.25);
        p.filter (0, flt::freq, 900).filter (0, flt::slope, 1).filter (0, flt::res, 0.4).filter (0, flt::circuitLpHp, 2).filter (0, flt::drive, 6);
        p.set (monoPoly, 0).set (glide, 0.04).env (0, 0.002, 0.5, 1.0, 0.15).set (volume, 0.28);
        p.x (ext::distOn, 1).x (ext::distDrive, 0.35).x (ext::distMix, 0.6);
    }));
    b.push_back (make ("Comb Pluck", "Pluck", { "comb", "pluck", "string", "bright" }, [] (Patch& p) {
        p.table (0, "Analog Square").osc (0, osc::effectMode, effectNone);
        p.x (ext::filterType, xfilter::combPlus).filter (0, flt::freq, 261.63).filter (0, flt::res, 1.1);
        p.cell (tgt::filterFreq, src::note, 1.0); // (the comb follows the keys)
        p.env (0, 0.0, 0.5, 0.0, 0.4).set (volume, 0.35);
        p.x (ext::reverbOn, 1).x (ext::reverbMix, 0.25);
    }));
    b.push_back (make ("Vox Pad", "Pad", { "formant", "vowel", "pad", "choir" }, [] (Patch& p) {
        p.table (0, "Analog Saw").osc (0, osc::effectMode, effectNone);
        p.x (ext::filterType, xfilter::formant).filter (0, flt::freq, 640).filter (0, flt::res, 0.7).filter (0, flt::morph, 0.2);
        p.lfo (0, 0, 0.15).cell (tgt::filterMorph, src::lfo1, 0.5);
        p.set (unisonMode, unisonClassic).set (unisonVoices, 6).set (unisonAmount, 0.25);
        p.env (0, 0.6, 1.0, 1.0, 1.5).set (volume, 0.4);
        p.x (ext::chorusOn, 1).x (ext::reverbOn, 1).x (ext::reverbSize, 0.8).x (ext::reverbMix, 0.35);
    }));
    b.push_back (make ("Octave Stack Pad", "Pad", { "stack", "octaves", "wide", "pad" }, [] (Patch& p) {
        p.table (0, "Glass").osc (0, osc::position, 0.2).osc (0, osc::effectMode, effectNone);
        p.lfo (1, 1, 0.08).cell (tgt::oscPos, src::lfo2, 0.3);
        p.set (unisonMode, unisonClassic).set (unisonAmount, 0.2).x (ext::unisonCount, 8).x (ext::unisonStack, 2).x (ext::unisonBlend, 0.5);
        p.filter (0, flt::freq, 3000).env (0, 1.2, 1.0, 1.0, 2.5).set (volume, 0.35);
        p.x (ext::reverbOn, 1).x (ext::reverbSize, 0.9).x (ext::reverbMix, 0.4);
    }));
    b.push_back (make ("Wobble Draw", "Bass", { "wobble", "drawn lfo", "dubstep", "sub" }, [] (Patch& p) {
        p.table (0, "Analog Saw").osc (0, osc::effectMode, effectNone);
        p.table (1, "Analog Square").osc (1, osc::on, 1).osc (1, osc::transpose, -12).osc (1, osc::gain, 0.6);
        p.lfo (0, 0, 1.0, true, 15).draw (0, { { 0.0, -1.0, 0.0 }, { 0.1, 1.0, 0.7 }, { 0.5, -0.6, 0.0 }, { 0.6, 0.6, 0.7 } });
        p.cell (tgt::filterFreq, src::lfo1, 0.45);
        p.filter (0, flt::freq, 200).filter (0, flt::slope, 1).filter (0, flt::res, 0.5).filter (0, flt::circuitLpHp, 2);
        p.set (subOn, 1).set (subGain, 0.5).env (0, 0.003, 0.4, 1.0, 0.2).set (volume, 0.3);
        p.x (ext::distOn, 1).x (ext::distDrive, 0.25).x (ext::distMix, 0.5);
    }));
    b.push_back (make ("Noise Riser", "FX", { "riser", "noise", "sweep", "build-up" }, [] (Patch& p) {
        p.osc (0, osc::on, 0).x (ext::noiseOn, 1).x (ext::noiseLevel, 0.8).x (ext::noiseColour, 0.7);
        p.filter (0, flt::freq, 300).env (1, 4.0, 1.0, 1.0, 1.0).cell (tgt::filterFreq, src::env2, 0.7);
        p.env (0, 3.0, 1.0, 1.0, 1.5).set (volume, 0.4);
        p.x (ext::reverbOn, 1).x (ext::reverbMix, 0.4).x (ext::delayOn, 1).x (ext::delayTime, 6).x (ext::delayFeedback, 0.4);
    }));
    b.push_back (make ("Phaser Keys", "Keys", { "phaser", "keys", "organ", "warm" }, [] (Patch& p) {
        p.table (0, "Organ Cycle").osc (0, osc::effectMode, effectNone);
        p.filter (1, flt::on, 1).x (ext::filterType + 1, xfilter::phaser).filter (1, flt::freq, 900).filter (1, flt::res, 0.6).filter (1, flt::morph, 0.6);
        p.lfo (1, 0, 0.3).cell (tgt::filterFreq + tgt::filterStride, src::lfo2, 0.2);
        p.env (0, 0.003, 1.0, 0.5, 0.4).set (volume, 0.35);
        p.x (ext::chorusOn, 1).x (ext::chorusMix, 0.4);
    }));
    // --- Helix+ 2 (round 190ml)
    b.push_back (make ("Triple Stack", "Lead", { "supersaw", "three oscillators", "multiband", "anthem" }, [] (Patch& p) {
        p.table (0, "Analog Saw").osc (0, osc::effectMode, effectNone).table (1, "Analog Saw").osc (1, osc::on, 1).osc (1, osc::detune, 0.08);
        p.table3 ("Analog Saw").osc3 (osc::on, 1).osc3 (osc::transpose, 12).osc3 (osc::detune, -0.06).osc3 (osc::gain, 0.6);
        p.set (unisonMode, unisonClassic).set (unisonAmount, 0.3).x (ext::unisonCount, 8).x (ext::unisonBlend, 0.7);
        p.filter (0, flt::freq, 7000).filter (0, flt::slope, 1).env (0, 0.004, 0.5, 0.85, 0.3).set (volume, 0.22);
        p.cell (tgt::filterFreq, src::macro1 + 4, 0.35);
        p.x2 (ext2::mbOn, 1).x2 (ext2::mbDepth, 0.4).x (ext::reverbOn, 1).x (ext::reverbMix, 0.18);
    }));
    b.push_back (make ("Formula Bell", "Keys", { "formula", "user table", "bell", "glassy" }, [] (Patch& p) {
        p.osc (0, osc::effectMode, effectNone).x2 (ext2::userTable, 1).osc (0, osc::position, 0.0);
        p.env (1, 0.0, 1.2, 0.0, 1.0).cell (tgt::oscPos, src::env2, 0.8);
        p.env (0, 0.001, 2.2, 0.0, 1.6).set (volume, 0.35);
        p.x (ext::delayOn, 1).x (ext::delayMix, 0.18).x (ext::reverbOn, 1).x (ext::reverbMix, 0.25);
    }, [] {
        w::HelixUserData d;
        d.tables[0] = formulaTable ("sin(pi*x + (0.2 + 2.5*(1-t)) * sin(3*pi*x)) * (0.7 + 0.3*cos(pi*x*t))", "Formula Bell");
        return d;
    }));
    b.push_back (make ("Organ Harmonics", "Keys", { "harmonics", "user table", "organ", "drawbars" }, [] (Patch& p) {
        p.osc (0, osc::effectMode, effectNone).x2 (ext2::userTable, 1).x (ext::macro, 0.0).cell (tgt::oscPos, src::macro1, 1.0);
        p.env (0, 0.005, 0.2, 1.0, 0.12).set (volume, 0.3).x (ext::chorusOn, 1).x (ext::chorusMix, 0.35);
    }, [] {
        w::HelixUserData d; // (drawbar-like sets of harmonics, from mellow to full as the position moves)
        d.tables[0] = partialTable ("Organ Harmonics", 16, [] (int h, double t) {
            const bool bar = h == 1 || h == 2 || h == 3 || h == 4 || h == 6 || h == 8 || h == 10 || h == 12 || h == 16;
            return bar ? (h <= 2 ? 0.8 : (0.15 + 0.6 * t) / std::sqrt ((double) h)) : 0.0;
        });
        return d;
    }));
    b.push_back (make ("Chaos Drift Pad", "Pad", { "chaos", "lorenz", "evolving", "ambient" }, [] (Patch& p) {
        p.table (0, "Glass").osc (0, osc::effectMode, effectNone).table3 ("Vowels").osc3 (osc::on, 1).osc3 (osc::gain, 0.5).osc3 (osc::transpose, 7);
        p.x2 (ext2::chaosMode, 1).x2 (ext2::chaosRate, 0.15).x2 (ext2::chaosMode + ext2::chaosStride, 0).x2 (ext2::chaosRate + ext2::chaosStride, 0.4);
        p.cell (tgt::oscPos, src::chaos1, 0.45).cell (tgt::osc3 + tgt::oscPos, src::chaos1 + 1, 0.5).cell (tgt::filterFreq, src::chaos1 + 1, 0.12);
        p.set (unisonMode, unisonClassic).set (unisonVoices, 4).set (unisonAmount, 0.2);
        p.filter (0, flt::freq, 3500).env (0, 0.5, 1.0, 1.0, 2.0).set (volume, 0.3);
        p.x (ext::reverbOn, 1).x (ext::reverbSize, 0.9).x (ext::reverbMix, 0.4);
    }));
    b.push_back (make ("Shifted Bells", "FX", { "frequency shifter", "bells", "metallic", "inharmonic" }, [] (Patch& p) {
        p.table (0, "Shapes").osc (0, osc::effectMode, effectFm).osc (0, osc::effect1, 0.5).osc (0, osc::effect2, 0.35);
        p.env (0, 0.0, 1.5, 0.0, 1.2).set (volume, 0.35);
        p.x2 (ext2::shiftOn, 1).x2 (ext2::shift, 0.28).x2 (ext2::shiftMix, 0.6).x (ext::delayOn, 1).x (ext::delayMix, 0.25);
        p.lfo (1, 0, 0.2).cell (tgt::shift, src::lfo2, 0.1);
    }));
    b.push_back (make ("OTT Growl", "Bass", { "multiband", "growl", "dubstep", "eq" }, [] (Patch& p) {
        p.table (0, "Analog Saw").osc (0, osc::effectMode, effectNone).x (ext::warpMode, warp::sync).x (ext::warpAmount, 0.35);
        p.lfo (0, 0, 1.0, true, 15).cell (tgt::warpAmount, src::lfo1, 0.3).cell (tgt::filterFreq, src::lfo1, 0.2);
        p.filter (0, flt::freq, 1400).filter (0, flt::slope, 1).filter (0, flt::res, 0.35).filter (0, flt::circuitLpHp, 2);
        p.set (monoPoly, 0).set (glide, 0.03).set (subOn, 1).set (subGain, 0.4).env (0, 0.002, 0.5, 1.0, 0.12).set (volume, 0.26);
        p.x (ext::distOn, 1).x (ext::distDrive, 0.3).x (ext::distMix, 0.5);
        p.x2 (ext2::eqOn, 1).x2 (ext2::eqLow, 3.0).x2 (ext2::eqMidFreq, 450).x2 (ext2::eqMid, -4.0).x2 (ext2::eqHigh, 2.0);
        p.x2 (ext2::mbOn, 1).x2 (ext2::mbDepth, 0.7).x2 (ext2::mbUp, 0.7).x2 (ext2::mbDown, 0.7);
    }));
    b.push_back (make ("Vowel Sync", "Lead", { "sample", "sync", "vocal", "talking" }, [] (Patch& p) {
        p.osc (0, osc::on, 0).x2 (ext2::sampleOn, 1).x2 (ext2::sampleLoop, 1).x2 (ext2::sampleRoot, 45).x2 (ext2::sampleStart, 0.1);
        p.x2 (ext2::sampleWarpMode, warp::sync).x2 (ext2::sampleWarpAmount, 0.25);
        p.set (monoPoly, 0).set (glide, 0.05).env (0, 0.01, 0.5, 1.0, 0.25).set (volume, 0.35);
        p.filter (0, flt::freq, 5000).x (ext::chorusOn, 1).x (ext::chorusMix, 0.3);
    }, [] {
        w::HelixUserData d;
        d.sampleName = "LYNX Vowel (made in Helix)";
        d.sample = vowelSample();
        return d;
    }));
    b.push_back (make ("Hit Pluck", "Pluck", { "sample", "transient", "pluck", "layered" }, [] (Patch& p) {
        p.table (0, "Analog Square").osc (0, osc::effectMode, effectNone).osc (0, osc::gain, 0.7);
        p.x2 (ext2::sampleOn, 1).x2 (ext2::sampleRoot, 60).x2 (ext2::sampleGain, 0.6);
        p.filter (0, flt::freq, 1800).env (1, 0.0, 0.25, 0.0, 0.2).cell (tgt::filterFreq, src::env2, 0.35);
        p.env (0, 0.0, 0.45, 0.0, 0.3).set (volume, 0.35).x (ext::reverbOn, 1).x (ext::reverbMix, 0.2);
    }, [] {
        w::HelixUserData d;
        d.sampleName = "LYNX Hit (made in Helix)";
        d.sample = hitSample();
        return d;
    }));
    b.push_back (make ("Macro Morph", "Pad", { "macros", "morph", "performance", "wide" }, [] (Patch& p) {
        p.table (0, "Vowels").osc (0, osc::effectMode, effectModern).table3 ("Glass").osc3 (osc::on, 1).osc3 (osc::gain, 0.0);
        p.cell (tgt::oscPos, src::macro1 + 4, 1.0).cell (tgt::osc3 + tgt::oscGain, src::macro1 + 5, 1.0);
        p.cell (tgt::oscFx1, src::macro1 + 6, 0.6).cell (tgt::filterFreq, src::macro1 + 7, 0.5);
        p.x2 (ext2::macro5, 0.3).x2 (ext2::macro5 + 1, 0.5).x2 (ext2::macro5 + 3, 0.4);
        p.set (unisonMode, unisonClassic).set (unisonAmount, 0.25).x (ext::unisonCount, 6).x (ext::unisonWidth, 0.8);
        p.filter (0, flt::freq, 2500).env (0, 0.8, 1.0, 1.0, 2.0).set (volume, 0.3).x (ext::reverbOn, 1).x (ext::reverbMix, 0.3);
    }));
    b.push_back (make ("Clean Sync Lead", "Lead", { "sync", "anti-aliased", "bright", "classic" }, [] (Patch& p) {
        p.table (0, "Analog Saw").osc (0, osc::effectMode, effectNone).x (ext::warpMode, warp::sync).x (ext::warpAmount, 0.2);
        p.env (1, 0.0, 0.8, 0.2, 0.5).cell (tgt::warpAmount, src::env2, 0.5);
        p.table3 ("Analog Square").osc3 (osc::on, 1).osc3 (osc::transpose, -12).osc3 (osc::gain, 0.45).x2 (ext2::osc3WarpMode, warp::pwm)
            .x2 (ext2::osc3WarpAmount, 0.3);
        p.set (monoPoly, 0).set (glide, 0.04).filter (0, flt::freq, 8000).env (0, 0.003, 0.5, 0.8, 0.2).set (volume, 0.25);
        p.x (ext::delayOn, 1).x (ext::delayMix, 0.15);
    }));
    b.push_back (make ("Random Keys", "Keys", { "chaos", "smooth random", "lofi", "wobbly" }, [] (Patch& p) {
        p.table (0, "Organ Cycle").osc (0, osc::effectMode, effectNone).x (ext::warpMode, warp::crush).x (ext::warpAmount, 0.35);
        p.x2 (ext2::chaosMode, 0).x2 (ext2::chaosRate, 2.0).cell (tgt::oscPitch, src::chaos1, 0.004).cell (tgt::oscPan, src::chaos1, 0.3);
        p.env (0, 0.004, 1.0, 0.6, 0.35).set (volume, 0.32).x (ext::chorusOn, 1).x2 (ext2::eqOn, 1).x2 (ext2::eqHigh, -6.0);
    }));
    b.push_back (make ("Drawn Pulse Bass", "Bass", { "user table", "drawn", "pulse", "sub" }, [] (Patch& p) {
        p.osc (0, osc::effectMode, effectNone).x2 (ext2::userTable, 1).lfo (0, 1, 0.25).cell (tgt::oscPos, src::lfo1, 0.5).osc (0, osc::position, 0.5);
        p.set (subOn, 1).set (subGain, 0.5).filter (0, flt::freq, 1200).filter (0, flt::slope, 1).set (monoPoly, 0);
        p.env (0, 0.002, 0.4, 1.0, 0.15).set (volume, 0.3).x2 (ext2::mbOn, 1).x2 (ext2::mbDepth, 0.3);
    }, [] {
        w::HelixUserData d; // (a pulse whose width the position sweeps, drawn by a formula)
        d.tables[0] = formulaTable ("p < 0.08 + 0.84*t ? 0.9 : -0.9", "Drawn Pulse");
        return d;
    }));
    // --- Helix round 3 (round 190mt): the granular and spectral oscillators on LYNX-made samples (their partials kept)
    b.push_back (make ("Grain Cloud", "Pad", { "granular", "cloud", "ambient", "evolving" }, [] (Patch& p) {
        p.osc (0, osc::on, 0).x2 (ext2::sampleRoot, 45);
        p.x3 (ext3::grainOn, 1).x3 (ext3::grainPosition, 0.35).x3 (ext3::grainSize, 0.14).x3 (ext3::grainDensity, 28).x3 (ext3::grainSpray, 0.3)
            .x3 (ext3::grainSpread, 0.85);
        p.lfo (0, 1, 0.07).cell (tgt::grainPos, src::lfo1, 0.25).cell (tgt::grainSize, src::macro1, 0.5);
        p.env (0, 0.9, 1.0, 1.0, 2.5).set (volume, 0.32).filter (0, flt::freq, 6500);
        p.x (ext::chorusOn, 1).x (ext::chorusMix, 0.35).x (ext::reverbOn, 1).x (ext::reverbSize, 0.8).x (ext::reverbMix, 0.35);
    }, [] {
        w::HelixUserData d;
        d.sampleName = "LYNX Vowel (made in Helix)";
        d.sample = vowelSample();
        return d;
    }));
    b.push_back (make ("Spectral Glass", "Keys", { "spectral", "glass", "bell", "inharmonic" }, [] (Patch& p) {
        p.osc (0, osc::on, 0).x2 (ext2::sampleRoot, 69);
        p.x3 (ext3::specOn, 1).x3 (ext3::specStretch, 0.12).x3 (ext3::specTilt, -0.25).x3 (ext3::specPartials, 16);
        p.env (1, 0.0, 1.4, 0.0, 1.0).cell (tgt::specPos, src::env2, 0.8).cell (tgt::specStretch, src::velocity, 0.15);
        p.env (0, 0.002, 1.8, 0.0, 1.2).set (volume, 0.4).filter (0, flt::freq, 12000);
        p.x (ext::delayOn, 1).x (ext::delayMix, 0.18).x (ext::reverbOn, 1).x (ext::reverbMix, 0.25);
    }, [] {
        w::HelixUserData d;
        d.sampleName = "LYNX Glass (made in Helix)";
        d.sample = glassSample();
        d.spectral = w::analyseSpectrum (d.sample);
        return d;
    }));
    b.push_back (make ("Frozen Choir", "Pad", { "spectral", "granular", "choir", "frozen" }, [] (Patch& p) {
        p.osc (0, osc::on, 0).x2 (ext2::sampleRoot, 45);
        p.x3 (ext3::specOn, 1).x3 (ext3::specPosition, 0.5).x3 (ext3::specShift, 0.12).x3 (ext3::specPartials, 40).x3 (ext3::specGain, 0.8);
        p.x3 (ext3::grainOn, 1).x3 (ext3::grainPosition, 0.6).x3 (ext3::grainSize, 0.25).x3 (ext3::grainDensity, 12).x3 (ext3::grainSpray, 0.15)
            .x3 (ext3::grainPitch, 12).x3 (ext3::grainGain, 0.35);
        p.lfo (0, 0, 0.11).cell (tgt::specPos, src::lfo1, 0.3).cell (tgt::specBand, src::macro1, 0.4).x3 (ext3::specBand, 900).x3 (ext3::specWidth, 6);
        p.env (0, 1.2, 1.0, 1.0, 3.0).set (volume, 0.3).x (ext::reverbOn, 1).x (ext::reverbSize, 0.9).x (ext::reverbMix, 0.4);
    }, [] {
        w::HelixUserData d;
        d.sampleName = "LYNX Vowel (made in Helix)";
        d.sample = vowelSample();
        d.spectral = w::analyseSpectrum (d.sample);
        return d;
    }));
    // --- Helix round 4 (round 190oc): unison on the granular and spectral oscillators, reverse grains
    b.push_back (make ("Grain Swarm", "Pad", { "granular", "unison", "reverse", "wide" }, [] (Patch& p) {
        p.osc (0, osc::on, 0).x2 (ext2::sampleRoot, 45);
        p.x3 (ext3::grainOn, 1).x3 (ext3::grainPosition, 0.45).x3 (ext3::grainSize, 0.12).x3 (ext3::grainDensity, 14).x3 (ext3::grainSpray, 0.25)
            .x3 (ext3::grainSpread, 0.3);
        p.x4 (ext4::grainUnison, 5).x4 (ext4::grainDetune, 0.3).x4 (ext4::grainWidth, 0.9).x4 (ext4::grainReverse, 0.35);
        p.lfo (0, 1, 0.05).cell (tgt::grainPos, src::lfo1, 0.2).cell (tgt::grainDensity, src::macro1, 0.4);
        p.env (0, 0.08, 1.0, 1.0, 2.0).set (volume, 0.32).filter (0, flt::freq, 7000);
        p.x (ext::reverbOn, 1).x (ext::reverbSize, 0.7).x (ext::reverbMix, 0.3);
    }, [] {
        w::HelixUserData d;
        d.sampleName = "LYNX Vowel (made in Helix)";
        d.sample = vowelSample();
        return d;
    }));
    b.push_back (make ("Spectral Ensemble", "Pad", { "spectral", "unison", "glass", "wide" }, [] (Patch& p) {
        p.osc (0, osc::on, 0).x2 (ext2::sampleRoot, 69);
        p.x3 (ext3::specOn, 1).x3 (ext3::specStretch, 0.06).x3 (ext3::specTilt, -0.15).x3 (ext3::specPartials, 24).x3 (ext3::specBand, 1400)
            .x3 (ext3::specWidth, 5);
        p.x4 (ext4::specUnison, 4).x4 (ext4::specDetune, 0.25).x4 (ext4::specWidth, 0.8);
        p.lfo (0, 0, 0.09).cell (tgt::specPos, src::lfo1, 0.25).cell (tgt::specBand, src::macro1, 0.4);
        p.env (0, 0.4, 1.0, 1.0, 1.8).set (volume, 0.3).x (ext::chorusOn, 1).x (ext::chorusMix, 0.25).x (ext::reverbOn, 1).x (ext::reverbMix, 0.3);
    }, [] {
        w::HelixUserData d;
        d.sampleName = "LYNX Glass (made in Helix)";
        d.sample = glassSample();
        d.spectral = w::analyseSpectrum (d.sample);
        return d;
    }));
    return b;
}
} // namespace lynx::banks
