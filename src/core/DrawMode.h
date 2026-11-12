#pragma once

// Draw Mode (round 190ef, the Clip-launch layout's B key; the reference DAW's "Options › Draw Mode", its pencil switch in
// the control bar). With Draw Mode on, a drag in an automation lane draws instead of moving points:
//   - with a grid: steps as wide as the grid, each one flat at the value the mouse had over it (arrangement-0089);
//   - without a grid (snap off, or Alt held): freehand, the line follows the mouse (arrangement-0090).
// What is outside the drawn range keeps its curve: a step's edges are vertical jumps, two points on one tick (the left
// one carries the old curve's value there, the right one the new value; automationCurveAt gives the right one at the
// tick itself). docs/analise/REA_rodada190ef.md has the evidence. No JUCE here.

#include <cstdint>
#include <functional>
#include <vector>

#include "core/Song.h"

namespace lynx::drawmode
{
// --- the switch (a preference of the session, not of a project) --------------------------------------------------------
bool isOn() noexcept;
void set (bool on);
inline void toggle() { set (! isOn()); }
// Called on the message thread after every change (the control bar's switch, the Options menu, the Playlist's cursor).
// Returns an id for removeListener.
int addListener (std::function<void (bool on)> listener);
void removeListener (int id);

// --- the grid ----------------------------------------------------------------------------------------------------------
// One cell of the grid: [from, to).
struct Cell
{
    std::int64_t from = 0, to = 0;
    bool operator== (const Cell&) const = default;
};
// The cells of a grid of `grid` ticks (counted from tick 0) a drag from t0 to t1 passes over, left to right (t1 may be
// before t0). Ticks below 0 count as 0. grid <= 0: none.
std::vector<Cell> cellsBetween (std::int64_t t0, std::int64_t t1, std::int64_t grid);

// A step to draw: a cell and its value.
struct Step
{
    Cell cell;
    float value = 0.0f;
};
// The steps of one mouse move from (t0, v0) to (t1, v1): each cell passed takes the value the mouse had over the cell's
// middle, on the straight line between the two positions (clamped to the move's ends, then to 0..1); a move that stays
// in one cell gives that cell the value v1.
std::vector<Step> stepsAlong (std::int64_t t0, float v0, std::int64_t t1, float v1, std::int64_t grid);

// --- drawing into a point list --------------------------------------------------------------------------------------------
// The curve over [from, to] becomes a straight piece from vFrom to vTo; before `from` and from `to` on it stays as it
// was (an edge where the old curve has another value becomes a jump: two points on that tick). The points stay sorted;
// points that only repeat a flat run inside [from, to] are merged. Values are clamped to 0..1. Exact outside [from, to]
// where the old segments that cross an edge are straight (LYNX's default); a bent segment that crosses an edge is cut
// there, so its part outside keeps its ends but not its exact bend. from >= to: nothing changes.
void drawPiece (std::vector<AutomationPoint>& points, std::int64_t from, std::int64_t to, float vFrom, float vTo);
// A step: [from, to) flat at `value`.
inline void drawStep (std::vector<AutomationPoint>& points, std::int64_t from, std::int64_t to, float value)
{
    drawPiece (points, from, to, value, value);
}
// Freehand: the mouse went from (t0, v0) to (t1, v1) (either way round).
void drawLine (std::vector<AutomationPoint>& points, std::int64_t t0, float v0, std::int64_t t1, float v1);
} // namespace lynx::drawmode
