    // Round 190sa (0.1.32 Q6 + the Simple layout's sound drop). Included in the app class of Main.cpp; --q6190satest <report>
    // [shot prefix] and --selftest "q6190sa".
    //  Q6 files: DAWproject's lossless FLAC (16 / 24-bit only, refused for floats on no grid and for overs; FL's WavPack
    //  sample embedded as FLAC, bit-identical to what LYNX plays; the Ogg one stays WAV), and the .als fixes Live 12.4.16
    //  showed (clips of muted rows deactivated or left out, a track's clips in time order, a "No Output" track's returns).
    //  Q1 G-S1 (QUEUE-0.1.32 item 100): in the Simple layout a sound (one of LYNX's sounds from the Loops panel, or any
    //  instrument from the Plugin database) dragged onto the timeline did nothing, and its hint said "drag it onto the
    //  Channel Rack", which the Simple layout doesn't show. BandLab (bandlab190p/shots/11-sounds-free-loops.png,
    //  14-loop-placed-key-set.png): a loop dropped on the timeline becomes a new track with its region where it was
    //  dropped. Now a sound dropped on the Simple timeline is a new instrument track named after it (what its double-click
    //  makes), its four-bar clip at the bar it was dropped on (the New track chooser's instrument: 190ds), one undo step;
    //  the ghost shows it on the new track's row while it is dragged, and the hint names the timeline. Effects dropped on
    //  the Simple timeline are still refused; the Pattern layout's drop is FL's (on the lanes nothing happens).
    juce::String q6190saCheck (juce::String& log, const juce::String& shots = {})
    {
        using namespace lynx;
        namespace st = simpletracks;
        juce::String result = "OK";
        // (the layout style is global: a check that leaves it on Simple made --selftest's later feel190qe run the Simple
        // layout's edge-scroll law in its Pattern layout; restored whatever happens)
        struct KeepStyle
        {
            layoutstyle::Style was = layoutstyle::current();
            ~KeepStyle() { layoutstyle::setCurrent (was); }
        } keepStyle;
        const auto check = [&] (const juce::String& what, bool ok, const juce::String& detail = {}) {
            log << (ok ? "ok   " : "FAIL ") << what << (detail.isNotEmpty() ? "  [" + detail + "]" : juce::String()) << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what + " " + detail;
        };

        // the hint of a sound in the Loops panel / Plugin database
        {
            const auto simple = BrowserComponent::generatorRowHint (true), pattern = BrowserComponent::generatorRowHint (false);
            check ("Simple hint names the timeline, not the Channel Rack", simple.contains ("timeline") && ! simple.contains ("Channel Rack"), simple);
            check ("Pattern hint keeps the Channel Rack", pattern.contains ("Channel Rack"), pattern);
        }

        // Q6: a sample other apps can't read goes into a DAWproject as a lossless FLAC when its audio is exactly 16- or
        // 24-bit (verified bit for bit), else as 32-bit float WAV
        {
            constexpr int n = 4000;
            std::vector<float> a16 (n), a24 (n), off (n), full (n);
            for (int i = 0; i < n; ++i)
            {
                const auto v = std::sin (0.01 * i) * 0.9;
                a16[(std::size_t) i] = (float) (std::round (v * 32768.0) / 32768.0);
                a24[(std::size_t) i] = (float) (std::round (v * 8388608.0) / 8388608.0);
                off[(std::size_t) i] = (float) v; // (a decoder's floats: on no integer grid)
                full[(std::size_t) i] = a16[(std::size_t) i];
            }
            full[10] = 1.0f; // (an over: FLAC can't hold +1.0)
            const auto flacOf = [] (const std::vector<float>& x, int& bits) {
                const float* planes[1] { x.data() };
                bits = 0;
                return losslessFlacOf (planes, 1, (juce::int64) x.size(), 44100.0, &bits);
            };
            int bits = 0;
            const auto f16 = flacOf (a16, bits);
            check ("FLAC: 16-bit audio, 16 bits, lossless", f16.getSize() > 4 && bits == 16 && std::memcmp (f16.getData(), "fLaC", 4) == 0);
            const auto f24 = flacOf (a24, bits);
            check ("FLAC: 24-bit audio, 24 bits, lossless", f24.getSize() > 4 && bits == 24);
            check ("FLAC refused: floats on no grid (a Vorbis decode)", flacOf (off, bits).getSize() == 0);
            check ("FLAC refused: full scale or over", flacOf (full, bits).getSize() == 0);

            const auto flRoot = juce::SystemStats::getEnvironmentVariable ("LYNX_FL_INSTALL", {});
            const auto wv = juce::File (flRoot).getChildFile ("Data/Patches/Packs/Risers/Riser Noise Metal.wv");
            const auto ogg = juce::File (flRoot).getChildFile ("Data/Patches/Packs/Drums/Percussion/808 Conga.wav");
            if (flRoot.isEmpty() || ! wv.existsAsFile() || ! ogg.existsAsFile())
                log << "skip DAWproject FLAC (no FL installation)\n";
            else
            {
                const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lynx-q6190sa", {}, false);
                folder.createDirectory();
                Song s;
                s.mode = PlayMode::song;
                s.channels.resize (2);
                s.channels[0].name = "Riser";
                s.channels[0].instrument = InstrumentKind::audioClip;
                s.channels[0].samplePath = wv.getFullPathName().toStdString();
                s.channels[1].name = "Conga";
                s.channels[1].instrument = InstrumentKind::audioClip;
                s.channels[1].samplePath = ogg.getFullPathName().toStdString();
                s.playlistTracks.resize (2);
                for (int k = 0; k < 2; ++k)
                {
                    Clip c;
                    c.kind = ClipKind::audio;
                    c.channel = k;
                    c.track = k;
                    c.length = 3840;
                    s.playlist.push_back (c);
                }
                juce::StringArray notes;
                juce::String error;
                const auto out = folder.getChildFile ("riser.dawproject");
                check ("DAWproject export with a WavPack and an Ogg-in-WAV sample", writeDawProjectFile (s, nullptr, out, notes, error), error);
                juce::ZipFile zip (out);
                const auto entry = [&zip] (const juce::String& name) {
                    juce::MemoryBlock m;
                    if (const auto* e = zip.getEntry (name))
                        if (std::unique_ptr<juce::InputStream> in { zip.createStreamForEntry (*e) })
                            in->readIntoMemoryBlock (m);
                    return m;
                };
                const auto flac = entry ("audio/Riser Noise Metal.flac");
                const auto wav = entry ("audio/808 Conga.wav");
                juce::String xml;
                if (const auto x = entry ("project.xml"); x.getSize() > 0)
                    xml = x.toString();
                check ("DAWproject: the WavPack sample embedded as FLAC", flac.getSize() > 4 && std::memcmp (flac.getData(), "fLaC", 4) == 0
                                                                            && xml.contains ("audio/Riser Noise Metal.flac"), juce::String ((int) flac.getSize()));
                check ("DAWproject: the Ogg sample stays a WAV", wav.getSize() > 4 && std::memcmp (wav.getData(), "RIFF", 4) == 0 && xml.contains ("audio/808 Conga.wav"));
                // the null test: the embedded FLAC decodes to exactly what LYNX plays from the .wv
                const auto copy = folder.getChildFile ("riser.flac");
                copy.replaceWithData (flac.getData(), flac.getSize());
                SampleLibrary library;
                const auto source = library.load (wv.getFullPathName()), back = library.load (copy.getFullPathName());
                auto same = source != nullptr && back != nullptr && source->frames() == back->frames() && source->isStereo() == back->isStereo()
                            && source->sampleRate == back->sampleRate
                            && std::memcmp (source->leftData(), back->leftData(), (std::size_t) source->frames() * sizeof (float)) == 0;
                if (same && source->isStereo())
                    same = std::memcmp (source->rightData(), back->rightData(), (std::size_t) source->frames() * sizeof (float)) == 0;
                check ("DAWproject FLAC: bit-identical to the WavPack sample LYNX plays (null test)", same,
                       source != nullptr && back != nullptr ? juce::String (source->frames()) + " / " + juce::String (back->frames()) + " frames" : juce::String ("not decoded"));
                folder.deleteRecursively();
            }
        }

        // Q6: the checked sample copy (the .als export's "Copy them into the project folder") in 4 MB blocks: byte-exact
        // across block edges, the size checked
        {
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lynx-q6190sa-copy", {}, false);
            folder.createDirectory();
            juce::MemoryBlock data ((size_t) (9 << 20) + 12345);
            juce::Random rnd (190);
            for (size_t i = 0; i < data.getSize(); ++i)
                static_cast<char*> (data.getData())[i] = (char) rnd.nextInt (256);
            const auto source = folder.getChildFile ("source.bin"), target = folder.getChildFile ("target.bin");
            source.replaceWithData (data.getData(), data.getSize());
            juce::String why;
            const auto ok = safewrite::copyFile (source, target, why);
            juce::MemoryBlock back;
            target.loadFileAsData (back);
            check ("checked copy: 9 MB + 12,345 bytes copied byte for byte", ok && back == data, why);
            folder.deleteRecursively();
        }

        // Q6: the .als export of clips on a muted Playlist track (Live's Patience: its take lanes, imported as muted rows,
        // played over the main lane in Live): deactivated in the set; an automation clip there is left out, and said
        {
            Song s;
            s.mode = PlayMode::song;
            s.mixer = Song::defaultMixer();
            s.channels.resize (1);
            s.channels[0].name = "Lead";
            s.channels[0].instrument = InstrumentKind::synth;
            s.channels[0].mixerTrack = 1;
            s.patterns.resize (1);
            Note note;
            note.start = 0;
            note.length = 480;
            note.key = 60;
            s.patterns[0].notes.push_back (note);
            s.playlistTracks.resize (3);
            s.playlistTracks[1].muted = true;
            for (int row = 0; row < 2; ++row)
            {
                Clip c;
                c.kind = ClipKind::pattern;
                c.pattern = 0;
                c.track = row;
                c.start = row * 3840;
                c.length = 3840;
                s.playlist.push_back (c);
            }
            Automation vol;
            vol.name = "Lead volume";
            vol.target = { AutomationTargetKind::mixerVolume, 1, 0, 0 };
            AutomationPoint p0, p1;
            p0.tick = 0, p0.value = 0.2f;
            p1.tick = 3840, p1.value = 0.9f;
            vol.points = { p0, p1 };
            s.automations.push_back (vol);
            Clip ac;
            ac.kind = ClipKind::automation;
            ac.automation = 0;
            ac.track = 1; // (on the muted row)
            ac.length = 3840;
            s.playlist.push_back (ac);
            const auto r = exportAls (s);
            const auto& xml = r.xml;
            const auto count = [&xml] (const std::string& what) {
                int n = 0;
                for (auto at = xml.find (what); at != std::string::npos; at = xml.find (what, at + 1))
                    ++n;
                return n;
            };
            // the two MIDI clips: the second (the muted row's) deactivated
            const auto first = xml.find ("<MidiClip "), second = first == std::string::npos ? first : xml.find ("<MidiClip ", first + 1);
            const auto disabledIn = [&xml] (std::size_t at) {
                const auto d = xml.find ("<Disabled Value=\"", at);
                return d != std::string::npos && xml.compare (d + 17, 4, "true") == 0;
            };
            check (".als: both clips written", first != std::string::npos && second != std::string::npos);
            check (".als: the main row's clip plays", first != std::string::npos && ! disabledIn (first));
            check (".als: the muted row's clip is deactivated (Live: shown, silent)", second != std::string::npos && disabledIn (second));
            const auto listed = std::any_of (r.notes.begin(), r.notes.end(), [] (const std::string& n) { return n.find ("Lead volume") != std::string::npos; });
            auto playing = s;
            playing.playlist.back().track = 0; // (the same automation clip on the playing row)
            const auto withEnvelope = exportAls (playing).xml;
            int envelopes = 0;
            for (auto at = withEnvelope.find ("<AutomationEnvelope Id="); at != std::string::npos; at = withEnvelope.find ("<AutomationEnvelope Id=", at + 1))
                ++envelopes;
            check (".als: the muted row's automation clip is left out and listed (on a playing row: an envelope)",
                   count ("<AutomationEnvelope Id=") == envelopes - 1 && listed, juce::String (count ("<AutomationEnvelope Id=")) + " / " + juce::String (envelopes));
        }

        // Q6: Live deletes a clip written after a later-starting one and cuts overlapped ones (its "Repair Track" on load):
        // a track's clips go in time order, a muted lane's clip that would overlap a playing one is left out (listed)
        {
            Song s;
            s.mode = PlayMode::song;
            s.mixer = Song::defaultMixer();
            s.channels.resize (1);
            s.channels[0].name = "Keys";
            s.channels[0].instrument = InstrumentKind::synth;
            s.channels[0].mixerTrack = 1;
            s.patterns.resize (1);
            Note note;
            note.length = 480;
            note.key = 60;
            s.patterns[0].notes.push_back (note);
            s.playlistTracks.resize (3);
            s.playlistTracks[2].muted = true; // (a take lane)
            const auto add = [&s] (int row, std::int64_t start) {
                Clip c;
                c.kind = ClipKind::pattern;
                c.pattern = 0;
                c.track = row;
                c.start = start;
                c.length = 3840;
                s.playlist.push_back (c);
            };
            add (0, 3 * 3840); // (Playlist order: the later clips first)
            add (1, 1 * 3840);
            add (0, 0);
            add (2, 0);        // the muted lane's clip under the first one: left out
            add (2, 2 * 3840); // the muted lane's clip in a gap: kept, deactivated
            const auto r = exportAls (s);
            std::vector<std::pair<double, std::string>> clips; // (start, Disabled)
            for (auto at = r.xml.find ("<MidiClip "); at != std::string::npos; at = r.xml.find ("<MidiClip ", at + 1))
            {
                const auto st = r.xml.find ("<CurrentStart Value=\"", at), dis = r.xml.find ("<Disabled Value=\"", at);
                if (st == std::string::npos || dis == std::string::npos)
                    break;
                clips.emplace_back (std::atof (r.xml.c_str() + st + 21), r.xml.substr (dis + 17, 4));
            }
            juce::String seen;
            for (const auto& [t, d] : clips)
                seen << juce::String (t, 0) << (d == "true" ? "(off) " : " ");
            const auto sorted = std::is_sorted (clips.begin(), clips.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
            check (".als: a track's clips in time order, the muted lane's overlapping clip left out, the one in a gap deactivated",
                   clips.size() == 4 && sorted && clips[2].first == 8.0 && clips[2].second == "true", seen.trim());
            check (".als: the left-out muted clip is listed",
                   std::any_of (r.notes.begin(), r.notes.end(), [] (const std::string& n) { return n.find ("Muted clips that overlap") != std::string::npos; }));
        }

        // Q6: a "No Output" track of a Live set (it plays through its sends only; LYNX: not to the Master, sends to Live
        // returns): its first send, to a return at -inf, made that return a group of it in the .als (a2-sends: the Reverb
        // return came back as a group, s1's send automation was dropped)
        {
            Song s;
            s.mode = PlayMode::song;
            s.mixer = Song::defaultMixer (4);
            s.mixer[3].name = "Reverb";
            s.mixer[3].liveReturn = true;
            s.mixer[4].name = "Delay";
            s.mixer[4].liveReturn = true;
            s.mixer[1].name = "s1";
            s.mixer[1].toMaster = false;
            s.mixer[1].sends = { { 3, 0.0f }, { 4, 1.0f } };
            s.channels.resize (1);
            s.channels[0].name = "s1";
            s.channels[0].instrument = InstrumentKind::synth;
            s.channels[0].mixerTrack = 1;
            s.patterns.resize (1);
            Note note;
            note.length = 480;
            note.key = 60;
            s.patterns[0].notes.push_back (note);
            s.playlistTracks.resize (2);
            Clip c;
            c.kind = ClipKind::pattern;
            c.pattern = 0;
            c.length = 3840;
            s.playlist.push_back (c);
            Automation send;
            send.name = "s1 send A";
            send.target = { AutomationTargetKind::mixerSend, 1, 3, 0 };
            AutomationPoint p0, p1;
            p0.tick = 0, p0.value = 0.0f;
            p1.tick = 3840, p1.value = 0.8f;
            send.points = { p0, p1 };
            s.automations.push_back (send);
            Clip ac;
            ac.kind = ClipKind::automation;
            ac.automation = 0;
            ac.track = 1;
            ac.length = 3840;
            s.playlist.push_back (ac);
            const auto r = exportAls (s);
            const auto has = [&r] (const std::string& what) { return r.xml.find (what) != std::string::npos; };
            int returns = 0;
            for (auto at = r.xml.find ("<ReturnTrack "); at != std::string::npos; at = r.xml.find ("<ReturnTrack ", at + 1))
                ++returns;
            check (".als: a No Output track's returns stay returns (no group)", returns == 2 && ! has ("<GroupTrack "), juce::String (returns) + " returns");
            check (".als: its output is None", has ("AudioOut/None"));
            const auto dropped = std::any_of (r.notes.begin(), r.notes.end(), [] (const std::string& n) { return n.find ("s1 send A") != std::string::npos; });
            check (".als: its send automation is exported", ! dropped && has ("<AutomationEnvelope Id="));
        }

        const auto& sounds = soundlib::builtIn();
        if (sounds.empty())
        {
            check ("LYNX's built-in sounds", false);
            return result;
        }
        const auto sound = sounds.front();
        const auto label = juce::String::fromUTF8 (sound.name.c_str());
        BrowserComponent::setDraggedPluginForCheck ([sound]() { return soundlib::channelFor (sound); }, nullptr);
        const juce::var drag (juce::String (BrowserComponent::generatorDragDescription) + "|" + label);
        const auto w = 1600, h = 900;

        const auto makeSong = [] {
            Song s;
            s.mixer = Song::defaultMixer();
            s.bpm = 120.0;
            return s;
        };
        {
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (w, h);
            const auto settle = [&] {
                for (int k = 0; k < 3; ++k)
                    offline.dispatchPendingMessages();
            };
            ui.selectTabForSnapshot (2);
            ui.setLayoutStyle (layoutstyle::Style::simple);
            settle();
            auto* simple = ui.getSimpleWorkflowForSnapshot();
            auto& playlist = ui.getPlaylistForSnapshot();
            check ("a Simple workflow", simple != nullptr);
            if (simple == nullptr)
                return result;
            offline.setSong (makeSong());
            settle();
            simple->hideChooser();
            // one instrument track already there (the drop lane is the next row)
            offline.editSong ([] (Song& s) {
                st::Request q;
                q.kind = st::Kind::instrument;
                q.name = "Keys";
                ChannelDef synth;
                synth.instrument = InstrumentKind::synth;
                q.channel = synth;
                q.emptyBars = 4;
                st::addTrack (s, q);
            });
            settle();
            const auto& song0 = offline.getSong();
            const auto bar = song0.barTicks();
            const auto first = 0;
            const auto lane = st::nextEmptyTrack (song0);
            check ("one track, the lane below it", st::kindOf (song0, first).has_value() && lane == 1, "lane " + juce::String (lane));

            const juce::DragAndDropTarget::SourceDetails details (drag, nullptr, {});
            check ("Simple: the timeline takes a sound", playlist.isInterestedInDragSource (details));
            BrowserComponent::setDraggedPluginForCheck (nullptr, [] { return std::optional<EffectSlot> (makeEffectSlot (EffectKind::eq)); });
            const juce::var fxDrag (juce::String (BrowserComponent::effectDragDescription) + "|EQ");
            check ("Simple: an effect is still refused on the timeline", ! playlist.isInterestedInDragSource ({ fxDrag, nullptr, {} }));
            BrowserComponent::setDraggedPluginForCheck ([sound]() { return soundlib::channelFor (sound); }, nullptr);

            // dragged over the first track at bar 5: the ghost on the lane's row (where the new track goes), the name tag
            const auto x5 = (int) playlist.xOfTickForSnapshot (4.0 * (double) bar) + 4;
            playlist.dragOverForSnapshot (drag, { x5, playlist.trackRowForSnapshot (first).getCentreY() });
            const auto ghost = playlist.dropGhostAreaForCheck();
            const auto laneRow = playlist.trackRowForSnapshot (lane);
            check ("ghost while dragged, on the new track's row", ! ghost.isEmpty() && ghost.intersects (laneRow) && playlist.dropGhostReasonForCheck().isEmpty(),
                   ghost.toString() + " lane " + laneRow.toString());
            if (shots.isNotEmpty())
                savePng (shot::snapshot (ui), shots + "-simple-sound-drag.png");
            playlist.dragOverForSnapshot ({}, {});
            check ("after the drag nothing is left to paint", playlist.dropGhostAreaForCheck().isEmpty());

            // dropped: a new instrument track named after the sound, its clip at bar 5, four bars, one undo step
            const auto history = offline.getHistorySize();
            const auto tracksBefore = st::nextEmptyTrack (offline.getSong());
            playlist.dropForSnapshot (drag, { x5, playlist.trackRowForSnapshot (first).getCentreY() });
            settle();
            const auto& song = offline.getSong();
            const auto made = tracksBefore;
            const auto kind = st::kindOf (song, made);
            check ("dropped: a new instrument track", kind.has_value() && *kind == st::Kind::instrument && st::nextEmptyTrack (song) == made + 1);
            const auto name = made < (int) song.playlistTracks.size() ? juce::String::fromUTF8 (song.playlistTracks[(std::size_t) made].name.c_str()) : juce::String();
            check ("named after the sound", name == label, name + " / " + label);
            const Clip* clip = nullptr;
            for (const auto& c : song.playlist)
                if (c.track == made && c.kind == ClipKind::pattern)
                    clip = &c;
            check ("its clip starts at bar 5, four bars long", clip != nullptr && clip->start == 4 * bar && clip->length == 4 * bar,
                   clip != nullptr ? juce::String (clip->start) + "+" + juce::String (clip->length) : juce::String ("none"));
            bool soundKept = false;
            if (const auto expected = soundlib::channelFor (sound))
                for (const auto& c : song.channels)
                    soundKept = soundKept || (c.instrument == expected->instrument && juce::String::fromUTF8 (c.name.c_str()) == label);
            check ("the track plays the sound", soundKept);
            check ("one undo step", offline.getHistorySize() == history + 1, juce::String (history) + " -> " + juce::String (offline.getHistorySize()));
            if (shots.isNotEmpty())
                savePng (shot::snapshot (ui), shots + "-simple-sound-dropped.png");
            offline.undo();
            settle();
            check ("undo removes it", st::nextEmptyTrack (offline.getSong()) == made && ! st::kindOf (offline.getSong(), made).has_value());

            // on the track headers: at the song's start
            playlist.dropForSnapshot (drag, { 20, playlist.trackRowForSnapshot (first).getCentreY() });
            settle();
            const Clip* atStart = nullptr;
            for (const auto& c : offline.getSong().playlist)
                if (c.track == made && c.kind == ClipKind::pattern)
                    atStart = &c;
            check ("dropped on the headers: its clip at the start", atStart != nullptr && atStart->start == 0);
            ui.setLayoutStyle (layoutstyle::Style::pattern); // (back, as a user leaving Simple: the global style with it)
            check ("back in the Pattern layout", layoutstyle::current() == layoutstyle::Style::pattern);
        }

        // the Pattern layout: FL's (a plugin on the lanes does nothing; on the headers it goes to the track's channel)
        {
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (w, h);
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.setSong (makeSong());
            for (int k = 0; k < 3; ++k)
                offline.dispatchPendingMessages();
            auto& playlist = ui.getPlaylistForSnapshot();
            const auto before = offline.getSong().channels.size();
            const auto history = offline.getHistorySize();
            playlist.dropForSnapshot (drag, { (int) playlist.xOfTickForSnapshot (4.0 * (double) offline.getSong().barTicks()) + 4,
                                              playlist.trackRowForSnapshot (0).getCentreY() });
            for (int k = 0; k < 3; ++k)
                offline.dispatchPendingMessages();
            check ("Pattern: a sound on the lanes changes nothing (FL)", offline.getSong().channels.size() == before && offline.getHistorySize() == history);
        }
        BrowserComponent::setDraggedPluginForCheck (nullptr, nullptr);
        return result;
    }
