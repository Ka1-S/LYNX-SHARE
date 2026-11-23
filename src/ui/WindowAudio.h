#pragma once

#include <functional>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
}

// Issue #232, stage 2 (round 190nz, docs/design/PROJECT_TABS.md; a LYNX extra): an audio driver that only one program
// may use at a time (ASIO, an exclusive-mode device) plays in one LYNX SHARE window. The other window says so in a bar
// under its toolbar, with "Make this window the one that plays": the window that has the device stops, lets it go and
// shows the same bar; this one opens it. The device saved in Settings › Audio stays the one both windows want (the
// window without it never saves its stand-in device over it). Messages go through ui/LynxWindows.h.
namespace lynx::windowaudio
{
// The bar (LYNX's look, as the update bar: raised surface, accent edge, the action in the accent).
class Bar final : public juce::Component
{
public:
    Bar();
    static constexpr int height = 30;
    std::function<void()> onPlayHere;
    void setText (const juce::String& text);
    juce::String getText() const { return message.getText(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label message;
    juce::TextButton playHere;
};

// The device setup a window wants: type and output device name (from LYNX's audio-device.xml / JUCE's state XML).
struct Device
{
    juce::String type, name;
    bool empty() const noexcept { return type.isEmpty() && name.isEmpty(); }
    bool operator== (const Device& o) const noexcept { return type == o.type && name == o.name; }
};
Device deviceOf (const juce::XmlElement* setup);
// What this window's device manager has open now (empty when none is open).
Device openDevice (juce::AudioDeviceManager& devices);

// The logic, one per main window.
class Link final : private juce::ChangeListener
{
public:
    // `relayout`: the bar was shown or hidden. `stopPlaying`: the transport stops before the device goes.
    Link (AudioEngine& engine, Bar& bar, std::function<void()> relayout, std::function<void()> stopPlaying);
    ~Link() override;

    // a message of another window (LynxWindows): release-audio, audio-released, audio-busy, hello / changed / bye
    void received (const juce::String& verb, int from);
    // the bar's button: asks the window that has the device for it
    void playHere();
    // checks the other windows again (one of them started, changed or went away)
    void refresh();

    // the window that has the device this one wants (0: none, or this one has it)
    int holder() const noexcept { return holderPid; }
    bool gaveAway() const noexcept { return gave; }

    // --tabs2shot190nz: a holder and a wanted device without any device or other window
    void showForSnapshot (int pid, const Device& wantedDevice, bool gaveItAway);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void publish();       // this window's device into its entry
    void evaluate();      // bar shown / hidden, the saved setup kept
    void release (int to);
    void reopen();
    void keepSavedSetup();

    AudioEngine& engine;
    Bar& bar;
    std::function<void()> relayout, stopPlaying;
    std::unique_ptr<juce::XmlElement> wanted; // the setup saved when this window started (what it opens when it plays)
    int holderPid = 0;
    bool gave = false;        // this window let another one have the device
    bool savingOff = false;   // this window no longer writes audio-device.xml by itself (restoreAudioDeviceState)
    int asked = 0;            // the window asked for the device (waiting for its answer)
};
} // namespace lynx::windowaudio
