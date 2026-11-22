#include "platform/CrashGuard.h"
#include "platform/CvWait.h"
#include "core/CrashReport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
 #include <dbghelp.h>
 #include <mmsystem.h>
 #include <tlhelp32.h>
 #include <intrin.h>
#else
 #include <signal.h>
 #include <fcntl.h>
 #include <setjmp.h>
 #include <unistd.h>
 #include <pthread.h>
 #include <dlfcn.h>
 #include <unwind.h>
 #if defined(__APPLE__)
  #include <sys/ucontext.h>
 #else
  #include <ucontext.h>
 #endif
 #if defined(__GLIBC__) || defined(__APPLE__)
  #include <execinfo.h>
  #define LYNX_EXECINFO 1
 #endif
#endif

namespace lynx::crashguard
{
#if defined(_WIN32) && (defined(_MSC_VER) || defined(__clang__))
namespace
{
// Breakpoints and single steps belong to a debugger; everything else is the plugin's crash.
int filter (unsigned long code) noexcept
{
    return code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

std::uint32_t call (void (*fn) (void*), void* context) noexcept
{
    // no C++ objects in this frame (SEH and C++ unwinding must not share one function)
    __try
    {
        fn (context);
        return 0;
    }
    __except (filter (GetExceptionCode()))
    {
        const auto code = (std::uint32_t) GetExceptionCode();
        return code != 0 ? code : 1u;
    }
}
#elif ! defined(_WIN32)
// Round 190dt (issue #155 follow-up): Linux / macOS. A fault is a signal there; see CrashGuard.h for what is caught,
// what is not, and why.
namespace
{
constexpr std::uint32_t cppExceptionCode = 0xE06D7363u; // (Windows' code for a C++ exception: describe() names it)
constexpr std::uint32_t signalCodeBase = 0x4C590000u;   // | the signal number ("LY" + signal: describe() names it)
constexpr int guardedSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };
constexpr int guardedCount = (int) (sizeof (guardedSignals) / sizeof (guardedSignals[0]));

// One armed call on this thread (a stack of them when a guarded call makes another one)
struct GuardFrame
{
    sigjmp_buf env;
    GuardFrame* prev = nullptr;
    volatile std::uint32_t code = 0;     // written by the handler before it jumps back
    volatile sig_atomic_t handling = 0;  // the handler is looking at a fault of this frame (a fault inside it is not caught)
};

// The armed frame of this thread. The handler reads it, so it must not allocate on first use: Linux' thread_local
// pointer in the executable is static TLS (initial-exec); macOS' thread_local may allocate lazily, so a pthread key.
// LYNX's exported plugins (this file is in their runtime, cmake/LynxPlugins.cmake, the only build that defines
// LYNX_EXPORT_VERSION) are modules a host loads with dlopen: there one initial-exec variable makes the module's whole
// TLS block static (DF_STATIC_TLS), glibc keeps only a small reserve for that, and hosts on glibc 2.35 failed to load
// the 0.1.27 Linux plugins ("cannot allocate memory in static TLS block", round 190fp). They use the key as well
// (pthread_getspecific does not allocate; scripts/check-linux-compat.sh fails a plugin that asks for static TLS).
#if defined(LYNX_EXPORT_VERSION)
 #define LYNX_GUARD_FRAME_KEY 1
#endif
#if defined(__APPLE__) || defined(LYNX_GUARD_FRAME_KEY)
pthread_key_t frameKey;
GuardFrame* currentFrame() noexcept { return static_cast<GuardFrame*> (pthread_getspecific (frameKey)); }
void setCurrentFrame (GuardFrame* f) noexcept { pthread_setspecific (frameKey, f); }
#else
__attribute__ ((tls_model ("initial-exec"))) thread_local GuardFrame* tlsFrame = nullptr;
GuardFrame* currentFrame() noexcept { return tlsFrame; }
void setCurrentFrame (GuardFrame* f) noexcept { tlsFrame = f; }
#endif

std::atomic<int> guardState { 0 }; // 0 = not installed, 1 = being installed, 2 = installed
struct sigaction previousAction[guardedCount];
const void* ownImage = nullptr; // where LYNX's executable (LYNX's and JUCE's code) is loaded

// A fault the kernel raised for an instruction of this thread (not a signal sent by kill / raise / sigqueue)
bool synchronousFault (const siginfo_t* info) noexcept
{
#if defined(__APPLE__)
    return info != nullptr && info->si_code > 0 && info->si_code < 0x10000; // (SI_USER and friends are 0x1000x)
#else
    return info != nullptr && info->si_code > 0; // (SI_USER 0, SI_TKILL / SI_QUEUE negative; SI_KERNEL 0x80 is a fault)
#endif
}

std::uintptr_t programCounter (const void* context) noexcept
{
    const auto* uc = static_cast<const ucontext_t*> (context);
#if defined(__APPLE__) && defined(__x86_64__)
    return (std::uintptr_t) uc->uc_mcontext->__ss.__rip;
#elif defined(__APPLE__) && defined(__aarch64__)
    return (std::uintptr_t) __darwin_arm_thread_state64_get_pc (uc->uc_mcontext->__ss);
#elif defined(__linux__) && defined(__x86_64__)
    return (std::uintptr_t) uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__linux__) && defined(__aarch64__)
    return (std::uintptr_t) uc->uc_mcontext.pc;
#else
    (void) uc;
    return 0; // (unknown: nothing is caught)
#endif
}

// Where the faulting function was called from, when the faulting address is in no module (a call through a null or
// stale function pointer): x86-64 has just pushed the return address, ARM64 holds it in the link register.
std::uintptr_t callerOfUnknownCode (const void* context) noexcept
{
    const auto* uc = static_cast<const ucontext_t*> (context);
#if defined(__APPLE__) && defined(__x86_64__)
    return *reinterpret_cast<const std::uintptr_t*> (uc->uc_mcontext->__ss.__rsp);
#elif defined(__APPLE__) && defined(__aarch64__)
    return (std::uintptr_t) __darwin_arm_thread_state64_get_lr (uc->uc_mcontext->__ss);
#elif defined(__linux__) && defined(__x86_64__)
    return *reinterpret_cast<const std::uintptr_t*> (uc->uc_mcontext.gregs[REG_RSP]);
#elif defined(__linux__) && defined(__aarch64__)
    return (std::uintptr_t) uc->uc_mcontext.regs[30];
#else
    (void) uc;
    return 0;
#endif
}

// The C / C++ runtime and the system's own libraries: a fault inside them belongs to whoever called them (a plugin's
// memcpy from a null pointer faults in libc), so the walk goes on to their caller.
bool runtimeLibrary (const char* path) noexcept
{
    if (path == nullptr)
        return false;
    const char* name = path;
    for (const char* p = path; *p != 0; ++p)
        if (*p == '/')
            name = p + 1;
    static const char* const prefixes[] = { "libc.so", "libc-", "libm.so", "libm-", "libmvec", "libstdc++", "libgcc_s", "ld-linux",
                                            "ld64.so", "libpthread", "libdl.so", "libdl-", "librt.so", "librt-", "libc++", "libunwind",
                                            "libsystem_", "libdyld", "libobjc", "libSystem" };
    for (const auto* prefix : prefixes)
        if (std::strncmp (name, prefix, std::strlen (prefix)) == 0)
            return true;
    return false;
}

enum class Owner { unknown, lynx, runtime, other };

Owner ownerOf (std::uintptr_t address) noexcept
{
    Dl_info info {};
    if (address == 0 || dladdr (reinterpret_cast<const void*> (address), &info) == 0 || info.dli_fbase == nullptr)
        return Owner::unknown;
    if (info.dli_fbase == ownImage)
        return Owner::lynx;
    return runtimeLibrary (info.dli_fname) ? Owner::runtime : Owner::other;
}

struct Walk
{
    std::uintptr_t frames[64];
    int count = 0;
};

_Unwind_Reason_Code collectFrame (_Unwind_Context* context, void* arg)
{
    auto& walk = *static_cast<Walk*> (arg);
    if (walk.count >= 64)
        return _URC_END_OF_STACK;
    int beforeInstruction = 0;
    const auto ip = (std::uintptr_t) _Unwind_GetIPInfo (context, &beforeInstruction);
    if (ip == 0)
        return _URC_END_OF_STACK;
    // a return address points after its call: one byte back is inside the call (the faulting frame's is exact)
    walk.frames[walk.count++] = beforeInstruction != 0 ? ip : ip - 1;
    return _URC_NO_REASON;
}

// Whose fault it is: the first frame, from the faulting instruction outwards, that is neither the system's runtime
// nor code outside every module decides. A plugin's module (or anything else LYNX did not build) = the plugin's;
// LYNX's executable, or no frame that tells = LYNX's own (never caught).
bool faultIsAPlugins (std::uintptr_t pc, const void* context) noexcept
{
    if (ownImage == nullptr)
        return false;
    Walk walk; // (the unwinder walks through the signal frame into the interrupted code)
    if (pc != 0) // (a call through a null pointer: the unwinder stops there; its caller below)
        _Unwind_Backtrace (collectFrame, &walk);
    int start = -1;
    for (int i = 0; i < walk.count && start < 0; ++i)
        if (walk.frames[i] == pc || walk.frames[i] == pc - 1)
            start = i;
    std::uintptr_t fallback[2] = { pc, 0 };
    const std::uintptr_t* frames = fallback;
    int count = 1;
    if (start >= 0)
    {
        frames = walk.frames + start;
        count = walk.count - start;
    }
    else if (ownerOf (pc) == Owner::unknown || ownerOf (pc) == Owner::runtime)
    {
        // the unwinder did not find the faulting frame: its caller only (a leaf: the return address)
        fallback[1] = callerOfUnknownCode (context);
        count = 2;
    }
    for (int i = 0; i < count; ++i)
        switch (ownerOf (frames[i]))
        {
            case Owner::lynx: return false;
            case Owner::other: return true;
            case Owner::unknown:
            case Owner::runtime: break;
        }
    return false;
}

#if ! defined(LYNX_EXPORT_VERSION)
 #define LYNX_PLUGIN_THREAD_GUARD 1
#endif
#if defined(LYNX_PLUGIN_THREAD_GUARD)
// Round 190gg (B4): a fault on a hosted plugin's own thread (see CrashGuard.h, watchPluginModule). The app only: the
// exported plugins' runtime never watches a module (nothing here would apply there), so it does not even have this.
// Written by the message thread (one writer, slots never reused); the handler reads the slots that are ready.
constexpr int watchedCapacity = 128;
struct WatchedModule
{
    char path[1024] {};
    std::atomic<int> ready { 0 };
};
WatchedModule watched[watchedCapacity];
std::atomic<int> watchedCount { 0 };

struct ThreadFaultSlot
{
    std::atomic<int> module { -1 };
    std::atomic<std::uint32_t> code { 0 };
    std::atomic<std::uintptr_t> offset { 0 };
};
constexpr int threadFaultCapacity = 16;
ThreadFaultSlot threadFaultSlots[threadFaultCapacity];
std::atomic<int> threadFaultTotal { 0 };
std::atomic<int> threadFaultChecking { 0 }; // a fault is being looked at (any thread): another one is not parked

// The watched module `path` is (its file, or a file inside its bundle folder), -1 = none
int watchedModuleOf (const char* path) noexcept
{
    if (path == nullptr)
        return -1;
    const auto count = std::min (watchedCount.load (std::memory_order_acquire), watchedCapacity);
    for (int i = 0; i < count; ++i)
    {
        if (watched[i].ready.load (std::memory_order_acquire) != 1)
            continue;
        const char* w = watched[i].path;
        std::size_t k = 0;
        while (w[k] != 0 && w[k] == path[k])
            ++k;
        if (w[k] == 0 && (path[k] == 0 || path[k] == '/' || (k > 0 && w[k - 1] == '/')))
            return i;
    }
    return -1;
}

struct LongWalk
{
    std::uintptr_t frames[256];
    int count = 0;
};

_Unwind_Reason_Code collectLongFrame (_Unwind_Context* context, void* arg)
{
    auto& walk = *static_cast<LongWalk*> (arg);
    if (walk.count >= 256)
        return _URC_END_OF_STACK;
    int beforeInstruction = 0;
    const auto ip = (std::uintptr_t) _Unwind_GetIPInfo (context, &beforeInstruction);
    if (ip == 0)
        return _URC_END_OF_STACK;
    walk.frames[walk.count++] = beforeInstruction != 0 ? ip : ip - 1;
    return _URC_NO_REASON;
}

// A fault outside every guarded call, on a thread that is a watched plugin's own: from the faulting instruction to the
// thread's first frame (which must be reached: the C library's thread start) there is no frame of LYNX's executable,
// and the first frame that is not the system's runtime is in a watched module. Then the fault is noted and this thread
// waits forever (it is the plugin's: nothing of LYNX's runs on it); the app switches that plugin off. Otherwise false.
bool parkPluginThread (int sig, const void* context) noexcept
{
    if (ownImage == nullptr || watchedCount.load (std::memory_order_acquire) == 0)
        return false;
    auto idle = 0;
    if (! threadFaultChecking.compare_exchange_strong (idle, 1))
        return false; // (a fault while looking at one, on this thread or another: the crash handler)
    const auto pc = programCounter (context);
    LongWalk walk;
    if (pc != 0)
        _Unwind_Backtrace (collectLongFrame, &walk);
    int start = -1;
    for (int i = 0; i < walk.count && start < 0; ++i)
        if (walk.frames[i] == pc || walk.frames[i] == pc - 1)
            start = i;
    auto plugin = -1;
    std::uintptr_t offset = 0;
    auto ownFrame = false;
    if (start >= 0 && walk.count < 256)
    {
        for (int i = start; i < walk.count && ! ownFrame; ++i)
        {
            Dl_info frame {};
            if (dladdr (reinterpret_cast<const void*> (walk.frames[i]), &frame) == 0 || frame.dli_fbase == nullptr)
                continue; // (generated code: judged by the frames around it)
            if (frame.dli_fbase == ownImage)
                ownFrame = true;
            else if (plugin < 0 && ! runtimeLibrary (frame.dli_fname))
            {
                plugin = watchedModuleOf (frame.dli_fname);
                if (plugin < 0)
                    ownFrame = true; // (the first module that is not the runtime is somebody else's: not a plugin's)
                offset = walk.frames[i] - (std::uintptr_t) frame.dli_fbase;
            }
        }
        // the walk must end at the thread's start (the runtime's), or it cannot tell what started the thread
        Dl_info last {};
        if (dladdr (reinterpret_cast<const void*> (walk.frames[walk.count - 1]), &last) == 0 || ! runtimeLibrary (last.dli_fname))
            ownFrame = true;
    }
    if (plugin < 0 || ownFrame)
    {
        threadFaultChecking.store (0);
        return false;
    }
    const auto index = threadFaultTotal.load (std::memory_order_relaxed);
    auto& slot = threadFaultSlots[index % threadFaultCapacity];
    slot.module.store (plugin, std::memory_order_relaxed);
    slot.code.store (signalCodeBase | (std::uint32_t) sig, std::memory_order_relaxed);
    slot.offset.store (offset, std::memory_order_relaxed);
    threadFaultTotal.store (index + 1, std::memory_order_release);
    threadFaultChecking.store (0);
    sigset_t all;
    sigfillset (&all);
    pthread_sigmask (SIG_SETMASK, &all, nullptr);
    for (;;)
        pause(); // (async-signal-safe; with every signal blocked it never returns)
}
#endif

// The handlers that were there before (JUCE's crash handler: the crash file and the emergency save)
void passOn (int sig, siginfo_t* info, void* context) noexcept
{
    int index = 0;
    while (index < guardedCount && guardedSignals[index] != sig)
        ++index;
    if (index < guardedCount)
    {
        const auto& previous = previousAction[index];
        if ((previous.sa_flags & SA_SIGINFO) != 0)
        {
            if (previous.sa_sigaction != nullptr)
                previous.sa_sigaction (sig, info, context);
        }
        else if (previous.sa_handler == SIG_IGN && ! synchronousFault (info))
            return;
        else if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN)
            previous.sa_handler (sig);
    }
    // it came back (or there was none): the default action, by the fault happening again on return
    struct sigaction fallback {};
    fallback.sa_handler = SIG_DFL;
    sigemptyset (&fallback.sa_mask);
    sigaction (sig, &fallback, nullptr);
    if (! synchronousFault (info))
        raise (sig);
}

void onFault (int sig, siginfo_t* info, void* context)
{
    const auto savedErrno = errno;
    if (auto* frame = currentFrame(); frame != nullptr && frame->handling == 0 && synchronousFault (info))
    {
        frame->handling = 1; // (a fault while looking at this one, e.g. in the unwinder, goes on to the crash handler)
        if (faultIsAPlugins (programCounter (context), context))
        {
            frame->code = signalCodeBase | (std::uint32_t) sig;
            // the thread's signal mask as it was before the fault (sigsetjmp did not save it: that costs a system
            // call on every guarded call), then back to the guarded call
            pthread_sigmask (SIG_SETMASK, &static_cast<ucontext_t*> (context)->uc_sigmask, nullptr);
            siglongjmp (frame->env, 1);
        }
        // (handling stays set: the crash handler below ends the process, and a fault inside it is not caught either)
    }
#if defined(LYNX_PLUGIN_THREAD_GUARD)
    else if (currentFrame() == nullptr && synchronousFault (info))
        parkPluginThread (sig, context); // (returns only when it is not a plugin's own thread)
#endif
    errno = savedErrno;
    passOn (sig, info, context);
}

void installFaultGuard() noexcept
{
    auto expected = 0;
    if (! guardState.compare_exchange_strong (expected, 1))
    {
        while (guardState.load() == 1)
            sched_yield();
        return;
    }
    Dl_info self {};
    if (dladdr (reinterpret_cast<const void*> (&installFaultGuard), &self) != 0)
        ownImage = self.dli_fbase;
#if defined(__APPLE__) || defined(LYNX_GUARD_FRAME_KEY)
    pthread_key_create (&frameKey, nullptr);
#endif
    {
        Walk warm; // (the unwinder's first use loads and sets up its tables: here, not inside a signal handler)
        _Unwind_Backtrace (collectFrame, &warm);
    }
    for (int i = 0; i < guardedCount; ++i)
    {
        struct sigaction action {};
        action.sa_sigaction = onFault;
        sigemptyset (&action.sa_mask);
        // SA_NODEFER: a fault inside the handler reaches it again (and goes on to the crash handler) instead of the
        // kernel ending the process at once; SA_ONSTACK: the alternate stack where the thread has one (hardenSignals)
        action.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
        sigaction (guardedSignals[i], &action, &previousAction[i]);
    }
    guardState.store (2, std::memory_order_release);
}

// The call itself, in a frame of its own: C++ exceptions leaving the plugin are caught here
__attribute__ ((noinline)) bool callCatching (void (*fn) (void*), void* context) noexcept
{
    try
    {
        fn (context);
        return false;
    }
    catch (...)
    {
        return true;
    }
}
} // namespace

std::uint32_t call (void (*fn) (void*), void* context) noexcept
{
    if (guardState.load (std::memory_order_acquire) != 2)
        installFaultGuard();
    GuardFrame frame;
    frame.prev = currentFrame();
    // (no signal mask saved: see onFault)
    if (sigsetjmp (frame.env, 0) != 0)
    {
        setCurrentFrame (frame.prev); // the plugin faulted: back here from onFault
        const std::uint32_t code = frame.code;
        return code != 0 ? code : 1u;
    }
    setCurrentFrame (&frame);
    const auto threw = callCatching (fn, context);
    setCurrentFrame (frame.prev);
    return threw ? cppExceptionCode : 0u;
}
#else
std::uint32_t call (void (*fn) (void*), void* context) noexcept
{
    fn (context);
    return 0;
}
#endif

void quietCrashReports() noexcept
{
#if defined(_WIN32)
    SetErrorMode (GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // WerSetFlags (kernel32, Vista+): no Windows Error Reporting UI for this process
    using WerSetFlagsFn = long (WINAPI*) (unsigned long);
    if (auto* kernel = GetModuleHandleW (L"kernel32.dll"))
        if (auto* set = reinterpret_cast<WerSetFlagsFn> (reinterpret_cast<void*> (GetProcAddress (kernel, "WerSetFlags"))))
            set (0x00000004 /* WER_FAULT_REPORTING_NO_UI */);
#endif
}

void exitNow (int code) noexcept
{
#if defined(_WIN32)
    TerminateProcess (GetCurrentProcess(), (UINT) code);
#endif
    std::_Exit (code);
}

namespace
{
std::mutex sessionLock;
std::string sessionFile, sessionHeader;
std::deque<std::string> sessionSteps;
std::vector<std::pair<std::string, std::string>> sessionInfo; // "info.<key>=value" lines (round 159)
std::vector<std::string> sessionPlugins;                       // "plugin=" lines: third-party plugins loaded
constexpr std::size_t maxSteps = 60; // (round 159: the crash report sends the last 60; was 10)

std::FILE* openUtf8 (const std::string& path, const char* mode)
{
#if defined(_WIN32)
    const auto n = MultiByteToWideChar (CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (n <= 0)
        return nullptr;
    std::wstring wide ((std::size_t) n, L'\0');
    MultiByteToWideChar (CP_UTF8, 0, path.c_str(), -1, wide.data(), n);
    std::wstring wmode;
    for (const char* m = mode; *m != 0; ++m)
        wmode += (wchar_t) *m;
    return _wfopen (wide.c_str(), wmode.c_str());
#else
    return std::fopen (path.c_str(), mode);
#endif
}

void writeSession() // sessionLock held
{
    if (sessionFile.empty())
        return;
    std::string text = sessionHeader;
    for (const auto& [key, value] : sessionInfo)
        text += "info." + key + "=" + value + "\n";
    for (const auto& p : sessionPlugins)
        text += "plugin=" + p + "\n";
    for (const auto& s : sessionSteps)
        text += "step=" + s + "\n";
    if (auto* f = openUtf8 (sessionFile, "wb"))
    {
        std::fwrite (text.data(), 1, text.size(), f);
        std::fclose (f);
    }
}
} // namespace

void beginSession (const std::string& fileUtf8, const std::string& header) noexcept
{
    try
    {
        const std::lock_guard<std::mutex> lock (sessionLock);
        sessionFile = fileUtf8;
        sessionHeader = header;
        sessionSteps.clear();
        sessionInfo.clear();
        sessionPlugins.clear();
        writeSession();
    }
    catch (...) {}
}

void note (const std::string& step) noexcept
{
    try
    {
        const std::lock_guard<std::mutex> lock (sessionLock);
        if (sessionFile.empty())
            return;
        char stamp[32] = {};
        const auto now = std::time (nullptr);
        if (const auto* t = std::localtime (&now))
            std::strftime (stamp, sizeof (stamp), "%H:%M:%S ", t);
        std::string line = stamp + step;
        for (auto& c : line)
            if (c == '\n' || c == '\r')
                c = ' ';
        sessionSteps.push_back (std::move (line));
        while (sessionSteps.size() > maxSteps)
            sessionSteps.pop_front();
        writeSession();
    }
    catch (...) {}
}

namespace
{
std::string oneLine (std::string text)
{
    for (auto& c : text)
        if (c == '\n' || c == '\r')
            c = ' ';
    return text;
}
} // namespace

void setSessionInfo (const std::string& key, const std::string& value) noexcept
{
    try
    {
        const std::lock_guard<std::mutex> lock (sessionLock);
        if (sessionFile.empty())
            return;
        const auto line = oneLine (value);
        for (auto& [k, v] : sessionInfo)
            if (k == key)
            {
                if (v != line)
                {
                    v = line;
                    writeSession();
                }
                return;
            }
        sessionInfo.emplace_back (oneLine (key), line);
        writeSession();
    }
    catch (...) {}
}

void notePlugin (const std::string& description) noexcept
{
    try
    {
        const std::lock_guard<std::mutex> lock (sessionLock);
        const auto line = oneLine (description);
        if (sessionFile.empty() || sessionPlugins.size() >= 300 || std::find (sessionPlugins.begin(), sessionPlugins.end(), line) != sessionPlugins.end())
            return;
        sessionPlugins.push_back (line);
        writeSession();
    }
    catch (...) {}
}

void endSession() noexcept
{
    try
    {
        const std::lock_guard<std::mutex> lock (sessionLock);
        if (sessionFile.empty())
            return;
#if defined(_WIN32)
        const auto n = MultiByteToWideChar (CP_UTF8, 0, sessionFile.c_str(), -1, nullptr, 0);
        std::wstring wide ((std::size_t) (n > 0 ? n : 1), L'\0');
        MultiByteToWideChar (CP_UTF8, 0, sessionFile.c_str(), -1, wide.data(), n);
        DeleteFileW (wide.c_str());
#else
        std::remove (sessionFile.c_str());
#endif
        sessionFile.clear();
    }
    catch (...) {}
}

std::uint32_t processId() noexcept
{
#if defined(_WIN32)
    return (std::uint32_t) GetCurrentProcessId();
#else
    return (std::uint32_t) getpid();
#endif
}

#if defined(_WIN32)
namespace
{
std::uint64_t creationTime (HANDLE process) noexcept
{
    FILETIME created {}, exited {}, kernel {}, user {};
    if (! GetProcessTimes (process, &created, &exited, &kernel, &user))
        return 0;
    return ((std::uint64_t) created.dwHighDateTime << 32) | created.dwLowDateTime;
}
} // namespace
#endif

std::uint64_t processStartTime() noexcept
{
#if defined(_WIN32)
    return creationTime (GetCurrentProcess());
#else
    return 0;
#endif
}

bool processRunning (std::uint32_t pid, std::uint64_t startTime) noexcept
{
    if (pid == 0)
        return false;
#if defined(_WIN32)
    auto* process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid);
    if (process == nullptr)
        return false;
    DWORD exitCode = 0;
    const auto running = GetExitCodeProcess (process, &exitCode) && exitCode == STILL_ACTIVE
                         && (startTime == 0 || creationTime (process) == startTime);
    CloseHandle (process);
    return running;
#else
    (void) startTime;
    return kill ((pid_t) pid, 0) == 0;
#endif
}

const char* describe (std::uint32_t code) noexcept
{
#if ! defined(_WIN32)
    if ((code & 0xFFFF0000u) == 0x4C590000u) // a signal caught by call() on Linux / macOS (round 190dt)
    {
        const auto sig = (int) (code & 0xFFu);
        return sig == SIGSEGV ? "segmentation fault" : sig == SIGBUS ? "bus error" : sig == SIGILL ? "illegal instruction"
             : sig == SIGFPE ? "floating-point exception" : "signal";
    }
#endif
    switch (code)
    {
        case 0xC0000005u: return "access violation";
        case 0xC000001Du: return "illegal instruction";
        case 0xC0000094u: return "integer division by zero";
        case 0xC00000FDu: return "stack overflow";
        case 0xC000008Eu: case 0xC0000090u: case 0xC0000091u: case 0xC0000093u: return "floating-point exception";
        case 0xC0000409u: return "buffer overrun";
        case 0xC0000374u: return "heap corruption";
        case 0xE06D7363u: case 0x20474343u: return "C++ exception";
        default: return "exception";
    }
}

// --- module backtraces and the UI hang watch (round 157a) ---------------------------------------------------------

namespace
{
#if defined(_WIN32)
std::string narrow (const wchar_t* w)
{
    const auto n = WideCharToMultiByte (CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return {};
    std::string s ((std::size_t) n - 1, '\0');
    WideCharToMultiByte (CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// the nearest exported / public symbol (dbghelp; a release LYNX.exe has none of its own, other modules have exports)
std::string symbolOf (DWORD64 address)
{
    static std::once_flag once;
    static std::mutex lock;
    std::call_once (once, [] {
        SymSetOptions (SymGetOptions() | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        // the search path: the exe's folder first (a LYNX.pdb put next to LYNX.exe names LYNX's own frames)
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW (nullptr, exe, MAX_PATH);
        std::wstring folder (exe);
        if (const auto slash = folder.find_last_of (L"\\/"); slash != std::wstring::npos)
            folder.resize (slash);
        SymInitializeW (GetCurrentProcess(), (folder + L";.").c_str(), TRUE);
    });
    const std::lock_guard<std::mutex> l (lock); // (dbghelp is single-threaded)
    alignas (SYMBOL_INFO) char buffer[sizeof (SYMBOL_INFO) + 256] = {};
    auto* info = reinterpret_cast<SYMBOL_INFO*> (buffer);
    info->SizeOfStruct = sizeof (SYMBOL_INFO);
    info->MaxNameLen = 255;
    DWORD64 displacement = 0;
    if (! SymFromAddr (GetCurrentProcess(), address, &displacement, info))
        return {};
    // LYNX.exe's own frames without its PDB: the nearest export is one of the few the exe has (pocketpy's py_*), not
    // the function (issue #32 reports named main and the CRT start-up "py_tpbase + 0x18a9"); the offset says more alone
    if (info->ModBase == (DWORD64) (std::uintptr_t) GetModuleHandleW (nullptr))
    {
        IMAGEHLP_MODULEW64 module {};
        module.SizeOfStruct = sizeof (module);
        if (! SymGetModuleInfoW64 (GetCurrentProcess(), address, &module) || module.SymType == SymExport || module.SymType == SymNone)
            return {};
    }
    char text[320];
    std::snprintf (text, sizeof (text), "%s + 0x%llx", info->Name, (unsigned long long) displacement);
    return text;
}
#endif

std::string formatFrames (const std::vector<std::uintptr_t>& frames)
{
    std::string text;
    int index = 0;
    for (const auto address : frames)
    {
        auto line = describeAddress (reinterpret_cast<const void*> (address));
#if defined(_WIN32)
        if (const auto symbol = symbolOf ((DWORD64) address); ! symbol.empty())
            line += "  (" + symbol + ")";
#endif
        text += std::to_string (index++) + ": " + (line.empty() ? std::string ("?") : line) + "\n";
    }
    return text;
}
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
 #define LYNX_UNWIND 1
// Frames from a thread context, unwound with the modules' own unwind tables (no symbols, no allocation: the thread
// being sampled may hold the heap lock)
int unwind (CONTEXT context, std::uintptr_t* frames, int capacity)
{
    int count = 0;
    for (; count < capacity && context.Rip != 0; ++count)
    {
        frames[count] = (std::uintptr_t) context.Rip;
        DWORD64 imageBase = 0;
        auto* function = RtlLookupFunctionEntry (context.Rip, &imageBase, nullptr);
        if (function == nullptr)
        {
            // a leaf function: the return address is on top of the stack
            if (context.Rsp == 0 || IsBadReadPtr (reinterpret_cast<void*> (context.Rsp), 8))
            {
                ++count;
                break;
            }
            context.Rip = *reinterpret_cast<DWORD64*> (context.Rsp);
            context.Rsp += 8;
            continue;
        }
        void* handlerData = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind (UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
    }
    return count;
}

// Round 190ci: whether a walk from `context` (a context of the calling thread) reaches the thread's first frame the
// way Windows' exception dispatch has to before it can call the top-level filter: every frame inside the thread's
// stack, every caller's address inside a module (or code with unwind tables of its own), and the last frame one of
// ntdll's thread / fiber start functions. No C++ objects here (SEH frame).
bool walksToThreadStart (CONTEXT context) noexcept
{
    const auto* tib = reinterpret_cast<const NT_TIB*> (NtCurrentTeb());
    const auto low = (DWORD64) (std::uintptr_t) tib->StackLimit, high = (DWORD64) (std::uintptr_t) tib->StackBase;
    static void* const ntdll = GetModuleHandleW (L"ntdll.dll");
    __try
    {
        DWORD64 last = 0;
        for (int frame = 0; frame < 4096; ++frame)
        {
            if (context.Rip == 0)
            {
                void* base = nullptr;
                return last != 0 && RtlPcToFileHeader (reinterpret_cast<void*> (last), &base) != nullptr && base == ntdll;
            }
            if (context.Rsp < low || context.Rsp + 8 > high || (context.Rsp & 7) != 0)
                return false;
            last = context.Rip;
            DWORD64 imageBase = 0;
            auto* function = RtlLookupFunctionEntry (context.Rip, &imageBase, nullptr);
            if (function == nullptr)
            {
                // a leaf function: the return address is on top of the stack. (A caller that is in no module and has no
                // unwind table is not code: the walk is reading something else than return addresses.)
                void* base = nullptr;
                if (frame > 0 && RtlPcToFileHeader (reinterpret_cast<void*> (context.Rip), &base) == nullptr)
                    return false;
                context.Rip = *reinterpret_cast<DWORD64*> (context.Rsp);
                context.Rsp += 8;
                continue;
            }
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind (UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return false;
}

bool onOwnStack (const CONTEXT& context) noexcept
{
    const auto* tib = reinterpret_cast<const NT_TIB*> (NtCurrentTeb());
    return context.Rsp >= (DWORD64) (std::uintptr_t) tib->StackLimit && context.Rsp + 8 <= (DWORD64) (std::uintptr_t) tib->StackBase
           && (context.Rsp & 7) == 0;
}

// The words of the calling thread's stack above `context`'s stack pointer that point into loaded modules, as
// "rsp+0x… module+0x…" lines (at most 96, the nearest 32 KiB)
std::string stackWordsInModules (const CONTEXT& context)
{
    if (! onOwnStack (context))
        return {};
    const auto high = (DWORD64) (std::uintptr_t) reinterpret_cast<const NT_TIB*> (NtCurrentTeb())->StackBase;
    const auto end = std::min<DWORD64> (high, context.Rsp + 32768);
    std::string text;
    int lines = 0;
    for (auto at = context.Rsp; at + 8 <= end && lines < 96; at += 8)
    {
        const auto word = *reinterpret_cast<const DWORD64*> (at);
        HMODULE module = nullptr;
        if (word < 0x10000 || ! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                    reinterpret_cast<LPCWSTR> (word), &module) || module == nullptr)
            continue;
        char offset[40];
        std::snprintf (offset, sizeof (offset), "rsp+0x%04llx  ", (unsigned long long) (at - context.Rsp));
        text += offset + describeAddress (reinterpret_cast<const void*> (word)) + "\n";
        ++lines;
    }
    return text;
}

// Whether `context` is Windows' exception dispatch itself faulting: the fault is in ntdll and one of the nearest frames
// is KiUserExceptionDispatcher (the dispatch of an earlier exception of this thread is under it). No C++ objects here.
bool insideExceptionDispatch (const EXCEPTION_RECORD& record, const CONTEXT& context) noexcept
{
    static void* const ntdll = GetModuleHandleW (L"ntdll.dll");
    static const auto dispatcher = ntdll != nullptr ? (std::uintptr_t) reinterpret_cast<void*> (GetProcAddress (static_cast<HMODULE> (ntdll), "KiUserExceptionDispatcher"))
                                                    : (std::uintptr_t) 0;
    void* base = nullptr;
    if (dispatcher == 0 || RtlPcToFileHeader (record.ExceptionAddress, &base) == nullptr || base != ntdll)
        return false;
    __try
    {
        std::uintptr_t frames[8] = {};
        const auto n = unwind (context, frames, 8);
        for (int i = 1; i < n; ++i)
            if (frames[i] >= dispatcher && frames[i] < dispatcher + 0x100)
                return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return false;
}

// Whether a fault on a stack that does not walk is the app's own: the first of the fault address and the nearest
// words on the stack that is in a module outside Windows' folder is in the app's .exe (the FPC fault: strcmp in
// ucrtbase called from LYNX.exe). A fault in a plugin's module, or in code no module holds, is not.
bool faultIsTheApps (const EXCEPTION_RECORD& record, const CONTEXT& context) noexcept
{
    const auto self = static_cast<void*> (GetModuleHandleW (nullptr));
    wchar_t windows[MAX_PATH] = {};
    const auto windowsLength = GetWindowsDirectoryW (windows, MAX_PATH);
    const auto high = (DWORD64) (std::uintptr_t) reinterpret_cast<const NT_TIB*> (NtCurrentTeb())->StackBase;
    auto address = (DWORD64) (std::uintptr_t) record.ExceptionAddress;
    auto at = context.Rsp;
    for (int candidates = 0; candidates < 5;)
    {
        void* base = nullptr;
        if (address >= 0x10000 && RtlPcToFileHeader (reinterpret_cast<void*> (address), &base) != nullptr && base != nullptr)
        {
            ++candidates;
            if (base == self)
                return true;
            wchar_t path[MAX_PATH] = {};
            const auto n = GetModuleFileNameW (static_cast<HMODULE> (base), path, MAX_PATH);
            const auto system = n > 0 && windowsLength > 0 && windowsLength < MAX_PATH
                                && CompareStringOrdinal (path, (int) windowsLength, windows, (int) windowsLength, TRUE) == CSTR_EQUAL;
            if (! system)
                return false;
        }
        else if (candidates == 0)
            return false; // (the fault itself is in no module: generated code)
        if (at + 8 > high || at > context.Rsp + 512)
            return false;
        address = *reinterpret_cast<const DWORD64*> (at);
        at += 8;
    }
    return false;
}

void (*unwalkableFaultHandler) (void*) = nullptr;
std::atomic<bool> unwalkableFaultReported { false };
std::atomic<DWORD> hookBusyThread { 0 };
// the newest processor fault on a stack that does not walk (one is enough: the next thing its thread does is the
// dispatch that fails)
struct PendingFault
{
    std::atomic<DWORD> thread { 0 };
    EXCEPTION_RECORD record {};
    CONTEXT context {};
};
PendingFault pendingFault;

LONG CALLBACK firstChance (EXCEPTION_POINTERS* ep) noexcept
{
    if (ep == nullptr || ep->ExceptionRecord == nullptr || ep->ContextRecord == nullptr || unwalkableFaultHandler == nullptr
        || ! crashreport::processorFault ((std::uint32_t) ep->ExceptionRecord->ExceptionCode) || unwalkableFaultReported.load())
        return EXCEPTION_CONTINUE_SEARCH;
    const auto thread = GetCurrentThreadId();
    if (hookBusyThread.load() == thread) // (a fault of the hook's own work)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD free = 0;
    if (! hookBusyThread.compare_exchange_strong (free, thread))
        return EXCEPTION_CONTINUE_SEARCH; // (two threads faulting at once: one report is enough)

    if (insideExceptionDispatch (*ep->ExceptionRecord, *ep->ContextRecord))
    {
        // Windows' dispatch of an earlier fault of this thread has faulted itself: no handler will ever run, the dispatch
        // of this fault walks the same stack and faults again until the stack is used up. Report the earlier fault (the
        // real one) and end as the top-level filter would have.
        if (! unwalkableFaultReported.exchange (true))
        {
            EXCEPTION_POINTERS original { ep->ExceptionRecord, ep->ContextRecord };
            if (pendingFault.thread.load() == thread)
                original = { &pendingFault.record, &pendingFault.context };
            const auto code = original.ExceptionRecord->ExceptionCode;
            unwalkableFaultHandler (&original);
            TerminateProcess (GetCurrentProcess(), (UINT) code);
        }
    }
    else if (onOwnStack (*ep->ContextRecord) && ! walksToThreadStart (*ep->ContextRecord))
    {
        // The app's own fault: no handler can be reached (the walk to any of them fails) and no plugin is playing with
        // exceptions here, so this is the crash. Report it and end as the top-level filter would have.
        if (faultIsTheApps (*ep->ExceptionRecord, *ep->ContextRecord) && ! unwalkableFaultReported.exchange (true))
        {
            const auto code = ep->ExceptionRecord->ExceptionCode;
            unwalkableFaultHandler (ep);
            TerminateProcess (GetCurrentProcess(), (UINT) code);
        }
        // Somebody else's (a plugin's module, generated code): remembered only. A plugin's copy protection may fault on
        // purpose and handle it in a hook of its own; if nothing does, see above.
        pendingFault.thread = 0;
        pendingFault.record = *ep->ExceptionRecord;
        pendingFault.record.ExceptionRecord = nullptr;
        pendingFault.context = *ep->ContextRecord;
        pendingFault.thread = thread;
    }
    hookBusyThread = 0;
    return EXCEPTION_CONTINUE_SEARCH;
}

std::vector<std::uintptr_t> sampleThread (HANDLE thread)
{
    std::uintptr_t frames[64] = {};
    int count = 0;
    if (thread == nullptr || SuspendThread (thread) == (DWORD) -1)
        return {};
    CONTEXT context {};
    context.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext (thread, &context))
        count = unwind (context, frames, 64);
    ResumeThread (thread);
    return std::vector<std::uintptr_t> (frames, frames + count);
}
#elif defined(_WIN32)
std::vector<std::uintptr_t> sampleThread (HANDLE) { return {}; }
#endif

} // namespace

#if defined(LYNX_UNWIND) && ! defined(LYNX_EXPORT_VERSION)
 #define LYNX_WIN_THREAD_GUARD 1
// Round 190oh (plugin host review): round 190gg's guard for faults on a hosted plugin's OWN threads, on Windows. A thread
// the plugin started (its renderer, a disk streamer, a licence check) runs no LYNX call, so call() cannot catch its
// fault and it reached the crash handler: LYNX ended. Windows calls the top-level filter (JUCE's, then onCrash) only
// for a fault nobody handled — after the plugin's own handlers (copy protections that fault on purpose and handle it
// are never touched here). parkWindowsPluginThread, the first thing onCrash does, then parks the thread for good when
// ALL of: the fault is one that may be caught at all (crashreport::editorFaultCatchable: not a heap corruption, a stack
// overflow or a fail-fast); the thread's frames walk to its first frame in ntdll; no frame is LYNX's executable; and the
// first frame outside Windows' own modules is in a module given to watchPluginModule (the plugin's file, or a file in
// its bundle folder). The app then switches every instance of that plugin off (engine/PluginHost.h,
// switchOffPluginsOfThreadFaults), as on Linux / macOS. Anything else goes on to the crash file as before.
namespace
{
constexpr int winWatchedCapacity = 128;
struct WinWatched
{
    wchar_t path[1024] {};
    char utf8[1024] {};
    std::atomic<int> ready { 0 };
};
WinWatched winWatched[winWatchedCapacity];
std::atomic<int> winWatchedCount { 0 };

struct WinThreadFault
{
    std::atomic<int> module { -1 };
    std::atomic<std::uint32_t> code { 0 };
    std::atomic<std::uintptr_t> offset { 0 };
};
constexpr int winThreadFaultCapacity = 16;
WinThreadFault winThreadFaults[winThreadFaultCapacity];
std::atomic<int> winThreadFaultTotal { 0 };
std::atomic<int> winThreadFaultChecking { 0 };

// the watched module `path` is (its file, or a file in its bundle folder; case and slash kind ignored), -1 = none
int winWatchedModuleOf (const wchar_t* path) noexcept
{
    const auto same = [] (wchar_t a, wchar_t b) {
        if (a == L'/')
            a = L'\\';
        if (b == L'/')
            b = L'\\';
        return std::towupper ((std::wint_t) a) == std::towupper ((std::wint_t) b);
    };
    const auto count = std::min (winWatchedCount.load (std::memory_order_acquire), winWatchedCapacity);
    for (int i = 0; i < count; ++i)
    {
        if (winWatched[i].ready.load (std::memory_order_acquire) != 1)
            continue;
        const wchar_t* w = winWatched[i].path;
        std::size_t k = 0;
        while (w[k] != 0 && path[k] != 0 && same (w[k], path[k]))
            ++k;
        if (w[k] == 0 && (path[k] == 0 || path[k] == L'\\' || path[k] == L'/'))
            return i;
    }
    return -1;
}

// a module of Windows itself (System32, SysWOW64, WinSxS...), as the editor guard's systemModule
bool winSystemModule (const wchar_t* path) noexcept
{
    wchar_t windows[MAX_PATH] = {};
    const auto n = GetWindowsDirectoryW (windows, MAX_PATH);
    return n > 0 && n < MAX_PATH && CompareStringOrdinal (path, (int) n, windows, (int) n, TRUE) == CSTR_EQUAL;
}

// the frames of `context` (SEH frame: a broken stack ends the walk, no C++ objects here)
int unwindGuarded (const CONTEXT& context, std::uintptr_t* frames, int capacity) noexcept
{
    __try
    {
        return unwind (context, frames, capacity);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool parkWindowsPluginThread (EXCEPTION_POINTERS* ep) noexcept
{
    if (ep == nullptr || ep->ExceptionRecord == nullptr || ep->ContextRecord == nullptr
        || winWatchedCount.load (std::memory_order_acquire) == 0)
        return false;
    const auto code = (std::uint32_t) ep->ExceptionRecord->ExceptionCode;
    if (! crashreport::editorFaultCatchable (code) || ! walksToThreadStart (*ep->ContextRecord))
        return false;
    auto idle = 0;
    if (! winThreadFaultChecking.compare_exchange_strong (idle, 1))
        return false; // (another fault is being looked at: the crash handler)
    static std::uintptr_t frames[256]; // (one checker at a time: the flag above)
    const auto count = unwindGuarded (*ep->ContextRecord, frames, 256);
    const auto self = GetModuleHandleW (nullptr);
    int watchedIndex = -1, blamed = -1;
    std::uintptr_t blamedBase = 0;
    for (int i = 0; i < count; ++i)
    {
        HMODULE module = nullptr;
        if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR> (frames[i]), &module) || module == nullptr)
        {
            if (blamed < 0)
                break; // generated code before any module: nobody to blame
            continue;
        }
        if (module == self)
        {
            watchedIndex = -1; // a LYNX frame anywhere on the thread: LYNX's own thread (or a LYNX call): a crash
            blamed = count;
            break;
        }
        static wchar_t path[1024];
        if (GetModuleFileNameW (module, path, 1024) == 0)
            continue;
        if (blamed < 0 && ! winSystemModule (path))
        {
            blamed = i;
            blamedBase = (std::uintptr_t) module;
            watchedIndex = winWatchedModuleOf (path);
            if (watchedIndex < 0)
                break; // a module nobody watches: the crash handler
        }
    }
    if (watchedIndex < 0 || blamed < 0 || blamed >= count)
    {
        winThreadFaultChecking.store (0);
        return false;
    }
    const auto n = winThreadFaultTotal.load (std::memory_order_relaxed);
    auto& slot = winThreadFaults[n % winThreadFaultCapacity];
    slot.module.store (watchedIndex, std::memory_order_relaxed);
    slot.code.store (code, std::memory_order_relaxed);
    slot.offset.store (frames[blamed] - blamedBase, std::memory_order_relaxed);
    winThreadFaultTotal.store (n + 1, std::memory_order_release);
    winThreadFaultChecking.store (0);
    for (;;) // parked for good: the thread is the plugin's; whatever it held stays held
        Sleep (INFINITE);
}
} // namespace
#endif

std::string describeAddress (const void* address)
{
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR> (address), &module) || module == nullptr)
    {
        char text[40];
        std::snprintf (text, sizeof (text), "0x%llx", (unsigned long long) (std::uintptr_t) address);
        return text;
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW (module, path, MAX_PATH);
    const wchar_t* name = path;
    for (const wchar_t* c = path; *c != 0; ++c)
        if (*c == L'\\' || *c == L'/')
            name = c + 1;
    char offset[40];
    std::snprintf (offset, sizeof (offset), "+0x%llx", (unsigned long long) ((std::uintptr_t) address - (std::uintptr_t) module));
    return narrow (name) + offset;
#elif defined(LYNX_EXECINFO)
    Dl_info info {};
    if (dladdr (address, &info) == 0 || info.dli_fname == nullptr)
    {
        char text[40];
        std::snprintf (text, sizeof (text), "0x%llx", (unsigned long long) (std::uintptr_t) address);
        return text;
    }
    std::string name = info.dli_fname;
    if (const auto slash = name.find_last_of ('/'); slash != std::string::npos)
        name = name.substr (slash + 1);
    char offset[40];
    std::snprintf (offset, sizeof (offset), "+0x%llx", (unsigned long long) ((std::uintptr_t) address - (std::uintptr_t) info.dli_fbase));
    name += offset;
    if (info.dli_sname != nullptr)
        name += std::string ("  (") + info.dli_sname + ")";
    return name;
#else
    (void) address;
    return {};
#endif
}

std::string moduleBacktrace (int skip)
{
    try
    {
#if defined(_WIN32)
        void* raw[96] = {};
        const auto n = RtlCaptureStackBackTrace ((DWORD) (skip + 1), 96, raw, nullptr);
        std::vector<std::uintptr_t> frames;
        for (USHORT i = 0; i < n; ++i)
            frames.push_back ((std::uintptr_t) raw[i]);
        return formatFrames (frames);
#elif defined(LYNX_EXECINFO)
        void* raw[96] = {};
        const auto n = backtrace (raw, 96);
        std::vector<std::uintptr_t> frames;
        for (int i = skip + 1; i < n; ++i)
            frames.push_back ((std::uintptr_t) raw[i]);
        return formatFrames (frames);
#else
        (void) skip;
        return {};
#endif
    }
    catch (...) { return {}; }
}

std::string exceptionBacktrace (void* exceptionPointers)
{
    try
    {
#if defined(LYNX_UNWIND)
        auto* ep = static_cast<EXCEPTION_POINTERS*> (exceptionPointers);
        if (ep == nullptr || ep->ContextRecord == nullptr)
            return {};
        std::string text;
        if (ep->ExceptionRecord != nullptr)
        {
            char head[160];
            std::snprintf (head, sizeof (head), "exception 0x%08lx (%s) at ", (unsigned long) ep->ExceptionRecord->ExceptionCode,
                           describe ((std::uint32_t) ep->ExceptionRecord->ExceptionCode));
            text = head + describeAddress (ep->ExceptionRecord->ExceptionAddress) + "\n";
        }
        std::uintptr_t frames[96] = {};
        auto n = unwind (*ep->ContextRecord, frames, 96);
        const auto unwalkable = onOwnStack (*ep->ContextRecord) && ! walksToThreadStart (*ep->ContextRecord);
        if (unwalkable) // (the frames end where the walk stops reading code addresses)
            for (int i = 1; i < n; ++i)
                if (void* base = nullptr; RtlPcToFileHeader (reinterpret_cast<void*> (frames[i]), &base) == nullptr)
                {
                    n = i + 1;
                    break;
                }
        text += formatFrames (std::vector<std::uintptr_t> (frames, frames + n));
        if (unwalkable)
            if (const auto words = stackWordsInModules (*ep->ContextRecord); ! words.empty())
                text += "--- the stack could not be walked to the thread's first frame; the words on it that point into modules "
                        "(the callers are among them)\n" + words;
        return text;
#elif defined(LYNX_EXECINFO)
        // macOS / Linux (round 161): JUCE's crash handler passes the signal number; the frames are this thread's, the
        // signal handler's own first (exceptionBacktrace, onCrash, JUCE's handleCrash, the kernel's signal trampoline)
        const auto sig = (int) (std::intptr_t) exceptionPointers;
        const char* name = sig == SIGSEGV ? "segmentation fault" : sig == SIGBUS ? "bus error" : sig == SIGILL ? "illegal instruction"
                         : sig == SIGFPE ? "floating-point exception" : sig == SIGABRT ? "abort" : "signal";
        char head[80];
        std::snprintf (head, sizeof (head), "signal %d (%s)\n", sig, name);
        return head + moduleBacktrace (1);
#else
        (void) exceptionPointers;
        return {};
#endif
    }
    catch (...) { return {}; }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((noinline))
#endif
void faultForTest (bool unwalkable) noexcept
{
    volatile std::uintptr_t marker = 0;
#if defined(LYNX_UNWIND)
    if (unwalkable)
    {
        // every word of the stack above this frame (the return addresses of all the callers): this never returns
        const auto* tib = reinterpret_cast<const NT_TIB*> (NtCurrentTeb());
        for (auto* word = const_cast<volatile std::uintptr_t*> (&marker) + 1; word < static_cast<volatile std::uintptr_t*> (tib->StackBase); ++word)
            *word = 0x10;
    }
#else
    (void) unwalkable;
#endif
    *reinterpret_cast<volatile int*> (marker + 0x10) = 1;
    std::_Exit (3); // (not reached)
}

namespace
{
#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((noinline))
#endif
void copyFromNullForTest() noexcept
{
    // what nih-plug did in issue #155 (memcpy from a null pointer: the fault is in the C library), but called by LYNX
    char to[64];
    const char* volatile from = nullptr;
    volatile std::size_t size = sizeof (to); // (a size the compiler can't see: a real call into the C library)
    std::memcpy (to, (const char*) from, size);
    std::fprintf (stderr, "%c", to[0]);
}
} // namespace

std::uint32_t faultInGuardedCallForTest (bool inCLibrary) noexcept
{
    return run ([inCLibrary] {
        if (inCLibrary)
            copyFromNullForTest();
        else
            faultForTest (false);
    });
}

void watchPluginModule (const std::string& fileUtf8) noexcept
{
#if defined(LYNX_PLUGIN_THREAD_GUARD)
    if (fileUtf8.empty() || fileUtf8.front() != '/' || fileUtf8.size() >= sizeof (WatchedModule::path))
        return; // (an identifier such as an LV2 URI, or a path too long: not watched)
    auto path = fileUtf8;
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();
    static std::mutex writers; // (one writer at a time; the handler never waits for it)
    const std::lock_guard<std::mutex> lock (writers);
    const auto count = watchedCount.load (std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
        if (path == watched[i].path)
            return;
    if (count >= watchedCapacity)
        return;
    std::memcpy (watched[count].path, path.c_str(), path.size() + 1);
    watched[count].ready.store (1, std::memory_order_release);
    watchedCount.store (count + 1, std::memory_order_release);
    if (guardState.load (std::memory_order_acquire) != 2)
        installFaultGuard(); // (the handler that parks the thread; hardenSignals installs it in the app anyway)
#elif defined(LYNX_WIN_THREAD_GUARD)
    // round 190oh: Windows (an absolute path: a drive letter or a UNC path; the slashes kept as given)
    if (fileUtf8.size() < 3 || fileUtf8.size() >= sizeof (WinWatched::utf8) || ! (fileUtf8[1] == ':' || (fileUtf8[0] == '\\' && fileUtf8[1] == '\\')))
        return;
    auto path = fileUtf8;
    while (path.size() > 3 && (path.back() == '/' || path.back() == '\\'))
        path.pop_back();
    wchar_t wide[1024] = {};
    if (MultiByteToWideChar (CP_UTF8, 0, path.c_str(), -1, wide, 1024) == 0)
        return;
    static std::mutex writers;
    const std::lock_guard<std::mutex> lock (writers);
    const auto count = winWatchedCount.load (std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
        if (path == winWatched[i].utf8)
            return;
    if (count >= winWatchedCapacity)
        return;
    std::memcpy (winWatched[count].utf8, path.c_str(), path.size() + 1);
    std::memcpy (winWatched[count].path, wide, sizeof (wide));
    winWatched[count].ready.store (1, std::memory_order_release);
    winWatchedCount.store (count + 1, std::memory_order_release);
#else
    (void) fileUtf8;
#endif
}

bool parkFaultingPluginThread (void* exceptionPointers) noexcept
{
#if defined(LYNX_WIN_THREAD_GUARD)
    return parkWindowsPluginThread (static_cast<EXCEPTION_POINTERS*> (exceptionPointers)); // (returns only when it did not park)
#else
    (void) exceptionPointers;
    return false;
#endif
}

bool pluginThreadFaultsCaught() noexcept
{
#if (defined(LYNX_PLUGIN_THREAD_GUARD) && (defined(__x86_64__) || defined(__aarch64__))) || defined(LYNX_WIN_THREAD_GUARD)
    return true;
#else
    return false;
#endif
}

int pluginThreadFaultCount() noexcept
{
#if defined(LYNX_PLUGIN_THREAD_GUARD)
    return threadFaultTotal.load (std::memory_order_acquire);
#elif defined(LYNX_WIN_THREAD_GUARD)
    return winThreadFaultTotal.load (std::memory_order_acquire);
#else
    return 0;
#endif
}

std::vector<PluginThreadFault> pluginThreadFaults()
{
    std::vector<PluginThreadFault> list;
#if defined(LYNX_PLUGIN_THREAD_GUARD)
    const auto total = threadFaultTotal.load (std::memory_order_acquire);
    for (int i = std::max (0, total - threadFaultCapacity); i < total; ++i)
    {
        const auto& slot = threadFaultSlots[i % threadFaultCapacity];
        const auto module = slot.module.load (std::memory_order_relaxed);
        if (module < 0 || module >= watchedCapacity)
            continue;
        PluginThreadFault f;
        f.module = watched[module].path;
        f.code = slot.code.load (std::memory_order_relaxed);
        char offset[40];
        std::snprintf (offset, sizeof (offset), "+0x%llx", (unsigned long long) slot.offset.load (std::memory_order_relaxed));
        auto name = f.module;
        if (const auto slash = name.find_last_of ('/'); slash != std::string::npos)
            name = name.substr (slash + 1);
        f.where = name + offset;
        list.push_back (std::move (f));
    }
#elif defined(LYNX_WIN_THREAD_GUARD)
    const auto total = winThreadFaultTotal.load (std::memory_order_acquire);
    for (int i = std::max (0, total - winThreadFaultCapacity); i < total; ++i)
    {
        const auto& slot = winThreadFaults[i % winThreadFaultCapacity];
        const auto module = slot.module.load (std::memory_order_relaxed);
        if (module < 0 || module >= winWatchedCapacity)
            continue;
        PluginThreadFault f;
        f.module = winWatched[module].utf8;
        f.code = slot.code.load (std::memory_order_relaxed);
        char offset[40];
        std::snprintf (offset, sizeof (offset), "+0x%llx", (unsigned long long) slot.offset.load (std::memory_order_relaxed));
        auto name = f.module;
        if (const auto slash = name.find_last_of ("/\\"); slash != std::string::npos)
            name = name.substr (slash + 1);
        f.where = name + offset;
        list.push_back (std::move (f));
    }
#endif
    return list;
}

void faultOnOwnThreadForTest (bool inCLibrary) noexcept
{
#if defined(LYNX_PLUGIN_THREAD_GUARD)
    // the worst case: LYNX's own executable registered as if it were a plugin's module
    Dl_info self {};
    if (dladdr (reinterpret_cast<const void*> (&faultOnOwnThreadForTest), &self) != 0 && self.dli_fname != nullptr)
        watchPluginModule (self.dli_fname);
    std::thread ([inCLibrary] {
        if (inCLibrary)
            copyFromNullForTest();
        else
            faultForTest (false);
    }).detach();
    std::this_thread::sleep_for (std::chrono::seconds (5)); // (the crash handler ends the process long before)
#else
    (void) inCLibrary;
#endif
}

bool pluginFaultsCaught() noexcept
{
#if defined(_WIN32) && (defined(_MSC_VER) || defined(__clang__))
    return true; // SEH
#elif (defined(__linux__) || defined(__APPLE__)) && (defined(__x86_64__) || defined(__aarch64__))
    return true; // signals (programCounter knows these)
#else
    return false;
#endif
}

void setUnwalkableFaultHandler (void (*handler) (void*)) noexcept
{
#if defined(LYNX_UNWIND)
    static std::atomic<bool> installed { false };
    unwalkableFaultHandler = handler;
    if (handler != nullptr && ! installed.exchange (true))
        AddVectoredExceptionHandler (0, firstChance); // (last in the chain: a plugin's own hooks added first run first)
#else
    (void) handler;
#endif
}

namespace
{
struct HangWatch
{
    std::mutex lock;
    std::condition_variable wake;
    std::thread thread;
    bool quit = false;
    std::atomic<std::int64_t> lastBeat { 0 };
    std::string folder, lastFile;
    int stallMs = 2000;
#if defined(_WIN32)
    HANDLE uiThread = nullptr;
#else
    pthread_t uiThread {};
#endif
};
HangWatch& hangWatch()
{
    static HangWatch w;
    return w;
}

std::int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

#if defined(LYNX_EXECINFO)
// the message thread records its own frames in a signal handler (backtrace() was called once beforehand, so it
// allocates nothing here); the watcher waits for them
std::atomic<int> sampledCount { -1 };
void* sampledFrames[64];
#if defined(__linux__) && defined(__aarch64__)
// Round 190oy (Linux arm64): on GitHub's arm64 runner (a VM) a thread spinning on the clock was sampled inside the vDSO
// every time, and backtrace() stopped there (the hang file held the one frame "linux-vdso.so.1+0x584"). A sample that
// short is completed from the interrupted registers instead: the PC, the link register (the vDSO function is a leaf),
// then the chain of AArch64 frame records (x29 -> {caller's x29, return address}; GCC keeps them in non-leaf functions
// by default). Each record must lie above the last one (the first at or above the interrupted stack pointer) and inside
// the thread's stack, so the walk reads nothing but that stack.
std::uintptr_t sampledStackTop = 0; // the sampled (UI) thread's stack end, from startHangWatch (0: unknown)
void onSampleSignal (int, siginfo_t*, void* context)
{
    auto n = backtrace (sampledFrames, 64);
    if (n < 5 && context != nullptr) // (fewer than 3 frames beyond the handler and the trampoline)
    {
        const auto& mc = static_cast<const ucontext_t*> (context)->uc_mcontext;
        // return addresses can carry a pointer-authentication code in their top bits (CPUs with PAC; seen on the arm
        // runner): user addresses on Linux arm64 have 48 bits
        const auto strip = [] (std::uint64_t a) { return reinterpret_cast<void*> (a & ((std::uint64_t (1) << 48) - 1)); };
        n = 2; // (sampleThread drops the first two: the handler and the trampoline)
        sampledFrames[n++] = reinterpret_cast<void*> (mc.pc);
        if (mc.regs[30] != 0)
            sampledFrames[n++] = strip (mc.regs[30]);
        const auto sp = (std::uintptr_t) mc.sp;
        auto fp = (std::uintptr_t) mc.regs[29], last = sp - 1; // (the interrupted function's own record can be at sp)
        const auto top = sampledStackTop != 0 ? sampledStackTop : sp + (1u << 20);
        while (n < 64 && fp > last && fp + 16 <= top && (fp & 15u) == 0)
        {
            const auto* record = reinterpret_cast<const std::uintptr_t*> (fp);
            const auto ret = record[1];
            if (ret == 0)
                break;
            if (n == 3 || strip (ret) != sampledFrames[n - 1]) // (the link register already gave the first caller)
                sampledFrames[n++] = strip (ret);
            last = fp;
            fp = record[0];
        }
    }
    sampledCount.store (n, std::memory_order_release);
}
#else
void onSampleSignal (int)
{
    sampledCount.store (backtrace (sampledFrames, 64), std::memory_order_release);
}
#endif

std::vector<std::uintptr_t> sampleOnce (pthread_t thread)
{
    sampledCount.store (-1);
    if (pthread_kill (thread, SIGPROF) != 0)
        return {};
    for (int i = 0; i < 100 && sampledCount.load (std::memory_order_acquire) < 0; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    const auto n = sampledCount.load (std::memory_order_acquire);
    std::vector<std::uintptr_t> frames;
    for (int i = 2; i < n; ++i) // (the handler and the signal trampoline first)
        frames.push_back ((std::uintptr_t) sampledFrames[i]);
    return frames;
}

std::vector<std::uintptr_t> sampleThread (pthread_t thread)
{
    // Round 190oy (Linux arm64): GitHub's arm64 runner sampled only 2 frames of a thread spinning on the clock (most
    // likely the signal landed in the vDSO's clock_gettime, which the unwinder can't leave there). A stack shorter than
    // 3 frames is sampled again, up to 5 times 10 ms apart, and the longest one is kept; a full first sample (x86) is
    // kept as it is
    auto frames = sampleOnce (thread);
    for (int retry = 0; retry < 5 && frames.size() < 3; ++retry)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
        auto again = sampleOnce (thread);
        if (again.size() > frames.size())
            frames = std::move (again);
    }
    return frames;
}
#endif

std::string timeStamp (const char* format)
{
    char stamp[40] = {};
    const auto now = std::time (nullptr);
    if (const auto* t = std::localtime (&now))
        std::strftime (stamp, sizeof (stamp), format, t);
    return stamp;
}

void watchLoop()
{
    auto& w = hangWatch();
    std::int64_t reportedStallStart = -1;
    int samples = 0;
    std::string file;
    for (;;)
    {
        {
            std::unique_lock<std::mutex> l (w.lock);
            cvwait::waitFor (w.wake, l, std::chrono::milliseconds (250), [&w] { return w.quit; }); // (round 190la: platform/CvWait.h)
            if (w.quit)
                return;
        }
        const auto beat = w.lastBeat.load();
        const auto stalled = nowMs() - beat;
        if (stalled < w.stallMs)
        {
            if (reportedStallStart >= 0)
            {
                note ("UI responding again after " + std::to_string ((nowMs() - reportedStallStart) / 1000) + " s");
                reportedStallStart = -1;
                samples = 0;
            }
            continue;
        }
        // a stall: one sample at the start, another later (a loop shows the same frames, a wait another)
        const bool first = reportedStallStart < 0;
        if (! first && (samples >= 2 || stalled < 4 * (std::int64_t) w.stallMs))
            continue;
        if (first)
        {
            reportedStallStart = beat;
            file = w.folder + "/hang " + timeStamp ("%Y-%m-%d %H-%M-%S") + ".txt";
        }
        ++samples;
        std::string text;
#if defined(_WIN32) || defined(LYNX_EXECINFO)
        text = formatFrames (sampleThread (w.uiThread));
#endif
        const auto header = "LYNX UI thread not responding for " + std::to_string (stalled) + " ms (sample " + std::to_string (samples) + ")\n";
        if (auto* f = openUtf8 (file, first ? "wb" : "ab"))
        {
            const auto all = header + (text.empty() ? std::string ("(no stack on this system)\n") : text) + "\n";
            std::fwrite (all.data(), 1, all.size(), f);
            std::fclose (f);
        }
        {
            const std::lock_guard<std::mutex> l (w.lock);
            w.lastFile = file;
        }
        if (first)
        {
            const auto slash = file.find_last_of ('/');
            note ("UI not responding: stack in autosave\\" + file.substr (slash == std::string::npos ? 0 : slash + 1));
        }
    }
}
} // namespace

#if ! defined(_WIN32)
namespace
{
int endSignalPipe[2] = { -1, -1 };

void onEndSignal (int sig)
{
    // async-signal-safe: only write(); the thread below notes it (the note takes a lock and allocates)
    const auto byte = (unsigned char) sig;
    if (endSignalPipe[1] >= 0)
        (void) ::write (endSignalPipe[1], &byte, 1);
}

std::string endSignalStep (int sig)
{
    return sig == SIGTERM ? "ended by SIGTERM (the system or another program asked LYNX to close)"
         : sig == SIGHUP  ? "ended by SIGHUP (the terminal or session that started LYNX closed)"
                          : "ended by signal " + std::to_string (sig);
}
} // namespace
#endif

void hardenSignals() noexcept
{
#if ! defined(_WIN32)
    try
    {
        // the alternate stack (the handler writes files and a backtrace: generous)
        static std::vector<char> altStack (std::max<std::size_t> (2u << 20, (std::size_t) SIGSTKSZ));
        stack_t ss {};
        ss.ss_sp = altStack.data();
        ss.ss_size = altStack.size();
        ss.ss_flags = 0;
        if (sigaltstack (&ss, nullptr) == 0)
            for (const auto sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS })
            {
                struct sigaction action {};
                if (sigaction (sig, nullptr, &action) == 0 && action.sa_handler != SIG_DFL && action.sa_handler != SIG_IGN)
                {
                    action.sa_flags |= SA_ONSTACK;
                    sigaction (sig, &action, nullptr);
                }
            }
        // round 190dt: the plugin-call guard (call()), on top of the crash handler it passes LYNX's own faults to
        installFaultGuard();

        if (endSignalPipe[0] < 0 && ::pipe (endSignalPipe) == 0)
        {
            std::thread ([] {
                unsigned char byte = 0;
                while (::read (endSignalPipe[0], &byte, 1) != 1)
                    if (errno != EINTR)
                        return;
                const int sig = byte;
                note (endSignalStep (sig)); // (the session file is written and closed here)
                ::signal (sig, SIG_DFL);
                ::raise (sig); // ends as before
            }).detach();
            for (const auto sig : { SIGTERM, SIGHUP })
            {
                struct sigaction action {};
                if (sigaction (sig, nullptr, &action) == 0 && action.sa_handler == SIG_IGN)
                    continue; // (started with it ignored, e.g. nohup: keep that)
                action = {};
                action.sa_handler = onEndSignal;
                sigemptyset (&action.sa_mask);
                action.sa_flags = SA_RESTART;
                sigaction (sig, &action, nullptr);
            }
        }
    }
    catch (...) {}
#endif
}

void startHangWatch (const std::string& folderUtf8, int stallMs) noexcept
{
    try
    {
        auto& w = hangWatch();
        if (w.thread.joinable())
            return;
        w.folder = folderUtf8;
        w.stallMs = stallMs;
        w.quit = false;
        w.lastBeat = nowMs();
#if defined(_WIN32)
        DuplicateHandle (GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &w.uiThread,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
#else
        w.uiThread = pthread_self();
 #if defined(LYNX_EXECINFO)
        {
            void* warm[4];
            backtrace (warm, 4); // (loads the unwinder now, not inside the signal handler)
            struct sigaction action {};
  #if defined(__linux__) && defined(__aarch64__)
            {
                pthread_attr_t attr;
                if (pthread_getattr_np (pthread_self(), &attr) == 0)
                {
                    void* base = nullptr;
                    std::size_t size = 0;
                    if (pthread_attr_getstack (&attr, &base, &size) == 0)
                        sampledStackTop = (std::uintptr_t) base + size;
                    pthread_attr_destroy (&attr);
                }
            }
            action.sa_sigaction = onSampleSignal;
            sigemptyset (&action.sa_mask);
            action.sa_flags = SA_RESTART | SA_SIGINFO;
  #else
            action.sa_handler = onSampleSignal;
            sigemptyset (&action.sa_mask);
            action.sa_flags = SA_RESTART;
  #endif
            sigaction (SIGPROF, &action, nullptr);
        }
 #endif
#endif
        w.thread = std::thread (watchLoop);
    }
    catch (...) {}
}

void heartbeat() noexcept
{
    hangWatch().lastBeat.store (nowMs());
}

void stopHangWatch() noexcept
{
    try
    {
        auto& w = hangWatch();
        if (! w.thread.joinable())
            return;
        {
            const std::lock_guard<std::mutex> l (w.lock);
            w.quit = true;
        }
        w.wake.notify_all();
        w.thread.join();
#if defined(_WIN32)
        if (w.uiThread != nullptr)
            CloseHandle (w.uiThread);
        w.uiThread = nullptr;
#endif
    }
    catch (...) {}
}

std::string symbolizeOwnFrames (const std::string& text)
{
    std::string out;
#if defined(_WIN32)
    const auto base = (std::uintptr_t) GetModuleHandleW (nullptr);
    std::size_t start = 0;
    while (start < text.size())
    {
        auto end = text.find ('\n', start);
        if (end == std::string::npos)
            end = text.size();
        auto line = text.substr (start, end - start);
        if (const auto at = line.find ("LYNX.exe+0x"); at != std::string::npos && line.find ("  (") == std::string::npos)
        {
            const auto offset = std::strtoull (line.c_str() + at + 11, nullptr, 16);
            if (const auto symbol = symbolOf ((DWORD64) (base + offset)); ! symbol.empty())
                line += "  (" + symbol + ")";
        }
        out += line + "\n";
        start = end + 1;
    }
#else
    out = text;
#endif
    return out;
}

// --- message-thread sampling profiler (round 174f) ----------------------------------------------------------------

namespace
{
struct MainProfiler
{
    std::mutex lock;
    std::condition_variable wake;
    std::thread thread;
    bool quit = false;
    std::string file;
    int intervalUs = 1000, windowS = 5;
#if defined(_WIN32)
    HANDLE uiThread = nullptr;
    std::uintptr_t stackBase = 0; // the message thread's stack top (its TEB)
#endif
};
MainProfiler& mainProfiler()
{
    static MainProfiler p;
    return p;
}

#if defined(LYNX_UNWIND)
// LYNX_PROFILE_SYMBOLS=1: LYNX.exe's own frames named in the process (its PDB). Off by default (round 190kz): dbghelp
// loads the whole ~750 MB LYNX.pdb on the first such frame, which took longer than a measurement run, so no window was
// ever written (rounds 190gr-190kr). The frames are written as "LYNX.exe+0x<rva>" and in a raw section per window that
// scripts/profile_symbolize.py names afterwards (llvm-symbolizer with the build's PDB). Other modules keep their exports.
bool symbolizeOwnFrames()
{
    static const bool on = [] {
        const auto* v = std::getenv ("LYNX_PROFILE_SYMBOLS");
        return v != nullptr && v[0] == '1';
    }();
    return on;
}

bool inOwnModule (std::uintptr_t address)
{
    HMODULE module = nullptr;
    return GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR> (address), &module)
           && module == GetModuleHandleW (nullptr);
}

// "function" of a frame: its symbol without the offset, else the module
std::string functionOf (std::uintptr_t address)
{
    if (! symbolizeOwnFrames() && inOwnModule (address))
        return describeAddress (reinterpret_cast<const void*> (address));
    auto name = symbolOf ((DWORD64) address);
    if (const auto plus = name.rfind (" + 0x"); plus != std::string::npos)
        name.resize (plus);
    auto module = describeAddress (reinterpret_cast<const void*> (address));
    if (const auto plus = module.find ("+0x"); plus != std::string::npos)
        module.resize (plus);
    return name.empty() ? module + " (?)" : module + "!" + name;
}

void writeProfileWindow (const std::string& file, double fromS, double toS, long samples, long ticks, double cpuPercent,
                         const std::map<std::uintptr_t, long>& self, const std::map<std::uintptr_t, long>& total)
{
    std::map<std::string, long> selfByName, totalByName, byModule;
    std::map<std::uintptr_t, std::string> names;
    auto nameOf = [&names] (std::uintptr_t a) -> const std::string& {
        auto it = names.find (a);
        if (it == names.end())
            it = names.emplace (a, functionOf (a)).first;
        return it->second;
    };
    for (const auto& [a, n] : self)
    {
        selfByName[nameOf (a)] += n;
        auto module = describeAddress (reinterpret_cast<const void*> (a));
        if (const auto plus = module.find ("+0x"); plus != std::string::npos)
            module.resize (plus);
        byModule[module] += n;
    }
    for (const auto& [a, n] : total)
        totalByName[nameOf (a)] += n;
    auto top = [] (const std::map<std::string, long>& m, std::size_t count) {
        std::vector<std::pair<long, std::string>> v;
        for (const auto& [k, n] : m)
            v.emplace_back (n, k);
        std::sort (v.begin(), v.end(), [] (const auto& x, const auto& y) { return x.first > y.first; });
        if (v.size() > count)
            v.resize (count);
        return v;
    };
    auto* out = std::fopen (file.c_str(), "ab");
    if (out == nullptr)
        return;
    std::fprintf (out, "=== window %.1f-%.1f s: %ld ticks, message thread CPU %.1f %% (shares below: by CPU cycles)\n", fromS, toS, ticks, cpuPercent);
    std::fprintf (out, "-- by module (self)\n");
    for (const auto& [n, k] : top (byModule, 12))
        std::fprintf (out, "%6.1f %%  %s\n", 100.0 * (double) n / (double) std::max (1L, samples), k.c_str());
    std::fprintf (out, "-- self\n");
    for (const auto& [n, k] : top (selfByName, 40))
        std::fprintf (out, "%6.1f %%  %s\n", 100.0 * (double) n / (double) std::max (1L, samples), k.c_str());
    std::fprintf (out, "-- total (self + callees)\n");
    for (const auto& [n, k] : top (totalByName, 70))
        std::fprintf (out, "%6.1f %%  %s\n", 100.0 * (double) n / (double) std::max (1L, samples), k.c_str());
    if (! symbolizeOwnFrames())
    {
        // (round 190kz) every own-module address with its weight, for scripts/profile_symbolize.py: "S|T <rva> <weight>"
        const auto base = (std::uintptr_t) GetModuleHandleW (nullptr);
        std::fprintf (out, "-- raw (LYNX.exe rva, weight; S = self, T = total; of %ld)\n", samples);
        for (const auto* m : { &self, &total })
            for (const auto& [a, n] : *m)
                if (inOwnModule (a))
                    std::fprintf (out, "%c %llx %ld\n", m == &self ? 'S' : 'T', (unsigned long long) (a - base), n);
    }
    std::fprintf (out, "-- end\n");
    std::fclose (out);
}

// One unwind step on a copied stack. The copy holds whatever the thread had: a frame register can point anywhere, so
// the unwinder may read an unmapped address (an access violation in RtlVirtualUnwind killed LYNX at start-up, round 174f).
// A fault only ends this sample's stack. No C++ objects in this frame (SEH and C++ unwinding must not share one function).
bool unwindStepGuarded (DWORD64 imageBase, DWORD64 rip, PRUNTIME_FUNCTION function, CONTEXT* context) noexcept
{
    __try
    {
        void* handlerData = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind (UNW_FLAG_NHANDLER, imageBase, rip, function, context, &handlerData, &establisher, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// One sample of the message thread without unwinding it while it is suspended: suspended, only its registers and the
// used part of its stack are copied (no locks, no allocation); resumed; then unwound on the copy. Unwinding needs the
// loader's function tables, whose lock the suspended thread may hold (a profiler that unwound the suspended thread
// hung LYNX at start-up while DLLs were loading).
std::vector<std::uintptr_t> sampleByCopy (HANDLE thread, std::uintptr_t stackBase, std::vector<std::uint8_t>& copy)
{
    constexpr std::size_t maxCopy = 256 * 1024;
    if (copy.size() < maxCopy)
        copy.resize (maxCopy);
    CONTEXT context {};
    context.ContextFlags = CONTEXT_FULL;
    std::size_t length = 0;
    if (SuspendThread (thread) == (DWORD) -1)
        return {};
    if (GetThreadContext (thread, &context) && context.Rsp != 0 && context.Rsp < stackBase)
    {
        length = std::min ((std::size_t) (stackBase - context.Rsp), maxCopy);
        std::memcpy (copy.data(), reinterpret_cast<const void*> (context.Rsp), length);
    }
    ResumeThread (thread);
    if (length == 0)
        return {};

    const auto low = (std::uintptr_t) context.Rsp, high = low + length;
    const auto delta = (std::intptr_t) ((std::uintptr_t) copy.data() - low);
    auto inStack = [&] (DWORD64 v) { return v >= low && v < high; };
    // registers pointing into the stack (the frame pointer, saved stack addresses) are moved onto the copy; the ones an
    // unwind step restores from the copy point at the real stack again, and any of them can be a function's frame register
    auto rebase = [&] {
        for (auto* r : { &context.Rbp, &context.Rbx, &context.Rsi, &context.Rdi, &context.R12, &context.R13, &context.R14, &context.R15 })
            if (inStack (*r))
                *r = (DWORD64) ((std::intptr_t) *r + delta);
    };
    context.Rsp = (DWORD64) ((std::intptr_t) context.Rsp + delta);
    rebase();
    const auto copyLow = (DWORD64) (std::uintptr_t) copy.data(), copyHigh = copyLow + length;

    std::vector<std::uintptr_t> frames;
    for (int count = 0; count < 64 && context.Rip != 0; ++count)
    {
        frames.push_back ((std::uintptr_t) context.Rip);
        if (context.Rsp < copyLow || context.Rsp + 8 > copyHigh)
            break;
        DWORD64 imageBase = 0;
        auto* function = RtlLookupFunctionEntry (context.Rip, &imageBase, nullptr);
        if (function == nullptr)
        {
            context.Rip = *reinterpret_cast<DWORD64*> (context.Rsp); // a leaf: the return address on top of the stack
            context.Rsp += 8;
            continue;
        }
        if (! unwindStepGuarded (imageBase, context.Rip, function, &context))
            break;
        rebase();
    }
    return frames;
}

void profileLoop()
{
    auto& p = mainProfiler();
    std::map<std::uintptr_t, long> self, total;
    long samples = 0, ticks = 0; // (samples: CPU cycles / 1000 attributed to the sampled stacks)
    ULONG64 lastCycles = 0;
    QueryThreadCycleTime (p.uiThread, &lastCycles);
    auto threadCpu100ns = [&p] {
        FILETIME created, exited, kernel, user;
        if (! GetThreadTimes (p.uiThread, &created, &exited, &kernel, &user))
            return 0.0;
        const auto v = [] (FILETIME f) { return (double) (((ULONG64) f.dwHighDateTime << 32) | f.dwLowDateTime); };
        return v (kernel) + v (user);
    };
    auto windowCpu = threadCpu100ns();
    const auto start = std::chrono::steady_clock::now();
    auto windowStart = start;
    std::vector<std::uintptr_t> seen;
    std::vector<std::uint8_t> stackCopy;
    timeBeginPeriod (1);
    for (;;)
    {
        {
            std::unique_lock<std::mutex> l (p.lock);
            if (p.wake.wait_for (l, std::chrono::microseconds (p.intervalUs), [&p] { return p.quit; }))
                break;
        }
        ++ticks;
        ULONG64 cycles = 0;
        QueryThreadCycleTime (p.uiThread, &cycles);
        // the cycles the thread used since the last sample, charged to the stack it is in now (a thread that only
        // woke for a moment and sleeps again weighs almost nothing)
        const auto weight = (long) ((cycles - lastCycles) / 1000u);
        if (weight > 0)
        {
            const auto frames = sampleByCopy (p.uiThread, p.stackBase, stackCopy);
            QueryThreadCycleTime (p.uiThread, &lastCycles); // (after the sample: its own suspend is not counted next time)
            if (! frames.empty())
            {
                samples += weight;
                self[frames.front()] += weight;
                seen.clear();
                for (const auto f : frames)
                    if (std::find (seen.begin(), seen.end(), f) == seen.end())
                    {
                        seen.push_back (f);
                        total[f] += weight;
                    }
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - windowStart >= std::chrono::seconds (p.windowS))
        {
            const auto s0 = std::chrono::duration<double> (windowStart - start).count(), s1 = std::chrono::duration<double> (now - start).count();
            const auto cpu = threadCpu100ns();
            writeProfileWindow (p.file, s0, s1, samples, ticks, (cpu - windowCpu) / 1.0e5 / std::max (1.0e-3, s1 - s0), self, total);
            windowCpu = cpu;
            self.clear();
            total.clear();
            samples = ticks = 0;
            windowStart = now;
        }
    }
    timeEndPeriod (1);
}
#endif
} // namespace

void startMainProfiler (const std::string& fileUtf8, int intervalUs, int windowS) noexcept
{
#if defined(LYNX_UNWIND)
    try
    {
        auto& p = mainProfiler();
        if (p.thread.joinable())
            return;
        p.file = fileUtf8;
        p.intervalUs = std::max (200, intervalUs);
        p.windowS = std::max (1, windowS);
        p.quit = false;
        DuplicateHandle (GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &p.uiThread,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
        p.stackBase = (std::uintptr_t) reinterpret_cast<NT_TIB*> (NtCurrentTeb())->StackBase;
        if (auto* out = std::fopen (p.file.c_str(), "wb"))
        {
            std::fprintf (out, "LYNX message-thread profile: a sample every %d us while the thread runs, windows of %d s\n", p.intervalUs, p.windowS);
            std::fclose (out);
        }
        p.thread = std::thread (profileLoop);
    }
    catch (...) {}
#else
    (void) fileUtf8;
    (void) intervalUs;
    (void) windowS;
#endif
}

void stopMainProfiler() noexcept
{
#if defined(LYNX_UNWIND)
    try
    {
        auto& p = mainProfiler();
        if (! p.thread.joinable())
            return;
        {
            const std::lock_guard<std::mutex> l (p.lock);
            p.quit = true;
        }
        p.wake.notify_all();
        p.thread.join();
        CloseHandle (p.uiThread);
        p.uiThread = nullptr;
    }
    catch (...) {}
#endif
}

// --- wall-clock stall sampler (round 190os) ------------------------------------------------------------------------

namespace
{
struct StallSampler
{
    std::mutex lock;
    std::condition_variable wake;
    std::thread thread;
    bool quit = false;
    int intervalUs = 2000;
    const std::atomic<int>* tag = nullptr;
#if defined(LYNX_UNWIND)
    struct Watched
    {
        HANDLE handle = nullptr;
        unsigned id = 0;
        std::string name;
        std::uintptr_t stackBase = 0, start = 0; // its stack's top (TEB) and start address (a driver's thread: its module)
        bool stacks = false;                     // sampled with its stack (else its cycles only, and its stack while busy)
        bool audio = false;                      // an audio thread (never suspended)
        int priority = 0;
        ULONG64 lastCycles = 0, lastCycles0 = 0;
    };
    std::vector<Watched> threads;
    struct Sample
    {
        double ms = 0.0;
        int tag = 0;
        std::uint64_t processCycles = 0;
        std::vector<std::pair<int, std::uint64_t>> cycles;                 // (thread index, cycles since the last sample)
        std::vector<std::pair<int, std::vector<std::uintptr_t>>> stacks; // (thread index, frames)
    };
    std::vector<Sample> samples;
    double qpcToMs = 0.0;
    std::uint64_t busyCycles = 0;
    std::uint64_t tsc0 = 0, tsc1 = 0;
    double ms0 = 0.0, ms1 = 0.0;
#endif
};
StallSampler& stallSampler()
{
    static StallSampler s;
    return s;
}

#if defined(LYNX_UNWIND)
double stallNowMs (const StallSampler& s)
{
    LARGE_INTEGER c {};
    QueryPerformanceCounter (&c);
    return (double) c.QuadPart * s.qpcToMs; // (the clock of juce::Time::getMillisecondCounterHiRes on Windows)
}

void stallLoop()
{
    auto& s = stallSampler();
    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    timeBeginPeriod (1);
    std::vector<std::uint8_t> stackCopy;
    ULONG64 lastProcess = 0;
    QueryProcessCycleTime (GetCurrentProcess(), &lastProcess);
    for (;;)
    {
        {
            std::unique_lock<std::mutex> l (s.lock);
            if (s.wake.wait_for (l, std::chrono::microseconds (s.intervalUs), [&s] { return s.quit; }))
                break;
        }
        if (s.samples.size() >= 200000)
            continue;
        StallSampler::Sample sample;
        sample.ms = stallNowMs (s);
        sample.tag = s.tag != nullptr ? s.tag->load (std::memory_order_relaxed) : 0;
        ULONG64 process = 0;
        QueryProcessCycleTime (GetCurrentProcess(), &process);
        sample.processCycles = process - lastProcess;
        lastProcess = process;
        for (int i = 0; i < (int) s.threads.size(); ++i)
        {
            auto& t = s.threads[(std::size_t) i];
            ULONG64 cycles = 0;
            if (! QueryThreadCycleTime (t.handle, &cycles))
                continue;
            if (cycles != t.lastCycles)
                sample.cycles.emplace_back (i, cycles - t.lastCycles);
            t.lastCycles = cycles;
            // (the threads asked for always; any other thread that ran at least ~0.2 ms since the last sample, except the
            // audio threads, which are never suspended here)
            const auto busy = cycles - t.lastCycles0 > s.busyCycles && ! t.audio;
            t.lastCycles0 = cycles;
            if ((t.stacks || busy) && t.stackBase != 0)
                if (auto frames = sampleByCopy (t.handle, t.stackBase, stackCopy); ! frames.empty())
                {
                    if (frames.size() > 28)
                        frames.resize (28);
                    sample.stacks.emplace_back (i, std::move (frames));
                }
        }
        s.samples.push_back (std::move (sample));
    }
    timeEndPeriod (1);
}

// a thread's stack top (from its TEB) and start address, through ntdll's NtQueryInformationThread
void threadFacts (HANDLE thread, std::uintptr_t& stackBase, std::uintptr_t& start)
{
    using Query = LONG (WINAPI*) (HANDLE, int, void*, ULONG, ULONG*);
    static const auto query = reinterpret_cast<Query> (reinterpret_cast<void*> (GetProcAddress (GetModuleHandleW (L"ntdll.dll"), "NtQueryInformationThread")));
    stackBase = start = 0;
    if (query == nullptr)
        return;
    struct BasicInformation
    {
        LONG exitStatus;
        void* tebBaseAddress;
        void* uniqueProcess;
        void* uniqueThread;
        ULONG_PTR affinityMask;
        LONG priority, basePriority;
    } basic {};
    if (query (thread, 0 /* ThreadBasicInformation */, &basic, sizeof (basic), nullptr) >= 0 && basic.tebBaseAddress != nullptr)
        stackBase = (std::uintptr_t) reinterpret_cast<const NT_TIB*> (basic.tebBaseAddress)->StackBase;
    void* address = nullptr;
    if (query (thread, 9 /* ThreadQuerySetWin32StartAddress */, &address, sizeof (address), nullptr) >= 0)
        start = (std::uintptr_t) address;
}
#endif
} // namespace

void startStallSampler (const std::vector<std::string>& stackNames, int intervalUs, const std::atomic<int>* tag) noexcept
{
#if defined(LYNX_UNWIND)
    try
    {
        auto& s = stallSampler();
        if (s.thread.joinable())
            return;
        LARGE_INTEGER f {};
        QueryPerformanceFrequency (&f);
        s.qpcToMs = 1000.0 / (double) f.QuadPart;
        s.intervalUs = std::max (500, intervalUs);
        s.tag = tag;
        s.quit = false;
        s.samples.clear();
        s.samples.reserve (16384);
        for (auto& t : s.threads)
            CloseHandle (t.handle);
        s.threads.clear();
        using GetDescription = HRESULT (WINAPI*) (HANDLE, PWSTR*);
        static const auto getDescription = (GetDescription) (void*) GetProcAddress (GetModuleHandleW (L"kernel32.dll"), "GetThreadDescription");
        const auto self = GetCurrentThreadId();
        const auto snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return;
        THREADENTRY32 entry {};
        entry.dwSize = sizeof (entry);
        for (auto ok = Thread32First (snapshot, &entry); ok; ok = Thread32Next (snapshot, &entry))
        {
            if (entry.th32OwnerProcessID != GetCurrentProcessId())
                continue;
            StallSampler::Watched t;
            t.id = entry.th32ThreadID;
            t.handle = OpenThread (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, t.id);
            if (t.handle == nullptr)
                continue;
            if (getDescription != nullptr)
            {
                PWSTR text = nullptr;
                if (SUCCEEDED (getDescription (t.handle, &text)) && text != nullptr)
                {
                    t.name = narrow (text);
                    LocalFree (text);
                }
            }
            threadFacts (t.handle, t.stackBase, t.start);
            t.priority = GetThreadPriority (t.handle);
            t.audio = t.name.find ("LYNX worker") != std::string::npos || t.name.find ("LYNX audio") != std::string::npos
                      || t.name.find ("render-ahead") != std::string::npos || t.priority >= THREAD_PRIORITY_TIME_CRITICAL;
            t.stacks = t.id == self;
            for (const auto& n : stackNames)
                t.stacks = t.stacks || (! n.empty() && t.name.find (n) != std::string::npos);
            QueryThreadCycleTime (t.handle, &t.lastCycles);
            t.lastCycles0 = t.lastCycles;
            if (t.id == self)
                t.name = "(message thread) " + t.name;
            s.threads.push_back (std::move (t));
        }
        CloseHandle (snapshot);
        s.tsc0 = __rdtsc();
        s.ms0 = stallNowMs (s);
        {
            // ~0.2 ms of a core in cycles (the time-stamp counter's rate, measured over 20 ms)
            const auto t0 = __rdtsc();
            const auto m0 = stallNowMs (s);
            Sleep (20);
            s.busyCycles = (std::uint64_t) ((double) (__rdtsc() - t0) / std::max (1.0, stallNowMs (s) - m0) * 0.2);
        }
        s.thread = std::thread (stallLoop);
    }
    catch (...) {}
#else
    (void) stackNames;
    (void) intervalUs;
    (void) tag;
#endif
}

void stopStallSampler (const std::string& fileUtf8) noexcept
{
#if defined(LYNX_UNWIND)
    try
    {
        auto& s = stallSampler();
        if (! s.thread.joinable())
            return;
        {
            const std::lock_guard<std::mutex> l (s.lock);
            s.quit = true;
        }
        s.wake.notify_all();
        s.thread.join();
        s.tsc1 = __rdtsc();
        s.ms1 = stallNowMs (s);
        auto* out = std::fopen (fileUtf8.c_str(), "wb");
        if (out != nullptr)
        {
            // the format (perf190os/s/stalls.py reads it): R <cycles per ms>; H <index> <id> <stacks 0|1> <start> <name>;
            // S <ms> <tag> <process cycles> then the threads that ran "<index>:<cycles>"; K <index> <frames…> (hex);
            // A <address> <LYNX.exe+0x… or module!export>
            std::fprintf (out, "R %.3f\n", s.ms1 > s.ms0 ? (double) (s.tsc1 - s.tsc0) / (s.ms1 - s.ms0) : 0.0);
            for (std::size_t i = 0; i < s.threads.size(); ++i)
            {
                const auto& t = s.threads[i];
                std::fprintf (out, "H %zu %u %d %s p%d %s\n", i, t.id, t.stacks ? 1 : 0,
                              t.start != 0 ? describeAddress (reinterpret_cast<const void*> (t.start)).c_str() : "?", t.priority, t.name.c_str());
            }
            std::map<std::uintptr_t, int> addresses;
            for (const auto& sample : s.samples)
            {
                std::fprintf (out, "S %.3f %d %llu", sample.ms, sample.tag, (unsigned long long) sample.processCycles);
                for (const auto& [i, c] : sample.cycles)
                    std::fprintf (out, " %d:%llu", i, (unsigned long long) c);
                std::fprintf (out, "\n");
                for (const auto& [i, frames] : sample.stacks)
                {
                    std::fprintf (out, "K %d", i);
                    for (const auto f : frames)
                    {
                        std::fprintf (out, " %llx", (unsigned long long) f);
                        addresses[f] = 0;
                    }
                    std::fprintf (out, "\n");
                }
            }
            const auto base = (std::uintptr_t) GetModuleHandleW (nullptr);
            for (const auto& [a, unused] : addresses)
            {
                if (inOwnModule (a))
                    std::fprintf (out, "A %llx LYNX.exe+0x%llx\n", (unsigned long long) a, (unsigned long long) (a - base));
                else
                    std::fprintf (out, "A %llx %s\n", (unsigned long long) a, functionOf (a).c_str());
            }
            std::fclose (out);
        }
        for (auto& t : s.threads)
            CloseHandle (t.handle);
        s.threads.clear();
        s.samples.clear();
        s.samples.shrink_to_fit();
    }
    catch (...) {}
#else
    (void) fileUtf8;
#endif
}

// --- plugin editor windows (issue #32, round 174a) ----------------------------------------------------------------

bool windowFaultCatchable (std::uint32_t code) noexcept
{
    return crashreport::editorFaultCatchable (code);
}

#if defined(LYNX_UNWIND)
namespace
{
WindowClaim windowClaim = nullptr;
void (*windowFaultHandler) (const WindowFault&) = nullptr;

// the fault being handled (written by the filter on the faulting thread, read right after the guard; the message
// thread only)
struct CaughtFault
{
    HWND root = nullptr;
    std::uint32_t code = 0;
    std::uintptr_t address = 0;
    std::uintptr_t frames[64] = {};
    int frameCount = 0;
    wchar_t module[MAX_PATH] = {};
};
CaughtFault caught;
bool filtering = false;

constexpr int maxDeadWindows = 16;
HWND deadWindows[maxDeadWindows] = {};

bool isDead (HWND root) noexcept
{
    if (root == nullptr)
        return false;
    for (auto* w : deadWindows)
        if (w == root)
            return true;
    return false;
}

// the window, its top-level window or any window up its owner chain is a faulted editor (LYNX makes plugin windows
// owned by the main window, so GA_ROOTOWNER would always be the main window)
bool underDeadWindow (HWND window) noexcept
{
    auto h = window != nullptr ? GetAncestor (window, GA_ROOT) : nullptr;
    for (int i = 0; i < 8 && h != nullptr; ++i)
    {
        if (isDead (h))
            return true;
        const auto owner = GetWindow (h, GW_OWNER);
        h = owner != nullptr ? GetAncestor (owner, GA_ROOT) : nullptr;
    }
    return false;
}

// a module of Windows itself (System32, SysWOW64, WinSxS...): not who to blame
bool systemModule (const wchar_t* path) noexcept
{
    wchar_t windows[MAX_PATH] = {};
    const auto n = GetWindowsDirectoryW (windows, MAX_PATH);
    return n > 0 && n < MAX_PATH && CompareStringOrdinal (path, (int) n, windows, (int) n, TRUE) == CSTR_EQUAL;
}

int windowFilter (const MSG* m, EXCEPTION_POINTERS* ep) noexcept
{
    if (ep == nullptr || ep->ExceptionRecord == nullptr || ep->ContextRecord == nullptr || windowClaim == nullptr || filtering)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto code = (std::uint32_t) ep->ExceptionRecord->ExceptionCode;
    if (! windowFaultCatchable (code))
        return EXCEPTION_CONTINUE_SEARCH;
    filtering = true;
    int result = EXCEPTION_CONTINUE_SEARCH;
    caught = {};
    caught.code = code;
    caught.address = (std::uintptr_t) ep->ExceptionRecord->ExceptionAddress;
    caught.frameCount = unwind (*ep->ContextRecord, caught.frames, 64);
    // the first frame outside Windows' modules is who faulted (a C++ throw starts in KERNELBASE's RaiseException)
    const auto self = GetModuleHandleW (nullptr);
    for (int i = 0; i < caught.frameCount; ++i)
    {
        HMODULE module = nullptr;
        if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR> (caught.frames[i]), &module) || module == nullptr)
            continue; // generated code (a plugin's JIT): look further
        wchar_t path[MAX_PATH] = {};
        if (GetModuleFileNameW (module, path, MAX_PATH) == 0 || systemModule (path))
            continue;
        if (module != self) // LYNX's own fault stays a crash (its crash file and report)
        {
            lstrcpynW (caught.module, path, MAX_PATH);
            if (auto* root = static_cast<HWND> (windowClaim (m->hwnd, path)))
            {
                caught.root = root;
                result = EXCEPTION_EXECUTE_HANDLER;
            }
        }
        break;
    }
    filtering = false;
    return result;
}

// no C++ objects in this frame (SEH and C++ unwinding must not share one function)
bool dispatchInGuard (const MSG* m) noexcept
{
    __try
    {
        DispatchMessageW (m);
        return false;
    }
    __except (windowFilter (m, GetExceptionInformation()))
    {
        return true;
    }
}
} // namespace

void setWindowGuard (WindowClaim claim, void (*onFault) (const WindowFault&)) noexcept
{
    windowClaim = claim;
    windowFaultHandler = onFault;
}

void forgetWindow (void* rootWindow) noexcept
{
    for (auto& w : deadWindows)
        if (w == static_cast<HWND> (rootWindow))
            w = nullptr;
}

void dispatchGuarded (const void* msg) noexcept
{
    const auto* m = static_cast<const MSG*> (msg);
    if (m == nullptr)
        return;
    // a faulted editor until it is closed: its messages are dropped (a dirty one would be painted again at once)
    if (m->hwnd != nullptr && underDeadWindow (m->hwnd))
    {
        if (m->message == WM_PAINT)
            ValidateRect (m->hwnd, nullptr);
        return;
    }
    if (! dispatchInGuard (m))
        return;

    const auto root = caught.root;
    ShowWindow (root, SW_HIDE); // at once: no more paints of a half-dead editor
    for (auto& w : deadWindows)
        if (w == nullptr || w == root)
        {
            w = root;
            break;
        }
    try
    {
        WindowFault fault;
        fault.rootWindow = root;
        fault.code = caught.code;
        fault.where = describeAddress (reinterpret_cast<const void*> (caught.address));
        fault.module = narrow (caught.module);
        char head[160];
        std::snprintf (head, sizeof (head), "exception 0x%08lx (%s) at ", (unsigned long) caught.code, describe (caught.code));
        fault.trace = head + fault.where + "\n" + formatFrames (std::vector<std::uintptr_t> (caught.frames, caught.frames + caught.frameCount));
        if (windowFaultHandler != nullptr)
            windowFaultHandler (fault);
    }
    catch (...) {}
}
#else
void setWindowGuard (WindowClaim, void (*) (const WindowFault&)) noexcept {}
void forgetWindow (void*) noexcept {}
void dispatchGuarded (const void* msg) noexcept
{
 #if defined(_WIN32)
    if (msg != nullptr)
        DispatchMessageW (static_cast<const MSG*> (msg));
 #else
    (void) msg;
 #endif
}
#endif

// ---- crash handler watchdog (rollup 0.1.30 step 12; CrashGuard.h) ------------------------------------------------------
namespace
{
std::atomic<int> watchdogSeconds { 60 };
std::atomic<bool> hangForTest { false };
// the crash folder, prepared at start-up: the heap-corruption crash file is written without allocating
#if defined(_WIN32)
wchar_t crashFolderWide[1024] {};
HANDLE watchdogWake = nullptr;
std::atomic<std::uint32_t> watchdogCode { 0xC0000374u };
DWORD WINAPI watchdogLate (LPVOID) noexcept
{
    Sleep ((DWORD) watchdogSeconds.load() * 1000u);
    TerminateProcess (GetCurrentProcess(), (UINT) watchdogCode.load());
    return 0;
}
DWORD WINAPI watchdogThread (LPVOID) noexcept
{
    WaitForSingleObject (watchdogWake, INFINITE);
    return watchdogLate (nullptr);
}
#else
char crashFolderUtf8[2048] {};
#endif

void storeFolder (const std::string& folder) noexcept
{
#if defined(_WIN32)
    const auto n = MultiByteToWideChar (CP_UTF8, 0, folder.c_str(), -1, crashFolderWide, (int) (sizeof (crashFolderWide) / sizeof (wchar_t)));
    if (n <= 0)
        crashFolderWide[0] = 0;
#else
    if (folder.size() < sizeof (crashFolderUtf8))
        std::memcpy (crashFolderUtf8, folder.c_str(), folder.size() + 1);
    else
        crashFolderUtf8[0] = 0;
#endif
}

#if ! defined(_WIN32)
// The C library aborted on its own (its heap checks: "free(): invalid next size", "corrupted double-linked list"...; its
// fortify checks): the function that called abort() is the C library's. glibc's allocator entry points jump to static
// functions, so their names are not among the frames (seen with glibc 2.42: free -> ?? -> ?? -> abort); only the module
// tells. macOS: any frame in libsystem_malloc. A C++ terminate (libstdc++), LYNX's or a plugin's abort() do not count.
bool inModule (const void* pc, const char* part, const char** symbol = nullptr) noexcept
{
    Dl_info info {};
    if (pc == nullptr || dladdr (pc, &info) == 0 || info.dli_fname == nullptr)
        return false;
    if (symbol != nullptr)
        *symbol = info.dli_sname;
    const char* base = std::strrchr (info.dli_fname, '/');
    base = base != nullptr ? base + 1 : info.dli_fname;
    return std::strncmp (base, part, std::strlen (part)) == 0;
}

bool libraryAbort (void* const* frames, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
 #if defined(__APPLE__)
        if (inModule (frames[i], "libsystem_malloc"))
            return true;
 #else
        const char* name = nullptr;
        if (inModule (frames[i], "libc.so", &name) && name != nullptr && std::strcmp (name, "abort") == 0)
            return i + 1 < n && inModule (frames[i + 1], "libc.so"); // (abort's caller)
 #endif
    }
    return false;
}

// unsigned decimal into `out` (no allocation); returns the end
char* putNumber (char* out, unsigned long long v) noexcept
{
    char digits[24];
    int n = 0;
    do
    {
        digits[n++] = (char) ('0' + (int) (v % 10));
        v /= 10;
    } while (v != 0 && n < 24);
    while (n > 0)
        *out++ = digits[--n];
    return out;
}

void writeAll (int fd, const char* text) noexcept
{
    auto left = std::strlen (text);
    while (left > 0)
    {
        const auto n = ::write (fd, text, left);
        if (n <= 0)
            return;
        text += n;
        left -= (std::size_t) n;
    }
}
#endif
} // namespace

void prepareCrashWatchdog (const std::string& crashFolder) noexcept
{
    if (const char* v = std::getenv ("LYNX_CRASH_WATCHDOG_S"))
        if (const auto s = std::atoi (v); s >= 1 && s <= 600)
            watchdogSeconds = s;
    storeFolder (crashFolder);
#if defined(_WIN32)
    if (watchdogWake == nullptr)
    {
        watchdogWake = CreateEventW (nullptr, TRUE, FALSE, nullptr);
        if (watchdogWake != nullptr)
            if (auto* t = CreateThread (nullptr, 64 * 1024, watchdogThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr))
                CloseHandle (t);
    }
#elif defined(LYNX_EXECINFO)
    void* warm[4];
    (void) backtrace (warm, 4); // (its first call loads the unwinder: an allocation, here and not in the handler)
#endif
}

void setCrashFolder (const std::string& crashFolder) noexcept { storeFolder (crashFolder); }
int crashWatchdogSeconds() noexcept { return watchdogSeconds.load(); }

void armCrashWatchdog (void* exceptionPointers) noexcept
{
#if defined(_WIN32)
    if (auto* ep = static_cast<EXCEPTION_POINTERS*> (exceptionPointers); ep != nullptr && ep->ExceptionRecord != nullptr)
        watchdogCode = (std::uint32_t) ep->ExceptionRecord->ExceptionCode;
    if (watchdogWake != nullptr)
        SetEvent (watchdogWake);
    else if (auto* t = CreateThread (nullptr, 64 * 1024, watchdogLate, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr))
        CloseHandle (t); // (not prepared: a thread of its own now)
#else
    (void) exceptionPointers;
    struct sigaction action {};
    action.sa_handler = SIG_DFL;
    sigemptyset (&action.sa_mask);
    sigaction (SIGALRM, &action, nullptr);
    sigset_t alarmOnly;
    sigemptyset (&alarmOnly);
    sigaddset (&alarmOnly, SIGALRM);
    pthread_sigmask (SIG_UNBLOCK, &alarmOnly, nullptr);
    alarm ((unsigned) watchdogSeconds.load());
#endif
}

bool heapCorruptionCrash (void* exceptionPointers) noexcept
{
#if defined(_WIN32)
    const auto* ep = static_cast<const EXCEPTION_POINTERS*> (exceptionPointers);
    return ep != nullptr && ep->ExceptionRecord != nullptr && (std::uint32_t) ep->ExceptionRecord->ExceptionCode == 0xC0000374u;
#elif defined(LYNX_EXECINFO)
    if ((int) reinterpret_cast<std::intptr_t> (exceptionPointers) != SIGABRT)
        return false;
    void* frames[64];
    const auto n = backtrace (frames, 64);
    return libraryAbort (frames, n);
#else
    (void) exceptionPointers;
    return false;
#endif
}

void writeHeapCrashFile (void* exceptionPointers) noexcept
{
#if defined(_WIN32)
    if (crashFolderWide[0] == 0)
        return;
    wchar_t path[1200];
    const auto len = lstrlenW (crashFolderWide);
    if (len + 64 >= (int) (sizeof (path) / sizeof (wchar_t)))
        return;
    lstrcpyW (path, crashFolderWide);
    lstrcatW (path, L"\\crash heap-corruption.txt");
    const auto file = CreateFileW (path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    char text[256];
    const auto* ep = static_cast<const EXCEPTION_POINTERS*> (exceptionPointers);
    const auto code = ep != nullptr && ep->ExceptionRecord != nullptr ? (unsigned long) ep->ExceptionRecord->ExceptionCode : 0ul;
    const auto n = wsprintfA (text, "LYNX crashed: heap corruption (exception 0x%08lx). The emergency save was skipped: the heap is broken.\r\n", code);
    DWORD written = 0;
    WriteFile (file, text, (DWORD) n, &written, nullptr);
    CloseHandle (file);
#else
    if (crashFolderUtf8[0] == 0)
        return;
    char path[sizeof (crashFolderUtf8) + 64];
    const auto len = std::strlen (crashFolderUtf8);
    std::memcpy (path, crashFolderUtf8, len);
    char* end = path + len;
    const char* name = "/crash heap-corruption ";
    std::memcpy (end, name, std::strlen (name));
    end += std::strlen (name);
    end = putNumber (end, (unsigned long long) std::time (nullptr));
    std::memcpy (end, ".txt", 5);
    const int fd = ::open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    char head[96];
    char* h = head;
    const char* what = "LYNX crashed: the C library's heap-corruption abort (signal ";
    writeAll (fd, what);
    h = putNumber (h, (unsigned long long) reinterpret_cast<std::uintptr_t> (exceptionPointers));
    *h = 0;
    writeAll (fd, head);
    writeAll (fd, ", abort). The emergency save was skipped: the heap is broken.\n--- frames\n");
 #if defined(LYNX_EXECINFO)
    void* frames[64];
    const auto n = backtrace (frames, 64);
    backtrace_symbols_fd (frames, n, fd);
 #endif
    ::close (fd);
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((noinline))
#endif
void heapCorruptionForTest() noexcept
{
#if defined(__linux__)
    // two chunks past the per-thread cache's sizes; the first one's neighbour gets a size of 0, so free() of the first
    // finds "invalid next size" while it holds the arena's lock (what round 190mo's self-test write ended in)
    using Alloc = void* (*) (std::size_t);
    using Free = void (*) (void*);
    volatile Alloc alloc = &std::malloc;
    volatile Free release = &std::free;
    auto* first = static_cast<unsigned char*> (alloc (4000));
    auto* second = static_cast<unsigned char*> (alloc (4000));
    std::memset (first, 1, 4000);
    reinterpret_cast<volatile std::size_t*> (second)[-1] = 0; // (the size word of `second`'s chunk header)
    release (first);
    release (second);
#elif ! defined(_WIN32)
    using Free = void (*) (void*);
    volatile Free release = &std::free;
    alignas (16) static unsigned char notFromMalloc[256];
    release (notFromMalloc + 16); // (libsystem_malloc: "pointer being freed was not allocated" -> abort)
#else
    RaiseException (0xC0000374u, EXCEPTION_NONCONTINUABLE, 0, nullptr); // (Windows: the code its heap raises)
#endif
    std::abort(); // (not reached when the C library noticed)
}

void setHangInCrashHandlerForTest() noexcept { hangForTest = true; }
bool hangInCrashHandlerForTest() noexcept { return hangForTest.load(); }

std::string lastHangFile()
{
    auto& w = hangWatch();
    const std::lock_guard<std::mutex> l (w.lock);
    return w.lastFile;
}
} // namespace lynx::crashguard
