#pragma once

// Round 190la: a timed std::condition_variable wait without libc++'s sub-millisecond spin on Windows.
//
// libc++ (the zig toolchain's, Windows builds) implements a timed wait with SleepConditionVariableSRW and the time left
// truncated to whole milliseconds (support/win32/thread_win32.cpp, __libcpp_condvar_timedwait): with under 1 ms left it
// does not sleep at all, and wait_for's loop spins until the deadline. Every timed wait of a background thread ended in
// such a spin (round 190la measured ~0.5 ms of spin per wait: a 10 ms supervisor loop = ~5 % of a core). Here the wait
// sleeps whole milliseconds rounded up (the deadline may be seen up to ~1 ms late, never early). JUCE's own
// WaitableEvent gets the same fix (cmake/LynxPatchJuce.cmake, fix 44). Everywhere else: std::condition_variable.

#include <chrono>
#include <condition_variable>
#include <mutex>

#if defined(_WIN32) && defined(_LIBCPP_VERSION)
// (the same declaration as <windows.h>'s, so either may come first)
extern "C" __declspec (dllimport) int __stdcall SleepConditionVariableSRW (struct _RTL_CONDITION_VARIABLE*, struct _RTL_SRWLOCK*,
                                                                          unsigned long, unsigned long);
#endif

namespace lynx::cvwait
{
// cv.wait_for (lock, timeout, pred): true when pred() holds at the end
template <class Rep, class Period, class Pred>
bool waitFor (std::condition_variable& cv, std::unique_lock<std::mutex>& lock, std::chrono::duration<Rep, Period> timeout, Pred pred)
{
#if defined(_WIN32) && defined(_LIBCPP_VERSION)
    using clock = std::chrono::steady_clock;
    const auto end = clock::now() + std::chrono::duration_cast<clock::duration> (timeout);
    while (! pred())
    {
        const auto left = end - clock::now();
        if (left <= clock::duration::zero())
            return pred();
        auto ms = std::chrono::ceil<std::chrono::milliseconds> (left).count();
        ms = ms < 1 ? 1 : (ms > 0x7ffffffe ? 0x7ffffffe : ms);
        SleepConditionVariableSRW (reinterpret_cast<struct _RTL_CONDITION_VARIABLE*> (cv.native_handle()),
                                   reinterpret_cast<struct _RTL_SRWLOCK*> (lock.mutex()->native_handle()), (unsigned long) ms, 0);
    }
    return true;
#else
    return cv.wait_for (lock, timeout, pred);
#endif
}

// cv.wait_for (lock, timeout) without a predicate: one wait (it may end early, as std's may)
template <class Rep, class Period>
void waitFor (std::condition_variable& cv, std::unique_lock<std::mutex>& lock, std::chrono::duration<Rep, Period> timeout)
{
#if defined(_WIN32) && defined(_LIBCPP_VERSION)
    auto ms = std::chrono::ceil<std::chrono::milliseconds> (timeout).count();
    ms = ms < 1 ? 1 : (ms > 0x7ffffffe ? 0x7ffffffe : ms);
    SleepConditionVariableSRW (reinterpret_cast<struct _RTL_CONDITION_VARIABLE*> (cv.native_handle()),
                               reinterpret_cast<struct _RTL_SRWLOCK*> (lock.mutex()->native_handle()), (unsigned long) ms, 0);
#else
    cv.wait_for (lock, timeout);
#endif
}
} // namespace lynx::cvwait
