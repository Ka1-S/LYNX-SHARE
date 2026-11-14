#pragma once

// Thread pool for the audio thread to split a block's work (channels and
// mixer tracks), like FL does with the mixer layers (docs/ANALISE_FL_STUDIO.md §2.2).
// parallelFor neither allocates nor blocks on a mutex: workers take indices from an atomic counter; the calling
// thread also works and spin-waits for the last index.
//
// Waking the workers (REA round 87): a worker that just finished spins for a short while (the next parallelFor of
// the same block — the next mixer layer, the next chunk — comes microseconds later), then sleeps on an OS
// semaphore (a Win32 semaphore on Windows, a libdispatch one on macOS, a futex-backed POSIX one on Linux). std::counting_semaphore is NOT used:
// the Windows libc++ build has no platform wait and falls back to Sleep() polling with growing intervals, so an
// idle worker woke 1–8 ms late — later than a whole audio block — and the audio thread ended up doing all the
// work alone.
//
// Parking (REA round 116): the spin only pays off between the parallelFor calls of one block. After the block's last
// one, park() sends the spinning workers to sleep at once; otherwise every worker burned ~250 µs per audio block
// (7 workers × 172 blocks/s at 256 samples ≈ 30 % of a core) even with the transport stopped.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace lynx
{
// An OS semaphore (a Win32 semaphore on Windows, a libdispatch one on macOS, a futex-backed POSIX one on Linux): the
// workers sleep on one, so does the render-ahead thread (round 190gq, core/RenderAhead.h). release() never blocks.
class OsSemaphore
{
public:
    OsSemaphore();
    ~OsSemaphore();
    OsSemaphore (const OsSemaphore&) = delete;
    OsSemaphore& operator= (const OsSemaphore&) = delete;
    void release (int n = 1) noexcept;
    void acquire() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class WorkerPool
{
public:
    // `threads` workers besides the calling thread. `onThreadStart` runs on each worker
    // (e.g. raising the priority); may be empty.
    explicit WorkerPool (int threads, std::function<void()> onThreadStart = {});
    ~WorkerPool();

    WorkerPool (const WorkerPool&) = delete;
    WorkerPool& operator= (const WorkerPool&) = delete;

    int workers() const noexcept { return (int) threads.size(); }

    // a spin-wait pause (x86 PAUSE / ARM YIELD), for callers waiting on each other inside a parallelFor
    static void relax() noexcept;

    // Calls fn(i) for i in [0, count), in parallel. Returns when all have finished.
    template <typename Fn>
    void parallelFor (int count, Fn& fn) noexcept
    {
        run (count, &fn, [] (void* f, int i) { (*static_cast<Fn*> (f)) (i); });
    }

    // The caller's block is finished (no more parallelFor until the next audio callback): spinning workers go to
    // sleep now instead of polling until spinMicros runs out. Wait-free; the next parallelFor wakes them as usual.
    void park() noexcept { parkedGeneration.store (generation, std::memory_order_release); }

    // How long a worker keeps polling for new work after finishing before it sleeps (microseconds). REA round 116
    // measured 250 → 60 on Windows (FL Studio ASIO, 256 samples, Thief / Exoplanet playing): process CPU 106/114 % →
    // 70/90 % of a core, the callback's average and peak load unchanged (the gaps between one block's parallelFor
    // calls are usually far shorter than 60 µs; a longer one costs a semaphore wake-up)
    static constexpr int spinMicros = 60;

    // Diagnostics: how many indices the workers (not the calling thread) ran, and how many times a worker
    // was woken from sleep. Relaxed counters.
    std::uint64_t indicesRunByWorkers() const noexcept { return byWorkers.load (std::memory_order_relaxed); }
    std::uint64_t wakeUps() const noexcept { return wokenUp.load (std::memory_order_relaxed); }

private:
    using Call = void (*) (void*, int);

    void run (int count, void* context, Call call) noexcept;
    bool work (bool isWorker) noexcept;
    bool hasWork() const noexcept;
    bool isParked() const noexcept;
    void workerLoop (std::function<void()> onStart);

    // Round 190gq (190gs Q5): what the hot path shares sits on cache lines of its own — the read-mostly work, the
    // ticket every index takes, the count the caller spins on, the sleep / park state — and the counters are added once
    // per batch, not per index: on a two-CCX Ryzen each line hand-off cost ~70-100 ns, three per index.
    std::unique_ptr<OsSemaphore> wake;
    std::vector<std::thread> threads;
    std::atomic<bool> quit { false };
    alignas (64) std::atomic<int> sleeping { 0 }; // workers blocked (or about to block) on `wake`
    // the generation (see `ticket`) of the last parallelFor before park(); while the ticket still carries it,
    // the pool is parked and a worker without work sleeps instead of spinning
    std::atomic<std::uint64_t> parkedGeneration { ~std::uint64_t (0) };

    // current work (published before `ticket`). The ticket packs generation (bits 40+), count (bits 20…39) and the
    // next index (bits 0…19), so an index taken by a late worker always carries its own round's count: a worker
    // that stalls between taking an index and running it can never run an index of a later round (no ABA)
    alignas (64) void* context = nullptr;
    Call call = nullptr;
    static constexpr int indexBits = 20;
    alignas (64) std::atomic<std::uint64_t> ticket { 0 };
    alignas (64) std::atomic<int> done { 0 };
    alignas (64) std::uint64_t generation = 0; // caller thread only
    std::atomic<std::uint64_t> byWorkers { 0 }, wokenUp { 0 };
};
} // namespace lynx
