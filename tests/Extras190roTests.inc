// Round 190ro (LYNX extras review 2, EXTRAS-2.md in the round's folder): the defaults it changed and the checks behind them.
//   ON-2  project.clipLaunchStrips on: a new Clip-launch project links Track N to insert N (Live's new set). Its export
//         to FL Studio holds the same channels, routing, patterns and clips as the same project unlinked (the rendered
//         WAVs of both, and of their .flp / .als exports, are compared by the round's gate script).
//   OFF-2 plugins.oneRow: the row of a plugin installed as CLAP, VST3 and VST picks a build an .als export keeps (Live
//         hosts no CLAP). Round 190rk changes the order to VST3 > CLAP > VST2; until that lands the check names the gap.

#include "core/AlsExport.h"
#include "core/FlpExport.h"
#include "core/PluginPicks.h"
#include "core/Templates.h"

namespace extras190ro
{
inline lynx::FlpImportOptions importOptions()
{
    lynx::FlpImportOptions o;
    o.fileExists = [] (const std::string&) { return true; };
    return o;
}

// the same content in a new project: a 4-bar pattern for the first channel on track 1, one for the second on track 2
inline void addContent (lynx::Song& song)
{
    using namespace lynx;
    const auto bar = (std::int64_t) 4 * ppq;
    for (int c = 0; c < 2 && c < (int) song.channels.size(); ++c)
    {
        Pattern p;
        p.name = "Part " + std::to_string (c + 1);
        for (int k = 0; k < 8; ++k)
        {
            Note n;
            n.start = k * bar / 2;
            n.length = bar / 4;
            n.channel = c;
            n.key = 60 + 2 * k;
            p.notes.push_back (n);
        }
        song.patterns.push_back (p);
        Clip clip;
        clip.kind = ClipKind::pattern;
        clip.pattern = (int) song.patterns.size() - 1;
        clip.track = c;
        clip.start = c * bar;
        clip.length = 4 * bar;
        song.playlist.push_back (clip);
    }
}
} // namespace extras190ro

TEST (extras190ro_cliplaunch_strips_on_and_the_flp_export_unchanged)
{
    using namespace lynx;
    // on by default since round 190ro (Live's new set: a strip on every track), still a switch
    CHECK (NewProjectDefaults {}.clipLaunchStrips && extras::defaultOn ("project.clipLaunchStrips") && ! extras::flExactOn ("project.clipLaunchStrips"));
    CHECK (NewProjectDefaults {}.toText().find ("newClipLaunchStrips=1") != std::string::npos);

    auto plain = makeNewProject();
    auto linked = plain;
    CHECK (linkClipLaunchStrips (linked, 16) > 0 && linked.linkTracksToMixer);
    extras190ro::addContent (plain);
    extras190ro::addContent (linked);
    // nothing is routed differently in the project itself
    CHECK (plain.channels.size() == linked.channels.size() && plain.mixer.size() == linked.mixer.size());
    for (std::size_t c = 0; c < plain.channels.size(); ++c)
        CHECK (plain.channels[c].mixerTrack == linked.channels[c].mixerTrack);

    // the .flp export of both and the import of that: the same channels, routing, patterns and clips
    flpexport::Options options;
    flpexport::Report r1, r2;
    const auto a = importFlp (flp::parseProject (flpexport::exportFlp (plain, options, r1)), extras190ro::importOptions());
    const auto b = importFlp (flp::parseProject (flpexport::exportFlp (linked, options, r2)), extras190ro::importOptions());
    CHECK (a.channels.size() == b.channels.size());
    for (std::size_t c = 0; c < std::min (a.channels.size(), b.channels.size()); ++c)
        CHECK (a.channels[c].mixerTrack == b.channels[c].mixerTrack && a.channels[c].name == b.channels[c].name
               && a.channels[c].instrument == b.channels[c].instrument);
    CHECK (a.patterns.size() == b.patterns.size());
    for (std::size_t p = 0; p < std::min (a.patterns.size(), b.patterns.size()); ++p)
    {
        CHECK (a.patterns[p].notes.size() == b.patterns[p].notes.size());
        for (std::size_t n = 0; n < std::min (a.patterns[p].notes.size(), b.patterns[p].notes.size()); ++n)
            CHECK (a.patterns[p].notes[n].start == b.patterns[p].notes[n].start && a.patterns[p].notes[n].key == b.patterns[p].notes[n].key
                   && a.patterns[p].notes[n].channel == b.patterns[p].notes[n].channel);
    }
    const auto clips = [] (const Song& s) {
        std::vector<std::tuple<int, std::int64_t, std::int64_t, int, int>> out;
        for (const auto& c : s.playlist)
            out.emplace_back (c.track, c.start, c.length, c.pattern, (int) c.kind);
        std::sort (out.begin(), out.end());
        return out;
    };
    CHECK (clips (a) == clips (b) && clips (a).size() == 2);
    CHECK (a.mixer.size() == b.mixer.size());
    for (std::size_t m = 0; m < std::min (a.mixer.size(), b.mixer.size()); ++m)
        CHECK (a.mixer[m].volume == b.mixer[m].volume && a.mixer[m].effects.size() == b.mixer[m].effects.size());
}

TEST (extras190ro_one_row_pick_reaches_an_als_export)
{
    using namespace lynx;
    // one product installed as CLAP, VST3 and VST: one row (the extra is on by default), and the build a new pick loads
    const std::vector<pluginpicks::Item> items { { "Vital", "Vital Audio", "VST", false }, { "Vital", "Vital Audio", "CLAP", false },
                                                 { "Vital", "Vital Audio", "VST3", false } };
    const auto rows = pluginpicks::groupByFormat (items, true);
    CHECK (rows.size() == 1 && rows[0].members.size() == 3);
    if (rows.empty() || rows[0].preferred() < 0)
        return;
    const auto picked = items[(std::size_t) rows[0].preferred()].format;

    // that pick on an insert, exported to Live 12 (the app's record callback reports the loaded build's format)
    const auto exportWith = [] (const std::string& format) {
        auto song = Song::makeDefault();
        EffectSlot fx;
        fx.kind = EffectKind::plugin;
        fx.plugin.id = format + "-Vital-1";
        fx.plugin.name = "Vital";
        song.mixer[1].effects.push_back (fx);
        AlsExportOptions options;
        options.pluginRecord = [format] (const PluginRef& ref, bool) -> std::optional<ForeignPlugin> {
            ForeignPlugin f;
            f.name = ref.name;
            f.format = format == "VST3" ? ForeignPlugin::Format::vst3 : format == "CLAP" ? ForeignPlugin::Format::clap : ForeignPlugin::Format::vst2;
            if (f.format == ForeignPlugin::Format::vst3)
                f.hasClassId = true, f.classId[0] = 0x56;
            if (f.format == ForeignPlugin::Format::clap)
                f.clapId = "audio.vital.synth";
            if (f.format == ForeignPlugin::Format::vst2)
                f.vst2Id = 0x56697461;
            return f;
        };
        return exportAls (song, options);
    };
    const auto dropped = [] (const AlsExportResult& r) {
        for (const auto& n : r.notes)
            if (n.find ("Plugin not exported") != std::string::npos)
                return true;
        return false;
    };
    // a VST3 build goes to Live with its device, a CLAP build is left out and named (Live has no CLAP)
    const auto vst3 = exportWith ("VST3"), clap = exportWith ("CLAP");
    CHECK (vst3.xml.find ("PluginDeviceVst3") != std::string::npos || vst3.xml.find ("Vst3PluginInfo") != std::string::npos);
    CHECK (! dropped (vst3) && dropped (clap));

    // the merged row's pick. Round 190rk (plugin sweep, same rollup) orders the builds VST3 > CLAP > VST2: then the pick is
    // the VST3 and the .als keeps it. Before that lands the pick is the CLAP, and the export names what it left out, so
    // nothing is lost silently. (Integration: once 190rk is in, only the first branch runs; the second can go.)
    if (pluginpicks::formatRank ("VST3") < pluginpicks::formatRank ("CLAP"))
    {
        CHECK (picked == "VST3");
        CHECK (! dropped (exportWith (picked)));
    }
    else
    {
        CHECK (picked == "CLAP");
        CHECK (dropped (exportWith (picked)));
    }
}
