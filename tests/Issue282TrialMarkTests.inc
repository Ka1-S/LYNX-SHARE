// Round 190qs, issue #282 (FL Studio Trial + LYNX: Slicex and SoundFont Player gone in FL after an export): FL's trial
// mark of a plugin record (0xD4 flag 0x80, core/FlWrapper.h flTrialSaved; REA_rodada190qs.md) travels with the plugin it
// was saved with, LYNX-made records never get it; and every FL generator of FL's render tests goes back to FL as itself
// with FL's own state bytes, in FL's channel record order (the Patcher generator as FL's Patcher, not its main synth),
// and LYNX reads its own export back into the same instruments.

#include "core/FlWrapper.h"
#include "flp/FlpWrite.h"

namespace issue282
{
lynx::Song song()
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.channels.resize (1);
    s.channels[0].name = "Lead";
    s.channels[0].instrument = InstrumentKind::threeosc; // LYNX writes FL's default 3x Osc state
    EffectSlot fx;
    fx.kind = EffectKind::fruityLimiter;
    fx.flOrigin = { "Fruity Limiter", flpexport::fruityLimiterDefaultState(), 0 };
    s.mixer[1].effects.push_back (fx);
    flpexport::stampOrigins (s);
    return s;
}

std::vector<std::uint8_t> exported (const lynx::Song& s, lynx::flpexport::Report& report)
{
    lynx::flpexport::Options options;
    return lynx::flpexport::exportFlp (s, options, report);
}

flp::Project written (const lynx::Song& s, int* notes = nullptr)
{
    lynx::flpexport::Report report;
    auto p = flp::parseProject (exported (s, report));
    if (notes != nullptr)
        *notes = (int) std::count_if (report.notes.begin(), report.notes.end(),
                                      [] (const std::string& n) { return n.find ("saved by a trial of FL Studio") != std::string::npos; });
    return p;
}

lynx::Song back (const flp::Project& p)
{
    lynx::FlpImportOptions o;
    o.fileExists = [] (const std::string&) { return true; };
    return lynx::importFlp (p, o);
}

int channelFlags (const flp::Project& p) { return p.channels.empty() ? -1 : p.channels[0].wrapperFlags; }

int slotFlags (const flp::Project& p)
{
    for (const auto& m : p.mixer)
        if (m.index == 1 && ! m.slots.empty())
            return m.slots[0].wrapperFlags;
    return -1;
}

bool marked (int flags) { return lynx::flTrialSaved (flags); }

std::vector<std::uint8_t> fileOf (const char* name)
{
    std::ifstream in (std::filesystem::path (realFlpDir) / "System" / "Render tests" / (std::string (name) + ".flp"), std::ios::binary);
    return { std::istreambuf_iterator<char> (in), {} };
}

lynx::Song importFile (const std::vector<std::uint8_t>& file)
{
    namespace fs = std::filesystem;
    const auto local = [] (std::string p) {
        std::replace (p.begin(), p.end(), '\\', '/');
        return lynx::existingPathIgnoringCase (p);
    };
    lynx::FlpImportOptions options;
    options.roots.data = fs::path (realFlpDir).string();
    options.roots.factoryData = fs::path (realFlpDir).parent_path().string();
    options.fileExists = [local] (const std::string& p) { return fs::is_regular_file (local (p)); };
    return lynx::importFlp (flp::parseProject (file), options);
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// the event IDs of each generator channel's record, from its 0x40 up to its state (FL 2026 writes 0x40, 0x15, 0xC9,
// 0xD4, [0xCB], …, 0xD5: REA_rodada5 §1.1 and the FL saves of this round)
std::vector<std::vector<std::uint32_t>> generatorRecords (const std::vector<std::uint8_t>& file)
{
    std::vector<std::vector<std::uint32_t>> out;
    flp::EventReader reader (file);
    flp::Event e;
    bool open = false;
    while (reader.next (e))
    {
        if (e.id == 0x40)
        {
            out.push_back ({});
            open = true;
        }
        if (! open || out.empty())
            continue;
        out.back().push_back (e.id);
        if (e.id == 0xD5)
            open = false;
    }
    return out;
}
} // namespace issue282

TEST (issue282_fl_trial_mark_stays_with_its_plugin)
{
    using namespace issue282;
    CHECK (lynx::flTrialSaved (0x1d0) && lynx::flTrialSaved (0x1c0) && ! lynx::flTrialSaved (0x150) && ! lynx::flTrialSaved (-1));

    // LYNX's own records never carry the mark
    auto p = written (song());
    CHECK (! marked (channelFlags (p)) && ! marked (slotFlags (p)) && channelFlags (p) >= 0 && slotFlags (p) >= 0);
    if (marked (channelFlags (p)) || channelFlags (p) < 0 || slotFlags (p) < 0)
        std::fprintf (stderr, "  LYNX's records: %x %x\n", channelFlags (p), slotFlags (p));

    // the same plugins as a trial of FL saves them (the user's Slicex record 0x1d0, an effect 0x1c0)
    p.channels[0].wrapperFlags = 0x1d0;
    for (auto& m : p.mixer)
        if (m.index == 1 && ! m.slots.empty())
            m.slots[0].wrapperFlags = 0x1c0;
    const auto imported = back (flp::parseProject (flp::writeProject (p)));
    CHECK (imported.channels.size() == 1 && imported.mixer.size() > 1 && imported.mixer[1].effects.size() == 1);
    if (imported.channels.size() != 1 || imported.mixer.size() < 2 || imported.mixer[1].effects.size() != 1)
        return;
    CHECK (imported.channels[0].flOrigin.trialSaved && imported.channels[0].flWrapperFlags == 0x1d0);
    CHECK (imported.mixer[1].effects[0].flOrigin.trialSaved);

    // back to FL: FL's records as FL wrote them (the mark kept), and the report says what FL will do with them
    int notes = 0;
    auto again = written (imported, &notes);
    CHECK (channelFlags (again) == 0x1d0 && slotFlags (again) == 0x1c0 && notes == 2);

    // edited in LYNX: still FL's trial plugin (its mark stays, as FL's own resave keeps it)
    auto edited = imported;
    edited.channels[0].volume = 0.3f;
    edited.mixer[1].effects[0].mix = 0.5f;
    again = written (edited, &notes);
    CHECK (marked (channelFlags (again)) && marked (slotFlags (again)) && notes == 2);

    // the channel holds something else now: a Sampler (FL writes the mark on plugin records only), a hosted plugin
    auto sampler = imported;
    sampler.channels[0].instrument = lynx::InstrumentKind::sampler;
    sampler.channels[0].samplePath = "C:/x.wav";
    sampler.channels[0].pluginState.clear();
    again = written (sampler, &notes);
    CHECK (channelFlags (again) == 0x150 && marked (slotFlags (again)) && notes == 1);
    auto hosted = imported;
    hosted.channels[0].instrument = lynx::InstrumentKind::plugin;
    hosted.channels[0].plugin.name = "Drums";
    hosted.channels[0].plugin.foreign.format = lynx::ForeignPlugin::Format::vst2;
    hosted.channels[0].plugin.foreign.vst2Id = 0x44524D31;
    hosted.channels[0].plugin.foreign.name = "Drums";
    again = written (hosted, &notes);
    CHECK (channelFlags (again) == 0x150 && notes == 1);
}

TEST (issue282_fl_generators_go_back_as_themselves)
{
    // FL's render tests (one generator each): import, export, read back. Each generator channel goes out as the same FL
    // generator (internal name), as type 2 with its plugin record, FL's own state bytes and FL's record order; and the
    // export imports again as the same LYNX instrument
    using namespace issue282;
    if (realFlpDir.empty())
        return;
    const char* names[] = { "3OSC", "Autogun", "Bassdrum", "Beepmap", "Boobass", "Directwave", "Drumaxx", "Drumpad", "Drumsynth Live",
                            "FL Keys", "FLEX", "FPC", "Fruit Kick", "Fruity DX10", "Fruity Granulizer", "Fruity Slicer",
                            "Fruity Voltage Controller", "GMS", "Harmless", "Harmor", "MiniSynth", "Ogun", "Patcher (generator)",
                            "PoiZone", "Sakura", "Sawer", "SimSynth", "Slicex", "Sytrus", "Wave Traveller" };
    int checked = 0;
    for (const auto* name : names)
    {
        const auto file = fileOf (name);
        if (file.empty())
            continue;
        const auto original = flp::parseProject (file);
        const auto song = importFile (file);
        lynx::flpexport::Report report;
        const auto bytes = exported (song, report);
        const auto out = flp::parseProject (bytes);
        int generators = 0;
        for (const auto& c : original.channels)
        {
            if (c.type != flp::ChannelType::instrument || c.pluginName.empty())
                continue;
            ++generators;
            const auto match = std::find_if (out.channels.begin(), out.channels.end(), [&] (const flp::Channel& o) { return o.name == c.name; });
            const auto found = match != out.channels.end();
            CHECK (found);
            if (! found)
            {
                std::fprintf (stderr, "  %s: channel '%s' missing\n", name, c.name.c_str());
                continue;
            }
            const auto same = match->type == flp::ChannelType::instrument && lower (match->pluginName) == lower (c.pluginName)
                              && match->pluginState == c.pluginState && match->wrapperFlags == c.wrapperFlags;
            CHECK (same);
            if (! same)
                std::fprintf (stderr, "  %s: '%s' went out as '%s' (state %zu / %zu bytes, flags %x / %x)\n", name, c.pluginName.c_str(),
                              match->pluginName.c_str(), match->pluginState.size(), c.pluginState.size(), match->wrapperFlags, c.wrapperFlags);
        }
        CHECK (generators == 1);
        // FL's record order of the exported generator channels
        for (const auto& record : generatorRecords (bytes))
        {
            if (record.size() < 4 || record[2] != 0xC9 || std::find (record.begin(), record.end(), 0xD5) == record.end())
                continue;
            CHECK (record[0] == 0x40 && record[1] == 0x15 && record[3] == 0xD4);
        }
        // and LYNX reads its own export as it read FL's file: the same instruments, from the same FL generators
        const auto again = importFile (bytes);
        CHECK (again.channels.size() == song.channels.size());
        for (std::size_t i = 0; i < std::min (again.channels.size(), song.channels.size()); ++i)
        {
            const auto same = again.channels[i].instrument == song.channels[i].instrument
                              && lower (again.channels[i].flOrigin.plugin) == lower (song.channels[i].flOrigin.plugin);
            CHECK (same);
            if (! same)
                std::fprintf (stderr, "  %s: channel %zu re-import: kind %d / %d, from '%s' / '%s'\n", name, i, (int) again.channels[i].instrument,
                              (int) song.channels[i].instrument, again.channels[i].flOrigin.plugin.c_str(), song.channels[i].flOrigin.plugin.c_str());
        }
        ++checked;
    }
    CHECK (checked == 30);

    // the reporter's case: FL's Slicex as a trial of FL saves it (record 0x1d0) — the export keeps the record, the state
    // and the mark, so FL treats it as its own trial save
    auto slicex = flp::parseProject (fileOf ("Slicex"));
    for (auto& c : slicex.channels)
        if (c.pluginName == "Slicex")
            c.wrapperFlags = 0x1d0;
    lynx::flpexport::Report report;
    const auto out = flp::parseProject (exported (importFile (flp::writeProject (slicex)), report));
    const auto it = std::find_if (out.channels.begin(), out.channels.end(), [] (const flp::Channel& c) { return c.pluginName == "Slicex"; });
    CHECK (it != out.channels.end() && it->wrapperFlags == 0x1d0 && it->pluginState.size() > 1000000);
    CHECK (std::count_if (report.notes.begin(), report.notes.end(), [] (const std::string& n) { return n.find ("Slicex was saved by a trial") != std::string::npos; }) == 1);
}
