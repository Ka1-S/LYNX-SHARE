// Round toolchain190gt: release build flags and the audio threads' OS set-up.
// (1) WorkerPool::setThreadRefresh / requestThreadRefresh: a per-thread set-up that runs on every worker, on that
//     worker's own thread, once per request (macOS joins the workers to the audio device's workgroup with it).
// (2) Every worker runs its work with flush-to-zero + denormals-are-zero (the audio callback sets it on its own thread).
// (3) Source guards: the audio threads (device, workers, prefetcher, sandbox / bridge) opt out of Windows' EcoQoS and the
//     process stays out of it while a device is open (platform/PowerThrottling.h); the device's workgroup is joined on
//     start and left on stop (macOS).

#include <mutex>

namespace toolchain190gt
{
// runs parallelFor rounds until every worker has done `want` refreshes (a sleeping worker refreshes when it wakes)
inline bool waitForRefreshes (lynx::WorkerPool& pool, const std::map<std::thread::id, int>& counts, std::mutex& m, int want,
                              int rounds = 4000)
{
    std::atomic<int> sink { 0 };
    auto fn = [&] (int i) {
        for (int k = 0; k < 200; ++k)
            sink.fetch_add (i & 1, std::memory_order_relaxed);
    };
    for (int round = 0; round < rounds; ++round)
    {
        pool.parallelFor (64, fn);
        {
            const std::lock_guard<std::mutex> lock (m);
            auto all = (int) counts.size() == pool.workers();
            for (const auto& [id, n] : counts)
                all = all && n >= want;
            if (all)
                return true;
        }
        if (round % 50 == 49)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return false;
}
} // namespace toolchain190gt

TEST (toolchain190gt_worker_refresh_runs_once_per_request_on_every_worker)
{
    lynx::WorkerPool pool (3);
    std::mutex m;
    std::map<std::thread::id, int> counts;
    pool.setThreadRefresh ([&] {
        const std::lock_guard<std::mutex> lock (m);
        ++counts[std::this_thread::get_id()];
    });
    // nothing runs before a request
    toolchain190gt::waitForRefreshes (pool, counts, m, 1, 100); // (work for the workers, no request yet)
    {
        const std::lock_guard<std::mutex> lock (m);
        CHECK (counts.empty());
    }
    pool.requestThreadRefresh();
    CHECK (toolchain190gt::waitForRefreshes (pool, counts, m, 1));
    {
        const std::lock_guard<std::mutex> lock (m);
        CHECK ((int) counts.size() == 3);
        CHECK (counts.count (std::this_thread::get_id()) == 0); // only the workers, never the calling thread
        for (const auto& [id, n] : counts)
            CHECK (n == 1);
    }
    pool.requestThreadRefresh();
    CHECK (toolchain190gt::waitForRefreshes (pool, counts, m, 2));
    {
        const std::lock_guard<std::mutex> lock (m);
        for (const auto& [id, n] : counts)
            CHECK (n == 2);
    }
}

TEST (toolchain190gt_workers_run_with_flush_to_zero_and_denormals_are_zero)
{
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64)
    lynx::WorkerPool pool (3);
    const auto caller = std::this_thread::get_id();
    std::atomic<int> byWorkers { 0 }, withoutFlush { 0 };
    std::atomic<float> sink { 0.0f };
    auto fn = [&] (int i) {
        float x = 1.0e-30f + (float) i;
        for (int k = 0; k < 4000; ++k)
            x = x * 0.999f + 1.0e-38f;
        sink.store (x, std::memory_order_relaxed);
        if (std::this_thread::get_id() != caller)
        {
            byWorkers.fetch_add (1);
            // FTZ (bit 15) and DAZ (bit 6); on ARM64 sse2neon maps both to FPCR.FZ
            if ((_mm_getcsr() & 0x8040u) != 0x8040u)
                withoutFlush.fetch_add (1);
        }
    };
    for (int round = 0; round < 200 && byWorkers.load() < 50; ++round)
        pool.parallelFor (64, fn);
    CHECK (byWorkers.load() > 0);
    CHECK (withoutFlush.load() == 0);
#endif
}

TEST (toolchain190gt_audio_threads_opt_out_of_ecoqos_and_join_the_device_workgroup)
{
    using namespace i18ntest;
    const auto engine = readFile ("src/engine/AudioEngine.cpp");
    // the workers (their start: raiseAudioThreadPriority) and the device's own callback thread
    const auto raise = engine.find ("void raiseAudioThreadPriority()");
    const auto raiseEnd = engine.find ("\n}\n", raise);
    CHECK (raise != std::string::npos && engine.find ("power::optOutOfPowerThrottling();", raise) < raiseEnd);
    const auto callback = engine.find ("void AudioEngine::audioDeviceIOCallbackWithContext");
    CHECK (callback != std::string::npos && engine.find ("power::optOutOfPowerThrottling();", callback) != std::string::npos);
    // MMCSS "Pro Audio" at CRITICAL, as Live (REA 190hc), and the reached priority in the real-time report
    CHECK (engine.find ("avPriority (handle, 2 /* AVRT_PRIORITY_CRITICAL */)", raise) < raiseEnd);
    CHECK (engine.find ("audio threads raised ") != std::string::npos);
    CHECK (readFile ("src/platform/PowerThrottling.h").find ("avPriority (handle, 2 /* AVRT_PRIORITY_CRITICAL") != std::string::npos);
    // the process priority class chosen on device start (WinMM raises it to HIGH otherwise), and reported
    CHECK (engine.find ("choosePriorityClass();", engine.find ("void AudioEngine::audioDeviceAboutToStart")) < engine.find ("void AudioEngine::audioDeviceStopped"));
    CHECK (engine.find ("process priority class ") != std::string::npos);
    // the stream prefetcher's thread
    CHECK (readFile ("src/engine/StreamPrefetcher.cpp").find ("power::optOutOfPowerThrottling();") != std::string::npos);
    // the plugin sandbox's / 32-bit bridge's audio thread: MMCSS + no EcoQoS
    const auto sandbox = readFile ("src/engine/SandboxWorker.cpp");
    CHECK (sandbox.find ("lynx::power::becomeProAudioThread();", sandbox.find ("void run() override")) != std::string::npos);
    // the whole process out of EcoQoS while a device is open (held on start, given back on stop)
    CHECK (engine.find ("power::holdProcessFullSpeed (true);", engine.find ("void AudioEngine::audioDeviceAboutToStart")) < engine.find ("void AudioEngine::audioDeviceStopped"));
    CHECK (engine.find ("power::holdProcessFullSpeed (false);", engine.find ("void AudioEngine::audioDeviceStopped")) != std::string::npos);
    // the device's workgroup: joined on start, left on stop
    const auto start = engine.find ("void AudioEngine::audioDeviceAboutToStart");
    const auto stop = engine.find ("void AudioEngine::audioDeviceStopped");
    CHECK (start != std::string::npos && engine.find ("joinDeviceWorkgroup (device);", start) < stop);
    CHECK (stop != std::string::npos && engine.find ("joinDeviceWorkgroup (nullptr);", stop) < engine.find ("\n}\n", stop));
    // the pool's refresh is set before the first request
    const auto join = engine.find ("void AudioEngine::joinDeviceWorkgroup");
    CHECK (join != std::string::npos && engine.find ("setThreadRefresh", join) < engine.find ("requestThreadRefresh", join));
}
