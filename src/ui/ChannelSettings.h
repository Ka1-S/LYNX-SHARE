#pragma once

#include <functional>
#include <optional>

#include "engine/AudioEngine.h"

namespace lynx
{
// Sampler settings window for a channel (pitch, attack, release, reverse,
// "cut itself"), like FL's Channel settings. One window per channel.
void openChannelSettings (AudioEngine& engine, int channel);
// Shows insert `insert` in the Mixer (the main window sets it): a double-click on the channel window's TRACK box, as in FL.
std::function<void (int insert)>& openMixerInsertHandler();
// Shows a file in the Browser (the main window sets it): the sampler's "Locate sample in browser" button (round 190df).
std::function<void (const juce::File&)>& locateInBrowserHandler();
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
// The designed plugin page inside a channel window shows page `page` (screenshots, round 169h); returns the page count
// of its layout (0 = no pages)
int showChannelLayoutPage (juce::Component& frame, int page);

// A Patcher node's built-in generator (round 164f): the same pages as on a channel — the instrument's page and, for
// the synth and the sampler, the Envelope / instrument tab — editing the node `locate` finds. Null: no page for it.
bool hasNodeEditor (const ChannelDef& channel) noexcept;
std::unique_ptr<juce::Component> makeNodeChannelFrame (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate);
} // namespace lynx
