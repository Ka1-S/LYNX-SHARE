#pragma once

#include <functional>
#include <memory>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
namespace scalekeys
{
enum class Mode : int;
}

// Touch keyboard (FL's "Touch keyboard", interpreted; REA_rodada84): an on-screen piano that plays the selected
// channel. How far down a key is touched sets the velocity (or a fixed velocity), several fingers play at once, a
// drag glides from key to key, octave buttons shift it. Every note it starts is stopped when the finger or mouse is
// released, leaves the keys, the selected channel changes or the window closes. Toggled from View.
void toggleTouchKeyboard (AudioEngine& engine);
bool isTouchKeyboardOpen();

// The content for --snapshot mode (`pressed` keys drawn as held, nothing is played).
std::unique_ptr<juce::Component> makeTouchKeyboardPanel (AudioEngine& engine);

// Round 190ds: the same keyboard with its modes Keys | Scale keys | Chord pad (core/ScaleKeys.h), live, playing
// `channel` (empty: the selected channel): the Simple dock's Keyboard tab
std::unique_ptr<juce::Component> makeKeyboardPanel (AudioEngine& engine, std::function<int()> channel = {});
// --snapshot: a panel in `mode` with degree / pad `pressed` drawn held (-1: none), nothing played
std::unique_ptr<juce::Component> makeKeyboardPanelForSnapshot (AudioEngine& engine, scalekeys::Mode mode, int pressed,
                                                               std::function<int()> channel = {});
// --snapshot: the Chord pad's right-click menu of pad `pad` (panel = one made above)
juce::PopupMenu chordPadMenuForSnapshot (juce::Component& panel, int pad);
// --snapshot: a panel shows the shared settings and the project key as they are now (its timer does it while live)
void refreshKeyboardPanel (juce::Component& panel);
// The typing key that plays a MIDI key now ("" none / typing keyboard off): the Keys mode labels its keys with it
void setTypingKeyLetters (std::function<std::string (int)> letterOfKey);
// Called when a keyboard changes the shared settings (scalekeys::current): the main window saves them
void setKeyboardSettingsListener (std::function<void()> listener);
} // namespace lynx
