// Round 190jd, fork C (live check 190iw Q6 + Q7): FL's Mixer peak hold, the Extra large layout's effects list, and the
// scrolling Fruity Limiter / Maximus displays. The paint is checked by the --snapshot / --parityshot pairs; these are the
// parts without JUCE.

#include "core/DynamicsHistory.h"
#include "core/MeterBallistics.h"
#include "core/StripInserts.h"
#include <cmath>

// FL's hold: the tick stays 2.6 s, then falls with acceleration (~3.2 dB in the first 1.3 s, ~4.5-5 dB/s by then)
TEST (livefix190jd_fl_peak_hold_law)
{
    using namespace lynx::meters;
    CHECK (flPeakFallDb (0.0) == 0.0 && flPeakFallDb (2599.0) == 0.0);
    CHECK (flPeakFallDb (2600.0 + 1300.0) > 2.9 && flPeakFallDb (2600.0 + 1300.0) < 3.5);
    // accelerating: the second 0.65 s falls three times the first
    const auto a = flPeakFallDb (2600.0 + 650.0), b = flPeakFallDb (2600.0 + 1300.0) - a;
    CHECK (b > 2.5 * a);
    const auto speed = (flPeakFallDb (2600.0 + 1350.0) - flPeakFallDb (2600.0 + 1250.0)) / 0.1; // dB/s at 1.3 s
    CHECK (speed > 4.0 && speed < 5.5);

    // the meter: a 0 dB hit, then silence at 60 Hz frames; the tick holds, then falls, and nothing resets it on Stop
    PeakHold m;
    m.advanceFl (1.0f, 16.7);
    CHECK (m.peak == 1.0f);
    double t = 16.7;
    while (t < 2500.0)
    {
        m.advanceFl (0.0f, 16.7);
        t += 16.7;
    }
    CHECK (m.peak == 1.0f && m.level < 0.01f); // the bar has fallen, the tick is still held
    while (t < 2600.0 + 1300.0)
    {
        m.advanceFl (0.0f, 16.7);
        t += 16.7;
    }
    const auto db = 20.0 * std::log10 ((double) m.peak);
    CHECK (db < -2.8 && db > -3.7);
    // a peak above the falling tick sets it again and holds it anew
    m.advanceFl (0.9f, 16.7);
    CHECK (m.peak == 0.9f && m.holdMsLeft > 2500.0);
    // a lower peak meanwhile does not move it
    m.advanceFl (0.5f, 16.7);
    CHECK (m.peak == 0.9f);
    // it reaches the floor eventually
    for (int i = 0; i < 1000; ++i)
        m.advanceFl (0.0f, 16.7);
    CHECK (m.peak == 0.0f);
    // the -3 dB colour threshold repaints the tick's rows
    PeakHold x, y;
    x.peak = 0.69f;
    y.peak = 0.72f;
    CHECK (! dirtyRows (x, y, 400, [] (float) { return 0.5f; }).empty()); // (same row, the colour changes)
}

// FL's Extra large list: only the used slots, 23 px rows, no dots, no "add" row; shown in Extra large unless the LYNX view
TEST (livefix190jd_extra_large_effects_list)
{
    using namespace lynx;
    using namespace lynx::stripinserts;
    CHECK (flListShown (mixerlayout::Layout::extraLarge, false));
    CHECK (! flListShown (mixerlayout::Layout::extraLarge, true)); // the LYNX view (under the band) replaces it
    CHECK (! flListShown (mixerlayout::Layout::wide, false) && ! flListShown (mixerlayout::Layout::compact, false));
    CHECK (flExtraLarge.rowHeight == 23 && ! flExtraLarge.addRow && ! flExtraLarge.dots);
    CHECK (itemCount (flExtraLarge, 0) == 0 && itemCount (flExtraLarge, 3) == 3); // Master with one effect: one row
    CHECK (itemCount (lynxView, 3) == 4);
    CHECK (rowsThatFit (flExtraLarge, 6 * 23) == 6);
    CHECK (itemAt (flExtraLarge, 2, 0, 6, 0) == 0 && itemAt (flExtraLarge, 2, 0, 6, 23) == 1);
    CHECK (itemAt (flExtraLarge, 2, 0, 6, 2 * 23) == -1); // (no "add" row)
    CHECK (itemAt (lynxView, 2, 0, 6, 2 * trackinserts::rowHeight) == trackinserts::addRow);
    // more effects than rows: it scrolls, the last rows fill the column
    CHECK (clampFirst (flExtraLarge, 9, 8, 6) == 2);
    CHECK (dropPosition (flExtraLarge, 4, 0, 6, 23 + 12) == 2);
    // the LYNX view keeps 190io's geometry
    CHECK (height (lynxView, 4) == height (4));
}

// FL's scrolling Limiter display / Maximus Monitor: ~87 columns a second of playback, the level axis, the gain line
TEST (livefix190jd_dynamics_history)
{
    using namespace lynx::dynhistory;
    CHECK (std::abs (axis (1.0f) - 0.5f) < 1.0e-5f); // 0 dBFS at half height
    CHECK (std::abs (axis (4.0f) - 1.0f) < 1.0e-5f); // +12 dB at the top (the Limiter's ceiling range)
    CHECK (axis (0.0f) == 0.0f && axis (-1.0f) == 0.0f);
    // the inverse of FL's law3 control law: raw x/1000 -> (3^x - 1) / 2 sits at x / 2
    for (const auto x : { 0.25, 0.8, 1.5 })
        CHECK (std::abs (axis ((float) ((std::pow (3.0, x) - 1.0) / 2.0)) - (float) (x / 2.0)) < 1.0e-4f);

    History h (64);
    const auto n = h.advance (1000.0); // one second: 87 columns
    CHECK (n == 86 || n == 87);
    CHECK (h.size() == 64 && h.pushed() == n); // (a ring of 64)
    CHECK (! h.at (0).started);               // silence before the first sound: no gain line
    h.clear();
    h.feed (0.5f, 0.25f);
    CHECK (h.advance (columnMs * 0.5) == 0); // not a whole column yet
    CHECK (h.advance (columnMs * 0.5 + 0.01) == 1);
    CHECK (h.at (0).in == 0.5f && h.at (0).out == 0.25f && std::abs (h.at (0).gain - 0.5f) < 1.0e-6f && h.at (0).started);
    CHECK (h.advance (columnMs) == 1); // a gap: no input, the gain line runs on at its last value
    CHECK (h.at (0).in == 0.0f && std::abs (h.at (0).gain - 0.5f) < 1.0e-6f && h.at (0).started);
    CHECK (h.at (1).in == 0.5f);
    h.clear();
    CHECK (h.size() == 0);
}
