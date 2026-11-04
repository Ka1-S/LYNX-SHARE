#include "core/Recorder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lynx
{
std::int64_t patternPositionForTick (const Song& song, double tick, std::int64_t loopLength)
{
    const auto position = (std::int64_t) std::floor (std::max (0.0, tick));

    if (song.mode == PlayMode::song)
    {
        const auto songLength = std::max<std::int64_t> (1, song.songLength());
        const auto inSong = position % songLength; // the song plays in a loop

        for (const auto& c : song.playlist)
            if (c.kind == ClipKind::pattern && c.pattern == song.currentPattern && inSong >= c.start && inSong < c.end())
                return clipPatternTick (song, c, inSong);
    }

    return position % std::max<std::int64_t> (1, loopLength);
}

void commitTake (Pattern& pattern, std::vector<Note> notes, RecordMode mode, std::int64_t grid, std::vector<int>& clearedChannels)
{
    if (mode == RecordMode::replace)
        for (const auto& n : notes)
            if (std::find (clearedChannels.begin(), clearedChannels.end(), n.channel) == clearedChannels.end())
            {
                clearedChannels.push_back (n.channel);
                auto& existing = pattern.notes;
                existing.erase (std::remove_if (existing.begin(), existing.end(), [&n] (const Note& o) { return o.channel == n.channel; }),
                                existing.end());
            }

    if (grid > 1)
        for (auto& n : notes)
            n.start = std::max<std::int64_t> (0, (n.start + grid / 2) / grid * grid);

    pattern.notes.insert (pattern.notes.end(), notes.begin(), notes.end());
    pattern.sortNotes();
}

void quantizeTake (std::vector<Note>& notes, const TakeQuantize& q)
{
    const auto nearest = [] (std::int64_t t, std::int64_t g) { return std::max<std::int64_t> (0, (t + g / 2) / g * g); };

    for (auto& n : notes)
    {
        const auto end = n.start + std::max<std::int64_t> (1, n.length);

        if (q.stepSequencer)
        {
            n.start = nearest (n.start, ticksPerStep);
            n.length = ticksPerStep;
            continue;
        }

        if (q.grid <= 1)
            continue;

        if (q.starts)
            n.start = nearest (n.start, q.grid);

        if (q.end == NoteEnd::quantize)
            n.length = std::max (q.grid, nearest (end, q.grid) - n.start);
        else if (q.end == NoteEnd::keepEnd)
            n.length = std::max<std::int64_t> (1, end - n.start);
        // keepLength: the length as played
    }
}

void commitTake (Pattern& pattern, std::vector<Note> notes, RecordMode mode, const TakeQuantize& q, std::vector<int>& clearedChannels)
{
    quantizeTake (notes, q);
    commitTake (pattern, std::move (notes), mode, 0, clearedChannels);
}

std::vector<PunchRange> punchRanges (const std::vector<Marker>& markers)
{
    std::vector<const Marker*> sorted;
    for (const auto& m : markers)
        if (m.action == Marker::punchIn || m.action == Marker::punchOut)
            sorted.push_back (&m);
    std::stable_sort (sorted.begin(), sorted.end(), [] (const Marker* a, const Marker* b) { return a->tick < b->tick; });

    std::vector<PunchRange> out;
    std::int64_t openAt = -1;
    bool any = false;
    for (const auto* m : sorted)
    {
        if (m->action == Marker::punchIn)
        {
            if (openAt < 0)
                openAt = m->tick;
        }
        else if (openAt >= 0 || ! any) // a stop with no start before it: from the song start
        {
            const auto from = openAt >= 0 ? openAt : 0;
            if (m->tick > from)
                out.push_back ({ from, m->tick });
            openAt = -1;
        }
        any = true;
    }
    if (openAt >= 0)
        out.push_back ({ openAt, std::numeric_limits<std::int64_t>::max() / 4 });
    if (out.empty() && any) // only markers that open and close nothing: an empty range (nothing recorded)
        out.push_back ({ 0, 0 });
    return out;
}

bool inPunchRanges (const std::vector<PunchRange>& ranges, double tick) noexcept
{
    if (ranges.empty())
        return true;
    return std::any_of (ranges.begin(), ranges.end(), [tick] (const PunchRange& r) { return tick >= (double) r.in && tick < (double) r.out; });
}

std::vector<ClipPart> punchParts (const std::vector<PunchRange>& ranges, std::int64_t start, std::int64_t length, std::int64_t offset)
{
    if (ranges.empty())
        return { { start, length, offset } };

    std::vector<ClipPart> out;
    const auto end = start + length;
    for (const auto& r : ranges)
    {
        const auto from = std::max (start, r.in), to = std::min (end, r.out);
        if (to > from)
            out.push_back ({ from, to - from, offset + (from - start) });
    }
    return out;
}

bool placePatternClipAt (Song& song, int pattern, std::int64_t tick)
{
    if (pattern < 0 || pattern >= (int) song.patterns.size())
        return false;

    tick = std::max<std::int64_t> (0, tick);

    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.pattern == pattern && tick >= c.start && tick < c.end())
            return false;

    const auto bar = song.barAt (tick);
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = pattern;
    clip.start = bar.tick;
    clip.length = song.patterns[(std::size_t) pattern].effectiveLength (song.beatsPerBar);

    // the first track with nothing from the clip's start on, a clip ending right there included: the take's clip grows
    // to the right while recording (FL, round 168k [EXEC] fl2: Pattern 1 on Track 1 up to bar 2, the take's clip from
    // bar 2 went to Track 2; on an empty Playlist to Track 1)
    for (int track = 0;; ++track)
    {
        const auto busy = std::any_of (song.playlist.begin(), song.playlist.end(), [&] (const Clip& c) {
            return c.track == track && c.end() >= clip.start;
        });

        if (! busy)
        {
            clip.track = track;
            break;
        }
    }

    song.playlist.push_back (clip);
    return true;
}

namespace
{
// The clip of the current pattern playing at song position `tick` (the last one in the list, as the Playlist draws it).
const Clip* currentPatternClipAt (const Song& song, std::int64_t tick)
{
    const Clip* found = nullptr;
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.pattern == song.currentPattern && tick >= c.start && tick < c.end())
            found = &c;
    return found;
}

bool inCurrentPatternClip (const Song& song, std::int64_t tick)
{
    return currentPatternClipAt (song, tick) != nullptr;
}

} // namespace

// Where a take played at song `tick` inside clip `c` lands in its pattern. With LYNX's "Stretched pattern clips loop
// their pattern" on (stretchedClipsLoop(); 0.1.23, the user's decision) a clip longer than its pattern loops it, so a
// note played in a repeat folds into the pattern (its place modulo the pattern's length, from the clip's offset): it
// then plays in every repeat and the pattern does not grow. Off, FL's rule (round 168k [EXEC]: a 1-bar pattern's clip
// stretched to 4 bars, notes played at bars 2.1 / 3.3 / 4.2 stayed there and the pattern became 4 bars): no fold.
std::int64_t clipPatternTick (const Song& song, const Clip& c, std::int64_t tick)
{
    if (c.pattern < 0 || c.pattern >= (int) song.patterns.size())
        return tick - c.start + c.offset;
    return (std::int64_t) std::floor (patternTickInClip (c, (double) tick, clipLoopLength (song, c)));
}

std::int64_t clipLoopLength (const Song& song, const Clip& c)
{
    if (! stretchedClipsLoop() || c.pattern < 0 || c.pattern >= (int) song.patterns.size())
        return 0;
    const auto length = song.patterns[(std::size_t) c.pattern].effectiveLength (song.beatsPerBar);
    return length > 0 && c.offset + c.length > length ? length : 0;
}

std::int64_t takeOriginFor (const Song& song, std::int64_t tick, bool* fromClip)
{
    tick = std::max<std::int64_t> (0, tick);
    if (const auto* c = currentPatternClipAt (song, tick))
    {
        if (fromClip != nullptr)
            *fromClip = true;
        return c->start - c->offset;
    }
    if (fromClip != nullptr)
        *fromClip = false;
    auto origin = song.barAt (tick).tick;
    // a loop region plays over and over: its notes keep one origin, the bar the region starts in
    if (song.hasLoop() && tick >= song.loopStart && tick < song.loopEnd)
        origin = std::min (origin, song.barAt (song.loopStart).tick);
    return origin;
}

std::int64_t songNotePosition (const Song& song, std::int64_t tick, std::int64_t origin)
{
    tick = std::max<std::int64_t> (0, tick);
    if (const auto* c = currentPatternClipAt (song, tick))
        return std::max<std::int64_t> (0, clipPatternTick (song, *c, tick));
    auto local = tick - std::max<std::int64_t> (0, origin);
    if (local < 0) // played after the song (or the loop region) went back to its start: the take goes on after it
        local += song.hasLoop() ? song.loopEnd - song.loopStart : std::max<std::int64_t> (1, song.songLength());
    return std::max<std::int64_t> (0, local);
}

bool growForSongTake (Song& song, int pattern, std::int64_t origin, std::int64_t reach, const std::vector<Note>& notes)
{
    if (pattern < 0 || pattern >= (int) song.patterns.size() || notes.empty() || origin < 0)
        return false;

    auto& p = song.patterns[(std::size_t) pattern];
    bool changed = false;
    std::int64_t lastStart = -1;
    for (const auto& n : notes)
        lastStart = std::max (lastStart, n.start);

    // a set length grows by whole bars to the notes recorded past it (FL's pattern length follows its notes)
    const std::int64_t bar = (std::int64_t) ppq * std::max (1, song.beatsPerBar);
    if (p.lengthTicks > 0 && lastStart >= p.lengthTicks)
    {
        p.lengthTicks = (lastStart / bar + 1) * bar;
        changed = true;
    }

    // the take's clip (the one whose pattern start is the origin) reaches the bar of the last note when no clip of the
    // pattern plays there: from its pattern position on, the whole pattern (it stays an automatic-length clip)
    if (reach < origin)
        return changed;
    const auto lastSong = reach;
    const auto covered = std::any_of (song.playlist.begin(), song.playlist.end(), [&] (const Clip& c) {
        return c.kind == ClipKind::pattern && c.pattern == pattern && lastSong >= c.start && lastSong < c.end();
    });
    if (covered)
        return changed;
    Clip* take = nullptr;
    for (auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.pattern == pattern && c.start - c.offset == origin && c.start <= lastSong
            && (take == nullptr || c.end() > take->end()))
            take = &c;
    if (take == nullptr)
        return changed;
    const auto lastBar = song.barAt (lastSong);
    const auto end = std::max (lastBar.tick + std::max<std::int64_t> (1, lastBar.length), origin + p.effectiveLength (song.beatsPerBar));
    if (end > take->end())
    {
        take->length = end - take->start;
        changed = true;
    }
    return changed;
}

void commitNoteBatch (Song& song, const NoteRecorder& recorder, std::vector<Note> notes, RecordMode mode, const TakeQuantize& q,
                      std::vector<int>& clearedChannels, bool placeClip, bool firstBatch)
{
    const auto pattern = song.currentPattern;
    if (pattern < 0 || pattern >= (int) song.patterns.size() || notes.empty())
        return;
    quantizeTake (notes, q);
    // a take folded into a looping clip (stretchedClipsLoop()): a note quantized onto the pattern's end goes to its start,
    // the next repeat's first beat, and a note held across the end is cut there (the pattern keeps its length)
    if (song.mode == PlayMode::song && recorder.originFromClip())
        for (const auto& c : song.playlist)
            if (c.kind == ClipKind::pattern && c.pattern == pattern && c.start - c.offset == recorder.songOrigin())
                if (const auto length = clipLoopLength (song, c); length > 0)
                {
                    for (auto& n : notes)
                    {
                        n.start %= length;
                        n.length = std::clamp<std::int64_t> (n.length, 1, length - n.start);
                    }
                    break;
                }
    commitTake (song.patterns[(std::size_t) pattern], notes, mode, 0, clearedChannels);

    if (song.mode != PlayMode::song || recorder.songOrigin() < 0)
        return;
    if (placeClip && firstBatch && ! recorder.originFromClip())
        placePatternClipAt (song, pattern, recorder.songOrigin());
    growForSongTake (song, pattern, recorder.songOrigin(), recorder.originReach(), notes);
}

// --- live note timing and the count-in catch (round 170d) -------------------------------------------------------------

double liveTickAt (const LiveClock& clock, double eventMs, double compensationMs) noexcept
{
    if (! clock.running || clock.ticksPerMs <= 0.0)
        return clock.tick;
    // (a time stamp from another clock or a stale queue: at most a quarter second either way)
    const auto elapsed = std::clamp (eventMs - clock.timeMs, -250.0, 250.0);
    return clock.tick + (elapsed - std::max (0.0, compensationMs)) * clock.ticksPerMs;
}

std::int64_t countInWrap (std::int64_t countIn, std::int64_t loopLength) noexcept
{
    const auto loop = std::max<std::int64_t> (1, loopLength);
    return std::max<std::int64_t> (1, (std::max<std::int64_t> (0, countIn) + loop - 1) / loop) * loop;
}

bool catchEarlyNote (RecordedEvent& e, CountInCatch& c)
{
    if (c.start < 0)
        return true;
    auto found = std::find_if (c.moved.begin(), c.moved.end(), [&e] (const CountInCatch::Moved& m) { return m.channel == e.channel && m.key == e.key; });
    const auto start = (double) c.start;

    if (e.on)
    {
        if (found != c.moved.end()) // a repeated note-on: the previous one's move is over
            c.moved.erase (found);
        if (e.tick >= start)
            return true;
        if (e.tick >= start - (double) std::max<std::int64_t> (0, c.window))
        {
            c.moved.push_back ({ e.channel, e.key, start - e.tick, false });
            e.tick = start;
            return true;
        }
        if (c.songMode)
            return e.tick >= 0.0; // its real song position (nothing before the song start)
        const auto wrap = (double) std::max<std::int64_t> (1, c.wrap);
        c.moved.push_back ({ e.channel, e.key, wrap, true });
        e.tick += wrap;
        return true;
    }

    // a release: follows its note-on's move
    if (found == c.moved.end())
        return true;
    if (e.tick < start)
        e.tick += found->shift; // released before the start: the played length
    else if (found->wrapped)
        e.tick = start + (double) std::max<std::int64_t> (1, c.wrap); // held over the start: to the loop's end
    // (a caught note released after the start keeps its release)
    c.moved.erase (found);
    return true;
}

std::int64_t countInLength (const Song& song, int bars, std::int64_t tick)
{
    return std::max (0, bars) * std::max<std::int64_t> (1, song.barAt (std::max<std::int64_t> (0, tick)).length);
}

// --- automation recording (REA round 143) ---------------------------------------------------------------

namespace
{
std::int64_t nearestOnGrid (std::int64_t t, std::int64_t grid) noexcept
{
    return grid > 1 ? std::max<std::int64_t> (0, (t + grid / 2) / grid * grid) : t;
}

struct LocalPoint
{
    std::int64_t tick = 0;
    float value = 0.0f;
};

// Splits points at the loop's wrap (a tick that goes back) into runs that move forward.
std::vector<std::vector<LocalPoint>> forwardRuns (const std::vector<LocalPoint>& points)
{
    std::vector<std::vector<LocalPoint>> runs;
    for (const auto& p : points)
    {
        if (runs.empty() || p.tick < runs.back().back().tick)
            runs.emplace_back();
        auto& run = runs.back();
        if (! run.empty() && run.back().tick == p.tick)
            run.back().value = p.value; // one value per tick: the last one
        else
            run.push_back (p);
    }
    return runs;
}

bool sameValue (float a, float b) noexcept { return std::abs (a - b) < 1.0e-6f; }
} // namespace

std::int64_t songTickForRecorded (const Song& song, double tick, bool linear)
{
    auto t = (std::int64_t) std::floor (std::max (0.0, tick));
    if (linear && ! song.hasLoop())
        return t;
    if (song.hasLoop())
    {
        if (t >= song.loopEnd)
            t = song.loopStart + (t - song.loopStart) % std::max<std::int64_t> (1, song.loopEnd - song.loopStart);
        return t;
    }
    if (const auto length = song.songLength(); length > 0)
        t %= length;
    return t;
}

void thinAutomation (std::vector<AutomationPoint>& points, std::int64_t from, std::int64_t to)
{
    for (std::size_t i = 1; i + 1 < points.size();)
    {
        const auto& p = points[i];
        if (p.tick >= from && p.tick <= to && sameValue (points[i - 1].value, p.value) && sameValue (points[i + 1].value, p.value)
            && points[i - 1].tension == 0.0f && p.tension == 0.0f)
            points.erase (points.begin() + (std::ptrdiff_t) i);
        else
            ++i;
    }
}

void growTakeClip (Song& song, const AutomationWrite& w, std::int64_t tick, bool finished)
{
    if (! w.createdByTake || w.automation < 0)
        return;
    for (auto& c : song.playlist)
        if (c.kind == ClipKind::automation && c.automation == w.automation)
        {
            if (tick <= c.start)
                return;
            auto end = std::max (c.end(), tick);
            if (finished)
            {
                const auto bar = song.barAt (std::max<std::int64_t> (c.start, end - 1));
                end = std::max (end, bar.tick + std::max<std::int64_t> (1, bar.length));
            }
            c.length = end - c.start;
            return;
        }
}

void commitAutomationPoints (Song& song, AutomationWrite& w, std::vector<RecordedValue> points, std::int64_t grid, bool linear)
{
    std::vector<LocalPoint> ticks;
    for (const auto& p : points)
        ticks.push_back ({ nearestOnGrid (songTickForRecorded (song, p.tick, linear), grid), std::clamp (p.value, 0.0f, 1.0f) });

    for (const auto& run : forwardRuns (ticks))
    {
        const auto t0 = run.front().tick, t1 = run.back().tick;
        auto covers = [&] (const Clip& c, bool own) {
            // (the take's own clip covers everything after its start: it grows with the take, even over a pause of
            // the gesture — issue #12, a new clip began after every pause)
            return c.kind == ClipKind::automation && c.start <= t0 && (own || t0 < c.end());
        };

        // the clip to write into: the one this take writes, else a clip of the parameter playing there, else a new one
        int clipIndex = -1;
        for (int i = 0; i < (int) song.playlist.size(); ++i)
            if (song.playlist[(std::size_t) i].automation == w.automation && w.automation >= 0 && covers (song.playlist[(std::size_t) i], w.createdByTake))
                clipIndex = i;
        if (clipIndex < 0)
            for (int i = 0; i < (int) song.playlist.size(); ++i)
            {
                const auto& c = song.playlist[(std::size_t) i];
                if (covers (c, false) && c.automation >= 0 && c.automation < (int) song.automations.size()
                    && song.automations[(std::size_t) c.automation].target == w.target && ! song.automations[(std::size_t) c.automation].isLive())
                    clipIndex = i;
            }
        if (clipIndex >= 0 && song.playlist[(std::size_t) clipIndex].automation != w.automation)
        {
            w.automation = song.playlist[(std::size_t) clipIndex].automation;
            w.createdByTake = false;
            w.lastLocal = -1;
        }
        if (clipIndex < 0)
        {
            Automation a;
            a.name = w.name;
            a.target = w.target;
            song.automations.push_back (std::move (a));
            const auto bar = song.barAt (t0);
            Clip clip;
            clip.kind = ClipKind::automation;
            clip.automation = (int) song.automations.size() - 1;
            clip.start = bar.tick;
            clip.length = std::max<std::int64_t> (1, bar.length);
            // the first track with nothing from the clip's start on (the clip grows while recording)
            for (int track = 0;; ++track)
                if (std::none_of (song.playlist.begin(), song.playlist.end(), [&] (const Clip& c) { return c.track == track && c.end() > clip.start; }))
                {
                    clip.track = track;
                    break;
                }
            song.playlist.push_back (clip);
            clipIndex = (int) song.playlist.size() - 1;
            w.automation = clip.automation;
            w.createdByTake = true;
            w.lastLocal = -1;
        }

        auto& clip = song.playlist[(std::size_t) clipIndex];
        if (w.createdByTake && t1 >= clip.end()) // the take's own clip grows by bars
        {
            const auto bar = song.barAt (t1);
            clip.length = std::max (t1 + 1, bar.tick + std::max<std::int64_t> (1, bar.length)) - clip.start;
        }

        auto& a = song.automations[(std::size_t) clip.automation];
        std::vector<AutomationPoint> fresh;
        for (const auto& p : run)
            if (p.tick >= clip.start && p.tick < clip.end()) // an existing clip is not stretched
                fresh.push_back ({ p.tick - clip.start + clip.offset, p.value, 0.0f });
        if (fresh.empty())
            continue;

        const auto l0 = fresh.front().tick, l1 = fresh.back().tick;
        const auto continuing = w.lastLocal >= 0 && w.lastLocal < l0;
        const auto from = continuing ? w.lastLocal + 1 : l0;
        const auto hasBefore = std::any_of (a.points.begin(), a.points.end(), [&] (const AutomationPoint& p) { return p.tick < from; });
        const auto hasAfter = std::any_of (a.points.begin(), a.points.end(), [&] (const AutomationPoint& p) { return p.tick > l1 + 1; });
        const auto oldAtFrom = a.valueAt ((double) from - 1.0);
        const auto oldAfter = a.valueAt ((double) l1 + 1.0);

        // what lay under the gesture is replaced; the curve before and after it stays
        a.points.erase (std::remove_if (a.points.begin(), a.points.end(), [&] (const AutomationPoint& p) { return p.tick >= from && p.tick <= l1 + 1; }),
                        a.points.end());
        if (! continuing && hasBefore && from > 0)
            a.points.push_back ({ from - 1, oldAtFrom, 0.0f });
        a.points.insert (a.points.end(), fresh.begin(), fresh.end());
        if (hasAfter)
            a.points.push_back ({ l1 + 1, oldAfter, 0.0f });
        std::stable_sort (a.points.begin(), a.points.end(), [] (const AutomationPoint& x, const AutomationPoint& y) { return x.tick < y.tick; });
        thinAutomation (a.points, from, l1);
        w.lastLocal = l1;
    }
}

void commitEventPoints (Song& song, int patternIndex, AutomationWrite& w, std::vector<RecordedValue> points, std::int64_t loopLength,
                        std::int64_t grid)
{
    if (patternIndex < 0 || patternIndex >= (int) song.patterns.size() || points.empty())
        return;

    loopLength = std::max<std::int64_t> (1, loopLength);
    std::vector<LocalPoint> ticks;
    for (const auto& p : points)
        ticks.push_back ({ nearestOnGrid (patternPositionForTick (song, p.tick, loopLength), grid) % loopLength, std::clamp (p.value, 0.0f, 1.0f) });

    auto& pattern = song.patterns[(std::size_t) patternIndex];
    auto lane = std::find_if (pattern.events.begin(), pattern.events.end(), [&] (const PatternEvents& e) { return e.target == w.target; });
    if (lane == pattern.events.end())
    {
        pattern.events.push_back ({ w.target, {} });
        lane = pattern.events.end() - 1;
        w.lastLocal = -1;
    }
    auto& events = lane->events;

    for (const auto& run : forwardRuns (ticks))
    {
        const auto l0 = run.front().tick, l1 = run.back().tick;
        const auto continuing = w.lastLocal >= 0 && w.lastLocal < l0;
        const auto from = continuing ? w.lastLocal + 1 : l0;
        const auto heldBefore = std::any_of (events.begin(), events.end(), [&] (const PatternEvent& e) { return e.tick < from; });
        const auto nextOld = std::find_if (events.begin(), events.end(), [&] (const PatternEvent& e) { return e.tick > l1 + 1; });
        // the value that held after the gesture before it was recorded (it comes back after the gesture)
        const auto oldAfter = lane->valueAt ((double) l1 + 1.0);
        const auto restore = heldBefore && (nextOld == events.end() || nextOld->tick > l1 + 1);

        events.erase (std::remove_if (events.begin(), events.end(), [&] (const PatternEvent& e) { return e.tick >= from && e.tick <= l1 + 1; }),
                      events.end());
        for (const auto& p : run)
            events.push_back ({ p.tick, p.value });
        if (restore && l1 + 1 < loopLength)
            events.push_back ({ l1 + 1, oldAfter });
        std::stable_sort (events.begin(), events.end(), [] (const PatternEvent& x, const PatternEvent& y) { return x.tick < y.tick; });
        // steps: an event that repeats the value before it changes nothing
        for (std::size_t i = 1; i < events.size();)
            if (events[i].tick >= from && events[i].tick <= l1 && sameValue (events[i].value, events[i - 1].value))
                events.erase (events.begin() + (std::ptrdiff_t) i);
            else
                ++i;
        w.lastLocal = l1;
    }
}

void NoteRecorder::begin (std::int64_t loopLength)
{
    active = true;
    linear = false;
    loop = std::max<std::int64_t> (1, loopLength);
    origin = -1;
    reach = -1;
    fromClip = false;
    held.clear();
}

std::int64_t NoteRecorder::songTick (const Song& song, double tick) const
{
    const auto position = (std::int64_t) std::floor (std::max (0.0, tick));
    if (linear || song.hasLoop()) // (a loop region: the transport wraps it itself)
        return position;
    return position % std::max<std::int64_t> (1, song.songLength()); // the song plays in a loop
}

void NoteRecorder::add (const Song& song, const RecordedEvent& e)
{
    if (! active)
        return;

    const auto id = std::make_pair (e.channel, e.key);

    if (e.on)
    {
        // Song mode: the take's first note sets where the pattern's start lies for the notes outside its clips
        if (song.mode == PlayMode::song && origin < 0)
            origin = takeOriginFor (song, songTick (song, e.tick), &fromClip);

        // repeated note without an "off" in between: close the previous one
        if (auto it = held.find (id); it != held.end())
            finish (song, e.channel, e.key, it->second, e.tick);

        held[id] = { e.tick, e.velocity };
        return;
    }

    if (auto it = held.find (id); it != held.end())
    {
        finish (song, e.channel, e.key, it->second, e.tick);
        held.erase (it);
    }
}

void NoteRecorder::end (const Song& song, double tick)
{
    for (const auto& [id, h] : held)
        finish (song, id.first, id.second, h, tick);

    held.clear();
    active = false;
}

void NoteRecorder::finish (const Song& song, int channel, int key, const Held& h, double endTick)
{
    Note n;
    if (song.mode == PlayMode::song)
    {
        const auto at = songTick (song, h.tick);
        if (origin < 0)
            origin = takeOriginFor (song, at, &fromClip);
        n.start = songNotePosition (song, at, origin);
        if (! inCurrentPatternClip (song, at))
            reach = std::max (reach, origin + n.start);
    }
    else
    {
        n.start = patternPositionForTick (song, h.tick, loop);
    }
    // Song mode loops (the loop region, or the whole song): a note held across the loop's end ends after the wrap
    auto played = endTick - h.tick;
    if (played < 0.0 && song.mode == PlayMode::song)
        played += (double) (song.hasLoop() ? song.loopEnd - song.loopStart : std::max<std::int64_t> (1, song.songLength()));
    n.length = std::max<std::int64_t> (ticksPerStep / 8, (std::int64_t) std::llround (played));
    n.channel = channel;
    n.key = std::clamp (key, 0, 127);
    n.velocity = std::clamp (h.velocity, 0.0f, 1.0f);
    finished.push_back (n);
}

std::vector<Note> NoteRecorder::takeFinished()
{
    std::vector<Note> out;
    out.swap (finished);
    return out;
}

// --- Capture MIDI (round 170d) -----------------------------------------------------------------------------------------

namespace
{
double captureEnd (const scorelog::Entry& e, const CaptureOptions& o) noexcept
{
    return e.endMs >= 0.0 ? e.endMs : std::max (e.startMs, o.nowMs);
}
} // namespace

std::vector<scorelog::Entry> capturePhrase (const Song& song, const std::vector<scorelog::Entry>& log, const CaptureOptions& options)
{
    std::vector<scorelog::Entry> phrase;
    auto usable = [&] (const scorelog::Entry& e) {
        return ! e.recorded && e.startMs > options.afterMs && e.channel >= 0 && e.channel < (int) song.channels.size();
    };
    // back from the newest note, while the transport state stays and no silence is longer than gapMs
    auto newest = (int) log.size() - 1;
    while (newest >= 0 && ! usable (log[(std::size_t) newest]))
        --newest;
    if (newest < 0)
        return phrase;
    const auto played = log[(std::size_t) newest].startTick >= 0.0;
    auto phraseStart = log[(std::size_t) newest].startMs;
    for (auto i = newest; i >= 0; --i)
    {
        const auto& e = log[(std::size_t) i];
        if (e.recorded || e.startMs <= options.afterMs)
            break;
        if ((e.startTick >= 0.0) != played || captureEnd (e, options) < phraseStart - std::max (0.0, options.gapMs))
            break;
        if (e.channel < 0 || e.channel >= (int) song.channels.size()) // (a channel deleted since)
            continue;
        phrase.push_back (e);
        phraseStart = std::min (phraseStart, e.startMs);
    }
    std::reverse (phrase.begin(), phrase.end());
    return phrase;
}

CaptureResult captureMidi (Song& song, const std::vector<scorelog::Entry>& log, const CaptureOptions& options)
{
    CaptureResult result;
    if (song.patterns.empty() || song.currentPattern < 0 || song.currentPattern >= (int) song.patterns.size())
        return result;
    auto endOf = [&options] (const scorelog::Entry& e) { return captureEnd (e, options); };
    const auto phrase = capturePhrase (song, log, options);
    if (phrase.empty())
        return result;
    for (const auto& e : phrase)
        result.newestMs = std::max (result.newestMs, e.startMs);
    const auto played = phrase.back().startTick >= 0.0;
    result.played = played;

    const auto ticksPerMs = std::max (1.0, song.bpm) * ppq / 60000.0;
    const auto pattern = song.currentPattern;
    std::vector<int> cleared;

    if (! played && song.mode != PlayMode::song)
    {
        // stopped, Pattern mode: from the pattern start, the timing kept (the pattern grows to the phrase)
        std::vector<Note> notes;
        for (const auto& e : phrase)
        {
            Note n;
            n.start = (std::int64_t) std::llround ((e.startMs - phrase.front().startMs) * ticksPerMs);
            n.length = std::max<std::int64_t> (ticksPerStep / 8, (std::int64_t) std::llround ((endOf (e) - e.startMs) * ticksPerMs));
            n.channel = e.channel;
            n.key = std::clamp (e.key, 0, 127);
            n.velocity = std::clamp (e.velocity, 0.0f, 1.0f);
            notes.push_back (n);
        }
        result.notes = (int) notes.size();
        NoteRecorder none;
        commitNoteBatch (song, none, std::move (notes), options.mode, options.quantize, cleared, false, true);
        return result;
    }

    // as a take: the note-ons and releases through a NoteRecorder (Song mode: placed where they were played, a clip at
    // the origin; Pattern mode: in the loop)
    const auto base = played ? 0.0 : (double) song.barAt (std::max<std::int64_t> (0, options.playhead)).tick;
    std::vector<RecordedEvent> events;
    for (const auto& e : phrase)
    {
        const auto on = played ? e.startTick : base + (e.startMs - phrase.front().startMs) * ticksPerMs;
        const auto off = on + (endOf (e) - e.startMs) * ticksPerMs;
        events.push_back ({ on, e.channel, e.key, e.velocity, true });
        events.push_back ({ off, e.channel, e.key, 0.0f, false });
    }
    // in the order they happened (a release before a note-on at the same tick)
    std::stable_sort (events.begin(), events.end(), [] (const RecordedEvent& a, const RecordedEvent& b) {
        return a.tick < b.tick || (a.tick == b.tick && ! a.on && b.on);
    });
    NoteRecorder recorder;
    recorder.begin (song.patterns[(std::size_t) pattern].effectiveLength (song.beatsPerBar));
    // (Song mode: the positions are already the song's — played ones as the transport wrapped them, stopped ones laid
    // out from the playhead's bar — none wraps again at the song's end)
    recorder.setLinear (song.mode == PlayMode::song);
    double last = 0.0;
    for (const auto& ev : events)
    {
        recorder.add (song, ev);
        last = std::max (last, ev.tick);
    }
    recorder.end (song, last);
    auto notes = recorder.takeFinished();
    result.notes = (int) notes.size();
    commitNoteBatch (song, recorder, std::move (notes), options.mode, options.quantize, cleared, options.placeClip, true);
    return result;
}
} // namespace lynx
