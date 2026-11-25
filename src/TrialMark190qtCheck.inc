    // Round 190qt (owner's option A, 11-24): a project from an .flp an FL Studio trial saved (0x1C = 0, REA round 190gy)
    // goes back to FL as a trial save (0x1C = 0, FL's values scrambled after it), through a .lynx save and reopen, and
    // after its tracks are imported into another project; a project made in LYNX or from a registered save stays
    // registered, with no report line. Included in the app class (--selftest).
    static juce::String trialMark190qtCheck (juce::String& log)
    {
        using namespace lynx;
        flp::Project fl;
        fl.tempo = 133.0;
        flp::Channel c;
        c.index = 0;
        c.type = flp::ChannelType::sampler;
        c.name = "Kick";
        c.samplePath = "C:/kick.wav";
        c.mixerTrack = 3;
        fl.channels.push_back (c);
        auto registeredValue = [] (const std::vector<std::uint8_t>& file) {
            flp::EventReader reader (file);
            flp::Event e;
            while (reader.next (e))
                if (e.id == 0x1C)
                    return (int) e.value;
            return -1;
        };
        auto exportOf = [] (const Song& s, int& notes) {
            flpexport::Options options;
            flpexport::Report report;
            auto bytes = flpexport::exportFlp (s, options, report);
            notes = (int) std::count_if (report.notes.begin(), report.notes.end(),
                                         [] (const std::string& n) { return n.find ("came from an FL Studio trial save") != std::string::npos; });
            return bytes;
        };
        FlpImportOptions options;
        options.fileExists = [] (const std::string&) { return true; };

        // LYNX's own project and a registered save: registered, no line
        int notes = 0;
        auto out = exportOf (Song::makeDefault(), notes);
        const auto registeredSong = importFlp (flp::parseProject (flp::writeProject (fl)), options);
        int notes2 = 0;
        const auto out2 = exportOf (registeredSong, notes2);
        log << "LYNX's own: 0x1C = " << registeredValue (out) << ", registered save: 0x1C = " << registeredValue (out2) << "\n";
        if (registeredValue (out) != 1 || registeredValue (out2) != 1 || notes != 0 || notes2 != 0 || registeredSong.flTrialSaved)
            return "ERROR a project made in LYNX or from a registered save did not go to FL as registered";

        // a trial save, imported, saved as .lynx and opened again, exported
        fl.trialSaved = true;
        const auto trialFile = flp::writeProject (fl);
        juce::String error;
        const auto reopened = ProjectFile::fromJson (ProjectFile::toJson (importFlp (flp::parseProject (trialFile), options)), error);
        if (! reopened || ! reopened->flTrialSaved)
            return "ERROR the trial origin was lost in .lynx (" + error + ")";
        out = exportOf (*reopened, notes);
        const auto back = flp::parseProject (out);
        log << "trial save back: 0x1C = " << registeredValue (out) << ", tempo " << back.tempo << ", report lines " << notes << "\n";
        if (registeredValue (out) != 0 || ! back.trialSaved || std::abs (back.tempo - 133.0) > 1.0e-9 || notes != 1
            || back.channels.size() != 1 || back.channels[0].mixerTrack != 3)
            return "ERROR the trial save did not go back to FL as one";

        // its tracks imported into a LYNX project: that project's export is a trial save too
        auto mine = Song::makeDefault();
        auto source = *reopened;
        if (source.playlist.empty())
        {
            Clip clip;
            clip.kind = ClipKind::pattern;
            clip.pattern = 0;
            clip.track = 0;
            clip.length = 384;
            if (source.patterns.empty())
                source.patterns.emplace_back();
            source.patterns[0].notes.push_back ({ 0, 96, 0, 60, 0.8f });
            source.playlist.push_back (clip);
        }
        const auto r = trackimport::importTracks (mine, source, { source.playlist.front().track }, {});
        out = exportOf (mine, notes);
        log << "tracks imported: " << (r.ok ? "ok" : "failed") << ", 0x1C = " << registeredValue (out) << "\n";
        if (! r.ok || ! mine.flTrialSaved || registeredValue (out) != 0)
            return "ERROR a project that imported tracks from a trial save lost the mark";
        return "OK";
    }
