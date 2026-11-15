#include "ui/ControllerBridge.h"

#include <algorithm>
#include <cmath>

#include "core/ClipOps.h"
#include "core/ModelParams.h"
#include "core/ProjectContents.h"

namespace lynx
{
using controller::Command;
using controller::Param;
using controller::Switch;

ControllerBridge::ControllerBridge (AudioEngine& e, Window w) : engine (e), window (std::move (w)) {}

// --- values ---------------------------------------------------------------------------------------------------------
float ControllerBridge::value (const AutomationTarget& t) const
{
    for (auto it = pending.rbegin(); it != pending.rend(); ++it)
        if (it->first == t)
            return it->second; // (set earlier in this batch)
    if (t.kind == AutomationTargetKind::channelParam)
        if (auto* plugin = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (t.index).get()))
        {
            const auto& params = plugin->getInstance().getParameters();
            return juce::isPositiveAndBelow (t.param, params.size()) ? params[t.param]->getValue() : 0.0f;
        }
    return juce::jlimit (0.0f, 1.0f, engine.getSong().targetValue (t));
}

void ControllerBridge::set (const AutomationTarget& t, float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    const auto it = std::find_if (pending.begin(), pending.end(), [&] (const auto& p) { return p.first == t; });
    if (it != pending.end())
        it->second = v;
    else
        pending.push_back ({ t, v });
    if (batch == 0)
        flush();
}

void ControllerBridge::flush()
{
    if (pending.empty())
        return;
    for (const auto& [target, v] : pending)
        engine.noteTweaked (target); // (Last tweaked, automation recording: as a linked controller's move)
    engine.applyTargetValues (pending);
    pending.clear();
}

void ControllerBridge::beginBatch() { ++batch; }

void ControllerBridge::endBatch()
{
    if (batch > 0 && --batch == 0)
        flush();
}

// --- transport --------------------------------------------------------------------------------------------------------
void ControllerBridge::command (Command c)
{
    flush(); // (an action reads the song)
    const auto& transport = engine.getTransport();
    const auto busy = transport.playing.load() || engine.isWaitingForInput() || engine.isCountingIn();
    auto run = [this] (const char* action) {
        if (window.runAction)
            window.runAction (action);
    };
    auto bar = [this] (bool forward) {
        // to the previous / next bar line (from inside a bar, back goes to its start)
        const auto ticks = (double) engine.getSong().barTicks();
        const auto at = engine.getTransport().positionTicks.load() / ticks;
        const auto target = forward ? std::floor (at + 1e-6) + 1.0 : std::ceil (at - 1e-6) - 1.0;
        engine.seek (juce::jmax (0.0, target) * ticks);
    };
    switch (c)
    {
        case Command::play: run (transport.playing.load() ? "transport.pause" : "transport.playStop"); break;
        case Command::stop:
            if (busy)
                run ("transport.playStop"); // stops and goes back to where playback started
            else
                engine.seek (0.0);
            break;
        case Command::record: run ("transport.record"); break;
        case Command::loop: run ("transport.toggleMode"); break;
        case Command::metronome: run ("transport.metronome"); break;
        case Command::tapTempo:
            if (const auto bpm = taps.tap (juce::Time::getMillisecondCounterHiRes()))
                if (std::abs (*bpm - engine.getSong().bpm) > 1e-9)
                    engine.editSong ([b = *bpm] (Song& s) { clipops::followTempo (s, b); });
            break;
        case Command::rewind: bar (false); break;
        case Command::forward: bar (true); break;
        case Command::undo: run ("edit.undo"); break;
        case Command::previousPattern: run ("pattern.previous"); break;
        case Command::nextPattern: run ("pattern.next"); break;
        case Command::count: break;
    }
}

bool ControllerBridge::lit (Command c) const
{
    const auto& transport = engine.getTransport();
    switch (c)
    {
        case Command::play: return transport.playing.load();
        case Command::stop: return ! transport.playing.load();
        case Command::record: return transport.recording.load();
        case Command::loop: return engine.getSong().mode == PlayMode::pattern;
        case Command::metronome: return transport.metronome.load();
        default: return false;
    }
}

void ControllerBridge::jog (int steps)
{
    engine.seek (juce::jmax (0.0, engine.getTransport().positionTicks.load() + (double) steps * ppq));
}

// --- mixer ------------------------------------------------------------------------------------------------------------
int ControllerBridge::mixerTracks() const { return (int) engine.getSong().mixer.size(); }
int ControllerBridge::selectedMixerTrack() const { return window.selectedMixerTrack ? window.selectedMixerTrack() : -1; }

void ControllerBridge::selectMixerTrack (int track)
{
    if (window.selectMixerTrack && track >= 0 && track < mixerTracks())
        window.selectMixerTrack (track);
}

std::string ControllerBridge::mixerName (int track) const { return projectcontents::insertName (engine.getSong(), track); }

float ControllerBridge::mixerValue (int track, Param p) const
{
    return value ({ p == Param::volume ? AutomationTargetKind::mixerVolume : AutomationTargetKind::mixerPan, track, 0, 0 });
}

void ControllerBridge::setMixerValue (int track, Param p, float v)
{
    if (track >= 0 && track < mixerTracks())
        set ({ p == Param::volume ? AutomationTargetKind::mixerVolume : AutomationTargetKind::mixerPan, track, 0, 0 }, v);
}

bool ControllerBridge::mixerSwitch (int track, Switch s) const
{
    const auto& song = engine.getSong();
    if (track < 0 || track >= (int) song.mixer.size())
        return false;
    switch (s)
    {
        case Switch::mute: return song.mixer[(std::size_t) track].muted;
        case Switch::solo: return song.mixer[(std::size_t) track].solo;
        case Switch::arm: return engine.isInsertArmed (track);
    }
    return false;
}

void ControllerBridge::setMixerSwitch (int track, Switch s, bool on)
{
    if (track < 0 || track >= mixerTracks())
        return;
    flush();
    switch (s)
    {
        case Switch::mute: engine.editSong ([track, on] (Song& song) { song.mixer[(std::size_t) track].muted = on; }); break;
        case Switch::solo: engine.editSong ([track, on] (Song& song) { song.mixer[(std::size_t) track].solo = on; }); break;
        case Switch::arm:
            if (! on || ! engine.getSong().mixer[(std::size_t) track].recordSafe) // (a record-safe insert is not armed)
                engine.setInsertArmed (track, on);
            break;
    }
}

// --- Channel Rack -----------------------------------------------------------------------------------------------------
int ControllerBridge::channels() const { return (int) engine.getSong().channels.size(); }
int ControllerBridge::selectedChannel() const { return engine.getSelectedChannel(); }

void ControllerBridge::selectChannel (int channel)
{
    if (channel >= 0 && channel < channels())
        engine.setSelectedChannel (channel);
}

std::string ControllerBridge::channelName (int channel) const
{
    const auto& song = engine.getSong();
    return channel >= 0 && channel < (int) song.channels.size() ? song.channels[(std::size_t) channel].name : std::string();
}

float ControllerBridge::channelValue (int channel, Param p) const
{
    return value ({ p == Param::volume ? AutomationTargetKind::channelVolume : AutomationTargetKind::channelPan, channel, 0, 0 });
}

void ControllerBridge::setChannelValue (int channel, Param p, float v)
{
    if (channel >= 0 && channel < channels())
        set ({ p == Param::volume ? AutomationTargetKind::channelVolume : AutomationTargetKind::channelPan, channel, 0, 0 }, v);
}

bool ControllerBridge::channelMuted (int channel) const
{
    const auto& song = engine.getSong();
    return channel >= 0 && channel < (int) song.channels.size() && song.channels[(std::size_t) channel].muted;
}

void ControllerBridge::setChannelMuted (int channel, bool muted)
{
    if (channel < 0 || channel >= channels())
        return;
    flush();
    engine.editSong ([channel, muted] (Song& song) { song.channels[(std::size_t) channel].muted = muted; });
}

// --- the focused plugin: the selected channel's instrument ---------------------------------------------------------------
std::string ControllerBridge::pluginName() const
{
    const auto channel = engine.getSelectedChannel();
    if (auto* plugin = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
        return plugin->getInstance().getName().toStdString();
    return channelName (channel);
}

int ControllerBridge::pluginParams() const
{
    const auto& song = engine.getSong();
    const auto channel = engine.getSelectedChannel();
    if (channel < 0 || channel >= (int) song.channels.size())
        return 0;
    if (auto* plugin = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
        return plugin->getInstance().getParameters().size();
    if (const auto* table = modelparams::tableOf (song.channels[(std::size_t) channel].instrument); table != nullptr && table->params != nullptr)
        return (int) table->params->size();
    return 0;
}

AutomationTarget ControllerBridge::paramTarget (int index) const
{
    const auto& song = engine.getSong();
    const auto channel = engine.getSelectedChannel();
    if (index < 0 || index >= pluginParams())
        return {};
    if (dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()) != nullptr)
        return { AutomationTargetKind::channelParam, channel, 0, index };
    const auto* table = modelparams::tableOf (song.channels[(std::size_t) channel].instrument);
    return { AutomationTargetKind::channelParam, channel, 0, (*table->params)[(std::size_t) index].index };
}

std::string ControllerBridge::pluginParamName (int index) const
{
    const auto& song = engine.getSong();
    const auto channel = engine.getSelectedChannel();
    if (index < 0 || index >= pluginParams())
        return {};
    if (auto* plugin = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
        return plugin->getInstance().getParameters()[index]->getName (32).toStdString();
    return (*modelparams::tableOf (song.channels[(std::size_t) channel].instrument)->params)[(std::size_t) index].name;
}

float ControllerBridge::pluginParam (int index) const
{
    const auto t = paramTarget (index);
    return t.kind == AutomationTargetKind::none ? 0.0f : value (t);
}

void ControllerBridge::setPluginParam (int index, float v)
{
    if (const auto t = paramTarget (index); t.kind != AutomationTargetKind::none)
        set (t, v);
}

// --- pads -------------------------------------------------------------------------------------------------------------
void ControllerBridge::playNote (int key, float velocity, bool on)
{
    // as a key of the typing keyboard or a note from a device without a profile: into the selected channels
    engine.injectMidiInput (on ? juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, key), juce::jlimit (1.0f / 127.0f, 1.0f, velocity))
                               : juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, key)));
}

int ControllerBridge::steps() const { return (int) juce::jmax<std::int64_t> (1, engine.getSong().barTicks() / ticksPerStep); }

bool ControllerBridge::step (int index) const
{
    const auto& song = engine.getSong();
    const auto channel = engine.getSelectedChannel();
    if (song.currentPattern < 0 || song.currentPattern >= (int) song.patterns.size() || channel < 0 || channel >= (int) song.channels.size())
        return false;
    return song.activePattern().hasStep (channel, index);
}

void ControllerBridge::setStep (int index, bool on)
{
    if (index < 0 || index >= steps() || step (index) == on)
        return;
    const auto channel = engine.getSelectedChannel();
    if (channel < 0 || channel >= channels())
        return;
    flush();
    engine.editSong ([channel, index] (Song& song) {
        if (song.currentPattern >= 0 && song.currentPattern < (int) song.patterns.size())
            song.activePattern().toggleStep (channel, index);
    });
}

bool ControllerBridge::linkedByHand (int cc, int channel) const
{
    for (const auto& link : linksWithGlobal (engine.getSong().midiLinks, engine.getGlobalLinks()))
        if (link.cc == cc && (link.channel < 0 || link.channel == channel))
            return true;
    return false;
}
} // namespace lynx
