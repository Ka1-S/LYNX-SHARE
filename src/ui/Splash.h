#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Instruments.h"

// The start-up splash screen: the app icon (logo by w3teal) drops in with a bounce on a dark card, "LYNX" and the
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

// What Main drives during the start-up (round 190hd): the card on the message thread (SplashScreen, a JUCE window: Linux,
// macOS, LYNX_SPLASH_THREAD=0) or, on Windows, a layered window of its own drawn by a thread of its own with the same
// drawing code (makeStartupSplash): the message thread can then start the engine, open the audio device and build the
// main window while the card is up and while the logo drops, without stopping a single frame of the drop.
class StartupSplash
{
public:
    virtual ~StartupSplash() = default;

    // Shows the card centred on the main display, above other windows (the clock has not started yet).
    virtual void showOnDesktop() = 0;
    // t = 0 of the animation (start the start-up sound at the same moment).
    virtual void startClock() = 0;
    virtual double getElapsed() const = 0;
    virtual void setStatus (const juce::String& text) = 0;
    // the progress bar fills over this (default SplashScreen::minimumDuration)
    virtual void setBarDuration (double seconds) = 0;
    // above every other window again, without taking the focus
    virtual void raise() = 0;
    // Fades out, removes itself from the desktop and calls `whenGone` (on the message thread).
    virtual void finish (std::function<void()> whenGone) = 0;

    // The frames painted during the drop (the clock's first `landing` seconds) and the longest time between two of
    // them, in ms (round 190go: the start-up trace shows that the window built behind the card leaves the drop alone)
    virtual int getDropFrames() const = 0;
    virtual double getLongestDropGapMs() const = 0;
    // When the first frame was on the screen (steady clock; the epoch: not yet)
    virtual std::chrono::steady_clock::time_point getFirstFrameTime() const = 0;
    // true: the card draws on a thread of its own, the message thread may work during the drop
    virtual bool drawsOnItsOwnThread() const { return false; }
    // Runs `then` on the message thread once the clock has reached `seconds` (the logo has landed). The JUCE card: a
    // JUCE timer, as before. The card on its own thread: its own clock (round 190hd: a JUCE timer started while the
    // message thread is busy can fire up to ~0.3 s early: JUCE's timer thread takes the time it waited for a busy
    // message thread off a timer added meanwhile, which showed the window and faded the card in the middle of the drop)
    virtual void whenLanded (double seconds, std::function<void()> then)
    {
        juce::Timer::callAfterDelay (juce::roundToInt (seconds * 1000.0), std::move (then));
    }
    // (the card on its own thread) the clock time of the longest gap between two frames of the drop, seconds
    virtual double getLongestDropGapAt() const { return -1.0; }
    // (the card on its own thread) where the frame before that gap spent its time
    virtual juce::String getLongestDropGapDetail() const { return {}; }

    // Called once on the message thread, after the card's first frame is on the screen (round 190go: the start-up goes
    // on then, not after a fixed wait)
    std::function<void()> onFirstFrame;
};

// The card for this start: Windows draws it on a thread of its own (round 190hd), unless LYNX_SPLASH_THREAD=0.
std::unique_ptr<StartupSplash> makeStartupSplash();

// One frame of the card (SplashScreen::width x height, the logical size): the same drawing for both kinds of card
// and for --snapshot. `fonts` resolved on the message thread (a card drawn by its own thread uses them there).
struct SplashFrame
{
    double t = 0.0;              // seconds of the clock (0: the still card before the drop)
    double barDuration = 2.1;    // the progress bar fills over this
    juce::String status, subtitle, version;
    const juce::Image* logo = nullptr;
    const juce::Font* fonts = nullptr; // [4]: "LYNX", "SHARE", the subtitle, the small text (nullptr: made here)
};
void drawSplashFrame (juce::Graphics& g, const SplashFrame& frame);
// The fonts drawSplashFrame uses, resolved (their typefaces looked up) on the calling thread.
std::array<juce::Font, 4> makeSplashFonts();
// LYNX --splashshot <folder> (round 190hd): the card at a few moments of its clock, drawn as the JUCE card is (the
// platform's renderer: Direct2D on Windows; "-gpu") and as the card on its own thread is (the CPU; "-cpu"), at 100 %
// and 150 %, as PNGs for a side by side. Returns the files written.
juce::Array<juce::File> writeSplashShots (const juce::File& folder);

class SplashScreen final : public juce::Component,
                           public StartupSplash,
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
    static constexpr double fadeOutTime = 0.25;

    void showOnDesktop() override;
    void startClock() override;
    double getElapsed() const override;
    void setStatus (const juce::String& text) override;
    void setBarDuration (double seconds) override { barDuration = seconds; }
    void raise() override { toFront (false); }
    void finish (std::function<void()> whenGone) override;

    // --snapshot: draws the frame at `seconds` (no desktop, no timer).
    void setTimeForSnapshot (double seconds);

    int getDropFrames() const override { return dropFrames; }
    double getLongestDropGapMs() const override { return longestDropGapMs; }
    std::chrono::steady_clock::time_point getFirstFrameTime() const override { return firstFrameTime; }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    juce::Image logoMark;
    juce::String status;
    double time = 0.0;
    double clockStartMs = -1.0, fadeStartMs = -1.0;
    double barDuration = minimumDuration;
    int dropFrames = 0;
    double longestDropGapMs = 0.0, lastDropPaintMs = -1.0;
    std::chrono::steady_clock::time_point firstFrameTime;
    std::function<void()> onGone;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplashScreen)
};
} // namespace lynx
