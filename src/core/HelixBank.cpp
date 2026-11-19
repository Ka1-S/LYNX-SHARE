// LYNX's own Helix presets (round 190mi): designed for LYNX on Helix (core/WavetableSynth.h) and its Helix+ block
// (second warps, unison stacking, noise, comb / formant / phaser filters, macros, drawn LFOs, the effects rack).
// Clean-room: no Serum, Live or other preset was read or converted; LYNX's own tables (core/WavetableTables.cpp).
// The first preset ("Init") is Helix's own default, so a new Helix channel sounds as before.

#include <cmath>
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

Preset make (const char* name, const char* category, std::vector<std::string> tags, const std::function<void (Patch&)>& build)
{
    Patch p;
    if (build)
        build (p);
    return { name, category, std::move (tags), [state = p.v] (ChannelDef& c) { c.modelParams = state; } };
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
    return b;
}
} // namespace lynx::banks
