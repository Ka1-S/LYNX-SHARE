#pragma once

// Game controller control (round 184a, slice S16; design: /mnt/e/lynx-work/simple184a/DESIGN.md §4.2). FL Studio Mobile
// can be driven with a game pad; LYNX does it on the desktop, for every layout style, off by default (Settings › MIDI ›
// "Use a game controller"). This is the plain logic: the pad's state as XInput reports it, dead zones and curves, the
// default map of buttons to actions, and the MIDI controllers each button and axis also sends, so any of them can be
// linked to any parameter through the usual Link to controller (the pad is then one more MIDI source). No JUCE; the
// Windows poller is ui/GamepadInput.

#include <array>
#include <cstdint>

namespace lynx::gamepad
{
// One pad's state, in XInput's units (XINPUT_GAMEPAD): button bits, triggers 0…255, sticks −32768…32767.
struct State
{
    bool connected = false;
    std::uint16_t buttons = 0;
    std::uint8_t leftTrigger = 0, rightTrigger = 0;
    std::int16_t leftX = 0, leftY = 0, rightX = 0, rightY = 0;
    bool operator== (const State&) const = default;
};

enum class Button : std::uint8_t
{
    a, b, x, y,
    leftShoulder, rightShoulder,
    back, start,
    leftThumb, rightThumb,
    up, down, left, right,
    count
};
constexpr int buttonCount = (int) Button::count;

enum class Axis : std::uint8_t
{
    leftTrigger, rightTrigger, // 0…1
    leftX, leftY, rightX, rightY, // −1…1 (Y up = +)
    count
};
constexpr int axisCount = (int) Axis::count;

// The XINPUT_GAMEPAD_* bit of a button.
std::uint16_t mask (Button b) noexcept;
inline bool pressed (const State& s, Button b) noexcept { return s.connected && (s.buttons & mask (b)) != 0; }

// An axis after its dead zone (XInput's recommended ones: 7849 / 8689 on the left / right stick, 30 on a trigger),
// rescaled so it starts from 0 at the edge of the dead zone: triggers 0…1, sticks −1…1, each stick's two axes with a
// round dead zone.
float axis (const State& s, Axis a) noexcept;

// What a button does by default (design §4.2).
enum class Action : std::uint8_t
{
    none,
    playStop,      // A
    record,        // B
    undo,          // X
    metronome,     // Y
    previousTrack, // D-pad up
    nextTrack,     // D-pad down
    barBack,       // D-pad left: the song position one bar back
    barForward,    // D-pad right
    previousPattern, // LB: the previous pattern (the Simple workflow's dock tabs once it has them)
    nextPattern,     // RB
    finish,          // Start: the Export window (the Simple workflow's Finish dock once it has one)
    hints          // Back (held): the picture of the pad with this map
};
using Map = std::array<Action, buttonCount>;
const Map& defaultMap() noexcept;
// "Play / stop"… (English, for tr())
const char* actionName (Action a) noexcept;
// "A", "B", "LB", "Start", "D-pad up"… (English, for tr())
const char* buttonName (Button b) noexcept;

// What the axes do by default: LT / RT turn the selected track's volume down / up (dB per second at full travel), the
// left stick's X sets its pan (held to where it is pushed; back to the centre does not move it), the right stick is
// free for links.
constexpr double volumeDbPerSecond = 12.0;

// The MIDI controller each button and axis sends on `midiChannel` (1…16) when it changes, so Link to controller can
// learn it: controllers nobody defines (102…119 and 20…21), buttons 127 / 0, axes 0…127 (sticks centred at 64).
constexpr int midiChannel = 16;
int controllerOf (Button b) noexcept;
int controllerOf (Axis a) noexcept;
int midiValue (const State& s, Axis a) noexcept;
inline int midiValue (const State& s, Button b) noexcept { return pressed (s, b) ? 127 : 0; }

// The changes from one poll to the next, in a fixed buffer (no allocation: the poller runs 60 times a second).
struct Changes
{
    std::array<Button, buttonCount> pressed {}, released {};
    int pressedCount = 0, releasedCount = 0;
    // a controller message per button or axis whose MIDI value changed
    std::array<std::uint8_t, buttonCount + axisCount> controller {}, value {};
    int controllerCount = 0;
    bool connectedChanged = false;
};
Changes compare (const State& before, const State& now) noexcept;

// Gains in dB per poll for the trigger volume (`dt` seconds since the last poll): −dB from LT, +dB from RT.
double volumeStepDb (const State& s, double dt) noexcept;
} // namespace lynx::gamepad
