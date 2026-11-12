#pragma once

// The LYNX Soundfont preset search (round 158, GitHub issue #6): every preset of every .sf2 / .sf3 under the user's
// folders (the Browser's folders + the SoundFont folders of the settings), indexed on a background thread and kept in a
// cache file between sessions. A font file is read again only when its size or modification time changed, and only
// its header chunks are read (core/SoundFont.h scanFontFile), so indexing thousands of presets takes moments.
//
// Search: the Browser's query language (core/SearchQuery.h: words AND-ed, OR, -word, "phrase", wildcards) over each
// preset's name, its font's name and file name and its category ("piano", "808", "pad", "drums"), ranked: whole-word
// name matches first, then name prefixes, then the rest; ties by font and bank:program.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/SoundFont.h"

namespace lynx::sf
{
struct IndexedFont
{
    std::string path, name;
    std::uint64_t size = 0;
    std::int64_t time = 0;
    std::vector<PresetInfo> presets;
};

struct SearchHit
{
    std::string path, fontName, presetName, category;
    int bank = 0, program = 0;
    int score = 0;
};

// The .sf2 / .sf3 files under a folder (recursive, sorted, at most `max`). `stop` (optional) is polled at every entry
// and ends the walk early; a folder or a name the system can't read is passed over, and the walk looks at no more than
// `maxEntries` entries (issue #159: a whole HDD in the Browser's folders)
std::vector<std::string> findFontFiles (const std::string& folder, std::size_t max = 20000, const std::atomic<bool>* stop = nullptr,
                                        std::size_t maxEntries = 2000000);

// Ranks and filters presets for a query (empty query: everything, by font then bank:program)
std::vector<SearchHit> searchFonts (const std::vector<IndexedFont>& fonts, const std::string& query, std::size_t max = 1000);

class PresetIndex
{
public:
    PresetIndex() = default;
    ~PresetIndex();

    static PresetIndex& shared();

    // where the index is kept between sessions ("" = memory only); read now
    void setCacheFile (const std::string& path);
    // the folders to index; starts a background refresh when they changed (or `force`)
    void setFolders (const std::vector<std::string>& folders, bool force = false);
    // a refresh now on this thread (tests)
    void refreshNow();
    // adds / updates one font right away (a font the user loaded)
    void addFont (const std::string& path);

    bool busy() const noexcept { return running.load(); }
    // increases each time the contents changed (the UI polls it)
    int generation() const noexcept { return gen.load(); }
    std::vector<IndexedFont> fonts() const;
    std::vector<SearchHit> search (const std::string& query, std::size_t max = 1000) const;
    std::size_t presetCount() const;

    // the cache's text form (one "F\t<size>\t<time>\t<path>\t<name>" line per font, "P\t<bank>\t<program>\t<name>"
    // per preset)
    static std::string toText (const std::vector<IndexedFont>& fonts);
    static std::vector<IndexedFont> fromText (const std::string& text);

private:
    void run();
    void scan (const std::vector<std::string>& folders, const std::atomic<bool>* stop);
    void save() const;

    mutable std::mutex lock;
    std::vector<IndexedFont> list;
    std::vector<std::string> folderList;
    std::string cacheFile;
    std::thread worker;
    std::atomic<bool> running { false }, stopping { false }, again { false };
    std::atomic<int> gen { 0 };
};
} // namespace lynx::sf
