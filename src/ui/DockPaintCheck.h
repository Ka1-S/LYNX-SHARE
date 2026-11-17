#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
class MainComponent;

namespace dockpaint
{
// --uiperf with LYNX_UIPERF_DOCKCHECK=1 (round 190jy, queue item 44): the checks of a docked LYNX editor drawn by the main
// window, in the real main window with the loaded project (Windows; elsewhere every step says "not available"). The
// project needs a Gross Beat insert (Ookay - Thief). `step (i)` runs step i (0 … count() - 1, a few hundred ms apart so
// the system has moved and drawn the windows) and returns its report lines ("dockcheck_<name>=OK" / "=FAIL <why>"); the
// last one adds "dockcheck_result=OK|FAIL". `shots`: where the on-screen pictures go (the pixel comparison of the same
// editor drawn as a native child window and by the main window).
class Check
{
public:
    Check (AudioEngine& engine, MainComponent& ui, juce::Component& mainWindow, juce::File shots);
    ~Check();
    static int count();
    juce::String step (int index);

private:
    struct State;
    std::unique_ptr<State> state;
};
} // namespace dockpaint
} // namespace lynx
