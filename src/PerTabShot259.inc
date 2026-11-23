    // Round 190pw (PR #259 by FKHR26: the channel window and a hosted plugin's window size to the tab they show, as FL's
    // channel and wrapper windows do; FL 2026 captures in pr259_190pw/shots/fl). LYNX --pertabshot259 <prefix>
    // [--plugin <file>]... [--shot-scale 1.5 --shot-suffix @150]:
    // (1) every native instrument's channel window (offscreen, docked look) on each tab: the window is the tab's page,
    //     the header and the title; the header's tabs and the channel's controls do not overlap; a tab and back gives the
    //     same size; with the header hidden by the gear, the same without the header;
    // (2) a real Synth channel window closed on its Misc tab opens again on its first tab at that tab's size and at the
    //     channel's own scale (its remembered place gives the position);
    // (3) each --plugin (an instrument on a channel, an effect on insert 1) in a real window: the editor page, the
    //     Wrapper settings page (the window follows each), back, then closed on the settings page and opened again (a
    //     fixed-size editor: the editor's size again).
    // <prefix>-checks.txt (result=OK), <prefix>-sizes.txt. With LYNX_PT259_GRAB=1 each real window waits at (40, 40)
    // for an outside screen grab: <prefix>-state.txt names the picture, <prefix>-ack.txt (the same name) answers.
    // With --shot-scale the real windows use that desktop scale (the logical sizes checked are the same).
    // Included in the app class of Main.cpp (savePng).
    struct PerTabShot259 final : juce::Timer
    {
        std::function<bool()> step; // false: done
        void timerCallback() override
        {
            if (! step())
                stopTimer();
        }
    };
    std::unique_ptr<PerTabShot259> perTabShot259;

    void runPerTabShot259 (const juce::String& prefix, const juce::StringArray& pluginFiles)
    {
        using namespace lynx;
        struct State
        {
            juce::String prefix;
            juce::StringArray checks, sizes;
            bool ok = true, grab = false;
            std::shared_ptr<AudioEngine> engine;
            std::vector<std::function<void()>> steps;
            std::size_t next = 0;
            juce::uint32 wakeAt = 0, giveUpAt = 0;
            juce::String waitingFor;
            void check (const juce::String& name, bool pass)
            {
                checks.add ((pass ? "ok " : "FAIL ") + name);
                ok = ok && pass;
            }
            static juce::String text (juce::Rectangle<int> r) { return juce::String (r.getWidth()) + "x" + juce::String (r.getHeight()); }
            void pause (int ms) { wakeAt = juce::Time::getMillisecondCounter() + (juce::uint32) ms; }
            // an outside screen grab of what shows now (LYNX_PT259_GRAB=1), else a short pause for the window to paint
            void grabAs (const juce::String& name)
            {
                if (! grab)
                {
                    pause (300);
                    return;
                }
                juce::File (shot::fileName (prefix + "-ack.txt")).deleteFile();
                juce::File (shot::fileName (prefix + "-state.txt")).replaceWithText (name);
                waitingFor = name;
                giveUpAt = juce::Time::getMillisecondCounter() + 20000;
                pause (700); // (painted first)
            }
        };
        auto st = std::make_shared<State>();
        st->prefix = prefix;
        st->grab = juce::SystemStats::getEnvironmentVariable ("LYNX_PT259_GRAB", {}) == "1";
        st->engine = std::make_shared<AudioEngine> (false);
        windowplaces::fromVar ({});
        const auto header = channelFrameHeaderHeight();

        // ---- (1) every native instrument's channel window on each tab, offscreen
        {
            auto& engine = *st->engine;
            auto song = Song::makeDefault();
            std::vector<std::pair<int, InstrumentKind>> channels;
            for (int k = 0; k <= (int) InstrumentKind::lynxModel; ++k)
            {
                const auto kind = (InstrumentKind) k;
                if (kind == InstrumentKind::plugin || kind == InstrumentKind::patcher || ! hasChannelSettings (kind))
                    continue;
                auto made = natives::makeChannel (kind, flStudioAccess());
                ChannelDef def;
                def.instrument = kind;
                def.name = "Kind " + std::to_string (k);
                song.channels.push_back (made ? *made : def);
                song.channels.back().instrument = kind;
                channels.push_back ({ (int) song.channels.size() - 1, kind });
            }
            engine.setSong (song);
            engine.dispatchPendingMessages();
            int narrow = 0, overlapping = 0;
            for (const auto& [channel, kind] : channels)
            {
                // the frame alone (its strip with ▸ and the presets, as in a window with the system's title bar): its
                // size per tab, the page it shows under the strip
                auto frame = makeChannelFrame (engine, channel, 0);
                if (frame == nullptr)
                    continue;
                int tabs = 0;
                for (auto* child : frame->getChildren())
                    if (dynamic_cast<juce::Button*> (child) != nullptr && child->getY() == 0 && child->getWidth() == 44)
                        ++tabs;
                juce::StringArray line;
                juce::Rectangle<int> first;
                bool pagesFit = true;
                // round 190qd: FL's Sampler window keeps ONE size on all its tabs (the largest page's); the others per tab
                const bool oneSize = kind == InstrumentKind::sampler;
                int largestW = 0, largestH = 0;
                bool sameSize = true;
                for (int tab = 0; tab < tabs; ++tab)
                {
                    showChannelWindowTab (*frame, tab);
                    juce::Component* page = nullptr;
                    for (auto* child : frame->getChildren())
                        if (child->isVisible() && child->getY() == header && child->getHeight() > 1)
                            page = child;
                    const auto size = frame->getLocalBounds();
                    if (tab == 0)
                        first = size;
                    line.add (State::text (size) + (page != nullptr ? " (page " + State::text (page->getLocalBounds()) + ")" : juce::String (" (no page)")));
                    // the window is the shown page under the strip (wider only where the strip needs it); the Sampler's
                    // is the same on every tab, each page inside it
                    if (oneSize)
                    {
                        pagesFit = pagesFit && page != nullptr && size.getHeight() >= header + page->getHeight() && size.getWidth() >= page->getWidth();
                        sameSize = sameSize && size == first;
                        if (page != nullptr)
                        {
                            largestW = juce::jmax (largestW, page->getWidth());
                            largestH = juce::jmax (largestH, page->getHeight());
                        }
                    }
                    else
                        pagesFit = pagesFit && page != nullptr && size.getHeight() == header + page->getHeight() && size.getWidth() >= page->getWidth();
                    // the channel's controls (right) clear of the tabs (left): ▸ 20, the tabs 45 each, 8 px, the controls
                    if (size.getWidth() < 20 + 45 * tabs + 8 + channelHeaderControlsWidth())
                        ++overlapping;
                    if (page != nullptr && page->getWidth() < 20 + 45 * tabs + 8 + channelHeaderControlsWidth())
                        ++narrow;
                }
                showChannelWindowTab (*frame, 0);
                if (oneSize)
                {
                    st->check ("Sampler (FL): one window size on all " + juce::String (tabs) + " tabs", tabs >= 3 && sameSize);
                    st->check ("Sampler (FL): that size is the largest page's under the strip",
                               first.getWidth() == largestW && first.getHeight() == header + largestH);
                }
                st->check ("kind " + juce::String ((int) kind) + (oneSize ? ": each tab's page fits the window" : ": each tab's size is its page under the strip"), pagesFit);
                st->check ("kind " + juce::String ((int) kind) + ": tab 0 again gives tab 0's size", frame->getLocalBounds() == first);
                st->sizes.add ("kind " + juce::String ((int) kind) + " (" + juce::String (song.channels[(std::size_t) channel].name) + "): " + line.joinIntoString (" | "));
            }
            st->sizes.add ("pages narrower than the strip's tabs + controls: " + juce::String (narrow));
            st->check ("no channel window narrower than its strip's tabs + the channel's controls", overlapping == 0);

            // the docked look (LYNX's title bar) on a Synth channel: per tab, back, and with the header hidden by the gear
            int synth = -1;
            for (const auto& [channel, kind] : channels)
                if (kind == InstrumentKind::synth)
                    synth = channel;
            if (auto w = synth >= 0 ? makeChannelWindowForShot (engine, synth, 0) : nullptr)
            {
                const auto onFirst = w->getBounds();
                savePng (shot::snapshot (*w), shot::fileName (prefix + "-synth-tab0.png"));
                showChannelWindowTab (*w, 1);
                const auto onEnvelope = w->getBounds();
                savePng (shot::snapshot (*w), shot::fileName (prefix + "-synth-tab1.png"));
                showChannelWindowTab (*w, -1);
                const auto onMisc = w->getBounds();
                savePng (shot::snapshot (*w), shot::fileName (prefix + "-synth-misc.png"));
                showChannelWindowTab (*w, 0);
                st->sizes.add ("Synth window (docked look): plugin " + State::text (onFirst) + ", envelope " + State::text (onEnvelope) + ", misc " + State::text (onMisc));
                st->check ("Synth window: the Misc tab gives another size (its own page's)", onMisc.getWidth() != onFirst.getWidth() || onMisc.getHeight() != onFirst.getHeight());
                st->check ("Synth window: back on the plugin tab, the plugin tab's size", w->getBounds() == onFirst);
                pressChannelHeaderGear (*w);
                const auto hiddenFirst = w->getBounds();
                showChannelWindowTab (*w, -1);
                const auto hiddenMisc = w->getBounds();
                savePng (shot::snapshot (*w), shot::fileName (prefix + "-synth-misc-noheader.png"));
                showChannelWindowTab (*w, 0);
                st->check ("header hidden: each tab's size less the header", hiddenFirst.getHeight() == onFirst.getHeight() - header
                                                                               && hiddenMisc.getHeight() == onMisc.getHeight() - header && w->getBounds() == hiddenFirst);
                pressChannelHeaderGear (*w);
                st->check ("header shown again: the plugin tab's size", w->getBounds() == onFirst);
            }
            else
                st->check ("a Synth channel window", false);

            // round 190qd: the docked look on a Sampler channel: one size on its Sample, Envelope / instrument and Misc
            // tabs (FL 2026's Sampler window), also with the header hidden by the gear
            int sampler = -1;
            for (const auto& [channel, kind] : channels)
                if (kind == InstrumentKind::sampler)
                    sampler = channel;
            if (auto w = sampler >= 0 ? makeChannelWindowForShot (engine, sampler, 0) : nullptr)
            {
                const auto onSample = w->getBounds();
                savePng (shot::snapshot (*w), shot::fileName (prefix + "-sampler-tab0.png"));
                showChannelWindowTab (*w, 1);
                const auto onEnvelope = w->getBounds();
                savePng (shot::snapshot (*w), shot::fileName (prefix + "-sampler-tab1.png"));
                showChannelWindowTab (*w, -1);
                const auto onMisc = w->getBounds();
                savePng (shot::snapshot (*w), shot::fileName (prefix + "-sampler-misc.png"));
                showChannelWindowTab (*w, 0);
                st->sizes.add ("Sampler window (docked look): sample " + State::text (onSample) + ", envelope " + State::text (onEnvelope) + ", misc " + State::text (onMisc));
                st->check ("Sampler window (FL): Sample, Envelope and Misc tabs one size", onEnvelope == onSample && onMisc == onSample && w->getBounds() == onSample);
                pressChannelHeaderGear (*w);
                const auto hiddenSample = w->getBounds();
                showChannelWindowTab (*w, -1);
                const auto hiddenMisc = w->getBounds();
                showChannelWindowTab (*w, 0);
                st->check ("Sampler window, header hidden: one size, less the header", hiddenSample.getHeight() == onSample.getHeight() - header && hiddenMisc == hiddenSample);
                pressChannelHeaderGear (*w);
                st->check ("Sampler window, header shown again: the same size", w->getBounds() == onSample);
            }
            else
                st->check ("a Sampler channel window", false);
            windowplaces::fromVar ({});
            st->engine->setSong (Song::makeDefault());
        }

        // ---- (2) a real Synth channel window: closed on its Misc tab, opened again (its place) on the first tab
        auto realScale = 1.0f;
        if (shot::given() && shot::scale() != 1.0f)
            realScale = shot::scale();
        juce::Desktop::getInstance().setGlobalScaleFactor (realScale);
        st->steps.push_back ([st] {
            auto& engine = *st->engine;
            engine.editSong ([] (Song& s) {
                ChannelDef d;
                d.instrument = InstrumentKind::synth;
                d.name = "Synth 259";
                d.mixerTrack = s.claimFreeInsert();
                s.channels.push_back (d);
            });
            engine.dispatchPendingMessages();
            openChannelSettings (engine, (int) engine.getSong().channels.size() - 1);
        });
        auto channelWindow = [] () -> juce::Component* {
            for (int i = juce::Desktop::getInstance().getNumComponents(); --i >= 0;)
                if (auto* c = juce::Desktop::getInstance().getComponent (i); c != nullptr && c->getName().startsWith ("Synth 259"))
                    return c;
            return nullptr;
        };
        auto first = std::make_shared<juce::Rectangle<int>>();
        st->steps.push_back ([st, channelWindow, first] {
            auto* w = channelWindow();
            st->check ("real channel window opens", w != nullptr);
            if (w == nullptr)
                return;
            w->setTopLeftPosition (40, 40);
            *first = w->getBounds();
            st->grabAs ("channel-real-tab0");
        });
        st->steps.push_back ([st, channelWindow] {
            if (auto* w = channelWindow())
            {
                showChannelWindowTab (*w, -1);
                st->sizes.add ("real Synth window: misc " + State::text (w->getBounds()));
                st->grabAs ("channel-real-misc");
            }
        });
        st->steps.push_back ([st, channelWindow, first] {
            auto& engine = *st->engine;
            const auto channel = (int) engine.getSong().channels.size() - 1;
            const auto scaleBefore = engine.getSong().channels.back().editorScale;
            closeChannelSettings (channel);
            openChannelSettings (engine, channel);
            auto* w = channelWindow();
            st->check ("real channel window opens again", w != nullptr);
            if (w == nullptr)
                return;
            st->sizes.add ("real Synth window: first " + State::text (*first) + " at " + juce::String (first->getX()) + "," + juce::String (first->getY())
                           + "; opened again " + State::text (w->getBounds()) + " at " + juce::String (w->getX()) + "," + juce::String (w->getY()));
            st->check ("opened again after closing on Misc: the first tab's size (not Misc's)", w->getWidth() == first->getWidth() && w->getHeight() == first->getHeight());
            st->check ("opened again: where it was", w->getPosition() == first->getPosition());
            st->check ("opened again: the channel's scale unchanged", engine.getSong().channels.back().editorScale == scaleBefore);
            st->grabAs ("channel-real-again");
        });
        st->steps.push_back ([st] {
            closeChannelSettings();
            windowplaces::fromVar ({});
        });

        // ---- (3) hosted plugins: an instrument on a channel, an effect on insert 1
        for (const auto& file : pluginFiles)
        {
            struct Plugin
            {
                juce::PluginDescription type;
                bool found = false;
                int channel = -1;
                juce::String name;
                std::shared_ptr<void> owner;
                juce::AudioPluginInstance* instance = nullptr;
                juce::Rectangle<int> editorPage;
            };
            auto p = std::make_shared<Plugin>();
            st->steps.push_back ([st, p, file] {
                auto& engine = *st->engine;
                engine.getPluginHost().addPluginFile (juce::File (file));
                for (const auto& t : engine.getPluginHost().getKnownPlugins().getTypes())
                    if (! p->found && (juce::File (t.fileOrIdentifier) == juce::File (file) || juce::File (file).isAChildOf (juce::File (t.fileOrIdentifier))
                                       || juce::File (t.fileOrIdentifier).isAChildOf (juce::File (file))))
                    {
                        p->type = t;
                        p->found = true;
                    }
                st->check ("plugin found: " + file, p->found);
                if (! p->found)
                    return;
                p->name = p->type.name + (p->type.isInstrument ? " (channel)" : " (insert 1)");
                PluginRef ref;
                ref.id = p->type.createIdentifierString().toStdString();
                ref.name = p->type.name.toStdString();
                engine.editSong ([&] (Song& s) {
                    while (s.mixer.size() < 2)
                        s.mixer.emplace_back();
                    if (p->type.isInstrument)
                    {
                        ChannelDef c;
                        c.name = ref.name;
                        c.instrument = InstrumentKind::plugin;
                        c.plugin = ref;
                        c.mixerTrack = 1;
                        p->channel = (int) s.channels.size();
                        s.channels.push_back (c);
                    }
                    else
                    {
                        auto slot = makeEffectSlot (EffectKind::plugin);
                        slot.plugin = ref;
                        s.mixer[1].effects = { slot };
                    }
                });
                engine.renderOffline (0.05, 48000.0);
                st->pause (1500); // (the plugin loads)
            });
            auto open = [st, p] {
                auto& engine = *st->engine;
                if (! p->found)
                    return;
                if (p->type.isInstrument)
                {
                    auto instrument = engine.getChannelInstrument (p->channel);
                    if (auto* pi = dynamic_cast<PluginInstrument*> (instrument.get()))
                        p->owner = instrument, p->instance = &pi->getInstance();
                }
                else
                {
                    auto effect = engine.getEffect (1, 0);
                    if (auto* pe = dynamic_cast<PluginEffect*> (effect.get()))
                        p->owner = effect, p->instance = &pe->getInstance();
                }
                st->check ("plugin running: " + p->name, p->instance != nullptr);
                if (p->instance == nullptr)
                    return;
                const auto channel = p->channel;
                PluginWindows::open (p->owner, *p->instance, p->name,
                                     { &engine, [channel] (Song& s) -> PluginRef* {
                                          if (channel >= 0)
                                              return channel < (int) s.channels.size() ? &s.channels[(std::size_t) channel].plugin : nullptr;
                                          return s.mixer.size() > 1 && ! s.mixer[1].effects.empty() ? &s.mixer[1].effects[0].plugin : nullptr;
                                      },
                                       channel >= 0, channel >= 0 ? -1 : 1, channel >= 0 ? -1 : 0, {}, {} });
                st->pause (1500);
            };
            auto place = [p] {
                for (int i = juce::Desktop::getInstance().getNumComponents(); --i >= 0;)
                    if (auto* c = juce::Desktop::getInstance().getComponent (i); c != nullptr && c->getName() == p->name)
                        c->setTopLeftPosition (40, 40);
            };
            const auto tag = juce::File (file).getFileNameWithoutExtension().removeCharacters (" ").toLowerCase();
            st->steps.push_back (open);
            st->steps.push_back ([st, p, place, tag] {
                if (p->instance == nullptr)
                    return;
                place();
                const auto sizes = PluginWindows::showWindowPage (*p->instance, 0);
                st->check (p->name + ": window open", sizes.has_value());
                if (! sizes)
                    return;
                p->editorPage = sizes->first;
                const auto* editor = p->instance->getActiveEditor();
                st->sizes.add (p->name + ": editor " + (editor != nullptr ? State::text (editor->getLocalBounds()) : juce::String ("none"))
                               + (editor != nullptr && editor->isResizable() ? " (resizable)" : " (fixed)") + ", window " + State::text (sizes->first)
                               + ", content " + State::text (sizes->second));
                if (editor != nullptr)
                    st->check (p->name + ": the content is the editor under the strip (no wider than the editor or the strip's need)",
                               sizes->second.getHeight() == editor->getHeight() + 46 && sizes->second.getWidth() >= editor->getWidth());
                st->grabAs (tag + "-plugin");
            });
            st->steps.push_back ([st, p, tag] {
                if (p->instance == nullptr)
                    return;
                const auto sizes = PluginWindows::showWindowPage (*p->instance, 1);
                if (! sizes)
                    return;
                st->sizes.add (p->name + ": settings page window " + State::text (sizes->first) + ", content " + State::text (sizes->second));
                st->check (p->name + ": the Wrapper settings page gives its own size (FL)", sizes->first.getWidth() != p->editorPage.getWidth() || sizes->first.getHeight() != p->editorPage.getHeight());
                st->grabAs (tag + "-settings");
            });
            st->steps.push_back ([st, p, tag] {
                if (p->instance == nullptr)
                    return;
                const auto sizes = PluginWindows::showWindowPage (*p->instance, 0);
                st->check (p->name + ": back on the editor, the editor page's size", sizes.has_value() && sizes->first.getWidth() == p->editorPage.getWidth()
                                                                                         && sizes->first.getHeight() == p->editorPage.getHeight());
                // closed on the settings page, then opened again
                PluginWindows::showWindowPage (*p->instance, 1);
                PluginWindows::close (*p->instance);
                st->pause (500);
            });
            st->steps.push_back (open);
            st->steps.push_back ([st, p, tag] {
                if (p->instance == nullptr)
                    return;
                const auto sizes = PluginWindows::showWindowPage (*p->instance, -1);
                const auto* editor = p->instance->getActiveEditor();
                st->sizes.add (p->name + ": opened again after closing on the settings page: window "
                               + (sizes ? State::text (sizes->first) + " at " + juce::String (sizes->first.getX()) + "," + juce::String (sizes->first.getY()) : juce::String ("none")));
                if (editor != nullptr && ! editor->isResizable())
                    st->check (p->name + ": opened again, a fixed editor's window is the editor page's size", sizes.has_value() && sizes->first.getWidth() == p->editorPage.getWidth()
                                                                                                             && sizes->first.getHeight() == p->editorPage.getHeight());
                st->check (p->name + ": opened again where it was", sizes.has_value() && sizes->first.getPosition() == p->editorPage.getPosition());
                st->grabAs (tag + "-again");
            });
            st->steps.push_back ([st, p] {
                if (p->instance != nullptr)
                    PluginWindows::close (*p->instance);
                p->owner.reset();
                p->instance = nullptr;
                windowplaces::fromVar ({});
                st->pause (300);
            });
        }

        auto finish = [this, st] {
            PluginWindows::closeAll();
            closeChannelSettings();
            windowplaces::fromVar ({});
            juce::Desktop::getInstance().setGlobalScaleFactor (1.0f);
            juce::File (shot::fileName (st->prefix + "-sizes.txt")).replaceWithText (st->sizes.joinIntoString ("\n") + "\n");
            juce::File (shot::fileName (st->prefix + "-checks.txt")).replaceWithText (st->checks.joinIntoString ("\n") + "\nresult=" + (st->ok ? "OK" : "FAIL") + "\n");
            juce::File (shot::fileName (st->prefix + "-state.txt")).replaceWithText ("done");
            st->engine.reset();
            setApplicationReturnValue (st->ok ? 0 : 1);
            quit();
        };
        perTabShot259 = std::make_unique<PerTabShot259>();
        perTabShot259->step = [st, finish] {
            const auto now = juce::Time::getMillisecondCounter();
            if (st->waitingFor.isNotEmpty())
            {
                const auto ack = juce::File (shot::fileName (st->prefix + "-ack.txt"));
                if (ack.loadFileAsString().trim() == st->waitingFor || now > st->giveUpAt)
                {
                    if (now > st->giveUpAt)
                        st->sizes.add ("(no screen grab for " + st->waitingFor + ")");
                    st->waitingFor = {};
                }
                return true;
            }
            if (now < st->wakeAt)
                return true;
            if (st->next >= st->steps.size())
            {
                finish();
                return false;
            }
            st->steps[st->next++]();
            return true;
        };
        perTabShot259->startTimer (50);
    }
