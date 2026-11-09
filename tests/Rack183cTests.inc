// Round 183c (docs/analise/REA_rodada183c.md): Ableton-style Racks as a Patcher in rack mode (core/Rack.h). The laws
// are Live 12.4's, measured on its renders (REA §8) and read from its stored mapping values (REA §4).

namespace rack183c
{
using namespace lynx;

bool approx (float a, float b, float tol = 1.0e-3f) { return std::abs (a - b) <= tol; }

// one block of a steady 0.5 / 0.5 input through a graph; returns the output's first and last samples (left, right)
std::array<float, 4> run (patcher::Graph& g, int n = 512, float in = 0.5f)
{
    std::vector<float> l ((std::size_t) n, in), r ((std::size_t) n, in);
    g.process (l.data(), r.data(), nullptr, nullptr, l.data(), r.data(), n, 120.0);
    return { l.front(), r.front(), l.back(), r.back() };
}

std::unique_ptr<patcher::Graph> graphOf (const PatcherDoc& doc)
{
    auto g = std::make_unique<patcher::Graph> (doc, 48000.0, patcher::builtInFactories (48000.0));
    g->prepare (48000.0);
    return g;
}

PatchNode utilityNode()
{
    PatchNode n;
    n.effect = makeEffectSlot (EffectKind::utility);
    return n;
}

PatchNode synthNode()
{
    PatchNode n;
    n.generator = true;
    n.channel.instrument = InstrumentKind::synth;
    n.channel.name = "Synth";
    return n;
}

PatchNode vfxNode()
{
    PatchNode n;
    n.generator = true;
    n.channel.instrument = InstrumentKind::vfx;
    return n;
}

float peak (patcher::Graph& g, int blocks = 8)
{
    float p = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        std::vector<float> l (512, 0.0f), r (512, 0.0f);
        g.process (l.data(), r.data(), nullptr, nullptr, l.data(), r.data(), 512, 120.0);
        for (std::size_t i = 0; i < l.size(); ++i)
            p = std::max ({ p, std::abs (l[i]), std::abs (r[i]) });
    }
    return p;
}
} // namespace rack183c

TEST (rack183c_note_fades_match_lives_renders)
{
    using namespace rack183c;
    // velocity zone 1..127, full from 80 (Live: v 10 / 20 / 40 / 60 / 70 -> 1.25 / 5 / 20 / 45 / 61.25)
    PatchZone z;
    z.velocity = { 1, 127, 80, 127 };
    const std::pair<int, float> fadeIn[] { { 10, 1.25f }, { 20, 5.0f }, { 40, 20.0f }, { 60, 45.0f }, { 70, 61.25f }, { 80, 80.0f }, { 127, 127.0f } };
    for (const auto& [v, want] : fadeIn)
        CHECK (approx (rack::zoneVelocity (z, 60, (float) v / 127.0f, 0.0f) * 127.0f, want, 0.01f));
    // the zone's first velocity still plays, at velocity 1
    CHECK (approx (rack::zoneVelocity (z, 60, 1.0f / 127.0f, 0.0f) * 127.0f, 1.0f, 0.01f));
    // fade-out: full to 40 (Live: 50 / 100 / 127 -> 44.3 / 31.8 / 1.44)
    z.velocity = { 1, 127, 1, 40 };
    CHECK (approx (rack::zoneVelocity (z, 60, 50.0f / 127.0f, 0.0f) * 127.0f, 44.32f, 0.02f));
    CHECK (approx (rack::zoneVelocity (z, 60, 100.0f / 127.0f, 0.0f) * 127.0f, 31.82f, 0.02f));
    CHECK (approx (rack::zoneVelocity (z, 60, 1.0f, 0.0f) * 127.0f, 1.443f, 0.01f));
    // key zone 48..127 full from 60 at velocity 127 (Live: keys 48 / 52 / 56 / 58 -> 9.72 / 48.60 / 87.92 / 107.45)
    PatchZone k;
    k.key = { 48, 127, 60, 127 };
    const std::pair<int, float> keys[] { { 48, 9.77f }, { 52, 48.85f }, { 56, 87.92f }, { 58, 107.46f }, { 60, 127.0f }, { 72, 127.0f } };
    for (const auto& [key, want] : keys)
        CHECK (approx (rack::zoneVelocity (k, key, 1.0f, 0.0f) * 127.0f, want, 0.05f));
    // outside a zone: the note doesn't enter
    CHECK (rack::zoneVelocity (k, 47, 1.0f, 0.0f) == 0.0f);
    PatchZone s;
    s.select = { 20, 127, 60, 127 };
    s.selector = rack::selectorOutput;
    CHECK (rack::zoneVelocity (s, 60, 1.0f, 19.0f) == 0.0f);
    CHECK (rack::zoneVelocity (s, 60, 1.0f, 40.0f) == 1.0f); // (an Instrument Rack's select fade is on its output, not the velocity)
    s.selectorScalesVelocity = true;                          // (a MIDI Effect Rack's is on the velocity)
    CHECK (approx (rack::zoneVelocity (s, 60, 1.0f, 40.0f), 21.0f / 41.0f));
}

TEST (rack183c_output_fade_pan_and_volume_match_lives_renders)
{
    using namespace rack183c;
    // chain select, zone 20..127 full from 60: sin (pi/2 * fade) (Live: 0.0383 / 0.2279 / 0.4091 / 0.7205 / 0.9275 / 0.9817)
    const RackZone in { 20, 127, 60, 127 };
    const std::pair<float, float> ins[] { { 20, 0.0383f }, { 25, 0.2279f }, { 30, 0.4091f }, { 40, 0.7205f }, { 50, 0.9275f }, { 55, 0.9817f }, { 60, 1.0f }, { 90, 1.0f } };
    for (const auto& [x, want] : ins)
        CHECK (approx (rack::selectOutputGain (in, x), want));
    // fade-out 0..100 full to 40 (Live: 55 / 70 / 100 -> 0.9263 / 0.7162 / 0.0257)
    const RackZone out { 0, 100, 0, 40 };
    CHECK (approx (rack::selectOutputGain (out, 55.0f), 0.9263f));
    CHECK (approx (rack::selectOutputGain (out, 70.0f), 0.7162f));
    CHECK (approx (rack::selectOutputGain (out, 100.0f), 0.0257f));
    // outside: silent on a side that fades; on a side with no fade the output rings on (the input closes instead)
    CHECK (rack::selectOutputGain (in, 10.0f) == 0.0f && rack::selectOutputGain (out, 110.0f) == 0.0f);
    const RackZone hard { 64, 127, 64, 127 };
    CHECK (rack::selectOutputGain (hard, 10.0f) == 1.0f && ! rack::selectInputOpen (hard, 10.0f) && rack::selectInputOpen (hard, 64.0f));
    // pan (Live: -1 -> +3.01 dB / silent; -0.5 -> +2.32 / -5.33 dB; centre unity)
    float l = 0.0f, r = 0.0f;
    rack::panGains (0.0f, l, r);
    CHECK (l == 1.0f && r == 1.0f);
    rack::panGains (-0.5f, l, r);
    CHECK (approx (l, 1.3066f) && approx (r, 0.5412f));
    rack::panGains (1.0f, l, r);
    CHECK (approx (l, 0.0f) && approx (r, 1.4142f));
    // the chain volume taper (Live's stored values): 0 dB at 0.85, -10 dB at 0.6, +6 dB at 1, -32 dB near 0.22
    CHECK (approx (rack::volumeGain (0.85f), 1.0f, 1.0e-4f));
    CHECK (approx (20.0f * std::log10 (rack::volumeGain (0.6f)), -10.0f, 0.01f));
    CHECK (approx (20.0f * std::log10 (rack::volumeGain (1.0f)), 6.0f, 0.01f));
    // 40 dB per unit down to -18 dB (at 0.4), Live's stored points below
    CHECK (approx (20.0f * std::log10 (rack::volumeGain (0.4f)), -18.0f, 0.01f));
    CHECK (approx (20.0f * std::log10 (rack::volumeGain (0.5f)), -14.0f, 0.01f));
    CHECK (20.0f * std::log10 (rack::volumeGain (0.39f)) < -18.0f);
    CHECK (std::abs (20.0f * std::log10 (rack::volumeGain (0.22f)) - (-32.0f)) < 1.0f);
    CHECK (rack::volumeGain (0.0f) == 0.0f);
    for (const float v : { 0.1f, 0.3f, 0.4f, 0.6f, 0.85f, 1.0f })
        CHECK (approx (rack::volumeValue (rack::volumeGain (v)), v, 1.0e-4f));
}

TEST (rack183c_audio_rack_sums_chains_with_volume_pan_solo_and_selector)
{
    using namespace rack183c;
    auto doc = rack::makeRack (RackDoc::Kind::audioEffect);
    CHECK (doc.isRack() && doc.rack[0].chains.size() == 1 && doc.rack[0].chains[0].mixer >= 0 && doc.rack[0].chains[0].gate >= 0);
    rack::addChain (doc);
    // two empty chains: the input twice (Live: +6.02 dB)
    for (auto& c : doc.rack[0].chains)
        c.select = { 0, 127, 0, 127 };
    rack::compile (doc);
    {
        auto g = graphOf (doc);
        const auto o = run (*g);
        CHECK (approx (o[2], 1.0f) && approx (o[3], 1.0f));
    }
    // solo: only the soloed chain; the activator: a chain off is silent
    rack::setSolo (doc, 1, true);
    rack::setChainVolume (doc, 1, 0.5f);
    {
        auto g = graphOf (doc);
        const auto o = run (*g);
        CHECK (approx (o[2], 0.25f));
    }
    rack::setSolo (doc, 1, false);
    rack::setActive (doc, 0, false);
    rack::setChainVolume (doc, 1, 1.0f);
    rack::setChainPan (doc, 1, -0.5f);
    {
        auto g = graphOf (doc);
        const auto o = run (*g);
        CHECK (approx (o[2], 0.5f * 1.3066f) && approx (o[3], 0.5f * 0.5412f));
    }
    // the chain selector: chain 1 fades in over 20..60; at 40 its output is sin (pi/2 * 21/41)
    rack::setActive (doc, 0, true);
    rack::setChainPan (doc, 1, 0.0f);
    doc.rack[0].chains[0].select = { 100, 127, 100, 127 }; // (chain 0: closed at 40, no fade: its input closes)
    doc.rack[0].chains[1].select = { 20, 127, 60, 127 };
    rack::compile (doc);
    rack::setSelector (doc, 40.0f);
    {
        auto g = graphOf (doc);
        run (*g); // (the control cables act from the next block)
        const auto o = run (*g);
        CHECK (approx (o[2], 0.5f * 0.7205f, 2.0e-3f));
        // automation of the selector (published parameter 16) moves it on the audio thread
        g->setPublished (rack::selectorOutput, 90.0f / 127.0f);
        run (*g);
        const auto o2 = run (*g);
        CHECK (approx (o2[2], 0.5f, 2.0e-3f));
    }
}

TEST (rack183c_macros_map_one_owner_per_target_and_drive_the_graph)
{
    using namespace rack183c;
    auto doc = rack::makeRack (RackDoc::Kind::audioEffect);
    doc.rack[0].chains[0].select = { 0, 127, 0, 127 };
    const auto u = rack::addDevice (doc, 0, utilityNode());
    CHECK (u >= 0 && rack::devices (doc.rack[0], 0) == std::vector<int> { u });
    // macro 1 -> the utility's gain from 0 dB (0.5) down to -12 dB (0.25): Live's law, linear in the 0..1 range
    RackMapping m;
    m.node = u, m.param = 0, m.min = 0.5f, m.max = 0.25f;
    CHECK (rack::map (doc, 0, m));
    CHECK (rack::macroOf (doc.rack[0], m) == 0);
    // the same target mapped to macro 2: macro 1 loses it
    CHECK (rack::map (doc, 1, m));
    CHECK (rack::macroOf (doc.rack[0], m) == 1 && doc.rack[0].macros[0].mappings.empty());
    // a chain's own mixer is not a device to map (its volume / pan are targets of their own)
    RackMapping bad;
    bad.node = doc.rack[0].chains[0].mixer;
    CHECK (! rack::map (doc, 0, bad));
    CHECK (rack::macroName (doc.rack[0], 1, "Gain") == "Gain" && rack::macroName (doc.rack[0], 0) == "Macro 1");
    // the macro at 127: the utility's gain at -12 dB, in the document and in the running graph
    rack::setMacro (doc, 1, 127.0f);
    CHECK (approx (doc.nodes[(std::size_t) u].effect.params[0], 0.25f));
    CHECK (approx (rack::mappedValue (m, 63.5f), 0.375f));
    {
        auto g = graphOf (doc);
        run (*g);
        const auto o = run (*g);
        CHECK (approx (o[2], 0.5f * std::pow (10.0f, -12.0f / 20.0f), 2.0e-3f));
        // automation of macro 2 (published parameter 1): back to 0 dB
        g->setPublished (1, 0.0f);
        run (*g);
        const auto o2 = run (*g);
        CHECK (approx (o2[2], 0.5f, 2.0e-3f));
    }
    // a macro on a chain's volume
    RackMapping v;
    v.target = RackMapping::Target::chainVolume, v.chain = 0, v.min = 0.0f, v.max = rack::unityVolume;
    CHECK (rack::map (doc, 2, v));
    rack::setMacro (doc, 2, 127.0f);
    CHECK (approx (doc.rack[0].chains[0].volume, 1.0f, 1.0e-4f));
    // unmap / invert
    CHECK (rack::invertRange (doc, 1, 0) && doc.rack[0].macros[1].mappings[0].min == 0.25f);
    CHECK (rack::unmap (doc, 1, 0) && doc.rack[0].macros[1].mappings.empty());
    // removing the device drops its mappings; the other indices follow
    rack::map (doc, 1, m);
    const auto u2 = rack::addDevice (doc, 0, utilityNode());
    RackMapping m2 = m;
    m2.node = u2;
    rack::map (doc, 3, m2);
    CHECK (rack::removeDevice (doc, 0, 0));
    CHECK (doc.rack[0].macros[1].mappings.empty() && doc.rack[0].macros[3].mappings.size() == 1
           && doc.rack[0].macros[3].mappings[0].node == u2 - 1 && rack::devices (doc.rack[0], 0) == std::vector<int> { u2 - 1 });
}

TEST (rack183c_variations_randomize_and_the_morph)
{
    using namespace rack183c;
    auto doc = rack::makeRack (RackDoc::Kind::audioEffect);
    const auto u = rack::addDevice (doc, 0, utilityNode());
    for (int k = 0; k < 3; ++k)
    {
        RackMapping m;
        m.node = u, m.param = k;
        rack::map (doc, k, m);
    }
    rack::setMacro (doc, 0, 10.0f);
    rack::setMacro (doc, 1, 20.0f);
    rack::setMacro (doc, 2, 30.0f);
    rack::setMacro (doc, 5, 77.0f); // (unmapped: variations hold no value for it)
    CHECK (rack::storeVariation (doc) == 0 && doc.rack[0].variations[0].name == "Variation 1");
    const auto& v0 = doc.rack[0].variations[0];
    CHECK (v0.has[0] && v0.has[2] && ! v0.has[5] && v0.values[1] == 20.0f);
    rack::setMacro (doc, 0, 110.0f);
    rack::setMacro (doc, 1, 120.0f);
    rack::setMacro (doc, 2, 127.0f);
    CHECK (rack::storeVariation (doc) == 1 && doc.rack[0].variations[1].name == "Variation 2");
    // launch: excluded macros and those without a value stay
    doc.rack[0].macros[2].excludeFromVariations = true;
    rack::setMacro (doc, 5, 3.0f);
    CHECK (rack::launchVariation (doc, 0));
    CHECK (doc.rack[0].macros[0].value == 10.0f && doc.rack[0].macros[1].value == 20.0f && doc.rack[0].macros[2].value == 127.0f
           && doc.rack[0].macros[5].value == 3.0f);
    CHECK (approx (doc.nodes[(std::size_t) u].effect.params[0], 10.0f / 127.0f));
    // rename, duplicate, delete, overwrite
    CHECK (rack::renameVariation (doc, 0, "Verse") && doc.rack[0].variations[0].name == "Verse");
    CHECK (rack::duplicateVariation (doc, 0) == 1 && doc.rack[0].variations.size() == 3 && doc.rack[0].variations[1].name == "Verse");
    CHECK (rack::deleteVariation (doc, 1) && doc.rack[0].variations.size() == 2);
    rack::setMacro (doc, 0, 64.0f);
    CHECK (rack::overwriteVariation (doc, 0) && doc.rack[0].variations[0].values[0] == 64.0f);
    // Randomize: mapped and not excluded only
    doc.rack[0].macros[1].excludeFromRandom = true;
    const auto before1 = doc.rack[0].macros[1].value, before5 = doc.rack[0].macros[5].value;
    std::uint32_t seed = 0x183cu;
    CHECK (rack::randomize (doc, seed) == 2);
    CHECK (doc.rack[0].macros[1].value == before1 && doc.rack[0].macros[5].value == before5);
    // the morph (LYNX's own, off by default): halfway between A and B for the macros both hold and not excluded
    CHECK (! doc.rack[0].morphOn);
    doc.rack[0].morphOn = true, doc.rack[0].morphA = 0, doc.rack[0].morphB = 1;
    rack::compile (doc);
    const auto mv = rack::morphValues (doc.rack[0], 0.5f);
    CHECK (approx (mv[0], (64.0f + 110.0f) / 2.0f) && approx (mv[1], (20.0f + 120.0f) / 2.0f) && mv[2] == doc.rack[0].macros[2].value);
    rack::setMorph (doc, 0.5f);
    CHECK (approx (doc.rack[0].macros[0].value, 87.0f));
    // in the graph the morph knob drives the morphing macros' cables (automation: published parameter 17)
    {
        auto g = graphOf (doc);
        run (*g);
        g->setPublished (rack::morphOutput, 1.0f);
        run (*g);
        run (*g);
        auto fx = g->effectAt (u);
        CHECK (fx != nullptr);
    }
    // shown macros: Live's 1, 2, 4 … 16
    CHECK (rack::moreMacros (1) == 2 && rack::moreMacros (2) == 4 && rack::moreMacros (8) == 10 && rack::moreMacros (16) == 16);
    CHECK (rack::fewerMacros (16) == 14 && rack::fewerMacros (2) == 1 && rack::fewerMacros (1) == 1);
}

TEST (rack183c_instrument_rack_keeps_the_device_order_and_filters_notes)
{
    using namespace rack183c;
    auto doc = rack::makeRack (RackDoc::Kind::instrument);
    CHECK (doc.rack[0].chains[0].gate < 0 && doc.rack[0].chains[0].mixer >= 0);
    const auto fx = rack::addDevice (doc, 0, utilityNode());
    const auto synth = rack::addDevice (doc, 0, synthNode());
    const auto midi = rack::addDevice (doc, 0, vfxNode());
    // MIDI effects first, then the instrument, then audio effects, whatever the drop place
    CHECK (rack::devices (doc.rack[0], 0) == (std::vector<int> { midi, synth, fx }));
    CHECK (rack::addDevice (doc, 0, synthNode()) == -1); // (one instrument per chain)
    auto audioRack = rack::makeRack (RackDoc::Kind::audioEffect);
    CHECK (rack::addDevice (audioRack, 0, synthNode()) == -1);
    auto midiRack = rack::makeRack (RackDoc::Kind::midiEffect);
    CHECK (rack::addDevice (midiRack, 0, utilityNode()) == -1 && rack::addDevice (midiRack, 0, vfxNode()) >= 0);
    CHECK (midiRack.rack[0].chains[0].mixer < 0);

    // a chain with the synth alone, velocity zone 64..127: a soft note doesn't sound, a loud one does
    auto keys = rack::makeRack (RackDoc::Kind::instrument);
    rack::addDevice (keys, 0, synthNode());
    keys.rack[0].chains[0].velocity = { 64, 127, 64, 127 };
    keys.rack[0].chains[0].select = { 0, 127, 0, 127 };
    rack::compile (keys);
    CHECK (keys.zones.size() == 1);
    {
        auto g = graphOf (keys);
        g->noteOn (60, 30.0f / 127.0f);
        CHECK (peak (*g) == 0.0f);
        g->noteOff (60);
        g->noteOn (60, 100.0f / 127.0f);
        CHECK (peak (*g, 2) > 0.0f);
        // its note-off goes through even though the zone changed meanwhile (the graph tracks what entered)
        g->noteOff (60);
        peak (*g, 400);
        CHECK (peak (*g, 2) < 1.0e-3f);
    }
}

TEST (rack183c_chains_group_ungroup_and_plain_patchers_untouched)
{
    using namespace rack183c;
    auto doc = rack::makeRack (RackDoc::Kind::audioEffect);
    const auto a = rack::addDevice (doc, 0, utilityNode());
    RackMapping m;
    m.node = a, m.param = 1;
    rack::map (doc, 0, m);
    RackMapping vol;
    vol.target = RackMapping::Target::chainVolume, vol.chain = 0;
    rack::map (doc, 1, vol);
    // duplicate: the copy's devices are new nodes, and the mappings follow onto them (as Live does)
    CHECK (rack::duplicateChain (doc, 0) == 1);
    CHECK (doc.rack[0].chains.size() == 2 && doc.rack[0].chains[1].nodes.size() == 1 && doc.rack[0].chains[1].nodes[0] != a);
    CHECK (doc.rack[0].macros[0].mappings.size() == 2);
    // move: chain mappings follow
    CHECK (rack::moveChain (doc, 0, 1) && doc.rack[0].macros[1].mappings[0].chain == 1);
    // remove: its nodes, mixer and gate go; the rest stay consistent
    const auto nodesBefore = doc.nodes.size();
    CHECK (rack::removeChain (doc, 1));
    CHECK (doc.nodes.size() == nodesBefore - 3 && doc.rack[0].chains.size() == 1 && doc.rack[0].macros[1].mappings.empty());
    for (const auto n : doc.rack[0].chains[0].nodes)
        CHECK (n >= 0 && n < (int) doc.nodes.size() && doc.nodes[(std::size_t) n].effect.kind == EffectKind::utility);
    // group: slots into a one-chain rack; ungroup gives them back
    std::vector<EffectSlot> slots { makeEffectSlot (EffectKind::utility), makeEffectSlot (EffectKind::fruityLimiter) };
    slots[1].row = 4;
    const auto grouped = rack::group (slots);
    CHECK (rack::isRack (grouped) && rack::rackOf (grouped.patcher)->chains[0].nodes.size() == 2);
    const auto back = rack::ungroup (grouped);
    CHECK (back.size() == 2 && back[0].kind == EffectKind::utility && back[1].kind == EffectKind::fruityLimiter);
    // FL's plain Patcher: compile and the rack helpers leave it alone, and no rack exists in it
    PatcherDoc plain;
    plain.nodes = { utilityNode() };
    PatchWire w;
    w.to = 0;
    plain.wires = { w };
    const auto copy = plain;
    rack::compile (plain);
    CHECK (plain == copy && ! plain.isRack() && rack::addChain (plain) == -1 && plain == copy);
    CHECK (! rack::isRack (makeEffectSlot (EffectKind::patcher)));
}

namespace rack183c
{
// A Live rack as Live writes it [DATA: the shipped sets' member names]; only what the import reads
inline std::string keyMidi (int macro)
{
    using als175a::v;
    return "<KeyMidi>" + v ("PersistentKeyString", "") + v ("IsNote", "false") + v ("Channel", "16") + v ("NoteOrController", std::to_string (macro)) +
           v ("LowerRangeNote", "-1") + v ("UpperRangeNote", "-1") + v ("ControllerMapMode", "0") + "</KeyMidi>";
}
inline std::string range (const char* tag, int lo, int hi, int flo, int fhi)
{
    using als175a::v;
    return std::string ("<") + tag + ">" + v ("Min", std::to_string (lo)) + v ("Max", std::to_string (hi)) + v ("CrossfadeMin", std::to_string (flo)) +
           v ("CrossfadeMax", std::to_string (fhi)) + "</" + tag + ">";
}
inline std::string branchMixer (double volume, double pan, const std::string& volumeMap = {})
{
    using als175a::v;
    return "<MixerDevice><Speaker><Manual Value=\"true\" /></Speaker><Volume>" + volumeMap + v ("Manual", std::to_string (volume)) +
           "<MidiControllerRange>" + v ("Min", "0.0003162277571") + v ("Max", "1") + "</MidiControllerRange></Volume><Panorama>" + v ("Manual", std::to_string (pan)) +
           "</Panorama></MixerDevice>";
}
inline std::string macros (const std::vector<std::string>& names, const std::vector<int>& values, const std::string& selectorMap)
{
    using als175a::v;
    std::string s;
    for (int k = 0; k < 16; ++k)
    {
        const auto n = std::to_string (k);
        s += "<MacroControls." + n + ">" + v ("Manual", std::to_string (k < (int) values.size() ? values[(std::size_t) k] : 0)) + "</MacroControls." + n + ">";
        s += v (("MacroDisplayNames." + n).c_str(), k < (int) names.size() ? names[(std::size_t) k] : "Macro " + std::to_string (k + 1));
        s += v (("MacroDefaults." + n).c_str(), "-1");
        s += v (("ExcludeMacroFromRandomization." + n).c_str(), k == 3 ? "true" : "false");
        s += v (("ExcludeMacroFromSnapshots." + n).c_str(), "false");
    }
    s += "<ChainSelector>" + selectorMap + v ("Manual", "20") + "<MidiControllerRange>" + v ("Min", "0") + v ("Max", "127") + "</MidiControllerRange></ChainSelector>";
    s += v ("NumVisibleMacroControls", "4");
    return s;
}
} // namespace rack183c

namespace rack183c
{
// The set of the import test: an Audio Effect Rack on an audio track, an Instrument Rack on a MIDI track
inline std::string rackSet()
{
    using als175a::v;
    // an Audio Effect Rack: "Dry" (a VST2 whose parameter 1 is on macro 3, 0.2…0.8) and "Wet" (Live's EQ Eight: dropped),
    // chain 2's volume on macro 2, the selector on macro 1; a stored variation "Verse"
    const auto mappedVst2 = std::string ("<PluginDevice Id=\"1\"><On><Manual Value=\"true\" /></On><PluginDesc><VstPluginInfo Id=\"0\">") + v ("Path", "C:/VST/kHs Gain.dll") +
                            v ("PlugName", "kHs Gain") + v ("UniqueId", "1802725230") + v ("NumAudioInputs", "1") + "</VstPluginInfo></PluginDesc><ParameterList>" +
                            "<PluginFloatParameter Id=\"0\">" + v ("ParameterName", "Gain") + v ("ParameterId", "1") + "<ParameterValue>" + keyMidi (2) +
                            v ("Manual", "0.5") + "<MidiControllerRange>" + v ("Min", "0.2") + v ("Max", "0.8") + "</MidiControllerRange></ParameterValue>" +
                            "</PluginFloatParameter></ParameterList></PluginDevice>";
    const auto audioRack = "<AudioEffectGroupDevice Id=\"5\"><On><Manual Value=\"true\" /></On>" + v ("UserName", "Bus") + "<Branches>" +
                           "<AudioEffectBranch Id=\"0\"><Name>" + v ("EffectiveName", "kHs Gain") + v ("UserName", "Dry") + "</Name>" + v ("IsSoloed", "false") +
                           "<DeviceChain><AudioToAudioDeviceChain><Devices>" + mappedVst2 + "</Devices></AudioToAudioDeviceChain></DeviceChain>" +
                           range ("BranchSelectorRange", 0, 63, 0, 40) + branchMixer (1.0, 0.0) + "</AudioEffectBranch>" +
                           "<AudioEffectBranch Id=\"1\"><Name>" + v ("EffectiveName", "EQ Eight") + v ("UserName", "") + "</Name>" + v ("IsSoloed", "false") +
                           "<DeviceChain><AudioToAudioDeviceChain><Devices><Eq8 Id=\"0\">" + v ("UserName", "") + "</Eq8></Devices></AudioToAudioDeviceChain></DeviceChain>" +
                           range ("BranchSelectorRange", 40, 127, 63, 127) + branchMixer (0.5, -0.25, keyMidi (1)) + "</AudioEffectBranch></Branches>" +
                           macros ({ "Tone", "Macro 2", "Level" }, { 64, 10, 100 }, keyMidi (0)) +
                           "<MacroVariations><MacroSnapshots><MacroSnapshot Id=\"0\">" + v ("SnapshotName", "Verse");
    std::string snap;
    for (int k = 0; k < 16; ++k)
        snap += v (("MacroValues." + std::to_string (k)).c_str(), k == 0 ? "32" : "-1");
    for (int k = 0; k < 16; ++k)
        snap += v (("MacroHasValue." + std::to_string (k)).c_str(), k == 0 ? "true" : "false");
    const auto audioRackFull = audioRack + snap + "</MacroSnapshot></MacroSnapshots></MacroVariations></AudioEffectGroupDevice>";
    // an Instrument Rack: one chain, a VST3 instrument (DeviceType 1) on keys 0…59
    std::string instrument = als175a::vst3Device ("01");
    for (std::size_t p = instrument.find ("<DeviceType Value=\"2\" />"); p != std::string::npos; p = instrument.find ("<DeviceType Value=\"2\" />"))
        instrument.replace (p, 24, "<DeviceType Value=\"1\" />");
    const auto instrumentRack = "<InstrumentGroupDevice Id=\"6\"><On><Manual Value=\"true\" /></On><Branches><InstrumentBranch Id=\"0\"><Name>" + v ("EffectiveName", "Keys") +
                                "</Name><DeviceChain><MidiToAudioDeviceChain><Devices>" + instrument + "</Devices></MidiToAudioDeviceChain></DeviceChain>" +
                                range ("BranchSelectorRange", 0, 127, 0, 127) + "<ZoneSettings>" + range ("KeyRange", 0, 59, 0, 59) + range ("VelocityRange", 1, 127, 1, 127) +
                                "</ZoneSettings>" + branchMixer (1.0, 0.0) + "</InstrumentBranch></Branches>" + macros ({}, {}, {}) + "</InstrumentGroupDevice>";
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12117\" Creator=\"test\">\n<LiveSet><Tracks>";
    s += "<AudioTrack Id=\"20\">" + als175a::name ("Vocal") + v ("Color", "20") + v ("TrackGroupId", "-1") + "<DeviceChain>" + als175a::mixer (200, 1.0, 0.0, true) +
         "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting><DeviceChain><Devices>" + audioRackFull + "</Devices></DeviceChain></DeviceChain></AudioTrack>";
    s += "<MidiTrack Id=\"10\">" + als175a::name ("Keys") + v ("Color", "3") + v ("TrackGroupId", "-1") + "<DeviceChain>" + als175a::mixer (100, 1.0, 0.0, true) +
         "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting><DeviceChain><Devices>" + instrumentRack + "</Devices></DeviceChain></DeviceChain></MidiTrack>";
    s += "</Tracks><MainTrack><DeviceChain>" + als175a::mixer (900, 1.0, 0.0, true, {}, "<Tempo><Manual Value=\"120\" /></Tempo>") + "</DeviceChain></MainTrack></LiveSet>\n</Ableton>\n";

    return s;
}
} // namespace rack183c

TEST (rack183c_als_racks_import_chains_zones_macros_and_variations)
{
    using namespace rack183c;
    const auto s = rackSet();
    FlpImportReport report;
    std::string error;
    const auto song = importAls (als175a::bytesOf (s), als175a::options (&report), &error);
    CHECK (song.has_value());
    if (! song)
        return;
    const MixerTrack* vocal = nullptr;
    for (const auto& t : song->mixer)
        if (t.name == "Vocal")
            vocal = &t;
    CHECK (vocal != nullptr && vocal->effects.size() == 1 && rack::isRack (vocal->effects[0]));
    if (vocal == nullptr || vocal->effects.empty() || ! rack::isRack (vocal->effects[0]))
        return;
    const auto& doc = vocal->effects[0].patcher;
    const auto& r = *rack::rackOf (doc);
    CHECK (r.kind == RackDoc::Kind::audioEffect && r.chains.size() == 2);
    if (r.chains.size() != 2)
        return;
    CHECK (r.chains[0].name == "Dry" && r.chains[1].name == "EQ Eight"); // (no user name: Live's effective name)
    CHECK (r.chains[0].nodes.size() == 1 && r.chains[1].nodes.empty());
    CHECK (r.chains[0].select == (RackZone { 0, 63, 0, 40 }) && r.chains[1].select == (RackZone { 40, 127, 63, 127 }));
    // chain 2's volume follows its macro (2, at 10 of 127, mapped from Live's floor to 0 dB), not its stored 0.5: Live drives a
    // mapped parameter from the macro
    CHECK (approx (r.chains[1].volume, rack::volumeGain (rack::unityVolume * 10.0f / 127.0f), 1.0e-5f) && approx (r.chains[1].pan, -0.25f));
    CHECK (approx (20.0f * std::log10 (rack::volumeGain (rack::unityVolume * 91.0f / 127.0f)), -9.637f, 0.01f)); // (Live's -9.637 dB [SHOT])
    CHECK (r.macros[0].name == "Tone" && r.macros[1].name.empty() && r.macros[2].name == "Level" && r.macros[0].value == 64.0f && r.macros[3].excludeFromRandom);
    CHECK (r.shownMacros == 4 && r.selector == 20.0f);
    // the mappings: macro 3 → the plugin's parameter 1 (0.2…0.8); macro 2 → chain 2's volume; macro 1 → the selector
    RackMapping p;
    p.node = r.chains[0].nodes[0], p.param = 1;
    int at2 = -1;
    CHECK (rack::macroOf (r, p, &at2) == 2 && approx (r.macros[2].mappings[(std::size_t) std::max (0, at2)].min, 0.2f)
           && approx (r.macros[2].mappings[(std::size_t) std::max (0, at2)].max, 0.8f));
    RackMapping vol;
    vol.target = RackMapping::Target::chainVolume, vol.chain = 1;
    CHECK (rack::macroOf (r, vol) == 1);
    RackMapping sel;
    sel.target = RackMapping::Target::selector;
    CHECK (rack::macroOf (r, sel) == 0);
    CHECK (r.variations.size() == 1 && r.variations[0].name == "Verse" && r.variations[0].has[0] && r.variations[0].values[0] == 32.0f && ! r.variations[0].has[1]);
    CHECK (report.droppedEffects.count ("Eq8") == 1);
    // the instrument rack: a channel whose Patcher is a rack, its chain's instrument the VST3, keys 0…59
    const ChannelDef* keys = nullptr;
    for (const auto& c : song->channels)
        if (c.name == "Keys")
            keys = &c;
    CHECK (keys != nullptr && rack::isRack (*keys));
    if (keys == nullptr || ! rack::isRack (*keys))
        return;
    const auto& ir = *rack::rackOf (keys->patcher);
    CHECK (ir.kind == RackDoc::Kind::instrument && ir.chains.size() == 1 && ir.chains[0].nodes.size() == 1
           && ir.chains[0].key == (RackZone { 0, 59, 0, 59 }));
    if (! ir.chains.empty() && ! ir.chains[0].nodes.empty())
    {
        const auto& n = keys->patcher.nodes[(std::size_t) ir.chains[0].nodes[0]];
        CHECK (n.generator && n.channel.instrument == InstrumentKind::plugin && n.channel.plugin.name == "kHs Gain");
    }
}

TEST (rack183c_als_racks_export_and_round_trip)
{
    using namespace rack183c;
    const auto first = importAls (als175a::bytesOf (rackSet()), als175a::options (nullptr));
    CHECK (first.has_value());
    if (! first)
        return;
    const auto racksOf = [] (const Song& song) {
        std::vector<RackDoc> out;
        for (const auto& t : song.mixer)
            for (const auto& fx : t.effects)
                if (rack::isRack (fx))
                    out.push_back (fx.patcher.rack.front());
        for (const auto& c : song.channels)
            if (rack::isRack (c))
                out.push_back (c.patcher.rack.front());
        return out;
    };
    const auto before = racksOf (*first);
    CHECK (before.size() == 2);
    for (const auto version : { AlsVersion::live12, AlsVersion::live11 })
    {
        AlsExportOptions o;
        o.version = version;
        const auto out = exportAls (*first, o);
        CHECK (! out.bytes.empty());
        std::string why;
        CHECK (als175a::loaderRulesHold (out.xml, 0, why));
        if (! why.empty())
            std::printf ("  loader rule: %s\n", why.c_str());
        CHECK (out.xml.find ("<AudioEffectGroupDevice") != std::string::npos && out.xml.find ("<InstrumentGroupDevice") != std::string::npos);
        CHECK (out.xml.find ("<MacroSnapshot ") != std::string::npos && out.xml.find ("<Channel Value=\"16\" />") != std::string::npos);
        const auto back = importAls (out.bytes, als175a::options (nullptr));
        CHECK (back.has_value());
        if (! back)
            continue;
        const auto after = racksOf (*back);
        CHECK (after.size() == before.size());
        for (std::size_t i = 0; i < std::min (after.size(), before.size()); ++i)
        {
            const auto& a = before[i];
            const auto& b = after[i];
            CHECK (a.kind == b.kind && a.chains.size() == b.chains.size() && a.shownMacros == b.shownMacros && a.selector == b.selector);
            for (std::size_t c = 0; c < std::min (a.chains.size(), b.chains.size()); ++c)
                CHECK (a.chains[c].name == b.chains[c].name && a.chains[c].select == b.chains[c].select && a.chains[c].key == b.chains[c].key
                       && a.chains[c].velocity == b.chains[c].velocity && approx (a.chains[c].volume, b.chains[c].volume, 1.0e-5f)
                       && approx (a.chains[c].pan, b.chains[c].pan, 1.0e-5f) && a.chains[c].nodes.size() == b.chains[c].nodes.size());
            for (int k = 0; k < rackMacroCount; ++k)
            {
                const auto& x = a.macros[(std::size_t) k];
                const auto& y = b.macros[(std::size_t) k];
                CHECK (x.name == y.name && x.value == y.value && x.excludeFromRandom == y.excludeFromRandom && x.mappings.size() == y.mappings.size());
                for (std::size_t m = 0; m < std::min (x.mappings.size(), y.mappings.size()); ++m)
                    CHECK (x.mappings[m].target == y.mappings[m].target && approx (x.mappings[m].min, y.mappings[m].min, 1.0e-4f)
                           && approx (x.mappings[m].max, y.mappings[m].max, 1.0e-4f) && x.mappings[m].param == y.mappings[m].param);
            }
            CHECK (a.variations == b.variations);
        }
    }
}

TEST (rack183c_midi_effect_rack_plays_its_children_only)
{
    using namespace lynx;
    // channel 0: a MIDI Effect Rack (one chain: VFX Key Mapper at its identity map); channels 1 and 2 its children,
    // channel 3 not (decision R1: like a Layer's children)
    Song song;
    song.mixer = Song::defaultMixer (2);
    ChannelDef midiRack;
    midiRack.name = "MIDI Effect Rack";
    midiRack.instrument = InstrumentKind::patcher;
    midiRack.patcher = rack::makeRack (RackDoc::Kind::midiEffect);
    PatchNode mapper;
    mapper.generator = true;
    vfx::makeChannel (mapper.channel, vfx::Kind::keyMapper);
    CHECK (rack::addDevice (midiRack.patcher, 0, mapper) >= 0);
    midiRack.patcher.rack[0].chains[0].select = { 0, 127, 0, 127 };
    midiRack.patcher.rack[0].children = { 1, 2 };
    rack::compile (midiRack.patcher);
    CHECK (! patcher::vfxHostPorts (midiRack.patcher).empty());
    song.channels.push_back (midiRack);
    for (int k = 1; k <= 3; ++k)
    {
        ChannelDef c;
        c.instrument = InstrumentKind::plugin;
        c.plugin.id = "spy" + std::to_string (k);
        song.channels.push_back (c);
    }
    song.patterns.emplace_back();
    std::vector<std::shared_ptr<layer164::NoteSpy>> spies (song.channels.size());
    RackState::Factories f;
    f.pluginInstruments = [&spies] (const PluginRef& ref, double) {
        const auto k = std::stoi (ref.id.substr (3));
        spies[(std::size_t) k] = std::make_shared<layer164::NoteSpy>();
        return std::shared_ptr<Instrument> (spies[(std::size_t) k]);
    };
    const auto state = RackState::build (song, nullptr, 44100.0, f);
    RackRenderer renderer;
    renderer.prepare (44100.0, 256);
    LiveEventQueue live;
    auto block = [&] {
        std::vector<float> l (256), r (256);
        renderer.process (state.get(), false, 0.0, 256, l.data(), r.data(), &live, nullptr);
    };
    NoteEvent on;
    on.channel = 0;
    on.key = 60;
    on.velocity = 0.8f;
    live.push (on);
    for (int b = 0; b < 4; ++b) // (what the rack plays goes out at the next chunk's start)
        block();
    CHECK (spies[1] != nullptr && spies[1]->ons() == 1 && spies[2] != nullptr && spies[2]->ons() == 1);
    CHECK (spies[3] != nullptr && spies[3]->ons() == 0);
    if (spies[1] != nullptr && ! spies[1]->got.empty())
        CHECK (spies[1]->got[0].key == 60);
    auto off = on;
    off.type = NoteEvent::Type::off;
    off.velocity = 0.0f;
    live.push (off);
    for (int b = 0; b < 4; ++b)
        block();
    CHECK (spies[1] != nullptr && spies[1]->got.size() == 2 && ! spies[1]->got.back().on);
}

TEST (rack183c_group_into_a_rack_keeps_the_sound_and_the_automation)
{
    using namespace rack183c;
    // insert 1: two utilities (the second at -12 dB), its gain automated; grouped into one Audio Effect Rack
    Song song;
    song.mixer = Song::defaultMixer (2);
    auto quiet = makeEffectSlot (EffectKind::utility);
    quiet.params[0] = 0.25f;
    song.mixer[1].effects = { makeEffectSlot (EffectKind::utility), quiet };
    Automation a;
    a.target = { AutomationTargetKind::effectParam, 1, 1, 0 };
    song.automations.push_back (a);
    const auto at = patcherize::patcherizeSlots (song, 1, 0, true);
    CHECK (at == 0 && song.mixer[1].effects.size() == 1);
    if (at != 0)
        return;
    auto& doc = song.mixer[1].effects[0].patcher;
    CHECK (rack::fromPatcher (doc));
    CHECK (rack::isRack (song.mixer[1].effects[0]) && doc.rack[0].chains.size() == 1 && doc.rack[0].chains[0].nodes == (std::vector<int> { 0, 1 }));
    // the automation now drives macro 0 (= its old published parameter), mapped on the same parameter over its full range
    const auto& target = song.automations[0].target;
    CHECK (target.slot == 0 && target.param >= 0 && target.param < rackMacroCount);
    if (target.param >= 0 && target.param < rackMacroCount)
    {
        const auto& m = doc.rack[0].macros[(std::size_t) target.param];
        CHECK (m.mappings.size() == 1 && m.mappings[0].node == 1 && m.mappings[0].param == 0 && approx (m.value, 0.25f * 127.0f, 1.0e-3f));
    }
    // the same sound: -12 dB
    auto g = graphOf (doc);
    run (*g);
    const auto o = run (*g);
    CHECK (approx (o[2], 0.5f * std::pow (10.0f, -12.0f / 20.0f), 2.0e-3f));
    // what can't go into a rack: a side-chain cable, a branch
    PatcherDoc branch;
    branch.nodes = { utilityNode(), utilityNode() };
    PatchWire w1, w2, w3, w4;
    w1.to = 0, w2.to = 1, w3.from = 0, w4.from = 1;
    branch.wires = { w1, w2, w3, w4 };
    CHECK (! rack::fromPatcher (branch) && ! branch.isRack());
    // a plugin parameter needs its current value (else its macro would move it)
    PatcherDoc plugin;
    PatchNode p;
    p.effect = makeEffectSlot (EffectKind::plugin);
    plugin.nodes = { p };
    PatchWire in, out;
    in.to = 0, out.from = 0;
    plugin.wires = { in, out };
    PatchParam pp;
    pp.node = 0, pp.param = 3;
    plugin.published = { pp };
    CHECK (! rack::fromPatcher (plugin));
    CHECK (rack::fromPatcher (plugin, [] (int, int) { return 0.4f; }) && approx (plugin.rack[0].macros[0].value, 0.4f * 127.0f, 1.0e-3f));
    // Ungroup gives the slots back: same parameters, the automation on the parameter again, a slot after it shifted
    song.mixer[1].effects.push_back (makeEffectSlot (EffectKind::fruityLimiter));
    Automation after;
    after.target = { AutomationTargetKind::effectParam, 1, 1, 2 };
    song.automations.push_back (after);
    CHECK (rack::ungroupSlot (song, 1, 0) == 0);
    const auto& fx = song.mixer[1].effects;
    CHECK (fx.size() == 3 && fx[0].kind == EffectKind::utility && fx[1].kind == EffectKind::utility && fx[2].kind == EffectKind::fruityLimiter);
    if (fx.size() == 3)
        CHECK (approx (fx[1].params[0], 0.25f, 1.0e-3f));
    CHECK (song.automations[0].target == (AutomationTarget { AutomationTargetKind::effectParam, 1, 1, 0 }));
    CHECK (song.automations[1].target == (AutomationTarget { AutomationTargetKind::effectParam, 1, 3, 2 }));
    // not a one-chain Audio Effect Rack: untouched
    const auto before = song.mixer[1].effects.size();
    CHECK (rack::ungroupSlot (song, 1, 0) == -1 && song.mixer[1].effects.size() == before);
    song.mixer[1].effects.push_back (rack::makeRackSlot());
    rack::addChain (song.mixer[1].effects.back().patcher);
    CHECK (rack::ungroupSlot (song, 1, (int) before) == -1);
}
