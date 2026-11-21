#pragma once

// FL's automation clip channels in the Channel rack (round 190oo, design review 2 PAT-7; the blocker of details DE-123).
//
// FL keeps every automation clip as a channel of the rack (.flp channel type 5, "automation clip"): a row among the other
// channels, its button in the clip's colour, a curve glyph where the mixer track box is, the two knobs (the clip's range),
// the steps; its button opens the clip (Thief: "Volume Duck", "GROSS..w Bass", "CRASH REVERB" between YELP and
// "Riser..Metal", design190ia shots/ref/fl-channelrack.png). LYNX keeps automation clips apart from the channels
// (Song::automations), so the rack shows them as rows of their own between the channel rows and never as channels: no
// audio, no notes, no mixer insert, not in the channel selection, the channel indices unchanged.
//
// Where a row sits is Automation::rackSlot, the gap it fills: above channel row `rackSlot` (FL's channel order from the
// .flp); −1 = after the last channel. The model's choice: −1 is what every automation of an older LYNX project and every
// clip made in LYNX has, so a project without automation clips keeps its rack row for row, and one with clips shows them
// after its channels, where FL appends a channel made last (FL lists every automation clip in the rack). Inside one gap
// the rows follow the automations' order. No JUCE: tests/RackAutomation190ooTests.inc.

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "core/RackFilter.h"
#include "core/Song.h"

namespace lynx::rackauto
{
// FL's built-in filter group of the automation clip channels (REA_rodada168t §3: FL files an automation clip channel
// under "Automation clips"; an imported .flp keeps the group's name)
constexpr const char* groupName = "Automation clips";

struct Row
{
    int automation = -1; // index into Song::automations
    int gap = 0;         // the row sits above channel row `gap` (channels.size(): after the last channel)
    bool operator== (const Row&) const = default;
};

// The gap of automation `a`'s row (−1 and anything past the end: after the last channel).
inline int gapOf (const Song& song, const Automation& a) noexcept
{
    const auto count = (int) song.channels.size();
    return a.rackSlot < 0 || a.rackSlot > count ? count : a.rackSlot;
}

// The automations that are FL automation clips, so they get a row: a clip with points; not a live controller link (FL's
// Peak / Envelope controller links are no clip), not the ruler's tempo markers (round 190hq) and not an automation lane
// under a Playlist track (Live / Bitwig style, round 177b).
inline std::vector<char> clipFlags (const Song& song)
{
    std::vector<char> flags (song.automations.size(), 1);
    for (std::size_t i = 0; i < song.automations.size(); ++i)
    {
        const auto& a = song.automations[i];
        if (a.isLive() || a.tempoMarkers || a.points.empty())
            flags[i] = 0;
    }
    for (const auto& t : song.playlistTracks)
        for (const auto& l : t.lanes)
            if (l.automation >= 0 && l.automation < (int) flags.size())
                flags[(std::size_t) l.automation] = 0;
    return flags;
}

inline bool listed (const Song& song, int automation)
{
    if (automation < 0 || automation >= (int) song.automations.size())
        return false;
    return clipFlags (song)[(std::size_t) automation] != 0;
}

// The automation rows in the rack's order: by gap, then in the automations' order.
inline std::vector<Row> rows (const Song& song)
{
    std::vector<Row> out;
    const auto flags = clipFlags (song);
    for (int i = 0; i < (int) song.automations.size(); ++i)
        if (flags[(std::size_t) i] != 0)
            out.push_back ({ i, gapOf (song, song.automations[(std::size_t) i]) });
    std::stable_sort (out.begin(), out.end(), [] (const Row& x, const Row& y) { return x.gap < y.gap; });
    return out;
}

// before[k] = how many of `rows` sit above channel row k (k = channels.size(): the "add channel" row).
inline std::vector<int> rowsBefore (const Song& song, const std::vector<Row>& rows)
{
    std::vector<int> before (song.channels.size() + 1, 0);
    for (const auto& r : rows)
        if (r.gap >= 0 && r.gap < (int) before.size())
            ++before[(std::size_t) r.gap];
    for (std::size_t k = 1; k < before.size(); ++k)
        before[k] += before[k - 1];
    return before;
}

// The whole rack top to bottom: channel rows and automation rows interleaved (the "add channel" row not included).
struct Entry
{
    bool automation = false;
    int index = -1; // a channel, or an automation
    bool operator== (const Entry&) const = default;
};

inline std::vector<Entry> order (const Song& song)
{
    std::vector<Entry> out;
    const auto list = rows (song);
    std::size_t next = 0;
    for (int c = 0; c <= (int) song.channels.size(); ++c)
    {
        for (; next < list.size() && list[next].gap == c; ++next)
            out.push_back ({ true, list[next].automation });
        if (c < (int) song.channels.size())
            out.push_back ({ false, c });
    }
    return out;
}

// Whether the rack shows the automation rows under its filter: "All", and FL's "Automation clips" group (a group of
// that name from an imported .flp, whose channels are FL's automation clips). Every other view lists channels only.
inline bool shownUnder (const Song& song, int filter)
{
    if (filter == rackfilter::all)
        return true;
    return filter >= 0 && filter < (int) song.channelGroups.size() && song.channelGroups[(std::size_t) filter] == groupName;
}

// --- keeping the rows in place while the channels change ------------------------------------------------------------

// `count` channels were inserted at row `index`: the rows at or below it move down with the channels there (a channel
// inserted where an automation row is goes above it, as FL puts a new channel right under the selected one).
inline void channelsInserted (Song& song, int index, int count = 1) noexcept
{
    for (auto& a : song.automations)
        if (a.rackSlot >= index)
            a.rackSlot += count;
}

// --- moving rows (FL: an automation clip channel moves as any channel does, Alt+Up / Alt+Down) ----------------------

// Moves automation `automation`'s row past the channel row above (delta < 0) or below it; false at the top / bottom.
inline bool moveRow (Song& song, int automation, int delta)
{
    if (automation < 0 || automation >= (int) song.automations.size() || delta == 0)
        return false;
    auto& a = song.automations[(std::size_t) automation];
    const auto count = (int) song.channels.size();
    const auto from = gapOf (song, a);
    const auto to = std::clamp (from + (delta < 0 ? -1 : 1), 0, count);
    if (to == from)
        return false;
    a.rackSlot = to;
    return true;
}

// The selected channels moved one row (delta < 0: up) pass an automation row next to them, as FL moves channels past an
// automation clip channel: when the selection is one block of channels and an automation row sits right above it (up)
// or right below it (down), that row goes to the block's other side and the channels keep their rows. Returns the
// automation and its new rackSlot, {-1, 0} when there is none (the channels move as before, moveChannels).
inline std::pair<int, int> passFor (const Song& song, std::vector<int> selection, int delta)
{
    std::sort (selection.begin(), selection.end());
    selection.erase (std::unique (selection.begin(), selection.end()), selection.end());
    const auto count = (int) song.channels.size();
    if (selection.empty() || delta == 0 || selection.front() < 0 || selection.back() >= count
        || selection.back() - selection.front() + 1 != (int) selection.size())
        return { -1, 0 };
    const auto first = selection.front(), last = selection.back();
    const auto list = rows (song);
    if (delta < 0) // the lowest row right above the block goes right below it
    {
        for (auto it = list.rbegin(); it != list.rend(); ++it)
            if (it->gap == first)
                return { it->automation, last + 1 };
    }
    else // the highest row right below the block goes right above it
    {
        for (const auto& r : list)
            if (r.gap == last + 1)
                return { r.automation, first };
    }
    return { -1, 0 };
}

// --- FL's triggered automation clips (round 190ow, details DE-123) -------------------------------------------------
// A step / note on an automation clip row starts the clip from its beginning and plays it while the note is held (FL's
// manual, "Triggering"; a step of no length plays the clip once). The engine plays them (RackState::buildTriggerClips,
// RackRenderer::updatePatternTriggers); this is the same rule for the UI: where the clip plays at `tick`.

inline std::int64_t heldFor (const Automation& a, const Note& n) noexcept
{
    return n.length > 0 ? n.length : a.length();
}

inline bool hasTriggers (const Song& song, int automation) noexcept
{
    for (const auto& p : song.patterns)
        for (const auto& n : p.autoNotes)
            if (n.channel == automation)
                return true;
    return false;
}

// The step / note of automation `automation` that started last at or before transport position `tick` (Song mode: the
// Playlist's pattern clips; Pattern mode: the current pattern in its loop): its start, how long it has played and how
// long it holds the clip. start < 0: none yet.
struct Trigger
{
    double start = -1.0, since = 0.0, held = 0.0;
    bool playing() const noexcept { return start >= 0.0 && since < held; }
};

inline Trigger lastTrigger (const Song& song, int automation, double tick)
{
    Trigger best;
    if (automation < 0 || automation >= (int) song.automations.size() || song.automations[(std::size_t) automation].muted)
        return best;
    const auto& a = song.automations[(std::size_t) automation];
    const auto meter = song.meter();
    const auto consider = [&] (double start, const Note& n, double limit) {
        const auto since = tick - start;
        if (since < 0.0 || start < best.start)
            return;
        best = { start, since, std::max (1.0, std::min ((double) heldFor (a, n), limit - start)) };
    };
    if (song.mode != PlayMode::song)
    {
        if (song.currentPattern < 0 || song.currentPattern >= (int) song.patterns.size())
            return best;
        const auto& p = song.patterns[(std::size_t) song.currentPattern];
        const auto length = (double) std::max<std::int64_t> (1, p.effectiveLength (meter));
        const auto base = std::floor (std::max (0.0, tick) / length) * length;
        for (const auto& n : p.autoNotes)
            if (n.channel == automation && ! n.muted)
            {
                if (base > 0.0)
                    consider (base - length + (double) n.start, n, 1.0e300);
                consider (base + (double) n.start, n, 1.0e300);
            }
        return best;
    }
    for (const auto& c : song.playlist)
    {
        if (c.kind != ClipKind::pattern || c.pattern < 0 || c.pattern >= (int) song.patterns.size() || c.muted || c.length <= 0
            || tick < (double) c.start || song.isTrackMuted (c.track) || c.start < song.performanceZoneEnd())
            continue;
        const auto& p = song.patterns[(std::size_t) c.pattern];
        if (p.autoNotes.empty())
            continue;
        const auto passes = clipPasses (c, p.effectiveLength (meter));
        for (auto k = passes.first; k <= passes.last; ++k)
            for (const auto& n : p.autoNotes)
            {
                if (n.channel != automation || n.muted || (passes.loops && n.start >= passes.length))
                    continue;
                const auto at = c.start - c.offset + (passes.loops ? k * passes.length : 0) + n.start;
                if (at >= c.start && at < c.end()) // (it plays on past the clip's end, as FL's note does)
                    consider ((double) at, n, 1.0e300);
            }
    }
    return best;
}

// The clip time the automation plays at `tick` because of a step / note (−1: none playing now; the editor's playhead)
inline double triggeredAt (const Song& song, int automation, double tick)
{
    const auto t = lastTrigger (song, automation, tick);
    return t.playing() ? t.since : -1.0;
}

// The value the step / note started last gives the automation's target at `tick` (it holds the value it reached once
// the note ends), and its start; NaN / −1 when none has started yet (the knobs on screen, AutomationDisplay)
inline std::pair<float, double> triggeredValue (const Song& song, int automation, double tick)
{
    const auto t = lastTrigger (song, automation, tick);
    if (t.start < 0.0)
        return { std::numeric_limits<float>::quiet_NaN(), -1.0 };
    return { song.automations[(std::size_t) automation].playAt (std::min (t.since, t.held), tick), t.start };
}

inline bool passRow (Song& song, const std::vector<int>& selection, int delta)
{
    const auto [automation, slot] = passFor (song, selection, delta);
    if (automation < 0)
        return false;
    song.automations[(std::size_t) automation].rackSlot = slot;
    return true;
}
} // namespace lynx::rackauto
