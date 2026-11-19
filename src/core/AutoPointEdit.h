#pragma once

// Issue #207 (round 190lw): editing several points of one automation curve at once — an automation clip's points, an
// audio clip's envelope lane or a Playlist automation lane. A rubber band selects the points inside it; the selection
// moves as a group (it keeps its shape and stays between the points around it), is deleted, copied and pasted; and a
// freehand pen stroke is thinned when the button is released so it keeps its shape with few points. No JUCE here.

#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::autopoints
{
// The points (indices, ascending) whose tick lies in [t0, t1] and value in [v0, v1] (the ends may come either way round).
std::vector<int> inBox (const std::vector<AutomationPoint>& points, std::int64_t t0, std::int64_t t1, float v0, float v1);

// Deletes the selected points (as FL's point delete does: the joined segment takes the curve of the one that started at
// the deleted point), keeping at least `keep` points (the earliest selected ones are deleted first). Returns how many
// were deleted.
int erase (std::vector<AutomationPoint>& points, std::vector<int> selected, int keep = 1);

// A group move: so many ticks later and so much higher.
struct Delta
{
    std::int64_t ticks = 0;
    float value = 0.0f;
    bool operator== (const Delta&) const = default;
};
// `want` limited so that, moved from `originals`, every selected point stays at tick >= 0, between the unselected points
// before and after it (equal ticks allowed), and every value stays in 0..1 (the group keeps its shape).
Delta clampMove (const std::vector<AutomationPoint>& originals, const std::vector<int>& selected, Delta want);
// `originals` with the selected points moved by clampMove's delta.
std::vector<AutomationPoint> moved (const std::vector<AutomationPoint>& originals, const std::vector<int>& selected, Delta want);

// Copied points: ticks from the first copied point, and the span they cover.
struct Clipboard
{
    std::vector<AutomationPoint> points;
    std::int64_t span = 0;
    bool empty() const noexcept { return points.empty(); }
};
Clipboard copy (const std::vector<AutomationPoint>& points, const std::vector<int>& selected);
// Pastes at tick `at` (>= 0): the points from `at` to `at + span` are replaced. Returns the pasted points' indices.
std::vector<int> paste (std::vector<AutomationPoint>& points, const Clipboard& clip, std::int64_t at);

// A freehand stroke thinned: among the points with a tick in [from, to], the straight ones (no bend, no mode) that a
// straight line through their kept neighbours passes within `tolerance` (in value, 0..1) are removed (Ramer-Douglas-
// Peucker). Kept always: the first and last point of the range, bent / moded points, and both points of a step (two
// points on one tick). Returns how many were removed.
int thin (std::vector<AutomationPoint>& points, std::int64_t from, std::int64_t to, float tolerance = 0.01f);
} // namespace lynx::autopoints
