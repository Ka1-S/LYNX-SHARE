#pragma once

// Missing files of an opened project (like FL Studio's "missing samples" message, interpreted): which samples a
// project uses that aren't on this PC, where they might be now (a search by file name in folders the user picks),
// and how to point the project at the found copies. Does not depend on JUCE so it can be tested in isolation:
// the file system is reached through the callbacks the caller passes.

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// One file a project references that doesn't exist on this PC.
struct MissingFile
{
    std::string path;                // as the project saved it (FL's variables already resolved)
    std::vector<std::string> usedBy; // "Kick", "Audio clip: Vocals", "Insert 3: Newtone" (English, for display)
    std::string found;               // the copy picked on this PC ("" = still missing)
    bool skipped = false;            // the user chose to leave it missing
};

// A plugin a project uses that isn't installed (or wasn't scanned) here, and what LYNX put in its place.
struct MissingPlugin
{
    std::string name;
    std::vector<std::string> usedBy;
    std::string replacement; // English, for display ("Built-in synth", "Removed from the mixer"...); may hold "{0}"
    std::string replacementArg; // what "{0}" stands for (a LYNX substitute's effect name), not translated
};

// The last component of a path saved on Windows or elsewhere ('\\' and '/' both separate).
std::string pathFileName (const std::string& path);

// Adds `user` to the entry for `path` (one entry per path, case-insensitive on the separators and letters).
void addMissingFile (std::vector<MissingFile>& list, const std::string& path, const std::string& user);

// The user samples a .lynx song references that `fileExists` says are gone: channel samples (sampler, audio clip,
// Granulizer, 3x Osc custom wave), sampler zones (not FL Keys' installation wavetables) and effect samples
// (Newtone/Newtime), also inside parallel containers and Patcher graphs. FL Studio installation data some engines keep in samplePath (DLLs, wave folders) is left out.
std::vector<MissingFile> findMissingSamples (const Song& song, const std::function<bool (const std::string&)>& fileExists);

// How well a found file matches a missing path: 0 = different file name; otherwise 1 + the number of trailing folder
// names they share ("Drums\Kicks\kick.wav" vs ".../Kicks/kick.wav" = 2). Case-insensitive.
int relocationScore (const std::string& missingPath, const std::string& candidate);

// For each missing file not yet found or skipped, picks the best candidate by `relocationScore` (ties: the first).
// Returns how many were newly found.
int relocateByName (std::vector<MissingFile>& missing, const std::vector<std::string>& candidates);

// Lists, recursively, the files under `folder` whose names match a missing file (the search a dialog runs).
// `listFolder(folder, onEntry)` reports each entry as (path, isFolder); `onEntry` returning false stops the walk.
// `maxEntries` bounds the walk (0 = no bound); `cancelled` is polled between entries. `onHit` is told each match as
// soon as it is found (issue #159: the window shows and can apply them while a slow drive is still being walked).
std::vector<std::string> searchFolderForMissing (const std::vector<MissingFile>& missing, const std::string& folder,
                                                 const std::function<void (const std::string&, const std::function<bool (const std::string&, bool)>&)>& listFolder,
                                                 std::size_t maxEntries = 0, const std::function<bool()>& cancelled = {},
                                                 const std::function<void (const std::string&)>& onHit = {});

// Picks again, from every candidate found so far, the files a running search placed (`placedBySearch[i]`: file i had no
// place and wasn't skipped when the search began; a later, better match replaces an earlier one, as if the whole search
// had ended). Returns how many of them have a place now.
int relocateFromSearch (std::vector<MissingFile>& missing, const std::vector<bool>& placedBySearch, const std::vector<std::string>& candidates);

// original path → found path, for the files that were found.
std::map<std::string, std::string> relocationMap (const std::vector<MissingFile>& missing);

// The new place of `path` in a relocation map (same file whatever the case or separators), "" when not relocated.
std::string relocatedPath (const std::map<std::string, std::string>& relocations, const std::string& path);

// Points every reference to a relocated file at its new place (channel samples, zones, effect samples, also inside
// parallel containers and Patcher graphs); a channel whose sampler had lost its sample keeps its kind. Returns how many references changed.
int applyRelocations (Song& song, const std::map<std::string, std::string>& relocations);

// The search folders list as saved (one folder per line): duplicates and empty lines dropped, order kept.
std::vector<std::string> parseFolderList (const std::string& text);
std::string folderListText (const std::vector<std::string>& folders);
// Adds `folder` at the front (most recent first), keeping at most `limit` entries.
void rememberFolder (std::vector<std::string>& folders, const std::string& folder, std::size_t limit = 20);
} // namespace lynx
