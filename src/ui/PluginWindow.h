#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/AudioEngine.h"

namespace lynx
{
class Workspace;

// Window with a plugin's own UI. Keeps the plugin alive while
// it is open (even if the channel is removed), and only one window per plugin.
namespace PluginWindows
{
// Where the plugin's wrapper options live (REA round 146a: FL's plugin wrapper settings page). With an engine, the
// window gets FL's wrapper header: the plugin's own editor and a "Wrapper settings" page with its processing, bridging
// and MIDI options and what the plugin reports. `find` locates the PluginRef in the song (nullptr: gone).
struct WrapperContext
{
    AudioEngine* engine = nullptr;
    std::function<PluginRef* (Song&)> find;
    bool channel = false; // a Channel rack plugin (the MIDI output port applies)
    // round 177b: a mixer slot's plugin (insert, slot): a plugin with a sidechain input gets the header's
    // "Sidechain from" picker; -1 = not a mixer slot
    int track = -1, slot = -1;
    // round 190ao (reload a crashed plugin): `reload` starts the plugin again and opens its window again (the caller
    // knows the channel / slot); with it, a window whose plugin went down (crashed inside a call, or its separate
    // process died) shows a notice with "Reload" in place of the editor. `down`: the adapter's own crash flag (a dead
    // separate process is seen by the window itself). Nothing reloads unless the user asks.
    std::function<void()> reload;
    std::function<bool()> down;
};
// `owner` is the adapter (PluginInstrument/PluginEffect) that holds the plugin.
void open (std::shared_ptr<void> owner, juce::AudioPluginInstance& plugin, const juce::String& title, WrapperContext context = {});
// Issue #116: the desktop docked plugin windows float in (developer switch, ui/NativeChild.h); nullptr: none.
void setDockWorkspace (Workspace* workspace);
// FL's plugin menu › Detached for an open editor: it closes and opens again in the other host (saved in the project).
void setDetached (juce::AudioPluginInstance& plugin, bool detached);
// FL's Tools › Macros › "Switch all plugin editors to" › Attached / Detached (round 190i): every hosted plugin of the
// song, and the open editors move to the other host.
void setAllDetached (AudioEngine& engine, bool detached);
// --plugwintest (issue #116): whether the open editor is docked, and the native window that holds it.
bool isDocked (const juce::AudioPluginInstance& plugin);
void* hostWindowHandle (const juce::AudioPluginInstance& plugin);
// Round 190ao, LYNX's "Pin on top" (pain point 11; FL has no pin): a detached plugin window that stays above LYNX's
// other secondary windows (not above other programs: like them it is owned by the main window; on Linux, where there
// are no owner windows, the window manager's "always on top"). The header's pin and the plugin menu's item set it; it
// is saved with the plugin in the project (PluginRef::pinned). Not for a docked window or a plugin in a separate
// process (its editor is a window of that process).
bool isPinnedOnTop (const juce::AudioPluginInstance& plugin);
void setPinnedOnTop (juce::AudioPluginInstance& plugin, bool pinned);
void closeAll();
// Closes now the windows whose plugin is no longer in the song (removed, replaced, its channel deleted); a timer
// also does it on its own while a window is open.
void closeOrphans();
// The plugins with an open window, the one used last first (the Browser applies a preset to it).
std::vector<juce::AudioPluginInstance*> openPlugins();
bool isOpen (const juce::AudioPluginInstance& plugin);
void close (const juce::AudioPluginInstance& plugin);
// Faults inside a plugin's own editor window (issue #32, round 174a; Windows): the editor was closed and LYNX went on.
// The main window shows each new one once (save the project, run the plugin in a separate process).
struct EditorCrash
{
    juce::String plugin; // its name (as PluginRef::name / pluginAlwaysSeparate)
    juce::String detail; // "access violation at Synth.dll+0x4be460"
    juce::File traceFile; // autosave\editor-crash <time>.txt
};
int editorCrashCount();
std::vector<EditorCrash> editorCrashes();
// Round 190ao (Bitwig's "Reload plug-in"): closes the windows of those hosted plugins and starts them again from their
// last captured state (AudioEngine::reloadHostedPlugins: no undo step, the project is not reopened). Says so when one
// could not be started. Returns how many run again.
int reload (AudioEngine& engine, const std::vector<AudioEngine::HostedSlot>& which);
// The user asked for the window of a plugin that is down: "<name> crashed and was switched off" with Reload (and, for a
// plugin that ran inside LYNX, "Reload in a separate process"). `reopen` runs after a reload that worked.
void promptReload (AudioEngine& engine, AudioEngine::HostedSlot where, std::function<void()> reopen);
// Round 190gg (B1, LYNX-own; FL has no sandbox): after a plugin crashed inside LYNX, "Always run <plugin> in a separate
// process". Offered unless every plugin already runs in one (Options) or this plugin is already on the list.
bool canAlwaysRunSeparately (const juce::String& plugin);
// Puts the plugin on the "always in a separate process" list (by its name, so new instances and other projects follow,
// and a rescan keeps it), and turns "Run in a separate process" on for every instance of it in this project and for the
// ones that are down: they start again in a process of their own, from their last captured settings (that is the
// Reload). Their windows close first. A plugin in another process opens its editor in a window of its own (no docking).
void alwaysRunSeparately (AudioEngine& engine, const juce::String& plugin);
// The hint that goes with the choice (the dialogs' message and the buttons' tooltips)
juce::String alwaysSeparateHint();
// The main window's "Plugin crashed" prompt for a plugin that crashed inside LYNX (MainComponent): its text and buttons
// (`reload`: the first button is Reload; `separate`: then "Always run <plugin> in a separate process"; Close is last).
// `onLoad`: it crashed on its saved settings (no Reload: they would crash it again).
struct CrashPrompt
{
    juce::String title, message;
    juce::StringArray buttons;
    bool reload = false, separate = false;
};
CrashPrompt crashPrompt (const juce::String& plugin, bool onLoad);
// --snapshot / --stabilityshot (round 190gg): that prompt, and the notice a plugin window shows in place of the editor
std::unique_ptr<juce::Component> makeCrashPromptPanel (const juce::String& plugin, bool onLoad);
std::unique_ptr<juce::Component> makeCrashNoticePanel (const juce::String& plugin, bool offerSeparate);
// A parameter gesture in an open plugin editor (`starting` = begin, else end), on the UI thread; the plugin is only
// compared, it may be gone (REA round 143: Last tweaked, automation recording).
extern std::function<void (const juce::AudioPluginInstance&, int, bool)> onGesture;
// `window` is a plugin's window and that plugin has "Allow LYNX to process keyboard presses" on (issue #96)
bool letsHostProcessKeys (juce::Component& window);
// (round 187a's check) a PNG of a plugin window's Wrapper settings page
bool saveSettingsPageShot (juce::Component& window, const juce::File& png);
// Tests / shots (round 190ao): runs the open window's periodic check now (the crash notice follows the plugin) and
// writes a PNG of its content: the editor page (or the crash notice in its place), or the Wrapper settings page.
bool saveWindowShot (const juce::AudioPluginInstance& plugin, const juce::File& png, bool settingsPage);
// Opens the open window's preset menu (FL's plugin menu), as a click on the preset name does.
bool showPresetMenu (const juce::AudioPluginInstance& plugin);
} // namespace PluginWindows

// The wrapper settings page of a hosted plugin drawn offscreen (round 188d: FL | LYNX shots of Processing ›
// Connections, LYNX --wrappershot): `tab` 0 Settings, 1 Processing, 2 Troubleshooting; no window is opened.
juce::Image wrapperSettingsSnapshot (AudioEngine& engine, std::function<PluginRef* (Song&)> find, juce::AudioPluginInstance& plugin,
                                     bool channel, int tab, float scale = 1.0f, int track = -1, int slot = -1);

// Scans the VST3 folders with a progress window. `onDone(newCount)` runs on the UI thread.
void scanPluginsWithProgress (AudioEngine& engine, std::function<void (int)> onDone);

// FL's wrapper processing options for one hosted plugin ("Use fixed size buffers", "Make bridged"): toggling one
// keeps the plugin's current state, closes its window and creates it again. `find` locates the PluginRef in the song
// (nullptr: gone); `instance` is the running plugin (may be null).
juce::PopupMenu pluginProcessingMenu (AudioEngine& engine, const PluginRef& current, juce::AudioPluginInstance* instance,
                                      std::function<PluginRef* (Song&)> find);

// FL's plugin menu items for a hosted plugin (round 190df; FL's PluginMenu): its spare state ("Store in spare state",
// "Flip with spare state", FL's hint "A/B compare") and Parameters > "Link all parameters..." (the Remote control settings window
// stepping through every parameter). `base`: the plugin's first parameter as an automation target (channelParam or
// effectParam; none = no parameter items).
void addPluginWrapperItems (juce::PopupMenu& menu, AudioEngine& engine, juce::AudioPluginInstance& plugin, const AutomationTarget& base);
// The parameters "Link all parameters..." queues (FL: every parameter of the plugin [0x124dea0])
std::vector<AutomationTarget> allPluginParameterTargets (juce::AudioPluginInstance& plugin, const AutomationTarget& base);

// Menus of known plugins (instruments or effects): the favourites, then "More plugins..." (the plugin picker) and
// the scan. `onChosen` receives the reference; from the picker (a window of its own, chosen later) it is only
// called while `owner` (the component whose menu this is) still exists.
// `onBuiltIn`: the picker lists LYNX's built-in note effects too (core/Vfx.h) and hands a new channel of one over.
void addPluginItems (juce::PopupMenu& menu, AudioEngine& engine, bool instruments, std::function<void (PluginRef)> onChosen,
                     juce::Component* owner = nullptr, std::function<void (ChannelDef)> onBuiltIn = nullptr);
} // namespace lynx
