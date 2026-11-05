// Round 171a: issues #67 (Parametric EQ state after save and reopen), #68 ("Cut itself" in two places), #63 (per-note
// pan), #66 (Peak Controller display) and #62 (detached windows after a restart).

#include "core/EffectPresets.h"
#include "core/FlEffects.h"
#include "core/ParametricEqFl.h"

namespace issues171a
{
std::vector<std::uint8_t> flEffectPreset (const char* folder, const char* file)
{
    if (realFlpDir.empty())
        return {};
    const auto p = std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Effects" / folder / file;
    std::ifstream in (p, std::ios::binary);
    return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}

// what a project save + reload does to a slot's parameters (engine/ProjectFile.cpp effectToVar / effectFromVar)
lynx::EffectSlot savedAndReloaded (const lynx::EffectSlot& slot)
{
    auto loaded = lynx::makeEffectSlot (slot.kind);
    const auto count = lynx::savedEffectParamCount (slot);
    for (std::size_t i = 0; i < count; ++i)
        loaded.params[i] = std::clamp ((float) lynx::savedEffectParamValue (slot.params[i]), 0.0f, 1.0f);
    lynx::upgradeLoadedEffectParams (loaded, count);
    return loaded;
}

// a Parametric EQ 2 state as FL saves it (version 8, 0x162 bytes): the preset "40Hz + 18kHz cut"'s values
std::vector<std::uint8_t> peq2State()
{
    std::vector<std::uint8_t> d (0x162, 0);
    auto put = [&d] (std::size_t at, std::int32_t v) { std::memcpy (d.data() + at, &v, 4); };
    put (0, 8);
    const std::int32_t gain[7] { 322, -184, 0, 0, -520, 50, -950 }, freq[7] { 4523, 7228, 25486, 32768, 64210, 64613, 64901 },
        width[7] { 36068, 41796, 25412, 25412, 47712, 7456, 36068 }, type[7] { 3, 0, 0, 0, 0, 0, 1 }, order[7] { -3, 0, 0, 0, 0, 0, -3 };
    for (int b = 0; b < 7; ++b)
    {
        put (4 + 4 * (std::size_t) b, gain[b]);
        put (4 + 4 * (std::size_t) (7 + b), freq[b]);
        put (4 + 4 * (std::size_t) (14 + b), width[b]);
        put (4 + 4 * (std::size_t) (21 + b), type[b]);
        put (4 + 4 * (std::size_t) (28 + b), order[b]);
    }
    d[0x13D] = 66;
    put (0x13E, 0x7F);
    return d;
}

bool sameSettings (const lynx::peqfl::Settings& a, const lynx::peqfl::Settings& b)
{
    return a.law == b.law && a.gain == b.gain && a.freq == b.freq && a.width == b.width && a.type == b.type && a.order == b.order
           && a.main == b.main && a.processing == b.processing && a.linearPhase == b.linearPhase && a.mask == b.mask;
}
} // namespace issues171a

// #67: "40Hz + 18kHz cut" (Steep 8 low cut at 32 Hz, Steep 8 high cut) read back as LYNX's own EQ after a reload: the
// FL law marker [36] and the processing / mask / linear fields after the 36 named parameters were not saved.
TEST (issues171a_parametric_eq_fl_law_survives_save_and_reload)
{
    using namespace issues171a;
    using namespace lynx;
    const auto state = peq2State();
    const auto settings = peqfl::parsePeq2 (state);
    CHECK (settings.has_value());
    auto slot = makeEffectSlot (EffectKind::parametricEq);
    peqfl::toParams (*settings, slot.params);
    CHECK (peq::flLaw (slot.params) && peq::orderText (slot.params, 0) == "Steep 8" && peq::orderText (slot.params, 6) == "Steep 8");

    // the saved count reaches past the named parameters to the law, processing, linear and mask fields
    CHECK (savedEffectParamCount (slot) > (std::size_t) peqfl::maskParam);
    const auto reloaded = savedAndReloaded (slot);
    CHECK (reloaded.params == slot.params); // bit for bit
    CHECK (peq::flLaw (reloaded.params) && peq::orderText (reloaded.params, 0) == "Steep 8");
    CHECK (sameSettings (peqfl::fromParams (reloaded.params), *settings)); // FL's integers, every one
    // the same curve before and after
    for (const double f : { 20.0, 32.0, 40.0, 100.0, 1000.0, 15000.0, 18000.0, 19000.0 })
        CHECK (peq::responseDb (reloaded.params, 48000.0, f) == peq::responseDb (slot.params, 48000.0, f));

    // the 4-decimal rounding of old saves lost FL's raw values (a frequency of raw/65536 moved by up to 3 steps)
    for (int raw = 0; raw <= 65536; raw += 7)
    {
        const auto v = (float) ((double) raw / 65536.0);
        CHECK ((float) savedEffectParamValue (v) == v);
    }
    // values that are already 4 decimals stay written as before
    CHECK (savedEffectParamValue (0.25f) == 0.25 && savedEffectParamValue (0.5f) == 0.5 && savedEffectParamValue (0.0f) == 0.0);

    // a user preset keeps it too (the hidden fields as "#index" lines)
    const auto text = presets::toText (EffectKind::parametricEq, "cut", slot.params);
    CHECK (text.find ("#36 = 0.5") != std::string::npos);
    const auto preset = presets::fromText (EffectKind::parametricEq, text);
    CHECK (preset && preset->params == slot.params);
    // a LYNX-law EQ saves no hidden fields
    CHECK (savedEffectParamCount (makeEffectSlot (EffectKind::parametricEq)) == effectInfo (EffectKind::parametricEq).params.size());
}

// #67 with FL's own preset files (when the FL installation is there): every Parametric EQ 2 / Parametric EQ preset reads
// FL's integers back exactly after a save and reload
TEST (issues171a_parametric_eq_fl_presets_round_trip_exactly)
{
    using namespace issues171a;
    using namespace lynx;
    const auto cut = flEffectPreset ("Fruity Parametric EQ 2", "40Hz + 18kHz cut.fst");
    if (cut.empty())
        return; // (no FL installation)
    const auto slot = effectFromFlPreset (cut);
    CHECK (slot && slot->kind == EffectKind::parametricEq && peqfl::lawOf (slot->params) == peqfl::Law::peq2);
    if (! slot)
        return;
    CHECK (peq::orderText (slot->params, 0) == "Steep 8" && peq::orderText (slot->params, 6) == "Steep 8");
    // FL's preset itself keeps gains on bands it does not use (band 2: −1.84 dB, band 5: −5.2 dB…): the sliders show them
    const auto s = peqfl::fromParams (slot->params);
    CHECK (s.type[1] == 0 && s.gain[1] == -184 && s.gain[4] == -520 && s.gain[6] == -950 && s.order[0] == -3 && s.order[6] == -3);
    CHECK (s.freq[0] == 4523 && s.width[0] == 36068);
    CHECK (savedAndReloaded (*slot).params == slot->params);

    for (const char* folder : { "Fruity Parametric EQ 2", "Fruity Parametric EQ" })
    {
        const auto dir = std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Effects" / folder;
        if (! std::filesystem::is_directory (dir))
            continue;
        for (const auto& e : std::filesystem::directory_iterator (dir))
        {
            if (e.path().extension() != ".fst")
                continue;
            std::ifstream in (e.path(), std::ios::binary);
            const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
            const auto fx = effectFromFlPreset (bytes);
            if (! fx || fx->kind != EffectKind::parametricEq)
                continue;
            const auto back = savedAndReloaded (*fx);
            CHECK (back.params == fx->params);
            CHECK (sameSettings (peqfl::fromParams (back.params), peqfl::fromParams (fx->params)));
        }
    }
}

// #67, the audit: every built-in effect, every parameter (named and hidden) at a value that is not its default, saved and
// reloaded (project) and as a user preset: the same values back
TEST (issues171a_every_effect_param_survives_save_and_reload)
{
    using namespace issues171a;
    using namespace lynx;
    int presetMismatches = 0;
    for (int k = 0; k <= (int) lastEffectKind; ++k)
    {
        const auto kind = (EffectKind) k;
        if (kind == EffectKind::plugin)
            continue;
        auto slot = makeEffectSlot (kind);
        for (std::size_t i = 0; i < slot.params.size(); ++i)
        {
            // an odd value (not 4 decimals), away from the default
            auto v = (float) ((double) ((i * 7919 + (std::size_t) k * 104729) % 65521) / 65521.0);
            if (v == slot.params[i])
                v = 1.0f - v;
            slot.params[i] = v;
        }
        const auto back = savedAndReloaded (slot);
        if (back.params != slot.params)
            std::printf ("  project round trip differs: %s\n", effectInfo (kind).name);
        CHECK (back.params == slot.params);

        // user presets: by name (hidden ones by index); Fruity Send's "Send to" is written over its insert span
        const auto text = presets::toText (kind, "audit", slot.params);
        const auto preset = presets::fromText (kind, text);
        CHECK (preset.has_value());
        if (! preset)
            continue;
        for (std::size_t i = 0; i < slot.params.size(); ++i)
            if (preset->params[i] != slot.params[i] && ! (kind == EffectKind::fruitySend && i == 3))
            {
                std::printf ("  preset round trip differs: %s param %d (%s)\n", effectInfo (kind).name, (int) i,
                             i < effectInfo (kind).params.size() ? effectInfo (kind).params[i].name : "hidden");
                ++presetMismatches;
            }
    }
    CHECK (presetMismatches == 0);
}

#include "core/RackOps.h"

// #68: the Channel Rack menu's "Cut itself" and the sample panel's checkbox are one state (FL: one "Cut self", the
// channel's Cut and By groups the same)
TEST (issues171a_cut_itself_is_one_state)
{
    using namespace lynx;
    ChannelDef c;
    CHECK (! channelCutsItself (c));
    setChannelCutsItself (c, 3, true); // the rack menu / the Misc page
    CHECK (channelCutsItself (c) && c.cutGroup == 1003 && c.cutBy == 1003 && c.sampler.cut); // the sample panel shows it
    setChannelCutsItself (c, 3, false);
    CHECK (! channelCutsItself (c) && c.cutGroup == 0 && c.cutBy == 0 && ! c.sampler.cut);
    // a project saved by the sample panel before 0.1.24 (only the sampler's flag): the rack menu shows it ticked
    c.sampler.cut = true;
    CHECK (channelCutsItself (c));
    // other cut groups are not "cut itself"
    c.sampler.cut = false;
    c.cutGroup = 2, c.cutBy = 5;
    CHECK (! channelCutsItself (c));
}

// #63: per-note pan (the Channel Rack's graph editor, the piano roll) on the built-in drums (the default project's Kick,
// Clap, Hat and Snare): it was ignored, the drums played every note in the middle
TEST (issues171a_note_pan_reaches_the_drums)
{
    using namespace lynx;
    for (const auto kind : { InstrumentKind::snare, InstrumentKind::kick, InstrumentKind::hat, InstrumentKind::clap })
    {
        auto render = [kind] (float pan, float cents, std::vector<float>& l, std::vector<float>& r) {
            auto inst = makeInstrument (kind, nullptr);
            inst->prepare (48000.0);
            inst->setNextNoteExpression (pan, cents);
            inst->noteOn (60, 1.0f);
            l.assign (2400, 0.0f);
            r.assign (2400, 0.0f);
            inst->render (l.data(), r.data(), 2400, 1.0f, 1.0f);
        };
        auto energy = [] (const std::vector<float>& x) { double e = 0.0; for (auto v : x) e += (double) v * v; return e; };
        std::vector<float> l, r;
        render (0.0f, 0.0f, l, r);
        const auto centre = energy (l);
        CHECK (centre > 1e-3 && l == r);
        render (-1.0f, 0.0f, l, r); // fully left
        CHECK (energy (l) > centre && energy (r) < 1e-9);
        render (1.0f, 0.0f, l, r); // fully right
        CHECK (energy (r) > centre && energy (l) < 1e-9);
        render (0.5f, 0.0f, l, r);
        CHECK (energy (r) > energy (l) && energy (l) > 1e-6);
        // the next note without an expression is centred again
        auto inst = makeInstrument (kind, nullptr);
        inst->prepare (48000.0);
        inst->setNextNoteExpression (-1.0f, 0.0f);
        inst->noteOn (60, 1.0f);
        inst->noteOn (60, 1.0f);
        std::vector<float> l2 (64), r2 (64);
        inst->render (l2.data(), r2.data(), 64, 1.0f, 1.0f);
        CHECK (energy (r2) > 0.0);
    }
    // fine pitch moves the kick's tone
    std::vector<float> a, b, l, r;
    auto kick = [] (float cents, std::vector<float>& out) {
        auto inst = makeInstrument (InstrumentKind::kick, nullptr);
        inst->prepare (48000.0);
        inst->setNextNoteExpression (0.0f, cents);
        inst->noteOn (60, 1.0f);
        std::vector<float> r (2400);
        out.assign (2400, 0.0f);
        inst->render (out.data(), r.data(), 2400, 1.0f, 1.0f);
    };
    kick (0.0f, a);
    kick (1200.0f, b);
    CHECK (a != b);

    // the whole way: the default project's Snare with a note panned hard left, through the rack
    auto song = Song::makeDefault();
    std::size_t snare = 0;
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        if (song.channels[c].instrument == InstrumentKind::snare)
            snare = c;
    CHECK (song.channels[snare].instrument == InstrumentKind::snare);
    for (auto& p : song.patterns)
        p.notes.clear();
    Note n { 0, 96, (int) snare, 60 };
    n.pan = -1.0f;
    song.patterns[0].notes = { n };
    song.mode = PlayMode::pattern;
    auto state = RackState::build (song, nullptr, 48000.0);
    RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    std::vector<float> bl (512), br (512);
    double el = 0.0, er = 0.0;
    for (int b2 = 0; b2 < 8; ++b2)
    {
        renderer.process (state.get(), true, (double) b2 * 512.0 * ticksPerSample (song.bpm, 48000.0), 512, bl.data(), br.data());
        for (int i = 0; i < 512; ++i)
            el += (double) bl[(std::size_t) i] * bl[(std::size_t) i], er += (double) br[(std::size_t) i] * br[(std::size_t) i];
    }
    std::printf ("  snare panned left through the rack: L %.4g R %.4g\n", el, er);
    CHECK (el > 1e-3 && er < el * 1e-3);
}

// #66: the Peak Controller's display (Peak + LFO, Peak, LFO) scrolled flat: the audio thread never published its outputs
// for the editor, and on a silent insert it was smart-disabled (its LFO stopped)
TEST (issues171a_peak_controller_publishes_its_outputs)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    for (auto& p : song.patterns)
        p.notes.clear();
    song.mixer = Song::defaultMixer (4);
    auto fx = makeEffectSlot (EffectKind::peakController);
    fx.params[4] = 0.3f; // LFO base
    fx.params[5] = 1.0f; // LFO amount: the most
    fx.params[7] = 0.0f; // the fastest speed
    song.mixer[2].effects.push_back (fx);
    auto state = RackState::build (song, nullptr, 48000.0);
    RackRenderer renderer;
    renderer.prepare (48000.0, 256);
    std::vector<float> l (256), r (256), loud (256, 0.8f);
    const auto key = ((std::size_t) 2 * maxEffectSlots + 0) * ProcessTimes::meterOutputs;
    auto& meters = renderer.processTimes().effectMeters;

    // silence for 2 s (past smart disable's hold): the LFO line still moves
    float lo = 1.0f, hi = 0.0f;
    for (int b = 0; b < 400; ++b)
    {
        renderer.process (state.get(), false, 0.0, 256, l.data(), r.data());
        if (b > 200)
        {
            const auto v = meters[key + 2].load();
            lo = std::min (lo, v), hi = std::max (hi, v);
        }
    }
    std::printf ("  peak controller LFO in silence: %.3f … %.3f\n", lo, hi);
    CHECK (hi - lo > 0.1f);

    // a loud input: the peak line rises
    const auto quiet = meters[key + 1].load();
    for (int b = 0; b < 8; ++b)
    {
        renderer.setMetronomeInput (loud.data(), loud.data(), 2);
        renderer.process (state.get(), false, 0.0, 256, l.data(), r.data());
    }
    CHECK (meters[key + 1].load() > quiet + 0.1f);
}

#include "core/Workspace.h"

// #62: a detached Mixer is detached again after a restart, at its place and size (the layout file round trip), also from a
// file written while the C locale used a decimal comma (a plugin's GTK switches it; the file then did not load at all)
TEST (issues171a_detached_windows_survive_a_restart)
{
    using namespace lynx::workspace;
    auto layout = Layout::makeDefault (1600, 900);
    for (const auto p : { Panel::mixer, Panel::channelRack, Panel::pianoRoll, Panel::playlist, Panel::browser })
    {
        layout.show (p);
        layout[p].detached = true;
        layout[p].windowBounds = { 100 + 10 * (int) p, 120, 900 + (int) p, 500 };
    }
    const auto text = layout.serialise();
    const auto back = Layout::parse (text, 1600, 900);
    CHECK (back.has_value());
    if (! back)
        return;
    for (const auto p : { Panel::mixer, Panel::channelRack, Panel::pianoRoll, Panel::playlist, Panel::browser })
        CHECK ((*back)[p].detached && (*back)[p].visible && (*back)[p].windowBounds.x == 100 + 10 * (int) p && (*back)[p].windowBounds.w == 900 + (int) p
               && (*back)[p].windowBounds.h == 500);

    // the same file with decimal commas
    std::string comma = text;
    for (std::size_t at = comma.find ("0."); at != std::string::npos; at = comma.find ("0.", at + 2))
        comma[at + 1] = ',';
    CHECK (comma != text);
    const auto fromComma = Layout::parse (comma, 1600, 900);
    CHECK (fromComma.has_value() && (*fromComma)[Panel::mixer].detached && (*fromComma)[Panel::mixer].windowBounds.x == 100 + 10 * (int) Panel::mixer);
}
