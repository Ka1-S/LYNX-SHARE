#pragma once

// The Loop Browser's keywords and filters (round gaps190kf, coverage rows gb-loops_media-001/002/003/008 and Live's
// browser-0043/0044; GarageBand for Mac User Guide "Find loops", "Customize the Loop Browser", "Set your favorite
// Apple Loops", "Change Loops settings"; evidence /mnt/e/lynx-work/gaps190kf/loops/NOTES.md).
//
// GarageBand's Loop Browser (guide picture gb-loop-browser.png): three keyword categories (Instrument, Genre,
// Descriptors) as tabs over a grid of keyword buttons (4 columns); "Clicking multiple buttons narrows the results to
// those loops that match all of the selected keywords"; "If a keyword is dimmed, there are no loops that match"; the
// reset button clears the picks; a Favorites button shows the favourite loops only; a Scale menu (Any, Minor, Major,
// Neither, Good for Both); the Loop Types button (All / Audio / MIDI / Drummer loops); a name search; the results list
// with the loop's type, name, tempo, key and a Favorites check box; Loops settings "Filter for more relevant results"
// (keys within two semitones of the project key).
//
// Apple Loops carry their keywords, scale and tempo in the file; the files LYNX browses (the user's sample folders, FL's
// packs) mostly don't. So LYNX gives a loop its keywords from (1) the words of its name and of its folders (a "Kicks"
// folder, "Dark Pad 90 Am.wav"), (2) the user's Browser tags (round 190iz) that name a keyword, and (3) factory
// keywords handed in by the caller (round 190ke's tags of LYNX's own content). Its key comes from the name
// (keyfollow::keyInName, round 190ds), its tempo from the name (previewtempo, round 190fk). LYNX's own words and
// lists, clean-room; the keyword ids are lower-case English. No JUCE: tests/LoopBrowser190kfTests.inc.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lynx::loopkeys
{
enum class Category
{
    instrument,
    genre,
    descriptor
};
constexpr int categoryCount = 3;

struct Keyword
{
    const char* id;                 // lower-case id, also the button's English label key ("all drums")
    const char* label;              // the button's text (translated by the UI)
    Category category;
    std::vector<const char*> words; // name / folder words that give it (lower-case, whole words; "hi hat" = two words in a row)
    std::vector<const char*> also;  // keywords it implies ("kick" gives "all drums")
};

// The keyword buttons in the grid's order (GarageBand's: columns of 4, filled row by row).
const std::vector<Keyword>& vocabulary();
const Keyword* find (const std::string& id);
std::vector<const Keyword*> ofCategory (Category c);

// Loop types (GarageBand's Loop Types: audio, MIDI, Drummer; LYNX's third type is a pattern: an FL score or a
// pattern file the Channel Rack plays)
enum LoopType : int
{
    audio = 1,
    midi = 2,
    pattern = 4,
    allTypes = audio | midi | pattern
};
// The type of a file by its extension (0: not a loop the Loop Browser lists)
int typeOf (const std::string& fileName);

// GarageBand's Scale menu
enum class Scale
{
    any,
    minor,
    major,
    neither, // no tonal scale: drums, percussion, effects
    both     // "Good for Both": a loop that has no third (LYNX: a tonal loop whose name gives a root without major / minor)
};

// What the Loop Browser knows of one file.
struct LoopInfo
{
    std::string path, name;   // name: the file name without folder and extension
    int type = 0;             // LoopType
    std::set<std::string> keywords;
    double tempo = 0.0;       // from the name (0: not known)
    int keyRoot = -1;         // 0 = C … 11 = B (-1: not known)
    Scale scale = Scale::any; // minor / major / neither / both (any: not known)
};

// Lower-case whole words of a path (folders and name; the extension dropped), split at anything but letters / digits.
std::vector<std::string> words (const std::string& path);

// The keywords the words give (a word, or two words in a row, as written, joined or with a plural "s"), and the
// keywords those imply.
std::set<std::string> keywordsOfWords (const std::vector<std::string>& w);

// Everything the browser shows and filters for `path`: the words of `wordsOf` (the path below the place it was found
// in, so the user's own folder names above it don't count; "" = the whole path), plus `tags` (the user's tags of the
// file: a tag that names a keyword — by id or label, any case — gives it) and `factory` (keyword ids handed in for
// LYNX's own content).
LoopInfo describe (const std::string& path, const std::string& wordsOf = {}, const std::vector<std::string>& tags = {},
                   const std::vector<std::string>& factory = {});

// Factory keywords: LYNX's own content tagged by keyword (round 190ke's factory tags register a source here; none by
// default). The source is called from the Browser's search worker thread, so it must be thread-safe (a lookup in a
// table it built before).
using FactorySource = std::function<std::vector<std::string> (const std::string& path)>;
void setFactorySource (FactorySource source);
std::vector<std::string> factoryKeywords (const std::string& path);

// The filter of the Loop Browser.
struct Filter
{
    std::vector<std::string> keywords; // picked keyword ids: a loop must carry all of them
    Scale scale = Scale::any;
    int types = allTypes;              // the Loop Types ticks
    bool favouritesOnly = false;
    double tempoMin = 0.0, tempoMax = 0.0; // > 0: only loops whose tempo is known and within [min, max]
    int nearKey = -1;                  // 0…11: only loops whose root is within 2 semitones of it (GarageBand's setting)
    std::string text;                  // the search box (search::matches on the name)

    bool active() const noexcept
    {
        return ! keywords.empty() || scale != Scale::any || types != allTypes || favouritesOnly || tempoMin > 0.0 || tempoMax > 0.0
               || nearKey >= 0 || ! text.empty();
    }
};

// Whether `loop` passes `filter` (`favourite`: the loop is in the user's favourites).
bool matches (const LoopInfo& loop, const Filter& filter, bool favourite);

// For each keyword, how many of `loops` would still pass `filter` with that keyword picked too (a picked keyword: how
// many pass now). A keyword with 0 is drawn dimmed (GarageBand). `favourite (i)` tells whether loops[i] is a favourite.
template <class IsFavourite>
std::map<std::string, int> keywordCounts (const std::vector<LoopInfo>& loops, const Filter& filter, IsFavourite&& favourite)
{
    std::map<std::string, int> counts;
    for (const auto& k : vocabulary())
        counts[k.id] = 0;
    auto rest = filter;
    rest.keywords.clear();
    for (std::size_t i = 0; i < loops.size(); ++i)
    {
        const auto& loop = loops[i];
        if (! matches (loop, rest, favourite (i)))
            continue;
        if (! std::all_of (filter.keywords.begin(), filter.keywords.end(), [&loop] (const std::string& k) { return loop.keywords.count (k) > 0; }))
            continue;
        for (const auto& k : loop.keywords)
            if (const auto it = counts.find (k); it != counts.end())
                ++it->second;
    }
    return counts;
}

// The tempo choices of the Tempo menu: Any, then ranges (LYNX's; Live's / Splice's tempo filter idea), and "Near the
// project tempo" (±6 %, the stretch Live's warping keeps transparent for most loops)
struct TempoRange
{
    const char* label;
    double min, max; // 0, 0 = any; min < 0: near the project tempo
};
const std::vector<TempoRange>& tempoRanges();
// The [min, max] of choice `index` for a project at `bpm` (0, 0 for Any)
std::pair<double, double> tempoBounds (int index, double bpm);

// Semitones between two roots (0…6)
inline int rootDistance (int a, int b) noexcept
{
    const auto d = ((a - b) % 12 + 12) % 12;
    return std::min (d, 12 - d);
}

// "120", "Am", "C#" — the results list's Tempo and Key columns
std::string tempoText (const LoopInfo& loop);
// Round 190mf (DS-41, BandLab's "4 bars · Synth · …"): a loop's length in whole bars of `beatsPerBar` beats at its own
// tempo (0: the tempo or the length is not known, or it is under half a bar).
int loopBars (double seconds, double tempo, int beatsPerBar = 4) noexcept;
std::string keyText (const LoopInfo& loop);

// The keyword-button layout the user changed (GarageBand: Control-click a button for another keyword, drag one onto
// another to swap; "Reset" puts them back): per category, the keyword ids in grid order. Kept with the Browser's
// prefs; the default is vocabulary()'s order.
struct Layout
{
    std::array<std::vector<std::string>, categoryCount> order;
    static Layout defaults();
    // swap the buttons at grid places a and b of `c`
    bool swap (Category c, int a, int b);
    // put keyword `id` on grid place `at` of `c` (the keyword it replaces goes where `id` was, when `id` is shown)
    bool place (Category c, int at, const std::string& id);
    std::string toText() const;              // "instrument=all drums,piano,...\n" ...
    static Layout fromText (const std::string& text); // unknown ids dropped, missing ones appended (defaults on error)
    bool operator== (const Layout&) const = default;
};
} // namespace lynx::loopkeys
