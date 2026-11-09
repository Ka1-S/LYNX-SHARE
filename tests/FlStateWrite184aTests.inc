// --- Round 184a S8a: FL's own states written from LYNX parameters (core/FlStateWrite.h); each reader of the import must
// give the same parameters back.

#include "core/FlStateWrite.h"
#include "core/FlpExport.h"

TEST (simple184a_flstate_soft_clipper_enhancer)
{
    using namespace lynx;
    auto clip = makeEffectSlot (EffectKind::fruitySoftClipper);
    for (auto [t, post] : { std::pair { 100, 128 }, { 1, 0 }, { 127, 160 }, { 64, 90 } })
    {
        flshape::SoftClipperState s { t, post };
        flshape::softClipperToParams (s, clip.params.data());
        const auto d = flstate::softClipper (clip.params);
        CHECK (d.size() == 8);
        const auto back = flshape::parseSoftClipper (d);
        CHECK (back && back->threshold == t && back->post == post);
    }
    auto wide = makeEffectSlot (EffectKind::stereoEnhancer);
    for (auto s : { stereofx::StereoEnhancerState {}, stereofx::StereoEnhancerState { -128, 0, -96, -512, 0, 2 },
                    stereofx::StereoEnhancerState { 128, 320, 96, 512, 1, 1 }, stereofx::StereoEnhancerState { 10, 250, -40, 0, 1, 0 } })
    {
        stereofx::stereoEnhancerToParams (s, wide.params.data());
        const auto d = flstate::stereoEnhancer (wide.params);
        CHECK (d.size() == 24);
        const auto back = stereofx::parseStereoEnhancer (d);
        CHECK (back && back->pan == s.pan && back->volume == s.volume && back->separation == s.separation && back->delay == s.delay
               && back->post == s.post && back->invert == s.invert);
    }
}

TEST (simple184a_flstate_limiter)
{
    using namespace lynx;
    const auto base = flpexport::fruityLimiterDefaultState();
    auto slot = makeEffectSlot (EffectKind::fruityLimiter);
    // a new LYNX slot writes FL's own fresh state byte for byte, but for the gate threshold: FL's new instance (its
    // Default.fst and the templates) has 0 there, LYNX's limiterDefault 100 (round 189k: noted, not changed here)
    slot.params[16] = 0.0f;
    CHECK (flstate::limiter (slot.params, base) == base);
    // every control at its minimum, its maximum and in between comes back
    for (int pass = 0; pass < 3; ++pass)
    {
        fldyn::LimiterState s;
        for (int i = 0; i < fldyn::limiterControls; ++i)
        {
            const auto lo = fldyn::limiterMin[(std::size_t) i], hi = fldyn::limiterMax[(std::size_t) i];
            s.p[(std::size_t) i] = pass == 0 ? lo : pass == 1 ? hi : lo + (hi - lo) * (i + 1) / 23;
        }
        s.exactCeiling = pass == 1;
        fldyn::limiterToParams (s, slot.params.data());
        const auto d = flstate::limiter (slot.params, base);
        CHECK (d.size() == base.size());
        const auto back = fldyn::parseLimiter (d);
        CHECK (back.has_value());
        if (! back)
            continue;
        CHECK (back->p == s.p && back->exactCeiling == s.exactCeiling && back->sidechain == 0 && back->version == 7);
    }
}

TEST (simple184a_flstate_multiband)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::fruityMultiband);
    for (int pass = 0; pass < 4; ++pass)
    {
        flmb::State s;
        for (int i = 0; i < flmb::controls; ++i)
        {
            const auto lo = flmb::controlMin[(std::size_t) i], hi = flmb::controlMax[(std::size_t) i];
            s.c[(std::size_t) i] = pass == 0 ? flmb::controlDefault[(std::size_t) i] : pass == 1 ? lo : pass == 2 ? hi : lo + (hi - lo) * (i + 3) / 37;
        }
        flmb::toParams (s, slot.params.data());
        const auto d = flstate::multiband (slot.params);
        CHECK (d.size() == 124); // FL's size (version 0, 30 words)
        const auto back = flmb::parse (d);
        CHECK (back && back->version == 0 && back->c == s.c);
    }
}
