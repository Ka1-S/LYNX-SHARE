// Round 190pt (0.1.32): FL's Patcher Control Surface / X-Y-Z controller links (Stroll Up's two master Patchers) and the
// Vocodex controls under FL's automation (the Vocodex demo), each checked against FL 2026's own renders and the
// installation's DLLs (REA_rodada190pt): the X-Y-Z Controller's state read in order and its input curves applied to the
// levels (projects saved before keep the straight path: flInputCurves 0), Patcher cables into X-Y-Z levels and into
// Flanger / Flangus / Fast Dist / Multiband parameters mapped, and Vocodex's 20 controls followed live with FL's band
// set-up (liveControls; projects saved before keep the state alone).
#include "core/Vocodex.h"
#include "core/XyController.h"
#include <cmath>

namespace patcherxyz190pt
{
// an FL X-Y-Z Controller state (version 3): 11 parameters, flags, combo, then per axis 8 bytes and two curves
// (input, output) of the given points (x, y, tension), each followed by FL's 20-byte tail
std::vector<std::uint8_t> xyzState (const std::array<int, 3>& levels, const std::array<std::array<std::vector<std::array<double, 3>>, 2>, 3>& curves)
{
    std::vector<std::uint8_t> d;
    auto i32 = [&d] (std::int32_t v) { const auto* p = (const std::uint8_t*) &v; d.insert (d.end(), p, p + 4); };
    auto f64 = [&d] (double v) { const auto* p = (const std::uint8_t*) &v; d.insert (d.end(), p, p + 8); };
    i32 (3);
    for (int k = 0; k < 11; ++k)
        i32 (k < 3 ? levels[(std::size_t) k] : 0);
    i32 (6), i32 (0x90);
    for (const auto& axis : curves)
    {
        i32 (0), i32 (0);
        for (const auto& curve : axis)
        {
            i32 (3), i32 (3), i32 ((std::int32_t) curve.size());
            for (std::size_t k = 0; k < curve.size(); ++k)
            {
                f64 (curve[k][0]), f64 (curve[k][1]);
                const auto t = (float) curve[k][2];
                const auto* p = (const std::uint8_t*) &t;
                d.insert (d.end(), p, p + 4);
                d.push_back (0), d.push_back (0), d.push_back (0);
                d.push_back (k == 0 ? 0 : t == 0.0f ? 2 : t > 0.0f ? 1 : 0xFF);
            }
            i32 (0);
            for (int k = 0; k < 4; ++k)
                i32 (-1);
        }
    }
    return d;
}

const std::vector<std::array<double, 3>> straight { { 0, 0, 0 }, { 1, 1, 0 } };

float xyzOutput (const lynx::EffectSlot& slot, int axis, int blocks = 4)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (44100.0);
    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    for (int b = 0; b < blocks; ++b)
        fx->process (l.data(), r.data(), 256, slot.params, 120.0);
    return fx->controlOutput (axis);
}

std::vector<std::uint8_t> readFile (const std::filesystem::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), {});
}

std::filesystem::path demos()
{
    const auto d = std::filesystem::path (realFlpDir) / "Data" / "Demo projects";
    return std::filesystem::exists (d) ? d : std::filesystem::path (realFlpDir) / "Demo projects";
}

lynx::EffectSlot savedAndReloaded (const lynx::EffectSlot& slot)
{
    auto loaded = lynx::makeEffectSlot (slot.kind);
    loaded.state = slot.state;
    const auto count = lynx::savedEffectParamCount (slot);
    for (std::size_t i = 0; i < count; ++i)
        loaded.params[i] = std::clamp ((float) lynx::savedEffectParamValue (slot.params[i]), 0.0f, 1.0f);
    lynx::upgradeLoadedEffectParams (loaded, count);
    return loaded;
}
} // namespace patcherxyz190pt

// The X-Y-Z state read in order: a 3-point input curve moves every curve after it (the fixed places read before took
// other bytes there); the input curves are FL's envelope walk, a segment of no length passed at once
TEST (patcherxyz190pt_xyz_state_and_input_curves)
{
    using namespace lynx;
    using patcherxyz190pt::straight;
    // Stroll Up's "mapper1" shape: X falls 1 → 0.5 over the first half and holds; Y holds 0.5 and jumps to 1 at 0.5;
    // the output curves are 2-point with ends 0.1 … 0.9 on Y (to see that they are read where they really are)
    const auto d = patcherxyz190pt::xyzState ({ 4522, 4522, 52264 }, { { { { { { 0, 1, 0 }, { 0.5, 0.5, 0 }, { 0.5, 0.5, 0 } }, straight } },
                                                                        { { { { 0, 0.5, 0 }, { 0.5, 0.5, 0 }, { 0.5, 1, 0 } }, { { 0, 0.1, 0 }, { 1, 0.9, 0 } } } },
                                                                        { { { { 0, 0.5, 0 }, { 1, 1, 0 } }, straight } } } });
    CHECK (d.size() == 608); // (FL's own size for such a state, Stroll Up's mapper1)
    const auto curves = xy::flCurves (d);
    CHECK (curves.size() == 3 && curves[0][0].size() == 3 && curves[1][1].size() == 2);
    CHECK (std::abs (xy::evaluate (curves[0][0], 0.25) - 0.75) < 1e-12 && xy::evaluate (curves[0][0], 0.9) == 0.5);
    CHECK (xy::evaluate (curves[1][0], 0.49) == 0.5 && xy::evaluate (curves[1][0], 0.5) == 1.0 && xy::evaluate (curves[1][0], 0.7) == 1.0);
    CHECK (std::abs (xy::evaluate (curves[2][0], 0.7975) - 0.89875) < 1e-12);
    CHECK (xy::isIdentity (curves[0][1]) && ! xy::isIdentity (curves[0][0]));
    // a tension bends the segment with FL's single-curve law (Stroll Up's clip mappers: −0.057 gives post 141 of 160)
    const std::vector<xy::CurvePoint> bent { { 0, 0.8, 0, 0 }, { 1, 0.9375, -0.057f, 0 } };
    CHECK (std::lround (160.0 * xy::evaluate (bent, 0.674561)) == 141);

    auto slot = xy::fromFl ("Fruity X-Y-Z Controller", d);
    CHECK (slot.has_value());
    if (! slot)
        return;
    CHECK (slot->params[(std::size_t) xy::flInputCurves] == 1.0f);
    CHECK (std::abs (slot->params[(std::size_t) xy::minimum (1)] - 0.1f) < 1e-6f && std::abs (slot->params[(std::size_t) xy::maximum (1)] - 0.9f) < 1e-6f);
    CHECK (slot->params[(std::size_t) xy::minimum (0)] == 0.0f && slot->params[(std::size_t) xy::maximum (0)] == 1.0f);
    // the level goes through the input curve, then the output curve: X 0.069 → 0.931; Y 0.069 → 0.5 → 0.1 + 0.8 · 0.5
    CHECK (std::abs (patcherxyz190pt::xyzOutput (*slot, 0) - (1.0f - 4522.0f / 65536.0f)) < 1e-5f);
    CHECK (std::abs (patcherxyz190pt::xyzOutput (*slot, 1) - 0.5f) < 1e-5f);
    auto moved = *slot;
    moved.params[(std::size_t) xy::level (1)] = 0.75f; // (a Patcher cable into the Y level: past the jump)
    CHECK (std::abs (patcherxyz190pt::xyzOutput (moved, 1) - 0.9f) < 1e-5f);

    // projects saved before (flInputCurves 0): the level is the target, as before, bit for bit
    auto old = *slot;
    old.params[(std::size_t) xy::flInputCurves] = 0.0f;
    auto plain = old;
    plain.state.clear();
    for (int a = 0; a < 3; ++a)
        CHECK (patcherxyz190pt::xyzOutput (old, a) == patcherxyz190pt::xyzOutput (plain, a));
    CHECK (patcherxyz190pt::xyzOutput (old, 0) == xy::output (EffectKind::xyzController, old.params, 0, (double) old.params[(std::size_t) xy::level (0)]));
    // the switch is a hidden parameter: 0 is not saved, 1 comes back from the file
    CHECK (savedEffectParamCount (old) == (std::size_t) xy::paramCount (EffectKind::xyzController));
    CHECK (patcherxyz190pt::savedAndReloaded (*slot).params[(std::size_t) xy::flInputCurves] == 1.0f);
    CHECK (patcherxyz190pt::savedAndReloaded (old).params[(std::size_t) xy::flInputCurves] == 0.0f);
    // a state whose input curves are all straight sets nothing (every X-Y-Z of FL's other demos)
    const auto plainState = patcherxyz190pt::xyzState ({ 32768, 32768, 32768 }, { { { { straight, straight } }, { { straight, straight } }, { { straight, straight } } } });
    CHECK (plainState.size() == 560);
    if (const auto s = xy::fromFl ("Fruity X-Y-Z Controller", plainState))
        CHECK (s->params[(std::size_t) xy::flInputCurves] == 0.0f);
}

// Patcher cables (and automation) into the X-Y-Z levels and into Flanger / Flangus / Fast Dist / Multiband Compressor
// parameters map; the Multiband line had lost its return in a merge, which dropped all four
TEST (patcherxyz190pt_fl_parameter_maps)
{
    using namespace lynx;
    for (int k = 0; k < 3; ++k)
    {
        const auto map = effectParamFromFl ("Fruity X-Y-Z Controller", k);
        CHECK (map && map->param == xy::level (k) && map->convert && map->convert (0.3f) == 0.3f);
    }
    CHECK (! effectParamFromFl ("Fruity X-Y-Z Controller", 3));
    for (const char* name : { "Fruity Flanger", "Fruity Flangus", "Fruity Fast Dist", "Fruity Multiband Compressor" })
    {
        const auto map = effectParamFromFl (name, 1);
        CHECK (map && map->param == 1);
    }
    for (int k = 0; k < vocodex::paramCount; ++k)
    {
        const auto map = effectParamFromFl ("Vocodex", k);
        CHECK (map && map->param == k);
    }
    // the DLL's own ranges (ProcessParam REC_FromMIDI): release 0…10000, modulator pitch −1200…1200, unison pan −256…256
    CHECK (vocodex::controlRange (11) == std::make_pair (0, 10000));
    CHECK (vocodex::controlRange (17) == std::make_pair (-1200, 1200));
    CHECK (vocodex::controlRange (19) == std::make_pair (-256, 256));
    CHECK (vocodex::controlRange (14) == std::make_pair (5, 1000));
}

// Stroll Up's two master Patchers: every Control Surface / X-Y-Z cable maps (none "not mapped" any more), and with the
// cables the graphs give what FL's saved states hold (the links agree with them in FL's file)
TEST (patcherxyz190pt_stroll_up_patcher_links)
{
    if (realFlpDir.empty())
        return;
    const auto path = patcherxyz190pt::demos() / "Demo songs" / "Purps On The Beat Feat Luclover - Stroll Up.flp";
    if (! std::filesystem::exists (path))
        return;
    lynx::FlpImportOptions options;
    options.roots = { realFlpDir, realFlpDir + "/Data", "" };
    lynx::FlpImportReport report;
    options.report = &report;
    const auto song = lynx::importFlp (flp::parseProject (patcherxyz190pt::readFile (path)), options);
    for (const auto& [what, n] : report.partialEffects)
        CHECK (what.find ("not mapped") == std::string::npos && what.find ("not reproduced") == std::string::npos);
    int patchers = 0, xyzWithCurves = 0, links = 0, intoXyz = 0, intoFlangers = 0;
    for (const auto& fx : song.mixer[0].effects)
        if (fx.kind == lynx::EffectKind::patcher)
        {
            ++patchers;
            for (const auto& n : fx.patcher.nodes)
                xyzWithCurves += n.effect.kind == lynx::EffectKind::xyzController && n.effect.params[(std::size_t) lynx::xy::flInputCurves] == 1.0f;
            for (const auto& w : fx.patcher.wires)
                if (w.kind == lynx::PatchWire::Kind::control)
                {
                    ++links;
                    const auto kind = fx.patcher.nodes[(std::size_t) w.to].effect.kind;
                    intoXyz += kind == lynx::EffectKind::xyzController;
                    intoFlangers += kind == lynx::EffectKind::fruityFlanger || kind == lynx::EffectKind::fruityFlangus;
                }
        }
    CHECK (patchers == 2);
    CHECK (xyzWithCurves >= 10 && intoXyz >= 30 && intoFlangers >= 30 && links >= 150);
}

// Vocodex follows its controls when their parameters move (FL's automation), with FL's band set-up on the running
// signal; a slot without liveControls (projects saved before) keeps the state alone
TEST (patcherxyz190pt_vocodex_follows_its_controls)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto path = patcherxyz190pt::demos() / "Product demos" / "Vocodex demo.flp";
    if (! std::filesystem::exists (path))
        return;
    const auto project = flp::parseProject (patcherxyz190pt::readFile (path));
    std::vector<std::uint8_t> state;
    for (const auto& ins : project.mixer)
        for (const auto& s : ins.slots)
            if (s.plugin == "Vocodex")
                state = s.state;
    CHECK (! state.empty());
    const auto parsed = vocodex::parseState (state);
    CHECK (parsed.has_value());
    if (! parsed)
        return;
    // the import writes the controls as parameters and switches them on
    const auto conv = vocodexEffect (state);
    CHECK (conv.has_value() && conv->slot.params[(std::size_t) vocodex::liveControls] == 1.0f);
    for (int k = 0; k < vocodex::paramCount && conv; ++k)
    {
        const auto [lo, hi] = vocodex::controlRange (k);
        CHECK (lo + (int) std::nearbyint ((double) conv->slot.params[(std::size_t) k] * (hi - lo)) == parsed->params[(std::size_t) k]);
    }
    // modulator = a pulse train, carrier = a saw (own input); release and modulator pitch move at block 40
    auto render = [&] (bool live, bool move) {
        auto slot = makeEffectSlot (EffectKind::vocodex);
        slot.state = state;
        if (live)
            vocodex::controlsToParams (*parsed, slot.params);
        auto fx = makeEffect (slot);
        fx->prepare (44100.0);
        std::vector<float> out, l (512), r (512), ml (512), mr (512);
        for (int b = 0; b < 120; ++b)
        {
            if (move && b == 40)
            {
                slot.params[11] = 0.4764f;
                slot.params[17] = (float) ((-600.0 + 1200.0) / 2400.0);
            }
            for (int i = 0; i < 512; ++i)
            {
                const auto t = b * 512 + i;
                l[(std::size_t) i] = r[(std::size_t) i] = 0.2f * (float) (2.0 * std::fmod (t * 130.81 / 44100.0, 1.0) - 1.0);
                ml[(std::size_t) i] = mr[(std::size_t) i] = t % 300 == 0 ? 0.8f : 0.0f;
            }
            fx->setSidechain (ml.data(), mr.data());
            fx->process (l.data(), r.data(), 512, slot.params, 120.0);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    const auto statePath = render (false, false), liveStill = render (true, false), liveMoved = render (true, true), oldMoved = render (false, true);
    CHECK (statePath == liveStill); // nothing moved: the same samples
    CHECK (statePath == oldMoved);  // a slot without liveControls ignores its parameters, as before
    CHECK (liveMoved != statePath);
    CHECK (std::equal (liveMoved.begin(), liveMoved.begin() + 40 * 512, statePath.begin())); // (until the change)
    bool finite = true;
    for (const auto v : liveMoved)
        finite = finite && std::isfinite (v);
    CHECK (finite);
    // the switch survives a save and reload; a slot of a project saved before has it off
    CHECK (patcherxyz190pt::savedAndReloaded (conv->slot).params[(std::size_t) vocodex::liveControls] == 1.0f);
    CHECK (makeEffectSlot (EffectKind::vocodex).params[(std::size_t) vocodex::liveControls] == 0.0f);
}
