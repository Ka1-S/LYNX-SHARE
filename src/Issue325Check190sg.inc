    // Round 190sg (issue #325, 0.1.32). Included in the app class of Main.cpp; --issue325test <report> [shot prefix] and
    // --selftest "issue325".
    //  FL 2026 (checked live, frames in the round's raw/fl*.png; FLEngine FUN_00e73950, undo "replace audio clip sample"):
    //  ONE sample dragged from the Browser onto an audio clip of the Playlist loads into that clip's channel: every clip of
    //  the channel plays the new file, the channel (its insert, effects, levels) stays, no clip is added; whole clips show
    //  the whole new file, a trimmed one keeps its length, fades stay. Shift (or several files) adds a clip where it is
    //  dropped instead; on a pattern clip nothing happens. LYNX made a new clip on top of the old one. Now the Pattern layout
    //  does as FL; while the drag is over the clip, its channel's clips are the ghost and the hint bar says what the drop
    //  does. The Clip-launch layout keeps Live's new clip where it is dropped (Live 12 checked: no sample replacement).
    juce::String issue325Check (juce::String& log, const juce::String& shots = {})
    {
        using namespace lynx;
        juce::String result = "OK";
        struct KeepStyle
        {
            layoutstyle::Style was = layoutstyle::current();
            ~KeepStyle()
            {
                layoutstyle::setCurrent (was);
                PlaylistComponent::shiftDropForCheck = false;
            }
        } keepStyle;
        const auto check = [&] (const juce::String& what, bool ok, const juce::String& detail = {}) {
            log << (ok ? "ok   " : "FAIL ") << what << (detail.isNotEmpty() ? "  [" + detail + "]" : juce::String()) << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what + " " + detail;
        };

        // two sample files: "Old loop" 2 s (4 beats at 120 BPM), "New hit" 1 s (2 beats)
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-issue325-" + juce::String (juce::Time::currentTimeMillis()));
        dir.createDirectory();
        const struct Cleanup
        {
            juce::File d;
            ~Cleanup() { d.deleteRecursively(); }
        } cleanup { dir };
        const auto writeWav = [] (const juce::File& f, double seconds, float hz) {
            const int rate = 44100, n = (int) (seconds * rate);
            juce::AudioBuffer<float> b (1, n);
            for (int i = 0; i < n; ++i)
                b.setSample (0, i, 0.4f * std::sin (juce::MathConstants<float>::twoPi * hz * (float) i / (float) rate) * (1.0f - (float) (i % 11025) / 11025.0f));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out (f.createOutputStream().release());
            if (out == nullptr)
                return false;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), rate, 1, 16, {}, 0));
            if (w == nullptr)
                return false;
            out.release();
            return w->writeFromAudioSampleBuffer (b, 0, n);
        };
        const auto oldFile = dir.getChildFile ("Old loop.wav"), newFile = dir.getChildFile ("New hit.wav");
        check ("the test samples", writeWav (oldFile, 2.0, 110.0f) && writeWav (newFile, 1.0, 220.0f));

        const auto makeSong = [&] {
            Song s;
            s.mixer = Song::defaultMixer();
            s.bpm = 120.0;
            const auto bar = s.barTicks();
            for (int i = 0; i < 2; ++i)
            {
                ChannelDef c;
                c.instrument = InstrumentKind::audioClip;
                c.name = i == 0 ? "Old loop" : "Other";
                c.samplePath = (i == 0 ? oldFile : newFile).getFullPathName().toStdString();
                c.mixerTrack = 3 + i;
                s.channels.push_back (c);
            }
            s.mixer[3].effects.push_back (makeEffectSlot (EffectKind::eq));
            const auto audio = [&s] (int channel, int track, std::int64_t start, std::int64_t length) {
                Clip c;
                c.kind = ClipKind::audio;
                c.channel = channel;
                c.track = track;
                c.start = start;
                c.length = length;
                s.playlist.push_back (c);
            };
            audio (0, 0, bar, bar);         // whole (4 beats)
            audio (0, 1, bar, bar);         // whole, with a fade out
            s.playlist.back().fadeOut = ppq;
            audio (0, 2, 3 * bar, 3 * bar / 4); // trimmed
            audio (1, 3, bar, bar / 2);     // another channel
            Pattern p;
            p.name = "Pattern 1";
            Note n;
            n.channel = 1;
            n.length = ppq;
            p.notes.push_back (n);
            s.patterns = { p };
            Clip pc;
            pc.kind = ClipKind::pattern;
            pc.track = 4;
            pc.start = bar;
            pc.length = bar;
            s.playlist.push_back (pc);
            s.mode = PlayMode::song;
            return s;
        };
        const juce::var drag (juce::String (BrowserComponent::dragDescription) + "|" + newFile.getFullPathName());
        const auto w = 1600, h = 900;

        for (const auto style : { layoutstyle::Style::pattern, layoutstyle::Style::clipLaunch })
        {
            const auto pattern = style == layoutstyle::Style::pattern;
            const juce::String tag = pattern ? "Pattern: " : "Clip-launch: ";
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (w, h);
            ui.setLayoutStyle (style);
            const auto settle = [&] {
                for (int k = 0; k < 3; ++k)
                    offline.dispatchPendingMessages();
            };
            offline.getSampleLibrary().load (oldFile.getFullPathName()); // (the clips' waveforms, as in a project that plays)
            offline.getSampleLibrary().load (newFile.getFullPathName());
            offline.setSong (makeSong());
            settle();
            ui.selectTabForSnapshot (2);
            settle();
            auto& playlist = ui.getPlaylistForSnapshot();
            const auto bar = offline.getSong().barTicks();
            const auto at = [&] (int track, double bars) {
                return juce::Point<int> ((int) playlist.xOfTickForSnapshot (bars * (double) bar) + 4, playlist.trackRowForSnapshot (track).getCentreY());
            };
            const auto onClip = at (0, 1.5);
            const auto target = playlist.sampleDropTargetForCheck (onClip, newFile);
            check (tag + "the drop target over the audio clip", pattern ? target == 0 : target < 0, juce::String (target));

            // while dragged over the clip: the ghost is the channel's three clips, the hint says what the drop does
            ui::clearPostedHint();
            playlist.dragOverForSnapshot (drag, onClip);
            const auto& ghost = playlist.dropGhostForCheck();
            if (pattern)
            {
                const auto marks = ghost.size() == 3 && std::all_of (ghost.begin(), ghost.end(), [] (const Clip& c) { return c.channel == 0; });
                check (tag + "the ghost marks the channel's clips", marks, juce::String ((int) ghost.size()));
                const auto hint = ui::postedHint();
                check (tag + "the hint names the channel and the file", hint && hint->text.contains ("Old loop") && hint->text.contains ("New hit"),
                       hint ? hint->text : juce::String ("none"));
                if (shots.isNotEmpty())
                {
                    // (the clips' waveforms are read on a worker when first painted: one paint, then the pictures)
                    ui.createComponentSnapshot (ui.getLocalBounds());
                    ui::WaveformCache::getInstance()->waitUntilIdle (5000);
                    settle();
                    if (hint)
                        ui.setHintForSnapshot (*hint);
                    savePng (ui.createComponentSnapshot (ui.getLocalBounds()), shots + "-drag-over-clip.png");
                    savePng (ui.createComponentSnapshot (ui.getLocalBounds(), true, 1.5f), shots + "-drag-over-clip@150.png");
                }
            }
            else
                check (tag + "a new clip's ghost", ghost.size() == 1 && ghost.front().channel < 0, juce::String ((int) ghost.size()));
            playlist.dragOverForSnapshot ({}, {});
            check (tag + "after the drag nothing is left to paint", playlist.dropGhostAreaForCheck().isEmpty());

            // dropped on the clip
            const auto history = offline.getHistorySize();
            const auto clipsBefore = offline.getSong().playlist.size();
            const auto channelsBefore = offline.getSong().channels.size();
            playlist.dropForSnapshot (drag, onClip);
            settle();
            {
                const auto& s = offline.getSong();
                check (tag + "one undo step", offline.getHistorySize() == history + 1, juce::String (history) + " -> " + juce::String (offline.getHistorySize()));
                if (pattern)
                {
                    const auto& c = s.channels[0];
                    check (tag + "no clip, no channel added", s.playlist.size() == clipsBefore && s.channels.size() == channelsBefore);
                    check (tag + "the channel plays the new file, named after it", juce::String::fromUTF8 (c.samplePath.c_str()) == newFile.getFullPathName()
                                                                                       && juce::String::fromUTF8 (c.name.c_str()) == "New hit");
                    check (tag + "its insert and effects stay", c.mixerTrack == 3 && s.mixer[3].effects.size() == 1);
                    check (tag + "whole clips show the whole new file", s.playlist[0].length == bar / 2 && s.playlist[1].length == bar / 2 && s.playlist[0].start == bar,
                           juce::String (s.playlist[0].length) + " / " + juce::String (s.playlist[1].length));
                    check (tag + "fades stay, the trimmed clip keeps its length", s.playlist[1].fadeOut == ppq && s.playlist[2].length == 3 * bar / 4);
                    check (tag + "the other channel is untouched", juce::String::fromUTF8 (s.channels[1].name.c_str()) == "Other");
                    if (shots.isNotEmpty())
                    {
                        ui.setHintForSnapshot ({});
                        savePng (ui.createComponentSnapshot (ui.getLocalBounds()), shots + "-dropped.png");
                        savePng (ui.createComponentSnapshot (ui.getLocalBounds(), true, 1.5f), shots + "-dropped@150.png");
                    }
                }
                else
                    check (tag + "a new clip where it was dropped (Live)", s.playlist.size() == clipsBefore + 1
                                                                                && juce::String::fromUTF8 (s.channels[0].samplePath.c_str()) == oldFile.getFullPathName());
            }
            offline.undo();
            settle();
            check (tag + "undo restores it", offline.getSong().playlist.size() == clipsBefore
                                                && juce::String::fromUTF8 (offline.getSong().channels[0].samplePath.c_str()) == oldFile.getFullPathName()
                                                && offline.getSong().playlist[0].length == bar);
            if (! pattern)
                continue;

            // Shift: a new clip where it is dropped (FL)
            PlaylistComponent::shiftDropForCheck = true;
            playlist.dropForSnapshot (drag, onClip);
            settle();
            PlaylistComponent::shiftDropForCheck = false;
            check (tag + "Shift adds a clip instead", offline.getSong().playlist.size() == clipsBefore + 1
                                                          && juce::String::fromUTF8 (offline.getSong().channels[0].samplePath.c_str()) == oldFile.getFullPathName());
            offline.undo();
            settle();

            // on the pattern clip: nothing (FL)
            const auto h2 = offline.getHistorySize();
            ui::clearPostedHint();
            playlist.dragOverForSnapshot (drag, at (4, 1.5));
            const auto hint = ui::postedHint();
            check (tag + "over a pattern clip: no ghost, the hint says so", playlist.dropGhostForCheck().empty() && hint && hint->text.contains ("pattern clip"),
                   hint ? hint->text : juce::String ("none"));
            playlist.dragOverForSnapshot ({}, {});
            playlist.dropForSnapshot (drag, at (4, 1.5));
            settle();
            check (tag + "a pattern clip takes nothing", offline.getSong().playlist.size() == clipsBefore && offline.getHistorySize() == h2);

            // on an empty spot: a new clip, as before
            playlist.dropForSnapshot (drag, at (5, 6.0));
            settle();
            check (tag + "an empty spot gets a new clip", offline.getSong().playlist.size() == clipsBefore + 1);
        }
        return result;
    }
