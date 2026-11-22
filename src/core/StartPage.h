#pragma once

// Issue #252 (round 190oq): the start page — recent projects and templates on one page when LYNX starts. FL Studio 2026
// has one: its "Welcome to FL Studio" window (TWelcomeWizard, REA_rodada190oq §6) lists "Recent projects" (up to 50, the
// file name, its folder and how long ago it changed), demo projects, "Other projects..." and "Start a new ... project",
// and shows at every start without a file until "Do not show this in the future" is ticked; Bitwig (Dashboard) and Studio
// One (Start page) have theirs, Live 12 has none. LYNX's page is drawn in LYNX's own look with FL's contents. On by
// default, as FL's window (owner decision, rollup 0.1.31 step 6); it shows over the start-up project,
// which stays open behind it (Esc or × keeps it). The first run keeps its welcome card (round 190np) and never shows the
// page in the same start. Also File › Start page... at any time.
//
// The logic here has no JUCE (tests/Issues190oqTests.inc); ui/StartPage.h draws the page.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/SearchQuery.h"

namespace lynx::startpage
{
inline std::atomic<bool>& enabledFlag() noexcept
{
    static std::atomic<bool> on { true }; // (FL-exact: FL shows its Welcome window at every start)
    return on;
}
inline bool enabled() noexcept { return enabledFlag().load (std::memory_order_relaxed); }
inline void setEnabled (bool on) noexcept { enabledFlag().store (on, std::memory_order_relaxed); }

// Whether the page opens at this start: switched on, no file was given to open (a double-clicked project opens as
// before), the first run's welcome card / old setup wizard is not showing, and not a command-line or test mode run.
inline bool showAtStart (bool on, bool fileGiven, bool firstRunCard, bool commandLineMode) noexcept
{
    return on && ! fileGiven && ! firstRunCard && ! commandLineMode;
}

struct Recent
{
    std::string path;   // as File › Open recent keeps it
    std::string name;   // the file's name without its extension
    std::string folder; // the folder it is in
    std::int64_t modifiedMs = 0; // last change (ms since 1970), 0 = unknown
    bool exists = true;
    std::int64_t bytes = 0; // the file's size (FL's row: "179 KB  <date>  (<ago>)")
};

// Lower-case ASCII (the Browser's search is ASCII case-insensitive too; the UI lower-cases other scripts itself)
inline std::string lowered (std::string s)
{
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c = (char) (c - 'A' + 'a');
    return s;
}

// A search lowered for matching, the Browser's OR / AND / NOT kept as they are (they are words of the query language)
inline std::string loweredQuery (const std::string& query)
{
    std::string out, word;
    const auto flush = [&] {
        out += (word == "OR" || word == "AND" || word == "NOT") ? word : lowered (word);
        word.clear();
    };
    for (const auto c : query)
    {
        if (c == ' ')
        {
            flush();
            out += c;
        }
        else
            word += c;
    }
    flush();
    return out;
}

// The entries a search keeps, in their order: the Browser's search syntax (words in any order, OR, -word, "a phrase")
// over the name and the folder; an empty search keeps them all. Missing files stay listed (dimmed) unless `hideMissing`.
inline std::vector<int> filter (const std::vector<Recent>& recent, const std::string& query, bool hideMissing = false)
{
    std::vector<int> out;
    const auto q = loweredQuery (query);
    for (int i = 0; i < (int) recent.size(); ++i)
    {
        const auto& r = recent[(std::size_t) i];
        if (hideMissing && ! r.exists)
            continue;
        if (q.find_first_not_of (' ') == std::string::npos || search::matches (q, lowered (r.name + " " + r.folder)))
            out.push_back (i);
    }
    return out;
}

// How long ago a project changed, in FL's Welcome window's words (REA_rodada190oq §6, FUN_01148c60): from a day on
// "%d days ago" (truncated; never a date), else "%dh %dm ago" with hours, else "%dm ago", else "seconds ago".
struct Ago
{
    enum class Kind { unknown, seconds, minutes, hours, days };
    Kind kind = Kind::unknown;
    int days = 0, hours = 0, minutes = 0;
};
inline Ago ago (std::int64_t nowMs, std::int64_t thenMs) noexcept
{
    Ago a;
    if (thenMs <= 0)
        return a;
    const auto d = nowMs > thenMs ? nowMs - thenMs : 0;
    const auto totalMinutes = d / 60000;
    a.days = (int) (d / (24LL * 3600LL * 1000LL));
    a.hours = (int) (totalMinutes / 60 % 24);
    a.minutes = (int) (totalMinutes % 60);
    a.kind = a.days >= 1 ? Ago::Kind::days : a.hours > 0 ? Ago::Kind::hours : a.minutes > 0 ? Ago::Kind::minutes : Ago::Kind::seconds;
    return a;
}

// FL's size text ("179 KB": KB from 1 KB up, MB from 1 MB, one decimal under 10 MB)
inline std::string sizeText (std::int64_t bytes)
{
    if (bytes < 1024)
        return std::to_string (std::max<std::int64_t> (0, bytes)) + " B";
    if (bytes < 1024 * 1024)
        return std::to_string (bytes / 1024) + " KB";
    const auto mb = (double) bytes / (1024.0 * 1024.0);
    if (mb < 10.0)
    {
        const auto tenths = (int) (mb * 10.0 + 0.5);
        return std::to_string (tenths / 10) + "." + std::to_string (tenths % 10) + " MB";
    }
    return std::to_string ((std::int64_t) (mb + 0.5)) + " MB";
}

// The row the keyboard moves to: Up / Down by one, Page Up / Down by `page`, kept inside 0..count-1 (-1: none)
inline int moveRow (int row, int delta, int count) noexcept
{
    if (count <= 0)
        return -1;
    if (row < 0)
        return delta >= 0 ? 0 : count - 1;
    const auto to = row + delta;
    return to < 0 ? 0 : to >= count ? count - 1 : to;
}
} // namespace lynx::startpage
