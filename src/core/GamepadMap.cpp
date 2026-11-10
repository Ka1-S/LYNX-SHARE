#include "core/GamepadMap.h"

#include <algorithm>
#include <cmath>

namespace lynx::gamepad
{
namespace
{
constexpr double leftDeadZone = 7849.0, rightDeadZone = 8689.0, triggerThreshold = 30.0; // XInput's recommended values

// one stick's two axes with a round dead zone, the magnitude rescaled from the zone's edge to the full range
void stick (std::int16_t rawX, std::int16_t rawY, double deadZone, float& x, float& y) noexcept
{
    const double ax = rawX, ay = rawY;
    const auto magnitude = std::sqrt (ax * ax + ay * ay);
    if (magnitude <= deadZone)
    {
        x = y = 0.0f;
        return;
    }
    const auto scaled = std::min (1.0, (std::min (magnitude, 32767.0) - deadZone) / (32767.0 - deadZone));
    x = (float) std::clamp (ax / magnitude * scaled, -1.0, 1.0);
    y = (float) std::clamp (ay / magnitude * scaled, -1.0, 1.0);
}

float trigger (std::uint8_t raw) noexcept
{
    return raw <= triggerThreshold ? 0.0f : (float) ((raw - triggerThreshold) / (255.0 - triggerThreshold));
}
} // namespace

std::uint16_t mask (Button b) noexcept
{
    switch (b)
    {
        case Button::up: return 0x0001;
        case Button::down: return 0x0002;
        case Button::left: return 0x0004;
        case Button::right: return 0x0008;
        case Button::start: return 0x0010;
        case Button::back: return 0x0020;
        case Button::leftThumb: return 0x0040;
        case Button::rightThumb: return 0x0080;
        case Button::leftShoulder: return 0x0100;
        case Button::rightShoulder: return 0x0200;
        case Button::a: return 0x1000;
        case Button::b: return 0x2000;
        case Button::x: return 0x4000;
        case Button::y: return 0x8000;
        case Button::count: break;
    }
    return 0;
}

float axis (const State& s, Axis a) noexcept
{
    if (! s.connected)
        return 0.0f;
    float x = 0.0f, y = 0.0f;
    switch (a)
    {
        case Axis::leftTrigger: return trigger (s.leftTrigger);
        case Axis::rightTrigger: return trigger (s.rightTrigger);
        case Axis::leftX: stick (s.leftX, s.leftY, leftDeadZone, x, y); return x;
        case Axis::leftY: stick (s.leftX, s.leftY, leftDeadZone, x, y); return y;
        case Axis::rightX: stick (s.rightX, s.rightY, rightDeadZone, x, y); return x;
        case Axis::rightY: stick (s.rightX, s.rightY, rightDeadZone, x, y); return y;
        case Axis::count: break;
    }
    return 0.0f;
}

const Map& defaultMap() noexcept
{
    static const Map map = [] {
        Map m {};
        m.fill (Action::none);
        m[(std::size_t) Button::a] = Action::playStop;
        m[(std::size_t) Button::b] = Action::record;
        m[(std::size_t) Button::x] = Action::undo;
        m[(std::size_t) Button::y] = Action::metronome;
        m[(std::size_t) Button::up] = Action::previousTrack;
        m[(std::size_t) Button::down] = Action::nextTrack;
        m[(std::size_t) Button::left] = Action::barBack;
        m[(std::size_t) Button::right] = Action::barForward;
        m[(std::size_t) Button::leftShoulder] = Action::previousPattern;
        m[(std::size_t) Button::rightShoulder] = Action::nextPattern;
        m[(std::size_t) Button::start] = Action::finish;
        m[(std::size_t) Button::back] = Action::hints;
        return m;
    }();
    return map;
}

const char* actionName (Action a) noexcept
{
    switch (a)
    {
        case Action::none: return "";
        case Action::playStop: return "Play / stop";
        case Action::record: return "Record";
        case Action::undo: return "Undo";
        case Action::metronome: return "Metronome";
        case Action::previousTrack: return "Previous track";
        case Action::nextTrack: return "Next track";
        case Action::barBack: return "One bar back";
        case Action::barForward: return "One bar forward";
        case Action::previousPattern: return "Previous pattern";
        case Action::nextPattern: return "Next pattern";
        case Action::finish: return "Export";
        case Action::hints: return "Show this map (hold)";
    }
    return "";
}

const char* buttonName (Button b) noexcept
{
    switch (b)
    {
        case Button::a: return "A";
        case Button::b: return "B";
        case Button::x: return "X";
        case Button::y: return "Y";
        case Button::leftShoulder: return "LB";
        case Button::rightShoulder: return "RB";
        case Button::back: return "Back";
        case Button::start: return "Start";
        case Button::leftThumb: return "Left stick press";
        case Button::rightThumb: return "Right stick press";
        case Button::up: return "D-pad up";
        case Button::down: return "D-pad down";
        case Button::left: return "D-pad left";
        case Button::right: return "D-pad right";
        case Button::count: break;
    }
    return "";
}

int controllerOf (Button b) noexcept { return 102 + (int) b; } // 102…115

int controllerOf (Axis a) noexcept
{
    switch (a)
    {
        case Axis::leftTrigger: return 20;
        case Axis::rightTrigger: return 21;
        case Axis::leftX: return 116;
        case Axis::leftY: return 117;
        case Axis::rightX: return 118;
        case Axis::rightY: return 119;
        case Axis::count: break;
    }
    return 0;
}

int midiValue (const State& s, Axis a) noexcept
{
    const auto v = axis (s, a);
    if (a == Axis::leftTrigger || a == Axis::rightTrigger)
        return std::clamp ((int) std::lround (v * 127.0f), 0, 127);
    return std::clamp ((int) std::lround ((v + 1.0f) * 63.5f), 0, 127); // −1 → 0, 0 → 64, 1 → 127
}

Changes compare (const State& before, const State& now) noexcept
{
    Changes c;
    c.connectedChanged = before.connected != now.connected;
    for (int i = 0; i < buttonCount; ++i)
    {
        const auto b = (Button) i;
        const auto was = pressed (before, b), is = pressed (now, b);
        if (is && ! was)
            c.pressed[(std::size_t) c.pressedCount++] = b;
        else if (was && ! is)
            c.released[(std::size_t) c.releasedCount++] = b;
        if (was != is)
        {
            c.controller[(std::size_t) c.controllerCount] = (std::uint8_t) controllerOf (b);
            c.value[(std::size_t) c.controllerCount++] = (std::uint8_t) midiValue (now, b);
        }
    }
    for (int i = 0; i < axisCount; ++i)
    {
        const auto a = (Axis) i;
        const auto v = midiValue (now, a);
        if (v != midiValue (before, a))
        {
            c.controller[(std::size_t) c.controllerCount] = (std::uint8_t) controllerOf (a);
            c.value[(std::size_t) c.controllerCount++] = (std::uint8_t) v;
        }
    }
    return c;
}

double volumeStepDb (const State& s, double dt) noexcept
{
    return (axis (s, Axis::rightTrigger) - axis (s, Axis::leftTrigger)) * volumeDbPerSecond * std::max (0.0, dt);
}
} // namespace lynx::gamepad
