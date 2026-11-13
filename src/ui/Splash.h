#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Instruments.h"

// The start-up splash screen: the logo (by w3teal) drops in with a bounce on a dark card, "LYNX" and the
// version slide in, a thin accent bar fills while the app initialises, then it fades out. It is shown
// only by the normal UI start (never in the command-line modes) and can be turned off, as can the
// start-up sound, in Options (stored in %APPDATA%\LYNX\ui-options.txt).
namespace lynx
{
struct StartupOptions
{
    bool splash = true; // show the splash screen
    bool sound = true;  // play the start-up sound

    static StartupOptions load(); // from ui-options.txt (keys "splash" and "startupSound")
};

// LYNX's start-up sound (core/StartupSound.h) as a sample the preview player can play.
std::shared_ptr<const SampleData> makeStartupSample();

class SplashScreen final : public juce::Component,
                           private juce::Timer
{
public:
    SplashScreen();
    ~SplashScreen() override;

    static constexpr int width = 560, height = 340;
    // seconds from the start of the process before it may close (round 121: counted from the process start, not from
    // when the card appeared — the window comes ~1 s sooner)
    static constexpr double minimumDuration = 2.1;
    // How long the card stays given that the process started `sinceProcessStart` seconds before its clock: the rest of
    // minimumDuration, but at least `landing` (the logo's drop, after which the main window is built).
    static double remainingDuration (double sinceProcessStart, double landing = 1.05);
    // the progress bar fills over this (default minimumDuration)
    void setBarDuration (double seconds) { barDuration = seconds; }
    static constexpr double fadeOutTime = 0.25;

    // Shows the card centred on the main display, above other windows (the clock has not started yet).
    void showOnDesktop();
    // t = 0 of the animation (start the start-up sound at the same moment).
    void startClock();
    double getElapsed() const;

    void setStatus (const juce::String& text);
    // Fades out, removes itself from the desktop and calls `whenGone`.
    void finish (std::function<void()> whenGone);

    // --snapshot: draws the frame at `seconds` (no desktop, no timer).
    void setTimeForSnapshot (double seconds);

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    juce::Image logoMark;
    juce::String status;
    double time = 0.0;
    double clockStartMs = -1.0, fadeStartMs = -1.0;
    double barDuration = minimumDuration;
    std::function<void()> onGone;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplashScreen)
};
} // namespace lynx
