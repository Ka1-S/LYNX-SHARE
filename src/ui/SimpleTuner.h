#pragma once

// The Simple top bar's tuner (round 190ke, owner decision D5; core/SimpleTuner.h has GarageBand's and BandLab's
// evidence). A one-segment capsule after the tempo capsule with a tuning-fork glyph (GarageBand's Tuner button in its
// control bar); a click opens the tuner beside it for the selected track's audio input: the Keynote (big note letter and
// its sharp), the needle on a −50…+50 cent arc, the Tune Deviation in cents and the Reference Tuning (drag; double-click:
// 440.0 Hz). LYNX vectors only; dark panel, amber while flat / sharp and green in tune as GarageBand's (LYNX's own warning
// and LED colours).
//
// Performance: nothing runs while it is closed. Open, a 30 Hz timer reads the engine's tuner tap (fed only then),
// runs the detector on the message thread and repaints the dial only when the reading moved.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

namespace simpletunerui
{
// The top bar's button (a segment of its own capsule: the main window paints the capsule, the button its glyph and its
// hover / open states). `onClick` opens or closes the tuner.
std::unique_ptr<juce::Button> makeButton();
void setOpen (juce::Button& button, bool open);

// The tuner panel for the track `track()` returns (-1: none; the device's first input). It listens while it shows.
std::unique_ptr<juce::Component> makePanel (AudioEngine& engine, std::function<int()> track);
// Shots: a panel that shows `midiNote` off by `cents` with the reference `hz` (no input), the source line `source`
// ("" = none) — or, with midiNote < 0, the panel with no pitch yet
std::unique_ptr<juce::Component> makePanelForSnapshot (AudioEngine& engine, int midiNote, double cents, double hz, const juce::String& source);
// Tests: feeds the panel's detector `seconds` of a sine at `hz` and returns the reading's note / cents / validity
struct CheckReading
{
    bool valid = false;
    int note = 0;
    double cents = 0.0, hz = 0.0;
};
CheckReading feedForCheck (juce::Component& panel, double sineHz, double seconds, double sampleRate = 48000.0);
// The reference tuning the user set (saved in the settings folder), and setting it
double reference();
void setReference (double hz);
inline constexpr int panelWidth = 360, panelHeight = 268;
} // namespace simpletunerui
} // namespace lynx
