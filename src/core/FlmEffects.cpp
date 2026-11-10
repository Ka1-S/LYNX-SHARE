#include "core/FlmEffects.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "core/Effects.h"
#include "core/ParametricEqFl.h"
#include "core/PocketFx.h"
#include "core/Reeverb2.h"

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

void equalizer (const flm::Module& m, bool live, Mapped& out)
{
    out.label = "FL Studio Mobile: Equalizer";
    std::vector<Band> bands;
    bool channels = false;
    for (int b = 0; b < 4; ++b)
    {
        const auto shown = ((double) param (m, 1 + 4 * b, 0.5f) - 0.5) * 24.0;
        const auto hz = equalizerHz (param (m, 2 + 4 * b, 0.2f + 0.2f * (float) b));
        const auto octaves = std::max ((double) param (m, 3 + 4 * b, 0.5f) * 7.0 + 0.25, 1e-4);
        const auto kind = std::clamp ((int) (param (m, 4 + 4 * b, 0.51f) * 2.0f), 0, 2);
        if ((int) (param (m, 17 + b, 0.0f) * 4.0f) != 0)
            channels = true;
        // FLM designs the band at twice the rate and runs it 2× oversampled; the slot keeps the band's width in octaves
        // at the slot's own rate (the cookbook's bandwidth law is the same at either rate)
        const auto w = std::clamp (2.0 * pi * hz / designRate, 0.0, 3.0);
        auto band = cookbookBand (kind, hz, shown, octaves, w, out);
        if (band.type != peq::BandType::off || live)
        {
            if (band.type == peq::BandType::off) // a live band keeps its place, flat
                band = { kind == 1 ? peq::BandType::peak : kind == 0 ? peq::BandType::lowShelf : peq::BandType::highShelf, hz, 0.0,
                         kind == 1 ? peakQ (octaves, w) : 1.0 };
            bands.push_back (band);
        }
    }
    if (channels)
        out.partial.push_back ("FL Studio Mobile: Equalizer band on one channel / mid-side (played on both)");
    out.slots.push_back (eqSlot (bands, 0.0));
}

void graphicEq (const flm::Module& m, bool live, Mapped& out)
{
    out.label = "FL Studio Mobile: Graphic EQ";
    static constexpr double centres[8] { 80.0, 160.0, 375.0, 750.0, 1500.0, 3000.0, 6000.0, 12000.0 };
    std::vector<Band> bands;
    for (int b = 0; b < 8; ++b)
    {
        const auto shown = (double) param (m, 1 + b, 0.5f) * 24.0 - 12.0;
        const auto kind = b == 0 ? 0 : b == 7 ? 2 : 1;
        const auto w = 2.0 * pi * centres[b] / designRate;
        auto band = cookbookBand (kind, centres[b], shown, 1.0, w, out);
        if (band.type == peq::BandType::off && live)
            band = { kind == 1 ? peq::BandType::peak : kind == 0 ? peq::BandType::lowShelf : peq::BandType::highShelf, centres[b], 0.0,
                     kind == 1 ? peakQ (1.0, w) : 1.0 };
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

void filter (const flm::Module& m, Mapped& out)
{
    out.label = "FL Studio Mobile: Filter";
    const auto hz = filterHz (param (m, 1, 0.7f));
    const auto q = filterQ (param (m, 2, 0.0f));
    const auto type = std::clamp ((int) (param (m, 3, 0.0f) * 2.0f), 0, 2);   // 0 low-pass, 1 band-pass, 2 high-pass
    const auto pairs = std::array<int, 3> { 1, 2, 4 }[(std::size_t) std::clamp ((int) (param (m, 4, 0.5f) * 2.0f), 0, 2)];
    const auto stages = 2 * pairs; // 24 / 48 / 96 dB per octave
    // FLM's low- and high-pass biquads are the cookbook's at Q/2 with their feed-forward part normalised for Q (a level
    // (1 + s/Q)/(1 + s/2Q) per biquad, s = sin ω); its low-pass output is then scaled by 0.8 per biquad pair; its
    // band-pass is the cookbook's constant-skirt form (peak level Q)
    const auto s = std::sin (2.0 * pi * std::min (hz, designRate * 0.5) / designRate);
    Band band;
    band.hz = hz;
    double level = 1.0;
    if (type == 1)
    {
        band.type = peq::BandType::bandPass;
        band.q = q;
        level = std::pow (q, stages);
    }
    else
    {
        band.type = type == 0 ? peq::BandType::highCut : peq::BandType::lowCut;
        band.q = q * 0.5;
        level = std::pow ((1.0 + s / q) / (1.0 + s / (2.0 * q)), stages) * (type == 0 ? std::pow (0.8, pairs) : 1.0);
    }
    auto db = 20.0 * std::log10 (level);
    if (std::abs (db) > 18.0)
    {
        out.partial.push_back ("FL Studio Mobile: Filter level beyond ±18 dB (capped)");
        db = std::clamp (db, -18.0, 18.0);
    }
    std::vector<Band> first (std::min (stages, peq::bands), band);
    out.slots.push_back (eqSlot (first, db));
    if (stages > peq::bands)
        out.slots.push_back (eqSlot (std::vector<Band> ((std::size_t) (stages - peq::bands), band), 0.0));
}

// Autoduck (legacy): per beat, the duck envelope rises from 0 to 1 over the first a = 0.5·Attack + 0.001 of the beat,
// then falls back to 0 at the next beat; the gain is 1 − Ducking²·env^((Curve + 1)/2), smoothed by a one-pole (0.01
// per sample, not reproduced). LYNX: the curve with full depth as 12 points of the rhythm gain, Ducking² as the mix.
void autoduck (const flm::Module& m, Mapped& out)
{
    out.label = "FL Studio Mobile: Autoduck (legacy)";
    const auto ducking = (double) param (m, 1, 0.501f);
    const auto a = std::clamp ((double) param (m, 2, 0.1f) * 0.5 + 0.001, 0.001, 0.999);
    const auto gamma = ((double) param (m, 3, 0.501f) + 1.0) * 0.5;
    auto slot = makeEffectSlot (EffectKind::rhythmGain);
    slot.params[0] = 1.0f / 64.0f; // period: one beat
    slot.params[1] = 1.0f;         // 12 points
    const auto point = [&] (int k, double x, double env) {
        slot.params[(std::size_t) (2 + 3 * k)] = (float) x;
        slot.params[(std::size_t) (3 + 3 * k)] = (float) (1.0 - std::pow (std::clamp (env, 0.0, 1.0), gamma));
        slot.params[(std::size_t) (4 + 3 * k)] = 0.5f; // straight segments
    };
    point (0, 0.0, 0.0);
    for (int k = 1; k <= 4; ++k)
        point (k, a * k / 4.0, k / 4.0);
    for (int j = 1; j <= 7; ++j)
        point (4 + j, a + (1.0 - a) * j / 7.0, 1.0 - j / 7.0);
    slot.mix = (float) std::clamp (ducking * ducking, 0.0, 1.0);
    out.slots.push_back (slot);
    out.partial.push_back ("FL Studio Mobile: Autoduck (legacy) curve as 12 points, without FLM's 2 ms smoothing");
}

// FL's Reeverb 2 level law (REA_rodada49 §2): gain = (e^(2.3978953·x) − 1)·0.1, x = p/128 (dry) or p/256 (early
// reflections, wet); its inverse, rounded to FL's integer control
int levelControl (double gain, double scale) noexcept
{
    if (gain <= 0.0)
        return 0;
    return std::clamp ((int) std::lround (scale * std::log (10.0 * gain + 1.0) / 2.3978953), 0, 160);
}

// FLM's reverbs run the plate of FL's Reeverb 2 (the same line proportions, early-reflection taps, decay and filter laws:
// fxReverb, 0x180737690), with FLM's own integer controls. Inner controls (min, max): 0 input (−361…0/60, tenths of
// dB, the minimum = off), 1 low cut 19…3000 Hz, 2 high cut 5…221 (×100 Hz), 3 predelay 0…250/1000 ms, 4 room 1…100,
// 5 diffusion 0…100, 6 bass 2…30 (×0.1), 7 crossover 25…2000 Hz, 8 decay 1…200 (×0.1 s), 9 damping 5…221,
// 10 separation 0…200, 11 dry, 12 early reflections, 13 wet (tenths of dB); gains 10^(t/200), early reflections ×0.5,
// wet ×0.3.
struct Plate
{
    int input = 0, lowCut = 75, highCut = 40, predelayMs = 0, room = 50, diffusion = 100, bass = 10, crossover = 500, decay = 15,
        damping = 40, separation = 100;
    double dry = 1.0, early = 0.0, wet = 0.0; // linear, FLM's output gains (the input level included)
};

double tenthsDb (int t) noexcept { return t <= -361 ? 0.0 : std::pow (10.0, t / 200.0); }

// FLM feeds its plate 0.5·(L + R) where FL feeds L + R, and ends with a mid/side stage on the wet and early-reflection
// outputs (0x1807383d0): L = (l + r) + (l − r)·σ, R = (l + r) − (l − r)·σ, σ = separation/100; at the default σ = 1
// that is 2l / 2r, which cancels the input's ½ (round 188h, REA_rodada182c "F5 effects, round 188h", which corrects
// 188f's side swap and 2× gains). So FL's gains are FLM's, with FL's f = 1 − σ (FL: f = (5^s − 1)/4 for s > 0, f = s
// below; s = control/64). FLM's input diffusers 2 and 4 differ slightly from FL's (a tilt of at most ~0.4 dB, not
// reproduced). FL's controls can't reach every FLM level: the three gains are then lowered together and a LYNX utility
// after the reverb gives the difference back.
void plateSlots (const Plate& p, bool live, Mapped& out)
{
    reeverb2::Settings s;
    s.p[0] = std::clamp (p.lowCut, 19, 3000);
    s.p[1] = std::clamp (p.highCut, 5, 221);
    s.p[2] = std::clamp ((int) std::lround (p.predelayMs * 0.384), 0, 384); // FL: predelay samples = p·fs/384
    s.p[3] = std::clamp (p.room, 1, 100);
    s.p[4] = std::clamp (p.diffusion, 0, 100);
    s.p[5] = std::clamp (p.decay, 1, 200);
    s.p[6] = std::clamp (p.damping, 5, 221);
    s.p[7] = std::clamp (p.bass * 10, 20, 300);
    s.p[8] = std::clamp (p.crossover, 25, 2000);
    const auto f = 1.0 - std::clamp (p.separation / 100.0, 0.0, 2.0);
    s.p[9] = f > 0.0 ? (int) std::lround (64.0 * std::log (4.0 * f + 1.0) / std::log (5.0)) : (int) std::lround (64.0 * f);
    auto dry = p.dry, early = p.early, wet = p.wet;
    constexpr double maxDry = 1.9031, maxSend = 0.34759; // FL's gains at control 160
    const auto k = std::max ({ 1.0, dry / maxDry, early / maxSend, wet / maxSend });
    auto makeup = 20.0 * std::log10 (k);
    if (makeup > 24.0)
    {
        out.partial.push_back ("FL Studio Mobile: reverb level above LYNX's range (capped)");
        makeup = 24.0;
    }
    s.p[10] = levelControl (dry / k, 128.0);
    s.p[11] = levelControl (early / k, 256.0);
    s.p[12] = levelControl (wet / k, 256.0);
    s.p[13] = 684;
    s.p[14] = 0;
    auto slot = makeEffectSlot (EffectKind::reeverb2);
    reeverb2::toParams (s, slot.params);
    out.slots.push_back (slot);
    if (k > 1.0 || live)
    {
        auto gain = makeEffectSlot (EffectKind::utility);
        gain.params[0] = (float) std::clamp (makeup / 48.0 + 0.5, 0.0, 1.0);
        out.slots.push_back (gain);
    }
}

void reverbLegacy (const flm::Module& m, bool live, Mapped& out)
{
    out.label = "FL Studio Mobile: Reverb (legacy)";
    const auto v = [&m] (int i, float fallback) { return param (m, i, fallback); };
    Plate p;
    const auto decay = v (1, 0.4f);
    p.decay = rawControl (decay * decay * decay, 1, 200); // FLM cubes the DECAY knob
    p.damping = rawControl (v (2, 0.8f), 5, 221);
    const auto wet = rawControl (v (3, 0.4f), -361, 60);
    p.predelayMs = rawControl (v (4, 0.0f), 0, 250);
    p.room = rawControl (v (5, 1.0f), 1, 100);
    p.diffusion = rawControl (v (6, 1.0f), 0, 100);
    p.separation = rawControl (v (7, 0.5f), 0, 200);
    p.lowCut = rawControl (v (8, 0.0f), 19, 3000);
    p.highCut = rawControl (v (9, 1.0f), 5, 221);
    const auto input = rawControl (v (10, 1.0f), -361, 60);
    const auto early = rawControl (v (11, 0.25f), -361, 60);
    const auto dry = rawControl (v (12, 0.0f), -361, 60);
    const auto off = [&v] (int i) { return v (i, 0.0f) >= 0.5f; }; // 13…16: input / dry / early / wet off
    p.bass = rawControl (v (17, 0.0f), 2, 30);
    p.crossover = rawControl (v (18, 0.0f), 25, 2000);
    const auto in = off (13) ? 0.0 : tenthsDb (input);
    // the legacy wrapper runs the engine in place and the engine adds onto its buffer: the input always passes at unity
    p.dry = 1.0 + (off (14) ? 0.0 : in * tenthsDb (dry));
    p.early = off (15) ? 0.0 : in * 0.5 * tenthsDb (early);
    p.wet = off (16) ? 0.0 : in * 0.3 * tenthsDb (wet);
    if (v (20, 0.0f) > 1e-4f)
        out.partial.push_back ("FL Studio Mobile: reverb modulation (not reproduced)");
    plateSlots (p, live, out);
}

void reverb2 (const flm::Module& m, bool live, Mapped& out)
{
    out.label = "FL Studio Mobile: Reverb 2";
    const auto v = [&m] (int i, float fallback) { return param (m, i, fallback); };
    Plate p;
    p.highCut = rawControl (v (1, 0.16f), 5, 221);
    p.lowCut = rawControl (v (2, 0.0187f), 19, 3000);
    p.room = rawControl (v (3, 0.5f), 1, 100);
    p.decay = rawControl (v (4, 0.068f), 1, 200);
    p.diffusion = rawControl (v (5, 1.0f), 0, 100);
    p.damping = rawControl (v (6, 0.16f), 5, 221);
    p.predelayMs = rawControl (v (11, 0.0f), 0, 1000);
    p.bass = rawControl (v (14, 0.28f), 2, 30);
    p.crossover = rawControl (v (17, 0.2405f), 25, 2000);
    // Reverb 2 sets the engine's input / ER / wet to 0 dB and mixes two of its outputs itself, × 1.25: the full output
    // (wet × 0.3 + early reflections × 0.5) × WET and the raw early reflections × ER (0x18073a430); SEPARATION 0.5 and
    // PAN 0.5 leave them as they are. In FL's plate units: 0.375·WET wet, 0.625·(ER + WET) early reflections
    p.dry = 1.25 * v (7, 0.8f);
    p.wet = 0.375 * v (9, 0.56f);
    p.early = 0.625 * ((double) v (8, 0.8f) + (double) v (9, 0.56f));
    if (std::abs (v (15, 0.5f) - 0.5f) > 0.01f)
        out.partial.push_back ("FL Studio Mobile: reverb width / separation (played at FL's normal width)");
    if (std::abs (v (18, 0.5f) - 0.5f) > 0.01f)
        out.partial.push_back ("FL Studio Mobile: Reverb 2 wet pan (not reproduced)");
    if (v (13, 0.0f) > 1e-4f)
        out.partial.push_back ("FL Studio Mobile: reverb modulation (not reproduced)");
    if (v (12, 0.0f) >= 0.5f)
        out.partial.push_back ("FL Studio Mobile: Reverb 2 tempo-synced predelay (played free)");
    plateSlots (p, live, out);
    if (v (10, 0.0f) >= 0.5f) // SIDE INPUT: FLM feeds the plate L − R, as FL's Mid/Side option (which also inverts the
    {                         // right wet output; FLM's doesn't)
        out.slots.front().params[16] = 1.0f;
        out.partial.push_back ("FL Studio Mobile: Reverb 2 side input (right reverb output inverted)");
    }
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
} // namespace

double equalizerHz (float v) noexcept { return std::pow (2.0, 9.25 * (double) v + 5.0); }
double filterHz (float v) noexcept { return std::pow (2.0, 10.3 * (double) v + 4.0); }
double filterQ (float v) noexcept { return 2.0 * (double) v * (double) v + 0.8; }
int rawControl (float v, float lo, float hi) noexcept
{
    const auto c = std::clamp (v, 0.0f, 1.0f);
    return (int) std::floor ((hi - lo) * c + lo + 0.5f);
}

Mapped map (const flm::Module& m, bool live, bool postGainLive)
{
    Mapped out;
    switch (m.type)
    {
        case 3: equalizer (m, live, out); break;
        case 7: graphicEq (m, live, out); break;
        case 9: filter (m, out); break;
        case 10: autoduck (m, out); break;
        case 12: reverbLegacy (m, live, out); break;
        case 35: reverb2 (m, live, out); break;
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
        default: return out;
    }
    // FLM's module wrapper (PROVEN, REA_rodada182c "F5 effects, round 188h"): Mix is a linear dry/wet blend (a LYNX
    // slot's mix); Post Gain = 10^(2g − 1) (40g − 20 dB) on the module's output before the blend; Wet Pan a linear
    // balance of the wet amount. Enable ([0] below 0.5) bypasses the module.
    const auto enabled = param (m, 0, 1.0f) >= 0.5f;
    const auto mix = std::clamp (reserved (m, 1, 1.0f), 0.0f, 1.0f);
    const auto postDb = 40.0 * (double) reserved (m, 0, 0.5f) - 20.0;
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
            out.partial.push_back ("FL Studio Mobile: effect Post Gain with Mix below 100% (not reproduced)");
    }
    for (auto& s : out.slots)
        s.enabled = enabled;
    if (mix < 0.9999f)
    {
        if (out.slots.size() == 1)
            out.slots[0].mix *= mix;
        else
            out.partial.push_back ("FL Studio Mobile: Mix of an effect that takes two LYNX slots (played fully wet)");
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
