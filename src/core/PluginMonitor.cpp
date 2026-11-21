#include "core/PluginMonitor.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "core/Effects.h"

namespace lynx::perfmon
{
namespace
{
std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}
} // namespace

std::vector<Source> sources (const Song& song, const std::function<std::string (const ChannelDef&)>& instrumentName)
{
    std::vector<Source> out;
    for (int c = 0; c < (int) song.channels.size() && c < channelKeys; ++c)
    {
        const auto& ch = song.channels[(std::size_t) c];
        const auto plugin = ch.instrument == InstrumentKind::plugin;
        const auto name = plugin && ! ch.plugin.name.empty() ? ch.plugin.name : instrumentName ? instrumentName (ch) : ch.name;
        out.push_back ({ c, name, "Channel " + std::to_string (c + 1) + ": " + ch.name, plugin, c, -1, -1 });
    }

    for (int t = 0; t < (int) song.mixer.size() && t < maxMixerTracks; ++t)
    {
        const auto& track = song.mixer[(std::size_t) t];
        const auto trackName = t == 0 ? std::string ("Master") : "Insert " + std::to_string (t) + (track.name.empty() ? "" : " (" + track.name + ")");
        for (int s = 0; s < (int) track.effects.size() && s < maxEffectSlots; ++s)
        {
            const auto& slot = track.effects[(std::size_t) s];
            const auto isPlugin = slot.kind == EffectKind::plugin;
            const auto name = isPlugin && ! slot.plugin.name.empty() ? slot.plugin.name : std::string (effectInfo (slot.kind).name);
            out.push_back ({ effectKey (t, s), name, trackName + " > slot " + std::to_string (s + 1), isPlugin, -1, t, s });
        }
    }
    return out;
}

void Monitor::reset()
{
    for (auto& [key, s] : stats)
    {
        s.totalBusy = 0;
        s.peak = 0.0;
        s.longestNs = 0;
        s.spikes = 0;
    }
    totalAudio = 0;
    rackMax = 0.0;
}

void Monitor::update (const std::vector<Source>& list, const std::vector<std::uint64_t>& busy, std::uint64_t audioNs, std::uint64_t wallNs,
                      const CallStats* calls)
{
    const auto dAudio = started && audioNs >= lastAudio ? audioNs - lastAudio : 0;
    const auto dWall = started && wallNs >= lastWall ? wallNs - lastWall : 0;
    lastAudio = audioNs;
    lastWall = wallNs;

    std::map<int, Stat> next;
    for (const auto& source : list)
    {
        auto it = stats.find (source.key);
        Stat s;
        if (it != stats.end() && it->second.source.name == source.name)
            s = it->second; // the same thing in the same place: keep its history
        s.source = source;

        const auto now = source.key >= 0 && (std::size_t) source.key < busy.size() ? busy[(std::size_t) source.key] : 0;
        // round 190of: the longest call since the last update, and the spikes counted up since the start
        const auto at = (std::size_t) std::max (0, source.key);
        const auto longest = calls != nullptr && at < calls->longestNs.size() ? (std::uint64_t) calls->longestNs[at] : 0;
        const auto spikesNow = calls != nullptr && at < calls->spikes.size() ? calls->spikes[at] : 0;
        if (s.fresh || ! started || now < s.lastBusy)
        {
            s.fresh = false;
            s.lastBusy = now;
            s.lastSpikes = spikesNow;
            next[source.key] = s;
            continue;
        }
        if (calls != nullptr)
        {
            s.longestNs = std::max (s.longestNs, longest);
            if (spikesNow >= s.lastSpikes)
                s.spikes += spikesNow - s.lastSpikes;
            s.lastSpikes = spikesNow;
        }

        const auto dBusy = now - s.lastBusy;
        s.lastBusy = now;
        if (dAudio > 0)
        {
            const auto load = 100.0 * (double) dBusy / (double) dAudio;
            s.current = 0.5 * s.current + 0.5 * load; // light smoothing, still reacts within a second
            s.peak = std::max (s.peak, load);
            s.totalBusy += dBusy;
        }
        next[source.key] = s;
    }
    stats = std::move (next);

    if (dAudio > 0)
    {
        totalAudio += dAudio;
        const auto load = 100.0 * (double) dWall / (double) dAudio;
        rack = 0.5 * rack + 0.5 * load;
        rackMax = std::max (rackMax, load);
    }
    started = true;
}

std::vector<Row> Monitor::rows (const std::string& find, SortBy sort) const
{
    std::vector<std::string> words;
    {
        std::istringstream in (lower (find));
        for (std::string w; in >> w;)
            words.push_back (w);
    }

    std::vector<Row> out;
    for (const auto& [key, s] : stats)
    {
        const auto text = lower (s.source.name + " " + s.source.where);
        if (! std::all_of (words.begin(), words.end(), [&text] (const std::string& w) { return text.find (w) != std::string::npos; }))
            continue;
        Row row;
        row.source = s.source;
        row.current = s.current;
        row.peak = s.peak;
        row.average = totalAudio > 0 ? 100.0 * (double) s.totalBusy / (double) totalAudio : 0.0;
        row.longestMs = (double) s.longestNs * 1.0e-6;
        row.spikes = s.spikes;
        out.push_back (row);
    }

    std::stable_sort (out.begin(), out.end(), [sort] (const Row& a, const Row& b) {
        switch (sort)
        {
            case SortBy::average: return a.average > b.average;
            case SortBy::peak: return a.peak > b.peak;
            case SortBy::name: return lower (a.source.name) < lower (b.source.name);
            case SortBy::longest: return a.longestMs > b.longestMs;
            case SortBy::spikes: return a.spikes > b.spikes || (a.spikes == b.spikes && a.longestMs > b.longestMs);
            case SortBy::current: break;
        }
        return a.current > b.current;
    });
    return out;
}
} // namespace lynx::perfmon
