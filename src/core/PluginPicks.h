#pragma once

// Round 190qi (TOP-NEXT R6, DETAILS-2 D2-BR1..BR3): the plugin lists' quality of life, LYNX-own extras (FL Studio 2026
// has none of them; Studio One's Plug-in Manager and Live users asked for them):
//   - Recent: the last plugins the user picked (8, newest first), shown at the top of the Quick-add palette, the Plugin
//     picker ("Recent" beside Favourites) and the empty mixer slot's menu. Kept per user (plugin-recent.txt).
//   - Hidden: plugins the user hid in the Plugin manager leave the pickers; projects still load them, and the Plugin
//     manager's "Hidden" filter shows them again (plugin-hidden.txt, the favourites' format).
//   - One row per plugin installed in several formats: VST2 / VST3 / CLAP builds of one plugin (same name and vendor,
//     same kind) are one row whose chip says the format a new pick loads, CLAP › VST3 › VST2; a right-click picks
//     another. A project keeps the exact format it holds (the plugin's identifier): only new picks are concerned.
// No JUCE here (tests/Picker190qiTests.inc).

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace lynx::pluginpicks
{
constexpr int maxRecent = 8;

// `key` becomes the newest; an older copy of it goes, and the list keeps at most maxRecent keys.
inline std::vector<std::string> pushRecent (std::vector<std::string> list, const std::string& key)
{
    if (key.empty() || key.find ('\n') != std::string::npos)
        return list;
    list.erase (std::remove (list.begin(), list.end(), key), list.end());
    list.insert (list.begin(), key);
    if ((int) list.size() > maxRecent)
        list.resize ((std::size_t) maxRecent);
    return list;
}

// One key per line, newest first (empty lines and repeats skipped, at most maxRecent).
inline std::vector<std::string> parseRecent (const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in (text);
    for (std::string line; std::getline (in, line);)
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || std::find (out.begin(), out.end(), line) != out.end())
            continue;
        out.push_back (line);
        if ((int) out.size() == maxRecent)
            break;
    }
    return out;
}

inline std::string formatRecent (const std::vector<std::string>& keys)
{
    std::string out;
    for (const auto& k : keys)
        out += k + "\n";
    return out;
}

// The preference among the formats of one plugin: CLAP (0) › VST3 (1) › VST2 (JUCE's "VST", 2) › anything else (3).
inline int formatRank (const std::string& format)
{
    std::string f;
    for (const auto c : format)
        f += (char) std::toupper ((unsigned char) c);
    if (f == "CLAP")
        return 0;
    if (f == "VST3")
        return 1;
    if (f == "VST" || f == "VST2")
        return 2;
    return 3;
}

// What makes two builds the same plugin: the name and vendor in lower-case letters and digits only ("Pro-Q 3" =
// "ProQ3"; a VST2 build's "x64" suffix dropped), and the kind.
inline std::string groupKey (const std::string& name, const std::string& vendor, bool instrument)
{
    auto squash = [] (const std::string& s) {
        std::string out;
        for (const auto c : s)
            if (std::isalnum ((unsigned char) c))
                out += (char) std::tolower ((unsigned char) c);
        return out;
    };
    auto n = squash (name);
    if (n.size() > 3 && n.compare (n.size() - 3, 3, "x64") == 0)
        n.resize (n.size() - 3);
    return (instrument ? "i|" : "e|") + n + "|" + squash (vendor);
}

struct Item
{
    std::string name, vendor, format;
    bool instrument = false;
};

// The rows: each group lists its members (indices into `items`) by format preference, then by index; the groups in
// the order of their first member in `items`. Items whose format is not VST / VST3 / CLAP (built-in entries, LV2,
// AU) stay rows of their own.
struct Group
{
    std::vector<int> members;
    int preferred() const noexcept { return members.empty() ? -1 : members.front(); }
};

inline std::vector<Group> groupByFormat (const std::vector<Item>& items)
{
    std::vector<Group> groups;
    std::map<std::string, std::size_t> byKey;
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto& it = items[(std::size_t) i];
        if (formatRank (it.format) > 2 || it.name.empty())
        {
            groups.push_back ({ { i } });
            continue;
        }
        const auto key = groupKey (it.name, it.vendor, it.instrument);
        const auto found = byKey.find (key);
        if (found == byKey.end())
        {
            byKey.emplace (key, groups.size());
            groups.push_back ({ { i } });
            continue;
        }
        auto& members = groups[found->second].members;
        // one build per format (a second VST3 of the same name, e.g. in two folders, stays a row of its own)
        if (std::any_of (members.begin(), members.end(), [&] (int m) { return formatRank (items[(std::size_t) m].format) == formatRank (it.format); }))
        {
            groups.push_back ({ { i } });
            continue;
        }
        members.push_back (i);
        std::stable_sort (members.begin(), members.end(),
                          [&] (int a, int b) { return formatRank (items[(std::size_t) a].format) < formatRank (items[(std::size_t) b].format); });
    }
    return groups;
}
} // namespace lynx::pluginpicks
