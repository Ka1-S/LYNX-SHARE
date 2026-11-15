#pragma once

// The Plugin database as a folder on disk (round 190ba, issue #104; docs/analise/REA_rodada190ba-104.md).
//
// FL Studio keeps its Plugin database as plain folders of small files (Documents\Image-Line\FL Studio\Presets\Plugin
// database: Effects\<category>\<plugin>.fst + .nfo, Generators\..., and Installed\Effects|Generators\<format>\ with one
// entry per plugin found by its scan). Users rearrange it in Explorer — move the entries, make folders and subfolders,
// rename, delete — and the Browser, the Plugin picker and the plugin menus show that tree.
//
// LYNX does the same with its own folder (Documents/LYNX/Plugin database):
//   Effects/<folder>/<name>.lynxplugin       the user's arrangement: what the menus, the picker and the Browser show
//   Generators/<folder>/<name>.lynxplugin
//   Installed/Effects|Generators/<format>/   every plugin LYNX has (built-in and scanned), written again by LYNX: the
//                                            place to copy an entry from after deleting it above
//   database.index                           LYNX's own notes (which plugins were given an entry once)
//   installed.index                          LYNX's own notes (the plugin list Installed was last written for: a start
//                                            whose list and Installed file names match reads none of its files)
//   Read me.txt
// An entry is a small text file (UTF-8):
//   LYNX plugin 1
//   key=<the plugin's key: its identifier string, or lynx:fx:<name> / lynx:gen:<name>>
//   name=<its own name>          format=<LYNX, FL engines, VST3, CLAP...>          kind=effect | generator
//   file=<the file name LYNX gave it, without the extension>
//   shown=<the name the user gave it in LYNX, when renamed there>
// The plugin is found by `key` alone; a file renamed in the file manager (its name is no longer `file`) is a renamed
// plugin, as in FL where the entry's file name is the plugin's name in the menus.
//
// The tree is what counts: refresh() reads it into the model the rest of LYNX uses (plugincatalog::Folders and the
// renamed names), apply() writes a change made in LYNX back to it.
//   - A built-in plugin gets an entry in its default folder (FL's category, or "LYNX SHARE") the first time LYNX sees it.
//   - A scanned plugin gets one only where the user files it (as before: unfiled ones are listed under Plugin ›).
//   - Deleting an entry takes the plugin out of the menus' folders and the picker (FL does the same; it stays in
//     Installed, in the Browser's lists and in the Plugin manager). Copying its Installed entry back restores it.
//   - Entries of plugins that are not installed any more are left alone (FL keeps them too); they come back with the plugin.
//   - The same plugin in two folders: the first path (A-Z) counts (the model holds one folder per plugin).
// No JUCE here (tests/PluginDb190baTests.inc). Message thread only.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/PluginCatalog.h"

namespace lynx::plugindb
{
inline constexpr const char* entryExtension = ".lynxplugin";
inline constexpr const char* indexFile = "database.index";

struct Plugin
{
    std::string key;           // what it is stored under (ui/QuickPick.h)
    std::string name;          // its own name (stable: English for the built-in ones)
    std::string format;        // "LYNX", "FL engines", "VFX", "VST3", "CLAP"... (the Installed sub-folder)
    bool instrument = false;
    bool builtin = false;      // one of LYNX's own (it has a default folder)
    std::string defaultFolder; // built-in: its folder until the user moves it ("Dynamics", "LYNX SHARE")
};

// What the rest of LYNX reads: the user's folders (plus `hidden`: built-in plugins whose entry the user deleted) and
// the names the user gave.
struct State
{
    plugincatalog::Folders folders;
    std::map<std::string, std::string> names;

    bool operator== (const State& o) const
    {
        return folders.of == o.folders.of && folders.effects == o.folders.effects && folders.generators == o.folders.generators
               && folders.hidden == o.folders.hidden && folders.top == o.folders.top && names == o.names;
    }
};

struct Report
{
    bool ok = false;        // the folder could be read / made
    bool migrated = false;  // the tree was made from `state` (first run)
    int created = 0, moved = 0, renamed = 0, removed = 0; // entry files
    int foldersMade = 0, foldersRemoved = 0;
};

// A name as a file / folder name on every system: \ / : * ? " < > | and control characters become '_', no leading or
// trailing spaces, no trailing dots, Windows' device names (CON, NUL, COM1...) get a '_' in front, at most 120 bytes
// (cut between characters), never empty ("Plugin").
std::string safeFileName (const std::string& name);

// The folders: `root`/Effects or `root`/Generators, plus a user folder path ("Stock/EQ"; "" = the kind's folder)
std::string kindFolder (const std::string& root, bool instruments);
std::string folderOnDisk (const std::string& root, bool instruments, const std::string& userFolder);

// Disk → model. The first time (no Effects / Generators folder with entries yet) the tree is made from `state` (the
// folders, places and names LYNX kept in its settings until now) and the built-in plugins' default folders. After
// that `state` is replaced by what the tree says; new plugins get their entries, Installed is brought up to date.
// installedLater (round 190go, the app): Installed is written on a thread of its own (a first start wrote ~1 400
// entries there on the UI thread: 2 s, a "not responding" report); entryFile / groupFolder wait for it.
Report refresh (const std::string& root, const std::vector<Plugin>& plugins, State& state, bool installedLater = false);

// Waits until Installed writes started by refresh (installedLater) are done (tests, tools).
void finishInstalledWrites();

// Tests (round 190gd): forget what this process remembers of `root` (the folders read and the Installed list written),
// as a new start of LYNX would
void forgetSession (const std::string& root);

// Model → disk, after a change made in LYNX (a move, a new / renamed / deleted folder, a rename): entries are moved,
// renamed, made or removed and folders made; a folder LYNX read at the last refresh that the model no longer has is
// removed when nothing else is left in it. Call refresh() afterwards (the tree may say it slightly differently, e.g.
// a folder name with characters a file name cannot have).
Report apply (const std::string& root, const std::vector<Plugin>& plugins, const State& state);

// The entry file of a plugin (round 190bf: the Browser's "Open parent folder", "Locate file", "Delete file..." on an
// entry, FL's System items): its place under Effects / Generators (the first A-Z), else its Installed entry; "" = none.
std::string entryFile (const std::string& root, const std::string& key, bool instrument);

// The folder on disk of one of the Browser's own groups under Effects / Generators (round 190bj, issue B4): "LYNX" =
// LYNX SHARE (its built-in plugins' default folder, root/<kind>/LYNX SHARE, when it is there; else their Installed
// entries), "FL engines" / "VFX" = their Installed format folder, "" = Installed/<kind>. A folder that is not there
// gives the nearest one that is (Installed/<kind>, else the root).
std::string groupFolder (const std::string& root, bool instruments, const std::string& group);

// One entry file's text / what it says (tests, tools)
struct Entry
{
    std::string key, name, format, file, shown;
    bool instrument = false;
};
std::string formatEntry (const Entry& entry);
bool parseEntry (const std::string& text, Entry& entry);
} // namespace lynx::plugindb
