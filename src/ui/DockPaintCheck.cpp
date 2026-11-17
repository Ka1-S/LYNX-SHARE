#include "ui/DockPaintCheck.h"

#include "core/Effects.h"
#include "engine/AudioEngine.h"
#include "ui/MainComponent.h"
#include "ui/Motion.h"
#include "ui/PluginSkin.h"
#include "ui/NativeChild.h"
#include "ui/PluginEditors.h"
#include "ui/WindowOwner.h"
#include "ui/Workspace.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace lynx::dockpaint
{
struct Check::State
{
    AudioEngine& engine;
    MainComponent& ui;
    juce::Component& mainWindow;
    juce::File shots;
    Workspace& workspace;
    juce::Component::SafePointer<juce::DocumentWindow> gb, detached;
    juce::Component::SafePointer<juce::Component> panel; // a native panel under the editor
    juce::Rectangle<int> bounds;                          // the editor's place in the desktop
    juce::Component* content = nullptr;
    juce::Image inMain, inMain2, native; // the editor on the screen, drawn each way (twice by the main window)
    bool playingBefore = false;
    int fails = 0;

    State (AudioEngine& e, MainComponent& u, juce::Component& m, juce::File s)
        : engine (e), ui (u), mainWindow (m), shots (std::move (s)), workspace (u.workspaceForTest())
    {
    }

    juce::String expect (const juce::String& name, bool ok, const juce::String& detail = {})
    {
        fails += ok ? 0 : 1;
        return "dockcheck_" + name + "=" + (ok ? juce::String ("OK") : "FAIL") + (detail.isNotEmpty() ? " (" + detail + ")" : juce::String()) + "\n";
    }

    void* mainHandle() const { return mainWindow.getWindowHandle(); }

    juce::DocumentWindow* findGrossBeat()
    {
        for (auto* w : dockedEditorWindows())
            if (w != nullptr && w->getName().startsWith ("Gross Beat"))
                return w;
        return nullptr;
    }

    // the native panels (not anchors) that meet the editor, and the one in front of the others among them
    std::vector<juce::Component*> panelsUnder() const
    {
        std::vector<juce::Component*> out;
        if (gb == nullptr)
            return out;
        const auto r = nativechild::physicalInTop (workspace, gb->getBounds());
        for (auto* c : nativechild::attachedChildren (mainHandle()))
            if (c->isVisible() && ! (bool) c->getProperties().getWithDefault ("lynxEditorAnchor", false) && c != gb.getComponent()
                && nativechild::rectInParentWindow (*c).intersects (r))
                out.push_back (c);
        return out;
    }

    juce::Component* anchor() const { return gb != nullptr ? workspace.editorAnchorForTest (*gb) : nullptr; }

    // the editor's pixels on the screen (what the system composed), physical pixels
    juce::Image capture() const
    {
       #if JUCE_WINDOWS
        if (gb == nullptr)
            return {};
        const auto r = juce::Desktop::getInstance().getDisplays().logicalToPhysical (gb->getScreenBounds());
        auto* screen = GetDC (nullptr);
        auto* dc = CreateCompatibleDC (screen);
        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof (info.bmiHeader);
        info.bmiHeader.biWidth = r.getWidth();
        info.bmiHeader.biHeight = -r.getHeight();
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        auto* bitmap = CreateDIBSection (screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        juce::Image out;
        if (bitmap != nullptr && bits != nullptr)
        {
            auto* old = SelectObject (dc, bitmap);
            if (BitBlt (dc, 0, 0, r.getWidth(), r.getHeight(), screen, r.getX(), r.getY(), SRCCOPY | CAPTUREBLT))
            {
                out = juce::Image (juce::Image::RGB, r.getWidth(), r.getHeight(), false);
                juce::Image::BitmapData data (out, juce::Image::BitmapData::writeOnly);
                const auto* src = static_cast<const juce::uint8*> (bits);
                for (int y = 0; y < r.getHeight(); ++y)
                    for (int x = 0; x < r.getWidth(); ++x)
                    {
                        const auto* px = src + ((size_t) y * (size_t) r.getWidth() + (size_t) x) * 4;
                        data.setPixelColour (x, y, juce::Colour (px[2], px[1], px[0]));
                    }
            }
            SelectObject (dc, old);
        }
        if (bitmap != nullptr)
            DeleteObject (bitmap);
        DeleteDC (dc);
        ReleaseDC (nullptr, screen);
        return out;
       #else
        return {};
       #endif
    }

    void save (const juce::Image& image, const juce::String& name) const
    {
        if (! image.isValid() || shots == juce::File())
            return;
        shots.createDirectory();
        auto file = shots.getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }

    // the pixels that differ between two pictures of the same drawing (an animation that runs by itself)
    static std::set<std::pair<int, int>> movingMask (const juce::Image& a, const juce::Image& b)
    {
        std::set<std::pair<int, int>> out;
        if (a.isValid() && b.isValid() && a.getBounds() == b.getBounds())
            for (int y = 0; y < a.getHeight(); ++y)
                for (int x = 0; x < a.getWidth(); ++x)
                    if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                        out.insert ({ x, y });
        return out;
    }

    static int differingPixels (const juce::Image& a, const juce::Image& b, const std::set<std::pair<int, int>>* ignore = nullptr)
    {
        if (! a.isValid() || ! b.isValid() || a.getBounds() != b.getBounds())
            return -1;
        int n = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y) && (ignore == nullptr || ignore->count ({ x, y }) == 0))
                    ++n;
        return n;
    }

    // a point of the editor (desktop coordinates) no native window in front of it covers: in the main window's client
    // area, physical pixels; none: {-1, -1}
    juce::Point<int> visiblePointOfEditor (juce::Rectangle<int> avoid) const
    {
        if (gb == nullptr)
            return { -1, -1 };
        const auto r = nativechild::physicalInTop (workspace, gb->getBounds());
        for (int y = r.getY() + 6; y < r.getBottom() - 4; y += 9)
            for (int x = r.getX() + 6; x < r.getRight() - 4; x += 9)
                if (! avoid.contains (x, y))
                    return { x, y };
        return { -1, -1 };
    }

    bool hasFocusInside() const
    {
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        return f != nullptr && gb != nullptr && (f == gb.getComponent() || gb->isParentOf (f));
    }
};

Check::Check (AudioEngine& engine, MainComponent& ui, juce::Component& mainWindow, juce::File shots)
    : state (std::make_unique<State> (engine, ui, mainWindow, std::move (shots)))
{
}

Check::~Check() = default;

int Check::count()
{
    return 17;
}

juce::String Check::step (int index)
{
#if ! JUCE_WINDOWS
    return index == 0 ? "dockcheck_result=OK (not available: Windows only)\n" : juce::String();
#else
    auto& s = *state;
    auto& ws = s.workspace;
    juce::String out;
    auto bail = [&s, &out] (const juce::String& why) {
        out << s.expect ("setup", false, why);
        return out;
    };
    if (index > 0 && index < 16 && s.gb == nullptr)
        return index == 1 ? bail ("no Gross Beat window") : juce::String();
    if (index >= 5 && index < 13 && s.anchor() == nullptr)
        return s.expect ("still_in_main_window_at_step_" + juce::String (index), false);

    switch (index)
    {
        case 0:
        {
            s.engine.stop();
            auto& song = s.engine.getSong();
            int track = -1, slot = -1;
            for (int t = 0; t < (int) song.mixer.size() && track < 0; ++t)
                for (int k = 0; k < (int) song.mixer[(std::size_t) t].effects.size(); ++k)
                    if (song.mixer[(std::size_t) t].effects[(std::size_t) k].kind == EffectKind::grossBeat)
                    {
                        track = t;
                        slot = k;
                        break;
                    }
            if (track < 0)
                return bail ("the project has no Gross Beat");
            NativeEffectWindows::open (s.engine, track, slot);
            s.gb = s.findGrossBeat();
            if (s.gb == nullptr)
                return bail ("Gross Beat did not open docked");
            moveDockedEditors (true);
            // (still pictures for the pixel comparison: the editors' animated art and LYNX's animations off)
            skin::setArtMotion (false);
            skin::kickArtLayers();
            ui::motion::enabled() = false;
            s.gb->repaint();
            break;
        }
        case 1:
        {
            auto& gb = *s.gb;
            s.bounds = gb.getBounds();
            s.content = gb.getContentComponent();
            auto* a = s.anchor();
            if (a == nullptr)
                return bail ("Gross Beat is not drawn by the main window");
            out << s.expect ("in_main_window", isEditorInMainWindow (gb) && gb.getParentComponent() == &ws && ! gb.isOnDesktop()
                                                   && gb.getPeer() == s.mainWindow.getPeer());
            out << s.expect ("anchor_hidden_child", a != nullptr && nativechild::isAttached (*a) && ! a->isVisible()
                                                        && nativechild::parentOf (a->getWindowHandle()) == s.mainHandle());
            const auto under = s.panelsUnder();
            bool holed = ! under.empty();
            const auto r = nativechild::physicalInTop (ws, gb.getBounds());
            for (auto* p : under)
            {
                const auto wr = nativechild::rectInParentWindow (*p);
                holed = holed && nativechild::cutsInto (p->getWindowHandle(), r.getIntersection (wr) - wr.getPosition())
                        && nativechild::isAbove (a->getWindowHandle(), p->getWindowHandle());
            }
            s.panel = under.empty() ? nullptr : under.back();
            out << s.expect ("panels_below_have_holes", holed, juce::String ((int) under.size()) + " panels under it");
            const auto centre = gb.getLocalBounds().getCentre();
            out << s.expect ("clicks_reach_main_window", nativechild::windowAt (gb, centre) == s.mainHandle());
            s.inMain = s.capture();
            s.save (s.inMain, "gb-in-main-window.png");
            break;
        }
        case 2:
        {
            s.inMain2 = s.capture(); // (what still moves by itself is not counted below)
            moveDockedEditors (false);
            break;
        }
        case 3:
        {
            auto& gb = *s.gb;
            out << s.expect ("undock_native_child", nativechild::isAttached (gb) && ! isEditorInMainWindow (gb)
                                                        && nativechild::parentOf (gb.getWindowHandle()) == s.mainHandle());
            out << s.expect ("undock_same_place", nativechild::boundsIn (gb, ws) == s.bounds && gb.getContentComponent() == s.content,
                             nativechild::boundsIn (gb, ws).toString() + " vs " + s.bounds.toString());
            bool cleared = true;
            for (auto* c : nativechild::attachedChildren (s.mainHandle()))
                cleared = cleared && nativechild::holesOf (*c).empty();
            out << s.expect ("undock_holes_closed", cleared);
            out << s.expect ("undock_keeps_stacking", s.panel == nullptr || nativechild::isAbove (gb.getWindowHandle(), s.panel->getWindowHandle()));
            break;
        }
        case 4:
        {
            s.native = s.capture();
            s.save (s.native, "gb-native-child.png");
            const auto moving = State::movingMask (s.inMain, s.inMain2);
            const auto diff = State::differingPixels (s.inMain, s.native, &moving);
            const auto total = State::differingPixels (s.inMain, s.native);
            out << s.expect ("pixels_identical", diff == 0, diff < 0 ? "no capture" : juce::String (diff) + " px differ of "
                                                                                          + juce::String (s.native.getWidth() * s.native.getHeight())
                                                                                          + " (" + juce::String (total) + " with the "
                                                                                          + juce::String ((int) moving.size()) + " px that moved by themselves)");
            moveDockedEditors (true);
            break;
        }
        case 5:
        {
            auto& gb = *s.gb;
            out << s.expect ("dock_again_same_place", isEditorInMainWindow (gb) && gb.getBounds() == s.bounds && gb.getContentComponent() == s.content);
            out << s.expect ("dock_again_keeps_stacking", s.panel == nullptr || (s.anchor() != nullptr && nativechild::isAbove (s.anchor()->getWindowHandle(), s.panel->getWindowHandle())));
            if (s.panel != nullptr)
            {
                s.panel->toFront (false); // (as Workspace::activate raises a native panel)
                nativechild::raiseOverlays();
            }
            break;
        }
        case 6:
        {
            auto& gb = *s.gb;
            if (s.panel != nullptr)
            {
                const auto r = nativechild::physicalInTop (ws, gb.getBounds());
                const auto wr = nativechild::rectInParentWindow (*s.panel);
                out << s.expect ("raised_panel_covers_editor", nativechild::isAbove (s.panel->getWindowHandle(), s.anchor()->getWindowHandle())
                                                                   && ! nativechild::cutsInto (s.panel->getWindowHandle(), r.getIntersection (wr) - wr.getPosition()));
                const auto at = gb.getLocalArea (&ws, ws.getLocalArea (nullptr, s.panel->getScreenBounds()).getIntersection (gb.getBounds())).getCentre();
                out << s.expect ("clicks_reach_raised_panel", nativechild::windowAt (gb, at) == s.panel->getWindowHandle());
            }
            gb.toFront (true); // (the editor asked for again: NativeEffectWindows::open)
            break;
        }
        case 7:
        {
            if (s.panel != nullptr)
            {
                out << s.expect ("editor_to_front_again", nativechild::isAbove (s.anchor()->getWindowHandle(), s.panel->getWindowHandle())
                                                              && ! nativechild::holesOf (*s.panel).empty());
                // a real click (a message to the panel's own window) where only the panel is: it comes in front
                const auto pr = nativechild::rectInParentWindow (*s.panel);
                const auto er = nativechild::physicalInTop (ws, s.gb->getBounds());
                juce::Point<int> free { -1, -1 };
                for (int y = pr.getY() + 40; y < pr.getBottom() - 4 && free.x < 0; y += 13)
                    for (int x = pr.getX() + 8; x < pr.getRight() - 8 && free.x < 0; x += 13)
                        if (! er.contains (x, y))
                            free = { x, y };
                if (free.x >= 0)
                    nativechild::postClickForTest (s.panel->getWindowHandle(), free - pr.getPosition());
                out << "dockcheck_panel_click_at=" << free.toString() << "\n";
            }
            break;
        }
        case 8:
        {
            if (s.panel != nullptr)
            {
                out << s.expect ("panel_click_raises_panel", nativechild::isAbove (s.panel->getWindowHandle(), s.anchor()->getWindowHandle()));
                // a real click on a part of the editor the panel leaves free: the editor comes in front (JUCE's mouse-down)
                const auto at = s.visiblePointOfEditor (nativechild::rectInParentWindow (*s.panel));
                if (at.x >= 0)
                    nativechild::postClickForTest (s.mainHandle(), at);
                else
                    s.gb->toFront (true);
                out << "dockcheck_editor_click_at=" << (at.x >= 0 ? at.toString() : juce::String ("none free: toFront")) << "\n";
            }
            break;
        }
        case 9:
        {
            if (s.panel != nullptr)
                out << s.expect ("editor_click_raises_editor", nativechild::isAbove (s.anchor()->getWindowHandle(), s.panel->getWindowHandle()));
            // the keyboard: the focus in the editor, Space as the system delivers it plays (the main window's shortcuts)
            s.gb->grabKeyboardFocus();
            s.playingBefore = s.engine.getTransport().playing.load();
            out << s.expect ("focus_in_editor", s.hasFocusInside());
            out << s.expect ("space_posted", nativechild::pressSpaceForTest (s.mainHandle()));
            break;
        }
        case 10:
        {
            const auto now = s.engine.getTransport().playing.load();
            out << s.expect ("space_from_editor_runs_shortcut", now != s.playingBefore);
            out << s.expect ("focus_stays_in_editor", s.hasFocusInside());
            s.engine.stop();
            // accessibility: the editor is in the main window's tree
            auto* handler = s.gb->getAccessibilityHandler();
            auto* root = s.mainWindow.getAccessibilityHandler();
            bool reaches = false;
            for (auto* h = handler; h != nullptr && ! reaches; h = h->getParent())
                reaches = h == root;
            out << s.expect ("accessibility_in_main_tree", handler != nullptr && root != nullptr && reaches
                                                               && handler->getRole() == juce::AccessibilityRole::window);
            // dragged far away: the title bar stays in the desktop (as a docked window's)
            s.gb->setTopLeftPosition (-5000, -300);
            out << s.expect ("drag_clamped", s.gb->getX() == -s.gb->getWidth() + 80 && s.gb->getY() == 0, s.gb->getPosition().toString());
            s.gb->setBounds (s.bounds.withSize (s.bounds.getWidth() + 40, s.bounds.getHeight() + 30));
            break;
        }
        case 11:
        {
            bool follows = true;
            const auto r = nativechild::physicalInTop (ws, s.gb->getBounds());
            for (auto* p : s.panelsUnder())
                if (nativechild::isAbove (s.anchor()->getWindowHandle(), p->getWindowHandle()))
                {
                    const auto wr = nativechild::rectInParentWindow (*p);
                    auto inP = r.getIntersection (wr) - wr.getPosition();
                    follows = follows && nativechild::cutsInto (p->getWindowHandle(), inP.removeFromRight (2).removeFromBottom (2));
                }
            out << s.expect ("resize_moves_holes", follows);
            s.gb->setBounds (s.bounds);
            // a floating editor (Detach all new plugins) stays a window of its own
            setEditorsDetached (true);
            auto& song = s.engine.getSong();
            const auto slot = (int) song.mixer[0].effects.size();
            s.engine.editSong ([] (Song& so) { so.mixer[0].effects.push_back (makeEffectSlot (EffectKind::fruityLimiter)); }, false);
            s.engine.dispatchPendingMessages();
            NativeEffectWindows::open (s.engine, 0, slot);
            for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                if (auto* w = dynamic_cast<juce::DocumentWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)); w != nullptr && w->getName().startsWith ("Fruity Limiter"))
                    s.detached = w;
            setEditorsDetached (false);
            break;
        }
        case 12:
        {
            out << s.expect ("floating_stays_own_window", s.detached != nullptr && s.detached->isOnDesktop() && ! nativechild::isAttached (*s.detached)
                                                              && ! isEditorInMainWindow (*s.detached));
            if (s.detached != nullptr)
                s.detached->closeButtonPressed();
            break;
        }
        case 13:
        {
            // the editor in front of the Playlist while the song plays: the Playlist's playhead (on the GPU overlay of the
            // Playlist's own window) must not show inside the editor (the hole clips the overlay as it clips the frames)
            ws.show (workspace::Panel::playlist);
            s.gb->toFront (true);
            s.engine.getTransport().songMode = true;
            s.engine.play();
            break;
        }
        case 14:
        {
            juce::Image last;
            int moving = 0;
            for (int i = 0; i < 4; ++i)
            {
                auto now = s.capture();
                if (last.isValid())
                    moving += State::differingPixels (last, now);
                last = now;
                juce::Thread::sleep (40);
            }
            s.save (last, "gb-over-playlist-playing.png");
            out << "dockcheck_over_playlist_playing_shot=" << (last.isValid() ? "saved" : "none") << " (changing px across 4 shots: " << moving << ")\n";
            s.engine.stop();
            break;
        }
        case 15:
        {
            s.gb->closeButtonPressed();
            break;
        }
        case 16:
        {
            bool cleared = ws.editorWindows().empty();
            for (auto* c : nativechild::attachedChildren (s.mainHandle()))
                cleared = cleared && nativechild::holesOf (*c).empty() && ! (bool) c->getProperties().getWithDefault ("lynxEditorAnchor", false);
            out << s.expect ("closed_leaves_no_holes", s.gb == nullptr && cleared);
            out << "dockcheck_result=" << (s.fails == 0 ? "OK" : "FAIL " + juce::String (s.fails)) << "\n";
            break;
        }
        default:
            break;
    }
    return out;
#endif
}
} // namespace lynx::dockpaint
