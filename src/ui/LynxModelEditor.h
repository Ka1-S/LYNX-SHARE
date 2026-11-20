#pragma once

// The editor of LYNX Model, LYNX's own physical-modelling instrument (round 190ni, core/physmod/LynxModel.h): LYNX's
// own design. A model row (marimba, vibraphone, xylophone, glockenspiel, tubular bells, kalimba), the struck bar (or
// tube / tine) drawn with its strike point - drag it to move Position - and three knob groups: Mallet (hardness,
// position, velocity, noise), Bar (damping, resonator, motor, release) and Output (tone, volume). Every knob is
// automatable (its engine parameter).

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
struct ChannelDef;
struct Song;

std::unique_ptr<juce::Component> makeLynxModelEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel);

namespace lynxmodelui
{
// (the off-screen shots and tests) the strike point's x in the bar view of `editor` for `position` 0…1, and the
// position a click at x gives; −1 when `editor` is not LYNX Model's
int strikeX (juce::Component& editor, double position);
double positionAtX (juce::Component& editor, int x);
} // namespace lynxmodelui
} // namespace lynx
