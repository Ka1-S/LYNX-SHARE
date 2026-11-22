// Round 190np: LYNX --demo, the view operations the tutorial-video pipeline uses through the Remote Control API
// (ui/DemoMode.h, docs/DEMO_MODE.md). They run on the message thread (the API's dispatch posts every request there).
// A control is found by its component ID (what the first-song tour uses), else by its name or button text; a click runs
// the control's own action (a button's click, a combo box's menu, else a mouse press and release delivered to the
// component itself): no input reaches the operating system.

#include "ui/DemoMode.h"

#include <atomic>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>
#include <optional>
#include <stdexcept>

#include "core/MixerOps.h"
#include "core/SessionIds.h"
#include "engine/LiveSessions.h"
#include "engine/PluginHost.h"
#include "ui/I18n.h"
#include "ui/LiveSessionUi.h"
#include "ui/ExportDialog.h"
#include "ui/ModRing.h"
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
juce::File& exportTarget()
{
    static juce::File file;
    return file;
}
}

bool active() noexcept { return on.load(); }
void setActive (bool value) noexcept { on.store (value); }

juce::String virtualKeyboardName() { return "LYNX demo keyboard"; }
void setExportTarget (const juce::File& file) { exportTarget() = file; }
juce::File takeExportTarget() { return std::exchange (exportTarget(), juce::File()); }

juce::StringArray operations()
{
    return { "ui.window", "ui.find", "ui.click", "ui.hover", "ui.list", "ui.layout", "ui.select_track", "ui.show", "ui.welcome", "ui.clipmenu", "ui.polyeditor",
             "ui.open", "ui.action", "ui.editor", "ui.place", "ui.polynotes", "session.start", "session.status",
             // round 190pl (tutorial batch 2)
             "ui.export", "file.export", "file.save", "midi.input", "plugin.scan", "ui.drag", "ui.controlmenu", "mixer.route", "ui.repaint", "ui.type" };
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
        {
            const auto title = menuText (h->getTitle());
            // (an item may end in a count: "Create scenes from sections (3)", or after its dots: "More plugins... (196 effects)")
            const auto counted = title.endsWithChar (')') && title.contains (" (") ? menuText (title.upToLastOccurrenceOf (" (", false, false)) : juce::String();
            if (title == translated || title == english || title.startsWith (translated + " (") || title.startsWith (english + " (")
                || (counted.isNotEmpty() && (counted == translated || counted == english)))
                return c;
        }
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

// Round 190pl: a press, moves and a release delivered through a LYNX window's peer, as the system's mouse events arrive
// (ComponentPeer::handleMouseEvent; nothing reaches the operating system): what is on top at each point gets them, a
// drag that starts a drag-and-drop (a loop onto the timeline, a plugin onto a slot) drops on what is under its end, a
// knob turns. `path` in screen pixels, one point per step of `stepMs`; the press at the first, the release at the last.
class PeerGesture final : private juce::Timer
{
public:
    // A drag: pressed at path's first point, moved with the button down through the others, released at the last
    static void drag (juce::Component& window, const std::vector<juce::Point<float>>& path, juce::ModifierKeys keys, bool right, int stepMs)
    {
        std::vector<Step> steps;
        for (std::size_t i = 0; i < path.size(); ++i)
            steps.push_back ({ path[i], i + 1 < path.size() });
        start (window, std::move (steps), keys, right, stepMs);
    }
    // `clicks` presses and releases at one point (2: a double click)
    static void click (juce::Component& window, juce::Point<float> at, int clicks, juce::ModifierKeys keys, bool right)
    {
        std::vector<Step> steps;
        for (int k = 0; k < clicks; ++k)
        {
            steps.push_back ({ at, true });
            steps.push_back ({ at, false });
        }
        start (window, std::move (steps), keys, right, 30);
    }

private:
    struct Step
    {
        juce::Point<float> at;
        bool down = false;
    };

    static void start (juce::Component& window, std::vector<Step> steps, juce::ModifierKeys keys, bool right, int stepMs)
    {
        auto* g = new PeerGesture (window, std::move (steps), keys, right);
        g->step(); // (the press now: a click is pressed by the time the operation answers)
        if (g->done)
            delete g;
        else
            g->startTimer (juce::jmax (1, stepMs));
    }

    PeerGesture (juce::Component& w, std::vector<Step> s, juce::ModifierKeys k, bool r)
        : window (&w), steps (std::move (s)), keys (k), right (r) {}

    void timerCallback() override
    {
        step();
        if (done)
        {
            stopTimer();
            delete this;
        }
    }

    void step()
    {
        auto* peer = window != nullptr ? window->getPeer() : nullptr;
        if (peer == nullptr || next >= steps.size())
        {
            done = true;
            return;
        }
        const auto button = right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier;
        const auto& s = steps[next];
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, peer->globalToLocal (s.at), s.down ? keys.withFlags (button) : keys,
                                s.down ? 1.0f : 0.0f, 0.0f, juce::Time::currentTimeMillis());
        // a drag's picture is a window of its own that moves over LYNX's: on an X server without a compositor what it
        // leaves behind is only painted over by a repaint, so every LYNX window is painted again as it moves and at the end
        if (steps.size() > 4)
        {
            auto& desktop = juce::Desktop::getInstance();
            for (int i = 0; i < desktop.getNumComponents(); ++i)
                if (auto* top = desktop.getComponent (i); top != nullptr && top->isShowing())
                    top->repaint();
        }
        if (++next >= steps.size())
            done = true;
    }

    juce::Component::SafePointer<juce::Component> window;
    std::vector<Step> steps;
    juce::ModifierKeys keys;
    bool right = false, done = false;
    std::size_t next = 0;
};

juce::ModifierKeys keysOf (const juce::var& mods)
{
    int flags = 0;
    if (auto* list = mods.getArray())
        for (const auto& m : *list)
        {
            const auto k = m.toString().toLowerCase();
            if (k == "shift") flags |= juce::ModifierKeys::shiftModifier;
            else if (k == "ctrl" || k == "control") flags |= juce::ModifierKeys::ctrlModifier;
            else if (k == "alt") flags |= juce::ModifierKeys::altModifier;
            else if (k == "cmd" || k == "command") flags |= juce::ModifierKeys::commandModifier;
            else fail ("mods: shift, ctrl, alt or cmd");
        }
    return juce::ModifierKeys (flags);
}

// Round 190pl: "param:<what>" targets, a control by the parameter it sets (the knob a right-click on it would automate):
// param:channel.<c>.<p> (a channel's instrument parameter p), param:channel.<c>.volume / .pan, param:insert.<t>.<slot>.<p>
// (an insert effect's parameter), param:insert.<t>.volume / .pan (a Mixer track's fader / pan).
std::optional<AutomationTarget> parseParamTarget (const juce::String& target)
{
    juce::StringArray parts;
    parts.addTokens (target.fromFirstOccurrenceOf ("param:", false, false), ".", {});
    const auto isNumber = [] (const juce::String& t) { return t.isNotEmpty() && t.containsOnly ("0123456789"); };
    if (parts.size() == 3 && parts[0] == "channel" && isNumber (parts[1]))
    {
        if (parts[2] == "volume") return AutomationTarget { AutomationTargetKind::channelVolume, parts[1].getIntValue(), 0, 0 };
        if (parts[2] == "pan") return AutomationTarget { AutomationTargetKind::channelPan, parts[1].getIntValue(), 0, 0 };
        if (isNumber (parts[2])) return AutomationTarget { AutomationTargetKind::channelParam, parts[1].getIntValue(), 0, parts[2].getIntValue() };
    }
    if (parts.size() == 3 && parts[0] == "insert" && isNumber (parts[1]))
    {
        if (parts[2] == "volume") return AutomationTarget { AutomationTargetKind::mixerVolume, parts[1].getIntValue(), 0, 0 };
        if (parts[2] == "pan") return AutomationTarget { AutomationTargetKind::mixerPan, parts[1].getIntValue(), 0, 0 };
    }
    if (parts.size() == 4 && parts[0] == "insert" && isNumber (parts[1]) && isNumber (parts[2]) && isNumber (parts[3]))
        return AutomationTarget { AutomationTargetKind::effectParam, parts[1].getIntValue(), parts[2].getIntValue(), parts[3].getIntValue() };
    return std::nullopt;
}

// The showing control that sets `target`, in any LYNX window: a knob tagged with it (modring::tagSlider), or the part of
// a component that paints several knobs (modring::TargetSource: sampled on a 3 px grid) where it answers with it.
struct ParamControl
{
    juce::Component* component = nullptr;
    juce::Rectangle<int> area; // in the component
};

bool findParamIn (juce::Component& root, const AutomationTarget& target, ParamControl& out)
{
    if (! root.isShowing() || root.getLocalBounds().isEmpty())
        return false;
    if (const auto t = modring::taggedTarget (root); t && *t == target)
    {
        out = { &root, root.getLocalBounds() };
        return true;
    }
    if (auto* painted = dynamic_cast<demo::ParamSource*> (&root))
        if (const auto r = painted->demoParamBounds ((int) target.kind, target.index, target.slot, target.param); ! r.isEmpty())
        {
            out = { &root, r };
            return true;
        }
    if (auto* source = dynamic_cast<modring::TargetSource*> (&root))
    {
        juce::Rectangle<int> hit;
        constexpr int step = 3;
        for (int y = step / 2; y < root.getHeight(); y += step)
            for (int x = step / 2; x < root.getWidth(); x += step)
                if (const auto t = source->modTargetAt ({ x, y }); t && *t == target)
                    hit = hit.isEmpty() ? juce::Rectangle<int> (x - step / 2, y - step / 2, step, step)
                                        : hit.getUnion ({ x - step / 2, y - step / 2, step, step });
        if (! hit.isEmpty())
        {
            out = { &root, hit };
            return true;
        }
    }
    for (auto* c : root.getChildren())
        if (c->isVisible() && findParamIn (*c, target, out))
            return true;
    return false;
}

std::optional<ParamControl> findParamControl (juce::Component& window, const AutomationTarget& target)
{
    ParamControl found;
    if (findParamIn (window, target, found))
        return found;
    auto& desktop = juce::Desktop::getInstance();
    for (int i = desktop.getNumComponents(); --i >= 0;)
        if (auto* top = desktop.getComponent (i); top != nullptr && top != window.getTopLevelComponent() && top->isShowing())
            if (findParamIn (*top, target, found))
                return found;
    return std::nullopt;
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
    // round 190pl: a "param:..." target (parseParamTarget): the control and its area, or an error
    const auto findParam = [&window] (const juce::String& target) -> ParamControl {
        const auto t = parseParamTarget (target);
        if (! t)
            fail ("param targets: param:channel.<c>.<p>, param:channel.<c>.volume|pan, param:insert.<t>.<slot>.<p>, param:insert.<t>.volume|pan");
        if (const auto found = findParamControl (window, *t))
            return *found;
        fail ("No control showing for \"" + target + "\" (open its editor first)");
    };
    // round 190pl: the newest of LYNX's windows that `open` made gets the component ID "editor" (ui.editor)
    const auto openEditorWindow = [this] (std::function<void()> open) -> juce::Component* {
        auto& desktop = juce::Desktop::getInstance();
        juce::Array<juce::Component*> before;
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            before.add (desktop.getComponent (i));
        open();
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
                return top;
            }
        return nullptr;
    };

    if (op == "ui.window")
    {
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
        return object ({ { "screen", rect (getScreenBounds()) }, { "scale", display != nullptr ? display->scale : 1.0 },
                         { "layout", layoutstyle::id (layoutStyle) }, { "language", i18n::current() } });
    }
    // "cell": { "row", "column" } on a control made of cells (demo::CellSource): that cell's rectangle, in its coordinates
    // (round 190pl: `cellArg` instead of args' "cell": a drag's end)
    const auto cellOf = [this, &args] (juce::Component& c, const juce::var* cellArg = nullptr) -> std::optional<juce::Rectangle<int>> {
        const auto& cell = cellArg != nullptr ? *cellArg : args["cell"];
        if (cell.getDynamicObject() == nullptr)
            return std::nullopt;
        // (round 190pl: "name" instead of "column" on a control that can look its cells up by what they show)
        if (cell["row"].isInt() && cell["name"].isString())
        {
            auto* named = dynamic_cast<demo::CellSource*> (&c);
            const auto r = named != nullptr ? named->demoCellNamed ((int) cell["row"], cell["name"].toString()).getIntersection (c.getLocalBounds())
                                            : juce::Rectangle<int>();
            if (r.isEmpty())
                fail ("No cell showing \"" + cell["name"].toString() + "\" (or it is scrolled out of view)");
            return r;
        }
        if (! cell["row"].isInt() || ! cell["column"].isInt())
            fail ("cell needs integer row and column (or row and name)");
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
            // (round 190pl: row 3 the route arrow under the track, 4 the fader's cap where it is now, 5 the pan knob, 6 solo, 7 its header)
            r = row == 0 ? strips.faderRailForSnapshot (column) : row == 1 ? strips.muteAreaForSnapshot (column) : row == 2 ? strips.meterAreaForSnapshot (column)
              : row == 3 ? strips.routeArrowForSnapshot (column) : row == 4 ? strips.faderCapForSnapshot (column) : row == 5 ? strips.panAreaForSnapshot (column)
              : row == 6 ? strips.soloAreaForSnapshot (column) : row == 7 ? strips.headerAreaForSnapshot (column) : juce::Rectangle<int>();
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
        if (target.startsWith ("param:"))
        {
            if (! parseParamTarget (target))
                findParam (target); // (the error: the forms it takes)
            if (const auto found = findParamControl (window, *parseParamTarget (target)))
            {
                auto described = describe (window, *found->component);
                described.getDynamicObject()->setProperty ("screen", rect (found->component->localAreaToGlobal (found->area)));
                return object ({ { "found", true }, { "control", described } });
            }
            return object ({ { "found", false } });
        }
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
    // round 190pl: a right-click on the control that sets a parameter (a knob's own menu: Create automation clip, Link to
    // controller, Modulate with...). The parameter as a "param:..." target, or as fields: channel + parameter (an index,
    // "volume" or "pan"), or insert (+ slot) + parameter. The menu opens where the pointer is, as the knob's own does.
    if (op == "ui.controlmenu")
    {
        juce::String target;
        if (args["target"].isString())
            target = text (args, "target");
        else
        {
            const auto& p = args["parameter"];
            const auto param = p.isString() ? p.toString() : (p.isInt() || p.isInt64()) ? juce::String ((int) p) : juce::String();
            if (args["channel"].isInt() && param.isNotEmpty())
                target = "param:channel." + juce::String ((int) args["channel"]) + "." + param;
            else if (args["insert"].isInt() && args["slot"].isInt() && param.isNotEmpty())
                target = "param:insert." + juce::String ((int) args["insert"]) + "." + juce::String ((int) args["slot"]) + "." + param;
            else if (args["insert"].isInt() && param.isNotEmpty())
                target = "param:insert." + juce::String ((int) args["insert"]) + "." + param;
            else
                fail ("ui.controlmenu needs target (param:...), or channel / insert (+ slot) with parameter");
        }
        const auto found = findParam (target);
        const auto area = found.area;
        pressAndRelease (*found.component, area.toFloat().getCentre(), 1, true);
        return object ({ { "menu", true }, { "target", target }, { "screen", rect (found.component->localAreaToGlobal (area)) } });
    }
    if (op == "ui.click" && text (args, "target").startsWith ("param:"))
    {
        const auto found = findParam (text (args, "target"));
        const auto right = args["right"].isBool() && (bool) args["right"];
        const auto clicks = args["double"].isBool() && (bool) args["double"] ? 2 : 1;
        const auto at = pointIn (found.area);
        pressAndRelease (*found.component, at, clicks, right);
        return object ({ { "clicked", true }, { "screen", rect (found.component->localAreaToGlobal (found.area)) } });
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
            // (round 190pl: a private X server without a compositor keeps a closed menu's pixels on the windows below it
            // until they are painted again)
            for (const int ms : { 120, 450 })
                juce::Timer::callAfterDelay (ms, [] {
                    auto& desktop = juce::Desktop::getInstance();
                    for (int i = 0; i < desktop.getNumComponents(); ++i)
                        if (auto* top = desktop.getComponent (i); top != nullptr && top->isShowing())
                            top->repaint();
                });
            return object ({ { "clicked", true }, { "control", described } });
        }
        // round 190pl: "peer": the press and the release go through the window, as the system's mouse would deliver them
        // (what is on top at that point gets them: a list's row, a table's cell)
        if (args["peer"].isBool() && (bool) args["peer"])
        {
            const auto cell = cellOf (c);
            const auto local = cell ? pointIn (*cell) : pointIn (c.getLocalBounds());
            const auto screen = c.localPointToGlobal (local);
            PeerGesture::click (*c.getTopLevelComponent(), screen, clicks, keysOf (args["mods"]), right);
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
        // round 190pl: "insert" + "slot": an insert effect's editor, as a double click on the Mixer slot opens it
        if (args["insert"].isInt() || args["slot"].isInt())
        {
            if (! args["insert"].isInt() || ! args["slot"].isInt())
                fail ("insert and slot must be integers");
            const auto track = (int) args["insert"], slot = (int) args["slot"];
            const auto& song = engine.getSong();
            if (track < 0 || track >= (int) song.mixer.size() || slot < 0 || slot >= (int) song.mixer[(std::size_t) track].effects.size())
                fail ("No effect in that insert's slot");
            auto* top = openEditorWindow ([this, track, slot] { mixer.openSlotEditor (track, slot); });
            return top != nullptr ? object ({ { "insert", track }, { "slot", slot }, { "screen", rect (top->getScreenBounds()) } })
                                  : object ({ { "insert", track }, { "slot", slot } });
        }
        if (! args["channel"].isInt())
            fail ("channel must be an integer");
        const auto channel = (int) args["channel"];
        if (channel < 0 || channel >= (int) engine.getSong().channels.size())
            fail ("channel is outside the project's channels");
        if (auto* top = openEditorWindow ([this, channel] { rack.showEditor (channel); }))
            return object ({ { "channel", channel }, { "screen", rect (top->getScreenBounds()) } });
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
        else if (what == "pluginManager") showPluginManager(); // (round 190pl: ID pluginManager)
        else if (what == "versions") showVersions();           // (round 190pl: ID versions)
        else
            fail ("panel must be channelRack, pianoRoll, playlist, mixer, browser, loops, welcome, gettingStarted, dock, pluginManager or versions");
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
    // ---- round 190pl (tutorial batch 2) ----------------------------------------------------------------------------------
    // Every LYNX window painted again (a private X server without a compositor can keep a closed menu's pixels on screen)
    if (op == "ui.repaint")
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* top = desktop.getComponent (i); top != nullptr && top->isShowing())
                top->repaint();
        return object ({ { "repainted", desktop.getNumComponents() } });
    }
    const auto formatOf = [&args]() -> int {
        static const char* names[] { "wav", "mp3", "ogg", "flac", "aiff", "m4a" };
        const auto f = text (args, "format").toLowerCase();
        for (int i = 0; i < (int) std::size (names); ++i)
            if (f == names[i])
                return i;
        fail ("format must be wav, mp3, ogg, flac, aiff or m4a");
    };
    const auto absoluteFile = [&args] (const char* field) {
        const auto path = text (args, field);
        if (! juce::File::isAbsolutePath (path))
            fail (juce::String (field) + " must be an absolute path");
        return juce::File (path);
    };
    // The Export window of File › Export › <format> file..., whose Export... button then writes `path` (no file dialog).
    // "run": false only sets the file, for a script that opens the window through the real menu.
    if (op == "ui.export")
    {
        if (args["path"].isString())
            demo::setExportTarget (absoluteFile ("path"));
        if (args["run"].isBool() && ! (bool) args["run"])
            return object ({ { "armed", args["path"].isString() } });
        exportAudio (formatOf());
        auto* panel = findInOtherWindows (window, "export.window");
        return object ({ { "opened", panel != nullptr }, { "screen", panel != nullptr ? rect (panel->getScreenBounds()) : juce::var() } });
    }
    // The same render without a window, here and now (checks): { format, path } -> the file's size, length and peak
    if (op == "file.export")
    {
        const auto file = absoluteFile ("path");
        double seconds = 0.0;
        float peak = 0.0f;
        if (const auto error = exportAudioNow (engine, formatOf(), file, seconds, peak); error.isNotEmpty())
            fail (error);
        return object ({ { "file", file.getFullPathName() }, { "bytes", (juce::int64) file.getSize() }, { "seconds", seconds },
                         { "peakDb", juce::Decibels::gainToDecibels (peak) } });
    }
    // Save (the project's own file), Save as `path` (no file dialog), or Save new version ("newVersion": true). "run": false
    // with a path: the next Save as... (the real menu item, Ctrl+Shift+S) writes it.
    if (op == "file.save")
    {
        const auto newVersion = args["newVersion"].isBool() && (bool) args["newVersion"];
        if (args["path"].isString())
        {
            demoSaveTarget = absoluteFile ("path");
            if (args["run"].isBool() && ! (bool) args["run"])
                return object ({ { "armed", true } });
            save (true);
        }
        else if (currentFile == juce::File())
            fail ("The project has no file yet: give path (Save as)");
        else if (newVersion)
            saveNewVersion();
        else
            save (false);
        return object ({ { "file", currentFile.getFullPathName() }, { "name", projectName }, { "modified", engine.isModified() } });
    }
    // Notes and controllers from the virtual input "LYNX demo keyboard" (listed in Options › MIDI while --demo is on), at
    // their times from now: { events: [ { type: note, note, velocity (1-127), at (seconds), length (seconds), channel (1-16) },
    // { type: cc, cc, value, at, channel } ] }. They go through the MIDI settings, recording and Map Mode as a keyboard's.
    if (op == "midi.input")
    {
        auto* list = args["events"].getArray();
        if (list == nullptr || list->isEmpty() || list->size() > 2000)
            fail ("events must be a list of 1-2000 events");
        double last = 0.0;
        std::vector<std::pair<double, juce::MidiMessage>> messages;
        for (const auto& e : *list)
        {
            const auto type = e["type"].toString();
            const auto at = juce::jlimit (0.0, 600.0, (double) e["at"]);
            const auto midiChannel = e["channel"].isInt() ? juce::jlimit (1, 16, (int) e["channel"]) : 1;
            if (type == "note")
            {
                if (! e["note"].isInt() || (int) e["note"] < 0 || (int) e["note"] > 127)
                    fail ("a note event needs note 0-127");
                const auto note = (int) e["note"];
                const auto velocity = e["velocity"].isInt() ? juce::jlimit (1, 127, (int) e["velocity"]) : 100;
                const auto length = e.hasProperty ("length") ? juce::jlimit (0.01, 60.0, (double) e["length"]) : 0.25;
                messages.push_back ({ at, juce::MidiMessage::noteOn (midiChannel, note, (juce::uint8) velocity) });
                messages.push_back ({ at + length, juce::MidiMessage::noteOff (midiChannel, note) });
                last = juce::jmax (last, at + length);
            }
            else if (type == "cc")
            {
                if (! e["cc"].isInt() || (int) e["cc"] < 0 || (int) e["cc"] > 127)
                    fail ("a cc event needs cc 0-127");
                messages.push_back ({ at, juce::MidiMessage::controllerEvent (midiChannel, (int) e["cc"], juce::jlimit (0, 127, (int) e["value"])) });
                last = juce::jmax (last, at);
            }
            else
                fail ("event type must be note or cc");
        }
        const juce::Component::SafePointer<MainComponent> safe (this);
        for (const auto& [at, message] : messages)
        {
            const auto ms = juce::roundToInt (at * 1000.0);
            const auto play = [safe, message = message] {
                if (safe != nullptr)
                    safe->engine.injectMidiInput (message, demo::virtualKeyboardName());
            };
            if (ms <= 0)
                play();
            else
                juce::Timer::callAfterDelay (ms, play);
        }
        return object ({ { "device", demo::virtualKeyboardName() }, { "messages", (int) messages.size() }, { "seconds", last } });
    }
    // The Plugin manager's scan of `folders` (absolute, existing), added to the plugin folders unless "add" is false; in
    // the background as the manager's Find new plugins. "status": true answers how the scan is going instead.
    if (op == "plugin.scan")
    {
        auto& host = engine.getPluginHost();
        if (args["status"].isBool() && (bool) args["status"])
        {
            const auto state = host.backgroundScan();
            return object ({ { "running", state.running }, { "progress", state.progress }, { "found", state.found }, { "finished", state.finished },
                             { "known", host.getKnownPlugins().getNumTypes() } });
        }
        if (host.backgroundScan().running)
            fail ("A scan is running already");
        juce::FileSearchPath paths;
        if (auto* list = args["folders"].getArray())
            for (const auto& f : *list)
            {
                const juce::File folder (f.toString());
                if (! juce::File::isAbsolutePath (f.toString()) || ! folder.isDirectory())
                    fail ("folders must be absolute paths of existing folders");
                paths.addIfNotAlreadyThere (folder);
            }
        if (paths.getNumPaths() == 0)
            paths = host.pluginFolders();
        else if (! (args["add"].isBool() && ! (bool) args["add"]))
        {
            auto all = host.pluginFolders();
            for (int i = 0; i < paths.getNumPaths(); ++i)
                all.addIfNotAlreadyThere (paths[i]);
            host.setPluginFolders (all);
        }
        host.startBackgroundScan (paths);
        juce::StringArray names;
        for (int i = 0; i < paths.getNumPaths(); ++i)
            names.add (paths[i].getFullPathName());
        return object ({ { "started", true }, { "folders", names } });
    }
    // A drag through the window, as the mouse makes it: from a point to another over "seconds" (default 0.6), with held
    // keys ("mods": ["ctrl"]...). Each end is { target, cell?, at? } (as ui.click's). A drag-and-drop (a loop onto the
    // timeline, a sample onto the rack) drops where it ends; a knob turns. Answers at once; the drag takes its time.
    if (op == "ui.drag")
    {
        const auto pointOf = [&] (const juce::var& end, juce::Component** comp) -> juce::Point<float> {
            if (! end["target"].isString())
                fail ("from and to need a target");
            const auto target = end["target"].toString();
            if (target.startsWith ("param:"))
            {
                const auto found = findParam (target);
                *comp = found.component;
                return found.component->localPointToGlobal (found.area.toFloat().getCentre());
            }
            auto* c = locate (target);
            if (c == nullptr)
                fail ("No control showing with the ID or name \"" + target + "\"");
            *comp = c;
            // (the end's own "cell" and "at", read as ui.click reads them)
            juce::Rectangle<int> area = c->getLocalBounds();
            if (const auto cell = cellOf (*c, &end["cell"]))
                area = *cell;
            auto p = area.toFloat().getCentre();
            if (const auto& at = end["at"]; at.getDynamicObject() != nullptr)
                p = { (float) area.getX() + (float) area.getWidth() * (float) juce::jlimit (0.0, 1.0, (double) at["x"]),
                      (float) area.getY() + (float) area.getHeight() * (float) juce::jlimit (0.0, 1.0, (double) at["y"]) };
            // ("dx" / "dy": pixels from there, e.g. a knob turned by dragging 40 px up)
            p += juce::Point<float> ((float) juce::jlimit (-2000.0, 2000.0, (double) end["dx"]), (float) juce::jlimit (-2000.0, 2000.0, (double) end["dy"]));
            return c->localPointToGlobal (p);
        };
        juce::Component* fromComp = nullptr;
        juce::Component* toComp = nullptr;
        const auto from = pointOf (args["from"], &fromComp);
        const auto to = pointOf (args["to"], &toComp);
        const auto seconds = args.hasProperty ("seconds") ? juce::jlimit (0.1, 10.0, (double) args["seconds"]) : 0.6;
        constexpr int stepMs = 16;
        const auto steps = juce::jmax (4, (int) (seconds * 1000.0 / stepMs));
        std::vector<juce::Point<float>> path;
        path.push_back (from);
        path.push_back (from + juce::Point<float> (2.0f, 1.0f)); // (a first small move: the press becomes a drag)
        for (int k = 1; k <= steps; ++k)
        {
            const auto u = (float) k / (float) steps;
            const auto e = u * u * (3.0f - 2.0f * u);
            path.push_back (from + (to - from) * e);
        }
        path.push_back (to); // (the release, where the drag ends)
        PeerGesture::drag (*fromComp->getTopLevelComponent(), path, keysOf (args["mods"]), false, stepMs);
        return object ({ { "from", object ({ { "x", from.x }, { "y", from.y } }) }, { "to", object ({ { "x", to.x }, { "y", to.y } }) },
                         { "seconds", seconds } });
    }
    // Text typed into a text field (a search box), one character every "interval" ms (default 70), as the keyboard would:
    // the field takes the keyboard focus and its listeners see each change. No key event reaches the operating system.
    if (op == "ui.type")
    {
        auto* editor = dynamic_cast<juce::TextEditor*> (&find());
        if (editor == nullptr)
            fail ("The target is not a text field");
        const auto typed = args["text"].toString().substring (0, 200);
        const auto interval = args["interval"].isInt() ? juce::jlimit (0, 1000, (int) args["interval"]) : 70;
        if (args["replace"].isBool() && (bool) args["replace"])
            editor->clear();
        editor->grabKeyboardFocus();
        editor->moveCaretToEnd();
        const juce::Component::SafePointer<juce::TextEditor> safe (editor);
        for (int i = 0; i < typed.length(); ++i)
            juce::Timer::callAfterDelay (juce::jmax (1, i * interval), [safe, c = typed.substring (i, i + 1)] {
                if (safe != nullptr)
                    safe->insertTextAtCaret (c);
            });
        return object ({ { "typing", typed }, { "seconds", (double) typed.length() * interval / 1000.0 } });
    }
    // A route between Mixer tracks in one undo step, as the route arrow's menu makes it: kind route (a send at unity, or
    // "level" 0..1.25 linear), routeOnly, sidechain (a level-0 send that the target's compressors listen to), sidechainOnly
    if (op == "mixer.route")
    {
        const auto& song = engine.getSong();
        if (! args["from"].isInt() || ! args["to"].isInt())
            fail ("from and to must be Mixer track numbers (0 = Master)");
        const auto from = (int) args["from"], to = (int) args["to"];
        if (from <= 0 || from >= (int) song.mixer.size() || to < 0 || to >= (int) song.mixer.size() || from == to)
            fail ("from must be an insert (1...) and to another track (0 = Master)");
        using K = mixerops::RouteKind;
        const auto kindName = args["kind"].isString() ? args["kind"].toString() : juce::String ("route");
        const auto kind = kindName == "route" ? K::route : kindName == "routeOnly" ? K::routeOnly : kindName == "sidechain" ? K::sidechain
                        : kindName == "sidechainOnly" ? K::sidechainOnly : (fail ("kind must be route, routeOnly, sidechain or sidechainOnly"), K::route);
        if ((kind == K::sidechain || kind == K::sidechainOnly) && to == 0)
            fail ("A sidechain goes to an insert, not to the Master");
        const auto hasLevel = args.hasProperty ("level");
        const auto level = hasLevel ? (float) juce::jlimit (0.0, 1.25, (double) args["level"]) : 1.0f;
        int routed = 0;
        engine.editSong ([&] (Song& s) {
            routed = mixerops::routeSelectedTo (s, { from }, to, kind);
            if (hasLevel && (kind == K::route || kind == K::routeOnly))
            {
                auto& track = s.mixer[(std::size_t) from];
                if (to == 0)
                    track.masterLevel = level;
                for (auto& send : track.sends)
                    if (send.target == to)
                        send.level = level;
            }
        });
        if (routed == 0)
            fail ("That route would make a loop (or the track cannot route there)");
        juce::Array<juce::var> sends;
        for (const auto& send : engine.getSong().mixer[(std::size_t) from].sends)
            sends.add (object ({ { "to", send.target }, { "level", send.level } }));
        return object ({ { "from", from }, { "to", to }, { "kind", kindName }, { "toMaster", engine.getSong().mixer[(std::size_t) from].toMaster },
                         { "sends", sends } });
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
