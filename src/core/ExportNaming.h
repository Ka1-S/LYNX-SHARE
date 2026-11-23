#pragma once

// Issue #215 (round 190ng): wildcards in the export file name (LYNX's own; FL names the file after the project). The
// names follow REAPER's render wildcards, which the issue points to: "$project_$year-$month-$day" -> "MySong_2026-11-19".
// The Export window keeps the template with its other choices (and in its presets), shows the name it gives, and offers
// the wildcards in a list to click. Every resolved name is made safe for Windows, macOS and Linux file systems.
//
// Stems: "$track" (or "$insert") left in the name the file dialog gets is filled per stem file (stemFileName): one file
// per stem named by the template. In a one-file export "$track" is "Master".
// No JUCE: tests/Issues190ngTests.inc.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::exportnaming
{
inline constexpr const char* defaultTemplate = "$project";

struct Wildcard
{
    std::string_view token;   // "$project"
    std::string_view meaning; // the list's text (English key, translated by the window)
    std::string_view example; // what it gives for the window's example
};

// The list the window offers, in its order (the dialog groups them as listed).
inline constexpr Wildcard wildcards[] = {
    { "$project", TR_NOOP ("Project name"), "MySong" },
    { "$date", TR_NOOP ("Date (year-month-day)"), "2026-11-19" },
    { "$year", TR_NOOP ("Year"), "2026" },
    { "$year2", TR_NOOP ("Year, two digits"), "26" },
    { "$month", TR_NOOP ("Month (01-12)"), "11" },
    { "$monthname", TR_NOOP ("Month name"), "November" },
    { "$day", TR_NOOP ("Day of the month"), "19" },
    { "$time", TR_NOOP ("Time (hours-minutes)"), "21-35" },
    { "$hour", TR_NOOP ("Hour (00-23)"), "21" },
    { "$minute", TR_NOOP ("Minute"), "35" },
    { "$second", TR_NOOP ("Second"), "07" },
    { "$bpm", TR_NOOP ("Tempo (BPM)"), "128" },
    { "$key", TR_NOOP ("Project key"), "A minor" },
    { "$samplerate", TR_NOOP ("Sample rate (Hz)"), "48000" },
    { "$bitdepth", TR_NOOP ("Bit depth or bitrate"), "24bit" },
    { "$format", TR_NOOP ("Format"), "WAV" },
    { "$track", TR_NOOP ("Stem name (mixer track or channel; Master for one file)"), "Master" },
    { "$insert", TR_NOOP ("Mixer track of the stem (same as $track)"), "Master" },
    { "$pattern", TR_NOOP ("Current pattern"), "Pattern 1" },
    { "$take", TR_NOOP ("Take: the first number not used yet in the folder"), "3" },
};

// What the wildcards stand for in one export.
struct Context
{
    std::string project = "Untitled";
    int year = 2000, month = 1, day = 1, hour = 0, minute = 0, second = 0;
    double bpm = 120.0;
    std::string key;            // "A minor" (empty: no key set)
    int sampleRate = 44100;
    std::string bitDepth;       // "24bit", "320kbps"...
    std::string format = "WAV";
    std::string track = "Master";
    std::string pattern;
    // $take: true when a file of that resolved name (without extension) already exists; null = take 1
    std::function<bool (const std::string&)> exists;
};

inline std::string twoDigits (int v)
{
    const auto s = std::to_string (v < 0 ? 0 : v);
    return s.size() < 2 ? "0" + s : s;
}

inline std::string bpmText (double bpm)
{
    const auto hundredths = (long long) (bpm * 100.0 + 0.5);
    auto s = std::to_string (hundredths / 100);
    if (const auto frac = hundredths % 100; frac != 0)
    {
        s += "." + twoDigits ((int) frac);
        if (s.back() == '0')
            s.pop_back();
    }
    return s;
}

inline std::string monthName (int month)
{
    static const char* names[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    return month >= 1 && month <= 12 ? names[month - 1] : std::string();
}

// The value of `token` (without "$"), or false when it is not a wildcard. `take` is the take number to use.
inline bool valueOf (std::string_view token, const Context& c, int take, std::string& out)
{
    if (token == "project") out = c.project;
    else if (token == "date") out = std::to_string (c.year) + "-" + twoDigits (c.month) + "-" + twoDigits (c.day);
    else if (token == "year") out = std::to_string (c.year);
    else if (token == "year2") out = twoDigits (c.year % 100);
    else if (token == "month") out = twoDigits (c.month);
    else if (token == "monthname") out = monthName (c.month);
    else if (token == "day") out = twoDigits (c.day);
    else if (token == "time") out = twoDigits (c.hour) + "-" + twoDigits (c.minute);
    else if (token == "hour") out = twoDigits (c.hour);
    else if (token == "minute") out = twoDigits (c.minute);
    else if (token == "second") out = twoDigits (c.second);
    else if (token == "bpm") out = bpmText (c.bpm);
    else if (token == "key") out = c.key;
    else if (token == "samplerate") out = std::to_string (c.sampleRate);
    else if (token == "bitdepth") out = c.bitDepth;
    else if (token == "format") out = c.format;
    else if (token == "track" || token == "insert") out = c.track;
    else if (token == "pattern") out = c.pattern;
    else if (token == "take") out = std::to_string (take);
    else return false;
    return true;
}

inline bool isTokenChar (char ch) noexcept
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

// `text` with every wildcard replaced; the longest wildcard that starts after a "$" wins ("$year2" before "$year",
// "$projectX" = "$project" + "X"); "$$" is a "$"; anything else stays as typed. `keep` (optional): wildcards left as they
// are (the stems' "$track").
inline std::string substitute (std::string_view text, const Context& c, int take, const std::vector<std::string_view>& keep = {})
{
    std::string out;
    for (std::size_t i = 0; i < text.size();)
    {
        if (text[i] != '$')
        {
            out += text[i++];
            continue;
        }
        if (i + 1 < text.size() && text[i + 1] == '$')
        {
            out += '$';
            i += 2;
            continue;
        }
        auto end = i + 1;
        while (end < text.size() && isTokenChar (text[end]))
            ++end;
        bool done = false;
        for (auto len = end - i - 1; len > 0 && ! done; --len)
        {
            std::string lower;
            for (auto k = i + 1; k < i + 1 + len; ++k)
                lower += (char) ((text[k] >= 'A' && text[k] <= 'Z') ? text[k] - 'A' + 'a' : text[k]);
            std::string value;
            if (valueOf (lower, c, take, value))
            {
                bool kept = false;
                for (const auto k : keep)
                    kept = kept || k.substr (1) == lower;
                out += kept ? std::string (text.substr (i, len + 1)) : value;
                i += len + 1;
                done = true;
            }
        }
        if (! done)
            out += text[i++];
    }
    return out;
}

// A name every file system takes: no < > : " / \ | ? * or control characters (each becomes "_"), no spaces or dots at
// the end, no Windows device name (CON, NUL, COM1...: "_" before it), at most 180 bytes (cut on a UTF-8 boundary), and
// never empty ("Untitled"). A "$" stays (the stems' "$track" goes through the file dialog).
inline std::string sanitise (std::string name)
{
    std::string clean;
    for (const auto ch : name)
        clean += std::string_view ("<>:\"/\\|?*").find (ch) != std::string_view::npos || (unsigned char) ch < 32 || ch == 127 ? '_' : ch;
    while (! clean.empty() && (clean.front() == ' '))
        clean.erase (clean.begin());
    if (clean.size() > 180)
    {
        auto cut = (std::size_t) 180;
        while (cut > 0 && ((unsigned char) clean[cut] & 0xC0) == 0x80) // (not inside a UTF-8 sequence)
            --cut;
        clean.resize (cut);
    }
    while (! clean.empty() && (clean.back() == ' ' || clean.back() == '.'))
        clean.pop_back();
    if (clean.empty())
        return "Untitled";
    std::string upper;
    for (const auto ch : clean.substr (0, clean.find ('.')))
        upper += (char) ((ch >= 'a' && ch <= 'z') ? ch - 'a' + 'A' : ch);
    static const char* reserved[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                      "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
    for (const auto* r : reserved)
        if (upper == r)
            return "_" + clean;
    return clean;
}

inline bool hasStemWildcard (std::string_view text)
{
    for (std::size_t i = 0; i + 1 < text.size(); ++i)
        if (text[i] == '$' && (i == 0 || text[i - 1] != '$'))
        {
            auto end = i + 1;
            while (end < text.size() && isTokenChar (text[end]))
                ++end;
            std::string lower;
            for (auto k = i + 1; k < end; ++k)
                lower += (char) ((text[k] >= 'A' && text[k] <= 'Z') ? text[k] - 'A' + 'a' : text[k]);
            if (lower.rfind ("track", 0) == 0 || lower.rfind ("insert", 0) == 0)
                return true;
        }
    return false;
}

// Issue #215 follow-up (round 190qb): a "/" (or a backslash) in the template puts the file in folders, made inside the folder the
// export goes to ("BOUNCE/$project_$date" or "/BOUNCE/..." -> "BOUNCE/MySong_2026-11-20", as in
// REAPER's render file name). The template is split before the wildcards are filled, so a "/" in a value (a project named
// "AC/DC") still becomes "_". Each folder is made safe like a file name; empty, "." and ".." folders are dropped, so a
// name never leaves the export folder. Folders take the one-file values ("$track" = "Master").
inline std::vector<std::string_view> splitFolders (std::string_view templ)
{
    std::vector<std::string_view> parts;
    std::size_t from = 0;
    for (std::size_t i = 0; i <= templ.size(); ++i)
        if (i == templ.size() || templ[i] == '/' || templ[i] == '\\')
        {
            parts.push_back (templ.substr (from, i - from));
            from = i + 1;
        }
    return parts;
}

inline bool hasFolders (std::string_view templ)
{
    return templ.find_first_of ("/\\") != std::string_view::npos;
}

// The file name (without extension) a template gives, with its folders before it ("BOUNCE/Mixes/MySong", always "/"
// between them). `stems`: "$track" / "$insert" stay in the file's own name for the per-stem files. The take is the first
// number from 1 whose name (folders included) `c.exists` doesn't report (at most 9999).
inline std::string resolve (std::string_view templ, const Context& c, bool stems = false)
{
    const auto blank = [] (std::string_view t) { return t.find_first_not_of (' ') == std::string_view::npos; };
    auto parts = splitFolders (blank (templ) ? std::string_view (defaultTemplate) : templ);
    auto fileTemplate = parts.back();
    parts.pop_back();
    if (blank (fileTemplate))
        fileTemplate = defaultTemplate; // ("BOUNCE/": the project's name in that folder)
    const std::vector<std::string_view> keep = stems ? std::vector<std::string_view> { "$track", "$insert" } : std::vector<std::string_view> {};
    const auto anyTake = [] (std::string_view t) {
        std::string lower;
        for (const auto ch : t)
            lower += (char) ((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch);
        return lower.find ("$take") != std::string::npos;
    };
    const auto build = [&] (int take) {
        std::string path;
        for (const auto part : parts)
        {
            if (blank (part))
                continue;
            const auto folder = sanitise (substitute (part, c, take));
            if (folder == "Untitled" && (part.find_first_not_of (". ") == std::string_view::npos))
                continue; // ("." / ".." / dots only: never a step out of the export folder)
            path += folder + "/";
        }
        return path + sanitise (substitute (fileTemplate, c, take, keep));
    };
    auto name = build (1);
    bool takes = anyTake (fileTemplate);
    for (const auto part : parts)
        takes = takes || anyTake (part);
    if (! takes)
        return name;
    for (int take = 1; take < 10000; ++take)
    {
        name = build (take);
        if (c.exists == nullptr || ! c.exists (name))
            break;
    }
    return name;
}
} // namespace lynx::exportnaming
