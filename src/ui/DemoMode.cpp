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
#include "ui/MainComponent.h"
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
    return { "ui.window", "ui.find", "ui.click", "ui.list", "ui.layout", "ui.select_track", "ui.show", "ui.welcome", "ui.clipmenu", "ui.polyeditor", "session.start", "session.status" };
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
void pressAndRelease (juce::Component& c, juce::Point<float> at, int clicks)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const juce::ModifierKeys down (juce::ModifierKeys::leftButtonModifier);
    for (int i = 1; i <= clicks; ++i)
    {
        const juce::MouseEvent press (source, at, down, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at, now, i, false);
        c.mouseDown (press);
        const juce::MouseEvent release (source, at, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at, now, i, false);
        c.mouseUp (release);
        if (i == 2)
            c.mouseDoubleClick (release);
    }
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
    const auto find = [&window, &args]() -> juce::Component& {
        const auto target = text (args, "target");
        if (auto* c = onboardui::findShowing (window, target))
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
    const auto cellOf = [&args] (juce::Component& c) -> std::optional<juce::Rectangle<int>> {
        const auto& cell = args["cell"];
        if (cell.getDynamicObject() == nullptr)
            return std::nullopt;
        auto* source = dynamic_cast<demo::CellSource*> (&c);
        if (source == nullptr || ! cell["row"].isInt() || ! cell["column"].isInt())
            fail ("cell needs a control made of cells and integer row and column");
        const auto r = source->demoCellBounds ((int) cell["row"], (int) cell["column"]);
        if (r.isEmpty())
            fail ("No such cell");
        return r;
    };
    if (op == "ui.find")
    {
        const auto target = text (args, "target");
        if (auto* c = onboardui::findShowing (window, target))
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
            if (auto* c = onboardui::findShowing (window, under))
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
        if (const auto cell = cellOf (c))
        {
            pressAndRelease (c, cell->toFloat().getCentre(), clicks); // (the control's own mouse handling: a step toggles)
            return object ({ { "clicked", true }, { "control", described }, { "cell", rect (c.localAreaToGlobal (*cell)) } });
        }
        if (auto* b = dynamic_cast<juce::Button*> (&c); b != nullptr && clicks == 1)
            b->triggerClick(); // (the button's own click, as a press of its shortcut)
        else if (auto* box = dynamic_cast<juce::ComboBox*> (&c); box != nullptr && clicks == 1)
            box->showPopup();
        else
        {
            // "at": a point inside the control as fractions of its size ({ "x": 0.5, "y": 0.5 } = its centre)
            auto at = c.getLocalBounds().toFloat().getCentre();
            if (const auto& p = args["at"]; p.getDynamicObject() != nullptr)
                at = { (float) c.getWidth() * (float) juce::jlimit (0.0, 1.0, (double) p["x"]), (float) c.getHeight() * (float) juce::jlimit (0.0, 1.0, (double) p["y"]) };
            pressAndRelease (c, at, clicks);
        }
        return object ({ { "clicked", true }, { "control", described } });
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
        else if (what == "pianoRoll") showPanel (Panel::pianoRoll);
        else if (what == "playlist") showPanel (Panel::playlist);
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
