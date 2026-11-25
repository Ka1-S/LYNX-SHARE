// LYNX's own Sawer bank (round 190rh). Designed for LYNX on the documented parameters (REA_rodada65 §3.3); no FL preset was
// read for it. The state is "SSLF" + size + "1rwS" + the 96 parameters: the engine's loader (FUN_180034770) takes the
// modulation-map block and the name that FL writes after them only when they are there, else it keeps its constructor's.

#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "core/LynxBanks.h"
#include "core/NativeData.h"
#include "core/Sawer.h"

namespace lynx::banks
{
namespace
{
enum Param
{
    level, pan, aAtk, aDec, aSus, aRel, transpose, fine, mono, glideTimeMode, glideKeyMode, glideTime, octaver, uniRetrig, uniVoices,
    uniDetune, uniPan, subPhase, subHarmonic, subLevel, subDetune, subRetrig, subInvert, subPulse, syncFreq, syncOn, noise, cutoff,
    resonance, keyTrack, velTrack, filterMode, fAtk, fDec, fSus, fRel, fAmount, fDest, lfoShape, lfoSpeed, lfoSync, lfoAtk, lfoRel,
    lfoRetrig, lfoInvert, lfoAmount, lfoDest, chorusOn, chorusDepth, chorusRate, chorusMix, phaserOn, phaserCentre, phaserRate,
    phaserDepth, phaserFeedback, phaserMix, delayOn, delayLeft, delayRight, delayFeedback, delayMod, delayLowCut, delayHighCut,
    delayMix, reverbOn, reverbSize, reverbDecay, reverbDamp, reverbMix, arpOn, arpMode, arpRange, unused73, arpDivision, polyphony,
    bendRange, unused77
};
constexpr int paramCount = 96;
// the law T(v) = 50·v⁵ seconds; transposition in semitones (−24…24 around sel (v) − 12 → 0.5 = 0); the unison menu
float time (float seconds) { return std::pow (std::max (seconds, 0.0011f) / 50.0f, 0.2f); }
constexpr float harmonic (int k) { return ((float) k + 0.2f) / 8.0f; } // {1/4, 1/3, 1/2, 2/3, 1, 3/2, 2, 3, 4}
constexpr float octaveDown = 0.25f;                                   // sel (0.25, −24, 24) = −12
constexpr float unison (int k) { return ((float) k + 0.2f) / 3.0f; }   // {1, 2, 4, 8} voices

std::vector<std::uint8_t> stateOf (const std::array<float, paramCount>& p)
{
    std::vector<std::uint8_t> s { 'S', 'S', 'L', 'F' };
    const auto put = [&s] (std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            s.push_back ((std::uint8_t) (v >> (8 * i)));
    };
    put (4 + 4 * paramCount);
    put (0x53777231u);
    for (const auto v : p)
    {
        std::uint32_t bits;
        std::memcpy (&bits, &v, 4);
        put (bits);
    }
    return s;
}

Preset make (const char* name, const char* category, std::initializer_list<std::pair<Param, float>> values, std::vector<std::string> tags = {})
{
    std::array<float, paramCount> p {};
    p[level] = 0.6f, p[pan] = 0.5f, p[aAtk] = time (0.003f), p[aDec] = time (0.8f), p[aSus] = 0.8f, p[aRel] = time (0.5f);
    p[transpose] = 0.5f, p[fine] = 0.5f, p[uniDetune] = 0.25f, p[uniPan] = 0.5f, p[subHarmonic] = harmonic (2);
    p[cutoff] = 0.8f, p[resonance] = 0.15f, p[keyTrack] = 0.4f, p[velTrack] = 0.15f, p[fAtk] = time (0.003f), p[fDec] = time (0.4f);
    p[fSus] = 0.4f, p[fRel] = time (0.4f), p[lfoSpeed] = 0.5f, p[lfoAtk] = 0.0f, p[lfoRel] = 1.0f;
    p[chorusDepth] = 0.3f, p[chorusRate] = 0.3f, p[chorusMix] = 0.4f;
    p[phaserCentre] = 0.4f, p[phaserRate] = 0.2f, p[phaserDepth] = 0.6f, p[phaserFeedback] = 0.3f, p[phaserMix] = 0.5f;
    p[delayLeft] = 0.4f, p[delayRight] = 0.5f, p[delayFeedback] = 0.35f, p[delayHighCut] = 0.8f, p[delayMix] = 0.3f;
    p[reverbSize] = 0.5f, p[reverbDecay] = 0.5f, p[reverbDamp] = 0.5f, p[reverbMix] = 0.3f;
    p[arpDivision] = 3.0f, p[polyphony] = 5.0f, p[bendRange] = 2.0f;
    for (int i = 78; i < 84; ++i)
        p[(std::size_t) i] = 0.5f; // (matrix amounts: none)
    for (int i = 84; i < 90; ++i)
        p[(std::size_t) i] = -1.0f; // (no destination)
    for (const auto& [i, v] : values)
        p[(std::size_t) i] = v;
    Preset out;
    out.name = name;
    out.category = category;
    out.tags = std::move (tags);
    out.apply = [p] (ChannelDef& c) {
        c.pluginState = stateOf (p);
        c.modelParams.clear();
        if (c.samplePath.empty())
            c.samplePath = nativedata::ownPath (InstrumentKind::sawer);
    };
    return out;
}
} // namespace

std::vector<Preset> sawerBank()
{
    const auto shortTime = time (0.12f), slow = time (1.2f), longer = time (2.5f);
    return {
        make ("Straight Saw", "Lead", {}, { "saw" }),
        make ("Twin Saws", "Lead", { { uniVoices, unison (1) }, { uniDetune, 0.3f } }, { "detuned" }),
        make ("Eight Wide", "Lead", { { uniVoices, unison (3) }, { uniDetune, 0.35f }, { uniPan, 1.0f }, { level, 0.5f } }, { "supersaw" }),
        make ("Sync Scream", "Lead", { { syncOn, 1.0f }, { syncFreq, 0.4f }, { fAmount, 0.6f }, { fDest, 0.34f }, { fDec, slow } }, { "sync" }),
        make ("Reso Lead", "Lead", { { mono, 1.0f }, { glideTime, 0.45f }, { cutoff, 0.55f }, { resonance, 0.6f }, { fAmount, 0.35f } }, { "mono" }),
        make ("Vibrato Saw", "Lead", { { lfoAmount, 0.2f }, { lfoDest, 0.55f }, { lfoSpeed, 0.8f }, { lfoAtk, 0.2f }, { delayOn, 1.0f } }, { "vibrato" }),
        make ("Octave Lead", "Lead", { { octaver, 1.0f }, { uniVoices, unison (1) }, { chorusOn, 1.0f } }, { "octave" }),
        make ("Echo Saw", "Lead", { { delayOn, 1.0f }, { delayFeedback, 0.5f }, { reverbOn, 1.0f } }, { "delay" }),
        make ("Sub Bass", "Bass", { { transpose, octaveDown }, { cutoff, 0.45f }, { subLevel, 0.7f }, { subPulse, 1.0f } }, { "sub" }),
        make ("Pluck Bass", "Bass", { { transpose, octaveDown }, { cutoff, 0.35f }, { fAmount, 0.4f }, { fDec, shortTime }, { fSus, 0.0f }, { aSus, 0.5f } }, { "short" }),
        make ("Pulse Floor", "Bass", { { transpose, octaveDown }, { subPulse, 1.0f }, { subLevel, 0.9f }, { subHarmonic, harmonic (4) }, { cutoff, 0.5f } }, { "pulse" }),
        make ("Moving Bass", "Bass", { { transpose, octaveDown }, { mono, 1.0f }, { uniVoices, unison (1) }, { cutoff, 0.5f }, { phaserOn, 1.0f } }, { "phaser" }),
        make ("Growl", "Bass", { { transpose, octaveDown }, { mono, 1.0f }, { cutoff, 0.42f }, { resonance, 0.5f }, { lfoAmount, 0.6f }, { lfoSpeed, 0.75f } }, { "lfo" }),
        make ("Slow Pad", "Pad", { { uniVoices, unison (2) }, { uniPan, 0.9f }, { aAtk, time (0.7f) }, { aRel, longer }, { cutoff, 0.6f }, { chorusOn, 1.0f } }, { "slow" }),
        make ("Hall Pad", "Pad", { { uniVoices, unison (2) }, { aAtk, time (0.5f) }, { aRel, longer }, { cutoff, 0.55f }, { reverbOn, 1.0f }, { reverbMix, 0.6f } }, { "reverb" }),
        make ("Phase Pad", "Pad", { { uniVoices, unison (1) }, { aAtk, time (0.6f) }, { aRel, longer }, { phaserOn, 1.0f }, { phaserMix, 0.8f } }, { "phaser" }),
        make ("Band Pad", "Pad", { { filterMode, 0.4f }, { uniVoices, unison (2) }, { cutoff, 0.55f }, { aAtk, time (0.5f) }, { aRel, longer } }, { "band-pass" }),
        make ("Short Pluck", "Pluck", { { cutoff, 0.4f }, { fAmount, 0.45f }, { fDec, shortTime }, { fSus, 0.0f }, { aDec, time (0.3f) }, { aSus, 0.0f } }, { "short" }),
        make ("Wide Pluck", "Pluck", { { uniVoices, unison (3) }, { uniPan, 1.0f }, { level, 0.52f }, { cutoff, 0.4f }, { fAmount, 0.45f }, { fDec, time (0.2f) }, { fSus, 0.0f }, { aDec, time (0.5f) }, { aSus, 0.0f }, { delayOn, 1.0f } }, { "supersaw" }),
        make ("Hollow Pluck", "Pluck", { { subPulse, 1.0f }, { subLevel, 0.8f }, { subHarmonic, harmonic (4) }, { cutoff, 0.45f }, { fAmount, 0.35f }, { fDec, shortTime }, { fSus, 0.0f }, { aDec, time (0.4f) }, { aSus, 0.0f } }, { "pulse" }),
        make ("Arp Runner", "Arp", { { arpOn, 1.0f }, { arpRange, 0.5f }, { cutoff, 0.55f }, { fAmount, 0.3f }, { fDec, shortTime }, { fSus, 0.1f }, { delayOn, 1.0f } }, { "arpeggio" }),
        make ("Noise Sweep", "FX", { { noise, 0.9f }, { cutoff, 0.5f }, { resonance, 0.5f }, { lfoAmount, 0.8f }, { lfoSpeed, 0.35f }, { aAtk, time (0.5f) }, { aRel, longer } }, { "noise" }),
        make ("Dive", "FX", { { fAmount, 1.0f }, { fDest, 1.0f }, { fDec, longer }, { fSus, 0.0f }, { reverbOn, 1.0f } }, { "pitch" }),
        make ("Drift Choir", "Pad", { { filterMode, 0.4f }, { uniVoices, unison (2) }, { cutoff, 0.6f }, { lfoAmount, 0.3f }, { lfoSpeed, 0.4f }, { aAtk, time (0.4f) }, { aRel, longer }, { chorusOn, 1.0f }, { reverbOn, 1.0f } }, { "slow" }),
    };
}
} // namespace lynx::banks
