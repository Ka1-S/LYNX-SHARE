// --- Round 180d: Export to FL Studio, automation clip trims (idea by Tookolld) ------------------------------------------

#include <map>

TEST (render180d_export_trims_automation_clips_shorter_than_their_content)
{
    // FL reads -1 as "the whole content", whatever the item's length says: a short clip of a long automation made
    // Thief 208 bars long. A clip showing all its content stays -1, as in FL's own files.
    using namespace lynx;
    auto song = Song::makeDefault();
    Automation a;
    a.target = { AutomationTargetKind::tempo, 0, 0, 0 };
    a.points = { { 0, 0.5f, 0.0f }, { ppq * 100, 0.5f, 0.0f } };
    song.automations.push_back (a);
    const auto index = (int) song.automations.size() - 1;
    auto addClip = [&] (std::int64_t start, std::int64_t length, std::int64_t offset) {
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = index;
        c.track = 3;
        c.start = start;
        c.length = length;
        c.offset = offset;
        song.playlist.push_back (c);
    };
    song.playlist.clear();
    addClip (0, ppq * 100, 0);        // all of it
    addClip (ppq * 200, ppq * 4, 0);  // its first 4 beats
    addClip (ppq * 300, ppq * 4, ppq * 8); // 4 beats from beat 8

    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (! project.arrangements.empty());
    std::map<double, std::pair<float, float>> byBeat;
    const auto fileBeat = (double) project.header.ppq;
    for (const auto& item : project.arrangements[0].items)
        if (item.kind == flp::PlaylistItem::Kind::channelClip)
            byBeat[item.position / fileBeat] = { item.startOffset, item.endOffset };
    CHECK (byBeat.size() == 3);
    CHECK (byBeat[0.0].first == -1.0f && byBeat[0.0].second == -1.0f);
    CHECK (byBeat[200.0].first == 0.0f && byBeat[200.0].second == 4.0f);
    CHECK (byBeat[300.0].first == 8.0f && byBeat[300.0].second == 12.0f);
}
