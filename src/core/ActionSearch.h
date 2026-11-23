#pragma once

// #228 (round 190nq), a LYNX extra: "?" opens a search over every action of the key map (REAPER's Actions list and the
// command palettes of code editors, interpreted; FL Studio 2026 has none — its Gopher assistant is a chat, Alt+F1). Type a
// few letters of what it does, its panel or its key; Enter runs it.
//
// Round 190on (research190oe P2, the command palette): the same box finds the main menus' items, the LYNX extras' switches,
// the Settings pages and the recent projects too (`Kind`), and matches loosely:
//   - case- and accent-insensitive ("edicao" finds "Edição", "ÜBER" finds "über"), in the UI language and in English;
//   - every word of the query somewhere in the item (its name, English name, place, keys or id), or else the query's
//     letters in order in its name ("mtrnm" finds "Metronome");
//   - the order: the query is the initials of the name's words ("pr" = Piano Roll), the name starts with it, a word of
//     the name starts with it, any other match, the loose (letters in order) match; within each, what was run lately
//     first, then the list's own order.
// No JUCE.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/SearchQuery.h"

namespace lynx::actionsearch
{
// what an entry is (the palette groups nothing by it: it only tells the row's tag and how it runs)
enum class Kind : std::uint8_t { action, menu, extra, setting, recent };

struct Entry
{
    std::string id;
    std::string text;    // what it does, in the user's language
    std::string english; // the same in English (found in any language)
    std::string where;   // its panel ("Mixer", "Everywhere"...) or menu path ("File > Export")
    std::string keys;    // its keys as shown ("Ctrl+S Alt+Left")
    Kind kind = Kind::action;
    bool enabled = true;   // a menu item that can't run now: listed greyed
    std::string state;     // a switch's state as shown ("on" / "off"), or ""
};

inline std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// --- folding: UTF-8 text -> lower-case code points without accents ---------------------------------------------------
namespace detail
{
inline char32_t foldOne (char32_t c) noexcept
{
    if (c < 0x80)
        return c >= 'A' && c <= 'Z' ? c + 32 : c;
    // Latin-1 Supplement and Latin Extended-A (U+00C0..U+017F): the base letter in lower case ('*': kept as it is)
    if (c >= 0xC0 && c <= 0x17F)
    {
        static constexpr char table[] = "aaaaaa*ceeeeiiiidnooooo*ouuuuy**aaaaaa*ceeeeiiiidnooooo*ouuuuy*y"
                                        "aaaaaaccccccccddddeeeeeeeeeegggggggghhhhiiiiiiiiii**jjkk*llllllllll"
                                        "nnnnnn***oooooo**rrrrrrsssssssstttt**uuuuuuuuuuuuwwyyyzzzzzz*";
        static_assert (sizeof (table) == 0x180 - 0xC0 + 1);
        const auto b = table[c - 0xC0];
        return b == '*' ? c : (char32_t) b;
    }
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) // Greek capitals
        return c + 32;
    if (c >= 0x410 && c <= 0x42F) // Cyrillic capitals А..Я
        return c + 32;
    if (c >= 0x400 && c <= 0x40F) // Ѐ..Џ
        return c + 80;
    if (c == 0x451 || c == 0x401) // ё / Ё -> е
        return 0x435;
    if (c == 0x130) // İ
        return 'i';
    if (c >= 0xFF21 && c <= 0xFF3A) // full-width Latin capitals -> ASCII
        return c - 0xFF21 + 'a';
    if (c >= 0xFF41 && c <= 0xFF5A)
        return c - 0xFF41 + 'a';
    return c;
}
} // namespace detail

inline std::u32string fold (const std::string& utf8)
{
    std::u32string out;
    out.reserve (utf8.size());
    for (std::size_t i = 0; i < utf8.size();)
    {
        const auto b = (unsigned char) utf8[i];
        char32_t c = b;
        int extra = 0;
        if (b >= 0xF0)
            c = b & 0x07, extra = 3;
        else if (b >= 0xE0)
            c = b & 0x0F, extra = 2;
        else if (b >= 0xC0)
            c = b & 0x1F, extra = 1;
        ++i;
        for (int k = 0; k < extra && i < utf8.size() && ((unsigned char) utf8[i] & 0xC0) == 0x80; ++k, ++i)
            c = (c << 6) | ((unsigned char) utf8[i] & 0x3F);
        if (c == 0x300 || (c > 0x300 && c <= 0x36F)) // combining accents (a decomposed "é"): dropped
            continue;
        out.push_back (detail::foldOne (c));
    }
    return out;
}

inline bool isWordChar (char32_t c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80;
}

// The query (letters and digits only, at least two) is the initials of consecutive words of `name`: "pr" in "Piano
// roll", "nm" in "Select the next mixer track" (n-ext m-ixer).
inline bool initialsMatch (const std::u32string& name, const std::u32string& query)
{
    if (query.size() < 2 || ! std::all_of (query.begin(), query.end(), isWordChar))
        return false;
    std::u32string initials; // the first letter of each word, in order
    for (std::size_t i = 0; i < name.size(); ++i)
        if (isWordChar (name[i]) && (i == 0 || ! isWordChar (name[i - 1])))
            initials.push_back (name[i]);
    return initials.find (query) != std::u32string::npos;
}

// The loose match: the query's letters (spaces left out, at least three) in this order in `name`, where the first one
// starts a word and each next one either follows in the same word or starts a later word ("mtrnm" in "Metronome",
// "nxt trnsnt" in "Song position to the next transient", but not "metro" in "Move the playback marker to the start").
inline bool looseMatch (const std::u32string& name, const std::u32string& query)
{
    std::u32string q;
    for (auto c : query)
        if (c != ' ')
            q.push_back (c);
    if (q.size() < 3 || name.size() > 400)
        return false;
    const auto n = name.size();
    const auto wordStart = [&name] (std::size_t i) { return isWordChar (name[i]) && (i == 0 || ! isWordChar (name[i - 1])); };
    // tried[qi * (n + 1) + i]: q[qi...] can't be matched with the last match just before i (inside its word)
    std::vector<char> tried (q.size() * (n + 1), 0);
    std::function<bool (std::size_t, std::size_t)> rest = [&] (std::size_t qi, std::size_t i) -> bool {
        if (qi == q.size())
            return true;
        auto& seen = tried[qi * (n + 1) + i];
        if (seen)
            return false;
        seen = 1;
        if (qi > 0) // on in the same word
            for (auto j = i; j < n && isWordChar (name[j]); ++j)
                if (name[j] == q[qi] && rest (qi + 1, j + 1))
                    return true;
        for (auto j = i; j < n; ++j) // or the start of a later word
            if (name[j] == q[qi] && wordStart (j) && rest (qi + 1, j + 1))
                return true;
        return false;
    };
    return rest (0, 0);
}

inline std::u32string trimmedFold (const std::string& query)
{
    auto q = fold (query);
    for (auto& c : q)
        if (c == '?' || c == '*') // ("?" and "*" are the Browser's wildcards: here they are plain characters)
            c = ' ';
    while (! q.empty() && q.front() == ' ')
        q.erase (q.begin());
    while (! q.empty() && q.back() == ' ')
        q.pop_back();
    return q;
}

inline bool contains (const std::u32string& text, const std::u32string& word) { return text.find (word) != std::u32string::npos; }

// The rank of `e` for `query` (lower = better), or -1 when it doesn't match:
// 0 initials, 1 the name starts with it, 2 a word of the name starts with its first word, 3 any other match (every
// word somewhere), 4 the loose match (letters in order).
inline int rankOf (const Entry& e, const std::string& query)
{
    const auto q = trimmedFold (query);
    if (q.empty())
        return 3;
    const auto text = fold (e.text), english = fold (e.english);
    std::vector<std::u32string> words;
    for (std::size_t at = 0; at < q.size();)
    {
        const auto end = std::min (q.find (U' ', at), q.size());
        if (end > at)
            words.push_back (q.substr (at, end - at));
        at = end + 1;
    }
    const auto all = text + U" | " + english + U" | " + fold (e.where) + U" | " + fold (e.keys) + U" | " + fold (e.id) + U" | " + fold (e.state);
    const auto everyWord = std::all_of (words.begin(), words.end(), [&all] (const auto& w) { return contains (all, w); });
    if (everyWord)
    {
        for (const auto* name : { &text, &english })
            if (initialsMatch (*name, q))
                return 0;
        for (const auto* name : { &text, &english })
            if (name->compare (0, q.size(), q) == 0)
                return 1;
        for (const auto* name : { &text, &english })
            for (auto at = name->find (words.front()); at != std::u32string::npos; at = name->find (words.front(), at + 1))
                if (at == 0 || ! isWordChar ((*name)[at - 1]))
                    return 2;
        return 3;
    }
    for (const auto* name : { &text, &english })
        if (initialsMatch (*name, q))
            return 0;
    for (const auto* name : { &text, &english })
        if (looseMatch (*name, q))
            return 4;
    return -1;
}

// The entries that match `query`, best first. `recent`: ids run lately, the latest first (they go first within a rank,
// and first of all when the query is empty).
inline std::vector<int> find (const std::vector<Entry>& entries, const std::string& query, const std::vector<std::string>& recent = {})
{
    struct Hit
    {
        int index, rank, recency;
    };
    std::vector<Hit> hits;
    const auto recencyOf = [&recent] (const std::string& id) {
        const auto it = std::find (recent.begin(), recent.end(), id);
        return it == recent.end() ? (int) recent.size() : (int) (it - recent.begin());
    };
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(std::size_t) i];
        if (const auto r = rankOf (e, query); r >= 0)
            hits.push_back ({ i, r, recent.empty() ? 0 : recencyOf (e.id) });
    }
    std::stable_sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) {
        return a.rank != b.rank ? a.rank < b.rank : a.recency < b.recency;
    });
    std::vector<int> out;
    out.reserve (hits.size());
    for (const auto& h : hits)
        out.push_back (h.index);
    return out;
}

// The recent list after `id` ran: it goes first, at most `keep` ids.
inline void noteRun (std::vector<std::string>& recent, const std::string& id, std::size_t keep = 20)
{
    recent.erase (std::remove (recent.begin(), recent.end(), id), recent.end());
    recent.insert (recent.begin(), id);
    if (recent.size() > keep)
        recent.resize (keep);
}
} // namespace lynx::actionsearch
