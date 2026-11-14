#pragma once

// The devices side of the MIDI controller layer (round 190aw; core/ControllerSession.h): which input devices run a
// controller profile, the quick test on the MIDI thread that takes a profile's messages out of the normal path, the
// queue to the message thread, and the output ports the feedback goes to.

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

#include "core/ControllerSession.h"
#include "core/LiveEventQueue.h"
#include "core/Preferences.h"

namespace lynx
{
class ControllerIo
{
public:
    ControllerIo();
    ~ControllerIo();

    // --- message thread ---
    // LYNX as the controllers see it (the main window's bridge); null closes every session.
    void setSurface (controller::Surface* surface);
    // The built-in profiles plus the user's folder (read again on each call).
    void loadProfiles (const juce::File& userFolder);
    const controller::Library& library() const noexcept { return profiles; }
    // The input devices now and their settings. `openOutputs`: open the ports for the feedback (the app; not tests).
    void configure (const prefs::MidiPrefs& prefs, const std::vector<std::string>& inputNames, bool openOutputs);
    // The MIDI device list changed (round 190gh, hot-plug): a feedback port whose device is no longer listed under its
    // identifier is closed and looked for again by name.
    void devicesChanged() { apply (false); }
    // The profile a device runs (null: a generic controller), and the output port its feedback goes to ("" = none).
    std::shared_ptr<const controller::Profile> profileOf (const std::string& input) const;
    std::string outputOf (const std::string& input) const;
    // Each timer tick: the queued messages into the sessions, then what changed back to the devices. `learning`:
    // a MIDI learn was waiting this tick, so controller moves made the link and are not also run by the profile.
    void pump (bool learning);
    // Instead of the output ports (tests, --snapshot).
    std::function<void (const std::string& input, const std::uint8_t* bytes, std::size_t size)> onSend;

    // --- MIDI thread ---
    // True when the device's profile takes the message (it is queued for the message thread and must not play).
    // `linkedCcs`: the controllers linked by hand (bit per CC number), which the profile never takes.
    bool take (const std::string& device, const std::uint8_t* bytes, int size, const std::array<std::atomic<std::uint64_t>, 2>& linkedCcs) noexcept;

private:
    struct Event
    {
        std::uint32_t generation = 0;
        std::uint8_t device = 0, size = 0;
        std::uint8_t bytes[3] {};
    };
    struct Claimed
    {
        std::string input;
        controller::Claims claims;
    };
    struct Port
    {
        std::string name;
        std::string id; // the opened device's identifier (round 190gh, hot-plug)
        std::unique_ptr<juce::MidiOutput> device;
    };

    void send (const std::string& input, const std::uint8_t* bytes, std::size_t size);
    void apply (bool devicesChanged);
    void closeSessions();

    controller::Surface* surface = nullptr;
    controller::Library profiles;
    controller::Hub hub;
    std::vector<controller::Hub::Device> wanted; // the devices with a profile, in the list's order
    bool wantOutputs = false;
    std::map<std::string, Port> ports;           // input name -> its output port

    juce::SpinLock claimLock;                    // guards `claimed` and `generation` (message thread writes, MIDI thread reads)
    std::vector<Claimed> claimed;
    std::uint32_t generation = 0;
    std::atomic<bool> anyClaimed { false };
    SpscQueue<Event, 2048> queue;                // MIDI thread -> message thread
};
} // namespace lynx
