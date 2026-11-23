// Rollup 0.1.31 step 6: the gold sweep for round 190om's decisions 4 and 5 (env-gated, a survey: no checks of its own).
// LYNX_SWEEP_DIR=<folder> (recursive): every .flp and .als in it is imported and listed when
//   (a) "shared-lanes": a control has event lanes in two or more patterns (190om's renderer plays the lane of the pattern
//       whose clip started last; before, only the last pattern's lane drove it and the other patterns' events did nothing);
//   (b) "song-curves": a pattern with notes that carry their own curves (Live's MPE expressions, a module's slides) has
//       Playlist clips (190om: Song mode plays the curves; before, they were sent in Pattern mode only).
// Run: LYNX_SWEEP_DIR=... LYNX_TEST=rollup031s6_gold_sweep lynx_tests

TEST (rollup031s6_gold_sweep_shared_lanes_and_song_mode_curves)
{
    const char* dir = std::getenv ("LYNX_SWEEP_DIR");
    if (dir == nullptr)
        return;
    int files = 0, failed = 0, listed = 0;
    for (auto& entry : std::filesystem::recursive_directory_iterator (dir, std::filesystem::directory_options::skip_permission_denied))
    {
        const auto ext = entry.path().extension().string();
        if (ext != ".flp" && ext != ".als")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        std::optional<lynx::Song> song;
        try
        {
            if (ext == ".flp")
            {
                lynx::FlpImportOptions o;
                o.fileExists = [] (const std::string&) { return false; };
                song = lynx::importFlp (flp::parseProject (bytes), o);
            }
            else
                song = lynx::importAls (bytes, als175a::options (nullptr));
        }
        catch (...)
        {
        }
        if (! song)
        {
            ++failed;
            continue;
        }
        ++files;
        std::map<std::tuple<int, int, int, int>, std::set<int>> patternsOf;
        std::set<int> placed;
        for (const auto& c : song->playlist)
            if (c.kind == lynx::ClipKind::pattern)
                placed.insert (c.pattern);
        int curvedPatterns = 0, curvedNotes = 0;
        for (int p = 0; p < (int) song->patterns.size(); ++p)
        {
            const auto& pattern = song->patterns[(std::size_t) p];
            for (const auto& lane : pattern.events)
                if (! lane.events.empty() && lane.target.kind != lynx::AutomationTargetKind::none)
                    patternsOf[{ (int) lane.target.kind, lane.target.index, lane.target.slot, lane.target.param }].insert (p);
            if (placed.count (p) != 0)
            {
                const auto n = (int) std::count_if (pattern.notes.begin(), pattern.notes.end(), [] (const lynx::Note& x) { return x.curves.has(); });
                curvedNotes += n;
                curvedPatterns += n > 0 ? 1 : 0;
            }
        }
        int shared = 0, sharedPlaced = 0;
        for (const auto& [t, ps] : patternsOf)
            if (ps.size() >= 2)
            {
                ++shared;
                sharedPlaced += std::count_if (ps.begin(), ps.end(), [&] (int p) { return placed.count (p) != 0; }) >= 2 ? 1 : 0;
            }
        if (shared > 0 || curvedPatterns > 0)
        {
            ++listed;
            std::printf ("  sweep%s%s mode=%s shared-lanes=%d (both placed %d) song-curves=%d patterns / %d notes: %s\n", shared > 0 ? " [a]" : "",
                         curvedPatterns > 0 ? " [b]" : "", song->mode == lynx::PlayMode::song ? "song" : "pattern", shared, sharedPlaced,
                         curvedPatterns, curvedNotes, entry.path().string().c_str());
        }
    }
    std::printf ("  sweep: %d projects read (%d not), %d listed\n", files, failed, listed);
}
