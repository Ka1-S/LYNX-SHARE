// Round 167q (loopwrap): the real-time transport's loop region (AudioEngine::audioDeviceIOCallbackWithContext) split
// each block where the pass ends with ceil((loopEnd − position) / tps). The position is a double moved by n · tps, so
// the remaining length of a 192000-sample pass came out as 192000.00000001 and was rounded up: every wrap played one
// sample more (measured with LYNX.exe --cliplooptest: the passes drifted 0, 1, 2… samples). The transport now uses
// transportSamplesUntil / transportReached (core/MarkerPlan.h), with a tick epsilon on both sides.

namespace loopwrap167q
{
// The callback's loop-region arithmetic, playback started at the song's start (before the region, as in
// --cliplooptest): the number of samples each of `passes` passes lasted (between two wraps).
inline std::vector<long> passLengths (double bpm, double rate, int block, double loopStart, double loopEnd, int passes)
{
    const auto tps = lynx::ticksPerSample (bpm, rate);
    double position = 0.0; // playback starts at the song's start (the position's rounding error grows on the way)
    long played = 0, passStart = -1;
    std::vector<long> out;
    while ((int) out.size() < passes)
    {
        for (int done = 0; done < block;)
        {
            auto n = block - done;
            if (lynx::transportReached (position, loopEnd))
                position = loopStart;
            n = lynx::transportSamplesUntil (position, loopEnd, tps, n);
            position += n * tps;
            played += n;
            done += n;
            if (lynx::transportReached (position, loopEnd))
            {
                position = loopStart;
                if (passStart >= 0)
                    out.push_back (played - passStart);
                passStart = played;
            }
        }
    }
    return out;
}
} // namespace loopwrap167q

TEST (loopwrap167q_loop_region_wraps_do_not_drift)
{
    const auto ppq = (double) lynx::ppq;
    struct Case
    {
        double bpm, rate;
        int block;
        double start, end;
    };
    for (const auto& c : { Case { 120.0, 48000.0, 512, 4 * ppq, 12 * ppq }, Case { 120.0, 48000.0, 480, 4 * ppq, 8 * ppq },
                           Case { 120.0, 44100.0, 441, 0.0, 4 * ppq }, Case { 133.7, 48000.0, 512, 4 * ppq, 12 * ppq },
                           Case { 97.0, 44100.0, 256, 3 * ppq, 7 * ppq }, Case { 174.0, 96000.0, 1024, 0.0, 16 * ppq } })
    {
        const auto tps = lynx::ticksPerSample (c.bpm, c.rate);
        const auto exact = (c.end - c.start) / tps; // samples per pass
        auto lengths = loopwrap167q::passLengths (c.bpm, c.rate, c.block, c.start, c.end, 100);
        lengths.resize (100);
        // every pass the same whole number of samples: the pass rounded up once (a wrap restarts on the region's exact
        // start tick, so a fraction of a sample is never carried: nothing accumulates); a whole-sample pass exactly
        const auto expected = (long) std::ceil (exact - 1.0e-6);
        long total = 0;
        bool each = true;
        for (const auto l : lengths)
        {
            total += l;
            each = each && l == expected;
        }
        const auto drift = (double) (total - 100 * expected);
        if (std::getenv ("LYNX_PRINT_LOOPWRAP") != nullptr)
            std::printf ("loopwrap %.1f BPM %.0f Hz block %d: pass %.4f samples, 100 passes = %ld samples (drift %.0f vs 100 x %ld)\n",
                         c.bpm, c.rate, c.block, exact, total, drift, expected);
        CHECK (lengths.size() == 100 && each && drift == 0.0);
    }
}

// The helpers: a point within the epsilon counts as reached; the samples until a point are never one too many.
TEST (loopwrap167q_transport_helpers)
{
    const auto tps = lynx::ticksPerSample (120.0, 48000.0);
    CHECK (lynx::transportReached (1000.0 - 1.0e-9, 1000.0));
    CHECK (! lynx::transportReached (1000.0 - tps, 1000.0));
    CHECK (lynx::transportSamplesUntil (1000.0 - 64 * tps, 1000.0, tps, 512) == 64);
    CHECK (lynx::transportSamplesUntil (1000.0 - 64 * tps + 1.0e-9, 1000.0, tps, 512) == 64);
    CHECK (lynx::transportSamplesUntil (1000.0 - 64 * tps - 1.0e-9, 1000.0, tps, 512) == 64);
    CHECK (lynx::transportSamplesUntil (1000.0 - 63.5 * tps, 1000.0, tps, 512) == 64); // a fraction: up to the point
    CHECK (lynx::transportSamplesUntil (0.0, 1000.0, tps, 512) == 512);
    CHECK (lynx::transportSamplesUntil (1000.0, 1000.0, tps, 512) == 1);
}
