// Round 190on (research190oe P2, P8, P10 and the Online Sessions "remove a guest" decision): the command palette's matcher,
// Tab to transient, stacked controller lanes, and the invite code change when the host removes someone.

// ==== 190on P2: the command palette
#include "core/ActionSearch.h"
#include "core/KeyMap.h"

namespace research190on
{
inline std::string ids (const std::vector<lynx::actionsearch::Entry>& e, const std::string& query, const std::vector<std::string>& recent = {})
{
    std::string s;
    for (auto i : lynx::actionsearch::find (e, query, recent))
        s += e[(std::size_t) i].id;
    return s;
}

// "English" = "Translation" lines of a resources/i18n table
inline std::map<std::string, std::string> table (const std::string& lang)
{
    std::map<std::string, std::string> out;
    std::ifstream in ("resources/i18n/" + lang + ".txt");
    std::string line;
    const auto unquote = [] (const std::string& s, std::size_t& at) {
        std::string v;
        if (at >= s.size() || s[at] != '"')
            return v;
        for (++at; at < s.size() && s[at] != '"'; ++at)
        {
            if (s[at] == '\\' && at + 1 < s.size())
                ++at;
            v += s[at];
        }
        ++at;
        return v;
    };
    while (std::getline (in, line))
    {
        std::size_t at = 0;
        if (line.empty() || line[0] != '"')
            continue;
        const auto key = unquote (line, at);
        at = line.find ('"', at);
        if (at == std::string::npos)
            continue;
        out[key] = unquote (line, at);
    }
    return out;
}
} // namespace research190on

TEST (research190on_p2_palette_matcher_ranks_and_folds)
{
    using namespace lynx::actionsearch;
    using research190on::ids;
    std::vector<Entry> e {
        { "pref", "Preferences", "Preferences", "Options", "" },
        { "pr", "Piano roll", "Piano roll", "View", "F7" },
        { "met", "Toggle the metronome", "Toggle the metronome", "Everywhere", "Ctrl+M" },
        { "edit", "Edição de áudio", "Audio editing", "Ferramentas", "" },
        { "uber", "ÜBER das Projekt", "About the project", "Hilfe", "" },
        { "ru", "Микшер", "Mixer", "Окна", "F9" },
    };
    CHECK (ids (e, "pr").substr (0, 6) == "prpref"); // "pr" = Piano Roll's initials, before "Preferences" (a prefix)
    CHECK (ids (e, "pr") == "prprefuber");             // (then a word that starts with it: "Projekt")
    CHECK (ids (e, "edicao") == "edit");       // accents and case don't matter
    CHECK (ids (e, "EDIÇÃO") == "edit");
    CHECK (ids (e, "audio") == "edit");        // the English name too
    CHECK (ids (e, "uber") == "uber");
    CHECK (ids (e, "МИКШ") == "ru");            // Cyrillic capitals fold
    CHECK (ids (e, "mtrnm") == "met");          // the loose match: the letters in order from a word start
    {
        using lynx::actionsearch::fold;
        using lynx::actionsearch::looseMatch;
        CHECK (looseMatch (fold ("Song position to the next transient"), fold ("nxt trnsnt")));
        CHECK (! looseMatch (fold ("Move the playback marker to the start"), fold ("metro"))); // (letters scattered over words)
        CHECK (looseMatch (fold ("Metronome on/off"), fold ("mtrnm")));
        CHECK (! looseMatch (fold ("Metronome"), fold ("me"))); // (fewer than three letters)
    }
    CHECK (ids (e, "tm") == "met");             // initials of consecutive words: "the metronome"
    CHECK (ids (e, "zz").empty());
    CHECK (ids (e, "xq").empty());
    // empty query: everything in the list's order; recent ones first
    CHECK (ids (e, "") == "prefprmetedituberru");
    CHECK (ids (e, "", { "ru", "met" }) == "rumetprefpredituber");
    // within one rank the recent one first
    std::vector<Entry> two { { "a", "Metronome sound", "Metronome sound", "", "" }, { "b", "Metronome volume", "Metronome volume", "", "" } };
    CHECK (ids (two, "metro") == "ab");
    CHECK (ids (two, "metro", { "b" }) == "ba");
    // the recent list: latest first, once each, at most 20
    std::vector<std::string> recent;
    for (int i = 0; i < 25; ++i)
        noteRun (recent, "id" + std::to_string (i % 22));
    CHECK (recent.size() == 20 && recent.front() == "id2");
    noteRun (recent, "id10");
    CHECK (recent.front() == "id10" && std::count (recent.begin(), recent.end(), std::string ("id10")) == 1);
    // the old #228 expectations hold
    std::vector<Entry> old {
        { "a", "Toggle the metronome", "Toggle the metronome", "Everywhere", "Ctrl+M" },
        { "b", "Metronome sound", "Metronome sound", "Everywhere", "" },
        { "c", "Select the next mixer track", "Select the next mixer track", "Mixer", "Right" },
        { "d", "Metronome-Klang", "Metronome sound (de)", "Überall", "" },
    };
    CHECK (ids (old, "metro") == "bda");
    CHECK (ids (old, "mixer next") == "c");
    CHECK (ids (old, "ctrl+m") == "a");
    CHECK (ids (old, "?") == "abcd");
}

TEST (research190on_p2_palette_key_and_languages)
{
    using namespace lynx;
    using namespace lynx::keys;
    const auto* palette = findAction ("view.commandPalette");
    CHECK (palette != nullptr);
    if (palette != nullptr)
    {
        CHECK (palette->context == Context::global);
        CHECK (palette->defaults.size() == 1 && palette->defaults[0] == (Chord { 'Y', (std::uint8_t) (ctrl | shift) }));
    }
    KeyMap map;
    CHECK (map.conflicts().empty());
    for (const auto context : { Context::global, Context::channelRack, Context::pianoRoll, Context::playlist, Context::mixer, Context::browser })
        CHECK (map.find (Chord { 'Y', (std::uint8_t) (ctrl | shift) }, context, true) == "view.commandPalette"); // (typing piano on too)
    // FL 2026's Ctrl+Shift+P (Patterns › Open in project browser) and Ctrl+Shift+Space (play) are not the palette's
    CHECK (map.find (Chord { 'P', (std::uint8_t) (ctrl | shift) }, Context::global, false) != "view.commandPalette");
    CHECK (map.find (Chord { space, (std::uint8_t) (ctrl | shift) }, Context::global, false) != "view.commandPalette");
    CHECK (findAction ("help.searchActions") != nullptr && findAction ("help.searchActions")->defaults.empty()); // ("?" stays)

    // matching in the UI language and in English: pt-BR and de tables, without accents
    for (const auto& [lang, query] : std::vector<std::pair<std::string, std::string>> { { "pt-BR", "metronomo" }, { "de", "metronom" } })
    {
        const auto t = research190on::table (lang);
        std::vector<actionsearch::Entry> entries;
        for (const auto& a : actions())
        {
            const auto it = t.find (a.description);
            entries.push_back ({ a.id, it != t.end() ? it->second : std::string (a.description), a.description, "", "" });
        }
        const auto hits = actionsearch::find (entries, query);
        bool metronome = false;
        for (const auto i : hits)
            metronome = metronome || std::string (entries[(std::size_t) i].id).find ("etronome") != std::string::npos;
        CHECK (! hits.empty() && metronome);
        CHECK (! actionsearch::find (entries, "metronome").empty()); // (English)
    }
}

