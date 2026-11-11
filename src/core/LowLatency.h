#pragma once

// "Low latency while recording" (LYNX extra, round 190aq; TOP-20 item 7 of round 190ak, the first step toward what
// Cubase's "Constrain Delay Compensation" and Studio One's low-latency monitoring do). Off by default.
//
// While an insert carries a live input — it has an audio input source and is armed, or its monitoring is "On" — the
// effects that add latency on its way to the listener are bypassed, and the PDC plan is made without them, so the
// player hears the input without the delay those effects (and the compensation for them) add. They come back when the
// insert is disarmed (at stop with "Disarm the inserts when recording stops"), its monitoring leaves "On", or the
// option is switched off.
//
// Which effects: an active slot whose latency is above 0 samples (what the effect reports, or the latency set by hand
// in a hosted plugin's wrapper settings), on the live insert itself and on every insert its signal passes through on
// the way out: its sends above level 0, its effects' own sends (Fruity Send, Transmitter...) and its route to the
// Master, the Master included. Slots with no latency keep running; so do all slots of inserts not on that path.
//
// What is recorded never changes. A take tapped after the effect slots ("Post effects", "Post EQ", FL's default, "Post
// level and panning", "Post track") holds those effects' sound, and a take of "External and mixer input" or later holds
// what other inserts route into it. So an insert keeps all its slots while an armed insert records them: itself with a
// tap after the slots, or an armed insert it feeds whose take is not "External input only". Only the monitoring path
// changes; the saved project is never touched (the bypass lives in the audio engine's snapshot, not in the song).

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::lowlatency
{
// The inserts audio leaving `from` passes through (`from` itself not included unless a cycle leads back; 0 = Master).
inline std::vector<char> reachOf (const Song& song, int from)
{
    const auto n = (int) song.mixer.size();
    std::vector<char> seen ((std::size_t) std::max (0, n), 0);
    if (from < 0 || from >= n)
        return seen;
    std::vector<int> stack { from };
    auto visit = [&] (int t) {
        if (t >= 0 && t < n && seen[(std::size_t) t] == 0)
        {
            seen[(std::size_t) t] = 1;
            stack.push_back (t);
        }
    };
    while (! stack.empty())
    {
        const auto t = stack.back();
        stack.pop_back();
        if (t == 0)
            continue; // the Master goes to the device
        const auto& track = song.mixer[(std::size_t) t];
        if (track.toMaster)
            visit (0);
        for (const auto& s : track.sends)
            if (s.level > 0.0f) // (a level-0 send is a side-chain link: it carries no sound)
                visit (s.target);
        for (const auto& fx : track.effects)
            if (song.slotActive (fx))
                for (int o = 0; o < maxSlotSends; ++o)
                    visit (slotSendTarget (fx, o));
    }
    return seen;
}

// An insert that carries a live input: an audio input source, and armed or monitored "On".
inline bool isLive (const Song& song, int track, const std::vector<int>& armed) noexcept
{
    if (track <= 0 || track >= (int) song.mixer.size())
        return false;
    const auto& t = song.mixer[(std::size_t) track];
    if (t.inputChannel < 0)
        return false;
    return t.monitor == InputMonitor::on || std::find (armed.begin(), armed.end(), track) != armed.end();
}

// [track] 1 = the latency-adding slots of this insert are bypassed now (empty: none, nothing is live).
inline std::vector<char> bypassTracks (const Song& song, const std::vector<int>& armed)
{
    const auto n = (int) song.mixer.size();
    std::vector<char> path ((std::size_t) std::max (0, n), 0);
    auto any = false;
    for (int t = 1; t < n; ++t)
        if (isLive (song, t, armed))
        {
            any = true;
            path[(std::size_t) t] = 1;
            const auto reach = reachOf (song, t);
            for (int x = 0; x < n; ++x)
                path[(std::size_t) x] = path[(std::size_t) x] != 0 || reach[(std::size_t) x] != 0;
        }
    if (! any)
        return {};

    // what a take holds stays as it is: the inserts an armed insert records keep their slots
    for (const auto d : armed)
    {
        if (d <= 0 || d >= n)
            continue;
        const auto source = song.mixer[(std::size_t) d].recordSource;
        if (source != RecordSource::externalInput && source != RecordSource::externalAndMixer)
            path[(std::size_t) d] = 0; // tapped after its own slots
        if (source == RecordSource::externalInput)
            continue; // the device input alone: nothing routed into it is in the take
        for (int x = 1; x < n; ++x)
            if (x != d && path[(std::size_t) x] != 0 && reachOf (song, x)[(std::size_t) d] != 0)
                path[(std::size_t) x] = 0;
    }
    return std::any_of (path.begin(), path.end(), [] (char c) { return c != 0; }) ? path : std::vector<char> {};
}

// The slots of a track as a bit mask (bit s = slot s), maxEffectSlots ≤ 16.
using SlotMask = std::uint16_t;
inline bool bypassed (const std::vector<SlotMask>& masks, int track, int slot) noexcept
{
    return track >= 0 && (std::size_t) track < masks.size() && slot >= 0 && slot < 16 && ((masks[(std::size_t) track] >> slot) & 1u) != 0;
}
} // namespace lynx::lowlatency
