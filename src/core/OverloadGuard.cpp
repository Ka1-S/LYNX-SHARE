#include "core/OverloadGuard.h"

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
  #define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
 #endif
#else
 #include <cerrno>
 #include <time.h>
#endif

namespace lynx
{
void OverloadGuard::sleepFor (double ms) noexcept
{
    if (! (ms > 0.0))
        return;
#if defined(_WIN32)
    // one timer per thread that yields (the device thread), made at its first yield: Windows 10 1803+ has the
    // high-resolution kind; an older system gets the plain one (its resolution is the process's timer period)
    struct Timer
    {
        HANDLE handle = nullptr;
        bool made = false;
        ~Timer()
        {
            if (handle != nullptr)
                CloseHandle (handle);
        }
    };
    thread_local Timer timer;
    if (! timer.made)
    {
        timer.made = true;
        timer.handle = CreateWaitableTimerExW (nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (timer.handle == nullptr)
            timer.handle = CreateWaitableTimerExW (nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG) (ms * 10000.0); // (relative, in 100 ns units)
    if (timer.handle != nullptr && SetWaitableTimer (timer.handle, &due, 0, nullptr, nullptr, FALSE))
        WaitForSingleObject (timer.handle, (DWORD) (ms + 50.0));
    else
        Sleep (ms >= 1.0 ? (DWORD) ms : 1);
#else
    timespec t { (time_t) (ms / 1000.0), (long) ((ms - 1000.0 * (double) (time_t) (ms / 1000.0)) * 1.0e6) };
    while (nanosleep (&t, &t) != 0 && errno == EINTR)
    {
    }
#endif
}
} // namespace lynx
