#pragma once

#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// New project templates (File > New from template).
std::vector<std::string> templateNames();
Song makeTemplate (int index);

// The New project window (FL's "New project", interpreted; REA_rodada84): name, folder, time settings and credits.
struct NewProjectSetup
{
    std::string name;         // the project's name (file name)
    std::string folder;       // where it is saved; empty = not saved yet
    bool ownFolder = true;    // create "<folder>/<name>/" for it
    double bpm = 130.0;
    int beatsPerBar = 4;
    ProjectInfo info;
};

// A name usable as a file name: characters Windows refuses become '_', spaces/dots trimmed at the ends; empty → "Untitled".
std::string legalProjectName (const std::string& name);

// Where the project file goes: "<folder>/<name>/<name><ext>" with ownFolder, else "<folder>/<name><ext>"; empty
// when there is no folder. Uses '/' (the caller converts).
std::string newProjectPath (const NewProjectSetup& setup, const char* extension);

// Applies the time settings (tempo 10..522, beats per bar 1..16) and the credits to a song made from a template.
void applySetup (Song& song, const NewProjectSetup& setup);

// What a new project starts with in the mixer (Options › Mixer, round 163): how many inserts, the Master's chain and
// whether the inserts have no built-in processing ("clean mixer": the per-track EQ is bypassed and hidden).
enum class MasterChain
{
    empty,      // nothing on the Master
    clipper,    // a soft clipper only (transparent below 0 dB)
    lynxDefault // LYNX's template: a limiter (transparent up to -0.3 dB)
};

struct NewProjectDefaults
{
    static constexpr int defaultInserts = 16;
    int inserts = defaultInserts;               // 0..maxMixerTracks - 1
    MasterChain master = MasterChain::lynxDefault;
    bool cleanMixer = false;
    int playlistTracks = 16;                    // tracks the Playlist shows at least (1..500)
    // FL's Options › Project settings › "Enable automatic crossfades in new projects" (round 166e; on by default)
    bool autoCrossfades = true;
    // LYNX extra (round 190gz, issue #170): "Link Playlist tracks to mixer tracks in new projects" (off: FL's tracks)
    bool linkTracks = false;

    bool operator== (const NewProjectDefaults&) const = default;

    // "key=value" lines for ui-options.txt (keys newInserts, newMasterChain, newCleanMixer, playlistTracks,
    // newAutoCrossfades, newLinkTracks) and back;
    // unknown or missing keys keep the defaults, values are clamped.
    std::string toText() const;
    void readKey (const std::string& key, const std::string& value);
};

// Applies the defaults to a song made from a built-in template (or Song::makeDefault): the mixer gets exactly
// `inserts` inserts (channels routed past them go to the Master), the Master's effects become the chosen chain and the
// clean-mixer switch is set. Effects already on inserts are kept.
void applyNewProjectDefaults (Song& song, const NewProjectDefaults& defaults);

// The app's current choice (Options › Mixer; the main window loads it from ui-options.txt).
NewProjectDefaults& newProjectDefaults();
// A new project from a built-in template (-1 = LYNX's default song) with the current defaults applied.
Song makeNewProject (int templateIndex = -1);

// Round 190ad: a new project plays hosted plugins at FL's wrapper level (Song::flPluginLevels). A song that starts a
// new project takes it unless it already holds a hosted plugin channel made under LYNX's own law (a user's template
// saved before: it keeps its sound). Returns whether the song has FL's levels afterwards.
bool adoptFlPluginLevels (Song& song);

// A user template's name from its file name ("Trap kit.lynx" → "Trap kit").
std::string templateDisplayName (const std::string& fileName);

// The default template (issue #123; FL's Options › General › Miscellaneous › "Default template", REA_rodada186a): what
// LYNX opens at start-up and what File › New starts from, always as a new untitled project. Saved as one line of text:
// "" = LYNX's default song, "builtin:<name>" = a built-in template (templateNames), "user:<file name>" = a .lynx file
// in the user's templates folder. Anything unreadable reads as LYNX's default.
struct TemplateChoice
{
    enum class Kind
    {
        lynxDefault,
        builtIn,
        user
    };
    Kind kind = Kind::lynxDefault;
    std::string name; // the built-in template's name, or the user template's file name ("Vocal chain.lynx")

    std::string toText() const;
    static TemplateChoice parse (const std::string& text);
    bool isLynxDefault() const noexcept { return kind == Kind::lynxDefault; }
    bool operator== (const TemplateChoice&) const = default;
};

// What LYNX opens at start-up without a file to open (FL's Options › General › Miscellaneous › "Startup project",
// REA_rodada186a: Empty project / Default template / Last used project; FL's default is the default template). Saved as
// "empty" / "template" / "last"; anything else reads as the default template.
enum class StartupProject
{
    emptyProject,
    defaultTemplate,
    lastProject
};
const char* startupProjectText (StartupProject choice) noexcept;
StartupProject parseStartupProject (const std::string& text) noexcept;

// The index of a built-in template by its name (templateNames), or -1.
int builtInTemplateIndex (const std::string& name);
} // namespace lynx
