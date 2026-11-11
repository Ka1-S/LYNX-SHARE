#pragma once

// FL's "Remote control settings" window (form TMIDIInputForm) and its "Global link settings" twin (form
// TMIDIInputForm_Generic), round 190bt (docs/analise/REA_rodada190bt.md): a control's link to a MIDI controller seen
// and edited: port, channel and controller number with Auto detect, Omni, the internal controller, the mapping
// formula with FL's presets and its curve, smoothing, Remove conflicts, Reset / Skip / Accept. One-step learning
// (the control menu's "Link to MIDI controller") stays the quick way; this is the detailed one.

#include "engine/AudioEngine.h"

namespace lynx
{
// Opens the window for these controls' project links (global: their links for every project, kept in the settings).
// With several controls ("Link to controllers..."), Accept and Skip step through them.
void openRemoteControlSettings (AudioEngine& engine, std::vector<AutomationTarget> targets, bool global = false);
void closeRemoteControlSettings();

// Just the panel (for --linkshot and the tests' offscreen checks). `close` is called when the last control was
// accepted or skipped.
std::unique_ptr<juce::Component> makeRemoteControlPanel (AudioEngine& engine, std::vector<AutomationTarget> targets, bool global,
                                                         std::function<void()> close = {});
// Offscreen drivers of that panel (--linkshot, selftest): what a user would do.
void remoteControlPanelSetFormula (juce::Component& panel, const juce::String& formula);
void remoteControlPanelSetController (juce::Component& panel, int port, int channel, int cc);
void remoteControlPanelSetSmoothing (juce::Component& panel, bool on, int position);
void remoteControlPanelPoll (juce::Component& panel); // one Auto detect check, as the panel's timer does
void remoteControlPanelPress (juce::Component& panel, int button); // 0 Reset, 1 Skip, 2 Accept
juce::String remoteControlPanelState (juce::Component& panel);     // "port|channel|cc|formula|message|conflicts|parameter"
bool remoteControlPanelFinished (juce::Component& panel);           // the last control was accepted or skipped
// FL's MLWheelMenu items that act on the controls queued by "Multilink to controllers" (round 190bt): Automation >
// Init song with these positions, Create automation clips; Remote control > Link to controllers..., Override global
// links... (the window, stepping through the queue); Value > Cancel. Greyed while nothing is queued.
void addMultilinkControlsItems (juce::PopupMenu& menu, AudioEngine& engine);
juce::PopupMenu remoteControlFormulaMenu (std::function<void (const juce::String&)> pick);
juce::PopupMenu remoteControlControllerMenu (std::function<void (int)> pick);
} // namespace lynx
