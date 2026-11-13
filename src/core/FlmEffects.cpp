#include "core/FlmEffects.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "core/Effects.h"
#include "core/LynxTune.h"
#include "core/VocalPitch.h"
#include "core/ParametricEqFl.h"
#include "core/PocketFx.h"

// The laws below are FLM's, read in round 188f (REA_rodada182c.md "F5, round 188f"); the addresses are in that text.

namespace lynx::flmfx
{
namespace
{
constexpr double pi = 3.141592653589793;
constexpr double halfLn2 = 0.34657359027997264; // ln 2 / 2: the cookbook's bandwidth term

float param (const flm::Module& m, int i, float fallback) noexcept
{
    return i >= 0 && i < (int) m.params.size() ? m.params[(std::size_t) i] : fallback;
}

// FLM's per-module reserved values (SMPR): [0] Post Gain (0.5), [1] Mix (1.0), [2] Wet Pan (0.5)
float reserved (const flm::Module& m, int i, float fallback) noexcept
{
    return i >= 0 && i < (int) m.reserved.size() ? m.reserved[(std::size_t) i] : fallback;
}

// A cookbook band's bandwidth in octaves at normalised frequency w (radians per sample) → its Q (peaks), and the
// shelf slope S that gives the same α for a shelf of linear amplitude a (LYNX's shelves take the cookbook's S)
double bandwidthTerm (double octaves, double w) noexcept
{
    const auto s = std::sin (w);
    return s > 1e-12 ? std::sinh (halfLn2 * octaves * w / s) : std::sinh (halfLn2 * octaves);
}
double peakQ (double octaves, double w) noexcept { return 1.0 / (2.0 * std::max (bandwidthTerm (octaves, w), 1e-9)); }
double shelfSlope (double octaves, double w, double a) noexcept
{
    const auto t = 2.0 * bandwidthTerm (octaves, w);
    const auto inverse = 1.0 + (t * t - 2.0) / (a + 1.0 / a);
    return inverse > 1e-9 ? 1.0 / inverse : 1e9;
}

// One band of a LYNX parametric EQ slot
struct Band
{
    peq::BandType type = peq::BandType::off;
    double hz = 1000.0, db = 0.0, q = 0.70710678;
};

EffectSlot eqSlot (const std::vector<Band>& bands, double outputDb)
{
    auto slot = makeEffectSlot (EffectKind::parametricEq);
    for (int b = 0; b < peq::bands; ++b)
    {
        const auto band = b < (int) bands.size() ? bands[(std::size_t) b] : Band {};
        slot.params[(std::size_t) peq::param (b, 0)] = peq::typeValue (band.type);
        slot.params[(std::size_t) peq::param (b, 1)] = peq::hzValue (band.hz);
        slot.params[(std::size_t) peq::param (b, 2)] = peq::gainValue (band.db);
        slot.params[(std::size_t) peq::param (b, 3)] = peq::qValue (band.q);
        slot.params[(std::size_t) (peq::slopeParam + b)] = peq::slopeValue (1);
    }
    slot.params[(std::size_t) peq::outputParam] = peq::gainValue (outputDb);
    slot.params[(std::size_t) peqfl::lawParam] = 0.0f; // LYNX's own EQ law (cookbook biquads)
    return slot;
}

// A shelf / peak band from FLM's amplitude law: FLM puts 10^(dB/20) where the cookbook puts 10^(dB/40), so the band
// moves twice the dB its knob shows. `shown` = FLM's dB, `w` = the normalised frequency of FLM's design.
Band cookbookBand (int kind, double hz, double shown, double octaves, double w, Mapped& out)
{
    Band band;
    band.hz = hz;
    band.db = 2.0 * shown;
    if (std::abs (band.db) > 18.0)
    {
        out.partial.push_back ("FL Studio Mobile: EQ band beyond ±18 dB (capped)");
        band.db = std::clamp (band.db, -18.0, 18.0);
    }
    if (std::abs (band.db) < 1e-9)
        return Band {}; // flat: no band
    if (kind == 1)
    {
        band.type = peq::BandType::peak;
        band.q = std::clamp (peakQ (octaves, w), 0.1, 18.0);
    }
    else
    {
        band.type = kind == 0 ? peq::BandType::lowShelf : peq::BandType::highShelf;
        band.q = std::clamp (shelfSlope (octaves, w, std::pow (10.0, band.db / 40.0)), 0.1, 4.0);
    }
    return band;
}

// FL Studio Mobile's own effects that LYNX runs exactly as Pocket engines (core/PocketFx.h, round 188h): the slot's
// parameter k is the module's parameter k + 1 (its Enable is the slot's on switch), copied as it is; a value the file
// doesn't hold keeps the engine's default (FLM's)
void pocket (const flm::Module& m, EffectKind kind, const char* label, Mapped& out)
{
    out.label = label;
    auto slot = makeEffectSlot (kind);
    const auto& info = effectInfo (kind);
    for (std::size_t k = 0; k < info.params.size(); ++k)
        slot.params[k] = std::clamp (param (m, (int) k + 1, slot.params[k]), 0.0f, 1.0f);
    out.slots.push_back (slot);
}

// --- round 190dw: Autoduck (legacy, module 10) is FLM's Autoduck (29) with its curve fixed — the same beat phase, rise
// over a = 0.5·Attack + 0.001, fall to the next beat, power (Curve + 1)/2, one-pole of 0.01 per sample — and the depth
// Ducking² where module 29 takes √Ducking: Pocket Autoduck with Ducking⁴, Time 1, Shift ½, Hold 0, Shape 0 is exact
// (FLM_FORMAT §7.10).
void autoduckLegacy (const flm::Module& m, Mapped& out)
{
    out.label = "FL Studio Mobile: Autoduck (legacy)";
    auto slot = makeEffectSlot (EffectKind::pocketAutoduck);
    const auto d = (double) std::clamp (param (m, 1, 0.501f), 0.0f, 1.0f);
    slot.params[0] = (float) (d * d * d * d);
    slot.params[1] = std::clamp (param (m, 2, 0.1f), 0.0f, 1.0f);
    slot.params[2] = std::clamp (param (m, 3, 0.501f), 0.0f, 1.0f);
    slot.params[3] = 1.0f; // Time: the whole beat
    slot.params[4] = 0.5f; // Shift: none
    slot.params[5] = 0.0f; // Hold
    slot.params[6] = 0.0f; // Shape: the rise's power law on the fall too
    out.slots.push_back (slot);
}
// --- end of round 190dw
// --- round 190by (REA_rodada190by.md §4, §5)
// Tuned EQ (module 30): the Graphic EQ's eight bands (low shelf, six peaks, high shelf; a band moves twice the dB
// its knob shows) on centres an octave apart from 110 Hz × the Tuning note (0 = C … 9 = A … 11 = B), all with the
// Bandwidth's 10v³ + 0.1 octaves.
void tunedEq (const flm::Module& m, bool live, Mapped& out)
{
    out.label = "FL Studio Mobile: Tuned EQ";
    const auto note = tunedEqNote (param (m, 9, 0.0f));
    const auto octaves = tunedEqOctaves (param (m, 10, 0.6f));
    std::vector<Band> bands;
    bool outOfRange = false;
    for (int b = 0; b < 8; ++b)
    {
        const auto hz = tunedEqHz (note, b);
        const auto shown = (double) param (m, 1 + b, 0.5f) * 24.0 - 12.0;
        const auto kind = b == 0 ? 0 : b == 7 ? 2 : 1;
        const auto w = 2.0 * pi * hz / designRate;
        auto band = cookbookBand (kind, hz, shown, octaves, w, out);
        if (band.type != peq::BandType::off)
        {
            // the Bandwidth reaches shapes LYNX's EQ does not (0.1 … 10.1 octaves): say so once
            const auto raw = kind == 1 ? peakQ (octaves, w) : shelfSlope (octaves, w, std::pow (10.0, band.db / 40.0));
            if (! outOfRange && (raw < (kind == 1 ? 0.1 : 0.1) || raw > (kind == 1 ? 18.0 : 4.0) || ! std::isfinite (raw)))
            {
                outOfRange = true;
                out.partial.push_back ("FL Studio Mobile: Tuned EQ bandwidth beyond LYNX's EQ range (nearest shape)");
            }
        }
        if (band.type == peq::BandType::off && live)
            band = { kind == 1 ? peq::BandType::peak : kind == 0 ? peq::BandType::lowShelf : peq::BandType::highShelf, hz, 0.0,
                     kind == 1 ? std::clamp (peakQ (octaves, w), 0.1, 18.0) : 1.0 };
        if (band.type != peq::BandType::off)
            bands.push_back (band);
    }
    if (bands.size() <= (std::size_t) peq::bands)
        out.slots.push_back (eqSlot (bands, 0.0));
    else
    {
        out.slots.push_back (eqSlot ({ bands.begin(), bands.begin() + peq::bands }, 0.0));
        out.slots.push_back (eqSlot ({ bands.begin() + peq::bands, bands.end() }, 0.0));
    }
}
// --- end of round 190by
} // namespace

int tunedEqNote (float v) noexcept { return std::clamp ((int) (11.0f * v + 0.001f), 0, 11); }
double tunedEqHz (int note, int band) noexcept { return 110.0 * std::pow (2.0, (double) (note - 9) / 12.0 + (double) band); }
double tunedEqOctaves (float v) noexcept { return std::max (10.0 * (double) v * (double) v * (double) v + 0.1, 0.0001); }

double equalizerHz (float v) noexcept { return std::pow (2.0, 9.25 * (double) v + 5.0); }
double filterHz (float v) noexcept { return std::pow (2.0, 10.3 * (double) v + 4.0); }
double filterQ (float v) noexcept { return 2.0 * (double) v * (double) v + 0.8; }
int rawControl (float v, float lo, float hi) noexcept
{
    const auto c = std::clamp (v, 0.0f, 1.0f);
    return (int) std::floor ((hi - lo) * c + lo + 0.5f);
}

// --- round 190de (FLM_FORMAT §7.6): Pitcher (25) and Pitcher 2 (45) → LYNX Tune with a custom scale. Both correct the
// sung pitch towards the nearest of twelve allowed pitch classes (Pitcher: a 12-byte mask in its CSTM; Pitcher 2: its
// parameters 1…12), so Scale = Custom with the same note switches is exact; the rest is mapped law by law.
namespace
{
EffectSlot tuneSlot (std::uint16_t allowed)
{
    namespace lt = lynxtune;
    auto slot = makeEffectSlot (EffectKind::lynxTune);
    slot.params[lt::scale] = 1.0f; // Custom (the last of LYNX Tune's 14 scales)
    for (int k = 0; k < 12; ++k)
        slot.params[(std::size_t) (lt::noteC + k)] = (allowed >> k) & 1u ? 1.0f : 0.0f;
    slot.params[lt::humanize] = 0.0f;
    slot.params[lt::flexTune] = 0.0f;
    if (allowed == 0)
        slot.params[lt::correction] = 0.0f; // (FLM corrects nothing without an allowed note)
    return slot;
}
float retuneValue (double ms) noexcept { return (float) std::sqrt (std::clamp (ms, 0.0, 400.0) / 400.0); }
} // namespace

void pitcher (const flm::Module& m, Mapped& out)
{
    namespace lt = lynxtune;
    out.label = "FL Studio Mobile: Pitcher (LYNX Tune)";
    const auto v = [&m] (int i, float fallback) { return param (m, i, fallback); };
    // CSTM: 12 bytes (pitch class C … B allowed when not 0), then the Key and Scale menus (the mask is what plays)
    std::uint16_t allowed = 0;
    if (m.custom.size() >= 12)
        for (int k = 0; k < 12; ++k)
            allowed |= m.custom[(std::size_t) k] != 0 ? (std::uint16_t) (1u << k) : 0;
    else
        allowed = 0x0AB5; // (no mask saved: the menus' default, C major)
    auto slot = tuneSlot (allowed);
    // Speed: a one-pole at τ = (3(1 − v)³ + 0.001) / 8.82 s, instant from 0.99 — LYNX's retune is the same one-pole
    const auto u = 1.0 - (double) v (1, 0.0f);
    slot.params[lt::retune] = u < 0.01 ? 0.0f : retuneValue (1000.0 * (3.0 * u * u * u + 0.001) / 8.82);
    const auto st = (int) ((v (2, 0.5f) - 0.5f) * 24.0f);
    slot.params[lt::transpose] = (float) (st + 12) / 24.0f;
    const auto cents = (int) ((v (3, 0.5f) - 0.5f) * 100.0f);
    slot.params[lt::formantPreserve] = v (4, 0.0f) != 0.0f ? 1.0f : 0.0f;
    if (v (4, 0.0f) != 0.0f && std::abs (v (5, 0.5f) - 0.5f) > 1e-4f)
    {
        slot.params[lt::wideFormant] = 1.0f;
        slot.params[lt::formantShift] = v (5, 0.5f);
        out.partial.push_back ("FL Studio Mobile: Pitcher formant shift (LYNX Tune's formant shift)");
    }
    // Base Freq A4 = 440·2^((2v − 1)/12), the fine tune folded into it (LYNX Tune's reference: 430…450 Hz)
    const auto a4 = 440.0 * std::exp2 ((2.0 * (double) v (7, 0.5f) - 1.0) / 12.0) * std::exp2 ((double) cents / 1200.0);
    slot.params[lt::reference] = (float) std::clamp ((a4 - 430.0) / 20.0, 0.0, 1.0);
    if (a4 < 430.0 || a4 > 450.0)
        out.partial.push_back ("FL Studio Mobile: Pitcher base frequency outside 430…450 Hz (capped)");
    if (cents != 0)
        out.partial.push_back ("FL Studio Mobile: Pitcher fine tune (on the reference pitch)");
    slot.params[lt::mix] = std::clamp (v (9, 1.0f), 0.0f, 1.0f);
    for (int k = 0; k < 12; ++k)
        slot.params[(std::size_t) (lt::bypassC + k)] = v (10 + k, 0.0f) >= 1.0f ? 1.0f : 0.0f;
    if (v (22, 0.0f) != 0.0f)
        slot.params[lt::correction] = 0.0f; // Bypass snapping: transpose only
    slot.params[lt::range] = 1.0f; // (FLM's detector: 55…1500 Hz; LYNX Tune's widest range)
    if (v (8, 0.0f) > 1e-4f)
        out.partial.push_back ("FL Studio Mobile: Pitcher Stability (not reproduced)");
    out.partial.push_back ("FL Studio Mobile: Pitcher (LYNX Tune's detector and shifter)");
    out.slots.push_back (slot);
}

void pitcher2 (const flm::Module& m, Mapped& out)
{
    namespace lt = lynxtune;
    out.label = "FL Studio Mobile: Pitcher 2 (LYNX Tune)";
    const auto v = [&m] (int i, float fallback) { return param (m, i, fallback); };
    static constexpr std::uint16_t cMinor = 0x05AD; // C, D, Eb, F, G, Ab, Bb: the module's defaults
    std::uint16_t allowed = 0;
    for (int k = 0; k < 12; ++k)
        if (v (1 + k, (cMinor >> k) & 1u ? 1.0f : 0.0f) > 0.5f)
            allowed |= (std::uint16_t) (1u << k);
    auto slot = tuneSlot (allowed);
    // Speed: four one-poles at (200^v − 0.9)·2.29896 Hz on the target; LYNX's one pole matched at the half-way time
    const auto fc = (std::pow (200.0, (double) v (13, 0.5f)) - 0.9) * 2.29896;
    slot.params[lt::retune] = retuneValue (1000.0 * 5.30 / (2.0 * 3.14159265358979 * std::max (fc, 1.0e-3)));
    slot.params[lt::formantPreserve] = v (16, 0.0f) == 0.0f ? 1.0f : 0.0f; // (the module's flag is inverted)
    if (std::abs (v (17, 0.5f) - 0.5f) > 1e-4f)
    {
        slot.params[lt::wideFormant] = 1.0f;
        slot.params[lt::formantShift] = v (17, 0.5f);
    }
    const auto stability = (double) v (19, 0.5f);
    slot.params[lt::toleranceCents] = (float) std::clamp ((50.0 * stability - 3.0) / 40.0, 0.0, 1.0);
    slot.params[lt::mix] = std::clamp (v (22, 1.0f), 0.0f, 1.0f);
    slot.params[lt::lowLatency] = v (25, 0.0f) != 0.0f ? 1.0f : 0.0f;
    slot.params[lt::range] = 1.0f;
    out.partial.push_back ("FL Studio Mobile: Pitcher 2 (LYNX Tune's detector, shifter and retune curve)");
    if (v (20, 0.0f) > 1e-4f)
        out.partial.push_back ("FL Studio Mobile: Pitcher 2 Humanization (not reproduced)");
    if (v (24, 0.0f) > 1e-4f)
        out.partial.push_back ("FL Studio Mobile: Pitcher 2 Vibrato (not reproduced)");
    out.slots.push_back (slot);
}
// --- end of round 190de

Mapped map (const flm::Module& m, bool live, bool postGainLive)
{
    Mapped out;
    switch (m.type)
    {
        // --- round 190dw: FLM's exact engines (they were the Parametric EQ, the rhythm gain and Reeverb 2 before)
        case 3: pocket (m, EffectKind::pocketEq, "FL Studio Mobile: Equalizer", out); break;
        case 7: pocket (m, EffectKind::pocketGraphicEq, "FL Studio Mobile: Graphic EQ", out); break;
        case 9: pocket (m, EffectKind::pocketFilter, "FL Studio Mobile: Filter", out); break;
        case 10: autoduckLegacy (m, out); break;
        case 12: pocket (m, EffectKind::pocketReverbClassic, "FL Studio Mobile: Reverb (legacy)", out); break;
        case 35: pocket (m, EffectKind::pocketReverb2, "FL Studio Mobile: Reverb 2", out); break;
        case 32: pocket (m, EffectKind::pocketReverb, "FL Studio Mobile: Reverb", out); break;
        // --- end of round 190dw
        case 5: pocket (m, EffectKind::pocketLimiterClassic, "FL Studio Mobile: Limiter (legacy)", out); break;
        case 6: pocket (m, EffectKind::pocketDistortion, "FL Studio Mobile: Distortion", out); break;
        case 28: pocket (m, EffectKind::pocketLimiter, "FL Studio Mobile: Limiter", out); break;
        case 20: pocket (m, EffectKind::pocketLeveller, "FL Studio Mobile: Leveller", out); break;
        case 27: pocket (m, EffectKind::pocketGate, "FL Studio Mobile: Gate", out); break;
        case 16:
            pocket (m, EffectKind::pocketTranceDelay, "FL Studio Mobile: Trance Delay", out);
            // FLM's saved flag "Input pan pans the dry too" (one byte of custom state; a module without it loads as on)
            out.slots[0].params[6] = m.custom.empty() || m.custom[0] != 0 ? 1.0f : 0.0f;
            break;
        case 18: pocket (m, EffectKind::pocketTapeDelay, "FL Studio Mobile: Tape Delay", out); break;
        case 4: pocket (m, EffectKind::pocketMultiFx, "FL Studio Mobile: MultiFX", out); break;
        case 29: pocket (m, EffectKind::pocketAutoduck, "FL Studio Mobile: Autoduck", out); break;
        case 11: pocket (m, EffectKind::pocketChorus, "FL Studio Mobile: Chorus", out); break;
        case 34: pocket (m, EffectKind::pocketSpacer, "FL Studio Mobile: Spacer", out); break;
        case 15: pocket (m, EffectKind::pocketStereoizer, "FL Studio Mobile: Stereoizer", out); break;
        case 43: pocket (m, EffectKind::pocketWowFlutter, "FL Studio Mobile: Wow & Flutter", out); break; // round 189h
        case 14: pocket (m, EffectKind::pocketPhaser, "FL Studio Mobile: Phaser", out); break;
        case 13: pocket (m, EffectKind::pocketFlanger, "FL Studio Mobile: Flanger", out); break;
        case 46: pocket (m, EffectKind::pocketTremolo, "FL Studio Mobile: Tremolo", out); break;
        case 41: pocket (m, EffectKind::pocketWaveshaper, "FL Studio Mobile: Waveshaper", out); break;
        case 8: pocket (m, EffectKind::pocketCompressor, "FL Studio Mobile: Compressor", out); break;
        case 44: pocket (m, EffectKind::pocketSpreader, "FL Studio Mobile: Spreader", out); break;
        // --- round 190by
        case 40: pocket (m, EffectKind::pocketComb, "FL Studio Mobile: Comb Filter", out); break;
        case 36:
            pocket (m, EffectKind::pocketMultiband, "FL Studio Mobile: Multiband Compressor", out);
            out.slots[0].params[3] = 0.0f; // (FLM's [4] is a flag of its own editor, which solos nothing by itself)
            break;
        case 42: pocket (m, EffectKind::pocketTapeStop, "FL Studio Mobile: Tape Stop", out); break;
        case 30: tunedEq (m, live, out); break;
        // --- end of round 190by
        case 25: pitcher (m, out); break;  // round 190de
        case 45: pitcher2 (m, out); break; // round 190de
        default: return out;
    }
    // FLM's module wrapper (PROVEN, REA_rodada182c "F5 effects, round 188h"): Mix is a linear dry/wet blend (a LYNX
    // slot's mix); Post Gain = 10^(2g − 1) (40g − 20 dB) on the module's output before the blend; Wet Pan a linear
    // balance of the wet amount. Enable ([0] below 0.5) bypasses the module.
    const auto enabled = param (m, 0, 1.0f) >= 0.5f;
    const auto mix = std::clamp (reserved (m, 1, 1.0f), 0.0f, 1.0f);
    const auto postDb = 40.0 * (double) reserved (m, 0, 0.5f) - 20.0;
    // round 190fe: a Mix below 100% on a module that takes several LYNX slots, or with a Post Gain, is FLM's blend
    // itself — dry × (1 − Mix) + Post Gain × wet × Mix — as a parallel container: a dry branch and the module's slots
    // in a branch (exact for every module whose slots need no side input). Automated modules keep the plain slots
    // (their lanes address the slots one by one).
    const auto sideInput = std::any_of (out.slots.begin(), out.slots.end(), [] (const EffectSlot& x) { return x.sidechain >= 0 || x.sidechain2 >= 0; });
    if (mix < 0.9999f && ! live && ! sideInput && ! out.slots.empty() && (out.slots.size() > 1 || std::abs (postDb) > 1e-3))
    {
        auto container = makeEffectSlot (EffectKind::parallel);
        container.branches.push_back ({ {}, 1.0f - mix });
        container.branches.push_back ({ std::move (out.slots), (float) ((double) mix * std::pow (10.0, postDb / 20.0)) });
        container.enabled = enabled;
        out.slots = { std::move (container) };
        if (std::abs (reserved (m, 2, 0.5f) - 0.5f) > 1e-4f)
            out.partial.push_back ("FL Studio Mobile: effect Wet Pan (not reproduced)");
        return out;
    }
    if (std::abs (postDb) > 1e-3 || postGainLive)
    {
        if (mix >= 0.9999f)
        {
            // a utility after the module's slots (the reverbs' make-up utility takes it as well)
            if (out.slots.empty() || out.slots.back().kind != EffectKind::utility)
            {
                auto gain = makeEffectSlot (EffectKind::utility);
                gain.params[0] = 0.5f;
                out.slots.push_back (gain);
            }
            auto& g = out.slots.back().params[0];
            const auto db = ((double) g - 0.5) * 48.0 + postDb;
            if (std::abs (db) > 24.0)
                out.partial.push_back ("FL Studio Mobile: effect Post Gain beyond LYNX's range (capped)");
            g = (float) std::clamp (db / 48.0 + 0.5, 0.0, 1.0);
        }
        else
            out.partial.push_back ("FL Studio Mobile: effect Post Gain with Mix below 100% on an automated module (not reproduced)");
    }
    for (auto& s : out.slots)
        s.enabled = enabled;
    if (mix < 0.9999f)
    {
        if (out.slots.size() == 1)
            out.slots[0].mix *= mix;
        else
            out.partial.push_back ("FL Studio Mobile: Mix of an automated effect that takes two LYNX slots (played fully wet)");
    }
    if (std::abs (reserved (m, 2, 0.5f) - 0.5f) > 1e-4f)
        out.partial.push_back ("FL Studio Mobile: effect Wet Pan (not reproduced)");
    return out;
}

std::vector<LaneTarget> automate (const flm::Module& m, int param, const std::vector<float>& values)
{
    if (param == 0 || values.empty())
        return {}; // Enable: a switch LYNX doesn't automate
    const auto at = [&] (float v) {
        auto copy = m;
        if (param == -2 || param == -3)
        {
            const float defaults[3] { 0.5f, 1.0f, 0.5f }; // Post Gain, Mix, Wet Pan
            for (auto i = copy.reserved.size(); i < 3; ++i)
                copy.reserved.push_back (defaults[i]);
            copy.reserved[(std::size_t) (param + 3)] = v;
        }
        else
        {
            if (param < 0)
                return Mapped {};
            if (copy.params.size() <= (std::size_t) param)
                copy.params.resize ((std::size_t) param + 1, 0.0f);
            copy.params[(std::size_t) param] = v;
        }
        return map (copy, true, param == -3);
    };
    const auto base = at (values.front());
    if (base.slots.empty())
        return {};
    std::vector<Mapped> maps;
    maps.reserve (values.size());
    for (const auto v : values)
    {
        maps.push_back (at (v));
        if (maps.back().slots.size() != base.slots.size())
            return {}; // the slot layout changes with the value (a Filter's slope)
    }
    std::vector<LaneTarget> out;
    for (std::size_t s = 0; s < base.slots.size(); ++s)
    {
        if (std::any_of (maps.begin(), maps.end(), [&] (const Mapped& x) { return x.slots[s].mix != base.slots[s].mix; }))
        {
            LaneTarget t { AutomationTargetKind::effectMix, (int) s, 0, {} };
            for (const auto& x : maps)
                t.values.push_back (x.slots[s].mix);
            out.push_back (std::move (t));
        }
        for (std::size_t k = 0; k < base.slots[s].params.size(); ++k)
            if (std::any_of (maps.begin(), maps.end(), [&] (const Mapped& x) { return x.slots[s].params[k] != base.slots[s].params[k]; }))
            {
                LaneTarget t { AutomationTargetKind::effectParam, (int) s, (int) k, {} };
                for (const auto& x : maps)
                    t.values.push_back (x.slots[s].params[k]);
                out.push_back (std::move (t));
            }
    }
    return out;
}
} // namespace lynx::flmfx
