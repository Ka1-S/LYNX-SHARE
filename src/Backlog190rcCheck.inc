    // Round 190rc (0.1.32 quality release): the backlog items through the real components, off screen. Included in the
    // app class of Main.cpp; --backlog190rctest <report> and --selftest "backlog190rc".
    //   #288 FL presets of LYNX's native generators: the Browser opens the generator itself (not LYNX's synth), the
    //        channel window's preset menu loads them, for every engine with a preset folder (FL installed) or a LYNX bank
    juce::String backlog190rcCheck (juce::String& log)
    {
        using namespace lynx;
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::String result = "OK";
        const auto check = [&] (const juce::String& what, bool ok) {
            log << (ok ? "ok   " : "FAIL ") << what << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what;
        };
        const auto plain = [] (juce::String t) { return t.upToFirstOccurrenceOf ("\t", false, false).removeCharacters ("&"); };
        // the menu's preset rows (sub-menus walked), in order, with their actions
        std::function<void (const juce::PopupMenu&, std::vector<juce::PopupMenu::Item>&)> rows;
        rows = [&rows, &plain] (const juce::PopupMenu& menu, std::vector<juce::PopupMenu::Item>& out) {
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            {
                const auto& item = it.getItem();
                if (item.subMenu != nullptr)
                    rows (*item.subMenu, out);
                else if (! item.isSeparator && ! item.isSectionHeader && item.isEnabled && item.action != nullptr
                         && plain (item.text) != tr ("Save preset...") && ! plain (item.text).startsWith (tr ("Delete preset \"{0}\"", { "" }).upToFirstOccurrenceOf ("\"", false, false))
                         && plain (item.text) != tr ("Show the presets folder"))
                    out.push_back (item);
            }
        };

        // ---- #288: every native generator's presets
        {
            const auto& fl = flStudioAccess();
            const auto flHere = ! fl.roots.data.empty();
            log << "FL Studio data: " << (flHere ? juce::String (fl.roots.data) : juce::String ("(none)")) << "\n";
            int engines = 0, browserOk = 0, menuOk = 0;
            for (const auto& e : natives::instrumentEngines())
            {
                const auto folder = natives::presetFolderOf (e.kind);
                const auto files = folder.empty() ? std::vector<std::string>() : natives::presetCandidates (folder, true, fl);
                if (files.empty() && ! banks::hasBank (e.kind))
                    continue;
                auto made = natives::makeChannel (e.kind, fl);
                if (! made)
                    continue;
                ++engines;
                const juce::String name (e.name);
                // the Browser's double-click: a preset file of the folder (not the Plugin database entry) opens this engine
                // every preset file of the folder (up to 120 per engine): how many open the engine itself; the rest
                // (old states LYNX doesn't read) still open as before (LYNX's synth approximation, or a message)
                {
                    std::vector<std::string> own;
                    for (const auto& f : files)
                        if (f.find ("Plugin database") == std::string::npos)
                            own.push_back (f);
                    int read = 0, tried = 0;
                    std::string firstMiss;
                    for (std::size_t k = 0; k < own.size() && tried < 120; k += std::max<std::size_t> (1, own.size() / 120), ++tried)
                    {
                        const auto c = natives::channelFromAnyPreset (own[k], "x", fl);
                        if (c && c->instrument == e.kind)
                            ++read;
                        else if (firstMiss.empty())
                            firstMiss = own[k];
                    }
                    log << "     " << name << ": " << read << " / " << tried << " presets open " << name
                        << (firstMiss.empty() ? juce::String() : "  (first not read: " + juce::String::fromUTF8 (firstMiss.c_str()) + ")") << "\n";
                }
                if (! files.empty() && std::any_of (files.begin(), files.end(), [] (const std::string& f) { return f.find ("Plugin database") == std::string::npos; }))
                {
                    std::string pick;
                    for (const auto& f : files)
                        if (f.find ("Plugin database") == std::string::npos && juce::File (juce::String::fromUTF8 (f.c_str())).getFileNameWithoutExtension() != "Default")
                        {
                            pick = f;
                            break;
                        }
                    if (pick.empty())
                        pick = files.back();
                    // (the first one the engine reads: a few old states are not read, see the counts above)
                    for (const auto& f : files)
                        if (f.find ("Plugin database") == std::string::npos && natives::channelFromAnyPreset (f, "x", fl).has_value())
                        {
                            pick = f;
                            break;
                        }
                    const auto presetName = juce::File (juce::String::fromUTF8 (pick.c_str())).getFileNameWithoutExtension().toStdString();
                    const auto c = natives::channelFromAnyPreset (pick, presetName, fl);
                    const auto ok = c.has_value() && c->instrument == e.kind && c->name == presetName;
                    browserOk += ok ? 1 : 0;
                    check ("288 browser opens " + name + " for " + juce::String::fromUTF8 (presetName.c_str()), ok);
                }
                // the channel window's preset menu (its title bar and the wrapper's Presets): a row loads its preset
                AudioEngine engine (false);
                engine.editSong ([&made] (Song& s) {
                    s.channels.clear();
                    s.channels.push_back (*made);
                    s.channels.back().mixerTrack = 1;
                });
                auto frame = makeChannelFrame (engine, 0, 0);
                frame->setSize (juce::jmax (500, frame->getWidth()), juce::jmax (300, frame->getHeight()));
                const auto menu = channelPresetMenu (*frame);
                std::vector<juce::PopupMenu::Item> items;
                if (menu)
                    rows (*menu, items);
                bool loaded = false;
                juce::String tried;
                for (std::size_t k = 1; k < items.size() && k < 4 && ! loaded; ++k) // (the 2nd, 3rd, 4th row: never the one shown now)
                {
                    const auto before = engine.getSong().channels[0];
                    tried = plain (items[k].text);
                    items[k].action();
                    const auto& after = engine.getSong().channels[0];
                    loaded = after.instrument == e.kind && juce::String::fromUTF8 (after.name.c_str()) == tried
                             && ! genpresets::sameSound (before, after);
                }
                if (items.size() < 2) // (only FL's Default here: 3x Osc's presets are channel presets, MiniSynth's .flms)
                {
                    log << "     " << name << ": " << (int) items.size() << " preset row(s) in its window menu, nothing to step to\n";
                    continue;
                }
                menuOk += loaded ? 1 : 0;
                check ("288 window menu loads " + name + " preset \"" + tried + "\" (" + juce::String ((int) items.size()) + " rows)", loaded);
            }
            log << "engines " << engines << ", browser " << browserOk << ", menus " << menuOk << "\n";
            check ("288 engines found", engines > 0);
            if (flHere)
            {
                // a channel preset of FL's "Channel presets\3x Osc": a 3x Osc channel, no longer LYNX's synth
                const auto threeOsc = juce::File (juce::String::fromUTF8 (fl.roots.data.c_str())).getChildFile ("Patches/Channel presets/3x Osc");
                const auto any = threeOsc.findChildFiles (juce::File::findFiles, true, "*.fst");
                if (! any.isEmpty())
                {
                    const auto c = natives::channelFromAnyPreset (any[0].getFullPathName().toStdString(), any[0].getFileNameWithoutExtension().toStdString(), fl);
                    check ("288 3x Osc channel preset opens 3x Osc", c.has_value() && c->instrument == InstrumentKind::threeosc);
                }
            }
        }
        const auto bar = (std::int64_t) 4 * ppq;
        const auto layoutBefore = layoutstyle::current();
        const auto mouseOn = [] (juce::Component& target, juce::Point<int> p, juce::ModifierKeys mods) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, p.toFloat(), now, 1, false);
        };

        // ---- #287: a .mid on the Playlist (Pattern layout): FL takes none there (desktop); the LYNX extra makes a clip
        {
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
            const auto mid = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-backlog190rc-loop.mid");
            {
                juce::MidiMessageSequence seq;
                for (int i = 0; i < 4; ++i)
                {
                    seq.addEvent (juce::MidiMessage::noteOn (1, 60 + i, (juce::uint8) 100), i * 96.0);
                    seq.addEvent (juce::MidiMessage::noteOff (1, 60 + i), i * 96.0 + 90.0);
                }
                seq.updateMatchedPairs();
                juce::MidiFile file;
                file.setTicksPerQuarterNote (96);
                file.addTrack (seq);
                mid.deleteFile();
                juce::FileOutputStream out (mid);
                file.writeTo (out);
            }
            AudioEngine engine (false);
            engine.setSong (Song::makeDefault());
            PlaylistComponent playlist (engine);
            playlist.setBounds (0, 0, 1400, 500);
            playlist.setVisible (true);
            playlist.setViewForSnapshot (40.0, 0, 0);
            const auto kept = mididrop::playlistDrop();
            mididrop::setPlaylistDrop (false);
            check ("287 extra off: the Playlist leaves a .mid to the window (FL)", ! playlist.isInterestedInFileDrag ({ mid.getFullPathName() }));
            mididrop::setPlaylistDrop (true);
            check ("287 extra on: the Playlist takes a .mid", playlist.isInterestedInFileDrag ({ mid.getFullPathName() }));
            const auto clipsBefore = engine.getSong().playlist.size();
            const auto patternsBefore = engine.getSong().patterns.size();
            const auto y = playlist.trackRowForSnapshot (1).getY() + 4;
            const auto x = (int) playlist.xOfTickForSnapshot ((double) (2 * bar)) + 2;
            playlist.filesDropped ({ mid.getFullPathName() }, x, y);
            const auto& song = engine.getSong();
            bool clipThere = false;
            for (const auto& c : song.playlist)
                clipThere = clipThere || (c.kind == ClipKind::pattern && c.track == 1 && c.start == 2 * bar && c.pattern == (int) patternsBefore);
            check ("287 dropped: a pattern clip on track 2 at bar 3 (" + juce::String ((int) song.playlist.size() - (int) clipsBefore) + " clip(s))",
                   song.playlist.size() == clipsBefore + 1 && song.patterns.size() == patternsBefore + 1 && clipThere
                       && song.patterns.back().notes.size() == 4u);
            mididrop::setPlaylistDrop (kept);
            mid.deleteFile();
        }

        // ---- #285: Playlist › Edit › "Cut clips at the song position"
        {
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.patterns.resize (3);
            song.playlist.clear();
            for (int t = 0; t < 3; ++t)
            {
                Clip c;
                c.kind = ClipKind::pattern;
                c.pattern = t;
                c.track = t;
                c.start = 0;
                c.length = 4 * bar;
                song.playlist.push_back (c);
            }
            engine.setSong (std::move (song));
            PlaylistComponent playlist (engine);
            playlist.setBounds (0, 0, 1400, 500);
            playlist.setVisible (true);
            playlist.setPlayheadForSnapshot ((double) (2 * bar));
            playlist.selectForSnapshot ({ 1 });
            check ("285 action cuts the selected clip only", playlist.runAction ("playlist.splitAtPlayhead") && engine.getSong().playlist.size() == 4u);
            playlist.selectForSnapshot ({});
            playlist.runAction ("playlist.splitAtPlayhead");
            check ("285 no selection: every clip there", engine.getSong().playlist.size() == 6u);
            engine.undo();
            engine.undo();
            check ("285 one undo step each", engine.getSong().playlist.size() == 3u);
        }

        // ---- Discord (sidthenugget): the Simple header's instrument icon opens the instrument's window
        {
            layoutstyle::setCurrent (layoutstyle::Style::simple);
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.playlist.clear();
            simpletracks::Request request;
            request.kind = simpletracks::Kind::instrument;
            ChannelDef synth;
            synth.instrument = InstrumentKind::synth;
            request.channel = synth;
            const auto added = simpletracks::addTrack (song, request);
            engine.setSong (std::move (song));
            PlaylistComponent playlist (engine);
            playlist.setSimpleHeaders (true);
            playlist.setBounds (0, 0, 1400, 500);
            playlist.setVisible (true);
            int opened = -1;
            playlist.onToggleChannelWindow = [&opened] (int channel) { opened = channel; };
            (void) playlist.createComponentSnapshot (playlist.getLocalBounds()); // (paint places the icons)
            const auto& boxes = playlist.simpleIconTargets();
            const auto it = boxes.find (added.track);
            check ("simple icon painted", added.ok() && it != boxes.end());
            if (it != boxes.end())
            {
                playlist.mouseDown (mouseOn (playlist, it->second.getCentre(), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
                playlist.mouseUp (mouseOn (playlist, it->second.getCentre(), {}));
                check ("simple icon click opens the instrument (channel " + juce::String (opened) + ")",
                       ! added.channels.empty() && opened == added.channels.front());
            }
        }
        // ---- Discord (sidthenugget): ghost notes in Track-only mode; shots when LYNX_BACKLOG190RC_SHOTS names a folder
        {
            const auto shots = juce::SystemStats::getEnvironmentVariable ("LYNX_BACKLOG190RC_SHOTS", {});
            const juce::File folder (shots);
            const auto shoot = [&folder, &shots] (juce::Component& c, const juce::String& name) {
                if (shots.isEmpty())
                    return;
                folder.createDirectory();
                savePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), folder.getChildFile (name + ".png").getFullPathName());
                savePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.5f), folder.getChildFile (name + "@150.png").getFullPathName());
            };
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
            const auto trackOnlyBefore = trackonly::enabled();
            trackonly::setEnabled (true);
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.playlist.clear();
            song.patterns.clear();
            song.channels.resize (2);
            song.channels[0].name = "Lead";
            song.channels[1].name = "Bass";
            const int keys[2][8] = { { 72, 74, 76, 79, 77, 76, 74, 72 }, { 64, 64, 67, 67, 65, 65, 62, 62 } };
            for (int p = 0; p < 2; ++p)
            {
                Pattern pat;
                pat.name = p == 0 ? "Lead · clip 1" : "Bass · clip 1";
                for (int i = 0; i < 8; ++i)
                {
                    Note n;
                    n.channel = p;
                    n.key = keys[p][i];
                    n.start = i * ppq;
                    n.length = ppq / 2 + (p == 1 ? ppq / 3 : 0);
                    pat.notes.push_back (n);
                }
                pat.lengthTicks = 2 * bar;
                song.patterns.push_back (pat);
                Clip c;
                c.kind = ClipKind::pattern;
                c.pattern = p;
                c.track = p;
                c.start = 0;
                c.length = 2 * bar;
                song.playlist.push_back (c);
            }
            song.currentPattern = 0;
            engine.setSong (std::move (song));
            engine.setSelectedChannel (0);
            engine.dispatchPendingMessages();
            const auto ghosts = polish::clipGhostNotes (engine.getSong(), 0, 0, 2000, true);
            check ("ghosts: the Bass track's 8 notes under the Lead clip (" + juce::String ((int) ghosts.size()) + ")", ghosts.size() == 8u);
            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 900, 640);
            roll.setVisible (true);
            roll.scrollToNotes (true);
            engine.dispatchPendingMessages();
            shoot (roll, "ghosts-trackonly-lynx");
            trackonly::setEnabled (trackOnlyBefore);
        }
        layoutstyle::setCurrent (layoutBefore);
        return result;
    }
