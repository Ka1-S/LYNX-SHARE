#pragma once

// The user's main-toolbar layout (no JUCE): which panel groups are shown and in which order, the icon size
// and the global UI scale. Saved as one line of text in the UI options. Like FL Studio's toolbar editing
// (docs/WORKFLOW_FL.md section 5.1), groups can be hidden and reordered; the menu with the hint bar under it
// always comes first and cannot be hidden. LYNX keeps it simple: no free spacers or presets, but a
// right-click menu on the toolbar and a "Customize toolbar" dialog.

#include <algorithm>
#include <array>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#ifndef TR_NOOP
#define TR_NOOP(text) text // marks the names for translation (ui/I18n.h defines the same)
#endif

namespace lynx::toolbarlayout
{
enum class Group
{
    transport, // Pattern/Song, play, stop, record, tempo, time display and song position
    master,    // master volume over master pitch
    record,    // metronome, wait for input, count-in, blend; what to record, typing piano, tempo tapper, MIDI light
    snap,      // global snap over the pattern selector
    windows,   // Playlist, Channel Rack, Piano Roll, Mixer, Browser, plugin picker
    file,      // new, open, save, export, undo, redo
    meters,    // CPU / memory / polyphony
    output,    // output scope and peak meter
    chord,     // FL's Chord detection panel: the chord being played (round 181b)
    multilink, // FL's Multilink controllers switch (MultiLinkBtn; round 177b-4): hidden by default, as in FL
    key,       // the project key button (round 184a S11; LYNX's own): hidden by default; Simple and Clip-launch show it
    link,      // Ableton Link's toggle and peer count (round 190gl): the reference DAW shows it first in its control bar
               // ("Show Link Toggle", on there); FL has none, so the Pattern layout hides it until the user shows it
    count_
};

constexpr int groupCount = (int) Group::count_;

// The id used in the settings line (stable; never translated).
inline const char* groupId (Group g)
{
    static constexpr std::array<const char*, groupCount> ids { "transport", "master", "record", "snap", "windows", "file", "meters", "output", "chord", "multilink", "key", "link" };
    return ids[(std::size_t) g];
}

// The English name shown in the menu and the dialog (translated by the UI).
inline const char* groupName (Group g)
{
    static constexpr std::array<const char*, groupCount> names {
        TR_NOOP ("Transport"), TR_NOOP ("Master volume and pitch"), TR_NOOP ("Recording options"), TR_NOOP ("Snap and pattern"),
        TR_NOOP ("Window shortcuts"), TR_NOOP ("File and undo"), TR_NOOP ("CPU, memory and polyphony"), TR_NOOP ("Output monitor"), TR_NOOP ("Chord detection"),
        TR_NOOP ("Multilink to controllers"), TR_NOOP ("Project key"), TR_NOOP ("Ableton Link")
    };
    return names[(std::size_t) g];
}

// When the window is too narrow, the group with the lowest priority is dropped first. The transport stays.
// Round 170e2: the recording options outlast the file buttons (FL users look for them on the toolbar; New / Open /
// Save / Undo are also in the File and Edit menus and their keys).
inline int groupPriority (Group g)
{
    static constexpr std::array<int, groupCount> p { 100, 80, 70, 90, 75, 60, 55, 50, 45, 65, 72, 74 }; // (Multilink: one button, shown only on request; Key: shown only in the workflows that use it, round 184a S11; Link: one switch, round 190gl)
    return p[(std::size_t) g];
}

// Groups that sit at the right end of the toolbar (after the spare room) by default.
inline bool rightAligned (Group g) { return g == Group::meters || g == Group::output || g == Group::chord; }

// Shown on a new install: every group but the Multilink switch (FL keeps its MultiLinkBtn off the default toolbar; it
// is shown from the toolbar's edit mode). Round 184a: the Key button is LYNX's own, so FL's Pattern workflow opens
// without it; the Simple and Clip-launch workflows show it (MainComponent::setLayoutStyle) until the user decides.
// Round 190gl: the Ableton Link toggle likewise (the Clip-launch workflow shows it, as the reference DAW does).
inline bool shownByDefault (Group g) { return g != Group::multilink && g != Group::key && g != Group::link; }

// The toolbar's order on a new install. Round 181b: FL has its Chord detection panel on the default toolbar, on the
// left of its meters (seen in FL 2026's toolbar edit mode).
inline const std::array<Group, groupCount>& defaultOrder()
{
    // round 177b-4: FL's MultiLinkBtn sits in its bottom toolbar after the window, pattern and edit buttons (DFM:
    // ... ParamCtrlBigBtn, DownloaderBtn, MultiLinkBtn, GopherBtn); LYNX's nearest neighbour is the window shortcuts
    // round 190gl: the Link toggle comes first, left of the transport, as the reference DAW's control bar starts with it
    static constexpr std::array<Group, groupCount> order { Group::link, Group::transport, Group::master, Group::record, Group::snap, Group::windows,
                                                           Group::multilink, Group::file, Group::key, Group::chord, Group::meters, Group::output };
    return order;
}

enum class IconSize
{
    small,
    medium,
    large
};

// Height of one toolbar row and the share of a button's height its icon takes, per icon size.
// Medium is the default. Round 170e3 (coordinator review: "16 px icons read as tiny at 1920"): 19 px icons on 31 px
// rows (was 16 on 28), still drawn with one fine line (ui/Icons.h strokeWidth). For comparison, FL Studio 2026 at 100%
// draws its toolbar glyphs at roughly 16-20 px on buttons about 26-30 px apart (docs/analise/UI_toolbar_study.md).
inline int rowHeight (IconSize s) { return s == IconSize::small ? 26 : s == IconSize::medium ? 31 : 37; }
inline float iconFraction (IconSize) { return 0.62f; }
inline float iconMaxPx (IconSize s) { return s == IconSize::small ? 15.0f : s == IconSize::medium ? 19.0f : 22.0f; }
// Editor tool strips (inside the panel title row): icon box in px and button width.
inline float toolIconPx (IconSize s) { return s == IconSize::small ? 16.0f : s == IconSize::medium ? 20.0f : 22.0f; }
inline int toolButtonWidth (IconSize s, int height) { return height + (s == IconSize::small ? 0 : s == IconSize::medium ? 2 : 4); }

// Round 170e3: on a very wide window (a 4K screen at 100 %) the default size grows one step, so the toolbar and the
// editors' tool strips use the width instead of leaving a gap; small and large stay as picked.
constexpr int wideWindowPx = 2400;
inline IconSize effectiveIconSize (IconSize chosen, int windowWidth) noexcept
{
    return chosen == IconSize::medium && windowWidth >= wideWindowPx ? IconSize::large : chosen;
}

// Round 170e3: the room left after the panels and the hint column is shared out as wider gaps between the panels
// (up to `maxExtra` px each) before the rest goes in front of the right-hand meters.
inline int extraGap (int spare, int gaps, int maxExtra = 28) noexcept
{
    return gaps <= 0 || spare <= 0 ? 0 : std::min (maxExtra, spare / gaps);
}

constexpr std::array<int, 9> uiScales { 80, 90, 100, 110, 125, 150, 175, 200, 250 };
inline int clampScale (int percent) { return std::clamp (percent, 75, 250); }

struct Entry
{
    Group group;
    bool shown = true;
};

struct Layout
{
    std::vector<Entry> entries; // every group exactly once, in toolbar order
    IconSize iconSize = IconSize::medium;
    int uiScale = 100; // percent
    bool separators = true; // hairlines between the panels (FL's "Separators")
    bool visible = true;    // the whole toolbar (Ctrl+F11 hides it, as in FL)

    static Layout makeDefault()
    {
        Layout l;
        for (auto g : defaultOrder())
            l.entries.push_back ({ g, shownByDefault (g) });
        return l;
    }

    bool isShown (Group g) const
    {
        for (const auto& e : entries)
            if (e.group == g)
                return e.shown;
        return false;
    }

    void setShown (Group g, bool shown)
    {
        for (auto& e : entries)
            if (e.group == g)
                e.shown = shown;
    }

    int indexOf (Group g) const
    {
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].group == g)
                return (int) i;
        return -1;
    }

    // Moves a group by `delta` places (clamped); returns true if it moved.
    bool move (Group g, int delta)
    {
        const auto from = indexOf (g);
        if (from < 0)
            return false;
        const auto to = std::clamp (from + delta, 0, (int) entries.size() - 1);
        if (to == from)
            return false;
        const auto e = entries[(std::size_t) from];
        entries.erase (entries.begin() + from);
        entries.insert (entries.begin() + to, e);
        return true;
    }

    // Moves `g` so that it sits just before `before` (nullptr: at the end). Returns true if the order changed.
    // Used by the toolbar's edit mode, where a panel is dropped between two others.
    bool placeBefore (Group g, const Group* before)
    {
        const auto from = indexOf (g);
        if (from < 0 || (before != nullptr && *before == g))
            return false;
        const auto old = entries;
        const auto e = entries[(std::size_t) from];
        entries.erase (entries.begin() + from);
        auto to = before != nullptr ? indexOf (*before) : -1;
        if (to < 0)
            entries.push_back (e);
        else
            entries.insert (entries.begin() + to, e);
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].group != old[i].group)
                return true;
        return false;
    }

    bool isDefault() const
    {
        const auto d = makeDefault();
        if (iconSize != d.iconSize || uiScale != d.uiScale || separators != d.separators || visible != d.visible)
            return false;
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].group != d.entries[i].group || entries[i].shown != d.entries[i].shown)
                return false;
        return true;
    }

    // "transport,-master,record,...", where "-" marks a hidden group.
    std::string orderToString() const
    {
        std::string s;
        for (const auto& e : entries)
        {
            if (! s.empty())
                s += ',';
            if (! e.shown)
                s += '-';
            s += groupId (e.group);
        }
        return s;
    }

    // Reads an order line. Unknown ids are ignored, duplicates keep the first, and groups missing from the
    // line (e.g. added in a newer version) are appended at their default place, shown as on a new toolbar.
    void orderFromString (const std::string& text)
    {
        std::vector<Entry> parsed;
        std::stringstream in (text);
        std::string item;

        while (std::getline (in, item, ','))
        {
            bool shown = true;
            while (! item.empty() && (item.front() == ' ' || item.front() == '-'))
            {
                if (item.front() == '-')
                    shown = false;
                item.erase (item.begin());
            }
            while (! item.empty() && (item.back() == ' ' || item.back() == '\r'))
                item.pop_back();

            for (int i = 0; i < groupCount; ++i)
                if (item == groupId ((Group) i)
                    && std::none_of (parsed.begin(), parsed.end(), [i] (const Entry& e) { return e.group == (Group) i; }))
                    parsed.push_back ({ (Group) i, shown });
        }

        // Round 181b: the Chord detection panel is new. A line that is just the old default gets it as a new install
        // does; a customised toolbar stays as its user made it (FL saves the list of placed panels, so one that isn't
        // listed stays in the toolbar's edit mode). It can be shown from there.
        const auto plainOldDefault = [&parsed] {
            const std::array<Group, 8> old { Group::transport, Group::master, Group::record, Group::snap, Group::windows, Group::file, Group::meters, Group::output };
            if (parsed.empty())
                return true;
            if (parsed.size() != old.size())
                return false;
            for (std::size_t i = 0; i < old.size(); ++i)
                if (parsed[i].group != old[i] || ! parsed[i].shown)
                    return false;
            return true;
        }();

        const auto& order = defaultOrder();
        for (std::size_t i = 0; i < order.size(); ++i)
        {
            const auto group = order[i];
            if (std::any_of (parsed.begin(), parsed.end(), [group] (const Entry& e) { return e.group == group; }))
                continue;

            // after the group that precedes it by default, if present
            auto at = i == 0 ? parsed.begin() : parsed.end();
            for (std::size_t before = i; before-- > 0;)
            {
                const auto previous = order[before];
                auto it = std::find_if (parsed.begin(), parsed.end(), [previous] (const Entry& e) { return e.group == previous; });
                if (it != parsed.end())
                {
                    at = it + 1;
                    break;
                }
            }
            parsed.insert (at, { group, shownByDefault (group) && (group != Group::chord || plainOldDefault) });
        }

        entries = parsed;
    }

    static const char* iconSizeId (IconSize s) { return s == IconSize::small ? "small" : s == IconSize::medium ? "medium" : "large"; }

    static IconSize iconSizeFromString (const std::string& s)
    {
        if (s == "small")
            return IconSize::small;
        if (s == "large")
            return IconSize::large;
        return IconSize::medium;
    }
};

// Where a panel dragged in the toolbar's edit mode lands: `shown` lists the panels on the toolbar, left to right,
// with the x of their centres; the dragged panel goes before the first other panel whose centre is right of `x`
// (none: at the end). Returns that panel, or groupCount for "at the end".
inline int dropBefore (const std::vector<std::pair<Group, int>>& shown, Group dragged, int x)
{
    for (const auto& [group, centre] : shown)
        if (group != dragged && centre > x)
            return (int) group;
    return groupCount;
}

// Ready-made layouts (LYNX's own, like FL's toolbar presets): which groups show, in the default order.
enum class Preset
{
    standard, // every group
    compact,  // transport, master, snap and pattern, windows
    minimal,  // transport and windows
    mixing,   // transport, master, windows, meters, output
    count_
};

constexpr int presetCount = (int) Preset::count_;

inline const char* presetName (Preset p)
{
    static constexpr std::array<const char*, presetCount> names { TR_NOOP ("Default (everything)"), TR_NOOP ("Compact"),
                                                                  TR_NOOP ("Minimal"), TR_NOOP ("Mixing") };
    return names[(std::size_t) p];
}

// The preset applied to `from` (icon size, UI scale and the other options are kept).
inline Layout applyPreset (Layout from, Preset p)
{
    const auto d = Layout::makeDefault();
    from.entries = d.entries;
    auto show = [&from] (std::initializer_list<Group> groups) {
        for (auto& e : from.entries)
            e.shown = std::find (groups.begin(), groups.end(), e.group) != groups.end();
    };
    switch (p)
    {
        case Preset::compact: show ({ Group::transport, Group::master, Group::snap, Group::windows }); break;
        case Preset::minimal: show ({ Group::transport, Group::windows }); break;
        case Preset::mixing: show ({ Group::transport, Group::master, Group::windows, Group::meters, Group::output }); break;
        case Preset::standard:
        case Preset::count_: break;
    }
    return from;
}

// The preset the layout's groups match, or presetCount (a custom layout).
inline int matchingPreset (const Layout& l)
{
    for (int p = 0; p < presetCount; ++p)
        if (applyPreset (l, (Preset) p).orderToString() == l.orderToString())
            return p;
    return presetCount;
}

// Decides which groups are visible for `room` px: every shown group needs widths[group] (+ `gap`); while
// they don't fit with `minFlexible` px left for the hint column, the shown group with the lowest priority
// (never the transport) is dropped. Returns visibility per group.
inline std::array<bool, groupCount> fit (const Layout& layout, const std::array<int, groupCount>& widths, int gap, int room, int minFlexible)
{
    std::array<bool, groupCount> visible {};
    for (const auto& e : layout.entries)
        visible[(std::size_t) e.group] = e.shown;

    auto used = [&] {
        int total = 0;
        for (int i = 0; i < groupCount; ++i)
            total += visible[(std::size_t) i] ? widths[(std::size_t) i] + gap : 0;
        return total;
    };

    while (used() + minFlexible > room)
    {
        int lowest = -1;
        for (int i = 0; i < groupCount; ++i)
            if (visible[(std::size_t) i] && groupPriority ((Group) i) < 100
                && (lowest < 0 || groupPriority ((Group) i) < groupPriority ((Group) lowest)))
                lowest = i;
        if (lowest < 0)
            break;
        visible[(std::size_t) lowest] = false;
    }

    return visible;
}
} // namespace lynx::toolbarlayout
