#include "core/PlaylistTrackOps.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <tuple>

#include "core/EventEdit.h"
#include "core/NoteEdit.h"
#include "core/PlaylistOps.h"
#include "core/RackOps.h"

namespace lynx::pltracks
{
namespace
{
PlaylistTrack& trackAt (Song& song, int track)
{
    if ((int) song.playlistTracks.size() <= track)
        song.playlistTracks.resize ((std::size_t) track + 1);
    return song.playlistTracks[(std::size_t) track];
}

const PlaylistTrack* findTrack (const Song& song, int track)
{
    return track >= 0 && track < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) track] : nullptr;
}

int sourceOf (const Clip& c) noexcept
{
    return c.kind == ClipKind::pattern ? c.pattern : c.kind == ClipKind::audio ? c.channel : c.automation;
}

bool sameSource (const Clip& a, const Clip& b) noexcept
{
    return a.kind == b.kind && sourceOf (a) == sourceOf (b);
}

bool anyClipOn (const Song& song, int track)
{
    bool found = false;
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        found = found || std::any_of (clips.begin(), clips.end(), [track] (const Clip& c) { return c.track == track; });
    });
    return found;
}

std::string freeName (const std::string& base, const std::function<bool (const std::string&)>& taken)
{
    if (! taken (base))
        return base;
    for (int n = 2;; ++n)
        if (auto name = base + " #" + std::to_string (n); ! taken (name))
            return name;
}
} // namespace

int dominantClip (const Song& song, int track)
{
    // FL counts identical items (FUN_012ca520 on the 0x54-byte item template): kind, source, length and offset
    std::map<std::tuple<int, int, std::int64_t, std::int64_t>, std::pair<int, int>> counts; // template → (count, first)
    int best = -1, bestCount = 0;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.track != track)
            continue;
        auto& [count, first] = counts.try_emplace ({ (int) c.kind, sourceOf (c), c.length, c.offset }, 0, i).first->second;
        if (++count > bestCount)
        {
            bestCount = count;
            best = first;
        }
    }
    return best;
}

void autoName (Song& song, int track)
{
    if (track < 0)
        return;
    // FUN_012233c0: the source most of the track's clips play
    std::map<std::pair<int, int>, int> counts;
    std::pair<int, int> best { -1, -1 };
    int bestCount = 0;
    for (const auto& c : song.playlist)
        if (c.track == track && ++counts[{ (int) c.kind, sourceOf (c) }] > bestCount)
        {
            best = { (int) c.kind, sourceOf (c) };
            bestCount = counts[best];
        }

    std::string name;
    std::uint32_t colour = 0;
    const auto source = best.second;
    if (bestCount > 0 && best.first == (int) ClipKind::pattern && source >= 0 && source < (int) song.patterns.size())
    {
        name = song.patterns[(std::size_t) source].name;
        colour = song.patterns[(std::size_t) source].colour;
    }
    else if (bestCount > 0 && best.first == (int) ClipKind::audio && source >= 0 && source < (int) song.channels.size())
    {
        name = song.channels[(std::size_t) source].name;
        colour = song.channels[(std::size_t) source].colour;
    }
    else if (bestCount > 0 && best.first == (int) ClipKind::automation && source >= 0 && source < (int) song.automations.size())
        name = song.automations[(std::size_t) source].name;

    auto& t = trackAt (song, track);
    t.name = name;
    t.colour = colour;
}

// FL (FUN_012e7a90): locking gives an icon-less track FL's lock icon (index 93), unlocking takes that icon away again
static void setLockIcon (PlaylistTrack& track, bool on)
{
    constexpr int flLockIcon = 93;
    if (on && track.icon == 0)
        track.icon = flLockIcon;
    else if (! on && track.icon == flLockIcon)
        track.icon = 0;
}

void toggleLockToContent (Song& song, const std::vector<int>& tracks)
{
    if (tracks.empty() || tracks.front() < 0)
        return;
    const auto* first = findTrack (song, tracks.front());
    const auto on = ! (first != nullptr && first->lockedToContent);
    for (const auto t : tracks)
    {
        if (t < 0)
            continue;
        auto& track = trackAt (song, t);
        track.lockedToContent = on;
        setLockIcon (track, on);
        if (on && song.playlistTracks[(std::size_t) t].name.empty())
            autoName (song, t);
    }
}

bool applyLockToContent (const Song& song, Clip& clip)
{
    const auto* t = findTrack (song, clip.track);
    if (t == nullptr || ! t->lockedToContent || t->mode == PlaylistTrackMode::instrument)
        return false;
    // a source the track already holds stays
    for (const auto& c : song.playlist)
        if (c.track == clip.track && sameSource (c, clip))
            return false;
    const auto dominant = dominantClip (song, clip.track);
    if (dominant < 0)
        return false;
    auto copy = song.playlist[(std::size_t) dominant];
    copy.start = clip.start;
    copy.track = clip.track;
    copy.muted = false;
    clip = copy;
    return true;
}

void lockAll (Song& song, int count, bool lock, bool resize)
{
    for (int t = 0; t < count; ++t)
    {
        auto& track = trackAt (song, t);
        track.lockedToContent = lock;
        setLockIcon (track, lock);
        if (lock && track.name.empty())
            autoName (song, t);
        if (resize)
        {
            auto& again = trackAt (song, t);
            again.lockedHeight = lock ? 1 : -1; // 1 px = the smallest row (FL: "their minimum size")
            again.height = 0;                    // FL: size 1.0
        }
    }
}

bool isSizeLocked (const Song& song, int track)
{
    const auto* t = findTrack (song, track);
    return t != nullptr && t->lockedHeight > 0;
}

void toggleSizeLock (Song& song, const std::vector<int>& tracks, int shownHeight)
{
    if (tracks.empty() || tracks.front() < 0)
        return;
    // FUN_01228cb0: not locked → the row's height now; locked → negated. The other selected tracks copy it (mask 0x10).
    auto& first = trackAt (song, tracks.front());
    first.lockedHeight = first.lockedHeight < 1 ? std::max (1, shownHeight) : -first.lockedHeight;
    const auto value = first.lockedHeight;
    for (const auto t : tracks)
        if (t >= 0)
            trackAt (song, t).lockedHeight = value;
}

void unlockSize (PlaylistTrack& track)
{
    track.lockedHeight = -std::abs (track.lockedHeight);
    if (track.lockedHeight == 0)
        track.lockedHeight = -1;
}

int sendPatternInstancesHere (Song& song, int track, const std::vector<int>& selection)
{
    if (track < 0)
        return 0;
    int moved = 0;
    auto move = [&] (Clip& c) {
        if (c.kind == ClipKind::pattern && c.track != track)
        {
            c.track = track;
            ++moved;
        }
    };
    if (! selection.empty())
    {
        for (const auto i : selection)
            if (i >= 0 && i < (int) song.playlist.size())
                move (song.playlist[(std::size_t) i]);
    }
    else
        for (auto& c : song.playlist)
            if (c.kind == ClipKind::pattern && c.pattern == song.currentPattern)
                move (c);
    // FUN_01228ea0: then auto name when the track has no name
    if (findTrack (song, track) == nullptr || song.playlistTracks[(std::size_t) track].name.empty())
        autoName (song, track);
    return moved;
}

int resetEmptyTracks (Song& song)
{
    int reset = 0;
    for (int t = (int) song.playlistTracks.size() - 1; t >= 0; --t)
    {
        const auto& track = song.playlistTracks[(std::size_t) t];
        if (anyClipOn (song, t) || track.mode != PlaylistTrackMode::unassigned)
            continue;
        if (track.name.empty() && track.colour == 0 && track.icon == 0 && ! track.lockedToContent)
            continue;
        song.playlistTracks[(std::size_t) t] = PlaylistTrack {};
        ++reset;
    }
    return reset;
}

void setAllGroupsCollapsed (Song& song, bool collapsed)
{
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        if (noteedit::isGroupHeader (song, t))
            song.playlistTracks[(std::size_t) t].collapsed = collapsed;
}

std::vector<int> unusedAudioClipChannels (const Song& song)
{
    std::vector<bool> used (song.channels.size(), false);
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) used.size())
                used[(std::size_t) c.channel] = true;
    });
    for (const auto& p : song.patterns)
        for (const auto& n : p.notes)
            if (n.channel >= 0 && n.channel < (int) used.size())
                used[(std::size_t) n.channel] = true;
    std::vector<int> out;
    for (int c = 0; c < (int) song.channels.size(); ++c)
        if (! used[(std::size_t) c] && song.channels[(std::size_t) c].instrument == InstrumentKind::audioClip)
            out.push_back (c);
    return out;
}

int purgeUnusedAudioClips (Song& song)
{
    return removeChannels (song, unusedAudioClipChannels (song));
}

std::vector<int> mergeCandidates (const Song& song, const std::vector<int>& selection)
{
    if (! selection.empty())
        return selection;
    std::vector<int> all ((std::size_t) song.playlist.size());
    for (std::size_t i = 0; i < all.size(); ++i)
        all[i] = (int) i;
    return all;
}

MergeResult mergePatternClips (Song& song, const std::vector<int>& clips, int* newClip)
{
    std::vector<int> list;
    for (const auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size())
            list.push_back (i);
    std::sort (list.begin(), list.end());
    list.erase (std::unique (list.begin(), list.end()), list.end());
    // FUN_01302d30: every clip of the same type
    for (const auto i : list)
        if (song.playlist[(std::size_t) i].kind != ClipKind::pattern)
            return MergeResult::mixedKinds;
    // FUN_012dc060's mask 0xa0: muted clips stay out
    list.erase (std::remove_if (list.begin(), list.end(), [&song] (int i) { return song.playlist[(std::size_t) i].muted; }), list.end());
    if (list.size() < 2)
        return MergeResult::notEnough;

    std::sort (list.begin(), list.end(), [&song] (int a, int b) {
        const auto& x = song.playlist[(std::size_t) a];
        const auto& y = song.playlist[(std::size_t) b];
        return x.start != y.start ? x.start < y.start : x.track < y.track;
    });
    const auto first = song.playlist[(std::size_t) list.front()];
    int lowest = (int) song.patterns.size();
    std::int64_t end = first.end();
    int topTrack = first.track;
    for (const auto i : list)
    {
        const auto& c = song.playlist[(std::size_t) i];
        lowest = std::min (lowest, c.pattern);
        end = std::max (end, c.end());
        topTrack = std::min (topTrack, c.track);
    }

    // the content: what each clip shows of its pattern (FL copies the clip's window; LYNX repeats a pattern along a
    // clip longer than it, so every repetition inside the window is copied)
    Pattern merged;
    for (const auto i : list)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.pattern < 0 || c.pattern >= (int) song.patterns.size())
            continue;
        const auto& p = song.patterns[(std::size_t) c.pattern];
        const auto period = std::max<std::int64_t> (1, p.effectiveLength (song.meter()));
        const auto from = c.offset, to = c.offset + c.length;
        const auto shift = c.start - first.start - c.offset;
        for (std::int64_t base = (from / period) * period; base < to; base += period)
        {
            for (auto n : p.notes)
                if (const auto at = base + n.start; at >= from && at < to)
                {
                    n.start = at + shift;
                    merged.notes.push_back (n);
                }
            for (const auto& lane : p.events)
            {
                auto it = std::find_if (merged.events.begin(), merged.events.end(), [&] (const PatternEvents& l) { return l.target == lane.target; });
                if (it == merged.events.end())
                {
                    merged.events.push_back ({ lane.target, {} });
                    it = merged.events.end() - 1;
                }
                for (auto e : lane.events)
                    if (const auto at = base + e.tick; at >= from && at < to)
                    {
                        e.tick = at + shift;
                        it->events.push_back (e);
                    }
            }
        }
    }
    merged.sortNotes();
    for (auto& lane : merged.events)
    {
        std::stable_sort (lane.events.begin(), lane.events.end(), [] (const PatternEvent& a, const PatternEvent& b) { return a.tick < b.tick; });
        lane.events.erase (std::unique (lane.events.begin(), lane.events.end(), [] (const PatternEvent& a, const PatternEvent& b) { return a.tick == b.tick; }),
                           lane.events.end());
    }
    const auto& source = song.patterns[(std::size_t) std::clamp (first.pattern, 0, (int) song.patterns.size() - 1)];
    merged.colour = source.colour;
    // FL (seen in FL 2026, round 180c): the free name, then " (merged)" ("P1" → "P1 #2 (merged)")
    merged.name = freeName (source.name, [&song] (const std::string& n) {
        return std::any_of (song.patterns.begin(), song.patterns.end(), [&n] (const Pattern& p) { return p.name == n; });
    }) + " (merged)";

    // the clips go, the new pattern comes after the lowest one involved, one clip of it at the first clip's place
    auto doomed = list;
    std::sort (doomed.rbegin(), doomed.rend());
    for (const auto i : doomed)
        song.playlist.erase (song.playlist.begin() + i);
    const auto index = insertPattern (song, std::min (lowest + 1, (int) song.patterns.size()), merged.name);
    auto name = merged.name;
    song.patterns[(std::size_t) index] = std::move (merged);
    song.patterns[(std::size_t) index].name = name;
    song.currentPattern = index;

    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = index;
    c.track = topTrack;
    c.start = first.start;
    c.length = std::max<std::int64_t> (1, end - first.start);
    song.playlist.push_back (c);
    if (newClip != nullptr)
        *newClip = (int) song.playlist.size() - 1;
    return MergeResult::merged;
}

MergeResult mergeSimilarPatternClips (Song& song, const std::vector<int>& clips, int* similarGroups)
{
    if (similarGroups != nullptr)
        *similarGroups = 0;

    // what FUN_012dc060 keeps of the list: unmuted pattern clips
    std::vector<int> list;
    for (const auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size())
            if (const auto& c = song.playlist[(std::size_t) i]; c.kind == ClipKind::pattern && ! c.muted)
                list.push_back (i);
    std::sort (list.begin(), list.end());
    list.erase (std::unique (list.begin(), list.end()), list.end());
    if (list.size() < 2)
        return MergeResult::notEnough;

    // the group to look for, in time order
    struct Wanted
    {
        int pattern;
        std::int64_t start, offset, length;
        bool found;
    };
    std::vector<Wanted> group;
    for (const auto i : list)
    {
        const auto& c = song.playlist[(std::size_t) i];
        group.push_back ({ c.pattern, c.start, c.offset, c.length, false });
    }
    std::stable_sort (group.begin(), group.end(), [] (const Wanted& a, const Wanted& b) { return a.start < b.start; });

    int added = -1;
    if (const auto result = mergePatternClips (song, list, &added); result != MergeResult::merged)
        return result;
    const auto merged = song.playlist[(std::size_t) added];
    for (auto& w : group) // the new pattern pushed the later ones up
        if (w.pattern >= merged.pattern)
            ++w.pattern;

    // FUN_012dbac0: the tracks don't count, and a clip that has no pattern window is simply offset 0 + its length
    const auto same = [] (const Wanted& w, const Clip& c) {
        return c.kind == ClipKind::pattern && ! c.muted && c.pattern == w.pattern && c.offset == w.offset && c.length == w.length;
    };

    // FUN_012dbb20, restarted from the first clip after every hit (as FUN_012dc060's loop does)
    for (bool again = true; again;)
    {
        again = false;
        std::vector<int> order ((std::size_t) song.playlist.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = (int) i;
        std::stable_sort (order.begin(), order.end(), [&song] (int a, int b) { return song.playlist[(std::size_t) a].start < song.playlist[(std::size_t) b].start; });

        for (const auto anchorIndex : order)
        {
            const auto anchor = song.playlist[(std::size_t) anchorIndex];
            if (! same (group.front(), anchor))
                continue;

            for (auto& w : group)
                w.found = false;
            std::vector<int> hits;
            int topTrack = anchor.track;
            std::size_t found = 0;
            const auto shift = group.front().start - anchor.start;
            const auto from = std::lower_bound (order.begin(), order.end(), anchor.start,
                                                [&song] (int i, std::int64_t tick) { return song.playlist[(std::size_t) i].start < tick; });
            for (auto it = from; it != order.end(); ++it)
            {
                const auto& c = song.playlist[(std::size_t) *it];
                for (auto& w : group)
                    if (! w.found && w.start == c.start + shift && same (w, c))
                    {
                        w.found = true;
                        ++found;
                        hits.push_back (*it);
                        topTrack = std::min (topTrack, c.track);
                        if (found >= group.size())
                            break;
                    }
            }
            if (found < group.size())
                continue;

            std::sort (hits.begin(), hits.end());
            hits.erase (std::unique (hits.begin(), hits.end()), hits.end());
            for (auto h = hits.rbegin(); h != hits.rend(); ++h)
                song.playlist.erase (song.playlist.begin() + *h);
            auto instance = merged;
            instance.uid = 0;
            instance.start = anchor.start;
            instance.track = topTrack;
            song.playlist.push_back (instance);
            if (similarGroups != nullptr)
                ++*similarGroups;
            again = true;
            break;
        }
    }
    return MergeResult::merged;
}

// The automation clips of `clips` in time order, or nothing when one of them isn't a plain automation clip
static std::vector<Clip> automationClipsInOrder (const Song& song, const std::vector<int>& clips)
{
    std::vector<Clip> list;
    for (const auto i : clips)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            continue;
        const auto& c = song.playlist[(std::size_t) i];
        if (c.kind != ClipKind::automation || c.automation < 0 || c.automation >= (int) song.automations.size()
            || song.automations[(std::size_t) c.automation].isLive())
            return {};
        list.push_back (c);
    }
    std::stable_sort (list.begin(), list.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    return list;
}

// FL's clip with no window (−1, −1): the whole automation. A LYNX clip may also run on past its last point, which only
// holds that point's value.
static bool showsWholeAutomation (const Clip& c, const Automation& a)
{
    return c.offset == 0 && c.length >= a.length();
}

// FUN_013007a0: a window needs control points at its start and its end
static bool hasControlPointsAtEnds (const Clip& c, const Automation& a)
{
    if (showsWholeAutomation (c, a))
        return true;
    const auto has = [&a] (std::int64_t tick) {
        return std::any_of (a.points.begin(), a.points.end(), [tick] (const AutomationPoint& p) { return p.tick == tick; });
    };
    return has (c.offset) && has (c.offset + c.length);
}

AutomationMergeCheck checkAutomationMerge (const Song& song, const std::vector<int>& clips)
{
    if (clips.size() < 2)
        return AutomationMergeCheck::tooFew;
    const auto list = automationClipsInOrder (song, clips);
    if (list.size() < 2)
        return AutomationMergeCheck::notAutomation;
    for (const auto& c : list)
        if (! hasControlPointsAtEnds (c, song.automations[(std::size_t) c.automation]))
            return AutomationMergeCheck::missingEnds;
    for (std::size_t i = 0; i + 1 < list.size(); ++i)
        if (list[i + 1].start < list[i].end())
            return AutomationMergeCheck::overlapping;
    return AutomationMergeCheck::ok;
}

bool automationLinksConflict (const Song& song, const std::vector<int>& clips)
{
    const auto list = automationClipsInOrder (song, clips);
    for (std::size_t i = 1; i < list.size(); ++i)
        if (! (song.automations[(std::size_t) list[i].automation].target == song.automations[(std::size_t) list.front().automation].target))
            return true;
    return false;
}

// FUN_012ff0d0: a clip that overlaps the next one stops where the next one starts
static void cutOverlaps (std::vector<Clip>& list)
{
    for (std::size_t i = 0; i + 1 < list.size(); ++i)
        list[i].length = std::min (list[i].length, list[i + 1].start - list[i].start);
}

PatternEvents automationMergeSamples (const Song& song, const std::vector<int>& clips)
{
    PatternEvents samples;
    auto list = automationClipsInOrder (song, clips);
    if (list.size() < 2)
        return samples;
    cutOverlaps (list);
    const auto origin = list.front().start;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& c = list[i];
        const auto& a = song.automations[(std::size_t) c.automation];
        const auto at = c.start - origin;
        if (! samples.events.empty() && samples.events.back().tick < at - 1)
            samples.events.push_back ({ at - 1, samples.events.back().value }); // the gap holds
        const auto last = i + 1 == list.size() ? c.length : c.length - 1; // the next clip takes over at its own start
        for (std::int64_t t = 0; t <= last; ++t)
            samples.events.push_back ({ at + t, a.valueAt ((double) (c.offset + t)) });
    }
    return samples;
}

// The new automation (points already made) takes the clips' place
static int replaceWithMergedAutomation (Song& song, const std::vector<int>& clips, std::vector<Clip> list, Automation merged, AutomationLinks links)
{
    const auto first = list.front();
    const auto origin = first.start;

    const auto& source = song.automations[(std::size_t) first.automation];
    merged.name = freeName (source.name + " - merged", [&song] (const std::string& n) {
        return std::any_of (song.automations.begin(), song.automations.end(), [&n] (const Automation& a) { return a.name == n; });
    });
    if (links != AutomationLinks::deleteAll)
        merged.target = source.target;

    std::vector<AutomationTarget> otherTargets;
    if (links == AutomationLinks::mergeAll)
        for (const auto& c : list)
        {
            const auto& t = song.automations[(std::size_t) c.automation].target;
            if (! (t == merged.target) && std::find (otherTargets.begin(), otherTargets.end(), t) == otherTargets.end())
                otherTargets.push_back (t);
        }

    std::int64_t end = origin;
    for (const auto& c : list)
        end = std::max (end, c.end());

    std::vector<int> doomed;
    for (const auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size())
            doomed.push_back (i);
    std::sort (doomed.rbegin(), doomed.rend());
    doomed.erase (std::unique (doomed.begin(), doomed.end()), doomed.end());
    for (const auto i : doomed)
        song.playlist.erase (song.playlist.begin() + i);

    const auto addClip = [&] (const Automation& a) {
        song.automations.push_back (a);
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = (int) song.automations.size() - 1;
        c.track = first.track;
        c.start = origin;
        c.length = std::max<std::int64_t> (1, end - origin);
        song.playlist.push_back (c);
        return (int) song.playlist.size() - 1;
    };
    const auto result = addClip (merged);
    for (const auto& target : otherTargets)
    {
        auto extra = merged;
        extra.target = target;
        extra.name = freeName (merged.name, [&song] (const std::string& n) {
            return std::any_of (song.automations.begin(), song.automations.end(), [&n] (const Automation& a) { return a.name == n; });
        });
        addClip (extra);
    }
    return result;
}

int mergeAutomationClips (Song& song, const std::vector<int>& clips, AutomationLinks links)
{
    const auto list = automationClipsInOrder (song, clips);
    if (list.size() < 2)
        return -1;
    const auto origin = list.front().start;

    Automation merged;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& c = list[i];
        const auto& a = song.automations[(std::size_t) c.automation];
        const auto at = c.start - origin;
        // FL's FUN_013009f0: a point with the last value where the next clip starts holds across a gap
        if (i > 0 && ! merged.points.empty())
        {
            merged.points.back().tension = 0.0f; // the curve after a clip's last point isn't shown
            merged.points.back().mode = 0;
            if (list[i - 1].end() < c.start)
                merged.points.push_back ({ at, merged.points.back().value, 0.0f });
        }
        const auto whole = showsWholeAutomation (c, a);
        for (auto p : a.points)
            if (whole || (p.tick >= c.offset && p.tick <= c.offset + c.length))
            {
                p.tick += at - c.offset;
                merged.points.push_back (p);
            }
    }
    return replaceWithMergedAutomation (song, clips, list, std::move (merged), links);
}

int mergeAutomationClipsLossy (Song& song, const std::vector<int>& clips, AutomationLinks links, float sensitivity)
{
    auto list = automationClipsInOrder (song, clips);
    if (list.size() < 2)
        return -1;
    Automation merged;
    merged.points = eventedit::toAutomationPoints (automationMergeSamples (song, clips), sensitivity);
    cutOverlaps (list);
    return replaceWithMergedAutomation (song, clips, std::move (list), std::move (merged), links);
}

std::string uniqueTrackName (const Song& song, const std::string& base, int except)
{
    auto taken = [&] (const std::string& name) {
        for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
            if (t != except && song.playlistTracks[(std::size_t) t].name == name)
                return true;
        return false;
    };
    if (! taken (base))
        return base;
    for (int n = 2;; ++n)
        if (auto name = base + " " + std::to_string (n); ! taken (name))
            return name;
}

std::vector<int> tracksToClone (const Song& song, std::vector<int> selected, bool groupedTracks)
{
    if (groupedTracks)
    {
        const auto chosen = selected;
        for (const auto t : chosen)
            if (noteedit::isGroupHeader (song, t))
                for (const auto m : noteedit::groupMembers (song, t))
                    selected.push_back (m);
    }
    std::sort (selected.begin(), selected.end());
    selected.erase (std::unique (selected.begin(), selected.end()), selected.end());
    selected.erase (std::remove_if (selected.begin(), selected.end(), [] (int t) { return t < 0; }), selected.end());
    return selected;
}

int cloneTracks (Song& song, const std::vector<int>& tracks, const CloneOptions& options)
{
    if (tracks.empty())
        return -1;
    // FUN_012edbe0: every selected track empty (no clips, default header) → "Nothing to clone"
    const auto allEmpty = std::all_of (tracks.begin(), tracks.end(), [&song] (int t) {
        const auto* info = findTrack (song, t);
        return ! anyClipOn (song, t) && (info == nullptr || (info->name.empty() && info->colour == 0 && info->mode == PlaylistTrackMode::unassigned));
    });
    if (allEmpty)
        return -1;

    // under the last track, past its group (FUN_012e8250)
    auto at = tracks.back() + 1;
    while (at < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) at].grouped)
        ++at;
    const auto firstClone = at;

    for (std::size_t k = 0; k < tracks.size(); ++k)
    {
        // source indices below `at` are unaffected by inserting at `at`
        const auto from = tracks[k];
        noteedit::insertTrack (song, at);
        auto info = findTrack (song, from) != nullptr ? song.playlistTracks[(std::size_t) from] : PlaylistTrack {};
        PlaylistTrack clone;
        clone.colour = info.colour;
        clone.lockedToContent = info.lockedToContent;
        clone.icon = info.icon;
        clone.lockedHeight = info.lockedHeight;
        clone.height = info.height;
        clone.muted = info.muted;
        clone.performance = info.performance;
        // grouping (+0x3e copied): a member's clone stays grouped (under the group it lands in); a header and its
        // members cloned together make a new group
        clone.grouped = info.grouped && at > 0;
        trackAt (song, at) = clone;
        if (! info.name.empty())
            song.playlistTracks[(std::size_t) at].name = uniqueTrackName (song, info.name, at);

        // track modes: an audio track gets its own mixer track, an instrument track a clone of its channel
        int newChannel = -1;
        if (info.mode == PlaylistTrackMode::audio && trackmodes::makeAudioTrack (song, at))
        {
            const auto mixer = song.playlistTracks[(std::size_t) at].mixerTrack;
            if (mixer > 0 && info.mixerTrack > 0 && info.mixerTrack < (int) song.mixer.size() && mixer < (int) song.mixer.size())
            {
                if (options.effects)
                    song.mixer[(std::size_t) mixer].effects = song.mixer[(std::size_t) info.mixerTrack].effects;
                if (options.routing)
                {
                    song.mixer[(std::size_t) mixer].sends = song.mixer[(std::size_t) info.mixerTrack].sends;
                    song.mixer[(std::size_t) mixer].toMaster = song.mixer[(std::size_t) info.mixerTrack].toMaster;
                }
            }
        }
        else if (info.mode == PlaylistTrackMode::instrument && info.channel >= 0 && info.channel < (int) song.channels.size())
        {
            newChannel = cloneChannel (song, info.channel);
            if (newChannel >= 0)
                trackmodes::makeInstrumentTrack (song, at, newChannel);
        }

        // the clips of the ticked kinds (positions kept); an instrument track's patterns are remade for its new channel
        std::vector<Clip> copies;
        std::map<int, int> patternFor;
        for (const auto& c : song.playlist)
        {
            if (c.track != from)
                continue;
            if ((c.kind == ClipKind::pattern && ! options.patterns) || (c.kind == ClipKind::audio && ! options.audioClips)
                || (c.kind == ClipKind::automation && ! options.automations))
                continue;
            auto copy = c;
            copy.track = at;
            copies.push_back (copy);
        }
        if (newChannel >= 0)
            for (auto& c : copies)
                if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
                {
                    if (! patternFor.count (c.pattern))
                    {
                        auto p = song.patterns[(std::size_t) c.pattern];
                        for (auto& n : p.notes)
                            if (n.channel == info.channel)
                                n.channel = newChannel;
                        p.name = song.channels[(std::size_t) newChannel].name;
                        song.patterns.push_back (std::move (p));
                        patternFor[c.pattern] = (int) song.patterns.size() - 1;
                    }
                    c.pattern = patternFor[c.pattern];
                }
        const auto firstCopy = (int) song.playlist.size();
        song.playlist.insert (song.playlist.end(), copies.begin(), copies.end());
        if (info.mode == PlaylistTrackMode::audio)
        {
            std::vector<int> touched;
            for (int i = firstCopy; i < (int) song.playlist.size(); ++i)
                if (song.playlist[(std::size_t) i].kind == ClipKind::audio)
                    touched.push_back (i);
            if (! touched.empty())
                trackmodes::followAudioTracks (song, touched, true);
        }
        ++at;
    }
    return firstClone;
}

int clipsOfChannel (const Song& song, int channel)
{
    int n = 0;
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        n += (int) std::count_if (clips.begin(), clips.end(), [channel] (const Clip& c) { return c.kind == ClipKind::audio && c.channel == channel; });
    });
    return n;
}

bool isVariant (const Clip& clip) noexcept
{
    return clip.kind == ClipKind::audio && (clip.pitch != 0.0f || clip.stretch != 1.0 || clip.reversed);
}

void stretchAudioClip (Song& song, const std::vector<Clip>& before, int index, std::int64_t newStart, std::int64_t newLength,
                       bool createVariants)
{
    if (index < 0 || index >= (int) before.size() || index >= (int) song.playlist.size() || before[(std::size_t) index].kind != ClipKind::audio)
        return;
    const auto& original = before[(std::size_t) index];
    newLength = std::max<std::int64_t> (1, newLength);
    const auto ratio = (double) newLength / (double) std::max<std::int64_t> (1, original.length);
    auto scale = [ratio] (Clip& c, const Clip& from) {
        c.stretch = std::clamp (from.stretch * ratio, 0.01, 100.0);
        const auto offset = ((double) from.offset + (double) from.offsetFraction) * ratio;
        c.offset = (std::int64_t) std::floor (offset);
        c.offsetFraction = (float) (offset - (double) c.offset);
        c.fadeIn = std::min (from.fadeIn, c.length);
        c.fadeOut = std::min (from.fadeOut, c.length - c.fadeIn);
    };

    // FL (FUN_00e66eb0): a variant when Create variants is on and another clip plays the same sample, or when the clip
    // already is one; else the channel's own timing changes and every clip of it follows
    int sameChannel = 0;
    for (const auto& c : before)
        sameChannel += c.kind == ClipKind::audio && c.channel == original.channel ? 1 : 0;
    const auto variant = (createVariants && sameChannel >= 2) || isVariant (original);

    for (int i = 0; i < (int) before.size() && i < (int) song.playlist.size(); ++i)
    {
        const auto& from = before[(std::size_t) i];
        if (i != index && (variant || from.kind != ClipKind::audio || from.channel != original.channel))
            continue;
        auto& c = song.playlist[(std::size_t) i];
        if (i == index)
        {
            c.start = std::max<std::int64_t> (0, newStart);
            c.length = newLength;
        }
        else
        {
            c.start = from.start;
            c.length = std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) from.length * ratio));
        }
        scale (c, from);
    }
}
} // namespace lynx::pltracks
