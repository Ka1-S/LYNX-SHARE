#include "core/WorkerPool.h"

#include <chrono>
#include <cstdlib>
#include <ctime>

#include "core/Denormals.h"

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#elif defined(__APPLE__)
 #include <dispatch/dispatch.h>
#else
 #include <semaphore.h>
#endif

#if defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
 #include "core/Simd.h"
#endif

namespace lynx
{
namespace
{
inline void cpuRelax() noexcept
{
#if defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
    _mm_pause();
#elif defined(__aarch64__)
    asm volatile ("yield");
#endif
}
} // namespace

#if defined(_WIN32)
struct OsSemaphore::Impl
{
    Impl() : handle (CreateSemaphoreW (nullptr, 0, 0x7fffffff, nullptr)) {}
    ~Impl()
    {
        if (handle != nullptr)
            CloseHandle (handle);
    }
    void release (int n) noexcept { ReleaseSemaphore (handle, n, nullptr); }
    void acquire() noexcept { WaitForSingleObject (handle, INFINITE); }
    HANDLE handle;
};
#elif defined(__APPLE__)
// libdispatch semaphore: a Mach semaphore underneath, woken by the kernel at once
struct OsSemaphore::Impl
{
    Impl() : handle (dispatch_semaphore_create (0)) {}
    ~Impl() { dispatch_release (handle); }
    void release (int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            dispatch_semaphore_signal (handle);
    }
    void acquire() noexcept { dispatch_semaphore_wait (handle, DISPATCH_TIME_FOREVER); }
    dispatch_semaphore_t handle;
};
#else
// POSIX semaphore: glibc implements it on a futex (uncontended post/wait stay in user space, a sleeper is woken
// by the kernel at once)
struct OsSemaphore::Impl
{
    Impl() { sem_init (&handle, 0, 0); }
    ~Impl() { sem_destroy (&handle); }
    void release (int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            sem_post (&handle);
    }
    void acquire() noexcept
    {
        while (sem_wait (&handle) != 0) {} // EINTR: a signal interrupted the wait
    }
    sem_t handle;
};
#endif

OsSemaphore::OsSemaphore() : impl (std::make_unique<Impl>()) {}
OsSemaphore::~OsSemaphore() = default;
void OsSemaphore::release (int n) noexcept
{
    if (n > 0)
        impl->release (n);
}
void OsSemaphore::acquire() noexcept { impl->acquire(); }

void WorkerPool::relax() noexcept
{
    cpuRelax();
}

std::uint64_t WorkerPool::threadCpuNs() noexcept
{
#if defined(_WIN32)
    FILETIME created, exited, kernel, user;
    if (! GetThreadTimes (GetCurrentThread(), &created, &exited, &kernel, &user))
        return 0;
    const auto k = ((std::uint64_t) kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
    const auto u = ((std::uint64_t) user.dwHighDateTime << 32) | user.dwLowDateTime;
    return (k + u) * 100;
#else
    timespec ts {};
    if (clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
        return 0;
    return (std::uint64_t) ts.tv_sec * 1000000000ull + (std::uint64_t) ts.tv_nsec;
#endif
}

WorkerPool::WorkerPool (int n, std::function<void()> onThreadStart) : wake (std::make_unique<OsSemaphore>())
{
    // ticket 0: count 0, nothing to take until the first parallelFor

    for (int i = 0; i < n; ++i)
        threads.emplace_back ([this, onThreadStart] { workerLoop (onThreadStart); });
}

WorkerPool::~WorkerPool()
{
    quit.store (true);
    wake->release ((int) threads.size());

    for (auto& t : threads)
        t.join();
}

bool WorkerPool::hasWork() const noexcept
{
    constexpr auto mask = (std::uint64_t (1) << indexBits) - 1;
    const auto t = ticket.load (std::memory_order_acquire);
    return (t & mask) < ((t >> indexBits) & mask);
}

bool WorkerPool::isParked() const noexcept
{
    return (ticket.load (std::memory_order_acquire) >> (2 * indexBits)) == parkedGeneration.load (std::memory_order_acquire);
}

void WorkerPool::workerLoop (std::function<void()> onStart)
{
    const ScopedFlushDenormals noDenormals;

    if (onStart)
        onStart();

    using clock = std::chrono::steady_clock;
    auto cpuMark = threadCpuNs();

    while (! quit.load (std::memory_order_relaxed))
    {
        // spin a little: within a block the next parallelFor (next mixer layer or chunk) follows right away
        auto spinStart = clock::now();
        auto found = false;

        for (int i = 0;; ++i)
        {
            if (hasWork())
            {
                found = true;
                break;
            }

            cpuRelax();

            if ((i & 63) == 63)
            {
                if (quit.load (std::memory_order_relaxed))
                    return;
                if (isParked()) // the block is over: sleep now
                    break;
                if (clock::now() - spinStart > std::chrono::microseconds (spinMicros))
                    break;
            }
        }

        if (found)
        {
            work (true);
            const auto now = threadCpuNs(); // (round 190gq: the workers' CPU, spinning included)
            workersCpu.fetch_add (now - cpuMark, std::memory_order_relaxed);
            cpuMark = now;
            continue;
        }

        // sleep. Announce it first, then look once more: run() publishes the ticket before reading `sleeping`,
        // so either it sees this worker asleep and releases the semaphore, or this check sees the work
        sleeping.fetch_add (1, std::memory_order_seq_cst);

        if (hasWork())
        {
            sleeping.fetch_sub (1, std::memory_order_seq_cst);
            work (true);
            continue;
        }

        {
            const auto now = threadCpuNs();
            workersCpu.fetch_add (now - cpuMark, std::memory_order_relaxed);
            wake->acquire();
            cpuMark = threadCpuNs();
        }
        sleeping.fetch_sub (1, std::memory_order_seq_cst);
        wokenUp.fetch_add (1, std::memory_order_relaxed);

        if (quit.load())
            return;

        work (true);
    }
}

bool WorkerPool::work (bool isWorker) noexcept
{
    constexpr auto mask = (std::uint64_t (1) << indexBits) - 1;
    auto any = false;
    // (round 190gq, 190gs Q5) the indices this batch ran, added to `done` once when the batch ends — before the round
    // can end, since the caller waits for all of them — and kept apart per round in case a ticket of the next one
    // comes up in the same loop
    int ran = 0;
    std::uint64_t ranGeneration = 0;
    auto flush = [&] {
        if (ran == 0)
            return;
        if (isWorker)
            byWorkers.fetch_add ((std::uint64_t) ran, std::memory_order_relaxed);
        if (done.fetch_add (ran, std::memory_order_acq_rel) + ran == target && callerWaiting.exchange (false, std::memory_order_acq_rel))
            finished->release(); // (the last of the round: the caller sleeps)
        ran = 0;
    };

    while (true)
    {
        const auto t = ticket.fetch_add (1, std::memory_order_acq_rel);
        const auto i = t & mask, n = (t >> indexBits) & mask, g = t >> (2 * indexBits);
        if (ran > 0 && g != ranGeneration)
            flush();

        if (i >= n)
        {
            flush();
            return any;
        }

        call (context, (int) i);
        any = true;
        ranGeneration = g;
        ++ran;
    }
}

void WorkerPool::run (int n, void* ctx, Call fn) noexcept
{
    if (n <= 0)
        return;

    if (n == 1 || threads.empty() || n >= (1 << indexBits))
    {
        for (int i = 0; i < n; ++i)
            fn (ctx, i);
        return;
    }

    context = ctx;
    call = fn;
    target = n;
    done.store (0, std::memory_order_relaxed);
    ++generation;
    const auto base = generation << (2 * indexBits);
    ticket.store (base | ((std::uint64_t) n << indexBits), std::memory_order_seq_cst); // from here on the workers may take indices

    // wake only the sleepers that are needed (spinning workers already see the ticket)
    if (const auto asleep = sleeping.load (std::memory_order_seq_cst); asleep > 0)
        wake->release (std::min (asleep, n - 1));

    work (false);

    // (round 190gq) a short spin for the last indices, then sleep until the worker finishing the round wakes us
    // (LYNX_CALLER_SLEEP=0: the old spin-and-yield wait, for A/B runs)
    static const bool callerSleeps = [] { const char* e = std::getenv ("LYNX_CALLER_SLEEP"); return e == nullptr || std::atoi (e) != 0; }();
    if (! callerSleeps)
        for (int spins = 0; done.load (std::memory_order_acquire) < n; ++spins)
        {
            cpuRelax();
            if ((spins & 1023) == 1023)
                std::this_thread::yield();
        }
    if (done.load (std::memory_order_acquire) < n)
    {
        const auto spinUntil = std::chrono::steady_clock::now() + std::chrono::microseconds (callerSpinMicros);
        for (int spins = 0; done.load (std::memory_order_acquire) < n; ++spins)
        {
            cpuRelax();
            if ((spins & 63) == 63 && std::chrono::steady_clock::now() > spinUntil)
            {
                callerWaiting.store (true, std::memory_order_seq_cst);
                if (done.load (std::memory_order_seq_cst) >= n)
                {
                    // finished meanwhile: take back the flag, or the wake already sent for it
                    if (! callerWaiting.exchange (false, std::memory_order_acq_rel))
                        finished->acquire();
                    break;
                }
                finished->acquire();
                break;
            }
        }
    }

    ticket.store (base, std::memory_order_release); // count 0: late workers take nothing
}
} // namespace lynx
