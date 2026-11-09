// --- FL Studio Mobile (.flm) import, round 188f: slice F5 (effects → LYNX slots, their automation lanes). FLM's own
// filter designs are rebuilt here from the formulas of REA_rodada182c "F5, round 188f" (cookbook biquads with FLM's
// amplitude and bandwidth laws) and compared with the response of the LYNX slot the import makes.

#include <array>
#include <complex>
#include <set>

#include "core/FlmEffects.h"
#include "core/ParametricEqFl.h"
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
    using flm188f::flmBand;
    const auto rate = flmfx::designRate;
    // Graphic EQ (designed at the running rate): the LYNX slot is FLM's filter
    {
        std::vector<float> p { 1.0f, 0.62f, 0.5f, 0.3f, 0.5f, 0.71f, 0.5f, 0.5f, 0.66f };
        const auto mapped = flmfx::map (flm188f::moduleOf (7, p));
        CHECK (mapped.slots.size() == 1 && mapped.partial.empty());
        static constexpr double centres[8] { 80, 160, 375, 750, 1500, 3000, 6000, 12000 };
        std::vector<flm188f::Bq> chain;
        for (int b = 0; b < 8; ++b)
            chain.push_back (flmBand (b == 0 ? 0 : b == 7 ? 2 : 1, centres[b], p[(std::size_t) b + 1] * 24.0 - 12.0, 1.0, rate));
        double worst = 0.0;
        if (mapped.slots.size() == 1)
            for (double f = 20.0; f < 20000.0; f *= 1.07)
                worst = std::max (worst, std::abs (peq::responseDb (mapped.slots[0].params, rate, f) - flm188f::magnitudeDb (chain, f, rate)));
        CHECK (worst < 1e-3);
        if (worst >= 1e-3)
            std::printf ("  graphic EQ: worst %.6f dB\n", worst);
    }
    // the eight bands all moved → two slots
    {
        const auto mapped = flmfx::map (flm188f::moduleOf (7, { 1.0f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f }));
        CHECK (mapped.slots.size() == 2);
    }
    // Equalizer ("Low Cut 200Hz": a −24 dB shelf, capped at LYNX's −18; "Low and High Boost"): FLM designs at twice the
    // rate, so the slot matches it below a few kHz
    {
        std::vector<float> p { 1.0f, 0.6263f, 0.2f, 0.5f, 0.0f, 0.5f, 0.4f, 0.5f, 0.51f, 0.5f, 0.6f, 0.5f, 0.51f, 0.625f, 0.8f, 0.5f, 1.0f };
        const auto mapped = flmfx::map (flm188f::moduleOf (3, p));
        CHECK (mapped.slots.size() == 1 && mapped.label == "FL Studio Mobile: Equalizer");
        std::vector<flm188f::Bq> chain;
        for (int b = 0; b < 4; ++b)
            chain.push_back (flmBand ((int) (p[(std::size_t) (4 + 4 * b)] * 2.0f), flmfx::equalizerHz (p[(std::size_t) (2 + 4 * b)]),
                                      (p[(std::size_t) (1 + 4 * b)] - 0.5) * 24.0, p[(std::size_t) (3 + 4 * b)] * 7.0 + 0.25, 2.0 * rate));
        double worst = 0.0;
        if (! mapped.slots.empty())
            for (double f = 20.0; f < 3000.0; f *= 1.05)
                worst = std::max (worst, std::abs (peq::responseDb (mapped.slots[0].params, rate, f) - flm188f::magnitudeDb (chain, f, 2.0 * rate)));
        CHECK (worst < 0.15); // the 5.4 kHz high shelf: FLM's 2× design keeps more top (0.1 dB at 1 kHz, 0.7 dB at 19 kHz)
        if (worst >= 0.15)
            std::printf ("  equalizer: worst %.4f dB\n", worst);
        const auto cut = flmfx::map (flm188f::moduleOf (3, { 1.0f, 0.0f, 0.2863f, 0.1062f, 0.0f }));
        CHECK (cut.partial.size() == 1 && cut.partial[0] == "FL Studio Mobile: EQ band beyond ±18 dB (capped)");
    }
}

TEST (flm188f_filter_responses)
{
    using namespace lynx;
    const auto rate = flmfx::designRate;
    // low-pass 24 dB (FLM's default slope), band-pass 48, high-pass 24; Q from 0.8 up
    const std::vector<std::vector<float>> cases { { 1.0f, 0.8075f, 0.2562f, 0.0f, 0.0f }, { 1.0f, 0.68f, 0.5037f, 0.5f, 0.5f },
                                                  { 1.0f, 0.5788f, 0.5037f, 1.0f, 0.0f }, { 1.0f, 0.9f, 0.0f, 0.0f, 0.5f } };
    for (const auto& p : cases)
    {
        const auto mapped = flmfx::map (flm188f::moduleOf (9, p));
        const auto type = (int) (p[3] * 2.0f), pairs = std::array<int, 3> { 1, 2, 4 }[(std::size_t) (int) (p[4] * 2.0f)];
        std::vector<flm188f::Bq> chain ((std::size_t) (2 * pairs), flm188f::flmFilter (type, flmfx::filterHz (p[1]), flmfx::filterQ (p[2]), rate));
        const auto level = type == 0 ? 20.0 * std::log10 (std::pow (0.8, pairs)) : 0.0;
        CHECK (mapped.slots.size() == 1);
        double worst = 0.0;
        if (mapped.slots.size() == 1)
            for (double f = 30.0; f < 18000.0; f *= 1.07)
            {
                const auto want = flm188f::magnitudeDb (chain, f, rate) + level;
                if (want > -80.0) // below that both are numerically nothing
                    worst = std::max (worst, std::abs (peq::responseDb (mapped.slots[0].params, rate, f) - want));
            }
        CHECK (worst < 1e-3);
        if (worst >= 1e-3)
            std::printf ("  filter type %d pairs %d: worst %.6f dB\n", type, pairs, worst);
    }
    // 96 dB/oct: eight biquads, two slots
    CHECK (flmfx::map (flm188f::moduleOf (9, { 1.0f, 0.5f, 0.0f, 0.0f, 1.0f })).slots.size() == 2);
    // the frequency knob moves every band and the level: one target per LYNX parameter, none for the slope (layout)
    const auto targets = flmfx::automate (flm188f::moduleOf (9, { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f }), 1, { 0.2f, 0.5f, 1.0f });
    CHECK (targets.size() == 3); // two band frequencies + the output level
    CHECK (flmfx::automate (flm188f::moduleOf (9, { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f }), 4, { 0.0f, 1.0f }).empty());
}

TEST (flm188f_autoduck_and_reverbs)
{
    using namespace lynx;
    // Autoduck (legacy) "Pump": duck to 1 − 0.8977² at a = 0.051 beat, back up by the next beat
    {
        const auto mapped = flmfx::map (flm188f::moduleOf (10, { 1.0f, 0.8977f, 0.1f, 0.49f }));
        CHECK (mapped.slots.size() == 1);
        if (mapped.slots.size() == 1)
        {
            const auto& s = mapped.slots[0];
            CHECK (s.kind == EffectKind::rhythmGain && std::abs (s.mix - 0.8977f * 0.8977f) < 1e-6f && std::abs (s.params[0] * 64.0f - 1.0f) < 1e-6f);
            CHECK (std::abs (s.params[2 + 3 * 4] - 0.051f) < 1e-6f && std::abs (s.params[3 + 3 * 4]) < 1e-6f // the bottom of the duck
                   && std::abs (s.params[2 + 3 * 11] - 1.0f) < 1e-6f && std::abs (s.params[3 + 3 * 11] - 1.0f) < 1e-6f);
        }
        // the Ducking lane moves the mix only
        const auto t = flmfx::automate (flm188f::moduleOf (10, { 1.0f, 0.8977f, 0.1f, 0.49f }), 1, { 0.0f, 0.5f, 1.0f });
        CHECK (t.size() == 1 && t[0].kind == AutomationTargetKind::effectMix && std::abs (t[0].values[1] - 0.25f) < 1e-6f);
    }
    // Reverb 2 as the user's song has it → FL Reeverb 2 controls (round 188h's corrected law: dry 1.25·DRY, wet
    // 0.375·WET, early reflections 0.625·(ER + WET)); FL's early-reflection control can't reach that level, so the three
    // levels are lowered by k = 1.674 and a utility after the reverb gives back +4.47 dB
    {
        const auto mapped = flmfx::map (flm188f::moduleOf (35, { 1.0f, 0.16f, 0.0187f, 0.5f, 0.068f, 1.0f, 0.16f, 0.8f, 0.8f, 0.131f, 0.0f, 0.0f,
                                                                  0.0f, 0.0f, 0.28f, 0.5f, 0.33f, 0.2405f, 0.5f }));
        CHECK (mapped.slots.size() == 2 && mapped.partial.empty());
        if (mapped.slots.size() == 2)
        {
            const auto s = reeverb2::fromParams (mapped.slots[0].params);
            CHECK (mapped.slots[0].kind == EffectKind::reeverb2 && mapped.slots[1].kind == EffectKind::utility);
            CHECK (s.p[0] == 75 && s.p[1] == 40 && s.p[2] == 0 && s.p[3] == 51 && s.p[4] == 100 && s.p[5] == 15 && s.p[6] == 40 && s.p[7] == 100
                   && s.p[8] == 500 && s.p[9] == 0 && s.p[14] == 0 && ! s.midSide);
            CHECK (s.p[10] == 104 && s.p[11] == 160 && s.p[12] == 27);
            CHECK (std::abs ((mapped.slots[1].params[0] - 0.5f) * 48.0f - 4.474f) < 0.01f);
        }
    }
    // the legacy Reverb's default preset: the input passes at unity (DRY at its minimum adds nothing: FL 128), INPUT
    // +6 dB, MIX −19.3 dB (FL 53), DECAY cubed (0.4 → 14 = 1.4 s), WIDTH 102 (FL separation −1)
    {
        const auto mapped = flmfx::map (flm188f::moduleOf (12, { 1.0f, 0.4f, 0.8f, 0.4f, 0.0f, 1.0f, 1.0f, 0.5104f, 0.0729f, 1.0f, 1.0f, 0.25f,
                                                                  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }));
        CHECK (mapped.slots.size() == 1);
        if (! mapped.slots.empty())
        {
            const auto s = reeverb2::fromParams (mapped.slots[0].params);
            CHECK (s.p[5] == 14 && s.p[10] == 128 && s.p[11] == 45 && s.p[12] == 53 && s.p[3] == 100 && s.p[0] == 236 && s.p[9] == -1);
        }
        // its MIX lane: the wet control and, for the stable layout, the utility (here at 0 dB throughout)
        const auto t = flmfx::automate (flm188f::moduleOf (12, { 1.0f, 0.4f, 0.8f, 0.4f, 0.0f, 1.0f, 1.0f, 0.5104f, 0.0729f, 1.0f, 1.0f, 0.25f }),
                                        3, { 0.0f, 0.4f, 0.6f });
        CHECK (t.size() == 1 && t[0].slot == 0 && t[0].param == 12);
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
        CHECK (fx[0].kind == EffectKind::parametricEq && fx[1].kind == EffectKind::parametricEq && fx[0].enabled);
    CHECK (report.converted.count ("FL Studio Mobile: Equalizer") == 1 && report.converted.count ("FL Studio Mobile: Filter") == 1
           && report.converted.count ("FL Studio Mobile: MultiFX") == 1 && report.droppedEffects.empty());
    // the Filter's frequency lane: both bands and the output level of slot 1, on three rows; the MultiFX lane is listed
    std::set<int> params;
    for (const auto& a : s.automations)
        if (a.target.kind == AutomationTargetKind::effectParam && a.target.index == 1 && a.target.slot == 1)
        {
            params.insert (a.target.param);
            CHECK (a.points.size() == 3);
            if (a.target.param == peq::param (0, 1) && a.points.size() == 3)
                CHECK (std::abs (a.points[0].value - peq::hzValue (flmfx::filterHz (1.0f))) < 1e-5f
                       && std::abs (a.points[1].value - peq::hzValue (flmfx::filterHz (32768.0f / 65535.0f))) < 1e-5f && a.points[1].tick == 2 * ppq);
        }
    CHECK (s.automations.size() == 3 && params.count (peq::param (0, 1)) == 1 && params.count (peq::param (1, 1)) == 1
           && params.count (peq::outputParam) == 1);
    CHECK (report.converted.count ("FL Studio Mobile: effect automation") == 1
           && report.unsupported.count ("FL Studio Mobile: automation of module parameters (not imported yet)") == 1);
    int rows = 0;
    for (const auto& t : s.playlistTracks)
        rows += t.name.rfind ("Pad Frequency : FX Filter", 0) == 0;
    CHECK (rows == 3);
}
