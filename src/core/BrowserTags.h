#pragma once

// The Browser's tags (round 190iz, pain point B14; FL's Browser tagging, docs/analise/REA_rodada190iz.md §2).
//
// FL Studio's Browser (manual "browser.htm" › Search / File menu; FLEngine strings "Add tag", "Edit tags", "Tags",
// "|Search by tags", "|Show items with any tags", "|Show items with all tags", "FilterTags", "TTagDatabase"):
//   - a file (or folder, or Plugin database entry) carries any number of user tags; "Add a tag" takes one or more names
//     separated by commas ("Kick,808,C#" gives three tags); "Tags ▸" ticks an existing tag on or off; "Edit tags"
//     renames or deletes a tag everywhere;
//   - the search row's "Tags" button shows the tags as a grid; the ones picked filter the content, with "Any" (items
//     that carry one of them) or "All" (items that carry every one); a tab remembers its picked tags ("FilterTags");
//   - the tags live in one tag database of the user, not in a project: "FL Studio was unable to read the tag database.
//     Tags and Favourites cannot be changed." (Favourite is FL's Starred, LYNX's collection 1).
// LYNX keeps them in the settings folder (browser-tags.txt), one store for every Browser of the process, as the
// collections (core/BrowserCollections.h); a tag name keeps the case it was first typed in and is matched ignoring
// ASCII case. Live's browser search takes "#tag" too (Live 12 manual 4.2); LYNX's search box does both.
// No JUCE: tested in isolation (tests/Browser190izTests.inc).

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace lynx::browsertags
{
namespace detail
{
inline std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}
inline std::string trim (const std::string& s)
{
    const auto first = s.find_first_not_of (" \t");
    if (first == std::string::npos)
        return {};
    const auto last = s.find_last_not_of (" \t");
    return s.substr (first, last - first + 1);
}
} // namespace detail

// A tag name as it is stored: trimmed, the characters the file format uses ('|', tab, line breaks, ',') removed.
// Round 190qi: '/' makes a tag a child of another ("Drums/Kick" sits under "Drums" in the tag lists); each part is
// trimmed and empty parts go ("Drums / /Kick/" = "Drums/Kick").
inline std::string cleanTag (const std::string& name)
{
    std::string out;
    for (const auto c : name)
        if (c != '|' && c != '\t' && c != '\n' && c != '\r' && c != ',')
            out += c;
    out = detail::trim (out);
    if (out.find ('/') == std::string::npos)
        return out;
    std::string joined, part;
    std::istringstream in (out);
    while (std::getline (in, part, '/'))
        if (const auto t = detail::trim (part); ! t.empty())
            joined += (joined.empty() ? "" : "/") + t;
    return joined;
}

// "Drums/Kick" -> "Drums" ("" for a top-level tag) and "Kick"
inline std::string tagParent (const std::string& tag)
{
    const auto slash = tag.rfind ('/');
    return slash == std::string::npos ? std::string() : tag.substr (0, slash);
}
inline std::string tagLeaf (const std::string& tag)
{
    const auto slash = tag.rfind ('/');
    return slash == std::string::npos ? tag : tag.substr (slash + 1);
}

// FL's "Add a tag" text: names separated by commas, each trimmed, empty ones and repeats (ASCII case ignored) dropped.
inline std::vector<std::string> splitTags (const std::string& text)
{
    std::vector<std::string> out;
    std::set<std::string> seen;
    std::string part;
    std::istringstream in (text);
    while (std::getline (in, part, ','))
    {
        const auto tag = cleanTag (part);
        if (! tag.empty() && seen.insert (detail::lower (tag)).second)
            out.push_back (tag);
    }
    return out;
}

// FL's tag filter logic (the tags panel's Any / All buttons).
enum class Match
{
    any, // items that carry at least one of the picked tags
    all  // items that carry every picked tag
};

struct TagCount
{
    std::string name;
    int items = 0;
    bool operator== (const TagCount&) const = default;
};

struct Store
{
    // every tag the user made, by lower-case key -> its name as typed first (a tag stays until "Edit tags" deletes it,
    // also with no item left: FL's tag list keeps it)
    std::map<std::string, std::string> names;
    // item (a file's / folder's path, or collections::pluginItem) -> the lower-case keys of its tags; never empty
    std::map<std::string, std::set<std::string>> items;

    static std::string key (const std::string& tag) { return detail::lower (cleanTag (tag)); }

    bool exists (const std::string& tag) const { return names.count (key (tag)) > 0; }
    std::string nameOf (const std::string& tag) const
    {
        const auto it = names.find (key (tag));
        return it != names.end() ? it->second : cleanTag (tag);
    }

    // Makes the tag known (FL's tag list) without putting it on anything. Returns its stored name ("" = not a name).
    std::string create (const std::string& tag)
    {
        const auto clean = cleanTag (tag);
        if (clean.empty())
            return {};
        return names.emplace (detail::lower (clean), clean).first->second;
    }

    bool has (const std::string& item, const std::string& tag) const
    {
        const auto it = items.find (item);
        return it != items.end() && it->second.count (key (tag)) > 0;
    }

    // Puts the tag on the item or takes it off. Returns whether anything changed.
    bool set (const std::string& item, const std::string& tag, bool on)
    {
        if (item.empty() || item.find ('\n') != std::string::npos)
            return false;
        const auto k = key (tag);
        if (k.empty())
            return false;
        if (on)
        {
            create (tag);
            return items[item].insert (k).second;
        }
        const auto it = items.find (item);
        if (it == items.end() || it->second.erase (k) == 0)
            return false;
        if (it->second.empty())
            items.erase (it);
        return true;
    }

    // FL's "Add a tag": every name of `text` (comma-separated) onto every item. Returns how many tag marks were added.
    int add (const std::vector<std::string>& onItems, const std::string& text)
    {
        int added = 0;
        for (const auto& tag : splitTags (text))
            for (const auto& item : onItems)
                added += set (item, tag, true) ? 1 : 0;
        return added;
    }

    // The item's tags by their names, A-Z (ASCII case ignored).
    std::vector<std::string> tagsOf (const std::string& item) const
    {
        std::vector<std::string> out;
        if (const auto it = items.find (item); it != items.end())
            for (const auto& k : it->second)
                out.push_back (nameOf (k));
        return out;
    }

    // Every tag with the number of items that carry it, A-Z.
    std::vector<TagCount> all() const
    {
        std::map<std::string, int> counts;
        for (const auto& [item, tags] : items)
            for (const auto& k : tags)
                ++counts[k];
        std::vector<TagCount> out;
        for (const auto& [k, name] : names)
            out.push_back ({ name, counts.count (k) ? counts.at (k) : 0 });
        return out;
    }

    // FL's "Edit tags": a new name for a tag on every item (into an existing tag of that name: the two become one).
    // Returns whether anything changed.
    bool rename (const std::string& from, const std::string& to)
    {
        const auto oldKey = key (from), clean = cleanTag (to), newKey = detail::lower (clean);
        if (oldKey.empty() || newKey.empty() || names.count (oldKey) == 0)
            return false;
        if (oldKey == newKey)
        {
            if (names[oldKey] == clean)
                return false;
            names[oldKey] = clean; // (only its case changes)
            return true;
        }
        names.erase (oldKey);
        names.emplace (newKey, clean);
        for (auto& [item, tags] : items)
            if (tags.erase (oldKey) > 0)
                tags.insert (newKey);
        return true;
    }

    // FL's "Edit tags" › delete: the tag goes from the list and from every item. Returns how many items lost it.
    int remove (const std::string& tag)
    {
        const auto k = key (tag);
        names.erase (k);
        int count = 0;
        for (auto it = items.begin(); it != items.end();)
        {
            count += (int) it->second.erase (k);
            if (it->second.empty())
                it = items.erase (it);
            else
                ++it;
        }
        return count;
    }

    // Whether a set of tag keys has `k` or a tag under it ("drums" has "drums/kick": round 190qi's tag tree)
    static bool hasOrBelow (const std::set<std::string>& tags, const std::string& k)
    {
        if (tags.count (k) > 0)
            return true;
        const auto below = k + "/";
        const auto it = tags.lower_bound (below);
        return it != tags.end() && it->compare (0, below.size(), below) == 0;
    }

    // Does the item pass the tag filter? No tags picked: everything passes. A picked tag also takes the items of the
    // tags under it (round 190qi: "Drums" shows what is tagged "Drums/Kick").
    bool matches (const std::string& item, const std::vector<std::string>& picked, Match match) const
    {
        if (picked.empty())
            return true;
        const auto it = items.find (item);
        if (it == items.end())
            return false;
        for (const auto& tag : picked)
        {
            const auto in = hasOrBelow (it->second, key (tag));
            if (match == Match::any && in)
                return true;
            if (match == Match::all && ! in)
                return false;
        }
        return match == Match::all;
    }

    // The items that pass the filter (none picked: none), in item order.
    std::vector<std::string> filter (const std::vector<std::string>& picked, Match match) const
    {
        std::vector<std::string> out;
        if (picked.empty())
            return out;
        for (const auto& [item, tags] : items)
            if (matches (item, picked, match))
                out.push_back (item);
        return out;
    }

    // A file or folder was renamed or moved: its tags go with it (and those of everything below a folder).
    int moved (const std::string& from, const std::string& to)
    {
        if (from.empty() || from == to)
            return 0;
        std::vector<std::pair<std::string, std::set<std::string>>> changed;
        for (auto it = items.begin(); it != items.end();)
        {
            const auto& item = it->first;
            const auto below = item.size() > from.size() && item.compare (0, from.size(), from) == 0
                               && (item[from.size()] == '/' || item[from.size()] == '\\');
            if (item == from || below)
            {
                changed.emplace_back (to + item.substr (from.size()), it->second);
                it = items.erase (it);
            }
            else
                ++it;
        }
        for (auto& [item, tags] : changed)
            items[item].insert (tags.begin(), tags.end());
        return (int) changed.size();
    }

    // --- the settings file ---------------------------------------------------------------------------------------------
    // "tag|<name>" and "item|<names joined by tabs>|<item>", one per line; unknown lines are skipped.
    std::string toText() const
    {
        std::string out = "lynx-browser-tags 1\n";
        for (const auto& [k, name] : names)
            out += "tag|" + name + "\n";
        for (const auto& [item, tags] : items)
        {
            std::string joined;
            for (const auto& k : tags)
                joined += (joined.empty() ? "" : "\t") + nameOf (k);
            out += "item|" + joined + "|" + item + "\n";
        }
        return out;
    }

    static Store fromText (const std::string& text)
    {
        Store s;
        std::istringstream in (text);
        for (std::string line; std::getline (in, line);)
        {
            if (! line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.rfind ("tag|", 0) == 0)
                s.create (line.substr (4));
            else if (line.rfind ("item|", 0) == 0)
            {
                const auto bar = line.find ('|', 5);
                if (bar == std::string::npos || bar + 1 >= line.size())
                    continue;
                const auto item = line.substr (bar + 1);
                std::istringstream tags (line.substr (5, bar - 5));
                for (std::string tag; std::getline (tags, tag, '\t');)
                    s.set (item, tag, true);
            }
        }
        return s;
    }

    bool operator== (const Store&) const = default;
};

// --- round 190qi: "a/b" tags as a tree (the tags panel, the Tags menus) ------------------------------------------------
struct TagNode
{
    std::string name, path; // "Kick", "Drums/Kick"
    bool exists = false;    // a tag of that name exists (a group made only by its children's names does not)
    int items = 0;          // items tagged with it exactly
    std::vector<TagNode> children;
};

// Every tag of `all` (Store::all: A-Z) under its parents; parents nobody made are groups (exists false). Children
// keep the A-Z order (ASCII case ignored).
inline std::vector<TagNode> tagTree (const std::vector<TagCount>& all)
{
    std::vector<TagNode> roots;
    auto childNamed = [] (std::vector<TagNode>& list, const std::string& name, const std::string& path) -> TagNode& {
        for (auto& n : list)
            if (detail::lower (n.name) == detail::lower (name))
                return n;
        const auto lowered = detail::lower (name);
        auto at = std::find_if (list.begin(), list.end(), [&lowered] (const TagNode& n) { return detail::lower (n.name) > lowered; });
        return *list.insert (at, TagNode { name, path });
    };
    for (const auto& t : all)
    {
        auto* level = &roots;
        TagNode* node = nullptr;
        std::string path, part;
        std::istringstream in (t.name);
        while (std::getline (in, part, '/'))
        {
            path += (path.empty() ? "" : "/") + part;
            node = &childNamed (*level, part, path);
            level = &node->children;
        }
        if (node != nullptr)
        {
            node->exists = true;
            node->items = t.items;
            node->name = tagLeaf (t.name); // (its own spelling, not a child's)
        }
    }
    return roots;
}

// --- round 190qi: the search box's Tab -------------------------------------------------------------------------------
// The last word of `query` completed to the shortest candidate that starts with it (ASCII case ignored; equal lengths:
// A-Z) and is longer than it: a "#word" completes to a tag (tags with spaces can't be a "#" word), any other word to
// one of `names` (folder names...) or else a tag. "#" alone takes the first tag. Returns the whole new query; "" =
// nothing to complete.
inline std::string completeQuery (const std::string& query, const std::vector<std::string>& tags, const std::vector<std::string>& names)
{
    if (query.empty() || std::isspace ((unsigned char) query.back()))
        return {};
    const auto start = query.find_last_of (" \t") == std::string::npos ? 0 : query.find_last_of (" \t") + 1;
    const auto word = query.substr (start);
    const auto hashed = word[0] == '#';
    const auto typed = detail::lower (hashed ? word.substr (1) : word);
    if (! hashed && typed.empty())
        return {};
    auto best = [&typed] (const std::vector<std::string>& list, bool noSpaces) {
        std::string found;
        for (const auto& c : list)
        {
            if (c.size() <= typed.size() || (noSpaces && c.find (' ') != std::string::npos))
                continue;
            if (detail::lower (c.substr (0, typed.size())) != typed)
                continue;
            if (found.empty() || c.size() < found.size() || (c.size() == found.size() && detail::lower (c) < detail::lower (found)))
                found = c;
        }
        return found;
    };
    auto found = hashed ? best (tags, true) : best (names, false);
    if (found.empty() && ! hashed)
        found = best (tags, false);
    if (found.empty())
        return {};
    return query.substr (0, start) + (hashed ? "#" : "") + found;
}

// The search box: words starting with '#' pick tags (Live's "#tag"; "#808 kick" = the items tagged 808 whose name has
// "kick"), the rest is searched as before.
struct TagQuery
{
    std::vector<std::string> tags;
    std::string rest;
};

inline TagQuery parseQuery (const std::string& query)
{
    TagQuery q;
    std::istringstream in (query);
    for (std::string word; in >> word;)
    {
        if (word.size() > 1 && word[0] == '#')
            q.tags.push_back (cleanTag (word.substr (1)));
        else
            q.rest += (q.rest.empty() ? "" : " ") + word;
    }
    return q;
}

// Does a name have every word of `text` in it (ASCII case ignored)? Empty text: yes.
inline bool nameHasWords (const std::string& name, const std::string& text)
{
    const auto lowered = detail::lower (name);
    std::istringstream in (detail::lower (text));
    for (std::string w; in >> w;)
        if (lowered.find (w) == std::string::npos)
            return false;
    return true;
}
} // namespace lynx::browsertags
