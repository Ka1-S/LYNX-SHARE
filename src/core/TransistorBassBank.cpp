// LYNX's own Transistor Bass bank (round 190rh). Designed for LYNX — never read from or converted from FL's "Factory
// programs" — on the documented layout of the plugin's state (REA_rodada57 §2.2, transistorBassWriteState). Only the
// presets in "303 Pulse" use the pulse cycles (FL's when the channel has them, else LYNX's: core/NativeData.h).

#include <array>
#include <initializer_list>
#include <utility>

#include "core/LynxBanks.h"
#include "core/NativeData.h"
#include "core/TransistorBass.h"

namespace lynx::banks
{
namespace
{
enum Param
{
    tuning, waveform, cutoff, keyFollow, resonance, envMod, decay, accent, volume, highPass, minDecay, pulseWidth, lfoRate,
    lfoToPw, reverbOn, rvLowCut, rvHighCut, rvPredelay, rvPreFeedback, rvSize, rvDiffusion, rvDecay, rvDamping, rvWidth,
    rvDry, rvEarly, rvWet, distOn, distHp, drive, distTone, distVolume, delayOn, delaySync, delayAmount, delayTime,
    delayRight, delayFeedback, delayTone, delayStereo, vcaSmooth, pulse303
};

// the knobs in the middle, the effects set to something useful and off
std::array<float, 42> base()
{
    std::array<float, 42> p {};
    p[tuning] = 0.5f, p[waveform] = 1.0f, p[cutoff] = 0.4f, p[keyFollow] = 0.5f, p[resonance] = 0.5f, p[envMod] = 0.5f;
    p[decay] = 0.4f, p[accent] = 0.6f, p[volume] = 0.8f, p[minDecay] = 0.5f;
    p[rvLowCut] = 0.08f, p[rvHighCut] = 0.35f, p[rvPredelay] = 0.08f, p[rvSize] = 0.45f, p[rvDiffusion] = 0.85f;
    p[rvDecay] = 0.09f, p[rvDamping] = 0.3f, p[rvWidth] = 0.5f, p[rvDry] = 361.0f / 421.0f, p[rvEarly] = 0.0f, p[rvWet] = 0.5f;
    p[distTone] = 0.6f, p[distVolume] = 0.5f, p[drive] = 0.3f, p[distHp] = 0.05f;
    p[delaySync] = 1.0f, p[delayAmount] = 0.35f, p[delayTime] = 0.3f, p[delayRight] = 1.0f, p[delayFeedback] = 0.4f;
    p[delayTone] = 0.55f, p[delayStereo] = 0.7f, p[vcaSmooth] = 0.2f;
    return p;
}

struct Step
{
    int note; // −1 = rest
    bool slide = false, accent = false;
};
using Values = std::initializer_list<std::pair<Param, float>>;

Preset make (const char* name, const char* category, int engine, Values values, std::vector<std::string> tags = {},
             std::vector<Step> steps = {})
{
    auto patch = base();
    for (const auto& [i, v] : values)
        patch[(std::size_t) i] = v;
    Preset p;
    p.name = name;
    p.category = category;
    p.tags = std::move (tags);
    p.apply = [patch, engine, steps, name] (ChannelDef& c) {
        std::vector<TransistorBassProgram::Step> sequence;
        for (const auto& s : steps)
            sequence.push_back ({ s.note < 0 ? 60 : s.note, s.note >= 0, s.slide, s.accent });
        c.pluginState = transistorBassWriteState (patch, 4, engine, sequence);
        transistorBassRename (c.pluginState, true, 60, name); // (program 60 = patch 60 is the one shown and played)
        if (! sequence.empty())
            transistorBassRename (c.pluginState, false, 60, name);
        c.modelParams.clear(); // (the desktop engine)
        if (c.samplePath.empty())
            c.samplePath = nativedata::ownPath (InstrumentKind::transistorbass);
    };
    return p;
}
} // namespace

std::vector<Preset> transistorBassBank()
{
    constexpr int v1 = 0, extended = 1, tb303 = 2;
    return {
        make ("Acid Line", "Acid", tb303, { { cutoff, 0.45f }, { resonance, 0.72f }, { envMod, 0.62f }, { decay, 0.35f }, { accent, 0.7f } }, { "303", "saw" }),
        make ("Squelch Box", "Acid", tb303, { { cutoff, 0.3f }, { resonance, 0.92f }, { envMod, 0.8f }, { decay, 0.28f }, { accent, 0.85f } }, { "303", "resonant" }),
        make ("Hollow Acid", "Acid", tb303, { { waveform, 0.0f }, { cutoff, 0.42f }, { resonance, 0.78f }, { envMod, 0.6f }, { decay, 0.4f } }, { "square" }),
        make ("Slow Wah", "Acid", extended, { { cutoff, 0.3f }, { resonance, 0.8f }, { envMod, 0.55f }, { decay, 0.8f } }, { "slow" }),
        make ("Rubber Bite", "Acid", extended, { { cutoff, 0.35f }, { resonance, 0.65f }, { envMod, 0.75f }, { decay, 0.2f }, { accent, 0.9f } }, { "short" }),
        make ("Silver Lead", "Acid", extended, { { tuning, 1.0f }, { cutoff, 0.5f }, { resonance, 0.75f }, { envMod, 0.4f }, { decay, 0.55f }, { delayOn, 1.0f } }, { "lead" }),
        make ("Rust Acid", "Driven", v1, { { cutoff, 0.4f }, { resonance, 0.7f }, { envMod, 0.6f }, { distOn, 1.0f }, { drive, 0.34f }, { distVolume, 0.90f } }, { "distortion" }),
        make ("Wall of Rat", "Driven", extended, { { cutoff, 0.5f }, { resonance, 0.6f }, { envMod, 0.5f }, { distOn, 1.0f }, { drive, 0.50f }, { distTone, 0.45f }, { distVolume, 1.00f } }, { "distortion", "hard" }),
        make ("Fuzz Square", "Driven", tb303, { { waveform, 0.0f }, { cutoff, 0.5f }, { resonance, 0.55f }, { distOn, 1.0f }, { drive, 0.40f }, { distVolume, 0.75f } }, { "distortion", "square" }),
        make ("Screamer Echo", "Driven", extended, { { tuning, 1.0f }, { cutoff, 0.45f }, { resonance, 0.85f }, { envMod, 0.6f }, { distOn, 1.0f }, { drive, 0.40f }, { distVolume, 1.00f }, { delayOn, 1.0f }, { delayFeedback, 0.5f } }, { "lead", "delay" }),
        make ("Round Bass", "Bass", extended, { { cutoff, 0.28f }, { resonance, 0.1f }, { envMod, 0.3f }, { decay, 0.5f }, { accent, 0.3f } }, { "soft" }),
        make ("Deep Square", "Bass", extended, { { waveform, 0.0f }, { cutoff, 0.25f }, { resonance, 0.15f }, { envMod, 0.2f }, { decay, 0.6f } }, { "sub" }),
        make ("Pluck Bass", "Bass", extended, { { cutoff, 0.22f }, { resonance, 0.3f }, { envMod, 0.6f }, { decay, 0.18f }, { minDecay, 0.2f } }, { "short" }),
        make ("Half Saw", "Bass", extended, { { waveform, 0.5f }, { cutoff, 0.42f }, { resonance, 0.25f }, { envMod, 0.35f } }, { "mixed" }),
        make ("Open Saw", "Bass", extended, { { volume, 0.6f }, { cutoff, 0.8f }, { resonance, 0.1f }, { envMod, 0.2f }, { decay, 0.7f } }, { "bright" }),
        make ("Wide Pulse", "Bass", extended, { { volume, 0.6f }, { waveform, 0.0f }, { pulseWidth, 0.55f }, { lfoRate, 0.12f }, { lfoToPw, 0.7f }, { cutoff, 0.5f }, { resonance, 0.2f } }, { "pwm" }),
        make ("Thin Reed", "Lead", extended, { { tuning, 1.0f }, { waveform, 0.0f }, { pulseWidth, 0.8f }, { cutoff, 0.6f }, { resonance, 0.35f }, { highPass, 0.5f } }, { "pwm" }),
        make ("Plate Lead", "Lead", extended, { { tuning, 1.0f }, { cutoff, 0.55f }, { resonance, 0.45f }, { envMod, 0.3f }, { decay, 0.6f }, { reverbOn, 1.0f }, { rvWet, 0.58f }, { rvDecay, 0.15f } }, { "reverb" }),
        make ("Dub Drop", "Lead", extended, { { cutoff, 0.35f }, { resonance, 0.5f }, { envMod, 0.5f }, { delayOn, 1.0f }, { delayFeedback, 0.65f }, { delayAmount, 0.5f }, { reverbOn, 1.0f } }, { "delay", "dub" }),
        make ("Transistor Square", "303 Pulse", tb303, { { waveform, 0.0f }, { pulse303, 1.0f }, { cutoff, 0.45f }, { resonance, 0.7f }, { envMod, 0.55f } }, { "303", "pulse" }),
        make ("Square Growl", "303 Pulse", v1, { { waveform, 0.0f }, { pulse303, 1.0f }, { cutoff, 0.38f }, { resonance, 0.8f }, { envMod, 0.7f }, { distOn, 1.0f }, { drive, 0.28f }, { distVolume, 0.80f } }, { "303", "pulse", "distortion" }),
        make ("Pulse and Saw", "303 Pulse", tb303, { { volume, 1.0f }, { waveform, 0.5f }, { pulse303, 1.0f }, { cutoff, 0.5f }, { resonance, 0.5f } }, { "303", "pulse" }),
        make ("Warehouse Run", "Sequence", tb303, { { volume, 0.6f }, { cutoff, 0.4f }, { resonance, 0.78f }, { envMod, 0.6f }, { decay, 0.3f }, { accent, 0.8f } }, { "sequence" },
              { { 60 }, { 60, false, true }, { 72, true }, { 60 }, { -1 }, { 63 }, { 60, true }, { 58 }, { 60, false, true }, { -1 }, { 60 }, { 67, true, true }, { 60 }, { 72 }, { -1 }, { 58, true } }),
        make ("Night Drive", "Sequence", extended, { { volume, 0.6f }, { waveform, 0.0f }, { cutoff, 0.34f }, { resonance, 0.45f }, { envMod, 0.45f }, { delayOn, 1.0f }, { delayAmount, 0.25f } }, { "sequence", "delay" },
              { { 60 }, { -1 }, { 60 }, { 60, false, true }, { -1 }, { 67 }, { 70, true }, { 67 }, { 60 }, { -1 }, { 58 }, { 60, false, true }, { -1 }, { 63, true }, { 65 }, { 67 } }),
    };
}
} // namespace lynx::banks
