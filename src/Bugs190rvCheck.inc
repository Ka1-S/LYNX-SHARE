    // Round 190rv (0.1.32 open bug issues). Included in the app class of Main.cpp; --bugs190rvtest <report> and --selftest
    // "bugs190rv".
    //  #317 the power LED in the header of LYNX's own effect windows (the generic panels with FL's or LYNX's layout, the
    //       display-first editors, Facet EQ) was only drawn: a click did nothing, while the Mixer slot's LED worked. For
    //       every such editor: the LED's hint, a click turns the effect off and on again, Ctrl+click solos it (the Mixer
    //       LED's edits).
    //  #319 Linux: the pin of a plugin window ("Always on top") made JUCE destroy the window and make a new one, and the
    //       X11-embedded plugin editor in the old one went with it (a blank editor). The window and a child window in it
    //       stay the same through pin on / off now (JUCE fix 56, "fix 190rv" in round 190rv).
    juce::String bugs190rvCheck (juce::String& log, const juce::String& shots = {})
    {
        using namespace lynx;
        juce::String result = "OK";
        const auto check = [&] (const juce::String& what, bool ok, const juce::String& detail = {}) {
            log << (ok ? "ok   " : "FAIL ") << what << (detail.isNotEmpty() ? "  [" + detail + "]" : juce::String()) << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what + " " + detail;
        };
        const auto mouseOn = [] (juce::Component& target, juce::Point<float> p, juce::ModifierKeys mods) {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, p, now, 1, false);
        };

        // ---- #317 --------------------------------------------------------------------------------------------------
        {
            AudioEngine offline (false);
            offline.setSong (Song::makeDefault());
            offline.dispatchPendingMessages();
            std::vector<EffectKind> kinds = plugui::flLayoutKinds();
            for (const auto k : plugui::ownLayoutKinds())
                kinds.push_back (k);
            for (int k = 0; k <= (int) EffectKind::harmonia; ++k)
                if (ProEditorWindows::handles ((EffectKind) k))
                    kinds.push_back ((EffectKind) k);
            kinds.push_back (EffectKind::facetEq);
            std::sort (kinds.begin(), kinds.end());
            kinds.erase (std::unique (kinds.begin(), kinds.end()), kinds.end());

            int withLed = 0, toggled = 0;
            juce::StringArray noLed, failed;
            const auto enabledOf = [&offline] (int slot) {
                const auto& fx = offline.getSong().mixer[1].effects;
                return slot < (int) fx.size() && fx[(std::size_t) slot].enabled;
            };
            for (const auto kind : kinds)
            {
                // the effect in slot 1 of insert 1, another effect in slot 2 (for the solo)
                offline.editSong ([kind] (Song& s) { s.mixer[1].effects = { makeEffectSlot (EffectKind::eq), makeEffectSlot (kind) }; });
                offline.dispatchPendingMessages();
                const auto name = juce::String (effectInfo (kind).name);
                std::unique_ptr<juce::Component> editor;
                if (kind == EffectKind::facetEq)
                    editor = makeFacetEqEditor (offline, 1, 1);
                else if (ProEditorWindows::handles (kind))
                    editor = makeProEditor (offline, 1, 1);
                else
                    editor = makeNativeEffectEditor (offline, 1, 1);
                if (editor == nullptr || editor->getWidth() <= 0)
                    continue;
                auto* hints = dynamic_cast<ui::HintSource*> (editor.get());
                if (hints == nullptr)
                {
                    noLed.add (name + " (no hint source)");
                    continue;
                }
                // the LED: where the header's slot grid puts it (compact below 520 px), found through its hint
                std::optional<juce::Point<float>> led;
                const auto ledX = editor->getWidth() < (int) pluginheader::compactBelow ? 28 : 48;
                for (int y = 2; y < 40 && ! led; ++y)
                    if (hints->hintAt ({ ledX, y }).text == pluginheader::ledHint())
                        led = juce::Point<float> ((float) ledX, (float) y + 2.0f);
                if (! led)
                {
                    noLed.add (name);
                    continue;
                }
                ++withLed;
                const auto click = [&] (juce::ModifierKeys mods) {
                    editor->mouseDown (mouseOn (*editor, *led, mods.withFlags (juce::ModifierKeys::leftButtonModifier)));
                    editor->mouseUp (mouseOn (*editor, *led, mods));
                    offline.dispatchPendingMessages();
                };
                // (shots: the header of Fruity Limiter's and Facet EQ's windows, the effect on and off, 100 % and 150 %)
                const auto shoot = [&] (const juce::String& state) {
                    if (shots.isEmpty() || (kind != EffectKind::fruityLimiter && kind != EffectKind::facetEq))
                        return;
                    const auto top = juce::Rectangle<int> (0, 0, editor->getWidth(), juce::jmin (editor->getHeight(), 80));
                    for (const auto scale : { 1.0f, 1.5f })
                    {
                        juce::PNGImageFormat png;
                        const juce::File f (shots + "-317-" + (kind == EffectKind::facetEq ? "facet" : "limiter") + "-" + state + (scale > 1.0f ? "@150" : "") + ".png");
                        f.deleteFile();
                        if (auto out = f.createOutputStream())
                            png.writeImageToStream (editor->createComponentSnapshot (top, true, scale), *out);
                    }
                };
                const auto before = enabledOf (1);
                shoot ("on");
                click ({});
                shoot ("off");
                const auto off = enabledOf (1);
                click ({});
                const auto on = enabledOf (1);
                click (juce::ModifierKeys (juce::ModifierKeys::commandModifier)); // solo: only this effect of the insert on (FL's Ctrl: Cmd on macOS,
                // where JUCE makes Control+click a popup click; rollup 0.1.32 step 6, the macOS self-test)
                const auto soloed = enabledOf (1) && ! enabledOf (0);
                if (before && ! off && on && soloed)
                    ++toggled;
                else
                    failed.add (name + " (" + juce::String ((int) before) + juce::String ((int) off) + juce::String ((int) on) + juce::String ((int) soloed) + ")");
            }
            log << "     #317 editors without a header LED: " << (noLed.isEmpty() ? juce::String ("none") : noLed.joinIntoString (", ")) << "\n";
            check ("#317: a click on the header's LED of " + juce::String (withLed) + " native effect editors turns the effect off and on, Ctrl+click solos it",
                   withLed > 40 && toggled == withLed, failed.joinIntoString (", "));
        }

        // ---- #319 --------------------------------------------------------------------------------------------------
       #if JUCE_LINUX
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
        {
            auto* x11 = dlopen ("libX11.so.6", RTLD_NOW | RTLD_NOLOAD);
            using CreateWindow = unsigned long (*) (void*, unsigned long, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long);
            using QueryTree = int (*) (void*, unsigned long, unsigned long*, unsigned long*, unsigned long**, unsigned*);
            using MapWindow = int (*) (void*, unsigned long);
            using Sync = int (*) (void*, int);
            using Free = int (*) (void*);
            using Destroy = int (*) (void*, unsigned long);
            auto* createWindow = x11 != nullptr ? (CreateWindow) dlsym (x11, "XCreateSimpleWindow") : nullptr;
            auto* queryTree = x11 != nullptr ? (QueryTree) dlsym (x11, "XQueryTree") : nullptr;
            auto* mapWindow = x11 != nullptr ? (MapWindow) dlsym (x11, "XMapWindow") : nullptr;
            auto* sync = x11 != nullptr ? (Sync) dlsym (x11, "XSync") : nullptr;
            auto* xfree = x11 != nullptr ? (Free) dlsym (x11, "XFree") : nullptr;
            auto* destroy = x11 != nullptr ? (Destroy) dlsym (x11, "XDestroyWindow") : nullptr;
            auto* display = juce::lynxX11Display();
            if (createWindow != nullptr && queryTree != nullptr && mapWindow != nullptr && sync != nullptr && xfree != nullptr && destroy != nullptr
                && display != nullptr)
            {
                juce::DocumentWindow window ("bugs190rv pin", juce::Colours::black, juce::DocumentWindow::closeButton);
                window.setUsingNativeTitleBar (true);
                window.setSize (300, 200);
                window.setVisible (true);
                auto* peerBefore = window.getPeer();
                const auto handle = peerBefore != nullptr ? (unsigned long) (juce::pointer_sized_uint) peerBefore->getNativeHandle() : 0ul;
                // a stand-in for a plugin's own editor window, embedded in the plugin window as a VST3 / CLAP / LV2 editor is
                const auto child = handle != 0 ? createWindow (display, handle, 0, 0, 100, 100, 0, 0, 0) : 0ul;
                if (child != 0)
                    mapWindow (display, child);
                sync (display, 0);
                const auto childOf = [&] (unsigned long parent) {
                    unsigned long root = 0, up = 0, *children = nullptr;
                    unsigned n = 0;
                    if (queryTree (display, child, &root, &up, &children, &n) == 0)
                        return false; // the child window is gone (BadWindow is ignored by JUCE's error handler)
                    if (children != nullptr)
                        xfree (children);
                    return up == parent;
                };
                window.setAlwaysOnTop (true);
                sync (display, 0);
                const auto samePinned = window.getPeer() == peerBefore && window.getPeer() != nullptr
                                        && (unsigned long) (juce::pointer_sized_uint) window.getPeer()->getNativeHandle() == handle;
                const auto childPinned = child != 0 && childOf (handle);
                window.setAlwaysOnTop (false);
                sync (display, 0);
                const auto sameUnpinned = window.getPeer() == peerBefore && child != 0 && childOf (handle);
                check ("#319: pinning a window (always on top) keeps its X window and the editor window inside it, also when unpinned",
                       samePinned && childPinned && sameUnpinned && window.isAlwaysOnTop() == false,
                       juce::String ((int) samePinned) + juce::String ((int) childPinned) + juce::String ((int) sameUnpinned));
                if (child != 0 && childOf (handle))
                    destroy (display, child);
                sync (display, 0);
            }
            else
                log << "     #319 skipped: no X11 display\n";
        }
       #endif
        return result;
    }
