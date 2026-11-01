#include "core/Scheduler.h"

#include <algorithm>
#include <cmath>

namespace lynx
{
void EventBuffer::add (const NoteEvent& e) noexcept
{
    if (count < capacity)
        events[(std::size_t) count++] = e;
    else
        ++dropped;
}

void EventBuffer::sortByOffset() noexcept
{
    // "off" before "on" on the same sample, so a repeated note isn't cut. A stable insertion sort in place: this runs
    // on the audio thread for every block with events, and std::stable_sort allocates a temporary buffer (round 160);
    // the events arrive nearly in order, so it is close to linear.
    const auto before = [] (const NoteEvent& a, const NoteEvent& b) {
        if (a.sampleOffset != b.sampleOffset)
            return a.sampleOffset < b.sampleOffset;
        return a.type != NoteEvent::Type::on && b.type == NoteEvent::Type::on;
    };
    for (int i = 1; i < count; ++i)
    {
        const auto e = events[(std::size_t) i];
        auto j = i;
        while (j > 0 && before (e, events[(std::size_t) j - 1]))
        {
            events[(std::size_t) j] = events[(std::size_t) j - 1];
            --j;
        }
        events[(std::size_t) j] = e;
    }
}

namespace
{
// The sample a tick falls on, as FL's sequencer places it: round(position in samples), ties to even (FLEngine
// FUN_00d0ece0: the k-th tick is at round(Σ samples per tick), cvtsd2si; REA_rodada76 §5). Positions within 10⁻⁶ of a
// half are taken as the exact tie the tick arithmetic rounds off.
double flSampleOf (double samples) noexcept
{
    const auto below = std::floor (samples);
    if (std::abs (samples - below - 0.5) < 1.0e-6)
        return std::fmod (below, 2.0) == 0.0 ? below : below + 1.0;
    return std::nearbyint (samples);
}

// Emits an event for each repetition of tick `t` (within the pattern) whose sample (FL's rounding) falls in the block
// [start, end).
void emitOccurrences (double t, std::int64_t loopLength, double start, double end, double tps, int numSamples,
                      NoteEvent event, EventBuffer& out) noexcept
{
    const auto len = (double) loopLength;
    const auto first = std::nearbyint (start / tps); // the block's first sample (start is a whole sample)
    // first repetition k with t + k*len >= start - one sample
    auto k = std::ceil ((start - tps - t) / len);

    for (auto tick = t + k * len; tick < end; tick += len)
    {
        const auto offset = flSampleOf (tick / tps) - first;
        if (offset < 0.0 || offset >= (double) numSamples)
            continue;
        event.sampleOffset = (int) offset;
        out.add (event);
    }
}
using NoteTiming = PlayedTiming;
NoteTiming noteTiming (const Note& note, float swing, const ChannelDef* c) noexcept { return playedTiming (note, swing, c); }

const ChannelDef* channelOf (const std::vector<ChannelDef>* channels, int channel) noexcept
{
    return channels != nullptr && channel >= 0 && channel < (int) channels->size() ? &(*channels)[(std::size_t) channel] : nullptr;
}
} // namespace

// FL's Misc › Time (REA round 146b [EXEC]): Shift delays a channel's notes (on and off), Gate cuts the long ones to its
// length — not the porta notes when Full porta is on.
PlayedTiming playedTiming (const Note& note, float swing, const ChannelDef* c) noexcept
{
    PlayedTiming t;
    t.start = note.start + swingDelay (note.start, swing, c != nullptr ? c->swingMix : 1.0f);
    t.length = std::max<std::int64_t> (note.length, 1);
    if (c == nullptr)
        return t;
    if (c->misc.shiftBeats > 0.0f)
        t.start += (std::int64_t) std::llround ((double) c->misc.shiftBeats * ppq);
    if (c->misc.gateBeats > 0.0f)
    {
        const auto porta = (c->instrument == InstrumentKind::synth ? c->synth.porta : c->instrument == InstrumentKind::sampler && c->sampler.porta) != note.porta;
        if (! (porta && c->misc.fullPorta))
            t.length = std::min (t.length, std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) c->misc.gateBeats * ppq)));
    }
    return t;
}

std::int64_t maxChannelShift (const std::vector<ChannelDef>& channels) noexcept
{
    float beats = 0.0f;
    for (const auto& c : channels)
        beats = std::max (beats, c.misc.shiftBeats);
    return (std::int64_t) std::ceil ((double) beats * ppq);
}

void schedulePatternLoop (const Pattern& pattern,
                          std::int64_t patternLength,
                          double startTick,
                          int numSamples,
                          double tps,
                          EventBuffer& out,
                          float swing,
                          const std::vector<ChannelDef>* channels,
                          bool chase) noexcept
{
    if (numSamples <= 0 || patternLength <= 0 || tps <= 0.0)
        return;

    const auto endTick = startTick + numSamples * tps;
    const auto position = std::fmod (startTick, (double) patternLength);

    for (const auto& note : pattern.notes)
    {
        if (note.start >= patternLength || note.muted)
            continue; // outside the loop / muted note

        NoteEvent on;
        on.type = NoteEvent::Type::on;
        on.channel = note.channel;
        on.key = note.key;
        on.velocity = note.velocity;
        on.pan = note.pan;
        on.cents = note.finePitch;
        on.porta = note.porta;
        on.modX = note.modX;
        on.modY = note.modY;
        on.release = note.release;
        on.colour = (std::int8_t) (note.colour & 15);
        const auto timing = noteTiming (note, swing, channelOf (channels, note.channel));
        const auto start = timing.start;

        if (note.slide)
        {
            on.type = NoteEvent::Type::slide;
            on.slideSamples = (float) ((double) std::max<std::int64_t> (note.length, 1) / tps);
            emitOccurrences ((double) start, patternLength, startTick, endTick, tps, numSamples, on, out);
            continue; // slide has no "off": the original note continues until its own end
        }

        emitOccurrences ((double) start, patternLength, startTick, endTick, tps, numSamples, on, out);
        if (chase && (double) start + tps <= position && (double) (start + timing.length) > position)
        {
            on.sampleOffset = 0; // a note crossing the start position ("Play truncated notes on transport")
            out.add (on);
        }

        NoteEvent off = on;
        off.type = NoteEvent::Type::off;
        off.velocity = 0.0f;
        const auto length = timing.length;
        emitOccurrences ((double) (start + length), patternLength, startTick, endTick, tps, numSamples, off, out);
    }

    out.sortByOffset();
}
ScheduleIndex makeScheduleIndex (const Song& song)
{
    ScheduleIndex index;
    index.songLength = song.songLength();
    index.maxShift = maxChannelShift (song.channels);
    index.maxNoteLength.reserve (song.patterns.size());
    index.patternLength.reserve (song.patterns.size());

    for (const auto& p : song.patterns)
    {
        std::int64_t longest = 0;
        for (const auto& n : p.notes)
            longest = std::max (longest, n.length);
        index.maxNoteLength.push_back (longest);
        index.patternLength.push_back (std::max<std::int64_t> (1, p.effectiveLength (song.beatsPerBar)));
    }

    return index;
}

PlaybackLoop playbackLoop (bool songMode, double loopStart, double loopEnd, double songLength, double songStart) noexcept
{
    if (! songMode)
        return {};
    if (loopEnd > loopStart)
        return { loopStart, loopEnd };
    if (songStart > 0.0 && songStart < songLength)
        return { songStart, songLength };
    return { 0.0, std::max (0.0, songLength) };
}

double hostPositionTick (const Song& song, double tick, std::int64_t songLength) noexcept
{
    tick = std::max (0.0, tick);
    if (song.mode == PlayMode::song)
        return song.hasLoop() ? tick : std::fmod (tick, (double) std::max<std::int64_t> (1, songLength));
    if (song.patterns.empty())
        return tick;
    const auto& pattern = song.patterns[(std::size_t) std::clamp (song.currentPattern, 0, (int) song.patterns.size() - 1)];
    return std::fmod (tick, (double) std::max<std::int64_t> (1, pattern.effectiveLength (song.beatsPerBar)));
}

namespace
{
// One clip of the Playlist (or a performance-mode launch, played as a clip of its own), its events in the block.
// [loopStart, loopEnd) = the block within the song loop of length `loop`.
void scheduleClip (const Song& song, const Clip& clip, std::size_t clipIndex, double startTick, double endTick, int numSamples,
                   double tps, std::int64_t loop, double loopStart, double loopEnd, bool resume, const ScheduleIndex* index,
                   EventBuffer& out, bool chase = false) noexcept
{
    auto overlaps = [&] (double from, double to) {
        return (from < loopEnd && to > loopStart) || (loopEnd > (double) loop && from < loopEnd - (double) loop);
    };
    const auto secondsPerTick = 60.0 / (std::max (song.bpm, 1.0) * ppq);

    if (clip.kind == ClipKind::audio)
    {
        if (clip.channel < 0 || clip.channel >= (int) song.channels.size() || clip.length <= 0)
            return;

        NoteEvent e;
        e.channel = clip.channel;
        e.key = audioClipKey (clipIndex);
        e.velocity = clip.gain;
        e.pan = clip.pan;
        e.fadeIn = (float) ((double) clip.effectiveFadeIn() / tps);
        e.fadeOut = (float) ((double) clip.effectiveFadeOut() / tps);
        e.fadeShape = (std::uint8_t) ((clip.crossfadeIn > 0 ? 1 : 0) | (clip.crossfadeOut > 0 ? 2 : 0) | (clip.reversed ? 4 : 0));
        e.clipPitch = clip.pitch; // FL 2026's clip pitch (round 167k): shifted, the clip's length stays
        e.clipStretch = (float) clip.stretch; // FL 2026's clip stretch (round 167l)

        if (resume && loopStart > (double) clip.start && loopStart < (double) clip.end())
        {
            e.type = NoteEvent::Type::on;
            e.sampleOffset = 0;
            e.startSeconds = ((double) clip.offset + (double) clip.offsetFraction + loopStart - (double) clip.start) * secondsPerTick;
            e.clipElapsed = (float) ((loopStart - (double) clip.start) / tps);
            e.clipRemaining = (float) (((double) clip.end() - loopStart) / tps);
            out.add (e);
        }

        if (! overlaps ((double) clip.start, (double) clip.end() + 1.0))
            return;

        e.type = NoteEvent::Type::on;
        e.startSeconds = std::max (0.0, ((double) clip.offset + (double) clip.offsetFraction) * secondsPerTick);
        e.clipElapsed = 0.0f;
        e.clipRemaining = (float) ((double) clip.length / tps);
        emitOccurrences ((double) clip.start, loop, startTick, endTick, tps, numSamples, e, out);

        e.type = NoteEvent::Type::off;
        e.startSeconds = 0.0;
        emitOccurrences ((double) clip.end(), loop, startTick, endTick, tps, numSamples, e, out);
        return;
    }

    if (clip.kind != ClipKind::pattern || clip.pattern < 0 || clip.pattern >= (int) song.patterns.size() || clip.length <= 0)
        return;

    // one tick of slack to catch the "off" that falls exactly at the end of the clip
    if (! overlaps ((double) clip.start, (double) clip.end() + 1.0))
        return;

    const auto& pattern = song.patterns[(std::size_t) clip.pattern];
    const auto& notes = pattern.notes;
    // FL repeats a pattern clip's content along a clip longer than its pattern (issue #25; the Playlist draws the
    // passes): pass k plays the pattern's notes k·L later, L = the pattern's length. A clip within its pattern is
    // one pass of the notes in [offset, offset + length), as before.
    const auto period = index != nullptr && (std::size_t) clip.pattern < index->patternLength.size()
                            ? index->patternLength[(std::size_t) clip.pattern]
                            : std::max<std::int64_t> (1, pattern.effectiveLength (song.beatsPerBar));
    const auto repeats = clip.offset + clip.length > period;
    const auto firstPass = repeats ? clip.offset / period : 0, lastPass = repeats ? (clip.offset + clip.length - 1) / period : 0;

    // one pass: the notes `shift` ticks later (pass ticks = pattern ticks + shift)
    auto playPass = [&] (std::int64_t shift) {
        auto first = notes.begin(), last = notes.end();

        // with the index and without the block wrapping around the end of the song: only the notes whose "on" or "off"
        // can fall in the block (swing delays by at most half a step)
        if (index != nullptr && loopEnd <= (double) loop && (std::size_t) clip.pattern < index->maxNoteLength.size())
        {
            const auto local = (double) (clip.offset - clip.start - shift);
            const auto from = loopStart + local - (double) (ticksPerStep + index->maxNoteLength[(std::size_t) clip.pattern] + index->maxShift) - 1.0;
            const auto to = loopEnd + local + 1.0;
            if (to < 0.0 || (repeats && from >= (double) period))
                return;
            first = std::lower_bound (notes.begin(), notes.end(), from, [] (const Note& n, double t) { return (double) n.start < t; });
            last = std::upper_bound (first, notes.end(), to, [] (double t, const Note& n) { return t < (double) n.start; });
        }

        for (auto it = first; it != last; ++it)
        {
            const auto& note = *it;
            if (repeats && note.start >= period) // a pass is the pattern's length
                continue;
            const auto start = note.start + shift; // where the note is in the clip's passes

            if (note.muted || start >= clip.offset + clip.length)
                continue;

            const auto timing = noteTiming (note, song.swing, channelOf (&song.channels, note.channel));
            auto on = std::min (clip.start + (start - clip.offset) + (timing.start - note.start), clip.end() - 1);
            auto off = std::min (on + timing.length, clip.end());

            // FL's "Play truncated notes in clips" (FUN_010608a0): a note starting before the clip's offset keeps what is
            // left of it from the clip's start (dropped when nothing is left); off: dropped
            if (start < clip.offset)
            {
                const auto left = start + timing.length - clip.offset;
                if (! song.playTruncatedNotes || left < 1)
                    continue;
                on = clip.start;
                off = std::min (clip.start + left, clip.end());
            }

            NoteEvent e;
            e.type = NoteEvent::Type::on;
            e.channel = note.channel;
            e.key = note.key;
            e.velocity = note.velocity;
            e.pan = note.pan;
            e.cents = note.finePitch;
            e.porta = note.porta;
            e.modX = note.modX;
            e.modY = note.modY;
            e.release = note.release;
            e.colour = (std::int8_t) (note.colour & 15);

            if (note.slide)
            {
                e.type = NoteEvent::Type::slide;
                e.slideSamples = (float) ((double) (off - on) / tps);
                emitOccurrences ((double) on, loop, startTick, endTick, tps, numSamples, e, out);
                continue;
            }

            emitOccurrences ((double) on, loop, startTick, endTick, tps, numSamples, e, out);
            if (chase && (double) on + tps <= loopStart && (double) off > loopStart)
            {
                e.sampleOffset = 0; // a note crossing the start position ("Play truncated notes on transport")
                out.add (e);
            }

            e.type = NoteEvent::Type::off;
            e.velocity = 0.0f;
            emitOccurrences ((double) off, loop, startTick, endTick, tps, numSamples, e, out);
        }
    };

    if (! repeats)
        return playPass (0);
    auto from = firstPass, to = lastPass;
    if (index != nullptr && loopEnd <= (double) loop && (std::size_t) clip.pattern < index->maxNoteLength.size())
    {
        // only the passes the block can reach (a long clip of a short pattern has many)
        const auto slack = (double) (ticksPerStep + index->maxNoteLength[(std::size_t) clip.pattern] + index->maxShift) + 1.0;
        const auto at = [&] (double tick) { return (std::int64_t) std::floor ((tick - (double) clip.start + (double) clip.offset) / (double) period); };
        from = std::max (from, at (loopStart - slack));
        to = std::min (to, at (loopEnd + 1.0));
    }
    for (auto pass = from; pass <= to; ++pass)
        playPass (pass * period);
}
} // namespace

void scheduleSingleClip (const Song& song, const Clip& clip, std::size_t clipIndex, double startTick, int numSamples, double tps,
                         EventBuffer& out, const ScheduleIndex* index) noexcept
{
    if (numSamples <= 0 || tps <= 0.0)
        return;
    const auto endTick = startTick + numSamples * tps;
    // a "loop" longer than anything played: the clip plays once, where it is
    const auto loop = (std::max<std::int64_t> ((std::int64_t) std::ceil (endTick), clip.end()) + 1) * 2;
    scheduleClip (song, clip, clipIndex, startTick, endTick, numSamples, tps, loop, startTick, endTick, false, index, out);
}

void scheduleSong (const Song& song, double startTick, int numSamples, double tps, EventBuffer& out, bool resume,
                   const ScheduleIndex* index, bool wrap, bool chase) noexcept
{
    if (numSamples <= 0 || tps <= 0.0 || song.playlist.empty())
        return;

    const auto endTick = startTick + numSamples * tps;
    auto loop = index != nullptr ? index->songLength : song.songLength();
    if (! wrap) // a "loop" longer than anything played: nothing starts over
        loop = std::max<std::int64_t> (loop, (std::int64_t) std::ceil (endTick) + 1) * 2;

    // Block window within the loop; if it crosses the end, it becomes two windows.
    const auto loopStart = std::fmod (startTick, (double) loop);
    const auto loopEnd = loopStart + (endTick - startTick);

    const auto zoneEnd = song.performanceZoneEnd();

    for (std::size_t clipIndex = 0; clipIndex < song.playlist.size(); ++clipIndex)
    {
        const auto& clip = song.playlist[clipIndex];

        if (clip.muted || song.isTrackMuted (clip.track))
            continue;
        if (clip.start < zoneEnd) // Performance mode: the zone's clips play only when launched (schedulePerformance)
            continue;

        scheduleClip (song, clip, clipIndex, startTick, endTick, numSamples, tps, loop, loopStart, loopEnd, resume, index, out, chase);
    }

    out.sortByOffset();
}
} // namespace lynx
