// Round 190hd (start-up faster than FL Studio): the Browser's search index is kept up to date by a low-priority child
// process woken by file-system changes (REA 190hc action 5, after Live's Indexer). The app's own search service then
// saves nothing, runs no re-check pass after a search, and takes in the file the indexer wrote (Index::adopt).

#include "core/SearchService.h"

#include <mutex>

namespace s190hd
{
lynx::search::FileSystem diskOf (std::map<std::string, lynx::search::FolderListing>& disk, std::mutex& m)
{
    lynx::search::FileSystem f;
    f.folderTime = [&disk, &m] (const std::string& p) {
        const std::lock_guard<std::mutex> g (m);
        const auto i = disk.find (p);
        return i == disk.end() ? (std::int64_t) -1 : i->second.time;
    };
    f.list = [&disk, &m] (const std::string& p) {
        const std::lock_guard<std::mutex> g (m);
        const auto i = disk.find (p);
        return i == disk.end() ? lynx::search::FolderListing {} : i->second;
    };
    return f;
}

lynx::search::FolderListing folder (std::int64_t time, std::vector<std::string> files, std::vector<std::string> subs = {})
{
    lynx::search::FolderListing l;
    l.exists = true;
    l.time = time;
    l.files = std::move (files);
    l.subfolders = std::move (subs);
    return l;
}

std::vector<std::string> findAll (const lynx::search::Index& index, const std::string& query, const std::vector<std::string>& roots)
{
    bool truncated = false;
    return index.find (query, roots, 1000, truncated);
}
} // namespace s190hd

TEST (startup190hd_adopt_takes_the_indexer_trees_and_keeps_the_app_own_ones)
{
    std::mutex m;
    std::map<std::string, lynx::search::FolderListing> disk;
    disk["/lib"] = s190hd::folder (1, { "kick.wav" }, { "old", "drums" });
    disk["/lib/old"] = s190hd::folder (1, { "old kick.wav" });
    disk["/lib/drums"] = s190hd::folder (1, { "snare.wav" });
    disk["/mine"] = s190hd::folder (1, { "my kick.wav" });
    const auto fs = s190hd::diskOf (disk, m);

    lynx::search::Index app; // the app's index: /lib and /mine walked to the end
    app.refresh ("/lib", fs);
    app.markComplete ("/lib");
    app.refresh ("/mine", fs);
    app.markComplete ("/mine");

    // meanwhile the files change: /lib/old is removed, a kick is added to /lib/drums; the indexer walks /lib again
    {
        const std::lock_guard<std::mutex> g (m);
        disk["/lib"] = s190hd::folder (2, { "kick.wav" }, { "drums" });
        disk.erase ("/lib/old");
        disk["/lib/drums"] = s190hd::folder (2, { "snare.wav", "new kick.wav" });
        disk["/mine"] = s190hd::folder (2, {}); // (the indexer does not walk /mine: the app's tree stays)
    }
    lynx::search::Index indexer;
    indexer.refresh ("/lib", fs);
    indexer.markComplete ("/lib");
    indexer.refresh ("/other", fs); // (not complete: an interrupted walk)

    const auto before = app.generation();
    app.adopt (indexer);
    CHECK (app.generation() != before);
    CHECK (! app.contains ("/lib/old"));
    CHECK (app.isComplete ("/lib") && app.isComplete ("/mine"));
    const auto kicks = s190hd::findAll (app, "kick", { "/lib" });
    CHECK (kicks.size() == 2);
    CHECK (std::find (kicks.begin(), kicks.end(), "/lib/drums/new kick.wav") != kicks.end());
    CHECK (s190hd::findAll (app, "kick", { "/mine" }).size() == 1); // (the app's own tree, unchanged)
    // the round trip of the result is the same index
    lynx::search::Index again;
    CHECK (again.fromText (app.toText()) && again.toText() == app.toText());
}

TEST (startup190hd_service_with_an_external_indexer_saves_nothing_and_reloads_what_it_wrote)
{
    std::mutex m, fileLock;
    std::map<std::string, lynx::search::FolderListing> disk;
    disk["/lib"] = s190hd::folder (1, { "kick.wav", "snare.wav" });
    std::string file; // the index file both processes share
    int appSaves = 0;
    std::atomic<int> changed { 0 };

    lynx::search::Service::Callbacks appCb;
    appCb.load = [&] { const std::lock_guard<std::mutex> g (fileLock); return file; };
    appCb.save = [&] (const std::string&) { ++appSaves; };
    appCb.indexChanged = [&] { ++changed; };
    lynx::search::Service app (s190hd::diskOf (disk, m), appCb);
    app.setExternalIndexer (true);
    CHECK (app.hasExternalIndexer());

    std::vector<std::string> found;
    std::mutex foundLock;
    const auto sink = [&] (const lynx::search::SearchResult& r) {
        if (! r.partial)
        {
            const std::lock_guard<std::mutex> g (foundLock);
            found = r.paths;
        }
    };
    app.search ("kick", { "/lib" }, 100, sink); // (a tree never indexed: walked for the search, not saved)
    CHECK (app.waitIdle (10000));
    {
        const std::lock_guard<std::mutex> g (foundLock);
        CHECK (found.size() == 1);
    }
    CHECK (appSaves == 0);
    CHECK (app.status().checkedAt == 0); // (no re-check pass ran after the search)

    // the indexer process: a kick added, its own service walks the tree and writes the file
    {
        const std::lock_guard<std::mutex> g (m);
        disk["/lib"] = s190hd::folder (2, { "kick.wav", "snare.wav", "kick 2.wav" });
    }
    lynx::search::Service::Callbacks indexerCb;
    indexerCb.load = appCb.load;
    indexerCb.save = [&] (const std::string& text) { const std::lock_guard<std::mutex> g (fileLock); file = text; };
    {
        lynx::search::Service indexer (s190hd::diskOf (disk, m), indexerCb);
        indexer.refresh ({ "/lib" });
        CHECK (indexer.waitIdle (10000));
    }
    {
        const std::lock_guard<std::mutex> g (fileLock);
        CHECK (file.find ("kick 2.wav") != std::string::npos);
    }

    const auto changedBefore = changed.load();
    app.reload();
    CHECK (app.waitIdle (10000));
    CHECK (changed.load() == changedBefore + 1);
    app.search ("kick", { "/lib" }, 100, sink);
    CHECK (app.waitIdle (10000));
    {
        const std::lock_guard<std::mutex> g (foundLock);
        CHECK (found.size() == 2);
    }
    CHECK (appSaves == 0);

    // the indexer gone: the service saves again
    app.setExternalIndexer (false);
    app.refresh ({ "/lib" });
    CHECK (app.waitIdle (10000));
    CHECK (appSaves == 0 || appSaves == 1); // (saved only when a folder was listed again: none changed)
}
