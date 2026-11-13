#include "core/MissingFiles.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace lynx
{
namespace
{
bool isSeparator (char c) { return c == '\\' || c == '/'; }

// lower-case, one kind of separator, no trailing separator: two spellings of the same path compare equal
std::string pathKey (const std::string& path)
{
    std::string key;
    key.reserve (path.size());
    for (const auto c : path)
        key.push_back (isSeparator (c) ? '/' : (char) std::tolower ((unsigned char) c));
    while (key.size() > 1 && key.back() == '/')
        key.pop_back();
    return key;
}

std::vector<std::string> pathParts (const std::string& path)
{
    std::vector<std::string> parts;
    std::string part;
    for (const auto c : pathKey (path))
    {
        if (c == '/')
        {
            if (! part.empty())
                parts.push_back (part);
            part.clear();
        }
        else
            part.push_back (c);
    }
    if (! part.empty())
        parts.push_back (part);
    return parts;
}

std::string channelUser (const ChannelDef& c)
{
    return c.instrument == InstrumentKind::audioClip ? "Audio clip: " + c.name : c.name;
}

bool usesChannelSample (InstrumentKind kind)
{
    return kind == InstrumentKind::sampler || kind == InstrumentKind::audioClip || kind == InstrumentKind::granulizer
           || kind == InstrumentKind::wavetraveller || kind == InstrumentKind::fruityslicer || kind == InstrumentKind::slicex
           || kind == InstrumentKind::threeosc || kind == InstrumentKind::soundfont;
}

// Every user sample of a channel / an effect slot, also inside parallel branches and Patcher graphs (any depth):
// fn (path, user). Templates so one walk serves `const Song&` (finding) and `Song&` (relinking).
template <typename Doc, typename Fn>
void docSamples (Doc& doc, const std::string& user, Fn& fn);

// "Rack (Patcher)" once, however deep the Patcher
std::string inPatcher (const std::string& user)
{
    const std::string tag = " (Patcher)";
    return user.size() >= tag.size() && user.compare (user.size() - tag.size(), tag.size(), tag) == 0 ? user : user + tag;
}

template <typename Slot, typename Fn>
void slotSamples (Slot& slot, const std::string& user, Fn& fn)
{
    fn (slot.samplePath, user + ": effect");
    for (auto& branch : slot.branches)
        for (auto& child : branch.chain)
            slotSamples (child, user, fn);
    docSamples (slot.patcher, inPatcher (user), fn);
}

template <typename Channel, typename Fn>
void channelSamples (Channel& c, const std::string& user, Fn& fn)
{
    if (usesChannelSample (c.instrument))
        fn (c.samplePath, user.empty() ? channelUser (c) : user);
    if (c.instrument != InstrumentKind::flkeys)
        for (auto& z : c.zones)
            fn (z.path, user.empty() ? c.name : user);
    for (auto& s : c.standIns) // Online Sessions' renders (round 168a)
        fn (s.path, (user.empty() ? c.name : user) + ": stand-in");
    for (auto& s : c.freeze.stems) // a frozen channel's audio (round 190ay)
        fn (s.path, (user.empty() ? c.name : user) + ": frozen audio");
    docSamples (c.patcher, inPatcher (user.empty() ? c.name : user), fn);
}

template <typename Doc, typename Fn>
void docSamples (Doc& doc, const std::string& user, Fn& fn)
{
    for (auto& n : doc.nodes)
    {
        if (n.generator)
            channelSamples (n.channel, user, fn);
        else
            slotSamples (n.effect, user, fn);
        for (auto& h : n.busHeads)
            if (h.head)
                slotSamples (*h.head, user, fn);
    }
}

template <typename SongT, typename Fn>
void forEachSample (SongT& song, Fn&& fn)
{
    for (auto& c : song.channels)
        channelSamples (c, {}, fn);
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
    {
        auto& track = song.mixer[t];
        for (auto& s : track.standIns)
            fn (s.path, (track.name.empty() ? "Insert " + std::to_string (t) : track.name) + ": stand-in");
        const auto name = track.name.empty() ? (t == 0 ? std::string ("Master") : "Insert " + std::to_string (t)) : track.name;
        for (auto& slot : track.effects)
            slotSamples (slot, name, fn);
    }
}
} // namespace

std::string pathFileName (const std::string& path)
{
    auto end = path.size();
    while (end > 0 && isSeparator (path[end - 1]))
        --end;
    auto start = end;
    while (start > 0 && ! isSeparator (path[start - 1]))
        --start;
    return path.substr (start, end - start);
}

void addMissingFile (std::vector<MissingFile>& list, const std::string& path, const std::string& user)
{
    if (path.empty())
        return;
    const auto key = pathKey (path);
    auto it = std::find_if (list.begin(), list.end(), [&] (const MissingFile& m) { return pathKey (m.path) == key; });
    if (it == list.end())
    {
        list.push_back ({ path, {}, {}, false });
        it = list.end() - 1;
    }
    if (! user.empty() && std::find (it->usedBy.begin(), it->usedBy.end(), user) == it->usedBy.end())
        it->usedBy.push_back (user);
}

std::vector<MissingFile> findMissingSamples (const Song& song, const std::function<bool (const std::string&)>& fileExists)
{
    std::vector<MissingFile> list;
    if (! fileExists)
        return list;

    std::map<std::string, bool> known; // each file is checked once
    auto gone = [&] (const std::string& path) {
        if (path.empty())
            return false;
        const auto key = pathKey (path);
        if (const auto it = known.find (key); it != known.end())
            return it->second;
        return known[key] = ! fileExists (path);
    };

    forEachSample (song, [&] (const std::string& path, const std::string& user) {
        if (gone (path))
            addMissingFile (list, path, user);
    });
    return list;
}

int relocationScore (const std::string& missingPath, const std::string& candidate)
{
    const auto a = pathParts (missingPath), b = pathParts (candidate);
    if (a.empty() || b.empty() || a.back() != b.back())
        return 0;
    int score = 1;
    for (std::size_t i = 2; i <= std::min (a.size(), b.size()) && a[a.size() - i] == b[b.size() - i]; ++i)
        ++score;
    return score;
}

int relocateByName (std::vector<MissingFile>& missing, const std::vector<std::string>& candidates)
{
    std::map<std::string, std::vector<const std::string*>> byName;
    for (const auto& c : candidates)
        byName[pathKey (pathFileName (c))].push_back (&c);

    int found = 0;
    for (auto& m : missing)
    {
        if (! m.found.empty() || m.skipped)
            continue;
        const auto it = byName.find (pathKey (pathFileName (m.path)));
        if (it == byName.end())
            continue;
        int best = 0;
        for (const auto* c : it->second)
            if (const auto s = relocationScore (m.path, *c); s > best)
            {
                best = s;
                m.found = *c;
            }
        found += best > 0 ? 1 : 0;
    }
    return found;
}

std::vector<std::string> searchFolderForMissing (const std::vector<MissingFile>& missing, const std::string& folder,
                                                 const std::function<void (const std::string&, const std::function<bool (const std::string&, bool)>&)>& listFolder,
                                                 std::size_t maxEntries, const std::function<bool()>& cancelled,
                                                 const std::function<void (const std::string&)>& onHit)
{
    std::set<std::string> wanted;
    for (const auto& m : missing)
        if (m.found.empty() && ! m.skipped)
            wanted.insert (pathKey (pathFileName (m.path)));

    std::vector<std::string> hits;
    if (wanted.empty() || folder.empty() || ! listFolder)
        return hits;

    std::vector<std::string> pending { folder };
    std::set<std::string> visited;
    std::size_t entries = 0;
    auto stop = false;

    while (! pending.empty() && ! stop)
    {
        const auto current = pending.back();
        pending.pop_back();
        if (! visited.insert (pathKey (current)).second)
            continue;

        listFolder (current, [&] (const std::string& path, bool isFolder) {
            if ((maxEntries > 0 && ++entries > maxEntries) || (cancelled && cancelled()))
            {
                stop = true;
                return false;
            }
            if (isFolder)
                pending.push_back (path);
            else if (wanted.count (pathKey (pathFileName (path))) != 0)
            {
                hits.push_back (path);
                if (onHit)
                    onHit (path);
            }
            return true;
        });
    }
    return hits;
}

int relocateFromSearch (std::vector<MissingFile>& missing, const std::vector<bool>& placedBySearch, const std::vector<std::string>& candidates)
{
    for (std::size_t i = 0; i < missing.size() && i < placedBySearch.size(); ++i)
        if (placedBySearch[i] && ! missing[i].skipped)
            missing[i].found.clear();
    // (relocateByName places only the files without a place that aren't skipped: these)
    return relocateByName (missing, candidates);
}

std::map<std::string, std::string> relocationMap (const std::vector<MissingFile>& missing)
{
    std::map<std::string, std::string> map;
    for (const auto& m : missing)
        if (! m.found.empty() && ! m.skipped)
            map[m.path] = m.found;
    return map;
}

std::string relocatedPath (const std::map<std::string, std::string>& relocations, const std::string& path)
{
    if (path.empty() || relocations.empty())
        return {};
    if (const auto it = relocations.find (path); it != relocations.end())
        return it->second;
    const auto key = pathKey (path);
    for (const auto& [from, to] : relocations)
        if (pathKey (from) == key)
            return to;
    return {};
}

int applyRelocations (Song& song, const std::map<std::string, std::string>& relocations)
{
    std::map<std::string, std::string> byKey;
    for (const auto& [from, to] : relocations)
        byKey[pathKey (from)] = to;

    int changed = 0;
    auto relocate = [&] (std::string& path) {
        if (path.empty())
            return;
        if (const auto it = byKey.find (pathKey (path)); it != byKey.end() && it->second != path)
        {
            path = it->second;
            ++changed;
        }
    };

    forEachSample (song, [&] (std::string& path, const std::string&) { relocate (path); });
    return changed;
}

std::vector<std::string> parseFolderList (const std::string& text)
{
    std::vector<std::string> folders;
    std::set<std::string> seen;
    std::string line;
    auto flush = [&] {
        while (! line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        const auto start = line.find_first_not_of (" \t");
        line = start == std::string::npos ? std::string() : line.substr (start);
        if (! line.empty() && seen.insert (pathKey (line)).second)
            folders.push_back (line);
        line.clear();
    };
    for (const auto c : text)
    {
        if (c == '\n')
            flush();
        else
            line.push_back (c);
    }
    flush();
    return folders;
}

std::string folderListText (const std::vector<std::string>& folders)
{
    std::string text;
    for (const auto& f : folders)
        text += f + "\n";
    return text;
}

void rememberFolder (std::vector<std::string>& folders, const std::string& folder, std::size_t limit)
{
    if (folder.empty())
        return;
    const auto key = pathKey (folder);
    folders.erase (std::remove_if (folders.begin(), folders.end(), [&] (const std::string& f) { return pathKey (f) == key; }), folders.end());
    folders.insert (folders.begin(), folder);
    if (limit > 0 && folders.size() > limit)
        folders.resize (limit);
}
} // namespace lynx
