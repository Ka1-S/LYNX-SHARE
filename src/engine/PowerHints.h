#pragma once
// What LYNX tells the OS around long jobs (rollup 0.1.28, REA round 190hb actions 3 and 4, FL Studio's own rules):
// - an export or a recording keeps the PC awake (Windows: SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED),
//   released afterwards; the state is the calling thread's, so a KeepAwake ends on the thread that made it);
// - an offline render runs below normal priority (FL's export threads: BELOW_NORMAL), so the desktop stays usable;
// - at start, the desktop compositor gets MMCSS scheduling (DwmEnableMMCSS (TRUE), FL's "Init DWM"; LYNX_DWM_MMCSS=0
//   leaves it off).
// Implemented in AudioEngine.cpp; everything does nothing outside Windows.
#include <cstdint>

namespace lynx::powerhints
{
std::uint32_t keepAwakeBegin() noexcept;              // returns the thread's previous state
void keepAwakeEnd (std::uint32_t previous) noexcept;
int lowerCurrentThread() noexcept;                     // below normal; returns the previous priority
void restoreCurrentThread (int previous) noexcept;
void lowerWorkerThread();                              // a worker of an offline engine (WorkerPool's thread start)
void enableCompositorMmcss() noexcept;

struct KeepAwake
{
    KeepAwake() noexcept : previous (keepAwakeBegin()) {}
    ~KeepAwake() { keepAwakeEnd (previous); }
    KeepAwake (const KeepAwake&) = delete;
    KeepAwake& operator= (const KeepAwake&) = delete;
    std::uint32_t previous;
};

struct BelowNormalPriority
{
    BelowNormalPriority() noexcept : previous (lowerCurrentThread()) {}
    ~BelowNormalPriority() { restoreCurrentThread (previous); }
    BelowNormalPriority (const BelowNormalPriority&) = delete;
    BelowNormalPriority& operator= (const BelowNormalPriority&) = delete;
    int previous;
};
} // namespace lynx::powerhints
