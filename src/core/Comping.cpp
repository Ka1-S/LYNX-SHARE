#include "core/Comping.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "core/NoteEdit.h"

namespace lynx::comp
{
namespace
{
bool isAudio (const Clip& c) noexcept { return c.kind == ClipKind::audio && c.length > 0; }

// The audio clips on `track`, by start.
std::vector<int> audioOn (const Song& song, int track)
{
    std::vector<int> out;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
        if (song.playlist[(std::size_t) i].track == track && isAudio (song.playlist[(std::size_t) i]))
            out.push_back (i);
    std::stable_sort (out.begin(), out.end(), [&song] (int a, int b) {
        return song.playlist[(std::size_t) a].start < song.playlist[(std::size_t) b].start;
    });
    return out;
}

// Two clips play the same audio at the same song time (a comp piece and the take it came from).
bool aligned (const Clip& a, const Clip& b) noexcept
{
    return a.channel == b.channel && a.offset - a.start == b.offset - b.start && a.offsetFraction == b.offsetFraction
           && a.reversed == b.reversed && a.stretch == b.stretch && a.pitch == b.pitch;
}

// The half of a comp crossfade on each side of a switch (a crossfade's centre is the switch point).
std::int64_t halfIn (const Clip& c) noexcept { return (c.fadeCurve & 1) != 0 ? c.fadeIn / 2 : 0; }
std::int64_t halfOut (const Clip& c) noexcept { return (c.fadeCurve & 2) != 0 ? c.fadeOut / 2 : 0; }

// A lane clip of `g` aligned with `c` that holds [from, to); −1: none.
int laneClipHolding (const Song& song, const Group& g, const Clip& c, std::int64_t from, std::int64_t to)
{
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& l = song.playlist[(std::size_t) i];
        if (g.isLane (l.track) && isAudio (l) && aligned (l, c) && l.start <= from && l.end() >= to)
            return i;
    }
    return -1;
}

// The comp track's audio clips without their uids, by start: what it plays.
std::vector<Clip> compClips (const std::vector<Clip>& playlist, int compTrack)
{
    std::vector<Clip> out;
    for (auto c : playlist)
        if (c.track == compTrack && isAudio (c))
        {
            c.uid = 0;
            out.push_back (c);
        }
    std::stable_sort (out.begin(), out.end(), [] (const Clip& a, const Clip& b) { return a.start != b.start ? a.start < b.start : a.offset < b.offset; });
    return out;
}

// Cuts the comp track's audio clips out of [from, to).
void clear (Song& song, int compTrack, std::int64_t from, std::int64_t to)
{
    std::vector<Clip> kept;
    kept.reserve (song.playlist.size() + 1);
    for (const auto& c : song.playlist)
    {
        if (c.track != compTrack || ! isAudio (c) || c.end() <= from || c.start >= to)
        {
            kept.push_back (c);
            continue;
        }
        if (c.start < from)
        {
            Clip head = c;
            head.length = from - c.start;
            head.fadeOut = 0;
            head.fadeCurve &= (std::uint8_t) ~2;
            head.fadeIn = std::min (head.fadeIn, head.length);
            kept.push_back (head);
        }
        if (c.end() > to)
        {
            Clip tail = c;
            tail.start = to;
            tail.length = c.end() - to;
            tail.offset = c.offset + (to - c.start);
            tail.fadeIn = 0;
            tail.fadeCurve &= (std::uint8_t) ~1;
            tail.fadeOut = std::min (tail.fadeOut, tail.length);
            tail.uid = 0; // a new clip (uids::normalize gives it its own)
            kept.push_back (tail);
        }
    }
    song.playlist = std::move (kept);
}
} // namespace

Group groupOf (const Song& song, int track)
{
    const auto count = (int) song.playlistTracks.size();
    if (track < 0 || track >= count)
        return {};
    auto header = track;
    while (header > 0 && song.playlistTracks[(std::size_t) header].grouped)
        --header;
    if (song.playlistTracks[(std::size_t) header].grouped)
        return {};
    Group g;
    g.comp = header;
    g.first = header + 1;
    g.last = header;
    while (g.last + 1 < count && song.playlistTracks[(std::size_t) g.last + 1].grouped)
        ++g.last;
    if (g.last < g.first)
        return {};
    return g;
}

std::int64_t crossfadeTicks (const Song& song)
{
    const auto ms = std::clamp ((double) song.compCrossfadeMs, 0.0, 100.0);
    const auto ticks = ms / 1000.0 * std::max (song.bpm, 1.0) / 60.0 * ppq;
    return 2 * (std::int64_t) std::llround (ticks / 2.0);
}

int sourceOf (const Song& song, const Group& g, int compClip)
{
    if (! g.valid() || compClip < 0 || compClip >= (int) song.playlist.size())
        return -1;
    const auto& c = song.playlist[(std::size_t) compClip];
    if (c.track != g.comp || ! isAudio (c))
        return -1;
    return laneClipHolding (song, g, c, c.start + halfIn (c), c.end() - halfOut (c));
}

std::vector<Used> usedRanges (const Song& song, const Group& g)
{
    std::vector<Used> out;
    if (! g.valid())
        return out;
    for (const auto i : audioOn (song, g.comp))
        if (const auto source = sourceOf (song, g, i); source >= 0)
        {
            const auto& c = song.playlist[(std::size_t) i];
            out.push_back ({ song.playlist[(std::size_t) source].track, source, i, c.start + halfIn (c), c.end() - halfOut (c) });
        }
    return out;
}

void removeCrossfades (Song& song, int compTrack)
{
    const auto order = audioOn (song, compTrack);
    std::vector<bool> inDone (song.playlist.size()), outDone (song.playlist.size());
    for (std::size_t k = 1; k < order.size(); ++k)
    {
        auto& a = song.playlist[(std::size_t) order[k - 1]];
        auto& b = song.playlist[(std::size_t) order[k]];
        if ((a.fadeCurve & 2) == 0 || (b.fadeCurve & 1) == 0 || b.start >= a.end() || b.start <= a.start)
            continue;
        const auto mid = b.start + (a.end() - b.start) / 2;
        if (mid >= b.end())
            continue;
        a.length = mid - a.start;
        b.offset += mid - b.start;
        b.length -= mid - b.start;
        b.start = mid;
        a.fadeOut = b.fadeIn = 0;
        a.fadeCurve &= (std::uint8_t) ~2;
        b.fadeCurve &= (std::uint8_t) ~1;
        outDone[(std::size_t) order[k - 1]] = inDone[(std::size_t) order[k]] = true;
    }
    // a crossfade whose other side is gone (the neighbour deleted or moved): back to the switch point
    for (const auto i : order)
    {
        auto& c = song.playlist[(std::size_t) i];
        if ((c.fadeCurve & 1) != 0 && ! inDone[(std::size_t) i] && c.length > c.fadeIn / 2)
        {
            const auto h = c.fadeIn / 2;
            c.start += h, c.offset += h, c.length -= h, c.fadeIn = 0;
            c.fadeCurve &= (std::uint8_t) ~1;
        }
        if ((c.fadeCurve & 2) != 0 && ! outDone[(std::size_t) i] && c.length > c.fadeOut / 2)
        {
            c.length -= c.fadeOut / 2, c.fadeOut = 0;
            c.fadeCurve &= (std::uint8_t) ~2;
        }
    }
}

void applyCrossfades (Song& song, int compTrack, std::int64_t ticks)
{
    removeCrossfades (song, compTrack);
    const auto g = groupOf (song, compTrack);
    const auto order = audioOn (song, compTrack);
    const auto half = std::max<std::int64_t> (0, ticks / 2);
    for (std::size_t k = 1; k < order.size(); ++k)
    {
        const auto ia = order[k - 1], ib = order[k];
        auto& a = song.playlist[(std::size_t) ia];
        auto& b = song.playlist[(std::size_t) ib];
        if (a.end() != b.start)
            continue; // not a switch point (a gap, or clips the user stacked)
        if (half == 0)
        {
            a.fadeOut = b.fadeIn = 0; // a hard cut
            continue;
        }
        // how far each take has audio past the switch: the lane clip it came from (a clip that is no lane's piece can
        // still start earlier by the audio its offset skips, never end later: its file's length is not known here)
        const auto sa = g.comp == compTrack ? sourceOf (song, g, ia) : -1;
        const auto sb = g.comp == compTrack ? sourceOf (song, g, ib) : -1;
        const auto roomA = sa >= 0 ? song.playlist[(std::size_t) sa].end() - a.end() : 0;
        // (b may start before its lane clip by the audio the file has there: a punch-in's pre-roll)
        const auto roomB = sb >= 0 ? b.start - song.playlist[(std::size_t) sb].start + song.playlist[(std::size_t) sb].offset : b.offset;
        const auto lengthA = a.length - halfIn (a), lengthB = b.length;
        const auto e = std::min ({ half, roomA, roomB, lengthA / 2, lengthB / 2 });
        if (e <= 0)
        {
            // no audio to overlap: short linear fades on both sides (round 163's), no click
            const auto f = std::min<std::int64_t> ({ half, 8, lengthA / 2, lengthB / 2 });
            a.fadeOut = b.fadeIn = std::max<std::int64_t> (0, f);
            continue;
        }
        a.length += e;
        b.start -= e;
        b.offset -= e;
        b.length += e;
        a.fadeOut = b.fadeIn = 2 * e;
        a.fadeCurve |= 2;
        b.fadeCurve |= 1;
    }
}

bool swipe (Song& song, int lane, std::int64_t from, std::int64_t to)
{
    const auto g = groupOf (song, lane);
    if (! g.isLane (lane) || to <= from)
        return false;
    auto before = song.playlist;

    removeCrossfades (song, g.comp);
    clear (song, g.comp, from, to);
    std::vector<Clip> pieces;
    for (const auto& l : song.playlist)
    {
        if (l.track != lane || ! isAudio (l) || l.end() <= from || l.start >= to)
            continue;
        Clip piece = l;
        const auto s = std::max (from, l.start), e = std::min (to, l.end());
        piece.track = g.comp;
        piece.start = s;
        piece.length = e - s;
        piece.offset = l.offset + (s - l.start);
        piece.muted = false;
        piece.fadeIn = s == l.start ? std::min (l.fadeIn, piece.length) : 0;
        piece.fadeOut = e == l.end() ? std::min (l.fadeOut, piece.length) : 0;
        piece.fadeCurve = 0;
        piece.crossfadeIn = piece.crossfadeOut = 0;
        piece.uid = 0;
        pieces.push_back (piece);
    }
    song.playlist.insert (song.playlist.end(), pieces.begin(), pieces.end());
    applyCrossfades (song, g.comp, crossfadeTicks (song));
    if (compClips (song.playlist, g.comp) == compClips (before, g.comp)) // the comp already played that (uids, order aside)
    {
        song.playlist = std::move (before);
        return false;
    }
    return true;
}

bool swipeClip (Song& song, int clipIndex, std::int64_t from, std::int64_t to)
{
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size())
        return false;
    const auto& l = song.playlist[(std::size_t) clipIndex];
    if (! isAudio (l))
        return false;
    return swipe (song, l.track, std::max (from, l.start), std::min (to, l.end()));
}

void setAudition (Song& song, int lane)
{
    const auto g = groupOf (song, lane);
    if (! g.isLane (lane))
    {
        song.auditionLane = song.auditionComp = -1;
        return;
    }
    song.auditionLane = lane;
    song.auditionComp = g.comp;
}

bool flatten (Song& song, int compTrack)
{
    const auto g = groupOf (song, compTrack);
    if (! g.valid() || g.comp != compTrack)
        return false;
    for (auto t = g.last; t >= g.first; --t)
        noteedit::removeTrack (song, t);
    if (compTrack < (int) song.playlistTracks.size())
    {
        song.playlistTracks[(std::size_t) compTrack].collapsed = false;
        song.playlistTracks[(std::size_t) compTrack].compTrack = false;
    }
    setAudition (song, -1);
    return true;
}

bool isComp (const Song& song, int track)
{
    const auto g = groupOf (song, track);
    return g.valid() && g.comp == track && song.playlistTracks[(std::size_t) track].compTrack;
}

bool canComp (const Song& song, int track)
{
    const auto g = groupOf (song, track);
    if (! g.valid() || isComp (song, g.comp))
        return false;
    return std::any_of (song.playlist.begin(), song.playlist.end(),
                        [&g] (const Clip& c) { return c.track >= g.comp && c.track <= g.last && isAudio (c); });
}

int compTakes (Song& song, int track)
{
    if (! canComp (song, track))
        return -1;
    const auto g = groupOf (song, track);

    // the recording order of the rows: by each row's latest clip (a pass has its own channel, or a later place in the file)
    using Key = std::pair<int, std::int64_t>;
    std::vector<std::pair<Key, int>> rows;
    for (auto t = g.comp; t <= g.last; ++t)
    {
        const auto clips = audioOn (song, t);
        if (clips.empty())
            continue;
        auto key = Key { -1, 0 };
        for (const auto i : clips)
        {
            const auto& c = song.playlist[(std::size_t) i];
            key = std::max (key, Key { c.channel, c.offset - c.start });
        }
        rows.push_back ({ key, t });
    }
    std::stable_sort (rows.begin(), rows.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

    // the names: the header's, else the insert's (loop passes leave their rows unnamed, as in FL)
    auto base = song.playlistTracks[(std::size_t) g.comp].name;
    if (base.empty())
        for (const auto i : audioOn (song, rows.front().second))
        {
            const auto channel = song.playlist[(std::size_t) i].channel;
            if (channel >= 0 && channel < (int) song.channels.size())
            {
                const auto insert = song.channels[(std::size_t) channel].mixerTrack;
                if (insert > 0 && insert < (int) song.mixer.size())
                    base = song.mixer[(std::size_t) insert].name;
            }
            break;
        }
    noteedit::insertTrack (song, g.comp);
    auto& comp = song.playlistTracks[(std::size_t) g.comp];
    comp.compTrack = true;
    comp.name = base.empty() ? "Comp" : base + " comp";

    for (auto t = g.comp + 1; t <= g.last + 1; ++t)
    {
        auto& lane = song.playlistTracks[(std::size_t) t];
        lane.grouped = true;
        lane.collapsed = false;
        if (lane.mode == PlaylistTrackMode::audio) // a later take goes to a new track, never onto a lane
        {
            lane.mode = PlaylistTrackMode::unassigned;
            lane.mixerTrack = -1;
        }
    }
    for (std::size_t k = 0; k < rows.size(); ++k)
    {
        auto& lane = song.playlistTracks[(std::size_t) rows[k].second + 1];
        if (lane.name.empty() || lane.name == base)
            lane.name = (base.empty() ? "Take " + std::to_string (k + 1) : base + " (take " + std::to_string (k + 1) + ")");
    }
    for (auto& c : song.playlist)
        if (c.track > g.comp && c.track <= g.last + 1 && isAudio (c))
            c.muted = true;
    setAudition (song, -1);

    const auto last = rows.back().second + 1;
    std::int64_t from = -1, to = 0;
    for (const auto i : audioOn (song, last))
    {
        const auto& c = song.playlist[(std::size_t) i];
        from = from < 0 ? c.start : std::min (from, c.start);
        to = std::max (to, c.end());
    }
    swipe (song, last, from, to);
    return g.comp;
}
} // namespace lynx::comp
