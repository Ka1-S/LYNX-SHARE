#pragma once

// Calls into code LYNX does not control (hosted VST3 / CLAP / VST2 plugins) behind a structured-exception guard, so a
// plugin that crashes (access violation, division by zero, a C++ exception escaping it) is caught at the call instead
// of taking the whole app down. On Windows this is SEH (__try / __except in CrashGuard.cpp, the only unit built with
// -fms-extensions).
//
// Linux / macOS (round 190dt; x86-64 and ARM64): a fault is a signal. call() arms a jump buffer of this thread
// (sigsetjmp, no system call) for the length of the call; a handler for SIGSEGV / SIGBUS / SIGILL / SIGFPE, installed
// on top of the crash handler (hardenSignals, or the first call), jumps back to call() (siglongjmp) only when ALL of:
//   - this thread is inside an armed call (other threads, and every fault outside a guarded call, go to the crash
//     handler as before: the crash file and the emergency save);
//   - the kernel raised it for an instruction (not a signal sent by kill / raise);
//   - the first frame from the faulting instruction outwards that is not the C / C++ runtime or the system's own
//     libraries (libc, libstdc++, libgcc_s, ld.so, libsystem_*, libobjc...) belongs to a module that is not LYNX's
//     executable: a plugin's memcpy from a null pointer faults in libc but is the plugin's; a fault in LYNX's or
//     JUCE's code (a host callback the plugin made, JUCE's VST3 wrapper) is LYNX's own and is never caught, nor is
//     one whose frames cannot tell (code outside every module called from LYNX, an unwinder that finds nothing).
// C++ exceptions leaving the call are caught there too (code 0xE06D7363, as on Windows).
// What it cannot make safe (as with SEH, the caller then never touches that instance again, and LYNX goes on):
//   - the jump skips the destructors of the frames between call() and the fault: what they held stays held (JUCE's
//     per-instance VST3 process lock, LYNX's per-instance CLAP gate; both only guard that dead instance), and what
//     the plugin held stays held (its own locks; a lock of the C library, e.g. malloc's when it faulted inside
//     malloc: then a later allocation of another thread may block, as the heap may be damaged anyway);
//   - finding out whose fault it is runs in the signal handler (dladdr, the unwinder): not async-signal-safe by the
//     letter of POSIX; a fault there is passed on to the crash handler (SA_NODEFER), never caught;
//   - not caught: aborts (SIGABRT: the C library's heap checks, assertions, Rust's panic=abort), stack overflows
//     (no alternate stack on the audio thread), faults inside its editor's event handling (see the window guard
//     below). Faults on the plugin's own threads: see watchPluginModule (round 190gg). "Run in a separate process"
//     (engine/PluginSandbox.h) is the complete protection: there the plugin dies alone.
//
// A caught crash leaves the plugin in an unknown state: the caller must never call that instance again (PluginEffect /
// PluginInstrument switch to bypass / silence and report it, engine/PluginHost.h).

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace lynx::crashguard
{
// Runs fn (context); returns 0 when it returned normally, otherwise the exception code (Windows' NTSTATUS, e.g.
// 0xC0000005 = access violation).
std::uint32_t call (void (*fn) (void*), void* context) noexcept;

template <typename Fn>
std::uint32_t run (Fn&& fn) noexcept
{
    return call ([] (void* c) { (*static_cast<std::remove_reference_t<Fn>*> (c)) (); }, &fn);
}
// This build catches a plugin's faults in call() (Windows SEH; Linux / macOS on x86-64 / ARM64 signals)
bool pluginFaultsCaught() noexcept;

// Round 190gg (B4): faults on a hosted plugin's OWN threads (Linux / macOS on x86-64 / ARM64; the app only, not the
// exported plugins' runtime; nothing on Windows). A thread the plugin started (its GUI renderer, a disk streamer, a
// licence check) runs no LYNX call, so call() cannot catch its fault. The handler of call() now also looks at a fault
// outside every guarded call: when, from the faulting instruction to the thread's first frame (which the walk must
// reach: the C library's thread start), no frame is LYNX's executable, and the first frame that is not the system's
// runtime belongs to a module given to watchPluginModule (the plugin's file, or a file in its bundle folder), the
// fault is noted (pluginThreadFaults) and that thread is parked for good (it waits with every signal blocked; whatever
// it held stays held). The app then switches every instance of that plugin off (engine/PluginHost.h,
// switchOffPluginsOfThreadFaults), as after a crash inside a call. Anything else goes to the crash handler as before:
// a fault on a LYNX thread (even inside a plugin's code it called), on a thread whose frames cannot be walked to its
// start, in a module nobody watches, or while another fault is being looked at.
// watchPluginModule: message thread, after the module was loaded in this process (an absolute path; at most 128).
void watchPluginModule (const std::string& fileUtf8) noexcept;
bool pluginThreadFaultsCaught() noexcept;
struct PluginThreadFault
{
    std::string module; // the watched path
    std::uint32_t code = 0;
    std::string where;  // "file+0xoffset" of the plugin's faulting frame
};
int pluginThreadFaultCount() noexcept;            // only grows
std::vector<PluginThreadFault> pluginThreadFaults(); // the last 16, oldest first
// --faulttest thread / thread-libc: a fault on a thread of LYNX's own with LYNX's executable watched as if it were a
// plugin's module: it must still end the process through the crash handler (a return = the thread was parked)
void faultOnOwnThreadForTest (bool inCLibrary) noexcept;

// Plugin editor windows (issue #32, round 174a; Windows x64 only, elsewhere these do nothing). A hosted plugin's own
// window procedure runs outside every LYNX call (DispatchMessageW calls it from the message loop), so a fault in it
// ended LYNX (the reporter's synth editor: access violation under DispatchMessageW). The JUCE patch
// (cmake/LynxPatchJuce.cmake fix 15) sends each message through dispatchGuarded, which dispatches it inside an SEH
// guard. A fault is caught only when
//   - it is not a debugger event, a heap corruption, a stack overflow or a fail-fast (those still end the process:
//     going on with a damaged heap or stack is not safe), and
//   - the first frame outside Windows' own modules is not LYNX.exe (a bug in LYNX stays a crash with its report), and
//   - `claim` (set by the app) says the message's window or that module belongs to an open plugin editor: it returns
//     that editor's top-level window, else nullptr.
// Then the editor's window is hidden at once, the messages for it are dropped (a WM_PAINT is validated) until
// forgetWindow, and `onFault` runs after the guard (still on the message thread) to close the editor and tell the
// user. Not done on macOS / Linux: there a fault is a signal, and jumping out of the Cocoa / X11 event dispatch from a
// signal handler leaves the run loop, autorelease pools and the X lock in an undefined state (see
// docs/analise/REA_rodada174a.md); "Run in a separate process" is the protection there.
struct WindowFault
{
    void* rootWindow = nullptr; // the editor's top-level window (HWND)
    std::uint32_t code = 0;     // NTSTATUS
    std::string where;          // "module+0xoffset" of the faulting instruction
    std::string module;         // the module blamed (first one outside Windows), UTF-8 full path
    std::string trace;          // every frame, as moduleBacktrace
};
using WindowClaim = void* (*) (void* window, const wchar_t* faultModulePath);
void setWindowGuard (WindowClaim claim, void (*onFault) (const WindowFault&)) noexcept;
void dispatchGuarded (const void* msg) noexcept; // msg: const MSG*
void forgetWindow (void* rootWindow) noexcept;   // the editor is gone: a new window may reuse the handle
// Whether a fault with this exception code may be caught at all (see above); true for access violations, illegal
// instructions, divisions by zero, C++ exceptions leaving the plugin...
bool windowFaultCatchable (std::uint32_t code) noexcept;

// Child processes that load plugins (scan, sandbox): a plugin crashing there must not pop Windows' "stopped working"
// dialog on the user's screen (the parent notices the exit code). Also covers fail-fast exits (0xC0000409).
void quietCrashReports() noexcept;

// Ends the process at once with `code`, without running DLL detach handlers or static destructors: some plugins
// (a Waves shell, round 111) crash in their own teardown at exit (DllMain / atexit calling into the GPU driver).
// Only after every file the process writes is closed.
[[noreturn]] void exitNow (int code) noexcept;

// Session breadcrumb (round 111). A fail-fast (0xC0000409, e.g. a GPU driver aborting inside a plugin's editor) ends
// the process without running any crash handler, so nothing tells what happened. While the UI runs, a small file
// (%APPDATA%\LYNX\sessions\session-<pid>.txt) holds a header and the last few steps (plugin being loaded, editor being
// opened, project being opened...). A clean exit removes it; the next start finds the file of a session whose process
// is gone and reports it (Main.cpp). Steps are written synchronously (the file survives the process dying right after);
// never call note() from the audio thread.
void beginSession (const std::string& fileUtf8, const std::string& header) noexcept;
void note (const std::string& step) noexcept; // no-op without a session
void endSession() noexcept;                   // removes the file
// Round 159 (crash reports): facts of the session kept in its file as "info.<key>=value" (e.g. the audio device, set
// again when it changes) and the third-party plugins it loaded as "plugin=name|vendor|format|version" (each once).
void setSessionInfo (const std::string& key, const std::string& value) noexcept;
void notePlugin (const std::string& nameVendorFormatVersion) noexcept;
std::uint32_t processId() noexcept;
std::uint64_t processStartTime() noexcept; // Windows: creation FILETIME; 0 elsewhere
// The process `pid` still runs and (where known) started at `startTime`: not a reused id.
bool processRunning (std::uint32_t pid, std::uint64_t startTime) noexcept;

// Short English description of an exception code ("access violation", ...), or "exception".
const char* describe (std::uint32_t code) noexcept;

// The calling thread's stack as "module+0xoffset" lines, every frame (round 157a): JUCE's backtrace leaves out frames
// it has no symbol for — every frame of a release LYNX.exe — so a crash file could not say where LYNX was. With the
// build's map / PDB the offsets give the functions. Empty where not supported.
std::string moduleBacktrace (int skip = 0);
// The faulting thread's frames from a crash handler's exception pointers (Windows: EXCEPTION_POINTERS*, what JUCE's
// crash handler receives), with the exception code and address on top; "" where not supported. Round 190ci: when the
// frames do not reach the thread's first frame (see setUnwalkableFaultHandler), the words on the faulting stack that
// point into loaded modules follow: the callers the walk could not find are among them.
std::string exceptionBacktrace (void* exceptionPointers);
// Round 190ci (Windows x64; nothing elsewhere). The crash handler (JUCE's setApplicationCrashHandler) is Windows'
// top-level exception filter, which only runs after Windows has walked the faulting thread's stack to its first frame.
// When the stack cannot be walked (code that ran off the end of a function into the next one, a smashed return
// address), Windows' walk reads something that is not a frame and either gives up or faults itself (and the dispatch
// of that fault faults again, until the stack is used up): the process ends without any handler having run, with no
// crash file and no emergency save. The 0.1.26 release candidate's FPC fault was one of those. A first-chance hook
// therefore looks at every processor fault (crashreport::processorFault) whose stack pointer is in the thread's own
// stack and whose stack does not walk to the thread's first frame:
// - the app's own (the fault, or its nearest caller outside Windows' modules, is in the .exe): `handler` (the function
//   the crash handler calls) gets the fault's EXCEPTION_POINTERS* on the faulting thread, then the process ends with
//   the fault's code, as it does after the top-level filter;
// - anybody else's (a plugin's module, generated code) is only remembered: a plugin's copy protection may fault on
//   purpose and handle it in a hook of its own. When the next fault of that thread is inside Windows' exception
//   dispatch itself, nothing can handle either: `handler` gets the remembered fault and the process ends.
// Once per run. A fault on a stack that walks is not touched: plugin guards and the top-level filter get it as always.
// Not covered: somebody else's fault on an unwalkable stack when Windows' walk gives up without faulting.
void setUnwalkableFaultHandler (void (*handler) (void* exceptionPointers)) noexcept;
// --faulttest (round 190ci): ends the process by an access violation on purpose, for the test of the crash handler:
// `unwalkable` = with the thread's stack above the fault overwritten first, so that it cannot be walked (Windows x64;
// elsewhere a plain fault).
[[noreturn]] void faultForTest (bool unwalkable) noexcept;
// --faulttest guarded / guarded-libc (round 190dt): a fault in LYNX's own code inside a guarded call (`inCLibrary`:
// a memcpy from a null pointer that LYNX makes, so the faulting instruction is in the C library). It must NOT be
// caught: the process ends through the crash handler; a return (with the code) means it was swallowed.
std::uint32_t faultInGuardedCallForTest (bool inCLibrary) noexcept;
// "module+0xoffset" of one code address ("" when unknown)
std::string describeAddress (const void* address);

// UI hang watch (round 157a). A frozen message thread leaves no crash file: Windows shows "not responding" and the
// user closes LYNX. startHangWatch (on the message thread) starts a watcher thread; heartbeat() (the message thread,
// from a timer, several times a second) proves the UI is alive. When no heartbeat arrives for `stallMs`, the watcher
// samples the message thread's stack (Windows x64: suspended for a moment, unwound without symbols;
// Linux / macOS: SIGPROF to that thread, which records its own frames), writes it to
// `folderUtf8`/hang <time>.txt and adds a session step ("UI not responding ..."); a second sample follows if the stall
// lasts, and "UI responding again after N s" when it ends. stopHangWatch ends the thread (before quitting).
void startHangWatch (const std::string& folderUtf8, int stallMs = 2000) noexcept;
// Issue #115 (Linux / macOS; nothing on Windows). Call after the crash handler is installed (JUCE's
// setApplicationCrashHandler), on the message thread:
// - an alternate signal stack for this thread, and the crash handler's signals moved onto it, so a stack overflow on the
//   message thread still writes its crash file;
// - SIGTERM / SIGHUP (the session logging out, the terminal that started LYNX closed, `kill`) leave a session step
//   ("ended by SIGTERM ...") before LYNX ends as it did before. Without them the report could only say "closed".
// - round 190dt: the plugin-call guard of call() (see the top of this file), on top of the crash handler.
void hardenSignals() noexcept;
void heartbeat() noexcept;
void stopHangWatch() noexcept;
// --symbolize: the "LYNX.exe+0x…" frames of a crash / hang file named with this very build's symbols (LYNX.pdb of
// the same build next to LYNX.exe; CI keeps it as the "LYNX-symbols" artifact). Other lines unchanged.
std::string symbolizeOwnFrames (const std::string& text);
// Test: the last hang file written by this process ("" when none)
std::string lastHangFile();

// Message-thread sampling profiler (round 174f; LYNX_PROFILE_MAIN=<file>). Called on the message thread: a thread samples
// it every `intervalUs` (Windows x64: suspended for a moment and unwound, as the hang watch does) and only counts the
// samples where the thread ran since the last one (its cycle counter moved), so waiting in the message loop does not
// count. Every `windowS` seconds it appends that window's functions by self and by total samples to the file (LYNX.exe
// named with LYNX.pdb next to it; system DLLs by their nearest export or module). Other platforms: nothing.
void startMainProfiler (const std::string& fileUtf8, int intervalUs = 1000, int windowS = 5) noexcept;
void stopMainProfiler() noexcept;
} // namespace lynx::crashguard
