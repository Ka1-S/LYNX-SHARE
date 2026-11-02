// LYNX's own SimSynth presets (round 168o): designed for LYNX on SimSynth's engine (core/SimSynth.h), clean-room — no
// FL preset was read or converted. Each preset starts from the plugin as FL creates it (simsynth::defaultState) and sets
// FL's host parameters (core/ModelParamsSynths.cpp): knob positions 0…1, coarse pitch in semitones (0 = as played),
// switches, waves 0 pulse, 1 half cosine, 2 triangle, 3 noise, 4 sine; LFO shapes 0 square, 1 saw, 2 triangle, 3 S&H.
// Laws (REA_rodada64 §3.1): oscillator gain (level)², amp attack x³·8 s, decay / release x²·8 s, sustain and cutoff
// linear, filter envelope and LFO amounts bipolar around 0.5.

#include <cmath>
#include <initializer_list>
#include <utility>

#include "core/LynxBanks.h"
#include "core/SimSynth.h"

namespace lynx::banks
{
namespace
{
enum : int
{
    lvl1 = 0, lfo1 = 1, env1 = 2, lvl2 = 3, lfo2 = 4, env2 = 5, lvl3 = 6, lfo3 = 7, env3 = 8, lfoDelay = 9, lfoRate = 10,
    cutoff = 11, emphasis = 12, fEnv = 13, fLfo = 14, high = 15, band = 16, att = 17, dec = 18, sus = 19, rel = 20, vel = 21,
    fAtt = 22, fDec = 23, fSus = 24, fRel = 25, shape1 = 26, coarse1 = 27, fine1 = 28, shape2 = 29, coarse2 = 30, fine2 = 31,
    shape3 = 32, coarse3 = 33, fine3 = 34, keyTrack = 35, chorus = 36, retrigger = 37, trackAmp = 38, lfoOn = 39, filterOn = 40,
    on1 = 41, on2 = 42, on3 = 43, ring = 44, oct1a = 45, oct1b = 46, warm1 = 47, oct2a = 48, oct2b = 49, warm2 = 50, oct3a = 51,
    oct3b = 52, warm3 = 53, wave1 = 54, wave2 = 55, wave3 = 56, lfoShape = 57
};

bool isKnob (int h) { return h < 36 && h != coarse1 && h != coarse2 && h != coarse3; }

// values: knobs as positions 0…1, coarse in semitones from the played key, the rest as the plugin's integers
Preset make (const char* name, const char* category, std::vector<std::string> tags, std::initializer_list<std::pair<int, float>> values)
{
    auto state = simsynth::defaultState();
    // a clean start: the three oscillators centred, the filter open and on, no modulation
    for (const auto& [h, v] : std::initializer_list<std::pair<int, float>> {
             { lvl1, 0.7f }, { lvl2, 0.0f }, { lvl3, 0.0f }, { lfo1, 0.5f }, { lfo2, 0.5f }, { lfo3, 0.5f }, { env1, 0.5f }, { env2, 0.5f },
             { env3, 0.5f }, { fine1, 0.5f }, { fine2, 0.5f }, { fine3, 0.5f }, { fEnv, 0.5f }, { fLfo, 0.5f }, { high, 0.0f }, { band, 0.0f },
             { cutoff, 1.0f }, { emphasis, 0.0f }, { keyTrack, 0.0f }, { vel, 0.5f } })
        simsynth::setHostParam (state, h, (int) std::lround (v * 65535.0f));
    for (int h : { coarse1, coarse2, coarse3 })
        simsynth::setHostParam (state, h, 24);
    for (const auto& [h, v] : std::initializer_list<std::pair<int, int>> { { on1, 1 }, { on2, 0 }, { on3, 0 }, { filterOn, 1 }, { lfoOn, 0 }, { chorus, 0 } })
        simsynth::setHostParam (state, h, v);
    for (const auto& [h, v] : values)
    {
        if (isKnob (h))
            simsynth::setHostParam (state, h, (int) std::lround (v * 65535.0f));
        else if (h == coarse1 || h == coarse2 || h == coarse3)
            simsynth::setHostParam (state, h, 24 + (int) std::lround (v));
        else
            simsynth::setHostParam (state, h, (int) std::lround (v));
    }
    return { name, category, std::move (tags), [state] (ChannelDef& c) { c.pluginState = state; } };
}
} // namespace

std::vector<Preset> simSynthBank()
{
    std::vector<Preset> b;
    b.push_back (make ("Init Pulse", "Init", { "init" }, { { wave1, 0 }, { shape1, 0.5f }, { lvl1, 0.9f }, { att, 0.0f }, { dec, 0.5f }, { sus, 0.8f }, { rel, 0.15f } }));
    // --- bass
    b.push_back (make ("Analog Bass", "Bass", { "analog", "punchy" },
                       { { wave1, 0 }, { shape1, 0.4f }, { on2, 1 }, { wave2, 0 }, { lvl2, 0.55f }, { coarse2, -12 }, { cutoff, 0.32f },
                         { emphasis, 0.35f }, { fEnv, 0.82f }, { fAtt, 0.0f }, { fDec, 0.28f }, { fSus, 0.1f }, { fRel, 0.2f },
                         { att, 0.0f }, { dec, 0.4f }, { sus, 0.6f }, { rel, 0.12f }, { trackAmp, 0 } }));
    b.push_back (make ("Warm Sine Sub", "Bass", { "sub", "round" },
                       { { wave1, 4 }, { shape1, 0.2f }, { lvl1, 0.85f }, { on2, 1 }, { wave2, 2 }, { lvl2, 0.35f }, { coarse2, 12 },
                         { cutoff, 0.45f }, { att, 0.0f }, { dec, 0.5f }, { sus, 0.85f }, { rel, 0.12f } }));
    b.push_back (make ("Resonant Squelch", "Bass", { "acid", "resonant" },
                       { { wave1, 0 }, { shape1, 0.5f }, { cutoff, 0.2f }, { emphasis, 0.75f }, { fEnv, 0.92f }, { fDec, 0.22f },
                         { fSus, 0.0f }, { fRel, 0.15f }, { att, 0.0f }, { dec, 0.5f }, { sus, 0.75f }, { rel, 0.1f } }));
    // --- lead
    b.push_back (make ("Warm Sync Lead", "Lead", { "lead", "warm" },
                       { { wave1, 0 }, { shape1, 0.3f }, { warm1, 1 }, { on2, 1 }, { wave2, 0 }, { lvl2, 0.6f }, { fine2, 0.56f },
                         { cutoff, 0.62f }, { emphasis, 0.25f }, { fEnv, 0.66f }, { fDec, 0.4f }, { fSus, 0.5f }, { lfoOn, 1 },
                         { lfoShape, 2 }, { lfoRate, 0.45f }, { lfoDelay, 0.4f }, { lfo1, 0.53f }, { lfo2, 0.53f }, { att, 0.02f },
                         { dec, 0.5f }, { sus, 0.85f }, { rel, 0.2f }, { chorus, 1 } }));
    b.push_back (make ("Square Chip Lead", "Lead", { "square", "8-bit" },
                       { { wave1, 0 }, { shape1, 0.5f }, { oct1a, 1 }, { cutoff, 0.9f }, { att, 0.0f }, { dec, 0.4f }, { sus, 0.7f },
                         { rel, 0.12f }, { lfoOn, 1 }, { lfoShape, 0 }, { lfoRate, 0.6f }, { lfoDelay, 0.5f }, { lfo1, 0.52f } }));
    b.push_back (make ("Ring Lead", "Lead", { "ring", "metallic" },
                       { { wave1, 2 }, { lvl1, 1.0f }, { on2, 1 }, { wave2, 2 }, { lvl2, 0.95f }, { coarse2, 7 }, { ring, 1 }, { cutoff, 0.7f },
                         { emphasis, 0.2f }, { att, 0.0f }, { dec, 0.45f }, { sus, 0.7f }, { rel, 0.2f } }));
    // --- pad
    b.push_back (make ("String Pad", "Pad", { "strings", "slow" },
                       { { wave1, 0 }, { shape1, 0.25f }, { lvl1, 0.6f }, { warm1, 1 }, { on2, 1 }, { wave2, 0 }, { shape2, 0.35f }, { warm2, 1 },
                         { lvl2, 0.52f }, { fine2, 0.53f }, { cutoff, 0.55f }, { emphasis, 0.1f }, { att, 0.62f }, { dec, 0.6f },
                         { sus, 0.85f }, { rel, 0.45f }, { lfoOn, 1 }, { lfoShape, 2 }, { lfoRate, 0.3f }, { lfo1, 0.52f },
                         { chorus, 1 } }));
    b.push_back (make ("Glass Pad", "Pad", { "airy", "bright" },
                       { { wave1, 4 }, { shape1, 0.6f }, { oct1a, 1 }, { on2, 1 }, { wave2, 1 }, { lvl2, 0.5f }, { coarse2, 12 },
                         { fine2, 0.54f }, { cutoff, 0.6f }, { high, 0.25f }, { att, 0.55f }, { dec, 0.6f }, { sus, 0.8f },
                         { rel, 0.5f }, { lfoOn, 1 }, { lfoShape, 2 }, { lfoRate, 0.25f }, { fLfo, 0.6f }, { chorus, 1 } }));
    b.push_back (make ("Sweep Pad", "Pad", { "sweep", "filter" },
                       { { wave1, 0 }, { shape1, 0.45f }, { lvl1, 0.62f }, { warm1, 1 }, { on2, 1 }, { wave2, 0 }, { lvl2, 0.5f }, { coarse2, -12 },
                         { cutoff, 0.3f }, { emphasis, 0.45f }, { fEnv, 0.72f }, { fAtt, 0.6f }, { fDec, 0.7f }, { fSus, 0.4f },
                         { fRel, 0.5f }, { att, 0.45f }, { dec, 0.6f }, { sus, 0.9f }, { rel, 0.45f }, { chorus, 1 } }));
    // --- pluck / keys
    b.push_back (make ("Bright Pluck", "Pluck", { "pluck", "short" },
                       { { wave1, 0 }, { shape1, 0.35f }, { lvl1, 0.95f }, { on2, 1 }, { wave2, 2 }, { lvl2, 0.7f }, { coarse2, 12 }, { cutoff, 0.3f },
                         { emphasis, 0.3f }, { fEnv, 0.88f }, { fDec, 0.25f }, { fSus, 0.0f }, { fRel, 0.2f }, { att, 0.0f },
                         { dec, 0.32f }, { sus, 0.0f }, { rel, 0.25f }, { chorus, 1 } }));
    b.push_back (make ("Soft Keys", "Keys", { "keys", "soft" },
                       { { wave1, 1 }, { on2, 1 }, { wave2, 4 }, { lvl2, 0.5f }, { coarse2, 12 }, { cutoff, 0.55f }, { fEnv, 0.66f },
                         { fDec, 0.4f }, { fSus, 0.2f }, { att, 0.0f }, { dec, 0.5f }, { sus, 0.35f }, { rel, 0.3f }, { vel, 0.7f } }));
    b.push_back (make ("Toy Organ", "Keys", { "organ", "retro" },
                       { { wave1, 2 }, { oct1a, 1 }, { oct1b, 1 }, { on2, 1 }, { wave2, 0 }, { shape2, 0.5f }, { lvl2, 0.4f },
                         { coarse2, 19 }, { cutoff, 0.75f }, { att, 0.0f }, { dec, 0.5f }, { sus, 0.9f }, { rel, 0.1f },
                         { lfoOn, 1 }, { lfoShape, 2 }, { lfoRate, 0.55f }, { lfo1, 0.52f }, { lfo2, 0.52f }, { chorus, 1 } }));
    // --- fx
    b.push_back (make ("Noise Sweep", "FX", { "noise", "sweep" },
                       { { wave1, 3 }, { lvl1, 0.46f }, { cutoff, 0.15f }, { emphasis, 0.6f }, { band, 0.4f }, { fEnv, 0.85f },
                         { fAtt, 0.65f }, { fDec, 0.75f }, { fSus, 0.2f }, { fRel, 0.6f }, { att, 0.3f }, { dec, 0.7f }, { sus, 0.8f },
                         { rel, 0.55f } }));
    b.push_back (make ("Random Bleeps", "FX", { "s&h", "random" },
                       { { wave1, 2 }, { lvl1, 1.0f }, { cutoff, 0.5f }, { emphasis, 0.55f }, { lfoOn, 1 }, { lfoShape, 3 }, { lfoRate, 0.62f },
                         { fLfo, 0.85f }, { lfo1, 0.62f }, { att, 0.0f }, { dec, 0.5f }, { sus, 0.8f }, { rel, 0.3f }, { chorus, 1 } }));
    return b;
}
} // namespace lynx::banks
