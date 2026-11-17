    // --playlistshot190jq <prefix> (round 190jq): issue #164's draggable header width, #191's pointer click and #191.3's
    // automation clips drawn as lanes under their tracks, driven with synthetic mouse events through the real Playlist,
    // offscreen; prefix-checks.txt and pictures (--shot-scale for @150). Included in the app class of Main.cpp.
    bool runPlaylistShot190jq (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        using namespace lynx;
        ui::motion::enabled() = false;
        // (a fresh profile: every switch at its default)
        headerwidth::draggable = false;
        headerwidth::extra = 0;
        pointerclick::buttonShown = pointerclick::on = false;
        cliplanes::patternOn = false;
        cliplanes::liveOn = true;

        layoutstyle::setCurrent (layoutstyle::Style::pattern); // (FL's look: the default layout; the style is global)
        AudioEngine engine (false);
        MainComponent ui (engine, false);
        ui.setSize (1600, 900);
        ui.setLayoutStyle (layoutstyle::Style::pattern, false);
        const auto bar = (std::int64_t) 4 * ppq;
        {
            // Beat on track 1, a "Lead" instrument track (track 3), a vocal on track 5; the Lead's volume envelope as two
            // clips on track 7 and its pan on track 8 (FL's way: automation clips on tracks of their own), the tempo on 9
            auto song = Song::makeDefault();
            song.playlist.clear();
            auto& p = song.patterns.front();
            p.name = "Beat";
            for (int step : { 0, 4, 8, 12 })
                p.toggleStep (0, step);
            Clip beat;
            beat.kind = ClipKind::pattern;
            beat.pattern = 0;
            beat.track = 0;
            beat.length = bar;
            for (int b = 0; b < 4; ++b)
            {
                beat.start = b * bar;
                song.playlist.push_back (beat);
            }
            song.playlistTracks.resize (12);
            const auto lead = trackmodes::addInstrumentTrack (song, mididrop::newChannel ("Lead"), 2);
            ChannelDef vox;
            vox.name = "Vocal take 1";
            vox.instrument = InstrumentKind::audioClip;
            vox.samplePath = "/shots/Vocal take 1.wav";
            song.channels.push_back (vox);
            Clip take;
            take.kind = ClipKind::audio;
            take.channel = (int) song.channels.size() - 1;
            take.track = 4;
            take.start = bar;
            take.length = 2 * bar;
            song.playlist.push_back (take);
            const auto envelope = [&song] (const char* name, AutomationTarget target, std::vector<AutomationPoint> points) {
                Automation a;
                a.name = name;
                a.target = target;
                a.points = std::move (points);
                song.automations.push_back (a);
                return (int) song.automations.size() - 1;
            };
            const auto vol = envelope ("Lead - Volume", { AutomationTargetKind::channelVolume, lead, 0, 0 },
                                       { { 0, 0.2f, 0.0f }, { 2 * bar, 0.9f, 0.0f }, { 4 * bar, 0.4f, 0.0f } });
            const auto pan = envelope ("Lead - Pan", { AutomationTargetKind::channelPan, lead, 0, 0 }, { { 0, 0.5f, 0.0f }, { 4 * bar, 0.1f, 0.0f } });
            const auto tempo = envelope ("Tempo", { AutomationTargetKind::tempo, 0, 0, 0 }, { { 0, 0.5f, 0.0f } });
            const auto autoClip = [&song] (int automation, int track, std::int64_t start, std::int64_t length) {
                Clip c;
                c.kind = ClipKind::automation;
                c.automation = automation;
                c.track = track;
                c.start = start;
                c.length = length;
                song.playlist.push_back (c);
            };
            autoClip (vol, 6, 0, 4 * bar);
            autoClip (vol, 6, 5 * bar, 2 * bar);
            autoClip (pan, 7, bar, 3 * bar);
            autoClip (tempo, 8, 0, 4 * bar);
            engine.setSong (std::move (song));
        }
        engine.dispatchPendingMessages();
        ui.selectTabForSnapshot (2);
        engine.dispatchPendingMessages();
        auto& pl = ui.getPlaylistForSnapshot();
        pl.setSize (juce::jmax (pl.getWidth(), 1100), juce::jmax (pl.getHeight(), 640));
        pl.setViewForSnapshot (120.0, 1, 0);
        pl.setToolForSnapshot (EditTool::pencil);
        engine.dispatchPendingMessages();
        const auto& song = [&]() -> const Song& { return engine.getSong(); };
        const auto snap = [&] (const juce::String& name, int rows, int width = 1000) {
            pl.refreshViewOptions();
            engine.dispatchPendingMessages();
            const auto bottom = pl.trackRowForSnapshot (juce::jmin (rows, pl.numTracksForSnapshot() - 1)).getY();
            savePng (shot::snapshot (pl, juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), width), juce::jmin (pl.getHeight(), bottom + 12))), prefix + name);
        };
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto event = [&] (juce::Point<int> p, juce::ModifierKeys mods, juce::Point<int> down, int clicks, bool dragged) {
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pl, &pl, now, down.toFloat(), now, clicks, dragged);
        };
        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
        // a press, optional drag (in two steps) and release; `midDrag` runs between the steps (a picture of the drag)
        const auto press = [&] (juce::Point<int> p, std::optional<juce::Point<int>> to = {}, int clicks = 1, std::function<void()> midDrag = nullptr) {
            pl.mouseMove (event (p, {}, p, 1, false));
            pl.mouseDown (event (p, left, p, clicks, false));
            if (to)
            {
                pl.mouseDrag (event ((p + *to) / 2, left, p, clicks, true));
                pl.mouseDrag (event (*to, left, p, clicks, true));
                if (midDrag)
                    midDrag();
            }
            pl.mouseUp (event (to ? *to : p, {}, p, clicks, to.has_value()));
            if (clicks > 1 && ! to)
                pl.mouseDoubleClick (event (p, left, p, clicks, false));
            engine.dispatchPendingMessages();
        };
        const auto rowCentre = [&] (int track) { return pl.trackRowForSnapshot (track).getCentreY(); };
        const auto xOf = [&] (std::int64_t tick) { return (int) std::lround (pl.xOfTickForSnapshot ((double) tick)); };
        const auto clipsNow = [&] { return (int) song().playlist.size(); };
        // undo back to a marked state (a drag can be several undo steps: one per move event)
        juce::String mark;
        const auto restore = [&] (const juce::String& state) {
            for (int i = 0; i < 12 && ProjectFile::toJson (song()) != state && engine.canUndo(); ++i)
            {
                engine.undo();
                engine.dispatchPendingMessages();
            }
        };

        // ================= #164: the header column's width =================
        const auto base = pl.headerWidthForSnapshot();
        check ("164_default_is_fl_width", base == PlaylistComponent::flHeaderWidthNow() && pl.headerBaseWidthForSnapshot() == base, juce::String (base));
        snap ("-164-default.png", 9);
        {
            // off (the default): the column's right edge is not a grip; a press just past it paints with the Pencil (FL)
            const auto edge = juce::Point<int> (base + 1, rowCentre (3));
            check ("164_off_no_grip_hint", ! pl.hintAt (edge).text.contains ("header width"), pl.hintAt (edge).text);
            const auto before = clipsNow();
            mark = ProjectFile::toJson (song());
            press (edge);
            check ("164_off_edge_paints_as_fl", clipsNow() == before + 1, juce::String (clipsNow()));
            restore (mark);
        }
        workflowextras::setExtra ("playlist.headerWidth", true);
        engine.dispatchPendingMessages();
        check ("164_on_keeps_fl_width_until_dragged", pl.headerWidthForSnapshot() == base, juce::String (pl.headerWidthForSnapshot()));
        {
            const auto edge = juce::Point<int> (base, rowCentre (3));
            check ("164_on_grip_hint", pl.hintAt (edge).text.contains ("header width"), pl.hintAt (edge).text);
            const auto before = clipsNow();
            const auto barX = xOf (4 * bar);
            press (edge, juce::Point<int> (base + 90, rowCentre (3)), 1, [&] { snap ("-164-dragging.png", 9); });
            check ("164_drag_widens", pl.headerWidthForSnapshot() == base + 90 && headerwidth::extra == 90, juce::String (pl.headerWidthForSnapshot()));
            check ("164_drag_places_nothing", clipsNow() == before, juce::String (clipsNow()));
            check ("164_width_saved_as_preference", workflowextras::uiOptionsText().contains ("headerWidthExtra=90")
                                                         && ! ProjectFile::toJson (song()).contains ("headerWidth"));
            check ("164_grid_moves_with_the_column", xOf (4 * bar) == barX + 90, juce::String (barX) + " -> " + juce::String (xOf (4 * bar)));
            snap ("-164-wider.png", 9);
            // the selection strip (left of the grip) still selects tracks, as in FL
            const auto stripX = pl.headerWidthForSnapshot() - trackhdr::selectionStripWidth + 3;
            press ({ stripX, rowCentre (1) });
            check ("164_selection_strip_still_selects", pl.selectedTracksForSnapshot() == std::vector<int> { 1 },
                   juce::String ((int) pl.selectedTracksForSnapshot().size()));
            // never more than half of the Playlist
            press ({ pl.headerWidthForSnapshot(), rowCentre (3) }, juce::Point<int> (pl.getWidth(), rowCentre (3)));
            check ("164_at_most_half", pl.headerWidthForSnapshot() <= pl.getWidth() / 2 && pl.headerWidthForSnapshot() > base + 90,
                   juce::String (pl.headerWidthForSnapshot()));
            // a double click on the edge: FL's width again
            press ({ pl.headerWidthForSnapshot(), rowCentre (3) }, {}, 2);
            check ("164_double_click_fl_width", pl.headerWidthForSnapshot() == base && headerwidth::extra == 0, juce::String (pl.headerWidthForSnapshot()));
            // switched off, a dragged width is not used
            press ({ base, rowCentre (3) }, juce::Point<int> (base + 60, rowCentre (3)));
            workflowextras::setExtra ("playlist.headerWidth", false);
            engine.dispatchPendingMessages();
            check ("164_off_again_fl_width", pl.headerWidthForSnapshot() == base, juce::String (pl.headerWidthForSnapshot()));
            workflowextras::setExtra ("playlist.headerWidth", true);
            engine.dispatchPendingMessages();
            check ("164_on_again_keeps_the_dragged_width", pl.headerWidthForSnapshot() == base + 60, juce::String (pl.headerWidthForSnapshot()));
            // the linked headers (round 190gz) grow by the same amount
            mark = ProjectFile::toJson (song());
            engine.editSong ([] (Song& s) { s.linkTracksToMixer = true; });
            engine.dispatchPendingMessages();
            check ("164_linked_headers_too", pl.headerWidthForSnapshot() == PlaylistComponent::linkedHeaderWidth + 60, juce::String (pl.headerWidthForSnapshot()));
            snap ("-164-linked-wider.png", 9);
            restore (mark);
            headerwidth::extra = 0;
            workflowextras::setExtra ("playlist.headerWidth", false);
            engine.dispatchPendingMessages();
        }

        // ================= #191: pointer click =================
        {
            check ("191_button_hidden_by_default", ! pl.pointerButtonForSnapshot().isVisible());
            snap ("-191-toolbar-off.png", 1, 760);
            workflowextras::setExtra ("playlist.pointerClick", true);
            engine.dispatchPendingMessages();
            auto& button = pl.pointerButtonForSnapshot();
            check ("191_extra_shows_the_switch_lit", button.isVisible() && pointerclick::on
                                                         && dynamic_cast<juce::Button&> (button).getToggleState());
            snap ("-191-toolbar-on.png", 1, 760);
            // a click on an empty spot: no clip, the selection cleared, the song position there
            pl.selectClipsForSnapshot ({ 0 });
            const auto before = clipsNow();
            const auto spot = juce::Point<int> (xOf (9 * bar) + 3, rowCentre (3));
            press (spot);
            check ("191_click_places_nothing", clipsNow() == before, juce::String (clipsNow()));
            check ("191_click_deselects", pl.getSelectedClips().empty());
            check ("191_click_moves_the_song_position", std::abs (pl.playheadTickForSnapshot() - (double) (9 * bar)) < (double) ppq,
                   juce::String (pl.playheadTickForSnapshot()));
            check ("191_pointer_hint", pl.hintAt (spot).text.contains ("Pointer click"), pl.hintAt (spot).text);
            // a drag from empty space draws a selection box (FL's Select tool): the Beat clips of bars 2 and 3
            press ({ xOf (bar) + 4, rowCentre (1) }, juce::Point<int> (xOf (3 * bar) - 4, pl.trackRowForSnapshot (0).getY() + 2),
                   1, [&] { snap ("-191-select-box.png", 6); });
            check ("191_drag_selects", pl.getSelectedClips().size() == 2 && clipsNow() == before, juce::String ((int) pl.getSelectedClips().size()));
            // a double click places the clip (what the single click did)
            mark = ProjectFile::toJson (song());
            press ({ xOf (12 * bar) + 3, rowCentre (3) }, {}, 2);
            check ("191_double_click_places", clipsNow() == before + 1, juce::String (clipsNow()));
            snap ("-191-double-click-placed.png", 6);
            restore (mark);
            // clips keep the Pencil's gestures: a drag on a clip moves it
            {
                const auto start0 = bar; // (the Beat clip of bar 2: in view)
                const auto at = juce::Point<int> (xOf (start0) + 30, rowCentre (0));
                mark = ProjectFile::toJson (song());
                press (at, at + juce::Point<int> ((int) (pl.xOfTickForSnapshot ((double) bar) - pl.xOfTickForSnapshot (0.0)) * 8, 0));
                check ("191_clip_still_moves", song().playlist.size() == (std::size_t) before && std::any_of (song().playlist.begin(), song().playlist.end(),
                                                                                                        [&] (const Clip& c) { return c.start == start0 + 8 * bar; }));
                restore (mark);
            }
            // the button switches it off (and stays shown); a click places again
            if (auto& b = dynamic_cast<juce::Button&> (button); b.onClick) // (what a click on it runs)
                b.onClick();
            engine.dispatchPendingMessages();
            check ("191_button_switches_off", ! pointerclick::on && button.isVisible() && ! dynamic_cast<juce::Button&> (button).getToggleState());
            mark = ProjectFile::toJson (song());
            press (spot);
            check ("191_off_click_places", clipsNow() == before + 1, juce::String (clipsNow()));
            restore (mark);
            // the switch in the Playlist menu, and saved
            workflowextras::runAction ("playlist.pointerClick");
            check ("191_menu_action_on", pointerclick::on && workflowextras::uiOptionsText().contains ("pointerClick=1"));
            const auto menu = ui.getPanelMenuForSnapshot (workspace::Panel::playlist);
            saveMenus (juce::StringArray { "Playlist" }, std::vector<juce::PopupMenu> { menu }, prefix + "-playlist-menu.png");
            juce::StringArray texts;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                texts.add (it.getItem().text);
            check ("menu_items", texts.contains (tr ("Automation clips as lanes under their tracks"))
                                     && texts.contains (tr ("Click on an empty spot selects and moves the song position (pointer click)"))
                                     && texts.contains (tr ("Drag the track header column's right edge to widen it")));
            workflowextras::setExtra ("playlist.pointerClick", false);
            engine.dispatchPendingMessages();
            check ("191_extra_off_hides_and_ends", ! pl.pointerButtonForSnapshot().isVisible() && ! pointerclick::on);
        }

        // ================= #191.3: automation clips as lanes under their track =================
        {
            const auto jsonBefore = ProjectFile::toJson (song());
            check ("1913_pattern_default_off", ! pl.projectionForSnapshot().any());
            snap ("-1913-pattern-off.png", 10);
            workflowextras::runAction ("playlist.clipLanes");
            engine.dispatchPendingMessages();
            const auto& p = pl.projectionForSnapshot();
            check ("1913_on_projects", p.any() && p.ownerOf (6) == 2 && p.ownerOf (7) == 2 && ! p.isChild (8) && p.childCount (2) == 2);
            check ("1913_lanes_right_under_the_owner", pl.trackRowForSnapshot (6).getY() == pl.trackRowForSnapshot (2).getBottom()
                                                           && pl.trackRowForSnapshot (7).getY() == pl.trackRowForSnapshot (6).getBottom()
                                                           && pl.trackRowForSnapshot (3).getY() == pl.trackRowForSnapshot (7).getBottom(),
                   juce::String (pl.trackRowForSnapshot (6).getY()) + " " + juce::String (pl.trackRowForSnapshot (2).getBottom()));
            check ("1913_song_untouched", ProjectFile::toJson (song()) == jsonBefore);
            check ("1913_saved_per_layout", workflowextras::uiOptionsText().contains ("clipLanesPattern=1") && workflowextras::uiOptionsText().contains ("clipLanesLive=1"));
            snap ("-1913-pattern-on.png", 10);
            // a lane's clip: hit where it is drawn, moves in time, stays in its lane
            {
                int index = -1;
                for (int i = 0; i < (int) song().playlist.size(); ++i)
                    if (song().playlist[(std::size_t) i].track == 7)
                        index = i;
                const auto r = pl.clipBoundsForSnapshot (index);
                check ("1913_clip_drawn_in_its_lane", pl.trackRowForSnapshot (7).contains (r.getCentre()));
                const auto start0 = song().playlist[(std::size_t) index].start;
                const auto at = juce::Point<int> (r.getX() + 30, r.getY() + 4); // (its title, right of the menu button)
                const auto oneBar = (int) std::lround (pl.xOfTickForSnapshot ((double) bar) - pl.xOfTickForSnapshot (0.0));
                mark = ProjectFile::toJson (song());
                press (at, at + juce::Point<int> (oneBar, pl.trackRowForSnapshot (4).getCentreY() - at.y));
                bool found = false;
                for (const auto& c : song().playlist)
                    found = found || (c.kind == ClipKind::automation && c.automation == 1 && c.track == 7 && c.start == start0 + bar);
                check ("1913_lane_clip_moves_in_time_only", found);
                restore (mark);
            }
            // an ordinary clip never lands in a lane; past the lanes it moves by drawn rows
            {
                const auto at = juce::Point<int> (xOf (bar) + 30, rowCentre (0));
                mark = ProjectFile::toJson (song());
                press (at, juce::Point<int> (at.x, rowCentre (6)));
                const auto onTrack = [&] (int track) {
                    return (int) std::count_if (song().playlist.begin(), song().playlist.end(), [track] (const Clip& c) { return c.kind == ClipKind::pattern && c.track == track; });
                };
                check ("1913_no_clip_into_a_lane", onTrack (6) == 0 && onTrack (0) == 4, juce::String (onTrack (0)));
                if (onTrack (0) != 4)
                {
                    restore (mark);
                }
                mark = ProjectFile::toJson (song());
                press (at, juce::Point<int> (at.x, rowCentre (3)));
                juce::String where;
                for (const auto& c : song().playlist)
                    where << (int) c.kind << ":" << c.track << "@" << (int) (c.start / bar) << " ";
                check ("1913_move_by_drawn_rows", std::any_of (song().playlist.begin(), song().playlist.end(),
                                                               [] (const Clip& c) { return c.kind == ClipKind::pattern && c.track == 3; }),
                       where + " sel " + juce::String ((int) pl.getSelectedClips().size()) + " rowY " + juce::String (rowCentre (3)) + " row0 " + juce::String (rowCentre (0)));
                restore (mark);
            }
            // the Pencil places nothing in a lane: a click there is the pointer
            {
                const auto before = clipsNow();
                press ({ xOf (12 * bar), rowCentre (6) });
                check ("1913_pencil_places_nothing_in_a_lane", clipsNow() == before);
            }
            // the owner's arrow folds its lanes
            {
                const auto arrow = pl.trackLaneArrowForSnapshot (2);
                check ("1913_owner_has_an_arrow", ! arrow.isEmpty());
                press (arrow.getCentre());
                check ("1913_arrow_folds", pl.trackRowForSnapshot (6).getHeight() == 0 && pl.trackRowForSnapshot (3).getY() == pl.trackRowForSnapshot (2).getBottom(),
                       juce::String (pl.trackRowForSnapshot (6).getHeight()));
                snap ("-1913-folded.png", 10);
                press (pl.trackLaneArrowForSnapshot (2).getCentre());
                check ("1913_arrow_unfolds", pl.trackRowForSnapshot (6).getHeight() > 0);
            }
            // Clip-launch: on by default (Live's lanes)
            workflowextras::runAction ("playlist.clipLanes");
            engine.dispatchPendingMessages();
            check ("1913_pattern_off_again", ! pl.projectionForSnapshot().any());
            ui.setLayoutStyle (layoutstyle::Style::clipLaunch, false);
            engine.dispatchPendingMessages();
            pl.refreshViewOptions();
            check ("1913_clip_launch_default_on", pl.projectionForSnapshot().any());
            snap ("-1913-cliplaunch.png", 10);
            ui.setLayoutStyle (layoutstyle::Style::pattern, false);
            engine.dispatchPendingMessages();
            pl.refreshViewOptions();
            check ("1913_back_in_pattern_off", ! pl.projectionForSnapshot().any());
        }

        // ================= Settings › LYNX extras =================
        {
            auto page = makeSettingsPanel (engine, SettingsPage::extras);
            page->setSize (juce::jmax (page->getWidth(), 760), 1900);
            juce::Component frame;
            frame.setSize (page->getWidth(), page->getHeight());
            frame.setOpaque (true);
            frame.addAndMakeVisible (*page);
            savePng (shot::paint (frame, theme::panel), prefix + "-settings-extras.png");
        }

        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }
