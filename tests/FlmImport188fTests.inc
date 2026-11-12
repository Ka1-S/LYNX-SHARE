// --- FL Studio Mobile (.flm) import, round 188f: slice F5 (effects → LYNX slots, their automation lanes). FLM's own
// filter designs are rebuilt here from the formulas of REA_rodada182c "F5, round 188f" (cookbook biquads with FLM's
// amplitude and bandwidth laws) and compared with the response of the LYNX slot the import makes.

#include <array>
#include <complex>
#include <set>

#include "core/FlmEffects.h"
#include "core/ParametricEqFl.h"
#include "core/PocketFx.h"
#include "core/Reeverb2.h"

namespace flm188f
{
// the flm182c helpers by name (a using-directive would make Bytes ambiguous with tests.cpp's own)
namespace flm = lynx::flm;
using flm182c::Bytes;
using flm182c::put;
using flm182c::u8;
using flm182c::u16;
using flm182c::u32;
using flm182c::f32;
using flm182c::f64;
using flm182c::tag;
using flm182c::fixed;
using flm182c::lp32;
using flm182c::chunk;
using flm182c::cat;
using flm182c::head;
using flm182c::sample;
using flm182c::module;
using flm182c::rack;
using flm182c::clip;
using flm182c::lane;
using flm182c::channel;

struct Bq
{
    double b0, b1, b2, a0, a1, a2;
};

inline double magnitudeDb (const std::vector<Bq>& chain, double hz, double rate)
{
    const auto w = 2.0 * 3.141592653589793 * hz / rate;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    std::complex<double> h = 1.0;
    for (const auto& q : chain)
        h *= (q.b0 + q.b1 * z1 + q.b2 * z2) / (q.a0 + q.a1 * z1 + q.a2 * z2);
    return 20.0 * std::log10 (std::abs (h));
}

// FLM's shelf / peak band (kind 0 low shelf, 1 peak, 2 high shelf): A = 10^(dB/20), α = sin ω · sinh(ln2/2 · BW · ω / sin ω)
inline Bq flmBand (int kind, double hz, double shownDb, double octaves, double designRate)
{
    const auto w = std::min (2.0 * 3.141592653589793 * hz / designRate, 3.0);
    const auto s = std::sin (w), c = std::cos (w);
    const auto a = std::pow (10.0, shownDb / 20.0);
    const auto alpha = s * std::sinh (0.34657359027997264 * octaves * w / s);
    const auto sq = 2.0 * std::sqrt (a) * alpha;
    if (kind == 1)
        return { 1 + alpha * a, -2 * c, 1 - alpha * a, 1 + alpha / a, -2 * c, 1 - alpha / a };
    if (kind == 0)
        return { a * ((a + 1) - (a - 1) * c + sq), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - sq),
                 (a + 1) + (a - 1) * c + sq, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - sq };
    return { a * ((a + 1) + (a - 1) * c + sq), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - sq),
             (a + 1) - (a - 1) * c + sq, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - sq };
}

// FLM's Filter biquad (FUN_18060dae0 / dc80 / d940): low-pass c·(x + 2x₁ + x₂), high-pass c·(x − 2x₁ + x₂), band-pass
// c·(x − x₂); the feedback 1 + s/Q (low/high) or 1 + s/2Q (band)
inline Bq flmFilter (int type, double hz, double q, double rate)
{
    const auto w = 2.0 * 3.141592653589793 * hz / rate;
    const auto s = std::sin (w), c = std::cos (w);
    if (type == 1)
    {
        const auto a0 = 1.0 + s / (2.0 * q);
        return { s * 0.5, 0, -s * 0.5, a0, -2 * c, 1 - s / (2.0 * q) };
    }
    const auto g = (type == 0 ? 1 - c : 1 + c) / (s / q + 2.0);
    const auto k = 1.0 + s / q; // the denominator, scaled to a0 = 1 below
    return { g * k, (type == 0 ? 2.0 : -2.0) * g * k, g * k, k, -2 * c, 1 - s / q };
}

inline lynx::flm::Module moduleOf (std::uint32_t type, std::vector<float> params)
{
    lynx::flm::Module m;
    m.type = type;
    m.instanceId = 2;
    m.params = std::move (params);
    return m;
}
} // namespace flm188f

TEST (flm188f_effect_laws)
{
    using namespace lynx;
    // FLM's knobs: the EQ presets "Low Cut 50Hz" / "Low Cut 200Hz" put 0.07 / 0.2863 on the frequency
    CHECK (std::abs (flmfx::equalizerHz (0.07f) - 50.1) < 0.1 && std::abs (flmfx::equalizerHz (0.2863f) - 200.6) < 0.2);
    CHECK (std::abs (flmfx::filterHz (0.0f) - 16.0) < 1e-9 && std::abs (flmfx::filterHz (1.0f) - std::pow (2.0, 14.3)) < 1e-6);
    CHECK (std::abs (flmfx::filterQ (0.5f) - 1.3) < 1e-9);
    // the reverb's integer controls: Reverb 2's defaults land on FL Reeverb 2's (75 Hz, 4 kHz, 1.5 s, 500 Hz)
    CHECK (flmfx::rawControl (0.0187f, 19, 3000) == 75 && flmfx::rawControl (0.16f, 5, 221) == 40 && flmfx::rawControl (0.068f, 1, 200) == 15
           && flmfx::rawControl (0.2405f, 25, 2000) == 500 && flmfx::rawControl (0.0f, -361, 60) == -361);
}

TEST (flm188f_eq_responses)
{
    using namespace lynx;
    // round 190dw: the Graphic EQ and the Equalizer are FLM's own engines (Pocket Graphic EQ / Pocket EQ, measured against
    // FLM inside FL in tests/PocketEq190dwTests.inc); the import copies the module's values (parameter k + 1 → k)
    {
        std::vector<float> p { 1.0f, 0.62f, 0.5f, 0.3f, 0.5f, 0.71f, 0.5f, 0.5f, 0.66f };
        const auto mapped = flmfx::map (flm188f::moduleOf (7, p));
        CHECK (mapped.slots.size() == 1 && mapped.partial.empty() && mapped.label == "FL Studio Mobile: Graphic EQ");
        if (mapped.slots.size() == 1)
        {
            CHECK (mapped.slots[0].kind == EffectKind::pocketGraphicEq && mapped.slots[0].enabled);
            for (int k = 0; k < 8; ++k)
                CHECK (mapped.slots[0].params[(std::size_t) k] == p[(std::size_t) k + 1]);
        }
        // the eight bands all moved: still one slot (two Parametric EQ slots before round 190dw)
        CHECK (flmfx::map (flm188f::moduleOf (7, { 1.0f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f })).slots.size() == 1);
    }
    {
        std::vector<float> p { 1.0f, 0.6263f, 0.2f, 0.5f, 0.0f, 0.5f, 0.4f, 0.5f, 0.51f, 0.5f, 0.6f, 0.5f, 0.51f, 0.625f, 0.8f, 0.5f, 1.0f };
        const auto mapped = flmfx::map (flm188f::moduleOf (3, p));
        CHECK (mapped.slots.size() == 1 && mapped.label == "FL Studio Mobile: Equalizer" && mapped.partial.empty());
        if (mapped.slots.size() == 1)
        {
            CHECK (mapped.slots[0].kind == EffectKind::pocketEq);
            for (int k = 0; k < 16; ++k)
                CHECK (mapped.slots[0].params[(std::size_t) k] == p[(std::size_t) k + 1]);
            // a module saved without its four Channel values keeps FLM's default (both channels)
            for (int k = 16; k < 20; ++k)
                CHECK (mapped.slots[0].params[(std::size_t) k] == 0.0f);
        }
        // "Low Cut 200Hz": a −24 dB shelf plays at −24 dB now (it was capped at the Parametric EQ's −18 dB and listed)
        const auto cut = flmfx::map (flm188f::moduleOf (3, { 1.0f, 0.0f, 0.2863f, 0.1062f, 0.0f }));
        CHECK (cut.partial.empty() && cut.slots.size() == 1);
        // a band on one channel is played on that channel (listed before round 190dw)
        auto one = p;
        one.resize (21, 0.0f);
        one[17] = 0.3f;
        CHECK (flmfx::map (flm188f::moduleOf (3, one)).partial.empty());
    }
}

TEST (flm188f_filter_responses)
{
    using namespace lynx;
    // round 190dw: Pocket Filter (FLM's engine, tests/PocketEq190dwTests.inc) with the module's four values, every slope
    // in one slot
    for (const auto& p : std::vector<std::vector<float>> { { 1.0f, 0.8075f, 0.2562f, 0.0f, 0.0f }, { 1.0f, 0.68f, 0.5037f, 0.5f, 0.5f },
                                                           { 1.0f, 0.5788f, 0.5037f, 1.0f, 0.0f }, { 1.0f, 0.5f, 0.0f, 0.0f, 1.0f } })
    {
        const auto mapped = flmfx::map (flm188f::moduleOf (9, p));
        CHECK (mapped.slots.size() == 1 && mapped.partial.empty());
        if (mapped.slots.size() == 1)
        {
            CHECK (mapped.slots[0].kind == EffectKind::pocketFilter);
            for (int k = 0; k < 4; ++k)
                CHECK (mapped.slots[0].params[(std::size_t) k] == p[(std::size_t) k + 1]);
        }
    }
    // the frequency knob is one LYNX parameter; the slope is automatable too now (the slot layout no longer changes)
    const auto targets = flmfx::automate (flm188f::moduleOf (9, { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f }), 1, { 0.2f, 0.5f, 1.0f });
    CHECK (targets.size() == 1 && targets[0].param == 0 && targets[0].values == std::vector<float> ({ 0.2f, 0.5f, 1.0f }));
    const auto slope = flmfx::automate (flm188f::moduleOf (9, { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f }), 4, { 0.0f, 1.0f });
    CHECK (slope.size() == 1 && slope[0].param == 3);
}

TEST (flm188f_autoduck_and_reverbs)
{
    using namespace lynx;
    // Autoduck (legacy) "Pump": since round 190dw Pocket Autoduck (module 29's engine) with the legacy curve: depth
    // Ducking² = √(Ducking⁴), rise over 0.5·Attack + 0.001, fall to the next beat, power (Curve + 1)/2
    {
        const auto mapped = flmfx::map (flm188f::moduleOf (10, { 1.0f, 0.8977f, 0.1f, 0.49f }));
        CHECK (mapped.slots.size() == 1 && mapped.partial.empty());
        if (mapped.slots.size() == 1)
        {
            const auto& s = mapped.slots[0];
            CHECK (s.kind == EffectKind::pocketAutoduck && s.mix == 1.0f);
            CHECK (std::abs (std::sqrt (s.params[0]) - 0.8977f * 0.8977f) < 1e-6f && s.params[1] == 0.1f && s.params[2] == 0.49f
                   && s.params[3] == 1.0f && s.params[4] == 0.5f && s.params[5] == 0.0f && s.params[6] == 0.0f);
            // the curve: full duck at a = 0.051 beat, half-way back at the middle of the fall
            CHECK (pocket::autoduckEnvelope (s.params, 0.051f) == 1.0f && pocket::autoduckEnvelope (s.params, 0.0f) == 0.0f);
            CHECK (std::abs (pocket::autoduckEnvelope (s.params, 0.051f + 0.949f * 0.5f) - std::pow (0.5f, (0.49f + 1.0f) * 0.5f)) < 1e-4f);
        }
        // the Ducking lane moves Pocket Autoduck's Ducking (as Ducking⁴)
        const auto t = flmfx::automate (flm188f::moduleOf (10, { 1.0f, 0.8977f, 0.1f, 0.49f }), 1, { 0.0f, 0.5f, 1.0f });
        CHECK (t.size() == 1 && t[0].kind == AutomationTargetKind::effectParam && t[0].param == 0 && std::abs (t[0].values[1] - 0.0625f) < 1e-6f);
    }
    // Reverb 2 as the user's song has it, and the legacy Reverb's default preset: FLM's own reverbs (Pocket Reverb 2 /
    // Pocket Reverb Classic, round 190dw: tests/PocketReverb190dwTests.inc) with the module's values
    {
        const std::vector<float> p { 1.0f, 0.16f, 0.0187f, 0.5f, 0.068f, 1.0f, 0.16f, 0.8f, 0.8f, 0.131f, 0.0f, 0.0f,
                                     0.0f, 0.0f, 0.28f, 0.5f, 0.33f, 0.2405f, 0.5f };
        const auto mapped = flmfx::map (flm188f::moduleOf (35, p));
        CHECK (mapped.slots.size() == 1 && mapped.partial.empty());
        if (mapped.slots.size() == 1)
        {
            CHECK (mapped.slots[0].kind == EffectKind::pocketReverb2);
            for (int k = 0; k < 18; ++k)
                CHECK (mapped.slots[0].params[(std::size_t) k] == p[(std::size_t) k + 1]);
        }
    }
    {
        const std::vector<float> p { 1.0f, 0.4f, 0.8f, 0.4f, 0.0f, 1.0f, 1.0f, 0.5104f, 0.0729f, 1.0f, 1.0f, 0.25f,
                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        const auto mapped = flmfx::map (flm188f::moduleOf (12, p));
        CHECK (mapped.slots.size() == 1 && mapped.partial.empty());
        if (! mapped.slots.empty())
        {
            CHECK (mapped.slots[0].kind == EffectKind::pocketReverbClassic);
            for (int k = 0; k < 18; ++k)
                CHECK (mapped.slots[0].params[(std::size_t) k] == p[(std::size_t) k + 1]);
            // values the file doesn't hold keep FLM's defaults
            const auto def = makeEffectSlot (EffectKind::pocketReverbClassic).params;
            CHECK (mapped.slots[0].params[18] == def[18] && mapped.slots[0].params[19] == def[19]);
        }
        // its MIX lane: one LYNX parameter
        const auto t = flmfx::automate (flm188f::moduleOf (12, p), 3, { 0.0f, 0.4f, 0.6f });
        CHECK (t.size() == 1 && t[0].slot == 0 && t[0].param == 2);
    }
    // MultiFX: Pocket MultiFX since round 188h
    CHECK (flmfx::map (flm188f::moduleOf (4, { 1.0f, 0.1429f, 0.5f, 0.5f, 0.61f, 0.0f, 0.0f })).slots.size() == 1);
}

namespace flm188f
{
// master with a MultiFX; "Pad" (no instrument) with an Equalizer, a Filter whose frequency is automated, and a
// MultiFX lane
inline Bytes fxProject()
{
    Bytes f;
    tag (f, "10LF");
    const Bytes tdiv { 4, 4 };
    const auto withParams = [] (std::uint32_t type, std::uint32_t id, std::initializer_list<float> values) {
        auto b = module (type, id, {}, "Default/Default", 0, 127, 0);
        // replace the empty PRMS the helper wrote by the values (the chunk is the module body's last)
        Bytes prms;
        for (const auto v : values)
            f32 (prms, v);
        b.resize (b.size() - 8);
        const auto body = chunk ("PRMS", prms);
        b.insert (b.end(), body.begin(), body.end());
        std::uint32_t size = 0;
        std::memcpy (&size, b.data() + 4, 4);
        size = (std::uint32_t) (b.size() - 8);
        std::memcpy (b.data() + 4, &size, 4);
        return b;
    };
    return cat ({ f, head ("FX Song", 120.0), chunk ("TDIV", tdiv), rack (flm::kindMaster, { withParams (4, 1, { 1.0f, 0.1429f, 0.5f, 0.5f, 0.61f, 0.0f, 0.0f }) }),
                  rack (flm::kindNormal, { withParams (3, 2, { 1.0f, 0.6263f, 0.2f, 0.5f, 0.0f, 0.5f, 0.4f, 0.5f, 0.51f, 0.5f, 0.6f, 0.5f, 0.51f, 0.5f, 0.8f, 0.5f, 1.0f }),
                                           withParams (9, 3, { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f }) }),
                  channel (flm::kindMaster, "MASTER", 0, {}),
                  channel (flm::kindNormal, "Pad", 1,
                           { lane (1, "Frequency : FX Filter", { flm187d::autoClip (0, 4, 4, 1, { { 0, 1, 65535 }, { 2, -1, 32768 }, { 4, 1, 0 } }) }, 3, 1),
                             lane (1, "Axis X : MultiFX", { flm187d::autoClip (0, 4, 4, 1, { { 0, 1, 0 } }) }, 1, 2) }) });
}
} // namespace flm188f

TEST (flm188f_imports_effects_and_their_lanes)
{
    using namespace lynx;
    FlpImportReport report;
    std::string error;
    const auto song = importFlm (flm188f::fxProject(), flm182c::options (&report), &error);
    CHECK (song.has_value());
    if (! song)
    {
        std::printf ("  import error: %s\n", error.c_str());
        return;
    }
    const auto& s = *song;
    CHECK (s.mixer.size() == 2);
    if (s.mixer.size() != 2)
        return;
    // the Pad insert: the Equalizer, then the Filter (one slot each); the master's MultiFX: Pocket MultiFX (round 188h)
    const auto& fx = s.mixer[1].effects;
    CHECK (fx.size() == 2 && s.mixer[0].effects.size() == 1 && s.mixer[0].effects[0].kind == EffectKind::pocketMultiFx);
    if (fx.size() == 2)
        CHECK (fx[0].kind == EffectKind::pocketEq && fx[1].kind == EffectKind::pocketFilter && fx[0].enabled); // round 190dw
    CHECK (report.converted.count ("FL Studio Mobile: Equalizer") == 1 && report.converted.count ("FL Studio Mobile: Filter") == 1
           && report.converted.count ("FL Studio Mobile: MultiFX") == 1 && report.droppedEffects.empty());
    // the Filter's frequency lane: Pocket Filter's Frequency of slot 1 (round 190dw: one row; the Parametric EQ mapping
    // took three); the MultiFX lane is listed
    std::set<int> params;
    for (const auto& a : s.automations)
        if (a.target.kind == AutomationTargetKind::effectParam && a.target.index == 1 && a.target.slot == 1)
        {
            params.insert (a.target.param);
            CHECK (a.points.size() == 3);
            if (a.target.param == 0 && a.points.size() == 3)
                CHECK (std::abs (a.points[0].value - 1.0f) < 1e-5f && std::abs (a.points[1].value - 32768.0f / 65535.0f) < 1e-5f
                       && a.points[1].tick == 2 * ppq);
        }
    CHECK (s.automations.size() == 1 && params.count (0) == 1);
    CHECK (report.converted.count ("FL Studio Mobile: effect automation") == 1
           && report.unsupported.count ("FL Studio Mobile: automation of module parameters (not imported yet)") == 1);
    int rows = 0;
    for (const auto& t : s.playlistTracks)
        rows += t.name.rfind ("Pad Frequency : FX Filter", 0) == 0;
    CHECK (rows == 1);
}
