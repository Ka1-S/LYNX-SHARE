#pragma once

#include <functional>

#include <juce_core/juce_core.h>

namespace lynx
{
// LYNX.exe --clapteardowntest "LYNX Test Restart.clap" report.txt (round 190az, tools/testplugin/lynx_test_restart.c):
// the last reference to a running CLAP plugin is dropped on a worker thread while the message thread is blocked on
// that very thread (what an export's thread and the thread that joins it do), and while it is free. The plugin aborts
// the process when deactivate or destroy reach it off the main thread, or stop_processing off an audio thread, so the
// host must leave the teardown to the message thread however long that takes, and nothing may be left over when the
// application closes. Runs on the message loop; done (exit code, 0 = OK) is called when it has ended.
void runClapTeardownTest (const juce::String& pluginPath, const juce::String& reportPath, std::function<void (int)> done);
} // namespace lynx
