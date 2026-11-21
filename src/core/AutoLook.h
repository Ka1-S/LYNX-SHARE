#pragma once

// Issue #239 (round 190od, LYNX extra; like REAPER's SWS Auto Color/Icon): rules that colour a Playlist track, a mixer
// track or a channel, and give it an icon, by its name. "Kick" → blue and the kick drum icon, "REC" → the microphone.
// A rule acts when an item gets a name it matches (a rename, a new channel or track with that name); "Apply to this
// project" runs every rule on every item once. The rules are the user's (one list for every project, kept in the
// settings folder), so a template-free start gets the same colours.
//
// FL 2026 has no name rules: it colours by content (a Playlist track takes a dropped clip's colour, Auto name / Lock to
// content take the source's name and colour, core/PlaylistTrackOps.h), which LYNX already does. No JUCE.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::autolook
{
enum class Match : int { contains = 0, startsWith, is };
enum Target : int { playlistTracks = 1, mixerTracks = 2, channels = 4, everything = 7 };

struct Rule
{
    std::string text;            // what the name is compared with (case does not matter; spaces at the ends dropped)
    Match match = Match::contains;
    std::uint32_t colour = 0;    // 0xFFRRGGBB; 0 = the rule leaves the colour alone
    int icon = -1;               // FL's icon index (core/TrackIcons.h); −1 = leaves the icon alone, 0 = no icon
    int targets = everything;    // Target bits
    bool operator== (const Rule&) const = default;
};

struct Rules
{
    bool on = false;
    std::vector<Rule> list;
    bool active() const noexcept { return on && ! list.empty(); }
    bool operator== (const Rules&) const = default;
};

// The user's rules (the message thread reads and edits them; ui loads and saves them).
inline Rules& current()
{
    static Rules rules;
    return rules;
}

namespace detail
{
inline std::string folded (const std::string& s)
{
    auto b = s.find_first_not_of (" \t"), e = s.find_last_not_of (" \t");
    std::string out = b == std::string::npos ? std::string() : s.substr (b, e - b + 1);
    for (auto& ch : out)
        ch = (char) std::tolower ((unsigned char) ch);
    return out;
}
} // namespace detail

inline bool matches (const Rule& rule, const std::string& name)
{
    const auto text = detail::folded (rule.text), n = detail::folded (name);
    if (text.empty() || n.empty())
        return false;
    switch (rule.match)
    {
        case Match::is: return n == text;
        case Match::startsWith: return n.rfind (text, 0) == 0;
        case Match::contains: break;
    }
    return n.find (text) != std::string::npos;
}

// The first rule for `target` that matches `name` (the list's order decides), or null.
inline const Rule* find (const Rules& rules, const std::string& name, Target target)
{
    for (const auto& r : rules.list)
        if ((r.targets & target) != 0 && matches (r, name))
            return &r;
    return nullptr;
}

// What the rules apply to: the names before an edit (AudioEngine::editSong).
struct Names
{
    std::vector<std::string> tracks, mixer, channels;
    bool taken = false;
};
inline Names names (const Song& song, const Rules& rules = current())
{
    Names n;
    if (! rules.active())
        return n;
    n.taken = true;
    for (const auto& t : song.playlistTracks)
        n.tracks.push_back (t.name);
    for (const auto& m : song.mixer)
        n.mixer.push_back (m.name);
    for (const auto& c : song.channels)
        n.channels.push_back (c.name);
    return n;
}

namespace detail
{
template <typename Item>
bool give (Item& item, const Rule& rule)
{
    bool changed = false;
    if (rule.colour != 0 && item.colour != rule.colour)
        item.colour = rule.colour, changed = true;
    if (rule.icon >= 0 && item.icon != rule.icon)
        item.icon = rule.icon, changed = true;
    return changed;
}
// a name counts as new where it changed AND the list holds more of it than before (a track inserted above, or channels
// moved, shift names without renaming them)
template <typename Items>
int follow (Items& items, const std::vector<std::string>& before, const Rules& rules, Target target, int first = 0)
{
    int changed = 0;
    for (int k = first; k < (int) items.size(); ++k)
    {
        auto& item = items[(std::size_t) k];
        if (item.name.empty() || ((std::size_t) k < before.size() && before[(std::size_t) k] == item.name))
            continue;
        const auto now = std::count_if (items.begin(), items.end(), [&item] (const auto& i) { return i.name == item.name; });
        const auto was = std::count (before.begin(), before.end(), item.name);
        if (now <= was)
            continue;
        if (const auto* rule = find (rules, item.name, target); rule != nullptr && give (item, *rule))
            ++changed;
    }
    return changed;
}
} // namespace detail

// After an edit: every item whose name the edit gave it takes the colour and icon of the first rule that matches.
// Returns how many items changed.
inline int follow (Song& song, const Names& before, const Rules& rules = current())
{
    if (! before.taken || ! rules.active())
        return 0;
    int changed = detail::follow (song.playlistTracks, before.tracks, rules, playlistTracks);
    changed += detail::follow (song.mixer, before.mixer, rules, mixerTracks, 1); // (not the Master)
    changed += detail::follow (song.channels, before.channels, rules, channels);
    return changed;
}

// "Apply to this project": every named item that a rule matches takes its colour and icon. Returns how many changed.
inline int applyAll (Song& song, const Rules& rules = current())
{
    if (rules.list.empty())
        return 0;
    int changed = 0;
    for (auto& t : song.playlistTracks)
        if (const auto* rule = find (rules, t.name, playlistTracks); rule != nullptr && detail::give (t, *rule))
            ++changed;
    for (std::size_t m = 1; m < song.mixer.size(); ++m)
        if (const auto* rule = find (rules, song.mixer[m].name, mixerTracks); rule != nullptr && detail::give (song.mixer[m], *rule))
            ++changed;
    for (auto& c : song.channels)
        if (const auto* rule = find (rules, c.name, channels); rule != nullptr && detail::give (c, *rule))
            ++changed;
    return changed;
}

// The rules as text (the settings file): "on 1", then one rule per line: match, colour (hex, - = none), icon (- =
// none), targets, the text (the rest of the line).
inline std::string toText (const Rules& rules)
{
    std::ostringstream out;
    out << "lynx-auto-look 1\non " << (rules.on ? 1 : 0) << "\n";
    for (const auto& r : rules.list)
    {
        char colour[16];
        std::snprintf (colour, sizeof (colour), "%08X", (unsigned) r.colour);
        out << (int) r.match << ' ' << (r.colour != 0 ? std::string (colour) : std::string ("-")) << ' '
            << (r.icon >= 0 ? std::to_string (r.icon) : std::string ("-")) << ' ' << r.targets << ' ' << r.text << "\n";
    }
    return out.str();
}

inline Rules fromText (const std::string& text)
{
    Rules rules;
    std::istringstream in (text);
    std::string line;
    if (! std::getline (in, line) || line.rfind ("lynx-auto-look", 0) != 0)
        return rules;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind ("on ", 0) == 0)
        {
            rules.on = line.size() > 3 && line[3] == '1';
            continue;
        }
        std::istringstream fields (line);
        int match = 0, targets = everything;
        std::string colour, icon;
        if (! (fields >> match >> colour >> icon >> targets))
            continue;
        std::string rest;
        std::getline (fields, rest);
        Rule r;
        r.match = (Match) std::clamp (match, 0, 2);
        r.colour = colour == "-" ? 0u : (std::uint32_t) std::strtoul (colour.c_str(), nullptr, 16);
        r.icon = icon == "-" ? -1 : std::max (0, std::atoi (icon.c_str()));
        r.targets = std::clamp (targets, 1, (int) everything);
        r.text = rest.empty() ? std::string() : rest.substr (1);
        if (! detail::folded (r.text).empty())
            rules.list.push_back (r);
    }
    return rules;
}
} // namespace lynx::autolook
