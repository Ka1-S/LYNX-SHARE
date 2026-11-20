#include "core/WorkerPool.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
 #include <pthread.h>
#else
 #include <pthread.h>
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

namespace
{
// round 190kb: the lane of the calling thread (a worker's own, or the caller's while it runs a round)
thread_local const WorkerPool* lanePool = nullptr;
thread_local int laneIndex = -1;

int parsedValue (const char* spec, const char* key, int fallback) noexcept
{
    const auto length = std::strlen (key);
    for (const char* at = spec; at != nullptr && *at != 0;)
    {
        if (std::strncmp (at, key, length) == 0 && at[length] == '=')
            return std::atoi (at + length + 1);
        at = std::strchr (at, ',');
        if (at != nullptr)
            ++at;
    }
    return fallback;
}
} // namespace

void WorkerPool::nameCurrentThread (const char* text) noexcept
{
#if defined(_WIN32)
    using SetDescription = HRESULT (WINAPI*) (HANDLE, PCWSTR);
    static const auto set = (SetDescription) (void*) GetProcAddress (GetModuleHandleW (L"kernel32.dll"), "SetThreadDescription");
    if (set != nullptr)
    {
        wchar_t name[64] {};
        for (int k = 0; k < 63 && text[k] != 0; ++k)
            name[k] = (wchar_t) (unsigned char) text[k];
        set (GetCurrentThread(), name);
    }
#elif defined(__linux__)
    char name[16] {};
    std::strncpy (name, text, sizeof (name) - 1);
    pthread_setname_np (pthread_self(), name);
#elif defined(__APPLE__)
    pthread_setname_np (text);
#endif
}

WorkerPool::Tuning& WorkerPool::tuning() noexcept
{
    static Tuning t;
    static const bool fromEnvironment = [] {
        if (const char* e = std::getenv ("LYNX_POOL"); e != nullptr)
            t.apply (e);
        return true;
    }();
    (void) fromEnvironment;
    return t;
}

void WorkerPool::Tuning::apply (const char* spec) noexcept
{
    if (spec == nullptr)
        return;
    workerSpinMicros.store (std::clamp (parsedValue (spec, "spin", workerSpinMicros.load()), 0, 10000));
    drySpinMicros.store (std::clamp (parsedValue (spec, "dry", drySpinMicros.load()), 0, 10000));
    adaptiveSpin.store (parsedValue (spec, "adaptive", adaptiveSpin.load() ? 1 : 0) != 0);
    waitMode.store (std::clamp (parsedValue (spec, "wait", waitMode.load()), 0, 2));
    waitSpinMicros.store (std::clamp (parsedValue (spec, "waitspin", waitSpinMicros.load()), 0, 10000));
    helpPercent.store (std::clamp (parsedValue (spec, "help", helpPercent.load()), 0, 1000));
    liveRounds.store (parsedValue (spec, "live", liveRounds.load()) != 0 ? 1 : 0);
    // (round 190kz: "rounds=chunk" / "rounds=block", or 0 / 1)
    for (const char* at = spec; at != nullptr && *at != 0;)
    {
        if (std::strncmp (at, "rounds=", 7) == 0)
        {
            const char* v = at + 7;
            if (std::strncmp (v, "chunk", 5) == 0)
                blockRounds.store (0);
            else if (std::strncmp (v, "block", 5) == 0)
                blockRounds.store (1);
            else
                blockRounds.store (std::atoi (v) != 0 ? 1 : 0);
        }
        at = std::strchr (at, ',');
        if (at != nullptr)
            ++at;
    }
}

WorkerPool::Stats WorkerPool::stats() const noexcept
{
    Stats s;
    s.rounds = roundsRun.load (std::memory_order_relaxed);
    for (int k = 0; k < laneCount; ++k)
    {
        const auto& l = lanes[(std::size_t) k];
        const auto r = [] (const std::atomic<std::uint64_t>& a) { return a.load (std::memory_order_relaxed); };
        s.workNs += r (l.workNs);
        s.spinNs += r (l.spinNs);
        s.spins += r (l.spins);
        s.spinFound += r (l.spinFound);
        s.spinParked += r (l.spinParked);
        s.spinTimedOut += r (l.spinTimedOut);
        s.sleeps += r (l.sleeps);
        s.earlyWakes += r (l.earlyWakes);
        s.waitNs += r (l.waitNs);
        s.waits += r (l.waits);
        s.waitYields += r (l.waitYields);
        s.waitSleeps += r (l.waitSleeps);
        s.waitSleepNs += r (l.waitSleepNs);
        s.callerWaitNs += r (l.callerWaitNs);
        s.callerSleeps += r (l.callerSleeps);
    }
    return s;
}

WorkerPool::Lane* WorkerPool::currentLane() const noexcept
{
    return lanePool == this && laneIndex >= 0 && laneIndex < laneCount ? &lanes[(std::size_t) laneIndex] : nullptr;
}

void WorkerPool::awaitSlow (int key, void* ready, ReadyFn fn) noexcept
{
    using clock = std::chrono::steady_clock;
    auto& t = tuning();
    auto* lane = currentLane();
    // (a thread outside this pool's round can't be woken by notify: it spins as before)
    const auto mode = lane != nullptr ? t.waitMode.load (std::memory_order_relaxed) : 0;
    const auto spinFor = std::chrono::microseconds (t.waitSpinMicros.load (std::memory_order_relaxed));
    const auto start = clock::now();
    std::uint64_t yields = 0, sleeps = 0, sleptNs = 0;

    for (int spins = 0; ! fn (ready); ++spins)
    {
        cpuRelax();
        if (mode == 0)
        {
            if ((spins & 1023) == 1023)
            {
                std::this_thread::yield();
                ++yields;
            }
            continue;
        }
        if ((spins & 63) != 63 || clock::now() - start < spinFor)
            continue;

        // sleep. Announce the key first, then look once more: notify() publishes its result before it reads
        // `waitSleepers`, so either it sees this thread asleep and takes the key, or this check sees the result
        lane->sleepKey.store (key, std::memory_order_seq_cst);
        waitSleepers.fetch_add (1, std::memory_order_seq_cst);
        if (fn (ready))
        {
            if (lane->sleepKey.exchange (-1, std::memory_order_acq_rel) == -1)
                lane->sleep.acquire(); // (a notify took the key at the same moment: its wake-up is ours to take)
            waitSleepers.fetch_sub (1, std::memory_order_seq_cst);
            break;
        }
        const auto asleep = clock::now();
        lane->sleep.acquire();
        waitSleepers.fetch_sub (1, std::memory_order_seq_cst);
        ++sleeps;
        sleptNs += (std::uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (clock::now() - asleep).count();
        spins = 0; // (woken: the result is there; should it not be, spin and sleep again)
    }

    if (lane != nullptr)
    {
        bump (lane->waitNs, (std::uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (clock::now() - start).count());
        bump (lane->waits, 1);
        bump (lane->waitYields, yields);
        bump (lane->waitSleeps, sleeps);
        bump (lane->waitSleepNs, sleptNs);
    }
}

void WorkerPool::noteWait (std::uint64_t ns, bool handedOff) noexcept
{
    if (auto* lane = currentLane())
    {
        bump (lane->waitNs, ns);
        bump (lane->waits, 1);
        if (handedOff)
            bump (lane->waitSleeps, 1);
    }
}

void WorkerPool::notifySlow (int key) noexcept
{
    for (int k = 0; k < laneCount; ++k)
    {
        auto expected = key;
        if (lanes[(std::size_t) k].sleepKey.compare_exchange_strong (expected, -1, std::memory_order_acq_rel))
            lanes[(std::size_t) k].sleep.release();
    }
}

WorkerPool::WorkerPool (int n, std::function<void()> onThreadStart) : wake (std::make_unique<OsSemaphore>())
{
    // ticket 0: count 0, nothing to take until the first parallelFor
    laneCount = std::max (0, n) + 1;
    lanes = std::make_unique<Lane[]> ((std::size_t) laneCount);

    for (int i = 0; i < n; ++i)
        threads.emplace_back ([this, i, onThreadStart] { workerLoop (i, onThreadStart); });
}

WorkerPool::~WorkerPool()
{
    quit.store (true);
    wake->release ((int) threads.size());

    for (auto& t : threads)
        t.join();
}

void WorkerPool::setThreadRefresh (std::function<void()> fn)
{
    threadRefresh = std::move (fn); // (the workers read it only after a request: its release publishes it)
}

void WorkerPool::refreshIfAsked (std::uint32_t& seen) const
{
    if (const auto now = refreshGeneration.load (std::memory_order_acquire); now != seen)
    {
        seen = now;
        if (threadRefresh)
            threadRefresh();
    }
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

void WorkerPool::workerLoop (int index, std::function<void()> onStart)
{
    const ScopedFlushDenormals noDenormals;
    {
        char name[32];
        std::snprintf (name, sizeof (name), "LYNX worker %d", index + 1);
        nameCurrentThread (name);
    }
    lanePool = this;
    laneIndex = index;
    auto& lane = lanes[(std::size_t) index];
    auto& tune = tuning();

    if (onStart)
        onStart();

    using clock = std::chrono::steady_clock;
    const auto ns = [] (clock::duration d) { return (std::uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (d).count(); };
    std::uint32_t refreshSeen = 0; // (the generation the pool started with: a request made before this thread ran counts too)
    auto cpuMark = threadCpuNs();
    int dry = 0;                   // round 190kb: spins in a row that found no work
    constexpr int dryEpisodes = 4;
    auto timedWork = [&] (clock::time_point from) {
        work (true);
        const auto end = clock::now();
        bump (lane.workNs, ns (end - from));
        return end;
    };
    auto spinStart = clock::now();

    while (! quit.load (std::memory_order_relaxed))
    {
        refreshIfAsked (refreshSeen);

        // spin a little: within a block the next parallelFor (next mixer layer or chunk) follows right away. Round
        // 190kb: after a few dry spins (one parallelFor per block, the task graph: the next round is a whole block
        // away) only a short one; the full spin returns once a wake-up comes within it
        const auto fullSpin = std::chrono::microseconds (tune.workerSpinMicros.load (std::memory_order_relaxed));
        const auto spinFor = tune.adaptiveSpin.load (std::memory_order_relaxed) && dry >= dryEpisodes
                                 ? std::min (fullSpin, std::chrono::microseconds (tune.drySpinMicros.load (std::memory_order_relaxed)))
                                 : fullSpin;
        auto found = false, parked = false;
        auto spinEnd = spinStart;

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
                {
                    parked = true;
                    break;
                }
                if ((spinEnd = clock::now()) - spinStart > spinFor)
                    break;
            }
        }
        if (found || parked)
            spinEnd = clock::now();
        bump (lane.spinNs, ns (spinEnd - spinStart));
        bump (lane.spins, 1);
        bump (found ? lane.spinFound : parked ? lane.spinParked : lane.spinTimedOut, 1);

        if (found)
        {
            dry = 0;
            spinStart = timedWork (spinEnd);
            const auto now = threadCpuNs(); // (round 190gq: the workers' CPU, spinning included)
            workersCpu.fetch_add (now - cpuMark, std::memory_order_relaxed);
            cpuMark = now;
            continue;
        }
        if (! parked)
            ++dry;

        // sleep. Announce it first, then look once more: run() publishes the ticket before reading `sleeping`,
        // so either it sees this worker asleep and releases the semaphore, or this check sees the work
        sleeping.fetch_add (1, std::memory_order_seq_cst);

        if (hasWork())
        {
            sleeping.fetch_sub (1, std::memory_order_seq_cst);
            spinStart = timedWork (clock::now());
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
        bump (lane.sleeps, 1);
        const auto woke = clock::now();
        // (round 190kb) woken within what the full spin would have waited: spinning pays off again
        if (! parked && woke - spinStart <= fullSpin)
        {
            dry = 0;
            bump (lane.earlyWakes, 1);
        }

        if (quit.load())
            return;

        refreshIfAsked (refreshSeen); // (before the block's work: a new workgroup is joined before working for it)
        spinStart = timedWork (woke);
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

    // round 190kb: the caller's lane while it works on this round (await() inside its indices can sleep on it)
    struct CallerLane
    {
        const WorkerPool* pool;
        int index;
        ~CallerLane() { lanePool = pool, laneIndex = index; }
    } restore { lanePool, laneIndex };
    lanePool = this;
    laneIndex = laneCount - 1;
    auto& lane = lanes[(std::size_t) laneCount - 1];
    roundsRun.store (roundsRun.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);

    context = ctx;
    call = fn;
    target = n;
    done.store (0, std::memory_order_relaxed);
    ++generation;
    const auto base = generation << (2 * indexBits);
    ticket.store (base | ((std::uint64_t) n << indexBits), std::memory_order_seq_cst); // from here on the workers may take indices

    // wake only the sleepers that are needed (spinning workers already see the ticket)
    if (const auto asleep = sleeping.load (std::memory_order_seq_cst); asleep > 0)
    {
        const auto limit = wakeLimit.load (std::memory_order_relaxed); // (round 190kr)
        if (const auto wanted = std::min ({ asleep, n - 1, limit < 0 ? n : limit }); wanted > 0)
            wake->release (wanted);
    }

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
        const auto waitStart = std::chrono::steady_clock::now();
        struct Timed
        {
            Lane& lane;
            std::chrono::steady_clock::time_point start;
            ~Timed() { bump (lane.callerWaitNs, (std::uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - start).count()); }
        } timed { lane, waitStart };
        const auto spinUntil = waitStart + std::chrono::microseconds (callerSpinMicros);
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
                bump (lane.callerSleeps, 1);
                break;
            }
        }
    }

    ticket.store (base, std::memory_order_release); // count 0: late workers take nothing
}
} // namespace lynx
