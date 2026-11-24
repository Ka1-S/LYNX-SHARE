#pragma once

// Round 190oa (DF-130, owner decision F3): plugin and channel windows keep their place with the project, as FL's do —
// FL writes each plugin's window position, size and open state into its wrapper record (event 0xD4: x / y at +36 / +40,
// width / height at +44 / +48, the flags at +16; Thief.flp's 122 records all carry a position, REA 190oa R3) and opens
// the project with those windows where they were. LYNX keeps the places of the windows a project shows (a channel's,
// a mixer slot's) in a registry keyed by what the window edits; the project file carries it next to the window layout
// (ProjectFile::WindowLayout::places) and a load opens the windows that were open, where they were.
//
// A window class calls tag() once it has its own default place: a known place moves it there, and its moves, resizes
// and closing are followed from then on. View state only: no undo step, the Song is untouched.

#include <map>
#include <set>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/NativeChild.h"

namespace lynx
{
juce::Component* editorDockArea(); // (ui/WindowOwner.h)
}

namespace lynx::windowplaces
{
// Hotfix 0.1.31.1 (round 190qp, "plugin interfaces refuse to open after a few clicks"): a docked window (a native child
// window of the main window, ui/NativeChild.h; every docked editor on Windows) keeps its place in the editor dock's
// coordinates (`docked`). 0.1.31 kept its screen place and gave it back as the child window's bounds, which are relative
// to the main window: each reopen moved the window by the dock's place on the screen, until it opened outside the main
// window. A window of its own keeps its screen place, as before.
struct Place
{
    juce::Rectangle<int> bounds;
    bool open = false;
    bool docked = false;
};

namespace detail
{
// the dock a docked window lives in (nullptr: a window of its own, or drawn inside the main window)
inline juce::Component* dockOf (const juce::Component& window)
{
    return nativechild::isAttached (window) ? editorDockArea() : nullptr;
}
} // namespace detail

inline std::map<juce::String, Place>& all()
{
    static std::map<juce::String, Place> places;
    return places;
}

// Rollup 0.1.31 step 7 (PRs #248 / #256 by FKHR26): the gear in a docked channel or plugin window's title bar hides the
// window's header strip (tabs, presets, the channel's controls); shown by default, each window keeps its choice (the window
// kind and its place key), with the places: in the project as [key, "headerHidden"], an entry older LYNX skips
inline std::set<juce::String>& hiddenHeaders()
{
    static std::set<juce::String> keys;
    return keys;
}
inline juce::String headerKey (const char* kind, const juce::String& placeKey) { return placeKey.isEmpty() ? juce::String() : juce::String (kind) + "|" + placeKey; }
inline bool headerHidden (const juce::String& key) { return key.isNotEmpty() && hiddenHeaders().count (key) != 0; }
inline void setHeaderHidden (const juce::String& key, bool hidden)
{
    if (key.isEmpty())
        return;
    if (hidden)
        hiddenHeaders().insert (key);
    else
        hiddenHeaders().erase (key);
}

inline juce::String channelKey (std::uint64_t uid, int index) { return uid != 0 ? "ch:" + juce::String ((juce::int64) uid) : "chi:" + juce::String (index); }
inline juce::String slotKey (int track, int slot) { return "fx:" + juce::String (track) + ":" + juce::String (slot); }

namespace detail
{
class Follower final : public juce::ComponentListener
{
public:
    explicit Follower (juce::String k) : key (std::move (k)) {}
    void componentMovedOrResized (juce::Component& c, bool, bool) override { note (c); }
    void componentVisibilityChanged (juce::Component& c) override { note (c); }
    void componentBeingDeleted (juce::Component& c) override
    {
        if (auto it = all().find (key); it != all().end())
            it->second.open = false;
        c.removeComponentListener (this);
        followers().erase (&c);
    }
    static std::map<juce::Component*, std::unique_ptr<Follower>>& followers()
    {
        static std::map<juce::Component*, std::unique_ptr<Follower>> list;
        return list;
    }

private:
    void note (juce::Component& c)
    {
        auto& place = all()[key];
        place.open = c.isVisible() && c.isOnDesktop();
        if (auto* dock = dockOf (c))
        {
            if (const auto r = nativechild::boundsIn (c, *dock); ! r.isEmpty())
                place.bounds = r, place.docked = true;
        }
        else if (c.isOnDesktop() && ! c.getScreenBounds().isEmpty())
            place.bounds = c.getScreenBounds(), place.docked = false;
    }
    juce::String key;
};
} // namespace detail

// `window`: a top-level window. A place known for `key` moves it there (kept on a display); then it is followed.
inline void tag (juce::Component& window, const juce::String& key)
{
    if (key.isEmpty())
        return;
    if (auto it = all().find (key); it != all().end() && ! it->second.bounds.isEmpty() && window.isOnDesktop())
    {
        if (auto* dock = detail::dockOf (window))
        {
            // docked: back where it was in the dock (its own size: the page it opens on decides it), kept inside the
            // dock. A place it had as a window of its own is not a place in the dock: the dock's placement stays.
            if (it->second.docked)
            {
                auto r = window.getBounds().withPosition (it->second.bounds.getPosition());
                const auto area = dock->getLocalBounds();
                r.setX (juce::jlimit (0, juce::jmax (0, area.getWidth() - r.getWidth()), r.getX()));
                r.setY (juce::jlimit (0, juce::jmax (0, area.getHeight() - r.getHeight()), r.getY()));
                nativechild::place (window, *dock, r);
            }
        }
        else if (! it->second.docked)
        {
            auto r = it->second.bounds;
            const auto& displays = juce::Desktop::getInstance().getDisplays();
            if (const auto* d = displays.getDisplayForRect (r); d != nullptr)
                r = r.constrainedWithin (d->userArea);
            window.setBounds (r);
        }
    }
    auto& list = detail::Follower::followers();
    if (auto it = list.find (&window); it != list.end())
        window.removeComponentListener (it->second.get());
    auto follower = std::make_unique<detail::Follower> (key);
    window.addComponentListener (follower.get());
    auto& place = all()[key];
    place.open = window.isVisible() && window.isOnDesktop();
    if (auto* dock = detail::dockOf (window))
        place.bounds = nativechild::boundsIn (window, *dock), place.docked = true;
    else if (window.isOnDesktop())
        place.bounds = window.getScreenBounds(), place.docked = false;
    list[&window] = std::move (follower);
}

// the registry as stored in a project: [[key, x, y, w, h, open], ...]; a docked window's place (in the dock's
// coordinates) has a 7th entry, "docked" (hotfix 0.1.31.1: 0.1.31 reads only six-entry places, so it skips them)
inline juce::var toVar()
{
    juce::Array<juce::var> list;
    for (const auto& [key, place] : all())
        if (! place.bounds.isEmpty())
        {
            juce::Array<juce::var> e { key, place.bounds.getX(), place.bounds.getY(), place.bounds.getWidth(), place.bounds.getHeight(), place.open };
            if (place.docked)
                e.add ("docked");
            list.add (e);
        }
    for (const auto& key : hiddenHeaders())
        list.add (juce::Array<juce::var> { key, "headerHidden" });
    return list.isEmpty() ? juce::var() : juce::var (list);
}

inline void fromVar (const juce::var& v)
{
    all().clear();
    hiddenHeaders().clear();
    if (const auto* list = v.getArray())
        for (const auto& e : *list)
            if (e.isArray() && (e.size() == 6 || (e.size() == 7 && e[6].toString() == "docked")))
                all()[e[0].toString()] = { { (int) e[1], (int) e[2], (int) e[3], (int) e[4] }, (bool) e[5], e.size() == 7 };
            else if (e.isArray() && e.size() == 2 && e[1].toString() == "headerHidden")
                hiddenHeaders().insert (e[0].toString());
}
} // namespace lynx::windowplaces
