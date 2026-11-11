#pragma once

// "Open without third-party plugins" (round 190ap). A project that will not open, or that crashes while it loads,
// opens with every hosted plugin switched off: nothing of a third-party plugin is loaded or run, the slot plays
// silence (an instrument) or passes the sound through (an effect), and the plugin's saved settings stay in the
// project exactly as they were read. The user then switches the plugins back on, one at a time or all, to find the
// one that fails.
//
// The rule that makes saving in this state safe: a slot's settings (PluginRef::state) are only ever replaced by what
// a live plugin instance hands out. A plugin that is switched off has no instance, so nothing can replace them; the
// project file written in this state carries the same settings it was opened with.
//
// Nothing is added to the project: which plugins are off is this session's knowledge only (a project saved here and
// opened again the normal way loads all its plugins). No JUCE here; the engine side is engine/PluginHost.*.

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::pluginhold
{
// Which plugins are off: all of them while `active`, except the ones switched back on (by plugin, so every slot of
// that plugin comes back together: the question is "which plugin breaks the project").
struct State
{
    bool active = false;
    std::set<std::string> released; // plugin ids (PluginRef::id) switched back on

    void start()
    {
        active = true;
        released.clear();
    }
    void end()
    {
        active = false;
        released.clear();
    }
    void release (const std::string& id)
    {
        if (active && ! id.empty())
            released.insert (id);
    }
    bool holds (const std::string& id) const { return active && ! id.empty() && released.count (id) == 0; }
};

// One hosted plugin of a song, with every place that uses it.
struct Use
{
    std::string id, name;
    int instruments = 0, effects = 0; // slots (those inside Patchers included)
    bool off = false;
    std::vector<std::string> usedBy;  // "Channel: Lead", "Insert 3: Vocals", "Patcher in ..." (once each, in project order)
    bool operator== (const Use&) const = default;
};

namespace detail
{
template <typename Fn>
void eachInPatcher (const PatcherDoc& doc, const std::string& owner, Fn&& fn)
{
    for (const auto& node : doc.nodes)
    {
        if (node.generator)
        {
            if (node.channel.instrument == InstrumentKind::plugin && ! node.channel.plugin.id.empty())
                fn (node.channel.plugin, true, owner);
            if (node.channel.instrument == InstrumentKind::patcher)
                eachInPatcher (node.channel.patcher, owner, fn);
        }
        else
        {
            if (node.effect.kind == EffectKind::plugin && ! node.effect.plugin.id.empty())
                fn (node.effect.plugin, false, owner);
            if (node.effect.kind == EffectKind::patcher)
                eachInPatcher (node.effect.patcher, owner, fn);
        }
    }
}

inline bool patcherUses (const PatcherDoc& doc, const std::string& id)
{
    auto found = false;
    eachInPatcher (doc, {}, [&] (const PluginRef& ref, bool, const std::string&) { found = found || id.empty() || ref.id == id; });
    return found;
}
} // namespace detail

// Every hosted plugin slot of the song: fn (ref, isInstrument, owner). Channels, insert effect slots, and the nodes
// of Patchers (nested ones too).
template <typename Fn>
void eachPlugin (const Song& song, Fn&& fn)
{
    for (const auto& c : song.channels)
    {
        const auto owner = "Channel: " + c.name;
        if (c.instrument == InstrumentKind::plugin && ! c.plugin.id.empty())
            fn (c.plugin, true, owner);
        if (c.instrument == InstrumentKind::patcher)
            detail::eachInPatcher (c.patcher, owner + " (Patcher)", fn);
    }
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
    {
        // "Insert 3: Vocals"; an insert that still has its default name is just "Insert 3" (not "Insert 3: Insert 3")
        const auto base = t == 0 ? std::string ("Master") : "Insert " + std::to_string (t);
        const auto owner = base + (song.mixer[t].name.empty() || song.mixer[t].name == base ? std::string() : ": " + song.mixer[t].name);
        for (const auto& fx : song.mixer[t].effects)
        {
            if (fx.kind == EffectKind::plugin && ! fx.plugin.id.empty())
                fn (fx.plugin, false, owner);
            if (fx.kind == EffectKind::patcher)
                detail::eachInPatcher (fx.patcher, owner + " (Patcher)", fn);
        }
    }
}

// The song's hosted plugins, one entry per plugin, in the order they first appear.
inline std::vector<Use> uses (const Song& song, const State& state)
{
    std::vector<Use> out;
    eachPlugin (song, [&] (const PluginRef& ref, bool instrument, const std::string& owner) {
        auto it = std::find_if (out.begin(), out.end(), [&] (const Use& u) { return u.id == ref.id; });
        if (it == out.end())
        {
            out.push_back ({ ref.id, ref.name, 0, 0, state.holds (ref.id), {} });
            it = out.end() - 1;
        }
        (instrument ? it->instruments : it->effects) += 1;
        if (std::find (it->usedBy.begin(), it->usedBy.end(), owner) == it->usedBy.end())
            it->usedBy.push_back (owner);
    });
    return out;
}

inline int countOff (const Song& song, const State& state)
{
    int n = 0;
    for (const auto& u : uses (song, state))
        n += u.off ? 1 : 0;
    return n;
}

// The channels whose own plugin (or a plugin inside their Patcher) is off, and the inserts with such an effect slot.
inline std::vector<int> channelsOff (const Song& song, const State& state)
{
    std::vector<int> out;
    if (! state.active)
        return out;
    for (std::size_t i = 0; i < song.channels.size(); ++i)
    {
        const auto& c = song.channels[i];
        auto off = c.instrument == InstrumentKind::plugin && state.holds (c.plugin.id);
        if (c.instrument == InstrumentKind::patcher)
            detail::eachInPatcher (c.patcher, {}, [&] (const PluginRef& ref, bool, const std::string&) { off = off || state.holds (ref.id); });
        if (off)
            out.push_back ((int) i);
    }
    return out;
}

inline std::vector<int> insertsOff (const Song& song, const State& state)
{
    std::vector<int> out;
    if (! state.active)
        return out;
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
    {
        auto off = false;
        for (const auto& fx : song.mixer[t].effects)
        {
            off = off || (fx.kind == EffectKind::plugin && state.holds (fx.plugin.id));
            if (fx.kind == EffectKind::patcher)
                detail::eachInPatcher (fx.patcher, {}, [&] (const PluginRef& ref, bool, const std::string&) { off = off || state.holds (ref.id); });
        }
        if (off)
            out.push_back ((int) t);
    }
    return out;
}

// Switching a plugin back on: the rack keeps a slot's running instance while the slot's plugin is the same, and a
// switched-off slot's "instance" is its silent placeholder. The engine therefore builds the rack once with
// RackState::Factories::rebuildPlugin answering true for the plugin `id` ("" = every hosted plugin): its slots are
// created anew from the settings in the song. This is how many slots that is (a Patcher that holds one counts once:
// it is built anew as a whole).
inline int slotsOf (const Song& song, const std::string& id)
{
    int n = 0;
    const auto match = [&id] (const PluginRef& ref) { return ! ref.id.empty() && (id.empty() || ref.id == id); };
    for (const auto& c : song.channels)
        n += (c.instrument == InstrumentKind::plugin && match (c.plugin)) || (c.instrument == InstrumentKind::patcher && detail::patcherUses (c.patcher, id)) ? 1 : 0;
    for (const auto& track : song.mixer)
        for (const auto& fx : track.effects)
            n += (fx.kind == EffectKind::plugin && match (fx.plugin)) || (fx.kind == EffectKind::patcher && detail::patcherUses (fx.patcher, id)) ? 1 : 0;
    return n;
}

// Every hosted plugin's settings in the song, in project order: what must be identical before and after a save made
// while plugins are off ("<owner>|<id>|<name>|<state>").
inline std::vector<std::string> pluginStates (const Song& song)
{
    std::vector<std::string> out;
    eachPlugin (song, [&] (const PluginRef& ref, bool, const std::string& owner) { out.push_back (owner + "|" + ref.id + "|" + ref.name + "|" + ref.state); });
    // (LYNX substitutes keep the original plugin's reference for when it is installed: its settings count too)
    for (const auto& track : song.mixer)
        for (const auto& fx : track.effects)
            if (fx.isSubstitute())
                out.push_back ("substitute|" + fx.substituteFor.id + "|" + fx.substituteFor.name + "|" + fx.substituteFor.state);
    return out;
}
} // namespace lynx::pluginhold
