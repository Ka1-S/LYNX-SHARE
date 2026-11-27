// Round 190rw (0.1.32 stage Q1, part A: Playlist, Piano roll, Channel rack) — the defects it found and fixed.

// Discord (sidthenugget, 11-26): an .flp's SoundFont Player whose font isn't on this PC said "could not be loaded: file
// not found" in its editor, but the Missing files window didn't list it, so it couldn't be located there. The font is
// now looked for like a sample (and listed / relocated); so are the Video Player's video and the user samples of 3x Osc
// and Granulizer, and in a saved project the video tracks' files and the Video Player's video.
TEST (q1a190rw_missing_soundfont_flp_listed_and_relocated)
{
    const std::string gone = "C:\\Users\\sid\\SoundFonts\\SGM-V2.01.sf2";
    const std::array<int, 13> p { 0, 0, 128, 128, 0, -1, -1, -1, -1, -1, -128, -1, -1 };
    flp::Project project;
    flp::Channel channel;
    channel.index = 0;
    channel.type = flp::ChannelType::native;
    channel.name = "Piano";
    channel.pluginName = "Fruity Soundfont Player";
    channel.pluginState = flSoundfontState (p, gone, 0, 4, 0, false);
    project.channels = { channel };

    lynx::FlpImportOptions options;
    options.fileExists = [] (const std::string& path) { return path == "/home/sid/fonts/SGM-V2.01.sf2"; };
    lynx::FlpImportReport report;
    options.report = &report;
    auto song = lynx::importFlp (project, options);
    CHECK (song.channels.size() == 1 && song.channels[0].instrument == lynx::InstrumentKind::soundfont);
    CHECK (song.channels.size() == 1 && song.channels[0].samplePath == gone); // the editor still names the missing font
    CHECK (report.missingFiles.size() == 1);
    CHECK (report.missingFiles.size() == 1 && report.missingFiles[0].path == gone && report.missingFiles[0].usedBy.size() == 1
           && report.missingFiles[0].usedBy[0] == "Piano");

    // the window's Locate / Search: the import runs again with the copy, and the channel plays from it
    lynx::FlpImportReport again;
    options.report = &again;
    options.relocations = { { gone, "/home/sid/fonts/SGM-V2.01.sf2" } };
    song = lynx::importFlp (project, options);
    CHECK (again.missingFiles.empty());
    CHECK (song.channels.size() == 1 && song.channels[0].samplePath == "/home/sid/fonts/SGM-V2.01.sf2"
           && song.channels[0].instrument == lynx::InstrumentKind::soundfont);

    // a font that is there: not listed, the path as before
    lynx::FlpImportReport here;
    options.report = &here;
    options.relocations.clear();
    options.fileExists = [&gone] (const std::string& path) { return path == gone; };
    song = lynx::importFlp (project, options);
    CHECK (here.missingFiles.empty() && song.channels.size() == 1 && song.channels[0].samplePath == gone);
}

TEST (q1a190rw_missing_video_and_osc_samples_flp_listed)
{
    flp::Project project;
    flp::Channel video;
    video.index = 0;
    video.type = flp::ChannelType::native;
    video.name = "Clip video";
    video.pluginName = "Fruity Video Player";
    lynx::video::Settings vs;
    vs.path = "D:\\Edit\\cut 3.mp4";
    video.pluginState = lynx::video::write (vs);
    project.channels = { video };

    lynx::FlpImportOptions options;
    options.fileExists = [] (const std::string& path) { return path == "/media/cut 3.mp4"; };
    lynx::FlpImportReport report;
    options.report = &report;
    auto song = lynx::importFlp (project, options);
    CHECK (report.missingFiles.size() == 1 && report.missingFiles[0].path == "D:\\Edit\\cut 3.mp4" && report.missingFiles[0].usedBy[0] == "Clip video");

    lynx::FlpImportReport again;
    options.report = &again;
    options.relocations = { { "D:\\Edit\\cut 3.mp4", "/media/cut 3.mp4" } };
    song = lynx::importFlp (project, options);
    CHECK (again.missingFiles.empty());
    CHECK (song.channels.size() == 1 && song.channels[0].instrument == lynx::InstrumentKind::videoplayer
           && lynx::video::readOrDefault (song.channels[0].pluginState).path == "/media/cut 3.mp4");

    // 3x Osc's FL data waves (relative to FL's Data folder) are FL's, never listed; a user sample is
    flp::Channel osc;
    osc.index = 0;
    osc.type = flp::ChannelType::native;
    osc.name = "Osc";
    osc.pluginName = "3x Osc";
    osc.samplePath = "\\Patches\\Packs\\wave.wav";
    project.channels = { osc };
    lynx::FlpImportReport flData;
    options.report = &flData;
    options.relocations.clear();
    lynx::importFlp (project, options);
    CHECK (flData.missingFiles.empty());
}

TEST (q1a190rw_missing_files_saved_project_video)
{
    lynx::Song song;
    lynx::ChannelDef font;
    font.name = "Piano";
    font.instrument = lynx::InstrumentKind::soundfont;
    font.samplePath = "/gone/SGM-V2.01.sf2";
    song.channels.push_back (font);
    lynx::ChannelDef player;
    player.name = "Video";
    player.instrument = lynx::InstrumentKind::videoplayer;
    lynx::video::Settings vs;
    vs.path = "/gone/cut.mp4";
    vs.volume = 90;
    player.pluginState = lynx::video::write (vs);
    song.channels.push_back (player);
    lynx::VideoTrack track;
    lynx::VideoClip clip;
    clip.path = "/gone/scene.mov";
    clip.length = 4.0;
    track.clips.push_back (clip);
    song.videoTracks.push_back (track);

    const auto missing = lynx::findMissingSamples (song, [] (const std::string&) { return false; });
    CHECK (missing.size() == 3);
    const auto listed = [&missing] (const std::string& path, const std::string& user) {
        return std::any_of (missing.begin(), missing.end(), [&] (const lynx::MissingFile& m) {
            return m.path == path && std::find (m.usedBy.begin(), m.usedBy.end(), user) != m.usedBy.end();
        });
    };
    CHECK (listed ("/gone/SGM-V2.01.sf2", "Piano"));
    CHECK (listed ("/gone/cut.mp4", "Video: video"));
    CHECK (listed ("/gone/scene.mov", "Video 1"));

    const std::map<std::string, std::string> moved { { "/gone/SGM-V2.01.sf2", "/here/SGM-V2.01.sf2" }, { "/gone/cut.mp4", "/here/cut.mp4" },
                                                     { "/gone/scene.mov", "/here/scene.mov" } };
    CHECK (lynx::applyRelocations (song, moved) == 3);
    CHECK (song.channels[0].samplePath == "/here/SGM-V2.01.sf2");
    const auto after = lynx::video::readOrDefault (song.channels[1].pluginState);
    CHECK (after.path == "/here/cut.mp4" && after.volume == 90); // the rest of the player's state kept
    CHECK (song.videoTracks[0].clips[0].path == "/here/scene.mov");
    // nothing left to relocate: the state is not written again
    const auto state = song.channels[1].pluginState;
    CHECK (lynx::applyRelocations (song, moved) == 0 && song.channels[1].pluginState == state);
}

// Channel rack edits that renumber notes were labelled by the notes: moving, cloning or deleting a channel that has
// notes read "Edit notes: Pattern 1" / "Delete notes: Pattern 1" in Edit › Undo and the Undo history.
TEST (q1a190rw_history_labels_channel_moves)
{
    auto before = lynx::Song::makeDefault();
    lynx::uids::normalize (before);
    CHECK (before.channels.size() >= 3 && ! before.patterns.empty());
    if (before.channels.size() < 3 || before.patterns.empty())
        return;
    for (int ch = 0; ch < 3; ++ch) // notes on the first three channels
        before.patterns[0].notes.push_back ({ ch * 240, 240, ch, 60, 0.8f });

    auto moved = before;
    std::vector<int> selection { 0 };
    CHECK (lynx::moveChannels (moved, selection, 1));
    CHECK (std::string (lynx::describeChange (before, moved).text) == "Move channels");

    auto cloned = before;
    lynx::cloneChannel (cloned, 0);
    lynx::uids::normalize (cloned);
    CHECK (std::string (lynx::describeChange (before, cloned).text) == "Add channel");

    auto removed = before;
    lynx::removeChannels (removed, { 0 });
    CHECK (std::string (lynx::describeChange (before, removed).text) == "Delete channel");

    // a note edit is still a note edit
    auto edited = before;
    edited.patterns[0].notes[0].key = 62;
    CHECK (lynx::describeChangeText (before, edited) == "Edit notes: " + before.patterns[0].name);
    // a project without channel ids (an old test song): a rename is not a move
    auto bare = lynx::Song::makeDefault();
    auto renamed = bare;
    renamed.channels[0].name = "Renamed";
    CHECK (std::string (lynx::describeChange (bare, renamed).text) != std::string ("Move channels"));
}

// Fill each N / Rotate / Euclidean fill in a 3/4 or 7/8 pattern: the rack shows one bar (12 / 14 steps, issue #279) but
// the fills worked on at least 16 steps, so steps landed past the bar and an empty pattern became two bars long.
TEST (q1a190rw_rack_fill_follows_the_time_signature)
{
    for (const auto& [beats, unit, shown] : { std::tuple { 3, 4, 12 }, std::tuple { 7, 8, 14 }, std::tuple { 4, 4, 16 } })
    {
        auto song = lynx::Song::makeDefault();
        song.beatsPerBar = beats;
        song.beatUnit = unit;
        auto& p = song.patterns.front();
        p.notes.clear();
        p.lengthTicks = 0; // (no set length: one bar of the meter)
        CHECK (lynx::patternSteps (song, p) == shown);
        lynx::fillEverySteps (p, 0, 2, lynx::patternSteps (song, p));
        CHECK ((int) p.notes.size() == shown / 2);
        CHECK (std::all_of (p.notes.begin(), p.notes.end(), [shown] (const lynx::Note& n) { return n.start < shown * lynx::ticksPerStep; }));
        CHECK (p.effectiveLength (song.meter()) == (std::int64_t) shown * lynx::ticksPerStep);
        lynx::rotateSteps (p, 0, 1, lynx::patternSteps (song, p));
        CHECK (std::all_of (p.notes.begin(), p.notes.end(), [shown] (const lynx::Note& n) { return n.start < shown * lynx::ticksPerStep; }));
    }
}
