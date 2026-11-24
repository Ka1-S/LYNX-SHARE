    // --issuesshot190qr <prefix> (round 190qr, issues #274-#281): the pictures for the FL | LYNX check — the marker's menu
    // on its name (#281), the Channel rack at 3/4, 6/4 and 7/8 (#279), the Playlist at FL's deepest zoom with the samples
    // drawn and one step out, the Piano roll at its deepest zoom (#277), and the clip gain handle in view (#274, LYNX
    // extra on). @150: LYNX_SHOT_SCALE=1.5 and LYNX_SHOT_SUFFIX=@150. Included in the app class of Main.cpp.
    bool runIssuesShot190qr (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        using namespace lynx;
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        ui::motion::enabled() = false;
        const auto bar = (std::int64_t) 4 * ppq;
        const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-issuesshot190qr.wav");
        {
            // a plucked tone with a click at 1 s (a cut point): something to see sample by sample
            wav.deleteFile();
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav), 48000.0, 2, 24, {}, 0));
            juce::AudioBuffer<float> buffer (2, 48000 * 12);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto t = (float) i / 48000.0f, local = std::fmod (t, 0.5f);
                const auto v = 0.6f * std::exp (-local * 6.0f) * (std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * t) + 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 1330.0f * t));
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v * 0.8f);
            }
            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }
        auto song = Song::makeDefault();
        song.mode = PlayMode::song;
        song.bpm = 120.0;
        {
            Marker m;
            m.tick = 2 * bar;
            m.name = "Verse";
            song.addMarker (m);
            ChannelDef audio;
            audio.name = "Pluck";
            audio.instrument = InstrumentKind::audioClip;
            audio.samplePath = wav.getFullPathName().toStdString();
            song.channels.push_back (audio);
            Clip c;
            c.kind = ClipKind::audio;
            c.channel = (int) song.channels.size() - 1;
            c.track = 0;
            c.start = bar;
            c.length = 6 * bar;
            song.playlist.push_back (c);
        }
        AudioEngine offline (false);
        offline.setSong (song);
        MainComponent mainUi (offline, false);
        mainUi.setSize (1600, 900);
        offline.dispatchPendingMessages();
        auto& pl = mainUi.getPlaylistForSnapshot();
        pl.setSize (1400, 420);
        offline.getSampleLibrary().load (wav.getFullPathName()); // (as the rack holds it while the song plays)

        // #281: the ruler around the marker, and the menu a right-click on its name opens
        pl.setViewForSnapshot (96.0, 0, 0);
        savePng (shot::snapshot (pl, juce::Rectangle<int> (0, 0, 900, 140)), prefix + "-281-playlist-marker.png");
        const auto markerX = (int) std::lround (pl.xOfTickForSnapshot ((double) (2 * bar)));
        saveMenus (juce::StringArray { "Verse" }, std::vector<juce::PopupMenu> { pl.rulerMenu (markerX + 14, pl.markerTagYForSnapshot()) }, prefix + "-281-marker-menu.png");

        // #279: the rack at three time signatures
        auto& rack = mainUi.getRackForSnapshot();
        for (const auto [b, u] : { std::pair { 3, 4 }, { 6, 4 }, { 7, 8 } })
        {
            offline.editSong ([b = b, u = u] (Song& s) {
                s.beatsPerBar = b;
                s.beatUnit = u;
                s.activePattern().lengthTicks = 0;
                s.activePattern().notes.clear();
            });
            offline.dispatchPendingMessages();
            rack.setSize (900, 300);
            savePng (shot::snapshot (rack), prefix + "-279-rack-" + juce::String (b) + "-" + juce::String (u) + ".png");
        }
        offline.editSong ([] (Song& s) {
            s.beatsPerBar = 4;
            s.beatUnit = 4;
        });
        offline.dispatchPendingMessages();

        // #277: the deepest zoom (8 px a tick) at the 1 s cut point, and one notch of 16 out
        for (const auto [ppt, name] : { std::pair { maxPixelsPerTick, "deepest" }, { maxPixelsPerTick / 16.0, "16x-out" } })
        {
            pl.setViewForSnapshot (ppt * (double) bar, 0, 0);
            const auto width = (double) (pl.getWidth() - pl.headerWidthForSnapshot());
            pl.setScrollTickForSnapshot ((double) (bar + 2 * ppq) - width / ppt / 2.0); // (1 s after the clip's start at 120 BPM)
            savePng (shot::snapshot (pl), prefix + "-277-playlist-" + juce::String (name) + ".png");
        }
        auto& roll = mainUi.getPianoRollForSnapshot();
        roll.setSize (1200, 420);
        juce::MouseWheelDetails up {};
        up.deltaY = 1.0f;
        for (int i = 0; i < 80; ++i)
        {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::Point<float> at (400.0f, 200.0f);
            roll.mouseWheelMove (juce::MouseEvent (source, at, juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &roll, &roll, now, at, now, 1, false), up);
        }
        savePng (shot::snapshot (roll), prefix + "-277-pianoroll-deepest.png");

        // #274: zoomed in on the clip's first bar and a half, its middle out of view; the handle with the extra off / on
        const auto before = cliphandle::gainFollowsView();
        int audioClip = 0;
        for (int i = 0; i < (int) offline.getSong().playlist.size(); ++i)
            if (offline.getSong().playlist[(std::size_t) i].kind == ClipKind::audio)
                audioClip = i;
        pl.setViewForSnapshot (1600.0, 0, 0);
        pl.setScrollTickForSnapshot ((double) bar + 1.5 * (double) bar - (double) (pl.getWidth() - pl.headerWidthForSnapshot()) / (1600.0 / (double) bar));
        for (const auto on : { false, true })
        {
            workflowextras::setExtra ("playlist.gainHandleFollowsView", on);
            pl.hoverFadeForSnapshot (audioClip, 0);
            savePng (shot::snapshot (pl, juce::Rectangle<int> (0, 0, pl.getWidth(), 200)), prefix + "-274-gain-handle-" + (on ? "on" : "off") + ".png");
        }
        workflowextras::setExtra ("playlist.gainHandleFollowsView", before);
        pl.hoverFadeForSnapshot (-1, 0);
        wav.deleteFile();
        return true;
    }
