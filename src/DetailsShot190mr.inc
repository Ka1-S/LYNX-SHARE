    // --details190mrshot <prefix> <project.flp> (round 190mr, details pass 8): FL's click-and-hold functions in the Piano
    // roll (ring, clone, slide / porta cycle, glue), the General settings of the round (Input, undo history in the
    // Browser, project data size warning, window layout with the project), the Extended Hint Panel, the window layout
    // saved with a project, the history shown on undo, and Live's rack Auto Select. <prefix>-checks.txt lists the checks.
    // Included in the app class of Main.cpp.
    bool runDetailsShot190mr (const juce::String& prefix, const juce::String& project)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        juce::String checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
        };
        const auto line = [&checks] (const juce::String& what) { checks << "     " << what << "\n"; };
        const auto zoomShot = [&prefix, &checks] (juce::Component& c, juce::Rectangle<int> r, float k, const juce::String& name) {
            const auto area = r.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
            {
                checks << "     (no picture: " << name << " is off the view)\n";
                return;
            }
            savePng (c.createComponentSnapshot (area, true, k), prefix + "-" + name + ".png");
        };
        const auto texts = [] (const juce::PopupMenu& menu) {
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                items.add (it.getItem().isSectionHeader ? "[" + it.getItem().text + "]" : it.getItem().isSeparator ? juce::String ("-") : it.getItem().text);
            return items;
        };
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) {
            savePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.5f), prefix + "-" + name + "@150.png");
        };
        using namespace lynx;
        if (project.isEmpty())
            return false;
        flsettings::reset();
        AudioEngine offline (false);

        // ---- Settings › General: FL's Input options, undo history in the Browser, data size warning, layout with project
        {
            auto page = makeGeneralPage (offline, {}, false);
            page->setSize (680, 3600);
            std::map<juce::String, juce::Component*> found;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                if (auto* t = dynamic_cast<juce::Button*> (&c))
                    found[t->getButtonText()] = t;
                if (auto* l = dynamic_cast<juce::Label*> (&c))
                    found["label:" + l->getText()] = l;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*page);
            const auto toggled = [&found] (const juce::String& text, bool& on) {
                auto* b = dynamic_cast<juce::Button*> (found.count (text) ? found[text] : nullptr);
                on = b != nullptr && b->getToggleState();
                return b != nullptr;
            };
            bool on = true;
            check ("General: \"Click-and-hold & special gesture functions\", off as in FL", toggled (tr ("Click-and-hold & special gesture functions"), on) && ! on);
            check ("General: \"Use both mouse buttons in step sequencer\", off as in FL", toggled (tr ("Use both mouse buttons in step sequencer"), on) && ! on);
            check ("General: \"Show undo history in the browser when undoing changes\", off as in FL",
                   toggled (tr ("Show undo history in the browser when undoing changes"), on) && ! on);
            check ("General: \"Load the window layout saved with a project\", on", toggled (tr ("Load the window layout saved with a project"), on) && on);
            auto* sizeLabel = found.count ("label:" + tr ("Project data size warning")) ? found["label:" + tr ("Project data size warning")] : nullptr;
            check ("General: \"Project data size warning\" row (100MB by default)", sizeLabel != nullptr);
            const auto crop = [&] (const juce::String& key, const juce::String& name, int above, int below) {
                if (auto* c = found.count (key) ? found[key] : nullptr)
                    zoomShot (*page, juce::Rectangle<int> (0, c->getY() - above, page->getWidth(), above + below), 1.0f, name);
            };
            crop (tr ("Click-and-hold & special gesture functions"), "settings-input", 30, 60);
            crop (tr ("Show undo history in the browser when undoing changes"), "settings-undo", 90, 60);
            crop ("label:" + tr ("Project data size warning"), "settings-datasize", 50, 36);
            crop (tr ("Load the window layout saved with a project"), "settings-layout", 100, 40);
        }

        MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        ui.keepLayoutInSavesForSnapshot();
        ui.openFile (juce::File (project));
        offline.dispatchPendingMessages();

        // ---- the Piano roll's click-and-hold functions (DE-63, capture 190mr A1.2)
        {
            auto& pr = ui.getPianoRollForSnapshot();
            ui.showPanelsForSnapshot ({ workspace::Panel::pianoRoll });
            offline.dispatchPendingMessages();
            int pattern = -1;
            for (int p = 0; p < (int) offline.getSong().patterns.size() && pattern < 0; ++p)
                if (! offline.getSong().patterns[(std::size_t) p].notes.empty())
                {
                    pattern = p;
                    const auto ch = offline.getSong().patterns[(std::size_t) p].notes.front().channel;
                    offline.editSong ([p] (Song& s) { s.currentPattern = p; }, false);
                    offline.setSelectedChannel (ch);
                }
            offline.dispatchPendingMessages();
            pr.runAction ("pianoRoll.zoomAll");
            pr.scrollToNotes();
            pr.setToolForSnapshot (EditTool::pencil);
            offline.dispatchPendingMessages();
            const auto notes = [&offline, pattern] { return offline.getSong().patterns[(std::size_t) juce::jmax (0, pattern)].notes; };
            const auto before = notes();
            // a key in view with no note in the first two beats (the cycle and glue tests draw there)
            const auto freeKey = [&] (int avoid) {
                const auto base = before.front().key;
                for (int d = 1; d < 24; ++d)
                    for (const auto key : { base + d, base - d })
                    {
                        if (key == avoid || key < 1 || key > 126)
                            continue;
                        const auto at = pr.pointForTests (0, key);
                        const auto clear = std::none_of (before.begin(), before.end(), [key] (const Note& n) { return n.key == key && n.start < 2 * lynx::ppq; });
                        if (clear && pr.getLocalBounds().reduced (0, 40).contains (at) && pr.getLocalBounds().contains (pr.pointForTests (2 * lynx::ppq, key)))
                            return key;
                    }
                return -1;
            };
            const auto cycleKey = freeKey (-1), glueKey = freeKey (cycleKey);
            line ("free keys for the cycle / glue: " + juce::String (cycleKey) + " / " + juce::String (glueKey));
            // off (FL's default): a still press does nothing more
            {
                const auto& n = before.front();
                const auto at = pr.pointForTests (n.start + n.length / 2, n.key);
                pr.pressForSnapshot (at);
                check ("click-and-hold off (FL's default): no hold is armed", ! pr.holdPendingForSnapshot());
                pr.releaseForSnapshot (at);
            }
            flsettings::options().clickHold = true;
            // hold on a note: the ring, then a clone one step later that the mouse drags
            {
                const auto& n = before.front();
                const auto at = pr.pointForTests (n.start + n.length / 2, n.key);
                pr.pressForSnapshot (at);
                check ("hold on a note: armed", pr.holdPendingForSnapshot());
                pr.holdRingForSnapshot (390 + 220); // (60 % of FL's ring)
                zoomShot (pr, juce::Rectangle<int> (60, 40).withCentre (at), 4.0f, "pr-hold-ring");
                zoomShot (pr, juce::Rectangle<int> (240, 90).withCentre (at), 2.0f, "pr-hold-ring-wide");
                pr.fireHoldForSnapshot();
                const auto after = notes();
                const auto step = pr.gridTicksForSnapshot();
                const auto clone = std::any_of (after.begin(), after.end(), [&n, step] (const Note& o) {
                    return o.key == n.key && o.channel == n.channel && o.start == n.start + step && o.length == n.length;
                });
                line ("grid step " + juce::String (step) + " ticks, notes " + juce::String ((int) before.size()) + " -> " + juce::String ((int) after.size()));
                check ("hold on a note (750 ms): a clone one grid step later, the original kept", after.size() == before.size() + 1 && clone);
                zoomShot (pr, juce::Rectangle<int> (240, 90).withCentre (at), 2.0f, "pr-hold-clone");
                pr.releaseForSnapshot (at);
                ui.undoForSnapshot();
                check ("one undo step removes the clone", notes().size() == before.size());
            }
            // hold while adding a note: slide, porta, plain
            {
                const auto key = cycleKey;
                const auto at = pr.pointForTests (0, key);
                pr.pressForSnapshot (at);
                const auto added = notes();
                const auto index = (int) added.size() - 1;
                const auto find = [&notes, key] { for (const auto& n : notes()) if (n.key == key && n.start == 0) return n; return Note {}; };
                check ("a new note is added and the cycle armed", added.size() == before.size() + 1 && pr.holdPendingForSnapshot());
                juce::ignoreUnused (index);
                pr.fireHoldForSnapshot();
                const auto slide = find();
                zoomShot (pr, juce::Rectangle<int> (200, 40).withPosition (at.x - 10, at.y - 20), 3.0f, "pr-hold-cycle-slide");
                pr.fireHoldForSnapshot();
                const auto porta = find();
                zoomShot (pr, juce::Rectangle<int> (200, 40).withPosition (at.x - 10, at.y - 20), 3.0f, "pr-hold-cycle-porta");
                pr.fireHoldForSnapshot();
                const auto plain = find();
                check ("the cycle: slide, then porta, then plain (750 ms each, as FL)", slide.slide && ! slide.porta && porta.porta && ! porta.slide && ! plain.slide && ! plain.porta);
                pr.releaseForSnapshot (at);
                ui.undoForSnapshot();
                check ("one undo step removes the added note", notes().size() == before.size());
            }
            // hold at the resize cursor between two touching notes: glued
            {
                const auto key = glueKey;
                Note a, b;
                a.key = b.key = key;
                a.channel = b.channel = offline.getSelectedChannel();
                a.start = 0;
                a.length = b.length = lynx::ppq;
                b.start = lynx::ppq;
                offline.editSong ([pattern, a, b] (Song& s) {
                    s.patterns[(std::size_t) pattern].notes.push_back (a);
                    s.patterns[(std::size_t) pattern].notes.push_back (b);
                });
                offline.dispatchPendingMessages();
                const auto with = notes().size();
                const auto at = pr.pointForTests (a.start + a.length, key) - juce::Point<int> (2, 0);
                zoomShot (pr, juce::Rectangle<int> (260, 40).withPosition (pr.pointForTests (0, key).x - 10, at.y - 20), 3.0f, "pr-hold-glue-before");
                pr.pressForSnapshot (at);
                check ("a press at the right edge of a note touching the next: the glue is armed", pr.holdPendingForSnapshot());
                pr.fireHoldForSnapshot();
                const auto now = notes();
                const auto glued = std::any_of (now.begin(), now.end(), [key] (const Note& n) { return n.key == key && n.start == 0 && n.length == 2 * lynx::ppq; });
                check ("hold (750 ms): the two notes are one", now.size() == with - 1 && glued);
                zoomShot (pr, juce::Rectangle<int> (260, 40).withPosition (pr.pointForTests (0, key).x - 10, at.y - 20), 3.0f, "pr-hold-glue-after");
                pr.releaseForSnapshot (at);
                ui.undoForSnapshot();
                ui.undoForSnapshot();
                check ("undo: the two notes again, then none", notes().size() == before.size());
            }
            flsettings::options().clickHold = false;
        }

        // ---- the Extended Hint Panel (DF-49, capture 190mr A2)
        {
            const auto items = texts (ui.getToolbarMenuForSnapshot());
            check ("toolbar menu: \"Hint bar (extended hint panel)\"", items.contains (tr ("Hint bar (extended hint panel)")));
            ui.showExtendedHintForSnapshot (true);
            auto* panel = dynamic_cast<ui::ExtendedHintPanel*> (ui.extendedHintForSnapshot());
            check ("the panel shows, 400 x 82 at the desktop's lower left", panel != nullptr && panel->getWidth() == 400 && panel->getHeight() == 82
                                                                             && panel->getX() <= 0 && panel->getBottom() >= ui.getHeight() - 140);
            if (panel != nullptr)
            {
                ui::Hint h;
                h.text = tr ("Master pitch");
                h.value = "+100 cents";
                h.automatable = true;
                panel->setHint (h, false);
                check ("click-through: only its grip takes the mouse", ! panel->hitTest (40, 20) && panel->hitTest (359, 41));
                zoomShot (ui, panel->getBounds().expanded (8), 1.0f, "extended-hint");
                zoomShot (ui, panel->getBounds().expanded (8), 1.5f, "extended-hint@150");
                h = {};
                h.text = "4:06:17";
                h.value = "F#6 / 78";
                panel->setHint (h, false);
                zoomShot (ui, panel->getBounds().expanded (8), 1.0f, "extended-hint-note");
            }
            ui.showExtendedHintForSnapshot (false);
            check ("off again: the panel is gone", ui.extendedHintForSnapshot() == nullptr);
        }

        // ---- the history shown on undo (DF-135)
        {
            auto& browser = ui.getBrowserForSnapshot();
            browser.setTab (0);
            offline.editSong ([] (Song& s) { s.swing = 0.25f; });
            flsettings::options().historyOnUndo = true;
            ui.undoForSnapshot();
            offline.dispatchPendingMessages();
            check ("Show undo history in the browser: an undo opens Current project › History", browser.historyShownForCheck());
            flsettings::options().historyOnUndo = false;
        }

        // ---- the window layout saved with the project (DF-141)
        {
            const auto file = juce::File (prefix + "-layout-check.lynx");
            ui.showPanelsForSnapshot ({ workspace::Panel::mixer, workspace::Panel::channelRack });
            offline.dispatchPendingMessages();
            const auto saved = ui.layoutTextForSnapshot();
            juce::String error;
            const auto ok = ProjectFile::save (offline.getSong(), file, error, ui.currentWindowLayout());
            const auto text = file.loadFileAsString();
            check ("the project file holds the window layout", ok && text.contains ("\"windowLayout\""));
            ui.showPanelsForSnapshot ({ workspace::Panel::playlist });
            offline.dispatchPendingMessages();
            const auto changed = ui.layoutTextForSnapshot() != saved;
            ui.openFile (file);
            offline.dispatchPendingMessages();
            check ("the project opens with its saved window layout", changed && ui.layoutTextForSnapshot() == saved);
            flsettings::options().projectWindowLayout = false;
            ui.showPanelsForSnapshot ({ workspace::Panel::playlist });
            const auto kept = ui.layoutTextForSnapshot();
            ui.openFile (file);
            offline.dispatchPendingMessages();
            check ("option off: the layout on screen stays", ui.layoutTextForSnapshot() == kept);
            flsettings::options().projectWindowLayout = true;
            const auto plain = ProjectFile::toJson (offline.getSong());
            check ("a song's own JSON has no window layout (view only, outside the Song)", ! plain.contains ("windowLayout"));
            file.deleteFile();
        }

        // ---- Live's rack chain list Auto Select (DL-92)
        {
            ChannelDef inst;
            inst.name = "Instrument Rack";
            inst.instrument = InstrumentKind::patcher;
            inst.patcher = rack::makeRack (RackDoc::Kind::instrument);
            for (int c = 0; c < 2; ++c)
                rack::addChain (inst.patcher);
            auto& r = *rack::rackOf (inst.patcher);
            const char* names[] = { "Bass", "Keys", "Lead" };
            for (int c = 0; c < 3 && c < (int) r.chains.size(); ++c)
            {
                r.chains[(std::size_t) c].name = names[c];
                r.chains[(std::size_t) c].key = c == 0 ? RackZone { 0, 47, 0, 47 } : c == 1 ? RackZone { 48, 71, 48, 71 } : RackZone { 72, 127, 72, 127 };
            }
            r.showChains = true;
            rack::compile (inst.patcher);
            int channel = -1;
            offline.editSong ([&inst, &channel] (Song& s) {
                s.channels.push_back (inst);
                channel = (int) s.channels.size() - 1;
            });
            offline.dispatchPendingMessages();
            auto view = RackWindows::makeChannelView (offline, channel, true);
            view->setSize (std::max (520, view->preferredWidth (220)), 220);
            write (*view, "rack-autoselect-off");
            const auto button = view->autoSelectButtonForSnapshot();
            check ("Auto Select: a switch at the foot of the rack's left column (Live 12, capture 190mr B2)", ! button.isEmpty() && button.getX() < 30);
            view->clickForSnapshot (button.getCentre());
            offline.dispatchPendingMessages();
            const auto* now = rack::rackOf (offline.getSong().channels[(std::size_t) channel].patcher);
            check ("a click turns Auto Select on (saved with the rack)", now != nullptr && now->autoSelect);
            std::array<std::uint64_t, 2> keys {};
            keys[1] |= std::uint64_t (1) << (76 - 64); // E5: the Lead chain's zone
            const auto selected = view->autoSelectForSnapshot (keys);
            check ("an incoming note in the Lead zone selects the Lead chain", selected == 2);
            write (*view, "rack-autoselect-on");
            keys = {};
            keys[0] |= std::uint64_t (1) << 36; // C2: Bass
            keys[0] |= std::uint64_t (1) << 60; // C4: Keys
            check ("two chains processing: both lit, the first selected", view->autoSelectForSnapshot (keys) == 0);
            write (*view, "rack-autoselect-two");
        }

        // ---- Live's crossfade by dragging a fade handle over the neighbour (DL-68), in the Clip-launch layout
        {
            ui.setLayoutStyle (layoutstyle::Style::clipLaunch);
            offline.dispatchPendingMessages();
            check ("the Clip-launch layout shows Live's look", theme::livelook::active());
            auto& pl = ui.getPlaylistForSnapshot();
            const auto& song = offline.getSong();
            // a test sound of 8 s (a plain decaying tone), as an audio clip channel
            const auto wav = juce::File (prefix + "-crossfade-tone.wav");
            {
                juce::AudioBuffer<float> audio (1, 8 * 48000);
                for (int i = 0; i < audio.getNumSamples(); ++i)
                    audio.setSample (0, i, 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / 48000.0) * (0.6f + 0.4f * (float) std::cos (i / 9000.0)));
                juce::WavAudioFormat format;
                wav.deleteFile();
                if (auto out = wav.createOutputStream())
                    if (auto* w = format.createWriterFor (out.get(), 48000.0, 1, 16, {}, 0))
                    {
                        out.release();
                        std::unique_ptr<juce::AudioFormatWriter> writer (w);
                        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
                    }
            }
            int a = -1;
            offline.editSong ([&wav, &a] (Song& s) {
                ChannelDef ch;
                ch.name = "Tone";
                ch.instrument = InstrumentKind::audioClip;
                ch.samplePath = wav.getFullPathName().toStdString();
                s.channels.push_back (ch);
                Clip c;
                c.kind = ClipKind::audio;
                c.channel = (int) s.channels.size() - 1;
                c.track = juce::jmax (0, (int) s.playlistTracks.size() - 1); // (the last track: empty in Thief)
                if (c.track < (int) s.playlistTracks.size())
                    s.playlistTracks[(std::size_t) c.track].height = 96; // (a tall lane: Live draws fades on an open lane only)
                c.start = 0;
                c.length = 8 * lynx::ppq;
                s.playlist.push_back (c);
                a = (int) s.playlist.size() - 1;
            });
            offline.dispatchPendingMessages();
            check ("an audio clip to cut in two", a >= 0);
            if (a >= 0)
            {
                // A = its first half (audio left past its end), B = a copy of A put right after it, on the same track
                const auto half = song.playlist[(std::size_t) a].length / 2;
                int b = -1;
                offline.editSong ([a, half, &b] (Song& s) {
                    auto& clip = s.playlist[(std::size_t) a];
                    clip.length = half;
                    clip.fadeIn = clip.fadeOut = 0;
                    auto copy = clip;
                    copy.start = clip.start + half;
                    s.playlist.push_back (copy);
                    b = (int) s.playlist.size() - 1;
                });
                offline.dispatchPendingMessages();
                const auto boundary = song.playlist[(std::size_t) a].end();
                const auto shownAt = pl.getBounds();
                pl.setSize (juce::jmax (1400, pl.getWidth()), juce::jmax (320, pl.getHeight())); // (wide enough, for the picture)
                pl.setViewForSnapshot (640.0, 0, juce::jmax (0, song.playlist[(std::size_t) a].track - 2)); // (the clips in view, 1 beat = 160 px)
                const auto both = pl.clipBoundsForSnapshot (a).getUnion (pl.clipBoundsForSnapshot (b));
                const auto edgeX = pl.clipBoundsForSnapshot (b).getX();
                const auto view = juce::Rectangle<int> (edgeX - 200, both.getY() - 6, 400, both.getHeight() + 12); // (the boundary ± 1¼ beats)
                zoomShot (pl, view, 2.0f, "pl-crossfade-before");
                const auto made = pl.crossfadeDragForSnapshot (a, boundary + lynx::ppq / 2);
                offline.dispatchPendingMessages();
                const auto& ca = song.playlist[(std::size_t) a];
                const auto& cb = song.playlist[(std::size_t) b];
                line ("A " + juce::String (ca.start) + "+" + juce::String (ca.length) + " fade out " + juce::String (ca.fadeOut) + ", B " + juce::String (cb.start)
                      + " fade in " + juce::String (cb.fadeIn));
                check ("the fade-out handle dragged half a beat over B: A goes on under B, both fades = the overlap",
                       made && ca.end() == boundary + lynx::ppq / 2 && ca.fadeOut == lynx::ppq / 2 && cb.fadeIn == lynx::ppq / 2 && cb.start == boundary);
                zoomShot (pl, view, 2.0f, "pl-crossfade-after");
                zoomShot (pl, view, 1.5f, "pl-crossfade-after@150");
                pl.crossfadeDragForSnapshot (a, boundary - lynx::ppq / 4, false, false); // (the same drag, back left)
                pl.setBounds (shownAt);
                check ("back over its own edge: the crossfade is gone, A as before", song.playlist[(std::size_t) a].end() == boundary
                                                                                       && song.playlist[(std::size_t) b].fadeIn == 0);
            }
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
            check ("the Pattern layout: a fade handle stops at the clip's edge (FL)", ! theme::livelook::active()
                                                                                      && (a < 0 || ! pl.crossfadeDragForSnapshot (a, song.playlist[(std::size_t) a].end() + lynx::ppq)));
            wav.deleteFile();
        }

        // ---- the Simple layout (BandLab): an empty pad's calls to action, recording into it (DS-36); Start from's ⏸ (DS-47)
        {
            ui.setLayoutStyle (layoutstyle::Style::simple);
            offline.dispatchPendingMessages();
            auto* simple = ui.getSimpleWorkflowForSnapshot();
            check ("the Simple layout", simple != nullptr);
            if (simple != nullptr && simple->addPads())
            {
                offline.dispatchPendingMessages();
                int track = -1;
                for (int t = 0; t < (int) offline.getSong().playlistTracks.size() && track < 0; ++t)
                    if (simpletracks::kindOf (offline.getSong(), t) == simpletracks::Kind::pads)
                        track = t;
                simple->trackSelected (track);
                offline.dispatchPendingMessages();
                if (auto* dock = simple->dockForSnapshot())
                {
                    dock->padsSelectForCheck (12);
                    offline.dispatchPendingMessages();
                    bool shownAll = true;
                    juce::StringArray names;
                    for (int k = 0; k < 3; ++k)
                    {
                        auto* b = dynamic_cast<juce::Button*> (dock->padsCtaForCheck (k));
                        shownAll = shownAll && b != nullptr && b->isVisible();
                        names.add (b != nullptr ? b->getButtonText() : juce::String());
                    }
                    line ("empty pad's buttons: " + names.joinIntoString (" | "));
                    check ("an empty pad: BandLab's Record / Import / Browse under their icons and captions",
                           shownAll && names == juce::StringArray { tr ("Record"), tr ("Import"), tr ("Browse") });
                    write (*dock, "pads-empty-cta");
                    dock->padsRecordForCheck(); // (no audio input here: it says so and records nothing)
                    check ("Record without an audio input: nothing starts", ! dock->padsRecordingForCheck());
                    dock->padsRecordingLookForSnapshot (true);
                    auto* rec = dynamic_cast<juce::Button*> (dock->padsCtaForCheck (0));
                    check ("while recording the button reads Stop (BandLab's red Stop)", rec != nullptr && rec->getButtonText() == tr ("Stop"));
                    write (*dock, "pads-recording");
                    dock->padsRecordingLookForSnapshot (false);
                }
                if (auto* panel = dynamic_cast<StartFromPanel*> (simple->startFromForSnapshot()))
                {
                    panel->previewLookForSnapshot (0);
                    write (*panel, "startfrom-previewing");
                    panel->previewLookForSnapshot (-1);
                }
            }
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            offline.dispatchPendingMessages();
        }

        juce::File (prefix + "-checks.txt").replaceWithText (checks + "result=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }
