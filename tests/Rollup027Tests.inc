// Round 190ck: the 0.1.27 pre-integration (wip/rollup027). Checks of what only exists once the rounds' branches are
// together: FLM racks with several instruments (190bs) × the SuperSaw engine and the newly mapped effects (190by);
// the hot-swap key (190ce) × the layout styles.

#include "core/HostSlide.h"
#include "core/HotSwap.h"
#include "core/KeyMap.h"
#include "core/LayoutStyle.h"
#include "core/PocketSuperSaw.h"
#include "core/SessionKeys.h"

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

// Round 190cm (coordinator's decision): the Ableton-style keys that clash with a global of the key map act as default
// keys only in the Clip-launch layout; in FL's layout (and Simple) the chord is not taken, so the global keeps it.
TEST (rollup027_ableton_style_keys_do_not_hide_fl_layout_globals)
{
    using namespace lynx;
    using layoutstyle::Style;
    using sessionkeys::Command;
    for (const auto c : { Command::gridNewMidiClip, Command::audioInsertWarpMarkers, Command::audioQuantize })
    {
        CHECK (sessionkeys::defaultKeyActs (c, Style::clipLaunch));
        CHECK (! sessionkeys::defaultKeyActs (c, Style::pattern) && ! sessionkeys::defaultKeyActs (c, Style::simple));
        // the menu row shows a key only where it acts
        CHECK (std::string (sessionkeys::menuKey (c, Style::pattern)).empty() && std::string (sessionkeys::menuKey (c, Style::simple)).empty());
        CHECK (std::string (sessionkeys::menuKey (c, Style::clipLaunch)).rfind ("\tCtrl+", 0) == 0);
    }
    CHECK (std::string (sessionkeys::menuKey (Command::gridNewMidiClip, Style::clipLaunch)) == "\tCtrl+Shift+M");
    CHECK (std::string (sessionkeys::menuKey (Command::audioInsertWarpMarkers, Style::clipLaunch)) == "\tCtrl+I");
    CHECK (std::string (sessionkeys::menuKey (Command::audioQuantize, Style::clipLaunch)) == "\tCtrl+U");
    // the style the main window shows is what the panels ask (Pattern until it says otherwise)
    const auto before = layoutstyle::current();
    layoutstyle::setCurrent (Style::pattern);
    CHECK (! sessionkeys::defaultKeyActs (Command::audioInsertWarpMarkers) && std::string (sessionkeys::menuKey (Command::gridNewMidiClip)).empty());
    layoutstyle::setCurrent (Style::clipLaunch);
    CHECK (sessionkeys::defaultKeyActs (Command::audioInsertWarpMarkers) && sessionkeys::defaultKeyActs (Command::gridNewMidiClip));
    layoutstyle::setCurrent (before);
    // the globals those chords belong to in the key map: still their default keys, found from any panel and from a
    // tool window such as the audio editor (which hands the chord back when its own key does not act)
    const keys::KeyMap map;
    const keys::Chord ctrlI { 'I', keys::ctrl }, ctrlShiftM { 'M', (std::uint8_t) (keys::ctrl | keys::shift) }, ctrlU { 'U', keys::ctrl };
    CHECK (map.find (ctrlI, keys::Context::global, false) == "record.waitForInput");
    CHECK (map.find (ctrlI, keys::Context::playlist, true) == "record.waitForInput");
    CHECK (map.findFromSecondaryWindow (ctrlI, false, false) == "record.waitForInput");
    CHECK (map.find (ctrlShiftM, keys::Context::global, false) == "file.exportMidi");
    CHECK (map.find (ctrlShiftM, keys::Context::playlist, false) == "file.exportMidi");
    CHECK (map.find (ctrlU, keys::Context::global, false).empty()); // (free today: the audio editor's Ctrl+U hides nothing)
}

// Round 190cm: Foil Plate's Voicing switched while the pad plays is click-free under both engine laws (the audit's
// own measure: the step at the switch against both voicings' own steps there), both ways.
TEST (rollup027_foil_plate_voicing_switch_is_click_free_under_both_laws)
{
    using namespace lynx;
    const auto prog = fxaudit::programme (0.6, 0.25);
    const auto voicing = (std::size_t) plugui::paramIndex (EffectKind::foilPlate, "Voicing");
    const auto law = (std::size_t) verbs::foil::engineLaw;
    for (const float lawValue : { 0.0f, 1.0f })
        for (const float from : { 0.0f, 1.0f })
        {
            auto a = makeEffectSlot (EffectKind::foilPlate);
            a.params[law] = lawValue;
            a.params[voicing] = from;
            auto b = a;
            b.params[voicing] = 1.0f - from;
            fxaudit::RunOptions ro;
            ro.switchBlock = prog.padMiddle() / fxaudit::blockSize;
            ro.after = &b;
            const auto sw = fxaudit::run (a, a, prog, ro);
            const auto steadyA = fxaudit::run (a, a, prog), steadyB = fxaudit::run (b, b, prog);
            const int at = ro.switchBlock * fxaudit::blockSize;
            const auto jump = fxaudit::maxStep (sw, at - 64, at + 2048);
            const auto own = std::max (fxaudit::maxStep (steadyA, at - 2048, at + 2048), fxaudit::maxStep (steadyB, at - 2048, at + 2048));
            if (! (jump <= 3.0 * own))
                std::printf ("  Foil Plate: Law %g, Voicing %g -> %g: step %.4f against %.4f\n", (double) lawValue, (double) from, (double) (1.0f - from), jump, own);
            CHECK (sw.finite && jump <= 3.0 * own);
        }
}

// Round 190cm: Pocket SuperSaw in LYNX's channel workflow — slide notes, the channel porta's glide, the channel's
// pitch live, and the MIDI wheel through the module's own Pitch bend range (it had no source before).
TEST (rollup027_pocket_supersaw_slides_glides_and_bends_like_a_channel)
{
    using namespace lynx;
    namespace ps = pocketsaw;
    using namespace saw190by;
    const double fs = 48000.0;
    const int n = (int) (0.5 * fs);
    // the pitch of x[from, to) in cents from `hz`, looked for within 4 semitones of `about` cents (the strongest
    // partial there is the saw's fundamental), then refined
    const auto centsAbout = [&] (const std::vector<float>& x, int from, int to, double hz, double about) {
        double best = hz, bestA = -1.0;
        for (double c = about - 400.0; c <= about + 400.0; c += 20.0)
        {
            const auto f = hz * std::pow (2.0, c / 1200.0);
            if (const auto a = tone (x, from, to, f, fs); a > bestA)
                bestA = a, best = f;
        }
        return 1200.0 * std::log2 (pitchNear (x, from, to, best, fs) / hz);
    };
    const auto cents = [&] (const std::vector<float>& x, int from, int to, double hz) { return centsAbout (x, from, to, hz, 0.0); };
    const auto make = [&] (ChannelDef& c) {
        auto inst = makeInstrument (InstrumentKind::pocketSuperSaw);
        c.instrument = InstrumentKind::pocketSuperSaw;
        c.modelParams = pocketSuperSawDefaults();
        c.modelParams[ps::flm::retrigPhases - 1] = 1.0f;
        inst->prepare (fs);
        inst->configure (c, 120.0);
        return inst;
    };
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    const auto block = [&] (Instrument& i) {
        std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
        i.render (l.data(), r.data(), n, 1.0f, 1.0f);
    };
    {
        // a slide note: the sounding voice glides to it over its length, keeps its key and stays there; its note
        // off still ends it
        ChannelDef c;
        auto inst = make (c);
        CHECK (inst->followsChannelPorta() && inst->followsSlides());
        inst->noteOn (57, 1.0f); // A3, 220 Hz
        block (*inst);
        CHECK (std::fabs (cents (l, n / 4, n, 220.0)) < 2.0);
        inst->slideTo (69, 0.0f, 0.25f * (float) fs); // to A4 in 0.25 s
        block (*inst);
        const auto mid = centsAbout (l, (int) (0.10 * fs), (int) (0.15 * fs), 220.0, 600.0); // half way: about +600 cents
        CHECK (mid > 450.0 && mid < 750.0);
        block (*inst);
        CHECK (std::fabs (cents (l, n / 4, n, 440.0)) < 2.0);
        inst->noteOff (57);
        for (int k = 0; k < 12 && ! inst->idle(); ++k)
            block (*inst);
        CHECK (inst->idle());
        // a released voice is not moved by a later slide; a new note starts at its own pitch
        inst->noteOn (57, 1.0f);
        block (*inst);
        CHECK (std::fabs (cents (l, n / 4, n, 220.0)) < 2.0);
    }
    {
        // the channel porta: the rack starts the note 7 semitones low (its cents hold the offset) and the voice
        // glides in over the rack's time
        ChannelDef c;
        auto inst = make (c);
        HostSlide::pendingGlide() = { -700.0f, 0.2f * (float) fs, false };
        inst->setNextNoteExpression (0.0f, -700.0f, false);
        inst->noteOn (69, 1.0f);
        CHECK (HostSlide::pendingGlide().taken);
        HostSlide::pendingGlide() = {};
        block (*inst);
        const auto early = centsAbout (l, (int) (0.02 * fs), (int) (0.06 * fs), 440.0, -550.0);
        CHECK (early < -400.0 && early > -700.0);
        CHECK (std::fabs (cents (l, n / 2, n, 440.0)) < 2.0);
    }
    {
        // the channel's pitch live: the knob and the main pitch / fine tune the rack gives before each render move a
        // sounding voice; a note's own cents no longer count the channel's part twice
        ChannelDef c;
        auto inst = make (c);
        inst->setChannelPitchNow (0.0f, 100.0f);
        inst->setNextNoteExpression (0.0f, 100.0f, false); // (the rack adds the channel's part to the note's cents)
        inst->noteOn (69, 1.0f);
        block (*inst);
        CHECK (std::fabs (centsAbout (l, n / 4, n, 440.0, 100.0) - 100.0) < 2.0);
        inst->setChannelPitchNow (200.0f, 500.0f);
        block (*inst);
        CHECK (std::fabs (centsAbout (l, n / 4, n, 440.0, 700.0) - 700.0) < 2.0);
    }
    for (const int range : { 2, 12 })
    {
        // the wheel: the rack gives it in semitones of the channel's pitch range (here ±2, the wheel at its top);
        // the module bends by its own Pitch bend range, 2 semitones by default
        ChannelDef c;
        auto inst = make (c);
        if (range != 2)
            inst->setParameter (ps::flm::bendRange - 1, 1.0f);
        CHECK (ps::bendRangeSemitones (range == 2 ? ps::defaults()[ps::flm::bendRange] : 1.0f) == range);
        inst->noteOn (57, 1.0f);
        inst->pitchBend (2.0f);
        block (*inst);
        CHECK (std::fabs (centsAbout (l, n / 4, n, 220.0, 100.0 * range) - 100.0 * range) < 3.0);
        inst->pitchBend (-1.0f); // half way down
        block (*inst);
        CHECK (std::fabs (centsAbout (l, n / 4, n, 220.0, -50.0 * range) + 50.0 * range) < 3.0);
    }
}
