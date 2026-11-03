// PR #44 / issue #42 (macOS crash): the Browser's background search listed folders with JUCE's macOS iterator, which
// makes an NSString / NSURL of every name and throws an Objective-C exception (ending the app) for a name NSString
// rejects ("-[NSURL initFileURLWithPath:]: nil string parameter"). On macOS the Browser now lists folders with
// search::listFolderNames (readdir), which skips the names that are not well-formed UTF-8. These tests make such
// names on the test machine's disk (Linux file systems take any bytes) and check the listing: odd names skipped and
// counted, every other entry (hidden files, links, NFD names, emoji) kept, missing / unreadable folders, threads.

#include "core/SearchIndex.h"

#include <algorithm>
#include <filesystem>
#include <thread>

#ifndef _WIN32
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

TEST (pr44_utf8_validation_matches_what_nsstring_accepts)
{
    using lynx::search::isWellFormedUtf8;
    auto ok = [] (const std::string& s) { return isWellFormedUtf8 (s.data(), s.size()); };
    CHECK (ok (""));
    CHECK (ok ("kick.wav"));
    CHECK (ok ("Caf\xC3\xA9.wav"));          // precomposed é
    CHECK (ok ("Cafe\xCC\x81.wav"));         // e + combining acute (NFD, how HFS+ stores it)
    CHECK (ok ("\xE2\x82\xAC 808"));         // €
    CHECK (ok ("\xF0\x9F\x8E\xB9 keys"));    // 🎹 (4 bytes)
    CHECK (ok ("\xEF\xBF\xBF"));             // U+FFFF, last 3-byte code point
    CHECK (ok ("\xF4\x8F\xBF\xBF"));         // U+10FFFF, the last code point
    CHECK (ok ("\xED\x9F\xBF"));             // U+D7FF, just below the surrogates
    CHECK (! ok ("\xFF bad.wav"));           // never valid in UTF-8
    CHECK (! ok ("caf\xE9.wav"));            // Latin-1 é (a name from an old Windows / SMB drive)
    CHECK (! ok ("\xC0\xAF"));               // overlong '/'
    CHECK (! ok ("\xC1\xBF"));
    CHECK (! ok ("\xE0\x80\xAF"));           // overlong 3-byte
    CHECK (! ok ("\xF0\x80\x80\xAF"));       // overlong 4-byte
    CHECK (! ok ("\xED\xA0\x80"));           // U+D800 (a lone surrogate, "WTF-8")
    CHECK (! ok ("\xED\xBF\xBF"));           // U+DFFF
    CHECK (! ok ("\xF4\x90\x80\x80"));       // past U+10FFFF
    CHECK (! ok ("\xF5\x80\x80\x80"));
    CHECK (! ok ("\xE2\x82"));               // cut short
    CHECK (! ok ("\x80"));                   // a lone continuation byte
    CHECK (! ok ("ab\xC3"));                 // cut short at the end
    CHECK (! ok (std::string ("a\0b", 3)));  // NUL
}

#ifndef _WIN32
namespace pr44
{
inline bool makeFile (const std::string& path)
{
    const int fd = ::open (path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return false;
    ::close (fd);
    return true;
}
} // namespace pr44

TEST (pr44_listing_skips_names_nsstring_rejects_and_keeps_the_rest)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto dir = (fs::temp_directory_path (ec) / ("lynx_pr44_" + processTempTag())).string();
    fs::remove_all (dir, ec);
    CHECK (::mkdir (dir.c_str(), 0755) == 0);

    const std::vector<std::string> good { "kick.wav", ".hidden.wav", "Cafe\xCC\x81.wav", "Caf\xC3\xA9 2.wav",
                                          "\xF0\x9F\x8E\xB9 keys.mid", "sub folder", "link to kick.wav", "link to sub", "broken link.wav" };
    const std::vector<std::string> bad { "\xFF bad.wav", "caf\xE9.wav", "\xC0\xAF overlong.wav", "\xED\xA0\x80 surrogate.wav",
                                         "\xF4\x90\x80\x80 past.wav", "cut \xE2\x82.wav", "bad folder \xFE" };
    CHECK (pr44::makeFile (dir + "/kick.wav"));
    CHECK (pr44::makeFile (dir + "/.hidden.wav"));
    CHECK (pr44::makeFile (dir + "/Cafe\xCC\x81.wav"));
    CHECK (pr44::makeFile (dir + "/Caf\xC3\xA9 2.wav"));
    CHECK (pr44::makeFile (dir + "/\xF0\x9F\x8E\xB9 keys.mid"));
    CHECK (::mkdir ((dir + "/sub folder").c_str(), 0755) == 0);
    CHECK (::symlink ("kick.wav", (dir + "/link to kick.wav").c_str()) == 0);
    CHECK (::symlink ("sub folder", (dir + "/link to sub").c_str()) == 0);
    CHECK (::symlink ("nowhere.wav", (dir + "/broken link.wav").c_str()) == 0);
    int made = 0;
    for (const auto& b : bad)
        if (b.rfind ("bad folder", 0) == 0 ? ::mkdir ((dir + "/" + b).c_str(), 0755) == 0 : pr44::makeFile (dir + "/" + b))
            ++made;
    CHECK (made == (int) bad.size()); // the disk took every odd name (the case JUCE's macOS iterator died on)

    bool opened = false;
    int skipped = -1;
    auto names = lynx::search::listFolderNames (dir, &opened, &skipped);
    std::sort (names.begin(), names.end());
    auto want = good;
    std::sort (want.begin(), want.end());
    CHECK (opened);
    CHECK (skipped == (int) bad.size());
    CHECK (names == want); // no ".", "..", nothing invalid; the NFD name kept byte for byte
    for (const auto& n : names)
        CHECK (lynx::search::isWellFormedUtf8 (n.data(), n.size()));

    // a trailing separator is fine too
    CHECK (lynx::search::listFolderNames (dir + "/").size() == good.size());
    // an empty folder
    CHECK (lynx::search::listFolderNames (dir + "/sub folder", &opened, &skipped).empty() && opened && skipped == 0);
    // a missing folder, a file
    CHECK (lynx::search::listFolderNames (dir + "/missing", &opened).empty() && ! opened);
    CHECK (lynx::search::listFolderNames (dir + "/kick.wav", &opened).empty() && ! opened);

    // a folder that can't be read (no permission): empty, not opened, no crash (root reads anything: skip then)
    const auto locked = dir + "/locked";
    CHECK (::mkdir (locked.c_str(), 0755) == 0);
    CHECK (pr44::makeFile (locked + "/inside.wav"));
    ::chmod (locked.c_str(), 0);
    if (::geteuid() != 0)
        CHECK (lynx::search::listFolderNames (locked, &opened).empty() && ! opened);
    ::chmod (locked.c_str(), 0755);

    // the background search lists from its own thread while the Browser opens folders: four threads at once
    std::vector<std::thread> threads;
    std::atomic<int> same { 0 };
    for (int t = 0; t < 4; ++t)
        threads.emplace_back ([&] {
            for (int i = 0; i < 50; ++i)
            {
                int sk = 0;
                auto got = lynx::search::listFolderNames (dir, nullptr, &sk);
                std::sort (got.begin(), got.end());
                got.erase (std::remove (got.begin(), got.end(), std::string ("locked")), got.end());
                if (got == want && sk == (int) bad.size())
                    ++same;
            }
        });
    for (auto& t : threads)
        t.join();
    CHECK (same == 200);

    // through the search index: only well-formed names get indexed (and so reach JUCE / NSString)
    lynx::search::FileSystem disk;
    disk.folderTime = [] (const std::string&) -> std::int64_t { return 1; };
    disk.list = [] (const std::string& folder) {
        lynx::search::FolderListing l;
        bool ok = false;
        for (const auto& n : lynx::search::listFolderNames (folder, &ok))
        {
            struct stat st {};
            if (::lstat ((folder + "/" + n).c_str(), &st) == 0 && S_ISDIR (st.st_mode))
                l.subfolders.push_back (n);
            else
                l.files.push_back (n);
        }
        l.exists = ok;
        return l;
    };
    lynx::search::Index index;
    index.refresh (dir, disk);
    bool truncated = false;
    CHECK (index.find ("kick", { dir }, 100, truncated).size() == 2); // kick.wav and its link
    CHECK (index.find ("keys", { dir }, 100, truncated).size() == 1);
    for (const auto* q : { "bad", "surrogate", "overlong", "past", "cut" })
        CHECK (index.find (q, { dir }, 100, truncated).empty());
    CHECK (index.contains (dir + "/sub folder"));
    CHECK (! index.contains (dir + "/bad folder \xFE"));

    fs::remove_all (dir, ec);
}
#endif
