    // --details190mfshot <prefix> <project.flp> (round 190mf, details pass 5): FL's normal meter law as the Pattern
    // layout's default mixer meter scale (the Clip-launch / Simple straight axis and FL's alternate law beside it), Gross
    // Beat's Step / Slide / Freeze switches and its mapping options menu. <prefix>-checks.txt lists the checks. Included
    // in the app class of Main.cpp.
    bool runDetailsShot190mf (const juce::String& prefix, const juce::String& project)
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
        const auto write = [&prefix] (juce::Component& c, const juce::String& name) { savePng (lynx::shot::snapshot (c), prefix + "-" + name + ".png"); };
        const auto zoomShot = [&prefix] (juce::Component& c, juce::Rectangle<int> r, float k, const juce::String& name) {
            savePng (c.createComponentSnapshot (r.getIntersection (c.getLocalBounds()), true, k), prefix + "-" + name + ".png");
        };
        using namespace lynx;

        if (project.isNotEmpty())
        {
            // ---- the mixer meters: the same levels in the Pattern layout (FL's normal law), the other layouts' straight
            // axis and FL's alternate law
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1920, 1080);
            ui.openFile (juce::File (project));
            offline.dispatchPendingMessages();
            ui.selectTabForSnapshot (3);
            offline.dispatchPendingMessages();
            auto& strips = ui.getMixerForSnapshot().getStripsForSnapshot();
            const auto feed = [&] {
                offline.getTransport().playing.store (true);
                double ms = 1000.0;
                for (int f = 0; f < 30; ++f)
                {
                    for (int k = 0; k < 8; ++k)
                        offline.getMeters().accumulate (k, 0.5f + 0.06f * (float) k, 0.45f + 0.06f * (float) k);
                    strips.frameForSnapshot (ms += 1000.0 / 60.0);
                }
                offline.getTransport().playing.store (false);
            };
            const auto wells = strips.meterAreaForSnapshot (0).getUnion (strips.meterAreaForSnapshot (5)).expanded (6);
            line ("meter wells " + wells.toString() + ", master meter " + strips.meterAreaForSnapshot (0).toString());
            check ("Pattern layout: FL's normal meter law", meterscale::current() == meterscale::Law::flNormal);
            // FL's Master meter (capture 190lx A1): 0 dB at (555 - 262) / 346 = 84.7 %, -6 dB at 69.7 %
            check ("Pattern layout: 0 dB at 85 % of the meter, -6 dB at 70 % (FL)",
                   std::abs (meters::scalePosition (0.0f) - 0.847f) < 0.004f && std::abs (meters::scalePosition (-6.0f) - 0.697f) < 0.004f);
            const auto marks = meters::scaleMarks ((float) strips.meterAreaForSnapshot (0).getHeight(), 11.0f);
            juce::StringArray markText;
            for (auto m : marks)
                markText.add (juce::String (m));
            line ("Pattern master marks: " + markText.joinIntoString (" "));
            feed();
            zoomShot (strips, wells, 3.0f, "mixer-meters-fl-normal@3x");
            zoomShot (strips, wells, 1.0f, "mixer-meters-fl-normal");
            write (strips, "mixer-pattern");
            layoutstyle::setCurrent (layoutstyle::Style::clipLaunch);
            strips.repaint();
            feed();
            check ("Clip-launch / Simple look: the straight -60 … +6 dB axis", meterscale::current() == meterscale::Law::straight
                                                                                 && std::abs (meters::scalePosition (0.0f) - 60.0f / 66.0f) < 1.0e-5f);
            zoomShot (strips, wells, 3.0f, "mixer-meters-straight@3x");
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
            meterscale::alternate() = true;
            strips.repaint();
            feed();
            check ("Alternate meter scale wins in every layout", meterscale::current() == meterscale::Law::flAlternate);
            zoomShot (strips, wells, 3.0f, "mixer-meters-alternate@3x");
            meterscale::alternate() = false;
            strips.repaint();
            // the hint bar's reading under the pointer is the law's inverse (FL: "-4.81dB 0.58" at y 304 of 209…555)
            const auto db = meterscale::dbAtPosition ((555.0f - 304.0f) / 346.0f, meterscale::current());
            line ("reading at FL's y 304: " + juce::String (db, 2) + " dB (FL: -4.81)");
            check ("the meter's hint reads FL's dB", std::abs (db + 4.81f) < 0.1f);
        }
        {
            // ---- Gross Beat: the tool column's Step / Slide / Freeze switches (DP-6) and the mapping options menu (DP-7)
            AudioEngine offline (false);
            Song gb;
            gb.mixer.resize (2);
            gb.mixer[0].name = "Master";
            gb.mixer[1].name = "Insert 1";
            gb.mixer[1].effects.push_back (makeEffectSlot (EffectKind::grossBeat));
            offline.setSong (std::move (gb));
            offline.dispatchPendingMessages();
            auto editor = makeNativeEffectEditor (offline, 1, 0, 1.0f, 0);
            showGrossBeatEnvelopeForSnapshot (*editor, 1);
            juce::PopupMenu menu;
            check ("Gross Beat: the switches (all off by default, as FL's)", grossBeatToolsForSnapshot (*editor, false, false, false, &menu));
            write (*editor, "plug-grossbeat-tools");
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (! it.getItem().text.isEmpty())
                    items.add (it.getItem().text);
            line ("mapping menu: " + items.joinIntoString (" | "));
            check ("mapping menu: FL's State and Tools items", items.contains (tr ("Copy state")) && items.contains (tr ("Scale levels..."))
                                                                 && items.contains (tr ("Decimate points...")) && items.contains (tr ("Turn all points smooth"))
                                                                 && items.contains (tr ("Create sequence...")) && items.contains (tr ("Insert point at locator"))
                                                                 && items.contains (tr ("Select all")) && items.contains (tr ("Invert")));
            saveMenus (juce::StringArray { "Gross Beat: mapping options" }, { menu }, prefix + "-menu-grossbeat-mapping.png");
            grossBeatToolsForSnapshot (*editor, true, false, true, nullptr);
            write (*editor, "plug-grossbeat-frozen");
            grossBeatToolsForSnapshot (*editor, false, false, false, nullptr);
            auto big = makeNativeEffectEditor (offline, 1, 0, 1.5f, 0);
            showGrossBeatEnvelopeForSnapshot (*big, 1);
            write (*big, "plug-grossbeat-tools@150");
        }
        {
            // ---- Fruity Limiter: KNEE held draws the compressor's transfer at the display's bottom-left (DP-26); FL's
            // capture settings (threshold at 10 % of the axis, ratio slope 0.23)
            AudioEngine offline (false);
            Song song;
            song.mixer.resize (2);
            song.mixer[0].name = "Master";
            song.mixer[1].name = "Insert 1";
            auto lim = makeEffectSlot (EffectKind::fruityLimiter);
            lim.params[8] = 0.284f;  // Comp. threshold
            lim.params[9] = 0.79f;   // Ratio
            lim.params[10] = 0.5f;   // Knee 0 %
            song.mixer[1].effects.push_back (lim);
            offline.setSong (std::move (song));
            offline.dispatchPendingMessages();
            const auto out = fldyn::limiterCompTransfer (offline.getSong().mixer[1].effects[0].params, 1.0f);
            line ("transfer at 0 dB in: " + juce::String (juce::Decibels::gainToDecibels (out), 2) + " dB out");
            check ("Fruity Limiter: the transfer compresses above the threshold, 1:1 under it",
                   out < 0.6f && std::abs (fldyn::limiterCompTransfer (offline.getSong().mixer[1].effects[0].params, 0.01f) - 0.01f) < 1.0e-4f);
            auto editor = makeNativeEffectEditor (offline, 1, 0, 1.0f, 1);
            check ("Fruity Limiter: KNEE held", limiterKneeHeldForSnapshot (*editor, true));
            write (*editor, "plug-limiter-knee-held");
            auto big = makeNativeEffectEditor (offline, 1, 0, 1.5f, 1);
            limiterKneeHeldForSnapshot (*big, true);
            write (*big, "plug-limiter-knee-held@150");
        }
        {
            // ---- Fruity Multiband Compressor's band meters (DP-29): IN (left / right) and C per band
            AudioEngine offline (false);
            Song song;
            song.mixer.resize (2);
            song.mixer[0].name = "Master";
            song.mixer[1].name = "Insert 1";
            song.mixer[1].effects.push_back (makeEffectSlot (EffectKind::fruityMultiband));
            offline.setSong (std::move (song));
            offline.dispatchPendingMessages();
            auto editor = makeNativeEffectEditor (offline, 1, 0, 1.0f, 0);
            check ("Multiband: band meters", multibandMetersForSnapshot (*editor));
            write (*editor, "plug-multiband-meters");
            auto big = makeNativeEffectEditor (offline, 1, 0, 1.5f, 0);
            multibandMetersForSnapshot (*big);
            write (*big, "plug-multiband-meters@150");
        }
        {
            // ---- Live's velocity marker hover / drag in the Clip View's Live look (DL-80)
            AudioEngine offline (false);
            Song song;
            song.channels.resize (1);
            song.channels[0].name = "Keys";
            auto& pat = song.activePattern();
            for (int k = 0; k < 4; ++k)
            {
                Note n;
                n.start = (std::int64_t) k * 4 * ticksPerStep;
                n.length = 3 * ticksPerStep;
                n.key = 60 + k * 2;
                n.velocity = 0.27f + 0.15f * (float) k;
                n.channel = 0;
                pat.notes.push_back (n);
            }
            offline.setSong (std::move (song));
            offline.setSelectedChannel (0);
            offline.dispatchPendingMessages();
            layoutstyle::setCurrent (layoutstyle::Style::clipLaunch);
            for (int dragging = 0; dragging < 2; ++dragging)
            {
                PianoRollComponent roll (offline);
                roll.setSize (760, 420);
                offline.dispatchPendingMessages();
                const auto value = roll.liveVelocityHoverForSnapshot (dragging == 1);
                line ("velocity readout " + juce::String (value));
                check (dragging == 0 ? "Live look: the hovered note's velocity marker (value " + juce::String (value) + ")" : "Live look: a velocity being dragged", value > 0);
                write (roll, dragging == 0 ? "live-velocity-hover" : "live-velocity-drag");
            }
            layoutstyle::setCurrent (layoutstyle::Style::pattern);
        }
        juce::File (prefix + "-checks.txt").replaceWithText (checks + (allOk ? "result=OK\n" : "result=FAIL\n"));
        return allOk;
    }
