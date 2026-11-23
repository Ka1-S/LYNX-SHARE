// recbench (round 190pz): LYNX's audio recording path without an audio device, on the real classes
// (core/TakeRecording.h TakeStream + WavTakeWriter), the way AudioEngine runs them: an "audio thread" paced at real
// time pushes every block of N takes into their streams (as the device callback does), a "recording thread" drains
// them every 5 ms into crash-safe WAV files (AudioEngine::Drainer / Take::drain: 8192-frame pulls, the writer's
// header + sync every `--sync` seconds).
//
// Reports: the audio thread's cost per block and take (instructions, Linux) and its page faults / context switches
// while recording (a first-touch page or a lock shows there), the recording thread's busy time, its longest write call
// (the sync), the most frames a stream held (how close the ring came to full: capacity 2^20 frames) and the frames
// lost (turned to silence). `--stall MS`: the recording thread also sleeps MS every second (a slow or busy disk).
//
// usage: recbench --dir <folder> [--takes N] [--seconds S] [--rate R] [--block B] [--channels 1|2] [--format pcm24|float32|pcm16]
//                 [--sync S] [--stall MS] [--capacity FRAMES]

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <sys/resource.h>
#include <time.h>
#if defined(__linux__)
 #include <linux/perf_event.h>
 #include <sys/ioctl.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif

#include "core/TakeRecording.h"

namespace
{
using namespace lynx;
using Clock = std::chrono::steady_clock;

struct InstructionCounter
{
    int fd = -1;
    InstructionCounter()
    {
#if defined(__linux__)
        perf_event_attr a {};
        a.type = PERF_TYPE_HARDWARE;
        a.size = sizeof (a);
        a.config = PERF_COUNT_HW_INSTRUCTIONS;
        a.disabled = 1;
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        fd = (int) syscall (SYS_perf_event_open, &a, 0, -1, -1, 0);
#endif
    }
    ~InstructionCounter()
    {
        if (fd >= 0)
            close (fd);
    }
    void start() { if (fd >= 0) { ioctl (fd, PERF_EVENT_IOC_RESET, 0); ioctl (fd, PERF_EVENT_IOC_ENABLE, 0); } }
    long long stop()
    {
        if (fd < 0)
            return -1;
        ioctl (fd, PERF_EVENT_IOC_DISABLE, 0);
        long long v = -1;
        if (::read (fd, &v, sizeof (v)) != (ssize_t) sizeof (v))
            v = -1;
        return v;
    }
};

struct ThreadUsage
{
    long minor = 0, major = 0, voluntary = 0, involuntary = 0;
    static ThreadUsage now()
    {
        rusage u {};
        getrusage (RUSAGE_THREAD, &u);
        return { u.ru_minflt, u.ru_majflt, u.ru_nvcsw, u.ru_nivcsw };
    }
};

double ms (Clock::duration d) { return std::chrono::duration<double, std::milli> (d).count(); }
} // namespace

int main (int argc, char** argv)
{
    std::string dir;
    int takes = 8, rate = 48000, block = 128, channels = 2, capacity = 1 << 20;
    double seconds = 20.0, syncSeconds = 1.0, stallMs = 0.0;
    auto format = takes::SampleFormat::pcm24;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string ("0"); };
        if (a == "--dir") dir = next();
        else if (a == "--takes") takes = std::clamp (std::atoi (next().c_str()), 1, 64);
        else if (a == "--seconds") seconds = std::atof (next().c_str());
        else if (a == "--rate") rate = std::atoi (next().c_str());
        else if (a == "--block") block = std::max (16, std::atoi (next().c_str()));
        else if (a == "--channels") channels = std::clamp (std::atoi (next().c_str()), 1, 2);
        else if (a == "--sync") syncSeconds = std::atof (next().c_str());
        else if (a == "--stall") stallMs = std::atof (next().c_str());
        else if (a == "--capacity") capacity = std::atoi (next().c_str());
        else if (a == "--format")
        {
            const auto f = next();
            format = f == "float32" ? takes::SampleFormat::float32 : f == "pcm16" ? takes::SampleFormat::pcm16 : takes::SampleFormat::pcm24;
        }
    }
    if (dir.empty())
    {
        std::fprintf (stderr, "usage: recbench --dir <folder> [--takes N] [--seconds S] [--rate R] [--block B] ...\n");
        return 2;
    }
    std::filesystem::create_directories (dir);

    struct Take
    {
        std::unique_ptr<takes::TakeStream> stream;
        takes::WavTakeWriter writer;
        std::atomic<std::int64_t> pulled { 0 };
        std::int64_t maxHeld = 0;
    };
    std::vector<std::unique_ptr<Take>> list;
    const auto made0 = Clock::now();
    for (int k = 0; k < takes; ++k)
    {
        auto t = std::make_unique<Take>();
        t->stream = std::make_unique<takes::TakeStream> (channels, capacity);
        if (! t->writer.open (dir + "/take" + std::to_string (k) + ".wav", rate, channels, format, syncSeconds))
        {
            std::fprintf (stderr, "could not open %s\n", t->writer.error().c_str());
            return 1;
        }
        list.push_back (std::move (t));
    }
    const auto makeMs = ms (Clock::now() - made0);

    // the input: a tone with a little noise per take
    std::vector<std::vector<float>> input ((std::size_t) channels, std::vector<float> ((std::size_t) block));
    std::atomic<bool> done { false };

    // recording thread
    double busyMs = 0.0, longestWrite = 0.0;
    std::vector<double> writeMs;
    std::thread recorder ([&] {
        std::vector<float> scratch (2 * 8192);
        auto nextStall = Clock::now() + std::chrono::seconds (1);
        for (;;)
        {
            const auto finishing = done.load();
            const auto t0 = Clock::now();
            for (auto& t : list)
                for (;;)
                {
                    float* out[] = { scratch.data(), scratch.data() + 8192 };
                    const auto got = t->stream->pull (out, 8192);
                    if (got <= 0)
                        break;
                    const float* in[] = { out[0], out[1] };
                    const auto w0 = Clock::now();
                    t->writer.write (in, got);
                    const auto w = ms (Clock::now() - w0);
                    longestWrite = std::max (longestWrite, w);
                    if (w > 1.0)
                        writeMs.push_back (w);
                    t->pulled.fetch_add (got);
                }
            if (stallMs > 0.0 && Clock::now() >= nextStall)
            {
                std::this_thread::sleep_for (std::chrono::duration<double, std::milli> (stallMs));
                nextStall += std::chrono::seconds (1);
            }
            busyMs += ms (Clock::now() - t0);
            if (finishing)
                break;
            std::this_thread::sleep_for (std::chrono::milliseconds (5)); // (the Drainer's 5 ms time slice)
        }
    });

    // audio thread (this one): paced blocks
    const auto blocks = (int) (seconds * rate / block);
    const auto period = std::chrono::duration<double> ((double) block / rate);
    InstructionCounter counter;
    long long instr = 0, worstInstr = 0;
    double worstPushUs = 0.0;
    const auto u0 = ThreadUsage::now();
    auto due = Clock::now();
    std::uint32_t seed = 1;
    double phase = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        std::this_thread::sleep_until (due);
        due += std::chrono::duration_cast<Clock::duration> (period);
        for (int i = 0; i < block; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            phase += 220.0 / rate;
            const auto v = (float) (0.3 * std::sin (6.283185307179586 * phase)) + (float) ((int) (seed >> 9) - (1 << 22)) * 1e-9f;
            for (auto& c : input)
                c[(std::size_t) i] = v;
        }
        const float* data[] = { input[0].data(), input[(std::size_t) (channels - 1)].data() };
        const auto p0 = Clock::now();
        counter.start();
        for (auto& t : list)
            t->stream->push (data, block);
        const auto n = counter.stop();
        worstPushUs = std::max (worstPushUs, 1000.0 * ms (Clock::now() - p0));
        instr += n;
        worstInstr = std::max (worstInstr, n);
        for (auto& t : list)
            t->maxHeld = std::max (t->maxHeld, t->stream->pushedFrames() - t->pulled.load (std::memory_order_relaxed));
    }
    const auto u1 = ThreadUsage::now();
    for (auto& t : list)
        t->stream->finish();
    done.store (true);
    recorder.join();
    std::int64_t lost = 0, held = 0, frames = 0;
    for (auto& t : list)
    {
        lost += t->stream->lostFrames();
        held = std::max (held, t->maxHeld);
        frames += t->writer.frames();
        t->writer.close();
    }
    std::sort (writeMs.begin(), writeMs.end());
    const auto audioMs = 1000.0 * blocks * block / rate;
    std::printf ("RESULT takes=%d rate=%d block=%d ch=%d sync=%.1fs stall=%.0fms | make %.1f ms | audio thread: %.1f instr/block/take, worst block %lld instr, "
                 "worst push %.1f us, minor faults %ld, major %ld, ctx vol %ld invol %ld | recorder: busy %.2f %% of real time, longest write %.1f ms, "
                 "writes > 1 ms: %zu (p50 %.1f, p95 %.1f) | ring: most held %lld frames (%.1f %% of %d) | lost %lld frames | written %lld of %lld\n",
                 takes, rate, block, channels, syncSeconds, stallMs, makeMs, (double) instr / blocks / takes, worstInstr, worstPushUs, u1.minor - u0.minor,
                 u1.major - u0.major, u1.voluntary - u0.voluntary, u1.involuntary - u0.involuntary, 100.0 * busyMs / audioMs, longestWrite, writeMs.size(),
                 writeMs.empty() ? 0.0 : writeMs[writeMs.size() / 2], writeMs.empty() ? 0.0 : writeMs[std::min (writeMs.size() - 1, (std::size_t) (0.95 * writeMs.size()))],
                 (long long) held, 100.0 * (double) held / capacity, capacity, (long long) lost, (long long) frames, (long long) blocks * block * takes);
    return 0;
}
