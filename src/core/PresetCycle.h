#pragma once

// The ◀ ▶ preset arrows of a plugin window, as FL Studio's plugin wrapper does them (round 167o, issue #24; FL's
// TPluginForm NextPrevPreset, FLEngine 0x12b6c70, measured in FL: docs/analise/REA_rodada167o.md).
//
// FL's list is the plugin's own programs followed by the .fst files of its preset folder
// "Plugin presets\<Generators|Effects>\<plugin>\" (FL's factory folder and the user's folder merged, subfolders
// included), sorted by FLEngine 0x662a40: the files of a folder before its subfolders, names compared with numbers
// as numbers. Without a current preset ▶ goes to the first and ◀ to the last; then both wrap around.

#include <string>
#include <vector>

#include "core/FlpImport.h"

namespace lynx::presetcycle
{
// FL's order of two preset paths relative to the preset folder ("Bass/Acid.fst", '/' or '\\'): true when `a` comes
// first. The folder part is compared first (a folder's own files before its subfolders), then the name without the
// extension ("Default" before "Default (pad)"), with numbers compared as numbers ("Preset 2" before "Preset 10").
bool flLess (const std::string& a, const std::string& b);

// Natural, case-insensitive comparison (<0, 0, >0) of two names: digit runs compare by value, other characters
// without letter case (ties broken by the exact bytes).
int compareNatural (const std::string& a, const std::string& b);

struct Preset
{
    std::string path;     // full path of the file
    std::string relative; // relative to the preset folder, '/' separated ("SFX/Waterphone.fst")
    std::string name;     // file name without the extension ("Waterphone")
};

// The .fst presets under `folders` (recursive), merged by relative path (a later folder's file replaces an earlier
// one's: the user's own over FL's factory), in FL's order. Missing folders are skipped.
std::vector<Preset> listPresets (const std::vector<std::string>& folders, const std::string& extension = ".fst");

// FL's preset folders of a plugin: <Data>\Patches\Plugin presets\<Generators|Effects>\<folder> and
// <User data>\Presets\Plugin presets\<Generators|Effects>\<folder> (those that are set).
std::vector<std::string> pluginPresetFolders (const FlPathRoots& roots, bool generator, const std::string& folder);

// The next index of the arrows: `current` < 0 (no preset yet): ▶ → 0, ◀ → count − 1; else (current + delta) mod
// count. −1 when the list is empty.
int step (int current, int delta, int count) noexcept;

// FL's hint after a step: "<index + 1> / <count>".
std::string positionText (int index, int count);

// The index of the preset named `name` (case-insensitive), -1 when none: the arrows of a reopened window continue
// from the preset the channel was named after.
int indexOfName (const std::vector<Preset>& list, const std::string& name);
} // namespace lynx::presetcycle
