// Round 190ov (0.1.31): FL automation clips linked to several controls. FL drives every control a clip is linked to (its
// .flp holds one 0xE3 record per link); LYNX kept the first. Several reproducible links now import as target links
// (Automation::links), the points in FL's clip value and each link's own scale; a target that other clips drive too plays
// them as one, the clip that plays deciding (as issue #97's groups).

namespace autolinks190ov
{
// two Synth channels on inserts 1 and 2, each one long note (song mode), so a channel's level follows its insert's fader
inline lynx::Song twoInserts (std::int64_t length)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.channels.clear();
    s.playlist.clear();
    s.automations.clear();
    s.patterns.clear();
    s.mixer = Song::defaultMixer (3);
    s.mode = PlayMode::song;
    for (int k = 0; k < 2; ++k)
    {
        ChannelDef c;
        c.name = k == 0 ? "A" : "B";
        c.instrument = InstrumentKind::synth;
        c.mixerTrack = 1 + k;
        s.channels.push_back (c);
        Pattern p;
        p.name = "P" + std::to_string (k + 1);
        Note n;
        n.start = 0;
        n.length = length;
        n.key = 72;
        n.channel = k;
        p.notes.push_back (n);
        s.patterns.push_back (p);
        Clip clip;
        clip.kind = ClipKind::pattern;
        clip.pattern = k;
        clip.track = k;
        clip.start = 0;
        clip.length = length;
        s.playlist.push_back (clip);
    }
    return s;
}

inline void addClip (lynx::Song& s, int automation, std::int64_t start, std::int64_t length)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::automation;
    c.automation = automation;
    c.track = 3 + automation;
    c.start = start;
    c.length = length;
    s.playlist.push_back (c);
}

inline lynx::Automation held (const std::string& name, const lynx::AutomationTarget& target, float v, std::int64_t length)
{
    lynx::Automation a;
    a.name = name;
    a.target = target;
    a.points = { { 0, v, 0.0f }, { length, v, 0.0f } };
    return a;
}

// the RMS of channel `alone`'s output over each window [from, to) of song ticks (the other pattern clip muted)
inline std::vector<double> levels (lynx::Song song, int alone, const std::vector<std::pair<double, double>>& windows)
{
    constexpr int rate = 44100, block = 256;
    for (auto& c : song.playlist)
        if (c.kind == lynx::ClipKind::pattern && c.pattern != alone)
            c.muted = true;
    const auto tps = lynx::ticksPerSample (song.bpm, rate);
    auto state = lynx::RackState::build (song, nullptr, rate);
    lynx::RackRenderer renderer;
    renderer.prepare (rate, block);
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    std::vector<double> sum (windows.size(), 0.0);
    std::vector<long> count (windows.size(), 0);
    double end = 0.0;
    for (const auto& w : windows)
        end = std::max (end, w.second);
    for (double tick = 0.0; tick < end;)
    {
        renderer.process (state.get(), true, tick, block, l.data(), r.data());
        for (std::size_t w = 0; w < windows.size(); ++w)
            if (tick >= windows[w].first && tick + block * tps <= windows[w].second)
            {
                for (const auto x : l)
                    sum[w] += (double) x * x;
                count[w] += block;
            }
        tick += block * tps;
    }
    std::vector<double> out;
    for (std::size_t w = 0; w < windows.size(); ++w)
        out.push_back (count[w] > 0 ? std::sqrt (sum[w] / (double) count[w]) : 0.0);
    return out;
}

inline lynx::AutomationTarget fader (int track) { return { lynx::AutomationTargetKind::mixerVolume, track, 0, 0 }; }

inline bool near (double a, double b) { return std::abs (a - b) <= 0.01 * std::max (a, b) + 1e-6; }
} // namespace autolinks190ov

TEST (autolinks190ov_links_drive_every_control_through_their_scales)
{
    using namespace lynx;
    using namespace autolinks190ov;
    // one clip linked to both faders, the second link through a scale (FL's value 0..1 -> 0.2..0.6): at a held clip value
    // v the first fader is at v, the second at 0.2 + 0.4 v, as each set by hand
    const auto length = 2 * ppq;
    for (const auto v : { 0.9f, 0.4f })
    {
        auto song = twoInserts (length);
        auto a = held ("Both faders", fader (1), v, length);
        a.links = { { fader (1), "", {} }, { fader (2), "", { 0.2f, 0.6f } } };
        song.automations.push_back (a);
        addClip (song, 0, 0, length);
        auto byHandA = twoInserts (length), byHandB = twoInserts (length);
        CHECK (byHandA.setTargetValue (fader (1), v) && byHandB.setTargetValue (fader (2), 0.2f + 0.4f * v));
        const std::vector<std::pair<double, double>> second { { (double) length / 2, (double) length } };
        CHECK (near (levels (song, 0, second)[0], levels (byHandA, 0, second)[0]));
        CHECK (near (levels (song, 1, second)[0], levels (byHandB, 1, second)[0]));
    }
    // the scale reads with straight lines between its points, clamped
    Automation::Link l { fader (1), "", { 0.0f, 1.0f, 0.5f } };
    CHECK (std::abs (l.scaled (0.25f) - 0.5f) < 1e-6f && std::abs (l.scaled (0.75f) - 0.75f) < 1e-6f && l.scaled (2.0f) == 0.5f);
    CHECK (Automation::Link {}.scaled (0.3f) == 0.3f);
}

TEST (autolinks190ov_a_shared_control_follows_the_clip_that_plays)
{
    using namespace lynx;
    using namespace autolinks190ov;
    // fader 1: a plain clip at 0.3 over beats 0..2, then a clip linked to faders 1 and 2 at 0.8 over beats 2..4. FL's rule
    // (issue #97): the clip that plays decides; before round 190ov the link took fader 1 over the whole song
    const auto length = 4 * ppq;
    auto song = twoInserts (length);
    song.automations.push_back (held ("Fader 1", fader (1), 0.3f, 2 * ppq));
    auto linked = held ("Both faders", fader (1), 0.8f, 2 * ppq);
    linked.links = { { fader (1), "", {} }, { fader (2), "", {} } };
    song.automations.push_back (linked);
    addClip (song, 0, 0, 2 * ppq);
    addClip (song, 1, 2 * ppq, 2 * ppq);
    const std::vector<std::pair<double, double>> windows { { 1.0 * ppq, 2.0 * ppq }, { 3.0 * ppq, 4.0 * ppq } };
    auto at03 = twoInserts (length), at08 = twoInserts (length);
    CHECK (at03.setTargetValue (fader (1), 0.3f) && at08.setTargetValue (fader (1), 0.8f));
    const auto got = levels (song, 0, windows);
    CHECK (near (got[0], levels (at03, 0, windows)[0]));
    CHECK (near (got[1], levels (at08, 0, windows)[1]));
    // fader 2 has only the linked clip: before it plays, its own value (none set: as at the start)
    auto at08b = twoInserts (length);
    CHECK (at08b.setTargetValue (fader (2), 0.8f));
    CHECK (near (levels (song, 1, windows)[1], levels (at08b, 1, windows)[1]));
}

TEST (autolinks190ov_flp_export_and_import_keep_every_link)
{
    using namespace lynx;
    using namespace autolinks190ov;
    // LYNX -> .flp: one 0xE3 record per link on the clip's channel; .flp -> LYNX: the links back, the points in FL's value
    auto song = twoInserts (2 * ppq);
    auto a = held ("Both faders", fader (1), 0.25f, 2 * ppq);
    a.points[1].value = 0.75f;
    a.links = { { fader (1), "", {} }, { fader (2), "(Input*.6+.4)*0.8", {} } };
    song.automations.push_back (a);
    song.automations.push_back (held ("Fader 2 alone", fader (2), 0.6f, 2 * ppq));
    addClip (song, 0, 0, 2 * ppq);
    addClip (song, 1, 0, 2 * ppq);
    flpexport::Report report;
    auto project = flp::parseProject (flpexport::exportFlp (song, {}, report));
    int clipChannel = -1, single = -1;
    for (const auto& c : project.channels)
        if (c.type == flp::ChannelType::automationClip)
            (c.name == "Both faders" ? clipChannel : single) = c.index;
    int linksOfClip = 0, linksOfSingle = 0;
    for (const auto& l : project.automationLinks)
    {
        if (l.channel == clipChannel)
            ++linksOfClip;
        else if (l.channel == single)
            ++linksOfSingle;
    }
    CHECK (linksOfClip == 2 && linksOfSingle == 1);
    // the formula in FL's own place, an 0xE6 after its record (rea/FL-FACTS-autolinks.md); none for a 1:1 link
    int withFormula = 0;
    for (const auto& l : project.automationLinks)
        withFormula += l.formula.empty() ? 0 : 1;
    CHECK (withFormula == 1);
    const auto back = importFlp (project);
    const Automation* both = nullptr;
    const Automation* alone = nullptr;
    for (const auto& x : back.automations)
        (x.name == "Both faders" ? both : alone) = &x;
    CHECK (both != nullptr && alone != nullptr);
    if (both != nullptr && alone != nullptr)
    {
        CHECK (both->links.size() == 2 && both->target == fader (1));
        CHECK (both->links.size() == 2 && both->links[0].target == fader (1) && both->links[1].target == fader (2));
        CHECK (both->links.size() == 2 && both->links[0].scale.empty() && both->links[1].scale.empty());
        CHECK (both->links.size() == 2 && both->links[0].formula.empty() && both->links[1].formula == "(Input*.6+.4)*0.8");
        CHECK (both->points.size() == 2 && std::abs (both->points[0].value - 0.25f) < 1e-4f && std::abs (both->points[1].value - 0.75f) < 1e-4f);
        CHECK (alone->links.empty() && alone->target == fader (2));
    }

    // a clip whose first link has no LYNX target but a later one has: that one, as a plain clip (FL drives it)
    auto broken = project;
    for (auto it = broken.automationLinks.begin(); it != broken.automationLinks.end(); ++it)
        if (it->channel == single)
        {
            auto none = *it;
            none.target.raw = 0x70000000u + (1u << 22) + (9u << 16) + 0x8000u; // (a plugin parameter of insert 1's empty slot 10)
            broken.automationLinks.insert (it, none);
            break;
        }
    const auto fixed = importFlp (broken);
    for (const auto& x : fixed.automations)
        if (x.name == "Fader 2 alone")
            CHECK (x.links.empty() && x.target == fader (2) && std::abs (x.points[0].value - 0.6f) < 1e-4f);

    // one link with a mapping formula: a target link of its own (FL maps the clip's value through it)
    auto formula = project;
    for (auto& l : formula.automationLinks)
        if (l.channel == single)
            l.formula = "1-Input";
    for (const auto& x : importFlp (formula).automations)
        if (x.name == "Fader 2 alone")
        {
            CHECK (x.links.size() == 1 && x.target == fader (2));
            CHECK (x.links.size() == 1 && x.links[0].formula == "1-Input" && x.links[0].target == fader (2));
        }

    // a tempo link next to another link: the tempo is LYNX's tempo map, not a target link, so the clip drives the other
    // one (as a plain clip when that is the only one left)
    auto tempo = project;
    for (auto& l : tempo.automationLinks)
        if (l.channel == clipChannel && l.target.raw != project.automationLinks.front().target.raw)
        {
            l.target.raw = flp::ControlId::tempo;
            l.formula.clear();
            break;
        }
    for (const auto& x : importFlp (tempo).automations)
        if (x.name == "Both faders")
            CHECK (x.links.empty() && x.target == fader (1));

    // the scale of a link: none for FL's own scale, two points for a straight map, more for a bent one
    CHECK (flLinkScale ([] (float v) { return v; }).empty());
    const auto straight = flLinkScale ([] (float v) { return valueFromTempo (60.0 + 120.0 * (double) v); });
    CHECK (straight.size() == 2 && std::abs (straight[0] - valueFromTempo (60.0)) < 1e-6f && std::abs (straight[1] - valueFromTempo (180.0)) < 1e-6f);
    const auto bent = flLinkScale ([] (float v) { return v * v; });
    Automation::Link probe { fader (1), "", bent };
    CHECK (bent.size() > 2 && bent.size() <= 1025);
    for (int i = 0; i <= 64; ++i)
        CHECK (std::abs (probe.scaled ((float) i / 64.0f) - (float) i * (float) i / 4096.0f) <= 2e-6f);
}

TEST (autolinks190ov_fl_demos_with_multi_link_clips)
{
    using namespace lynx;
    // FL's own demos (when the FL folder is given): Synth_dfr - Overcome's clips each drive two sends; Bombs Away has a clip
    // with 26 links. Every reproducible link is there, and a clip with one link stays a plain clip
    if (realFlpDir.empty())
        return;
    const auto demos = std::filesystem::path (realFlpDir) / "Data" / "Demo projects";
    const auto dir = std::filesystem::exists (demos) ? demos : std::filesystem::path (realFlpDir) / "Demo projects";
    const auto read = [] (const std::filesystem::path& path) {
        std::ifstream in (path, std::ios::binary);
        return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), {});
    };
    const auto overcome = dir / "Product demos" / "Synth_dfr - Overcome - Sytrus.flp";
    if (! std::filesystem::exists (overcome))
        return;
    const auto project = flp::parseProject (read (overcome));
    std::map<int, int> records;
    for (const auto& l : project.automationLinks)
        ++records[l.channel];
    const auto song = importFlp (project);
    int linked = 0, sendLinks = 0;
    for (const auto& a : song.automations)
    {
        if (a.links.size() > 1)
            ++linked;
        for (const auto& l : a.links)
            if (l.target.kind == AutomationTargetKind::mixerSend)
                ++sendLinks;
        CHECK (a.links.empty() || a.target == a.links.front().target);
    }
    int multi = 0;
    for (const auto& [channel, n] : records)
        multi += n > 1 ? 1 : 0;
    int formulas = 0, formulaLinks = 0;
    for (const auto& l : project.automationLinks)
        formulas += l.formula.empty() ? 0 : 1;
    for (const auto& a : song.automations)
        for (const auto& l : a.links)
            formulaLinks += l.formula.empty() ? 0 : 1;
    std::printf ("  Synth_dfr - Overcome: %d clip channels with several links, %d imported with target links, %d send links, "
                 "%d link formulas in the file, %d on imported links\n", multi, linked, sendLinks, formulas, formulaLinks);
    CHECK (formulas >= 5); // (REA: clip channel 0x10 drives 5 targets, each through its own formula)
    for (const auto& a : song.automations)
        for (const auto& l : a.links)
            CHECK (l.formula.empty() || linkformula::Formula::compile (l.formula).isDefault() == false);
    CHECK (multi > 0 && linked > 0 && linked <= multi && sendLinks >= 2);
}
