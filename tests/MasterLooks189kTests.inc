// --- Round 189k S8: Master looks (core/MasterLooks.h), Parametric EQ 2's FL state writer and the FL export of a look.

#include "core/FlStateWrite.h"
#include "core/FlpExport.h"
#include "core/MasterLooks.h"
#include "core/ParametricEqFl.h"

TEST (simple189k_peq2_writer_round_trip)
{
    using namespace lynx;
    const auto base = flpexport::parametricEq2DefaultState();
    CHECK (base.size() == 354);
    // a fresh FL instance read and written again gives FL's bytes back
    const auto fresh = peqfl::parsePeq2 (base);
    CHECK (fresh.has_value());
    if (! fresh)
        return;
    auto slot = makeEffectSlot (EffectKind::parametricEq);
    peqfl::toParams (*fresh, slot.params);
    CHECK (flstate::parametricEq2 (slot.params, base) == base);
    // every control at extremes and in between comes back
    peqfl::Settings s;
    for (int b = 0; b < 7; ++b)
    {
        const auto ub = (std::size_t) b;
        s.gain[ub] = -1800 + 517 * b;
        s.freq[ub] = 3000 + 9000 * b;
        s.width[ub] = 65536 - 8000 * b;
        s.type[ub] = b % 8;
        s.order[ub] = b - 3;
    }
    s.main = -1234;
    s.mask = 0x55;
    for (auto proc : { peqfl::Processing::plain, peqfl::Processing::hq, peqfl::Processing::hqExternal, peqfl::Processing::hqAlternative })
        for (bool lin : { false, true })
        {
            s.processing = proc;
            s.linearPhase = lin;
            s.linearQuality = lin ? 5 : 2;
            s.linearFade = lin ? 3 : 0;
            peqfl::toParams (s, slot.params);
            const auto d = flstate::parametricEq2 (slot.params, base);
            CHECK (d.size() == base.size());
            const auto back = peqfl::parsePeq2 (d);
            CHECK (back && back->gain == s.gain && back->freq == s.freq && back->width == s.width && back->type == s.type
                   && back->order == s.order && back->main == s.main && back->mask == s.mask && back->processing == s.processing
                   && back->linearPhase == s.linearPhase && back->linearQuality == s.linearQuality && back->linearFade == s.linearFade);
        }
}

TEST (simple189k_looks_apply_remove_bypass)
{
    using namespace lynx;
    Song song;
    song.mixer.resize (3);
    song.mixer[0].effects.push_back (makeEffectSlot (EffectKind::eq));
    CHECK (! looks::present (song));
    for (int id = 1; id <= looks::count; ++id)
    {
        CHECK (std::string (looks::name (id)).size() > 0);
        for (float in : { 0.0f, 0.25f, 0.5f, 0.85f, 1.0f })
        {
            MasterLookSettings s { id, in, { 0, 0, 0 }, 0 };
            const auto c = looks::chain (s);
            CHECK (c.size() == (std::size_t) looks::chainSlots);
            CHECK (c[0].kind == EffectKind::parametricEq && c[1].kind == EffectKind::fruityMultiband && c[3].kind == EffectKind::fruityLimiter);
            CHECK (c[4].kind == EffectKind::apexLimiter); // (round 190ea: the true-peak stage)
            CHECK (c[2].kind == EffectKind::stereoEnhancer || c[2].kind == EffectKind::fruitySoftClipper);
            for (const auto& slot : c)
            {
                CHECK (slot.look == id);
                for (auto v : slot.params)
                    CHECK (std::isfinite (v) && v >= 0.0f && v <= 1.0f);
            }
        }
    }
    MasterLookSettings s { looks::loud, 0.5f, { 2, 0, -1 }, 3 };
    CHECK (looks::apply (song, s));
    CHECK (song.mixer[0].effects.size() == 6 && song.mixer[0].effects[0].look == 0);
    CHECK (song.mixer[0].effects[1].row == 1 && song.mixer[0].effects[5].row == 5);
    CHECK (looks::present (song) && ! looks::custom (song) && song.masterLook == s);
    looks::setBypassed (song, true);
    CHECK (looks::bypassed (song));
    // another look replaces the chain in place and keeps the A/B state
    MasterLookSettings w { looks::wide, 0.85f, { 0, 0, 0 }, 0 };
    CHECK (looks::apply (song, w));
    CHECK (song.mixer[0].effects.size() == 6 && looks::bypassed (song) && song.mixer[0].effects[3].kind == EffectKind::stereoEnhancer);
    looks::setBypassed (song, false);
    CHECK (! looks::bypassed (song));
    // a hand edit makes it "Custom"
    song.mixer[0].effects[2].params[6] = 0.1f;
    CHECK (looks::custom (song));
    looks::remove (song);
    CHECK (song.mixer[0].effects.size() == 1 && ! looks::present (song) && song.masterLook.id == 0);
    // no room: nothing changes
    for (int i = 0; i < 8; ++i)
        song.mixer[0].effects.push_back (makeEffectSlot (EffectKind::eq));
    CHECK (! looks::apply (song, s));
    CHECK (song.mixer[0].effects.size() == 9 && ! looks::present (song));
    // Auto level
    CHECK (looks::autoInputGainDb (-24.0) == 6.0f && looks::autoInputGainDb (-10.0) == -8.0f && looks::autoInputGainDb (-90.0) == 0.0f);
    CHECK (looks::autoInputGainDb (-60.0) == 18.0f);
}

TEST (simple189k_look_exports_fl_plugins)
{
    using namespace lynx;
    Song song;
    song.mixer.resize (3);
    for (int id : { (int) looks::balanced, (int) looks::wide })
    {
        MasterLookSettings s { id, 0.6f, { 1, -1, 2 }, -2 };
        CHECK (looks::apply (song, s));
        const auto& fx = song.mixer[0].effects;
        CHECK (fx.size() == 5); // (round 190ea: + the true-peak stage)
        // each slot is written as FL's own plugin, and the import reader gives its settings back
        auto eq = flpexport::nativeLookState (fx[0]);
        CHECK (eq && eq->first == "Fruity Parametric EQ 2");
        if (eq)
        {
            const auto back = peqfl::parsePeq2 (eq->second);
            const auto want = peqfl::fromParams (fx[0].params);
            CHECK (back && back->gain == want.gain && back->freq == want.freq && back->type == want.type && back->main == want.main);
        }
        auto mb = flpexport::nativeLookState (fx[1]);
        CHECK (mb && mb->first == "Fruity Multiband Compressor" && mb->second.size() == 124);
        if (mb)
        {
            const auto back = flmb::parse (mb->second);
            CHECK (back.has_value());
            if (back)
                for (int i = 0; i < flmb::controls; ++i)
                    CHECK (std::abs (flmb::normalised (back->c[(std::size_t) i], flmb::controlMin[(std::size_t) i], flmb::controlMax[(std::size_t) i])
                                     - fx[1].params[(std::size_t) i]) < 1.0e-6f);
        }
        auto fin = flpexport::nativeLookState (fx[2]);
        CHECK (fin && (fin->first == "Fruity Soft Clipper" || fin->first == "Fruity Stereo Enhancer"));
        auto lim = flpexport::nativeLookState (fx[3]);
        CHECK (lim && lim->first == "Fruity Limiter" && lim->second.size() == 169);
        CHECK (fx[4].kind == EffectKind::apexLimiter && ! flpexport::nativeLookState (fx[4])); // (FL has no true-peak limiter)
    }
    // an ordinary slot is not a look's
    CHECK (! flpexport::nativeLookState (makeEffectSlot (EffectKind::fruityLimiter)));
}

TEST (simple189k_limiter_default_gate_threshold)
{
    using namespace lynx;
    // a new Fruity Limiter has FL's gate threshold (0); a saved project keeps its own value (every control is saved), and
    // with the gate's gain at 0 dB (the default) the old value 100 sounds the same, sample for sample
    auto fresh = makeEffectSlot (EffectKind::fruityLimiter);
    CHECK (fresh.params[16] == 0.0f);
    auto old = fresh;
    old.params[16] = fldyn::normalised (100, fldyn::limiterMin[16], fldyn::limiterMax[16]);
    CHECK (savedEffectParamCount (old) >= 17);
    auto a = makeEffect (fresh), b = makeEffect (old);
    a->prepare (44100.0), b->prepare (44100.0);
    std::uint32_t seed = 1;
    bool same = true;
    for (int block = 0; block < 200; ++block)
    {
        float l1[256], r1[256], l2[256], r2[256];
        for (int i = 0; i < 256; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const auto burst = (block / 20) % 2 == 0 ? 1.5f : 0.002f; // loud bursts and near-silence (gate territory)
            l1[i] = l2[i] = burst * ((float) (seed >> 8) / 8388608.0f - 1.0f);
            r1[i] = r2[i] = 0.5f * l1[i];
        }
        a->process (l1, r1, 256, fresh.params, 120.0);
        b->process (l2, r2, 256, old.params, 120.0);
        for (int i = 0; i < 256; ++i)
            same = same && l1[i] == l2[i] && r1[i] == r2[i];
    }
    CHECK (same);
}
