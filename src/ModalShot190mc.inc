    // --modal190mc <folder> (round 190mc): the Chord helper (discussion #105) and Transistor Bass's program row
    // (discussion #202), drawn offscreen, each at 1x and at 150 % ("@150").
    //  1. The Piano roll with Am F G C (a bar each) and the C selected; beside it the Chord helper in the Pattern layout's
    //     look, then hovering F minor, the dominant key G chosen, the 7th extension; the same in the Clip-launch layout's
    //     Live look and in the Simple layout's look; the Tools menu with its item.
    //  2. Insert: F minor after the C, voice-led; the helper follows the new chord.
    //  3. Transistor Bass from FL's "Factory programs" (needs FL Studio: LYNX_FL_INSTALL): the page with FL's program
    //     row, another program chosen, the PATCH list (FL's right-click), WRITE lit after a knob edit.
    // <folder>/checks.txt lists what was checked.
    bool runModalShot190mc (const juce::File& folder)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        folder.createDirectory();
        juce::StringArray report;
        bool allOk = true;
        const auto check = [&report, &allOk] (const char* name, bool ok, const juce::String& detail = {}) {
            report.add (juce::String (name) + "=" + (ok ? "OK" : "FAIL") + (detail.isEmpty() ? juce::String() : " " + detail));
            allOk = allOk && ok;
        };
        const auto writeImage = [&] (const juce::Image& image, const juce::File& f, const juce::String& name) {
            f.deleteFile();
            bool ok = false;
            if (auto out = f.createOutputStream())
                ok = juce::PNGImageFormat().writeImageToStream (image, *out);
            check (name.toRawUTF8(), ok && image.getWidth() > 0);
        };
        const auto shoot = [&] (juce::Component& c, const juce::String& name) {
            writeImage (shot::snapshot (c), shot::file (folder.getChildFile (name + ".png")), name);
            if (! shot::given())
                writeImage (c.createComponentSnapshot (c.getLocalBounds(), true, 1.5f), folder.getChildFile (name + "@150.png"), name + "@150");
        };
        // the Piano roll and the helper side by side in one picture (the editor left, the helper's window right)
        const auto shootPair = [&] (juce::Component& roll, juce::Component& panel, const juce::String& name) {
            for (const auto scale : { 1.0f, 1.5f })
            {
                const auto w = roll.getWidth() + panel.getWidth() + 12, h = juce::jmax (roll.getHeight(), panel.getHeight());
                juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) w * scale), juce::roundToInt ((float) h * scale), true);
                {
                    juce::Graphics g (image);
                    g.fillAll (juce::Colour (0xff101114));
                    g.drawImageAt (roll.createComponentSnapshot (roll.getLocalBounds(), true, scale), 0, 0);
                    g.drawImageAt (panel.createComponentSnapshot (panel.getLocalBounds(), true, scale), juce::roundToInt ((float) (roll.getWidth() + 12) * scale), 0);
                }
                writeImage (image, folder.getChildFile (name + (scale > 1.0f ? "@150.png" : ".png")), name + (scale > 1.0f ? "@150" : ""));
            }
        };
        const auto menuText = [] (const juce::PopupMenu& m) {
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                items.add (it.getItem().isSeparator ? juce::String ("-") : it.getItem().text.upToFirstOccurrenceOf ("\t", false, false));
            return items;
        };

        // --- 1. the Chord helper ----------------------------------------------------------------------------------------
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            song.currentPattern = 0;
            auto& p = song.patterns.front();
            p.notes.clear();
            const auto bar = 4 * (std::int64_t) ppq;
            const std::vector<std::vector<int>> progression { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 }, { 60, 64, 67 } }; // Am F G C
            for (std::size_t c = 0; c < progression.size(); ++c)
                for (const auto key : progression[c])
                {
                    Note n;
                    n.channel = 0;
                    n.start = (std::int64_t) c * bar;
                    n.length = bar;
                    n.key = key;
                    p.notes.push_back (n);
                }
            p.lengthTicks = 6 * bar;
            engine.setSong (std::move (song));
            engine.setSelectedChannel (0);
            engine.dispatchPendingMessages();
            const auto selectLast = [&engine] {
                std::vector<int> last;
                const auto& notes = engine.getSong().patterns.front().notes;
                std::int64_t latest = -1;
                for (const auto& n : notes)
                    latest = std::max (latest, n.start);
                for (int i = 0; i < (int) notes.size(); ++i)
                    if (notes[(std::size_t) i].start == latest)
                        last.push_back (i);
                return last;
            };

            PianoRollComponent roll (engine);
            roll.setBounds (0, 0, 820, 440);
            roll.setVisible (true);
            engine.sendChangeMessage(); // (the editor learns its pattern and channel, as it has long done in the app)
            engine.dispatchPendingMessages();
            roll.selectMidiToolResult (selectLast());
            roll.scrollToNotes (true);
            engine.dispatchPendingMessages();
            check ("tools_menu_has_chord_helper", menuText (roll.getToolsMenuForSnapshot()).contains (tr ("Chord helper") + "..."));

            const auto target = [&roll, &engine] {
                ChordHelperPanel::Target t;
                t.pattern = engine.getSong().currentPattern;
                t.channel = engine.getSelectedChannel();
                bool selected = false;
                t.selected = roll.midiToolNotes (selected);
                return t;
            };
            const auto selectInRoll = [&roll] (const std::vector<int>& indices) { roll.selectOwnNewNotes (indices); };

            for (const auto look : { ChordHelperPanel::Look::fl, ChordHelperPanel::Look::live, ChordHelperPanel::Look::simple })
            {
                const auto tag = look == ChordHelperPanel::Look::fl ? juce::String ("pattern") : look == ChordHelperPanel::Look::live ? juce::String ("cliplaunch") : juce::String ("simple");
                ChordHelperPanel panel (engine, target, selectInRoll, look);
                panel.setVisible (true);
                const auto& a = panel.analysis();
                if (look == ChordHelperPanel::Look::fl)
                {
                    check ("key_c_ionian_from_context", a.valid && a.key.tonic == 0 && a.key.mode == modalhelp::Mode::ionian && a.contextChords == 3);
                    check ("rank1_parallel_c_minor", ! panel.relatedKeys().empty() && panel.relatedKeys().front().scale.mode == modalhelp::Mode::aeolian);
                    check ("first_borrowed_f_minor_up_a_4th", ! panel.borrowedChords().empty() && panel.borrowedChords().front().chord.root == 5
                                                                  && panel.borrowedChords().front().move.up == 5);
                    check ("no_timer_while_idle", ! panel.previewRunning());
                    const auto runs = panel.computations();
                    for (int i = 0; i < 20; ++i) // (nothing changed: nothing is computed again)
                        panel.refresh();
                    check ("computes_only_on_change", panel.computations() == runs, juce::String (panel.computations()));
                }
                shootPair (roll, panel, "modal-" + tag + "-01-open");
                shoot (panel, "modal-" + tag + "-02-panel");
                panel.hoverChordRowForShot (0);
                shoot (panel, "modal-" + tag + "-03-hover-fm");
                panel.hoverChordRowForShot (-1);
                for (int i = 0; i < (int) panel.relatedKeys().size(); ++i)
                    if (panel.relatedKeys()[(std::size_t) i].relation == modalhelp::Relation::dominant)
                        panel.selectKeyRow (i);
                if (look == ChordHelperPanel::Look::fl)
                    check ("dominant_key_lends_v_of_v", ! panel.borrowedChords().empty() && panel.borrowedChords().front().numeral == "V/V");
                shoot (panel, "modal-" + tag + "-04-dominant-key");
                panel.selectKeyRow (0);
                panel.setExtension (modalhelp::Extension::seventh);
                shoot (panel, "modal-" + tag + "-05-sevenths");
                panel.setExtension (modalhelp::Extension::triad);
            }

            // --- 2. insert F minor after the C ------------------------------------------------------------------------
            {
                ChordHelperPanel panel (engine, target, selectInRoll, ChordHelperPanel::Look::fl);
                const auto before = engine.getSong().patterns.front().notes.size();
                check ("insert_fm", panel.insertChordRow (0));
                engine.dispatchPendingMessages();
                const auto& notes = engine.getSong().patterns.front().notes;
                std::vector<int> keys;
                for (const auto& n : notes)
                    if (n.start == 4 * bar)
                        keys.push_back (n.key);
                std::sort (keys.begin(), keys.end());
                check ("inserted_voice_led_c_f_ab", notes.size() == before + 3 && keys == std::vector<int> { 60, 65, 68 });
                bool selected = false;
                check ("new_chord_selected", roll.midiToolNotes (selected).size() == 3 && selected);
                check ("helper_follows_new_chord", panel.analysis().chord.root == 5 && panel.analysis().quality == modalhelp::Quality::minor);
                roll.scrollToNotes (true);
                shootPair (roll, panel, "modal-pattern-06-inserted");
            }
        }

        // --- 3. Transistor Bass's program row --------------------------------------------------------------------------
        {
            AudioEngine engine (false);
            auto song = Song::makeDefault();
            const auto& fl = flStudioAccess();
            auto tb = natives::makeChannel (InstrumentKind::transistorbass, fl);
            check ("tb_channel_needs_fl", tb.has_value(), tb ? juce::String() : juce::String ("no FL Studio: set LYNX_FL_INSTALL"));
            if (tb)
            {
                song.channels.push_back (*tb);
                const auto index = (int) song.channels.size() - 1;
                engine.setSong (std::move (song));
                engine.dispatchPendingMessages();
                auto window = makeChannelWindowForShot (engine, index, 0);
                window->setVisible (true);
                engine.dispatchPendingMessages();
                shoot (*window, "tb-01-factory-programs");
                const auto value = [&engine, index] (int param) {
                    return modelparams::rawValue (engine.getSong().channels[(std::size_t) index], param).value_or (-1.0f);
                };
                check ("tb_program_65_shown", value (tbprog::program) == 65.0f);
                // another program, as the PROGRAM wheel does
                engine.editSong ([index] (Song& s) { modelparams::setRawValue (s.channels[(std::size_t) index], tbprog::program, 36.0f); });
                engine.dispatchPendingMessages();
                window = makeChannelWindowForShot (engine, index, 0);
                window->setVisible (true);
                shoot (*window, "tb-02-program-c3");
                check ("tb_program_36_patch_36", value (tbprog::program) == 36.0f && value (tbprog::patch) == 36.0f);
                // a knob edit: WRITE lights
                engine.editSong ([index] (Song& s) { modelparams::setRawValue (s.channels[(std::size_t) index], 2, 0.9f); });
                engine.dispatchPendingMessages();
                window->repaint();
                shoot (*window, "tb-03-write-lit");
                check ("tb_write_lit_after_edit", transistorBassBank (engine.getSong().channels[(std::size_t) index].pluginState)->edited);
                // the PATCH list (FL's right-click on the number)
                if (const auto bank = transistorBassBank (engine.getSong().channels[(std::size_t) index].pluginState))
                {
                    juce::PopupMenu list, more;
                    for (int i = 0; i < 128; ++i)
                        (i < 64 ? list : more).addItem (juce::String (i + 1) + "  " + (bank->patches[(std::size_t) i].empty() ? tr ("(no name)") : juce::String::fromUTF8 (bank->patches[(std::size_t) i].c_str())),
                                                        true, i == bank->patch, [] {});
                    list.addSeparator();
                    list.addSubMenu ("65 - 128", more);
                    saveMenus (juce::StringArray { "Patch" }, std::vector<juce::PopupMenu> { list }, shot::fileName (folder.getChildFile ("tb-04-patch-list.png").getFullPathName()));
                    check ("tb_patch_names", ! bank->patches[0].empty());
                }
            }
        }

        folder.getChildFile ("checks.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        return allOk;
    }
