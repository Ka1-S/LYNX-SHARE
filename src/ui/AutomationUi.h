#pragma once

#include "engine/AudioEngine.h"

namespace lynx
{
// Readable name of what an automation controls ("Kick - Volume", "Insert 3 - Pan"...).
juce::String describeAutomationTarget (AudioEngine& engine, const AutomationTarget& target);

// The target's current value as its control shows it ("78%", "-3.2 dB", "L50", a plugin parameter's own text);
// "" when the target is gone (round 162d: the toolbar's last-tweaked read-out).
juce::String describeTargetValue (AudioEngine& engine, const AutomationTarget& target);

// Creates an automation for the target, starting at the current value, plus a clip of it in the Playlist
// (from the start of the song to its end, on a free track), like FL's "Create automation clip".
void createAutomationClip (AudioEngine& engine, const AutomationTarget& target);
// Round 177b: a lane for the target under a Playlist track (`track` < 0: autolanes::trackFor), a flat line at the
// current value; the existing lane when the track has one for it.
void createAutomationLane (AudioEngine& engine, const AutomationTarget& target, int track = -1);
// The same with a given name and value (a flat line), for any target (none: an unlinked clip).
void addAutomationClip (AudioEngine& engine, const AutomationTarget& target, const std::string& name, float value);
// FL's "Automation Clip" generator (Plugin picker, round 175b): an automation linked to nothing, its clip on a free
// Playlist track, its editor open.
void createUnlinkedAutomationClip (AudioEngine& engine);

// Round 165: shared by the point editor and the Playlist's in-clip editing — a point's value as text (percent, or BPM
// for the tempo), the copied value of FL's "Copy value" (< 0 = none) and FL's "Type in value..." dialog.
juce::String automationPointValueText (const AutomationTarget& target, float value);
// Round 190em: a target's value `value` (0..1) in the unit its control shows (dB, pan, BPM, a LYNX effect's own unit, a
// hosted plugin's own text); "" when there is none.
juce::String targetValueTextAt (AudioEngine& engine, const AutomationTarget& target, float value);
float& copiedAutomationValue();
void typeAutomationPointValue (AudioEngine& engine, int automation, int point);

// Round 190bw, FL's per-point curve modes. FL's envelope editor remembers the mode and the tension last used and gives
// them to the next new point [DECOMP FLEngine 0xcce3b0: new point; 0xcd79f0: "change point curve"; 0xcc9790: tension
// drag; 0xccfd0c: "default tension"]. One memory for the point editor and the Playlist's in-clip editing (LYNX's
// tension sign).
struct AutomationPointMemory
{
    int mode = 0;
    float tension = 0.0f;
};
AutomationPointMemory& automationPointMemory();
// The header of FL's automation point menu: the point's number and the clip's point count, "Point 2/14" (`point` is
// 0-based) [live FL check 190cq, shot curves190bw-01].
juce::String automationPointMenuHeader (int point, int count);
// The "Mode" block of FL's automation point menu (EEACPointMenu): the header and the 13 modes as radio items in FL's
// order (Smooth, Hold, Single curve, Single curve 2, Single curve 3, Double curve…, Wave). `current`: the mode of the
// segment that ends at the point (−1: none ticked); `enabled` false on the first point, which ends no segment.
void addAutomationModeItems (juce::PopupMenu& menu, int current, bool enabled, std::function<void (int)> pick);

// The next sample (x1, y1) of an automation curve drawn from (x0, y0), keeping the jumps of Hold, Stairs and Pulse
// segments square: when the curve's level halfway between the two samples (`midY`, asked only for a big change)
// equals one of them, the line turns a corner instead of slanting.
template <typename MidY>
void automationLineTo (juce::Path& path, float x0, float y0, float x1, float y1, MidY&& midY)
{
    if (std::abs (y1 - y0) > 1.5f)
    {
        const auto m = (float) midY();
        if (std::abs (m - y0) < 0.01f)
            path.lineTo (x1, y0);
        else if (std::abs (m - y1) < 0.01f)
            path.lineTo (x0, y1);
    }
    path.lineTo (x1, y1);
}

// Opens (or brings to front) the point editor window of an automation.
void openAutomationEditor (AudioEngine& engine, int automation);
void closeAutomationEditors();

// Round 177b: LYNX's modulators window (one list: shape, rate, phase, routes with their depth), and the parameter
// menu's "Modulate" submenu (LYNX's own controls only; after FL's items).
void openModulators (AudioEngine& engine);
void closeModulators();
std::unique_ptr<juce::Component> makeModulatorsPanel (AudioEngine& engine); // (--snapshot mode)
void addModulationItems (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& target);

// Just the editor (for --snapshot mode).
std::unique_ptr<juce::Component> makeAutomationEditor (AudioEngine& engine, int automation);

// A parameter's menu items, in FL's order (REA round 143 / 146): Edit events in the pattern, Edit events in piano
// roll, Init song with this position, Delete initial value, Create automation clip; remote control: Link to MIDI
// controller, Override global link..., internal controllers, unlink.
// The parameter menu's automation and remote-control items; with `headers`, under FL's section titles "Automation" and
// "Remote control" (REA round 149 [EXEC]: FL's parameter menu).
void addAutomationItem (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& target, bool headers = false);
// Opens the Piano roll with the target's pattern events in its bottom lane (the main window sets it).
extern std::function<void (const AutomationTarget&)> onEditEventsInPianoRoll;

// An output of a built-in controller that knobs can be linked to (Envelope / Keyboard Controller channels, Formula and
// Peak Controller mixer slots), as FL's "Link to controller" lists them.
struct ControllerOutput
{
    int track = -1, slot = 0, output = 0; // a mixer slot's controller effect, or
    int channel = -1;                     // a controller channel
    juce::String label;
};
std::vector<ControllerOutput> controllerOutputs (const Song& song);
// Drives `target` live from that output (replaces the target's previous controller link).
void linkToController (AudioEngine& engine, const AutomationTarget& target, const ControllerOutput& source);
// "Link to internal controller" submenu.
void addControllerLinkItems (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& target);

// "MIDI CC #64 (Damper pedal (sustain))", "MIDI Channel 2 Aftertouch"… for a MIDI index (core/MidiControls.h).
juce::String midiParamLabel (int param);

// Submenu with the MIDI CCs, channel aftertouch and pitch bend a hosted plugin can be automated with (FL's wrapper).
void addMidiParameterItems (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& base);

// Submenu with a plugin's parameters (channel instrument or slot effect).
void addPluginParameterItems (juce::PopupMenu& menu, AudioEngine& engine, juce::AudioPluginInstance& plugin,
                              const AutomationTarget& base);

// Submenu with a Patcher's published parameters (round 171b, issue #65: the parameter inputs activated inside it,
// FL's record 1100), each with the automation / link items. Nothing when it publishes none.
void addPatcherParameterItems (juce::PopupMenu& menu, AudioEngine& engine, const PatcherDoc& doc, const AutomationTarget& base);
} // namespace lynx
