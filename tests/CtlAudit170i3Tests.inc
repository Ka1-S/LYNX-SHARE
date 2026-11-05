// Round 170i3: "every control changes the render" for every FL-native generator (tools/CtlAudit.h; the full survey is
// scripts/ctlaudit.sh, docs/analise/REA_rodada170i3c.md). Each engine's new channel (FL's Default preset) plays a short
// chord pattern; a deterministic sample of its editor controls is flipped to the far end of its range and must change
// the render, directly or in one of a few random settings of the other controls (a section switched on). The controls
// that can't move with this pattern are listed per engine with the reason. Skipped without FL's Data folder.

#include "../tools/CtlAudit.h"
#include "../tools/WavReader.h"

namespace ctl170i3
{
struct Allowed
{
    const char* name;   // a part of the control's name
    const char* reason;
};

struct Case
{
    const char* engine;                      // natives::InstrumentEngine::name
    std::vector<std::pair<int, float>> sets; // controls set before the audit (a section on)
    std::vector<Allowed> allowed;            // the controls that don't move this pattern
    std::vector<const char*> noAutomation;   // controls whose automation is not applied while notes play (as in FL)
};

const std::vector<Case>& cases()
{
    static const std::vector<Case> all = {
        { "3x Osc", {}, {}, {} },
        { "Sytrus",
          {},
          { { "Porta", "needs legato notes" },
            { "Export oversampling", "export render only" },
            { "Export env accuracy", "export render only" } },
          {} },
        { "Harmor", {}, { { "A/B mix", "the Default preset's part B is off" } }, {} },
        { "Harmless",
          { { 92, 1.0f }, { 93, 1000.0f }, { lynx::harmlessComboIndex + 3, 1.0f } }, // delay and compressor on
          { { "Release velocity", "needs a note-off velocity" }, { "Threaded", "CPU use only" } },
          {} },
        { "GMS",
          {},
          { { "phase retrigger", "needs a retriggered oscillator 2 phase with notes in flight" },
            { "Reverb LFO", "the reverb slot has no LFO (FUN_180100950)" },
            { "Delay LFO shape", "the delay slot uses only its LFO rate (its time)" },
            { "Delay LFO amount", "the delay slot uses only its LFO rate (its time)" },
            { "Trancer LFO shape", "the trancer has its own gate shape" },
            { "Trancer LFO amount", "the trancer has its own gate shape" },
            { "Trancer LFO sync", "rate ×4 of a gate already at a step" },
            { "Pitch bend range", "needs pitch bend" } },
          {} },
        { "Morphine",
          {},
          { { "Bend range", "needs pitch bend" }, { "Noise", "the Default preset has no noise sample" } },
          {} },
        { "Ogun",
          {},
          { { "Delay", "the editor has no Delay on switch yet (round 170i3 report)" } },
          { "Richness", "Quality" } },
        { "Toxic Biohazard",
          {},
          { { "Seq ", "the sequencer's steps play only with the sequencer on" },
            { "Mod ", "the modulation matrix needs its sources (wheel, aftertouch…)" },
            { "flanger", "an inactive effect type" }, { "chorus", "an inactive effect type" }, { "reverb", "an inactive effect type" },
            { "delay", "an inactive effect type" }, { "lo-fi", "an inactive effect type" }, { "phaser", "an inactive effect type" },
            { "LFO 1 free", "a free LFO differs only after a pause with no voice" },
            { "LFO 2 free", "a free LFO differs only after a pause with no voice" },
            { "Bend range", "needs pitch bend" } },
          { "wave", "type", "delay sync" } },
        { "PoiZone",
          {},
          { { "Mod wheel", "needs the mod wheel" }, { "Gate step", "the gate plays only with the gate on" },
            { "Gate speed", "the gate plays only with the gate on" }, { "Bend range", "needs pitch bend" } },
          {} },
        { "Sakura", {}, {}, {} },
        { "Kepler", {}, { { "Arp range", "the arpeggiator is off" } }, {} },
        { "Kepler Exo",
          {},
          { { "Retrigger", "needs legato notes" }, { "Release mode", "needs a mono patch's overlapping keys" },
            { "amount", "the modulation matrix needs its sources" }, { "offset", "the X/Y pad's offset needs its modulation" } },
          {} },
        { "BooBass", {}, {}, {} },
        { "SimSynth", {}, {}, {} },
        { "MiniSynth",
          {},
          { { "Mod wheel", "needs the mod wheel" } },
          {} },
        { "Fruity DX10", {}, {}, {} },
        { "Plucked!", {}, {}, {} },
        { "Sawer",
          {},
          { { "Glide key mode", "needs legato notes" }, { "Bend range", "needs pitch bend" }, { "Mod ", "the modulation matrix needs its sources" } },
          {} },
        { "BeepMap", {}, { { "Loop", "the picture is longer than the pattern" } }, {} },
        { "FL Keys", {}, {}, {} },
        { "FL Slayer", {}, { { "Glissando", "needs legato notes" } }, {} },
        { "Fruity Granulizer",
          {},
          { { "Transient hold", "the loop's transients" }, { "Tempo locked", "the pattern's tempo is the loop's" } },
          { "Transients source" } },
        { "Drumaxx",
          {},
          { { "retrig", "needs a pad hit again while it sounds" }, { "Pad 11", "not in the Default Kit's pattern" },
            { "Pad 12", "not in the Default Kit's pattern" }, { "Pad 13", "not in the Default Kit's pattern" },
            { "Pad 14", "not in the Default Kit's pattern" }, { "Pad 15", "not in the Default Kit's pattern" },
            { "Pad 16", "not in the Default Kit's pattern" }, { "Limiter release", "the kit stays under the limiter" } },
          {} },
        { "Drumpad", {}, {}, {} },
        { "Fruit Kick", {}, {}, {} },
        { "BassDrum", {}, {}, {} },
    };
    return all;
}

bool listed (const std::string& name, const std::vector<Allowed>& allowed)
{
    for (const auto& a : allowed)
        if (name.find (a.name) != std::string::npos)
            return true;
    return false;
}

bool listed (const std::string& name, const std::vector<const char*>& names)
{
    for (const auto* a : names)
        if (name.find (a) != std::string::npos)
            return true;
    return false;
}

struct Setup
{
    lynx::ChannelDef channel;
    std::vector<lynx::ctlaudit::Control> all;      // every editor control (the random settings change them all)
    std::vector<lynx::ctlaudit::Control> controls; // the sample (the allowed ones out)
    std::vector<int> at;                           // the sample's places in `all`
    lynx::ctlaudit::Pattern pattern;
};

lynx::ctlaudit::Samples samples()
{
    return [] (const std::string& p) {
        auto q = p;
        std::replace (q.begin(), q.end(), '\\', '/');
        return lynx::tools::readWav (lynx::existingPathIgnoringCase (q));
    };
}

// the engine's new channel with the case's settings and a sample of at most `most` of its editor controls
std::optional<Setup> setup (const Case& k, std::size_t most)
{
    using namespace lynx;
    static const auto fl = natives142::flAccess();
    ctlaudit::flAccess = &fl;
    const natives::InstrumentEngine* engine = nullptr;
    for (const auto& e : natives::instrumentEngines())
        if (std::string (e.name) == k.engine)
            engine = &e;
    CHECK (engine != nullptr);
    if (engine == nullptr)
        return std::nullopt;
    auto c = natives::makeChannel (engine->kind, fl);
    if (! c)
        return std::nullopt;
    if (engine->kind == InstrumentKind::granulizer && c->samplePath.empty())
        c->samplePath = (std::filesystem::path (realFlpDir) / "Patches" / "Packs" / "Loops" / "DL Pendulus.wav").string();
    Setup s;
    s.channel = *c;
    s.pattern = ctlaudit::patternFor (engine->kind);
    s.all = ctlaudit::editorControls (s.channel);
    for (const auto& [index, raw] : k.sets)
        for (const auto& ctl : s.all)
            if (ctl.index == index)
                CHECK (ctl.set (s.channel, raw));
    std::vector<int> open;
    for (std::size_t i = 0; i < s.all.size(); ++i)
        if (! listed (s.all[i].name, k.allowed))
            open.push_back ((int) i);
    const auto step = std::max<std::size_t> (1, (open.size() + most - 1) / most);
    for (std::size_t i = 0; i < open.size(); i += step)
        s.controls.push_back (s.all[(std::size_t) open[i]]), s.at.push_back (open[i]);
    return s;
}
} // namespace ctl170i3

// each engine's sampled controls change the render through a new instrument (directly or with other settings)
TEST (ctl170i3_every_control_changes_the_render)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto smp = ctl170i3::samples();
    for (const auto& k : ctl170i3::cases())
    {
        const auto s = ctl170i3::setup (k, 24);
        if (! s)
            continue;
        const auto rep = ctlaudit::audit (s->channel, s->controls, s->pattern, smp, false, false);
        CHECK (rep.basePeak > 1e-3);
        std::vector<int> silent; // (places in s->all)
        for (std::size_t i = 0; i < s->controls.size(); ++i)
            for (const auto& r : rep.results)
                if (r.index == s->controls[i].index && r.name == s->controls[i].name && ! ctlaudit::moved (r.state, rep.noise))
                    silent.push_back (s->at[i]);
        const auto left = silent.empty() ? silent : ctlaudit::stillDead (s->channel, s->all, silent, s->pattern, smp);
        for (const auto i : left)
            std::printf ("  %s: %d %s never changes the render\n", k.engine, s->all[(std::size_t) i].index, s->all[(std::size_t) i].name.c_str());
        CHECK (left.empty());
    }
}

// the editor's path (a knob edit reaching the running instrument) and automation move the sound too, and an edit
// keeps the sounding notes (the instrument is configured, not replaced; its engine keeps its voices)
TEST (ctl170i3_editor_edits_and_automation_reach_the_playing_instrument)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto smp = ctl170i3::samples();
    for (const auto& k : ctl170i3::cases())
    {
        auto s = ctl170i3::setup (k, 6);
        if (! s)
            continue;
        // a control that moved through a new instrument, not a level (an edit to 0 would silence the notes)
        const auto rep = ctlaudit::audit (s->channel, s->controls, s->pattern, smp, false, false);
        std::vector<ctlaudit::Control> pick;
        for (const auto& ctl : s->controls)
            for (const auto& r : rep.results)
                if (r.index == ctl.index && r.name == ctl.name && ctlaudit::moved (r.state, rep.noise))
                    pick.push_back (ctl);
        if (pick.size() > 3)
            pick.resize (3);
        CHECK (! pick.empty());
        const auto paths = ctlaudit::audit (s->channel, pick, s->pattern, smp);
        for (const auto& r : paths.results)
        {
            const bool live = ctlaudit::moved (r.live, paths.noise);
            const bool automation = ! r.automatable || ctl170i3::listed (r.name, k.noAutomation) || ctlaudit::moved (r.automation, paths.noise);
            if (! live || ! automation || r.rebuilt)
                std::printf ("  %s: %d %s live %.0f dB, automation %.0f dB%s\n", k.engine, r.index, r.name.c_str(), r.live, r.automation,
                             r.rebuilt ? ", rebuilt" : "");
            CHECK (live);
            CHECK (automation);
            CHECK (! r.rebuilt);
        }
    }
}

// a knob turned while notes play keeps them sounding (before round 170i3 Morphine's knobs restarted its synth and cut
// them). Sytrus and Harmor still build a new engine for any edit of their state (listed in REA_rodada170i3c §3).
TEST (ctl170i3_a_knob_edit_keeps_the_sounding_notes)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto smp = ctl170i3::samples();
    for (const auto& k : ctl170i3::cases())
    {
        const std::string name = k.engine;
        if (name == "Sytrus" || name == "Harmor")
            continue;
        auto s = ctl170i3::setup (k, 12);
        if (! s)
            continue;
        const auto& p = s->pattern;
        // the notes' energy between the edit and the second chord
        const auto energy = [&p] (const std::vector<float>& out) {
            double e = 0.0;
            for (auto i = (std::size_t) (p.editBlock + 1) * 1024; i < (std::size_t) p.secondBlock * 1024 && i < out.size(); ++i)
                e += (double) out[i] * (double) out[i];
            return e;
        };
        const auto base = ctlaudit::render (s->channel, p, smp);
        const auto before = energy (base);
        if (before < 1e-6)
            continue; // (a drum hit already over)
        int tried = 0;
        for (const auto& ctl : s->controls)
        {
            std::string lower = ctl.name;
            std::transform (lower.begin(), lower.end(), lower.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
            bool level = false;
            for (const auto* word : { "vol", "level", "gain", "mix", "master", "mute", "amp", "output", "height", "polyphony", "voices" })
                level = level || lower.find (word) != std::string::npos;
            level = level || lower == "on" || (lower.size() > 3 && lower.compare (lower.size() - 3, 3, " on") == 0); // (a switch)
            const auto v = ctl.get (s->channel);
            if (level || ! v || tried == 2)
                continue;
            auto changed = s->channel;
            if (! ctl.set (changed, ctlaudit::flipped (ctl, *v)))
                continue;
            ++tried;
            const auto after = energy (ctlaudit::render (s->channel, p, smp, [&changed] (ctlaudit::Player& pl) { pl.rebuild (changed); }));
            if (! (after > before * 1e-3))
                std::printf ("  %s: editing %s cut the notes (%.3g of their energy left)\n", k.engine, ctl.name.c_str(), after / before);
            CHECK (after > before * 1e-3);
        }
    }
}
