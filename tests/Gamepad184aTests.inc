// --- Round 184a, slice S16: game controller control (core/GamepadMap.h). XInput's state → dead zones, the default
// map, and the MIDI controllers that make every button and axis linkable; a fake state stands in for the pad.

#include "core/GamepadMap.h"
#include "core/Preferences.h"

TEST (gamepad184a_default_map)
{
    using namespace lynx::gamepad;
    const auto& m = defaultMap();
    CHECK (m[(std::size_t) Button::a] == Action::playStop);
    CHECK (m[(std::size_t) Button::b] == Action::record);
    CHECK (m[(std::size_t) Button::x] == Action::undo);
    CHECK (m[(std::size_t) Button::y] == Action::metronome);
    CHECK (m[(std::size_t) Button::up] == Action::previousTrack && m[(std::size_t) Button::down] == Action::nextTrack);
    CHECK (m[(std::size_t) Button::left] == Action::barBack && m[(std::size_t) Button::right] == Action::barForward);
    CHECK (m[(std::size_t) Button::leftShoulder] == Action::previousPattern && m[(std::size_t) Button::rightShoulder] == Action::nextPattern);
    CHECK (m[(std::size_t) Button::start] == Action::finish && m[(std::size_t) Button::back] == Action::hints);
    CHECK (m[(std::size_t) Button::leftThumb] == Action::none); // (free for links)
    for (int i = 0; i < buttonCount; ++i)
        CHECK (std::string (buttonName ((Button) i)).size() > 0);
}

TEST (gamepad184a_button_bits_are_xinputs)
{
    using namespace lynx::gamepad;
    CHECK (mask (Button::up) == 0x0001 && mask (Button::right) == 0x0008);
    CHECK (mask (Button::start) == 0x0010 && mask (Button::back) == 0x0020);
    CHECK (mask (Button::leftShoulder) == 0x0100 && mask (Button::rightShoulder) == 0x0200);
    CHECK (mask (Button::a) == 0x1000 && mask (Button::y) == 0x8000);
    std::uint16_t all = 0;
    for (int i = 0; i < buttonCount; ++i)
    {
        CHECK ((all & mask ((Button) i)) == 0); // each bit once
        all |= mask ((Button) i);
    }
    State s;
    s.buttons = 0x1000;
    CHECK (! pressed (s, Button::a)); // a pad that is not connected presses nothing
    s.connected = true;
    CHECK (pressed (s, Button::a) && ! pressed (s, Button::b));
}

TEST (gamepad184a_dead_zones)
{
    using namespace lynx::gamepad;
    State s;
    s.connected = true;
    s.leftX = 7000; // inside the left stick's dead zone (7849)
    CHECK (axis (s, Axis::leftX) == 0.0f);
    s.leftX = 32767;
    CHECK (std::abs (axis (s, Axis::leftX) - 1.0f) < 1e-6f);
    s.leftX = -32768;
    CHECK (std::abs (axis (s, Axis::leftX) + 1.0f) < 1e-6f);
    s.leftX = 20000; // halfway out of the zone
    const auto half = axis (s, Axis::leftX);
    CHECK (half > 0.4f && half < 0.55f);
    // round: a diagonal inside the zone stays at rest on both axes
    s.leftX = 5000;
    s.leftY = 5000; // magnitude 7071 < 7849
    CHECK (axis (s, Axis::leftX) == 0.0f && axis (s, Axis::leftY) == 0.0f);
    s.rightX = 8000; // inside the right stick's zone (8689), outside the left one's
    CHECK (axis (s, Axis::rightX) == 0.0f);
    s.leftTrigger = 30;
    CHECK (axis (s, Axis::leftTrigger) == 0.0f);
    s.leftTrigger = 255;
    CHECK (axis (s, Axis::leftTrigger) == 1.0f);
}

TEST (gamepad184a_controllers_are_linkable_and_unique)
{
    using namespace lynx::gamepad;
    std::vector<int> used;
    for (int i = 0; i < buttonCount; ++i)
        used.push_back (controllerOf ((Button) i));
    for (int i = 0; i < axisCount; ++i)
        used.push_back (controllerOf ((Axis) i));
    for (auto cc : used)
    {
        CHECK (cc >= 0 && cc < 128);
        CHECK (cc != 1 && cc != 7 && cc != 10 && cc != 11 && cc != 64 && cc != 120 && cc != 121 && cc != 123); // not a defined controller
        CHECK (std::count (used.begin(), used.end(), cc) == 1);
    }
    State s;
    s.connected = true;
    CHECK (midiValue (s, Axis::leftX) == 64); // centred
    CHECK (midiValue (s, Axis::leftTrigger) == 0);
    s.leftX = 32767;
    CHECK (midiValue (s, Axis::leftX) == 127);
    s.leftX = -32768;
    CHECK (midiValue (s, Axis::leftX) == 0);
}

TEST (gamepad184a_changes_between_polls)
{
    using namespace lynx::gamepad;
    State before, now;
    before.connected = now.connected = true;
    auto c = compare (before, now);
    CHECK (c.pressedCount == 0 && c.releasedCount == 0 && c.controllerCount == 0 && ! c.connectedChanged);
    now.buttons = mask (Button::a) | mask (Button::up);
    now.rightTrigger = 255;
    c = compare (before, now);
    CHECK (c.pressedCount == 2);
    CHECK (c.pressed[0] == Button::a && c.pressed[1] == Button::up);
    CHECK (c.controllerCount == 3); // two buttons and the trigger
    CHECK (c.controller[0] == controllerOf (Button::a) && c.value[0] == 127);
    CHECK (c.controller[2] == controllerOf (Axis::rightTrigger) && c.value[2] == 127);
    c = compare (now, before);
    CHECK (c.releasedCount == 2 && c.pressedCount == 0);
    CHECK (c.value[0] == 0);
    // unplugged: everything held is released, the sticks go back to the centre value
    State gone;
    c = compare (now, gone);
    CHECK (c.connectedChanged && c.releasedCount == 2);
    // a tiny stick move inside the dead zone sends nothing
    State drift = before;
    drift.leftX = 300;
    CHECK (compare (before, drift).controllerCount == 0);
}

TEST (gamepad184a_trigger_volume)
{
    using namespace lynx::gamepad;
    State s;
    s.connected = true;
    CHECK (volumeStepDb (s, 1.0 / 60.0) == 0.0);
    s.rightTrigger = 255;
    CHECK (std::abs (volumeStepDb (s, 1.0) - volumeDbPerSecond) < 1e-9);
    s.leftTrigger = 255;
    CHECK (volumeStepDb (s, 1.0) == 0.0);
    s.rightTrigger = 0;
    CHECK (std::abs (volumeStepDb (s, 0.5) + volumeDbPerSecond / 2) < 1e-9);
}

TEST (gamepad184a_setting_is_off_by_default_and_saved)
{
    lynx::prefs::MidiPrefs p;
    CHECK (! p.gamepad);
    CHECK (p.toText().find ("gamepad") == std::string::npos); // (an old settings file stays as it was)
    p.gamepad = true;
    CHECK (lynx::prefs::MidiPrefs::fromText (p.toText()).gamepad);
    CHECK (lynx::prefs::MidiPrefs::fromText (p.toText()) == p);
}
