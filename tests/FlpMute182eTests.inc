// --- Round 182e: FL import / export fixes from issues #110-#113 ---------------------------------------------------------

TEST (flp182e_clip_mute_flag_round_trips_for_every_clip_kind)
{
    // Issue #110: FL's clip mute is bit 0x2000 of the playlist item flags (+0x12). FL tests bit 0x20 of byte +0x13 on the
    // record it keeps as loaded (REA round 182e). Muted pattern, audio and automation clips must stay muted on import,
    // and the export writes the bit back instead of leaving the clip out.
    using namespace lynx;
    auto song = Song::makeDefault();
    song.playlist.clear();

    ChannelDef audio;
    audio.name = "take";
    audio.instrument = InstrumentKind::audioClip;
    audio.samplePath = "C:\\samples\\take.wav";
    song.channels.push_back (audio);
    const auto audioChannel = (int) song.channels.size() - 1;

    Automation a;
    a.target = { AutomationTargetKind::tempo, 0, 0, 0 };
    a.points = { { 0, 0.5f, 0.0f }, { ppq * 4, 0.5f, 0.0f } };
    song.automations.push_back (a);
    const auto automation = (int) song.automations.size() - 1;

    auto add = [&] (ClipKind kind, int track, std::int64_t start, bool muted) {
        Clip c;
        c.kind = kind;
        c.track = track;
        c.start = start;
        c.length = ppq * 4;
        c.muted = muted;
        if (kind == ClipKind::pattern)
            c.pattern = 0;
        else if (kind == ClipKind::audio)
            c.channel = audioChannel;
        else
            c.automation = automation;
        song.playlist.push_back (c);
    };
    add (ClipKind::pattern, 0, 0, false);
    add (ClipKind::pattern, 0, ppq * 4, true);
    add (ClipKind::audio, 1, 0, false);
    add (ClipKind::audio, 1, ppq * 4, true);
    add (ClipKind::automation, 2, 0, false);
    add (ClipKind::automation, 2, ppq * 4, true);

    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (! project.arrangements.empty());
    if (project.arrangements.empty())
        return;
    const auto& items = project.arrangements[0].items;
    CHECK (items.size() == 6);
    const auto fileBeat = (std::uint32_t) project.header.ppq;
    for (const auto& item : items)
    {
        // the second clip of each row is the muted one; the rest of the flags stay FL's default 0x40
        const bool muted = item.position == fileBeat * 4;
        CHECK (item.muted() == muted);
        CHECK ((item.flags & ~flp::PlaylistItem::mutedFlag) == flp::PlaylistItem::defaultFlags);
    }

    FlpImportOptions io;
    io.fileExists = [] (const std::string&) { return true; };
    io.sampleSeconds = [] (const std::string&) { return 2.0; };
    const auto back = importFlp (project, io);
    std::map<std::pair<int, int>, int> seen; // (kind, muted) -> count
    for (const auto& c : back.playlist)
    {
        CHECK (c.muted == (c.start == ppq * 4));
        ++seen[std::make_pair ((int) c.kind, c.muted ? 1 : 0)];
    }
    for (const auto kind : { ClipKind::pattern, ClipKind::audio, ClipKind::automation })
    {
        CHECK ((seen[{ (int) kind, 0 }] == 1));
        CHECK ((seen[{ (int) kind, 1 }] == 1));
    }
}
