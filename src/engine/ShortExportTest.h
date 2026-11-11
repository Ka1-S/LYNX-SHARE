#pragma once

// Issue #138 (round 190c): a short note at the very start of the Playlist must export like the same note a bar later.
// Each case goes through the Export window's path (an ExportJob made on the message thread, run on its own thread
// while the message loop runs) twice: the note at tick 0, and at bar 2. See engine/ShortExportTest.cpp.

#include <functional>

#include <juce_core/juce_core.h>

namespace lynx::shortexport
{
// Message thread, with the message loop running; returns at once. `done` gets "OK (...)" or "ERROR ..." on the message
// thread. `everyUnit`: every instrument LYNX can make here (else a handful, for the selftest); `repeats`: each pair
// that many times (a race shows up in some of them); `pluginFiles`: hosted instruments to add; `log`: a line per pair.
void run (bool everyUnit, int repeats, const juce::StringArray& pluginFiles, std::function<void (const juce::String&)> log,
          std::function<void (juce::String)> done);
} // namespace lynx::shortexport
