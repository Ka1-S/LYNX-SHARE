// Round 190pz (0.1.32 perf study: LYNX's native plugins and recording). The recording path: "Safe overloads" never
// yields while audio takes record (core/OverloadGuard.h), the take writer's durable sync every 5 s with the takes spread
// over it (core/TakeRecording.h: the file's bytes are the same as before), the take stream's copies in two pieces
// (the same samples, the same timeline). nativeperf_190pz/RECORDING.md.

#include <random>

#include "core/OverloadGuard.h"
#include "core/TakeRecording.h"

TEST (nativeperf190pz_overload_guard_never_yields_while_audio_takes_record)
{
    using namespace lynx;
    GuardRun r; // (tests/Perf190osTests.inc: 64-sample callbacks at 48 kHz, the UI starving)
    r.run (600.0, 2.0 * GuardRun::budget, true);
    CHECK (r.g.engaged() && r.g.timesEngaged() == 1);
    // a take starts: the very next callback yields nothing and the guard is off
    r.g.setRecordingAudio (true);
    CHECK (r.g.isRecordingAudio());
    CHECK (r.g.afterCallback (r.now, 2.0 * GuardRun::budget, GuardRun::budget, true) == 0.0);
    CHECK (! r.g.engaged());
    r.now += 2.0 * GuardRun::budget;
    // 3 s of a deep overload while recording, the screen starving: never engaged, nothing given up
    const auto yielded = r.g.yieldedMs();
    for (const auto heavy : { 2.0, 5.0, 12.0 })
    {
        r.run (1000.0, heavy * GuardRun::budget, true);
        CHECK (! r.g.engaged() && r.g.timesEngaged() == 1 && r.g.yieldedMs() == yielded);
    }
    // the takes close, the overload goes on: the guard's usual rule again
    r.g.setRecordingAudio (false);
    r.run (600.0, 2.0 * GuardRun::budget, true);
    CHECK (r.g.engaged() && r.g.timesEngaged() == 2 && r.g.yieldedMs() > yielded);
}

namespace nativeperf190pz
{
std::string tempWav (const std::string& name)
{
    return (std::filesystem::temp_directory_path() / ("lynx-nativeperf190pz-" + processTempTag() + "-" + name)).string();
}

std::vector<unsigned char> bytesOf (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    return std::vector<unsigned char> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
}

// writes 7.3 s of a stereo programme in uneven pieces (as the recording thread's drains come) through a writer opened
// with these sync settings; `abandonAt` > 0: stops there as a crash would (no final header)
void writeTake (const std::string& path, lynx::takes::SampleFormat format, double syncSeconds, double durableSeconds, double phase,
                std::int64_t abandonAt = 0)
{
    using namespace lynx::takes;
    WavTakeWriter w;
    CHECK (w.open (path, 48000.0, 2, format, syncSeconds, durableSeconds, phase));
    std::mt19937 rng (190);
    std::uniform_int_distribution<int> piece (1, 3000);
    std::vector<float> l (3000), r (3000);
    std::int64_t done = 0;
    const std::int64_t total = (std::int64_t) (7.3 * 48000.0);
    while (done < total && (abandonAt <= 0 || done < abandonAt))
    {
        const auto n = (int) std::min<std::int64_t> (piece (rng), total - done);
        for (int i = 0; i < n; ++i)
        {
            const auto t = (double) (done + i) / 48000.0;
            l[(std::size_t) i] = (float) (0.5 * std::sin (6.283185307179586 * 220.0 * t));
            r[(std::size_t) i] = (float) (0.25 * std::sin (6.283185307179586 * 330.0 * t));
        }
        const float* d[] = { l.data(), r.data() };
        CHECK (w.write (d, n));
        done += n;
    }
    if (abandonAt > 0)
        w.abandon();
    else
        CHECK (w.close());
}
} // namespace nativeperf190pz

TEST (nativeperf190pz_take_writer_durable_every_5_s_writes_the_same_file)
{
    using namespace lynx::takes;
    for (const auto format : { SampleFormat::pcm24, SampleFormat::float32, SampleFormat::pcm16 })
    {
        // before round 190pz every 1-second flush was durable (durable = sync); now durable every 5 s, spread by a phase
        const auto before = nativeperf190pz::tempWav ("before.wav"), after = nativeperf190pz::tempWav ("after.wav"),
                   late = nativeperf190pz::tempWav ("late.wav");
        nativeperf190pz::writeTake (before, format, 1.0, 1.0, 0.0);
        nativeperf190pz::writeTake (after, format, 1.0, 5.0, 0.0);
        nativeperf190pz::writeTake (late, format, 1.0, 5.0, 0.6875);
        const auto a = nativeperf190pz::bytesOf (before);
        CHECK (a.size() > 44 && a == nativeperf190pz::bytesOf (after) && a == nativeperf190pz::bytesOf (late));
        std::filesystem::remove (before);
        std::filesystem::remove (after);
        std::filesystem::remove (late);
    }
    // a crash 2.6 s in: the header of the last 1-second flush (2 s) is in the file, whatever the durable phase
    for (const auto phase : { 0.0, 0.3, 0.9 })
    {
        const auto crashed = nativeperf190pz::tempWav ("crashed.wav");
        nativeperf190pz::writeTake (crashed, SampleFormat::pcm24, 1.0, 5.0, phase, (std::int64_t) (2.6 * 48000.0));
        std::vector<std::vector<float>> back;
        double rate = 0.0;
        CHECK (readWav (crashed, back, &rate) && rate == 48000.0 && back.size() == 2);
        CHECK (! back.empty() && back[0].size() >= (std::size_t) (2 * 48000) && back[0].size() < (std::size_t) (3 * 48000));
        std::filesystem::remove (crashed);
    }
}

TEST (nativeperf190pz_take_stream_two_piece_copies_keep_every_sample_and_the_timeline)
{
    using namespace lynx::takes;
    // a small ring that wraps hundreds of times; pushes of every size (silence pushes too), pulls of every size
    for (const auto channels : { 1, 2 })
    {
        TakeStream s (channels, 1024);
        std::mt19937 rng ((unsigned) (7 + channels));
        std::uniform_int_distribution<int> size (1, 700), coin (0, 9);
        std::vector<float> in0 (700), in1 (700), out0 (4096), out1 (4096);
        std::vector<float> expect0, expect1, got0, got1;
        auto drain = [&] (int most) {
            float* o[] = { out0.data(), out1.data() };
            const auto n = s.pull (o, most);
            got0.insert (got0.end(), out0.begin(), out0.begin() + n);
            got1.insert (got1.end(), out1.begin(), out1.begin() + n);
            return n;
        };
        std::uint32_t v = 1;
        for (int step = 0; step < 4000; ++step)
        {
            const auto n = size (rng);
            const auto silent = coin (rng) == 0;
            for (int i = 0; i < n; ++i)
            {
                in0[(std::size_t) i] = (float) (v++ & 0xFFFFF);
                in1[(std::size_t) i] = -(float) (v & 0xFFFFF);
            }
            const float* d[] = { in0.data(), in1.data() };
            if (silent)
                s.pushSilence (n);
            else
                s.push (d, n);
            for (int i = 0; i < n; ++i)
            {
                expect0.push_back (silent ? 0.0f : in0[(std::size_t) i]);
                expect1.push_back (silent ? 0.0f : (channels == 2 ? in1[(std::size_t) i] : in0[(std::size_t) i]));
            }
            while ((int) (s.pushedFrames() - (std::int64_t) got0.size()) > 1024 - 700) // (never full: every sample kept)
                drain (1 + coin (rng) * 300);
        }
        s.finish();
        while (drain (4096) > 0) {}
        CHECK (s.lostFrames() == 0 && s.empty());
        CHECK (got0 == expect0);
        if (channels == 2)
            CHECK (got1 == expect1);
    }
    // the writer falls behind: what doesn't fit becomes silence at its place, everything else is kept, the length too
    {
        TakeStream s (1, 1024);
        std::vector<float> in (300), out (8192), got;
        std::vector<float> pushed;
        std::uint32_t v = 1;
        for (int step = 0; step < 200; ++step)
        {
            for (auto& x : in)
                x = (float) (v++);
            const float* d[] = { in.data() };
            s.push (d, (int) in.size());
            pushed.insert (pushed.end(), in.begin(), in.end());
            if (step % 9 == 0) // a slow disk: a drain every 9 blocks
            {
                float* o[] = { out.data() };
                const auto n = s.pull (o, 700);
                got.insert (got.end(), out.begin(), out.begin() + n);
            }
        }
        s.finish();
        for (;;)
        {
            float* o[] = { out.data() };
            const auto n = s.pull (o, (int) out.size());
            if (n <= 0)
                break;
            got.insert (got.end(), out.begin(), out.begin() + n);
        }
        CHECK (s.lostFrames() > 0 && got.size() == pushed.size());
        std::int64_t zeros = 0;
        auto placed = true;
        for (std::size_t i = 0; i < got.size() && i < pushed.size(); ++i)
        {
            zeros += got[i] == 0.0f ? 1 : 0;
            placed = placed && (got[i] == 0.0f || got[i] == pushed[i]);
        }
        CHECK (placed && zeros == s.lostFrames());
    }
}
