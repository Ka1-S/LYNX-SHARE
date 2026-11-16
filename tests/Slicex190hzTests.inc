// Round 190hz: Slicex's articulator editor grid, zoom and snap (#172) and the keyboard's marks, as FL's TMEnvEditor and
// Slicex draw them (docs/analise/REA_rodada190hz_slicex.md)
#include "core/Slicex.h"

namespace slicex190hz
{
inline bool near (double a, double b, double eps = 1.0e-9) { return std::abs (a - b) <= eps; }
} // namespace slicex190hz

TEST (slicex190hz_time_axis_and_view_follow_fl)
{
    using namespace lynx;
    // Slicex answers the editor's subdivision question with 10 (0x881cb0): base 0.2 / 10 in 0.5 s units, 1/4 / 10 beat
    const auto t = slicex::articulatorTimeAxis (false);
    CHECK (slicex190hz::near (t.base, 0.02) && t.step1 == 2.0 && t.step2 == 0.0);
    const auto b = slicex::articulatorTimeAxis (true, 4);
    CHECK (slicex190hz::near (b.base, 0.025) && b.step1 == 1.0 && b.step2 == 4.0);
    CHECK (slicex::articulatorTimeAxis (true, 3).step2 == 3.0);
    // the range shown (0x851120): max(last + 1, last · 1.25), 2 … 7200; an LFO one unit further
    CHECK (slicex::articulatorView (1.0, false) == 2.0);  // Slicex's default volume envelope: FL shows 2 units
    CHECK (slicex::articulatorView (0.0, true) == 2.0);   // a one-point LFO: 2 beats
    CHECK (slicex::articulatorView (0.0, false) == 2.0);
    CHECK (slicex::articulatorView (3.0, false) == 4.0);
    CHECK (slicex::articulatorView (8.0, false) == 10.0); // past 4 the quarter margin is the larger
    CHECK (slicex::articulatorView (3.0, true) == 5.0);
    CHECK (slicex::articulatorView (1.0e6, false) == 7200.0);
}

TEST (slicex190hz_ruler_step_law_matches_fl_shots)
{
    using namespace lynx;
    const auto env = slicex::articulatorTimeAxis (false);
    // FL's graph is 365 px over 2 units (182.5 px a unit): lines every 14.6 px = 0.08 units (shot m0), no thick ones
    // until 2.0 (25 lines)
    auto g = slicex::rulerStep (env.base, env.step1, env.step2, 182.5, 12.0);
    CHECK (slicex190hz::near (g.step, 0.08));
    CHECK (g.perStep1 == 25 && g.perStep2 == 0);
    CHECK (slicex::gridLineClass (25, g) == 1 && slicex::gridLineClass (24, g) == 0 && slicex::gridLineClass (50, g) == 1);
    // tempo-based (FL's LFO shot): 0.1 beat = 18.25 px; every beat (10 lines) thicker, every bar (40) thickest
    const auto lfo = slicex::articulatorTimeAxis (true, 4);
    g = slicex::rulerStep (lfo.base, lfo.step1, lfo.step2, 182.5, 12.0);
    CHECK (slicex190hz::near (g.step, 0.1));
    CHECK (g.perStep1 == 10 && g.perStep2 == 40);
    CHECK (slicex::gridLineClass (10, g) == 1 && slicex::gridLineClass (40, g) == 2 && slicex::gridLineClass (7, g) == 0);
    // zoomed in (0.5 units shown): the base step itself is ≥ 12 px
    g = slicex::rulerStep (env.base, env.step1, env.step2, 365.0 / 0.5, 12.0);
    CHECK (slicex190hz::near (g.step, 0.02));
    // zoomed out (100 units): 100 base steps (7.3 px) are too close, so the count doubles (a multiple of step1's count)
    g = slicex::rulerStep (env.base, env.step1, env.step2, 365.0 / 100.0, 12.0);
    CHECK (slicex190hz::near (g.step, 4.0));
    CHECK (g.perStep1 == 1);
    // a count that doesn't divide step1's (3 of 100) is skipped: 0.08 rather than 0.06 at 220 px a unit
    g = slicex::rulerStep (env.base, env.step1, env.step2, 220.0, 12.0);
    CHECK (slicex190hz::near (g.step, 0.08));
    // the level axis keeps FL's fixed 1/16 with a thick line every 1/4
    const slicex::GridStep y { slicex::levelGridStep, slicex::levelGridPerQuarter, 0 };
    CHECK (slicex::gridLineClass (4, y) == 1 && slicex::gridLineClass (5, y) == 0);
    // no base: FL's fallback step
    CHECK (slicex::rulerStep (0.0, 1.0, 0.0, 100.0, 12.0).step == 65536.0);
}

TEST (slicex190hz_snap_rounds_and_alt_inverts)
{
    using namespace lynx;
    CHECK (slicex190hz::near (slicex::snapToGrid (0.13, 0.08), 0.16));
    CHECK (slicex190hz::near (slicex::snapToGrid (0.03, 0.08), 0.0));
    CHECK (slicex::snapToGrid (0.375, 0.25) == 0.5);  // 1.5 steps: halves go to the even count (Delphi's Round)
    CHECK (slicex::snapToGrid (0.125, 0.25) == 0.0);  // 0.5 steps → 0
    CHECK (slicex::snapToGrid (0.625, 0.25) == 0.5);  // 2.5 steps → 2
    CHECK (slicex190hz::near (slicex::snapToGrid (0.53, slicex::levelGridStep), 0.5));
    CHECK (slicex190hz::near (slicex::snapToGrid (1.27, 0.5, 0.1), 1.1));
    CHECK (slicex::snapToGrid (0.37, 0.0) == 0.37);                    // no step: unchanged
    CHECK (slicex::snapActive (true, false) && ! slicex::snapActive (true, true));
    CHECK (slicex::snapActive (false, true) && ! slicex::snapActive (false, false));
}

TEST (slicex190hz_wheel_zoom_and_keyboard_marks)
{
    using namespace lynx;
    // a wheel notch zooms by FL's measured menu step (2^-0.225), Ctrl by a quarter of that exponent
    CHECK (slicex190hz::near (slicex::wheelZoomFactor (1.0, false), slicex::zoomStep, 1.0e-9));
    CHECK (slicex190hz::near (slicex::wheelZoomFactor (-1.0, false), 1.0 / slicex::zoomStep, 1.0e-9));
    CHECK (slicex190hz::near (slicex::wheelZoomFactor (1.0, true), std::pow (2.0, -0.05625), 1.0e-12));
    // the keyboard marks the current deck's keys in the full display mode, deck A's in the others
    CHECK (slicex::keyboardMarkDeck (0, 1) == 1);
    CHECK (slicex::keyboardMarkDeck (0, 0) == 0);
    for (int mode = 1; mode < 4; ++mode)
        CHECK (slicex::keyboardMarkDeck (mode, 1) == 0);
}
