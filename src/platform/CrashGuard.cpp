#include "platform/CrashGuard.h"
#include "core/CrashReport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
 #include <dbghelp.h>
#else
 #include <signal.h>
 #include <unistd.h>
 #include <pthread.h>
 #include <dlfcn.h>
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
        const auto n = unwind (*ep->ContextRecord, frames, 96);
        return text + formatFrames (std::vector<std::uintptr_t> (frames, frames + n));
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
void onSampleSignal (int)
{
    sampledCount.store (backtrace (sampledFrames, 64), std::memory_order_release);
}

std::vector<std::uintptr_t> sampleThread (pthread_t thread)
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
            w.wake.wait_for (l, std::chrono::milliseconds (250), [&w] { return w.quit; });
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
            action.sa_handler = onSampleSignal;
            sigemptyset (&action.sa_mask);
            action.sa_flags = SA_RESTART;
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

std::string lastHangFile()
{
    auto& w = hangWatch();
    const std::lock_guard<std::mutex> l (w.lock);
    return w.lastFile;
}
} // namespace lynx::crashguard
