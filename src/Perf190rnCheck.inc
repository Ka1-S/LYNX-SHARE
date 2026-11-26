    // Round 190rn (0.1.32 perf round 10): the undo history's memory cap (AudioEngine::trimUndoHistory, core/SongBytes.h)
    // through a real engine. Included in the app class of Main.cpp; --perf190rntest <report> and --selftest "perf190rn".
    juce::String perf190rnCheck (juce::String& log)
    {
        using namespace lynx;
        juce::String result = "OK";
        const auto check = [&] (const juce::String& what, bool ok) {
            log << (ok ? "ok   " : "FAIL ") << what << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what;
        };

        // the estimate follows what a copy holds: notes, curves, Patcher graphs, slot states
        {
            auto song = Song::makeDefault();
            song.patterns[0].notes.clear();
            const auto empty = songbytes::song (song);
            song.patterns[0].notes.resize (10000);
            const auto withNotes = songbytes::song (song);
            check ("estimate: 10,000 notes add their size", withNotes - empty == 10000 * sizeof (Note));
            song.patterns[0].notes[0].curves.edit().lane (NoteCurveLane::pitch) = { { 0, 0.0f }, { 96, 1.0f } };
            check ("estimate: a note's curves count", songbytes::song (song) > withNotes + sizeof (NoteCurves));
            song.mixer[1].effects.push_back ({});
            song.mixer[1].effects.back().state.resize (100000);
            check ("estimate: a slot's state counts", songbytes::song (song) > withNotes + 100000);
            song.channels[0].patcher.nodes.resize (3);
            check ("estimate: a Patcher graph's nodes count", songbytes::song (song) > withNotes + 100000 + 3 * sizeof (PatchNode));
        }

        // the cap: the oldest steps go, one always stays, undo / redo still walk what is kept
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.patterns[0].notes.resize (20000); // ~2 MB a step
            engine.setSong (std::move (song));
            const auto step = songbytes::song (engine.getSong());
            engine.setUndoBytesForTest (step * 5 + step / 2);
            for (int i = 0; i < 12; ++i)
                engine.editSong ([i] (Song& s) { s.patterns[0].notes[0].key = 30 + i; });
            const auto kept = engine.getHistoryPosition();
            log << "  step " << juce::String ((double) step / 1048576.0, 2) << " MB, cap 5.5 steps: " << kept << " steps kept, "
                << juce::String ((double) engine.getUndoBytes() / 1048576.0, 2) << " MB\n";
            check ("cap: 5 of 12 steps kept", kept == 5);
            check ("cap: the history's bytes under the cap", engine.getUndoBytes() <= step * 5 + step / 2);
            check ("cap: the newest step undoes to the edit before it", (engine.undo(), engine.getSong().patterns[0].notes[0].key == 40));
            for (int i = 0; i < 10; ++i)
                engine.undo();
            check ("cap: undo stops at the oldest kept step", engine.getSong().patterns[0].notes[0].key == 36 && ! engine.canUndo());
            for (int i = 0; i < 10; ++i)
                engine.redo();
            check ("cap: redo walks back to the last edit", engine.getSong().patterns[0].notes[0].key == 41 && ! engine.canRedo());
            engine.setUndoBytesForTest (1);
            check ("cap: one step always stays", engine.getHistoryPosition() == 1 && engine.canUndo());
            engine.setUndoBytesForTest (std::size_t (1) << 30);
            engine.setUndoLevels (10); // (Settings' least)
            for (int i = 0; i < 14; ++i)
                engine.editSong ([i] (Song& s) { s.patterns[0].notes[0].key = 60 + i; });
            check ("steps: Settings' undo levels still cap the count", engine.getHistoryPosition() == 10);
        }
        return result;
    }
