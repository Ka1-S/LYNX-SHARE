    // --slots260shot <prefix> (round 190qf, PR #260 by codewithamoako; FL 2026.2's mixer slots) and --selftest's slots260:
    // FL 2026.2 shows 10 effect slots and adds one per click on the "+" tab under them, up to 25; the list keeps the
    // height of 10 rows and scrolls (FL's video "Coming to FL Studio 2026.2: Mixer updates"). Checks: the panel (the "+"
    // tab through real mouse events, the list scrolling with the wheel and showing the new slot, Shift+wheel moving an
    // effect, the right-click menu's Remove slot), a project with 12 effects and 14 rows saved and opened again, a project
    // without added rows saving no "slotRows", "Save a copy for 0.1.30 / 0.1.28" leaving the effects past slot 10 out with
    // one line (and their automation detached), the .flp export listing them as dropped, and FL's "Number of effect slots on
    // new mixer tracks". With a prefix: <prefix>-checks.txt and pictures at 100 % and 150 %. Included in the app class.
    juce::String slots260Checks (const juce::String& prefix)
    {
        using namespace lynx;
        juce::String checks, firstFailure;
        const auto check = [&checks, &firstFailure] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            if (! ok && firstFailure.isEmpty())
                firstFailure = what;
        };
        const auto line = [&checks] (const juce::String& what) { checks << "     " << what << "\n"; };
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) {
            if (prefix.isEmpty())
                return;
            savePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.5f), prefix + "-" + name + "@150.png");
        };

        // ---- the panel
        {
            AudioEngine offline (false);
            {
                auto song = Song::makeDefault();
                if (song.mixer.size() < 3)
                    song.mixer = Song::defaultMixer (4);
                auto& t = song.mixer[1];
                t.name = "Vox";
                t.effects.clear();
                for (const auto kind : { EffectKind::eq, EffectKind::compressor, EffectKind::reverb })
                    t.effects.push_back (makeEffectSlot (kind));
                t.effects[0].row = 0;
                t.effects[1].row = 3;
                t.effects[2].row = 9;
                offline.setSong (song);
            }
            offline.dispatchPendingMessages();
            EffectsPanel panel (offline);
            panel.setTrack (1);
            panel.fitToView (300, 760, 8);
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const auto event = [&] (juce::Point<int> p, juce::ModifierKeys mods) {
                const auto at = p.toFloat();
                return juce::MouseEvent (source, at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &panel, &panel, now, at, now, 1, false);
            };
            const auto click = [&] (juce::Point<int> p) {
                panel.mouseDown (event (p, juce::ModifierKeys::leftButtonModifier));
                panel.mouseUp (event (p, {}));
            };
            const auto wheel = [&] (juce::Point<int> p, float deltaY, juce::ModifierKeys mods) {
                juce::MouseWheelDetails w;
                w.deltaX = 0.0f;
                w.deltaY = deltaY;
                w.isReversed = false;
                w.isSmooth = false;
                w.isInertial = false;
                panel.mouseWheelMove (event (p, mods), w);
            };
            const auto rows = [&offline] { return mixerops::effectRowCount (offline.getSong().mixer[1]); };

            check ("10 slots by default, all in view, no scroll bar, the \"+\" tab under them",
                   rows() == 10 && panel.firstRowForCheck() == 0 && ! panel.slotBoundsForCheck (9).isEmpty()
                       && panel.slotScrollForCheck().isEmpty() && panel.addTabForCheck().getY() > panel.slotBoundsForCheck (9).getBottom());
            write (panel, "panel-10");

            const auto tab = panel.addTabForCheck().getCentre();
            click (tab);
            check ("\"+\" click: an 11th slot, the list scrolls to show it (FL)", rows() == 11 && panel.firstRowForCheck() == 1
                                                                                  && ! panel.slotBoundsForCheck (10).isEmpty() && panel.slotBoundsForCheck (0).isEmpty()
                                                                                  && ! panel.slotScrollForCheck().isEmpty());
            check ("the list keeps the height of 10 rows (the tab stays put)", panel.addTabForCheck().getCentre() == tab);
            for (int i = 0; i < 3; ++i)
                click (tab);
            check ("three more clicks: 14 slots, the last in view", rows() == 14 && panel.firstRowForCheck() == 4 && ! panel.slotBoundsForCheck (13).isEmpty());
            write (panel, "panel-14-bottom");

            const auto listPoint = panel.slotBoundsForCheck (8).getCentre().withX (panel.slotBoundsForCheck (8).getX() + 40);
            wheel (listPoint, 1.0f, {});
            wheel (listPoint, 1.0f, {});
            check ("the wheel over the list scrolls it (two notches up: rows 3 to 12 in view)", panel.firstRowForCheck() == 2 && rows() == 14);
            for (int i = 0; i < 4; ++i)
                wheel (listPoint, 1.0f, {});
            check ("... up to the first slot", panel.firstRowForCheck() == 0);
            write (panel, "panel-14-top");

            // Shift+wheel over the Reverb (slot 10) moves it one slot down, into slot 11 (FL 2026.2), and the list follows
            const auto reverbRow = panel.slotBoundsForCheck (9);
            wheel (reverbRow.getCentre().withX (reverbRow.getX() + 40), -1.0f, juce::ModifierKeys::shiftModifier);
            const auto rowsNow = mixerops::effectRows (offline.getSong().mixer[1].effects);
            line ("after Shift+wheel down on slot 10: rows " + juce::String (rowsNow[0] + 1) + ", " + juce::String (rowsNow[1] + 1) + ", "
                  + juce::String (rowsNow[2] + 1) + "; first row in view " + juce::String (panel.firstRowForCheck() + 1));
            check ("Shift+wheel moves the effect (slot 10 -> 11) and keeps it in view", rowsNow.size() == 3 && rowsNow[2] == 10
                                                                                      && ! panel.slotBoundsForCheck (10).isEmpty());

            // 25 at most: the tab dims and a click adds nothing
            for (int i = 0; i < 20; ++i)
                click (tab);
            check ("25 slots at most (FL 2026.2)", rows() == 25 && panel.firstRowForCheck() == 15);
            write (panel, "panel-25");

            // LYNX's own Remove slot (the tab's right-click menu): an empty last row goes; a row holding an effect stays
            bool removed = false;
            offline.editSong ([&removed] (Song& s) { removed = mixerops::removeEffectRow (s, 1); }); // (the menu's action, without the menu)
            check ("Remove slot: the empty last row goes", removed && rows() == 24);
            offline.editSong ([] (Song& s) { s.mixer[1].slotRows = 0; });
            check ("back to 10 rows: the Reverb in slot 11 keeps its row shown", rows() == 11);
            panel.setTrack (2);
            check ("another insert: its list starts at its first slot", panel.firstRowForCheck() == 0 && rows() == 11);
        }

        // ---- the project file, a 0.1.31 project, Save a copy for an older LYNX, the .flp export
        {
            auto song = Song::makeDefault();
            if (song.mixer.size() < 3)
                song.mixer = Song::defaultMixer (4);
            auto& t = song.mixer[1];
            t.name = "Bus";
            t.effects.clear();
            for (int i = 0; i < 12; ++i)
            {
                auto fx = makeEffectSlot (i % 2 == 0 ? EffectKind::utility : EffectKind::eq);
                fx.row = i;
                t.effects.push_back (fx);
            }
            t.slotRows = 14;
            Automation a;
            a.name = "Slot 12 gain";
            a.target = { AutomationTargetKind::effectParam, 1, 11, 0 };
            a.points.resize (2);
            a.points[0].value = 0.25f;
            a.points[1].tick = 384;
            a.points[1].value = 0.75f;
            song.automations.push_back (a);

            const auto json = ProjectFile::toJson (song);
            juce::String why;
            const auto back = ProjectFile::fromJson (json, why);
            check ("a project with 12 effects and 14 rows opens again the same", back && back->mixer[1].effects.size() == 12 && back->mixer[1].slotRows == 14
                                                                                    && mixerops::effectRows (back->mixer[1].effects).back() == 11
                                                                                    && ProjectFile::toJson (*back, false) == ProjectFile::toJson (song, false));

            auto plain = Song::makeDefault();
            check ("a project without added rows saves no \"slotRows\" (0.1.31's file)", ! ProjectFile::toJson (plain).contains ("slotRows"));

            for (const auto* release : { "0.1.30", "0.1.28" })
            {
                const auto copy = olderlynx::forRelease (song, release);
                const auto root = juce::JSON::parse (copy.json);
                const auto& fx = root["mixer"][1]["effects"];
                int lines = 0;
                juce::String said;
                for (const auto& l : copy.lost)
                    if (juce::String (l.text).startsWith ("This release holds 10 effect slots"))
                    {
                        ++lines;
                        said = l.detail;
                    }
                bool detached = false;
                if (auto* autos = root["automations"].getArray())
                    for (const auto& au : *autos)
                        if (au["name"].toString() == "Slot 12 gain")
                            detached = au["target"][0].toString() == "none";
                line (juce::String ("copy for ") + release + ": " + juce::String (fx.size()) + " effects, line \"" + said + "\"");
                check (juce::String ("Save a copy for ") + release + ": 10 effects, no slotRows, one line naming slots 11 and 12, their automation detached",
                       copy.ok && fx.size() == 10 && ! copy.json.contains ("slotRows") && lines == 1 && said.contains ("11") && said.contains ("12") && detached);
            }
            const auto plainCopy = olderlynx::forRelease (plain, "0.1.30");
            check ("a project without slots past 10: no line", plainCopy.ok && std::none_of (plainCopy.lost.begin(), plainCopy.lost.end(), [] (const olderlynx::Loss& l) {
                       return juce::String (l.text).startsWith ("This release holds 10 effect slots");
                   }));

            flpexport::Options options;
            flpexport::Report report;
            const auto flp = flpexport::exportFlp (song, options, report);
            const auto past = std::count_if (report.dropped.begin(), report.dropped.end(), [] (const std::string& d) { return d.find ("(in a slot past FL's 10)") != std::string::npos; });
            check (".flp export: the 2 effects past slot 10 are listed as dropped", ! flp.empty() && past == 2);
        }

        // ---- FL 2026.2's "Number of effect slots on new mixer tracks"
        {
            const auto keep = mixerops::newTrackSlots();
            auto song = Song::makeDefault();
            mixerops::newTrackSlots() = 4;
            const auto at = (int) song.mixer.size();
            const auto added = mixerops::insertTracks (song, at, 1);
            check ("a new insert gets the setting's count (4)", added == 1 && mixerops::effectRowCount (song.mixer[(std::size_t) at]) == 4);
            check ("the other inserts keep FL's 10", mixerops::effectRowCount (song.mixer[1]) == 10);
            mixerops::newTrackSlots() = keep;
        }

        if (prefix.isNotEmpty())
            juce::File (prefix + "-checks.txt").replaceWithText (checks);
        return firstFailure.isEmpty() ? juce::String ("OK") : "ERROR " + firstFailure;
    }

    bool runSlots260Shot (const juce::String& prefix)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        const auto result = slots260Checks (prefix);
        std::cout << "slots260 result=" << result << std::endl;
        return result == "OK";
    }
