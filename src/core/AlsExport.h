#pragma once

// Live set (.als) export, round 175a (N2). Format: docs/analise/ALS_FORMAT.md. Live 12 and Live 11 sets are written from
// LYNX's generated element skeletons (core/AlsSkeleton.h, tools/als_skeleton.py): Live's loader needs complete objects
// (§9), so each element starts as the skeleton of its kind and gets the song's values. The mapping:
//  - each channel with notes or a plugin → a MIDI track (its notes, pattern clip by pattern clip, as MIDI clips);
//  - each Playlist track with audio clips → an audio track (warp markers from the clip's stretch);
//  - a mixer insert shared by several tracks → a group track; an insert fed only by sends → a return track;
//    an insert with one track → that track's own mixer (volume, pan, mute, solo, send levels);
//  - third-party VST2 / VST3 instruments and effects → plugin devices with their state (byte for byte);
//  - tempo, time signature, markers (→ locators), the song loop, mixer and tempo automation.
// LYNX's own instruments and effects are not converted yet (N3): they are listed in `notes`.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
enum class AlsVersion
{
    live12,
    live11
};

struct AlsExportOptions
{
    AlsVersion version = AlsVersion::live12;
    std::string creator = "LYNX";
    // How a sample's path is written: the path the set refers to (absolute, or relative to the set's folder when
    // `relative` is set: then FileRef's RelativePath is used). Default: the path as LYNX has it, absolute.
    std::function<std::string (const std::string& path, bool& relative)> samplePath;
    // A sample's length in seconds (for the warp markers); 0 = unknown (the markers then span one second).
    std::function<double (const std::string& path)> sampleSeconds;
    // A loaded plugin's record (format, ids, state in FL's layout: PluginHost::flWrapperRecord). A plugin LYNX has
    // loaded no longer carries its `foreign` record; without this callback only unloaded records are exported.
    std::function<std::optional<ForeignPlugin> (const PluginRef& ref, bool instrument)> pluginRecord;
};

struct AlsExportResult
{
    std::vector<std::uint8_t> bytes; // the .als file (gzip)
    std::string xml;                 // the same, uncompressed (tests, debugging)
    std::vector<std::string> notes;  // what was left out or approximated (one line each)
};

AlsExportResult exportAls (const Song& song, const AlsExportOptions& options = {});
} // namespace lynx
