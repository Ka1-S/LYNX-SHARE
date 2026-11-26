#pragma once
// Round 190rn: about how many bytes of heap a copy of a Song holds: what one undo step costs. The undo history keeps
// whole copies (AudioEngine::saveUndoStep), so the history is capped by these bytes as well as by its number of steps.
// An estimate (the containers that make up nearly all of a big project: notes and their curves, patterns, channels,
// mixer slots, Patcher graphs, clips, automation; a few small ones and the allocator's own overhead are left out).
// Against the bytes a copy really allocates (round 190rn): Patience (Live 12 demo) 7.17 of 7.40 MB, Ookay - Thief 0.72
// of 0.80 MB, Exoplanet 2.49 of 2.54 MB.
#include <cstddef>
#include <string>
#include <vector>

#include "core/NoteCurves.h"
#include "core/Song.h"

namespace lynx::songbytes
{
inline std::size_t str (const std::string& s) noexcept { return s.size() > 15 ? s.size() + 1 : 0; } // (a copy: past the small-string buffer)
template <typename T>
std::size_t flat (const std::vector<T>& v) noexcept { return v.size() * sizeof (T); } // (a copy holds no spare capacity)

std::size_t effect (const EffectSlot& e) noexcept;
std::size_t channel (const ChannelDef& c) noexcept;

inline std::size_t plugin (const PluginRef& p) noexcept
{
    return str (p.id) + str (p.name) + str (p.state) + str (p.foreign.clapId) + str (p.foreign.name) + str (p.foreign.path)
           + flat (p.foreign.state);
}

inline std::size_t patcher (const PatcherDoc& d) noexcept
{
    std::size_t n = flat (d.nodes) + flat (d.wires) + flat (d.published) + flat (d.outputTracks) + flat (d.fromMidiPorts) + flat (d.toMidiPorts);
    for (const auto& node : d.nodes)
    {
        n += effect (node.effect) + channel (node.channel) + flat (node.busHeads) + flat (node.paramIns) + flat (node.paramOuts);
        for (const auto& head : node.busHeads)
            if (head.head)
                n += effect (*head.head);
    }
    for (const auto& p : d.published)
        n += flat (p.curve) + str (p.name);
    return n;
}

inline std::size_t effect (const EffectSlot& e) noexcept
{
    std::size_t n = plugin (e.plugin) + plugin (e.substituteFor) + flat (e.state) + flat (e.abOther) + str (e.samplePath) + str (e.presetName)
                    + flat (e.branches) + flat (e.links);
    for (const auto& b : e.branches)
    {
        n += flat (b.chain);
        for (const auto& inner : b.chain)
            n += effect (inner);
    }
    for (const auto& l : e.links)
        n += flat (l.curve);
    if (! e.patcher.nodes.empty())
        n += patcher (e.patcher);
    return n;
}

inline std::size_t channel (const ChannelDef& c) noexcept
{
    std::size_t n = str (c.name) + str (c.samplePath) + flat (c.zones) + plugin (c.plugin) + flat (c.controllerState) + flat (c.modelParams)
                    + flat (c.pluginState) + flat (c.polyEdit) + flat (c.standIns) + flat (c.layer.children) + c.keyLabels.size() * 64;
    for (const auto& z : c.zones)
        n += str (z.path);
    if (! c.patcher.nodes.empty())
        n += patcher (c.patcher);
    return n;
}

inline std::size_t pattern (const Pattern& p) noexcept
{
    std::size_t n = str (p.name) + flat (p.notes) + flat (p.autoNotes) + flat (p.events) + flat (p.channelLoops) + flat (p.markers);
    for (const auto* list : { &p.notes, &p.autoNotes })
        for (const auto& note : *list)
            if (const auto* curves = note.curves.get())
            {
                n += sizeof (NoteCurves);
                for (const auto& lane : curves->lanes)
                    n += flat (lane);
            }
    for (const auto& lane : p.events)
        n += flat (lane.events);
    return n;
}

inline std::size_t song (const Song& s) noexcept
{
    std::size_t n = sizeof (Song) + flat (s.channels) + flat (s.patterns) + flat (s.playlist) + flat (s.blocks) + flat (s.arrangements)
                    + flat (s.mixer) + flat (s.automations) + flat (s.playlistTracks);
    for (const auto& c : s.channels)
        n += channel (c);
    for (const auto& p : s.patterns)
        n += pattern (p);
    for (const auto& a : s.arrangements)
        n += str (a.name) + flat (a.clips) + flat (a.markers) + flat (a.blocks) + flat (a.sections) + flat (a.chords);
    for (const auto& t : s.mixer)
    {
        n += str (t.name) + flat (t.effects) + flat (t.sends) + flat (t.lostSends) + flat (t.standIns);
        for (const auto& e : t.effects)
            n += effect (e);
    }
    for (const auto& a : s.automations)
    {
        n += str (a.name) + flat (a.points) + flat (a.curve) + flat (a.links);
        for (const auto& l : a.links)
            n += flat (l.scale) + str (l.formula);
    }
    if (s.clipGrid)
        n += flat (s.clipGrid->clips);
    for (const auto& t : s.playlistTracks)
        for (const auto& v : t.versions)
            n += flat (v.clips);
    return n;
}
} // namespace lynx::songbytes
