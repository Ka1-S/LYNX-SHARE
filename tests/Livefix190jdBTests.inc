// Round 190jd item 4: the Clip-launch layout's MPE tab as Live 12.4's (livecheck190iw ANSWERS.md Q3): lane set and
// order of the "Show Lane:" menu, the lanes' layout, the click-on-the-line / point hit tests, the pitch bend law.
// The paint and the gestures are shown by --mpeshot190hy (clip-mpe*, -190jd files).

#include "core/MpeTab.h"
#include "core/NoteCurves.h"

TEST (livefix190jd_mpe_tab_lanes)
{
    using namespace lynx::mpetab;
    // Live's default: Slide and Pressure checked, Velocity and Release Velocity not
    CHECK (shows (defaultLanes, Lane::slide) && shows (defaultLanes, Lane::pressure));
    CHECK (! shows (defaultLanes, Lane::velocity) && ! shows (defaultLanes, Lane::releaseVelocity));
    CHECK (countOf (defaultLanes) == 2);
    // the menu's order is the stacking order; several lanes at once
    auto set = toggled (defaultLanes, Lane::releaseVelocity);
    set = toggled (set, Lane::velocity);
    const auto order = shownLanes (set);
    CHECK (order.size() == 4);
    CHECK (order[0] == Lane::slide && order[1] == Lane::pressure && order[2] == Lane::velocity && order[3] == Lane::releaseVelocity);
    // unchecking works; the last lane shown stays
    set = toggled (toggled (toggled (set, Lane::slide), Lane::pressure), Lane::velocity);
    CHECK (set == bit (Lane::releaseVelocity));
    CHECK (toggled (set, Lane::releaseVelocity) == set);
    // 40 px a lane + the footer, at most half the dock, at least 22 px a lane
    CHECK (areaHeight (2, 600) == 2 * 40 + footerHeight);
    CHECK (areaHeight (4, 600) == 4 * 40 + footerHeight);
    CHECK (areaHeight (4, 200) == 4 * 22 + footerHeight);
    CHECK (areaHeight (2, 150) == 75);
}

TEST (livefix190jd_mpe_tab_hits)
{
    using namespace lynx::mpetab;
    // a note from x 100 to 200; its curve: points at (100, 50) and (150, 30), held to 200
    const std::vector<Px> points { { 100.0f, 50.0f }, { 150.0f, 30.0f } };
    const std::vector<Px> line { { 100.0f, 50.0f }, { 100.0f, 50.0f }, { 150.0f, 30.0f }, { 200.0f, 30.0f } };
    // on a point: the point (it wins over the line)
    auto r = hitTest (points, line, 100.0f, 200.0f, { 151.0f, 33.0f });
    CHECK (r.hit == Hit::point && r.index == 1);
    // on the line between the points: the line, with its y there
    r = hitTest (points, line, 100.0f, 200.0f, { 125.0f, 41.0f });
    CHECK (r.hit == Hit::line);
    CHECK (std::abs (r.lineY - 40.0f) < 0.01f);
    // off the line: nothing (Live: a click there adds nothing)
    r = hitTest (points, line, 100.0f, 200.0f, { 125.0f, 60.0f });
    CHECK (r.hit == Hit::none);
    // outside the note: nothing
    r = hitTest (points, line, 100.0f, 200.0f, { 220.0f, 30.0f });
    CHECK (r.hit == Hit::none);
    // the held part after the last point is line too
    r = hitTest (points, line, 100.0f, 200.0f, { 190.0f, 28.0f });
    CHECK (r.hit == Hit::line && std::abs (r.lineY - 30.0f) < 0.01f);
    // a note without a curve: its own value's dashed line
    r = hitTest ({}, { { 100.0f, 70.0f }, { 200.0f, 70.0f } }, 100.0f, 200.0f, { 160.0f, 73.0f });
    CHECK (r.hit == Hit::line && std::abs (r.lineY - 70.0f) < 0.01f);
}

TEST (livefix190jd_mpe_tab_pitch_on_notes)
{
    using namespace lynx::mpetab;
    // one key row (14 px) a semitone, whole semitones; up = higher; Shift: whole cents
    CHECK (bendCents (0.0f, 0.0f, 14.0f, false) == 0.0f);
    CHECK (bendCents (0.0f, -14.0f, 14.0f, false) == 100.0f);
    CHECK (bendCents (0.0f, -20.0f, 14.0f, false) == 100.0f);   // 1.43 st -> 1 st
    CHECK (bendCents (0.0f, -22.0f, 14.0f, false) == 200.0f);   // 1.57 st -> 2 st
    CHECK (bendCents (0.0f, 70.0f, 14.0f, false) == -500.0f);
    CHECK (bendCents (0.0f, -7.0f, 14.0f, true) == 50.0f);
    CHECK (bendCents (0.0f, -14.0f * 60.0f, 14.0f, false) == 4800.0f); // clamped to ±48 st
    // a click on a selected note adds a pitch point at the curve's value there ("0 st" on a plain note), then the drag
    // moves it: the curve starts from the note's own pitch at tick 0
    lynx::NoteCurvesRef curves;
    const auto added = lynx::notecurves::addPoint (curves, lynx::NoteCurveLane::pitch, 480.0, 0.0f, 0.0f);
    CHECK (added == 1);
    CHECK (curves->lane (lynx::NoteCurveLane::pitch).size() == 2);
    lynx::notecurves::movePoint (curves, lynx::NoteCurveLane::pitch, added, 480.0, bendCents (0.0f, -28.0f, 14.0f, false));
    CHECK (curves->lane (lynx::NoteCurveLane::pitch)[1].value == 200.0f);
    CHECK (curves->valueAt (lynx::NoteCurveLane::pitch, 240.0, 0.0f) == 100.0f);
    // a double-click on the point deletes it
    lynx::notecurves::removePoint (curves, lynx::NoteCurveLane::pitch, 1);
    CHECK (curves->lane (lynx::NoteCurveLane::pitch).size() == 1);
}
