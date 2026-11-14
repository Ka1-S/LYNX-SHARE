#include "core/ClipLauncher.h"
#include "core/RemoteMap.h"
#include "core/Groove.h"

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
    // (round 190fm) a Playlist clip's groove goes with it: on the grid a clip's groove is its slot's
    if (clip.kind == ClipKind::pattern)
        grid.slots[(std::size_t) id].groove = clip.groove;
    clip.groove = 0;
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
    remotemap::scenesInserted (song, at); // (round 190ef: a mapping belongs to its slot / scene and moves with it)
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
    remotemap::sceneRemoved (song, scene); // (round 190ef)
    return true;
}

double quantTicks (LaunchQuant quant, Meter meter) noexcept
{
    const auto beat = (double) ppq, bar = (double) meter.bar(); // (beat: a quarter note; Live's 1/4 is a quarter in any meter)
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
    return quantTicks (q, song.meter());
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

int Player::recordingOn (int track) const noexcept
{
    for (int i = 0; i < numActive; ++i)
        if (active[(std::size_t) i].track == track)
            return active[(std::size_t) i].recScene;
    return -1;
}

// The end of a track's recording becomes known: the UI is told once (it makes the clip), again if the end moves earlier.
void Player::tellTake (TrackState& t, Runtime* runtime) noexcept
{
    if (runtime != nullptr && t.recScene >= 0)
        runtime->takes.push ({ t.track, t.recScene, t.recFrom, std::max (t.recFrom, t.recUntil), t.recThenPlay });
    t.recTold = true;
}

void Player::tellCancelled (Runtime* runtime, int track, int scene) noexcept
{
    if (runtime != nullptr && scene >= 0)
        runtime->takes.push ({ track, scene, 0.0, 0.0, false });
}

// The slot queued to record after the current recording is given up (a stop, a launch, another project).
void Player::dropNextRecording (TrackState& t, Runtime* runtime) noexcept
{
    tellCancelled (runtime, t.track, t.recNextScene);
    t.recNextScene = -1;
    t.recNextLength = 0.0;
}

void Player::endRecording (const Song& song, const ClipGrid& grid, TrackState& t, double now, bool thenPlay, Runtime* runtime) noexcept
{
    if (t.recScene < 0)
        return;
    const auto quant = quantTicks (grid.globalQuant, song.meter());
    if (! t.recStarted) // not begun yet (or asked for while the transport stood still): nothing was recorded
    {
        t.recUntil = t.recFrom;
        t.recThenPlay = false;
        tellTake (t, runtime);
        t.recScene = -1;
        t.recFrom = t.recUntil = never;
        t.recTold = false;
        return;
    }
    auto until = nextGrid (std::max (now, t.recFrom), quant);
    if (until < t.recFrom + (quant > 0.0 ? quant : 1.0) - 1.0e-9)
        until = t.recFrom + (quant > 0.0 ? quant : 1.0);
    if (until >= t.recUntil && t.recTold)
        return; // (a fixed length, or an end already asked for, that comes first)
    t.recUntil = std::min (t.recUntil, until);
    t.recThenPlay = thenPlay;
    tellTake (t, runtime);
}

void Player::launch (const Song& song, const ClipGrid& grid, const GridIndex& index, TrackState& t, int slot, double now) noexcept
{
    const auto* clip = clipOfSlot (grid, index, slot);
    if (clip == nullptr)
        return;
    const auto& settings = grid.slots[(std::size_t) slot];
    const auto from = nextGrid (now, slotQuant (song, grid, slot));
    // a clip launched on a track that records ends the recording where it starts (round 190bv)
    dropNextRecording (t, commandRuntime);
    if (t.recScene >= 0 && (t.recUntil == never || t.recUntil > from))
    {
        if (! t.recStarted && from <= t.recFrom)
            endRecording (song, grid, t, t.recFrom - 1.0, false, commandRuntime);
        else
        {
            t.recUntil = std::max (from, t.recFrom);
            t.recThenPlay = false;
            tellTake (t, commandRuntime);
        }
    }
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
    const auto from = own == LaunchQuant::global ? at : nextGrid (at, quantTicks (own, song.meter()));
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
    t.wantScene = -1;
    dropNextRecording (t, commandRuntime);
    endRecording (song, grid, t, now, false, commandRuntime); // (a stop ends a recording on its grid point too)
    if (t.recScene >= 0 && t.recThenPlay) // (its end was asked for already, to play on: a stop now ends it there without that)
    {
        t.recThenPlay = false;
        tellTake (t, commandRuntime);
    }
    if (t.current.slot < 0)
        return;
    const auto at = nextGrid (now, quantTicks (grid.globalQuant, song.meter()));
    t.current.until = std::min (t.current.until, std::max (at, t.current.from));
    t.current.stopping = true;
}

void Player::apply (const Song& song, const ClipGrid& grid, const GridIndex& index, const Command& c, double now) noexcept
{
    switch (c.type)
    {
        case Command::Type::record:
        {
            if (c.track < 0 || c.track >= maxTracks || c.scene < 0)
                return;
            auto* t = clipAt (grid, c.track, c.scene) == nullptr ? stateOf (c.track, true) : nullptr;
            if (t != nullptr && t->recScene >= 0 && t->recScene != c.scene && t->recNextScene < 0)
            {
                // another record button on a track that records (round 190cy; Live 12.4.16, live check 190cw: the
                // first take ends and goes on playing, the clicked slot records the next take)
                if (! t->recStarted)
                    endRecording (song, grid, *t, now, false, commandRuntime); // (not begun: the wait moves to the new slot)
                else
                {
                    endRecording (song, grid, *t, now, true, commandRuntime);
                    t->recNextScene = c.scene;
                    t->recNextLength = std::max (0.0, c.at);
                    return;
                }
            }
            if (t == nullptr || t->recScene >= 0)
            {
                if (t == nullptr || (t->recScene != c.scene && t->recNextScene != c.scene))
                    tellCancelled (commandRuntime, c.track, c.scene); // (the engine forgets it)
                return;
            }
            {
                const auto from = nextGrid (now, quantTicks (grid.globalQuant, song.meter()));
                t->next = Play {};
                t->wantScene = -1;
                t->heldSlot = -1;
                t->repeatEvery = 0.0;
                if (t->current.slot >= 0)
                    t->current.until = std::min (t->current.until, std::max (from, t->current.from));
                t->recScene = c.scene;
                t->recFrom = from;
                t->recUntil = c.at > 0.0 ? from + c.at : never;
                t->recStarted = false;
                t->recThenPlay = c.at > 0.0; // a fixed length plays on
                t->recTold = false;
                if (c.at > 0.0)
                    tellTake (*t, commandRuntime);
            }
            return;
        }
        case Command::Type::recordEnd:
            if (auto* t = stateOf (c.track, false))
                endRecording (song, grid, *t, now, c.scene != 0, commandRuntime);
            return;
        case Command::Type::launch:
            if (c.track < 0 || c.track >= maxTracks)
                return;
            if (c.at >= 0.0) // the clip just recorded: in phase with its recording, as soon as it exists
            {
                if (auto* t = stateOf (c.track, true))
                {
                    t->wantScene = c.scene;
                    t->wantAnchor = c.at;
                    t->wantBlocks = 0;
                }
                return;
            }
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
            for (int i = 0; i < numActive; ++i)
                if (auto& t = active[(std::size_t) i]; t.recScene >= 0)
                {
                    dropNextRecording (t, commandRuntime);
                    t.recUntil = t.recFrom; // (nothing of it is kept)
                    t.recThenPlay = false;
                    tellTake (t, commandRuntime);
                }
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
    // a recording into a slot ends where the transport stopped (round 190bv): what was played up to there is the clip
    for (int i = 0; i < numActive; ++i)
        if (auto& t = active[(std::size_t) i]; t.recScene >= 0)
        {
            dropNextRecording (t, runtime);
            t.recUntil = t.recStarted ? std::min (t.recUntil, std::max (lastEnd, t.recFrom + 1.0)) : t.recFrom;
            t.recThenPlay = false;
            tellTake (t, runtime);
        }
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
        v.recording.store (-1, std::memory_order_relaxed);
        v.recordPending.store (-1, std::memory_order_relaxed);
        if (giveBack)
            v.taken.store (false, std::memory_order_relaxed);
    }
    runtime->anyPlaying.store (false, std::memory_order_relaxed);
    runtime->running.store (false, std::memory_order_relaxed);
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
        runtime->clockNow.store (clock, std::memory_order_relaxed);
        runtime->clockMinusSong.store (clock - songTick, std::memory_order_relaxed);
        commandRuntime = runtime;
        Command c;
        while (runtime->commands.pop (c))
            apply (song, grid, index, c, clock);
        commandRuntime = nullptr;
    }

    // recordings into slots (round 190bv) and the launches that wait for their recorded clip
    for (int i = 0; i < numActive; ++i)
    {
        auto& t = active[(std::size_t) i];
        if (t.wantScene >= 0)
        {
            if (const auto* clip = clipAt (grid, t.track, t.wantScene))
            {
                // position 0 at the anchor; it plays from the anchor, or from now when the clip arrived after it
                const auto slot = (int) clip->gridSlot;
                const auto from = std::max (t.wantAnchor, std::ceil (clock - 1.0e-6));
                const auto length = (double) std::max<std::int64_t> (1, clip->length);
                t.next = Play {};
                t.next.slot = slot;
                t.next.anchor = t.wantAnchor;
                t.next.from = from;
                if (! grid.slots[(std::size_t) slot].loop)
                    t.next.until = t.wantAnchor + (std::floor ((from - t.wantAnchor) / length + 1.0e-9) + 1.0) * length;
                scheduleFollow (grid, index, t.next);
                t.wantScene = -1;
            }
            else if (++t.wantBlocks > 4000) // (it never came: an undo, another project)
                t.wantScene = -1;
        }
        if (t.recScene < 0)
            continue;
        if (! t.recStarted && t.recFrom < end)
        {
            t.recStarted = true;
            // the track is the grid's from here on, as when a clip starts
            if (t.track >= 0 && t.track < maxTracks && ! takenTracks[(std::size_t) t.track])
            {
                takenTracks.set ((std::size_t) t.track);
                if (numTakeovers < (int) takeovers.size())
                    takeovers[(std::size_t) numTakeovers++] = Takeover { t.track, std::max (t.recFrom, clock) };
            }
        }
        if (t.recStarted && t.recUntil < end)
        {
            if (! t.recTold)
                tellTake (t, runtime);
            const auto ended = t.recUntil;
            t.recScene = -1;
            t.recFrom = t.recUntil = never;
            t.recStarted = t.recTold = false;
            // the slot queued by another record button records from here, while the take that ended plays on (round 190cy)
            if (t.recNextScene >= 0)
            {
                const auto scene = t.recNextScene;
                const auto length = t.recNextLength;
                t.recNextScene = -1;
                t.recNextLength = 0.0;
                if (clipAt (grid, t.track, scene) != nullptr)
                    tellCancelled (runtime, t.track, scene); // (a clip was put there meanwhile)
                else
                {
                    t.recScene = scene;
                    t.recFrom = ended;
                    t.recUntil = length > 0.0 ? ended + length : never;
                    t.recStarted = true;
                    t.recThenPlay = length > 0.0;
                    t.recTold = false;
                    if (length > 0.0)
                        tellTake (t, runtime);
                }
            }
        }
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
            view.recording.store (t.recStarted ? t.recScene : -1, std::memory_order_relaxed);
            view.recordPending.store (t.recStarted ? t.recNextScene : t.recScene, std::memory_order_relaxed);
            if (t.recScene >= 0)
                view.recordFrom.store (t.recFrom, std::memory_order_relaxed);
            if (playingNow >= 0)
                view.anchor.store (t.current.anchor, std::memory_order_relaxed);
            if (const auto* clip = clipOfSlot (grid, index, playingNow))
            {
                const auto length = (double) std::max<std::int64_t> (1, clip->length);
                view.progress.store ((float) (std::fmod (std::max (0.0, clock - t.current.anchor), length) / length), std::memory_order_relaxed);
            }
        }
        if (t.current.slot < 0 && t.next.slot < 0 && t.recScene < 0 && t.wantScene < 0 && (t.previous.slot < 0 || t.previous.until < clock - 1.0))
        {
            if (runtime != nullptr && t.track >= 0 && t.track < maxTracks)
            {
                runtime->tracks[(std::size_t) t.track].playing.store (-1, std::memory_order_relaxed);
                runtime->tracks[(std::size_t) t.track].pending.store (-1, std::memory_order_relaxed);
                runtime->tracks[(std::size_t) t.track].stopping.store (false, std::memory_order_relaxed);
                runtime->tracks[(std::size_t) t.track].recording.store (-1, std::memory_order_relaxed);
                runtime->tracks[(std::size_t) t.track].recordPending.store (-1, std::memory_order_relaxed);
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
                                                       : p.effectiveLength (song.meter()));
    const auto loops = stretchedClipsLoop() && clip.offset + clip.length > period;
    const auto at = std::max (0.0, position);
    auto slack = (double) ticksPerStep + (index != nullptr ? (double) index->maxShift : 0.0);
    if (const auto* g = groove::ofClip (song, clip)) // (a groove may have moved the note later: round 190bv)
        slack += (double) groove::maxShift (*g, song.grooves.amount);
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
    runtime.running.store (true, std::memory_order_relaxed);
}
} // namespace lynx::launcher
