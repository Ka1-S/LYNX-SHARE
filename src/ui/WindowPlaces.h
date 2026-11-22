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

namespace lynx::windowplaces
{
struct Place
{
    juce::Rectangle<int> bounds;
    bool open = false;
};

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
        if (c.isOnDesktop() && ! c.getScreenBounds().isEmpty())
            place.bounds = c.getScreenBounds();
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
        auto r = it->second.bounds;
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        if (const auto* d = displays.getDisplayForRect (r); d != nullptr)
            r = r.constrainedWithin (d->userArea);
        window.setBounds (r);
    }
    auto& list = detail::Follower::followers();
    if (auto it = list.find (&window); it != list.end())
        window.removeComponentListener (it->second.get());
    auto follower = std::make_unique<detail::Follower> (key);
    window.addComponentListener (follower.get());
    auto& place = all()[key];
    place.open = window.isVisible() && window.isOnDesktop();
    if (window.isOnDesktop())
        place.bounds = window.getScreenBounds();
    list[&window] = std::move (follower);
}

// the registry as stored in a project: [[key, x, y, w, h, open], ...]
inline juce::var toVar()
{
    juce::Array<juce::var> list;
    for (const auto& [key, place] : all())
        if (! place.bounds.isEmpty())
            list.add (juce::Array<juce::var> { key, place.bounds.getX(), place.bounds.getY(), place.bounds.getWidth(), place.bounds.getHeight(), place.open });
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
            if (e.isArray() && e.size() == 6)
                all()[e[0].toString()] = { { (int) e[1], (int) e[2], (int) e[3], (int) e[4] }, (bool) e[5] };
            else if (e.isArray() && e.size() == 2 && e[1].toString() == "headerHidden")
                hiddenHeaders().insert (e[0].toString());
}
} // namespace lynx::windowplaces
