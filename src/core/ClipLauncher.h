#pragma once

// The clip grid of the Clip-launch workflow (round 181a, slice N5): clips in a grid of Playlist tracks × scenes, launched
// live and in time. The model (ClipGrid, in Song.h) exists only once a project uses it, and the engine only then too:
// RackState::launchers is the one test the audio thread makes, in place of the one it made for Performance mode.
//
// The behaviour follows the published manual of the session-style launcher this workflow studies; docs/analise/REA_rodada181a.md
// says what was checked and how:
//   - a launch waits for the next multiple of the clip's launch quantisation (the grid's global one by default), counted
//     from the start of the song on the steady transport clock;
//   - a track plays one clip: the launched clip replaces the one playing, at that same point;
//   - legato: the launched clip starts at the position the clip it replaces has reached;
//   - a clip that starts on a track takes the track away from the Playlist's clips until "back to arrangement".
//
// Threads: the UI posts commands into Runtime::commands; the audio thread applies them at the start of a block, never
// allocates, and publishes what plays per track (Runtime::tracks).

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <limits>
#include <vector>

#include "core/LiveEventQueue.h"
#include "core/Scheduler.h"
#include "core/Song.h"

namespace lynx::launcher
{
constexpr int maxTracks = 500;  // the Playlist's tracks
constexpr int maxScenes = 1000;
constexpr int maxSlots = 32000; // Clip::gridSlot is 16 bits
// Audio clips: a grid clip's voice key sits above any real playlist index (audioClipKey wraps at 1000000)
constexpr std::size_t slotKeyBase = 900000;

// --- the model (message thread) ------------------------------------------------------------------------------------

// The song's grid, created when it has none (with one scene).
ClipGrid& ensureGrid (Song& song);
// The clip in (track, scene), or nullptr.
const Clip* clipAt (const ClipGrid& grid, int track, int scene) noexcept;
// Puts a clip in a slot, replacing what is there; its settings start at their defaults. `clip` keeps what it plays
// (kind, source, length, offset, gain…); pattern and audio clips only. Returns the slot's id, −1 when it cannot be placed.
int setClip (Song& song, int track, int scene, Clip clip);
// Empties a slot. False when it was empty.
bool clearClip (Song& song, int track, int scene);
// The settings of the clip in (track, scene), or nullptr.
ClipGridSlot* settingsAt (ClipGrid& grid, int track, int scene) noexcept;
// An empty slot's stop button (on by default): a scene launch stops the track only where its slot has one.
void setStopButton (Song& song, int track, int scene, bool on);
bool hasStopButton (const ClipGrid& grid, int track, int scene) noexcept;
// Scenes: `at` is the new scene's row; rows after it move down (with their clips).
int insertScene (Song& song, int at);
bool removeScene (Song& song, int scene); // with its clips (a grid keeps at least one scene)

// Ticks of a launch quantisation (0 = none; `global` is resolved by the caller).
double quantTicks (LaunchQuant quant, int beatsPerBar) noexcept;

// A row's clips form groups: runs of filled slots in a column, as follow actions see them. Writes the rows of the group
// holding `scene` on `track` to out[] (capacity rows), in order; returns how many, 0 for an empty slot.
int followGroup (const ClipGrid& grid, int track, int scene, int* out, int capacity) noexcept;
// What a follow action leads to from row `scene` (−1 = stop or nothing). `random`: any 32 random bits, for Any / Other.
struct FollowOutcome
{
    bool stops = false;
    int scene = -1; // the row to launch, −1 = none
};
FollowOutcome followTarget (const ClipGrid& grid, int track, int scene, FollowKind kind, int jump, std::uint32_t random) noexcept;

// A pass the recorder turns into a Playlist clip: the clip in `slot`, played from song tick `songTick` for `length`
// ticks, starting `offset` ticks into the clip (its own offset not included).
struct RecordedPass
{
    int slot = -1;
    double songTick = 0.0, length = 0.0, offset = 0.0;
};
// The Playlist clip for a recorded pass (message thread). False when the slot is gone.
bool recordedClip (const ClipGrid& grid, const RecordedPass& pass, Clip& out);

// Slot id → its clip's index in ClipGrid::clips (−1 = free), built off the audio thread with the rack state.
using GridIndex = std::vector<int>;
GridIndex makeIndex (const ClipGrid& grid);

// --- launching ---------------------------------------------------------------------------------------------------

struct Command
{
    // record / recordEnd (round 190bv, core/SessionOps.h): a recording into the empty slot (track, scene) starts on the
    // next point of the global quantisation — what plays on the track ends there and the track is taken — and
    // recordEnd ends it on the next one (`scene` 1: the UI then launches the new clip; 0: the track stops).
    // record on a track that already records into another slot (round 190cy, as Live 12.4.16 does): that recording
    // ends on the next point and plays on in a loop, and the new slot records from there; a record the engine cannot
    // take is told back as a cancelled take (until == from).
    // `at` > 0 on record: a fixed length in ticks, the recording ends by itself. `at` >= 0 on launch: the clip's
    // position 0 is at that point of the clock (the end of its recording), whenever the command arrives; the launch
    // waits for the clip to exist, since the song that holds it is published separately.
    enum class Type : std::uint8_t { launch, release, launchScene, stopTrack, stopAll, backToArrangement, reset, record, recordEnd };
    Type type = Type::launch;
    int track = 0;
    int scene = 0;
    double at = -1.0;
};

// A slot recording whose end is known (audio → UI): [from, until) on the clock; until == from: it was cancelled before
// it began. `thenPlay`: the UI launches the new clip at `until`.
struct RecordedTake
{
    int track = 0, scene = 0;
    double from = 0.0, until = 0.0;
    bool thenPlay = false;
};

static_assert (maxTracks == scheduleMaskTracks);

// The clip's pass the scheduler plays in this block: a Clip copy placed on the Player's clock.
struct VirtualClip
{
    Clip clip;
    int slot = -1;
};

class Runtime;

class Player
{
public:
    static constexpr int maxActive = 64; // tracks with a clip at once
    static constexpr int maxVirtual = 4 * maxActive;
    static constexpr int maxCuts = 2 * maxActive;

    // A clip's play that ended before its pass did, in this block: its sounding notes end there.
    struct Cut
    {
        int slot = -1;
        double position = 0.0; // ticks into the clip's source (its own offset included)
        double at = 0.0;       // on the clock
    };

    // Audio thread. `clock` = the steady clock at the block's start (ticks), `blockTicks` = the block's length.
    void process (const Song& song, const ClipGrid& grid, const GridIndex& index, Runtime* runtime, double clock,
                  double blockTicks, double songTick = 0.0) noexcept;
    // Transport stopped: every clip ends at once. Taken tracks stay taken until "back to arrangement", unless `giveBack`
    // (ClipGrid::backOnStop).
    void stopAll (Runtime* runtime, bool giveBack = false) noexcept;
    // Before stopAll: the passes the stop cuts short are recorded up to there.
    void recordStop (const ClipGrid& grid, const GridIndex& index, Runtime* runtime) noexcept;
    void seed (std::uint32_t s) noexcept { rng = s != 0 ? s : 1u; } // the follow actions' random source (tests seed it)

    int virtualCount() const noexcept { return numVirtual; }
    const VirtualClip& virtualClip (int i) const noexcept { return virtuals[(std::size_t) i]; }
    int cutCount() const noexcept { return numCuts; }
    const Cut& cut (int i) const noexcept { return cuts[(std::size_t) i]; }

    // The tracks whose Playlist clips are silent (a grid clip took them), those a clip took in this block (with when, on
    // the clock), and those given back in this block (their Playlist clips resume where the song is).
    const TrackMask& taken() const noexcept { return takenTracks; }
    const TrackMask& givenBack() const noexcept { return backTracks; }
    int takeoverCount() const noexcept { return numTakeovers; }
    int takeoverTrack (int i) const noexcept { return takeovers[(std::size_t) i].track; }
    double takeoverAt (int i) const noexcept { return takeovers[(std::size_t) i].at; }

    // test access: the slot playing on a track (−1 = none)
    int playingOn (int track) const noexcept;
    // test access: the scene a track records into (−1 = none) and its range on the clock
    int recordingOn (int track) const noexcept;

private:
    static constexpr double never = std::numeric_limits<double>::infinity();

    struct Play
    {
        int slot = -1;
        double anchor = 0.0, from = 0.0, until = never; // the clip's position 0 is at anchor + k·length
        double followAt = never;                        // when its follow action acts
        bool recorded = false;                          // started while recording
        bool stopping = false;                          // asked to stop, waiting for the grid point
        double songFrom = 0.0;                          // recording: the song position where the current pass began
    };
    struct TrackState
    {
        int track = -1;
        Play current, previous, next;
        int heldSlot = -1;        // gate / repeat: the slot whose trigger is down
        double repeatEvery = 0.0; // repeat: while held, the clip starts over at this rate
        // recording into a slot (round 190bv): the scene, and [recFrom, recUntil) on the clock (never: still open)
        int recScene = -1;
        double recFrom = never, recUntil = never;
        bool recStarted = false, recThenPlay = false, recTold = false;
        // another slot's record button pressed while recording (round 190cy, Live 12.4.16): the slot that records next,
        // from the end of this recording (with its fixed length in ticks, 0: open)
        int recNextScene = -1;
        double recNextLength = 0.0;
        // a launch that waits for its clip to exist (the recorded clip arrives with the next published song)
        int wantScene = -1;
        double wantAnchor = 0.0;
        int wantBlocks = 0;
    };
    struct Takeover
    {
        int track = 0;
        double at = 0.0;
    };

    std::array<TrackState, maxActive> active {};
    int numActive = 0;
    std::array<VirtualClip, maxVirtual> virtuals {};
    int numVirtual = 0;
    std::array<Cut, maxCuts> cuts {};
    int numCuts = 0;
    std::array<Takeover, maxActive> takeovers {};
    int numTakeovers = 0;
    TrackMask takenTracks, backTracks;

    TrackState* stateOf (int track, bool create) noexcept;
    void launch (const Song& song, const ClipGrid& grid, const GridIndex& index, TrackState& t, int slot, double now) noexcept;
    void stopTrack (const Song& song, const ClipGrid& grid, TrackState& t, double now) noexcept;
    void endRecording (const Song& song, const ClipGrid& grid, TrackState& t, double now, bool thenPlay, Runtime* runtime) noexcept;
    void tellTake (TrackState& t, Runtime* runtime) noexcept;
    void dropNextRecording (TrackState& t, Runtime* runtime) noexcept;
    static void tellCancelled (Runtime* runtime, int track, int scene) noexcept;
    Runtime* commandRuntime = nullptr; // (while apply() runs)
    void apply (const Song& song, const ClipGrid& grid, const GridIndex& index, const Command& c, double now) noexcept;
    void addCut (const ClipGrid& grid, const GridIndex& index, const Play& p, double at) noexcept;
    void scheduleFollow (const ClipGrid& grid, const GridIndex& index, Play& p) noexcept;
    void follow (const Song& song, const ClipGrid& grid, const GridIndex& index, TrackState& t, double at) noexcept;
    void recordPartial (const ClipGrid& grid, const GridIndex& index, const Play& p, double at, Runtime* runtime) noexcept;
    std::uint32_t nextRandom() noexcept;
    std::uint32_t rng = 0x9E3779B9u;
    double lastEnd = 0.0;
};

// Everything the launcher needs at run time, made the first time a project has a grid (never with Pattern workflow) and
// kept for the engine's life: the rack states only point at it.
class Runtime
{
public:
    Runtime() noexcept { instances.fetch_add (1, std::memory_order_relaxed); }
    static inline std::atomic<int> instances { 0 }; // how many were ever made (Pattern workflow: none; the tests and --selftest check)

    SpscQueue<Command, 256> commands; // UI → audio (one producer: the message thread)

    // audio → UI, per Playlist track: the scene playing (−1 = none), the one waiting to start, the position in the clip (0..1)
    struct TrackView
    {
        std::atomic<int> playing { -1 }, pending { -1 };
        std::atomic<float> progress { 0.0f };
        std::atomic<bool> stopping { false }; // a stop waits for its grid point
        std::atomic<bool> taken { false }; // its Playlist clips are silent until "back to arrangement"
        // recording into a slot (round 190bv): the scene being recorded (−1 = none), the one waiting for its start, and
        // while a clip plays: where its position 0 is on the clock (overdub needs it)
        std::atomic<int> recording { -1 }, recordPending { -1 };
        std::atomic<double> recordFrom { 0.0 };
        std::atomic<double> anchor { 0.0 };
    };
    std::array<TrackView, maxTracks> tracks;
    std::atomic<bool> anyPlaying { false };
    std::atomic<bool> running { false }; // the clock runs (the song plays): set by playBlock, cleared when the transport stops

    // UI → audio: record the passes; audio → UI: the passes that ended
    std::atomic<bool> recording { false };
    SpscQueue<RecordedPass, 1024> recorded;
    SpscQueue<RecordedTake, 256> takes; // audio → UI: slot recordings whose end is known (round 190bv)
    // audio → UI: the clock at the start of the last block and what must be added to a song position to get the clock
    // (constant between wraps of the song; the recorder places played notes with it)
    std::atomic<double> clockNow { 0.0 }, clockMinusSong { 0.0 };

    Player player;
    double clock = 0.0; // the steady clock (ticks): the transport's position when playback started, never wrapping
};

// One block of the song while the project has a grid: the commands, then what the Playlist plays around the taken tracks,
// then the grid's passes; ends what was cut. `clock` advances. Audio thread; `index` the song's schedule index.
void playBlock (Runtime& runtime, const Song& song, const ClipGrid& grid, const GridIndex& gridIndex, double startTick, int numSamples,
                double tps, EventBuffer& out, bool resume, const ScheduleIndex* index, bool wrap, bool chase, ScheduledNotes& notes) noexcept;
} // namespace lynx::launcher
