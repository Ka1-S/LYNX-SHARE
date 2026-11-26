#include "ui/LynxWindows.h"

#include <juce_events/juce_events.h>

#include "platform/Paths.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <cerrno>
 #include <csignal>
 #include <fcntl.h>
 #include <poll.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace lynx::windows
{
namespace
{
constexpr int maxLine = 480; // one write of a message stays whole (PIPE_BUF is 512 on macOS)

juce::File entryFile (int pid)
{
    return folder().getChildFile (juce::String (pid) + ".json");
}

juce::var toVar (const Entry& e)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("pid", e.pid);
    o->setProperty ("project", e.project);
    o->setProperty ("inbox", e.inbox);
    o->setProperty ("deviceType", e.deviceType);
    o->setProperty ("deviceName", e.deviceName);
    o->setProperty ("started", e.started);
    return juce::var (o.release());
}

Entry fromVar (const juce::var& v)
{
    Entry e;
    e.pid = (int) v.getProperty ("pid", 0);
    e.project = v["project"].toString();
    e.inbox = v["inbox"].toString();
    e.deviceType = v["deviceType"].toString();
    e.deviceName = v["deviceName"].toString();
    e.started = (double) v.getProperty ("started", 0.0);
    return e;
}

bool same (const Entry& a, const Entry& b)
{
    return a.pid == b.pid && a.project == b.project && a.inbox == b.inbox && a.deviceType == b.deviceType && a.deviceName == b.deviceName;
}

bool alive (int pid)
{
    if (pid <= 0)
        return false;
#if JUCE_WINDOWS
    const auto h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid);
    if (h == nullptr)
        return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const auto live = GetExitCodeProcess (h, &code) && code == STILL_ACTIVE;
    CloseHandle (h);
    return live;
#else
    return ::kill ((pid_t) pid, 0) == 0 || errno == EPERM;
#endif
}

juce::String inboxName (int pid)
{
#if JUCE_WINDOWS
    return "lynx-share-" + juce::String (pid);
#else
    auto dir = juce::SystemStats::getEnvironmentVariable ("XDG_RUNTIME_DIR", {});
    if (dir.isEmpty() || ! juce::File::isAbsolutePath (dir) || ! juce::File (dir).isDirectory())
        dir = "/tmp";
    return juce::File (dir).getChildFile ("lynx-share-" + juce::String ((int) ::getuid()) + "-" + juce::String (pid) + ".inbox").getFullPathName();
#endif
}

void deliver (const juce::String& line);

// the window's inbox: a thread that waits for whole lines and hands them to the message thread
class Inbox final : private juce::Thread
{
public:
    explicit Inbox (juce::String where) : juce::Thread ("LYNX windows inbox"), path (std::move (where)) {}

    ~Inbox() override
    {
        signalThreadShouldExit();
#if JUCE_WINDOWS
        pipe.close(); // (cancels the wait for a writer)
#else
        if (wake[1] >= 0)
            [[maybe_unused]] const auto n = ::write (wake[1], "x", 1);
#endif
        stopThread (2000);
#if ! JUCE_WINDOWS
        for (const auto h : { fd, wake[0], wake[1] })
            if (h >= 0)
                ::close (h);
        ::unlink (path.toRawUTF8());
#endif
    }

    bool open()
    {
#if JUCE_WINDOWS
        if (! pipe.createNewPipe (path, true))
            return false;
#else
        ::unlink (path.toRawUTF8()); // (a window that crashed with this pid left it)
        if (::mkfifo (path.toRawUTF8(), 0600) != 0)
            return false;
        fd = ::open (path.toRawUTF8(), O_RDWR | O_NONBLOCK | O_CLOEXEC); // (read-write: no end of file when a writer closes)
        if (fd < 0 || ::pipe (wake) != 0)
            return false;
        for (auto w : wake)
            ::fcntl (w, F_SETFD, FD_CLOEXEC);
#endif
        return startThread (juce::Thread::Priority::low);
    }

private:
    void run() override
    {
        std::string pending;
        char buffer[1024];
        while (! threadShouldExit())
        {
#if JUCE_WINDOWS
            const auto n = pipe.read (buffer, (int) sizeof (buffer), -1);
            if (n <= 0)
            {
                if (threadShouldExit())
                    break;
                juce::Thread::sleep (50); // (an error other than a writer leaving: don't spin)
                continue;
            }
#else
            pollfd fds[2] = { { fd, POLLIN, 0 }, { wake[0], POLLIN, 0 } };
            if (::poll (fds, 2, -1) < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (fds[1].revents != 0)
                break;
            const auto n = (int) ::read (fd, buffer, sizeof (buffer));
            if (n <= 0)
                continue;
#endif
            pending.append (buffer, (std::size_t) n);
            for (auto end = pending.find ('\n'); end != std::string::npos; end = pending.find ('\n'))
            {
                auto line = juce::String::fromUTF8 (pending.data(), (int) end);
                pending.erase (0, end + 1);
                juce::MessageManager::callAsync ([line] { deliver (line); });
            }
            if (pending.size() > 4096) // (garbage: not one of ours)
                pending.clear();
        }
    }

    const juce::String path;
#if JUCE_WINDOWS
    juce::NamedPipe pipe;
#else
    int fd = -1;
    int wake[2] = { -1, -1 };
#endif
};

struct State
{
    Entry me;
    Receiver receiver;
    std::unique_ptr<Inbox> inbox;
};

State& state()
{
    static State s;
    return s;
}

void deliver (const juce::String& line)
{
    auto& s = state();
    if (s.inbox == nullptr || s.receiver == nullptr)
        return;
    const auto verb = line.upToFirstOccurrenceOf (" ", false, false);
    const auto rest = line.fromFirstOccurrenceOf (" ", false, false);
    const auto from = rest.upToFirstOccurrenceOf (" ", false, false).getIntValue();
    const auto argument = rest.fromFirstOccurrenceOf (" ", false, false);
    if (verb.isNotEmpty() && from > 0)
        s.receiver (verb, from, argument);
}

void writeEntry (const Entry& e)
{
    if (folder().createDirectory())
        entryFile (e.pid).replaceWithText (juce::JSON::toString (toVar (e)));
}

bool deliverTo (const Entry& e, const juce::String& line)
{
    if (e.inbox.isEmpty())
        return false;
    const auto bytes = line.toStdString();
#if JUCE_WINDOWS
    juce::NamedPipe pipe;
    if (! pipe.openExisting (e.inbox))
        return false;
    return pipe.write (bytes.data(), (int) bytes.size(), 1000) == (int) bytes.size();
#else
    const auto fd = ::open (e.inbox.toRawUTF8(), O_WRONLY | O_NONBLOCK | O_CLOEXEC); // (fails at once when nobody reads it)
    if (fd < 0)
        return false;
    const auto n = ::write (fd, bytes.data(), bytes.size());
    ::close (fd);
    return n == (ssize_t) bytes.size();
#endif
}
} // namespace

int processId()
{
#if JUCE_WINDOWS
    return (int) GetCurrentProcessId();
#else
    return (int) ::getpid();
#endif
}

bool enabled()
{
    static const bool on = ! platform::testMode() || juce::SystemStats::getEnvironmentVariable ("LYNX_WINDOWS", {}) == "1";
    return on;
}

juce::File folder()
{
    return platform::settingsFolder().getChildFile ("Windows");
}

bool start (Entry me, Receiver receiver)
{
    auto& s = state();
    if (s.inbox != nullptr || ! enabled())
        return s.inbox != nullptr;
#if ! JUCE_WINDOWS
    // a window that goes away between our open and write must not end this one (SIGPIPE's default)
    struct sigaction current {};
    if (::sigaction (SIGPIPE, nullptr, &current) == 0 && current.sa_handler == SIG_DFL)
        ::signal (SIGPIPE, SIG_IGN);
#endif
    me.pid = processId();
    me.started = (double) juce::Time::currentTimeMillis() / 1000.0;
    me.inbox = inboxName (me.pid);
    auto inbox = std::make_unique<Inbox> (me.inbox);
    if (! inbox->open())
        return false;
    s.inbox = std::move (inbox);
    s.receiver = std::move (receiver);
    s.me = me;
    writeEntry (me);
    broadcast ("hello");
    return true;
}

void stop()
{
    auto& s = state();
    if (s.inbox == nullptr)
        return;
    broadcast ("bye");
    entryFile (s.me.pid).deleteFile();
    s.inbox.reset();
    s.receiver = nullptr;
    s.me = {};
}

bool started()
{
    return state().inbox != nullptr;
}

Entry mine()
{
    return state().me;
}

void update (const std::function<void (Entry&)>& change)
{
    auto& s = state();
    if (s.inbox == nullptr)
        return;
    auto next = s.me;
    change (next);
    next.pid = s.me.pid;
    next.inbox = s.me.inbox;
    next.started = s.me.started;
    if (same (next, s.me))
        return;
    s.me = next;
    writeEntry (next);
    broadcast ("changed");
}

std::vector<Entry> others()
{
    std::vector<Entry> out;
    const auto me = processId();
    for (const auto& file : folder().findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        const auto e = fromVar (juce::JSON::parse (file.loadFileAsString()));
        if (e.pid <= 0 || e.pid == me)
            continue;
#if JUCE_WINDOWS
        const auto inboxThere = true;
#else
        const auto inboxThere = juce::File (e.inbox).exists();
#endif
        if (! alive (e.pid) || ! inboxThere)
        {
            file.deleteFile(); // (a window that crashed)
            continue;
        }
        out.push_back (e);
    }
    std::sort (out.begin(), out.end(), [] (const Entry& a, const Entry& b) { return a.started < b.started; });
    return out;
}

bool send (int pid, const juce::String& verb, const juce::String& argument)
{
    if (state().inbox == nullptr)
        return false;
    const auto file = entryFile (pid);
    if (! file.existsAsFile())
        return false;
    const auto e = fromVar (juce::JSON::parse (file.loadFileAsString()));
    auto line = verb + " " + juce::String (processId());
    if (argument.isNotEmpty())
        line << " " << argument.replaceCharacters ("\r\n", "  ");
    line = line.substring (0, maxLine) + "\n";
    if (deliverTo (e, line))
        return true;
    if (! alive (pid))
        file.deleteFile();
    return false;
}

void broadcast (const juce::String& verb, const juce::String& argument)
{
    for (const auto& e : others())
        send (e.pid, verb, argument);
}

void allowToFront ([[maybe_unused]] int pid)
{
#if JUCE_WINDOWS
    AllowSetForegroundWindow ((DWORD) pid);
#endif
}
} // namespace lynx::windows
