// Round 190ax, bug 2: the Playlist's "Line (follows zoom)" snap step against FL's Line / Cell step (FL 2026 FLEngine
// FUN_00e59e90, docs/analise/REA_rodada190ax.md). LYNX used noteedit::screenStep (the finest of 1/4 step, 1/2 step,
// step, beat, bars that is 16 px wide): a beat where FL steps half a beat (round 190ag's live check: 87 px per bar,
// Shift+Right moves 48 of FL's 96 PPQ ticks with Line and with Cell), and quarter steps where FL stays on steps.

#include "core/NoteEdit.h"

namespace s190ax
{
// the step at `pixelsPerBar` px per bar of `numerator`/`denominator`
std::int64_t at (double pixelsPerBar, int numerator = 4, int denominator = 4)
{
    const auto bar = (double) numerator * (16 / denominator) * lynx::ticksPerStep;
    return lynx::noteedit::flLineStep (pixelsPerBar / bar, numerator, denominator);
}
} // namespace s190ax

TEST (s190ax_fl_line_and_cell_step_by_zoom_in_4_4)
{
    using s190ax::at;
    const std::int64_t step = lynx::ticksPerStep, beat = lynx::ppq, bar = 4 * beat;
    // zoom (px per 4/4 bar) -> step; FL's Line and Cell modes take the same step (they round a position differently).
    // The limits are FL's 10 px: a step is 10 px wide at 160 px per bar, two steps at 80, a beat at 40, two beats at 20,
    // a bar at 10, two bars at 5.
    const struct { double pixelsPerBar; std::int64_t ticks; } table[] = {
        { 800.0, step },      // LYNX's closest zoom: FL never goes under a step (screenStep gave 1/2 step from 512 px)
        { 512.0, step },
        { 256.0, step },
        { 161.0, step },
        { 159.0, 2 * step },  // two steps (half a beat)
        { 96.0, 2 * step },   // LYNX's default zoom (screenStep: a beat)
        { 87.0, 2 * step },   // round 190ag, live in FL 2026: Line and Cell both moved half a beat per Shift+Right
        { 81.0, 2 * step },
        { 79.0, beat },
        { 64.0, beat },
        { 41.0, beat },
        { 39.0, 2 * beat },   // two beats (screenStep: a bar)
        { 21.0, 2 * beat },
        { 19.0, bar },
        { 16.0, bar },
        { 11.0, bar },
        { 9.0, 2 * bar },
        { 8.25, 2 * bar },    // (round 169d's zoom: two bars there too)
        { 6.0, 2 * bar },
        { 4.5, 4 * bar },
        { 4.0, 4 * bar },     // LYNX's furthest zoom
        { 2.0, 8 * bar },
        { 1.0, 16 * bar },
        { 0.5, 32 * bar },
        { 0.01, 32 * bar },   // FL stops doubling at 32 bars
    };
    for (const auto& row : table)
    {
        const auto got = at (row.pixelsPerBar);
        if (got != row.ticks)
            std::printf ("  %.2f px per bar: %lld ticks, expected %lld\n", row.pixelsPerBar, (long long) got, (long long) row.ticks);
        CHECK (got == row.ticks);
        CHECK ((double) got * row.pixelsPerBar / (double) bar >= 10.0 || got == step || got == 32 * bar); // never under 10 px, but for the two ends
    }
    // the step never shrinks as the view zooms out
    auto last = at (800.0);
    for (double p = 800.0; p >= 1.0; p *= 0.97)
    {
        const auto now = at (p);
        CHECK (now >= last && now % step == 0);
        last = now;
    }
    // no zoom at all (a window not laid out yet): a bar, and no endless loop
    CHECK (lynx::noteedit::flLineStep (0.0, 4) == bar && lynx::noteedit::flLineStep (-1.0, 4) == bar);
}

TEST (s190ax_fl_line_step_in_other_time_signatures)
{
    using s190ax::at;
    const std::int64_t step = lynx::ticksPerStep, beat = lynx::ppq;
    // 3/4: an odd numerator has no two-beat step (a bar of three beats does not split in two): beat, then the bar
    CHECK (at (3 * 12.0, 3) == beat);       // a beat is 12 px
    CHECK (at (3 * 9.0, 3) == 3 * beat);    // a beat is 9 px, two beats would be 18: the bar (27 px)
    CHECK (at (4 * 9.0, 4) == 2 * beat);    // (4/4 at the same beat width: two beats)
    CHECK (at (3 * 22.0, 3) == 2 * step);   // two steps still, 11 px
    CHECK (at (3 * 3.0, 3) == 2 * 3 * beat); // a bar is 9 px: two bars
    // 6/8: a beat is two steps, so "two steps" is the beat; then two beats (even numerator)
    CHECK (at (6 * 2 * 6.0, 6, 8) == 2 * step);   // a step is 6 px
    CHECK (at (6 * 2 * 3.0, 6, 8) == 4 * step);   // a step is 3 px, a beat 6, two beats 12
    CHECK (at (6 * 2 * 10.5, 6, 8) == step);
    // x/16 with an odd numerator: no two-step and no two-beat step, the bar comes straight after the step
    CHECK (at (7 * 10.5, 7, 16) == step);
    CHECK (at (7 * 6.0, 7, 16) == 7 * step);      // a step is 6 px: the bar (42 px)
    CHECK (at (8 * 6.0, 8, 16) == 2 * step);      // (an even numerator: two steps)
    CHECK (at (7 * 1.0, 7, 16) == 2 * 7 * step);  // a bar is 7 px: two bars
    // odd values are held: numerator below 1 counts as 1, the denominator is kept inside 1..16
    CHECK (lynx::noteedit::flLineStep (1.0, 0) == step && lynx::noteedit::flLineStep (1.0, 4, 64) == step);
}

TEST (s190ax_the_line_step_is_no_longer_screen_steps)
{
    // what the Playlist used before, at the zooms where the two differ (the Piano roll keeps screenStep)
    const std::int64_t step = lynx::ticksPerStep, beat = lynx::ppq, bar = 4 * beat;
    CHECK (lynx::noteedit::screenStep (87.0 / (double) bar, bar) == beat && s190ax::at (87.0) == 2 * step);
    CHECK (lynx::noteedit::screenStep (600.0 / (double) bar, bar) == step / 2 && s190ax::at (600.0) == step);
    CHECK (lynx::noteedit::screenStep (30.0 / (double) bar, bar) == bar && s190ax::at (30.0) == 2 * beat);
}
