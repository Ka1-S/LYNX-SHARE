#pragma once

// Rollup 0.1.30 step 9 (round 190lu): the instructions the calling thread retires in user space, for --paintbench (the
// performance gate's paint cells). A software-rendered frame of the same pixels retires nearly the same instructions on
// every run and on any x86-64 model, so the gate can hold paint to a tight limit where wall / CPU time (divided by a
// calculation-bound calibration) moves with the runner's memory system. Linux only (perf_event_open, as tools/perfbench
// counts the audio rendering); -1 where the counter cannot be read (macOS, Windows, a VM without the PMU,
// perf_event_paranoid too strict).

#if defined(__linux__)
 #include <linux/perf_event.h>
 #include <sys/ioctl.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif

namespace lynx::platform
{
class ThreadInstructions
{
public:
#if defined(__linux__)
    ThreadInstructions()
    {
        perf_event_attr a {};
        a.type = PERF_TYPE_HARDWARE;
        a.size = sizeof (a);
        a.config = PERF_COUNT_HW_INSTRUCTIONS;
        a.disabled = 1;
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        fd = (int) syscall (SYS_perf_event_open, &a, 0, -1, -1, 0);
        if (fd >= 0)
            ioctl (fd, PERF_EVENT_IOC_ENABLE, 0);
    }
    ~ThreadInstructions()
    {
        if (fd >= 0)
            close (fd);
    }
    // the running count (-1: not available); the difference of two reads is what the thread retired between them
    long long now() const
    {
        long long v = -1;
        if (fd < 0 || ::read (fd, &v, sizeof (v)) != (ssize_t) sizeof (v))
            return -1;
        return v;
    }
    bool available() const { return fd >= 0; }

private:
    int fd = -1;
#else
    ThreadInstructions() = default;
    long long now() const { return -1; }
    bool available() const { return false; }
#endif
    ThreadInstructions (const ThreadInstructions&) = delete;
    ThreadInstructions& operator= (const ThreadInstructions&) = delete;
};
} // namespace lynx::platform
