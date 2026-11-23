    // LYNX --automodes190pashot <prefix> (round 190pa, 0.1.32: research190oe P21 automation write modes, P14 range edits on
    // chosen tracks) off-screen through the real components, in the three layouts: the Pattern Playlist with the range
    // extra off and on, a range over two tracks (Ctrl+Alt+drag's result), its menu, Delete / Duplicate on it, a range on
    // one automation lane; the record menu's "Automation write mode (LYNX)" submenu and the armed mode's badge on Record
    // (Pattern, Simple) and on Automation Arm (Clip-launch); the Clip-launch Arrangement with a range (a plain drag on
    // empty space there, Live's time selection: rollup 0.1.31 step 7). <prefix>-checks.txt
    // lists the checks. Included in the app class of Main.cpp (savePng / saveMenus). @150: LYNX_SHOT_SCALE=1.5
    // LYNX_SHOT_SUFFIX=@150.
    bool runAutoModesShot190pa (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        using namespace lynx;
        ui::motion::enabled() = false;
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        const auto bar = (std::int64_t) 4 * ppq;
        const auto rangeExtra = rangeedit::patternLayoutOn();
        const auto lanesShown = autolanes::shown();

        // five tracks: Drums, Lead, Bass, Pad, FX, two-bar clips; insert 3 (Bass) has a volume lane on track 2
        const auto makeSong = [bar] {
            auto s = Song::makeDefault();
            s.playlist.clear();
            s.patterns.clear();
            s.channels.resize (1);
            s.channels[0].name = "Kick";
            s.channels[0].mixerTrack = 1;
            const std::pair<const char*, std::uint32_t> named[] { { "Lead", 0xFFE5383Bu }, { "Bass", 0xFF6E9BD8u }, { "Pad", 0xFF5FB36Bu } };
            for (const auto& [name, colour] : named)
            {
                ChannelDef c;
                c.name = name;
                c.colour = colour;
                c.mixerTrack = (int) s.channels.size() + 1;
                s.channels.push_back (c);
            }
            if (s.mixer.size() < 8)
                s.mixer.resize (8);
            const char* inserts[] { "Master", "Kick", "Lead", "Bass", "Pad", "FX" };
            for (int t = 1; t < 6; ++t)
                s.mixer[(std::size_t) t].name = inserts[t];
            const char* trackNames[] { "Drums", "Lead", "Bass", "Pad", "FX" };
            s.playlistTracks.resize (5);
            for (int t = 0; t < 5; ++t)
                s.playlistTracks[(std::size_t) t].name = trackNames[t];
            for (int c = 0; c < 4; ++c)
            {
                auto& p = s.patterns.emplace_back();
                p.name = s.channels[(std::size_t) c].name;
                p.colour = c == 0 ? 0xFFD9A62Bu : s.channels[(std::size_t) c].colour;
                for (int k = 0; k < 8; ++k)
                {
                    Note n;
                    n.channel = c;
                    n.key = c == 0 ? 60 : 48 + 12 * (c % 2) + k % 3;
                    n.start = k * ppq / 2;
                    n.length = ppq / 4;
                    p.notes.push_back (n);
                }
                for (int k = 0; k < 4; ++k)
                {
                    Clip clip;
                    clip.pattern = c;
                    clip.track = c;
                    clip.start = k * 2 * bar;
                    clip.length = 2 * bar;
                    s.playlist.push_back (clip);
                }
            }
            s.mode = PlayMode::song;
            return s;
        };

        AudioEngine engine (false);
        engine.setSong (makeSong());
        engine.dispatchPendingMessages();
        MainComponent ui (engine, false);
        ui.setSize (1920, 1080);
        engine.dispatchPendingMessages();
        ui.selectTabForSnapshot (2);
        engine.dispatchPendingMessages();
        auto& pl = ui.getPlaylistForSnapshot();
        pl.setViewForSnapshot (110.0, 0, 0);
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) {
            c.resized();
            savePng (shot::snapshot (c), prefix + "-" + name + ".png");
        };
        const auto zoom = [&prefix] (juce::Component& c, juce::Rectangle<int> r, float k, const juce::String& name) {
            savePng (c.createComponentSnapshot (r.getIntersection (c.getLocalBounds()), true, k * shot::scale()), prefix + "-" + name + ".png");
        };
        const auto has = [] (const juce::PopupMenu& m, const juce::String& text) {
            for (juce::PopupMenu::MenuItemIterator it (m, true); it.next();)
                if (it.getItem().text.startsWith (text))
                    return true;
            return false;
        };
        std::function<juce::Component* (juce::Component&, const juce::String&)> find = [&find] (juce::Component& c, const juce::String& id) -> juce::Component* {
            if (c.getComponentID() == id || c.getName() == id)
                return c.isShowing() || c.isVisible() ? &c : nullptr;
            for (auto* child : c.getChildren())
                if (auto* hit = find (*child, id))
                    return hit;
            return nullptr;
        };
        const auto around = [&ui] (juce::Component* c) {
            return c == nullptr ? juce::Rectangle<int>() : ui.getLocalArea (c->getParentComponent(), c->getBounds()).expanded (90, 8);
        };
        const auto tracksArea = [&pl] { return juce::Rectangle<int> (0, pl.trackRowForSnapshot (0).getY() - 4, pl.headerWidthForSnapshot() + 9 * 110, 5 * 60 + 8); };
        const auto rowY = [&pl] (int t) { return pl.trackRowForSnapshot (t).getCentreY(); };
        const auto barX = [&pl] (double b) { return pl.headerWidthForSnapshot() + (int) std::lround (b * 110.0) + 2; };

        // ---- Pattern layout: the extra off (FL's Playlist as it is), then on
        rangeedit::patternLayoutOn() = false;
        check ("pattern_extra_off", ! pl.rangeEditsOn());
        write (ui, "01-pattern-extra-off");
        rangeedit::patternLayoutOn() = true;
        check ("pattern_extra_on", pl.rangeEditsOn());

        // Ctrl+Alt+drag from bar 3 on Lead to bar 6 on Bass: the gesture's range
        pl.drawRangeForSnapshot ({ barX (3.0), rowY (1) }, { barX (6.0), rowY (2) });
        engine.dispatchPendingMessages();
        const auto drawn = engine.getSong().trackRange;
        check ("range_drawn", drawn.from == 3 * bar && drawn.to == 6 * bar && drawn.tracks == std::vector<int> ({ 1, 2 }),
               juce::String (drawn.from) + ".." + juce::String (drawn.to) + " tracks " + juce::String ((int) drawn.tracks.size()));
        write (ui, "02-pattern-range-two-tracks");
        zoom (ui, ui.getLocalArea (&pl, tracksArea()), 1.5f, "02b-pattern-range@1.5x");
        const auto menu = pl.rangeMenuForSnapshot();
        check ("range_menu", has (menu, tr ("Cut range")) && has (menu, tr ("Duplicate range")) && has (menu, tr ("Render range to an audio clip...")));
        saveMenus (juce::StringArray { "Range" }, std::vector<juce::PopupMenu> { menu }, prefix + "-03-range-menu.png");

        // Delete: only Lead and Bass lose bars 3..6; Drums and Pad keep their clips
        const auto before = engine.getSong().playlist;
        pl.rangeCommand (PlaylistComponent::RangeCommand::clear);
        engine.dispatchPendingMessages();
        const auto count = [&engine] (int track) {
            return (int) std::count_if (engine.getSong().playlist.begin(), engine.getSong().playlist.end(), [track] (const Clip& c) { return c.track == track; });
        };
        check ("range_delete_only_two_tracks", count (0) == 4 && count (3) == 4 && count (1) == 3 && count (2) == 3
               && std::none_of (engine.getSong().playlist.begin(), engine.getSong().playlist.end(), [bar] (const Clip& c) {
                      return (c.track == 1 || c.track == 2) && c.start < 6 * bar && c.end() > 3 * bar;
                  }));
        write (ui, "04-pattern-range-deleted");
        engine.undo();
        engine.dispatchPendingMessages();
        check ("range_delete_undo", engine.getSong().playlist == before && engine.getSong().trackRange == drawn);
        pl.rangeCommand (PlaylistComponent::RangeCommand::duplicate);
        engine.dispatchPendingMessages();
        check ("range_duplicate", engine.getSong().trackRange.from == 6 * bar && engine.getSong().trackRange.to == 9 * bar);
        write (ui, "05-pattern-range-duplicated");
        engine.undo();
        engine.dispatchPendingMessages();

        // one automation lane: Bass's volume lane, a range on it only
        autolanes::setShown (true);
        engine.editSong ([bar] (Song& s) {
            const auto a = autolanes::addLane (s, 2, { AutomationTargetKind::mixerVolume, 3, 0, 0 }, "Bass volume", 0.6f);
            if (a >= 0)
                s.automations[(std::size_t) a].points = { { 0, 0.2f, 0.0f }, { 4 * bar, 0.9f, 0.0f }, { 8 * bar, 0.3f, 0.0f } };
        });
        engine.dispatchPendingMessages();
        pl.headerOptionsChanged();
        const auto lane = engine.getSong().playlistTracks[2].lanes.empty() ? -1 : engine.getSong().playlistTracks[2].lanes[0].automation;
        TrackRange laneRange;
        laneRange.from = 2 * bar;
        laneRange.to = 6 * bar;
        laneRange.tracks = { 2 };
        laneRange.laneAutomation = lane;
        pl.setTrackRangeForSnapshot (laneRange);
        engine.dispatchPendingMessages();
        check ("lane_range", lane >= 0 && engine.getSong().trackRange.laneAutomation == lane);
        write (ui, "06-pattern-range-one-lane");
        zoom (ui, ui.getLocalArea (&pl, tracksArea().withHeight (6 * 60 + 80)), 1.5f, "06b-pattern-range-one-lane@1.5x");
        pl.setTrackRangeForSnapshot ({});
        autolanes::setShown (false);
        pl.headerOptionsChanged();
        engine.dispatchPendingMessages();

        // ---- automation write modes: the submenu, then the badge per mode on Record
        const auto recordMenu = ui.getFullRecordMenuForSnapshot();
        check ("record_menu_write_mode", has (recordMenu, tr ("Automation write mode (LYNX)")));
        const auto modes = ui.automationModeMenuForSnapshot();
        check ("mode_menu_read_default", engine.getSong().automationWriteMode == 0 && has (modes, tr ("Read: FL Studio's Record automation (a moved control writes while you hold or move it)")));
        saveMenus (juce::StringArray { "Recording", "Automation write mode (LYNX)" }, std::vector<juce::PopupMenu> { recordMenu, modes },
                   prefix + "-07-record-menu-write-mode.png");
        auto* record = find (ui, "toolbar.record");
        check ("record_button", record != nullptr);
        zoom (ui, around (record), 2.0f, "08a-pattern-record-read@2x");
        const char* names[] { "read", "touch", "latch", "write" };
        for (int m = 1; m < 4; ++m)
        {
            ui.setAutomationWriteModeForSnapshot (m);
            engine.dispatchPendingMessages();
            ui.resized();
            const auto* b = dynamic_cast<IconButton*> (record);
            check ((juce::String ("badge_") + names[m]).toRawUTF8(), b != nullptr && b->getBadge().isNotEmpty(), b != nullptr ? b->getBadge() : juce::String());
            zoom (ui, around (record), 2.0f, juce::String ("08") + juce::String::charToString ((juce::juce_wchar) ('a' + m)) + "-pattern-record-" + names[m] + "@2x");
        }
        ui.setAutomationWriteModeForSnapshot (2);
        engine.dispatchPendingMessages();
        write (ui, "09-pattern-latch-armed");

        // ---- Clip-launch: the Arrangement with a range, Automation Arm carries the badge
        {
            ui.setClipLaunchWorkflow (true);
            ui.showPanelsForSnapshot ({ workspace::Panel::browser, workspace::Panel::playlist });
            ui.setClipGridView (PlaylistArea::GridView::song);
            engine.dispatchPendingMessages();
            auto& lp = ui.getPlaylistForSnapshot();
            lp.setViewForSnapshot (110.0, 0, 0);
            check ("cliplaunch_range_on", lp.rangeEditsOn());
            // rollup 0.1.31 step 7 (Live-exact): the real gesture, a plain drag of the Select tool on empty space from bar 2
            // on track 1 to bar 4 on track 3 (Live's time selection); Ctrl+Alt+drag draws nothing here (Live scrolls with
            // it); a click without a drag leaves no range (Live's insert marker)
            {
                auto source = juce::Desktop::getInstance().getMainMouseSource();
                const auto now = juce::Time::getCurrentTime();
                const juce::ModifierKeys leftButton (juce::ModifierKeys::leftButtonModifier);
                const auto gesture = [&] (juce::Point<int> a, juce::Point<int> b, juce::ModifierKeys mods) {
                    const auto pa = a.toFloat(), pb = b.toFloat();
                    const auto held = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
                    lp.mouseDown (juce::MouseEvent (source, pa, held, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &lp, &lp, now, pa, now, 1, false));
                    if (a != b)
                        lp.mouseDrag (juce::MouseEvent (source, pb, held, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &lp, &lp, now, pa, now, 1, true));
                    lp.mouseUp (juce::MouseEvent (source, pb, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &lp, &lp, now, pa, now, 1, a != b));
                    engine.dispatchPendingMessages();
                };
                lp.setToolForSnapshot (EditTool::lasso);
                // (an empty spot: past the demo's clips, bars 10..12)
                const auto ex = [&lp] (double b) { return lp.headerWidthForSnapshot() + (int) std::lround (b * 110.0) + 2; };
                lp.setViewForSnapshot (110.0, 8, 0);
                engine.dispatchPendingMessages();
                const auto x0 = ex (2.0), x1 = ex (4.0);
                gesture ({ x0, lp.trackRowForSnapshot (0).getCentreY() }, { x0 + 2, lp.trackRowForSnapshot (0).getCentreY() }, {});
                check ("cliplaunch_click_no_range", engine.getSong().trackRange.empty());
                gesture ({ x0, lp.trackRowForSnapshot (0).getCentreY() }, { x1, lp.trackRowForSnapshot (2).getCentreY() },
                         juce::ModifierKeys (juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier));
                check ("cliplaunch_ctrl_alt_drag_no_range", engine.getSong().trackRange.empty());
                gesture ({ x0, lp.trackRowForSnapshot (0).getCentreY() }, { x1, lp.trackRowForSnapshot (2).getCentreY() }, {});
                const auto plain = engine.getSong().trackRange;
                check ("cliplaunch_plain_drag_range", plain.tracks == std::vector<int> ({ 0, 1, 2 }) && plain.from == 10 * bar && plain.to == 12 * bar,
                       juce::String (plain.from) + ".." + juce::String (plain.to) + " tracks " + juce::String ((int) plain.tracks.size()));
                lp.setTrackRangeForSnapshot ({});
                lp.setViewForSnapshot (110.0, 0, 0);
                engine.dispatchPendingMessages();
            }
            lp.drawRangeForSnapshot ({ lp.headerWidthForSnapshot() + 2 * 110 + 2, lp.trackRowForSnapshot (0).getCentreY() },
                                     { lp.headerWidthForSnapshot() + 4 * 110 + 2, lp.trackRowForSnapshot (2).getCentreY() });
            engine.dispatchPendingMessages();
            check ("cliplaunch_range_drawn", engine.getSong().trackRange.tracks.size() == 3);
            ui.setAutomationWriteModeForSnapshot (3);
            engine.dispatchPendingMessages();
            ui.resized();
            write (ui, "10-cliplaunch-arrangement-range-write");
            auto* arm = find (ui, "automationArm");
            const auto* armButton = dynamic_cast<IconButton*> (arm);
            const auto* rec = dynamic_cast<IconButton*> (find (ui, "toolbar.record"));
            check ("cliplaunch_badge_on_arm", armButton != nullptr && armButton->getBadge().isNotEmpty() && (rec == nullptr || rec->getBadge().isEmpty()));
            zoom (ui, around (arm), 2.0f, "11-cliplaunch-automation-arm-write@2x");
            saveMenus (juce::StringArray { "Range" }, std::vector<juce::PopupMenu> { lp.rangeMenuForSnapshot() }, prefix + "-12-cliplaunch-range-menu.png");
            lp.setTrackRangeForSnapshot ({});
            ui.setClipLaunchWorkflow (false);
            engine.dispatchPendingMessages();
        }

        // ---- Simple: no range gesture (BandLab has none); the badge on its Record
        {
            ui.setLayoutStyle (layoutstyle::Style::simple);
            ui.setAutomationWriteModeForSnapshot (1);
            engine.dispatchPendingMessages();
            ui.resized();
            check ("simple_no_range", ! ui.getPlaylistForSnapshot().rangeEditsOn());
            write (ui, "13-simple-touch-armed");
            auto* rec = find (ui, "toolbar.record");
            zoom (ui, around (rec), 2.0f, "14-simple-record-touch@2x");
            const auto* b = dynamic_cast<IconButton*> (rec);
            check ("simple_badge", b != nullptr && b->getBadge().isNotEmpty());
            ui.setLayoutStyle (layoutstyle::Style::pattern);
            engine.dispatchPendingMessages();
        }

        // ---- Read again: no badge anywhere
        ui.setAutomationWriteModeForSnapshot (0);
        engine.dispatchPendingMessages();
        if (const auto* b = dynamic_cast<IconButton*> (find (ui, "toolbar.record")))
            check ("read_no_badge", b->getBadge().isEmpty());

        rangeedit::patternLayoutOn() = rangeExtra;
        autolanes::setShown (lanesShown);
        layoutstyle::setCurrent (layoutstyle::Style::pattern);
        juce::File (prefix + "-checks.txt").replaceWithText (report.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }
