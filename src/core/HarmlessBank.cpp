// LYNX's own Harmless presets (round 168o): designed for LYNX on Harmless's engine (core/Harmless.h), clean-room — no
// FL preset was read or converted. Values are the plugin's raw parameter numbers (core/ModelParamsSynths.cpp names):
// every preset starts from FL's untouched patch (saw, open filter, no modulation, effects off) and sets what it changes.
// Times (REA_rodada32): attack (101^x − 1)·0.05 s, decay (1001^x − 1)·0.12 + 0.05 s (1000 = hold), release
// (101^x − 1)·0.025 + 0.01 s, pluck (1001^x − 1)·0.12 s (1000 = off), delay time p95/192 beat; x = value/1000.

#include <initializer_list>
#include <utility>

#include "core/Harmless.h"
#include "core/LynxBanks.h"
#include "core/ModelParamsSynths.h"

namespace lynx::banks
{
namespace
{
using Values = std::initializer_list<std::pair<int, float>>;

enum : int
{
    phaseRand = 0, lowProtect = 1, sub1 = 2, sub3 = 3, sub4 = 4, eq1 = 5, mask1 = 11, maskMix = 23, maskLfo = 24, trem = 25,
    att = 26, dec = 27, rel = 28, velAtt = 29, pluck = 31, pluckVel = 32, pluckAlt = 33, pluckMask = 34, vol = 36, velVol = 37,
    pitch = 38, detune = 39, grit = 40, vib = 41, pitchLfo = 42, porta = 43, mono = 44, portaTime = 45, adaptive = 47,
    fAtt = 48, fDec = 49, fAttSlope = 50, fDecSlope = 51, fEnv = 52, velFEnv = 53, freq = 54, width = 56, keyTrack = 57, fLfo = 58,
    res = 59, resOfs = 61, selfOsc = 62, uniOrder = 65, uniPan = 66, uniPitch = 67, uniPhase = 68, uniVar = 69, uniType = 70,
    phMix = 71, phWidth = 73, phWidthLfo = 74, phOfs = 75, phOfsLfo = 76, phSpeed = 77, hzMix = 79, hzWidth = 80, hzStrength = 83,
    hzType = 84, hzPos = 85, lfoShape = 86, lfoGlobal = 87, lfoAtt = 88, lfoSpeed = 89, chType = 90, chMix = 91, delType = 92,
    delVol = 93, delFb = 94, delTime = 95, revType = 96, revWet = 97,
    filterType = harmlessComboIndex + 0, resType = harmlessComboIndex + 1, phaserType = harmlessComboIndex + 2,
    colour = harmlessComboIndex + 4
};

Preset make (const char* name, const char* category, std::vector<std::string> tags, Values values)
{
    std::vector<std::pair<int, float>> set (values);
    return { name, category, std::move (tags), [set] (ChannelDef& c) {
                c.modelParams = harmlessDefaultModelParams();
                for (const auto& [index, value] : set)
                    if (index >= 0 && index < (int) c.modelParams.size())
                        c.modelParams[(std::size_t) index] = value;
            } };
}

void mask (std::vector<std::pair<int, float>>& v, std::initializer_list<float> levels)
{
    int i = 0;
    for (const auto l : levels)
        v.push_back ({ mask1 + i++, l });
}
} // namespace

std::vector<Preset> harmlessBank()
{
    std::vector<Preset> b;
    b.push_back (make ("Init Saw", "Init", { "init", "saw" }, { { rel, 250 } }));

    // --- bass
    b.push_back (make ("Round Sub Bass", "Bass", { "sub", "mono", "dark" },
                       { { sub1, 900 }, { freq, 2600 }, { width, 600 }, { fEnv, 300 }, { fDec, 260 }, { mono, 1 }, { porta, 1 },
                         { portaTime, 300 }, { rel, 180 }, { vol, 900 }, { velVol, 1 } }));
    b.push_back (make ("Rubber Pluck Bass", "Bass", { "pluck", "punchy" },
                       { { vol, 1200 }, { freq, 2200 }, { fEnv, 700 }, { fDec, 230 }, { fDecSlope, -400 }, { res, 300 }, { pluck, 380 },
                         { dec, 420 }, { rel, 160 }, { mono, 1 }, { velFEnv, 1 } }));
    b.push_back (make ("Square Growl", "Bass", { "square", "gritty" },
                       { { colour, 5 }, { freq, 3000 }, { fEnv, 450 }, { fDec, 300 }, { res, 450 }, { grit, 350 }, { rel, 200 },
                         { mono, 1 } }));
    b.push_back (make ("Acid Squelch", "Bass", { "acid", "resonant" },
                       { { vol, 760 }, { freq, 1900 }, { width, 350 }, { fEnv, 820 }, { fDec, 280 }, { res, 780 }, { selfOsc, 300 }, { dec, 1000 },
                         { rel, 120 }, { mono, 1 }, { porta, 1 }, { portaTime, 200 }, { velFEnv, 1 } }));
    // --- lead
    b.push_back (make ("Supersaw Lead", "Lead", { "unison", "bright" },
                       { { uniOrder, 7 }, { uniPitch, 150 }, { uniPan, 128 }, { freq, 6400 }, { rel, 300 }, { vib, 120 },
                         { chType, 1 }, { chMix, 300 }, { delType, 2 }, { delVol, 300 }, { delFb, 350 }, { delTime, 144 },
                         { vol, 1100 } }));
    b.push_back (make ("Glide Mono Lead", "Lead", { "mono", "portamento" },
                       { { colour, 1 }, { freq, 5200 }, { fEnv, 300 }, { fDec, 400 }, { res, 250 }, { mono, 1 }, { porta, 1 },
                         { portaTime, 380 }, { vib, 220 }, { lfoSpeed, 3800 }, { rel, 260 }, { delType, 1 }, { delVol, 250 },
                         { delFb, 300 }, { delTime, 96 } }));
    b.push_back (make ("Hollow Square Lead", "Lead", { "square", "mask" },
                       { { colour, 6 }, { maskMix, 700 }, { freq, 5600 }, { res, 200 }, { vib, 160 }, { rel, 280 },
                         { revType, 2 }, { revWet, 250 } }));
    // --- pad
    {
        Values v { { uniOrder, 5 }, { uniPitch, 130 }, { att, 560 }, { rel, 700 }, { freq, 4300 }, { width, 650 }, { fEnv, 250 },
                   { fAtt, 520 }, { fDec, 640 }, { chType, 1 }, { chMix, 450 }, { revType, 3 }, { revWet, 380 }, { vol, 690 } };
        b.push_back (make ("Velvet Pad", "Pad", { "warm", "unison", "slow" }, v));
    }
    b.push_back (make ("Phaser Choir", "Pad", { "phaser", "airy" },
                       { { att, 420 }, { rel, 650 }, { phMix, 850 }, { phWidth, 2600 }, { phOfs, 300 }, { phSpeed, 1600 },
                         { phaserType, 1 }, { uniOrder, 3 }, { uniPitch, 110 }, { revType, 2 }, { revWet, 350 }, { vol, 615 } }));
    b.push_back (make ("Harmonized Air", "Pad", { "harmonizer", "bright", "wide" },
                       { { att, 380 }, { rel, 720 }, { hzMix, 650 }, { hzWidth, 420 }, { hzStrength, 700 }, { hzType, 1 },
                         { freq, 5800 }, { chType, 2 }, { chMix, 500 }, { revType, 4 }, { revWet, 400 }, { vol, 545 } }));
    {
        std::vector<std::pair<int, float>> v { { maskMix, 900 }, { maskLfo, 500 }, { lfoSpeed, 1500 }, { att, 500 }, { rel, 750 },
                                               { uniOrder, 3 }, { uniPitch, 120 }, { revType, 3 }, { revWet, 420 }, { vol, 715 } };
        mask (v, { 1000, 300, 900, 200, 800, 150, 700, 150, 600, 100, 500, 100 });
        b.push_back ({ "Shimmer Mask", "Pad", { "mask", "moving" }, [v] (ChannelDef& c) {
                          c.modelParams = harmlessDefaultModelParams();
                          for (const auto& [i, x] : v)
                              c.modelParams[(std::size_t) i] = x;
                      } });
    }
    b.push_back (make ("Dark Drone", "Pad", { "dark", "drone", "resonant" },
                       { { colour, 3 }, { att, 650 }, { rel, 800 }, { freq, 2500 }, { res, 600 }, { fLfo, 350 }, { lfoSpeed, 600 },
                         { sub1, 600 }, { uniOrder, 3 }, { uniPitch, 90 }, { revType, 3 }, { revWet, 450 }, { vol, 595 } }));
    // --- pluck
    b.push_back (make ("Glass Pluck", "Pluck", { "pluck", "bright" },
                       { { vol, 1250 }, { pluck, 330 }, { dec, 380 }, { rel, 300 }, { freq, 7000 }, { revType, 2 }, { revWet, 300 },
                         { delType, 2 }, { delVol, 260 }, { delFb, 320 }, { delTime, 144 } }));
    b.push_back (make ("Soft Mallet", "Pluck", { "mallet", "soft" },
                       { { vol, 1250 }, { colour, 6 }, { pluck, 280 }, { pluckAlt, 1 }, { dec, 330 }, { rel, 260 }, { freq, 4400 }, { velFEnv, 1 },
                         { fEnv, 350 }, { fDec, 200 }, { revType, 6 }, { revWet, 280 } }));
    b.push_back (make ("Echo Pluck", "Pluck", { "delay", "rhythmic" },
                       { { vol, 1200 }, { pluck, 300 }, { dec, 300 }, { rel, 220 }, { freq, 4800 }, { fEnv, 400 }, { fDec, 220 }, { res, 300 },
                         { delType, 2 }, { delVol, 450 }, { delFb, 500 }, { delTime, 96 } }));
    // --- keys
    b.push_back (make ("Tine Keys", "Keys", { "electric", "tremolo" },
                       { { vol, 485 }, { colour, 7 }, { pluck, 450 }, { pluckMask, 1 }, { dec, 500 }, { rel, 260 }, { trem, 350 }, { freq, 5000 },
                         { velVol, 1 }, { chType, 1 }, { chMix, 300 } }));
    {
        std::vector<std::pair<int, float>> v { { vol, 390 }, { maskMix, 1000 }, { rel, 120 }, { freq, 6000 }, { vib, 140 }, { lfoSpeed, 4200 },
                                               { chType, 9 }, { chMix, 500 }, { revType, 6 }, { revWet, 220 } };
        // odd harmonics stronger: a drawbar-like organ colour
        mask (v, { 1000, 250, 1000, 200, 800, 150, 700, 100, 500, 80, 400, 60 });
        b.push_back ({ "Drawbar Organ", "Keys", { "organ", "mask" }, [v] (ChannelDef& c) {
                          c.modelParams = harmlessDefaultModelParams();
                          for (const auto& [i, x] : v)
                              c.modelParams[(std::size_t) i] = x;
                      } });
    }
    // --- fx
    b.push_back (make ("Slow Sweep", "FX", { "sweep", "lfo" },
                       { { freq, 2400 }, { res, 650 }, { fLfo, 700 }, { lfoSpeed, -2000 }, { lfoShape, 6 }, { att, 300 }, { rel, 600 },
                         { revType, 3 }, { revWet, 400 }, { vol, 800 } }));
    b.push_back (make ("Noise Wind", "FX", { "noise", "wind" },
                       { { resType, 8 }, { res, 900 }, { resOfs, 0 }, { freq, 3800 }, { fLfo, 500 }, { lfoSpeed, 900 }, { att, 450 },
                         { rel, 700 }, { revType, 4 }, { revWet, 450 }, { vol, 900 } }));
    b.push_back (make ("Grit Riser", "FX", { "riser", "gritty" },
                       { { grit, 600 }, { fEnv, 700 }, { fAtt, 700 }, { fDec, 1000 }, { freq, 1500 }, { res, 500 }, { att, 600 },
                         { rel, 500 }, { uniOrder, 5 }, { uniPitch, 180 }, { revType, 4 }, { revWet, 400 }, { vol, 1400 } }));
    return b;
}
} // namespace lynx::banks
