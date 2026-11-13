// --- Round 190ca: state maps of the stand-in rows (docs/PARITY.md, the tail lab) -------------------------------------
// The ValhallaDelay translator's Tape level, the per-device default presets of the Complete Access reverb role, and the
// guarantee the maps rest on: a slot that already is a substitute is never translated again (saved projects keep theirs).

namespace p190ca
{
inline lynx::ForeignPlugin vst3Named (const char* name, const std::string& xml = {}, const char* classHex = nullptr)
{
    lynx::ForeignPlugin f;
    f.format = lynx::ForeignPlugin::Format::vst3;
    if (classHex != nullptr)
    {
        f.classId = substtest::classIdOf (classHex);
        f.hasClassId = true;
    }
    f.name = name;
    f.path = std::string ("C:\\Program Files\\Common Files\\VST3\\") + name + ".vst3";
    if (! xml.empty())
        f.state = substtest::flVst3State (substtest::juceXmlComponent (xml));
    return f;
}
inline int paramNamed (lynx::EffectKind kind, const char* name)
{
    const auto& params = lynx::effectInfo (kind).params;
    for (std::size_t i = 0; i < params.size(); ++i)
        if (std::string (params[i].name) == name)
            return (int) i;
    return 0;
}
inline lynx::EffectSlot substituted (const lynx::ForeignPlugin& f)
{
    lynx::EffectSlot slot;
    slot.kind = lynx::EffectKind::plugin;
    slot.plugin.name = f.name;
    slot.plugin.foreign = f;
    lynx::substituteEffectSlot (slot, lynx::pluginIdentity (f));
    return slot;
}
} // namespace p190ca

TEST (p190ca_valhalladelay_tape_echoes_at_the_measured_level)
{
    namespace ef = lynx::echosuite::echoform;
    const char* cls = "64545356614c7679616c68616c6c6164";
    // Tape (the default mode, 1/24): round 190ca's Output -1.67 dB is gone since round 190dl — the measured tape stage
    // (Law = Measured, which a translated slot gets) has the level law itself
    const auto tape = p190ca::substituted (p190ca::vst3Named ("ValhallaDelay", "<ValhallaDelay Mode=\"0.0416666679084301\" Mix=\"0.5\"/>", cls));
    CHECK (tape.kind == lynx::EffectKind::echoform && ef::modeOf (tape.params[ef::mode]) == ef::Mode::tape);
    CHECK (std::abs (tape.params[ef::output] - 0.5f) < 1e-6f && tape.params[ef::engineLaw] >= 0.5f);
    CHECK (std::abs (tape.params[ef::mix] - 0.5f) < 1e-6f); // the dry/wet setting is the reference's own
    // every other mode stays at 0 dB, the tape variants (DuckTape 10/24, ChromeTape 16/24) included: not measured
    for (const char* value : { "0.10", "0.1875", "0.4375", "0.5625", "0.6875", "0.7291667" })
    {
        const auto s = p190ca::substituted (p190ca::vst3Named ("ValhallaDelay", std::string ("<ValhallaDelay Mode=\"") + value + "\"/>", cls));
        CHECK (s.kind == lynx::EffectKind::echoform && std::abs (s.params[ef::output] - 0.5f) < 1e-6f);
    }
}

TEST (p190ca_reverb_role_devices_start_at_their_own_default)
{
    using namespace lynx;
    const auto paramOf = [] (const EffectSlot& s, const char* name) { return s.params[(std::size_t) p190ca::paramNamed (s.kind, name)]; };
    // (round 190cm: FlexVerb and VerbSuite, all wet at their own default, keep the role's 25 % slot mix — a stand-in must
    // not open wetter than it did before round 190ca; the two plates' even mix is Foil's own Mix at a slot mix of 100 %)
    struct Want { const char* name; float mix, decay, voicing, type, width, slotMix; };
    for (const auto& w : { Want { "SSL PlateVerb", 0.2687f, 0.3028f, 1.0f, 0.29f, 0.5f, 1.0f }, Want { "SSL SpringVerb", 0.1933f, 0.4877f, 0.0f, 0.375f, 0.5f, 1.0f },
                           Want { "SSL Native FlexVerb", 1.0f, 0.0541f, 0.0f, 0.875f, 0.4694f, 0.25f }, Want { "VerbSuite Classics", 1.0f, 0.278f, 1.0f, 0.29f, 0.5f, 0.25f } })
    {
        const auto s = p190ca::substituted (p190ca::vst3Named (w.name));
        CHECK (s.kind == EffectKind::foilPlate && s.isSubstitute());
        CHECK (s.mix == w.slotMix); // Foil's own Mix carries the device's dry/wet, or the role's slot mix when unknown
        CHECK (std::abs (paramOf (s, "Mix") - w.mix) < 1e-6f && std::abs (paramOf (s, "Decay") - w.decay) < 1e-6f);
        CHECK (std::abs (paramOf (s, "Voicing") - w.voicing) < 1e-6f && std::abs (paramOf (s, "Type") - w.type) < 1e-6f);
        CHECK (std::abs (paramOf (s, "Width") - w.width) < 1e-6f);
        // both shelves at 700 Hz (equal gains = a flat wet gain), the modulation at its slowest rate
        CHECK (std::abs (paramOf (s, "Low EQ freq") - 0.742f) < 1e-6f && std::abs (paramOf (s, "High EQ freq") - 0.0606f) < 1e-6f);
        CHECK (paramOf (s, "Mod rate") == 0.0f);
    }
    // the role's other devices keep the role preset (no measured default of their own), and so does the entry itself
    for (const char* name : { "Lustrous Plates", "SSL GateVerb", "Seventh Heaven", "smartreverb" })
    {
        const auto s = p190ca::substituted (p190ca::vst3Named (name));
        CHECK (s.kind == EffectKind::foilPlate && std::abs (s.mix - 0.25f) < 1e-6f);
        CHECK (std::abs (paramOf (s, "Decay") - 0.3f) < 1e-6f && paramOf (s, "Mix") == 1.0f && paramOf (s, "Voicing") == 0.0f);
    }
    const auto id = pluginIdentity (p190ca::vst3Named ("SSL PlateVerb"));
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr && entry->key == "complete-access-reverbs");
    if (entry != nullptr)
    {
        const auto plain = makeSubstituteSlot (*entry); // the quick-add palette: no original plugin
        CHECK (std::abs (plain.mix - 0.25f) < 1e-6f && std::abs (paramOf (plain, "Decay") - 0.3f) < 1e-6f && paramOf (plain, "Voicing") == 0.0f);
    }
}

TEST (p190ca_device_defaults_parse_and_report_errors)
{
    using namespace lynx;
    int errors = 0;
    auto t = parseSubstituteTable ("[p190ca-test]\nlynx = Foil Plate\nnames = Some Verb; Other Verb\npreset = Decay=0.4\nmix = 0.5\n"
                                   "default = Other Verb | Decay=0.7; Voicing=1\n", &errors);
    CHECK (errors == 0 && t.size() == 1);
    if (t.size() == 1)
    {
        PluginIdentity a, b;
        a.format = b.format = ForeignPlugin::Format::vst3;
        a.name = "Some Verb";
        b.name = "Other Verb";
        const auto sa = makeSubstituteSlot (t[0], &a), sb = makeSubstituteSlot (t[0], &b);
        const auto decay = (std::size_t) p190ca::paramNamed (EffectKind::foilPlate, "Decay"), voicing = (std::size_t) p190ca::paramNamed (EffectKind::foilPlate, "Voicing");
        CHECK (std::abs (sa.params[decay] - 0.4f) < 1e-6f && sa.params[voicing] == 0.0f && std::abs (sa.mix - 0.5f) < 1e-6f);
        CHECK (std::abs (sb.params[decay] - 0.7f) < 1e-6f && sb.params[voicing] == 1.0f && sb.mix == 1.0f);
    }
    // a default without a name, or with an unknown parameter, counts as an error
    parseSubstituteTable ("[p190ca-test]\nlynx = Foil Plate\nnames = Some Verb\ndefault = Decay=0.7\n", &errors);
    CHECK (errors == 1);
    parseSubstituteTable ("[p190ca-test]\nlynx = Foil Plate\nnames = Some Verb\ndefault = Some Verb | Nope=0.7\n", &errors);
    CHECK (errors == 1);
    // (round 190cm) a lower-case mix= item is the slot's dry/wet, "Mix" stays the effect's parameter
    t = parseSubstituteTable ("[p190ca-test]\nlynx = Foil Plate\nnames = Send Verb\npreset = Mix=1\nmix = 0.25\ndefault = Send Verb | Decay=0.7; Mix=0.9; mix=0.3\n", &errors);
    CHECK (errors == 0 && t.size() == 1);
    if (t.size() == 1)
    {
        PluginIdentity a;
        a.format = ForeignPlugin::Format::vst3;
        a.name = "Send Verb";
        const auto s = makeSubstituteSlot (t[0], &a);
        CHECK (std::abs (s.mix - 0.3f) < 1e-6f && std::abs (s.params[(std::size_t) p190ca::paramNamed (EffectKind::foilPlate, "Mix")] - 0.9f) < 1e-6f);
    }
    parseSubstituteTable ("[p190ca-test]\nlynx = Foil Plate\nnames = Some Verb\n", &errors); // leaves no default behind
    CHECK (errors == 0);
}

TEST (p190ca_a_substitute_is_not_translated_again)
{
    using namespace lynx;
    // what a saved project holds: the substitute slot with its own parameters. Changing the maps does not reach it.
    auto slot = p190ca::substituted (p190ca::vst3Named ("SSL PlateVerb"));
    CHECK (slot.kind == EffectKind::foilPlate && slot.isSubstitute());
    const auto decay = (std::size_t) p190ca::paramNamed (EffectKind::foilPlate, "Decay");
    slot.params[decay] = 0.123f;
    slot.mix = 0.6f;
    const auto before = slot.params;
    CHECK (! substituteEffectSlot (slot, pluginIdentity (slot.substituteFor.foreign)));
    CHECK (slot.params == before && slot.mix == 0.6f && slot.kind == EffectKind::foilPlate);

    namespace ef = lynx::echosuite::echoform;
    auto echo = p190ca::substituted (p190ca::vst3Named ("ValhallaDelay", "<ValhallaDelay Mode=\"0.0416666679084301\"/>", "64545356614c7679616c68616c6c6164"));
    echo.params[ef::output] = 0.5f; // a project saved before this round
    CHECK (! substituteEffectSlot (echo, pluginIdentity (echo.substituteFor.foreign)));
    CHECK (echo.params[ef::output] == 0.5f);
}
