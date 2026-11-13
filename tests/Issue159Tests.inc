// Issue #159: searching folders for a project's missing file (GenesiSF.sf2) on a slow HDD froze LYNX and it closed.
// The window's search hands its matches over as they are found, and a later, better match replaces an earlier one; the
// SoundFont index's folder walk stops when asked, at once, and passes over folders it can't read.

#include <filesystem>
#include <fstream>
#if ! defined(_WIN32)
 #include <unistd.h>
#endif

#include "core/MissingFiles.h"
#include "core/SoundFontIndex.h"

namespace issue159
{
// a fake folder tree: folder → (name, isFolder)
using Tree = std::map<std::string, std::vector<std::pair<std::string, bool>>>;
auto lister (const Tree& tree, int* calls = nullptr)
{
    return [&tree, calls] (const std::string& folder, const std::function<bool (const std::string&, bool)>& onEntry) {
        if (calls != nullptr)
            ++*calls;
        if (const auto it = tree.find (folder); it != tree.end())
            for (const auto& [name, isFolder] : it->second)
                if (! onEntry (folder + "/" + name, isFolder))
                    return;
    };
}
} // namespace issue159

TEST (issue159_missing_file_search_streams_its_matches)
{
    using namespace lynx;
    std::vector<MissingFile> missing;
    addMissingFile (missing, "C:\\Sounds\\Genesis\\GenesiSF.sf2", "Soundfont player");
    addMissingFile (missing, "C:\\Drums\\kick.wav", "Kick");
    const issue159::Tree tree {
        { "/d", { { "Genesis", true }, { "other", true } } }, // (walked last folder first: "other", then "Genesis")
        { "/d/other", { { "GenesiSF.sf2", false }, { "snare.wav", false } } },
        { "/d/Genesis", { { "GenesiSF.sf2", false } } },
    };
    std::vector<std::string> streamed;
    const auto hits = searchFolderForMissing (missing, "/d", issue159::lister (tree), 0, {}, [&] (const std::string& h) { streamed.push_back (h); });
    CHECK (streamed == hits && hits.size() == 2); // each match told as it was found, the same as the result

    // picked again from the matches so far: the first one now, the better one (same parent folder) once it comes
    std::vector<bool> placed (missing.size(), true);
    auto files = missing;
    CHECK (relocateFromSearch (files, placed, { hits[0] }) == 1);
    CHECK (files[0].found == "/d/other/GenesiSF.sf2");
    CHECK (relocateFromSearch (files, placed, hits) == 1);
    CHECK (files[0].found == "/d/Genesis/GenesiSF.sf2" && files[1].found.empty());

    // a file the search didn't begin with (located or skipped by hand) is left alone
    files = missing;
    files[0].found = "/x/GenesiSF.sf2";
    placed[0] = false;
    relocateFromSearch (files, placed, hits);
    CHECK (files[0].found == "/x/GenesiSF.sf2");

    // cancelled: the walk ends at the next entry
    int calls = 0;
    const auto none = searchFolderForMissing (missing, "/d", issue159::lister (tree, &calls), 0, [] { return true; });
    CHECK (none.empty() && calls == 1);
}

TEST (issue159_soundfont_folder_walk_stops_and_skips_unreadable_folders)
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("lynx-iss159-" + std::to_string ((long long) std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories (root / "a" / "deep");
    fs::create_directories (root / "locked" / "inside");
    for (const auto& p : { root / "a" / "one.sf2", root / "a" / "deep" / "two.SF2", root / "a" / "not.wav", root / "locked" / "inside" / "three.sf2" })
        std::ofstream (p) << "x";

    auto all = lynx::sf::findFontFiles (root.string());
    CHECK (all.size() == 3);

   #if ! defined(_WIN32)
    // a folder the user can't read (as on another user's or the system's folders of a big drive): passed over, no throw
    fs::permissions (root / "locked", fs::perms::none);
    const auto readable = lynx::sf::findFontFiles (root.string());
    CHECK ((readable.size() == 2 || getuid() == 0)); // (root reads everything)
    fs::permissions (root / "locked", fs::perms::owner_all);
   #endif

    // asked to stop: nothing more is walked
    std::atomic<bool> stop { true };
    CHECK (lynx::sf::findFontFiles (root.string(), 20000, &stop).empty());
    // a bound on the entries looked at
    CHECK (lynx::sf::findFontFiles (root.string(), 20000, nullptr, 1).size() <= 1);

    std::error_code ec;
    fs::remove_all (root, ec);
}
