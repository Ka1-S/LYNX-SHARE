#pragma once

// What a Browser shows (round 163, user feedback "the browser always has a ton of useless folders I don't care about,
// I just need my samples/audio/soundfonts"): which of its top-level entries (roots) are visible and which kinds of
// files it lists. The main Browser and the separate Sample browser window each keep their own view, saved as
// "key=value" lines in the settings folder. No JUCE, so it can be tested in isolation.

#include <set>
#include <string>
#include <vector>

namespace lynx::browserview
{
// What the Browser lists in its folders and search results (folders are always listed).
enum class ContentFilter
{
    all,      // every file LYNX can use (audio, MIDI, projects, presets, SoundFonts)
    audio,    // audio files only
    samples,  // audio files and SoundFonts
    presets,  // channel / plugin presets and SoundFonts
    projects, // .lynx / .flp projects and MIDI files
    count
};

constexpr int contentFilterCount = (int) ContentFilter::count;
const char* contentFilterId (ContentFilter filter);    // "all", "audio", "samples", "presets", "projects"
const char* contentFilterLabel (ContentFilter filter); // English menu text (the UI translates it)
ContentFilter contentFilterFromId (const std::string& id); // unknown: all

// Whether a file named `fileName` (only its extension matters, case-insensitive) is listed under `filter`.
// Files LYNX doesn't use are never listed.
bool listed (const std::string& fileName, ContentFilter filter);

// The kinds of top-level entries a Browser has. Folder roots are identified by their path; the two entries without a
// folder have fixed ids (projectRootId, pluginsRootId).
enum class RootKind
{
    project,        // "Current project"
    pluginDatabase, // the Plugin database
    flSamples,      // FL's sample packs
    flDemos,        // FL's demo projects
    flMidi,         // FL's MIDI scores
    flPresets,      // FL's channel / plugin presets (3x Osc, synth presets)
    pluginPresets,  // LYNX's own plugin presets folder
    speech,         // speech files (.speech): FL's Data/Patches/Speech and LYNX's own (round 168b)
    clipboardFiles, // FL's "Clipboard files": what Tools › Clipboard saved (round 181b)
    documents,
    music,
    desktop,
    downloads,
    user            // a folder the user added
};

// Merged roots (round 178a): FL's Browser shows one "Scores" root for its Data\\Patches\\Scores and the user's
// Presets\\Scores, and one "Mixer presets" for both of theirs; a folder of the same name in both is one folder there
// (Scores\\Automation lists the user's files and FL's). One folder's listing: its subfolder and file names.
struct Listing
{
    std::vector<std::string> folders, files;
};

// One entry of a merged listing: `from` = the folder whose entry it is (the first that has the name), `also` = the
// later folders with a subfolder of that name (merged into it in turn; files only take the first).
struct MergedEntry
{
    std::string name;
    int from = 0;
    std::vector<int> also;
};

struct MergedListing
{
    std::vector<MergedEntry> folders, files; // in the order of first appearance
};

// The union of the listings, names compared case-insensitively (ASCII), as Windows does.
MergedListing mergeListings (const std::vector<Listing>& listings);

// A merged root's id is its first folder's path; before round 178a its other folders were roots of their own ("FL
// MIDI" = Data\\Patches\\Scores, "Mixer presets (FL)"). A view that hid one of those keeps the merged root and leaves
// that folder out of it (View::excluded); one that hid the first folder (the user's own) still hides the root, as
// its id is the same. Returns whether the view changed (then it is saved).
struct View;
bool migrateMergedRoot (View& view, const std::string& rootId, const std::vector<std::string>& formerIds);

inline constexpr const char* projectRootId = "@project";
inline constexpr const char* pluginsRootId = "@plugins";

// Whether a root is shown in the Sample browser before the user chose (only sample / audio places).
bool sampleBrowserShowsByDefault (RootKind kind);

struct View
{
    std::set<std::string> hidden; // ids of the hidden roots (a folder's path, or projectRootId / pluginsRootId)
    std::set<std::string> excluded; // folders left out of a merged root (their paths; round 178a)
    ContentFilter filter = ContentFilter::all;
    bool open = false;            // the Sample browser window was open (unused for the main Browser)
    int x = 0, y = 0, width = 0, height = 0; // the window's last bounds (0 = default)

    bool shows (const std::string& id) const { return hidden.count (id) == 0; }
    void setShown (const std::string& id, bool shown);

    std::string toText() const;
    static View fromText (const std::string& text);
    bool operator== (const View&) const = default;
};
} // namespace lynx::browserview
