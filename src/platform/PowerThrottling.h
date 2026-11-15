#pragma once
// Windows power throttling (EcoQoS) and LYNX's audio threads (round toolchain190gt; research round 190gs Q6).
//
// Windows 11 gives a process it considers in the background (window minimised or covered, and not heard through the
// Windows audio engine: an ASIO driver bypasses that engine, so a DAW playing through ASIO counts as silent) Low QoS /
// EcoQoS: the efficiency cores on hybrid CPUs, lowered clocks on battery, and it ignores the process' timer resolution
// request. An audio thread that loses clock speed misses its buffer deadline.
//  - The threads that render audio (the device's callback thread, the render workers, the stream prefetcher, the
//    sandbox's and the 32-bit bridge's audio thread) opt out, one call per thread at its start.
//  - While an audio device is open the whole process opts out too (execution speed and the timer resolution: the UI,
//    MIDI sync and the plugins' own threads keep running at full speed when LYNX is minimised); closing the device gives
//    the choice back to the system.
//
// Header-only. Elsewhere (macOS, Linux) it does nothing.

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace lynx::power
{
#if defined(_WIN32)
namespace detail
{
// THREAD_POWER_THROTTLING_STATE (processthreadsapi.h, Windows 10 1709+); declared here so older SDK headers build too
struct ThreadThrottlingState
{
    unsigned long version;     // THREAD_POWER_THROTTLING_CURRENT_VERSION (1)
    unsigned long controlMask; // THREAD_POWER_THROTTLING_EXECUTION_SPEED (1): this thread decides its execution speed
    unsigned long stateMask;   // 0: never throttled; THREAD_POWER_THROTTLING_EXECUTION_SPEED: EcoQoS
};
using GetSetThreadInformation = BOOL (WINAPI*) (HANDLE, int, void*, DWORD);
constexpr int threadPowerThrottling = 3; // THREAD_INFORMATION_CLASS::ThreadPowerThrottling

inline GetSetThreadInformation load (const char* name) noexcept
{
    if (auto* kernel = GetModuleHandleW (L"kernel32.dll"))
        return reinterpret_cast<GetSetThreadInformation> (reinterpret_cast<void*> (GetProcAddress (kernel, name)));
    return nullptr;
}
} // namespace detail

// The calling thread runs at full speed whatever Windows decides about the process. False when the system has no
// such setting (before Windows 10 1709). Real-time safe after its first call (a cached function pointer, one syscall).
inline bool optOutOfPowerThrottling() noexcept
{
    static const auto set = detail::load ("SetThreadInformation");
    detail::ThreadThrottlingState state { 1, 1, 0 };
    return set != nullptr && set (GetCurrentThread(), detail::threadPowerThrottling, &state, sizeof (state)) != 0;
}

// The calling thread joins MMCSS "Pro Audio" at critical priority (the class of LYNX's own render workers, AudioEngine.cpp)
// and opts out of power throttling. For audio threads outside the engine (the plugin sandbox, the 32-bit bridge).
inline void becomeProAudioThread() noexcept
{
    using AvSet = HANDLE (WINAPI*) (LPCWSTR, LPDWORD);
    using AvPriority = BOOL (WINAPI*) (HANDLE, int);
    static auto* const avrt = LoadLibraryW (L"avrt.dll");
    static const auto avSet = avrt != nullptr ? reinterpret_cast<AvSet> (reinterpret_cast<void*> (GetProcAddress (avrt, "AvSetMmThreadCharacteristicsW"))) : nullptr;
    static const auto avPriority = avrt != nullptr ? reinterpret_cast<AvPriority> (reinterpret_cast<void*> (GetProcAddress (avrt, "AvSetMmThreadPriority"))) : nullptr;
    DWORD task = 0;
    if (auto* handle = avSet != nullptr ? avSet (L"Pro Audio", &task) : nullptr)
    {
        if (avPriority != nullptr && avPriority (handle, 2 /* AVRT_PRIORITY_CRITICAL, as LYNX's own audio threads */) == 0)
            avPriority (handle, 1 /* AVRT_PRIORITY_HIGH */);
    }
    else
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    optOutOfPowerThrottling();
}

// The whole process at full speed (`on`) or back to the system's choice. Counted: the process opts out while at
// least one caller holds it on (each `true` matched by one `false`). Message thread (device start / stop).
inline bool holdProcessFullSpeed (bool on) noexcept
{
    struct ProcessThrottlingState
    {
        unsigned long version, controlMask, stateMask; // PROCESS_POWER_THROTTLING_STATE
    };
    static int holders = 0;
    const auto before = holders;
    holders = on ? holders + 1 : (holders > 0 ? holders - 1 : 0);
    if ((before > 0) == (holders > 0))
        return true;
    static const auto set = reinterpret_cast<BOOL (WINAPI*) (HANDLE, int, void*, DWORD)> (
        reinterpret_cast<void*> (GetProcAddress (GetModuleHandleW (L"kernel32.dll"), "SetProcessInformation")));
    // EXECUTION_SPEED (1) | IGNORE_TIMER_RESOLUTION (4); state 0 = never throttled. Control 0 = the system decides.
    ProcessThrottlingState state { 1, holders > 0 ? 1ul | 4ul : 0ul, 0 };
    return set != nullptr && set (GetCurrentProcess(), 4 /* ProcessPowerThrottling */, &state, sizeof (state)) != 0;
}

// The calling thread's setting: 1 = opted out (never throttled), 0 = the system decides, -1 = unknown. (Tests and
// diagnostics.)
inline int powerThrottlingOptOut() noexcept
{
    static const auto get = detail::load ("GetThreadInformation");
    detail::ThreadThrottlingState state { 1, 0, 0 };
    if (get == nullptr || get (GetCurrentThread(), detail::threadPowerThrottling, &state, sizeof (state)) == 0)
        return -1;
    return (state.controlMask & 1u) != 0 && (state.stateMask & 1u) == 0 ? 1 : 0;
}
#else
inline bool optOutOfPowerThrottling() noexcept { return false; }
inline void becomeProAudioThread() noexcept {}
inline bool holdProcessFullSpeed (bool) noexcept { return false; }
inline int powerThrottlingOptOut() noexcept { return -1; }
#endif
} // namespace lynx::power
