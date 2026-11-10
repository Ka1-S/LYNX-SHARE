#pragma once

// The game controller poller (round 184a, slice S16): while Settings › MIDI › "Use a game controller" is on, the first
// connected XInput pad is read 60 times a second and each change goes to `onChanges` (core/GamepadMap.h). Windows only
// for now (design D18): xinput1_4.dll (or xinput9_1_0.dll) is loaded at run time, so LYNX has no new link dependency;
// elsewhere the pad always reads as not connected. The object exists only while the option is on: no timer otherwise.
// Also the hints overlay: the pad with what each button does, shown while Back is held.

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/GamepadMap.h"

namespace lynx
{
class GamepadInput final : private juce::Timer
{
public:
    // `changes`, the state now and the seconds since the previous poll
    using Handler = std::function<void (const gamepad::Changes&, const gamepad::State&, double)>;
    explicit GamepadInput (Handler onChanges);
    ~GamepadInput() override;

    // Whether XInput could be loaded (always false outside Windows).
    bool available() const noexcept { return getState != nullptr; }
    const gamepad::State& state() const noexcept { return last; }

    // Tests: polls `fake` instead of the pad (as if the pad reported it now). Calls the handler like a real poll.
    void inject (const gamepad::State& fake, double seconds);

private:
    void timerCallback() override;
    void apply (const gamepad::State& now, double seconds);

    Handler handler;
    gamepad::State last;
    double lastTime = 0.0;
    int padIndex = 0;          // the pad in use (the first connected one, looked for again when it goes away)
    int searchCountdown = 0;   // while no pad is connected, look for one only every second
    void* library = nullptr;   // HMODULE
    using GetState = unsigned long (*) (unsigned long, void*); // XInputGetState (x64: one calling convention)
    GetState getState = nullptr;
};

// The hints overlay: the pad drawn by LYNX with the current map.
std::unique_ptr<juce::Component> makeGamepadHints();
} // namespace lynx
