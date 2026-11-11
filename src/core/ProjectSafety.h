#pragma once

// Project safety logic (REA round 118, FL Studio's backup/autosave/"save new version"/project-data features
// interpreted): backup file names and their rotation, incremental version names, which backup to offer after a crash,
// and the sample paths a project carries (packing them next to the project, finding them again after the project
// moved). No JUCE, so it can be tested in isolation: file-system questions go through callbacks.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::safety
{
// --- backups --------------------------------------------------------------------------------------------------------
// Why a backup was written: the periodic autosave, or the previous file kept when a save replaced it.
enum class BackupKind
{
    autosaved,
    overwritten
};

// "Song (autosaved on 2026-10-20 at 14h19)" / "Song (overwritten on 2026-10-20 at 14h19)" (without extension).
// Minutes only; when that name is taken, uniqueName() adds "_2", "_3"...
std::string backupName (const std::string& stem, BackupKind kind, int year, int month, int day, int hour, int minute);

// True when `name` (no extension) is a backup of project `stem`: this module's names (with an optional "_N"), and the
// older LYNX names (prefs::backupName "Song (2026-10-16 19.50.03)" and the plain "Song").
bool isBackupOf (const std::string& name, const std::string& stem);
// The kind of a backup name made by backupName (older names count as autosaves).
BackupKind backupKind (const std::string& name);

// A file in the backup folder: its name without extension and its modification time (any monotonic unit).
struct BackupFile
{
    std::string name;
    std::int64_t time = 0;
};

// The backups of `stem` that go when only the `keep` newest are kept (by time, then name; oldest first).
std::vector<std::string> backupsToRemove (const std::vector<BackupFile>& files, const std::string& stem, int keep);
// The newest backup of `stem` ("" if none); `includeOverwritten` false skips the "overwritten" ones.
std::string newestBackup (const std::vector<BackupFile>& files, const std::string& stem, bool includeOverwritten = true);

// The file to offer after LYNX did not close properly: the newest file written since the session started
// (`sessionStart`, same unit as the times), not an "overwritten" copy (that is older than the saved project).
// "" when nothing was written in that session.
std::string crashRecoveryCandidate (const std::vector<BackupFile>& files, std::int64_t sessionStart);

// --- names ----------------------------------------------------------------------------------------------------------
// `stem` if `taken` says it is free, else stem + "_2", "_3"... (FL's rule for backup and collected file names).
std::string uniqueName (const std::string& stem, const std::function<bool (const std::string&)>& taken);

// "Save new version": "Song" -> "Song_2", "Song_2" -> "Song_3", skipping names `taken` reports (a trailing "_<digits>"
// is the version number; "Mix_v2" has none, so it becomes "Mix_v2_2").
std::string nextVersionName (const std::string& stem, const std::function<bool (const std::string&)>& taken);

// --- recordings (REA round 134) -------------------------------------------------------------------------------------
// FL's name for a recorded take: "<project>_<yyyy-mm-dd hh-mm-ss>_<insert name>" ("untitled" without a project name;
// no "_<insert>" part when `insertName` is empty). Without extension; the caller makes it unique (uniqueName) and legal.
std::string recordingName (const std::string& projectStem, int year, int month, int day, int hour, int minute, int second,
                           const std::string& insertName);

// The folder a project records into, relative to the project file's folder (FL's project data folder "Audio\Recorded").
inline constexpr const char* recordedSubfolder = "Audio/Recorded";

// Paths compared the way the file system would (separators unified; case folded, as on Windows and macOS).
bool samePath (const std::string& a, const std::string& b);

// Recordings made in this session that the project will no longer use: the files of `recorded` that `referenced`
// (the samples of the project as it will stay on disk) does not contain, once each, in recording order.
std::vector<std::string> unusedRecordings (const std::vector<std::string>& recorded, const std::vector<std::string>& referenced);

// --- sample paths ---------------------------------------------------------------------------------------------------
// True for paths that are relative ("samples/kick.wav"): no drive letter, no leading separator.
bool isRelativePath (const std::string& path);
// `folder` joined with `relative` using the folder's own separator style.
std::string joinPath (const std::string& folder, const std::string& relative);

// Every user sample file `song` references (channel samples, zones, effect samples), once each, in project order.
std::vector<std::string> referencedSamples (const Song& song);

// Points the song's samples to their place in a package: each referenced file -> "samples/<file name>", the file names
// made unique ("kick.wav", "kick_2.wav") when two different files share one. Returns original path -> package path.
std::map<std::string, std::string> packagePlan (const Song& song, const std::string& subfolder = "samples");

// After loading a project from `projectFolder`: relative sample paths become paths inside that folder, and samples
// that are missing where the project saved them are looked for next to the project (the folder itself, then its
// "samples", "Samples", "Audio" and "Audio/Recorded" subfolders) by file name. Returns how many references changed.
int resolveProjectSamples (Song& song, const std::string& projectFolder, const std::function<bool (const std::string&)>& fileExists);
// --- versions (pain point 37, round 177d) ---------------------------------------------------------------------------
// Compares two LYNX version strings ("0.1.25-alpha", "0.1.9"): the dotted numbers in order, so "0.1.10" is newer than
// "0.1.9"; a suffix after the numbers ("-alpha") does not count. < 0: a is older, 0: same, > 0: a is newer.
// An empty or unreadable string counts as 0.0.0.
int compareVersions (const std::string& a, const std::string& b);
// True when a project written by `savedBy` may hold things this build (`running`) does not know, and so would drop if
// the project were saved here. False for projects from before "savedBy" was written (savedBy empty).
inline bool savedByNewerVersion (const std::string& savedBy, const std::string& running)
{
    return ! savedBy.empty() && compareVersions (savedBy, running) > 0;
}

// --- the Versions panel (round 190ap) -------------------------------------------------------------------------------
// One list of a project's saved versions ("Save new version": Song, Song_2, Song_3... next to the project) and of its
// backups (the timed autosaves and the copies a save replaced, in the backup folder). Nothing here changes how either
// is named or stored; the optional labels live in a text file beside the files, never inside them.
namespace versions
{
// The name the versions of one project share: "Song_3" -> "Song" (`number` 3), "Song" -> "Song" (1), "Mix_v2" -> "Mix_v2".
std::string familyOf (const std::string& stem, int* number = nullptr);

// A file as the panel needs it: its name without extension, modification time (ms) and size (bytes).
struct FileInfo
{
    std::string name;
    std::int64_t time = 0;
    std::int64_t size = 0;
};

enum class Kind
{
    version,     // a file of the family next to the project
    autosaved,   // a timed backup
    overwritten  // the file a save replaced
};

struct Entry
{
    std::string name;       // without extension
    Kind kind = Kind::version;
    int number = 0;         // versions: 1 for the plain name, else the "_N"
    std::int64_t time = 0, size = 0;
    bool current = false;   // the open project's own file
    std::string label;      // the user's note ("" = none)
    bool operator== (const Entry&) const = default;
};

using Labels = std::map<std::string, std::string>; // file name without extension -> label

// The list, newest first: the files of `projectFolder` that belong to the family of `currentStem`, and the files of
// `backupFolder` that are backups of any member of that family (or of `currentStem` itself, for a project never
// saved: its backups are filed under its name). `versionLabels` / `backupLabels`: the labels of each folder.
std::vector<Entry> list (const std::vector<FileInfo>& projectFolder, const std::vector<FileInfo>& backupFolder, const std::string& currentStem,
                         bool currentIsSaved, const Labels& versionLabels = {}, const Labels& backupLabels = {});

// The labels file of a family in a folder: "<family>.version-labels.txt" (one per folder: the project's own folder
// for its versions, the backup folder for its backups).
std::string labelsFileName (const std::string& family);
// A label as it is kept: one line, no tabs, trimmed, at most 80 characters.
std::string cleanLabel (const std::string& text);
// The file's text: "<file name><TAB><label>" lines (UTF-8), sorted by name; empty labels are left out.
std::string labelsToText (const Labels& labels);
Labels labelsFromText (const std::string& text);
// `labels` without the entries whose file is gone (`exists`), so a rotated backup does not leave its label behind.
Labels pruned (const Labels& labels, const std::function<bool (const std::string&)>& exists);

// A labelled backup is one the user wants to find again: the rotation leaves it alone. `files` without them.
std::vector<BackupFile> withoutLabelled (const std::vector<BackupFile>& files, const Labels& labels);

// "Open as a copy": the name the copy opens under ("Song_2 (copy)"; a backup's stamp is dropped:
// "Song (autosaved on 2026-10-20 at 14h19)" -> "Song (copy)").
std::string copyName (const Entry& entry, const std::string& currentStem);

// How long ago, in the two units the panel says ("just now" is 0 minutes): whole minutes under an hour, whole hours
// under two days, else whole days. `unit`: 0 minutes, 1 hours, 2 days.
int agoValue (std::int64_t seconds, int& unit) noexcept;
} // namespace versions

// --- opening files from outside LYNX (D#92, round 180a) ------------------------------------------------------------
// `name` with `extension` (".lynx") added unless it already ends with it, in any case. A dot inside the name stays:
// "beat.v2" -> "beat.v2.lynx", never "beat.lynx" (which could be another project).
std::string withProjectExtension (const std::string& name, const std::string& extension);

// Whether a start-up argument names a file rather than an option ("--lang", macOS's "-psn_0_123").
bool isFileArgument (const std::string& argument);

// The files in the string the operating system hands to a running application (macOS: one string, a path with spaces
// in double quotes, a quote inside a path as \"). Options are left out.
std::vector<std::string> filesInCommandLine (const std::string& commandLine);
} // namespace lynx::safety
