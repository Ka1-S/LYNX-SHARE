// Round 183c (docs/analise/REA_rodada183c.md): Ableton-style Racks as a Patcher in rack mode (core/Rack.h). The laws
// are Live 12.4's, measured on its renders (REA §8) and read from its stored mapping values (REA §4).

namespace rack183c
{
using namespace lynx;

bool near (float a, float b, float tol = 1.0e-3f) { return std::abs (a - b) <= tol; }

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
        CHECK (near (rack::zoneVelocity (z, 60, (float) v / 127.0f, 0.0f) * 127.0f, want, 0.01f));
    // the zone's first velocity still plays, at velocity 1
    CHECK (near (rack::zoneVelocity (z, 60, 1.0f / 127.0f, 0.0f) * 127.0f, 1.0f, 0.01f));
    // fade-out: full to 40 (Live: 50 / 100 / 127 -> 44.3 / 31.8 / 1.44)
    z.velocity = { 1, 127, 1, 40 };
    CHECK (near (rack::zoneVelocity (z, 60, 50.0f / 127.0f, 0.0f) * 127.0f, 44.32f, 0.02f));
    CHECK (near (rack::zoneVelocity (z, 60, 100.0f / 127.0f, 0.0f) * 127.0f, 31.82f, 0.02f));
    CHECK (near (rack::zoneVelocity (z, 60, 1.0f, 0.0f) * 127.0f, 1.443f, 0.01f));
    // key zone 48..127 full from 60 at velocity 127 (Live: keys 48 / 52 / 56 / 58 -> 9.72 / 48.60 / 87.92 / 107.45)
    PatchZone k;
    k.key = { 48, 127, 60, 127 };
    const std::pair<int, float> keys[] { { 48, 9.77f }, { 52, 48.85f }, { 56, 87.92f }, { 58, 107.46f }, { 60, 127.0f }, { 72, 127.0f } };
    for (const auto& [key, want] : keys)
        CHECK (near (rack::zoneVelocity (k, key, 1.0f, 0.0f) * 127.0f, want, 0.05f));
    // outside a zone: the note doesn't enter
    CHECK (rack::zoneVelocity (k, 47, 1.0f, 0.0f) == 0.0f);
    PatchZone s;
    s.select = { 20, 127, 60, 127 };
    s.selector = rack::selectorOutput;
    CHECK (rack::zoneVelocity (s, 60, 1.0f, 19.0f) == 0.0f);
    CHECK (rack::zoneVelocity (s, 60, 1.0f, 40.0f) == 1.0f); // (an Instrument Rack's select fade is on its output, not the velocity)
    s.selectorScalesVelocity = true;                          // (a MIDI Effect Rack's is on the velocity)
    CHECK (near (rack::zoneVelocity (s, 60, 1.0f, 40.0f), 21.0f / 41.0f));
}

TEST (rack183c_output_fade_pan_and_volume_match_lives_renders)
{
    using namespace rack183c;
    // chain select, zone 20..127 full from 60: sin (pi/2 * fade) (Live: 0.0383 / 0.2279 / 0.4091 / 0.7205 / 0.9275 / 0.9817)
    const RackZone in { 20, 127, 60, 127 };
    const std::pair<float, float> ins[] { { 20, 0.0383f }, { 25, 0.2279f }, { 30, 0.4091f }, { 40, 0.7205f }, { 50, 0.9275f }, { 55, 0.9817f }, { 60, 1.0f }, { 90, 1.0f } };
    for (const auto& [x, want] : ins)
        CHECK (near (rack::selectOutputGain (in, x), want));
    // fade-out 0..100 full to 40 (Live: 55 / 70 / 100 -> 0.9263 / 0.7162 / 0.0257)
    const RackZone out { 0, 100, 0, 40 };
    CHECK (near (rack::selectOutputGain (out, 55.0f), 0.9263f));
    CHECK (near (rack::selectOutputGain (out, 70.0f), 0.7162f));
    CHECK (near (rack::selectOutputGain (out, 100.0f), 0.0257f));
    // outside: silent on a side that fades; on a side with no fade the output rings on (the input closes instead)
    CHECK (rack::selectOutputGain (in, 10.0f) == 0.0f && rack::selectOutputGain (out, 110.0f) == 0.0f);
    const RackZone hard { 64, 127, 64, 127 };
    CHECK (rack::selectOutputGain (hard, 10.0f) == 1.0f && ! rack::selectInputOpen (hard, 10.0f) && rack::selectInputOpen (hard, 64.0f));
    // pan (Live: -1 -> +3.01 dB / silent; -0.5 -> +2.32 / -5.33 dB; centre unity)
    float l = 0.0f, r = 0.0f;
    rack::panGains (0.0f, l, r);
    CHECK (l == 1.0f && r == 1.0f);
    rack::panGains (-0.5f, l, r);
    CHECK (near (l, 1.3066f) && near (r, 0.5412f));
    rack::panGains (1.0f, l, r);
    CHECK (near (l, 0.0f) && near (r, 1.4142f));
    // the chain volume taper (Live's stored values): 0 dB at 0.85, -10 dB at 0.6, +6 dB at 1, -32 dB near 0.22
    CHECK (near (rack::volumeGain (0.85f), 1.0f, 1.0e-4f));
    CHECK (near (20.0f * std::log10 (rack::volumeGain (0.6f)), -10.0f, 0.01f));
    CHECK (near (20.0f * std::log10 (rack::volumeGain (1.0f)), 6.0f, 0.01f));
    CHECK (std::abs (20.0f * std::log10 (rack::volumeGain (0.22f)) - (-32.0f)) < 1.0f);
    CHECK (rack::volumeGain (0.0f) == 0.0f);
    for (const float v : { 0.1f, 0.3f, 0.4f, 0.6f, 0.85f, 1.0f })
        CHECK (near (rack::volumeValue (rack::volumeGain (v)), v, 1.0e-4f));
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
        CHECK (near (o[2], 1.0f) && near (o[3], 1.0f));
    }
    // solo: only the soloed chain; the activator: a chain off is silent
    rack::setSolo (doc, 1, true);
    rack::setChainVolume (doc, 1, 0.5f);
    {
        auto g = graphOf (doc);
        const auto o = run (*g);
        CHECK (near (o[2], 0.25f));
    }
    rack::setSolo (doc, 1, false);
    rack::setActive (doc, 0, false);
    rack::setChainVolume (doc, 1, 1.0f);
    rack::setChainPan (doc, 1, -0.5f);
    {
        auto g = graphOf (doc);
        const auto o = run (*g);
        CHECK (near (o[2], 0.5f * 1.3066f) && near (o[3], 0.5f * 0.5412f));
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
        CHECK (near (o[2], 0.5f * 0.7205f, 2.0e-3f));
        // automation of the selector (published parameter 16) moves it on the audio thread
        g->setPublished (rack::selectorOutput, 90.0f / 127.0f);
        run (*g);
        const auto o2 = run (*g);
        CHECK (near (o2[2], 0.5f, 2.0e-3f));
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
    CHECK (near (doc.nodes[(std::size_t) u].effect.params[0], 0.25f));
    CHECK (near (rack::mappedValue (m, 63.5f), 0.375f));
    {
        auto g = graphOf (doc);
        run (*g);
        const auto o = run (*g);
        CHECK (near (o[2], 0.5f * std::pow (10.0f, -12.0f / 20.0f), 2.0e-3f));
        // automation of macro 2 (published parameter 1): back to 0 dB
        g->setPublished (1, 0.0f);
        run (*g);
        const auto o2 = run (*g);
        CHECK (near (o2[2], 0.5f, 2.0e-3f));
    }
    // a macro on a chain's volume
    RackMapping v;
    v.target = RackMapping::Target::chainVolume, v.chain = 0, v.min = 0.0f, v.max = rack::unityVolume;
    CHECK (rack::map (doc, 2, v));
    rack::setMacro (doc, 2, 127.0f);
    CHECK (near (doc.rack[0].chains[0].volume, 1.0f, 1.0e-4f));
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
    CHECK (near (doc.nodes[(std::size_t) u].effect.params[0], 10.0f / 127.0f));
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
    CHECK (near (mv[0], (64.0f + 110.0f) / 2.0f) && near (mv[1], (20.0f + 120.0f) / 2.0f) && mv[2] == doc.rack[0].macros[2].value);
    rack::setMorph (doc, 0.5f);
    CHECK (near (doc.rack[0].macros[0].value, 87.0f));
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
