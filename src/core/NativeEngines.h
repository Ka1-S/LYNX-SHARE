#pragma once

// FL Studio engines that LYNX reproduces, as instruments and effects a user adds to a native LYNX project (round 142,
// issue #5) instead of only meeting them inside an imported .flp. docs/analise/REA_rodada142.md.
//
// Most of these engines run on the plugin's own saved state (ChannelDef::pluginState / EffectSlot::state), and many
// also read data files of the user's FL installation at run time (wave tables, multisamples, impulses). A new instance
// therefore starts from the plugin's "Default" preset of the user's own FL installation (Data\Patches\Plugin presets),
// imported exactly like an .flp channel or mixer slot: nothing of FL is stored in LYNX. Engines that need no FL data
// start from LYNX's own defaults; the others say "requires FL Studio" in the menus when FL is not installed.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/FlpImport.h"
#include "core/Song.h"

namespace lynx::natives
{
enum class Needs
{
    nothing,   // LYNX's own default state: always available
    flStudio   // the Default preset (and data files) of the user's FL installation
};

struct InstrumentEngine
{
    InstrumentKind kind;
    const char* name;          // English name (the channel's name)
    const char* presetFolder;  // folder under Data\Patches\Plugin presets\Generators ("" = none)
    Needs needs;
    bool beta;                 // partly reproduced: shown with a "(beta)" tag
};

struct EffectEngine
{
    EffectKind kind;
    const char* presetFolder;  // folder under Data\Patches\Plugin presets\Effects ("" = none)
    Needs needs;
    bool beta;
};

// Every FL instrument engine a native project can add (the Channel Rack's "+" menu, the quick-add palette, the
// Patcher, the Browser's plugin database), in menu order
const std::vector<InstrumentEngine>& instrumentEngines();
// The FL effect engines that are not in the plain effect list (makeEffectSlot alone is not a usable instance)
const std::vector<EffectEngine>& effectEngines();
const InstrumentEngine* instrumentEngine (InstrumentKind kind);
const EffectEngine* effectEngine (EffectKind kind);

// How the engines reach the user's FL installation (the app: findFlStudioRoots + the file system; tests: a folder)
struct FlAccess
{
    FlPathRoots roots;
    std::function<bool (const std::string&)> fileExists;
    std::function<std::vector<std::uint8_t> (const std::string&)> readFile;
    std::function<std::vector<std::string> (const std::string& folder)> listPresets; // the preset files under a folder, sorted
    std::function<std::string (const std::vector<std::uint8_t>& wav, const std::string& name)> saveEmbedded; // (FL Keys' raw tables)
};

// The presets a new instance may start from, in order: Default.fst in <Data>\Patches\Plugin presets\<Generators|Effects>\<folder>,
// then the folder's other presets (the first one LYNX's engine reads exactly is used: some engines don't
// read every version of their plugin's state); empty = FL not installed, or no preset for this plugin
std::vector<std::string> presetCandidates (const std::string& presetFolder, bool generator, const FlAccess& fl);
std::string defaultPresetPath (const std::string& presetFolder, bool generator, const FlAccess& fl);

// A channel of engine `kind` from one of its plugin's preset files (.fst, or Toxic's .tbio, GMS' .gmsynth, Morphine's
// .mrp), named after the engine; nullopt when the file is not for this engine or LYNX can't read it (the preset arrows
// of a channel window, round 167o).
std::optional<ChannelDef> channelFromPresetFile (InstrumentKind kind, const std::string& path, const FlAccess& fl);

// The folder under Plugin presets\Generators that holds a channel kind's presets ("" = none): the engines above and
// MIDI Out (the ◀ ▶ arrows of a channel window, round 167o).
std::string presetFolderOf (InstrumentKind kind);

// Channel `current` with preset file `path` loaded, as FL does it from the ◀ ▶ arrows: the instrument's state comes
// from the preset (MIDI Out keeps its channel and port), the channel keeps its own settings (mixer insert, volume, pan,
// Misc tab, colour...) and takes the preset's name. nullopt when the file can't be read for that kind.
std::optional<ChannelDef> withPreset (const ChannelDef& current, const std::string& path, const std::string& presetName, const FlAccess& fl);

// Whether a new instance can be made on this PC
bool available (const InstrumentEngine& e, const FlAccess& fl);
bool available (const EffectEngine& e, const FlAccess& fl);

// A new channel / slot of that engine, or nullopt when it can't be made here (FL not installed, preset unreadable,
// the engine's data files missing). The channel keeps its default name; mixerTrack is left to the caller.
std::optional<ChannelDef> makeChannel (InstrumentKind kind, const FlAccess& fl);
std::optional<EffectSlot> makeSlot (EffectKind kind, const FlAccess& fl, double bpm = 120.0);

// An Autogun channel for the editor pictures (--instrumentshot, round 169h): the Autogun channel of FL's own
// "System/Render tests/Autogun.flp", imported with its Magic Dust data (Autogun has no preset files: its patches come
// from its own patch numbers, so it is not offered in the Add menus); nullopt without FL
std::optional<ChannelDef> autogunShotChannel (const FlAccess& fl);

// Any effect kind: the FL engines through makeSlot, the others makeEffectSlot (nullopt only for an FL engine that
// can't be made here)
std::optional<EffectSlot> newEffect (EffectKind kind, const FlAccess& fl, double bpm = 120.0);

// The effect's name in the menus, with " (beta)" for partly reproduced engines
std::string effectMenuName (EffectKind kind);
std::string instrumentMenuName (const InstrumentEngine& e);
} // namespace lynx::natives
