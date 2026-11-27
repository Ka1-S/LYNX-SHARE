#pragma once

// FL Studio integration, read-only: what FL Studio already knows about the plugins on this PC and where a
// project's samples moved to. Nothing here writes to FL's files.
//
// FL keeps a "plugin database" in its user data folder (Presets\Plugin database\Installed\...), one entry per
// plugin it found when it scanned: a text .nfo (key=value lines) and an .fst preset that holds the same Fruity
// Wrapper record a project stores (plugin name, file path, VST3 class ID / CLAP id / VST2 id). Reading those
// lets LYNX map a project's plugins to the very files FL uses, without scanning. Formats: docs/FL_INTEGRATION.md.
// Does not depend on JUCE so it can be tested in isolation.

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/FlpImport.h"
#include "core/Song.h"

namespace lynx
{
struct FlPluginDbEntry
{
    std::string name, vendor;
    std::string path;   // the plugin binary FL loads (.dll, .vst3, .clap)
    std::string source; // the database file this came from (diagnostics)
    ForeignPlugin::Format format = ForeignPlugin::Format::none;
    int role = 0; // 0 unknown, 1 effect, 2 generator (FL's ps_file_type)
    std::array<std::uint8_t, 16> classId {};
    bool hasClassId = false;
    std::string clapId;
    std::uint32_t vst2Id = 0;
};

// Plugin format from a binary's extension: .vst3 → vst3, .clap → clap, .dll → vst2 (none otherwise).
ForeignPlugin::Format pluginFormatFromPath (const std::string& path);

// Lower-case letters and digits of a plugin or file name, without the usual architecture/format suffixes
// ("Serum_x64" → "serum", "Vital.vst3" stem "Vital" → "vital"): the key name matching compares.
std::string pluginNameKey (const std::string& name);

// An .nfo of FL's plugin database: "key=value" lines. Uses ps_name, ps_file_name_N / ps_file_filename_N
// (any ps_file_*name*_N whose value is a plugin binary path), ps_file_vendorname_N, ps_file_type_N
// (1 effect, 2 generator), ps_file_guid_N / ps_file_magic_N when they hold an ID. One entry per N that
// names a plugin binary; `role` falls back to the folder the file sits in (Effects / Generators).
std::vector<FlPluginDbEntry> parseFlPluginNfo (const std::string& text, const std::string& sourcePath = {});

// An .fst of FL's plugin database (a channel/effect preset): reads the Fruity Wrapper record it holds.
bool parseFlPluginFst (std::span<const std::uint8_t> bytes, FlPluginDbEntry& out);

class FlPluginDb
{
public:
    // Adds an entry; an entry for a path already known fills in what the earlier one lacked (the .nfo and the
    // .fst of the same plugin describe complementary things).
    void add (const FlPluginDbEntry& entry);

    const std::vector<FlPluginDbEntry>& entries() const noexcept { return list; }
    bool empty() const noexcept { return list.empty(); }

    // The entry for a project's plugin: by VST3 class ID, CLAP id or VST2 id; then by the saved file path; then by
    // name (preferring the project's format and the same role). Null when FL doesn't know it.
    const FlPluginDbEntry* match (const ForeignPlugin& plugin, bool instrument) const;

    // Folders holding the database's plugins (for a .vst3 bundle, the folder that contains the bundle).
    std::vector<std::string> pluginFolders() const;

private:
    std::vector<FlPluginDbEntry> list;
};

// Round 190sb (the first 15 s after start-up): FL's database kept between starts. Reading it opened every .fst and
// .nfo (2,193 files on the developer's PC, in a OneDrive folder: 0.6-0.7 s of CPU on a worker 3 s after every start).
// The cache keeps each file's entries with the size and modification time its folder listing showed; a file whose
// listing is unchanged is not opened again, any other is read as before. The database built from it is the same,
// entry for entry, in the same order (readFlPluginDatabase replays the files in the listing's order).
struct FlPluginDbFileStamp
{
    std::string path;
    std::int64_t size = 0, modified = 0; // bytes, ms since 1970
};

class FlPluginDbCache
{
public:
    // the entries kept for this file, or null when it is new or its size / time changed
    const std::vector<FlPluginDbEntry>* find (const FlPluginDbFileStamp& stamp) const;
    void put (const FlPluginDbFileStamp& stamp, std::vector<FlPluginDbEntry> entries);
    std::size_t size() const noexcept { return files.size(); }

    // the cache file's text, and back (text that is not a whole cache of this version: an empty cache)
    std::string serialise() const;
    static FlPluginDbCache parse (std::string_view text);

private:
    struct File
    {
        std::int64_t size = 0, modified = 0;
        std::vector<FlPluginDbEntry> entries;
    };
    std::map<std::string, File> files;
};

// Whether `file` (a plugin binary found in a search folder) is likely the plugin a project needs: same file as the
// project or FL's database names, or the same name key. Used to pick the few files a targeted scan examines.
bool pluginFileMatches (const std::string& file, const ForeignPlugin& plugin, const FlPluginDbEntry* entry);

// The third-party plugins a parsed .flp uses (Fruity Wrapper channels and mixer slots); `instrument` per plugin.
struct FlpPluginUse
{
    ForeignPlugin plugin;
    bool instrument = false;
};
std::vector<FlpPluginUse> flpForeignPlugins (const flp::Project& project);

// A sample an .flp references, on this PC: the path with FL's variables resolved if it exists; otherwise the same
// file re-rooted from another machine's FL user data / FL install / user profile to this PC's, or next to the
// project (the project folder, then its sub-folders named in the original path). Empty when none exists.
std::string locateFlSample (const std::string& flPath, const FlPathRoots& roots, const std::string& projectFolder,
                            const std::function<bool (const std::string&)>& fileExists);
} // namespace lynx
