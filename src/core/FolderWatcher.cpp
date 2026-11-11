#include "core/FolderWatcher.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#elif defined(__linux__)
 #include <poll.h>
 #include <sys/eventfd.h>
 #include <sys/inotify.h>
 #include <unistd.h>
#endif

namespace lynx::folderwatch
{
namespace
{
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

fs::path u8path (const std::string& s) { return fs::path ((const char8_t*) s.c_str()); }

// every folder of a tree with its modification time (the tree itself first); empty when it is missing
std::map<std::string, std::int64_t> folderTimes (const std::string& folder)
{
    std::map<std::string, std::int64_t> out;
    std::error_code ec;
    const auto base = u8path (folder);
    if (! fs::is_directory (base, ec))
        return out;
    auto add = [&out] (const fs::path& p) {
        std::error_code e;
        const auto t = fs::last_write_time (p, e);
        const auto s = p.generic_u8string();
        out[std::string ((const char*) s.data(), s.size())] = e ? 0 : (std::int64_t) t.time_since_epoch().count();
    };
    add (base);
    for (fs::recursive_directory_iterator it (base, fs::directory_options::skip_permission_denied, ec), end; it != end && ! ec; it.increment (ec))
    {
        std::error_code e;
        if (it->is_directory (e) && ! it->is_symlink (e))
            add (it->path());
    }
    return out;
}

// The fallback: the folders' times, checked once per period; a tree is listed again only when one of them changed
class Poll
{
public:
    explicit Poll (const std::vector<std::string>& f) : folders (f)
    {
        for (const auto& folder : folders)
            known.push_back (folderTimes (folder));
    }

    bool changed()
    {
        bool any = false;
        for (std::size_t i = 0; i < folders.size(); ++i)
        {
            bool differs = false;
            std::error_code ec;
            if (known[i].empty())
                differs = fs::is_directory (u8path (folders[i]), ec); // (it appeared)
            else
                for (const auto& [path, time] : known[i])
                {
                    const auto t = fs::last_write_time (u8path (path), ec);
                    if (ec || (std::int64_t) t.time_since_epoch().count() != time)
                    {
                        differs = true;
                        break;
                    }
                }
            if (differs)
            {
                known[i] = folderTimes (folders[i]);
                any = true;
            }
        }
        return any;
    }

private:
    std::vector<std::string> folders;
    std::vector<std::map<std::string, std::int64_t>> known;
};
} // namespace

std::string treeStamp (const std::string& folder)
{
    std::string out;
    for (const auto& [path, time] : folderTimes (folder))
        out += path + "\t" + std::to_string (time) + "\n";
    return out;
}

struct Watcher::Impl
{
    std::vector<std::string> folders;
    std::function<void()> changed;
    Options options;
    std::atomic<bool> stopping { false };
    std::string how = "poll";
    std::mutex lock;
    std::condition_variable wake; // (the poll's sleep)
    std::thread thread;
#if defined(_WIN32)
    HANDLE stopEvent = nullptr;
    std::vector<HANDLE> handles;
#elif defined(__linux__)
    int inotifyFd = -1, stopFd = -1;
    std::map<int, std::string> watches;
#endif
    std::unique_ptr<Poll> poll;
    std::atomic<int>* count = nullptr;

    void openNative()
    {
        if (options.forcePoll)
            return;
#if defined(_WIN32)
        stopEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
        if (stopEvent == nullptr)
            return;
        for (const auto& folder : folders)
        {
            const auto wide = u8path (folder).wstring();
            const auto h = FindFirstChangeNotificationW (wide.c_str(), TRUE,
                                                         FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
            if (h == INVALID_HANDLE_VALUE)
            {
                closeNative(); // (a missing tree: the poll sees it appear)
                return;
            }
            handles.push_back (h);
        }
        how = "windows";
#elif defined(__linux__)
        inotifyFd = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC);
        stopFd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (inotifyFd < 0 || stopFd < 0)
        {
            closeNative();
            return;
        }
        for (const auto& folder : folders)
        {
            std::error_code ec;
            if (! fs::is_directory (u8path (folder), ec))
            {
                closeNative();
                return;
            }
        }
        addWatches();
        how = "inotify";
#endif
    }

#if defined(__linux__)
    // a watch on every folder of the trees (inotify does not look into subfolders); adding one twice is harmless
    void addWatches()
    {
        for (const auto& folder : folders)
            for (const auto& [path, time] : folderTimes (folder))
            {
                const auto wd = inotify_add_watch (inotifyFd, path.c_str(),
                                                   IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF);
                if (wd >= 0)
                    watches[wd] = path;
            }
    }
#endif

    void closeNative()
    {
#if defined(_WIN32)
        for (auto h : handles)
            FindCloseChangeNotification (h);
        handles.clear();
        if (stopEvent != nullptr)
            CloseHandle (stopEvent);
        stopEvent = nullptr;
#elif defined(__linux__)
        if (inotifyFd >= 0)
            close (inotifyFd);
        if (stopFd >= 0)
            close (stopFd);
        inotifyFd = stopFd = -1;
        watches.clear();
#endif
    }

    // sleeps up to `ms` (or until stopped); true when a change was seen
    bool wait (int ms)
    {
#if defined(_WIN32)
        if (! handles.empty())
        {
            std::vector<HANDLE> all (handles);
            all.push_back (stopEvent);
            const auto r = WaitForMultipleObjects ((DWORD) all.size(), all.data(), FALSE, (DWORD) std::max (0, ms));
            if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + (DWORD) handles.size())
            {
                FindNextChangeNotification (handles[r - WAIT_OBJECT_0]);
                return true;
            }
            return false;
        }
#elif defined(__linux__)
        if (inotifyFd >= 0)
        {
            pollfd fds[2] = { { inotifyFd, POLLIN, 0 }, { stopFd, POLLIN, 0 } };
            if (::poll (fds, 2, std::max (0, ms)) <= 0 || (fds[0].revents & POLLIN) == 0)
                return false;
            alignas (inotify_event) char buffer[8192];
            bool any = false, newFolder = false;
            for (;;)
            {
                const auto n = read (inotifyFd, buffer, sizeof (buffer));
                if (n <= 0)
                    break;
                for (char* p = buffer; p < buffer + n;)
                {
                    const auto* e = (const inotify_event*) p;
                    if ((e->mask & IN_IGNORED) != 0)
                        watches.erase (e->wd);
                    else
                        any = true;
                    if ((e->mask & IN_ISDIR) != 0 && (e->mask & (IN_CREATE | IN_MOVED_TO)) != 0)
                        newFolder = true;
                    if ((e->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0)
                        newFolder = true; // (the tree itself: watched again if it comes back)
                    p += sizeof (inotify_event) + e->len;
                }
            }
            if (newFolder)
                addWatches();
            return any;
        }
#endif
        // the poll: sleep (woken early by stop), then compare the folders' times
        {
            std::unique_lock<std::mutex> l (lock);
            wake.wait_for (l, std::chrono::milliseconds (std::max (1, std::min (ms, options.pollMs))), [this] { return stopping.load(); });
        }
        if (stopping)
            return false;
        return poll->changed();
    }

    void run()
    {
        bool pending = false;
        auto first = Clock::now(), last = first;
        auto elapsed = [] (Clock::time_point since) {
            return (int) std::chrono::duration_cast<std::chrono::milliseconds> (Clock::now() - since).count();
        };
        while (! stopping)
        {
            const auto timeout = pending ? std::max (1, std::min (options.quietMs - elapsed (last), options.maxDelayMs - elapsed (first)))
                                         : (how == "poll" ? options.pollMs : 60000);
            const auto seen = wait (timeout);
            if (stopping)
                break;
            if (seen)
            {
                if (! pending)
                    first = Clock::now();
                pending = true;
                last = Clock::now();
            }
            if (pending && (elapsed (last) >= options.quietMs || elapsed (first) >= options.maxDelayMs))
            {
                pending = false;
                if (how == "poll")
                    poll = std::make_unique<Poll> (folders); // (what was seen is the new normal)
                count->fetch_add (1);
                if (changed)
                    changed();
            }
        }
    }
};

Watcher::Watcher (std::vector<std::string> folders, std::function<void()> changed, Options options) : impl (std::make_unique<Impl>())
{
    impl->folders = std::move (folders);
    impl->changed = std::move (changed);
    impl->options = options;
    impl->count = &count;
    impl->openNative();
    if (impl->how == "poll")
        impl->poll = std::make_unique<Poll> (impl->folders);
    impl->thread = std::thread ([i = impl.get()] { i->run(); });
}

Watcher::~Watcher()
{
    impl->stopping = true;
    {
        const std::lock_guard<std::mutex> l (impl->lock);
        impl->wake.notify_all();
    }
#if defined(_WIN32)
    if (impl->stopEvent != nullptr)
        SetEvent (impl->stopEvent);
#elif defined(__linux__)
    if (impl->stopFd >= 0)
    {
        const std::uint64_t one = 1;
        [[maybe_unused]] const auto written = write (impl->stopFd, &one, sizeof (one));
    }
#endif
    if (impl->thread.joinable())
        impl->thread.join();
    impl->closeNative();
}

std::string Watcher::method() const
{
    return impl->how;
}
} // namespace lynx::folderwatch
