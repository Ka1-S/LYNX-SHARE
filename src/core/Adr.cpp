#include "core/Adr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lynx::adr
{
namespace
{
std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::string trim (const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a]))
        ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1]))
        --b;
    return s.substr (a, b - a);
}

std::string csvField (const std::string& s)
{
    if (s.find_first_of (",\"\n\r") == std::string::npos)
        return s;
    std::string out = "\"";
    for (const auto c : s)
    {
        if (c == '"')
            out += '"';
        out += c;
    }
    return out + "\"";
}

// Splits CSV text into rows of fields (RFC 4180 quoting) with separator `sep`
std::vector<std::vector<std::string>> parseRows (const std::string& text, char sep)
{
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false, any = false;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto c = text[i];
        if (quoted)
        {
            if (c == '"')
            {
                if (i + 1 < text.size() && text[i + 1] == '"')
                {
                    field += '"';
                    ++i;
                }
                else
                    quoted = false;
            }
            else
                field += c;
            continue;
        }
        if (c == '"')
        {
            quoted = true;
            any = true;
        }
        else if (c == sep)
        {
            row.push_back (field);
            field.clear();
            any = true;
        }
        else if (c == '\n' || c == '\r')
        {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            if (any || ! field.empty())
            {
                row.push_back (field);
                rows.push_back (row);
            }
            row.clear();
            field.clear();
            any = false;
        }
        else
        {
            field += c;
            any = true;
        }
    }
    if (any || ! field.empty())
    {
        row.push_back (field);
        rows.push_back (row);
    }
    return rows;
}

std::optional<double> parseTime (const std::string& s, const vtime::Rate& rate, double startTimecode)
{
    const auto t = trim (s);
    if (t.empty())
        return std::nullopt;
    // timecode (has a separator after the first field) or plain seconds ("12.5" / "12,5")
    const auto colon = t.find_first_of (":;");
    if (colon != std::string::npos)
    {
        if (const auto tc = vtime::parseSeconds (t, rate))
            return *tc - startTimecode;
        return std::nullopt;
    }
    std::string n = t;
    std::replace (n.begin(), n.end(), ',', '.');
    char* end = nullptr;
    const auto v = std::strtod (n.c_str(), &end);
    if (end == n.c_str() || *end != 0)
        return std::nullopt;
    return v;
}
} // namespace

const char* statusName (AdrCue::Status s) noexcept
{
    switch (s)
    {
        case AdrCue::Status::todo: return "To do";
        case AdrCue::Status::recorded: return "Recorded";
        case AdrCue::Status::approved: return "Approved";
        case AdrCue::Status::omit: return "Omit";
    }
    return "To do";
}

std::optional<AdrCue::Status> statusFromName (const std::string& s) noexcept
{
    const auto l = lower (trim (s));
    if (l == "to do" || l == "todo" || l.empty())
        return AdrCue::Status::todo;
    if (l == "recorded" || l == "done")
        return AdrCue::Status::recorded;
    if (l == "approved" || l == "ok")
        return AdrCue::Status::approved;
    if (l == "omit" || l == "omitted" || l == "cut")
        return AdrCue::Status::omit;
    return std::nullopt;
}

std::string nextId (const std::vector<AdrCue>& cues)
{
    if (cues.empty())
        return "1";
    // the last id's trailing number + 1, keeping its width and prefix
    std::string best;
    long bestValue = -1;
    for (const auto& c : cues)
    {
        std::size_t i = c.id.size();
        while (i > 0 && std::isdigit ((unsigned char) c.id[i - 1]))
            --i;
        if (i == c.id.size())
            continue;
        const auto v = std::strtol (c.id.c_str() + i, nullptr, 10);
        if (v >= bestValue)
        {
            bestValue = v;
            best = c.id;
        }
    }
    if (bestValue < 0)
        return std::to_string (cues.size() + 1);
    std::size_t i = best.size();
    while (i > 0 && std::isdigit ((unsigned char) best[i - 1]))
        --i;
    const auto width = best.size() - i;
    auto digits = std::to_string (bestValue + 1);
    while (digits.size() < width)
        digits.insert (digits.begin(), '0');
    auto id = best.substr (0, i) + digits;
    // (never a duplicate)
    while (std::any_of (cues.begin(), cues.end(), [&] (const AdrCue& c) { return c.id == id; }))
        id += "b";
    return id;
}

std::string takeName (const AdrCue& cue, int number)
{
    auto clean = [] (const std::string& s) {
        std::string out;
        for (const auto c : s)
        {
            if (c == ' ' || c == '\t')
                out += '-';
            else if (std::string ("\\/:*?\"<>|").find (c) == std::string::npos && (unsigned char) c >= 32)
                out += c;
        }
        return out;
    };
    auto name = clean (cue.id.empty() ? std::string ("cue") : cue.id);
    if (! cue.character.empty())
        name += "_" + clean (cue.character);
    return name + "_T" + std::to_string (number);
}

int nextTakeNumber (const AdrCue& cue) noexcept
{
    int n = 0;
    for (const auto& t : cue.takes)
        n = std::max (n, t.number);
    return n + 1;
}

void sortByTime (std::vector<AdrCue>& cues)
{
    std::stable_sort (cues.begin(), cues.end(), [] (const AdrCue& a, const AdrCue& b) { return a.in < b.in; });
}

int cueAt (const std::vector<AdrCue>& cues, double t) noexcept
{
    for (std::size_t i = 0; i < cues.size(); ++i)
        if (t >= cues[i].in && t < cues[i].out)
            return (int) i;
    return -1;
}

std::string toCsv (const std::vector<AdrCue>& cues, const vtime::Rate& rate, double startTimecode)
{
    std::string out = "Cue,In,Out,Character,Line,Notes,Status\r\n";
    for (const auto& c : cues)
    {
        out += csvField (c.id) + "," + vtime::format (c.in + startTimecode, rate) + "," + vtime::format (c.out + startTimecode, rate) + ","
               + csvField (c.character) + "," + csvField (c.line) + "," + csvField (c.notes) + "," + statusName (c.status) + "\r\n";
    }
    return out;
}

CsvResult fromCsv (const std::string& input, const vtime::Rate& rate, double startTimecode)
{
    CsvResult r;
    auto text = input;
    if (text.size() >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB && (unsigned char) text[2] == 0xBF)
        text.erase (0, 3);
    // the separator: the one the header line has most of
    const auto firstLine = text.substr (0, text.find_first_of ("\r\n"));
    char sep = ',';
    std::size_t most = 0;
    for (const auto s : { ',', ';', '\t' })
        if (const auto n = (std::size_t) std::count (firstLine.begin(), firstLine.end(), s); n > most)
        {
            most = n;
            sep = s;
        }
    const auto rows = parseRows (text, sep);
    if (rows.empty())
    {
        r.error = "empty";
        return r;
    }
    int cId = -1, cIn = -1, cOut = -1, cChar = -1, cLine = -1, cNotes = -1, cStatus = -1;
    for (int i = 0; i < (int) rows[0].size(); ++i)
    {
        const auto h = lower (trim (rows[0][(std::size_t) i]));
        if (h == "cue" || h == "id" || h == "cue id" || h == "#" || h == "number")
            cId = i;
        else if (h == "in" || h == "start" || h == "tc in" || h == "timecode in" || h == "time in")
            cIn = i;
        else if (h == "out" || h == "end" || h == "tc out" || h == "timecode out" || h == "time out")
            cOut = i;
        else if (h == "character" || h == "role" || h == "actor" || h == "speaker")
            cChar = i;
        else if (h == "line" || h == "dialogue" || h == "dialog" || h == "text")
            cLine = i;
        else if (h == "notes" || h == "note" || h == "comment" || h == "comments")
            cNotes = i;
        else if (h == "status")
            cStatus = i;
    }
    if (cIn < 0)
    {
        r.error = "no In column";
        return r;
    }
    auto at = [] (const std::vector<std::string>& row, int c) { return c >= 0 && c < (int) row.size() ? row[(std::size_t) c] : std::string(); };
    for (std::size_t k = 1; k < rows.size(); ++k)
    {
        const auto& row = rows[k];
        const auto in = parseTime (at (row, cIn), rate, startTimecode);
        if (! in || *in < 0.0)
        {
            ++r.skipped;
            continue;
        }
        AdrCue c;
        c.in = *in;
        const auto out = parseTime (at (row, cOut), rate, startTimecode);
        c.out = out && *out > c.in ? *out : c.in + 2.0;
        c.id = trim (at (row, cId));
        if (c.id.empty())
            c.id = nextId (r.cues);
        c.character = trim (at (row, cChar));
        c.line = at (row, cLine);
        c.notes = at (row, cNotes);
        c.status = statusFromName (at (row, cStatus)).value_or (AdrCue::Status::todo);
        r.cues.push_back (c);
    }
    return r;
}

std::vector<double> beepTimes (double in)
{
    return { in - 3.0, in - 2.0, in - 1.0 };
}

std::vector<float> renderBeep (double sampleRate, double frequency, double length)
{
    const auto n = (std::size_t) std::max (1.0, std::round (length * sampleRate));
    const auto fade = std::max<std::size_t> (1, (std::size_t) (0.002 * sampleRate));
    std::vector<float> out (n);
    constexpr double twoPi = 6.283185307179586;
    for (std::size_t i = 0; i < n; ++i)
    {
        double g = 0.5; // −6 dBFS
        if (i < fade)
            g *= (double) i / (double) fade;
        if (n - 1 - i < fade)
            g *= (double) (n - 1 - i) / (double) fade;
        out[i] = (float) (g * std::sin (twoPi * frequency * (double) i / sampleRate));
    }
    return out;
}

Signals signalsAt (const AdrCue& cue, const AdrSettings& settings, double t, double frameSeconds) noexcept
{
    Signals s;
    if (settings.streamer && settings.streamerLength > 0.0 && t >= cue.in - settings.streamerLength && t < cue.in)
        s.streamer = (t - (cue.in - settings.streamerLength)) / settings.streamerLength;
    s.punch = settings.punch && t >= cue.in && t < cue.in + 2.0 * frameSeconds;
    s.inLine = t >= cue.in && t < cue.out;
    return s;
}

std::vector<CompSpan> compSpans (const AdrCue& cue)
{
    std::vector<CompSpan> out;
    auto parts = cue.comp;
    std::sort (parts.begin(), parts.end(), [] (const AdrCompPart& a, const AdrCompPart& b) { return a.from < b.from; });
    double at = cue.in;
    auto push = [&] (int take, double from, double to) {
        from = std::max (from, cue.in);
        to = std::min (to, cue.out);
        if (to <= from + 1e-9 || take <= 0)
            return;
        if (! out.empty() && out.back().take == take && std::abs (out.back().to - from) < 1e-9)
            out.back().to = to;
        else
            out.push_back ({ take, from, to });
    };
    for (const auto& p : parts)
    {
        if (p.from > at)
            push (cue.selected, at, p.from);
        push (p.take, std::max (at, p.from), p.to);
        at = std::max (at, p.to);
    }
    if (at < cue.out)
        push (cue.selected, at, cue.out);
    return out;
}

void setCompRange (AdrCue& cue, double from, double to, int take)
{
    from = std::max (from, cue.in);
    to = std::min (to, cue.out);
    if (to <= from)
        return;
    std::vector<AdrCompPart> out;
    for (const auto& p : cue.comp)
    {
        if (p.to <= from || p.from >= to)
        {
            out.push_back (p);
            continue;
        }
        if (p.from < from)
            out.push_back ({ p.from, from, p.take });
        if (p.to > to)
            out.push_back ({ to, p.to, p.take });
    }
    out.push_back ({ from, to, take });
    std::sort (out.begin(), out.end(), [] (const AdrCompPart& a, const AdrCompPart& b) { return a.from < b.from; });
    // neighbours of the same take merge
    std::vector<AdrCompPart> merged;
    for (const auto& p : out)
        if (! merged.empty() && merged.back().take == p.take && std::abs (merged.back().to - p.from) < 1e-9)
            merged.back().to = p.to;
        else
            merged.push_back (p);
    cue.comp = std::move (merged);
}
} // namespace lynx::adr
