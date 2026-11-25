#pragma once

// A new instance of the FL effects whose settings live in the plugin's own state, without FL (round 190rh): a state LYNX writes
// from the documented layout with neutral or plainly useful values. Nothing of FL's preset files is in it. With FL
// installed a new one still starts from FL's Default preset (core/NativeEngines.h).

#include <optional>

#include "core/Song.h"

namespace lynx::nativeslots
{
std::optional<EffectSlot> make (EffectKind kind); // nullopt: no state of its own here
} // namespace lynx::nativeslots
