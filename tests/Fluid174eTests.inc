// Round 174e: display-rate meters. The mixer meters follow the display's refresh (144 Hz and more), so their
// ballistics go by time (PeakHold::advance) and only the rows that moved are repainted (meters::dirtyRows).

#include "core/MeterBallistics.h"

TEST (fluid174e_meter_advance_matches_30hz_ticks)
{
    // a burst then silence: 1 s of 144 Hz frames falls as far as 1 s of 30 Hz ticks (the same dB per second)
    lynx::meters::PeakHold slow, fast;
    slow.tick (0.9f);
    fast.advance (0.9f, 1000.0 / 30.0);
    for (int i = 0; i < 30; ++i)
        slow.tick (0.0f);
    for (int i = 0; i < 144; ++i)
        fast.advance (0.0f, 1000.0 / 144.0);
    const auto db = [] (float v) { return 20.0f * std::log10 (std::max (v, 1.0e-6f)); };
    CHECK (std::abs (db (slow.level) - db (fast.level)) < 0.6f);
    // the hold line stays for 1.5 s whatever the frame rate, then falls
    CHECK (fast.peak == 0.9f && slow.peak == 0.9f);
    for (int i = 0; i < 72; ++i) // 0.5 s more: 1.5 s since the burst
        fast.advance (0.0f, 1000.0 / 144.0);
    CHECK (fast.peak == 0.9f);
    for (int i = 0; i < 10; ++i)
        fast.advance (0.0f, 1000.0 / 144.0);
    CHECK (fast.peak < 0.9f && fast.peak >= fast.level);

    // a long gap (the window was hidden) does not fall more than 30 ticks' worth; silence still reaches 0
    lynx::meters::PeakHold g;
    g.advance (0.5f, 33.0);
    g.advance (0.0f, 10000.0);
    CHECK (g.level > 0.0f);
    for (int i = 0; i < 2000; ++i)
        g.advance (0.0f, 7.0);
    CHECK (g.level == 0.0f && g.peak == 0.0f);

    // the clip latch and its reset work as with tick()
    g.advance (1.3f, 7.0);
    g.advance (0.0f, 7.0);
    CHECK (g.clipped);
    g.resetClip();
    CHECK (! g.clipped && g.holdMsLeft == 0.0);
}

TEST (fluid174e_meter_dirty_rows)
{
    const auto lin = [] (float v) { return std::clamp (v, 0.0f, 1.0f); };
    lynx::meters::PeakHold a, b;
    // nothing moved: nothing to redraw
    CHECK (lynx::meters::dirtyRows (a, b, 100, lin).empty());
    // the bar rose from 20 % to 50 % of 100 px: rows 50..80 and the new hold line around row 50
    a.level = 0.2f;
    b.level = 0.5f;
    b.peak = 0.5f;
    auto r = lynx::meters::dirtyRows (a, b, 100, lin);
    CHECK (r.top <= 49 && r.bottom >= 80 && r.bottom <= 81);
    // a sub-pixel move redraws nothing
    a = b;
    b.level = 0.5001f;
    CHECK (lynx::meters::dirtyRows (a, b, 100, lin).empty());
    // the clip latch: the top 4 px
    a = b;
    b.clipped = true;
    r = lynx::meters::dirtyRows (a, b, 100, lin);
    CHECK (r.top == 0 && r.bottom >= 4);
    // the hold line falling by one pixel: its old and new rows only
    a = {};
    b = {};
    a.peak = 0.70f;
    b.peak = 0.69f;
    r = lynx::meters::dirtyRows (a, b, 100, lin);
    CHECK (! r.empty() && r.top >= 28 && r.bottom <= 33);
}

// Round 174e2: Settings › General › Animation rate. A capped animation runs on evenly spread display frames, the same
// frames for every animation with that cap.
#include "core/FramePacing.h"

TEST (fluid174e2_frame_pacing)
{
    // one second of vertical blanks at `displayHz` (stamps with a little jitter, from an arbitrary clock), cap `capHz`:
    // the frames run and the longest gap between two runs, in display frames
    auto count = [] (double displayHz, double capHz, int frames, double jitterMs = 0.3) {
        lynx::framepace::Pacer pacer;
        const auto period = 1000.0 / displayHz;
        int n = 0, longestGap = 0, gap = 0;
        for (int i = 0; i < frames + 200; ++i)
        {
            const auto stamp = 37'000'000.0 + i * period + ((i * 7919) % 11 - 5) * jitterMs / 5.0;
            const auto ran = pacer.runs (stamp, period, capHz);
            CHECK (pacer.runs (stamp, period, capHz) == ran); // (another ticker on the same blank: the same answer)
            if (i < 200)
                continue; // (settled)
            ++gap;
            if (ran)
            {
                ++n;
                longestGap = std::max (longestGap, gap);
                gap = 0;
            }
        }
        return std::make_pair (n, longestGap);
    };
    CHECK (count (144.0, 144.0, 144).first == 144); // the display's rate
    CHECK (count (144.0, 170.0, 144).first == 144); // a cap above the refresh: every frame
    CHECK (count (144.0, 72.0, 144) == std::make_pair (72, 2));
    const auto at60 = count (144.0, 60.0, 144);
    CHECK (at60.first >= 59 && at60.first <= 61 && at60.second <= 3); // 60 Hz: every 2nd or 3rd frame
    const auto at30 = count (144.0, 30.0, 144);
    CHECK (at30.first >= 29 && at30.first <= 31 && at30.second <= 5);
    // a 60 Hz display with a 30 Hz cap: every other frame; with the toolbar's 72 Hz cap: every frame
    CHECK (count (60.0, 30.0, 60) == std::make_pair (30, 2));
    CHECK (count (60.0, 72.0, 60).first == 60);

    // blanks of a second monitor (60 Hz, another phase) mixed in: still at most the cap, evenly
    {
        lynx::framepace::Pacer pacer;
        int n = 0;
        for (int i = 0; i < 144 * 3; ++i)
        {
            const auto t = 1000.0 * i / 144.0;
            if (pacer.runs (t, 1000.0 / 144.0, 60.0))
                ++n;
            if (i % 12 == 5 && pacer.runs (t + 3.1, 1000.0 / 144.0, 60.0)) // (another monitor's blank between two)
                ++n;
        }
        CHECK (n >= 3 * 60 - 3 && n <= 3 * 60 + 3);
    }
    // a stall of half a second: no burst of catch-up frames after it
    {
        lynx::framepace::Pacer pacer;
        const auto period = 1000.0 / 144.0;
        for (int i = 0; i < 100; ++i)
            pacer.runs (i * period, period, 60.0);
        int burst = 0;
        for (int i = 172; i < 175; ++i)
            burst += pacer.runs (i * period, period, 60.0) ? 1 : 0;
        CHECK (burst <= 2);
    }
}
