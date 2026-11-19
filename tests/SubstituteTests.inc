#include "core/LynxVerbs.h" // (round 190mb: Epoch's rate law index in the VintageVerb substitute test)
// --- LYNX substitutes for missing third-party plugins (core/PluginSubstitutes.h) - round 98 -------------------------

namespace substtest
{
std::vector<std::uint8_t> bytesOf (const std::string& s)
{
    return { s.begin(), s.end() };
}

void putRecord (std::vector<std::uint8_t>& out, std::uint32_t id, const std::vector<std::uint8_t>& data)
{
    for (int i = 0; i < 4; ++i)
        out.push_back ((std::uint8_t) (id >> (8 * i)));
    const auto size = (std::uint64_t) data.size();
    for (int i = 0; i < 8; ++i)
        out.push_back ((std::uint8_t) (size >> (8 * i)));
    out.insert (out.end(), data.begin(), data.end());
}

// FL's VST3 state (record 0x35): u32 0 + records (3 = component, 4 = parameter slots)
std::vector<std::uint8_t> flVst3State (const std::vector<std::uint8_t>& component)
{
    std::vector<std::uint8_t> state { 0, 0, 0, 0 };
    putRecord (state, 4, { 1, 0, 0, 0, 0x12, 0, 0, 0 });
    putRecord (state, 3, component);
    return state;
}

std::array<std::uint8_t, 16> classIdOf (const char* hex)
{
    std::array<std::uint8_t, 16> id {};
    for (int i = 0; i < 16; ++i)
        id[(std::size_t) i] = (std::uint8_t) std::stoul (std::string (hex + 2 * i, 2), nullptr, 16);
    return id;
}

// a JUCE-style state as a VST2-in-VST3 wrapper would hold it: some binary header, then "VC2!" + size + XML
std::vector<std::uint8_t> juceXmlComponent (const std::string& element)
{
    auto c = bytesOf (std::string ("VstW\0\0\0\x08", 8));
    c.insert (c.end(), 40, 0x00);
    const auto xml = std::string ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") + element;
    auto vc2 = bytesOf ("VC2!");
    const auto size = (std::uint32_t) xml.size() + 1;
    for (int i = 0; i < 4; ++i)
        vc2.push_back ((std::uint8_t) (size >> (8 * i)));
    c.insert (c.end(), vc2.begin(), vc2.end());
    c.insert (c.end(), xml.begin(), xml.end());
    c.push_back (0);
    c.insert (c.end(), { 0x00, 0x01, 0x02, 0xFF });
    return c;
}

std::vector<std::uint8_t> wavesComponent (const std::string& pluginName, const std::string& parametersA, const std::string& active = "SETUP_A")
{
    auto c = bytesOf (std::string ("\0\0\x03\xe2\0\0\0\x03\0\0\0\x01URCSsetA\0\0\x03\xc6XPst", 28));
    c.push_back (0);
    const auto xml = "<PresetChunkXMLTree version=\"3\">\n <Preset Name=\"\" GenericType=\"ARCS\">\n  <PresetHeader>\n   <PluginName>"
                     + pluginName + "</PluginName>\n   <ActiveSetup>" + active
                     + "</ActiveSetup>\n  </PresetHeader>\n  <PresetData Setup=\"SETUP_A\">\n   <Parameters Type=\"RealWorld\">\n"
                     + parametersA + "\n</Parameters>\n  </PresetData>\n  <PresetData Setup=\"SETUP_B\">\n   <Parameters Type=\"RealWorld\">\n7 8 9\n</Parameters>\n  </PresetData>\n </Preset>\n</PresetChunkXMLTree>\n";
    c.insert (c.end(), xml.begin(), xml.end());
    return c;
}
} // namespace substtest

TEST (substitute_builtin_table_parses)
{
    int errors = -1;
    // the built-in table (through the registry) has no bad line; re-parsing a copy of an entry counts errors
    const auto& entries = lynx::pluginSubstitutes();
    CHECK (entries.size() >= 20);
    for (const auto& e : entries)
    {
        CHECK (! e.key.empty() && ! e.product.empty());
        CHECK (e.effect != lynx::EffectKind::plugin && e.effect != lynx::EffectKind::parallel);
        CHECK (! e.instrument);
        CHECK (! e.vst2Ids.empty() || ! e.vst3ClassIds.empty() || ! e.names.empty() || ! e.wavesNames.empty());
    }
    const auto parsed = lynx::parseSubstituteTable ("[a]\nlynx = Reeverb 2\npreset = Dry=0; Nope=1; Wet=x\nmap = xml:Mix -> mix\nmap = vst2:x -> Wet\n"
                                                    "bogus line\nfoo = bar\n[b]\nlynx = No such effect\n[c]\nlynx = Compressor\nvst2 = StEB; 0x10; 12345678901\n"
                                                    "vst3 = 00112233445566778899aabbccddeeff; 0011\nnames = FabFilter Pro-C 2 (Stereo)\n",
                                                    &errors);
    CHECK (parsed.size() == 2 && parsed[0].key == "a" && parsed[1].key == "c");
    CHECK (errors == 8); // Nope, Wet=x, vst2:x, bogus line, foo, entry b, 12345678901, 0011
    if (parsed.size() == 2)
    {
        CHECK (parsed[0].effect == lynx::EffectKind::reeverb2 && parsed[0].preset.size() == 1 && parsed[0].preset[0].first == 10);
        CHECK (parsed[0].params.size() == 1 && parsed[0].params[0].toSlotMix);
        CHECK (parsed[1].vst2Ids == std::vector<std::uint32_t> ({ 0x53744542u, 0x10u }));
        CHECK (parsed[1].vst3ClassIds.size() == 1 && parsed[1].vst3ClassIds[0][15] == 0xff);
        CHECK (parsed[1].names == std::vector<std::string> ({ "fabfilterproc2" })); // "(Stereo)" dropped
    }
}

TEST (substitute_valhalla_vst3_translates_mix_and_keeps_original)
{
    using namespace substtest;
    lynx::ForeignPlugin vv;
    vv.format = lynx::ForeignPlugin::Format::vst3;
    vv.classId = classIdOf ("7654535665657633616c68616c6c6176");
    vv.hasClassId = true;
    vv.name = "ValhallaVintageVerb";
    vv.path = "C:\\Program Files\\Common Files\\VST3\\ValhallaDSP\\ValhallaVintageVerb.vst3";
    vv.state = flVst3State (juceXmlComponent ("<ValhallaVintageVerb pluginVersion=\"4.0.5\" presetName=\"Big &amp; Wide\" Mix=\"0.42\" Decay=\"0.3\"/>"));
    vv.midiInPort = 3;

    const auto id = lynx::pluginIdentity (vv);
    const auto* entry = lynx::findPluginSubstitute (id, false);
    CHECK (entry != nullptr && entry->key == "valhalla-vintageverb");
    CHECK (lynx::findPluginSubstitute (id, true) == nullptr); // not an instrument

    const auto attrs = lynx::juceXmlStateAttributes (id.state);
    CHECK (attrs.size() == 4 && attrs[1].first == "presetName" && attrs[1].second == "Big & Wide" && attrs[2].second == "0.42");

    lynx::EffectSlot slot;
    slot.kind = lynx::EffectKind::plugin;
    slot.plugin.name = vv.name;
    slot.plugin.foreign = vv;
    slot.mix = 0.5f;
    slot.enabled = false;
    slot.sidechain = 4;
    const auto before = slot.plugin;
    CHECK (lynx::substituteEffectSlot (slot, id));
    CHECK (slot.kind == lynx::EffectKind::epochVerb && slot.isSubstitute()); // LYNX's own vintage-digital reverb (round 100)
    CHECK (std::abs (slot.mix - 0.5f) < 1e-6f);                       // FL's slot dry/wet; the plugin's Mix is the effect's own
    CHECK (! slot.enabled && slot.sidechain == 4);
    CHECK (std::abs (slot.params[0] - 0.42f) < 1e-6f && std::abs (slot.params[2] - 0.3f) < 1e-6f); // Mix, Decay: same laws
    CHECK (slot.substituteFor == before);
    CHECK (slot.substituteFor.foreign.state == vv.state); // byte for byte
    CHECK (lynx::substituteOriginalName (slot) == "ValhallaVintageVerb");

    // a second substitution doesn't apply (no longer a plugin slot); restoring gives the original back unchanged
    CHECK (! lynx::substituteEffectSlot (slot, id));
    CHECK (lynx::restoreOriginalPlugin (slot));
    CHECK (slot.kind == lynx::EffectKind::plugin && slot.plugin == before && ! slot.isSubstitute() && ! slot.enabled && slot.sidechain == 4);
    CHECK (! lynx::restoreOriginalPlugin (slot));

    // unreadable state: the default preset
    vv.state = flVst3State ({ 1, 2, 3 });
    int translated = -1;
    const auto plain = lynx::makeSubstituteSlot (*entry, nullptr, &translated);
    const auto id2 = lynx::pluginIdentity (vv);
    const auto fromJunk = lynx::makeSubstituteSlot (*entry, &id2, &translated);
    // (round 190mb: a state that came from an original device also gets the entry's `replaced` items — Epoch's rate law)
    auto junkBase = fromJunk;
    CHECK (junkBase.params[(std::size_t) lynx::verbs::epoch::rateLaw] == 1.0f && plain.params[(std::size_t) lynx::verbs::epoch::rateLaw] == 0.0f);
    junkBase.params[(std::size_t) lynx::verbs::epoch::rateLaw] = 0.0f;
    CHECK (translated == 0 && fromJunk.mix == plain.mix && plain.mix == 1.0f && junkBase.params == plain.params);
}

TEST (substitute_waves_shell_by_plugin_name)
{
    using namespace substtest;
    lynx::ForeignPlugin shell;
    shell.format = lynx::ForeignPlugin::Format::vst3;
    shell.name = "WaveShell1-VST3 15.5_x64";
    shell.path = "C:\\Program Files\\Common Files\\VST3\\WaveShell1-VST3 15.5_x64.vst3";
    shell.state = flVst3State (wavesComponent ("RComp", "160.31 -15 0 * * 0.5"));
    const auto id = lynx::pluginIdentity (shell);
    CHECK (lynx::wavesPluginName (id.state) == "RComp");
    const auto values = lynx::wavesRealWorldParams (id.state);
    CHECK (values.size() == 6 && std::abs (values[0] - 160.31) < 1e-9 && values[1] == -15.0 && std::isnan (values[3]) && values[5] == 0.5);
    const auto* entry = lynx::findPluginSubstitute (id, false);
    CHECK (entry != nullptr && entry->key == "waves-rcomp" && entry->effect == lynx::EffectKind::compressor);

    // the active setup is the one read
    const auto b = lynx::pluginIdentity ([&] { auto s = shell; s.state = flVst3State (wavesComponent ("RComp", "1 2", "SETUP_B")); return s; }());
    CHECK (lynx::wavesRealWorldParams (b.state) == std::vector<double> ({ 7, 8, 9 }));

    // CLA-2A by class ID alone (no state), H-Delay by name in the state
    lynx::PluginIdentity cla;
    cla.format = lynx::ForeignPlugin::Format::vst3;
    cla.name = shell.name;
    cla.classId = classIdOf ("4c545356324163536c612d3261207374");
    cla.hasClassId = true;
    CHECK (lynx::findPluginSubstitute (cla, false) != nullptr && lynx::findPluginSubstitute (cla, false)->key == "waves-cla-2a");
    shell.state = flVst3State (wavesComponent ("H-Delay", "1"));
    CHECK (lynx::findPluginSubstitute (lynx::pluginIdentity (shell), false) != nullptr
           && lynx::findPluginSubstitute (lynx::pluginIdentity (shell), false)->effect == lynx::EffectKind::echoform); // round 119

    // round 119: NS1 and Doubler have LYNX roles now
    shell.state = flVst3State (wavesComponent ("NS1", "1"));
    CHECK (lynx::findPluginSubstitute (lynx::pluginIdentity (shell), false) != nullptr
           && lynx::findPluginSubstitute (lynx::pluginIdentity (shell), false)->effect == lynx::EffectKind::quellDenoiser);
    shell.state = flVst3State (wavesComponent ("Doubler", "1"));
    CHECK (lynx::findPluginSubstitute (lynx::pluginIdentity (shell), false) != nullptr
           && lynx::findPluginSubstitute (lynx::pluginIdentity (shell), false)->effect == lynx::EffectKind::geminiDoubler);

    // a shell sub-plugin LYNX has no substitute for: nothing (the shell's file name must not match anything; round 124:
    // S1 Imager has one now, Vocal Rider serves as the example)
    shell.state = flVst3State (wavesComponent ("Vocal Rider", "1"));
    CHECK (lynx::findPluginSubstitute (lynx::pluginIdentity (shell), false) == nullptr);
    // a .lynx reference of an installed-elsewhere Waves plugin: its display name
    lynx::PluginIdentity named;
    named.format = lynx::ForeignPlugin::Format::vst3;
    named.name = "SSLChannel Stereo";
    named.state = wavesComponent ("SSLChannel", "0");
    CHECK (lynx::findPluginSubstitute (named, false) != nullptr && lynx::findPluginSubstitute (named, false)->effect == lynx::EffectKind::ridgeStrip);
}

TEST (substitute_vst2_ids_names_and_float_params)
{
    // EchoBoy (chunk state: nothing to translate) by its VST2 ID
    lynx::PluginIdentity echo;
    echo.format = lynx::ForeignPlugin::Format::vst2;
    echo.vst2Id = 0x53744542u;
    echo.name = "Some renamed plugin";
    const auto* e = lynx::findPluginSubstitute (echo, false);
    CHECK (e != nullptr && e->key == "soundtoys-echoboy");
    // by name / file name, case and punctuation aside, any format
    lynx::PluginIdentity proq;
    proq.format = lynx::ForeignPlugin::Format::vst3;
    proq.name = "Pro-Q 3";
    CHECK (lynx::findPluginSubstitute (proq, false) != nullptr && lynx::findPluginSubstitute (proq, false)->effect == lynx::EffectKind::facetEq);
    lynx::PluginIdentity prol;
    prol.format = lynx::ForeignPlugin::Format::vst2;
    prol.path = "C:\\VstPlugins\\FabFilter\\FabFilter Pro-L 2 x64.dll";
    CHECK (lynx::findPluginSubstitute (prol, false) != nullptr && lynx::findPluginSubstitute (prol, false)->effect == lynx::EffectKind::apexLimiter);
    lynx::PluginIdentity unknown;
    unknown.name = "Serum";
    CHECK (lynx::findPluginSubstitute (unknown, false) == nullptr);

    // a registered entry with a VST2 float-parameter map (the hook later modules use); later registrations win
    CHECK (lynx::registerPluginSubstitutes ("[test-vst2-floats]\nproduct = Test Verb\nlynx = Reeverb 2\nvst2 = TsT1\n"
                                            "vst2params = 3\nmap = vst2:2 -> Wet | 0 1 0 0.5\nmap = vst2:0 -> mix\nmap = vst2:9 -> Decay\n")
           == 1);
    lynx::FlVst2State s;
    s.version = -9;
    s.flags = 1; // all programs: 2 programs × 3 parameters, the second is current
    s.currentProgram = 1;
    s.params = { 0.1f, 0.2f, 0.3f, 0.6f, 0.7f, 0.8f };
    s.programNames = { "A", "B" };
    lynx::PluginIdentity t;
    t.format = lynx::ForeignPlugin::Format::vst2;
    t.vst2Id = 0x54735431u; // "TsT1"
    t.state = lynx::writeFlVst2State (s);
    const auto* te = lynx::findPluginSubstitute (t, false);
    CHECK (te != nullptr && te->key == "test-vst2-floats");
    if (te != nullptr)
    {
        int translated = 0;
        const auto slot = lynx::makeSubstituteSlot (*te, &t, &translated);
        CHECK (translated == 2); // parameter 9 doesn't exist
        CHECK (std::abs (slot.params[12] - 0.4f) < 1e-6f && std::abs (slot.mix - 0.6f) < 1e-6f);
    }
    CHECK (lynx::registerPluginSubstitutes ("[test-vst2-floats]\nlynx = Compressor\nvst2 = TsT1\n") == 1);
    CHECK (lynx::findPluginSubstitute (t, false) != nullptr && lynx::findPluginSubstitute (t, false)->effect == lynx::EffectKind::compressor);
}

TEST (substitute_foreign_text_round_trip_is_byte_identical)
{
    lynx::ForeignPlugin p;
    p.format = lynx::ForeignPlugin::Format::vst3;
    p.vst2Id = 0xDEADBEEF;
    for (int i = 0; i < 16; ++i)
        p.classId[(std::size_t) i] = (std::uint8_t) (i * 17);
    p.hasClassId = true;
    p.clapId = "com.example.x";
    p.name = "N\xc3\xa3o \"quoted\"";
    p.path = "C:\\x\\y.vst3";
    p.midiInPort = -1;
    p.midiOutPort = 7;
    for (std::size_t n : { 0u, 1u, 2u, 3u, 4u, 1000u, 65537u })
    {
        p.state.resize (n);
        for (std::size_t i = 0; i < n; ++i)
            p.state[i] = (std::uint8_t) (i * 31 + 7);
        lynx::ForeignPlugin back;
        CHECK (lynx::foreignFromText (lynx::foreignToText (p), back) && back == p);
    }
    lynx::ForeignPlugin out;
    CHECK (! lynx::foreignFromText ("", out) && ! lynx::foreignFromText ("not base64!", out) && ! lynx::foreignFromText ("AAAA", out));
    auto text = lynx::foreignToText (p);
    text.resize (text.size() / 2); // truncated
    CHECK (! lynx::foreignFromText (text, out));

    std::vector<std::uint8_t> bytes;
    CHECK (lynx::base64Decode ("aGVsbG8=", bytes) && std::string (bytes.begin(), bytes.end()) == "hello");
    CHECK (lynx::base64Encode ((const std::uint8_t*) "hello", 5) == "aGVsbG8=");
}

TEST (substitute_table_reaches_the_newest_effects)
{
    // effectKindNamed walks the enum up to its last value: a kind appended later must still be nameable in the table
    for (const auto kind : { lynx::EffectKind::facetEq, lynx::EffectKind::apexLimiter })
    {
        int errors = -1;
        const auto t = lynx::parseSubstituteTable (std::string ("[k]\nnames = X\nlynx = ") + lynx::effectInfo (kind).name + "\n", &errors);
        CHECK (errors == 0 && t.size() == 1 && t[0].effect == kind);
    }
}
