// --- FL Studio Mobile (.flm) import, round 190fe: drum-kit cut groups as FLM plays them (a hit spares its own pad, a
// held note's end cuts too), lanes on 3x Osc parameters, an effect's Mix with Post Gain or several LYNX slots (FLM's
// blend as a parallel container), and racks whose instruments and effects take turns (one insert per part)
// (docs/analise/FLM_FORMAT.md §6, §7.4, §9). Every project is written by the tests; no file of Image-Line's is committed.

#include "core/RackOps.h"
#include "core/ThreeOsc.h"

namespace flm190fe
{
using namespace flm182c;
using Bytes = flm182c::Bytes;

// FLM's 3x Osc with every parameter at ½ except Enable, the volume envelope's amount (1: FLM's default, full level)
// and the oscillators' shapes (sine)
inline std::vector<float> threeOsc()
{
    std::vector<float> p (149, 0.5f);
    p[0] = 1.0f;
    p[1] = p[2] = p[3] = 0.0f;
    p[26] = 0.0f; // (no phase randomness)
    p[106] = 1.0f;
    for (int k = 146; k <= 148; ++k)
        p[(std::size_t) k] = 0.0f;
    return p;
}

inline int indexOf (const lynx::Song& s, const std::string& n)
{
    for (std::size_t i = 0; i < s.channels.size(); ++i)
        if (s.channels[i].name == n)
            return (int) i;
    return -1;
}

inline float at (const std::vector<float>& y, double seconds) { return y[(std::size_t) (seconds * 44100.0)]; }
} // namespace flm190fe

// FLM's cut groups (0x1807503a0): any trigger of a pad fades out the voices of the *other* pads of its group, never its
// own; on a lane that holds its notes the note's end is a trigger too
TEST (flm190fe_cut_groups_spare_the_pad_that_plays)
{
    using namespace lynx;
    namespace t = flm190bs;
    namespace sp = flm::samplepad;
    const std::vector<float> d (sp::defaults, sp::defaults + sp::count);
    const auto kit = [&] { return t::rack (flm::kindNormal, {}, { t::sampleOf ({ "A", d, false, 0.0, 1 }), t::sampleOf ({ "B", d, false, 0.0, 1 }),
                                                                   t::sampleOf ({ "C", d, false, 0.0, 1 }), t::sampleOf ({ "D", d }) }); };
    // tempo 120: a beat is 0.5 s. One-shot lane: A at 0 and again at 0.5 beat, B at 1 beat. Held lane: A from 0 for one
    // beat, B from 0.5 beat for 2 beats
    const auto bytes = t::song ({ t::rack (flm::kindMaster, {}), kit(), kit(), t::channel (flm::kindMaster, "MASTER", 0, {}),
                                  t::channel (flm::kindNormal, "Hits", 1, { t::lane (3, "Hits", { t::clip (0, 4, 4, 0, 1, { { 0, 0.25, 0 }, { 0.5, 0.25, 0 }, { 1, 0.25, 1 } }) }) }),
                                  t::channel (flm::kindNormal, "Held", 2, { t::lane (0x103, "Held", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 0 }, { 0.5, 2, 1 } }) }) }) });
    FlpImportReport report;
    const auto song = importFlm (bytes, t::anyFile (&report));
    CHECK (song.has_value());
    if (! song)
        return;
    std::vector<const ChannelDef*> pads;
    for (const auto& c : song->channels)
        if (c.instrument == InstrumentKind::sampler)
            pads.push_back (&c);
    CHECK (pads.size() == 8);
    if (pads.size() != 8)
        return;
    // a group of three: one shared id, sparing the pad that plays; the held lane's pads also cut at a note's end
    for (int k = 0; k < 3; ++k)
    {
        CHECK (pads[(std::size_t) k]->cutGroup > 0 && pads[(std::size_t) k]->cutGroup == pads[(std::size_t) k]->cutBy && pads[(std::size_t) k]->cutOthersOnly
               && ! pads[(std::size_t) k]->cutAtNoteEnd && ! channelCutsItself (*pads[(std::size_t) k]));
        CHECK (pads[(std::size_t) k + 4]->cutOthersOnly && pads[(std::size_t) k + 4]->cutAtNoteEnd);
    }
    CHECK (pads[3]->cutGroup == 0 && ! pads[3]->cutOthersOnly && ! pads[7]->cutAtNoteEnd);
    CHECK (report.converted["FL Studio Mobile: drum pad cut group"] == 2);
    // "Cut itself" from the rack menu turns the FLM rule off
    auto own = *pads[0];
    setChannelCutsItself (own, 1, true);
    CHECK (! own.cutOthersOnly && ! own.cutAtNoteEnd && channelCutsItself (own));

    // heard (a 1 s test sample at 0.5 on every pad): A's second hit adds to its first; B's hit cuts both
    auto hits = *song;
    for (auto& c : hits.channels)
        if (&c - hits.channels.data() >= (pads[4] - song->channels.data()))
            c.muted = true;
    const auto y = t::rendered (hits, 1.0, 0.5f, 44100);
    const auto one = flm190fe::at (y, 0.1);
    CHECK (one > 0.1f);
    CHECK (std::abs (flm190fe::at (y, 0.4) - 2.0f * one) < 1e-3f); // (LYNX's old rule: A cut itself, 1×)
    CHECK (std::abs (flm190fe::at (y, 0.6) - one) < 1e-3f);
    // the held lane: B's hit at 0.25 s cuts A; A's note ends at 0.5 s and cuts B
    auto held = *song;
    for (auto& c : held.channels)
        if (&c - held.channels.data() < (pads[4] - song->channels.data()) && c.instrument == InstrumentKind::sampler)
            c.muted = true;
    const auto h = t::rendered (held, 1.0, 0.5f, 44100);
    CHECK (std::abs (flm190fe::at (h, 0.4) - one) < 1e-3f);
    CHECK (std::abs (flm190fe::at (h, 0.6)) < 1e-4f);
    auto noEnd = held;
    for (auto& c : noEnd.channels)
        c.cutAtNoteEnd = false;
    CHECK (std::abs (flm190fe::at (t::rendered (noEnd, 1.0, 0.5f, 44100), 0.6) - one) < 1e-3f); // (without the rule B goes on)
}

// Every 3x Osc parameter a lane may move writes what the import writes for the same value (the laws are one)
TEST (flm190fe_3xosc_lane_laws_equal_the_import)
{
    using namespace lynx;
    namespace t = flm190de;
    const auto stateFor = [] (const std::vector<float>& p) {
        const auto bytes = flm190bs::song ({ t::rack (flm::kindMaster, {}), t::rack (flm::kindNormal, { t::moduleWith (39, 2, p) }),
                                             t::channel (flm::kindMaster, "MASTER", 0, {}),
                                             t::channel (flm::kindNormal, "Osc", 1, { t::lane (0, "Osc", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }) }) });
        const auto s = importFlm (bytes, flm190bs::anyFile (nullptr));
        const auto* c = s ? flm182c::channelNamed (*s, "Osc") : nullptr;
        return c != nullptr && c->instrument == InstrumentKind::threeosc ? c->pluginState : std::vector<std::uint8_t> {};
    };
    const auto base = flm190fe::threeOsc();
    const auto s0 = stateFor (base);
    CHECK (s0.size() == 460);
    if (s0.size() != 460)
        return;
    std::uint32_t seed = 190;
    int compared = 0;
    for (int k = 1; k < threeosc::flmParamCount; ++k)
    {
        if (! threeosc::isFlmTarget (k) || k == 114)
            continue;
        for (const auto v : { 0.0f, 1.0f, -1.0f })
        {
            seed = seed * 1664525u + 1013904223u;
            const auto x = v >= 0.0f ? v : (float) (seed >> 8) / 16777216.0f;
            auto p = base;
            p[(std::size_t) k] = x;
            const auto expected = stateFor (p);
            auto written = s0;
            // the import folds the offsets (½ here) into the cutoff and resonance: the target holds the sum
            CHECK (threeosc::writeFlmParam (written.data(), k, x, 4, 120.0));
            const auto same = written == expected;
            CHECK (same);
            if (! same)
                std::printf ("  3x Osc parameter %d at %g differs\n", k, (double) x);
            ++compared;
        }
    }
    CHECK (compared == 3 * 110);
    CHECK (! threeosc::writeFlmParam (std::vector<std::uint8_t> (460).data(), 25, 1.0, 4, 120.0)
           && ! threeosc::writeFlmParam (std::vector<std::uint8_t> (460).data(), 114, 1.0, 4, 120.0)
           && ! threeosc::isFlmTarget (113) && ! threeosc::isFlmTarget (106) && ! threeosc::isFlmTarget (140) && ! threeosc::isFlmTarget (147));
}

// The lanes: FLM's values on the engine's targets (the cutoff / resonance offsets and the volume envelope's amount folded
// in), voice settings listed; the engine plays a lane held at v as the import plays v
TEST (flm190fe_3xosc_lanes_play)
{
    using namespace lynx;
    namespace t = flm190de;
    const auto ramp = [] (std::uint32_t id) { return flm187d::autoClip (0, 4, 4, id, { { 0, 1, 0 }, { 4, 1, 65535 } }); };
    auto p = flm190fe::threeOsc();
    p[106] = 0.75f; // the volume envelope's amount: half of FLM's gain
    const auto bytes = flm190bs::song ({
        t::rack (flm::kindMaster, {}), t::rack (flm::kindNormal, { t::moduleWith (39, 2, p) }), t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Osc", 1, { t::lane (0, "Osc", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }),
                                                 t::lane (1, "Osc 1 Coarse Pitch : 3x Osc", { ramp (1) }, 2, 13),
                                                 t::lane (1, "Filter Cutoff Offset : 3x Osc", { ramp (2) }, 2, 115),
                                                 t::lane (1, "Volume : 3x Osc", { ramp (3) }, 2, 114),
                                                 t::lane (1, "Arpeggio Time : 3x Osc", { ramp (4) }, 2, 140) }) });
    FlpImportReport report;
    const auto s = importFlm (bytes, flm190bs::anyFile (&report));
    CHECK (s.has_value());
    if (! s)
        return;
    const auto osc = flm190fe::indexOf (*s, "Osc");
    CHECK (osc >= 0 && s->channels[(std::size_t) osc].instrument == InstrumentKind::threeosc && threeOscIsFlm (s->channels[(std::size_t) osc]));
    const auto* coarse = t::automationOf (*s, AutomationTargetKind::channelParam, osc, threeosc::flmParamBase + 13);
    const auto* cutoff = t::automationOf (*s, AutomationTargetKind::channelParam, osc, threeosc::flmParamBase + 111);
    const auto* volume = t::automationOf (*s, AutomationTargetKind::channelParam, osc, threeosc::flmParamBase + 114);
    CHECK (coarse != nullptr && cutoff != nullptr && volume != nullptr);
    if (coarse != nullptr && coarse->points.size() == 2)
        CHECK (coarse->points[0].value == 0.0f && coarse->points[1].value == 1.0f);
    if (cutoff != nullptr && cutoff->points.size() == 2) // cutoff ½ + 2·offset − 1
        CHECK (cutoff->points[0].value == 0.0f && std::abs (cutoff->points[1].value - 1.0f) < 1e-6f);
    if (volume != nullptr && volume->points.size() == 2) // v · clamp(2·0.75 − 1)
        CHECK (volume->points[0].value == 0.0f && std::abs (volume->points[1].value - 0.5f) < 1e-6f);
    CHECK (report.unsupported.count ("FL Studio Mobile: automation of 3x Osc parameter 140 (not imported)") == 1);
    CHECK (report.unsupported.count ("FL Studio Mobile: automation of 3x Osc parameter 13 (not imported)") == 0);

    // heard: a lane held at a value against the import of that value, without the lane: coarse +12, the cutoff at ¼
    // (on the 3x Osc's filter type 3, HP, so it is heard), the Volume at 0.8 (FLM's gain 0.8 with the envelope amount 0.75)
    const auto compare = [&] (int k, std::uint16_t raw) {
        auto q = flm190fe::threeOsc();
        q[106] = 0.75f;
        q[110] = k == 111 ? 3.0f / 7.0f : 0.0f; // (a high-pass to hear the cutoff; else the filter open)
        q[111] = 1.0f;
        auto still = q;
        still[(std::size_t) k] = (float) raw / 65535.0f;
        const auto make = [&] (const std::vector<float>& params, bool withLane) {
            return flm190bs::song ({ t::rack (flm::kindMaster, {}), t::rack (flm::kindNormal, { t::moduleWith (39, 2, params) }),
                                     t::channel (flm::kindMaster, "MASTER", 0, {}),
                                     withLane ? t::channel (flm::kindNormal, "Osc", 1, { t::lane (0, "Osc", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }),
                                                                                         t::lane (1, "Lane", { flm187d::autoClip (0, 4, 4, 1, { { 0, 1, raw }, { 4, 1, raw } }) }, 2, k) })
                                              : t::channel (flm::kindNormal, "Osc", 1, { t::lane (0, "Osc", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }) }) });
        };
        const auto withLane = importFlm (make (q, true), flm190bs::anyFile (nullptr));
        const auto without = importFlm (make (still, false), flm190bs::anyFile (nullptr));
        if (! withLane || ! without || withLane->automations.size() != 1)
        {
            std::printf ("  import: %d %d automations %d\n", (int) withLane.has_value(), (int) without.has_value(), withLane ? (int) withLane->automations.size() : -1);
            return 1e9;
        }
        const auto x = flm190bs::rendered (*withLane, 0.4, 0.5f, 44100), y = flm190bs::rendered (*without, 0.4, 0.5f, 44100);
        const auto moved = flm190bs::rendered (*importFlm (make (q, false), flm190bs::anyFile (nullptr)), 0.4, 0.5f, 44100);
        double diff = 0.0, peak = 0.0, change = 0.0;
        for (std::size_t i = 0; i < x.size() && i < y.size() && i < moved.size(); ++i)
        {
            diff = std::max (diff, (double) std::abs (x[i] - y[i]));
            peak = std::max (peak, (double) std::abs (y[i]));
            change = std::max (change, (double) std::abs (moved[i] - y[i]));
        }
        if (! (peak > 0.01 && change > 0.01))
            std::printf ("  peak %g change %g\n", peak, change);
        return peak > 0.01 && change > 0.01 ? diff / peak : 1e9; // (the value must change the sound, else nothing is shown)
    };
    for (const auto& [k, raw, tolerance] : { std::tuple<int, std::uint16_t, double> { 13, 49151, 1e-6 }, { 111, 16384, 1e-6 }, { 114, 52428, 1e-4 } })
    {
        const auto d = compare (k, raw);
        CHECK (d < tolerance); // (114: 52428 / 65535 is a hair above 0.8, which the import keeps)
        if (! (d < tolerance))
            std::printf ("  lane on 3x Osc parameter %d: relative difference %g\n", k, d);
    }
}

// FLM's wrapper around an effect: dry × (1 − Mix) + Post Gain × wet × Mix, heard
TEST (flm190fe_effect_mix_with_post_gain_is_flms_blend)
{
    using namespace lynx;
    namespace t = flm190bs;
    using flm182c::Bytes;
    const auto songWith = [] (const std::vector<Bytes>& modules) {
        auto r = flm182c::rack (flm::kindNormal, {}, { t::sampleOf ({ "Hit", {} }) });
        Bytes body (r.begin() + 8, r.end());
        for (const auto& m : modules)
            flm182c::put (body, m.data(), m.size());
        return t::song ({ t::rack (flm::kindMaster, {}), flm182c::chunk ("RACK", body), t::channel (flm::kindMaster, "MASTER", 0, {}),
                          t::channel (flm::kindNormal, "Kit", 1, { t::lane (3, "Kit", { t::clip (0, 4, 4, 0, 1, { { 0, 0.25, 0 } }) }) }) });
    };
    // Equalizer (3) at its defaults (flat), Mix 0.5, Post Gain 0.65 (+6 dB)
    const auto eq = flm190de::moduleWith (3, 2, { 1.0f }, { 0.65f, 0.5f, 0.5f });
    FlpImportReport report;
    const auto s = importFlm (songWith ({ eq }), t::anyFile (&report));
    const auto plain = importFlm (songWith ({}), t::anyFile (nullptr));
    CHECK (s.has_value() && plain.has_value());
    if (! s || ! plain)
        return;
    const auto& fx = s->mixer[1].effects;
    CHECK (fx.size() == 1 && fx[0].kind == EffectKind::parallel);
    if (fx.size() == 1 && fx[0].branches.size() == 2)
        CHECK (fx[0].branches[0].chain.empty() && std::abs (fx[0].branches[0].gain - 0.5f) < 1e-6f && fx[0].branches[1].chain.size() == 1
               && fx[0].branches[1].chain[0].kind == EffectKind::pocketEq && std::abs (fx[0].branches[1].gain - 0.5f * (float) std::pow (10.0, 0.3)) < 1e-5f);
    for (const auto& [what, n] : report.partialEffects)
        CHECK (what.find ("Mix") == std::string::npos && what.find ("Post Gain") == std::string::npos);
    const auto a = t::rendered (*s, 0.5, 0.5f, 44100), b = t::rendered (*plain, 0.5, 0.5f, 44100);
    CHECK (std::abs (flm190fe::at (b, 0.2)) > 0.1f);
    CHECK (std::abs (flm190fe::at (a, 0.2) / flm190fe::at (b, 0.2) - (0.5f + 0.5f * (float) std::pow (10.0, 0.3))) < 2e-3f);
}

// A rack's modules work on one buffer in order (0x18059d3b0): an effect hears only what is before it, and an
// instrument's Post Gain / Mix scales what is already there. Each part becomes an insert sending into the next
TEST (flm190fe_rack_parts_take_turns)
{
    using namespace lynx;
    namespace t = flm190de;
    const auto bytes = flm190bs::song ({
        t::rack (flm::kindMaster, {}),
        // instrument, Graphic EQ, instrument
        t::rack (flm::kindNormal, { t::moduleWith (39, 2, flm190fe::threeOsc()), t::moduleWith (7, 3, { 1.0f }), t::moduleWith (39, 4, flm190fe::threeOsc()) }),
        // a drum pad, then an instrument at Post Gain 0.35 (−6 dB): the pad is lowered with it
        flm182c::rack (flm::kindNormal, { t::moduleWith (39, 2, flm190fe::threeOsc(), { 0.35f, 1.0f, 0.5f }) }, { flm190bs::sampleOf ({ "Hit", {} }) }),
        flm182c::rack (flm::kindNormal, { t::moduleWith (39, 2, flm190fe::threeOsc()) }, { flm190bs::sampleOf ({ "Hit", {} }) }),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Turns", 1, { t::lane (0, "Turns", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }) }),
        t::channel (flm::kindNormal, "Scaled", 2, { t::lane (3, "Scaled", { t::clip (0, 4, 4, 0, 1, { { 0, 0.25, 0 } }) }) }),
        t::channel (flm::kindNormal, "Plain", 3, { t::lane (3, "Plain", { t::clip (0, 4, 4, 0, 1, { { 0, 0.25, 0 } }) }) }) });
    FlpImportReport report;
    const auto s = importFlm (bytes, flm190bs::anyFile (&report));
    CHECK (s.has_value());
    if (! s)
        return;
    const auto* layer = flm182c::channelNamed (*s, "Turns");
    CHECK (layer != nullptr && layer->instrument == InstrumentKind::layer && layer->layer.children.size() == 2);
    if (layer == nullptr || layer->layer.children.size() != 2)
        return;
    const auto& first = s->channels[(std::size_t) layer->layer.children[0]];
    const auto& second = s->channels[(std::size_t) layer->layer.children[1]];
    const auto main = layer->mixerTrack;
    CHECK (second.mixerTrack == main && first.mixerTrack != main && first.mixerTrack > 0);
    if (first.mixerTrack > 0 && first.mixerTrack < (int) s->mixer.size())
    {
        const auto& part = s->mixer[(std::size_t) first.mixerTrack];
        CHECK (part.effects.size() == 1 && part.effects[0].kind == EffectKind::pocketGraphicEq && ! part.toMaster && part.sends.size() == 1
               && part.sends[0].target == main && part.sends[0].level == 1.0f && part.name == "Turns (rack part 1)");
        CHECK (s->mixer[(std::size_t) main].effects.empty());
    }
    CHECK (report.converted["FL Studio Mobile: rack whose instruments and effects take turns (one insert per part)"] == 2);
    CHECK (report.partialEffects.count ("FL Studio Mobile: an instrument placed after an effect in its rack (the effect processes it too)") == 0
           && report.partialEffects.count ("FL Studio Mobile: instrument Post Gain / Mix in a rack with other sounds (the others are not changed)") == 0);
    // heard: the pad of "Scaled" at −6 dB against "Plain" (the instruments get no notes)
    const auto* scaledPad = flm182c::channelNamed (*s, "Hit");
    CHECK (scaledPad != nullptr);
    auto one = *s, other = *s;
    for (auto& c : one.channels)
        c.muted = c.muted || c.mixerTrack != (scaledPad != nullptr ? scaledPad->mixerTrack : -1);
    const auto plainAt = [&] {
        for (const auto& c : s->channels)
            if (c.name == "Hit" && scaledPad != nullptr && &c != scaledPad)
                return c.mixerTrack;
        return -1;
    }();
    for (auto& c : other.channels)
        c.muted = c.muted || c.mixerTrack != plainAt;
    const auto a = flm190bs::rendered (one, 0.3, 0.5f, 44100), b = flm190bs::rendered (other, 0.3, 0.5f, 44100);
    CHECK (std::abs (flm190fe::at (b, 0.1)) > 0.1f);
    CHECK (std::abs (flm190fe::at (a, 0.1) / flm190fe::at (b, 0.1) - (float) std::pow (10.0, -0.3)) < 1e-3f);
}
