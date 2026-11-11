#pragma once

// The MIDI controller layer (round 190aw, phase A of REA_rodada190as §3.3): what a controller profile
// (core/ControllerProfile.h) drives, and the code that runs a profile against it.
//
//   device bytes ─► Hub::input ─► Session ─► Surface (transport, mixer bank, Channel Rack, plugin page, pads)
//   device bytes ◄─ Hub::refresh ◄─ Session ◄─ Surface (LEDs, motor faders, display text; only what changed)
//
// The Surface is LYNX seen from a controller; the app implements it (ui/ControllerBridge), tests use a fake one. The
// later phases (the local control API of issue #75, FL-style device scripts) sit on the same Surface.
//
// Links made by hand come first: a controller the user linked with "Link to MIDI controller" is not taken by the
// profile (Surface::linkedByHand), and a device without a profile ("Generic controller") is not touched at all.
// No JUCE.

#include <array>
#include <bitset>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/ControllerProfile.h"
#include "core/Preferences.h"

namespace lynx::controller
{
enum class Param : std::uint8_t { volume, pan };
enum class Switch : std::uint8_t { mute, solo, arm };

// LYNX as a controller sees it. Values are 0…1 as automation and MIDI links use them (a pan's centre is 0.5).
// Mixer track 0 is the Master, 1… are the inserts. Message thread only.
class Surface
{
public:
    virtual ~Surface() = default;

    // transport and other buttons
    virtual void command (Command c) = 0;
    virtual bool lit (Command c) const = 0; // play: playing, stop: stopped, record, loop: pattern mode, metronome
    virtual void jog (int steps) = 0;       // the song position, in beats

    // mixer
    virtual int mixerTracks() const = 0;
    virtual int selectedMixerTrack() const = 0;
    virtual void selectMixerTrack (int track) = 0;
    virtual std::string mixerName (int track) const = 0;
    virtual float mixerValue (int track, Param p) const = 0;
    virtual void setMixerValue (int track, Param p, float value) = 0;
    virtual bool mixerSwitch (int track, Switch s) const = 0;
    virtual void setMixerSwitch (int track, Switch s, bool on) = 0;

    // Channel Rack
    virtual int channels() const = 0;
    virtual int selectedChannel() const = 0;
    virtual void selectChannel (int channel) = 0;
    virtual std::string channelName (int channel) const = 0;
    virtual float channelValue (int channel, Param p) const = 0;
    virtual void setChannelValue (int channel, Param p, float value) = 0;
    virtual bool channelMuted (int channel) const = 0;
    virtual void setChannelMuted (int channel, bool muted) = 0;

    // the focused plugin (the selected channel's instrument) and its parameters
    virtual std::string pluginName() const = 0;
    virtual int pluginParams() const = 0;
    virtual std::string pluginParamName (int index) const = 0;
    virtual float pluginParam (int index) const = 0;
    virtual void setPluginParam (int index, float value) = 0;

    // pads: notes into the selected channels, and the selected channel's steps in the current pattern
    virtual void playNote (int key, float velocity, bool on) = 0;
    virtual int steps() const = 0;
    virtual bool step (int index) const = 0;
    virtual void setStep (int index, bool on) = 0;

    // a controller the user linked by hand (project or global link): the profile leaves it alone
    virtual bool linkedByHand (int cc, int channel) const = 0;

    // around the messages of one timer tick (the app applies the values set in between as one edit)
    virtual void beginBatch() {}
    virtual void endBatch() {}
};

// What the devices of one group share (a Mackie Control and its extenders move as one bank).
struct Shared
{
    int mixerBank = 0;   // strip 0 of the first unit is insert 1 + mixerBank
    int channelBank = 0;
    int page = 0;        // the plugin parameter page
    int layer = 0;
    int strips = 8;      // all units together
};

// The messages a profile takes, for a quick test on the MIDI thread (every layer's lines).
struct Claims
{
    std::array<std::bitset<128>, 16> notes {}, ccs {};
    std::bitset<16> bend {};

    explicit Claims (const Profile& profile);
    Claims() = default;
    // A short message (status + data). Notes off count as their note. False for anything else.
    bool has (const std::uint8_t* bytes, std::size_t size) const noexcept;
    // The same, leaving out the controllers linked by hand: `linkedCcs` has a bit per CC number (0…63, 64…127) that a
    // project or global MIDI link uses.
    bool takes (const std::uint8_t* bytes, std::size_t size, std::uint64_t linkedLow, std::uint64_t linkedHigh) const noexcept;
};

using Send = std::function<void (const std::uint8_t* bytes, std::size_t size)>;

// One device running one profile.
class Session
{
public:
    Session (std::shared_ptr<const Profile> profile, int unitOffset);

    const Profile& profile() const noexcept { return *prof; }
    int unitOffset() const noexcept { return offset; }

    // The device opened: the profile's init messages, then everything the device shows.
    void open (const Surface& surface, const Shared& shared, const Send& send);
    // The device closes: LEDs off, display cleared, the profile's exit messages.
    void close (const Send& send);

    // A message from the device. Returns false when the profile has nothing for it (it then plays as before).
    bool input (const std::uint8_t* bytes, std::size_t size, Surface& surface, Shared& shared);
    // Sends what changed since the last call.
    void refresh (const Surface& surface, const Shared& shared, const Send& send);
    // The bank, page or layer moved: absolute controls have to reach their new targets' values again.
    void targetsMoved() noexcept;

private:
    struct Target;
    int mixerTrack (int strip, const Surface& surface, const Shared& shared) const noexcept;
    int channelIndex (int strip, const Surface& surface, const Shared& shared) const noexcept;
    int paramIndex (int strip, const Surface& surface, const Shared& shared) const noexcept;
    void apply (const Binding& b, int element, int value14, bool is14, Surface& surface, Shared& shared);
    int feedbackValue (const FeedbackLine& f, int element, const Surface& surface, const Shared& shared) const;
    std::string displayText (const DisplayRow& row, int cell, const Surface& surface, const Shared& shared) const;
    void sendValue (const FeedbackLine& f, int element, int value, const Send& send) const;
    void invalidateMotor (Feedback what, int strip) noexcept;

    std::shared_ptr<const Profile> prof;
    int offset = 0;
    std::vector<std::vector<prefs::PickupState>> pickups; // [binding][element]
    std::vector<std::vector<int>> sent;                   // [feedback line][element], −1 = nothing sent yet
    std::map<std::pair<int, int>, std::string> shown;     // (row, cell) -> the text on the display
    std::bitset<64> touched;                              // mixer strips whose fader is held
    bool masterTouched = false;
    bool opened = false;
};

// Every connected device with a profile.
class Hub
{
public:
    struct Device
    {
        std::string input;  // the input port's name (the session's key)
        std::shared_ptr<const Profile> profile;
        bool operator== (const Device&) const = default;
    };

    // The devices now (in the Settings list's order). Sessions of devices that stay are kept; units of one group take
    // their strips in this order. Devices without a profile are left out. `send` gets the closing messages of the
    // sessions that go and the opening ones of the new.
    using DeviceSend = std::function<void (const std::string& input, const std::uint8_t* bytes, std::size_t size)>;
    void configure (const std::vector<Device>& devices, const Surface& surface, const DeviceSend& send);
    void closeAll (const DeviceSend& send);

    bool has (const std::string& input) const noexcept;
    const Session* session (const std::string& input) const noexcept;
    bool input (const std::string& device, const std::uint8_t* bytes, std::size_t size, Surface& surface);
    void refresh (const Surface& surface, const DeviceSend& send);
    const Shared* shared (const std::string& group) const noexcept;

private:
    struct Entry
    {
        std::string input, group;
        std::unique_ptr<Session> session;
    };
    std::vector<Entry> entries;
    std::map<std::string, Shared> groups; // (a device without a group: its own, keyed by the input name)
};

// Display helpers (also used by tests): a value as a controller's small display shows it.
std::string volumeText (float value01);    // "80%"
std::string panText (float value01);       // "C", "L50", "R100"
// A text cut and padded to `width` plain ASCII characters (accented Latin letters lose their accent, others become '?').
std::string displayAscii (const std::string& utf8, int width);
} // namespace lynx::controller
