// Issue #191's run-time check (round 190mx), see ZOrder191Test.h.
#include "ui/ZOrder191Test.h"

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "ui/DetailView.h"
#include "ui/MainComponent.h"
#include "ui/NativeChild.h"
#include "ui/PluginWindow.h"
#include "ui/WindowOwner.h"
#include "ui/Workspace.h"

#include <set>
#include <typeinfo>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace lynx
{
namespace
{
#if JUCE_WINDOWS
// `a` is above `b` among the windows of the screen (top-level windows, owned ones included)
bool aboveOnScreen (void* a, void* b)
{
    if (a == nullptr || b == nullptr)
        return false;
    for (auto h = GetWindow ((HWND) b, GW_HWNDPREV); h != nullptr; h = GetWindow (h, GW_HWNDPREV))
        if (h == (HWND) a)
            return true;
    return false;
}
#else
bool aboveOnScreen (void*, void*) { return false; }
#endif

struct ZOrderTest final : juce::Timer
{
    juce::String reportPath, report, pluginPath;
    juce::LookAndFeel* lookAndFeel = nullptr;
    std::function<void (bool)> done;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<juce::DocumentWindow> window;
    MainComponent* ui = nullptr;
    juce::AudioPluginInstance* plugin = nullptr;
    bool ok = true, finished = false;
    int stage = 0, wait = 0;
    int holdMs = juce::SystemStats::getEnvironmentVariable ("LYNX_ZTEST_HOLD_MS", {}).getIntValue();
    std::set<juce::Component*> desktopBefore;
    bool heldFloating = false;

    void line (const juce::String& text)
    {
        report << text << "\n";
        juce::File (reportPath).replaceWithText (report);
    }
    void expect (const juce::String& name, bool value)
    {
        line (name + "=" + juce::String ((int) value));
        ok = ok && value;
    }

    DetailView* detail() const { return ui != nullptr ? ui->getDetailViewForSnapshot() : nullptr; }
    juce::Component& playlistFrame() const { return ui->workspaceForTest().getFrame (workspace::Panel::playlist); }

    // The native window on screen at `local` in `c` (Windows: WindowFromPoint), for the report
    juce::String windowAt (juce::Component& c, juce::Point<int> local) const
    {
#if JUCE_WINDOWS
        const auto screen = c.localPointToGlobal (local).toFloat() * juce::Desktop::getInstance().getGlobalScaleFactor();
        auto* h = WindowFromPoint ({ (LONG) screen.x, (LONG) screen.y });
        juce::String what = h == nullptr ? "none" : h == (HWND) window->getWindowHandle() ? "main" : h == (HWND) playlistHandle() ? "playlist" : "other";
        if (h != nullptr && what == "other")
        {
            wchar_t name[64] {};
            GetClassNameW (h, name, 63);
            RECT r {};
            GetWindowRect (h, &r);
            what << "(" << juce::String (name) << " " << (int) r.left << "," << (int) r.top << "-" << (int) r.right << "," << (int) r.bottom << ")";
        }
        return what + "@" + juce::String ((int) screen.x) + "," + juce::String ((int) screen.y);
#else
        juce::ignoreUnused (c, local);
        return "-";
#endif
    }

    // A press (and its release) at `local` in `c`, through the peer that holds `c`, as the system delivers a click.
    // Rollup 0.1.31 step 2 (190nr item 4): the press goes where JUCE finds `c` under the mouse. A peer's positions carry the
    // Desktop's global scale (LYNX_ZTEST_SCALE runs); the first move that finds `c` (or a child) decides the factor. When a
    // native window covers `local` on screen (at 150 % the docked Surge XT editor nearly fills the 1240 x 648 window),
    // `scanRow` tries the rest of that row of `c`, as a user clicks the part they see. Returns whether `c` took the press.
    bool click (juce::Component& c, juce::Point<int> local, bool scanRow = false)
    {
        auto* peer = c.getPeer();
        if (peer == nullptr)
            return false;
        using MIS = juce::MouseInputSource;
        const auto global = juce::Desktop::getInstance().getGlobalScaleFactor();
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto name = c.getName().isNotEmpty() ? c.getName() : juce::String (typeid (c).name());
        const auto hits = [&] (juce::Point<int> p, float factor) {
            const auto at = peer->getComponent().getLocalPoint (&c, p).toFloat() * factor;
            peer->handleMouseEvent (MIS::InputSourceType::mouse, at, juce::ModifierKeys(), MIS::defaultPressure, MIS::defaultOrientation, juce::Time::currentTimeMillis());
            auto* under = source.getComponentUnderMouse();
            return under == &c || (under != nullptr && c.isParentOf (under));
        };
        std::vector<juce::Point<int>> points { local };
        if (scanRow)
            for (int x = 8; x < c.getWidth() - 8; x += 16)
                points.push_back ({ x, local.y });
        line ("click_" + name + "_screen=" + windowAt (c, local));
        for (const auto& p : points)
            for (const auto factor : { global, 1.0f })
                if (hits (p, factor))
                {
                    const auto at = peer->getComponent().getLocalPoint (&c, p).toFloat() * factor;
                    line ("click_" + name + "=hit at " + p.toString() + " factor " + juce::String (factor, 2));
                    const auto now = juce::Time::currentTimeMillis();
                    peer->handleMouseEvent (MIS::InputSourceType::mouse, at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), MIS::defaultPressure,
                                            MIS::defaultOrientation, now + 1);
                    peer->handleMouseEvent (MIS::InputSourceType::mouse, at, juce::ModifierKeys(), MIS::defaultPressure, MIS::defaultOrientation, now + 60);
                    return true;
                }
        line ("click_" + name + "=covered (no visible point)");
        return false;
    }

    bool clickWindowButton (int card)
    {
        auto* dv = detail();
        if (dv == nullptr)
            return false;
        const auto r = dv->windowButtonForCheck (card);
        line ("window_button_" + juce::String (card) + "=" + r.toString());
        if (r.isEmpty())
            return false;
        if (click (*dv, r.getCentre()))
            return true;
        // covered on screen by a plugin editor (a user would move it first): the button's own action, logged
        line ("window_button_" + juce::String (card) + "_pressed_directly=1");
        return dv->pressWindowButtonForCheck (card);
    }

    void snapshotDesktop()
    {
        desktopBefore.clear();
        auto& d = juce::Desktop::getInstance();
        for (int i = 0; i < d.getNumComponents(); ++i)
            desktopBefore.insert (d.getComponent (i));
    }
    // a window put on the desktop since snapshotDesktop (a floating editor)
    juce::Component* newDesktopWindow() const
    {
        auto& d = juce::Desktop::getInstance();
        for (int i = d.getNumComponents(); --i >= 0;)
            if (auto* c = d.getComponent (i); desktopBefore.count (c) == 0 && c->isVisible() && ! nativechild::isAttached (*c)
                                              && dynamic_cast<juce::DocumentWindow*> (c) != nullptr)
                return c;
        return nullptr;
    }
    // the newest docked LYNX editor (the synth's channel window)
    juce::Component* newestDockedEditor() const
    {
        const auto list = dockedEditorWindows();
        for (auto it = list.rbegin(); it != list.rend(); ++it)
            if (*it != nullptr && (*it)->isVisible())
                return *it;
        return nullptr;
    }

    void finish()
    {
        if (finished)
            return;
        finished = true;
        stopTimer();
        line (juce::String ("fix191=") + (juce::SystemStats::getEnvironmentVariable ("LYNX_FIX191", {}).trim() == "0" ? "off" : "on"));
        line (ok ? "result=OK" : "result=FAILED");
        juce::MessageManager::callAsync ([this] {
            PluginWindows::closeAll();
            setMainWindowForOwnership (nullptr);
            window = nullptr; // (the main component with it, before the engine)
            engine = nullptr;
            auto d = std::move (done);
            const auto passed = ok;
            delete this;
            if (d)
                d (passed);
        });
    }

    void start()
    {
        engine = std::make_unique<AudioEngine> (false);
        // LYNX_ZTEST_SCALE=1.5: the run at 150 % (the shots' @150)
        if (const auto scale = juce::SystemStats::getEnvironmentVariable ("LYNX_ZTEST_SCALE", {}).getFloatValue(); scale > 0.5f && scale < 4.0f)
            juce::Desktop::getInstance().setGlobalScaleFactor (scale);
        if (lookAndFeel != nullptr)
            juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel);
        window = std::make_unique<juce::DocumentWindow> ("LYNX zorder191 test", juce::Colours::darkgrey, 0);
        ui = new MainComponent (*engine, false);
        window->setUsingNativeTitleBar (true);
        window->setContentOwned (ui, false);
        // round 190nr: on the screen at every scale (1500 x 900 logical was 2250 x 1350 px at 1.5, larger than a 1920 x 1080
        // screen, and the clicks the test sends off the screen were dropped: 0.1.30 release QA). 1500 / scale x 900 / scale
        // was too small at 1.5 (the docked editor then covered the Playlist's title bar and the window button), so the
        // window takes what the primary display's work area has room for (logical px), up to 1500 x 900
        const auto scale = juce::Desktop::getInstance().getGlobalScaleFactor();
        auto room = juce::Rectangle<int> (0, 0, 1500, 900);
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            room = display->userArea;
        window->setBounds (room.getX() + 20, room.getY() + 20, juce::jmin (1500, room.getWidth() - 40), juce::jmin (900, room.getHeight() - 40));
        line ("window=" + window->getBounds().toString() + " scale=" + juce::String (scale, 2) + " work_area=" + room.toString());
        window->setVisible (true);
        setMainWindowForOwnership (window.get());
        line ("docking=" + juce::String ((int) nativechild::dockingEnabled()));
        if (! nativechild::dockingEnabled())
        {
            line ("result=OK (nothing to check: docking is off on this system)");
            finished = true;
            juce::MessageManager::callAsync ([this] {
                setMainWindowForOwnership (nullptr);
                window = nullptr;
                engine = nullptr;
                auto d = std::move (done);
                delete this;
                if (d)
                    d (true);
            });
            return;
        }

        PluginRef ref;
        if (pluginPath.isNotEmpty())
        {
            auto& host = engine->getPluginHost();
            line ("plugins_in_file=" + juce::String (host.addPluginFile (juce::File (pluginPath))));
            for (const auto& type : host.getKnownPlugins().getTypes())
                if (type.isInstrument && (juce::File (type.fileOrIdentifier).getFileName() == juce::File (pluginPath).getFileName() || type.fileOrIdentifier == pluginPath))
                {
                    ref.id = type.createIdentifierString().toStdString();
                    ref.name = type.name.toStdString();
                    break;
                }
            line ("plugin=" + juce::String (ref.name));
        }
        engine->editSong ([&ref] (Song& song) {
            while (song.mixer.size() < 4)
                song.mixer.emplace_back();
            song.channels.clear();
            ChannelDef synth;
            synth.name = "Synth 191";
            synth.instrument = InstrumentKind::synth;
            synth.mixerTrack = 1;
            song.channels.push_back (synth);
            if (! ref.id.empty())
            {
                ChannelDef p;
                p.name = ref.name;
                p.instrument = InstrumentKind::plugin;
                p.plugin = ref;
                p.mixerTrack = 2;
                song.channels.push_back (p);
            }
            while (song.playlistTracks.size() < 2)
                song.playlistTracks.emplace_back();
            for (int t = 0; t < (int) song.channels.size(); ++t)
            {
                song.playlistTracks[(std::size_t) t].mode = PlaylistTrackMode::instrument;
                song.playlistTracks[(std::size_t) t].channel = t;
                song.playlistTracks[(std::size_t) t].mixerTrack = t + 1;
            }
        });
        ui->setLayoutStyle (layoutstyle::Style::clipLaunch);
        startTimer (700);
    }

    void* playlistHandle() const { return playlistFrame().getWindowHandle(); }

    // one round: the card's window button opens its editor in front of the Playlist, a click on the Playlist brings the
    // Playlist in front, the button again closes it. `name` names the checks; `handle` finds the editor's native window.
    int round (int at, int card, const juce::String& name, const std::function<void*()>& handle)
    {
        switch (at)
        {
            case 0:
                expect (name + "_pressed", clickWindowButton (card));
                return 1;
            case 1:
            {
                auto* h = handle();
                expect (name + "_opened", h != nullptr);
                expect (name + "_docked", h != nullptr && nativechild::parentOf (h) == window->getWindowHandle());
                expect (name + "_above_playlist_when_opened", h != nullptr && nativechild::isAbove (h, playlistHandle()));
                if (holdMs > 0) // (LYNX_ZTEST_HOLD_MS: time for a screen shot of the window in front)
                {
                    line ("hold " + name + " " + juce::String (juce::Time::currentTimeMillis()));
                    startTimer (holdMs);
                }
                return 2;
            }
            case 2:
            {
                startTimer (700);
                // a click on the Playlist's title bar: the Playlist comes in front (FL's / issue #116's rule stays)
                auto& frame = playlistFrame();
                expect ("playlist_title_bar_clicked_" + name, click (frame, { frame.getWidth() / 2, 8 }, true));
                return 3;
            }
            case 3:
            {
                auto* h = handle();
                expect ("playlist_above_" + name + "_after_its_click", h != nullptr && nativechild::isAbove (playlistHandle(), h));
                expect (name + "_closed_pressed", clickWindowButton (card)); // (it toggles: the window closes)
                return 4;
            }
            default:
                expect (name + "_closed", handle() == nullptr);
                return -1;
        }
    }

    void timerCallback() override
    {
        if (finished)
            return;
        auto* dv = detail();
        auto& d = juce::Desktop::getInstance();
        juce::ignoreUnused (d);
        switch (stage)
        {
            case 0: // the Device View shows track 1's chain
            {
                if (dv == nullptr)
                {
                    expect ("detail_view", false);
                    return finish();
                }
                dv->setPage (DetailView::Page::devices);
                dv->showSelection (0, 0);
                line ("layout_clip_launch=" + juce::String ((int) ui->isClipLaunchWorkflow()));
                line ("playlist_native=" + juce::String ((int) nativechild::isAttached (playlistFrame())));
                ++stage;
                return;
            }
            case 1:
                expect ("device_view_showing", dv->isShowing());
                expect ("chain_channel_0", dv->chainChannelForCheck() == 0);
                ++stage;
                wait = 0;
                return;
            default:
                break;
        }

        // stages 2..6: the synth's channel window
        if (stage >= 2 && stage <= 6)
        {
            const auto next = round (stage - 2, 0, "synth_editor", [this] {
                auto* w = newestDockedEditor();
                return w != nullptr ? w->getWindowHandle() : nullptr;
            });
            stage = next < 0 ? 7 : 2 + next;
            return;
        }

        // stages 7..8: the hosted plugin's card (track 2): wait for it to load
        if (stage == 7)
        {
            if (pluginPath.isEmpty())
            {
                stage = 20;
                return;
            }
            if (auto* p = dynamic_cast<PluginInstrument*> (engine->getChannelInstrument (1).get()))
            {
                plugin = &p->getInstance();
                dv->showSelection (1, 0);
                ++stage;
                return;
            }
            if (++wait > 30)
            {
                expect ("plugin_loaded", false);
                stage = 20;
            }
            return;
        }
        if (stage == 8)
        {
            expect ("chain_channel_1", dv->chainChannelForCheck() == 1);
            ++stage;
            return;
        }
        if (stage >= 9 && stage <= 13)
        {
            const auto next = round (stage - 9, 0, "plugin_editor", [this] { return plugin != nullptr ? PluginWindows::hostWindowHandle (*plugin) : nullptr; });
            stage = next < 0 ? 20 : 9 + next;
            return;
        }

        // stages 20..: the Playlist floating (detached): the editor opens as a window of its own, above it
        if (stage == 20)
        {
            ui->workspaceForTest().setDetached (workspace::Panel::playlist, true);
            dv->showSelection (0, 0);
            ++stage;
            return;
        }
        if (stage == 21)
        {
            auto* top = dv->getTopLevelComponent();
            expect ("playlist_floating", ui->workspaceForTest().isDetached (workspace::Panel::playlist) && top != window.get());
            snapshotDesktop();
            expect ("floating_synth_pressed", clickWindowButton (0));
            ++stage;
            return;
        }
        if (stage == 22)
        {
            auto* w = newDesktopWindow();
            auto* top = dv->getTopLevelComponent();
            expect ("floating_synth_editor_own_window", w != nullptr);
            expect ("floating_synth_editor_above_playlist_window", w != nullptr && aboveOnScreen (w->getWindowHandle(), top->getWindowHandle()));
            if (holdMs > 0 && ! heldFloating)
            {
                heldFloating = true;
                line ("hold floating_synth " + juce::String (juce::Time::currentTimeMillis()));
                startTimer (holdMs);
                return;
            }
            startTimer (700);
            // (its window may cover the Device View's button now, as for a user: it is closed by its own close button)
            if (auto* dw = dynamic_cast<juce::DocumentWindow*> (w))
                dw->closeButtonPressed();
            if (plugin == nullptr)
            {
                stage = 30;
                return;
            }
            dv->showSelection (1, 0);
            ++stage;
            return;
        }
        if (stage == 23)
        {
            line ("floating_chain_channel=" + juce::String (dv->chainChannelForCheck()) + " open_before=" + juce::String ((int) PluginWindows::isOpen (*plugin)));
            expect ("floating_plugin_pressed", clickWindowButton (0));
            line ("open_after_press=" + juce::String ((int) PluginWindows::isOpen (*plugin)) + " crashes=" + juce::String (PluginWindows::editorCrashCount())
                  + " focus_top=" + (juce::Component::getCurrentlyFocusedComponent() != nullptr ? juce::Component::getCurrentlyFocusedComponent()->getTopLevelComponent()->getName() : juce::String ("-")));
            wait = 0;
            ++stage;
            return;
        }
        if (stage == 24)
        {
            auto* h = PluginWindows::hostWindowHandle (*plugin);
            line ("floating_plugin_open=" + juce::String ((int) PluginWindows::isOpen (*plugin)));
            if (h == nullptr && ++wait < 6) // (a window of its own may take a moment)
                return;
            auto* top = dv->getTopLevelComponent();
            expect ("floating_plugin_opened", h != nullptr);
            expect ("floating_plugin_not_docked", ! PluginWindows::isDocked (*plugin));
            expect ("floating_plugin_above_playlist_window", h != nullptr && aboveOnScreen (h, top->getWindowHandle()));
            PluginWindows::close (*plugin);
            stage = 30;
            return;
        }
        if (stage == 30)
        {
            ui->workspaceForTest().setDetached (workspace::Panel::playlist, false);
            ++stage;
            return;
        }
        finish();
    }
};
} // namespace

void runZOrder191Test (const juce::String& reportPath, const juce::String& pluginPath, juce::LookAndFeel* lookAndFeel, std::function<void (bool)> done)
{
    auto* test = new ZOrderTest();
    test->reportPath = reportPath;
    test->pluginPath = pluginPath;
    test->lookAndFeel = lookAndFeel;
    test->done = std::move (done);
    test->start();
}
} // namespace lynx
