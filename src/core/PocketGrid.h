#pragma once

// FL drives a hosted FL Studio Mobile rack in chunks of one song tick (1/96 beat) cut on the song's grid, each with its
// tick's exact position (REA_rodada182c.md "F5 effects, round 189h"). FLM's modules read their LFOs and song phases once
// per call, so the Pocket engines cut their host blocks on the same grid. A chunk that a host block boundary cuts goes on
// with what it started with ("continuing"); a chunk that starts on the grid gets its tick's exact position.

#include <cmath>

namespace lynx::pocket
{
struct TickGrid
{
    double freeSamples = 0.0, expected = 0.0;
    bool wasPlaying = false, started = false;

    void reset() noexcept { freeSamples = 0.0, wasPlaying = false, started = false; }

    // one block: songBeats < 0 = stopped (the grid then runs on its own). f (offset, frames, beats or −1, continuing)
    // is called per chunk; pass = false gives the block's chunk list again (run once per effect stage)
    struct Block
    {
        double start = 0.0, tickLen = 0.0, firstBeats = -1.0;
        bool onGrid = true, contiguous = false, playing = false;
        int n = 0;
        template <typename F>
        void forEach (F&& f) const
        {
            if (tickLen < 1.0)
            {
                f (0, n, playing ? firstBeats : -1.0, false);
                return;
            }
            auto beats = firstBeats;
            auto continuing = ! onGrid && contiguous;
            for (int done = 0; done < n;)
            {
                auto k = (long long) std::floor ((start + done) / tickLen);
                auto next = (int) std::floor ((double) k * tickLen + 0.5 - start);
                while (next <= done)
                    next = (int) std::floor ((double) ++k * tickLen + 0.5 - start);
                const auto m = (next < n ? next : n) - done;
                f (done, m, beats, continuing);
                done += m;
                continuing = false;
                if (playing)
                    beats = (double) k / 96.0;
            }
        }
    };

    Block begin (double songBeats, double bpm, double fs, int n) noexcept
    {
        Block b;
        b.n = n;
        b.playing = songBeats >= 0.0;
        b.tickLen = bpm >= 1.0 ? fs * 60.0 / (bpm * 96.0) : 0.0;
        if (b.tickLen < 1.0)
        {
            b.firstBeats = songBeats;
            started = true;
            return b;
        }
        auto start = b.playing ? songBeats * 96.0 * b.tickLen : freeSamples; // the block's first sample on the grid
        if (std::abs (start - std::round (start)) < 1e-6)
            start = std::round (start); // a whole sample (the grid's half-sample ties must not flip on a rounding error)
        b.start = start;
        const auto kHere = (long long) std::floor (start / b.tickLen + 0.5);
        b.onGrid = (long long) std::floor ((double) kHere * b.tickLen + 0.5 - start) == 0;
        b.contiguous = b.playing ? wasPlaying && std::abs (start - expected) < 0.25 : ! wasPlaying && started;
        b.firstBeats = ! b.playing ? -1.0 : b.onGrid ? (double) kHere / 96.0 : songBeats;
        freeSamples = b.playing ? 0.0 : std::fmod (freeSamples + n, b.tickLen);
        expected = start + n;
        wasPlaying = b.playing;
        started = true;
        return b;
    }
};
} // namespace lynx::pocket
