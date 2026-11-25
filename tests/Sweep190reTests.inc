// Round 190re (0.1.32 regression sweep): one test per defect the sweep found and fixed. The app-side checks of the same
// defects are the self-test's `sweep190re` item and the --sweep190re route (src/Sweep190re.inc).

// Undo / redo put back a whole earlier Song, with that moment's working time: every undo took the time since that edit
// off the project's clock (Settings › Info, the .flp's work days). A history step keeps the clock it leaves.
TEST (sweep190re_undo_keeps_the_project_clock)
{
    using namespace lynx;
    ProjectInfo older;
    older.title = "Before";
    older.created = 0.0;      // (a snapshot from before the first save)
    older.workSeconds = 60.0; // (a minute in)
    ProjectInfo now;
    now.title = "After";
    now.created = 1.7e9;
    now.workSeconds = 3600.0; // (an hour in)
    auto restored = older;
    keepProjectClock (restored, now);
    CHECK (restored.workSeconds == 3600.0 && restored.created == 1.7e9);
    CHECK (restored.title == "Before"); // (the credits stay undoable: "Project info" steps)
    // both history paths keep it: the normal one and the Online Sessions' selective one
    const auto engine = i18ntest::readFile ("src/engine/AudioEngine.cpp");
    const auto normal = engine.find ("void AudioEngine::historyStep (bool redo)");
    const auto session = engine.find ("void AudioEngine::sessionStep (bool redo)");
    CHECK (normal != std::string::npos && session != std::string::npos);
    const auto keepNormal = engine.find ("keepProjectClock (song.info, clock);", normal);
    const auto keepSession = engine.find ("keepProjectClock (next.info, song.info);", session);
    CHECK (keepNormal != std::string::npos && keepNormal < engine.find ("\n}\n", normal));
    CHECK (keepSession != std::string::npos && keepSession < engine.find ("\n}\n", session));
}

// "Nothing to play in Song mode yet" (round 190np's tip) stayed up after it stopped being true: a project opened, a clip
// placed, Pattern mode, the Clip-launch or Simple layout. The empty-state refresh (every song change and layout switch)
// closes it when the signal no longer holds.
TEST (sweep190re_song_mode_tip_follows_the_song)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.playlist.clear();
    s.arrangements.clear();
    CHECK (onboarding::songModeEmptySignal (true, s, layoutstyle::Style::pattern));
    CHECK (! onboarding::songModeEmptySignal (false, s, layoutstyle::Style::pattern));     // (Pattern mode)
    CHECK (! onboarding::songModeEmptySignal (true, s, layoutstyle::Style::clipLaunch));   // (other layouts)
    CHECK (! onboarding::songModeEmptySignal (true, s, layoutstyle::Style::simple));
    s.playlist.push_back ({ 0, 0, 0, 4 * ppq, 0 });
    CHECK (! onboarding::songModeEmptySignal (true, s, layoutstyle::Style::pattern));      // (a clip placed / a project opened)
    const auto ui = i18ntest::readFile ("src/ui/MainOnboarding.cpp");
    const auto refresh = ui.find ("void MainComponent::updateEmptyHint()");
    const auto close = ui.find ("tipCard->getTip() == onboarding::Tip::songModeEmpty", refresh);
    CHECK (refresh != std::string::npos && close != std::string::npos && close < ui.find ("\n}\n", refresh));
    CHECK (ui.find ("closeTip();", close) < ui.find ("\n}\n", refresh));
}

// Export › FL Studio project's report: the Master slots left out (a user's 0.1.29 project: a limiter, then five plugins
// that are not installed here and a visualizer) all read "Master slot 2: Plugin (LYNX only)". Each is now named by its own
// slot, and a missing hosted plugin by its name, as a channel's is.
TEST (sweep190re_flp_export_report_names_each_left_out_slot)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer.resize (std::max<std::size_t> (song.mixer.size(), 2));
    auto& master = song.mixer[0].effects;
    master.clear();
    master.push_back (makeEffectSlot (EffectKind::limiter));
    for (const auto* name : { "TR5 Saturator X", "CamelCrusher" })
    {
        auto fx = makeEffectSlot (EffectKind::plugin);
        fx.plugin.name = name;
        master.push_back (fx);
    }
    master.push_back (makeEffectSlot (EffectKind::visualizer));
    flpexport::Options options; // (no plugin records: nothing is installed)
    flpexport::Report report;
    flpexport::exportFlp (song, options, report);
    const auto has = [&report] (const std::string& text) {
        return std::any_of (report.dropped.begin(), report.dropped.end(), [&text] (const std::string& l) { return l.find (text) != std::string::npos; });
    };
    CHECK (has ("slot 2: plugin 'TR5 Saturator X' (not found)"));
    CHECK (has ("slot 3: plugin 'CamelCrusher' (not found)"));
    CHECK (has ("slot 4: ") && has ("(LYNX only)"));
    CHECK (! has ("Plugin (LYNX only)"));
}

// GarageBand / BandLab imports: an audio region's start into its file was split with floor, so the sub-tick rest went up to
// 0.99, past the -0.5..0.5 a .lynx keeps: a saved and reopened project played the clip up to half a tick earlier (CC
// Jingle.band: 0.9927 -> 0.5 after a reopen). Rounded now, as the FL and DAWproject imports do; the clip starts at the
// same place in the file.
TEST (sweep190re_band_and_blx_offsets_keep_the_lynx_fraction_range)
{
    using namespace lynx;
    band::Project project;
    project.bpm = 96.0;
    project.sampleRate = 44100.0;
    band::Track track;
    track.name = "Voice";
    track.audio = true;
    band::Region region;
    region.audio = true;
    region.file = "Audio Recorder#01.aif";
    region.start = 0;
    region.trimSamples = 44137; // (just over a second: the rest is past half a tick)
    region.lengthSamples = 44100;
    track.regions.push_back (region);
    project.tracks.push_back (track);
    std::map<std::string, band190ic::Bytes> files;
    files["/songs/Jingle.band/Media/Audio Recorder#01.aif"] = { 0 };
    FlpImportReport report;
    const auto song = importBand (project, band190ic::options (&report, &files));
    const Clip* clip = nullptr;
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::audio)
            clip = &c;
    CHECK (clip != nullptr);
    if (clip != nullptr)
    {
        const auto ticks = 44137.0 / 44100.0 * (96.0 / 60.0 * ppq);
        CHECK (clip->offsetFraction >= -0.5f && clip->offsetFraction <= 0.5f);
        CHECK (std::abs ((double) clip->offset + (double) clip->offsetFraction - ticks) < 1e-3);
    }
    // the BandLab revision's voice take (0.4 s into its file = 640 ticks, a double that floor cut to 639 + 0.99...)
    auto blxFiles = blx190jr::folder();
    const auto blxSong = importBlxBytes (blx190jr::bytesOf (blx190jr::revision()), blx190jr::options (&report, &blxFiles), nullptr);
    CHECK (blxSong.has_value());
    if (blxSong)
        for (const auto& c : blxSong->playlist)
            if (c.kind == ClipKind::audio)
                CHECK (c.offsetFraction >= -0.5f && c.offsetFraction <= 0.5f);
    const auto band = i18ntest::readFile ("src/core/BandImport.cpp");
    const auto blx = i18ntest::readFile ("src/core/BlxImport.cpp");
    CHECK (band.find ("clip.offset = (std::int64_t) std::floor (offset);") == std::string::npos);
    CHECK (blx.find ("clip.offset = (std::int64_t) std::floor (offset);") == std::string::npos);
}

// Export › FL Studio project: Gross Beat's automation, which the import has driven since round 190py (0.1.32), had no way
// back: "its target has no FL equivalent" (FL's Asher Postman demo: 2 of its clips; 0.1.31 kept them). FL's own parameter
// numbers and raw ranges again; imported, exported and imported once more, the same clips on the same parameters.
TEST (sweep190re_gross_beat_automation_goes_back_to_fl)
{
    using namespace lynx;
    if (realFlpDir.empty() || ! std::filesystem::exists (parity190py::demos()))
        return;
    std::filesystem::path asher;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (parity190py::demos()))
        if (entry.path().filename() == "Asher Postman - Future House.flp")
            asher = entry.path();
    if (asher.empty())
        return;
    const auto grossBeatClips = [] (const Song& s) {
        std::vector<std::pair<int, int>> list; // (insert, parameter)
        for (const auto& a : s.automations)
            if (a.target.kind == AutomationTargetKind::effectParam && a.target.index >= 0 && a.target.index < (int) s.mixer.size()
                && a.target.slot >= 0 && a.target.slot < (int) s.mixer[(std::size_t) a.target.index].effects.size()
                && s.mixer[(std::size_t) a.target.index].effects[(std::size_t) a.target.slot].kind == EffectKind::grossBeat)
                list.push_back ({ a.target.index, a.target.param });
        std::sort (list.begin(), list.end());
        return list;
    };
    FlpImportOptions options;
    const auto first = importFlp (flp::parseProject (parity190py::readFile (asher)), options);
    const auto before = grossBeatClips (first);
    CHECK (! before.empty());
    flpexport::Options exportOptions;
    flpexport::Report report;
    const auto bytes = flpexport::exportFlp (first, exportOptions, report);
    for (const auto& line : report.dropped)
        CHECK (line.find ("Gross Beat") == std::string::npos);
    const auto second = importFlp (flp::parseProject (bytes), options);
    CHECK (grossBeatClips (second) == before);
}
