#pragma once

// Round 190ou: an offline render made again. A second AudioEngine::renderOffline on the same engine came out silent
// (found by round 190op); the user flows that render more than once (export twice, export then play, stems then the
// mixdown, an Online-session bounce, Render to audio clip) are checked here too. See engine/RenderAgainTest.cpp.

#include <juce_core/juce_core.h>

namespace lynx::renderagain
{
// Synchronous, on the message thread. "OK (...)" or "ERROR ..."; `log` (optional) gets a line per check.
juce::String check (std::function<void (const juce::String&)> log = {});
} // namespace lynx::renderagain
