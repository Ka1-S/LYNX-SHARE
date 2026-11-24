    // Round 190qs (issue #282, "FL Studio Trial + LYNX problem": Slicex and SoundFont Player gone in FL after Export to FL
    // Studio). FL marks a plugin it saved while that plugin was a trial (plugin record 0xD4 flag 0x80, core/FlWrapper.h
    // flTrialSaved) and keeps such a plugin locked where it is still a trial. The mark goes back with the plugin it was
    // saved with, through a .lynx save too, and the export report says so; LYNX's own records never carry it. FL's
    // Patcher used as a generator goes back as FL's Patcher (its whole graph), not as its main synth. Included in the app
    // class (--selftest).
    static juce::String issue282Check (juce::String& log)
    {
        using namespace lynx;
        auto song = Song::makeDefault();
        song.channels.resize (1);
        song.channels[0].name = "Lead";
        song.channels[0].instrument = InstrumentKind::threeosc;
        EffectSlot fx;
        fx.kind = EffectKind::fruityLimiter;
        fx.flOrigin = { "Fruity Limiter", flpexport::fruityLimiterDefaultState(), 0 };
        song.mixer[1].effects.push_back (fx);
        flpexport::stampOrigins (song);

        auto exportOf = [] (const Song& s, int& notes) {
            flpexport::Options options;
            flpexport::Report report;
            auto p = flp::parseProject (flpexport::exportFlp (s, options, report));
            notes = (int) std::count_if (report.notes.begin(), report.notes.end(),
                                         [] (const std::string& n) { return n.find ("saved by a trial of FL Studio") != std::string::npos; });
            return p;
        };
        auto flags = [] (const flp::Project& p, int& channel, int& slot) {
            channel = p.channels.empty() ? -1 : p.channels[0].wrapperFlags;
            slot = -1;
            for (const auto& m : p.mixer)
                if (m.index == 1 && ! m.slots.empty())
                    slot = m.slots[0].wrapperFlags;
        };
        int notes = 0, channel = 0, slot = 0;
        auto p = exportOf (song, notes);
        flags (p, channel, slot);
        log << "LYNX's records: channel " << juce::String::toHexString (channel) << ", slot " << juce::String::toHexString (slot) << "\n";
        if (flTrialSaved (channel) || flTrialSaved (slot) || notes != 0)
            return "ERROR LYNX's own records carry FL's trial mark";

        // as a trial of FL saves them, imported, saved as .lynx and opened again, exported
        p.channels[0].wrapperFlags = 0x1d0;
        for (auto& m : p.mixer)
            if (m.index == 1 && ! m.slots.empty())
                m.slots[0].wrapperFlags = 0x1c0;
        FlpImportOptions options;
        options.fileExists = [] (const std::string&) { return true; };
        juce::String error;
        const auto reopened = ProjectFile::fromJson (ProjectFile::toJson (importFlp (flp::parseProject (flp::writeProject (p)), options)), error);
        if (! reopened || reopened->channels.size() != 1 || reopened->mixer.size() < 2 || reopened->mixer[1].effects.size() != 1)
            return "ERROR the trial-saved project did not come back from .lynx (" + error + ")";
        if (! reopened->channels[0].flOrigin.trialSaved || ! reopened->mixer[1].effects[0].flOrigin.trialSaved)
            return "ERROR the trial mark is lost in .lynx";
        flags (exportOf (*reopened, notes), channel, slot);
        log << "FL's trial records back: channel " << juce::String::toHexString (channel) << ", slot " << juce::String::toHexString (slot)
            << ", report notes " << notes << "\n";
        if (channel != 0x1d0 || slot != 0x1c0 || notes != 2)
            return "ERROR the trial-saved plugins did not go back as FL saved them";
        auto replaced = *reopened;
        replaced.channels[0].instrument = InstrumentKind::sampler;
        replaced.channels[0].samplePath = "C:/x.wav";
        replaced.channels[0].pluginState.clear();
        flags (exportOf (replaced, notes), channel, slot);
        if (channel != 0x150 || slot != 0x1c0 || notes != 1)
            return "ERROR a channel that holds another instrument kept the old plugin's trial mark";

        // FL's Patcher as a generator (when FL Studio is here): FL's Patcher with its own state
        const auto& fl = flStudioAccess();
        if (! fl.roots.data.empty())
        {
            const auto file = juce::File (juce::String::fromUTF8 (fl.roots.data.c_str())).getChildFile ("System/Render tests/Patcher (generator).flp");
            juce::MemoryBlock bytes;
            if (file.loadFileAsData (bytes))
            {
                const std::vector<std::uint8_t> data ((const std::uint8_t*) bytes.getData(), (const std::uint8_t*) bytes.getData() + bytes.getSize());
                const auto original = flp::parseProject (data);
                const auto imported = importFlp (original, options);
                const auto out = exportOf (imported, notes);
                const auto patcher = [] (const flp::Project& q) {
                    return std::find_if (q.channels.begin(), q.channels.end(), [] (const flp::Channel& c) { return c.pluginName == "Patcher"; });
                };
                const auto a = patcher (original), b = patcher (out);
                log << "Patcher (generator): " << (b != out.channels.end() ? "Patcher" : "not Patcher") << "\n";
                if (a != original.channels.end() && (b == out.channels.end() || b->pluginState != a->pluginState))
                    return "ERROR FL's Patcher generator did not go back as FL's Patcher";
            }
        }
        return "OK";
    }
