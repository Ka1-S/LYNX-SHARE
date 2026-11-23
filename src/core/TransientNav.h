#pragma once

// Round 190on (research190oe P8): Tab to transient, Pro Tools' editing move (its "Tab to Transients": Tab moves the
// insertion point to the next transient, Option/Ctrl+Tab back, Shift extends the selection; with nothing to stop at, the
// clip boundaries). A LYNX extra: FL Studio 2026 and Live 12 have no such keys (Live's Sample Editor shows its transients
// and moves between warp markers with Ctrl+Left / Right; its manual's chapter 9 and 42.11), so the actions are unbound in
// every layout and live in the Shortcut manager and the command palette.
//
// The stops are the attacks LYNX already finds for the audio editor's transient ticks and Quantize audio
// (clipwarp::attacks: stretchdetail::detectAttacks, 6 dB), placed in song time as the groove extraction places them
// (grooveaudio::contentBeat: through the channel's warp markers when it is warped). A clip with no attack in its playing
// portion stops at its two edges instead. No JUCE.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/GrooveAudio.h"
#include "core/Song.h"

namespace lynx::transientnav
{
// The song ticks of `clip`'s attacks (`attackSeconds`: file seconds, as clipwarp::attacks gives them) inside its playing
// portion, in order. A clip that plays backwards has none (its attacks are not where the file has them).
inline std::vector<std::int64_t> clipStops (const Song& song, const Clip& clip, const std::vector<double>& attackSeconds)
{
    std::vector<std::int64_t> out;
    if (clip.kind != ClipKind::audio || clip.channel < 0 || clip.channel >= (int) song.channels.size() || clip.length <= 0 || clip.reversed)
        return out;
    const auto& s = song.channels[(std::size_t) clip.channel].sampler;
    if (s.reverse)
        return out;
    const auto stretch = std::clamp (clip.stretch, 0.01, 100.0);
    for (const auto t : attackSeconds)
    {
        const auto tick = (std::int64_t) std::llround (grooveaudio::contentBeat (s, t, song.bpm) * ppq * stretch - ((double) clip.offset + (double) clip.offsetFraction));
        if (tick < 0 || tick >= clip.length)
            continue;
        out.push_back (clip.start + tick);
    }
    return out;
}

// Every stop of `clips` (indices into the playlist): their attacks, or the edges of a clip that has none. Sorted, once
// each. `attacksOf (index)` gives a clip's attacks in file seconds.
template <typename AttacksOf>
std::vector<std::int64_t> stops (const Song& song, const std::vector<int>& clips, AttacksOf&& attacksOf)
{
    std::vector<std::int64_t> out;
    for (const auto i : clips)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            continue;
        const auto& c = song.playlist[(std::size_t) i];
        if (c.kind != ClipKind::audio)
            continue;
        auto ticks = clipStops (song, c, attacksOf (i));
        if (ticks.empty())
            ticks = { c.start, c.start + c.length };
        out.insert (out.end(), ticks.begin(), ticks.end());
    }
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

// The next stop after `from` (direction > 0) or the one before it (< 0); nothing when there is none that way.
inline std::optional<std::int64_t> step (const std::vector<std::int64_t>& sorted, std::int64_t from, int direction)
{
    if (direction > 0)
    {
        const auto it = std::upper_bound (sorted.begin(), sorted.end(), from);
        return it == sorted.end() ? std::nullopt : std::optional<std::int64_t> (*it);
    }
    const auto it = std::lower_bound (sorted.begin(), sorted.end(), from);
    return it == sorted.begin() ? std::nullopt : std::optional<std::int64_t> (*(it - 1));
}

// A time selection [from, to) (empty: to <= from).
struct Range
{
    std::int64_t from = 0, to = 0;
    bool empty() const noexcept { return to <= from; }
    bool operator== (const Range&) const = default;
};

// Shift + the move: with no selection, from `position` to the next (previous) stop; with one, its end moves to the next
// stop after it (its start to the stop before it). Unchanged when there is no stop that way.
inline Range extend (Range selection, std::int64_t position, const std::vector<std::int64_t>& sorted, int direction)
{
    if (selection.empty())
    {
        const auto t = step (sorted, position, direction);
        if (! t)
            return selection;
        return direction > 0 ? Range { position, *t } : Range { *t, position };
    }
    if (direction > 0)
    {
        if (const auto t = step (sorted, selection.to, 1))
            selection.to = *t;
        return selection;
    }
    if (const auto t = step (sorted, selection.from, -1))
        selection.from = *t;
    return selection;
}

// The same in frames for the audio editor: its attacks (frames), or with none, its markers' and regions' edges and the
// file's two ends (`fallback`). Sorted, once each.
inline std::vector<std::int64_t> frameStops (std::vector<std::int64_t> attacks, std::vector<std::int64_t> fallback)
{
    auto& out = attacks.empty() ? fallback : attacks;
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}
} // namespace lynx::transientnav
