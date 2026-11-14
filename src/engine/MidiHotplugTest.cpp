// Round 190gh (B8, community complaint #10: "FL crashes when a MIDI device is plugged or unplugged", "devices are only
// seen after a restart"): MIDI devices plugged in and out while LYNX SHARE runs.
//
// AudioEngine::midiHotplugSelfTest() replaces the system's device lists with stand-in ones (midiDeviceListOverride)
// and runs the engine's device check the way the system's notification and the 2-second check do, while another
// thread keeps delivering MIDI messages as an input's callback would. A keyboard and its output, a pad controller that
// is unplugged and plugged back into another USB port (another identifier), and a device that appears later. After
// each step: the device-list version counts up only when something changed, the hint gets the right names, the
// start-up list gives no hint, and nothing crashes while messages arrive. (The stand-in devices cannot be opened: the
// opening and closing decisions themselves are covered by the core tests, tests/Small190ghTests.inc.)

#include <thread>

#include "engine/AudioEngine.h"

namespace lynx
{
juce::String AudioEngine::midiHotplugSelfTest()
{
    struct Device
    {
        juce::String name, id;
        bool input = true, output = false;
    };
    std::vector<Device> devices;
    auto lists = [&devices] (bool inputs) {
        juce::Array<juce::MidiDeviceInfo> out;
        for (const auto& d : devices)
            if (inputs ? d.input : d.output)
                out.add ({ d.name, (inputs ? "in-" : "out-") + d.id });
        return out;
    };
    midiDeviceListOverride() = lists;
    struct Restore
    {
        ~Restore() { midiDeviceListOverride() = nullptr; }
    } restore;

    AudioEngine engine (false);
    juce::StringArray lastAdded, lastRemoved;
    int hints = 0;
    engine.onMidiDevicesChanged = [&] (const juce::StringArray& added, const juce::StringArray& removed) {
        lastAdded = added;
        lastRemoved = removed;
        ++hints;
    };

    // messages keep arriving from "the MIDI thread" through every change (no device: the generic path)
    std::atomic<bool> stop { false };
    std::atomic<int> delivered { 0 };
    std::thread feeder ([&engine, &stop, &delivered] {
        int k = 0;
        while (! stop.load())
        {
            const auto note = 48 + (k++ % 24);
            engine.handleIncomingMidiMessage (nullptr, juce::MidiMessage::noteOn (1, note, (juce::uint8) 90));
            engine.handleIncomingMidiMessage (nullptr, juce::MidiMessage::noteOff (1, note));
            engine.handleIncomingMidiMessage (nullptr, juce::MidiMessage::controllerEvent (1, 1, k & 127));
            delivered.fetch_add (3);
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    });

    juce::StringArray errors;
    auto step = [&] (const char* what, int expectVersion, int expectHints, const juce::StringArray& added, const juce::StringArray& removed) {
        for (int i = 0; i < 3; ++i) // (the notification and the 2-second check may both come: the second finds nothing new)
            engine.enableAllMidiInputs();
        if (engine.getMidiDeviceListVersion() != expectVersion)
            errors.add (juce::String (what) + ": version " + juce::String (engine.getMidiDeviceListVersion()) + ", expected " + juce::String (expectVersion));
        if (hints != expectHints)
            errors.add (juce::String (what) + ": " + juce::String (hints) + " hints, expected " + juce::String (expectHints));
        else if (expectHints > 0 && (lastAdded != added || lastRemoved != removed))
            errors.add (juce::String (what) + ": hint +" + lastAdded.joinIntoString (",") + " -" + lastRemoved.joinIntoString (","));
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    };

    devices = { { "Keystation 49", "usb1", true, true }, { "Pads", "usb2", true, false } };
    step ("start-up", 0, 0, {}, {});                                      // the first list: no hint
    devices.erase (devices.begin() + 1);
    step ("pads unplugged", 1, 1, {}, { "Pads" });
    devices.push_back ({ "Pads", "usb3", true, false });
    step ("pads back in another port", 2, 2, { "Pads" }, {});
    devices[0].id = "usb4";
    step ("keyboard moved to another port", 3, 2, {}, {});                // (opened again; no hint: nothing new)
    devices.push_back ({ "Drum pad", "usb5", true, true });
    devices.erase (devices.begin());
    step ("one in, one out", 4, 3, { "Drum pad" }, { "Keystation 49" });
    devices.clear();
    step ("everything unplugged", 5, 4, {}, { "Pads", "Drum pad" });
    step ("still nothing", 5, 4, {}, { "Pads", "Drum pad" });

    stop = true;
    feeder.join();
    engine.onMidiDevicesChanged = nullptr;
    if (delivered.load() == 0)
        errors.add ("no MIDI message was delivered");
    if (! errors.isEmpty())
        return "ERROR " + errors.joinIntoString ("; ");
    return "OK (6 changes, " + juce::String (delivered.load()) + " messages during them)";
}
} // namespace lynx
