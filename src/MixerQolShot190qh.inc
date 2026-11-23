    // --mixerqolshot190qh <prefix> [project.flp] (round 190qh, research190qa R5 "Mixer QoL"), offscreen: the Mixer with the
    // LYNX extras on (peak values under the meters, correlation bars), the title bar's S / M chips lit and in their restore
    // look, and the gestures through real mouse events: Alt+click on a meter clears every clip light, a click on a peak
    // value box resets it, a switch or a slot LED on a strip of a multi-selection acts on every selected strip, the chips
    // clear and restore, the input monitoring action. Also Options › Theme with the meter thresholds. <prefix>-checks.txt
    // lists the checks; pictures @100 and @150. Included in the app class of Main.cpp.
    bool runMixerQolShot190qh (const juce::String& prefix, const juce::String& project)
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
        const auto write = [&prefix] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            savePng (c.createComponentSnapshot (area, true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (area, true, 1.5f), prefix + "-" + name + "@150.png");
        };
        const auto mouseOn = [] (juce::Component& target, juce::Point<int> p, juce::ModifierKeys mods) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, p.toFloat(), now, 1, false);
        };
        const auto click = [&mouseOn] (juce::Component& target, juce::Point<int> p, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier) {
            target.mouseDown (mouseOn (target, p, mods));
            target.mouseUp (mouseOn (target, p, mods));
        };
        using namespace lynx;
        const auto keptPeaks = MixerStrips::showPeakValues, keptCorrelation = MixerStrips::showCorrelation;
        AudioEngine offline (false);
        if (project.isNotEmpty())
        {
            juce::String error;
            if (! offline.importFlpFile (juce::File (project), error))
            {
                checks << "FAIL open " << project << ": " << error << "\n";
                juce::File (prefix + "-checks.txt").replaceWithText (checks);
                return false;
            }
        }
        else
        {
            auto song = Song::makeDefault();
            song.mixer = Song::defaultMixer (10);
            for (int t = 1; t <= 8; ++t)
            {
                ChannelDef c;
                c.name = "Ch " + std::to_string (t);
                c.mixerTrack = t;
                song.channels.push_back (c);
                song.mixer[(std::size_t) t].name = "Insert " + std::to_string (t);
            }
            offline.setSong (song);
        }
        MainComponent ui (offline, false);
        ui.setSize (1920, 1080);
        ui.selectTabForSnapshot (3);
        offline.dispatchPendingMessages();
        auto& mixer = ui.getMixerForSnapshot();
        auto& strips = mixer.getStripsForSnapshot();
        offline.editSong ([] (Song&) {});
        offline.dispatchPendingMessages();
        strips.layoutForTest();
        // (the inserts used below: 2-4 get an EQ in slot 1 when they have none, for the slot LED)
        // (a project's own inserts: their slot 1 rows are cleared first, so slot 1 holds the EQ on all three)
        offline.editSong ([] (Song& s) {
            for (const auto t : { 2, 3, 4 })
                if (t < (int) s.mixer.size())
                {
                    auto& fx = s.mixer[(std::size_t) t].effects;
                    if (fx.empty() || mixerops::effectRows (fx).front() != 0)
                    {
                        auto eq = makeEffectSlot (EffectKind::parametricEq);
                        eq.row = 0;
                        fx.insert (fx.begin(), eq);
                    }
                }
            for (auto& m : s.mixer)
                m.solo = m.muted = false;
        });
        offline.dispatchPendingMessages();
        const auto mixerArea = [&ui, &mixer] {
            // the Mixer panel with its title bar (the chips and the layout box)
            // (the strips' foot too: the extras sit under the dB read-out; the left part, where the inserts are)
            const auto r = ui.getLocalArea (&mixer, mixer.getLocalBounds());
            return r.withTop (juce::jmax (0, r.getY() - 30)).withHeight (r.getHeight() + 30).withWidth (juce::jmin (r.getWidth(), 700));
        };

        // 1. extras off (FL's Mixer as before) and on
        strips.setShowPeakValues (false);
        strips.setShowCorrelation (false);
        const auto faderOff = strips.faderAreaForSnapshot (3);
        check ("extras off by default here: no peak box, no correlation bar", strips.peakValueAreaForSnapshot (3).isEmpty() && strips.correlationAreaForSnapshot (3).isEmpty());
        write (ui, mixerArea(), "mixer-extras-off");
        strips.setShowPeakValues (true);
        strips.setShowCorrelation (true);
        const auto faderOn = strips.faderAreaForSnapshot (3), peakBox = strips.peakValueAreaForSnapshot (3), corr = strips.correlationAreaForSnapshot (3);
        line ("fader " + faderOff.toString() + " -> " + faderOn.toString() + ", correlation " + corr.toString() + ", peak box " + peakBox.toString()
              + ", route arrow " + strips.routeArrowForSnapshot (3).toString());
        check ("on: the meters and the fader are shorter by the two rows, the rows under the dB read-out",
               faderOff.getHeight() - faderOn.getHeight() == MixerStrips::correlationRowHeight + MixerStrips::peakRowHeight
                   && corr.getY() > faderOn.getBottom() + 12 && peakBox.getY() >= corr.getBottom());
        check ("on: the peak box ends above the route arrow", peakBox.getBottom() <= strips.routeArrowForSnapshot (3).getY());
        const float peaks[] { 0.0f, 0.5f, 0.92f, 1.12f, 0.25f, 0.71f, 0.06f, 0.33f, 0.8f };
        const float correlations[] { 0.0f, 0.95f, 0.6f, 0.2f, -0.35f, 1.0f, 0.0f, 0.45f, 0.8f };
        for (int t = 0; t < 9 && t < (int) offline.getSong().mixer.size(); ++t)
        {
            strips.setPeakForSnapshot (t, peaks[t]);
            strips.setCorrelationForSnapshot (t, correlations[t]);
        }
        strips.latchClipForSnapshot (3);
        strips.latchClipForSnapshot (5);
        write (ui, mixerArea(), "mixer-extras-on");
        write (ui, ui.getLocalArea (&strips, strips.faderAreaForSnapshot (0).withY (strips.faderAreaForSnapshot (0).getBottom() - 60)
                                                 .withHeight (strips.routeArrowForSnapshot (3).getBottom() - strips.faderAreaForSnapshot (0).getBottom() + 66)
                                                 .withX (0).withWidth (560)),
               "strips-foot-extras");

        // 2. a click on a peak box resets it; Alt+click on a meter clears every clip light and peak value
        click (strips, peakBox.getCentre());
        check ("a click on insert 3's peak box resets it (and its clip light)", strips.heldPeak (3) == 0.0f && ! strips.clipLatchedForSnapshot (3) && strips.heldPeak (2) > 0.0f);
        strips.latchClipForSnapshot (3);
        click (strips, strips.meterAreaForSnapshot (6).getCentre(), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier));
        check ("Alt+click on insert 6's meter clears every clip light and peak value",
               ! strips.clipLatchedForSnapshot (3) && ! strips.clipLatchedForSnapshot (5) && strips.heldPeak (2) == 0.0f && strips.heldPeak (8) == 0.0f);

        // 3. a multi-selection: the mute switch and the slot LED act on every selected strip
        strips.setSelection ({ 2, 3, 4 }, 3);
        click (strips, strips.muteAreaForSnapshot (3).getCentre());
        const auto& song = offline.getSong();
        check ("mute on a strip of the selection 2-4: all three muted, insert 5 not", song.mixer[2].muted && song.mixer[3].muted && song.mixer[4].muted && ! song.mixer[5].muted);
        write (ui, mixerArea(), "mixer-selection-muted");
        click (strips, strips.muteAreaForSnapshot (3).getCentre());
        check ("again: all three back", ! song.mixer[2].muted && ! song.mixer[3].muted && ! song.mixer[4].muted);
        click (strips, strips.muteAreaForSnapshot (6).getCentre());
        check ("a strip outside the selection: only it (and the selection becomes it)", song.mixer[6].muted && ! song.mixer[2].muted && strips.getSelection() == std::vector<int> { 6 });
        click (strips, strips.muteAreaForSnapshot (6).getCentre());
        strips.setSelection ({ 2, 3, 4 }, 3);
        auto& effects = mixer.getEffectsForSnapshot();
        mixer.selectTrack (3);
        strips.setSelection ({ 2, 3, 4 }, 3);
        offline.dispatchPendingMessages();
        click (effects, effects.ledBoundsForCheck (0).getCentre());
        check ("slot 1's LED with inserts 2-4 selected: slot 1 off on all three",
               ! song.mixer[2].effects[0].enabled && ! song.mixer[3].effects[0].enabled && ! song.mixer[4].effects[0].enabled);
        click (effects, effects.ledBoundsForCheck (0).getCentre());
        check ("again: on on all three", song.mixer[2].effects[0].enabled && song.mixer[3].effects[0].enabled && song.mixer[4].effects[0].enabled);

        // 4. the title bar's S / M chips: lit, a click clears and remembers, the restore look, a click brings them back
        auto& chips = mixer.getSwitchChips();
        check ("chips: nothing shows without a solo or a mute", chips.lookOf (true) == MixerSwitchChips::Look::hidden && chips.lookOf (false) == MixerSwitchChips::Look::hidden);
        offline.editSong ([] (Song& s) {
            s.mixer[2].solo = s.mixer[5].solo = true;
            s.mixer[7].muted = true;
        });
        offline.dispatchPendingMessages();
        check ("chips: S and M lit", chips.lookOf (true) == MixerSwitchChips::Look::lit && chips.lookOf (false) == MixerSwitchChips::Look::lit);
        write (ui, mixerArea(), "mixer-chips-lit");
        const auto chipArea = ui.getLocalArea (&chips, chips.getLocalBounds()).expanded (6, 6).withTrimmedLeft (-180);
        write (ui, chipArea, "chips-lit");
        click (chips, chips.chipBounds (true).getCentre());
        check ("S chip: every solo cleared, the chip in its restore look", ! mixerops::anySolo (song) && chips.lookOf (true) == MixerSwitchChips::Look::restore);
        click (chips, chips.chipBounds (false).getCentre());
        check ("M chip: the mute cleared, restore look", ! mixerops::anyMute (song) && chips.lookOf (false) == MixerSwitchChips::Look::restore);
        write (ui, chipArea, "chips-restore");
        click (chips, chips.chipBounds (true).getCentre());
        click (chips, chips.chipBounds (false).getCentre());
        check ("both again: inserts 2 and 5 soloed, 7 muted, as before", song.mixer[2].solo && song.mixer[5].solo && ! song.mixer[3].solo && song.mixer[7].muted);
        check ("the actions do the same (Un-solo all / restore)", ui.runActionForSnapshot ("mixer.unsoloAll") && ! mixerops::anySolo (song)
                                                                     && ui.runActionForSnapshot ("mixer.unsoloAll") && song.mixer[2].solo);
        offline.editSong ([] (Song& s) {
            for (auto& m : s.mixer)
                m.solo = m.muted = false;
        });
        MixerStrips::soloMemory = {};
        MixerStrips::muteMemory = {};

        // 5. input monitoring on / off (the selected insert)
        mixer.selectTrack (4);
        const auto before = song.mixer[4].monitor;
        ui.runActionForSnapshot ("mixer.inputMonitoring");
        const auto onNow = song.mixer[4].monitor;
        ui.runActionForSnapshot ("mixer.inputMonitoring");
        check ("Input monitoring on/off: insert 4 On, then back to " + juce::String ((int) before), onNow == InputMonitor::on && song.mixer[4].monitor == before);

        // 5b. FL's measured case (REA 190qc, Thief's Insert 27: 7.82 → 3.99 → 2 → none ms as Vocodex, then each Fruity
        // Limiter, is switched off; 7.82 again with the track muted): LYNX's plan for the same steps, in the app's engine
        if (project.isNotEmpty() && offline.getSong().mixer.size() > 27)
        {
            const auto t = 27;
            const auto ms = [&offline, t] {
                const auto rate = juce::jmax (1.0, offline.getTransport().sampleRate.load());
                return juce::String (1000.0 * insertEffectsLatency (offline.getPdcPlan(), offline.getSong(), t, rate) / rate, 2) + " ms";
            };
            juce::StringArray names;
            for (const auto& fx : offline.getSong().mixer[(std::size_t) t].effects)
                names.add (juce::String (effectInfo (fx.kind).name) + (fx.kind == EffectKind::plugin ? " (" + juce::String (fx.plugin.name) + ")" : ""));
            line ("Insert 27 (" + juce::String (offline.getSong().mixer[(std::size_t) t].name) + "): " + names.joinIntoString (", "));
            const auto allOn = ms();
            auto steps = "all on " + allOn;
            int limiters = 0;
            auto offSlot = [&offline, t] (EffectKind kind, int nth) {
                offline.editSong ([kind, nth, t] (Song& s) {
                    int seen = 0;
                    for (auto& fx : s.mixer[(std::size_t) t].effects)
                        if (fx.kind == kind && seen++ == nth)
                            fx.enabled = false;
                });
            };
            offSlot (EffectKind::vocodex, 0);
            steps << ", Vocodex off " << ms();
            for (const auto& fx : offline.getSong().mixer[(std::size_t) t].effects)
                limiters += fx.kind == EffectKind::fruityLimiter ? 1 : 0;
            for (int k = 0; k < limiters; ++k)
            {
                offSlot (EffectKind::fruityLimiter, k);
                steps << ", Fruity Limiter " << (k + 1) << " off " << ms();
            }
            offline.editSong ([t] (Song& s) {
                for (auto& fx : s.mixer[(std::size_t) t].effects)
                    fx.enabled = true;
                s.mixer[(std::size_t) t].muted = true;
            });
            steps << ", all on and muted " << ms();
            line ("LYNX's track latency (FL: 7.82 / 3.99 / 2 / none, muted 7.82): " + steps);
            check ("Insert 27 muted keeps its latency (FL)", ms() == allOn);
            offline.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].muted = false; });
        }

        // 6. Options › Theme: the meter thresholds next to the Meters dot (off = the meters' own), and the Mixer with them
        if (auto page = makeSettingsPanel (offline, SettingsPage::theme))
        {
            page->setSize (760, 900);
            savePng (lynx::shot::snapshot (*page), prefix + "-theme-meters.png");
            savePng (page->createComponentSnapshot (page->getLocalBounds(), true, 1.5f), prefix + "-theme-meters@150.png");
        }
        {
            const auto kept = themeui::current();
            auto spec = kept;
            spec.meterZones = true;
            spec.meterYellowDb = -20.0f;
            spec.meterRedDb = -9.0f;
            themeui::apply (spec, false);
            check ("theme: own thresholds reach the meters", theme::colour::meterZonesOwn && theme::colour::meterYellowDb == -20.0f && theme::colour::meterRedDb == -9.0f);
            for (int t = 0; t < 9 && t < (int) offline.getSong().mixer.size(); ++t)
                strips.setPeakForSnapshot (t, peaks[t]);
            strips.repaint();
            write (ui, mixerArea(), "mixer-theme-thresholds");
            themeui::apply (kept, false);
            check ("theme: back to the meters' own", ! theme::colour::meterZonesOwn);
        }

        strips.setShowPeakValues (keptPeaks);
        strips.setShowCorrelation (keptCorrelation);
        juce::File (prefix + "-checks.txt").replaceWithText (checks + (allOk ? "result=OK\n" : "result=FAIL\n"));
        return allOk;
    }
