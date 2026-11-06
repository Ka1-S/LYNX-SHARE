#include "core/ProjectSafety.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

#include "core/MissingFiles.h"
#include "core/Preferences.h"

namespace lynx::safety
{
namespace
{
const char* kindWord (BackupKind kind) { return kind == BackupKind::overwritten ? "overwritten" : "autosaved"; }

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// "_<digits>" at the end: where it starts, or npos
std::size_t versionSuffix (const std::string& s)
{
    auto i = s.size();
    while (i > 0 && std::isdigit ((unsigned char) s[i - 1]))
        --i;
    if (i == s.size() || i < 2 || s[i - 1] != '_')
        return std::string::npos;
    return i - 1;
}

// " (autosaved on dddd-dd-dd at ddhdd)" / " (overwritten on ...)", optionally followed by "_N"
bool isNewStamp (const std::string& rest)
{
    auto body = rest;
    if (const auto v = versionSuffix (body); v != std::string::npos)
        body = body.substr (0, v);

    for (auto kind : { BackupKind::autosaved, BackupKind::overwritten })
    {
        const auto head = std::string (" (") + kindWord (kind) + " on ";
        const std::string pattern = "dddd-dd-dd at ddhdd)";
        if (body.size() != head.size() + pattern.size() || body.compare (0, head.size(), head) != 0)
            continue;
        bool ok = true;
        for (std::size_t i = 0; i < pattern.size() && ok; ++i)
        {
            const auto c = body[head.size() + i];
            ok = pattern[i] == 'd' ? std::isdigit ((unsigned char) c) != 0 : c == pattern[i];
        }
        if (ok)
            return true;
    }
    return false;
}

std::vector<BackupFile> backupsOf (const std::vector<BackupFile>& files, const std::string& stem)
{
    std::vector<BackupFile> mine;
    for (const auto& f : files)
        if (isBackupOf (f.name, stem))
            mine.push_back (f);
    std::sort (mine.begin(), mine.end(), [] (const BackupFile& a, const BackupFile& b) {
        return a.time != b.time ? a.time < b.time : a.name < b.name;
    });
    return mine;
}

char separatorOf (const std::string& path) { return path.find ('\\') != std::string::npos && path.find ('/') == std::string::npos ? '\\' : '/'; }
} // namespace

std::string backupName (const std::string& stem, BackupKind kind, int year, int month, int day, int hour, int minute)
{
    char stamp[64];
    std::snprintf (stamp, sizeof (stamp), " (%s on %04d-%02d-%02d at %02dh%02d)", kindWord (kind), year, month, day, hour, minute);
    return stem + stamp;
}

bool isBackupOf (const std::string& name, const std::string& stem)
{
    if (prefs::isBackupOf (name, stem))
        return true;
    return name.size() > stem.size() && name.compare (0, stem.size(), stem) == 0 && isNewStamp (name.substr (stem.size()));
}

BackupKind backupKind (const std::string& name)
{
    return name.find (" (overwritten on ") != std::string::npos ? BackupKind::overwritten : BackupKind::autosaved;
}

std::vector<std::string> backupsToRemove (const std::vector<BackupFile>& files, const std::string& stem, int keep)
{
    const auto mine = backupsOf (files, stem);
    const auto k = (std::size_t) std::max (1, keep);
    std::vector<std::string> gone;
    for (std::size_t i = 0; i + k < mine.size(); ++i)
        gone.push_back (mine[i].name);
    return gone;
}

std::string newestBackup (const std::vector<BackupFile>& files, const std::string& stem, bool includeOverwritten)
{
    auto mine = backupsOf (files, stem);
    for (auto it = mine.rbegin(); it != mine.rend(); ++it)
        if (includeOverwritten || backupKind (it->name) != BackupKind::overwritten)
            return it->name;
    return {};
}

std::string crashRecoveryCandidate (const std::vector<BackupFile>& files, std::int64_t sessionStart)
{
    const BackupFile* best = nullptr;
    for (const auto& f : files)
        if (f.time >= sessionStart && backupKind (f.name) != BackupKind::overwritten
            && (best == nullptr || f.time > best->time || (f.time == best->time && f.name > best->name)))
            best = &f;
    return best != nullptr ? best->name : std::string();
}

std::string uniqueName (const std::string& stem, const std::function<bool (const std::string&)>& taken)
{
    if (! taken || ! taken (stem))
        return stem;
    for (int n = 2;; ++n)
        if (const auto name = stem + "_" + std::to_string (n); ! taken (name))
            return name;
}

std::string nextVersionName (const std::string& stem, const std::function<bool (const std::string&)>& taken)
{
    auto base = stem;
    long long n = 1;
    if (const auto v = versionSuffix (stem); v != std::string::npos && stem.size() - v <= 10)
    {
        n = std::stoll (stem.substr (v + 1));
        base = stem.substr (0, v);
    }
    for (;;)
    {
        const auto name = base + "_" + std::to_string (++n);
        if (! taken || ! taken (name))
            return name;
    }
}

bool isRelativePath (const std::string& path)
{
    if (path.empty())
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return false;
    if (path.size() >= 2 && path[1] == ':' && std::isalpha ((unsigned char) path[0]))
        return false;
    if (path.find ("://") != std::string::npos)
        return false;
    return true;
}

std::string joinPath (const std::string& folder, const std::string& relative)
{
    if (folder.empty())
        return relative;
    const auto sep = separatorOf (folder);
    auto rel = relative;
    for (auto& c : rel)
        if (c == '/' || c == '\\')
            c = sep;
    while (! rel.empty() && rel[0] == sep)
        rel.erase (0, 1);
    return (folder.back() == '/' || folder.back() == '\\') ? folder + rel : folder + sep + rel;
}

std::string recordingName (const std::string& projectStem, int year, int month, int day, int hour, int minute, int second,
                           const std::string& insertName)
{
    char stamp[32];
    std::snprintf (stamp, sizeof (stamp), "%04d-%02d-%02d %02d-%02d-%02d", year, month, day, hour, minute, second);
    auto name = (projectStem.empty() ? std::string ("untitled") : projectStem) + "_" + stamp;
    if (! insertName.empty())
        name += "_" + insertName;
    return name;
}

bool samePath (const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        auto x = (unsigned char) a[i], y = (unsigned char) b[i];
        if (x == '\\')
            x = '/';
        if (y == '\\')
            y = '/';
        if (std::tolower (x) != std::tolower (y))
            return false;
    }
    return true;
}

std::vector<std::string> unusedRecordings (const std::vector<std::string>& recorded, const std::vector<std::string>& referenced)
{
    std::vector<std::string> unused;
    for (const auto& file : recorded)
    {
        const auto same = [&file] (const std::string& other) { return samePath (file, other); };
        if (file.empty() || std::any_of (referenced.begin(), referenced.end(), same) || std::any_of (unused.begin(), unused.end(), same))
            continue;
        unused.push_back (file);
    }
    return unused;
}

std::vector<std::string> referencedSamples (const Song& song)
{
    std::vector<std::string> paths;
    for (const auto& m : findMissingSamples (song, [] (const std::string&) { return false; }))
        paths.push_back (m.path);
    return paths;
}

std::map<std::string, std::string> packagePlan (const Song& song, const std::string& subfolder)
{
    std::map<std::string, std::string> plan;
    std::set<std::string> used; // lower-case file names already given out
    for (const auto& path : referencedSamples (song))
    {
        const auto file = pathFileName (path);
        if (file.empty())
            continue;
        const auto dot = file.rfind ('.');
        const auto stem = dot == std::string::npos || dot == 0 ? file : file.substr (0, dot);
        const auto ext = dot == std::string::npos || dot == 0 ? std::string() : file.substr (dot);
        const auto name = uniqueName (stem, [&] (const std::string& s) { return used.count (lower (s + ext)) > 0; }) + ext;
        used.insert (lower (name));
        plan[path] = subfolder.empty() ? name : subfolder + "/" + name;
    }
    return plan;
}

int resolveProjectSamples (Song& song, const std::string& projectFolder, const std::function<bool (const std::string&)>& fileExists)
{
    if (projectFolder.empty() || ! fileExists)
        return 0;

    std::map<std::string, std::string> moves;
    for (const auto& path : referencedSamples (song))
    {
        if (isRelativePath (path))
        {
            moves[path] = joinPath (projectFolder, path); // even if it is not there: the Missing files dialog shows a full path
            continue;
        }
        if (fileExists (path))
            continue;
        const auto file = pathFileName (path);
        if (file.empty())
            continue;
        for (const char* sub : { "", "samples", "Samples", "Audio", "Audio/Recorded" })
        {
            const auto candidate = joinPath (*sub != 0 ? joinPath (projectFolder, sub) : projectFolder, file);
            if (fileExists (candidate))
            {
                moves[path] = candidate;
                break;
            }
        }
    }
    return moves.empty() ? 0 : applyRelocations (song, moves);
}
int compareVersions (const std::string& a, const std::string& b)
{
    const auto numbers = [] (const std::string& v) {
        std::vector<long> parts;
        long n = 0;
        bool inNumber = false;
        for (const auto c : v)
        {
            if (std::isdigit ((unsigned char) c))
            {
                n = std::min (n * 10 + (c - '0'), 1000000000L);
                inNumber = true;
            }
            else if (c == '.' && inNumber)
            {
                parts.push_back (n);
                n = 0;
                inNumber = false;
            }
            else
                break; // the suffix ("-alpha", " beta") is not compared
        }
        if (inNumber)
            parts.push_back (n);
        return parts;
    };
    const auto x = numbers (a), y = numbers (b);
    for (std::size_t i = 0; i < std::max (x.size(), y.size()); ++i)
    {
        const auto p = i < x.size() ? x[i] : 0L, q = i < y.size() ? y[i] : 0L;
        if (p != q)
            return p < q ? -1 : 1;
    }
    return 0;
}
} // namespace lynx::safety
