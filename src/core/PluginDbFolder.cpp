#include "core/PluginDbFolder.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

namespace lynx::plugindb
{
namespace
{
namespace fs = std::filesystem;

fs::path u8path (const std::string& s) { return fs::path ((const char8_t*) s.c_str()); }
std::string utf8 (const fs::path& p)
{
    const auto s = p.generic_u8string();
    return std::string ((const char*) s.data(), s.size());
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::string readFile (const fs::path& file)
{
    std::ifstream in (file, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool writeFile (const fs::path& file, const std::string& text)
{
    std::error_code ec;
    fs::create_directories (file.parent_path(), ec);
    std::ofstream out (file, std::ios::binary | std::ios::trunc);
    out.write (text.data(), (std::streamsize) text.size());
    return (bool) out;
}

// the folder of a user path, each level as a safe name
fs::path folderPath (const fs::path& base, const std::string& userFolder)
{
    auto out = base;
    std::size_t start = 0;
    while (start <= userFolder.size())
    {
        const auto slash = userFolder.find ('/', start);
        const auto part = userFolder.substr (start, slash == std::string::npos ? std::string::npos : slash - start);
        if (! part.empty())
            out /= u8path (safeFileName (part));
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return out;
}

// a free file name in `dir` for `stem`: itself, "stem (VST3)", "stem (2)"...
std::string freeStem (const fs::path& dir, const std::string& stem, const std::string& format)
{
    std::error_code ec;
    auto taken = [&] (const std::string& s) { return fs::exists (dir / u8path (s + entryExtension), ec); };
    if (! taken (stem))
        return stem;
    if (! format.empty())
        if (const auto withFormat = safeFileName (stem + " (" + format + ")"); ! taken (withFormat))
            return withFormat;
    for (int n = 2; n < 1000; ++n)
        if (const auto numbered = safeFileName (stem + " (" + std::to_string (n) + ")"); ! taken (numbered))
            return numbered;
    return stem;
}

struct Found
{
    fs::path path;
    std::string folder, stem; // the user folder ("" = the kind's own) and the file name without the extension
    Entry entry;

    // the name it shows: the file's when it was renamed in the file manager, else the one given in LYNX ("" = its own)
    std::string shownName() const { return stem == entry.file ? entry.shown : stem; }
};

struct Tree
{
    std::map<std::string, Found> entries; // by key; the first path (A-Z) of a plugin filed twice
    std::vector<Found> others;            // the later copies
    std::set<std::string> dirs;           // the user folder paths on disk
};

// entry files already read: path → (time, size, what it says)
struct Cached
{
    std::int64_t time = 0;
    std::uintmax_t size = 0;
    bool ok = false;
    Entry entry;
};
std::map<std::string, Cached>& cache()
{
    static std::map<std::string, Cached> c;
    return c;
}

bool readEntry (const fs::path& file, Entry& entry)
{
    std::error_code ec;
    const auto time = (std::int64_t) fs::last_write_time (file, ec).time_since_epoch().count();
    const auto size = fs::file_size (file, ec);
    auto& c = cache()[utf8 (file)];
    if (ec || c.time != time || c.size != size || time == 0)
    {
        c.time = time;
        c.size = size;
        c.entry = {};
        c.ok = ! ec && size < 65536 && parseEntry (readFile (file), c.entry);
    }
    entry = c.entry;
    return c.ok;
}

bool isEntryFile (const fs::path& p) { return lower (utf8 (p.extension())) == entryExtension; }

Tree readKind (const std::string& root, bool instruments)
{
    Tree tree;
    const auto base = u8path (kindFolder (root, instruments));
    std::error_code ec;
    std::vector<std::pair<std::string, fs::path>> files; // relative path (for the order) and the file
    for (fs::recursive_directory_iterator it (base, fs::directory_options::skip_permission_denied, ec), end; it != end && ! ec; it.increment (ec))
    {
        std::error_code e2;
        const auto rel = utf8 (it->path().lexically_relative (base));
        if (it->is_directory (e2))
        {
            if (const auto folder = plugincatalog::cleanFolder (rel); ! folder.empty())
                tree.dirs.insert (folder);
        }
        else if (isEntryFile (it->path()))
            files.push_back ({ rel, it->path() });
    }
    std::sort (files.begin(), files.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [rel, path] : files)
    {
        Found f;
        if (! readEntry (path, f.entry) || f.entry.instrument != instruments)
            continue;
        f.path = path;
        f.stem = utf8 (path.stem());
        const auto slash = rel.rfind ('/');
        f.folder = slash == std::string::npos ? std::string() : plugincatalog::cleanFolder (rel.substr (0, slash));
        if (tree.entries.count (f.entry.key))
            tree.others.push_back (std::move (f));
        else
            tree.entries.emplace (f.entry.key, std::move (f));
    }
    return tree;
}

std::set<std::string> loadIndex (const std::string& root, bool& exists)
{
    std::set<std::string> seen;
    std::error_code ec;
    const auto file = u8path (root) / indexFile;
    exists = fs::is_regular_file (file, ec);
    std::istringstream in (exists ? readFile (file) : std::string());
    std::string line;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind ("seen\t", 0) == 0 && line.size() > 5)
            seen.insert (line.substr (5));
    }
    return seen;
}

void saveIndex (const std::string& root, const std::set<std::string>& seen)
{
    std::string text = "LYNX plugin database 1\n";
    for (const auto& key : seen)
        if (key.find_first_of ("\r\n") == std::string::npos)
            text += "seen\t" + key + "\n";
    const auto file = u8path (root) / indexFile;
    std::error_code ec;
    if (! fs::is_regular_file (file, ec) || readFile (file) != text)
        writeFile (file, text);
}

Entry entryOf (const Plugin& p, const std::string& stem, const std::string& shown)
{
    Entry e;
    e.key = p.key;
    e.name = p.name;
    e.format = p.format;
    e.file = stem;
    e.shown = shown;
    e.instrument = p.instrument;
    return e;
}

// a new entry of `p` in the user folder `folder`; false when it could not be written
bool createEntry (const std::string& root, const Plugin& p, const std::string& folder, const std::string& shown)
{
    const auto dir = folderPath (u8path (kindFolder (root, p.instrument)), folder);
    std::error_code ec;
    fs::create_directories (dir, ec);
    const auto stem = freeStem (dir, safeFileName (shown.empty() ? p.name : shown), p.format);
    return writeFile (dir / u8path (stem + entryExtension), formatEntry (entryOf (p, stem, shown)));
}

std::set<std::string> defaultFolders (const std::vector<Plugin>& plugins, bool instruments)
{
    std::set<std::string> out;
    for (const auto& p : plugins)
        if (p.builtin && p.instrument == instruments && ! p.defaultFolder.empty())
            out.insert (p.defaultFolder);
    return out;
}

// the folders LYNX read at the last refresh of a root ("fx:Stock/EQ", "gen:Synths"): only those may be removed by apply()
std::map<std::string, std::set<std::string>>& lastDirs()
{
    static std::map<std::string, std::set<std::string>> d;
    return d;
}
std::map<std::string, std::string>& installedSignature()
{
    static std::map<std::string, std::string> s;
    return s;
}

// The signature of the Installed folder LYNX wrote last (round 190gd): a 64-bit FNV-1a hash of the plugin list
// that made it, kept in <root>/installed.index (LYNX's own notes, like database.index)
std::string signatureHash (const std::string& signature)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (const unsigned char c : signature)
        h = (h ^ c) * 1099511628211ULL;
    char text[32];
    std::snprintf (text, sizeof (text), "%016llx", (unsigned long long) h);
    return text;
}

// Held while Installed is written (round 190go: also on the writer thread below) and while it is read
// (entryFile, groupFolder) or its memory is forgotten (forgetSession)
std::mutex& installedMutex()
{
    static std::mutex m;
    return m;
}

void writeInstalled (const std::string& root, const std::vector<Plugin>& plugins);

// Round 190go: Installed written on a thread of its own. The newest list per root waits; one write at a time.
class InstalledWriter
{
public:
    InstalledWriter()
    {
        installedMutex(); // (made first, so they outlive this writer at exit)
        installedSignature();
    }
    ~InstalledWriter()
    {
        {
            const std::lock_guard<std::mutex> lock (m);
            stop = true;
        }
        cv.notify_all();
        if (thread.joinable())
            thread.join();
    }

    void post (const std::string& root, const std::vector<Plugin>& plugins)
    {
        {
            const std::lock_guard<std::mutex> lock (m);
            pending[root] = plugins;
            if (! thread.joinable())
                thread = std::thread ([this] { run(); });
        }
        cv.notify_all();
    }

    void wait()
    {
        std::unique_lock<std::mutex> lock (m);
        cv.wait (lock, [this] { return pending.empty() && ! busy; });
    }

private:
    void run()
    {
        std::unique_lock<std::mutex> lock (m);
        for (;;)
        {
            cv.wait (lock, [this] { return stop || ! pending.empty(); });
            if (stop) // (at exit: a list still waiting is written at the next start, see writeInstalled)
                return;
            auto job = std::move (*pending.begin());
            pending.erase (pending.begin());
            busy = true;
            lock.unlock();
            {
                const std::lock_guard<std::mutex> folder (installedMutex());
                writeInstalled (job.first, job.second);
            }
            lock.lock();
            busy = false;
            cv.notify_all();
        }
    }

    std::mutex m;
    std::condition_variable cv;
    std::map<std::string, std::vector<Plugin>> pending;
    bool busy = false, stop = false;
    std::thread thread;
};

InstalledWriter& installedWriter()
{
    static InstalledWriter writer;
    return writer;
}

// Installed/Effects|Generators/<format>/: one entry per plugin, as the list says now (installedMutex held)
void writeInstalled (const std::string& root, const std::vector<Plugin>& plugins)
{
    std::string signature;
    for (const auto& p : plugins)
        signature += p.key + "\t" + p.name + "\t" + p.format + (p.instrument ? "\tg\n" : "\tf\n");
    const auto installed = u8path (root) / "Installed";
    std::error_code ec;
    if (const auto it = installedSignature().find (root); it != installedSignature().end() && it->second == signature && fs::is_directory (installed, ec))
        return;
    auto sorted = plugins;
    std::stable_sort (sorted.begin(), sorted.end(), [] (const Plugin& a, const Plugin& b) { return a.key < b.key; });
    struct Wanted
    {
        fs::path file;
        std::string text;
    };
    std::vector<Wanted> files;
    files.reserve (sorted.size());
    std::set<std::string> wanted, takenNames; // generic paths; lower-case ones for name clashes
    for (const auto& p : sorted)
    {
        const auto dir = installed / (p.instrument ? "Generators" : "Effects") / u8path (safeFileName (p.format.empty() ? std::string ("Other") : p.format));
        auto stem = safeFileName (p.name);
        for (int n = 2; ! takenNames.insert (lower (utf8 (dir / u8path (stem)))).second && n < 1000; ++n)
            stem = safeFileName (p.name + " (" + std::to_string (n) + ")");
        auto file = dir / u8path (stem + entryExtension);
        wanted.insert (utf8 (file));
        files.push_back ({ std::move (file), formatEntry (entryOf (p, stem, {})) });
    }
    // what is there now: one walk of the folder (names only: on Windows a directory listing costs one call per batch of
    // names, where checking or reading each file opens it — 1400 entries took ~1 s of every start, round 190gd)
    std::set<std::string> present;
    std::vector<fs::path> stale, dirs;
    for (fs::recursive_directory_iterator it (installed, fs::directory_options::skip_permission_denied, ec), end; it != end && ! ec; it.increment (ec))
    {
        std::error_code e2;
        if (it->is_directory (e2))
            dirs.push_back (it->path());
        else if (isEntryFile (it->path()))
        {
            if (wanted.count (utf8 (it->path())))
                present.insert (utf8 (it->path()));
            else
                stale.push_back (it->path());
        }
    }
    // the folder LYNX wrote for this very list, with every entry in place: nothing to read or write (an entry deleted or
    // renamed in the file manager is noticed by its name and written again below; an entry's text edited by hand is
    // written again when the plugin list next changes)
    const auto indexPath = u8path (root) / "installed.index";
    const auto hash = signatureHash (signature);
    if (stale.empty() && present.size() == wanted.size() && fs::is_regular_file (indexPath, ec) && readFile (indexPath) == "LYNX installed 1\n" + hash + "\n")
    {
        installedSignature()[root] = signature;
        return;
    }
    for (const auto& w : files)
        if (! present.count (utf8 (w.file)) || readFile (w.file) != w.text)
            writeFile (w.file, w.text);
    for (const auto& f : stale)
        fs::remove (f, ec);
    std::sort (dirs.begin(), dirs.end(), [] (const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
    for (const auto& d : dirs)
        fs::remove (d, ec); // (only when empty)
    writeFile (indexPath, "LYNX installed 1\n" + hash + "\n");
    installedSignature()[root] = signature;
}

void writeReadMe (const std::string& root)
{
    const auto file = u8path (root) / "Read me.txt";
    std::error_code ec;
    if (fs::exists (file, ec))
        return;
    writeFile (file,
               "LYNX SHARE: Plugin database\r\n"
               "\r\n"
               "Effects and Generators hold one small file per plugin (.lynxplugin). The folders here are the folders\r\n"
               "of the Browser's Plugin database, the Plugin picker and the plugin menus of the Mixer, the Channel Rack\r\n"
               "and the Add menu.\r\n"
               "\r\n"
               "You can arrange them here, in the file manager:\r\n"
               "- make folders and subfolders (up to 4 levels) and move the files into them;\r\n"
               "- rename a file to rename the plugin in the menus;\r\n"
               "- delete a file to take the plugin out of the menus (it is not uninstalled).\r\n"
               "\r\n"
               "Installed holds an entry for every plugin LYNX has; LYNX writes that folder again by itself. To bring a\r\n"
               "plugin back, or to add a scanned VST / CLAP plugin to a folder, copy its file from Installed into a\r\n"
               "folder under Effects or Generators.\r\n"
               "\r\n"
               "LYNX reads the folders again when a plugin menu or the Browser opens.\r\n");
}

// Model → disk for the plugins and folders of `state` (see apply()); `first`: the tree is being made, nothing is removed
Report applyImpl (const std::string& root, const std::vector<Plugin>& plugins, const State& state, std::set<std::string>& seen, bool first)
{
    Report r;
    std::error_code ec;
    for (const bool instruments : { false, true })
    {
        const auto base = u8path (kindFolder (root, instruments));
        fs::create_directories (base, ec);
        const auto tree = readKind (root, instruments);
        const auto& set = state.folders.kind (instruments);
        const auto defaults = defaultFolders (plugins, instruments);
        std::set<std::string> known;
        for (const auto& p : plugins)
        {
            if (p.instrument != instruments)
                continue;
            known.insert (p.key);
            std::optional<std::string> want;
            if (const auto it = state.folders.of.find (p.key); it != state.folders.of.end() && set.count (it->second))
                want = it->second;
            else if (state.folders.top.count (p.key))
                want = std::string(); // at the top of the kind's folder (round 190bf)
            else if (p.builtin && ! state.folders.hidden.count (p.key))
                want = p.defaultFolder;
            const auto nameIt = state.names.find (p.key);
            const auto wantName = nameIt != state.names.end() && nameIt->second != p.name ? nameIt->second : std::string();

            const auto found = tree.entries.find (p.key);
            if (found == tree.entries.end())
            {
                if (want && createEntry (root, p, *want, wantName))
                {
                    ++r.created;
                    seen.insert (p.key);
                }
                continue;
            }
            const auto& f = found->second;
            seen.insert (p.key);
            if (! want)
            {
                if (! first && fs::remove (f.path, ec))
                    ++r.removed;
                continue;
            }
            auto shown = f.shownName();
            if (shown == p.name)
                shown.clear();
            const auto rename = shown != wantName;
            // (a built-in one at the top of the kind's folder stays there: the model has no word for it)
            const auto move = f.folder != *want && ! (f.folder.empty() && p.builtin && *want == p.defaultFolder && ! state.folders.of.count (p.key));
            if (! rename && ! move)
                continue;
            const auto dir = move ? folderPath (base, *want) : f.path.parent_path();
            fs::create_directories (dir, ec);
            if (rename)
            {
                const auto wanted = safeFileName (wantName.empty() ? p.name : wantName);
                const auto same = ! move && lower (wanted) == lower (f.stem); // (only the letter case changes)
                const auto stem = same ? wanted : freeStem (dir, wanted, p.format);
                const auto target = dir / u8path (stem + entryExtension);
                if (same)
                    fs::rename (f.path, target, ec);
                if (writeFile (target, formatEntry (entryOf (p, stem, wantName))))
                {
                    if (! same)
                        fs::remove (f.path, ec);
                    ++r.renamed;
                    if (move)
                        ++r.moved;
                }
            }
            else
            {
                const auto stem = freeStem (dir, f.stem, p.format);
                auto e = f.entry;
                if (stem != f.stem) // (its name was taken there: the new file name is LYNX's, not a rename)
                {
                    e.shown = f.shownName();
                    e.file = stem;
                }
                const auto target = dir / u8path (stem + entryExtension);
                fs::rename (f.path, target, ec);
                if (! ec)
                {
                    if (stem != f.stem)
                        writeFile (target, formatEntry (e));
                    ++r.moved;
                }
            }
        }
        for (const auto& folder : set)
        {
            const auto dir = folderPath (base, folder);
            if (! fs::is_directory (dir, ec) && fs::create_directories (dir, ec))
                ++r.foldersMade;
        }
        if (first)
            continue;
        // folders taken out in LYNX: those read at the last refresh that the model no longer has (deepest first)
        std::vector<std::string> gone;
        if (const auto last = lastDirs().find (root); last != lastDirs().end())
            for (const auto& tagged : last->second)
            {
                const auto prefix = std::string (instruments ? "gen:" : "fx:");
                if (tagged.rfind (prefix, 0) != 0)
                    continue;
                const auto folder = tagged.substr (prefix.size());
                if (! set.count (folder) && ! defaults.count (folder))
                    gone.push_back (folder);
            }
        std::sort (gone.begin(), gone.end(), [] (const std::string& a, const std::string& b) { return a.size() > b.size(); });
        for (const auto& folder : gone)
        {
            const auto dir = folderPath (base, folder);
            if (! fs::is_directory (dir, ec))
                continue;
            // entries of plugins LYNX does not have now go with their folder
            std::vector<fs::path> left;
            for (fs::directory_iterator it (dir, ec), end; it != end && ! ec; it.increment (ec))
                if (Entry e; ! it->is_directory (ec) && isEntryFile (it->path()) && readEntry (it->path(), e) && ! known.count (e.key))
                    left.push_back (it->path());
            for (const auto& file : left)
                if (fs::remove (file, ec))
                    ++r.removed;
            if (fs::remove (dir, ec)) // (only when empty: other files keep it)
                ++r.foldersRemoved;
        }
    }
    r.ok = true;
    return r;
}

// Disk → `state` (the view and the names of plugins without an entry are kept)
void readState (const std::string& root, const std::vector<Plugin>& plugins, const std::set<std::string>& seen, State& state)
{
    State out;
    out.folders.view = state.folders.view;
    out.names = state.names;
    auto& dirsNow = lastDirs()[root];
    dirsNow.clear();
    for (const bool instruments : { false, true })
    {
        const auto tree = readKind (root, instruments);
        for (const auto& [key, f] : tree.entries)
            if (! f.folder.empty())
                out.folders.of[key] = f.folder;
        std::set<std::string> used; // the folders holding a plugin that is not there by default
        for (const auto& p : plugins)
        {
            if (p.instrument != instruments)
                continue;
            const auto it = tree.entries.find (p.key);
            if (it == tree.entries.end())
            {
                out.folders.of.erase (p.key);
                if (p.builtin && seen.count (p.key))
                    out.folders.hidden.insert (p.key);
                continue;
            }
            const auto& f = it->second;
            if (f.folder.empty())
                out.folders.top.insert (p.key); // a root entry (round 190bf)
            if (f.folder.empty() || (p.builtin && f.folder == p.defaultFolder))
                out.folders.of.erase (p.key);
            else
                used.insert (f.folder);
            if (const auto shown = f.shownName(); shown.empty() || shown == p.name)
                out.names.erase (p.key);
            else
                out.names[p.key] = shown;
        }
        // a default folder ("Dynamics") holding only what is there by default is not one of the user's
        const auto defaults = defaultFolders (plugins, instruments);
        auto& set = out.folders.kind (instruments);
        for (const auto& dir : tree.dirs)
        {
            dirsNow.insert ((instruments ? "gen:" : "fx:") + dir);
            auto implicit = defaults.count (dir) > 0 && ! used.count (dir);
            if (implicit)
                for (const auto& other : tree.dirs)
                    if (other.size() > dir.size() && other.compare (0, dir.size() + 1, dir + "/") == 0)
                        implicit = false;
            if (! implicit)
                set.insert (dir);
        }
        // (a default folder used by a plugin of the user's is one of the user's folders, with its parents)
        for (const auto& folder : used)
            for (auto path = folder; ! path.empty(); path = plugincatalog::parentFolder (path))
                set.insert (path);
    }
    state = std::move (out);
}
} // namespace

std::string safeFileName (const std::string& name)
{
    std::string out;
    for (const auto c : name)
    {
        const auto u = (unsigned char) c;
        out += (u < 32 || u == 127 || std::string ("\\/:*?\"<>|").find (c) != std::string::npos) ? '_' : c;
    }
    while (! out.empty() && out.front() == ' ')
        out.erase (out.begin());
    if (out.size() > 120)
    {
        auto cut = (std::size_t) 120;
        while (cut > 0 && ((unsigned char) out[cut] & 0xc0) == 0x80) // (not inside a UTF-8 character)
            --cut;
        out.resize (cut);
    }
    while (! out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    if (out.empty())
        return "Plugin";
    auto device = lower (out.substr (0, out.find ('.')));
    while (! device.empty() && device.back() == ' ')
        device.pop_back();
    const auto numbered = device.size() == 4 && (device.rfind ("com", 0) == 0 || device.rfind ("lpt", 0) == 0) && std::isdigit ((unsigned char) device[3]);
    if (device == "con" || device == "prn" || device == "aux" || device == "nul" || numbered)
        out = "_" + out;
    return out;
}

std::string kindFolder (const std::string& root, bool instruments)
{
    return utf8 (u8path (root) / (instruments ? "Generators" : "Effects"));
}

std::string folderOnDisk (const std::string& root, bool instruments, const std::string& userFolder)
{
    return utf8 (folderPath (u8path (kindFolder (root, instruments)), userFolder));
}

std::string formatEntry (const Entry& entry)
{
    auto line = [] (std::string value) {
        for (auto& c : value)
            if (c == '\r' || c == '\n')
                c = ' ';
        return value + "\n";
    };
    std::string out = "LYNX plugin 1\n";
    out += "key=" + line (entry.key);
    out += "name=" + line (entry.name);
    out += "format=" + line (entry.format);
    out += std::string ("kind=") + (entry.instrument ? "generator" : "effect") + "\n";
    out += "file=" + line (entry.file);
    if (! entry.shown.empty())
        out += "shown=" + line (entry.shown);
    return out;
}

bool parseEntry (const std::string& text, Entry& entry)
{
    entry = {};
    std::istringstream in (text.rfind ("\xef\xbb\xbf", 0) == 0 ? text.substr (3) : text);
    std::string line;
    if (! std::getline (in, line) || line.rfind ("LYNX plugin", 0) != 0)
        return false;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto tag = line.substr (0, eq);
        const auto value = line.substr (eq + 1);
        if (tag == "key") entry.key = value;
        else if (tag == "name") entry.name = value;
        else if (tag == "format") entry.format = value;
        else if (tag == "file") entry.file = value;
        else if (tag == "shown") entry.shown = value;
        else if (tag == "kind") entry.instrument = value == "generator";
    }
    return ! entry.key.empty();
}

void forgetSession (const std::string& root)
{
    finishInstalledWrites();
    lastDirs().erase (root);
    const std::lock_guard<std::mutex> lock (installedMutex());
    installedSignature().erase (root);
}

void finishInstalledWrites()
{
    installedWriter().wait();
}

Report refresh (const std::string& root, const std::vector<Plugin>& plugins, State& state, bool installedLater)
{
    Report r;
    if (root.empty())
        return r;
    std::error_code ec;
    for (const bool instruments : { false, true })
    {
        fs::create_directories (u8path (kindFolder (root, instruments)), ec);
        if (! fs::is_directory (u8path (kindFolder (root, instruments)), ec))
            return r;
    }
    bool indexed = false;
    auto seen = loadIndex (root, indexed);
    const auto before = seen;
    const auto effects = readKind (root, false), generators = readKind (root, true);
    if (! indexed && effects.entries.empty() && generators.entries.empty())
    {
        // the first time: the tree from what LYNX kept in its settings
        r = applyImpl (root, plugins, state, seen, true);
        r.migrated = true;
    }
    else
    {
        // new built-in plugins get their entry; a deleted one (seen before) stays out
        for (const auto& p : plugins)
        {
            const auto& tree = p.instrument ? generators : effects;
            if (tree.entries.count (p.key))
                seen.insert (p.key);
            else if (p.builtin && ! seen.count (p.key) && createEntry (root, p, p.defaultFolder, {}))
            {
                seen.insert (p.key);
                ++r.created;
            }
        }
    }
    readState (root, plugins, seen, state);
    if (! indexed || seen != before)
        saveIndex (root, seen);
    if (installedLater)
        installedWriter().post (root, plugins);
    else
    {
        const std::lock_guard<std::mutex> lock (installedMutex());
        writeInstalled (root, plugins);
    }
    writeReadMe (root);
    r.ok = true;
    return r;
}

std::string groupFolder (const std::string& root, bool instruments, const std::string& group)
{
    finishInstalledWrites(); // (Installed may be being written, or about to be: round 190go)
    const std::lock_guard<std::mutex> lock (installedMutex());
    std::error_code ec;
    const auto installed = u8path (root) / "Installed" / (instruments ? "Generators" : "Effects");
    if (group == "LYNX")
        if (const auto own = u8path (folderOnDisk (root, instruments, plugincatalog::lynxCategory)); fs::is_directory (own, ec))
            return utf8 (own);
    if (! group.empty())
        if (const auto format = installed / u8path (safeFileName (group)); fs::is_directory (format, ec))
            return utf8 (format);
    return fs::is_directory (installed, ec) ? utf8 (installed) : root;
}

std::string entryFile (const std::string& root, const std::string& key, bool instrument)
{
    if (root.empty() || key.empty())
        return {};
    const auto tree = readKind (root, instrument);
    if (const auto it = tree.entries.find (key); it != tree.entries.end())
        return utf8 (it->second.path);
    // not filed: its entry under Installed (FL's Browser shows the System items there too)
    finishInstalledWrites(); // (it may be being written, or about to be: round 190go)
    const std::lock_guard<std::mutex> lock (installedMutex());
    std::error_code ec;
    const auto installed = u8path (root) / "Installed" / (instrument ? "Generators" : "Effects");
    for (fs::recursive_directory_iterator it (installed, fs::directory_options::skip_permission_denied, ec), end; it != end && ! ec; it.increment (ec))
        if (Entry e; isEntryFile (it->path()) && readEntry (it->path(), e) && e.key == key && e.instrument == instrument)
            return utf8 (it->path());
    return {};
}

Report apply (const std::string& root, const std::vector<Plugin>& plugins, const State& state)
{
    Report r;
    std::error_code ec;
    if (root.empty() || ! fs::is_directory (u8path (root), ec))
        return r;
    bool indexed = false;
    auto seen = loadIndex (root, indexed);
    const auto before = seen;
    r = applyImpl (root, plugins, state, seen, false);
    if (! indexed || seen != before)
        saveIndex (root, seen);
    return r;
}
} // namespace lynx::plugindb
