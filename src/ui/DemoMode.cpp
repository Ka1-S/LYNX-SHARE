// Round 190np: LYNX --demo, the view operations the tutorial-video pipeline uses through the Remote Control API
// (ui/DemoMode.h, docs/DEMO_MODE.md). They run on the message thread (the API's dispatch posts every request there).
// A control is found by its component ID (what the first-song tour uses), else by its name or button text; a click runs
// the control's own action (a button's click, a combo box's menu, else a mouse press and release delivered to the
// component itself): no input reaches the operating system.

#include "ui/DemoMode.h"

#include <atomic>
#include <optional>
#include <stdexcept>

#include "core/SessionIds.h"
#include "engine/LiveSessions.h"
#include "ui/I18n.h"
#include "ui/LiveSessionUi.h"
#include "ui/ClipGridComponent.h"
#include "ui/MainComponent.h"
#include "ui/MixerComponent.h"
#include "ui/PolyEditor.h"

namespace lynx
{
namespace demo
{
namespace
{
std::atomic<bool> on { false };
}

bool active() noexcept { return on.load(); }
void setActive (bool value) noexcept { on.store (value); }

juce::StringArray operations()
{
    return { "ui.window", "ui.find", "ui.click", "ui.hover", "ui.list", "ui.layout", "ui.select_track", "ui.show", "ui.welcome", "ui.clipmenu", "ui.polyeditor",
             "ui.open", "ui.action", "ui.editor", "ui.place", "ui.polynotes", "session.start", "session.status" };
}

bool isLocalRelay (const juce::String& url)
{
    const auto u = url.trim().toLowerCase();
    for (const auto* prefix : { "http://127.0.0.1:", "http://localhost:", "http://[::1]:", "ws://127.0.0.1:", "ws://localhost:" })
        if (u.startsWith (prefix))
        {
            const auto rest = u.fromFirstOccurrenceOf (prefix, false, false);
            const auto port = rest.upToFirstOccurrenceOf ("/", false, false);
            return port.isNotEmpty() && port.containsOnly ("0123456789") && port.getIntValue() > 0 && port.getIntValue() < 65536;
        }
    return false;
}
} // namespace demo

namespace
{
using Fields = std::initializer_list<std::pair<const char*, juce::var>>;

juce::var object (Fields fields)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [name, value] : fields)
        o->setProperty (name, value);
    return o;
}

juce::var rect (juce::Rectangle<int> r)
{
    return object ({ { "x", r.getX() }, { "y", r.getY() }, { "width", r.getWidth() }, { "height", r.getHeight() } });
}

[[noreturn]] void fail (const juce::String& why) { throw std::runtime_error (why.toStdString()); }

juce::String text (const juce::var& args, const char* field, bool required = true)
{
    if (! args[field].isString() || args[field].toString().isEmpty())
    {
        if (required)
            fail (juce::String (field) + " must be a non-empty string");
        return {};
    }
    if (args[field].toString().length() > 200)
        fail (juce::String (field) + " is too long");
    return args[field].toString();
}

juce::String kindOf (juce::Component& c)
{
    if (dynamic_cast<juce::ToggleButton*> (&c) != nullptr) return "toggle";
    if (dynamic_cast<juce::Button*> (&c) != nullptr) return "button";
    if (dynamic_cast<juce::ComboBox*> (&c) != nullptr) return "combo";
    if (dynamic_cast<juce::Slider*> (&c) != nullptr) return "slider";
    if (dynamic_cast<juce::TextEditor*> (&c) != nullptr) return "text";
    if (dynamic_cast<juce::Label*> (&c) != nullptr) return "label";
    return "component";
}

// What a script needs of a control: its identity, its screen rectangle (logical pixels: post.py draws the cursor, the
// ripple, the zoom and the callout there), its place in the window, its state
juce::var describe (juce::Component& window, juce::Component& c)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", c.getComponentID());
    o->setProperty ("name", c.getName());
    o->setProperty ("kind", kindOf (c));
    if (auto* b = dynamic_cast<juce::Button*> (&c))
    {
        o->setProperty ("text", b->getButtonText());
        o->setProperty ("on", b->getToggleState());
    }
    else if (auto* box = dynamic_cast<juce::ComboBox*> (&c))
        o->setProperty ("text", box->getText());
    else if (auto* label = dynamic_cast<juce::Label*> (&c))
        o->setProperty ("text", label->getText());
    o->setProperty ("enabled", c.isEnabled());
    o->setProperty ("showing", c.isShowing());
    o->setProperty ("screen", rect (c.getScreenBounds()));
    o->setProperty ("window", rect (onboardui::areaIn (window, c)));
    return o;
}

void listInto (juce::Component& window, juce::Component& root, juce::Array<juce::var>& out, int limit)
{
    for (auto* c : root.getChildren())
    {
        if (out.size() >= limit)
            return;
        if (! c->isVisible())
            continue;
        if (c->getComponentID().isNotEmpty() && ! c->getLocalBounds().isEmpty())
            out.add (describe (window, *c));
        listInto (window, *c, out, limit);
    }
}

// a press and a release delivered to the component itself (not through the operating system)
void pressAndRelease (juce::Component& c, juce::Point<float> at, int clicks, bool right = false)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const juce::ModifierKeys down (right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier); // (round 190pg: "right")
    for (int i = 1; i <= clicks; ++i)
    {
        const juce::MouseEvent press (source, at, down, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at, now, i, false);
        c.mouseDown (press);
        // (round 190pg: as JUCE's own mouse-up, the event still carries the button that went up, so a mouseDoubleClick that
        // checks e.mods.isLeftButtonDown(), such as the toolbar's CPU panel, takes it)
        const juce::MouseEvent release (source, at, down, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at, now, i, false);
        c.mouseUp (release);
        if (i == 2)
            c.mouseDoubleClick (release);
    }
}

// Round 190pg: a menu's item by its text, as "menu:<English text>" (the menu bar's titles too). JUCE's menu items are
// components without IDs, but each has an accessibility handler whose title is the item's text: the English text matches,
// and so does its translation (tr), without a trailing "..." and the shortcut after a tab.
juce::String menuText (juce::String t)
{
    t = t.upToFirstOccurrenceOf ("\t", false, false).removeCharacters ("&").trim();
    while (t.endsWith (".") || t.endsWith (juce::String::fromUTF8 ("\xe2\x80\xa6")))
        t = t.dropLastCharacters (1).trimEnd();
    return t;
}

juce::Component* findMenuItemIn (juce::Component& root, const juce::String& english, const juce::String& translated)
{
    for (auto* c : root.getChildren())
    {
        if (! c->isVisible())
            continue;
        if (auto* h = c->getAccessibilityHandler(); h != nullptr && h->getRole() == juce::AccessibilityRole::menuItem && ! c->getLocalBounds().isEmpty())
            if (const auto title = menuText (h->getTitle());
                title == translated || title == english || title.startsWith (translated + " (") || title.startsWith (english + " (")) // (an item may end in a count: "Create scenes from sections (3)")
                return c;
        if (auto* inner = findMenuItemIn (*c, english, translated))
            return inner;
    }
    return nullptr;
}

// the open menus first (the newest window on top), then the main window (its menu bar)
juce::Component* findMenuItem (juce::Component& window, const juce::String& englishText)
{
    // (the translation of the text as written, "Wave file..." with its dots: the tables hold it that way)
    const auto raw = englishText.upToFirstOccurrenceOf ("\t", false, false).trim();
    const auto english = menuText (raw), translated = menuText (tr (raw));
    auto& desktop = juce::Desktop::getInstance();
    for (int i = desktop.getNumComponents(); --i >= 0;)
        if (auto* top = desktop.getComponent (i); top != nullptr && top != window.getTopLevelComponent() && top->isShowing())
            if (auto* c = findMenuItemIn (*top, english, translated))
                return c;
    return findMenuItemIn (window, english, translated);
}

// a control in another of LYNX's windows (a plugin's editor, a tool window, a dialog): by ID first, then by name
juce::Component* findInOtherWindows (juce::Component& window, const juce::String& key)
{
    auto& desktop = juce::Desktop::getInstance();
    for (const bool byId : { true, false })
        for (int i = desktop.getNumComponents(); --i >= 0;)
            if (auto* top = desktop.getComponent (i); top != nullptr && top != window.getTopLevelComponent() && top->isShowing())
            {
                if (top->getComponentID() == key || (! byId && top->getName() == key))
                    return top;
                if (auto* c = onboardui::findShowing (*top, key); c != nullptr && (! byId || c->getComponentID() == key))
                    return c;
            }
    return nullptr;
}

juce::String stateName (LiveSessionManager::State s)
{
    using S = LiveSessionManager::State;
    switch (s)
    {
        case S::idle: return "idle";
        case S::starting: return "starting";
        case S::connecting: return "connecting";
        case S::knocking: return "knocking";
        case S::loading: return "loading";
        case S::live: return "live";
        case S::reconnecting: return "reconnecting";
        case S::waitingHost: return "waitingHost";
        case S::ended: return "ended";
        default: return "unknown";
    }
}
} // namespace

juce::var MainComponent::handleDemoOp (const juce::String& op, const juce::var& args)
{
    auto& window = *this;
    // round 190pg: "menu:<text>" is an open menu's item (or the menu bar's title); other targets are looked for in the main
    // window, then in LYNX's other windows (plugin editors, tool windows)
    const auto locate = [&window] (const juce::String& target) -> juce::Component* {
        if (target.startsWith ("menu:"))
            return findMenuItem (window, target.substring (5));
        if (auto* c = onboardui::findShowing (window, target))
            return c;
        return findInOtherWindows (window, target);
    };
    const auto find = [&args, &locate]() -> juce::Component& {
        const auto target = text (args, "target");
        if (auto* c = locate (target))
            return *c;
        fail ("No control showing with the ID or name \"" + target + "\"");
    };

    if (op == "ui.window")
    {
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
        return object ({ { "screen", rect (getScreenBounds()) }, { "scale", display != nullptr ? display->scale : 1.0 },
                         { "layout", layoutstyle::id (layoutStyle) }, { "language", i18n::current() } });
    }
    // "cell": { "row", "column" } on a control made of cells (demo::CellSource): that cell's rectangle, in its coordinates
    const auto cellOf = [this, &args] (juce::Component& c) -> std::optional<juce::Rectangle<int>> {
        const auto& cell = args["cell"];
        if (cell.getDynamicObject() == nullptr)
            return std::nullopt;
        if (! cell["row"].isInt() || ! cell["column"].isInt())
            fail ("cell needs integer row and column");
        const auto row = (int) cell["row"], column = (int) cell["column"];
        juce::Rectangle<int> r;
        if (auto* source = dynamic_cast<demo::CellSource*> (&c))
            r = source->demoCellBounds (row, column);
        // round 190pg: the editors' cells (docs/DEMO_MODE.md): Channel rack row (0 = top) and step; Piano roll key and
        // step (a 16th from the pattern's start); Playlist track and step (a 16th from the song's start); Mixer track
        // (row 0 its fader, 1 its mute switch, 2 its meter); clip grid scene and column (column -1: the scene's launch
        // button, row -1: the column's stop button, row -2: the column's title)
        else if (&c == &rack)
        {
            if (column >= 0 && row >= 0)
                r = rack.stepBoundsForTests (row, column);
        }
        else if (&c == &pianoRoll)
        {
            if (row >= 0 && row <= 127 && column >= 0)
            {
                const auto tick = (std::int64_t) column * ticksPerStep;
                const auto a = pianoRoll.pointForTests (tick, row), b = pianoRoll.pointForTests (tick + ticksPerStep, row);
                const auto h = juce::jmax (4, pianoRoll.pointForTests (tick, row - 1).y - a.y);
                r = { a.x, a.y - h / 2, juce::jmax (4, b.x - a.x), h };
            }
        }
        else if (&c == &playlist)
        {
            if (row >= 0 && row < 1000 && column >= 0) // (a new project's Playlist draws its tracks before any of them is stored)
            {
                const auto band = playlist.trackRowForSnapshot (row);
                const auto x0 = (int) playlist.xOfTickForSnapshot ((double) column * ticksPerStep);
                const auto x1 = (int) playlist.xOfTickForSnapshot ((double) (column + 1) * ticksPerStep);
                r = { x0, band.getY(), juce::jmax (2, x1 - x0), band.getHeight() };
            }
        }
        else if (&c == &mixer.getStripsForSnapshot())
        {
            auto& strips = mixer.getStripsForSnapshot();
            r = row == 0 ? strips.faderRailForSnapshot (column) : row == 1 ? strips.muteAreaForSnapshot (column) : row == 2 ? strips.meterAreaForSnapshot (column) : juce::Rectangle<int>();
        }
        else if (clipGridView != nullptr && &c == clipGridView.get())
        {
            if (row == -2 && column >= 0) // (the column's title, above its first slot: its right-click menu has Deactivate Track)
            {
                const auto first = clipGridCellForSnapshot (c, 0, column, 0);
                r = first.withY (0).withHeight (juce::jmax (0, first.getY()));
            }
            else
                r = column < 0 ? clipGridCellForSnapshot (c, 2, 0, row) : row < 0 ? clipGridCellForSnapshot (c, 1, column, 0) : clipGridCellForSnapshot (c, 0, column, row);
        }
        else
            fail ("cell needs a control made of cells");
        r = r.getIntersection (c.getLocalBounds());
        if (r.isEmpty())
            fail ("No such cell (or it is scrolled out of view)");
        return r;
    };
    // a point in a rectangle: "at" as fractions of its size, else its centre
    const auto pointIn = [&args] (juce::Rectangle<int> r) {
        if (const auto& p = args["at"]; p.getDynamicObject() != nullptr)
            return juce::Point<float> ((float) r.getX() + (float) r.getWidth() * (float) juce::jlimit (0.0, 1.0, (double) p["x"]),
                                       (float) r.getY() + (float) r.getHeight() * (float) juce::jlimit (0.0, 1.0, (double) p["y"]));
        return r.toFloat().getCentre();
    };
    if (op == "ui.find")
    {
        const auto target = text (args, "target");
        if (auto* c = locate (target))
        {
            auto described = describe (window, *c);
            if (const auto cell = cellOf (*c))
            {
                described.getDynamicObject()->setProperty ("screen", rect (c->localAreaToGlobal (*cell)));
                const auto origin = onboardui::areaIn (window, *c).getPosition();
                described.getDynamicObject()->setProperty ("window", rect (cell->translated (origin.x, origin.y)));
            }
            return object ({ { "found", true }, { "control", described } });
        }
        return object ({ { "found", false } });
    }
    if (op == "ui.list")
    {
        auto* root = static_cast<juce::Component*> (this);
        if (const auto under = text (args, "under", false); under.isNotEmpty())
            if (auto* c = locate (under))
                root = c;
        const auto limit = args["limit"].isInt() ? juce::jlimit (1, 2000, (int) args["limit"]) : 500;
        juce::Array<juce::var> list;
        listInto (window, *root, list, limit);
        return object ({ { "controls", list } });
    }
    if (op == "ui.click")
    {
        auto& c = find();
        if (! c.isEnabled())
            fail ("The control \"" + text (args, "target") + "\" is disabled");
        const auto described = describe (window, c); // (before the click: it may close what it is in)
        const auto clicks = args["double"].isBool() && (bool) args["double"] ? 2 : 1;
        const auto right = args["right"].isBool() && (bool) args["right"]; // (round 190pg: a right-click, e.g. a slot's menu)
        // round 190pg: a menu's item runs as its keyboard path does: a submenu opens, an item runs (and the menu closes);
        // a menu bar title opens its menu
        if (text (args, "target").startsWith ("menu:"))
        {
            auto* h = c.getAccessibilityHandler();
            if (h == nullptr)
                fail ("That menu item cannot be clicked");
            h->getActions().invoke (juce::AccessibilityActionType::focus);
            if (! h->getActions().invoke (juce::AccessibilityActionType::showMenu) && ! h->getActions().invoke (juce::AccessibilityActionType::press))
                fail ("That menu item is disabled");
            return object ({ { "clicked", true }, { "control", described } });
        }
        if (const auto cell = cellOf (c))
        {
            pressAndRelease (c, pointIn (*cell), clicks, right); // (the control's own mouse handling: a step toggles)
            return object ({ { "clicked", true }, { "control", described }, { "cell", rect (c.localAreaToGlobal (*cell)) } });
        }
        if (auto* b = dynamic_cast<juce::Button*> (&c); b != nullptr && clicks == 1 && ! right)
            b->triggerClick(); // (the button's own click, as a press of its shortcut)
        else if (auto* box = dynamic_cast<juce::ComboBox*> (&c); box != nullptr && clicks == 1 && ! right)
            box->showPopup();
        else
        {
            // "at": a point inside the control as fractions of its size ({ "x": 0.5, "y": 0.5 } = its centre)
            auto at = c.getLocalBounds().toFloat().getCentre();
            if (const auto& p = args["at"]; p.getDynamicObject() != nullptr)
                at = { (float) c.getWidth() * (float) juce::jlimit (0.0, 1.0, (double) p["x"]), (float) c.getHeight() * (float) juce::jlimit (0.0, 1.0, (double) p["y"]) };
            pressAndRelease (c, at, clicks, right);
        }
        return object ({ { "clicked", true }, { "control", described } });
    }
    // round 190pg: the pointer rests on a control (an open menu's item lights, as under the mouse; others: only measured)
    if (op == "ui.hover")
    {
        auto& c = find();
        const auto described = describe (window, c);
        if (text (args, "target").startsWith ("menu:"))
            if (auto* h = c.getAccessibilityHandler())
                h->getActions().invoke (juce::AccessibilityActionType::focus);
        return object ({ { "hovered", true }, { "control", described } });
    }
    // round 190pg: opens a project or a file LYNX imports (.lynx, .flp, .als, .flm, .dawproject, BandLab .band/.blx...),
    // as File › Open does after its file dialog; the open project is not asked about (a demo take)
    if (op == "ui.open")
    {
        const juce::File file (text (args, "file"));
        if (! juce::File::isAbsolutePath (file.getFullPathName()) || ! file.existsAsFile())
            fail ("file must be the absolute path of an existing file");
        openFile (file);
        return object ({ { "opened", file.getFileName() } });
    }
    // round 190pg: a channel's editor window (its instrument: LYNX Model, Helix, a plugin...), as a click on the channel's name
    // in the Channel rack opens it. The window gets the component ID "editor" (the newest one), so a script can point at it.
    if (op == "ui.editor")
    {
        if (! args["channel"].isInt())
            fail ("channel must be an integer");
        const auto channel = (int) args["channel"];
        if (channel < 0 || channel >= (int) engine.getSong().channels.size())
            fail ("channel is outside the project's channels");
        auto& desktop = juce::Desktop::getInstance();
        juce::Array<juce::Component*> before;
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            before.add (desktop.getComponent (i));
        rack.showEditor (channel);
        for (int i = desktop.getNumComponents(); --i >= 0;)
            if (auto* top = desktop.getComponent (i); top != nullptr && ! before.contains (top) && top->isShowing())
            {
                for (int k = 0; k < desktop.getNumComponents(); ++k) // (one window carries the ID: the newest)
                    if (auto* other = desktop.getComponent (k); other != nullptr && other->getComponentID() == "editor")
                        other->setComponentID ({});
                top->setComponentID ("editor");
                // a window taller than the screen is moved up into it (Helix's editor is ~1000 px tall), and painted again
                // once it is mapped (a private X server without a compositor otherwise shows stale pixels in it for a while)
                if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
                    top->setBounds (top->getBounds().constrainedWithin (display->userArea));
                for (const int ms : { 150, 600, 1500 })
                    juce::Timer::callAfterDelay (ms, [sp = juce::Component::SafePointer<juce::Component> (top)] {
                        if (sp != nullptr)
                            sp->repaint();
                    });
                return object ({ { "channel", channel }, { "screen", rect (top->getScreenBounds()) } });
            }
        return object ({ { "channel", channel } }); // (a window inside the main one, or one that was open already)
    }
    // round 190pg: moves one of LYNX's own windows (a tool window, an editor: by its ID) so that two of them show side by side
    if (op == "ui.place")
    {
        auto& c = find();
        auto* top = c.getTopLevelComponent();
        if (top == nullptr || top == getTopLevelComponent())
            fail ("The target is not in a window of its own");
        if (! args["x"].isInt() || ! args["y"].isInt())
            fail ("x and y must be integers (screen pixels)");
        auto r = top->getBounds().withPosition ((int) args["x"], (int) args["y"]);
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
            r = r.constrainedWithin (display->userArea);
        top->setBounds (r);
        top->repaint();
        return object ({ { "screen", rect (top->getScreenBounds()) } });
    }
    // round 190pg: one of the key map's actions by its id (what its shortcut runs), e.g. view.commandPalette
    if (op == "ui.action")
    {
        const auto id = text (args, "id");
        if (! runAction (id.toStdString()))
            fail ("No action ran for \"" + id + "\"");
        return object ({ { "ran", id } });
    }
    if (op == "ui.layout")
    {
        const auto id = text (args, "style");
        if (id != "pattern" && id != "clipLaunch" && id != "simple")
            fail ("style must be pattern, clipLaunch or simple");
        setLayoutStyle (layoutstyle::fromId (id.toStdString()));
        return object ({ { "layout", layoutstyle::id (layoutStyle) } });
    }
    if (op == "ui.select_track")
    {
        if (! args["track"].isInt())
            fail ("track must be an integer");
        const auto track = (int) args["track"];
        if (track < 0 || track >= (int) engine.getSong().playlistTracks.size())
            fail ("track is outside the Playlist's tracks");
        playlist.selectTrack (track);
        if (simpleWorkflow != nullptr && layoutStyle == layoutstyle::Style::simple)
            simpleWorkflow->trackSelected (track); // (the Simple dock shows the selected track)
        return object ({ { "track", track } });
    }
    if (op == "ui.show")
    {
        const auto what = text (args, "panel");
        if (what == "channelRack") showPanel (Panel::channelRack);
        else if (what == "pianoRoll")
        {
            // round 190pg: "channel" selects that channel first, and the Piano roll opens on its notes (as a double click on
            // the channel's steps opens it)
            if (args["channel"].isInt() && (int) args["channel"] >= 0 && (int) args["channel"] < (int) engine.getSong().channels.size())
                engine.setSelectedChannel ((int) args["channel"]);
            openPianoRoll();
        }
        else if (what == "playlist")
        {
            showPanel (Panel::playlist);
            if (args["fit"].isBool() && (bool) args["fit"])
                playlist.zoomAllForSnapshot(); // (round 190pg: the whole song in view, as the Playlist's zoom-all does)
        }
        else if (what == "mixer") showPanel (Panel::mixer);
        else if (what == "browser") showPanel (Panel::browser);
        else if (what == "loops") showLoopsDock();
        else if (what == "welcome") showWelcomeCard (false);
        else if (what == "gettingStarted") showGettingStarted();
        else if (what == "dock" && simpleWorkflow != nullptr) simpleWorkflow->trackSelected (playlist.getCurrentTrack());
        else
            fail ("panel must be channelRack, pianoRoll, playlist, mixer, browser, loops, welcome, gettingStarted or dock");
        return object ({ { "shown", what } });
    }
    if (op == "ui.welcome")
    {
        const auto id = text (args, "choice");
        const auto choice = onboarding::choiceFromId (id.toStdString());
        if (choice == onboarding::Choice::none && id != "skip")
            fail ("choice must be mobile, fl, live, timeline, new or skip");
        setupOverlay.reset();
        applyWelcomeChoice (choice);
        return object ({ { "choice", id }, { "layout", layoutstyle::id (layoutStyle) } });
    }
    // round 190nx: an audio clip's context menu and the polyphonic note editor, by ID. A clip is `clip` (its index in the project
    // snapshot's playlist) or `clipId` (the clip's stable id as 16 hex digits, uids::toHex); a channel is `channel` (index) or
    // `channelId` (the snapshot's hex id), or the channel of the clip
    if (op == "ui.clipmenu" || op == "ui.polyeditor")
    {
        const auto& song = engine.getSong();
        auto uidOf = [] (const juce::var& v) { return uids::fromHex (v.toString().trim().toStdString()); };
        int clip = -1, channel = -1;
        if (args["clip"].isInt() || args["clip"].isInt64())
            clip = (int) args["clip"];
        else if (args["clipId"].isString())
        {
            for (std::size_t i = 0; i < song.playlist.size() && clip < 0; ++i)
                if (song.playlist[i].uid != 0 && song.playlist[i].uid == uidOf (args["clipId"]))
                    clip = (int) i;
        }
        if ((args["clip"].isInt() || args["clip"].isInt64() || args["clipId"].isString()) && (clip < 0 || clip >= (int) song.playlist.size()))
            fail ("No Playlist clip with that clip index or clipId");
        if (op == "ui.clipmenu")
        {
            if (clip < 0)
                fail ("ui.clipmenu needs clip (index) or clipId");
            if (! playlist.isShowing())
                fail ("The Playlist is not showing (ui.show panel playlist)");
            const auto screen = playlist.openClipMenuForDemo (clip);
            if (screen.isEmpty())
                fail ("The clip is not in view in the Playlist (scroll to it first)");
            return object ({ { "clip", clip }, { "screen", rect (screen) }, { "menu", true } });
        }
        if (args["channel"].isInt() || args["channel"].isInt64())
            channel = (int) args["channel"];
        else if (args["channelId"].isString())
        {
            for (std::size_t i = 0; i < song.channels.size() && channel < 0; ++i)
                if (song.channels[i].uid != 0 && song.channels[i].uid == uidOf (args["channelId"]))
                    channel = (int) i;
        }
        else if (clip >= 0)
            channel = song.playlist[(std::size_t) clip].channel;
        if (channel < 0 || channel >= (int) song.channels.size())
            fail ("ui.polyeditor needs channel (index), channelId or a clip that plays audio");
        const auto& ch = song.channels[(std::size_t) channel];
        if (ch.instrument != InstrumentKind::audioClip || ch.ara.active() || engine.getSampleLibrary().load (juce::String::fromUTF8 (ch.samplePath.c_str())) == nullptr)
            fail ("That channel is not an audio clip with a loaded sample, so it has no polyphonic editor");
        PolyEditorWindows::open (engine, channel);
        return object ({ { "channel", channel }, { "screen", rect (PolyEditorWindows::boundsOf (channel)) } });
    }
    // round 190pg: the open polyphonic editor's notes ("k midi start-end [edit]" per line: k is the note's cell column)
    if (op == "ui.polynotes")
    {
        auto* editor = onboardui::findShowing (window, "polyEditor");
        if (editor == nullptr)
            editor = findInOtherWindows (window, "polyEditor");
        if (editor == nullptr)
            fail ("No polyphonic editor is open (ui.polyeditor)");
        return object ({ { "notes", polyEditorNotes (*editor) } });
    }
    if (op == "session.start" || op == "session.status")
    {
        if (live == nullptr)
            fail ("Online sessions are not available");
        if (op == "session.start")
        {
            const auto relay = juce::SystemStats::getEnvironmentVariable ("LYNX_RELAY_URL", {});
            if (! demo::isLocalRelay (relay))
                fail ("session.start needs LYNX_RELAY_URL set to a local test relay (http://127.0.0.1:port)");
            if (live->isActive())
                fail ("A live session is already running");
            LiveSessionManager::Options o;
            o.relay = relay;
            o.name = args["name"].isString() && args["name"].toString().isNotEmpty() ? args["name"].toString().substring (0, 40) : juce::String ("Host");
            o.projectFile = currentFile;
            o.autoAccept = true; // (a recorded demo: nobody to click "Let them in")
            live->start (o);
        }
        return object ({ { "state", stateName (live->getState()) }, { "inviteCode", live->getInviteCode() }, { "status", live->getStatus() },
                         { "host", live->isHost() } });
    }
    fail ("Unsupported demo operation: " + op);
}
} // namespace lynx
