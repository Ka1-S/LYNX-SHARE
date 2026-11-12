// Round 190ck: the 0.1.27 pre-integration (wip/rollup027). Checks of what only exists once the rounds' branches are
// together: FLM racks with several instruments (190bs) × the SuperSaw engine and the newly mapped effects (190by);
// the hot-swap key (190ce) × the layout styles.

#include "core/HotSwap.h"
#include "core/KeyMap.h"
#include "core/LayoutStyle.h"
#include "core/PocketSuperSaw.h"

TEST (rollup027_flm_layer_plays_supersaw_on_its_own_engine)
{
    using namespace lynx;
    namespace t = flm190bs;
    const std::string device = "/storage/emulated/0/Android/data/com.imageline.FLM/files/My Samples/";
    const auto& d = pocketsaw::defaults();
    std::vector<float> saw (d.begin(), d.end()); // the module's 60 parameters, Enable first
    std::vector<float> comb (12, 0.5f);
    comb[0] = 1.0f;
    const auto bytes = t::song ({
        t::rack (flm::kindMaster, {}),
        t::rack (flm::kindNormal, { t::moduleOf (1, 2, {}, 0, 59, {}, device + "Low.wav"), t::moduleOf (23, 4, saw, 60, 127), t::moduleOf (40, 5, comb) }),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Stack", 1, { t::lane (0, "Stack", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 48 }, { 1, 1, 72 } }) }) }) });
    FlpImportReport report;
    const auto song = importFlm (bytes, t::anyFile (&report));
    CHECK (song.has_value());
    if (! song)
        return;
    const auto* layer = flm182c::channelNamed (*song, "Stack");
    CHECK (layer != nullptr && layer->instrument == InstrumentKind::layer && layer->layer.children.size() == 2);
    if (layer == nullptr || layer->layer.children.size() != 2)
        return;
    const auto& low = song->channels[(std::size_t) layer->layer.children[0]];
    const auto& high = song->channels[(std::size_t) layer->layer.children[1]];
    CHECK (low.instrument == InstrumentKind::sampler && low.keyHigh == 59);
    // the Layer's SuperSaw is Pocket SuperSaw with the module's values (not the generic synth), in its key range
    CHECK (high.instrument == InstrumentKind::pocketSuperSaw && (int) high.modelParams.size() == pocketsaw::paramCount && ! high.muted);
    CHECK (high.name == "Stack (SuperSaw)" && high.keyLow == 60 && high.keyHigh == 127 && high.mixerTrack == layer->mixerTrack);
    CHECK (report.converted.count ("FL Studio Mobile: SuperSaw") == 1 && report.converted.count ("FL Studio Mobile: rack with several instruments (Layer)") == 1);
    for (const auto& [what, count] : report.unsupported)
        CHECK (what.find ("SuperSaw") == std::string::npos);
    // the effect 190by mapped sits on the Layer's insert and is not listed as dropped
    const auto& slots = song->mixer[(std::size_t) layer->mixerTrack].effects;
    CHECK (std::any_of (slots.begin(), slots.end(), [] (const EffectSlot& s) { return s.kind == EffectKind::pocketComb; }));
    for (const auto& [what, count] : report.droppedEffects)
        CHECK (what.find ("Comb Filter") == std::string::npos);
}

TEST (rollup027_hot_swap_default_key_only_in_the_ableton_style_layout)
{
    using namespace lynx;
    using layoutstyle::Style;
    // the default Q acts in the Clip-launch (Ableton-style) layout only; a key the user chose acts everywhere
    CHECK (hotswap::keyActsInLayout (Style::clipLaunch, true) && ! hotswap::keyActsInLayout (Style::pattern, true) && ! hotswap::keyActsInLayout (Style::simple, true));
    CHECK (hotswap::keyActsInLayout (Style::pattern, false) && hotswap::keyActsInLayout (Style::simple, false) && hotswap::keyActsInLayout (Style::clipLaunch, false));
    keys::KeyMap map;
    CHECK (map.isDefault ("view.hotSwap"));
    map.setChords ("view.hotSwap", { { 'H', 0 } });
    CHECK (! map.isDefault ("view.hotSwap") && hotswap::keyActsInLayout (Style::pattern, map.isDefault ("view.hotSwap")));
}

// 190bz (the reverbs' Measured law; the stand-in rule pinned to Classic) × 190ca (per-device starting presets fitted on
// the classic Foil engine): a stand-in that starts from its own preset still runs the Classic law.
TEST (rollup027_reverb_stand_ins_with_their_own_preset_keep_the_classic_law)
{
    using namespace lynx;
    const auto law = (std::size_t) verbs::foil::engineLaw;
    CHECK (std::string (effectInfo (EffectKind::foilPlate).params[law].name) == "Law");
    CHECK (makeEffectSlot (EffectKind::foilPlate).params[law] == 1.0f); // a new Foil Plate: Measured
    for (const char* name : { "SSL PlateVerb", "SSL SpringVerb", "SSL Native FlexVerb", "VerbSuite Classics", "Lustrous Plates", "SSL GateVerb" })
    {
        const auto s = p190ca::substituted (p190ca::vst3Named (name));
        CHECK (s.kind == EffectKind::foilPlate && s.isSubstitute());
        CHECK (s.params[law] == 0.0f);
    }
    // (and the preset of 190ca is the one in use: PlateVerb's fitted decay, not the role's 0.3)
    const auto plate = p190ca::substituted (p190ca::vst3Named ("SSL PlateVerb"));
    CHECK (std::abs (plate.params[(std::size_t) p190ca::paramNamed (plate.kind, "Decay")] - 0.3028f) < 1e-6f);
}
