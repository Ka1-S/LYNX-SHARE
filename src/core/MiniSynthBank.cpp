// LYNX's own MiniSynth bank (round 190rh). Designed for LYNX on the documented parameters (REA_rodada41 §1–2) and state
// layout (§1.1: "SSLF" + the .flms stream); no FL preset was read for it.

#include <array>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "core/LynxBanks.h"
#include "core/MiniSynth.h"
#include "core/NativeData.h"

namespace lynx::banks
{
namespace
{
enum Param
{
    voices, transpose, slide, mwDest, mwAmount, oscKind, oscMod, noise, cutoff, peak, kbd, fltVel, fAtk, fDec, fSus, fRel, fAmount,
    lfoShape, lfoDest, lfoRate, lfoAmount, aAtk, aDec, aSus, aRel, distOn, overdrive, decimator, delayOn, delayTime, delayFeedback,
    delayFilter, delayMix, chorusOn, chorusRate, chorusDepth, chorusMix, phaserOn, phaserSpeed, phaserWidth, phaserFeedback, phaserMix, phaserFreq,
    count
};
// the oscillator kinds (int (9p))
constexpr float kind (int k) { return ((float) k + 0.2f) / 9.0f; }
constexpr float saw = kind (0), sawSaw = kind (1), pulse = kind (2), sqrSaw = kind (3), sqrSqr = kind (4), superSaw = kind (5), bell = kind (6),
                deaf = kind (7), spread = kind (8), poly = 0.2f, mono = 0.5f, lead = 1.0f;

std::vector<std::uint8_t> stateOf (const std::string& name, const std::array<float, count>& p)
{
    std::vector<std::uint8_t> body;
    const auto put = [&body] (const auto v) {
        const auto* b = (const std::uint8_t*) &v;
        body.insert (body.end(), b, b + sizeof (v));
    };
    const auto tag = [&body] (const char* four) { body.insert (body.end(), four, four + 4); };
    for (const std::uint8_t b : { 0x99, 0x46, 0x4C, 0x53, 0x0D, 0x0A, 0x1A, 0x0A })
        body.push_back (b);
    tag ("HEAD"), put ((std::uint32_t) (6 + 2 * name.size()));
    put ((std::uint16_t) 1), put ((std::uint16_t) 1), put ((std::uint16_t) name.size());
    for (const auto c : name)
        put ((std::uint16_t) (unsigned char) c);
    tag ("DATA"), put ((std::uint32_t) (4 + 4 * p.size())), tag ("0rev");
    for (const auto v : p)
        put (v);
    std::vector<std::uint8_t> out { 'S', 'S', 'L', 'F' };
    for (int i = 0; i < 4; ++i)
        out.push_back ((std::uint8_t) (body.size() >> (8 * i)));
    out.insert (out.end(), body.begin(), body.end());
    return out;
}

Preset make (const char* name, const char* category, std::initializer_list<std::pair<Param, float>> values, std::vector<std::string> tags = {})
{
    std::array<float, count> p {};
    p[voices] = poly, p[transpose] = 0.5f, p[oscKind] = saw, p[cutoff] = 0.75f, p[peak] = 0.1f, p[kbd] = 0.3f, p[fltVel] = 0.2f;
    p[fDec] = 0.5f, p[fSus] = 0.5f, p[fRel] = 0.2f, p[fAmount] = 0.5f, p[lfoRate] = 0.4f, p[lfoAmount] = 0.5f;
    p[aDec] = 0.6f, p[aSus] = 0.8f, p[aRel] = 0.12f;
    p[delayTime] = 0.7f, p[delayFeedback] = 0.4f, p[delayFilter] = 0.7f, p[delayMix] = 0.3f;
    p[chorusRate] = 0.3f, p[chorusDepth] = 0.4f, p[chorusMix] = 0.5f;
    p[phaserSpeed] = 0.4f, p[phaserWidth] = 0.5f, p[phaserFeedback] = 0.3f, p[phaserMix] = 0.5f, p[phaserFreq] = 0.5f;
    for (const auto& [i, v] : values)
        p[(std::size_t) i] = v;
    Preset out;
    out.name = name;
    out.category = category;
    out.tags = std::move (tags);
    out.apply = [p, n = std::string (name)] (ChannelDef& c) {
        c.pluginState = stateOf (n, p);
        c.modelParams.clear(); // (the desktop plugin)
        if (c.samplePath.empty())
            c.samplePath = nativedata::ownPath (InstrumentKind::minisynth);
    };
    return out;
}
} // namespace

std::vector<Preset> miniSynthBank()
{
    return {
        make ("Plain Saw", "Keys", {}, { "saw" }),
        make ("Glass Keys", "Keys", { { oscKind, bell }, { oscMod, 0.35f }, { cutoff, 0.8f }, { aSus, 0.3f }, { aDec, 0.5f }, { aRel, 0.3f }, { chorusOn, 1.0f } }, { "bell" }),
        make ("Clav Stab", "Keys", { { oscKind, pulse }, { oscMod, 0.7f }, { cutoff, 0.45f }, { fAmount, 0.8f }, { fDec, 0.25f }, { fSus, 0.0f }, { aDec, 0.4f }, { aSus, 0.0f } }, { "pluck" }),
        make ("Organ Drift", "Keys", { { oscKind, sqrSqr }, { oscMod, 0.5f }, { cutoff, 0.6f }, { aSus, 0.45f }, { aDec, 0.15f }, { phaserOn, 1.0f } }, { "organ" }),
        make ("Big Super", "Lead", { { oscKind, superSaw }, { oscMod, 0.45f }, { cutoff, 0.9f }, { aRel, 0.25f }, { delayOn, 1.0f } }, { "supersaw" }),
        make ("Mono Cutter", "Lead", { { voices, lead }, { slide, 0.35f }, { oscKind, sawSaw }, { oscMod, 0.25f }, { cutoff, 0.7f }, { peak, 0.35f } }, { "mono" }),
        make ("Square Whistle", "Lead", { { voices, lead }, { slide, 0.45f }, { oscKind, pulse }, { oscMod, 0.4f }, { cutoff, 0.75f }, { lfoDest, 0.5f }, { lfoAmount, 0.56f }, { delayOn, 1.0f } }, { "vibrato" }),
        make ("Dirty Lead", "Lead", { { voices, mono }, { oscKind, sqrSaw }, { oscMod, 0.3f }, { distOn, 1.0f }, { overdrive, 0.5f }, { cutoff, 0.6f } }, { "distortion" }),
        make ("Sync Bite", "Lead", { { oscKind, spread }, { oscMod, 0.4f }, { peak, 0.5f }, { cutoff, 0.55f }, { fAmount, 0.75f }, { fDec, 0.4f }, { fSus, 0.2f } }, { "resonant" }),
        make ("Sub Saw", "Bass", { { voices, mono }, { transpose, 0.0f }, { cutoff, 0.4f }, { fAmount, 0.65f }, { fDec, 0.35f }, { fSus, 0.2f } }, { "bass" }),
        make ("Pulse Bass", "Bass", { { voices, mono }, { transpose, 0.0f }, { oscKind, pulse }, { oscMod, 0.4f }, { cutoff, 0.42f }, { fAmount, 0.7f }, { fDec, 0.3f }, { fSus, 0.1f } }, { "bass" }),
        make ("Reese", "Bass", { { voices, mono }, { transpose, 0.0f }, { oscKind, sawSaw }, { oscMod, 0.5f }, { cutoff, 0.5f }, { aSus, 1.0f }, { phaserOn, 1.0f }, { phaserMix, 0.3f } }, { "detuned" }),
        make ("Wobble", "Bass", { { voices, mono }, { transpose, 0.0f }, { oscKind, sqrSaw }, { cutoff, 0.35f }, { peak, 0.4f }, { lfoRate, 0.2f }, { lfoAmount, 0.8f }, { aSus, 1.0f } }, { "lfo" }),
        make ("Deaf Growl", "Bass", { { voices, mono }, { transpose, 0.0f }, { oscKind, deaf }, { oscMod, 0.6f }, { cutoff, 0.5f }, { distOn, 1.0f }, { overdrive, 0.35f } }, { "distortion" }),
        make ("Soft Pad", "Pad", { { oscKind, superSaw }, { oscMod, 0.3f }, { cutoff, 0.55f }, { aAtk, 0.6f }, { aSus, 1.0f }, { aRel, 0.5f }, { chorusOn, 1.0f } }, { "slow" }),
        make ("Phase Pad", "Pad", { { oscKind, sawSaw }, { oscMod, 0.3f }, { cutoff, 0.6f }, { aAtk, 0.55f }, { aSus, 1.0f }, { aRel, 0.5f }, { phaserOn, 1.0f }, { phaserMix, 0.7f } }, { "phaser" }),
        make ("Breathing", "Pad", { { oscKind, sqrSaw }, { noise, 0.35f }, { cutoff, 0.45f }, { lfoRate, 0.6f }, { lfoAmount, 0.65f }, { aAtk, 0.5f }, { aSus, 1.0f }, { aRel, 0.5f } }, { "noise" }),
        make ("Bell Cloud", "Pad", { { oscKind, bell }, { oscMod, 0.6f }, { aAtk, 0.4f }, { aRel, 0.6f }, { delayOn, 1.0f }, { chorusOn, 1.0f } }, { "bell" }),
        make ("Trance Pluck", "Pluck", { { oscKind, superSaw }, { oscMod, 0.4f }, { cutoff, 0.5f }, { fAmount, 0.85f }, { fDec, 0.3f }, { fSus, 0.05f }, { aDec, 0.45f }, { aSus, 0.0f }, { aRel, 0.25f }, { delayOn, 1.0f } }, { "supersaw" }),
        make ("Dry Pluck", "Pluck", { { cutoff, 0.35f }, { fAmount, 0.8f }, { fDec, 0.25f }, { fSus, 0.0f }, { aDec, 0.4f }, { aSus, 0.0f } }, { "short" }),
        make ("Hollow Pluck", "Pluck", { { oscKind, sqrSqr }, { oscMod, 0.2f }, { cutoff, 0.4f }, { fAmount, 0.75f }, { fDec, 0.3f }, { fSus, 0.0f }, { aDec, 0.45f }, { aSus, 0.0f }, { chorusOn, 1.0f } }, { "square" }),
        make ("Bit Toy", "FX", { { oscKind, pulse }, { oscMod, 0.3f }, { distOn, 1.0f }, { decimator, 0.6f }, { aSus, 0.5f } }, { "lofi" }),
        make ("Wind", "FX", { { noise, 1.0f }, { cutoff, 0.72f }, { peak, 0.5f }, { lfoRate, 0.7f }, { lfoAmount, 0.7f }, { aAtk, 0.6f }, { aSus, 1.0f }, { aRel, 0.6f } }, { "noise" }),
        make ("Siren", "FX", { { voices, mono }, { lfoDest, 0.5f }, { lfoAmount, 0.85f }, { lfoRate, 0.45f }, { aSus, 1.0f }, { delayOn, 1.0f } }, { "lfo" }),
    };
}
} // namespace lynx::banks
