#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
// LYNX --zorder191test report.txt [plugin.vst3]: issue #191's run-time check (Windows; no injected input, the presses
// go through the main window's own peer as the system's would). In the Clip-launch layout, the Device View's window
// button of a built-in synth's card (and of a hosted plugin's card when a plugin file is given) opens its editor in
// front of the Playlist; a later click on the Playlist brings the Playlist in front; with the Playlist floating, the
// editor opens as a window of its own above it. LYNX_FIX191=0 shows the old order. Run it with LYNX_PROFILE_DIR set
// (it switches layouts, which saves the UI settings). `done` gets true when every check passed.
void runZOrder191Test (const juce::String& reportPath, const juce::String& pluginPath, juce::LookAndFeel* lookAndFeel,
                       std::function<void (bool)> done);
} // namespace lynx
