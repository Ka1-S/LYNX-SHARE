#pragma once

// Round 190rp (0.1.32 Q4, crash group G1: one Windows machine hung at every start inside its ASIO driver's open, 6 starts
// in a row): an audio device type whose devices are made and opened on a worker thread with a time limit
// (LYNX_DEVICE_OPEN_TIMEOUT_MS, 10 s by default). While the caller waits, messages sent to its windows are answered
// (an ASIO driver may send one to JUCE's message window from the ASIO thread). When the driver does not answer in time:
//  - the open is given up: the caller gets an error, the worker keeps the driver's device (it is never touched from
//    elsewhere again, and freed by the worker if the driver ever answers), and quitting does not wait for it;
//  - the device is marked hung for this process: later opens of it fail at once with the same error, until "Try
//    again" (allowAgain) asks for one more bounded attempt;
//  - lastHung() names it, so the engine can open the default device instead and say which driver hung.
// Applied to JUCE's ASIO type (Windows) and to the "Hang test" type (test mode with LYNX_TEST_DEVICE_HANG=1, every
// platform: a device that never returns from open, for the checks).

#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>

namespace lynx::deviceguard
{
// `inner` behind the time limit (the same type name, device names and listeners)
std::unique_ptr<juce::AudioIODeviceType> wrap (std::unique_ptr<juce::AudioIODeviceType> inner);
// The test type: "Hangs on open" (its open never returns) and "Answers" (a silent device whose thread calls back in real
// time). Only when platform::testMode() and LYNX_TEST_DEVICE_HANG=1, else nullptr.
std::unique_ptr<juce::AudioIODeviceType> makeHangTestType (bool forCheck = false); // (forCheck: the self-test's own)
constexpr const char* hangTestTypeName = "Hang test";

// the time limit (ms); setTimeoutForChecks: another one for this process (0 = back to the setting)
int timeoutMs();
void setTimeoutForChecks (int ms);
// "<type>: <device>" of the newest device that did not answer in this process ("" when none) and a count of them
juce::String lastHung();
int hungCount();
// is that device marked hung (its opens fail at once)? An open asks with `consume` (it uses up a "Try again")
bool isHung (const juce::String& typeName, const juce::String& deviceName, bool consume = false);
// one more bounded attempt for it ("Try again")
void allowAgain (const juce::String& typeName, const juce::String& deviceName);
// the error the caller gets for a hung device
juce::String hungError (const juce::String& deviceName);
// the prewarmed ASIO device did not answer in time (DllAsioDevice.cpp): marked hung, as an open that timed out
void markHungForPrewarm (const juce::String& typeName, const juce::String& deviceName);
// waits for `done` up to `ms`, answering messages sent to this thread's windows meanwhile (Windows); true when signalled
bool waitAnsweringSentMessages (juce::WaitableEvent& done, int ms);
} // namespace lynx::deviceguard
