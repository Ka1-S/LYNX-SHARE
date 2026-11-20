// --- Round 190jv (BUILD-LIST 19 "FLM residue", FLM coverage instruments-0022): the "FL Studio Mobile Rack" generator of
// an .flp with several instruments (a Layer whose children are the rack's instruments, each on its .flm engine and key
// range) and with the rack's parts (an effect before an instrument hears only what comes before it: FLM_FORMAT §8.2).
// Every state is written by the tests (flm182c's / flm190fn's helpers). Also Pitcher 2 on LYNX Tune as FL renders it.

#include "core/FlmEffects.h"
#include "core/LynxTune.h"

namespace flm190jv
{
using Bytes = flm182c::Bytes;
namespace flm = lynx::flm;

inline Bytes saw (std::uint32_t id, int low = 0, int high = 127)
{
    return flm182c::module (23, id, {}, "Default/Default", low, high, lynx::pocketsaw::flmParamCount);
}
inline Bytes filter (std::uint32_t id) { return flm182c::module (9, id, {}, "Default/Default", 0, 127, 5); }
inline Bytes graphicEq (std::uint32_t id) { return flm182c::module (7, id, {}, "Default/Default", 0, 127, 4); }

// the rack in an FL project: channel 0 "Lead" (FL insert 3), then a sampler channel "After" (FL insert 4); one pattern
// with a note on each
inline ::flp::Project project (const Bytes& state)
{
    auto p = flm190fn::flpWithRack (state, 3);
    ::flp::Channel after;
    after.index = 1;
    after.type = ::flp::ChannelType::sampler;
    after.name = "After";
    after.mixerTrack = 4;
    p.channels.push_back (after);
    ::flp::Pattern pat;
    pat.index = 1;
    for (std::uint16_t ch = 0; ch < 2; ++ch)
    {
        ::flp::Note n;
        n.position = 96u * ch;
        n.length = 96;
        n.channel = ch;
        n.key = 60;
        pat.notes.push_back (n);
    }
    p.patterns.push_back (pat);
    return p;
}

inline lynx::FlpImportOptions options (lynx::FlpImportReport& report)
{
    lynx::FlpImportOptions o;
    o.report = &report;
    o.fileExists = [] (const std::string&) { return false; };
    return o;
}
} // namespace flm190jv

TEST (flm190jv_flp_mobile_rack_with_several_instruments_is_a_layer)
{
    using namespace lynx;
    namespace t = flm190jv;
    // two SuperSaws split at C5 (the second panned right by its Wet Pan), then a Filter after both
    const auto state = flm190fn::rackState (flm182c::rack (flm::kindNormal, { t::saw (2, 0, 59), flm190fn::withReserved (t::saw (3, 60, 127), 0.5f, 1.0f, 0.75f),
                                                                           t::filter (4) }));
    FlpImportReport report;
    const auto s = importFlp (t::project (state), t::options (report));
    CHECK (s.channels.size() == 4);
    if (s.channels.size() != 4)
        return;
    const auto& layer = s.channels[0];
    CHECK (layer.name == "Lead" && layer.instrument == InstrumentKind::layer);
    CHECK ((layer.layer.children == std::vector<int> { 1, 2 }));
    // the Layer only hands the notes on (0 dB, centred, unpitched); each child is the instrument on FL's channel as a
    // one-instrument rack would be: FL's default volume, the module's Wet Pan on FL's pan
    CHECK (layer.volume == 1.0f && layer.pan == 0.0f && layer.sampler.pitch == 0.0f && layer.flOrigin.empty());
    const auto& low = s.channels[1];
    const auto& high = s.channels[2];
    CHECK (low.instrument == InstrumentKind::pocketSuperSaw && high.instrument == InstrumentKind::pocketSuperSaw);
    CHECK (low.name == "Lead (SuperSaw)" && low.keyLow == 0 && low.keyHigh == 59 && high.keyLow == 60 && high.keyHigh == 127);
    CHECK (std::abs (high.pan - 0.5f) < 1.0e-6f && low.pan == 0.0f && low.flOrigin.empty() && high.flOrigin.empty());
    CHECK (std::abs (low.volume - 0.78f) < 1.0e-6f && std::abs (high.volume - 0.78f) < 1.0e-6f);
    CHECK (s.channels[3].name == "After" && s.channels[3].mixerTrack == 4);
    // FL's notes: the rack's on the Layer, the next channel's on it
    CHECK (s.patterns.size() == 1 && s.patterns[0].notes.size() == 2);
    if (s.patterns.size() == 1 && s.patterns[0].notes.size() == 2)
    {
        CHECK (s.patterns[0].notes[0].channel == 0);
        CHECK (s.patterns[0].notes[1].channel == 3);
    }
    // the rack's level and its Filter on the rack's insert, sending into FL's insert 3; both instruments play into it
    const auto rackInsert = layer.mixerTrack;
    CHECK (rackInsert > 4 && rackInsert < (int) s.mixer.size());
    CHECK (low.mixerTrack == rackInsert && high.mixerTrack == rackInsert);
    if (rackInsert > 4 && rackInsert < (int) s.mixer.size())
    {
        const auto& m = s.mixer[(std::size_t) rackInsert];
        CHECK (m.name == "Lead (FL Studio Mobile Rack)" && ! m.toMaster && s.routes (rackInsert, 3) && m.effects.size() == 1);
    }
    CHECK (report.converted.count ("FL Studio Mobile Rack") == 1 && report.converted.count ("FL Studio Mobile: rack with several instruments (Layer)") == 1);
    CHECK (report.converted.count ("Layer") == 0);
    for (const auto* list : { &report.unsupported, &report.notReproduced }) // (round qafix190kq: both lists)
        for (const auto& [what, n] : *list)
            CHECK (what.find ("FL Studio Mobile Rack") == std::string::npos);
}

TEST (flm190jv_flp_mobile_rack_parts_take_turns)
{
    using namespace lynx;
    namespace t = flm190jv;
    // instrument, Graphic EQ, instrument: the EQ hears only the first (FLM_FORMAT §6.1, 190fe's FL render)
    const auto state = flm190fn::rackState (flm182c::rack (flm::kindNormal, { t::saw (2), t::graphicEq (3), t::saw (4) }));
    FlpImportReport report;
    const auto s = importFlp (t::project (state), t::options (report));
    CHECK (s.channels.size() == 4 && s.channels[0].instrument == InstrumentKind::layer);
    if (s.channels.size() != 4)
        return;
    const auto rackInsert = s.channels[0].mixerTrack;
    const auto& first = s.channels[1];
    const auto& second = s.channels[2];
    CHECK (rackInsert > 4 && second.mixerTrack == rackInsert && first.mixerTrack != rackInsert && first.mixerTrack > 4);
    if (first.mixerTrack > 4 && first.mixerTrack < (int) s.mixer.size() && rackInsert > 0 && rackInsert < (int) s.mixer.size())
    {
        const auto& part = s.mixer[(std::size_t) first.mixerTrack];
        CHECK (part.effects.size() == 1 && part.effects[0].kind == EffectKind::pocketGraphicEq && ! part.toMaster && part.sends.size() == 1
               && part.sends[0].target == rackInsert && part.sends[0].level == 1.0f && part.name == "Lead (rack part 1)");
        CHECK (s.mixer[(std::size_t) rackInsert].effects.empty() && s.routes (rackInsert, 3));
    }
    CHECK (report.converted.count ("FL Studio Mobile: rack whose instruments and effects take turns (one insert per part)") == 1);
}

TEST (flm190jv_flp_mobile_rack_effect_before_its_only_instrument_hears_nothing)
{
    using namespace lynx;
    namespace t = flm190jv;
    // Filter → SuperSaw plays as the SuperSaw alone (190fe's FL render of "fe_lp_then_osc": the instrument alone, to the bit)
    const auto first = flm190fn::rackState (flm182c::rack (flm::kindNormal, { t::filter (3), t::saw (2) }));
    const auto alone = flm190fn::rackState (flm182c::rack (flm::kindNormal, { t::saw (2) }));
    FlpImportReport report, plain;
    const auto s = importFlp (t::project (first), t::options (report));
    const auto r = importFlp (t::project (alone), t::options (plain));
    CHECK (s.channels.size() == 2 && r.channels.size() == 2 && s.mixer.size() == r.mixer.size());
    if (s.channels.size() != 2 || r.channels.size() != 2 || s.mixer.size() != r.mixer.size())
        return;
    auto a = s.channels[0], b = r.channels[0];
    CHECK (a.flOrigin.plugin == "FL Studio Mobile Rack" && a.flOrigin.state == first && b.flOrigin.state == alone);
    a.flOrigin.state.clear();
    b.flOrigin.state.clear();
    CHECK (a == b);
    CHECK (s.mixer == r.mixer);
    CHECK (report.converted.count ("FL Studio Mobile Rack: an effect before the rack's first instrument (hears nothing in FLM either: left out)") == 1);
    CHECK (report.converted.count ("FL Studio Mobile: rack whose instruments and effects take turns (one insert per part)") == 0);
    // one instrument: the result of round 190fn (no children, no parts)
    FlmImportOptions o;
    const auto rack = importFlmRackState (alone, o, 120.0, "Lead");
    CHECK (rack.has_value() && rack->children.empty() && rack->parts.empty() && rack->childPart.empty());
}

TEST (flm190jv_flp_mobile_rack_kit_and_arpeggiator_stay_stand_ins)
{
    using namespace lynx;
    namespace t = flm190jv;
    FlpImportReport report;
    const auto kit = flm190fn::rackState (flm182c::rack (flm::kindNormal, { t::saw (2) }, { flm182c::sample ("Kick", "@R@", "Drum Samples\\Kick\\Kick 1.wav") }));
    const auto s = importFlp (t::project (kit), t::options (report));
    CHECK (s.channels.size() == 2 && s.channels[0].instrument == InstrumentKind::synth);
    CHECK (report.unsupported.count ("FL Studio Mobile Rack (drum kit: not reproduced yet); played with a LYNX SHARE synth preset") == 1);
    const auto arp = flm190fn::rackState (flm182c::rack (flm::kindNormal, { flm182c::module (51, 4, {}, "Default/Default", 0, 127, 20), t::saw (2) }));
    FlpImportReport other;
    const auto a = importFlp (t::project (arp), t::options (other));
    CHECK (a.channels.size() == 2 && a.channels[0].instrument == InstrumentKind::synth);
    CHECK (other.unsupported.count ("FL Studio Mobile Rack (an Arpeggiator or Note Echo: not reproduced yet); played with a LYNX SHARE synth preset") == 1);
}

TEST (flm190jv_flp_mobile_rack_note_effects_move_fls_notes)
{
    using namespace lynx;
    namespace t = flm190jv;
    // Transpose +12 (Octave 0.5 = 0, Semitone 1.0 = +12) moves the Lead's D up an octave; the next channel's note is
    // untouched. Then a Scale of C only without Snap drops the D (FLM plays nothing for it)
    const auto state = flm190fn::rackState (flm182c::rack (flm::kindNormal, {
        flm190de::moduleWith (48, 4, { 1.0f, 0.5f, 1.0f }),
        t::saw (2) }));
    FlpImportReport report;
    auto project = t::project (state);
    project.patterns[0].notes[0].key = 62;
    const auto s = importFlp (project, t::options (report));
    CHECK (s.channels.size() == 2 && s.channels[0].instrument == InstrumentKind::pocketSuperSaw);
    CHECK (s.patterns.size() == 1 && s.patterns[0].notes.size() == 2);
    if (s.patterns.size() == 1 && s.patterns[0].notes.size() == 2)
    {
        CHECK (s.patterns[0].notes[0].channel == 0 && s.patterns[0].notes[0].key == 74);
        CHECK (s.patterns[0].notes[1].channel == 1 && s.patterns[0].notes[1].key == 60);
    }
    CHECK (report.converted.count ("FL Studio Mobile Rack: note effects (on the channel's notes)") == 1);
    // Scale: C only (switches 4…15 = C…B), Snap off: the D is not played
    std::vector<float> scale (16, 0.0f);
    scale[0] = 1.0f;
    scale[4] = 1.0f;
    const auto dropping = flm190fn::rackState (flm182c::rack (flm::kindNormal, { flm190de::moduleWith (50, 4, scale), t::saw (2) }));
    FlpImportReport dropped;
    auto other = project;
    other.channels[0].pluginState = dropping;
    const auto e = importFlp (other, t::options (dropped));
    CHECK (e.patterns.size() == 1 && e.patterns[0].notes.size() == 1 && e.patterns[0].notes[0].channel == 1);
    CHECK (dropped.converted["FL Studio Mobile: notes a note effect holds back (silent in FLM too)"] == 1);
}

TEST (flm190jv_flm_layer_takes_the_instruments_common_gate)
{
    using namespace lynx;
    namespace t = flm190de;
    // a note's Time › Gate is its channel's — the Layer's: two 3x Osc with the same Poly › Gate give it to the Layer;
    // different gates are listed and the notes play as written
    auto gated = flm190fe::threeOsc();
    gated[129] = 0.3f;
    auto open = flm190fe::threeOsc();
    open[129] = 1.0f; // (gate off)
    const auto bytes = flm190bs::song ({
        t::rack (flm::kindMaster, {}),
        t::rack (flm::kindNormal, { t::moduleWith (39, 2, gated), t::moduleWith (39, 3, gated) }),
        t::rack (flm::kindNormal, { t::moduleWith (39, 2, gated), t::moduleWith (39, 3, open) }),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Same", 1, { t::lane (0, "Same", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }) }),
        t::channel (flm::kindNormal, "Mixed", 2, { t::lane (0, "Mixed", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }) }) });
    FlpImportReport report;
    const auto s = importFlm (bytes, flm190bs::anyFile (&report));
    CHECK (s.has_value());
    if (! s)
        return;
    const auto* same = flm182c::channelNamed (*s, "Same");
    const auto* mixed = flm182c::channelNamed (*s, "Mixed");
    CHECK (same != nullptr && same->instrument == InstrumentKind::layer && same->layer.children.size() == 2);
    CHECK (mixed != nullptr && mixed->instrument == InstrumentKind::layer && mixed->layer.children.size() == 2);
    if (same == nullptr || mixed == nullptr || same->layer.children.size() != 2 || mixed->layer.children.size() != 2)
        return;
    const auto& child = s->channels[(std::size_t) same->layer.children[0]];
    CHECK (child.misc.gateBeats > 0.0f && same->misc.gateBeats == child.misc.gateBeats);
    CHECK (mixed->misc.gateBeats == 0.0f);
    CHECK (report.partialEffects.count ("FL Studio Mobile: instruments of one rack with different note gates (the notes play ungated)") == 1);
}

TEST (flm190jv_flp_mobile_rack_layer_keeps_the_module_gate)
{
    using namespace lynx;
    namespace t = flm190jv;
    // two 3x Osc with one Poly › Gate in an .flp's Rack generator: the gate on the Layer (190fe's FL render "fe_osc_pg"
    // plays the first 3x Osc's notes ~70 ms long; ungated, LYNX rendered it 7.4 dB too loud)
    auto gated = flm190fe::threeOsc();
    gated[129] = 0.3f;
    const auto state = flm190fn::rackState (flm182c::rack (flm::kindNormal, { flm190de::moduleWith (39, 2, gated), flm190de::moduleWith (39, 3, gated) }));
    FlpImportReport report;
    const auto s = importFlp (t::project (state), t::options (report));
    CHECK (s.channels.size() == 4 && s.channels[0].instrument == InstrumentKind::layer);
    if (s.channels.size() != 4)
        return;
    CHECK (s.channels[0].misc.gateBeats > 0.0f && s.channels[0].misc.gateBeats == s.channels[1].misc.gateBeats);
    CHECK (s.channels[1].instrument == InstrumentKind::threeosc && s.channels[2].instrument == InstrumentKind::threeosc);
}

TEST (flm190jv_pitcher2_on_lynx_tune_as_fl_renders_it)
{
    using namespace lynx;
    namespace lt = lynxtune;
    // FL's renders of Pitcher 2 (rea flm.md §4): only D allowed — the notes of an octave all land on its own D (A4, B4 →
    // D4, not D5): C and C# go up, E … B down; the Speed matched at FL's 90 % time (at least 9 ms)
    flm::Module m;
    m.type = 45;
    m.params.assign (27, 0.0f);
    m.params[0] = 1.0f;
    m.params[3] = 1.0f; // (note D)
    m.params[18] = 1.0f, m.params[19] = 0.5f, m.params[22] = 1.0f, m.params[23] = 0.5f;
    const auto retuneMs = [] (float v) { return 400.0 * (double) v * (double) v; };
    for (const auto& [speed, ms] : std::vector<std::pair<float, double>> { { 1.0f, 9.0 }, { 0.5f, 12.08 }, { 0.25f, 55.9 }, { 0.1f, 200.2 }, { 0.0f, 400.0 } })
    {
        m.params[13] = speed;
        const auto mapped = flmfx::map (m);
        CHECK (mapped.slots.size() == 1);
        if (mapped.slots.size() != 1)
            continue;
        const auto& p = mapped.slots[0].params;
        CHECK (std::abs (retuneMs (p[lt::retune]) - ms) < 0.01 * ms + 0.05);
        CHECK (p[lt::noteC + 2] == 1.0f && p[lt::noteC] == 0.0f);
        CHECK (std::abs ((-24.0 + 48.0 * (double) p[lt::outputTrim]) - (-3.0103)) < 1e-3); // (1/√2, FL's render)
        CHECK (p[lt::noteDirC] == 0.5f && p[lt::noteDirC + 1] == 0.5f && p[lt::noteDirC + 2] == 0.0f);
        for (int k = 3; k < 12; ++k)
            CHECK (p[(std::size_t) (lt::noteDirC + k)] == 1.0f);
    }
    // C, E and A allowed: D and F# … G# have both sides (the nearest), A# and B only below (down)
    m.params.assign (27, 0.0f);
    m.params[0] = 1.0f, m.params[1] = 1.0f, m.params[5] = 1.0f, m.params[10] = 1.0f;
    const auto mixed = flmfx::map (m);
    CHECK (mixed.slots.size() == 1);
    if (mixed.slots.size() == 1)
    {
        const auto& p = mixed.slots[0].params;
        CHECK (p[lt::noteDirC + 2] == 0.0f && p[lt::noteDirC + 7] == 0.0f && p[lt::noteDirC + 10] == 1.0f && p[lt::noteDirC + 11] == 1.0f);
    }
}
