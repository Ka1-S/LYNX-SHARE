#pragma once

// Round 190po: an offline render is the same bit for bit whichever threads compute it. On Windows a `LYNX --export` of
// one project gave different files run to run (round 190pd: Exoplanet, three hashes from one exe): the render workers
// ran LYNX's long double engines at the x87's 53-bit precision, the main thread at 64 (core/Denormals.h,
// ScopedExtendedPrecision). See engine/Determinism190poTest.cpp.

#include <functional>

#include <juce_core/juce_core.h>

namespace lynx::determinism190po
{
// Synchronous, on the message thread. "OK (...)" or "ERROR ..."; `log` (optional) gets a line per check.
juce::String check (std::function<void (const juce::String&)> log = {});
} // namespace lynx::determinism190po
