#include "core/SoundFontIndex.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "core/SearchQuery.h"

namespace lynx::sf
{
namespace
{
namespace fs = std::filesystem;

fs::path u8path (const std::string& s) { return fs::path ((const char8_t*) s.c_str()); }
std::string utf8 (const fs::path& p)
{
    const auto u = p.u8string();
    return std::string ((const char*) u.data(), u.size());
}

std::string lower (std::string s)
{
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c = (char) (c - 'A' + 'a');
    return s;
}

bool isFontFile (const fs::path& p)
{
    const auto e = lower (utf8 (p.extension()));
    return e == ".sf2" || e == ".sf3";
}

bool stamp (const std::string& path, std::uint64_t& size, std::int64_t& time)
{
    std::error_code ec;
    size = (std::uint64_t) fs::file_size (u8path (path), ec);
    if (ec)
        return false;
    time = (std::int64_t) fs::last_write_time (u8path (path), ec).time_since_epoch().count();
    return ! ec;
}

// whole-word / prefix / substring rank of a word in a text (both lower case); 0 = absent
int wordRank (const std::string& text, const std::string& word)
{
    if (word.empty())
        return 0;
    int best = 0;
    for (std::size_t at = text.find (word); at != std::string::npos; at = text.find (word, at + 1))
    {
        const auto startOk = at == 0 || ! std::isalnum ((unsigned char) text[at - 1]);
        const auto end = at + word.size();
        const auto endOk = end >= text.size() || ! std::isalnum ((unsigned char) text[end]);
        best = std::max (best, startOk && endOk ? 3 : startOk ? 2 : 1);
    }
    return best;
}
} // namespace

std::vector<std::string> findFontFiles (const std::string& folder, std::size_t max, const std::atomic<bool>* stop, std::size_t maxEntries)
{
    std::vector<std::string> out;
    std::error_code ec;
    if (folder.empty() || ! fs::is_directory (u8path (folder), ec))
        return out;
    std::size_t entries = 0;
    try
    {
        for (fs::recursive_directory_iterator it (u8path (folder), fs::directory_options::skip_permission_denied, ec), end; it != end && ! ec;
             it.increment (ec))
        {
            if ((stop != nullptr && stop->load()) || (maxEntries > 0 && ++entries > maxEntries))
                break;
            if (it.depth() > 12)
            {
                it.disable_recursion_pending();
                continue;
            }
            try // (a name the system can't convert to UTF-8 is passed over)
            {
                std::error_code e2;
                if (it->is_regular_file (e2) && isFontFile (it->path()))
                {
                    out.push_back (utf8 (it->path()));
                    if (out.size() >= max)
                        break;
                }
            }
            catch (...)
            {
            }
        }
    }
    catch (...) // (the walk itself failed: what it found so far)
    {
    }
    std::sort (out.begin(), out.end());
    return out;
}

std::vector<SearchHit> searchFonts (const std::vector<IndexedFont>& fonts, const std::string& query, std::size_t max)
{
    const auto parsed = search::parse (query);
    std::vector<std::string> words;
    {
        std::istringstream in (lower (query));
        for (std::string w; in >> w;)
            if (w != "and" && w != "or" && w[0] != '-' && w != "not")
            {
                w.erase (std::remove (w.begin(), w.end(), '"'), w.end());
                words.push_back (w);
            }
    }
    std::vector<SearchHit> hits;
    for (const auto& f : fonts)
    {
        const auto stem = utf8 (u8path (f.path).stem());
        for (const auto& p : f.presets)
        {
            const auto* cat = category (p.bank, p.program, p.name);
            const auto haystack = p.name + " | " + f.name + " | " + stem + " | " + cat;
            if (! query.empty() && ! search::matches (parsed, haystack))
                continue;
            SearchHit h { f.path, f.name.empty() ? stem : f.name, p.name, cat, p.bank, p.program, 0 };
            const auto name = lower (p.name);
            for (const auto& w : words)
                h.score += 10 * wordRank (name, w) + 2 * wordRank (lower (cat), w) + wordRank (lower (h.fontName + " " + stem), w);
            hits.push_back (std::move (h));
        }
    }
    std::stable_sort (hits.begin(), hits.end(), [] (const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.fontName != b.fontName)
            return lower (a.fontName) < lower (b.fontName);
        if (a.path != b.path)
            return a.path < b.path;
        return a.bank != b.bank ? a.bank < b.bank : a.program < b.program;
    });
    if (hits.size() > max)
        hits.resize (max);
    return hits;
}

// --- the shared index ---------------------------------------------------------------------------------------------

PresetIndex::~PresetIndex()
{
    stopping = true;
    if (worker.joinable())
        worker.join();
}

PresetIndex& PresetIndex::shared()
{
    static auto* index = new PresetIndex(); // (never destroyed: its thread may be scanning at exit)
    return *index;
}

std::string PresetIndex::toText (const std::vector<IndexedFont>& fonts)
{
    const auto clean = [] (std::string s) {
        for (auto& c : s)
            if (c == '\t' || c == '\n' || c == '\r')
                c = ' ';
        return s;
    };
    std::string out = "LYNX-SF-INDEX\t1\n";
    for (const auto& f : fonts)
    {
        out += "F\t" + std::to_string (f.size) + "\t" + std::to_string (f.time) + "\t" + clean (f.path) + "\t" + clean (f.name) + "\n";
        for (const auto& p : f.presets)
            out += "P\t" + std::to_string (p.bank) + "\t" + std::to_string (p.program) + "\t" + clean (p.name) + "\n";
    }
    return out;
}

std::vector<IndexedFont> PresetIndex::fromText (const std::string& text)
{
    std::vector<IndexedFont> out;
    std::istringstream in (text);
    std::string line;
    if (! std::getline (in, line) || line.rfind ("LYNX-SF-INDEX\t", 0) != 0)
        return out;
    while (std::getline (in, line))
    {
        std::vector<std::string> f;
        std::size_t start = 0;
        for (std::size_t tab; (tab = line.find ('\t', start)) != std::string::npos; start = tab + 1)
            f.push_back (line.substr (start, tab - start));
        f.push_back (line.substr (start));
        try
        {
            if (f.size() >= 5 && f[0] == "F")
                out.push_back ({ f[3], f[4], std::stoull (f[1]), std::stoll (f[2]), {} });
            else if (f.size() >= 4 && f[0] == "P" && ! out.empty())
                out.back().presets.push_back ({ f[3], std::stoi (f[1]), std::stoi (f[2]) });
        }
        catch (...)
        {
        }
    }
    return out;
}

void PresetIndex::setCacheFile (const std::string& path)
{
    std::string text;
    {
        std::ifstream in (u8path (path), std::ios::binary);
        if (in)
            text.assign (std::istreambuf_iterator<char> (in), {});
    }
    std::lock_guard<std::mutex> g (lock);
    cacheFile = path;
    if (list.empty() && ! text.empty())
    {
        list = fromText (text);
        ++gen;
    }
}

void PresetIndex::save() const
{
    std::string file, text;
    {
        std::lock_guard<std::mutex> g (lock);
        file = cacheFile;
        text = toText (list);
    }
    if (file.empty())
        return;
    std::error_code ec;
    fs::create_directories (u8path (file).parent_path(), ec);
    std::ofstream out (u8path (file + ".tmp"), std::ios::binary);
    out << text;
    out.close();
    fs::rename (u8path (file + ".tmp"), u8path (file), ec);
}

void PresetIndex::setFolders (const std::vector<std::string>& folders, bool force)
{
    {
        std::lock_guard<std::mutex> g (lock);
        if (folders == folderList && ! force && gen.load() > 0 && ! list.empty())
            return;
        folderList = folders;
    }
    if (running.exchange (true))
    {
        again = true; // the running scan starts over when it ends
        return;
    }
    if (worker.joinable())
        worker.join();
    worker = std::thread ([this] { run(); });
}

void PresetIndex::run()
{
    for (;;)
    {
        do
        {
            again = false;
            std::vector<std::string> folders;
            {
                std::lock_guard<std::mutex> g (lock);
                folders = folderList;
            }
            try // (the index thread never ends the app: issue #159)
            {
                scan (folders, &stopping);
            }
            catch (...)
            {
            }
        } while (again && ! stopping);
        running = false;
        // a setFolders between the loop's last check and here asked for another pass (it saw `running` still set)
        if (! again || stopping || running.exchange (true))
            break;
    }
}

void PresetIndex::refreshNow()
{
    std::vector<std::string> folders;
    {
        std::lock_guard<std::mutex> g (lock);
        folders = folderList;
    }
    scan (folders, nullptr);
}

void PresetIndex::scan (const std::vector<std::string>& folders, const std::atomic<bool>* stop)
{
    std::map<std::string, IndexedFont> known;
    {
        std::lock_guard<std::mutex> g (lock);
        for (const auto& f : list)
            known[f.path] = f;
    }
    std::vector<IndexedFont> next;
    std::set<std::string> seen;
    for (const auto& folder : folders)
        for (const auto& path : findFontFiles (folder, 20000, stop))
        {
            if (stop != nullptr && stop->load())
                return;
            if (! seen.insert (path).second)
                continue;
            IndexedFont f;
            f.path = path;
            if (! stamp (path, f.size, f.time))
                continue;
            if (const auto it = known.find (path); it != known.end() && it->second.size == f.size && it->second.time == f.time)
            {
                next.push_back (it->second);
                continue;
            }
            const auto info = scanFontFile (path);
            if (! info.valid)
                continue;
            f.name = info.name.empty() ? utf8 (u8path (path).stem()) : info.name;
            f.presets = info.presets;
            next.push_back (std::move (f));
        }
    // fonts added one by one (loaded by the user) outside the folders stay
    for (const auto& [path, f] : known)
        if (seen.count (path) == 0)
        {
            std::uint64_t size = 0;
            std::int64_t time = 0;
            bool under = false;
            for (const auto& folder : folders)
                under = under || (! folder.empty() && path.rfind (folder, 0) == 0);
            if (! under && stamp (path, size, time))
                next.push_back (f);
        }
    {
        std::lock_guard<std::mutex> g (lock);
        list = std::move (next);
    }
    ++gen;
    save();
}

void PresetIndex::addFont (const std::string& path)
{
    IndexedFont f;
    f.path = path;
    if (! stamp (path, f.size, f.time))
        return;
    const auto info = scanFontFile (path);
    if (! info.valid)
        return;
    f.name = info.name.empty() ? utf8 (u8path (path).stem()) : info.name;
    f.presets = info.presets;
    {
        std::lock_guard<std::mutex> g (lock);
        list.erase (std::remove_if (list.begin(), list.end(), [&] (const IndexedFont& x) { return x.path == path; }), list.end());
        list.push_back (std::move (f));
    }
    ++gen;
    save();
}

std::vector<IndexedFont> PresetIndex::fonts() const
{
    std::lock_guard<std::mutex> g (lock);
    return list;
}

std::vector<SearchHit> PresetIndex::search (const std::string& query, std::size_t max) const
{
    std::lock_guard<std::mutex> g (lock);
    return searchFonts (list, query, max);
}

std::size_t PresetIndex::presetCount() const
{
    std::lock_guard<std::mutex> g (lock);
    std::size_t n = 0;
    for (const auto& f : list)
        n += f.presets.size();
    return n;
}
} // namespace lynx::sf
