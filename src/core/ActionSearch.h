#pragma once

// #228 (round 190nq), a LYNX extra: "?" opens a search over every action of the key map (REAPER's Actions list and the
// command palettes of code editors, interpreted; FL Studio 2026 has none — its Gopher assistant is a chat, Alt+F1). Type a
// few letters of what it does, its panel or its key; Enter runs it. The order: names that start with the text, then names
// with a word that starts with it, then the rest that match, each in the key map's own order. No JUCE.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "core/SearchQuery.h"

namespace lynx::actionsearch
{
struct Entry
{
    std::string id;
    std::string text;    // what it does, in the user's language
    std::string english; // the same in English (found in any language)
    std::string where;   // its panel ("Mixer", "Everywhere"...)
    std::string keys;    // its keys as shown ("Ctrl+S Alt+Left")
};

inline std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// 0: the name starts with the query; 1: a word of the name starts with its first word; 2: any other match.
inline int rankOf (const Entry& e, const std::string& query)
{
    const auto q = lower (query);
    const auto first = q.substr (0, q.find (' '));
    for (const auto* name : { &e.text, &e.english })
        if (lower (*name).rfind (q, 0) == 0)
            return 0;
    for (const auto* name : { &e.text, &e.english })
    {
        const auto n = lower (*name);
        for (std::size_t at = n.find (first); at != std::string::npos; at = n.find (first, at + 1))
            if (at == 0 || ! std::isalnum ((unsigned char) n[at - 1]))
                return 1;
    }
    return 2;
}

// The entries that match `query` (every word, in the name, the English name, the panel, the keys or the id), best first.
inline std::vector<int> find (const std::vector<Entry>& entries, const std::string& query)
{
    std::vector<int> out;
    auto trimmed = query;
    trimmed.erase (0, trimmed.find_first_not_of (' '));
    trimmed.erase (trimmed.find_last_not_of (' ') + 1);
    // ("?" and "*" are the Browser's wildcards: here they are plain characters)
    std::string plain;
    for (auto c : trimmed)
        plain += (c == '?' || c == '*') ? ' ' : c;
    const auto parsed = search::parse (plain);
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(std::size_t) i];
        if (search::matches (parsed, e.text + " | " + e.english + " | " + e.where + " | " + e.keys + " | " + e.id))
            out.push_back (i);
    }
    if (! plain.empty())
        std::stable_sort (out.begin(), out.end(), [&] (int a, int b) { return rankOf (entries[(std::size_t) a], trimmed) < rankOf (entries[(std::size_t) b], trimmed); });
    return out;
}
} // namespace lynx::actionsearch
