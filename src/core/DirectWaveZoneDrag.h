#pragma once
// DirectWave's key / velocity zone map gestures, as FL's ZoneEditor does them (live check 190iw Q2, round 190jd):
// - a drag on the box (its edges included) moves the whole zone: keys left/right, the velocity range up/down (same size);
//   the root stays, unless Alt (or Ctrl+Alt) is held, then it moves with the keys; Ctrl alone is a plain drag;
// - only two corner squares resize: the upper-left one sets L-Key + L-Vel, the lower-right one H-Key + H-Vel.
// Velocity 0 is at the top of the map, so "upper" = the low velocity.

#include <algorithm>

#include "core/DirectWave.h"

namespace lynx::dw
{
enum class ZoneHandle
{
    body,      // the box and its edges: moves the zone
    lowCorner, // the upper-left square: low key + low velocity
    highCorner // the lower-right square: high key + high velocity
};

// `before` is the zone at mouse-down; dk / dv the drag in keys / velocity steps; `alt` = Alt (or Ctrl+Alt) held
inline Zone dragZone (const Zone& before, ZoneHandle handle, int dk, int dv, bool alt)
{
    auto z = before;
    switch (handle)
    {
        case ZoneHandle::body:
        {
            const auto k = std::clamp (dk, -before.lowKey, 127 - before.highKey);
            const auto v = std::clamp (dv, -before.lowVel, 127 - before.highVel);
            z.lowKey += k; z.highKey += k;
            z.lowVel += v; z.highVel += v;
            if (alt)
                z.root = std::clamp (before.root + k, 0, 127);
            break;
        }
        case ZoneHandle::lowCorner:
            z.lowKey = std::clamp (before.lowKey + dk, 0, before.highKey);
            z.lowVel = std::clamp (before.lowVel + dv, 0, before.highVel);
            break;
        case ZoneHandle::highCorner:
            z.highKey = std::clamp (before.highKey + dk, before.lowKey, 127);
            z.highVel = std::clamp (before.highVel + dv, before.lowVel, 127);
            break;
    }
    return z;
}
} // namespace lynx::dw
