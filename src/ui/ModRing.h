#pragma once

// Round 190ox (modulators stage 2, research 190oe P9): the modulation amount ring on LYNX's knobs, drag-to-assign and the
// Modulators panel's Assign mode. LYNX's own look (Bitwig's idea of a ring per routed source, not its drawing): a thin
// arc in the automation colour just inside the knob's value arc, from the knob's value to value + depth (bipolar
// modulators: both ways, the far side dimmer). On screen only: a hardware controller's LED ring keeps showing the
// value (the patent gate of round 190ox, US 9,196,236 B1, claims a rotary encoder's indicator of base + offset).
//
// Where a knob is: the native effect editors' layout knobs, the controller knobs (ctlui::Knob) and JUCE sliders tagged
// with `tagSlider`. A component that paints several knobs says which one is where by being a TargetSource.

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Song.h"

namespace lynx
{
class AudioEngine;

namespace modring
{
// The engine the rings read (the main window sets it; nullptr: no rings).
void setEngine (AudioEngine* engine) noexcept;
AudioEngine* engine() noexcept;

struct Ring
{
    static constexpr int maxRoutes = 4;
    float depth[maxRoutes] {};
    bool bipolar[maxRoutes] {};
    int count = 0;
    int armedRoute = -1; // the route of the armed modulator among them
};
// The routes on `target` (false: none, or no engine).
bool ringFor (const AutomationTarget& target, Ring& out);

// Draws the ring around a knob drawn in `circle` whose value arc runs from `startAngle` over `sweep` radians, at value
// `value` (0..1). The standard LYNX knob: −135° … +135°.
// `knobAccent`: the knob's own arc colour; a ring close to it in hue is drawn in the second colour instead (a plugin
// page in the automation colour's teal).
void draw (juce::Graphics& g, juce::Rectangle<float> circle, float value, const Ring& ring, juce::Colour knobAccent = {},
           float startAngle = -juce::MathConstants<float>::pi * 0.75f, float sweep = juce::MathConstants<float>::pi * 1.5f);
// ringFor + draw.
void drawFor (juce::Graphics& g, juce::Rectangle<float> circle, float value, const AutomationTarget& target, juce::Colour knobAccent = {});
juce::Colour colour() noexcept;
juce::Colour colourAgainst (juce::Colour knobAccent) noexcept;

// Assign mode: while a modulator is armed (the Modulators panel's Assign button), a drag on a knob sets that
// modulator's depth on it (the route is made on the first move) instead of its value. −1: none.
int armed() noexcept;
void setArmed (int modulator);
// a depth drag on `target` (armed mode): begin (one undo step), move by `deltaPixels` up, end
bool beginDepthDrag (const AutomationTarget& target);
void dragDepth (float deltaPixels, bool fine);
void endDepthDrag();
bool depthDragging() noexcept;

// Routes `modulator` to `target` (default depth 25 %; a hosted plugin's base from its current value). False when the
// target can't be modulated or is routed already.
bool assign (int modulator, const AutomationTarget& target);

class TargetSource
{
public:
    virtual ~TargetSource() = default;
    // The modulatable control at `local` (this component's coordinates), if any.
    virtual std::optional<AutomationTarget> modTargetAt (juce::Point<int> local) = 0;
};
// A JUCE slider that is a modulatable knob (its ring is drawn by the look and feel).
void tagSlider (juce::Slider& slider, const AutomationTarget& target);
std::optional<AutomationTarget> taggedTarget (const juce::Component& c);
// The modulatable control under a point on the screen (drag-to-assign).
std::optional<AutomationTarget> targetUnder (juce::Point<int> screenPosition);
} // namespace modring
} // namespace lynx
