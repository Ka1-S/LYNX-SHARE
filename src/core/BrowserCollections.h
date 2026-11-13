#pragma once

// The Browser's collections (round 190ce; owner decision L of round 190bk: "several collections, Starred is the first").
//
// The reference DAW's browser gives every item up to seven colour labels at once and lists each colour as a
// "collection" [docs/analise/REA_rodada190ce.md §1: its library settings hold seven label entries "colors=1..7", the
// first named and switched on, the other six unnamed and switched off; its file index keeps the labels of an item as
// one bit mask]. LYNX keeps FL's single Starred flag as collection 1 and adds the other six:
//   - an item (a file, a folder, a Plugin database entry) carries any of the seven, several at once;
//   - a collection has a name of the user's (empty: "Starred" for the first, the colour's name for the others) and is
//     shown in the STARRED tab or not; only Starred is shown at first, so the tab looks as it always did;
//   - the search also finds items by the name of a collection they are in.
// Stored as text in the settings folder (browser-collections.txt); the older browser-starred.txt (one path per line)
// is read once when the new file does not exist and is still written, so an older LYNX keeps its stars.
// No JUCE: tested in isolation (tests/LiveBrowser190ceTests.inc).

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace lynx::collections
{
constexpr int count = 7;

// The colours in the order of the number keys 1..7.
inline const char* colourName (int collection) noexcept
{
    static constexpr const char* names[count] = { "Red", "Orange", "Yellow", "Green", "Blue", "Purple", "Gray" };
    return collection >= 0 && collection < count ? names[collection] : "";
}

inline std::uint32_t colourArgb (int collection) noexcept
{
    static constexpr std::uint32_t colours[count] = { 0xFFE5484D, 0xFFF08A2E, 0xFFE8C53A, 0xFF4CB96B, 0xFF4A90E2, 0xFF9B6DDB, 0xFF9AA0A6 };
    return collection >= 0 && collection < count ? colours[collection] : 0;
}

// A Plugin database entry is stored under its plugin key with this prefix; everything else is a file or folder path.
inline constexpr const char* pluginPrefix = "plugin:";
inline std::string pluginItem (const std::string& pluginKey) { return pluginPrefix + pluginKey; }
inline bool isPluginItem (const std::string& item) noexcept { return item.rfind (pluginPrefix, 0) == 0; }
inline std::string pluginKeyOf (const std::string& item) { return isPluginItem (item) ? item.substr (std::char_traits<char>::length (pluginPrefix)) : std::string(); }

struct Label
{
    std::string name; // the user's name; empty = the default
    bool shown = false;
    bool operator== (const Label&) const = default;
};

namespace detail
{
inline std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}
inline std::string baseName (const std::string& item)
{
    if (isPluginItem (item))
        return pluginKeyOf (item);
    const auto cut = item.find_last_of ("/\\");
    return cut == std::string::npos ? item : item.substr (cut + 1);
}
} // namespace detail

struct Store
{
    std::array<Label, count> labels;
    std::map<std::string, std::uint8_t> items; // item -> bit mask of its collections (bit 0 = Starred); never 0

    Store() { labels[0].shown = true; }

    static bool valid (int collection) noexcept { return collection >= 0 && collection < count; }

    // The English name shown for a collection: the user's, else "Starred" / the colour.
    std::string name (int collection) const
    {
        if (! valid (collection))
            return {};
        if (! labels[(std::size_t) collection].name.empty())
            return labels[(std::size_t) collection].name;
        return collection == 0 ? "Starred" : colourName (collection);
    }
    bool hasOwnName (int collection) const { return valid (collection) && ! labels[(std::size_t) collection].name.empty(); }

    void rename (int collection, const std::string& name)
    {
        if (! valid (collection))
            return;
        auto clean = name;
        std::replace (clean.begin(), clean.end(), '\n', ' ');
        std::replace (clean.begin(), clean.end(), '\r', ' ');
        std::replace (clean.begin(), clean.end(), '|', ' ');
        const auto first = clean.find_first_not_of (' ');
        const auto last = clean.find_last_not_of (' ');
        labels[(std::size_t) collection].name = first == std::string::npos ? std::string() : clean.substr (first, last - first + 1);
    }
    void setShown (int collection, bool shown)
    {
        if (valid (collection))
            labels[(std::size_t) collection].shown = shown;
    }
    bool isShown (int collection) const { return valid (collection) && labels[(std::size_t) collection].shown; }
    std::vector<int> shown() const
    {
        std::vector<int> out;
        for (int c = 0; c < count; ++c)
            if (labels[(std::size_t) c].shown)
                out.push_back (c);
        return out;
    }

    std::uint8_t mask (const std::string& item) const
    {
        const auto it = items.find (item);
        return it != items.end() ? it->second : (std::uint8_t) 0;
    }
    bool has (const std::string& item, int collection) const { return valid (collection) && (mask (item) >> collection & 1) != 0; }

    // Puts the item into the collection or takes it out. Returns whether anything changed.
    bool set (const std::string& item, int collection, bool on)
    {
        if (! valid (collection) || item.empty() || item.find ('\n') != std::string::npos)
            return false;
        const auto before = mask (item);
        const auto after = (std::uint8_t) (on ? before | (1u << collection) : before & ~(1u << collection));
        if (after == before)
            return false;
        if (after == 0)
            items.erase (item);
        else
            items[item] = after;
        return true;
    }
    // set() as the number keys and the item menu do it: an item put into a hidden collection shows that collection
    // (round 190cy, Live 12.4.16 live check 190cw: the key of a hidden colour labels the item and the collection
    // appears in the sidebar). Returns whether anything changed.
    bool label (const std::string& item, int collection, bool on)
    {
        const auto changed = set (item, collection, on);
        if (on && has (item, collection) && ! isShown (collection))
        {
            setShown (collection, true);
            return true;
        }
        return changed;
    }
    // The number keys: in if it was out, out if it was in. Returns the new state.
    bool toggle (const std::string& item, int collection)
    {
        const auto on = ! has (item, collection);
        label (item, collection, on);
        return has (item, collection);
    }
    // Key 0: out of every collection. Returns whether it was in any.
    bool clear (const std::string& item) { return items.erase (item) > 0; }

    // The items of a collection, by item text.
    std::vector<std::string> itemsOf (int collection) const
    {
        std::vector<std::string> out;
        if (valid (collection))
            for (const auto& [item, bits] : items)
                if ((bits >> collection & 1) != 0)
                    out.push_back (item);
        return out;
    }
    int countOf (int collection) const { return (int) itemsOf (collection).size(); }

    // A file or folder was renamed or moved: its labels go with it (and those of everything below a folder).
    int moved (const std::string& from, const std::string& to)
    {
        if (from.empty() || from == to)
            return 0;
        std::vector<std::pair<std::string, std::uint8_t>> changed;
        for (auto it = items.begin(); it != items.end();)
        {
            const auto& item = it->first;
            const auto below = item.size() > from.size() && item.compare (0, from.size(), from) == 0 && (item[from.size()] == '/' || item[from.size()] == '\\');
            if (item == from || below)
            {
                changed.emplace_back (to + item.substr (from.size()), it->second);
                it = items.erase (it);
            }
            else
                ++it;
        }
        for (const auto& [item, bits] : changed)
            items[item] = (std::uint8_t) (items[item] | bits);
        return (int) changed.size();
    }

    // The search: the items found through a collection's name. Every word of `query` (split at spaces, ASCII case
    // ignored) must be in the item's own name or in the name of one of its collections, and at least one word must be
    // in a collection's name (items that only match by their own name are the file search's business). `names`: the
    // names as the user sees them (translated defaults); nullptr = the English ones.
    std::vector<std::string> search (const std::string& query, const std::array<std::string, count>* names = nullptr) const
    {
        std::vector<std::string> words;
        {
            std::istringstream in (detail::lower (query));
            for (std::string w; in >> w;)
                words.push_back (w);
        }
        std::vector<std::string> out;
        if (words.empty())
            return out;
        std::array<std::string, count> lowered;
        for (int c = 0; c < count; ++c)
            lowered[(std::size_t) c] = detail::lower (names != nullptr ? (*names)[(std::size_t) c] : name (c));
        for (const auto& [item, bits] : items)
        {
            const auto own = detail::lower (detail::baseName (item));
            bool all = true, byLabel = false;
            for (const auto& w : words)
            {
                bool inLabel = false;
                for (int c = 0; c < count; ++c)
                    inLabel = inLabel || ((bits >> c & 1) != 0 && lowered[(std::size_t) c].find (w) != std::string::npos);
                byLabel = byLabel || inLabel;
                all = all && (inLabel || own.find (w) != std::string::npos);
            }
            if (all && byLabel)
                out.push_back (item);
        }
        return out;
    }

    // --- the settings file ---------------------------------------------------------------------------------------------
    // "label|<1..7>|<shown 0/1>|<name>" and "item|<mask 1..127>|<item>", one per line; unknown lines are skipped.
    std::string toText() const
    {
        std::string out = "lynx-browser-collections 1\n";
        for (int c = 0; c < count; ++c)
            out += "label|" + std::to_string (c + 1) + "|" + (labels[(std::size_t) c].shown ? "1" : "0") + "|" + labels[(std::size_t) c].name + "\n";
        for (const auto& [item, bits] : items)
            out += "item|" + std::to_string ((int) bits) + "|" + item + "\n";
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
            const auto a = line.find ('|');
            if (a == std::string::npos)
                continue;
            const auto kind = line.substr (0, a);
            const auto b = line.find ('|', a + 1);
            if (b == std::string::npos)
                continue;
            int number = 0;
            try
            {
                number = std::stoi (line.substr (a + 1, b - a - 1));
            }
            catch (...)
            {
                continue;
            }
            if (kind == "label" && number >= 1 && number <= count)
            {
                const auto c = line.find ('|', b + 1);
                if (c == std::string::npos)
                    continue;
                s.labels[(std::size_t) (number - 1)].shown = line.substr (b + 1, c - b - 1) == "1";
                s.rename (number - 1, line.substr (c + 1));
            }
            else if (kind == "item" && number >= 1 && number < (1 << count) && b + 1 < line.size())
                s.items[line.substr (b + 1)] = (std::uint8_t) number;
        }
        return s;
    }

    // The older file: one starred path per line.
    static Store fromStarred (const std::vector<std::string>& paths)
    {
        Store s;
        for (const auto& p : paths)
            s.set (p, 0, true);
        return s;
    }
    // What goes on being written to the older file: the starred files and folders (no Plugin database entries).
    std::vector<std::string> starredPaths() const
    {
        std::vector<std::string> out;
        for (const auto& item : itemsOf (0))
            if (! isPluginItem (item))
                out.push_back (item);
        return out;
    }

    bool operator== (const Store&) const = default;
};
} // namespace lynx::collections
