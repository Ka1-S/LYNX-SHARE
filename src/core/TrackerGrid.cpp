#include "core/TrackerGrid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

#include "core/NoteCurves.h"
#include "core/NoteNames.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx::tracker
{
namespace
{
const float defaultVelocity = Note {}.velocity;

int hexDigits (std::int64_t maxValue) noexcept
{
    int d = 1;
    while (maxValue >= 16)
    {
        maxValue /= 16;
        ++d;
    }
    return d;
}

std::int64_t endOf (const Note& n) noexcept { return n.start + std::max<std::int64_t> (1, n.length); }

void insertSorted (std::vector<Note>& notes, const Note& n, int* index = nullptr)
{
    const auto at = std::upper_bound (notes.begin(), notes.end(), n.start, [] (std::int64_t t, const Note& o) { return t < o.start; });
    const auto i = (int) (at - notes.begin());
    notes.insert (at, n);
    if (index != nullptr)
        *index = i;
}

void eraseIndices (std::vector<Note>& notes, std::vector<int> indices)
{
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        if (*it >= 0 && *it < (int) notes.size())
            notes.erase (notes.begin() + *it);
}

bool validNote (const Pattern& p, int i) noexcept { return i >= 0 && i < (int) p.notes.size(); }

// A note's start moves (its Dly): a note of the same column that ended exactly where it started (legato) keeps ending
// at its new start, so the two stay one column and the OFF does not appear between them.
void keepLegato (Pattern& pattern, const Layout& layout, const Placed& moved, std::int64_t oldStart, std::int64_t newStart)
{
    if (moved.track < 0 || moved.track >= (int) layout.columns.size() || moved.column >= (int) layout.columns[(std::size_t) moved.track].size())
        return;
    for (auto pi : layout.columns[(std::size_t) moved.track][(std::size_t) moved.column])
    {
        const auto& p = layout.placed[(std::size_t) pi];
        if (p.note == moved.note || ! validNote (pattern, p.note))
            continue;
        auto& n = pattern.notes[(std::size_t) p.note];
        if (n.start < newStart && n.start + std::max<std::int64_t> (1, n.length) == oldStart)
            n.length = newStart - n.start;
    }
}

// the first note of the column starting after row `row` (its start tick), or the pattern's end
std::int64_t nextStartInColumn (const Layout& layout, const Pattern& pattern, int track, int column, std::int64_t row)
{
    if (track < 0 || track >= (int) layout.columns.size() || column < 0 || column >= (int) layout.columns[(std::size_t) track].size())
        return layout.rows * layout.rowTicks;
    for (auto pi : layout.columns[(std::size_t) track][(std::size_t) column])
    {
        const auto& p = layout.placed[(std::size_t) pi];
        if (p.startRow > row && validNote (pattern, p.note))
            return pattern.notes[(std::size_t) p.note].start;
    }
    return std::max (layout.rows * layout.rowTicks, layout.rowStart (row + 1));
}
} // namespace

// ------------------------------------------------------------------------------------------------------------- fx
char fxLetter (Fx fx) noexcept
{
    static const char letters[fxKinds] = { 'G', 'P', 'Y', 'R', 'T', 'M', 'C', 'B', 'V' };
    return letters[(int) fx];
}

std::optional<Fx> fxFromLetter (char letter) noexcept
{
    const auto c = (char) (letter >= 'a' && letter <= 'z' ? letter - 'a' + 'A' : letter);
    for (int i = 0; i < fxKinds; ++i)
        if (fxLetter ((Fx) i) == c)
            return (Fx) i;
    return std::nullopt;
}

const char* fxDescription (Fx fx) noexcept
{
    switch (fx)
    {
        case Fx::slide: return TR_NOOP ("Gxx glide into this note (FL slide note); 01 on");
        case Fx::porta: return TR_NOOP ("Pxx portamento from the previous note; 01 on");
        case Fx::chance: return TR_NOOP ("Yxx chance the note plays, 00 to FF (FF = always)");
        case Fx::release: return TR_NOOP ("Rxx release velocity, 00 to 7F");
        case Fx::finePitch: return TR_NOOP ("Txx fine pitch, 80 = none, each step one cent");
        case Fx::mute: return TR_NOOP ("Mxx mute the note; 01 muted");
        case Fx::cut: return TR_NOOP ("Cxx the note ends inside its row, after xx ticks");
        case Fx::bend: return TR_NOOP ("Bxx pitch curve: its end in semitones, 80 = the key");
        case Fx::volume: return TR_NOOP ("Vxx channel volume from this row, 80 = 100 %");
    }
    return "";
}

bool fxActive (const Note& n, Fx fx) noexcept
{
    switch (fx)
    {
        case Fx::slide: return n.slide;
        case Fx::porta: return n.porta;
        case Fx::chance: return n.chance < 1.0f;
        case Fx::release: return std::abs (n.release - 0.5f) > 1.0e-6f;
        case Fx::finePitch: return n.finePitch != 0.0f;
        case Fx::mute: return n.muted;
        case Fx::cut: return false; // (a layout fact: Placed::shortNote)
        case Fx::bend: return n.curves.has() && ! n.curves->lane (NoteCurveLane::pitch).empty();
        case Fx::volume: return false; // (the channel's pattern events: Track::volumeSlot)
    }
    return false;
}

int fxValueOf (const Note& n, Fx fx, std::int64_t length)
{
    switch (fx)
    {
        case Fx::slide: return n.slide ? 1 : 0;
        case Fx::porta: return n.porta ? 1 : 0;
        case Fx::chance: return (int) std::lround (std::clamp (n.chance, 0.0f, 1.0f) * 255.0f);
        case Fx::release: return (int) std::lround (std::clamp (n.release, 0.0f, 1.0f) * 127.0f);
        case Fx::finePitch: return std::clamp ((int) std::lround (n.finePitch) + 0x80, 0x08, 0xF8);
        case Fx::mute: return n.muted ? 1 : 0;
        case Fx::cut: return (int) std::max<std::int64_t> (1, length);
        case Fx::bend:
        {
            const auto& lane = n.curves->lane (NoteCurveLane::pitch);
            return lane.empty() ? 0x80 : std::clamp ((int) std::lround (lane.back().value / 100.0f) + 0x80, 0x50, 0xB0);
        }
        case Fx::volume: return 0;
    }
    return 0;
}

const PatternEvents* volumeLane (const Pattern& pattern, int channel) noexcept
{
    for (const auto& lane : pattern.events)
        if (lane.target.kind == AutomationTargetKind::channelAdjust && lane.target.param == leveladjust::volume && lane.target.index == channel
            && ! lane.events.empty())
            return &lane;
    return nullptr;
}
int volumeValueOf (float eventValue) noexcept { return std::clamp ((int) std::lround (eventValue * 256.0f), 0, 255); }
float volumeEventValue (int shown) noexcept { return (float) std::clamp (shown, 0, 255) / 256.0f; }

bool isVolumeSlot (const Layout& layout, const Cursor& at) noexcept
{
    if (at.track < 0 || at.track >= (int) layout.tracks.size() || at.column != 0 || (at.field != Field::fxLetter && at.field != Field::fxValue))
        return false;
    const auto slot = layout.tracks[(std::size_t) at.track].volumeSlot;
    return slot >= 0 && at.fxSlot == slot;
}

namespace
{
void setFxValue (Note& n, Fx fx, int v, std::int64_t rowTicks)
{
    switch (fx)
    {
        case Fx::slide: n.slide = v != 0; break;
        case Fx::porta: n.porta = v != 0; break;
        case Fx::chance: n.chance = std::clamp (v, 0, 255) / 255.0f; break;
        case Fx::release: n.release = std::clamp (v, 0, 127) / 127.0f; break;
        case Fx::finePitch: n.finePitch = (float) (std::clamp (v, 0x08, 0xF8) - 0x80); break;
        case Fx::mute: n.muted = v != 0; break;
        case Fx::cut:
        {
            const auto room = rowTicks - (n.start % rowTicks + rowTicks) % rowTicks;
            n.length = std::clamp<std::int64_t> (v, 1, std::max<std::int64_t> (1, room - 1));
            break;
        }
        case Fx::bend:
        {
            // a typed value: a glide from the key to xx − 80 semitones over the note's first row, then held (an imported
            // curve shows the pitch it ends at; typing replaces it)
            auto& lane = n.curves.edit().lane (NoteCurveLane::pitch);
            lane = { { 0.0f, 0.0f }, { (float) rowTicks, (float) ((std::clamp (v, 0x50, 0xB0) - 0x80) * 100) } };
            n.curves.tidy();
            break;
        }
        case Fx::volume: break;
    }
}

void resetFx (Note& n, Fx fx, std::int64_t rowTicks)
{
    const Note d;
    switch (fx)
    {
        case Fx::slide: n.slide = d.slide; break;
        case Fx::porta: n.porta = d.porta; break;
        case Fx::chance: n.chance = d.chance; break;
        case Fx::release: n.release = d.release; break;
        case Fx::finePitch: n.finePitch = d.finePitch; break;
        case Fx::mute: n.muted = d.muted; break;
        case Fx::cut:
        {
            // the note lasts to the next row's start
            const auto inRow = (n.start % rowTicks + rowTicks) % rowTicks;
            n.length = rowTicks - inRow;
            break;
        }
        case Fx::bend:
            if (n.curves.has())
            {
                n.curves.edit().lane (NoteCurveLane::pitch).clear();
                n.curves.tidy();
            }
            break;
        case Fx::volume: break;
    }
}

int defaultOnValue (Fx fx, std::int64_t rowTicks)
{
    switch (fx)
    {
        case Fx::slide:
        case Fx::porta:
        case Fx::mute: return 1;
        case Fx::chance: return 0x80;
        case Fx::release: return 0x7F;
        case Fx::finePitch: return 0x81;
        case Fx::cut: return (int) std::max<std::int64_t> (1, rowTicks / 2);
        case Fx::bend: return 0x81;
        case Fx::volume: return 0x80;
    }
    return 1;
}

// round 190om: the volume events in row `row` (ticks [from, from + rowTicks)) of the track's channel go; `value` ≥ 0: one
// event at the row's start takes their place
bool setVolumeRow (Pattern& pattern, const Layout& layout, int track, std::int64_t row, int value)
{
    if (track < 0 || track >= (int) layout.tracks.size() || layout.tracks[(std::size_t) track].group || layout.tracks[(std::size_t) track].channels.empty())
        return false;
    const auto channel = layout.tracks[(std::size_t) track].channels.front();
    PatternEvents* lane = nullptr;
    for (auto& l : pattern.events)
        if (l.target.kind == AutomationTargetKind::channelAdjust && l.target.param == leveladjust::volume && l.target.index == channel)
            lane = &l;
    if (lane == nullptr)
    {
        if (value < 0)
            return false;
        pattern.events.push_back ({ { AutomationTargetKind::channelAdjust, channel, 0, leveladjust::volume }, {} });
        lane = &pattern.events.back();
    }
    const auto from = layout.rowStart (row), to = from + layout.rowTicks;
    const auto before = lane->events;
    lane->events.erase (std::remove_if (lane->events.begin(), lane->events.end(), [&] (const PatternEvent& e) { return e.tick >= from && e.tick < to; }),
                        lane->events.end());
    if (value >= 0)
    {
        const auto at = std::lower_bound (lane->events.begin(), lane->events.end(), from, [] (const PatternEvent& e, std::int64_t t) { return e.tick < t; });
        lane->events.insert (at, { from, volumeEventValue (value) });
    }
    const auto changed = lane->events != before;
    if (lane->events.empty())
        pattern.events.erase (pattern.events.begin() + (lane - pattern.events.data()));
    return changed;
}
} // namespace

// --------------------------------------------------------------------------------------------------------- layout
int Layout::trackOfChannel (int channel) const noexcept
{
    for (int t = 0; t < (int) tracks.size(); ++t)
        for (auto c : tracks[(std::size_t) t].channels)
            if (c == channel)
                return t;
    return -1;
}

const Placed* Layout::placedAt (int track, int column, std::int64_t row) const noexcept
{
    if (track < 0 || track >= (int) columns.size() || column < 0 || column >= (int) columns[(std::size_t) track].size())
        return nullptr;
    const auto& list = columns[(std::size_t) track][(std::size_t) column];
    const auto it = std::lower_bound (list.begin(), list.end(), row, [this] (int pi, std::int64_t r) { return placed[(std::size_t) pi].startRow < r; });
    if (it != list.end() && placed[(std::size_t) *it].startRow == row)
        return &placed[(std::size_t) *it];
    return nullptr;
}

const Placed* Layout::soundingAt (int track, int column, std::int64_t row) const noexcept
{
    if (track < 0 || track >= (int) columns.size() || column < 0 || column >= (int) columns[(std::size_t) track].size())
        return nullptr;
    const auto& list = columns[(std::size_t) track][(std::size_t) column];
    // the last note starting at or before the row
    auto it = std::upper_bound (list.begin(), list.end(), row, [this] (std::int64_t r, int pi) { return r < placed[(std::size_t) pi].startRow; });
    if (it == list.begin())
        return nullptr;
    const auto& p = placed[(std::size_t) *(it - 1)];
    if (row < p.occupiedEnd || (row == p.offRow && p.offShown))
        return &p;
    return nullptr;
}

const Placed* Layout::placedOfNote (int note) const noexcept
{
    for (const auto& p : placed)
        if (p.note == note)
            return &p;
    return nullptr;
}

void resolveGroups (const Song& song, const TrackerSettings& settings, BuildOptions& options)
{
    std::set<int> taken;
    for (const auto& g : settings.groups)
    {
        std::vector<int> channels;
        for (auto uid : g.channels)
            for (int c = 0; c < (int) song.channels.size(); ++c)
                if (uid != 0 && song.channels[(std::size_t) c].uid == uid && taken.insert (c).second)
                    channels.push_back (c);
        if (channels.size() >= 2)
        {
            options.groups.push_back (channels);
            options.groupNames.push_back (g.name);
        }
        else
            for (auto c : channels)
                taken.erase (c);
    }
}

Layout build (const Song& song, int patternIndex, const BuildOptions& options)
{
    Layout layout;
    layout.pattern = patternIndex;
    layout.rowsPerBeat = std::clamp (options.rowsPerBeat, TrackerSettings::minRowsPerBeat, TrackerSettings::maxRowsPerBeat);
    // a row: ppq / rowsPerBeat ticks of the song's beat (a 6/8 beat is an eighth: its rows are shorter)
    layout.rowTicks = std::max<std::int64_t> (1, song.beatTicks() / layout.rowsPerBeat);
    layout.rowsPerBar = (int) std::max<std::int64_t> (1, song.barTicks() / layout.rowTicks);
    layout.delayDigits = std::max (2, hexDigits (layout.rowTicks - 1));
    if (patternIndex < 0 || patternIndex >= (int) song.patterns.size())
    {
        layout.rows = 0;
        return layout;
    }
    const auto& pattern = song.patterns[(std::size_t) patternIndex];
    const auto length = pattern.effectiveLength (song.meter());
    std::int64_t lastStartRow = -1;
    for (const auto& n : pattern.notes)
        lastStartRow = std::max (lastStartRow, layout.rowOf (n.start));
    layout.rows = std::max<std::int64_t> ((length + layout.rowTicks - 1) / layout.rowTicks, lastStartRow + 1);

    // the tracks: the groups, then one per channel with notes (or asked for), in rack order
    const auto channelCount = (int) song.channels.size();
    std::vector<int> trackOf ((std::size_t) std::max (0, channelCount), -1);
    std::vector<char> wanted ((std::size_t) std::max (0, channelCount), 0);
    for (const auto& n : pattern.notes)
        if (n.channel >= 0 && n.channel < channelCount)
            wanted[(std::size_t) n.channel] = 1;
    for (auto c : options.alsoShow)
        if (c >= 0 && c < channelCount)
            wanted[(std::size_t) c] = 1;
    if (options.onlyChannels)
    {
        std::fill (wanted.begin(), wanted.end(), 0);
        for (auto c : *options.onlyChannels)
            if (c >= 0 && c < channelCount)
                wanted[(std::size_t) c] = 1;
    }
    std::vector<int> groupOf ((std::size_t) std::max (0, channelCount), -1);
    for (int g = 0; g < (int) options.groups.size(); ++g)
        for (auto c : options.groups[(std::size_t) g])
            if (c >= 0 && c < channelCount && groupOf[(std::size_t) c] < 0)
                groupOf[(std::size_t) c] = g;
    std::vector<int> groupTrack (options.groups.size(), -1);
    for (int c = 0; c < channelCount; ++c)
    {
        const auto g = groupOf[(std::size_t) c];
        if (g >= 0)
        {
            // a group shows when any of its channels would
            bool any = false;
            for (auto m : options.groups[(std::size_t) g])
                any = any || (m >= 0 && m < channelCount && wanted[(std::size_t) m]);
            if (! any)
                continue;
            if (groupTrack[(std::size_t) g] < 0)
            {
                Track t;
                t.group = true;
                for (auto m : options.groups[(std::size_t) g])
                    if (m >= 0 && m < channelCount)
                        t.channels.push_back (m);
                t.name = (std::size_t) g < options.groupNames.size() && ! options.groupNames[(std::size_t) g].empty()
                             ? options.groupNames[(std::size_t) g]
                             : song.channels[(std::size_t) t.channels.front()].name;
                groupTrack[(std::size_t) g] = (int) layout.tracks.size();
                layout.tracks.push_back (std::move (t));
            }
            trackOf[(std::size_t) c] = groupTrack[(std::size_t) g];
            continue;
        }
        if (! wanted[(std::size_t) c])
            continue;
        Track t;
        t.channels = { c };
        t.name = song.channels[(std::size_t) c].name;
        trackOf[(std::size_t) c] = (int) layout.tracks.size();
        layout.tracks.push_back (std::move (t));
    }

    // the notes, per track in start order, into the first column whose last note is over (stable: a preferred column
    // first)
    const auto rt = layout.rowTicks;
    std::vector<std::vector<int>> perTrack (layout.tracks.size());
    for (int i = 0; i < (int) pattern.notes.size(); ++i)
    {
        const auto c = pattern.notes[(std::size_t) i].channel;
        if (c >= 0 && c < channelCount && trackOf[(std::size_t) c] >= 0)
            perTrack[(std::size_t) trackOf[(std::size_t) c]].push_back (i);
    }
    layout.columns.assign (layout.tracks.size(), {});
    for (int t = 0; t < (int) layout.tracks.size(); ++t)
    {
        auto& list = perTrack[(std::size_t) t];
        std::stable_sort (list.begin(), list.end(), [&pattern] (int a, int b) {
            const auto& x = pattern.notes[(std::size_t) a];
            const auto& y = pattern.notes[(std::size_t) b];
            return x.start != y.start ? x.start < y.start : x.key < y.key;
        });
        struct ColumnEnd
        {
            bool has = false;
            bool isShort = false;
            std::int64_t lastRow = -1; // the last row the column's last note shows on (its OFF, or its start if short)
            std::int64_t endTick = -1;
        };
        std::vector<ColumnEnd> ends;
        auto& cols = layout.columns[(std::size_t) t];
        for (auto i : list)
        {
            const auto& n = pattern.notes[(std::size_t) i];
            Placed p;
            p.note = i;
            p.track = t;
            p.startRow = layout.rowOf (n.start);
            p.startDelay = (int) (n.start - p.startRow * rt);
            const auto end = endOf (n);
            p.offRow = layout.rowOf (end);
            p.offDelay = (int) (end - p.offRow * rt);
            p.shortNote = p.offRow == p.startRow;
            p.occupiedEnd = p.shortNote ? p.startRow + 1 : p.offRow + (p.offDelay > 0 ? 1 : 0);
            // a column is free for the note after its last note's last row; on that row only for an exact legato (the
            // note starts where the last one ends: the OFF is this note's start)
            auto fits = [&] (const ColumnEnd& e) {
                return ! e.has || p.startRow > e.lastRow || (! e.isShort && p.startRow == e.lastRow && n.start == e.endTick);
            };
            int column = -1;
            if (options.preferredColumn)
                if (const auto want = options.preferredColumn (n); want >= 0 && want < 64)
                {
                    if ((int) ends.size() <= want)
                        ends.resize ((std::size_t) want + 1);
                    if (fits (ends[(std::size_t) want]))
                        column = want;
                }
            for (int k = 0; column < 0 && k < (int) ends.size(); ++k)
                if (fits (ends[(std::size_t) k]))
                    column = k;
            if (column < 0)
            {
                column = (int) ends.size();
                ends.emplace_back();
            }
            p.column = column;
            auto& e = ends[(std::size_t) column];
            e.has = true;
            e.isShort = p.shortNote;
            e.lastRow = p.shortNote ? p.startRow : p.offRow;
            e.endTick = end;
            if ((int) cols.size() <= column)
                cols.resize ((std::size_t) column + 1);
            cols[(std::size_t) column].push_back ((int) layout.placed.size());
            layout.placed.push_back (p);
        }
        // an OFF is shown unless the next note of the column starts exactly on it, or it is past the last row
        for (auto& col : cols)
            for (std::size_t k = 0; k < col.size(); ++k)
            {
                auto& p = layout.placed[(std::size_t) col[k]];
                if (p.shortNote || p.offRow >= layout.rows)
                    continue;
                const auto end = endOf (pattern.notes[(std::size_t) p.note]);
                const auto legato = k + 1 < col.size() && pattern.notes[(std::size_t) layout.placed[(std::size_t) col[k + 1]].note].start == end;
                p.offShown = ! legato;
            }
        auto& track = layout.tracks[(std::size_t) t];
        track.noteColumns = std::max (1, (int) cols.size());
        for (const auto& [channel, extra] : options.extraColumns)
            if (std::find (track.channels.begin(), track.channels.end(), channel) != track.channels.end())
                track.noteColumns = std::max (track.noteColumns, (int) cols.size() + std::max (0, extra));
        cols.resize ((std::size_t) track.noteColumns);
    }
    // fx columns: as many as a note of the track shows
    std::vector<int> noteFx (layout.tracks.size(), 0);
    for (const auto& p : layout.placed)
    {
        const auto& n = pattern.notes[(std::size_t) p.note];
        auto count = (int) shownFx (layout, p, n).size();
        auto& t = layout.tracks[(std::size_t) p.track];
        t.fxColumns = std::max (t.fxColumns, count);
        noteFx[(std::size_t) p.track] = std::max (noteFx[(std::size_t) p.track], count);
    }
    // round 190om: a one-channel track whose channel has volume multiplier events in the pattern gets one more fx slot,
    // the Vxx of each row with an event (the last one in the row: where a slide gets to)
    for (std::size_t ti = 0; ti < layout.tracks.size(); ++ti)
    {
        auto& t = layout.tracks[ti];
        if (t.group || t.channels.size() != 1)
            continue;
        const auto* lane = volumeLane (pattern, t.channels.front());
        if (lane == nullptr)
            continue;
        t.volumeRows.assign ((std::size_t) std::max<std::int64_t> (0, layout.rows), -1);
        bool any = false;
        for (const auto& e : lane->events)
            if (const auto r = layout.rowOf (e.tick); r >= 0 && r < layout.rows)
            {
                t.volumeRows[(std::size_t) r] = volumeValueOf (e.value);
                any = true;
            }
        if (! any)
        {
            t.volumeRows.clear();
            continue;
        }
        t.volumeSlot = noteFx[ti]; // (after the slots the notes' own fx take)
        t.fxColumns = std::max (t.fxColumns, t.volumeSlot + 1);
    }
    return layout;
}

// ---------------------------------------------------------------------------------------------------------- cells
std::vector<FieldSlot> fieldsOf (const Layout& layout, int track, const ShownColumns& shown)
{
    std::vector<FieldSlot> out { { Field::note } };
    if (track < 0 || track >= (int) layout.tracks.size())
        return out;
    const auto& t = layout.tracks[(std::size_t) track];
    if (t.group)
        out.push_back ({ Field::ins });
    out.push_back ({ Field::vol });
    if (shown.pan)
        out.push_back ({ Field::pan });
    if (shown.dly)
        out.push_back ({ Field::dly });
    if (shown.fx)
        for (int k = 0; k < t.fxColumns; ++k)
        {
            out.push_back ({ Field::fxLetter, k });
            out.push_back ({ Field::fxValue, k });
        }
    return out;
}

namespace
{
int fieldIndex (const std::vector<FieldSlot>& fields, const Cursor& c)
{
    for (int i = 0; i < (int) fields.size(); ++i)
        if (fields[(std::size_t) i].field == c.field && (c.field != Field::fxLetter && c.field != Field::fxValue ? true : fields[(std::size_t) i].fxSlot == c.fxSlot))
            return i;
    return 0;
}
} // namespace

Cursor clampCursor (const Layout& layout, const ShownColumns& shown, Cursor c)
{
    if (layout.tracks.empty())
        return {};
    c.track = std::clamp (c.track, 0, (int) layout.tracks.size() - 1);
    c.column = std::clamp (c.column, 0, layout.tracks[(std::size_t) c.track].noteColumns - 1);
    c.row = std::clamp<std::int64_t> (c.row, 0, std::max<std::int64_t> (0, layout.rows - 1));
    const auto fields = fieldsOf (layout, c.track, shown);
    const auto i = fieldIndex (fields, c);
    const auto matches = fields[(std::size_t) i].field == c.field;
    if (! matches)
    {
        c.field = Field::note;
        c.fxSlot = 0;
    }
    else
        c.fxSlot = fields[(std::size_t) i].fxSlot;
    return c;
}

Cursor moveHorizontal (const Layout& layout, const ShownColumns& shown, Cursor c, int direction)
{
    if (layout.tracks.empty())
        return c;
    c = clampCursor (layout, shown, c);
    auto fields = fieldsOf (layout, c.track, shown);
    auto i = fieldIndex (fields, c) + (direction > 0 ? 1 : -1);
    if (i < 0)
    {
        if (c.column > 0)
            --c.column;
        else if (c.track > 0)
        {
            --c.track;
            c.column = layout.tracks[(std::size_t) c.track].noteColumns - 1;
        }
        else
            return c;
        fields = fieldsOf (layout, c.track, shown);
        i = (int) fields.size() - 1;
    }
    else if (i >= (int) fields.size())
    {
        if (c.column + 1 < layout.tracks[(std::size_t) c.track].noteColumns)
            ++c.column;
        else if (c.track + 1 < (int) layout.tracks.size())
        {
            ++c.track;
            c.column = 0;
        }
        else
            return c;
        fields = fieldsOf (layout, c.track, shown);
        i = 0;
    }
    c.field = fields[(std::size_t) i].field;
    c.fxSlot = fields[(std::size_t) i].fxSlot;
    return c;
}

std::string keyText (int key)
{
    if (key < 0 || key > 127)
        return "???";
    auto name = notenames::pitchName (key % 12);
    if (name.size() == 1)
        name += '-';
    return name + std::to_string (key / 12);
}

std::string numberText (int value, int digits, bool hex)
{
    if (value < 0)
        return std::string ((std::size_t) digits, '.');
    char buffer[16];
    std::snprintf (buffer, sizeof buffer, hex ? "%0*X" : "%0*d", digits, value);
    return buffer;
}

Cell cellAt (const Layout& layout, int track, int column, std::int64_t row)
{
    if (const auto* p = layout.placedAt (track, column, row))
        return { Cell::Kind::start, p };
    if (const auto* p = layout.soundingAt (track, column, row))
    {
        if (row == p->offRow && p->offShown)
            return { Cell::Kind::off, p };
        if (row > p->startRow && row < p->occupiedEnd)
            return { Cell::Kind::sustain, p };
    }
    return {};
}

std::vector<Fx> shownFx (const Layout&, const Placed& p, const Note& n)
{
    std::vector<Fx> out;
    for (int i = 0; i < fxKinds; ++i)
        if ((Fx) i != Fx::cut && fxActive (n, (Fx) i))
            out.push_back ((Fx) i);
    if (p.shortNote)
        out.push_back (Fx::cut);
    return out;
}

int fieldValue (const Song& song, const Layout& layout, const Placed& p, Field field, int fxSlot)
{
    if (layout.pattern < 0 || layout.pattern >= (int) song.patterns.size())
        return -1;
    const auto& pattern = song.patterns[(std::size_t) layout.pattern];
    if (! validNote (pattern, p.note))
        return -1;
    const auto& n = pattern.notes[(std::size_t) p.note];
    switch (field)
    {
        case Field::note: return n.key;
        case Field::ins: return n.channel + 1;
        case Field::vol: return (int) std::lround (std::clamp (n.velocity, 0.0f, 1.0f) * 127.0f);
        case Field::pan: return (int) std::lround ((std::clamp (n.pan, -1.0f, 1.0f) + 1.0f) * 64.0f);
        case Field::dly: return p.startDelay > 0 ? p.startDelay : -1;
        case Field::fxLetter:
        case Field::fxValue:
        {
            const auto fx = shownFx (layout, p, n);
            if (fxSlot < 0 || fxSlot >= (int) fx.size())
                return -1;
            return field == Field::fxLetter ? (int) fx[(std::size_t) fxSlot] : fxValueOf (n, fx[(std::size_t) fxSlot], n.length);
        }
    }
    return -1;
}

int maxValue (const Layout& layout, Field field, std::optional<Fx> fx)
{
    switch (field)
    {
        case Field::vol: return 127;
        case Field::pan: return 128;
        case Field::dly: return (int) layout.rowTicks - 1;
        case Field::ins: return 4095;
        case Field::fxValue:
            if (fx == Fx::cut)
                return (int) layout.rowTicks - 1;
            if (fx == Fx::chance || fx == Fx::finePitch || fx == Fx::bend || fx == Fx::volume)
                return 255;
            if (fx == Fx::release)
                return 127;
            return fx ? 1 : 255;
        default: return 127;
    }
}

// ---------------------------------------------------------------------------------------------------------- edits
int typeNote (Pattern& pattern, const Layout& layout, const Cursor& at, int key, int channel, float velocity)
{
    key = std::clamp (key, 0, 127);
    if (const auto* p = layout.placedAt (at.track, at.column, at.row); p != nullptr && validNote (pattern, p->note))
    {
        auto& n = pattern.notes[(std::size_t) p->note];
        n.key = key;
        return p->note;
    }
    const auto start = layout.rowStart (at.row);
    // a note sounding in this column across the row ends here (one note at a time per column)
    if (const auto* s = layout.soundingAt (at.track, at.column, at.row); s != nullptr && validNote (pattern, s->note))
    {
        auto& n = pattern.notes[(std::size_t) s->note];
        if (n.start < start && endOf (n) > start)
            n.length = start - n.start;
    }
    const auto next = nextStartInColumn (layout, pattern, at.track, at.column, at.row);
    Note n;
    n.start = start;
    n.length = std::max<std::int64_t> (1, next - start);
    n.channel = channel;
    n.key = key;
    n.velocity = std::clamp (velocity, 0.0f, 1.0f);
    int index = -1;
    insertSorted (pattern.notes, n, &index);
    return index;
}

bool typeOff (Pattern& pattern, const Layout& layout, const Cursor& at)
{
    const auto start = layout.rowStart (at.row);
    bool changed = false;
    std::vector<int> erase;
    if (const auto* p = layout.placedAt (at.track, at.column, at.row); p != nullptr && validNote (pattern, p->note))
        erase.push_back (p->note);
    // the note sounding into the row from above ends at the row's start
    const auto& col = at.track >= 0 && at.track < (int) layout.columns.size() && at.column >= 0
                              && at.column < (int) layout.columns[(std::size_t) at.track].size()
                          ? layout.columns[(std::size_t) at.track][(std::size_t) at.column]
                          : std::vector<int> {};
    for (auto pi : col)
    {
        const auto& p = layout.placed[(std::size_t) pi];
        if (p.startRow >= at.row || ! validNote (pattern, p.note))
            continue;
        auto& n = pattern.notes[(std::size_t) p.note];
        if (endOf (n) > start)
        {
            n.length = start - n.start;
            changed = true;
        }
    }
    changed = changed || ! erase.empty();
    eraseIndices (pattern.notes, erase);
    return changed;
}

bool clearCell (Pattern& pattern, const Layout& layout, const Cursor& at)
{
    if (isVolumeSlot (layout, at)) // (round 190om: the row's Vxx, whatever the cell holds)
        return setVolumeRow (pattern, layout, at.track, at.row, -1);
    const auto cell = cellAt (layout, at.track, at.column, at.row);
    if (cell.placed == nullptr || ! validNote (pattern, cell.placed->note))
        return false;
    auto& n = pattern.notes[(std::size_t) cell.placed->note];
    const auto rowStart = layout.rowStart (at.row);
    if (cell.kind == Cell::Kind::off)
    {
        if (at.field == Field::dly)
        {
            if (cell.placed->offDelay == 0)
                return false;
            n.length = std::max<std::int64_t> (1, rowStart - n.start);
            return true;
        }
        if (at.field != Field::note)
            return false;
        // the OFF goes: the note lasts to the next note of its column (or the pattern's end)
        const auto next = nextStartInColumn (layout, pattern, at.track, at.column, at.row);
        n.length = std::max<std::int64_t> (1, next - n.start);
        return true;
    }
    if (cell.kind != Cell::Kind::start)
        return false;
    switch (at.field)
    {
        case Field::note:
        {
            eraseIndices (pattern.notes, { cell.placed->note });
            return true;
        }
        case Field::vol:
            if (n.velocity == defaultVelocity)
                return false;
            n.velocity = defaultVelocity;
            return true;
        case Field::pan:
            if (n.pan == 0.0f)
                return false;
            n.pan = 0.0f;
            return true;
        case Field::dly:
        {
            if (n.start == rowStart)
                return false;
            const auto end = endOf (n);
            const auto oldStart = n.start;
            n.start = rowStart;
            n.length = std::max<std::int64_t> (1, end - rowStart);
            keepLegato (pattern, layout, *cell.placed, oldStart, rowStart);
            pattern.sortNotes();
            return true;
        }
        case Field::fxLetter:
        case Field::fxValue:
        {
            const auto fx = shownFx (layout, *cell.placed, n);
            if (at.fxSlot < 0 || at.fxSlot >= (int) fx.size())
                return false;
            resetFx (n, fx[(std::size_t) at.fxSlot], layout.rowTicks);
            return true;
        }
        case Field::ins: return false;
    }
    return false;
}

bool setField (Pattern& pattern, const Layout& layout, const Cursor& at, int value, int channelCount)
{
    if (isVolumeSlot (layout, at)) // (round 190om: a Vxx on any row)
        return at.field == Field::fxValue && setVolumeRow (pattern, layout, at.track, at.row, std::clamp (value, 0, 255));
    const auto cell = cellAt (layout, at.track, at.column, at.row);
    if (cell.placed == nullptr || ! validNote (pattern, cell.placed->note))
        return false;
    auto& n = pattern.notes[(std::size_t) cell.placed->note];
    const auto rowStart = layout.rowStart (at.row);
    if (cell.kind == Cell::Kind::off)
    {
        if (at.field != Field::dly)
            return false;
        const auto end = rowStart + std::clamp<std::int64_t> (value, 0, layout.rowTicks - 1);
        if (end <= n.start)
            return false;
        n.length = end - n.start;
        return true;
    }
    if (cell.kind != Cell::Kind::start)
        return false;
    const Note before = n;
    switch (at.field)
    {
        case Field::note: n.key = std::clamp (value, 0, 127); break;
        case Field::ins:
            if (value < 1 || value > channelCount)
                return false;
            n.channel = value - 1;
            break;
        case Field::vol: n.velocity = std::clamp (value, 0, 127) / 127.0f; break;
        case Field::pan: n.pan = std::clamp ((std::clamp (value, 0, 128) - 64) / 64.0f, -1.0f, 1.0f); break;
        case Field::dly:
        {
            const auto end = endOf (n);
            const auto start = rowStart + std::clamp<std::int64_t> (value, 0, layout.rowTicks - 1);
            const auto oldStart = n.start;
            n.start = start;
            n.length = std::max<std::int64_t> (1, end - start);
            keepLegato (pattern, layout, *cell.placed, oldStart, start);
            pattern.sortNotes();
            return true;
        }
        case Field::fxLetter: return false;
        case Field::fxValue:
        {
            const auto fx = shownFx (layout, *cell.placed, n);
            if (at.fxSlot < 0 || at.fxSlot >= (int) fx.size())
                return false;
            setFxValue (n, fx[(std::size_t) at.fxSlot], value, layout.rowTicks);
            break;
        }
    }
    return ! (n == before);
}

bool setFxLetter (Pattern& pattern, const Layout& layout, const Cursor& at, Fx fx)
{
    // round 190om: V typed in the track's volume slot sets 100 % on a row without an event; V is not a note's fx
    if (fx == Fx::volume || isVolumeSlot (layout, at))
    {
        const auto& t = layout.tracks[(std::size_t) std::clamp (at.track, 0, std::max (0, (int) layout.tracks.size() - 1))];
        if (fx != Fx::volume || ! isVolumeSlot (layout, at) || at.row < 0 || at.row >= (std::int64_t) t.volumeRows.size() || t.volumeRows[(std::size_t) at.row] >= 0)
            return false;
        return setVolumeRow (pattern, layout, at.track, at.row, defaultOnValue (Fx::volume, layout.rowTicks));
    }
    const auto* p = layout.placedAt (at.track, at.column, at.row);
    if (p == nullptr || ! validNote (pattern, p->note))
        return false;
    auto& n = pattern.notes[(std::size_t) p->note];
    const auto shown = shownFx (layout, *p, n);
    if (std::find (shown.begin(), shown.end(), fx) != shown.end())
        return false;
    if (at.fxSlot >= 0 && at.fxSlot < (int) shown.size())
        resetFx (n, shown[(std::size_t) at.fxSlot], layout.rowTicks);
    setFxValue (n, fx, defaultOnValue (fx, layout.rowTicks), layout.rowTicks);
    return true;
}

namespace
{
bool inTrack (const Layout& layout, int track, int channel)
{
    if (track < 0 || track >= (int) layout.tracks.size())
        return false;
    const auto& ch = layout.tracks[(std::size_t) track].channels;
    return std::find (ch.begin(), ch.end(), channel) != ch.end();
}
} // namespace

namespace
{
// round 190om: a note's curve points from `from` ticks into it on move by `by` (a row inserted, or removed: the points
// inside the removed span go, the value there is kept by the point at its start)
void shiftCurves (Note& n, double from, double by)
{
    if (! n.curves.has())
        return;
    auto& c = n.curves.edit();
    for (auto& lane : c.lanes)
    {
        if (lane.empty())
            continue;
        std::vector<NoteCurvePoint> kept;
        for (const auto& q : lane)
            if ((double) q.tick < from)
                kept.push_back (q);
            else if (by < 0.0 && (double) q.tick < from - by)
                continue;
            else
                kept.push_back ({ (float) ((double) q.tick + by), q.value });
        lane = std::move (kept);
    }
    n.curves.tidy();
}

// the volume events of the track's channel from `from` on move by `by`; `dropFrom` ≥ 0: those in [dropFrom, from) go
bool shiftVolumeEvents (Pattern& pattern, const Layout& layout, int track, std::int64_t from, std::int64_t dropFrom, std::int64_t by)
{
    if (track < 0 || track >= (int) layout.tracks.size() || layout.tracks[(std::size_t) track].group)
        return false;
    bool changed = false;
    for (auto& lane : pattern.events)
    {
        if (lane.target.kind != AutomationTargetKind::channelAdjust || lane.target.param != leveladjust::volume
            || ! inTrack (layout, track, lane.target.index))
            continue;
        const auto before = lane.events.size();
        if (dropFrom >= 0)
            lane.events.erase (std::remove_if (lane.events.begin(), lane.events.end(), [&] (const PatternEvent& e) { return e.tick >= dropFrom && e.tick < from; }),
                               lane.events.end());
        changed = changed || lane.events.size() != before;
        for (auto& e : lane.events)
            if (e.tick >= from)
            {
                e.tick += by;
                changed = true;
            }
    }
    pattern.events.erase (std::remove_if (pattern.events.begin(), pattern.events.end(), [] (const PatternEvents& l) { return l.events.empty(); }), pattern.events.end());
    return changed;
}
} // namespace

bool insertRow (Pattern& pattern, const Layout& layout, int track, std::int64_t row)
{
    const auto at = layout.rowStart (row), rt = layout.rowTicks;
    bool changed = false;
    for (auto& n : pattern.notes)
    {
        if (! inTrack (layout, track, n.channel))
            continue;
        if (n.start >= at)
        {
            n.start += rt;
            changed = true;
        }
        else if (endOf (n) > at)
        {
            n.length += rt;
            shiftCurves (n, (double) (at - n.start), (double) rt); // (round 190om: its Bxx moves with its rows)
            changed = true;
        }
    }
    changed = shiftVolumeEvents (pattern, layout, track, at, -1, rt) || changed;
    if (changed)
        pattern.sortNotes();
    return changed;
}

bool deleteRow (Pattern& pattern, const Layout& layout, int track, std::int64_t row)
{
    const auto from = layout.rowStart (row), rt = layout.rowTicks, to = from + rt;
    bool changed = false;
    std::vector<int> erase;
    for (int i = 0; i < (int) pattern.notes.size(); ++i)
    {
        auto& n = pattern.notes[(std::size_t) i];
        if (! inTrack (layout, track, n.channel))
            continue;
        if (n.start >= from && n.start < to)
            erase.push_back (i);
        else if (n.start >= to)
        {
            n.start -= rt;
            changed = true;
        }
        else if (endOf (n) > from)
        {
            const auto cut = std::min (endOf (n), to) - from;
            n.length = std::max<std::int64_t> (1, n.length - cut);
            shiftCurves (n, (double) (from - n.start), -(double) cut);
            changed = true;
        }
    }
    changed = shiftVolumeEvents (pattern, layout, track, to, from, -rt) || changed;
    changed = changed || ! erase.empty();
    eraseIndices (pattern.notes, erase);
    if (changed)
        pattern.sortNotes();
    return changed;
}

// ------------------------------------------------------------------------------------------------------ selection
Selection Selection::of (const Cursor& a, const Cursor& b)
{
    Selection s;
    const auto aFirst = a.track < b.track || (a.track == b.track && a.column <= b.column);
    const auto& f = aFirst ? a : b;
    const auto& l = aFirst ? b : a;
    s.track0 = f.track;
    s.column0 = f.column;
    s.track1 = l.track;
    s.column1 = l.column;
    s.row0 = std::min (a.row, b.row);
    s.row1 = std::max (a.row, b.row);
    return s;
}

bool Selection::contains (const Layout&, int track, int column, std::int64_t row) const noexcept
{
    if (row < row0 || row > row1)
        return false;
    const auto afterStart = track > track0 || (track == track0 && column >= column0);
    const auto beforeEnd = track < track1 || (track == track1 && column <= column1);
    return afterStart && beforeEnd;
}

std::vector<int> selectedNotes (const Layout& layout, const Selection& s)
{
    std::vector<int> out;
    for (const auto& p : layout.placed)
        if (s.contains (layout, p.track, p.column, p.startRow))
            out.push_back (p.note);
    std::sort (out.begin(), out.end());
    return out;
}

Clipboard copy (const Pattern& pattern, const Layout& layout, const Selection& s)
{
    Clipboard clip;
    clip.rows = s.row1 - s.row0 + 1;
    clip.rowTicks = layout.rowTicks;
    clip.tracks = s.track1 - s.track0 + 1;
    const auto origin = layout.rowStart (s.row0);
    for (const auto& p : layout.placed)
        if (s.contains (layout, p.track, p.column, p.startRow) && validNote (pattern, p.note))
        {
            ClipNote c;
            c.note = pattern.notes[(std::size_t) p.note];
            c.note.start -= origin;
            c.track = p.track - s.track0;
            c.column = p.column;
            c.groupTrack = layout.tracks[(std::size_t) p.track].group;
            clip.notes.push_back (c);
        }
    return clip;
}

bool erase (Pattern& pattern, const Layout& layout, const Selection& s)
{
    const auto notes = selectedNotes (layout, s);
    eraseIndices (pattern.notes, notes);
    return ! notes.empty();
}

std::vector<int> paste (Pattern& pattern, const Layout& layout, const Cursor& at, const Clipboard& clip)
{
    std::vector<int> out;
    if (layout.tracks.empty() || clip.empty())
        return out;
    // the clip's rows map onto this layout's rows (a clip copied at another rows-per-beat keeps its rows)
    const auto scale = (double) layout.rowTicks / (double) std::max<std::int64_t> (1, clip.rowTicks);
    const auto origin = layout.rowStart (at.row);
    const auto endTick = origin + (std::int64_t) std::llround ((double) clip.rows * clip.rowTicks * scale);
    // the target tracks' notes starting in the pasted rows go
    std::vector<int> erase;
    for (int t = at.track; t < at.track + clip.tracks && t < (int) layout.tracks.size(); ++t)
        for (int i = 0; i < (int) pattern.notes.size(); ++i)
        {
            const auto& n = pattern.notes[(std::size_t) i];
            if (inTrack (layout, t, n.channel) && n.start >= origin && n.start < endTick)
                erase.push_back (i);
        }
    eraseIndices (pattern.notes, erase);
    std::vector<Note> added;
    for (const auto& c : clip.notes)
    {
        const auto t = at.track + c.track;
        if (t < 0 || t >= (int) layout.tracks.size())
            continue;
        const auto& track = layout.tracks[(std::size_t) t];
        auto n = c.note;
        n.start = origin + (std::int64_t) std::llround ((double) c.note.start * scale);
        n.length = std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) c.note.length * scale));
        if (track.channels.size() == 1)
            n.channel = track.channels.front();
        else if (std::find (track.channels.begin(), track.channels.end(), n.channel) == track.channels.end())
            n.channel = track.channels.front();
        added.push_back (n);
    }
    for (const auto& n : added)
        insertSorted (pattern.notes, n);
    // their indices after every insert
    for (int i = 0; i < (int) pattern.notes.size(); ++i)
        for (const auto& n : added)
            if (pattern.notes[(std::size_t) i] == n)
            {
                out.push_back (i);
                break;
            }
    return out;
}

bool transpose (Pattern& pattern, const Layout& layout, const Selection& s, int semitones)
{
    bool changed = false;
    for (auto i : selectedNotes (layout, s))
    {
        auto& n = pattern.notes[(std::size_t) i];
        const auto key = std::clamp (n.key + semitones, 0, 127);
        changed = changed || key != n.key;
        n.key = key;
    }
    return changed;
}

bool interpolateVolume (Pattern& pattern, const Layout& layout, const Selection& s)
{
    bool changed = false;
    std::map<std::pair<int, int>, std::vector<int>> perColumn;
    for (const auto& p : layout.placed)
        if (s.contains (layout, p.track, p.column, p.startRow) && validNote (pattern, p.note))
            perColumn[{ p.track, p.column }].push_back (p.note);
    for (auto& [key, list] : perColumn)
    {
        if (list.size() < 3)
            continue;
        std::sort (list.begin(), list.end(), [&pattern] (int a, int b) { return pattern.notes[(std::size_t) a].start < pattern.notes[(std::size_t) b].start; });
        const auto& first = pattern.notes[(std::size_t) list.front()];
        const auto& last = pattern.notes[(std::size_t) list.back()];
        const auto t0 = (double) first.start, t1 = (double) last.start;
        const auto v0 = first.velocity, v1 = last.velocity;
        if (t1 <= t0)
            continue;
        for (std::size_t k = 1; k + 1 < list.size(); ++k)
        {
            auto& n = pattern.notes[(std::size_t) list[k]];
            const auto v = (float) (v0 + (v1 - v0) * (((double) n.start - t0) / (t1 - t0)));
            if (std::abs (v - n.velocity) > 1.0e-6f)
            {
                n.velocity = v;
                changed = true;
            }
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------------------------------- text clipboard
namespace
{
const char* const textHeader = "LYNX tracker 1";

const char* const englishNames[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };

std::string englishKey (int key)
{
    if (key < 0 || key > 127)
        return "???";
    return std::string (englishNames[key % 12]) + std::to_string (key / 12);
}

int parseKey (const std::string& t)
{
    if (t.size() < 3)
        return -1;
    for (int k = 0; k < 12; ++k)
        if (t.compare (0, 2, englishNames[k]) == 0)
        {
            const auto octave = std::atoi (t.c_str() + 2);
            const auto key = octave * 12 + k;
            return key >= 0 && key <= 127 ? key : -1;
        }
    return -1;
}

int parseHex (const std::string& t)
{
    if (t.empty() || t[0] == '.' || t[0] == '-')
        return -1;
    char* end = nullptr;
    const auto v = std::strtol (t.c_str(), &end, 16);
    return end != nullptr && *end == 0 ? (int) v : -1;
}
} // namespace

std::string clipboardText (const Song& song, const Layout& layout, const Selection& s, bool)
{
    std::ostringstream out;
    out << textHeader << " rows=" << (s.row1 - s.row0 + 1) << " rowTicks=" << layout.rowTicks << "\n";
    if (layout.pattern < 0 || layout.pattern >= (int) song.patterns.size())
        return out.str();
    const auto& pattern = song.patterns[(std::size_t) layout.pattern];
    for (auto row = s.row0; row <= s.row1; ++row)
    {
        bool firstTrack = true;
        for (int t = s.track0; t <= s.track1 && t < (int) layout.tracks.size(); ++t)
        {
            if (! firstTrack)
                out << "|";
            firstTrack = false;
            const auto c0 = t == s.track0 ? s.column0 : 0;
            const auto c1 = t == s.track1 ? s.column1 : layout.tracks[(std::size_t) t].noteColumns - 1;
            for (int c = c0; c <= c1; ++c)
            {
                if (c > c0)
                    out << " ";
                const auto cell = cellAt (layout, t, c, row);
                if (cell.kind == Cell::Kind::start)
                {
                    const auto& n = pattern.notes[(std::size_t) cell.placed->note];
                    out << englishKey (n.key) << " " << numberText (n.channel + 1, 3, true) << " "
                        << numberText ((int) std::lround (n.velocity * 127.0f), 2, true) << " "
                        << numberText ((int) std::lround ((n.pan + 1.0f) * 64.0f), 2, true) << " "
                        << numberText (cell.placed->startDelay, layout.delayDigits, true) << " ";
                    const auto fx = shownFx (layout, *cell.placed, n);
                    if (fx.empty())
                        out << "...";
                    for (std::size_t k = 0; k < fx.size(); ++k)
                        out << (k > 0 ? "," : "") << fxLetter (fx[k]) << numberText (fxValueOf (n, fx[k], n.length), layout.delayDigits, true);
                }
                else if (cell.kind == Cell::Kind::off)
                    out << "OFF ... .. .. " << numberText (cell.placed->offDelay, layout.delayDigits, true) << " ...";
                else
                    out << "--- ... .. .. " << std::string ((std::size_t) layout.delayDigits, '.') << " ...";
            }
        }
        out << "\n";
    }
    return out.str();
}

std::optional<Clipboard> parseClipboardText (const std::string& text, std::int64_t rowTicks)
{
    std::istringstream in (text);
    std::string line;
    if (! std::getline (in, line) || line.rfind (textHeader, 0) != 0)
        return std::nullopt;
    Clipboard clip;
    clip.rowTicks = rowTicks;
    if (const auto at = line.find ("rowTicks="); at != std::string::npos)
        clip.rowTicks = std::max<std::int64_t> (1, std::atoll (line.c_str() + at + 9));
    // per (track, column): the open note (its index in clip.notes)
    std::map<std::pair<int, int>, int> open;
    std::int64_t row = 0;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        int track = 0;
        std::size_t from = 0;
        while (from <= line.size())
        {
            const auto bar = line.find ('|', from);
            const auto part = line.substr (from, bar == std::string::npos ? std::string::npos : bar - from);
            std::istringstream cells (part);
            std::vector<std::string> tokens;
            for (std::string tok; cells >> tok;)
                tokens.push_back (tok);
            for (int column = 0; (std::size_t) (column + 1) * 6 <= tokens.size(); ++column)
            {
                const auto* t = &tokens[(std::size_t) column * 6];
                const auto key = std::make_pair (track, column);
                const auto delay = std::max (0, parseHex (t[4]));
                const auto tick = row * clip.rowTicks + delay;
                if (t[0] == "OFF" || parseKey (t[0]) >= 0)
                    if (const auto o = open.find (key); o != open.end())
                    {
                        auto& n = clip.notes[(std::size_t) o->second].note;
                        n.length = std::max<std::int64_t> (1, tick - n.start);
                        open.erase (o);
                    }
                if (const auto k = parseKey (t[0]); k >= 0)
                {
                    ClipNote c;
                    c.track = track;
                    c.column = column;
                    c.note.start = tick;
                    c.note.key = k;
                    if (const auto ins = parseHex (t[1]); ins > 0)
                        c.note.channel = ins - 1;
                    if (const auto v = parseHex (t[2]); v >= 0)
                        c.note.velocity = std::clamp (v, 0, 127) / 127.0f;
                    if (const auto p = parseHex (t[3]); p >= 0)
                        c.note.pan = std::clamp ((std::clamp (p, 0, 128) - 64) / 64.0f, -1.0f, 1.0f);
                    bool shortNote = false;
                    std::istringstream fxs (t[5]);
                    for (std::string f; std::getline (fxs, f, ',');)
                        if (f.size() >= 2)
                            if (const auto fx = fxFromLetter (f[0]); fx)
                            {
                                const auto v = parseHex (f.substr (1));
                                if (v < 0)
                                    continue;
                                if (*fx == Fx::cut)
                                {
                                    c.note.length = std::max (1, v);
                                    shortNote = true;
                                }
                                else
                                    setFxValue (c.note, *fx, v, clip.rowTicks);
                            }
                    clip.notes.push_back (c);
                    if (! shortNote)
                        open[key] = (int) clip.notes.size() - 1;
                }
                clip.tracks = std::max (clip.tracks, track + 1);
            }
            if (bar == std::string::npos)
                break;
            from = bar + 1;
            ++track;
        }
        ++row;
    }
    clip.rows = row;
    for (const auto& [key, i] : open)
    {
        auto& n = clip.notes[(std::size_t) i].note;
        n.length = std::max<std::int64_t> (1, row * clip.rowTicks - n.start);
    }
    return clip;
}

// ----------------------------------------------------------------------------------------------------------- keys
int ValueEntry::add (int digit, bool hex, int maxDigits, bool& done) noexcept
{
    const auto base = hex ? 16 : 10;
    if (digits >= maxDigits)
        reset();
    value = value * base + digit;
    ++digits;
    done = digits >= maxDigits;
    return value;
}

int digitOf (int keyChar, bool hex) noexcept
{
    if (keyChar >= '0' && keyChar <= '9')
        return keyChar - '0';
    if (hex)
    {
        if (keyChar >= 'A' && keyChar <= 'F')
            return keyChar - 'A' + 10;
        if (keyChar >= 'a' && keyChar <= 'f')
            return keyChar - 'a' + 10;
    }
    return -1;
}

std::int64_t rowAt (const Layout& layout, double tick) noexcept
{
    if (tick < 0.0 || layout.rows <= 0)
        return -1;
    const auto row = (std::int64_t) std::floor (tick / (double) layout.rowTicks);
    return row < layout.rows ? row : -1;
}
} // namespace lynx::tracker
