#pragma once

// FL's "Remote control settings" window (form TMIDIInputForm) and its "Global link settings" twin (form
// TMIDIInputForm_Generic), round 190bt (docs/analise/REA_rodada190bt.md): a control's link to a MIDI controller seen
// and edited: port, channel and controller number with Auto detect, Omni, the internal controller, the mapping
// formula with FL's presets and its curve, smoothing, Remove conflicts, Reset / Skip / Accept. One-step learning
// (the control menu's "Link to MIDI controller") stays the quick way; this is the detailed one.

#include "core/FlControlMenus.h"
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
// (pressEnter false: typed but not compiled yet, as FL compiles on Enter)
void remoteControlPanelSetFormula (juce::Component& panel, const juce::String& formula, bool pressEnter = true);
void remoteControlPanelSetMode (juce::Component& panel, int mode); // FL's Mode: 0 Default ... 4 Stay
void remoteControlPanelSetInternal (juce::Component& panel, int which, bool recordAutomation); // 0 = (none)
void remoteControlPanelSetController (juce::Component& panel, int port, int channel, int cc);
void remoteControlPanelSetSmoothing (juce::Component& panel, bool on, int position);
void remoteControlPanelPoll (juce::Component& panel); // one Auto detect check, as the panel's timer does
void remoteControlPanelPress (juce::Component& panel, int button); // 0 Reset, 1 Skip, 2 Accept
// "port|channel|cc|formula|message|conflicts|parameter|mode=n|shown=<P A I O C S>|omni=on/off|h=<height>"
juce::String remoteControlPanelState (juce::Component& panel);
bool remoteControlPanelFinished (juce::Component& panel);           // the last control was accepted or skipped
// FL's multilink menu (MLWheelMenu; rounds 190bt, 190df; core/FlControlMenus.h mlmenu): the caption ("2 parameters
// selected", or the last tweaked parameters while multilink is off), Automation > Edit events, Init song / Init pattern
// with these positions, Create automation clips; Remote control > Link to controllers..., Override global links...
// (the window, stepping through them); Value > Randomize, Humanize; Cancel (only while multilink is on).
void addMultilinkControlsItems (juce::PopupMenu& menu, AudioEngine& engine);
// The Multilink button's right-click menu: FL's items above, then LYNX's own "Link to controller" / "Override global
// links" (one-step linking of the queued controls, round 177b-4) below a line
// (round 190ey, live check 190ew CTL-1: with multilink off and a last tweaked control, `lastTweakedColumn` fills FL's
// left column, that control's own menu; LYNX's one-step items show only while multilink is on)
juce::PopupMenu multilinkButtonMenu (AudioEngine& engine, const std::function<void (juce::PopupMenu&)>& lastTweakedColumn = {});
// What one of its items does (seed: Randomize / Humanize's random numbers, 0 = from the clock; for the shot checks)
void runMultilinkAction (AudioEngine& engine, mlmenu::Action action, const std::vector<AutomationTarget>& targets, std::uint32_t seed);
std::vector<AutomationTarget> multilinkMenuTargets (AudioEngine& engine);
juce::PopupMenu remoteControlFormulaMenu (std::function<void (const juce::String&)> pick);
juce::PopupMenu remoteControlControllerMenu (std::function<void (int)> pick);
} // namespace lynx
