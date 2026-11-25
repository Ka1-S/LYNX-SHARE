// LYNX's own Kepler and Kepler Exo banks (round 190rh). Designed for LYNX on the documented parameters and state streams
// ("Kepler-1": REA_rodada46 §1.1; "Kepler-2": REA_rodada167d §1, the layouts core/Kepler.cpp reads); no FL preset was read for
// them. The LFO curve and the noise are FL's on a channel that has them, else LYNX's (core/NativeData.h).

#include <cmath>
#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "core/Kepler.h"
#include "core/BankTrims.h"
#include "core/LynxBanks.h"
#include "core/NativeData.h"

namespace lynx::banks
{
namespace
{
struct Bytes
{
    std::vector<std::uint8_t> b;
    void i (std::uint32_t v)
    {
        for (int k = 0; k < 4; ++k)
            b.push_back ((std::uint8_t) (v >> (8 * k)));
    }
    void f (float v)
    {
        std::uint32_t bits;
        std::memcpy (&bits, &v, 4);
        i (bits);
    }
    void name (const char* n)
    {
        i (9);
        for (int k = 0; k < 9; ++k)
            b.push_back ((std::uint8_t) n[k]);
    }
};
std::vector<std::uint8_t> wrapped (const std::vector<std::uint8_t>& stream)
{
    Bytes s;
    s.i (0), s.i ((std::uint32_t) stream.size()), s.i (0);
    s.b.insert (s.b.end(), stream.begin(), stream.end());
    return s.b;
}

// FL keeps Kepler Exo's engine state inside a wrapper (REA_rodada167d §1): 1, 1, its size, 0, the state, then 68 bytes
std::vector<std::uint8_t> exoWrapped (const std::vector<std::uint8_t>& engine)
{
    Bytes s;
    s.i (1), s.i (1), s.i ((std::uint32_t) engine.size()), s.i (0);
    s.b.insert (s.b.end(), engine.begin(), engine.end());
    s.b.resize (s.b.size() + 68, 0);
    return s.b;
}

enum Panel // Kepler's 36, in the stream's order
{
    arpOn, arpMode, arpRange, arpRate, arpSync, lfoRate, lfoSync, lfoDelay, lfoTrigMode, lfoTrigger, dcoLfo, pwAmount, pwSource, pulseOn,
    sawOn, subOn, subLevel, noise, highPass, cutoff, resonance, envPolarity, envAmount, vcfLfo, keyFollow, vcaSource, attack, decay,
    sustain, release, chorus, hold, octave, volume, retrigOn, retrigPhase, panelCount
};
using Values = std::initializer_list<std::pair<int, float>>;

Preset make (const char* name, const char* category, Values values, std::vector<std::string> tags = {})
{
    std::array<float, panelCount> p {};
    p[arpRate] = 0.5f, p[lfoRate] = 0.5f, p[pwAmount] = 0.4f, p[sawOn] = 1.0f, p[subLevel] = 0.5f, p[cutoff] = 0.6f, p[resonance] = 0.15f;
    p[envAmount] = 0.3f, p[keyFollow] = 0.5f, p[attack] = 0.05f, p[decay] = 0.45f, p[sustain] = 0.6f, p[release] = 0.3f;
    p[octave] = 0.5f, p[volume] = 0.55f;
    for (const auto& [i, v] : values)
        p[(std::size_t) i] = v;
    p[volume] = std::clamp (p[volume] * (float) std::pow (10.0, trimDb ("Kepler", name) / 20.0), 0.0f, 1.0f);
    Preset out;
    out.name = name;
    out.category = category;
    out.tags = std::move (tags);
    out.apply = [p] (ChannelDef& c) {
        Bytes s;
        s.name ("Kepler-1");
        s.i (4), s.i (panelCount);
        for (const auto v : p)
            s.f (v);
        s.b.push_back (0); // (arpeggiator restart)
        s.f (0.6f);        // velocity to volume
        s.f (0.5f);        // sub phase
        for (int k = 0; k < 3; ++k)
            s.b.push_back (0); // gate, chorus noise, ×4 oversampling: off
        c.pluginState = wrapped (s.b);
        c.modelParams.clear();
        if (c.samplePath.empty())
            c.samplePath = nativedata::ownPath (InstrumentKind::kepler);
    };
    return out;
}

// Kepler Exo: by internal index (the named list in core/Kepler.cpp), the engine's defaults for the rest
enum Exo
{
    xPw = 0x05, xPwSource = 0x06, xPulse = 0x07, xSaw = 0x08, xSub = 0x09, xSubLevel = 0x0a, xDco2 = 0x0c, xSemi = 0x0d, xDetune = 0x0e,
    xSaw2 = 0x0f, xPulse2 = 0x1a, xSync = 0x1b, xCross12 = 0x1c, xLevel1 = 0x20, xLevel2 = 0x21, xNoise = 0x22, xAm = 0x23, xCutoff = 0x25,
    xRes = 0x26, xKey = 0x27, xFm = 0x29, xEnvAmount = 0x2c, xVcfLfo1 = 0x2d, xLfo1 = 0x30, xAtk = 0x3d, xDec = 0x3e, xSus = 0x3f,
    xRel = 0x40, xChorus = 0x45, xVolume = 0x48, xMono = 0x4b, xLegato = 0x4c, xOctave = 0x53, xArp = 0x55, xArpRate = 0x56, xArpRange = 0x58,
    xDelay = 0x6e, xReverb = 0x79, xReverbWet = 0x85, xSat = 0x86, xSatAmount = 0x87, xHyper = 0x89, xHyperMix = 0x93, exoCount = 0x9b
};

Preset makeExo (const char* name, const char* category, Values values, std::vector<std::string> tags = {})
{
    const std::vector<std::pair<int, float>> own (values);
    Preset out;
    out.name = name;
    out.category = category;
    out.tags = std::move (tags);
    out.apply = [own] (ChannelDef& c) {
        std::array<float, exoCount> p {};
        for (int i = 0; i < exoCount; ++i)
            p[(std::size_t) i] = keplerExoDefault (i);
        p[xDelay] = 0.0f; // (off unless a preset asks for it)
        p[xCutoff] = 0.6f, p[xRes] = 0.15f, p[xEnvAmount] = 0.3f, p[xKey] = 0.5f, p[xAtk] = 0.05f, p[xDec] = 0.45f, p[xSus] = 0.6f, p[xRel] = 0.3f, p[xVolume] = 0.5f;
        for (const auto& [i, v] : own)
            p[(std::size_t) i] = v;
        Bytes s;
        s.name ("Kepler-2");
        s.i (4), s.i (exoCount);
        for (const auto v : p)
            s.f (v);
        s.i (1), s.i (8); // the matrix: eight slots without a source and a destination
        for (int k = 0; k < 16; ++k)
            s.i (0xffffffffu);
        for (int k = 0; k < 3; ++k)
            s.b.push_back (0); // arpeggiator restart, x4 oversampling, the other oscillator: off
        c.pluginState = exoWrapped (wrapped (s.b));
        c.modelParams.clear();
        if (c.samplePath.empty())
            c.samplePath = nativedata::ownPath (InstrumentKind::keplerExo);
    };
    return out;
}
} // namespace

std::vector<Preset> keplerBank()
{
    return {
        make ("Plain Saw", "Keys", {}),
        make ("Soft Brass", "Keys", { { attack, 0.25f }, { cutoff, 0.45f }, { envAmount, 0.5f }, { sustain, 0.7f }, { chorus, 0.4f } }, { "brass" }),
        make ("Clavi Pulse", "Keys", { { sawOn, 0.0f }, { pulseOn, 1.0f }, { pwAmount, 0.7f }, { cutoff, 0.45f }, { envAmount, 0.6f }, { decay, 0.3f }, { sustain, 0.0f } }, { "pulse" }),
        make ("Organ Sub", "Keys", { { pulseOn, 1.0f }, { sawOn, 0.0f }, { subOn, 1.0f }, { vcaSource, 1.0f }, { cutoff, 0.7f }, { chorus, 1.0f } }, { "organ" }),
        make ("Strings", "Pad", { { attack, 0.55f }, { release, 0.6f }, { sustain, 1.0f }, { cutoff, 0.62f }, { chorus, 0.4f } }, { "slow" }),
        make ("PWM Pad", "Pad", { { pulseOn, 1.0f }, { pwAmount, 0.8f }, { lfoRate, 0.35f }, { attack, 0.5f }, { release, 0.6f }, { sustain, 1.0f }, { chorus, 1.0f } }, { "pwm" }),
        make ("Dark Pad", "Pad", { { subOn, 1.0f }, { attack, 0.6f }, { release, 0.65f }, { sustain, 1.0f }, { cutoff, 0.42f }, { chorus, 0.4f } }, { "slow" }),
        make ("Wind Pad", "Pad", { { noise, 0.35f }, { attack, 0.6f }, { release, 0.65f }, { sustain, 1.0f }, { cutoff, 0.5f }, { resonance, 0.4f }, { vcfLfo, 0.3f }, { chorus, 1.0f } }, { "noise" }),
        make ("Sweep Pad", "Pad", { { attack, 0.7f }, { decay, 0.8f }, { release, 0.65f }, { cutoff, 0.3f }, { envAmount, 0.8f }, { resonance, 0.45f }, { chorus, 1.0f } }, { "filter" }),
        make ("Sub Bass", "Bass", { { sawOn, 0.0f }, { subOn, 1.0f }, { subLevel, 1.0f }, { octave, 0.0f }, { cutoff, 0.4f }, { release, 0.1f } }, { "sub" }),
        make ("Saw Bass", "Bass", { { subOn, 1.0f }, { octave, 0.0f }, { cutoff, 0.35f }, { envAmount, 0.5f }, { decay, 0.35f }, { sustain, 0.2f }, { release, 0.1f } }, { "saw" }),
        make ("Pulse Bass", "Bass", { { sawOn, 0.0f }, { pulseOn, 1.0f }, { pwAmount, 0.5f }, { octave, 0.0f }, { cutoff, 0.4f }, { envAmount, 0.45f }, { decay, 0.35f }, { sustain, 0.2f }, { release, 0.1f } }, { "pulse" }),
        make ("Reso Bass", "Bass", { { octave, 0.0f }, { cutoff, 0.3f }, { resonance, 0.6f }, { envAmount, 0.55f }, { decay, 0.4f }, { sustain, 0.0f }, { release, 0.1f } }, { "resonant" }),
        make ("Bright Lead", "Lead", { { cutoff, 0.8f }, { sustain, 0.9f }, { dcoLfo, 0.1f }, { lfoDelay, 0.3f } }, { "vibrato" }),
        make ("Hollow Lead", "Lead", { { sawOn, 0.0f }, { pulseOn, 1.0f }, { pwAmount, 0.1f }, { cutoff, 0.6f }, { sustain, 0.9f }, { chorus, 0.4f } }, { "square" }),
        make ("Stack Lead", "Lead", { { pulseOn, 1.0f }, { subOn, 1.0f }, { subLevel, 0.8f }, { cutoff, 0.65f }, { envAmount, 0.4f }, { chorus, 1.0f } }, { "thick" }),
        make ("Short Pluck", "Pluck", { { cutoff, 0.35f }, { envAmount, 0.65f }, { decay, 0.28f }, { sustain, 0.0f }, { release, 0.25f } }),
        make ("Chorus Pluck", "Pluck", { { subOn, 1.0f }, { cutoff, 0.4f }, { envAmount, 0.6f }, { decay, 0.35f }, { sustain, 0.0f }, { release, 0.35f }, { chorus, 1.0f } }),
        make ("Noise Pluck", "Pluck", { { noise, 0.4f }, { cutoff, 0.4f }, { resonance, 0.35f }, { envAmount, 0.55f }, { decay, 0.25f }, { sustain, 0.0f } }, { "noise" }),
        make ("Arp Run", "Arp", { { arpOn, 1.0f }, { arpRange, 0.5f }, { arpRate, 0.55f }, { cutoff, 0.5f }, { envAmount, 0.45f }, { decay, 0.3f }, { sustain, 0.1f } }, { "arpeggio" }),
        make ("Self Tone", "FX", { { sawOn, 0.0f }, { resonance, 1.0f }, { cutoff, 0.5f }, { keyFollow, 1.0f }, { noise, 0.05f }, { sustain, 1.0f }, { chorus, 0.4f } }, { "whistle" }),
        make ("Surf", "FX", { { sawOn, 0.0f }, { noise, 1.0f }, { cutoff, 0.45f }, { vcfLfo, 0.6f }, { lfoRate, 0.2f }, { attack, 0.6f }, { release, 0.7f }, { sustain, 1.0f } }, { "noise" }),
    };
}

std::vector<Preset> keplerExoBank()
{
    return {
        makeExo ("Two Saws", "Lead", { { xDco2, 1.0f }, { xDetune, 0.45f } }, { "detuned" }),
        makeExo ("Fifth Stack", "Lead", { { xDco2, 1.0f }, { xSemi, 0.65f }, { xCutoff, 0.7f }, { xChorus, 0.4f } }, { "fifth" }),
        makeExo ("Sync Edge", "Lead", { { xDco2, 1.0f }, { xSync, 1.0f }, { xSemi, 0.75f }, { xCutoff, 0.75f } }, { "sync" }),
        makeExo ("Solo Glide", "Lead", { { xMono, 1.0f }, { xLegato, 1.0f }, { 0x50, 0.4f }, { xCutoff, 0.7f }, { xDelay, 1.0f } }, { "mono" }),
        makeExo ("Driven Saw", "Lead", { { xDco2, 1.0f }, { xSat, 1.0f }, { xSatAmount, 0.5f }, { xVolume, 0.4f } }, { "saturation" }),
        makeExo ("Pulse Lead", "Lead", { { xSaw, 0.0f }, { xPulse, 1.0f }, { xPw, 0.3f }, { xDelay, 1.0f } }, { "pulse" }),
        makeExo ("Wide Strings", "Pad", { { xDco2, 1.0f }, { xAtk, 0.55f }, { xRel, 0.6f }, { xSus, 1.0f }, { xHyper, 1.0f }, { xHyperMix, 0.6f }, { xReverb, 1.0f } }, { "slow" }),
        makeExo ("Night Pad", "Pad", { { xSub, 1.0f }, { xAtk, 0.6f }, { xRel, 0.65f }, { xSus, 1.0f }, { xCutoff, 0.42f }, { xChorus, 1.0f }, { xReverb, 1.0f } }, { "slow" }),
        makeExo ("PWM Cloud", "Pad", { { xSaw, 0.0f }, { xPulse, 1.0f }, { xPw, 0.8f }, { xDco2, 1.0f }, { xPulse2, 1.0f }, { xSaw2, 0.0f }, { xAtk, 0.5f }, { xRel, 0.6f }, { xSus, 1.0f }, { xReverb, 1.0f } }, { "pwm" }),
        makeExo ("FM Mist", "Pad", { { xDco2, 1.0f }, { xCross12, 0.3f }, { xAtk, 0.6f }, { xRel, 0.65f }, { xSus, 1.0f }, { xCutoff, 0.5f }, { xReverb, 1.0f } }, { "cross modulation" }),
        makeExo ("Breath Pad", "Pad", { { xNoise, 0.35f }, { xAtk, 0.55f }, { xRel, 0.65f }, { xSus, 1.0f }, { xCutoff, 0.5f }, { xRes, 0.4f }, { xReverb, 1.0f } }, { "noise" }),
        makeExo ("Deep Bass", "Bass", { { xSub, 1.0f }, { xSubLevel, 1.0f }, { xOctave, 0.0f }, { xCutoff, 0.38f }, { xRel, 0.1f } }, { "sub" }),
        makeExo ("Growl Bass", "Bass", { { xDco2, 1.0f }, { xCross12, 0.5f }, { xOctave, 0.0f }, { xCutoff, 0.45f }, { xSat, 1.0f }, { xSatAmount, 0.4f }, { xVolume, 0.4f }, { xRel, 0.1f } }, { "cross modulation" }),
        makeExo ("Reese", "Bass", { { xDco2, 1.0f }, { xDetune, 0.6f }, { xOctave, 0.0f }, { xCutoff, 0.45f }, { xSus, 1.0f }, { xRel, 0.1f } }, { "detuned" }),
        makeExo ("Reso Bass", "Bass", { { xOctave, 0.0f }, { xCutoff, 0.3f }, { xRes, 0.6f }, { xEnvAmount, 0.55f }, { xDec, 0.4f }, { xSus, 0.0f }, { xRel, 0.1f } }, { "resonant" }),
        makeExo ("Hall Pluck", "Pluck", { { xDco2, 1.0f }, { xCutoff, 0.38f }, { xEnvAmount, 0.6f }, { xDec, 0.3f }, { xSus, 0.0f }, { xReverb, 1.0f } }),
        makeExo ("Echo Pluck", "Pluck", { { xCutoff, 0.4f }, { xEnvAmount, 0.6f }, { xDec, 0.28f }, { xSus, 0.0f }, { xDelay, 1.0f } }),
        makeExo ("Ring Keys", "Keys", { { xDco2, 1.0f }, { xAm, 0.6f }, { xSemi, 0.7f }, { xDec, 0.5f }, { xSus, 0.2f }, { xReverb, 1.0f } }, { "ring" }),
        makeExo ("Soft Brass", "Keys", { { xDco2, 1.0f }, { xAtk, 0.22f }, { xCutoff, 0.42f }, { xEnvAmount, 0.5f }, { xSus, 0.7f }, { xChorus, 0.4f } }, { "brass" }),
        makeExo ("Arp Night", "Arp", { { xArp, 1.0f }, { xArpRange, 0.5f }, { xArpRate, 0.55f }, { xCutoff, 0.5f }, { xEnvAmount, 0.45f }, { xDec, 0.3f }, { xSus, 0.1f }, { xDelay, 1.0f } }, { "arpeggio" }),
    };
}
} // namespace lynx::banks
