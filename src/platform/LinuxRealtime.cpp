#include "platform/LinuxRealtime.h"

#if JUCE_LINUX

 #include <array>
 #include <atomic>
 #include <dlfcn.h>
 #include <pthread.h>
 #include <sched.h>
 #include <sys/resource.h>
 #include <sys/syscall.h>
 #include <unistd.h>

 #ifndef SCHED_RESET_ON_FORK
  #define SCHED_RESET_ON_FORK 0x40000000
 #endif

namespace lynx::rt
{
namespace
{
// --- the noted threads ------------------------------------------------------------------------------------------
struct Slot
{
    std::atomic<pid_t> tid { 0 };
    std::atomic<bool> device { false };
};
std::array<Slot, 64> slots;
std::atomic<int> epoch { 1 };
std::atomic<Status> deviceStatus { Status::unknown };
std::atomic<int> devicePriority { 0 };
std::atomic<bool> deviceNoted { false };

// --- libdbus-1, loaded at run time (only the calls RealtimeKit and the portal need) ---------------------------------
struct DBusErrorStorage
{
    const char* name;
    const char* message;
    unsigned int dummy;
    void* padding;
    char spare[32];
};
struct DBusIterStorage
{
    alignas (8) unsigned char bytes[128]; // DBusMessageIter is 72 bytes on 64-bit systems
};
constexpr int busSession = 0, busSystem = 1;
constexpr int typeInvalid = 0, typeInt32 = 'i', typeUInt32 = 'u', typeInt64 = 'x', typeUInt64 = 't', typeString = 's', typeVariant = 'v';

struct DBus
{
    void (*errorInit) (DBusErrorStorage*) = nullptr;
    void (*errorFree) (DBusErrorStorage*) = nullptr;
    void* (*busGetPrivate) (int, DBusErrorStorage*) = nullptr;
    void (*setExitOnDisconnect) (void*, unsigned int) = nullptr;
    void* (*newMethodCall) (const char*, const char*, const char*, const char*) = nullptr;
    unsigned int (*appendArgs) (void*, int, ...) = nullptr;
    void* (*sendWithReplyAndBlock) (void*, void*, int, DBusErrorStorage*) = nullptr;
    void (*messageUnref) (void*) = nullptr;
    unsigned int (*iterInit) (void*, DBusIterStorage*) = nullptr;
    int (*iterArgType) (DBusIterStorage*) = nullptr;
    void (*iterRecurse) (DBusIterStorage*, DBusIterStorage*) = nullptr;
    void (*iterGetBasic) (DBusIterStorage*, void*) = nullptr;

    static DBus* get()
    {
        static DBus* dbus = [] () -> DBus* {
            auto* lib = dlopen ("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
            if (lib == nullptr)
                return nullptr;
            auto* d = new DBus();
            auto ok = true;
            auto load = [&] (auto& fn, const char* name) {
                fn = reinterpret_cast<std::remove_reference_t<decltype (fn)>> (dlsym (lib, name));
                ok = ok && fn != nullptr;
            };
            load (d->errorInit, "dbus_error_init");
            load (d->errorFree, "dbus_error_free");
            load (d->busGetPrivate, "dbus_bus_get_private");
            load (d->setExitOnDisconnect, "dbus_connection_set_exit_on_disconnect");
            load (d->newMethodCall, "dbus_message_new_method_call");
            load (d->appendArgs, "dbus_message_append_args");
            load (d->sendWithReplyAndBlock, "dbus_connection_send_with_reply_and_block");
            load (d->messageUnref, "dbus_message_unref");
            load (d->iterInit, "dbus_message_iter_init");
            load (d->iterArgType, "dbus_message_iter_get_arg_type");
            load (d->iterRecurse, "dbus_message_iter_recurse");
            load (d->iterGetBasic, "dbus_message_iter_get_basic");
            if (! ok)
            {
                delete d;
                return nullptr;
            }
            return d;
        }();
        return dbus;
    }

    // One private connection per bus for the whole session (message thread only)
    void* bus (int which)
    {
        auto& c = which == busSystem ? system : session;
        auto& tried = which == busSystem ? triedSystem : triedSession;
        if (c == nullptr && ! tried)
        {
            tried = true;
            DBusErrorStorage e {};
            errorInit (&e);
            c = busGetPrivate (which, &e);
            errorFree (&e);
            if (c != nullptr)
                setExitOnDisconnect (c, 0);
        }
        return c;
    }

    // Properties.Get of an integer property; `fallback` when it is not there
    std::int64_t property (int which, const char* dest, const char* path, const char* iface, const char* name, std::int64_t fallback)
    {
        auto* c = bus (which);
        if (c == nullptr)
            return fallback;
        auto* m = newMethodCall (dest, path, "org.freedesktop.DBus.Properties", "Get");
        if (m == nullptr)
            return fallback;
        appendArgs (m, typeString, &iface, typeString, &name, typeInvalid);
        DBusErrorStorage e {};
        errorInit (&e);
        auto* reply = sendWithReplyAndBlock (c, m, 1000, &e);
        messageUnref (m);
        errorFree (&e);
        if (reply == nullptr)
            return fallback;
        auto value = fallback;
        DBusIterStorage it {}, sub {};
        if (iterInit (reply, &it) != 0 && iterArgType (&it) == typeVariant)
        {
            iterRecurse (&it, &sub);
            const auto t = iterArgType (&sub);
            if (t == typeInt32 || t == typeUInt32)
            {
                std::int32_t v = 0;
                iterGetBasic (&sub, &v);
                value = v;
            }
            else if (t == typeInt64 || t == typeUInt64)
            {
                std::int64_t v = 0;
                iterGetBasic (&sub, &v);
                value = v;
            }
        }
        messageUnref (reply);
        return value;
    }

    bool call (void* m, int which)
    {
        auto* c = bus (which);
        if (c == nullptr)
        {
            messageUnref (m);
            return false;
        }
        DBusErrorStorage e {};
        errorInit (&e);
        auto* reply = sendWithReplyAndBlock (c, m, 1000, &e);
        messageUnref (m);
        errorFree (&e);
        if (reply == nullptr)
            return false;
        messageUnref (reply);
        return true;
    }

    void* system = nullptr;
    void* session = nullptr;
    bool triedSystem = false, triedSession = false;
};

// RealtimeKit refuses threads of processes without an RLIMIT_RTTIME (a runaway real-time thread could freeze the
// machine); set it to what it allows, as PipeWire does.
void limitRtTime (std::int64_t maxMicroseconds)
{
    rlimit rl {};
    if (maxMicroseconds <= 0 || getrlimit (RLIMIT_RTTIME, &rl) != 0)
        return;
    const auto limit = (rlim_t) maxMicroseconds;
    if (rl.rlim_max == RLIM_INFINITY || rl.rlim_max > limit)
        rl.rlim_max = limit;
    rl.rlim_cur = rl.rlim_max;
    setrlimit (RLIMIT_RTTIME, &rl);
}

bool viaRtkit (pid_t tid, int wanted, int& granted)
{
    auto* d = DBus::get();
    if (d == nullptr || d->bus (busSystem) == nullptr)
        return false;
    constexpr auto dest = "org.freedesktop.RealtimeKit1", path = "/org/freedesktop/RealtimeKit1";
    const auto max = (int) d->property (busSystem, dest, path, dest, "MaxRealtimePriority", 20);
    limitRtTime (d->property (busSystem, dest, path, dest, "RTTimeUSecMax", 200000));
    const auto priority = (std::uint32_t) juce::jlimit (1, juce::jmax (1, max), wanted);
    auto* m = d->newMethodCall (dest, path, dest, "MakeThreadRealtime");
    if (m == nullptr)
        return false;
    std::uint64_t thread = (std::uint64_t) tid;
    d->appendArgs (m, typeUInt64, &thread, typeUInt32, &priority, typeInvalid);
    if (! d->call (m, busSystem))
        return false;
    granted = (int) priority;
    return true;
}

bool viaPortal (pid_t tid, int wanted, int& granted)
{
    auto* d = DBus::get();
    if (d == nullptr || d->bus (busSession) == nullptr)
        return false;
    constexpr auto dest = "org.freedesktop.portal.Desktop", path = "/org/freedesktop/portal/desktop", iface = "org.freedesktop.portal.Realtime";
    const auto max = (int) d->property (busSession, dest, path, iface, "MaxRealtimePriority", 0);
    if (max <= 0)
        return false;
    limitRtTime (d->property (busSession, dest, path, iface, "RTTimeUSecMax", 200000));
    const auto priority = (std::uint32_t) juce::jlimit (1, max, wanted);
    auto* m = d->newMethodCall (dest, path, iface, "MakeThreadRealtimeWithPID");
    if (m == nullptr)
        return false;
    std::uint64_t process = (std::uint64_t) getpid(), thread = (std::uint64_t) tid;
    d->appendArgs (m, typeUInt64, &process, typeUInt64, &thread, typeUInt32, &priority, typeInvalid);
    if (! d->call (m, busSession))
        return false;
    granted = (int) priority;
    return true;
}

Status promote (pid_t tid, int wanted, int& granted)
{
    const auto policy = sched_getscheduler (tid) & ~SCHED_RESET_ON_FORK;
    if (policy == SCHED_FIFO || policy == SCHED_RR)
    {
        sched_param p {};
        sched_getparam (tid, &p);
        granted = p.sched_priority;
        return Status::alreadyRealtime;
    }
    if (policy < 0)
        return Status::unknown; // the thread is gone

    rlimit rl {};
    if (getrlimit (RLIMIT_RTPRIO, &rl) == 0 && rl.rlim_cur > 0)
    {
        sched_param p {};
        p.sched_priority = juce::jlimit (1, juce::jmin ((int) rl.rlim_cur, sched_get_priority_max (SCHED_FIFO)), wanted);
        if (sched_setscheduler (tid, SCHED_FIFO | SCHED_RESET_ON_FORK, &p) == 0)
        {
            granted = p.sched_priority;
            return Status::direct;
        }
    }

    const auto sandboxed = juce::File ("/.flatpak-info").exists();
    if (sandboxed && viaPortal (tid, wanted, granted))
        return Status::portal;
    if (viaRtkit (tid, wanted, granted))
        return Status::rtkit;
    if (! sandboxed && viaPortal (tid, wanted, granted))
        return Status::portal;
    return Status::denied;
}

bool disabled()
{
    static const auto off = juce::SystemStats::getEnvironmentVariable ("LYNX_NO_RT", {}).isNotEmpty();
    return off;
}
} // namespace

void noteAudioThread (bool deviceThread) noexcept
{
    thread_local int notedEpoch = 0;
    const auto now = epoch.load (std::memory_order_relaxed);
    if (notedEpoch == now)
        return;
    notedEpoch = now;
    const auto tid = (pid_t) syscall (SYS_gettid);
    const auto isDevice = deviceThread && ! deviceNoted.exchange (true);
    for (auto& s : slots)
    {
        pid_t empty = 0;
        if (s.tid.compare_exchange_strong (empty, tid))
        {
            s.device = isDevice;
            return;
        }
    }
}

bool promotePending()
{
    auto changed = false;
    for (auto& s : slots)
    {
        const auto tid = s.tid.load();
        if (tid == 0)
            continue;
        const auto isDevice = s.device.load();
        int granted = 0;
        const auto result = disabled() ? Status::denied : promote (tid, isDevice ? 70 : 65, granted);
        if (isDevice)
        {
            deviceStatus = result;
            devicePriority = granted;
            changed = true;
        }
        s.tid = 0;
    }
    return changed;
}

Status status() { return deviceStatus.load(); }

void resetForNewDevice()
{
    deviceNoted = false;
    deviceStatus = Status::unknown;
    devicePriority = 0;
    ++epoch;
}

juce::String describe()
{
    const auto p = devicePriority.load();
    switch (deviceStatus.load())
    {
        case Status::alreadyRealtime: return "driver thread, SCHED_FIFO " + juce::String (p);
        case Status::direct:          return "SCHED_FIFO " + juce::String (p) + " (RLIMIT_RTPRIO)";
        case Status::rtkit:           return "RealtimeKit, priority " + juce::String (p);
        case Status::portal:          return "desktop portal, priority " + juce::String (p);
        case Status::denied:          return "normal priority";
        case Status::unknown:         break;
    }
    return {};
}

bool shouldShowHint (const juce::File& flagFile)
{
    if (deviceStatus.load() != Status::denied || flagFile.existsAsFile())
        return false;
    flagFile.replaceWithText ("shown\n");
    return true;
}
} // namespace lynx::rt

#else

namespace lynx::rt
{
void noteAudioThread (bool) noexcept {}
bool promotePending() { return false; }
Status status() { return Status::unknown; }
void resetForNewDevice() {}
juce::String describe() { return {}; }
bool shouldShowHint (const juce::File&) { return false; }
} // namespace lynx::rt

#endif
