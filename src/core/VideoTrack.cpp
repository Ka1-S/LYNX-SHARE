#include "core/VideoTrack.h"

#include <algorithm>
#include <cmath>

#include "core/Song.h"

namespace lynx::videotrack
{
namespace
{
void sortClips (VideoTrack& track)
{
    std::stable_sort (track.clips.begin(), track.clips.end(), [] (const VideoClip& a, const VideoClip& b) { return a.start < b.start; });
}

// Cuts the other clips where `keep` covers them (a clip fully under it goes)
void clearUnder (VideoTrack& track, std::size_t keep)
{
    const auto k = track.clips[keep];
    std::vector<VideoClip> out;
    for (std::size_t i = 0; i < track.clips.size(); ++i)
    {
        auto c = track.clips[i];
        if (i == keep || c.end() <= k.start + 1e-9 || c.start >= k.end() - 1e-9)
        {
            out.push_back (c);
            continue;
        }
        if (c.start < k.start)
        {
            // the part before `keep` stays (and a part after it, when `keep` sits inside)
            auto head = c;
            head.length = k.start - c.start;
            out.push_back (head);
            if (c.end() > k.end())
            {
                auto tail = c;
                tail.offset += k.end() - c.start;
                tail.start = k.end();
                tail.length = c.end() - k.end();
                tail.uid = c.uid ^ 0x9e3779b97f4a7c15ull; // (a new id for the new piece)
                out.push_back (tail);
            }
        }
        else if (c.end() > k.end())
        {
            c.offset += k.end() - c.start;
            c.length = c.end() - k.end();
            c.start = k.end();
            out.push_back (c);
        }
        // (else entirely covered: removed)
    }
    track.clips = std::move (out);
    sortClips (track);
}

int indexOfUid (const VideoTrack& track, std::uint64_t uid, double start)
{
    for (std::size_t i = 0; i < track.clips.size(); ++i)
        if (track.clips[i].uid == uid && std::abs (track.clips[i].start - start) < 1e-9)
            return (int) i;
    return -1;
}
} // namespace

std::optional<Showing> showing (const VideoTrack& track, double songSeconds) noexcept
{
    if (track.muted)
        return std::nullopt;
    // the last clip starting at or before t (clips are sorted and do not overlap)
    const auto it = std::upper_bound (track.clips.begin(), track.clips.end(), songSeconds,
                                      [] (double t, const VideoClip& c) { return t < c.start; });
    if (it == track.clips.begin())
        return std::nullopt;
    const auto& c = *(it - 1);
    if (songSeconds >= c.end())
        return std::nullopt;
    return Showing { (int) (it - 1 - track.clips.begin()), c.offset + (songSeconds - c.start) };
}

double ticksForSeconds (const Song& song, double seconds)
{
    if (seconds <= 0.0)
        return 0.0;
    const auto hasTempo = song.mode == PlayMode::song
                          && std::any_of (song.automations.begin(), song.automations.end(),
                                          [] (const Automation& a) { return a.target.kind == AutomationTargetKind::tempo; });
    if (! hasTempo)
        return seconds * std::max (1.0, song.bpm) * ppq / 60.0;
    // the same 1/16 steps as Song::secondsForTicks
    constexpr std::int64_t step = ticksPerStep;
    double at = 0.0;
    for (std::int64_t t = 0;; t += step)
    {
        const auto perTick = 60.0 / (std::max (1.0, song.tempoAt ((double) t)) * ppq);
        const auto span = perTick * (double) step;
        if (at + span >= seconds || t > (std::int64_t) 1 << 40)
            return (double) t + (seconds - at) / perTick;
        at += span;
    }
}

TempoMap::TempoMap (const Song& song, double maxSeconds)
{
    secondsPerTick = 60.0 / (std::max (1.0, song.bpm) * ppq);
    const auto hasTempo = song.mode == PlayMode::song
                          && std::any_of (song.automations.begin(), song.automations.end(),
                                          [] (const Automation& a) { return a.target.kind == AutomationTargetKind::tempo; });
    if (! hasTempo)
        return;
    constexpr std::int64_t step = ticksPerStep;
    double at = 0.0;
    stepStarts.push_back (0.0);
    for (std::int64_t t = 0; at <= maxSeconds && stepStarts.size() < 4000000; t += step)
    {
        secondsPerTick = 60.0 / (std::max (1.0, song.tempoAt ((double) t)) * ppq);
        at += secondsPerTick * (double) step;
        stepStarts.push_back (at);
    }
}

double TempoMap::ticks (double s) const noexcept
{
    if (s <= 0.0)
        return 0.0;
    if (stepStarts.size() < 2)
        return s / secondsPerTick;
    constexpr double step = (double) ticksPerStep;
    if (s >= stepStarts.back())
        return (double) (stepStarts.size() - 1) * step + (s - stepStarts.back()) / secondsPerTick;
    const auto it = std::upper_bound (stepStarts.begin(), stepStarts.end(), s);
    const auto k = (std::size_t) (it - stepStarts.begin()) - 1;
    const auto span = stepStarts[k + 1] - stepStarts[k];
    return ((double) k + (span > 0.0 ? (s - stepStarts[k]) / span : 0.0)) * step;
}

double TempoMap::seconds (double t) const noexcept
{
    if (t <= 0.0)
        return 0.0;
    if (stepStarts.size() < 2)
        return t * secondsPerTick;
    constexpr double step = (double) ticksPerStep;
    const auto k = (std::size_t) (t / step);
    if (k + 1 >= stepStarts.size())
        return stepStarts.back() + (t - (double) (stepStarts.size() - 1) * step) * secondsPerTick;
    return stepStarts[k] + (stepStarts[k + 1] - stepStarts[k]) * (t / step - (double) k);
}

int addClip (VideoTrack& track, const std::string& path, double start, double duration, std::uint64_t uid)
{
    VideoClip c;
    c.path = path;
    c.start = std::max (0.0, start);
    c.offset = 0.0;
    c.length = duration > 0.0 ? duration : 10.0;
    c.duration = std::max (0.0, duration);
    c.uid = uid;
    track.clips.push_back (c);
    clearUnder (track, track.clips.size() - 1);
    return indexOfUid (track, uid, c.start);
}

int moveClip (VideoTrack& track, int index, double start)
{
    if (index < 0 || index >= (int) track.clips.size())
        return -1;
    auto& c = track.clips[(std::size_t) index];
    c.start = std::max (0.0, start);
    const auto uid = c.uid;
    const auto s = c.start;
    clearUnder (track, (std::size_t) index);
    return indexOfUid (track, uid, s);
}

void trimStart (VideoClip& clip, double newStart, double minLength) noexcept
{
    // the head can't go before the file's start nor past the end minus minLength
    const auto earliest = clip.start - clip.offset;
    newStart = std::clamp (newStart, std::max (0.0, earliest), clip.end() - minLength);
    const auto delta = newStart - clip.start;
    clip.offset += delta;
    clip.length -= delta;
    clip.start = newStart;
}

void trimEnd (VideoClip& clip, double newEnd, double minLength) noexcept
{
    auto latest = clip.duration > 0.0 ? clip.start + (clip.duration - clip.offset) : 1e12;
    newEnd = std::clamp (newEnd, clip.start + minLength, std::max (clip.start + minLength, latest));
    clip.length = newEnd - clip.start;
}

bool splitClip (VideoTrack& track, int index, double at, std::uint64_t uid)
{
    if (index < 0 || index >= (int) track.clips.size())
        return false;
    auto& c = track.clips[(std::size_t) index];
    if (at <= c.start + 1e-6 || at >= c.end() - 1e-6)
        return false;
    auto right = c;
    right.uid = uid;
    right.offset += at - c.start;
    right.start = at;
    right.length = c.end() - at;
    c.length = at - c.start;
    track.clips.insert (track.clips.begin() + index + 1, right);
    return true;
}

double snapToFrame (const VideoSettings& v, double songSeconds) noexcept
{
    const auto tc = timecodeOf (v, songSeconds);
    const auto f = std::floor (tc * v.rate.fps() + 0.5);
    return songOf (v, vtime::frameStart ((std::int64_t) f, v.rate));
}

double videoEnd (const std::vector<VideoTrack>& tracks) noexcept
{
    double e = 0.0;
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            e = std::max (e, c.end());
    return e;
}
} // namespace lynx::videotrack
