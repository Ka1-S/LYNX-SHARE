// LYNX's own GMS bank (round 190rh). Designed for LYNX on the documented parameters (REA_rodada39 §1.2–1.13) and state
// layout ("SSLF" + size + "Syn4": seven 512-byte path slots, three editor integers, 212 parameters, a 512-byte name); no FL
// preset was read for it. The wave names are GMS' own, so FL's waves play on a channel that has them.

#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "core/Gms.h"
#include "core/LynxBanks.h"
#include "core/NativeData.h"

namespace lynx::banks
{
namespace
{
enum Param
{
    level = 0, pan = 1, pitch = 2, attack = 4, decay = 5, sustain = 6, release = 7, ampLevel = 8, slideTime = 10, slide = 11, cutoff = 12,
    resonance = 13, keyTrack = 14, filterType = 15, env1 = 16, env2 = 17, env1Inv = 18, env2Inv = 19, env1Atk = 20, env2Atk = 21,
    env1Dec = 22, env2Dec = 23, env1Dest = 24, env2Dest = 25, lfo1Retrig = 26, lfo2Retrig = 27, lfo1 = 28, lfo2 = 29, lfo1Shape = 32,
    lfo2Shape = 33, lfo1Rate = 34, lfo2Rate = 35, lfo1Sync = 36, lfo2Sync = 37, lfo1Dest = 38, lfo2Dest = 39, mono = 40, unison = 41,
    uniDetune = 42, uniStereo = 43, octave = 44, noise = 45, uniPhase = 46, uniRetrig = 47, osc2Mix = 48, osc3Mix = 49, modulation = 50,
    modKind = 51, osc2Phase = 52, osc2Retrig = 53, osc1Pitch = 55, osc1Cents = 56, osc2Pitch = 57, osc2Cents = 58, osc3Pitch = 59,
    osc3Cents = 60, velAmount = 198, velDest = 199, velLevel = 200, bend = 205
};
// destinations (int (3p)) and other choices
constexpr float toCutoff = 0.0f, toMod = 0.4f, toPitch = 0.75f, toVolume = 1.0f, sync = 0.0f, fm = 0.6f, am = 1.0f;
float semis (int n) { return ((float) n + 36.5f) / 72.0f; }                   // trunc (72·P − 36)
float cents (int n) { return ((float) n + 50.0f + (n < 0 ? -0.5f : 0.5f)) / 100.0f; }
constexpr float voices (int n) { return ((float) (n - 1) + 0.2f) / 15.0f; }   // 1 − trunc (−15·P)
enum Fx { distortion, crusher, flanger, phaser, lowPass, highPass, delayFx, reverbFx, panner, trancer };

struct Value
{
    int index;
    float v;
};
struct Waves
{
    const char *one = "Saw", *two = "Saw", *three = "Sine";
};
struct FxOn
{
    Fx fx;
    float x, y;
};

std::vector<std::uint8_t> stateOf (const std::string& name, const Waves& waves, const std::array<float, 212>& p)
{
    std::vector<std::uint8_t> s { 'S', 'S', 'L', 'F' };
    const auto put = [&s] (std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            s.push_back ((std::uint8_t) (v >> (8 * i)));
    };
    const auto text = [&s] (const std::string& t, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i)
            s.push_back (i < t.size() && i + 1 < size ? (std::uint8_t) t[i] : (std::uint8_t) 0);
    };
    put (4 + 7 * 512 + 12 + 4 * 212 + 512);
    put (0x53796E34u);
    for (const auto* w : { waves.one, waves.two, waves.three })
        text (std::string (w) + ".wav", 512);
    for (int i = 0; i < 4; ++i)
        text ({}, 512);
    for (int i = 0; i < 3; ++i)
        put (0);
    for (const auto v : p)
    {
        std::uint32_t bits;
        std::memcpy (&bits, &v, 4);
        put (bits);
    }
    text (name, 512);
    return s;
}

Preset make (const char* name, const char* category, Waves waves, std::initializer_list<Value> values, std::initializer_list<FxOn> effects = {},
             std::vector<std::string> tags = {})
{
    std::array<float, 212> p {};
    p[level] = 0.8f, p[pan] = 0.5f, p[pitch] = 0.5f, p[attack] = 0.03f, p[decay] = 0.4f, p[sustain] = 0.8f, p[release] = 0.25f, p[ampLevel] = 0.58f;
    p[slideTime] = 0.3f, p[cutoff] = 0.75f, p[resonance] = 0.15f, p[keyTrack] = 0.3f, p[env1Dec] = 0.35f, p[env2Dec] = 0.35f;
    p[lfo1Rate] = p[lfo2Rate] = 0.5f, p[uniDetune] = 0.4f, p[uniStereo] = 0.7f, p[octave] = 0.5f, p[osc2Mix] = 0.6f;
    for (int i : { osc1Pitch, osc2Pitch, osc3Pitch })
        p[(std::size_t) i] = semis (0);
    for (int i : { osc1Cents, osc2Cents, osc3Cents })
        p[(std::size_t) i] = cents (0);
    for (int i : { 198, 201, 203 })
        p[(std::size_t) i] = 0.5f; // (matrix amounts: none)
    for (int i : { 199, 202, 204 })
        p[(std::size_t) i] = -1.0f;
    p[velLevel] = 1.0f, p[bend] = 0.2f;
    for (int i = 207; i < 212; ++i)
        p[(std::size_t) i] = 0.5f;
    for (int k = 0; k < 10; ++k)
        p[(std::size_t) (177 + k)] = 0.5f; // (no effect LFO)
    for (const auto& v : values)
        p[(std::size_t) v.index] = v.v;
    for (const auto& f : effects)
    {
        p[(std::size_t) (147 + f.fx)] = 1.0f;
        p[(std::size_t) (117 + f.fx)] = f.x;
        p[(std::size_t) (127 + f.fx)] = f.y;
    }
    Preset out;
    out.name = name;
    out.category = category;
    out.tags = std::move (tags);
    out.apply = [p, waves, n = std::string (name)] (ChannelDef& c) {
        c.pluginState = stateOf (n, waves, p);
        c.modelParams.clear(); // (the desktop plugin)
        if (c.samplePath.empty())
            c.samplePath = nativedata::ownPath (InstrumentKind::gms);
    };
    return out;
}
} // namespace

std::vector<Preset> gmsBank()
{
    const auto two = cents (9), down = cents (-9);
    const auto v4 = voices (4), v7 = voices (7), v16 = voices (16);
    return {
        make ("Two Saws", "Lead", {}, { { osc2Cents, two } }, {}, { "saw" }),
        make ("Stack Lead", "Lead", {}, { { unison, v7 }, { uniDetune, 0.45f }, { osc2Cents, two } }, { { delayFx, 0.4f, 0.35f } }, { "supersaw" }),
        make ("Sync Lead", "Lead", {}, { { modKind, sync }, { modulation, 0.3f }, { osc2Mix, 0.0f }, { env1, 0.7f }, { env1Dest, toMod }, { env1Dec, 0.45f } }, {}, { "sync" }),
        make ("Square Solo", "Lead", { "Square", "Square", "Sine" }, { { mono, 1.0f }, { slide, 1.0f }, { osc2Pitch, semis (12) }, { osc2Mix, 0.4f } }, { { delayFx, 0.45f, 0.4f } }, { "mono" }),
        make ("Retro Lead", "Lead", { "Saw Retro", "Square Retro", "Sine" }, { { osc2Cents, down }, { lfo1, 0.2f }, { lfo1Dest, toPitch }, { lfo1Rate, 0.55f } }, {}, { "vibrato" }),
        make ("Mean Lead", "Lead", { "Saw Mean", "Saw", "Sine" }, { { osc2Cents, two }, { resonance, 0.4f }, { cutoff, 0.6f } }, { { distortion, 0.4f, 0.5f } }, { "distortion" }),
        make ("Round Bass", "Bass", { "Saw", "Square", "Sine" }, { { octave, 0.25f }, { cutoff, 0.45f }, { env1, 0.5f }, { env1Dec, 0.3f }, { sustain, 0.6f } }),
        make ("Sub Layer", "Bass", { "Square Smooth", "Sine", "Smooth Sub" }, { { octave, 0.25f }, { osc3Mix, 0.6f }, { cutoff, 0.5f } }, {}, { "sub" }),
        make ("Wide Reese", "Bass", {}, { { octave, 0.25f }, { unison, v4 }, { uniDetune, 0.55f }, { cutoff, 0.5f } }, {}, { "detuned" }),
        make ("FM Bass", "Bass", { "Sine", "Sine", "Sine" }, { { octave, 0.25f }, { modKind, fm }, { modulation, 0.35f }, { osc2Mix, 0.0f }, { env1, 0.5f }, { env1Dest, toMod }, { env1Dec, 0.3f } }, {}, { "fm" }),
        make ("Wobble", "Bass", { "Saw", "Saw", "Sine" }, { { octave, 0.25f }, { cutoff, 0.35f }, { resonance, 0.45f }, { lfo1, 0.65f }, { lfo1Sync, 0.6f }, { lfo1Rate, 0.6f } }, {}, { "lfo" }),
        make ("Growl", "Bass", { "Throat", "Vocal", "Sine" }, { { octave, 0.25f }, { mono, 1.0f }, { modKind, fm }, { modulation, 0.3f }, { lfo1, 0.5f }, { lfo1Dest, toMod } }, { { distortion, 0.35f, 0.5f } }, { "growl" }),
        make ("Wide Pad", "Pad", {}, { { unison, v7 }, { attack, 0.7f }, { release, 0.6f }, { cutoff, 0.6f }, { sustain, 1.0f } }, { { reverbFx, 0.6f, 0.4f } }, { "slow" }),
        make ("Silk Pad", "Pad", { "Silk", "String", "Sine" }, { { unison, v4 }, { attack, 0.65f }, { release, 0.6f }, { sustain, 1.0f } }, { { reverbFx, 0.6f, 0.45f }, { phaser, 0.4f, 0.4f } }, { "soft" }),
        make ("Choir Air", "Pad", { "Vocal", "Vocal", "Sine" }, { { unison, v4 }, { osc2Pitch, semis (12) }, { attack, 0.65f }, { release, 0.6f }, { noise, 0.2f } }, { { reverbFx, 0.7f, 0.5f } }, { "voice" }),
        make ("Sweep Pad", "Pad", {}, { { unison, v4 }, { attack, 0.6f }, { release, 0.6f }, { cutoff, 0.35f }, { env1, 0.7f }, { env1Atk, 0.5f }, { env1Dec, 0.8f } }, { { reverbFx, 0.5f, 0.4f } }, { "filter" }),
        make ("Pluck Saw", "Pluck", {}, { { unison, v4 }, { cutoff, 0.4f }, { env1, 0.65f }, { env1Dec, 0.25f }, { decay, 0.45f }, { sustain, 0.0f } }, { { delayFx, 0.4f, 0.35f } }),
        make ("Keys Pluck", "Pluck", { "Keys", "Rhodes", "Sine" }, { { decay, 0.5f }, { sustain, 0.0f }, { release, 0.4f } }, { { reverbFx, 0.4f, 0.25f } }),
        make ("Strat Pluck", "Pluck", { "Strat", "String", "Sine" }, { { cutoff, 0.55f }, { env1, 0.5f }, { env1Dec, 0.3f }, { decay, 0.55f }, { sustain, 0.0f } }),
        make ("Soft Keys", "Keys", { "Rhodes", "Sine", "Sine" }, { { decay, 0.6f }, { sustain, 0.3f }, { release, 0.4f } }, { { panner, 0.3f, 0.4f } }),
        make ("Glass Keys", "Keys", { "Sine", "Sine", "Sine" }, { { modKind, fm }, { modulation, 0.25f }, { osc2Pitch, semis (19) }, { osc2Mix, 0.0f }, { decay, 0.6f }, { sustain, 0.2f }, { release, 0.5f } }, { { reverbFx, 0.5f, 0.3f } }, { "fm" }),
        make ("Organ", "Keys", { "Funky", "Sine", "Sine" }, { { osc2Pitch, semis (12) }, { osc3Pitch, semis (-12) }, { osc3Mix, 0.4f }, { sustain, 1.0f } }, {}, { "organ" }),
        make ("Huge Stack", "Stab", {}, { { unison, v16 }, { uniDetune, 0.55f }, { uniStereo, 1.0f }, { osc2Pitch, semis (12) }, { osc2Mix, 0.5f } }, { { reverbFx, 0.4f, 0.3f } }, { "supersaw" }),
        make ("Noise Riser", "FX", {}, { { noise, 1.0f }, { cutoff, 0.3f }, { resonance, 0.5f }, { env1, 0.8f }, { env1Atk, 0.75f }, { env1Dec, 0.9f }, { release, 0.5f } }, {}, { "noise" }),
    };
}
} // namespace lynx::banks
