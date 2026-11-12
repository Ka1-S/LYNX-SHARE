#pragma once

// The project's Lyrics / Notes (round 190ch, LYNX's own; the panel is ui/LyricsPanel.h). The text IS the project's
// Comments (Song::info.comments: FL's Settings › Info page, kept in .lynx, written to and read from .flp), so there is
// one store and every older project already has its notes in the panel. It stays plain text; two things in it are read:
//
//   [Chorus]            a line that is only a name in square brackets is a section heading
//   [9] or [9.3] ...    a line may start with a position, bar or bar.beat (beats are quarter notes, both from 1): the
//                       line belongs to that place of the song, and a click on it moves the playhead there
//
// A heading without a position jumps to the Playlist marker of the same name, when there is one. "Marker at playhead"
// gives the caret's line the playhead's position and puts a Playlist marker there, named after the line. No JUCE:
// tested in tests/BandlabExtras190chTests.inc.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Song.h"

namespace lynx::lyrics
{
struct Line
{
    std::size_t begin = 0, end = 0; // the line's bytes in the text, [begin, end), without its line break
    bool stamped = false;           // it starts with a position
    int bar = 1, beat = 1;          // the position (both from 1)
    std::size_t bodyBegin = 0;      // where the line's own text starts (after the position and the blanks behind it)
    bool section = false;           // a heading: the body is "[Name]"
    std::string title;              // the heading's name
    std::string body;               // the line without its position
};

namespace detail
{
inline bool blank (char c) noexcept { return c == ' ' || c == '\t'; }
inline bool digit (char c) noexcept { return c >= '0' && c <= '9'; }
inline std::string_view trimmed (std::string_view s) noexcept
{
    while (! s.empty() && (blank (s.front()) || s.front() == '\r'))
        s.remove_prefix (1);
    while (! s.empty() && (blank (s.back()) || s.back() == '\r'))
        s.remove_suffix (1);
    return s;
}
inline char lower (char c) noexcept { return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c; }
inline bool sameText (std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && std::equal (a.begin(), a.end(), b.begin(), [] (char x, char y) { return lower (x) == lower (y); });
}

// "[12]" / "[12.3]" at the start of `s` (blanks before it allowed): its length with the blanks behind it, or 0
inline std::size_t readStamp (std::string_view s, int& bar, int& beat) noexcept
{
    std::size_t i = 0;
    while (i < s.size() && blank (s[i]))
        ++i;
    if (i >= s.size() || s[i] != '[')
        return 0;
    ++i;
    const auto numberAt = [&s] (std::size_t& at, int& value) {
        const auto from = at;
        long v = 0;
        while (at < s.size() && digit (s[at]) && at - from < 6)
            v = v * 10 + (s[at++] - '0');
        value = (int) v;
        return at > from;
    };
    int b = 0, q = 1;
    if (! numberAt (i, b))
        return 0;
    if (i < s.size() && s[i] == '.')
    {
        ++i;
        if (! numberAt (i, q))
            return 0;
    }
    if (i >= s.size() || s[i] != ']' || b < 1 || q < 1)
        return 0;
    ++i;
    while (i < s.size() && blank (s[i]))
        ++i;
    bar = b;
    beat = q;
    return i;
}
} // namespace detail

// The text as lines (a text without a line break is one line; an empty text has one empty line).
inline std::vector<Line> parse (std::string_view text)
{
    std::vector<Line> lines;
    std::size_t at = 0;
    for (;;)
    {
        const auto nl = text.find ('\n', at);
        Line line;
        line.begin = at;
        line.end = nl == std::string_view::npos ? text.size() : nl;
        const auto whole = text.substr (line.begin, line.end - line.begin);
        const auto stamp = detail::readStamp (whole, line.bar, line.beat);
        line.stamped = stamp > 0;
        line.bodyBegin = line.begin + stamp;
        const auto body = detail::trimmed (whole.substr (stamp));
        line.body = std::string (body);
        if (body.size() >= 3 && body.front() == '[' && body.back() == ']' && body.find (']') == body.size() - 1)
        {
            const auto name = detail::trimmed (body.substr (1, body.size() - 2));
            int b = 0, q = 0;
            if (! name.empty() && detail::readStamp (body, b, q) == 0) // ("[12]" alone is a position, not a heading)
            {
                line.section = true;
                line.title = std::string (name);
            }
        }
        lines.push_back (std::move (line));
        if (nl == std::string_view::npos)
            break;
        at = nl + 1;
    }
    return lines;
}

// The line that holds byte `offset` of the text.
inline int lineAt (const std::vector<Line>& lines, std::size_t offset) noexcept
{
    for (int i = (int) lines.size() - 1; i >= 0; --i)
        if (offset >= lines[(std::size_t) i].begin)
            return i;
    return 0;
}

// --- positions ------------------------------------------------------------------------------------------------------
// Where bar `bar`, beat `beat` (quarter notes, both from 1) is in the song, time signature changes included. A beat past
// the bar's end stays inside the bar.
inline std::int64_t tickOf (const Song& song, int bar, int beat)
{
    bar = std::clamp (bar, 1, 100000);
    auto b = song.barAt (0);
    for (int guard = 0; b.number < bar && b.length > 0 && guard < 100000; ++guard)
        b = song.barAt (b.tick + b.length);
    const auto inBar = (std::int64_t) (std::max (1, beat) - 1) * ppq;
    return b.tick + std::min (inBar, std::max<std::int64_t> (0, b.length - 1));
}

// The bar and beat of a tick (the beat it is in).
inline std::pair<int, int> barBeatOf (const Song& song, std::int64_t tick)
{
    const auto b = song.barAt (std::max<std::int64_t> (0, tick));
    return { b.number, (int) ((std::max<std::int64_t> (0, tick) - b.tick) / ppq) + 1 };
}

// "[9]" on the first beat of a bar, else "[9.3]"
inline std::string stampText (int bar, int beat)
{
    char text[32];
    if (beat <= 1)
        std::snprintf (text, sizeof (text), "[%d]", bar);
    else
        std::snprintf (text, sizeof (text), "[%d.%d]", bar, beat);
    return text;
}

// The name a line gives its marker: the heading's name, else the line's words (at most 40 bytes, cut at a blank and
// never inside a UTF-8 character), else "Line N".
inline std::string markerName (const Line& line, int index)
{
    if (line.section)
        return line.title;
    auto name = line.body;
    if (name.size() > 40)
    {
        auto cut = name.rfind (' ', 40);
        if (cut == std::string::npos || cut < 12)
        {
            cut = 40;
            while (cut > 0 && ((unsigned char) name[cut] & 0xC0) == 0x80)
                --cut;
        }
        name.resize (cut);
    }
    if (name.empty())
        name = "Line " + std::to_string (index + 1);
    return name;
}

// Where a click on the line goes: its own position; a heading without one, the Playlist marker of the same name.
inline std::optional<std::int64_t> jumpTick (const Song& song, const Line& line)
{
    if (line.stamped)
        return tickOf (song, line.bar, line.beat);
    if (line.section)
        for (const auto& m : song.markers())
            if (! m.isSignature() && detail::sameText (m.name, line.title))
                return m.tick;
    return std::nullopt;
}

// The line that is playing at `tick`: the last one (in song order) whose place is at or before it; −1 before the first.
inline int currentLine (const Song& song, const std::vector<Line>& lines, std::int64_t tick)
{
    int best = -1;
    std::int64_t bestTick = -1;
    for (int i = 0; i < (int) lines.size(); ++i)
        if (const auto at = jumpTick (song, lines[(std::size_t) i]); at && *at <= tick && *at >= bestTick)
        {
            best = i;
            bestTick = *at;
        }
    return best;
}

// --- edits (each returns the new text) ---------------------------------------------------------------------------------
inline std::string withStamp (const std::string& text, int lineIndex, int bar, int beat)
{
    const auto lines = parse (text);
    if (lineIndex < 0 || lineIndex >= (int) lines.size())
        return text;
    const auto& line = lines[(std::size_t) lineIndex];
    auto out = text.substr (0, line.begin) + stampText (bar, beat);
    const auto rest = text.substr (line.bodyBegin, line.end - line.bodyBegin);
    if (! rest.empty())
        out += " " + rest;
    return out + text.substr (line.end);
}

inline std::string withoutStamp (const std::string& text, int lineIndex)
{
    const auto lines = parse (text);
    if (lineIndex < 0 || lineIndex >= (int) lines.size() || ! lines[(std::size_t) lineIndex].stamped)
        return text;
    const auto& line = lines[(std::size_t) lineIndex];
    return text.substr (0, line.begin) + text.substr (line.bodyBegin);
}

// "Marker at playhead": line `lineIndex` of the project's notes takes the position of `tick` (the beat it is in) and a
// Playlist marker named after the line is put there. A plain marker already at that tick is kept as it is (one place,
// one marker). False when the line does not exist.
inline bool markAtPlayhead (Song& song, int lineIndex, std::int64_t tick)
{
    const auto lines = parse (song.info.comments);
    if (lineIndex < 0 || lineIndex >= (int) lines.size())
        return false;
    const auto [bar, beat] = barBeatOf (song, tick);
    const auto at = tickOf (song, bar, beat);
    const auto name = markerName (lines[(std::size_t) lineIndex], lineIndex);
    song.info.comments = withStamp (song.info.comments, lineIndex, bar, beat);
    const auto& markers = song.markers();
    const auto taken = std::any_of (markers.begin(), markers.end(), [at] (const Marker& m) { return m.tick == at && ! m.isSignature() && m.action == Marker::none; });
    if (! taken)
    {
        Marker m;
        m.tick = at;
        m.name = name;
        song.addMarker (m);
    }
    return true;
}

// --- search -------------------------------------------------------------------------------------------------------------
// Every place `query` is in the text, as byte ranges [first, second), in order and not overlapping. Letters A–Z match
// in either case; everything else must be the same bytes.
inline std::vector<std::pair<std::size_t, std::size_t>> find (std::string_view text, std::string_view query)
{
    std::vector<std::pair<std::size_t, std::size_t>> hits;
    if (query.empty() || query.size() > text.size())
        return hits;
    for (std::size_t i = 0; i + query.size() <= text.size();)
    {
        if (detail::sameText (text.substr (i, query.size()), query))
        {
            hits.emplace_back (i, i + query.size());
            i += query.size();
        }
        else
            ++i;
    }
    return hits;
}

// --- export -------------------------------------------------------------------------------------------------------------
enum class Stamps
{
    keep,  // as written: [9.3]
    times, // as minutes and seconds at the project's tempo: [0:16]
    none   // left out
};

// Seconds from the song's start to `tick` at the project's tempo (tempo automation is not followed).
inline double secondsAt (const Song& song, std::int64_t tick) noexcept
{
    return song.bpm > 0.0 ? (double) tick / (double) ppq * 60.0 / song.bpm : 0.0;
}

// The notes as a text file: the project's title and author on top when it has them, then the text (LF line breaks).
inline std::string exportText (const Song& song, Stamps stamps = Stamps::keep)
{
    std::string out;
    if (! song.info.title.empty())
        out += song.info.title + "\n";
    if (! song.info.author.empty())
        out += song.info.author + "\n";
    if (! out.empty())
        out += "\n";
    const auto& text = song.info.comments;
    const auto lines = parse (text);
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const auto& line = lines[i];
        auto raw = text.substr (line.begin, line.end - line.begin);
        if (! raw.empty() && raw.back() == '\r')
            raw.pop_back();
        if (line.stamped && stamps != Stamps::keep)
        {
            raw = text.substr (line.bodyBegin, line.end - line.bodyBegin);
            if (! raw.empty() && raw.back() == '\r')
                raw.pop_back();
            if (stamps == Stamps::times)
            {
                const auto seconds = (long) std::floor (secondsAt (song, tickOf (song, line.bar, line.beat)) + 1.0e-6);
                char time[32];
                std::snprintf (time, sizeof (time), "[%ld:%02ld]", seconds / 60, seconds % 60);
                raw = raw.empty() ? std::string (time) : std::string (time) + " " + raw;
            }
        }
        out += raw;
        if (i + 1 < lines.size())
            out += "\n";
    }
    if (out.empty() || out.back() != '\n')
        out += "\n";
    return out;
}

// How many words the notes hold (positions and headings not counted): the panel's footer.
inline int wordCount (std::string_view text)
{
    int words = 0;
    for (const auto& line : parse (text))
    {
        if (line.section)
            continue;
        bool in = false;
        for (const char c : line.body)
        {
            const auto space = c == ' ' || c == '\t' || c == '\r';
            if (! space && ! in)
                ++words;
            in = ! space;
        }
    }
    return words;
}
} // namespace lynx::lyrics
