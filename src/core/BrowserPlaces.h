#pragma once

// What the Browser's tabs and the Live-style sidebar list (round 190iz: BUILD-LIST 9, design slices FM-S7 and CL-J;
// docs/analise/REA_rodada190iz.md §1 and §3). No JUCE: tested in isolation (tests/Browser190izTests.inc).
//
// FL layout (Pattern workflow): FL 2026's Browser has six icon tabs, in this order (FLEngine strings "PROJECT",
// "PLUGINS", "PRESETS", "SOUNDS", "STARRED" next to "UserTabs", "*.tabconfig"; Settings/Browser/Tab_*.tabconfig;
// reference shot fl-playlist.png 0,104,205,140: waveform, documents, plug, globe, cloud, star): ALL, PROJECT
// (Current project), PLUGINS (Plugin database), PRESETS, SOUNDS (FL Cloud), STARRED. LYNX has no FL Cloud: PRESETS
// lists the preset places on disk and SOUNDS the sample places, each under its content filter.
//
// Clip-launch layout: Live 12's browser (reference live-session.png 0..318, 86..800): a sidebar with three headed groups
// (Collections, Library, Places) beside the content list. Library categories LYNX has nothing for (Max for Live,
// Grooves as files, Tunings) are shown dimmed, as Live dims the places it can't open (Cloud, Push).

#include <array>
#include <string>
#include <vector>

#include "core/BrowserView.h"

namespace lynx::browserplaces
{
// --- FL's tabs ---------------------------------------------------------------------------------------------------------
// The tab ids keep the Browser's earlier numbers (0 ALL, 1 PROJECT, 2 STARRED) so a saved tab or a caller's
// setTab (starredTab) means what it did; the three new ones follow.
enum class Tab
{
    all = 0,
    project = 1,
    starred = 2,
    plugins = 3,
    presets = 4,
    sounds = 5
};
constexpr int tabCount = 6;

// The tabs left to right, as FL shows them.
constexpr std::array<Tab, tabCount> tabOrder { Tab::all, Tab::project, Tab::plugins, Tab::presets, Tab::sounds, Tab::starred };

inline int slotOf (Tab tab) noexcept
{
    for (int i = 0; i < tabCount; ++i)
        if (tabOrder[(std::size_t) i] == tab)
            return i;
    return 0;
}

// FL's tab names (the tooltip / hint; the tab itself shows only its icon, as FL's default "Icons" tab style).
inline const char* tabName (Tab tab) noexcept
{
    switch (tab)
    {
        case Tab::all: return "ALL";
        case Tab::project: return "PROJECT";
        case Tab::plugins: return "PLUGINS";
        case Tab::presets: return "PRESETS";
        case Tab::sounds: return "SOUNDS";
        case Tab::starred: return "STARRED";
    }
    return "";
}

// Which folder places a tab lists (ALL: every one the view shows; PROJECT / PLUGINS / STARRED: none).
inline bool tabShowsRoot (Tab tab, browserview::RootKind kind) noexcept
{
    using browserview::RootKind;
    switch (tab)
    {
        case Tab::all: return true;
        case Tab::presets: return kind == RootKind::flPresets || kind == RootKind::pluginPresets;
        case Tab::sounds:
            return kind == RootKind::flSamples || kind == RootKind::user || kind == RootKind::music || kind == RootKind::documents
                   || kind == RootKind::desktop || kind == RootKind::downloads || kind == RootKind::clipboardFiles;
        case Tab::project:
        case Tab::plugins:
        case Tab::starred: return false;
    }
    return false;
}

// The content filter a tab lists its folders with: its own (PRESETS: presets, SOUNDS: audio), else the Browser's.
inline browserview::ContentFilter tabFilter (Tab tab, browserview::ContentFilter browserFilter) noexcept
{
    if (tab == Tab::presets)
        return browserview::ContentFilter::presets;
    if (tab == Tab::sounds)
        return browserview::ContentFilter::audio;
    return browserFilter;
}

// --- Live's sidebar ----------------------------------------------------------------------------------------------------
enum class Group
{
    collections,
    library,
    places
};

// The sidebar's Library entries in Live's order, and the places LYNX adds.
enum class Category
{
    // Library
    all,
    sounds,      // instrument presets (FL synth / 3x Osc presets, the plugins' presets)
    drums,       // drum samples: the sample places searched for drum words (kick, snare, hat, clap, perc, drum)
    instruments, // the Plugin database's generators
    audioEffects,
    midiEffects, // FL's VFX note processors
    modulators,  // LYNX's controllers (Peak, Formula, LFO, Envelope controllers)
    maxForLive,  // none in LYNX (dimmed)
    plugIns,     // the installed VST3 / CLAP plugins
    clips,       // MIDI clips: the Scores places
    samples,     // every sample place, audio only
    grooves,     // the groove pool lives under the grid (dimmed here)
    tunings,     // none in LYNX (dimmed)
    templates,   // the user's project templates
    // Places
    packs,          // FL's sample packs
    userLibrary,    // LYNX's own places (plugin presets, Speech, Clipboard files)
    currentProject, // the project's items
    folder,         // one of the folders the Browser shows (its path in Entry::path)
    addFolder,      // "Add Folder..."
    // Collections
    collection // Entry::collection
};

struct Entry
{
    Group group = Group::library;
    Category category = Category::all;
    std::string label;     // English (the UI translates it; a folder's / collection's own name as it is)
    bool available = true; // false: dimmed, can't be picked
    std::string path;      // Category::folder
    int collection = -1;   // Category::collection
};

inline const char* categoryLabel (Category c) noexcept
{
    switch (c)
    {
        case Category::all: return "All";
        case Category::sounds: return "Sounds";
        case Category::drums: return "Drums";
        case Category::instruments: return "Instruments";
        case Category::audioEffects: return "Audio Effects";
        case Category::midiEffects: return "MIDI Effects";
        case Category::modulators: return "Modulators";
        case Category::maxForLive: return "Max for Live";
        case Category::plugIns: return "Plug-Ins";
        case Category::clips: return "Clips";
        case Category::samples: return "Samples";
        case Category::grooves: return "Grooves";
        case Category::tunings: return "Tunings";
        case Category::templates: return "Templates";
        case Category::packs: return "Packs";
        case Category::userLibrary: return "User Library";
        case Category::currentProject: return "Current Project";
        case Category::folder: return "";
        case Category::addFolder: return "Add Folder...";
        case Category::collection: return "";
    }
    return "";
}

constexpr std::array<Category, 14> libraryOrder { Category::all,         Category::sounds,       Category::drums,     Category::instruments,
                                                  Category::audioEffects, Category::midiEffects, Category::modulators, Category::maxForLive,
                                                  Category::plugIns,     Category::clips,        Category::samples,   Category::grooves,
                                                  Category::tunings,     Category::templates };

inline bool libraryAvailable (Category c) noexcept
{
    return c != Category::maxForLive && c != Category::grooves && c != Category::tunings;
}

// One folder place of the Browser (its roots, in their order).
struct Place
{
    std::string name, path;
    browserview::RootKind kind = browserview::RootKind::user;
};

// The sidebar's rows: Collections (the shown collections: `collections` = their numbers and names), Library, Places
// (Packs when FL's packs are there, User Library, Current Project, the user's own folders and the system folders the
// Browser shows, Add Folder...).
inline std::vector<Entry> sidebar (const std::vector<std::pair<int, std::string>>& collections, const std::vector<Place>& places)
{
    std::vector<Entry> out;
    for (const auto& [number, name] : collections)
    {
        Entry e;
        e.group = Group::collections;
        e.category = Category::collection;
        e.collection = number;
        e.label = name;
        out.push_back (e);
    }
    for (const auto c : libraryOrder)
    {
        Entry e;
        e.group = Group::library;
        e.category = c;
        e.label = categoryLabel (c);
        e.available = libraryAvailable (c);
        out.push_back (e);
    }
    using browserview::RootKind;
    bool packs = false;
    for (const auto& p : places)
        packs = packs || p.kind == RootKind::flSamples;
    auto place = [&out] (Category c, const std::string& label = {}, const std::string& path = {}) {
        Entry e;
        e.group = Group::places;
        e.category = c;
        e.label = label.empty() ? categoryLabel (c) : label;
        e.path = path;
        out.push_back (e);
    };
    if (packs)
        place (Category::packs);
    place (Category::userLibrary);
    place (Category::currentProject);
    for (const auto& p : places)
        if (p.kind == RootKind::user || p.kind == RootKind::documents || p.kind == RootKind::music || p.kind == RootKind::desktop
            || p.kind == RootKind::downloads || p.kind == RootKind::flDemos)
            place (Category::folder, p.name, p.path);
    place (Category::addFolder);
    return out;
}

// Which folder places a Library / Places entry lists (and under which content filter); the Plugin database and the
// project are separate (wantsPlugins / wantsProject).
inline bool categoryShowsRoot (Category c, browserview::RootKind kind) noexcept
{
    using browserview::RootKind;
    switch (c)
    {
        case Category::all: return true;
        case Category::sounds: return kind == RootKind::flPresets || kind == RootKind::pluginPresets;
        case Category::drums:
        case Category::samples:
            return kind == RootKind::flSamples || kind == RootKind::user || kind == RootKind::music || kind == RootKind::documents
                   || kind == RootKind::desktop || kind == RootKind::downloads || kind == RootKind::clipboardFiles;
        case Category::clips: return kind == RootKind::flMidi;
        case Category::packs: return kind == RootKind::flSamples;
        case Category::userLibrary: return kind == RootKind::pluginPresets || kind == RootKind::speech || kind == RootKind::clipboardFiles;
        default: return false;
    }
}

inline browserview::ContentFilter categoryFilter (Category c, browserview::ContentFilter browserFilter) noexcept
{
    switch (c)
    {
        case Category::sounds: return browserview::ContentFilter::presets;
        case Category::drums:
        case Category::samples: return browserview::ContentFilter::audio;
        case Category::clips: return browserview::ContentFilter::projects;
        default: return browserFilter;
    }
}

// The Plugin database part an entry lists: -1 none, 0 effects, 1 generators, 2 both (All).
inline int wantsPlugins (Category c) noexcept
{
    switch (c)
    {
        case Category::all: return 2;
        case Category::instruments:
        case Category::midiEffects: return 1;
        case Category::audioEffects: return 0;
        case Category::plugIns:
        case Category::modulators: return 2;
        default: return -1;
    }
}

// Which Plugin database entries an entry keeps (`group`: the entry's format group, core/PluginDbFolder.h: "LYNX",
// "FL engines", "VFX", "" = installed; `name`: its name).
inline bool keepsPlugin (Category c, bool generator, const std::string& group, const std::string& name) noexcept
{
    auto contains = [&name] (const char* word) {
        std::string n, w = word;
        for (auto ch : name)
            n += (char) (ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
        return n.find (w) != std::string::npos;
    };
    switch (c)
    {
        case Category::all: return true;
        case Category::instruments: return generator && group != "VFX";
        case Category::midiEffects: return generator && group == "VFX";
        case Category::audioEffects: return ! generator;
        case Category::plugIns: return group.empty();
        case Category::modulators: return contains ("controller") || contains ("lfo") || contains ("envelope follower");
        default: return false;
    }
}

inline bool wantsProject (Category c) noexcept { return c == Category::currentProject || c == Category::all; }

// Live's Drums: the words a drum sample's path is searched for (core/SearchQuery syntax, OR between them).
inline const char* drumsQuery() noexcept { return "kick OR snare OR hat OR clap OR perc OR drum OR tom OR cymbal OR rim OR 808"; }
} // namespace lynx::browserplaces
