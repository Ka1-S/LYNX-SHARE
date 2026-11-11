#pragma once

// A folder watcher (round 190bf, issue #104): FL Studio's Browser shows a change made to its Plugin database in Explorer
// by itself, within about a second (seen live, fl190be Q2: a folder and an entry made by the shell appeared in the next
// frame, ~1 s later; an open menu or picker was not changed, the next one opened showed it). LYNX watches its own
// database folder the same way.
//
// One background thread per watcher, sleeping on the system's change notification where there is one (Windows:
// FindFirstChangeNotification on the tree; Linux: inotify on every folder of the tree) and otherwise on a cheap poll
// (the folders' modification times, once a second: a file made, removed or renamed in a folder changes its time; the
// tree is listed again only then). After a change it waits until the folder has been quiet for `quietMs` (at most
// `maxDelayMs` after the first change) and then calls `changed` once, ON ITS OWN THREAD (the caller hands it over to
// its message thread). Nothing runs on the audio thread. No JUCE here (tests/Fl190bfTests.inc).

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lynx::folderwatch
{
struct Options
{
    int quietMs = 600;     // the debounce
    int maxDelayMs = 1500; // a folder that never stays quiet is still reported
    int pollMs = 1000;     // the fallback's period
    bool forcePoll = false; // tests: the fallback even where the system has notifications
};

class Watcher
{
public:
    // `folders`: the trees to watch (missing ones are watched for appearing, by the poll)
    Watcher (std::vector<std::string> folders, std::function<void()> changed, Options options = {});
    ~Watcher(); // stops the thread (returns once it has ended)

    Watcher (const Watcher&) = delete;
    Watcher& operator= (const Watcher&) = delete;

    // "windows", "inotify" or "poll": how it learns of changes (tests, the log)
    std::string method() const;
    int reports() const noexcept { return count.load(); } // how many times `changed` was called

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<int> count { 0 };
};

// The poll's view of a tree: every folder's path and modification time ("" for a missing tree). Changes when a file or
// folder is made, removed or renamed anywhere in it.
std::string treeStamp (const std::string& folder);
} // namespace lynx::folderwatch
