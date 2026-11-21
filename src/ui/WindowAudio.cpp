#include "ui/WindowAudio.h"

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "platform/Paths.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/LynxWindows.h"
#include "ui/Theme.h"

namespace lynx::windowaudio
{
namespace
{
juce::String shown (const Device& d)
{
    if (d.name.isEmpty())
        return d.type;
    return d.type.isEmpty() ? d.name : d.type + ": " + d.name;
}

// the saved setup as JUCE reads it back, without the MIDI parts (the engine opens MIDI itself, as at the start)
std::unique_ptr<juce::XmlElement> withoutMidi (const juce::XmlElement* setup)
{
    if (setup == nullptr)
        return nullptr;
    auto copy = std::make_unique<juce::XmlElement> (*setup);
    copy->removeAttribute ("defaultMidiOutput");
    copy->removeAttribute ("defaultMidiOutputDevice");
    copy->deleteAllChildElementsWithTagName ("MIDIINPUT");
    return copy;
}
} // namespace

Bar::Bar()
{
    message.setFont (theme::font (theme::type::body));
    message.setColour (juce::Label::textColourId, theme::colour::textPrimary);
    message.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (message);
    playHere.setButtonText (tr ("Make this window the one that plays"));
    playHere.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
    playHere.setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
    playHere.onClick = [this] {
        if (onPlayHere != nullptr)
            onPlayHere();
    };
    addAndMakeVisible (playHere);
}

void Bar::setText (const juce::String& text)
{
    message.setText (text, juce::dontSendNotification);
}

void Bar::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour::bg3);
    g.setColour (theme::colour::accent);
    g.fillRect (0, 0, 3, getHeight());
    g.setColour (theme::colour::border);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

void Bar::resized()
{
    auto r = getLocalBounds().reduced (0, 4).withTrimmedLeft (12).withTrimmedRight (8);
    const auto w = juce::jmax (90, playHere.getBestWidthForHeight (r.getHeight()) + 12);
    playHere.setBounds (r.removeFromRight (w));
    r.removeFromRight (8);
    message.setBounds (r);
}

Device deviceOf (const juce::XmlElement* setup)
{
    if (setup == nullptr)
        return {};
    auto name = setup->getStringAttribute ("audioOutputDeviceName");
    if (name.isEmpty())
        name = setup->getStringAttribute ("audioInputDeviceName");
    return { setup->getStringAttribute ("deviceType"), name };
}

Device openDevice (juce::AudioDeviceManager& devices)
{
    auto* device = devices.getCurrentAudioDevice();
    if (device == nullptr)
        return {};
    return { devices.getCurrentAudioDeviceType(), device->getName() };
}

Link::Link (AudioEngine& e, Bar& b, std::function<void()> layout, std::function<void()> stop)
    : engine (e), bar (b), relayout (std::move (layout)), stopPlaying (std::move (stop))
{
    // the setup Settings › Audio saved, read before this window's device opens (the main window is made first)
    wanted = juce::XmlDocument::parse (PluginHost::settingsFolder().getChildFile ("audio-device.xml"));
    bar.onPlayHere = [this] { playHere(); };
    engine.getDeviceManager().addChangeListener (this);
}

Link::~Link()
{
    engine.getDeviceManager().removeChangeListener (this);
}

void Link::changeListenerCallback (juce::ChangeBroadcaster*)
{
    publish();
    evaluate();
    // the window that plays: what it plays through is what it wants from now on (a device chosen in Settings › Audio);
    // one that no longer saves by itself (it once let the device go or waited for it) still keeps it for the next start
    if (holderPid == 0 && ! gave && asked == 0 && ! openDevice (engine.getDeviceManager()).empty())
        if (auto state = engine.getDeviceManager().createStateXml())
        {
            if (savingOff)
                engine.restoreAudioDeviceState (state.get());
            wanted = std::move (state);
        }
}

void Link::publish()
{
    const auto d = openDevice (engine.getDeviceManager());
    windows::update ([&d] (windows::Entry& e) {
        e.deviceType = d.type;
        e.deviceName = d.name;
    });
}

void Link::keepSavedSetup()
{
    if (std::exchange (savingOff, true))
        return;
    // the stand-in device this window opened (or none) never replaces the setup both windows want
    engine.restoreAudioDeviceState (wanted.get());
}

void Link::evaluate()
{
    const auto want = deviceOf (wanted.get());
    const auto now = openDevice (engine.getDeviceManager());
    holderPid = 0;
    juce::String holderProject;
    if (! want.empty() && ! (now == want))
        for (const auto& e : windows::others())
            if (Device { e.deviceType, e.deviceName } == want)
            {
                holderPid = e.pid;
                holderProject = e.project;
                break;
            }
    if (holderPid != 0)
        keepSavedSetup();
    const auto show = holderPid != 0;
    if (show)
        bar.setText (tr ("{0} plays in another LYNX SHARE window ({1}): only one program can use this audio device at a time.",
                         { shown (want), holderProject.isNotEmpty() ? holderProject : tr ("Untitled") }));
    if (bar.isVisible() != show)
    {
        bar.setVisible (show);
        if (relayout != nullptr)
            relayout();
    }
}

void Link::refresh()
{
    evaluate();
    // the window that had the device went away: this one, which let it go, plays again
    if (gave && holderPid == 0 && asked == 0)
    {
        const auto want = deviceOf (wanted.get());
        const auto others = windows::others();
        if (std::none_of (others.begin(), others.end(), [&want] (const windows::Entry& e) { return Device { e.deviceType, e.deviceName } == want; }))
            reopen();
    }
}

void Link::received (const juce::String& verb, int from)
{
    if (verb == "release-audio")
    {
        auto& e = engine;
        if (e.isRecording() || e.isLiveRecording() || e.isBouncing())
        {
            windows::send (from, "audio-busy");
            return;
        }
        release (from);
    }
    else if (verb == "audio-released")
    {
        if (asked == from)
        {
            asked = 0;
            reopen();
        }
    }
    else if (verb == "audio-busy")
    {
        asked = 0;
        ui::postHint ({ tr ("The other window is recording or rendering: try again when it is done.") }, 5000);
    }
    else if (verb == "bye")
        refresh();
    else if (verb == "hello" || verb == "changed")
        evaluate();
}

void Link::playHere()
{
    evaluate();
    if (holderPid == 0)
    {
        reopen(); // (nobody has it now)
        return;
    }
    asked = holderPid;
    if (! windows::send (holderPid, "release-audio"))
    {
        asked = 0;
        refresh();
    }
}

void Link::release (int to)
{
    if (stopPlaying != nullptr)
        stopPlaying();
    if (auto state = engine.getDeviceManager().createStateXml())
        wanted = std::move (state); // (what it plays through now: what it opens again when it plays)
    keepSavedSetup();
    engine.getDeviceManager().closeAudioDevice();
    gave = true;
    publish();
    windows::send (to, "audio-released");
    evaluate();
}

void Link::reopen()
{
    auto& devices = engine.getDeviceManager();
    if (platform::testMode()) // (a test never opens a second audio device)
    {
        gave = false;
        platform::testModeLog ("audio device: would open " + shown (deviceOf (wanted.get())));
        evaluate();
        return;
    }
    const auto setup = withoutMidi (wanted.get());
    const auto error = devices.initialise (0, 2, setup.get(), false);
    gave = false;
    if (error.isNotEmpty() || openDevice (devices).empty())
    {
        ui::postHint ({ tr ("Could not open the audio device {0}", { shown (deviceOf (wanted.get())) }) }, 5000);
        devices.restartLastAudioDevice();
    }
    else if (savingOff)
        if (auto state = devices.createStateXml())
            engine.restoreAudioDeviceState (state.get());
    publish();
    evaluate();
}

void Link::showForSnapshot (int pid, const Device& wantedDevice, bool gaveItAway)
{
    holderPid = pid;
    gave = gaveItAway;
    bar.setText (tr ("{0} plays in another LYNX SHARE window ({1}): only one program can use this audio device at a time.",
                     { shown (wantedDevice), "Album - track 2" }));
    bar.setVisible (pid != 0);
    if (relayout != nullptr)
        relayout();
}
} // namespace lynx::windowaudio
