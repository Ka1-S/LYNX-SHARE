#pragma once

// Round 190ec: the Drum Rack's headless pictures (LYNX --drumshot190ec <prefix>): a demo Drum Rack's pad view, its chain
// list with the I-O, send and return sections, a new empty Drum Rack and the Piano roll's key column with the pad names.
// Off-screen only (component snapshots, no window, no foreground); writes <prefix>-checks.txt with hit-test checks.

#include <juce_core/juce_core.h>

namespace lynx::drumshot
{
void run (const juce::String& prefix);
} // namespace lynx::drumshot
