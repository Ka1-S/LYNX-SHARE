#pragma once

#include <functional>
#include <optional>

#include "engine/AudioEngine.h"

namespace lynx
{
namespace wrappermenu
{
struct Spec;
}
// Round 190in (details190if R8): the Channel rack's items of FL's channel ▸ wrapper menu (Piano roll, Replace ▸,
// Delete..., Rename, color and icon..., Change color, Random color, Change icon...), for channel `channel`. The main
// window sets it; the channel and plugin windows call it.
std::function<void (int channel, wrappermenu::Spec&)>& channelWrapperItemsHandler();
// Sampler settings window for a channel (pitch, attack, release, reverse,
// "cut itself"), like FL's Channel settings. One window per channel.
void openChannelSettings (AudioEngine& engine, int channel);
// Shows insert `insert` in the Mixer (the main window sets it): a double-click on the channel window's TRACK box, as in FL.
std::function<void (int insert)>& openMixerInsertHandler();
// Shows a file in the Browser (the main window sets it): the sampler's "Locate sample in browser" button (round 190df).
std::function<void (const juce::File&)>& locateInBrowserHandler();
// Opens a file in a new channel of kind `kind` (the main window sets it): the Sampler waveform menu's "Open in new
// sampler channel" / "... Fruity Granulizer channel" (round 190ey, live check 190ew SMP-7).
std::function<void (const juce::File&, InstrumentKind kind)>& openInNewChannelHandler();
// The Sampler page's waveform menu inside `root` (round 190ey; the shot route); nullopt: no sampler page.
std::optional<juce::PopupMenu> samplerWaveMenu (juce::Component& root);
// The sampler page's File row inside `root` (round 190df; the shot route and its checks): the "Sample history" menu of
// its Load button (nullopt: no sampler page), and what its buttons do: 0 Load's wheel down, 1 Load's wheel up,
// 2 Locate sample in browser, 3 Remove sample. False: no sampler page.
std::optional<juce::PopupMenu> samplerHistoryMenu (juce::Component& root);
bool samplerFileRowPress (juce::Component& root, int button);
// The sampler page inside `root` (round 190dh; the shot route): turns a control as a user would ("pitch", "mul", "time",
// "loopStart", "loopEnd", "crossfade", "smpStart", "length", "useLoops", "pingPong", "keepOnDisk", "mode", "declick",
// "legacy", "zoom"; `shift`: Shift held), and reads what the page shows ("legacy", "keepOnDisk", "loopStart", "mode",
// "scroll"). False / empty: no sampler page.
bool samplerPageTurn (juce::Component& root, const juce::String& what, double value, bool shift = false);
juce::String samplerPageQuery (juce::Component& root, const juce::String& what);
// Round 190fs (shots, tests): the preset box of the channel window `root` (the ◀ name ▶ bar of an FL engine or a LYNX
// bank, LYNX's synth's "Presets..." box): its menu (nullopt: no box), "Save preset..." with `name` (no name box), and
// the preset it shows ("" = none).
std::optional<juce::PopupMenu> channelPresetMenu (juce::Component& root);
bool channelPresetSave (juce::Component& root, const juce::String& name);
juce::String channelPresetName (juce::Component& root);
void closeChannelSettings();
// FL's View › Align all channel editors (Shift+F12): every open channel editor moves to the focused one's place (else the
// selected channel's), and channel editors opened later come up there too (round 169k).
void alignAllChannelEditors (int selectedChannel);

// Channel button toggle (Channel Rack): which kinds have a settings window, and whether it is open.
bool hasChannelSettings (InstrumentKind kind) noexcept;
bool isChannelSettingsOpen (int channel);
void closeChannelSettings (int channel);

// The channel window on its "Misc" tab (FL's wrench tab: levels, polyphony, time, cut groups, arpeggiator); for a channel
// without a settings page (a hosted plugin) a window with that tab alone.
void openChannelMisc (AudioEngine& engine, int channel);

// Channel modulation window (cutoff/pitch envelopes and LFOs), for the synth or the sampler.
void openChannelModulation (AudioEngine& engine, int channel);

// Window content only (for --snapshot mode).
std::unique_ptr<juce::Component> makeChannelSettingsPanel (AudioEngine& engine, int channel);
std::unique_ptr<juce::Component> makeChannelModulationPanel (AudioEngine& engine, int channel);
// The Sampler's "Precomputed effects" panel (round 167h; normally a call-out from the Sampler settings).
std::unique_ptr<juce::Component> makeChannelPrecomputePanel (AudioEngine& engine, int channel);
// The whole channel window (REA round 143b): FL's header strip (tabs; On, Pan, Vol, Track) over the instrument page,
// the "Envelope / instrument" tab (synth, sampler) and the "Misc" tab. `tab` < 0 = the Misc tab.
std::unique_ptr<juce::Component> makeChannelFrame (AudioEngine& engine, int channel, int tab = 0);
// the height of that window's channel header (tabs, pan / volume / pitch...) above the instrument's pages
int channelFrameHeaderHeight() noexcept;
// Round 190in (design190ia FW-7): FL's ON / PAN / VOL / PITCH / RANGE / TRACK strip of channel `channel` (the right part
// of the channel window's header), for a hosted instrument's wrapper header; `channelHeaderControlsWidth` wide.
std::unique_ptr<juce::Component> makeChannelHeaderControls (AudioEngine& engine, int channel);
// Round 190in (shots): the channel window as it shows docked (LYNX's title bar with FL's ▸, the two-tone title and the
// presets), off the desktop, on tab `tab` (< 0 = Misc); and FL's ▸ wrapper menu of the channel window inside `root`.
std::unique_ptr<juce::Component> makeChannelWindowForShot (AudioEngine& engine, int channel, int tab);
std::optional<juce::PopupMenu> channelWrapperMenu (juce::Component& root);
// Rollup 0.1.31 step 7 (PR #248, tests): whether the channel window `root`'s header strip shows (nullopt: no channel frame),
// and a press of the gear in its title bar (false: none; docked windows only)
std::optional<bool> channelWindowHeaderShown (juce::Component& root);
bool pressChannelHeaderGear (juce::Component& root);
// Round 190pw (PR #259, tests): the channel window or frame inside `root` shows tab `tab` (< 0 = Misc), as a click on
// the tab does; false: no channel frame there
bool showChannelWindowTab (juce::Component& root, int tab);
int channelHeaderControlsWidth() noexcept;
// The designed plugin page inside a channel window shows page `page` (screenshots, round 169h); returns the page count
// of its layout (0 = no pages)
int showChannelLayoutPage (juce::Component& frame, int page);

// A Patcher node's built-in generator (round 164f): the same pages as on a channel — the instrument's page and, for
// the synth and the sampler, the Envelope / instrument tab — editing the node `locate` finds. Null: no page for it.
bool hasNodeEditor (const ChannelDef& channel) noexcept;
std::unique_ptr<juce::Component> makeNodeChannelFrame (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate);
} // namespace lynx
