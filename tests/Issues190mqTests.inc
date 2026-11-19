// Round 190mq (issues #211, #212, #213): where Ctrl+V puts the clipboard per layout (core/PastePlace.h).

#include "core/PastePlace.h"

TEST (issues190mq_paste_layout_rules)
{
    using namespace lynx::pasteplace;
    CHECK (ruleFor (lynx::layoutstyle::Style::pattern) == Rule::flView);
    CHECK (ruleFor (lynx::layoutstyle::Style::clipLaunch) == Rule::liveInsert);
    CHECK (ruleFor (lynx::layoutstyle::Style::simple) == Rule::playhead);

    const UniformBars bars { 384 };
    Where w;
    w.first = 2 * 384 + 96;
    w.viewFrom = 20 * 384;
    w.viewTo = 30 * 384;
    // Live: the time selection's start, else the insert marker (the song position), else in place
    w.position = 5 * 384 + 10;
    CHECK (start (Rule::liveInsert, w, bars) == 5 * 384 + 10);
    w.selection = 7 * 384;
    CHECK (start (Rule::liveInsert, w, bars) == 7 * 384);
    // BandLab: the playhead, whatever the selection
    CHECK (start (Rule::playhead, w, bars) == 5 * 384 + 10);
    w.position = w.selection = -1;
    CHECK (start (Rule::liveInsert, w, bars) == w.first);
    CHECK (start (Rule::playhead, w, bars) == w.first);
}

// FL 2026's Ctrl+V (Pattern layout), the cases captured in FL (s/notes.md of round 190mq): 4/4 bars of 3840 ticks
TEST (issues190mq_fl_paste_rule_matches_the_capture)
{
    using namespace lynx::pasteplace;
    constexpr std::int64_t bar = 3840, beat = 960, step = 240;
    const UniformBars bars { bar };
    Where w;
    w.first = 2 * beat + step; // the captured D5: bar 1 beat 3.25
    w.grid = step;
    // f28 / f04: view from bar 1, no anchor: onto itself, so one grid step later
    w.viewFrom = 0;
    w.viewTo = 3 * bar;
    CHECK (start (Rule::flView, w, bars) == w.first + step);
    // f20: left edge bar 3 + 0.63 beats -> bar 3 + 2.25 beats
    w.viewFrom = 2 * bar + 630 * beat / 1000;
    w.viewTo = w.viewFrom + 2 * bar;
    CHECK (start (Rule::flView, w, bars) == 2 * bar + 2 * beat + step);
    // f23: left edge bar 3 + 2.03 beats -> the nearest bar line, bar 4 (not bar 3 + 2.25, though that is in view)
    w.viewFrom = 2 * bar + 2030 * beat / 1000;
    w.viewTo = w.viewFrom + 2 * bar;
    CHECK (start (Rule::flView, w, bars) == 3 * bar + 2 * beat + step);
    // f18: left edge bar 2 + 2.93 beats -> bar 3
    w.viewFrom = bar + 2930 * beat / 1000;
    CHECK (start (Rule::flView, w, bars) == 2 * bar + 2 * beat + step);
    // f13 / f16: a ruler click in view is the anchor (the first note lands on it, not at its bar offset)
    w.viewFrom = 4 * bar;
    w.viewTo = 7 * bar;
    w.rulerMark = 5 * bar + 2 * beat;
    CHECK (start (Rule::flView, w, bars) == 5 * bar + 2 * beat);
    // f18: the anchor out of view does not count
    w.viewTo = 5 * bar;
    CHECK (start (Rule::flView, w, bars) == 4 * bar + 2 * beat + step);
    // f25 / p10: a time selection in view wins; p17: out of view, the view rule
    w.viewTo = 7 * bar;
    w.selection = 6 * bar + beat;
    CHECK (start (Rule::flView, w, bars) == 6 * bar + beat);
    w.viewFrom = 7 * bar;
    w.viewTo = 9 * bar;
    CHECK (start (Rule::flView, w, bars) == 7 * bar + 2 * beat + step);
    // an anchor at the very start is none (FL's 0..0 = no selection; f04: Ctrl+A's selection from bar 1 is no anchor)
    Where atStart;
    atStart.first = 2 * beat + step;
    atStart.viewTo = 3 * bar;
    atStart.selection = 0;
    atStart.rulerMark = 0;
    atStart.grid = step;
    CHECK (start (Rule::flView, atStart, bars) == 2 * beat + 2 * step);
    // the anchor exactly on the source: moved a grid step too
    Where same;
    same.first = 2 * beat;
    same.viewTo = bar;
    same.rulerMark = 2 * beat;
    same.grid = step;
    CHECK (start (Rule::flView, same, bars) == 2 * beat + step);
}

TEST (issues190mq_fl_paste_bar_lines_follow_time_signatures)
{
    using namespace lynx::pasteplace;
    // bars of 3 beats then of 4 (a signature change at tick 2880): the nearest line and the offset in the source's bar
    const auto barAt = [] (std::int64_t t) { return t < 2880 ? Bar { t / 2880 * 2880, 2880 } : Bar { 2880 + (t - 2880) / 3840 * 3840, 3840 }; };
    CHECK (nearestBarLine (2880 + 100, barAt) == 2880);
    CHECK (nearestBarLine (2880 + 2000, barAt) == 2880 + 3840);
    CHECK (nearestBarLine (1000, barAt) == 0);
    CHECK (nearestBarLine (1500, barAt) == 2880);
    Where w;
    w.first = 960; // beat 2 of the 3/4 bar
    w.viewFrom = 2880 + 3840 * 3;
    w.viewTo = w.viewFrom + 3840 * 4;
    CHECK (start (Rule::flView, w, barAt) == 2880 + 3840 * 3 + 960);
}
