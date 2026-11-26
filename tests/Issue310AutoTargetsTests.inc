// Round 190rt, issue #310 (NewZyro: "after importing an .flp, some clips aren't connected to anything"): every FL
// automation clip of FL's own files keeps a target in LYNX when its control exists in LYNX.

#include "core/ClipLinks.h"

namespace issue310
{
std::vector<std::uint8_t> readFile (const std::filesystem::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return { std::istreambuf_iterator<char> (in), {} };
}

lynx::Song importBytes (const std::vector<std::uint8_t>& file)
{
    lynx::FlpImportOptions options;
    options.fileExists = [] (const std::string&) { return false; };
    return lynx::importFlp (flp::parseProject (file), options);
}
} // namespace issue310

// Diagnostic (LYNX_SURVEY310=1): every FL Data .flp, its clips that end without a target and the FL link ids
TEST (issue310_survey_fl_clip_targets)
{
    if (realFlpDir.empty() || std::getenv ("LYNX_SURVEY310") == nullptr)
        return;
    int clips = 0, lost = 0;
    for (auto& entry : std::filesystem::recursive_directory_iterator (realFlpDir))
    {
        if (entry.path().extension() != ".flp")
            continue;
        const auto bytes = issue310::readFile (entry.path());
        flp::Project project;
        try { project = flp::parseProject (bytes); } catch (...) { continue; }
        lynx::Song song;
        try { song = issue310::importBytes (bytes); } catch (...) { continue; }
        for (const auto& c : project.channels)
        {
            if (c.type != flp::ChannelType::automationClip || c.automation.empty())
                continue;
            ++clips;
            const auto name = c.name.empty() ? "Automation " + std::to_string (c.index + 1) : c.name;
            const auto match = std::find_if (song.automations.begin(), song.automations.end(), [&] (const lynx::Automation& a) {
                return a.name == name && a.target.kind == lynx::AutomationTargetKind::none && a.links.empty();
            });
            if (match == song.automations.end())
                continue;
            ++lost;
            std::string ids;
            for (const auto& l : project.automationLinks)
                if (l.channel == c.index)
                {
                    char buf[32];
                    std::snprintf (buf, sizeof buf, " %08x", l.target.raw);
                    ids += buf;
                    std::string plugin;
                    if (l.target.isChannel())
                    {
                        for (const auto& o : project.channels)
                            if (o.index == l.target.channel())
                                plugin = o.pluginName.empty() ? "(type " + std::to_string ((int) o.type) + ")" : o.pluginName;
                    }
                    else if (l.target.isMixer())
                        for (const auto& m : project.mixer)
                            if (m.index == l.target.track())
                                for (const auto& s : m.slots)
                                    if (s.index == l.target.slot())
                                        plugin = s.plugin;
                    ids += "[" + plugin + "]";
                }
            std::printf ("SURVEY310 %s | %s |%s\n", entry.path().filename().string().c_str(), name.c_str(), ids.c_str());
        }
    }
    std::printf ("SURVEY310 clips=%d lost=%d\n", clips, lost);
}

// FL's gesture (checked in FL 2026, round 190rt): a control's "Link to controller..." lists the clips; Accept with
// "Remove conflicts" on re-targets the clip, off adds the control to the clip's target links
TEST (issue310_link_an_existing_clip_to_a_control)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.channels.resize (2);
    AutomationTarget vol0 { AutomationTargetKind::channelVolume, 0, 0, 0 }, pan1 { AutomationTargetKind::channelPan, 1, 0, 0 },
        vol1 { AutomationTargetKind::channelVolume, 1, 0, 0 };
    Automation lost; // an imported clip whose control LYNX has no equivalent for
    lost.name = "Lost";
    lost.points = { { 0, 0.2f }, { 384, 0.9f } };
    Automation plain;
    plain.name = "Plain";
    plain.target = vol0;
    plain.points = { { 0, 0.5f } };
    s.automations = { lost, plain };
    CHECK (cliplinks::linkable (s.automations[0]) && cliplinks::clipDriving (s, pan1) == -1);
    // a clip without a target takes the control (both ways)
    CHECK (cliplinks::link (s, 0, pan1, false));
    CHECK (s.automations[0].target == pan1 && s.automations[0].links.empty() && s.automations[0].points.size() == 2);
    CHECK (cliplinks::clipDriving (s, pan1) == 0);
    // Remove conflicts off: the control joins the clip's targets (the old one first, 1:1)
    CHECK (cliplinks::link (s, 1, vol1, false));
    CHECK (s.automations[1].links.size() == 2 && s.automations[1].links[0].target == vol0 && s.automations[1].links[1].target == vol1);
    CHECK (s.automations[1].target == vol0 && cliplinks::drives (s.automations[1], vol0) && cliplinks::drives (s.automations[1], vol1));
    CHECK (! cliplinks::link (s, 1, vol1, false)); // (already there)
    // taking one out: one 1:1 link left is the plain clip again
    CHECK (cliplinks::unlink (s, 1, vol1));
    CHECK (s.automations[1].links.empty() && s.automations[1].target == vol0);
    // Remove conflicts on (FL's default): a re-target
    CHECK (cliplinks::link (s, 1, vol1, true));
    CHECK (s.automations[1].links.empty() && s.automations[1].target == vol1 && ! cliplinks::drives (s.automations[1], vol0));
    // the last target out: the clip drives nothing and keeps its points
    CHECK (cliplinks::unlink (s, 1, vol1) && s.automations[1].target.kind == AutomationTargetKind::none && s.automations[1].points.size() == 1);
    // tempo clips and live controller links are not listed (LYNX's tempo map reads the tempo clips)
    Automation tempo;
    tempo.target.kind = AutomationTargetKind::tempo;
    Automation live;
    live.source.channel = 0;
    live.target = vol0;
    CHECK (! cliplinks::linkable (tempo) && ! cliplinks::linkable (live));
    CHECK (! cliplinks::drives (live, vol0));
    // a clip relinked by hand plays its points on the new control
    s.automations[0].target = vol1;
    CHECK (s.automations[0].valueAt (384.0) > 0.85f);
}
