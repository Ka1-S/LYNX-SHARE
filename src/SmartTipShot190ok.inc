    // --smarttipshot190ok <folder> [project.flp] (round 190ok, owner decision D1): the smart-disable tip off screen, at
    // 100 % and 150 %: the hint bar's notice (shown once) and the CPU meter's hint that keeps it. With an FL project its
    // own slots are counted (Exoplanet: 111 of its 113 slots in use are off); without one, 20 effects with Smart disable off
    // are made; the stopped load is the one round 190of measured on Exoplanet (16 % of a core). Also Settings › Audio (Safe
    // overloads). <folder>/checks.txt lists the checks. Included in the app class of Main.cpp.
    bool runSmartTipShot190ok (const juce::String& folderPath, const juce::String& project)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (lynx::ui::showingForShots(), true);
        const juce::File folder (folderPath);
        folder.createDirectory();
        juce::String checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok) {
            checks << (ok ? "ok   " : "FAIL ") << what << "\n";
            allOk = allOk && ok;
        };
        using namespace lynx;
        {
            AudioEngine offline (false);
            MainComponent ui (offline, false);
            ui.setSize (1920, 1080);
            if (project.isNotEmpty())
                ui.openFile (juce::File (project));
            else
                offline.editSong ([] (Song& s) {
                    for (int t = 1; t <= 10 && t < (int) s.mixer.size(); ++t)
                        for (const auto kind : { EffectKind::reverb, EffectKind::delay })
                        {
                            auto slot = makeEffectSlot (kind);
                            slot.smartDisable = false;
                            s.mixer[(std::size_t) t].effects.push_back (slot);
                        }
                });
            offline.dispatchPendingMessages();
            const auto c = smarttip::count (offline.getSong());
            const double stoppedLoad = 0.161;
            check ("the project's slots: " + juce::String (c.off) + " of " + juce::String (c.total) + " with Smart disable off",
                   c.total > 0 && smarttip::due (c, stoppedLoad));
            check ("a light stopped project gets no tip (1 % load)", ! smarttip::due (c, 0.01));
            auto& meter = ui.getPerfMeterForSnapshot();
            ui.showSmartDisableTip (c.off, c.total, false);
            const auto meterHint = meter.hintAt ({ meter.getWidth() / 2, meter.getHeight() / 4 }); // (the CPU half)
            check ("the CPU meter's hint starts with the tip", meterHint.text.startsWith (ui.smartDisableNoticeText (c.off, c.total)));
            const auto toolbar = ui.getToolbarBoundsForSnapshot();
            const auto save = [&ui, &folder, toolbar] (const juce::String& name) {
                for (const auto scale : { 1.0f, 1.5f })
                    savePng (ui.createComponentSnapshot (toolbar, true, scale),
                             folder.getChildFile (name + (scale > 1.0f ? "@150.png" : ".png")).getFullPathName());
            };
            const auto notice = ui.smartDisableNoticeText (c.off, c.total);
            check ("the notice names FL's macro", notice.contains ("Switch smart disable for all plugins") || notice.contains (tr ("Switch smart disable for all plugins")));
            ui.setHintForSnapshot ({ notice }); // the notice the hint bar shows once
            save ("smart-tip-notice");
            ui.setHintForSnapshot (meterHint); // the CPU meter's hint (the mouse over it)
            save ("smart-tip-cpu-hint");
            // FL's extended hint panel (View): the CPU meter's whole hint, the tip in full
            ui.showExtendedHintForSnapshot (true);
            if (auto* panel = ui.extendedHintForSnapshot())
            {
                if (auto* e = dynamic_cast<ui::ExtendedHintPanel*> (panel))
                    e->setHint (meterHint, false);
                for (const auto scale : { 1.0f, 1.5f })
                    savePng (panel->createComponentSnapshot (panel->getLocalBounds(), true, scale),
                             folder.getChildFile (scale > 1.0f ? "smart-tip-extended@150.png" : "smart-tip-extended.png").getFullPathName());
            }
            ui.showExtendedHintForSnapshot (false);
            // Tools › Macros › "Switch smart disable for all plugins" takes the tip away and switches every slot on
            ui.switchSmartDisableForAllForSnapshot();
            offline.dispatchPendingMessages();
            const auto after = smarttip::count (offline.getSong());
            check ("after the macro: 0 slots off, no tip", after.off == 0 && meter.smartDisableTip().isEmpty());
        }
        {
            // Settings › Audio with the round's two options: "Safe overloads" under "Auto close" (FL's Input / output panel,
            // checked by default) and "Smart disable for new instrument plugins" under "Smart disable" (a LYNX extra)
            AudioEngine offline (false);
            auto page = makeSettingsPanel (offline, SettingsPage::audio);
            check ("the audio settings page", page != nullptr && page->getHeight() > 0);
            if (page != nullptr)
                for (const auto scale : { 1.0f, 1.5f })
                    savePng (page->createComponentSnapshot (page->getLocalBounds(), true, scale),
                             folder.getChildFile (scale > 1.0f ? "settings-audio@150.png" : "settings-audio.png").getFullPathName());
        }
        folder.getChildFile ("checks.txt").replaceWithText (checks);
        return allOk;
    }
