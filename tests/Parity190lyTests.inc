// Round 190ly (Valhalla → LYNX stand-ins, part 2). Included after Parity190lsTests.inc.
// 1. The owner's perf rule: Atrium's dense 32-line late tank (verbs::atrium::reaLaw, round 190ls, +48 % instructions) is
//    set only when a ValhallaRoom is translated (the substitute table's `replaced = #24=1`); a new Atrium — and one added
//    by hand from a substitute menu — keeps the light 8-line tank, and renders as revision 1 did (Parity190ls's gold).
#include <cmath>
#include <cstdint>
#include <string>

#include "core/Effects.h"
#include "core/LynxVerbs.h"
#include "core/PluginSubstitutes.h"

TEST (parity190ly_atrium_dense_tank_only_for_a_translated_valhallaroom)
{
    using namespace lynx;
    namespace at = verbs::atrium;
    // a new Atrium: revision 1, the light tank, saved as before round 190ls (one value past the controls)
    const auto fresh = makeEffectSlot (EffectKind::atrium);
    CHECK (fresh.params[(std::size_t) at::revision] == 1.0f && fresh.params[(std::size_t) at::reaLaw] == 0.0f);
    CHECK (savedEffectParamCount (fresh) == (std::size_t) at::revision + 1);
    auto loaded = fresh; // and it loads back the same
    upgradeLoadedEffectParams (loaded, savedEffectParamCount (fresh));
    CHECK (loaded.params[(std::size_t) at::reaLaw] == 0.0f && loaded.params[(std::size_t) at::revision] == 1.0f);

    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "ValhallaRoom";
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr)
        return;
    CHECK (entry->effect == EffectKind::atrium);
    // translated (with or without a readable state): the dense tank, saved with it and kept on load
    const auto blank = makeSubstituteSlot (*entry, &id);
    CHECK (blank.params[(std::size_t) at::reaLaw] == 1.0f && blank.params[(std::size_t) at::revision] == 1.0f);
    const std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ValhallaRoom pluginVersion=\"1.6.6\" mix=\"0.3\" "
                            "decay=\"0.05\" type=\"0.4162\" predelay=\"0.1\"/>";
    id.state.assign (xml.begin(), xml.end());
    int n = 0;
    auto translated = makeSubstituteSlot (*entry, &id, &n);
    CHECK (n > 0);
    CHECK (translated.params[(std::size_t) at::reaLaw] == 1.0f && translated.params[(std::size_t) at::revision] == 1.0f);
    CHECK (savedEffectParamCount (translated) == (std::size_t) at::reaLaw + 1);
    auto reopened = translated;
    upgradeLoadedEffectParams (reopened, savedEffectParamCount (translated));
    CHECK (reopened.params[(std::size_t) at::reaLaw] == 1.0f);
    // the same entry added by hand (no original device): the light tank
    const auto byHand = makeSubstituteSlot (*entry);
    CHECK (byHand.params[(std::size_t) at::reaLaw] == 0.0f && byHand.params[(std::size_t) at::revision] == 1.0f);
    // no other entry carries replaced items onto an Atrium
    for (const auto& e : pluginSubstitutes())
        if (e.effect == EffectKind::atrium && e.key != "valhalla-room")
            CHECK (e.replacedPreset.empty());
    // the table grammar: replaced items take hidden "#<index>" positions like preset items, and bad ones count as errors
    int errors = 0;
    const auto t = parseSubstituteTable ("[x]\nproduct = X\nlynx = Atrium\nnames = X\nreplaced = #24=1; Decay=0.5\n", &errors);
    CHECK (errors == 0 && t.size() == 1);
    if (t.size() == 1)
        CHECK (t[0].replacedPreset.size() == 2 && t[0].preset.empty() && t[0].replacedPreset[0].first == at::reaLaw);
    parseSubstituteTable ("[x]\nproduct = X\nlynx = Atrium\nnames = X\nreplaced = #3=1\n", &errors); // a control: by name only
    CHECK (errors == 1);
}

namespace parity190ly
{
// stereo render of a slot at `sr` in blocks of 256 from a reset
inline void render (const lynx::EffectSlot& slot, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    const int n = (int) l.size();
    for (int off = 0; off < n; off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, n - off), slot.params, 120.0);
}
// energy (dB) of both sides in [a, b) seconds
inline double windowDb (const std::vector<float>& l, const std::vector<float>& r, double sr, double a, double b)
{
    double e = 0.0;
    for (int i = (int) (a * sr); i < std::min ((int) l.size(), (int) (b * sr)); ++i)
        e += (double) l[(std::size_t) i] * l[(std::size_t) i] + (double) r[(std::size_t) i] * r[(std::size_t) i];
    return 10.0 * std::log10 (e + 1e-30);
}
} // namespace parity190ly

// 2. ValhallaFreqEcho at 96 kHz: Spiral Echo's shifted loop read its analytic filter (fixed per-sample coefficients) at
//    another phase at every rate; the hidden rateLaw turns it onto 48 kHz's (refcap030 sr96 3.31 -> 4.81, sr48 identical)
TEST (parity190ly_spiral_loop_phase_does_not_depend_on_the_rate)
{
    using namespace lynx;
    namespace sp = echosuite::spiral;
    CHECK (effectInfo (EffectKind::spiralEcho).params.size() == (std::size_t) sp::paramCount); // hidden
    CHECK ((int) sp::rateLaw == (int) sp::revision + 1);
    const auto s = makeEffectSlot (EffectKind::spiralEcho);
    CHECK (s.params[(std::size_t) sp::rateLaw] == 1.0f && savedEffectParamCount (s) == (std::size_t) sp::rateLaw + 1);
    auto before = s; // a Spiral Echo saved before this round: as it was
    upgradeLoadedEffectParams (before, (std::size_t) sp::revision + 1);
    CHECK (before.params[(std::size_t) sp::rateLaw] == 0.0f && before.params[(std::size_t) sp::revision] == 1.0f);
    // the turn: none at 48 kHz, about −67° and half a sample later at 96 kHz, a few degrees at 44.1 kHz
    double th = 1.0, adv = 1.0;
    sp::rateTurn (48000.0, th, adv);
    CHECK (th == 0.0 && adv == 0.0);
    sp::rateTurn (96000.0, th, adv);
    CHECK (std::abs (th * 180.0 / 3.14159265358979323846 + 66.7) < 2.0 && std::abs (adv + 0.52) < 0.1);
    sp::rateTurn (44100.0, th, adv);
    CHECK (std::abs (th * 180.0 / 3.14159265358979323846 - 8.2) < 2.0 && std::abs (adv) < 0.2);
    // a steady 1 kHz tone through the loop (Mix 100 %, the defaults): the same level at 44.1 / 48 / 96 kHz with the law
    const auto toneDb = [] (float law, double sr) {
        auto slot = makeEffectSlot (EffectKind::spiralEcho);
        slot.params[(std::size_t) sp::mix] = 1.0f;
        slot.params[(std::size_t) sp::rateLaw] = law;
        const int n = (int) (3.0 * sr);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        for (int i = 0; i < n; ++i)
            l[(std::size_t) i] = r[(std::size_t) i] = (float) (0.1 * std::sin (2.0 * 3.14159265358979323846 * 1000.0 * i / sr));
        parity190ly::render (slot, sr, l, r);
        return parity190ly::windowDb (l, r, sr, 1.5, 3.0) - 10.0 * std::log10 (1.5 * sr);
    };
    const auto at48 = toneDb (1.0f, 48000.0);
    std::printf ("  spiral 1 kHz tone: 48k %.2f, 96k %.2f (law off %.2f), 44.1k %.2f dB\n", at48, toneDb (1.0f, 96000.0), toneDb (0.0f, 96000.0),
                 toneDb (1.0f, 44100.0));
    CHECK (std::abs (toneDb (1.0f, 96000.0) - at48) < 0.4);
    CHECK (std::abs (toneDb (1.0f, 44100.0) - at48) < 0.4);
    CHECK (std::abs (toneDb (0.0f, 96000.0) - at48) > 1.5); // what the law fixes
    CHECK (toneDb (0.0f, 48000.0) == at48);                  // and at 48 kHz it is the old path
}

// 3. ValhallaDelay at Feedback 100 %: the reference's echoes fade (−1.4 … −1.5 dB a pass), Echoform's measured tape loop grew;
//    and its diffusion keeps the echo time. Hidden valhallaLaw, set by the ValhallaDelay translator only
TEST (parity190ly_echoform_valhalla_law_fades_at_feedback_100_and_keeps_the_time)
{
    using namespace lynx;
    namespace ef = echosuite::echoform;
    // (rollup 0.1.30 step 10: after round 190kw's lineLaw and round 190lf's five hybrid values; the table's `replaced` item
    // names the index: it must set this law and nothing of the others)
    CHECK ((int) ef::valhallaLaw == (int) ef::hybridRate + 1);
    CHECK (makeEffectSlot (EffectKind::echoform).params[(std::size_t) ef::valhallaLaw] == 0.0f); // a new Echoform: as before
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "ValhallaDelay";
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr && entry->effect == EffectKind::echoform);
    if (entry == nullptr)
        return;
    const std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ValhallaDelay pluginVersion=\"3.0.5\" Mix=\"1.0\" DelayStyle=\"0.0\" "
                            "DelayLSync=\"0.25\" DelayL_Ms=\"0.3\" DelayRSync=\"0.25\" DelayR_Ms=\"0.3\" Feedback=\"0.5\" Age=\"0.0\" "
                            "Diffusion=\"0.2\" DiffSize=\"1.0\" Wow=\"0.0\" Flutter=\"0.0\" Mode=\"0.0416666679084301\" Era=\"0.3333\"/>";
    id.state.assign (xml.begin(), xml.end());
    const auto vd = makeSubstituteSlot (*entry, &id);
    CHECK (vd.params[(std::size_t) ef::valhallaLaw] == 1.0f && savedEffectParamCount (vd) == (std::size_t) ef::valhallaLaw + 1);
    for (int k = (int) ef::lineLaw; k <= (int) ef::hybridRate; ++k)
        CHECK (vd.params[(std::size_t) k] == 0.0f); // rollup step 10: not the Timeless 3 line law nor the H-Delay law
    CHECK (std::abs (ef::timeMs (vd.params[ef::timeL]) - 300.0) < 1.0 && std::abs (vd.params[ef::feedback] - 0.5f) < 1e-6f);
    CHECK (makeSubstituteSlot (*entry).params[(std::size_t) ef::valhallaLaw] == 0.0f); // added by hand: as before
    auto old = vd;
    upgradeLoadedEffectParams (old, (std::size_t) ef::valhallaLaw);
    CHECK (old.params[(std::size_t) ef::valhallaLaw] == 0.0f);
    // an impulse at 0.1 s: the echo train's pass energies (windows of one echo time around n × 300 ms)
    const double sr = 48000.0;
    const auto passes = [sr] (const EffectSlot& slot, std::vector<double>& out, double& centreMs) {
        const int n = (int) (4.0 * sr);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        l[(std::size_t) (0.1 * sr)] = r[(std::size_t) (0.1 * sr)] = 0.5f;
        parity190ly::render (slot, sr, l, r);
        out.clear();
        for (int k = 1; k <= 10; ++k)
            out.push_back (parity190ly::windowDb (l, r, sr, 0.1 + 0.3 * k - 0.15, 0.1 + 0.3 * k + 0.15));
        double e = 0.0, t = 0.0; // the first echo's energy centre
        for (int i = (int) (0.25 * sr); i < (int) (0.55 * sr); ++i)
        {
            const auto v = (double) l[(std::size_t) i] * l[(std::size_t) i] + (double) r[(std::size_t) i] * r[(std::size_t) i];
            e += v, t += v * i;
        }
        centreMs = (t / std::max (1e-30, e) / sr - 0.1) * 1000.0;
    };
    std::vector<double> a, b;
    double ca = 0.0, cb = 0.0;
    passes (vd, a, ca);
    auto plain = vd;
    plain.params[(std::size_t) ef::valhallaLaw] = 0.0f;
    passes (plain, b, cb);
    const auto slope = [] (const std::vector<double>& v) { return (v[8] - v[2]) / 6.0; };
    std::printf ("  ValhallaDelay role, Feedback 100 %%: %.2f dB a pass (law off %.2f); first echo centre %.1f ms (law off %.1f)\n", slope (a),
                 slope (b), ca, cb);
    CHECK (slope (a) < -0.6 && slope (a) > -2.5); // the reference: −1.5
    CHECK (slope (b) > slope (a) + 0.5);
    CHECK (std::abs (ca - 300.0) < std::abs (cb - 300.0)); // the diffused echo stays on its time
    for (const auto v : a)
        CHECK (std::isfinite (v));
}

// 4. ValhallaVintageVerb Concert Hall at short decays: a trim at 0.51 s faded out by 2.9 s (hidden shortTrim)
TEST (parity190ly_epoch_concert_hall_short_decay_trim)
{
    using namespace lynx;
    namespace ep = verbs::epoch;
    // (rollup 0.1.30 step 10: after round 190kw's loopLaw, which a new Epoch also runs)
    CHECK (ep::shortTrim == ep::loopLaw + 1 && effectInfo (EffectKind::epochVerb).params.size() == (std::size_t) ep::paramCount);
    const auto s = makeEffectSlot (EffectKind::epochVerb);
    // (rollup 0.1.30 step 11: a new Epoch runs round 190kw's loop law and round 190mb's refit of the trim on top of it; the
    // rate law after it is set only by the VintageVerb translator)
    CHECK (s.params[(std::size_t) ep::shortTrim] == 1.0f && s.params[(std::size_t) ep::rateLaw] == 0.0f
           && savedEffectParamCount (s) == (std::size_t) ep::shortTrim + 1);
    auto rev1 = s; // saved by 0.1.30 before this round
    upgradeLoadedEffectParams (rev1, (std::size_t) ep::revision + 1);
    CHECK (rev1.params[(std::size_t) ep::shortTrim] == 0.0f && rev1.params[(std::size_t) ep::revision] == 1.0f);
    auto loop = s; // rollup step 10: saved with round 190kw's loop law, before this round: keeps the loop law, no trim
    upgradeLoadedEffectParams (loop, (std::size_t) ep::loopLaw + 1);
    CHECK (loop.params[(std::size_t) ep::shortTrim] == 0.0f && loop.params[(std::size_t) ep::loopLaw] == 1.0f);
    const double sr = 48000.0;
    const auto run = [sr] (float decay, float mode, float trim, std::vector<float>& l, std::vector<float>& r) {
        auto slot = makeEffectSlot (EffectKind::epochVerb);
        slot.params[0] = 1.0f;  // Mix
        slot.params[2] = decay; // Decay
        slot.params[16] = mode; // Mode (0 = Concert Hall)
        slot.params[(std::size_t) ep::shortTrim] = trim;
        const int n = (int) (1.5 * sr);
        l.assign ((std::size_t) n, 0.0f), r.assign ((std::size_t) n, 0.0f);
        l[(std::size_t) (0.05 * sr)] = r[(std::size_t) (0.05 * sr)] = 0.5f;
        parity190ly::render (slot, sr, l, r);
    };
    std::vector<float> l0, r0, l1, r1;
    // Decay 0.2 (0.51 s), Concert Hall: the trim moves the sound (louder: the reference sat 2 dB over)
    run (0.2f, 0.0f, 0.0f, l0, r0);
    run (0.2f, 0.0f, 1.0f, l1, r1);
    const auto d = parity190ly::windowDb (l1, r1, sr, 0.0, 1.5) - parity190ly::windowDb (l0, r0, sr, 0.0, 1.5);
    std::printf ("  Epoch Concert Hall 0.51 s: short-decay trim %+.2f dB\n", d);
    CHECK (d > 0.2 && d < 3.5); // (round 190mb: refitted on top of round 190kw's loop law, which now carries the level: +0.5 dB)
    // from 2.9 s (Decay ≈ 0.38) and in other modes: bit for bit as without it
    run (0.40f, 0.0f, 0.0f, l0, r0);
    run (0.40f, 0.0f, 1.0f, l1, r1);
    CHECK (l0 == l1 && r0 == r1);
    run (0.2f, 0.5f, 0.0f, l0, r0);
    run (0.2f, 0.5f, 1.0f, l1, r1);
    CHECK (l0 == l1 && r0 == r1);
}
