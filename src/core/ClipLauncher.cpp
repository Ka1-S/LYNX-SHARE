#include "core/ClipLauncher.h"

#include <algorithm>
#include <cmath>

namespace lynx::launcher
{
// --- the model ---------------------------------------------------------------------------------------------------
ClipGrid& ensureGrid (Song& song)
{
    if (! song.clipGrid)
    {
        song.clipGrid.emplace();
        song.clipGrid->scenes.emplace_back();
    }
    return *song.clipGrid;
}

namespace
{
int clipIndexAt (const ClipGrid& grid, int track, int scene) noexcept
{
    for (int i = 0; i < (int) grid.clips.size(); ++i)
    {
        const auto& c = grid.clips[(std::size_t) i];
        if (c.track == track && c.length > 0 && c.gridSlot >= 0 && c.gridSlot < (int) grid.slots.size()
            && grid.slots[(std::size_t) c.gridSlot].scene == scene)
            return i;
    }
    return -1;
}

// The hidden entry of an empty slot without a stop button: a Clip of length 0, so that it follows its track too.
int stopMarkerAt (const ClipGrid& grid, int track, int scene) noexcept
{
    for (int i = 0; i < (int) grid.clips.size(); ++i)
    {
        const auto& c = grid.clips[(std::size_t) i];
        if (c.track == track && c.length <= 0 && c.gridSlot >= 0 && c.gridSlot < (int) grid.slots.size()
            && grid.slots[(std::size_t) c.gridSlot].scene == scene)
            return i;
    }
    return -1;
}

// A slot id no clip refers to, or a new one (−1 at the limit).
int freeSlot (ClipGrid& grid)
{
    std::vector<char> used (grid.slots.size(), 0);
    for (const auto& c : grid.clips)
        if (c.gridSlot >= 0 && c.gridSlot < (int) used.size())
            used[(std::size_t) c.gridSlot] = 1;
    for (int i = 0; i < (int) used.size(); ++i)
        if (used[(std::size_t) i] == 0)
            return i;
    if ((int) grid.slots.size() >= maxSlots)
        return -1;
    grid.slots.emplace_back();
    return (int) grid.slots.size() - 1;
}
} // namespace

const Clip* clipAt (const ClipGrid& grid, int track, int scene) noexcept
{
    const auto i = clipIndexAt (grid, track, scene);
    return i >= 0 ? &grid.clips[(std::size_t) i] : nullptr;
}

ClipGridSlot* settingsAt (ClipGrid& grid, int track, int scene) noexcept
{
    const auto i = clipIndexAt (grid, track, scene);
    return i >= 0 ? &grid.slots[(std::size_t) grid.clips[(std::size_t) i].gridSlot] : nullptr;
}

int setClip (Song& song, int track, int scene, Clip clip)
{
    if (track < 0 || track >= maxTracks || scene < 0 || scene >= maxScenes || clip.length <= 0
        || (clip.kind != ClipKind::pattern && clip.kind != ClipKind::audio))
        return -1;
    auto& grid = ensureGrid (song);
    clearClip (song, track, scene);
    const auto id = freeSlot (grid);
    if (id < 0)
        return -1;
    while ((int) grid.scenes.size() <= scene)
        grid.scenes.emplace_back();
    grid.slots[(std::size_t) id] = ClipGridSlot {};
    grid.slots[(std::size_t) id].scene = scene;
    clip.track = track;
    clip.start = 0;
    clip.crossfadeIn = clip.crossfadeOut = 0;
    clip.gridSlot = (std::int16_t) id;
    grid.clips.push_back (clip);
    return id;
}

bool clearClip (Song& song, int track, int scene)
{
    if (! song.clipGrid)
        return false;
    auto& grid = *song.clipGrid;
    const auto i = clipIndexAt (grid, track, scene);
    if (i < 0)
        return false;
    grid.clips.erase (grid.clips.begin() + i);
    return true;
}

bool hasStopButton (const ClipGrid& grid, int track, int scene) noexcept
{
    return stopMarkerAt (grid, track, scene) < 0;
}

void setStopButton (Song& song, int track, int scene, bool on)
{
    if (track < 0 || track >= maxTracks || scene < 0 || scene >= maxScenes)
        return;
    auto& grid = ensureGrid (song);
    const auto marker = stopMarkerAt (grid, track, scene);
    if (on)
    {
        if (marker >= 0)
            grid.clips.erase (grid.clips.begin() + marker);
        return;
    }
    if (marker >= 0 || clipIndexAt (grid, track, scene) >= 0)
        return; // (a clip's slot has no stop button to remove)
    const auto id = freeSlot (grid);
    if (id < 0)
        return;
    grid.slots[(std::size_t) id] = ClipGridSlot {};
    grid.slots[(std::size_t) id].scene = scene;
    grid.slots[(std::size_t) id].noStop = true;
    Clip clip;
    clip.pattern = -1;
    clip.track = track;
    clip.length = 0;
    clip.gridSlot = (std::int16_t) id;
    grid.clips.push_back (clip);
}

int followGroup (const ClipGrid& grid, int track, int scene, int* out, int capacity) noexcept
{
    if (clipIndexAt (grid, track, scene) < 0)
        return 0;
    auto first = scene;
    while (first > 0 && clipIndexAt (grid, track, first - 1) >= 0)
        --first;
    int n = 0;
    for (int r = first; n < capacity && r < (int) grid.scenes.size() && clipIndexAt (grid, track, r) >= 0; ++r)
        out[n++] = r;
    return n;
}

FollowOutcome followTarget (const ClipGrid& grid, int track, int scene, FollowKind kind, int jump, std::uint32_t random) noexcept
{
    FollowOutcome outcome;
    std::array<int, 256> rows {};
    const auto n = followGroup (grid, track, scene, rows.data(), (int) rows.size());
    if (n <= 0)
        return outcome;
    auto here = 0;
    while (here < n && rows[(std::size_t) here] != scene)
        ++here;
    switch (kind)
    {
        case FollowKind::none: return outcome;
        case FollowKind::stop: outcome.stops = true; return outcome;
        case FollowKind::playAgain: outcome.scene = scene; return outcome;
        case FollowKind::previous: outcome.scene = rows[(std::size_t) ((here + n - 1) % n)]; return outcome;
        case FollowKind::next: outcome.scene = rows[(std::size_t) ((here + 1) % n)]; return outcome;
        case FollowKind::first: outcome.scene = rows[0]; return outcome;
        case FollowKind::last: outcome.scene = rows[(std::size_t) (n - 1)]; return outcome;
        case FollowKind::any: outcome.scene = rows[(std::size_t) (random % (std::uint32_t) n)]; return outcome;
        case FollowKind::other:
            if (n == 1)
                outcome.scene = scene;
            else
            {
                auto pick = (int) (random % (std::uint32_t) (n - 1));
                if (pick >= here)
                    ++pick;
                outcome.scene = rows[(std::size_t) pick];
            }
            return outcome;
        case FollowKind::jump:
            if (clipIndexAt (grid, track, jump) >= 0)
                outcome.scene = jump;
            return outcome;
    }
    return outcome;
}

bool recordedClip (const ClipGrid& grid, const RecordedPass& pass, Clip& out)
{
    for (const auto& c : grid.clips)
        if (c.gridSlot == pass.slot && c.length > 0 && pass.length >= 1.0)
        {
            out = c;
            out.gridSlot = -1;
            out.uid = 0;
            out.start = std::max<std::int64_t> (0, (std::int64_t) std::llround (pass.songTick));
            out.length = std::max<std::int64_t> (1, (std::int64_t) std::llround (pass.length));
            out.offset = c.offset + (std::int64_t) std::llround (pass.offset);
            return true;
        }
    return false;
}

int insertScene (Song& song, int at)
{
    auto& grid = ensureGrid (song);
    at = std::clamp (at, 0, (int) grid.scenes.size());
    if ((int) grid.scenes.size() >= maxScenes)
        return -1;
    grid.scenes.insert (grid.scenes.begin() + at, ClipGridScene {});
    for (const auto& c : grid.clips)
        if (c.gridSlot >= 0 && c.gridSlot < (int) grid.slots.size())
            if (auto& s = grid.slots[(std::size_t) c.gridSlot]; s.scene >= at)
                ++s.scene;
    return at;
}

bool removeScene (Song& song, int scene)
{
    if (! song.clipGrid || scene < 0 || scene >= (int) song.clipGrid->scenes.size() || song.clipGrid->scenes.size() <= 1)
        return false;
    auto& grid = *song.clipGrid;
    grid.clips.erase (std::remove_if (grid.clips.begin(), grid.clips.end(),
                                      [&] (const Clip& c) {
                                          return c.gridSlot >= 0 && c.gridSlot < (int) grid.slots.size()
                                                 && grid.slots[(std::size_t) c.gridSlot].scene == scene;
                                      }),
                      grid.clips.end());
    grid.scenes.erase (grid.scenes.begin() + scene);
    for (const auto& c : grid.clips)
        if (c.gridSlot >= 0 && c.gridSlot < (int) grid.slots.size())
            if (auto& s = grid.slots[(std::size_t) c.gridSlot]; s.scene > scene)
                --s.scene;
    return true;
}

double quantTicks (LaunchQuant quant, int beatsPerBar) noexcept
{
    const auto beat = (double) ppq, bar = beat * std::max (1, beatsPerBar);
    switch (quant)
    {
        case LaunchQuant::global:
        case LaunchQuant::none: return 0.0;
        case LaunchQuant::bars8: return 8.0 * bar;
        case LaunchQuant::bars4: return 4.0 * bar;
        case LaunchQuant::bars2: return 2.0 * bar;
        case LaunchQuant::bar: return bar;
        case LaunchQuant::half: return 2.0 * beat;
        case LaunchQuant::halfTriplet: return 4.0 * beat / 3.0;
        case LaunchQuant::quarter: return beat;
        case LaunchQuant::quarterTriplet: return 2.0 * beat / 3.0;
        case LaunchQuant::eighth: return beat / 2.0;
        case LaunchQuant::eighthTriplet: return beat / 3.0;
        case LaunchQuant::sixteenth: return beat / 4.0;
        case LaunchQuant::sixteenthTriplet: return beat / 6.0;
        case LaunchQuant::thirtySecond: return beat / 8.0;
    }
    return 0.0;
}

GridIndex makeIndex (const ClipGrid& grid)
{
    GridIndex index (grid.slots.size(), -1);
    for (int i = 0; i < (int) grid.clips.size(); ++i)
        if (const auto s = grid.clips[(std::size_t) i].gridSlot; s >= 0 && s < (int) index.size())
            index[(std::size_t) s] = i;
    return index;
}

// --- the player --------------------------------------------------------------------------------------------------
namespace
{
const Clip* clipOfSlot (const ClipGrid& grid, const GridIndex& index, int slot) noexcept
{
    if (slot < 0 || slot >= (int) index.size() || index[(std::size_t) slot] < 0)
        return nullptr;
    return &grid.clips[(std::size_t) index[(std::size_t) slot]];
}

double slotQuant (const Song& song, const ClipGrid& grid, int slot) noexcept
{
    auto q = slot >= 0 && slot < (int) grid.slots.size() ? grid.slots[(std::size_t) slot].quant : LaunchQuant::global;
    if (q == LaunchQuant::global)
        q = grid.globalQuant;
    return quantTicks (q, song.beatsPerBar);
}

double nextGrid (double now, double step) noexcept
{
    return step > 0.0 ? std::ceil (now / step - 1.0e-9) * step : std::ceil (now - 1.0e-6);
}
} // namespace

Player::TrackState* Player::stateOf (int track, bool create) noexcept
{
    for (int i = 0; i < numActive; ++i)
        if (active[(std::size_t) i].track == track)
            return &active[(std::size_t) i];
    if (! create || numActive >= maxActive)
        return nullptr;
    auto& t = active[(std::size_t) numActive++];
    t = TrackState {};
    t.track = track;
    return &t;
}

int Player::playingOn (int track) const noexcept
{
    for (int i = 0; i < numActive; ++i)
        if (active[(std::size_t) i].track == track)
            return active[(std::size_t) i].current.slot;
    return -1;
}

void Player::launch (const Song& song, const ClipGrid& grid, const GridIndex& index, TrackState& t, int slot, double now) noexcept
{
    const auto* clip = clipOfSlot (grid, index, slot);
    if (clip == nullptr)
        return;
    const auto& settings = grid.slots[(std::size_t) slot];
    const auto from = nextGrid (now, slotQuant (song, grid, slot));
    const auto length = (double) std::max<std::int64_t> (1, clip->length);

    // legato: the position the clip it replaces will have reached
    const auto& replaced = t.current;
    const auto legato = settings.legato && replaced.slot >= 0 && replaced.until > from;
    const auto anchor = legato ? replaced.anchor : from;

    auto until = never;
    if (! settings.loop) // one pass, from where it starts
        until = anchor + (std::floor ((from - anchor) / length + 1.0e-9) + 1.0) * length;
    t.next = Play {};
    t.next.slot = slot;
    t.next.anchor = anchor;
    t.next.from = from;
    t.next.until = until;
    scheduleFollow (grid, index, t.next);
}

void Player::scheduleFollow (const ClipGrid& grid, const GridIndex& index, Play& p) noexcept
{
    p.followAt = never;
    if (! grid.followActions || p.slot < 0 || p.slot >= (int) grid.slots.size())
        return;
    const auto& follow = grid.slots[(std::size_t) p.slot].follow;
    if (! follow.enabled)
        return;
    const auto* clip = clipOfSlot (grid, index, p.slot);
    if (clip == nullptr)
        return;
    p.followAt = p.from + (follow.linked ? (double) std::max (1, follow.iterations) * (double) std::max<std::int64_t> (1, clip->length)
                                         : (double) std::max<std::int64_t> (1, follow.ticks));
}

std::uint32_t Player::nextRandom() noexcept
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

// The time has come: one of the clip's two actions, by their chances. A clip launched by it waits for its own launch
// quantisation only; the global one does not apply ([MANUAL] 16.7).
void Player::follow (const Song& song, const ClipGrid& grid, const GridIndex& index, TrackState& t, double at) noexcept
{
    t.current.followAt = never;
    const auto& f = grid.slots[(std::size_t) t.current.slot].follow;
    const auto a = std::max (0, f.chanceA), b = std::max (0, f.chanceB);
    if (a + b <= 0)
        return;
    const auto useA = (int) (nextRandom() % (std::uint32_t) (a + b)) < a;
    const auto outcome = followTarget (grid, t.track, grid.slots[(std::size_t) t.current.slot].scene, useA ? f.a : f.b, useA ? f.jumpA : f.jumpB, nextRandom());
    if (outcome.stops)
    {
        t.next = Play {};
        t.current.until = std::min (t.current.until, at);
        return;
    }
    if (outcome.scene < 0)
        return;
    const auto* target = clipAt (grid, t.track, outcome.scene);
    if (target == nullptr)
        return;
    const auto slot = (int) target->gridSlot;
    const auto own = grid.slots[(std::size_t) slot].quant;
    const auto from = own == LaunchQuant::global ? at : nextGrid (at, quantTicks (own, song.beatsPerBar));
    const auto length = (double) std::max<std::int64_t> (1, target->length);
    t.next = Play {};
    t.next.slot = slot;
    t.next.anchor = grid.slots[(std::size_t) slot].legato ? t.current.anchor : from;
    t.next.from = from;
    if (! grid.slots[(std::size_t) slot].loop)
        t.next.until = t.next.anchor + (std::floor ((from - t.next.anchor) / length + 1.0e-9) + 1.0) * length;
    scheduleFollow (grid, index, t.next);
}

void Player::recordPartial (const ClipGrid& grid, const GridIndex& index, const Play& p, double at, Runtime* runtime) noexcept
{
    if (runtime == nullptr || ! p.recorded || p.slot < 0 || at <= p.from || p.until <= at - 1.0e-6)
        return; // (a play that ended already was recorded by its passes)
    const auto* clip = clipOfSlot (grid, index, p.slot);
    if (clip == nullptr)
        return;
    const auto length = (double) std::max<std::int64_t> (1, clip->length);
    const auto ws = p.anchor + std::floor ((at - 1.0e-6 - p.anchor) / length) * length;
    const auto vs = std::max (ws, p.from);
    if (at > vs + 0.5)
        runtime->recorded.push ({ p.slot, p.songFrom, at - vs, vs - ws });
}

void Player::stopTrack (const Song& song, const ClipGrid& grid, TrackState& t, double now) noexcept
{
    t.next = Play {};
    if (t.current.slot < 0)
        return;
    const auto at = nextGrid (now, quantTicks (grid.globalQuant, song.beatsPerBar));
    t.current.until = std::min (t.current.until, std::max (at, t.current.from));
    t.current.stopping = true;
}

void Player::apply (const Song& song, const ClipGrid& grid, const GridIndex& index, const Command& c, double now) noexcept
{
    switch (c.type)
    {
        case Command::Type::launch:
            if (c.track < 0 || c.track >= maxTracks)
                return;
            if (const auto* clip = clipAt (grid, c.track, c.scene))
                if (auto* t = stateOf (c.track, true))
                {
                    const auto slot = (int) clip->gridSlot;
                    const auto mode = grid.slots[(std::size_t) slot].mode;
                    // toggle: the next press stops the clip
                    if (mode == LaunchMode::toggle && t->current.slot == slot && t->current.until == never && t->next.slot < 0)
                    {
                        stopTrack (song, grid, *t, now);
                        return;
                    }
                    t->heldSlot = mode == LaunchMode::gate || mode == LaunchMode::repeat ? slot : -1;
                    t->repeatEvery = mode == LaunchMode::repeat ? slotQuant (song, grid, slot) : 0.0;
                    launch (song, grid, index, *t, slot, now);
                }
            return;
        case Command::Type::release:
            if (auto* t = stateOf (c.track, false); t != nullptr && t->heldSlot >= 0)
            {
                const auto* clip = clipAt (grid, c.track, c.scene);
                if (clip == nullptr || clip->gridSlot != t->heldSlot)
                    return;
                const auto gate = grid.slots[(std::size_t) t->heldSlot].mode == LaunchMode::gate;
                t->heldSlot = -1;
                t->repeatEvery = 0.0;
                if (gate) // up stops the clip
                    stopTrack (song, grid, *t, now);
            }
            return;
        case Command::Type::launchScene:
            // a track with no clip in the scene stops, if its slot has a stop button
            for (int i = 0; i < numActive; ++i)
                if (const auto track = active[(std::size_t) i].track;
                    clipAt (grid, track, c.scene) == nullptr && hasStopButton (grid, track, c.scene))
                    stopTrack (song, grid, active[(std::size_t) i], now);
            for (const auto& clip : grid.clips)
                if (clip.gridSlot >= 0 && clip.gridSlot < (int) grid.slots.size() && grid.slots[(std::size_t) clip.gridSlot].scene == c.scene
                    && clip.track >= 0 && clip.track < maxTracks)
                    if (auto* t = stateOf (clip.track, true))
                        launch (song, grid, index, *t, clip.gridSlot, now);
            return;
        case Command::Type::stopTrack:
            if (auto* t = stateOf (c.track, false))
                stopTrack (song, grid, *t, now);
            return;
        case Command::Type::stopAll:
            for (int i = 0; i < numActive; ++i)
                stopTrack (song, grid, active[(std::size_t) i], now);
            return;
        case Command::Type::backToArrangement:
            // at once: the grid's clips end here and the Playlist resumes in this block
            for (int i = 0; i < numActive; ++i)
            {
                auto& t = active[(std::size_t) i];
                t.next = Play {};
                if (t.current.slot >= 0)
                    t.current.until = std::min (t.current.until, std::max (std::ceil (now - 1.0e-6), t.current.from));
            }
            backTracks = takenTracks;
            takenTracks.reset();
            return;
        case Command::Type::reset: // another project
            numActive = 0;
            takenTracks.reset();
            backTracks.reset();
            return;
    }
}

void Player::addCut (const ClipGrid& grid, const GridIndex& index, const Play& p, double at) noexcept
{
    const auto* clip = clipOfSlot (grid, index, p.slot);
    if (clip == nullptr || numCuts >= maxCuts)
        return;
    const auto length = (double) std::max<std::int64_t> (1, clip->length);
    cuts[(std::size_t) numCuts++] = Cut { p.slot, std::fmod (std::max (0.0, at - p.anchor), length) + (double) clip->offset, at };
}

void Player::recordStop (const ClipGrid& grid, const GridIndex& index, Runtime* runtime) noexcept
{
    for (int i = 0; i < numActive; ++i)
        recordPartial (grid, index, active[(std::size_t) i].current, lastEnd, runtime);
}

void Player::stopAll (Runtime* runtime, bool giveBack) noexcept
{
    numActive = numVirtual = numCuts = numTakeovers = 0;
    backTracks.reset();
    if (giveBack)
        takenTracks.reset();
    if (runtime == nullptr)
        return;
    for (auto& v : runtime->tracks)
    {
        v.playing.store (-1, std::memory_order_relaxed);
        v.pending.store (-1, std::memory_order_relaxed);
        v.stopping.store (false, std::memory_order_relaxed);
        if (giveBack)
            v.taken.store (false, std::memory_order_relaxed);
    }
    runtime->anyPlaying.store (false, std::memory_order_relaxed);
}

void Player::process (const Song& song, const ClipGrid& grid, const GridIndex& index, Runtime* runtime, double clock,
                      double blockTicks, double songTick) noexcept
{
    numVirtual = numCuts = numTakeovers = 0;
    backTracks.reset();
    const auto end = clock + blockTicks;
    lastEnd = end;
    const auto recording = runtime != nullptr && runtime->recording.load (std::memory_order_relaxed);
    const auto songAt = [&] (double c) { return songTick + (c - clock); };

    if (runtime != nullptr)
    {
        Command c;
        while (runtime->commands.pop (c))
            apply (song, grid, index, c, clock);
    }

    for (int i = 0; i < numActive; ++i)
    {
        auto& t = active[(std::size_t) i];
        for (int guard = 0; guard < 8; ++guard)
        {
            const auto pending = t.next.slot >= 0 ? t.next.from : never;
            const auto ev = std::min ({ pending, t.current.until, t.current.followAt });
            if (ev >= end)
                break;
            if (ev == t.current.followAt && ev < pending && ev < t.current.until)
            {
                follow (song, grid, index, t, ev);
                continue;
            }
            if (ev == pending)
            {
                if (t.current.slot >= 0)
                {
                    t.current.until = std::min (t.current.until, ev);
                    addCut (grid, index, t.current, ev);
                }
                t.previous = t.current;
                t.current = t.next;
                t.next = Play {};
                t.current.recorded = recording;
                // repeat: while held, it starts over at the quantisation rate
                if (t.repeatEvery > 0.0 && t.heldSlot == t.current.slot)
                {
                    t.next = t.current;
                    t.next.anchor = t.next.from = t.current.from + t.repeatEvery;
                    t.next.recorded = false;
                    scheduleFollow (grid, index, t.next);
                }
                if (t.track >= 0 && t.track < maxTracks && ! takenTracks[(std::size_t) t.track])
                {
                    takenTracks.set ((std::size_t) t.track);
                    if (numTakeovers < (int) takeovers.size())
                        takeovers[(std::size_t) numTakeovers++] = Takeover { t.track, ev };
                }
            }
            else
            {
                addCut (grid, index, t.current, ev);
                t.previous = t.current;
                t.current = Play {};
            }
        }
    }

    // this block's passes: each play's loop iterations that touch the block (the previous play's last one too, for its
    // ends on the block's first sample)
    auto addPlay = [&] (Play& p) {
        if (p.slot < 0 || p.until < clock - 1.0 || p.from >= end)
            return;
        const auto* clip = clipOfSlot (grid, index, p.slot);
        if (clip == nullptr || clip->muted || song.isTrackMuted (clip->track))
            return;
        const auto length = (double) std::max<std::int64_t> (1, clip->length);
        auto k = std::floor ((std::max (p.from, clock - 1.0) - p.anchor) / length);
        for (int guard = 0; guard < 4 && numVirtual < maxVirtual; ++guard, k += 1.0)
        {
            const auto ws = p.anchor + k * length, we = ws + length;
            if (ws >= end)
                break;
            const auto vs = std::max (ws, p.from), ve = std::min (we, p.until);
            if (ve <= vs || ve < clock - 1.0)
                continue;
            // recording: a pass becomes a Playlist clip once it ends (the song position where it began)
            if (p.recorded && vs >= clock - 1.0e-6 && vs < end)
                p.songFrom = songAt (vs);
            if (p.recorded && recording && ve >= clock && ve < end && ve > vs)
                runtime->recorded.push ({ p.slot, p.songFrom, ve - vs, vs - ws });
            auto& v = virtuals[(std::size_t) numVirtual++];
            v.clip = *clip;
            v.slot = p.slot;
            v.clip.start = (std::int64_t) std::llround (vs);
            v.clip.length = std::max<std::int64_t> (1, (std::int64_t) std::llround (ve) - v.clip.start);
            v.clip.offset = clip->offset + (std::int64_t) std::llround (vs - ws);
            if (clip->kind == ClipKind::audio)
            {
                // the fades belong to the clip's own start / end only
                if (vs > ws + 0.5)
                    v.clip.fadeIn = 0;
                if (ve < we - 0.5)
                    v.clip.fadeOut = 0;
            }
        }
    };

    auto any = false;
    for (int i = 0; i < numActive;)
    {
        auto& t = active[(std::size_t) i];
        addPlay (t.previous);
        addPlay (t.current);
        if (runtime != nullptr && t.track >= 0 && t.track < maxTracks)
        {
            auto& view = runtime->tracks[(std::size_t) t.track];
            const auto sceneOf = [&] (int slot) { return slot >= 0 && slot < (int) grid.slots.size() ? grid.slots[(std::size_t) slot].scene : -1; };
            const auto playingNow = t.current.slot >= 0 && t.current.from <= end ? t.current.slot : -1;
            view.playing.store (sceneOf (playingNow), std::memory_order_relaxed);
            view.pending.store (sceneOf (t.next.slot), std::memory_order_relaxed);
            view.stopping.store (playingNow >= 0 && t.current.stopping && t.next.slot < 0, std::memory_order_relaxed);
            if (const auto* clip = clipOfSlot (grid, index, playingNow))
            {
                const auto length = (double) std::max<std::int64_t> (1, clip->length);
                view.progress.store ((float) (std::fmod (std::max (0.0, clock - t.current.anchor), length) / length), std::memory_order_relaxed);
            }
        }
        if (t.current.slot < 0 && t.next.slot < 0 && (t.previous.slot < 0 || t.previous.until < clock - 1.0))
        {
            if (runtime != nullptr && t.track >= 0 && t.track < maxTracks)
            {
                runtime->tracks[(std::size_t) t.track].playing.store (-1, std::memory_order_relaxed);
                runtime->tracks[(std::size_t) t.track].pending.store (-1, std::memory_order_relaxed);
                runtime->tracks[(std::size_t) t.track].stopping.store (false, std::memory_order_relaxed);
            }
            active[(std::size_t) i] = active[(std::size_t) --numActive];
            continue;
        }
        any = true;
        ++i;
    }
    if (runtime != nullptr)
    {
        runtime->anyPlaying.store (any, std::memory_order_relaxed);
        for (int i = 0; i < numTakeovers; ++i)
            runtime->tracks[(std::size_t) takeovers[(std::size_t) i].track].taken.store (true, std::memory_order_relaxed);
        if (backTracks.any())
            for (int t = 0; t < maxTracks; ++t)
                if (backTracks[(std::size_t) t])
                    runtime->tracks[(std::size_t) t].taken.store (false, std::memory_order_relaxed);
    }
}

void schedulePasses (const Song& song, const Player& player, double clock, int numSamples, double tps, EventBuffer& out,
                     const ScheduleIndex* index) noexcept
{
    if (player.virtualCount() == 0)
        return;
    for (int i = 0; i < player.virtualCount(); ++i)
    {
        const auto& v = player.virtualClip (i);
        scheduleSingleClip (song, v.clip, slotKeyBase + (std::size_t) v.slot, clock, numSamples, tps, out, index);
    }
    out.sortByOffset();
}

namespace
{
// The notes of `clip`'s pattern sounding at `position` (ticks into its source, its offset included) end `offsetTicks`
// into the block, before anything that starts there: a note on the same key must survive. A clip reaching past its
// pattern loops it (core/Scheduler.cpp), so a note may have started in an earlier pass and be sounding across the
// pattern's wrap, or for longer than the pattern: what counts is the time since the note's last start. Swing and a
// channel's shift move a note's end, hence the slack: a note that has ended already is not in `notes`.
void cutPatternNotes (const Song& song, const Clip& clip, double position, double offsetTicks, int sample, const ScheduleIndex* index,
                      EventBuffer& out, ScheduledNotes& notes) noexcept
{
    if (clip.pattern < 0 || clip.pattern >= (int) song.patterns.size())
        return;
    const auto& p = song.patterns[(std::size_t) clip.pattern];
    const auto period = std::max<std::int64_t> (1, index != nullptr && (std::size_t) clip.pattern < index->patternLength.size()
                                                       ? index->patternLength[(std::size_t) clip.pattern]
                                                       : p.effectiveLength (song.beatsPerBar));
    const auto loops = stretchedClipsLoop() && clip.offset + clip.length > period;
    const auto at = std::max (0.0, position);
    const auto slack = (double) ticksPerStep + (index != nullptr ? (double) index->maxShift : 0.0);
    for (const auto& n : p.notes)
    {
        const auto start = (double) n.start;
        if (! loops && start >= at)
            break; // (a rack state's notes are sorted by start)
        if (n.muted || (loops && n.start >= period))
            continue;
        auto since = at - start;
        if (loops)
        {
            since = std::fmod (since, (double) period);
            if (since <= 0.0)
                since += (double) period; // (0: it starts just now; the pass before's is the one that may be sounding)
            if (since > at)
                continue; // no pass has played it yet
        }
        if (since >= (double) n.length + slack)
            continue;
        if (notes.shorten (n.channel, n.key, offsetTicks))
        {
            NoteEvent off;
            off.type = NoteEvent::Type::off;
            off.channel = n.channel;
            off.key = n.key;
            off.velocity = 0.0f;
            off.sampleOffset = sample;
            out.add (off);
        }
    }
}

// A track was just taken `offsetTicks` into the block: what its Playlist clips were sounding ends there.
void cutPlaylistTrack (const Song& song, int track, double songTick, double offsetTicks, int sample, const ScheduleIndex* index,
                       EventBuffer& out, ScheduledNotes& notes) noexcept
{
    const auto loop = (double) std::max<std::int64_t> (1, index != nullptr ? index->songLength : song.songLength());
    const auto at = std::fmod (std::max (0.0, songTick), loop);
    for (std::size_t i = 0; i < song.playlist.size(); ++i)
    {
        const auto& clip = song.playlist[i];
        if (clip.track != track || clip.muted || (double) clip.start > at || (double) clip.end() <= at)
            continue;
        if (clip.kind == ClipKind::audio && clip.channel >= 0)
        {
            NoteEvent off;
            off.type = NoteEvent::Type::off;
            off.channel = clip.channel;
            off.key = audioClipKey (i);
            off.sampleOffset = sample;
            out.add (off);
        }
        else if (clip.kind == ClipKind::pattern)
            cutPatternNotes (song, clip, at - (double) clip.start + (double) clip.offset, offsetTicks, sample, index, out, notes);
    }
}
} // namespace

void playBlock (Runtime& runtime, const Song& song, const ClipGrid& grid, const GridIndex& gridIndex, double startTick, int numSamples,
                double tps, EventBuffer& out, bool resume, const ScheduleIndex* index, bool wrap, bool chase, ScheduledNotes& notes) noexcept
{
    if (numSamples <= 0 || tps <= 0.0)
        return;
    auto& player = runtime.player;
    const auto clock = runtime.clock;
    player.process (song, grid, gridIndex, &runtime, clock, numSamples * tps, startTick);

    const auto sampleOf = [&] (double at) { return std::clamp ((int) std::llround ((at - clock) / tps), 0, numSamples - 1); };
    // the ends first: on a shared sample, the note that ends must not take the one that starts with it
    for (int i = 0; i < player.takeoverCount(); ++i)
    {
        const auto at = player.takeoverAt (i);
        cutPlaylistTrack (song, player.takeoverTrack (i), startTick + (at - clock), at - clock, sampleOf (at), index, out, notes);
    }
    for (int i = 0; i < player.cutCount(); ++i)
    {
        const auto& cut = player.cut (i);
        if (const auto* clip = clipOfSlot (grid, gridIndex, cut.slot); clip != nullptr && clip->kind == ClipKind::pattern)
            cutPatternNotes (song, *clip, cut.position, cut.at - clock, sampleOf (cut.at), index, out, notes);
    }
    scheduleSongAround (song, player.taken(), player.givenBack(), startTick, numSamples, tps, out, resume, index, wrap, chase);
    schedulePasses (song, player, clock, numSamples, tps, out, index);
    out.sortByOffset();
    runtime.clock = clock + numSamples * tps;
}
} // namespace lynx::launcher
