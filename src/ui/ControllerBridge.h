#pragma once

// LYNX as a MIDI controller profile sees it (round 190aw): the app's side of controller::Surface
// (core/ControllerSession.h). Values go through the same path as MIDI links (AudioEngine::applyTargetValues), buttons
// through the main window's actions, so a controller does exactly what the mouse and the shortcuts do.

#include <functional>
#include <string>
#include <vector>

#include "core/ControllerSession.h"
#include "core/ValueMath.h"
#include "engine/AudioEngine.h"

namespace lynx
{
class ControllerBridge final : public controller::Surface
{
public:
    // What the bridge needs from the main window.
    struct Window
    {
        std::function<bool (const std::string& action)> runAction;
        std::function<int()> selectedMixerTrack;
        std::function<void (int)> selectMixerTrack;
    };

    ControllerBridge (AudioEngine& engine, Window window);

    void command (controller::Command c) override;
    bool lit (controller::Command c) const override;
    void jog (int steps) override;

    int mixerTracks() const override;
    int selectedMixerTrack() const override;
    void selectMixerTrack (int track) override;
    std::string mixerName (int track) const override;
    float mixerValue (int track, controller::Param p) const override;
    void setMixerValue (int track, controller::Param p, float value) override;
    bool mixerSwitch (int track, controller::Switch s) const override;
    void setMixerSwitch (int track, controller::Switch s, bool on) override;

    int channels() const override;
    int selectedChannel() const override;
    void selectChannel (int channel) override;
    std::string channelName (int channel) const override;
    float channelValue (int channel, controller::Param p) const override;
    void setChannelValue (int channel, controller::Param p, float value) override;
    bool channelMuted (int channel) const override;
    void setChannelMuted (int channel, bool muted) override;

    std::string pluginName() const override;
    int pluginParams() const override;
    std::string pluginParamName (int index) const override;
    float pluginParam (int index) const override;
    void setPluginParam (int index, float value) override;

    void playNote (int key, float velocity, bool on) override;
    int steps() const override;
    bool step (int index) const override;
    void setStep (int index, bool on) override;

    bool linkedByHand (int cc, int channel) const override;
    void beginBatch() override;
    void endBatch() override;

private:
    // the selected channel's parameter `index` as an automation target (kind none: there is none)
    AutomationTarget paramTarget (int index) const;
    float value (const AutomationTarget& t) const;
    void set (const AutomationTarget& t, float v);
    void flush();

    AudioEngine& engine;
    Window window;
    std::vector<std::pair<AutomationTarget, float>> pending; // values set since beginBatch
    int batch = 0;
    controls::TapTempo taps;
};
} // namespace lynx
