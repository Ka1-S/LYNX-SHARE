#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "engine/GeneratorCatalog.h"

namespace lynx::quickpick
{
// FL's renamed plugins (issue #11): users rename their most used plugins in the Plugin database ("1_EQ", "2_Limiter")
// and then, on an empty mixer slot (or the Channel Rack's add menu), press "1" to load that one. The names live in
// the plugin host (PluginHost::pluginNames); these helpers list LYNX's generators / effects with their keys, show the
// renamed ones first in the menus and pick them by their first character.

// One generator or effect of the Plugin database. `key` is what a user name is stored under: the plugin's identifier
// string, or "lynx:gen:<English name>" / "lynx:fx:<English name>" for LYNX's built-in ones and FL's engines.
//
// generatorEntries() is THE list of generators (issue #18): the Channel Rack's Insert ›, Replace › and "+" menus
// (addGeneratorMenu), the Browser's Plugin database, the user's plugin folders and the renamed picks all read it, so
// they never differ.
using GeneratorEntry = generators::Entry;

struct EffectEntry
{
    std::string key;
    juce::String label, detail;
    int folder = 0; // 0 LYNX, 1 FL engines, 3 installed
    std::function<std::optional<EffectSlot>()> make;
    bool available = true; // false: an FL engine without the user's FL installation (listed disabled in the menus)
    bool flPlugin = false; // one of FL's own effects (its FL category in the menus, round 170b)
};

// `withUnavailable`: also FL engines this PC cannot make (the menu lists them disabled, "(requires FL Studio)").
std::vector<GeneratorEntry> generatorEntries (AudioEngine& engine, bool withUnavailable = false);

// The Channel Rack's generator menu, the same for Insert ›, Replace › and the "+" button (issue #18): Kick … Synth,
// the built-in tools, FL engines ›, VFX ›, Patcher, LYNX Soundfont, Sampler (choose file)..., Plugin ›. `pick` gets
// the new generator; `chooseSample` (may be empty) runs for "Sampler (choose file)..."; `current` (may be null) is the
// channel being replaced, whose own generator gets a tick. `owner`: the component whose menu this is (the plugin
// picker window calls back only while it exists).
void addGeneratorMenu (juce::PopupMenu& menu, AudioEngine& engine, std::function<void (ChannelDef)> pick,
                       std::function<void()> chooseSample, const ChannelDef* current, juce::Component* owner);
std::vector<EffectEntry> effectEntries (AudioEngine& engine, bool withUnavailable = false);

// The user's name for `key`, else `original`.
juce::String shownName (AudioEngine& engine, const std::string& key, const juce::String& original);

// What a mixer slot shows (round 165c): a plugin renamed in the Plugin database ("1_EQ") shows that name, as in FL;
// otherwise the slot's own name (core/ProjectContents.h effectSlotName).
juce::String slotName (AudioEngine& engine, const EffectSlot& slot);

// Asks for a new name (empty: the original again); `done` runs after a change.
void rename (AudioEngine& engine, const std::string& key, const juce::String& original, std::function<void()> done);

// A menu item typing can pick.
struct Pick
{
    juce::String label;
    std::function<void()> action;
};

// The renamed effects / generators (sorted by name), each adding itself through `add`.
std::vector<Pick> renamedEffects (AudioEngine& engine, std::function<void (EffectSlot)> add);
std::vector<Pick> renamedGenerators (AudioEngine& engine, std::function<void (ChannelDef)> add);

// Round 166f (issue #16, FL's Plugin database folders) and 170b (Busy Works: "random order"): every built-in plugin
// and every plugin the user put in a folder (the Browser's Plugin database), under its folder's heading ("Stock",
// "Stock › EQ"), A-Z, as FL's mixer slot / Add menus: the user's folder, else FL's own category for FL's plugins
// (Delay reverb, Dynamics, Filter...), else "LYNX SHARE" (first). Installed VST / CLAP plugins in no folder are not
// listed here (the Plugin › submenu). The view (Categories / Simple / Tree) is the user's. Returns whether anything
// was added.
bool addFolderGroups (juce::PopupMenu& menu, AudioEngine& engine, std::function<void (EffectSlot)> add);
bool addFolderGroups (juce::PopupMenu& menu, AudioEngine& engine, std::function<void (ChannelDef)> add);
// FL's view switch at the bottom of those menus (radio items Categories / Simple / Tree)
void addFolderViewItems (juce::PopupMenu& menu, AudioEngine& engine);
// Whether the plugin with this database key ("lynx:fx:Facet EQ") sits in one of the user's folders (it is then listed
// under its folder's heading only, as in FL where every plugin is in one folder)
bool inUserFolder (AudioEngine& engine, const std::string& key);

// Round 190ba (issue #104): the Plugin database is a folder on disk (core/PluginDbFolder.h; FL keeps its own the same
// way and its users arrange it in Explorer). pluginDatabaseFolder(): Documents/LYNX/Plugin database, or the folder of
// one kind's user folder ("" = Effects / Generators). refreshPluginDatabase reads it into the plugin host (folders,
// places, names; the first time it is made from the settings), at most every two seconds unless `force`; the plugin
// menus, the Browser's Plugin database and the Plugin picker call it before they list anything. installPluginDatabase
// (once, at start) also makes the host write the changes made in LYNX to the folder. Nothing happens for an engine
// whose plugin host keeps no settings (snapshots, exports, tests).
juce::File pluginDatabaseFolder();
juce::File pluginDatabaseFolder (bool instruments, const std::string& userFolder);
void refreshPluginDatabase (AudioEngine& engine, bool force = false);
// Round 190bf: the entry file of a plugin in that folder (its place under Effects / Generators, else under Installed;
// a missing file when it has none), for the Browser's "System" items; and "the folder changed on disk" (the folder
// watcher, ui/MainComponent): the next plugin menu or picker reads it again (FL: menus and the picker that are already
// open stay as they are, the next one shows the change).
juce::File pluginDatabaseEntry (const std::string& key, bool instruments);
void markPluginDatabaseChanged();
// A radio item (FL's Categories / Simple / Tree, Disk recording's Format): a dot instead of a tick
void addRadioItem (juce::PopupMenu& menu, const juce::String& text, bool selected, std::function<void()> action);
void installPluginDatabase (AudioEngine& engine);

// Adds `picks` at the top of `menu` under a header ("Renamed plugins: type the first character").
void addPicks (juce::PopupMenu& menu, const std::vector<Pick>& picks);

// Shows `menu`; while it is open, typing a character that starts exactly one of `picks` runs that one (FL's menus),
// and `onKey` sees the other keys first (e.g. Delete in a slot menu; true = used, the menu closes). Any other letter
// moves the highlight to the next item starting with it in the menu (or open submenu) being typed in, as FL's plugin
// menus do (issue #64): "e" EQ, "e" again Effector...; Enter picks it.
void showMenu (juce::PopupMenu& menu, const juce::PopupMenu::Options& options, std::vector<Pick> picks,
               std::function<bool (const juce::KeyPress&)> onKey = {});
} // namespace lynx::quickpick
