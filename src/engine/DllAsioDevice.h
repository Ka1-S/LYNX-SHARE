#pragma once
// An ASIO driver loaded straight from its DLL (no registry): LYNX's own lynx-asio.dll, or any ASIO driver DLL for a
// comparison. Hidden test mode `--asiodll <path>[|{clsid}]` (REA round 97); the device type is named
// "ASIO driver file".
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

namespace lynx
{
// The type name the device manager lists for it.
inline constexpr const char* dllAsioTypeName = "ASIO driver file";

// The built-in type (round 106): LYNX ASIO loaded from lynx-asio.dll next to LYNX.exe when it is not registered
// (registered, it is listed under JUCE's "ASIO" type like any driver). One device, also named "LYNX ASIO".
inline constexpr const char* lynxAsioTypeName = "LYNX ASIO";

// nullptr when not on Windows. `spec` = "C:\\path\\driver.dll" or "C:\\path\\driver.dll|{CLSID}"; without a CLSID
// LYNX ASIO's own class id is used. `typeName` / `deviceName`: empty = "ASIO driver file" / the DLL's file name.
std::unique_ptr<juce::AudioIODeviceType> makeDllAsioDeviceType (const juce::String& spec, const juce::String& typeName = {},
                                                                const juce::String& deviceName = {});

// The built-in LYNX ASIO type when lynx-asio.dll sits next to LYNX.exe and LYNX ASIO is not registered; else nullptr.
std::unique_ptr<juce::AudioIODeviceType> makeBuiltInLynxAsioType();

// Round 190go: when `savedSetup` (audio-device.xml) names a device of JUCE's "ASIO" type, that driver's DLL (its
// registered InprocServer32) is loaded on a thread of its own and kept loaded. Opening the device on the message
// thread then finds the module loaded: its load (DllMain, the driver's own signature check: ~0.35 s for FL Studio
// ASIO) is off the start-up's path. Only LoadLibrary: no driver object is made and no COM call is made on that
// thread. Returns at once; does nothing elsewhere, for another type or when the driver is not registered.
void preloadAsioDriver (const juce::File& savedSetup);
} // namespace lynx
