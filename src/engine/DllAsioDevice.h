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

// Round 190go (start-up): when `savedSetup` (audio-device.xml) names a device of JUCE's "ASIO" type, that device is
// made and opened on a worker thread at once, while the main window is built (making and opening "FL Studio ASIO" took
// ~0.8 s of the start-up's critical path). The driver itself is called on JUCE's ASIO thread only (JUCE patch 27 in
// cmake/LynxPatchJuce.cmake: one thread makes, uses and releases it, as COM wants). The engine's device manager takes
// the device when it opens its audio device (LynxDeviceManager). Returns at once; nothing elsewhere, for another
// type, or with LYNX_ASIO_THREAD=0.
void prewarmAsioDevice (const juce::File& savedSetup);

// For the start-up trace: what the prewarm gave ("" when there was none)
juce::String asioPrewarmReport();

// The device manager of the engine: JUCE's, whose ASIO type hands over the device prewarmAsioDevice made (its first
// scan is the prewarm's too) and otherwise does what JUCE's does.
class LynxDeviceManager final : public juce::AudioDeviceManager
{
public:
    void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override;
};
} // namespace lynx
