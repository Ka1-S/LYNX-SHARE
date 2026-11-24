    // --extras190qkshot <prefix> (round 190qk, the owner's policy of 11-23: LYNX extras that only change the look are on by
    // default): off screen at 100 % and @150 — Settings › LYNX extras with its "FL-exact" and "Recommended" buttons (and
    // after each), the value pill beside the mouse over a Mixer fader (general.hoverValues, which stays off), and linked track headers with
    // their effects column on (the new default) and off (playlist.trackInserts). <prefix>-checks.txt lists the checks
    // (result=OK / FAIL). Included in the app class of Main.cpp.
    bool runExtras190qkShot (const juce::String& prefix)
    {
        using namespace lynx;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::StringArray checks;
        bool allOk = true;
        const auto check = [&checks, &allOk] (const juce::String& what, bool ok) {
            checks.add ((ok ? "ok   " : "FAIL ") + what);
            allOk = allOk && ok;
        };
        const auto both = [&prefix] (juce::Component& c, juce::Rectangle<int> area, const juce::String& name) {
            area = area.getIntersection (c.getLocalBounds());
            if (area.isEmpty())
                return;
            savePng (c.createComponentSnapshot (area, true, 1.0f), prefix + "-" + name + ".png");
            savePng (c.createComponentSnapshot (area, true, 1.5f), prefix + "-" + name + "@150.png");
        };
        AudioEngine engine (false);
        const auto pump = [&engine] (int ms) {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < end)
            {
                engine.dispatchPendingMessages();
#if JUCE_LINUX || JUCE_WINDOWS
                for (int i = 0; i < 64 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
#endif
                juce::Thread::sleep (5);
            }
        };

        // --- 1. the defaults, then Settings › LYNX extras and its two buttons ---------------------------------------------
        {
            const auto on = [&engine] (const char* id) { return lynxExtraOn (engine, {}, id); };
            check ("new profile: the visual extras are on (chord highlight, inserts column, project name, Live scale); shade and hover values off",
                   ! on ("playlist.shadeFadedAudio") && on ("pianoRoll.chordHighlight") && on ("playlist.trackInserts")
                       && on ("simple.projectName") && on ("controlBar.liveScale") && ! GeneralOptions {}.hoverValues);
            check ("new profile: the gesture / shortcut / file extras are off",
                   ! on ("playlist.wheelZones") && ! on ("editing.tempTools") && ! on ("transport.wToStart") && ! on ("playlist.rangeEdits")
                       && ! on ("playlist.headerWidth") && ! on ("playlist.midiDrop") && ! on ("project.linkTracks"));
            auto page = makeExtrasPage (engine, {}, false);
            page->setSize (760, 4800);
            const auto find = [&page] (const juce::String& id) {
                juce::Component* found = nullptr;
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                    if (c.getComponentID() == id)
                        found = &c;
                    for (auto* child : c.getChildren())
                        walk (*child);
                };
                walk (*page);
                return found;
            };
            auto* flExact = dynamic_cast<juce::Button*> (find ("extras.flExact"));
            auto* recommended = dynamic_cast<juce::Button*> (find ("extras.recommended"));
            auto* shade = dynamic_cast<juce::Button*> (find ("playlist.trackInserts"));
            check ("the page has FL-exact and Recommended", flExact != nullptr && recommended != nullptr && shade != nullptr);
            if (flExact != nullptr && recommended != nullptr && shade != nullptr)
            {
                const auto top = page->getLocalArea (flExact, flExact->getLocalBounds());
                check ("the buttons fit their text", flExact->getWidth() >= juce::GlyphArrangement::getStringWidthInt (
                                                                               page->getLookAndFeel().getTextButtonFont (*dynamic_cast<juce::TextButton*> (flExact), flExact->getHeight()),
                                                                               flExact->getButtonText()));
                // (the intro and the two buttons, then the Playlist rows around "Show insert effects on linked tracks")
                const auto area = juce::Rectangle<int> (0, 0, page->getWidth(), top.getBottom() + 12);
                const auto inserts = page->getLocalArea (shade, shade->getLocalBounds());
                const auto rowArea = juce::Rectangle<int> (0, inserts.getY() - 70, page->getWidth(), 190);
                check ("default: Show insert effects on linked tracks ticked", shade->getToggleState());
                both (*page, area, "page-default");
                both (*page, rowArea, "page-default-row");
                flExact->onClick();
                check ("FL-exact: every extra off but FL's own (Esc closes, start page)",
                       ! on ("pianoRoll.chordHighlight") && ! on ("controlBar.liveScale")
                           && ! on ("simple.projectName") && ! on ("playlist.trackInserts") && ! on ("simple.loopDetect")
                           && on ("general.escCloses") && on ("general.startPage") && ! shade->getToggleState());
                both (*page, area, "page-flexact");
                both (*page, rowArea, "page-flexact-row");
                extras::userSet() = { "playlist.wheelZones" };
                recommended->onClick();
                bool allDefault = true;
                for (const auto& item : extras::items)
                    if (item.id != "general.hoverValues" && item.id != "general.stretchedClipsLoop" && item.id != "project.linkTracks"
                        && on (std::string (item.id).c_str()) != extras::defaultOn (item.id))
                        allDefault = false;
                check ("Recommended: every extra at its default, the user-set list empty", allDefault && extras::userSet().empty() && shade->getToggleState());
            }
        }

        // --- 2. the value pill beside the mouse over a Mixer fader ----------------------------------------------------------
        {
            juce::Component holder;
            holder.setSize (900, 520);
            MixerComponent mixer (engine);
            mixer.setBounds (holder.getLocalBounds());
            holder.addAndMakeVisible (mixer);
            holder.addToDesktop (0);
            holder.setVisible (true);
            pump (300);
            auto& strips = mixer.getStripsForSnapshot();
            const auto cap = strips.faderCapForSnapshot (1);
            const auto at = cap.getCentre();
            const auto hint = ui::hintFor (&strips, at);
            check ("a fader's hint has a value (\"" + hint.value + "\")", hint.value.isNotEmpty());
            // (as MainComponent::updateHoverValue draws it while hovering, the value pill with hoverValues on)
            ui::showValueTip (strips, juce::Rectangle<int> (1, 18).withCentre (at), hint.value);
            pump (100);
            const auto area = holder.getLocalArea (&strips, cap).expanded (150, 110);
            both (holder, area, "hover-fader");
            ui::hideValueTip();
            pump (50);
            both (holder, area, "hover-fader-off");
            holder.removeFromDesktop();
        }

        // --- 3. linked track headers: the effects column on (the new default) and off ---------------------------------------
        {
            MainComponent ui (engine, false);
            ui.setSize (1600, 900);
            ui.setVisible (true);
            const auto bar = (std::int64_t) 4 * ppq;
            auto song = Song::makeDefault();
            song.playlist.clear();
            ChannelDef vox;
            vox.name = "Lead vocal";
            vox.instrument = InstrumentKind::audioClip;
            vox.samplePath = "/shots/Lead vocal.wav";
            vox.mixerTrack = 12;
            song.channels.push_back (vox);
            const auto voxChannel = (int) song.channels.size() - 1;
            song.mixer[12].name = "Vox chain";
            for (auto k : { EffectKind::parametricEq, EffectKind::compressor, EffectKind::delay, EffectKind::reverb })
                song.mixer[12].effects.push_back (makeEffectSlot (k));
            song.mixer[12].effects[2].enabled = false;
            song.channels[0].mixerTrack = 3;
            song.mixer[3].name = "Kick";
            song.mixer[3].effects.push_back (makeEffectSlot (EffectKind::compressor));
            Clip kick;
            kick.kind = ClipKind::pattern;
            kick.pattern = 0;
            kick.track = 0;
            kick.length = 4 * bar;
            song.playlist.push_back (kick);
            Clip take;
            take.kind = ClipKind::audio;
            take.channel = voxChannel;
            take.track = 1;
            take.length = 8 * bar;
            song.playlist.push_back (take);
            song.playlistTracks.resize (4);
            song.playlistTracks[1].height = 70;
            ui.startNewProjectForSnapshot (std::move (song));
            ui.selectTabForSnapshot (2);
            pump (300);
            auto& pl = ui.getPlaylistForSnapshot();
            PlaylistComponent::answerLinkConfirmation = [] (const autolink::Report&) { return true; };
            pl.runAction ("playlist.linkTracks");
            PlaylistComponent::answerLinkConfirmation = nullptr;
            pump (300);
            const auto rows = [&pl] { return juce::Rectangle<int> (0, 0, juce::jmin (pl.getWidth(), 760), pl.trackRowForSnapshot (2).getBottom() + 8); };
            check ("linked headers with the effects column (default)", PlaylistComponent::showTrackInserts
                                                                          && pl.headerWidthForSnapshot() == PlaylistComponent::linkedHeaderWidth + PlaylistComponent::insertsColumnWidth);
            both (pl, rows(), "linked-inserts-on");
            pl.runAction ("playlist.trackInserts");
            pump (200);
            check ("off: the linked headers' own width", ! PlaylistComponent::showTrackInserts && pl.headerWidthForSnapshot() == PlaylistComponent::linkedHeaderWidth);
            both (pl, rows(), "linked-inserts-off");
            pl.runAction ("playlist.trackInserts");
            pump (100);
        }

        juce::File (prefix + "-checks.txt").replaceWithText (checks.joinIntoString ("\n") + "\nresult=" + (allOk ? "OK" : "FAIL") + "\n");
        return allOk;
    }
