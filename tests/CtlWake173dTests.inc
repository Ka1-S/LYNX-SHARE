// Round 173d2 (issue #77, generators): "no built-in generator has a dead control". Every editor control of every
// FL-native generator (tools/CtlWake.h) is moved to the far end of its range on the engine's new channel and must
// change the render, plainly or in the context the last survey found it needs (CtlWake173dKnown.inc,
// scripts/ctlwake-known.py: a legato note, the pitch wheel, a velocity, a section switched on, a sample loaded…).
// allowed() lists the controls that cannot change a render here, each with its reason (FL's own inert ones, with the
// evidence; CPU-only options); knownGaps() lists the FL-parity gaps (dead controls) with a tracker id. A control that
// stops moving in its context fails, as does a new control that never moves. Skipped without FL's Data folder.
// The full survey (every context searched): tools/ctlwake.cpp, /mnt/e/lynx-work/audit173d2.

#include "../tools/CtlWake.h"

namespace ctlwake173d
{
struct Known
{
    const char* engine;
    const char* control;
    const char* key;
};

const std::vector<Known>& known()
{
    static const std::vector<Known> k {
#include "CtlWake173dKnown.inc"
    };
    return k;
}

struct Allowed
{
    const char* engine;
    const char* control; // '*' = any run of characters
    const char* reason;
};

// Legitimately silent here. fl-inert: FL's own engine ignores the control in this situation too (the evidence named);
// non-audio: the control changes how the engine runs, not what it renders.
const std::vector<Allowed>& allowed()
{
    static const std::vector<Allowed> a {
        // FL's native generators take no MIDI input (REA_rodada164e: MIDIIn abstract or gated, [EXEC] CC 1 and pressure
        // never reach the engine), so the sources only MIDI would feed stay 0 in FL; the pitch wheel reaches them as the
        // channel's pitch (round 173d wheel slice), which a plugin's own bend range does not scale (FL render 2026-11-05,
        // allpitch2.flp with Sawer / PoiZone / GMS at their max bend range: bit-identical)
        { "GMS", "Pitch bend range", "fl-inert: scales the engine's MIDI pitch bend, which FL never sends a generator (REA_rodada164e; FL render at max range bit-identical)" },
        { "Sawer", "Bend range", "fl-inert: scales Sawer's MIDI pitch bend, which FL never sends a generator (REA_rodada164e; FL render at max range bit-identical)" },
        { "Morphine", "Bend range", "fl-inert: scales the engine's MIDI pitch bend, which FL never sends a generator (REA_rodada164e)" },
        { "Toxic Biohazard", "Bend range", "fl-inert: scales the engine's MIDI pitch bend, which FL never sends a generator (REA_rodada164e)" },
        { "MiniSynth", "Mod wheel *", "fl-inert: the mod wheel source is fed by CC 1 only, gated off in FL's generator build ([EXEC] REA_rodada164e: stays 0)" },
        // the effect slots of GMS: FL's reverb has no LFO, the delay uses only its LFO rate, the trancer its own gate
        // (REA_rodada170i3c, FUN_180100950)
        { "GMS", "Reverb LFO *", "fl-inert: FL's GMS reverb slot has no LFO (REA_rodada170i3c, FUN_180100950)" },
        { "GMS", "Delay LFO shape", "fl-inert: FL's GMS delay slot uses only its LFO rate (REA_rodada170i3c)" },
        { "GMS", "Delay LFO amount", "fl-inert: FL's GMS delay slot uses only its LFO rate (REA_rodada170i3c)" },
        { "GMS", "Trancer LFO shape", "fl-inert: FL's GMS trancer has its own gate shape (REA_rodada170i3c)" },
        { "GMS", "Trancer LFO amount", "fl-inert: FL's GMS trancer has its own gate shape (REA_rodada170i3c)" },
        // Sytrus glides through each operator's pitch articulator, only where that envelope is on, its range is not 0 and it
        // is not GLOBAL (REA_rodada31 §6.4, EXEC t_porta.py -131 dB); FL's Default patch has no pitch envelope on
        { "Sytrus", "Porta", "fl-inert on FL's Default patch: the glide is the start offset of an operator's pitch envelope, and none is on (REA_rodada31 §6.4)" },
        // the RM layer's filter rows (round 172a's matrix page): FL's voice assembly reads ring modulation for the operator
        // rows only, a filter's input comes from the FM layer (REA_rodada11 §1.5, FUN_0086dd40 [DECOMP + EXEC])
        { "Sytrus", "RM op * to filter *", "fl-inert: FL's voice assembly reads the RM layer for operators only; a filter's input is the FM layer's row (REA_rodada11 §1.5)" },
        { "Harmless", "Threaded", "non-audio: renders the voices on worker threads (same output)" },
    };
    // FL's Drumaxx engine reads pad 1's Noise Retrig and EG Retrig (pad fields 5, 6) for every pad (REA_rodada67 §6.2),
    // so pads 2…16's own switches do nothing in FL either
    static const std::vector<Allowed> all = [] {
        auto v = a;
        static std::vector<std::string> names;
        for (int pad = 2; pad <= 16; ++pad)
            for (const auto* what : { " Mallet retrig", " Mid retrig" })
                names.push_back ("Pad " + std::to_string (pad) + what);
        for (const auto& n : names)
            v.push_back ({ "Drumaxx", n.c_str(), "fl-inert: FL's Drumaxx reads pad 1's Noise / EG Retrig for every pad (REA_rodada67 §6.2)" });
        return v;
    }();
    return all;
}

struct Gap
{
    const char* engine;
    const char* control;
    const char* tracker;
};

// FL-parity gaps: the control is dead in LYNX but acts in FL (briefs in /mnt/e/lynx-work/audit173d/REPORT.md)
const std::vector<Gap>& knownGaps()
{
    static const std::vector<Gap> g {
        // Harmless's "release velocity to release" scales the release by FL's answer to voice event 5, which LYNX
        // answers 1 (the reference's; FL's not observed, REA_rodada28)
        { "Harmless", "Release velocity to release", "gap173d2-2" },
        // Kepler Exo's eight modulation slots take a source and a destination picked in FL's editor (the state's matrix
        // list, REA_rodada167d §4.3); the Default preset routes none and LYNX's editor can't pick them, so the slots'
        // amounts and the X / Y pad offsets (sources only through a slot) act on nothing
        { "Kepler Exo", "Mod * amount", "gap173d2-3" },
        { "Kepler Exo", "X offset", "gap173d2-3" },
        { "Kepler Exo", "Y offset", "gap173d2-3" },
    };
    return g;
}

bool glob (const char* pattern, const char* text)
{
    if (*pattern == 0)
        return *text == 0;
    if (*pattern == '*')
        return glob (pattern + 1, text) || (*text != 0 && glob (pattern, text + 1));
    return *pattern == *text && glob (pattern + 1, text + 1);
}

bool isAllowed (const std::string& engine, const std::string& control)
{
    for (const auto& a : allowed())
        if (engine == a.engine && glob (a.control, control.c_str()))
            return true;
    for (const auto& g : knownGaps())
        if (engine == g.engine && glob (g.control, control.c_str()))
            return true;
    return false;
}

void auditEngines (const std::set<std::string>& names, bool except)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    static const auto fl = natives142::flAccess();
    ctlaudit::flAccess = &fl;
    const ctlaudit::Samples samples = [] (const std::string& p) {
        auto q = p;
        std::replace (q.begin(), q.end(), '\\', '/');
        return tools::readWav (existingPathIgnoringCase (q));
    };
    for (const auto& e : natives::instrumentEngines())
    {
        if ((names.count (e.name) != 0) == except)
            continue;
        auto c = natives::makeChannel (e.kind, fl);
        if (c && e.kind == InstrumentKind::granulizer && c->samplePath.empty())
            c->samplePath = (std::filesystem::path (realFlpDir) / "Patches" / "Packs" / "Loops" / "DL Pendulus.wav").string();
        if (! c)
            continue;
        const auto controls = ctlaudit::editorControls (*c);
        std::map<std::string, std::string> keys;
        for (const auto& k : known())
            if (e.name == std::string (k.engine))
                keys[k.control] = k.key;
        std::vector<int> open;
        for (int i = 0; i < (int) controls.size(); ++i)
            if (! isAllowed (e.name, controls[(std::size_t) i].name))
                open.push_back (i);
        if (open.empty())
            continue;
        const auto verdicts = ctlwake::wake (*c, controls, samples, keys, false, open);
        for (const auto& v : verdicts)
        {
            const auto& k = controls[(std::size_t) v.control];
            const bool dead = ! (v.best > ctlwake::wakeDb);
            if (dead)
            {
                const auto it = keys.find (k.name);
                std::printf ("  %s: %d \"%s\" changes nothing%s%s (best %.0f dB)\n", e.name, k.index, k.name.c_str(),
                             it != keys.end() ? " in its context " : "", it != keys.end() ? it->second.c_str() : "", v.best);
            }
            CHECK (! dead);
        }
    }
}

// the slow engines in tests of their own (the build runs the chunks in parallel)
const std::set<std::string>& slow()
{
    static const std::set<std::string> s { "GMS", "Sawer", "Toxic Biohazard", "Harmless", "Kepler Exo", "Transistor Bass", "Sytrus", "Drumaxx" };
    return s;
}
} // namespace ctlwake173d

TEST (ctlwake173d_generator_controls_rest) { ctlwake173d::auditEngines (ctlwake173d::slow(), true); }
TEST (ctlwake173d_generator_controls_gms) { ctlwake173d::auditEngines ({ "GMS" }, false); }
TEST (ctlwake173d_generator_controls_sawer) { ctlwake173d::auditEngines ({ "Sawer" }, false); }
TEST (ctlwake173d_generator_controls_toxic) { ctlwake173d::auditEngines ({ "Toxic Biohazard" }, false); }
TEST (ctlwake173d_generator_controls_harmless_sytrus) { ctlwake173d::auditEngines ({ "Harmless", "Sytrus" }, false); }
TEST (ctlwake173d_generator_controls_kepler_exo_tb) { ctlwake173d::auditEngines ({ "Kepler Exo", "Transistor Bass" }, false); }
TEST (ctlwake173d_generator_controls_drumaxx) { ctlwake173d::auditEngines ({ "Drumaxx" }, false); }

// every editor control reads and writes on the engine's new channel: a state saved before a parameter existed (Slayer's
// version 0, Kepler's 34 floats, Kepler Exo's 151) silently dropped its edits
TEST (ctlwake173d_every_generator_control_reads_and_writes_on_a_new_channel)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    static const auto fl = natives142::flAccess();
    for (const auto& e : natives::instrumentEngines())
    {
        auto c = natives::makeChannel (e.kind, fl);
        if (! c)
            continue;
        for (const auto& k : ctlaudit::editorControls (*c))
        {
            auto d = *c;
            const auto v = k.get (*c);
            const bool ok = v && (k.set (d, k.low) || k.set (d, k.high));
            if (! ok)
                std::printf ("  %s: \"%s\" %s on a new channel\n", e.name, k.name.c_str(), v ? "can't be written" : "can't be read");
            CHECK (ok);
        }
    }
}

// every allowed / known-gap entry still names a control of its engine (a renamed control would hide a dead one)
TEST (ctlwake173d_allowed_entries_name_real_controls)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    static const auto fl = natives142::flAccess();
    auto names = [&] (const char* engine) {
        std::vector<std::string> out;
        for (const auto& e : natives::instrumentEngines())
            if (std::string (e.name) == engine)
                if (const auto c = natives::makeChannel (e.kind, fl))
                    for (const auto& k : ctlaudit::editorControls (*c))
                        out.push_back (k.name);
        return out;
    };
    auto check = [&] (const char* engine, const char* control) {
        bool found = false;
        for (const auto& n : names (engine))
            found = found || ctlwake173d::glob (control, n.c_str());
        if (! found)
            std::printf ("  allowed / gap entry %s \"%s\" matches no control\n", engine, control);
        CHECK (found);
    };
    for (const auto& a : ctlwake173d::allowed())
        check (a.engine, a.control);
    for (const auto& g : ctlwake173d::knownGaps())
        check (g.engine, g.control);
}

// FL Slayer's mod wheel (parameter 28, sent as CC 1) on an installation preset's version-0 state (28 ints): the
// editor's edit upgrades the state to FL 2026's version 1 (29 ints, REA_rodada68 §6.1) instead of being lost
TEST (ctlwake173d_slayer_mod_wheel_on_a_version_0_state)
{
    using namespace lynx;
    ChannelDef c;
    c.instrument = InstrumentKind::slayer;
    c.pluginState.assign (4 + 4 * 28, 0);
    for (int i = 0; i < 28; ++i)
    {
        const std::int32_t v = i == 0 ? 2 : 100;
        std::memcpy (c.pluginState.data() + 4 + 4 * i, &v, 4);
    }
    CHECK (drumcontrols::rawValue (c, 28) == 0.0f);
    CHECK (drumcontrols::setRawValue (c, 28, 127.0f));
    CHECK (c.pluginState.size() == 4 + 4 * 29);
    std::int32_t version = -1, wheel = -1, first = -1;
    std::memcpy (&version, c.pluginState.data(), 4);
    std::memcpy (&wheel, c.pluginState.data() + 4 + 4 * 28, 4);
    std::memcpy (&first, c.pluginState.data() + 4, 4);
    CHECK (version == 1 && wheel == 127 && first == 2);
    CHECK (drumcontrols::rawValue (c, 28) == 127.0f);
}

// Kepler's LFO retrig panels (34 "Retrig Phase Active", 35 "Phase") on an installation preset (version 2, 34 floats):
// an edit grows the state to 36 floats (as FL's later states, REA_rodada46 §1.2) instead of being lost, and plays
TEST (ctlwake173d_kepler_lfo_retrig_on_a_34_float_state)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    static const auto fl = natives142::flAccess();
    auto c = natives::makeChannel (InstrumentKind::kepler, fl);
    if (! c)
        return;
    const auto* retrig = modelparams::findByName (InstrumentKind::kepler, "LFO retrig");
    const auto* phase = modelparams::findByName (InstrumentKind::kepler, "LFO phase");
    CHECK (retrig != nullptr && phase != nullptr);
    if (retrig == nullptr || phase == nullptr)
        return;
    const auto before = c->pluginState.size();
    CHECK (modelparams::rawValue (*c, retrig->index).has_value());
    CHECK (modelparams::setRawValue (*c, retrig->index, 1.0f));
    CHECK (modelparams::setRawValue (*c, phase->index, 0.5f));
    CHECK (c->pluginState.size() == before + 8 || before >= 0x21 + 4 * 36);
    CHECK (modelparams::rawValue (*c, retrig->index) == 1.0f && modelparams::rawValue (*c, phase->index) == 0.5f);
    CHECK (modelparams::rawValue (*c, 33).has_value()); // (the panels before it are where they were)
}

// Kepler Exo: FL's Default preset stores 151 values (0…0x96), so VCA smooth, Saturation HQ, Master detune and HPF on
// (0x97…0x9a) had nowhere to go; an edit grows the state to 0x9b values, the stream and wrapper sizes with it
TEST (ctlwake173d_kepler_exo_last_parameters_on_a_151_float_state)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    static const auto fl = natives142::flAccess();
    auto c = natives::makeChannel (InstrumentKind::keplerExo, fl);
    if (! c)
        return;
    const auto* detune = modelparams::findByName (InstrumentKind::keplerExo, "Master detune");
    const auto* hpf = modelparams::findByName (InstrumentKind::keplerExo, "HPF on");
    CHECK (detune != nullptr && hpf != nullptr);
    if (detune == nullptr || hpf == nullptr)
        return;
    const auto cutoff = modelparams::rawValue (*c, 0x25);
    CHECK (modelparams::rawValue (*c, detune->index) == 0.5f && modelparams::rawValue (*c, hpf->index) == 1.0f); // (the defaults)
    CHECK (modelparams::setRawValue (*c, detune->index, 1.0f));
    CHECK (modelparams::setRawValue (*c, hpf->index, 0.0f));
    CHECK (modelparams::rawValue (*c, detune->index) == 1.0f && modelparams::rawValue (*c, hpf->index) == 0.0f);
    CHECK (modelparams::rawValue (*c, 0x25) == cutoff); // (the values before them stay where they were)
    CHECK (keplerExoState (c->pluginState)); // (still a valid "Kepler-2" state, its matrix slots after the values)
    const auto patch = c->pluginState;
    auto again = *c;
    CHECK (modelparams::setRawValue (again, detune->index, 0.25f) && again.pluginState.size() == patch.size()); // (grown once)
}
