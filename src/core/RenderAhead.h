#pragma once

// Render-ahead (anticipative processing; LYNX extra, round 190gq, owner decision O22: on by default, Options › Audio
// settings › "Render ahead (lower CPU at small buffers)"). Reaper's model: the part of the rack nobody plays live renders
// a few device blocks ahead of the song position on its own thread, into a ring; the device callback renders only the
// live part at the device block and mixes in what is ready. A heavy project then has a whole window (about 23 ms) for
// its slow blocks instead of one device block, and its serial chains no longer bound the callback.
//
// The live part, always real-time: every insert with an audio input source (armed, monitored or not), the inserts with
// a recording tap, hardware outputs or hardware inserts, the metronome's and the side input's insert, an insert whose
// audio the audio editor records, the units pinned to the audio thread ("Allow threaded processing" off), the channels
// played live (the selected channels, the MIDI inputs' targets, any channel a live note reached), the units whose editor
// shows live meters or a spectrum, the Master — and everything downstream of any of them (routes, sends, send taps,
// side-chains, a hosted effect's Connections), plus whatever is tied to them by notes or controls (MIDI routes, MIDI
// Out / Dashboard ports, Layers, cut groups, VFX channels, controllers driving a parameter, a generator's extra
// outputs). Each unit is rendered by exactly one of the two renderers (core/RackRenderer.h Split); the units of the
// part rendered ahead that a live insert takes (the boundary) cross over through the ring, chunk by chunk.
//
// Exactness. In steady playback (Song or Pattern mode, loops, the song's wrap, tempo automation) the next blocks'
// positions are worked out exactly as the device callback works them out (planSubBlock), so the ahead part renders the
// very blocks the callback would have rendered: the output is bit-identical to the real-time path (null tests: tests
// render_ahead_*, tools/perfbench --ahead). Offline renders never use it.
//
// What is heard later than real time, by at most the window: edits that reach the ahead part (a knob, a fader, a note
// edited in a pattern, a plugin's own editor) — while edits keep coming the window holds at one block (the "tweak
// hold": a dragged knob is heard about one block late), and 250 ms after the last one it grows back; an edit that changes
// the partition or the timeline (routing, tempo, play mode, current pattern) waits for the ring to drain and then takes
// effect for both parts together. A live note for a channel of the ahead part is played there at once (heard a window
// late) and that channel joins the live part. Stop, a jump and a change of the transport that the window did not foresee
// drop what was rendered ahead: the ahead part's notes end there (a jump then chases them as usual), and their effects'
// tails may hold up to one window of audio that was never heard.
//
// The audio thread never allocates, locks or waits on a lock here; it may spin briefly while the render-ahead thread
// finishes the very block it needs.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "core/LiveEventQueue.h"
#include "core/LiveInput.h"
#include "core/MarkerPlan.h"
#include "core/Meters.h"
#include "core/RackRenderer.h"
#include "core/SnapshotExchange.h"

namespace lynx
{
class WorkerPool;
class OsSemaphore;

// Options › Audio settings › "Render ahead (lower CPU at small buffers)": on by default (O22). App-wide, kept in
// ui-options.txt ("renderAhead=0/1").
std::atomic<bool>& renderAheadOption() noexcept;

namespace renderahead
{
// The live targets the engine knows (UI / MIDI threads write, the audio thread reads): the channels notes are played into
// live and the inserts that must stay real-time for the UI (an open editor's live meters...). A seqlock: the reader
// gets a consistent copy or keeps its last one.
struct Targets
{
    static constexpr int maxChannels = 64, maxTracks = 32;
    void publish (const int* channels, int channelCount, const int* tracks, int trackCount) noexcept;
    // false: unchanged since `version` (or being written: try again next block)
    bool read (std::array<int, maxChannels>& channels, int& channelCount, std::array<int, maxTracks>& tracks, int& trackCount,
               std::uint32_t& version) const noexcept;
    // An edit that may reach the part rendered ahead without a new snapshot (a fader or knob drag through the live
    // levels, a hosted plugin's parameter): the window holds at one block while they keep coming.
    void tweaked() noexcept { tweaks.fetch_add (1, std::memory_order_relaxed); }
    std::uint32_t tweakCount() const noexcept { return tweaks.load (std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> versionCounter { 0 };
    std::array<std::atomic<int>, maxChannels> channelList {};
    std::array<std::atomic<int>, maxTracks> trackList {};
    std::atomic<int> channelTotal { 0 }, trackTotal { 0 };
    std::atomic<std::uint32_t> tweaks { 0 };
};

// The device callback's transport, as it moves the song position block by block (AudioEngine's audio callback: the
// loop region or the song's wrap, FL's marker actions). Read by the audio thread from the engine's Transport each block.
struct TransportModel
{
    double sampleRate = 44100.0;
    int deviceBlock = 0; // the callback's samples
    int offset = 0;      // where the current sub-block starts in it
    bool songMode = false;
    // the Transport's atomics as the callback reads them
    double loopStart = 0.0, loopEnd = 0.0, songEnd = 0.0, songStart = 0.0;
    bool linear = false, throughRegion = false;
    const MarkerPlan* markers = nullptr; // Song mode: FL's marker actions (nullptr = none)
    double pendingAt = -1.0;             // the user's quantized jump waiting at this tick (< 0 = none)
    // anything the window cannot foresee (an external MIDI clock): false
    bool predictable = true;
    bool operator== (const TransportModel& o) const noexcept;
};

// One sub-block of the device callback while playing: where it starts and how long it is. `position` is the transport's
// position before it (as the callback has it after the previous sub-block), `done` the samples of the device block
// already played, `tps` the ticks per sample the callback uses for it. Mirrors AudioEngine::audioDeviceIOCallbackWithContext.
struct SubBlock
{
    double start = 0.0; // the sub-block's first tick (after a wrap at the loop's end)
    int samples = 0;
    double next = 0.0;  // the transport's position after it (wrapped at the loop's end)
    bool last = false;  // a marker jump / pause is due after it: nothing after it can be foreseen
};
SubBlock planSubBlock (const TransportModel& model, double position, int done, double tps) noexcept;
// the transport's ticks per sample at `bpm` (engine/Transport.h ticksPerSample, the same arithmetic)
inline double transportTps (double bpm, double sampleRate) noexcept { return bpm * ppq / (60.0 * sampleRate); }

// What the callback renders now (ChannelRackProcessor::processBlock)
struct Block
{
    bool playing = false;
    double startTick = 0.0;
    int samples = 0;
    double tps = 0.0;             // the ticks per sample the callback advances this sub-block with
    double tempoOverride = 0.0;   // an external MIDI clock's tempo (> 0: no render-ahead)
    bool playTruncated = true, resetPlugins = false;
    unsigned userJumps = 0;
    const TransportModel* model = nullptr; // nullptr: not the device callback (an offline render, a warm-up)
    int metronomeTrack = 0;       // the metronome's insert (live)
    int sideTrack = -1;           // the side-chain bus's insert (live)
    int captureTrack = -1;        // an insert the audio editor records (live)
    bool metering = true;         // false: an export through the graph (no render-ahead)
};

// Counters for the tests, perfbench and the CPU panel (audio thread writes, any thread reads)
struct Stats
{
    std::atomic<std::uint64_t> blocks { 0 }, fromRing { 0 }, waited { 0 }, inline_ { 0 }, flushes { 0 }, drains { 0 },
        engagements { 0 }, partitionChanges { 0 }, promoted { 0 }, splitMismatches { 0 }, statesSwapped { 0 }, backoffs { 0 },
        probes { 0 }, keptOn { 0 }, keptOff { 0 };
    // the last probe: the rack's CPU (% of a core) and overruns per minute, render-ahead off and on
    std::atomic<float> probeCpuOff { 0.0f }, probeCpuOn { 0.0f }, probeOverOff { 0.0f }, probeOverOn { 0.0f };
    std::atomic<int> aheadChannels { 0 }, aheadTracks { 0 }, boundary { 0 }, window { 0 };
    std::atomic<bool> engaged { false };
};
} // namespace renderahead

class RenderAhead
{
public:
    // `live`: the renderer the callback renders with (it renders everything while render-ahead is off)
    explicit RenderAhead (RackRenderer& live);
    ~RenderAhead();
    RenderAhead (const RenderAhead&) = delete;
    RenderAhead& operator= (const RenderAhead&) = delete;

    // Off the audio thread (allocates; the audio callback is not running): as RackRenderer::prepare
    void prepare (double sampleRate, int maxBlockSize);
    // the workers (shared with the live renderer while render-ahead is off; may be nullptr) and the start of the
    // render-ahead thread; `onThreadStart` raises its priority (as the workers'). Once, before playback.
    void setWorkerPool (WorkerPool* pool, std::function<void()> onThreadStart = {});
    void setLiveInput (LiveInput* input) noexcept { liveInput = input; }
    void setTargets (const renderahead::Targets* t) noexcept { targets = t; }
    // the window in samples (default about 23 ms; tests use other values)
    void setWindowSamples (int samples) noexcept { windowSamples = samples; }
    // With the option on, render-ahead runs when the project needs it: the callback's average load at or above
    // `engageLoad` of its block, or one block at or above `engageSpike` (a light project has no deadline to ease and
    // would only pay the second thread); it stops after `idleSeconds` with the whole rack under `idleLoad`.
    // always = true (tests, null runs): whenever it can.
    void setAlwaysEngage (bool always) noexcept { alwaysEngage = always; }
    static constexpr double engageLoad = 0.20, engageSpike = 0.60, idleLoad = 0.08, idleSeconds = 10.0;

    // Audio thread, one sub-block: as ChannelRackProcessor would call RackRenderer::process. The snapshot is taken from
    // `exchange` here (while render-ahead runs, a new one waits until the part rendered ahead can take it too).
    void process (SnapshotExchange<RackState>& exchange, const renderahead::Block& block, float* left, float* right, LiveEventQueue* live,
                  MeterBank* meters, LiveEventQueue* moreLive) noexcept;

    // The tempo the next sub-block moves with while render-ahead runs (the transport follows it instead of the song's
    // tempo until a tempo edit takes effect for both parts): > 0, else the Transport's own
    double pinnedBpm() const noexcept { return pinned.load (std::memory_order_relaxed); }
    // The workers: the audio callback parks them after its block only while render-ahead is off (the render-ahead thread
    // parks them when it sleeps)
    bool ownsWorkers() const noexcept { return engagedNow.load (std::memory_order_relaxed); }
    // The renderer that renders `channel` now (soundingKeys: the Piano roll's keyboard)
    const RackRenderer& rendererOf (int channel) const noexcept;
    RackRenderer& aheadRenderer() noexcept { return *ahead; }
    // the snapshot the live renderer rendered the last block with (audio thread)
    const RackState* currentState() const noexcept { return liveState; }
    const renderahead::Stats& stats() const noexcept { return counters; }
    // the touched / released automation slots (RackRenderer::setOverriddenSlots / setReleasedSlots) reach both renderers
    void setOverriddenSlots (const std::vector<int>& slots) noexcept;
    void setReleasedSlots (const std::vector<int>& slots) noexcept;
    void setProfiling (bool on) noexcept;
    // The render-ahead thread renders nothing more (the engine is going away: its snapshots are about to be freed);
    // returns once it is idle. prepare() lets it work again.
    void halt() noexcept;

    // the partition of a state (tests): [channel] / [track] 1 = rendered ahead; false = nothing to render ahead
    struct Partition;
    static bool partitionOf (const RackState& state, const int* liveChannels, int liveChannelCount, const int* liveTracks, int liveTrackCount,
                             const LiveInput* input, std::vector<char>& aheadChannels, std::vector<char>& aheadTracks, int& boundary);

private:
    struct Entry;
    void processBlock (SnapshotExchange<RackState>& exchange, const renderahead::Block& block, float* left, float* right, LiveEventQueue* live,
                       MeterBank* meters, LiveEventQueue* moreLive) noexcept;
    // Round 190gq, the owner's rule "no project gets worse": when render-ahead might help, the engine measures it — a
    // probe of probeRounds × (probeSeconds off, probeSeconds on), the rack's own CPU time (the callback's, the workers',
    // the render-ahead thread's: per-thread CPU clocks) and its overruns (a callback longer than its block) — and keeps
    // render-ahead only with no more overruns and at most probeCpuMargin more CPU. The choice holds for decidedSeconds
    // of playback or until the project changes; the back-off rules still apply while it is on. Each switch is a clean
    // sync (the output stays bit-identical).
    void account (double wallNs, double cpuNs, int samples, bool playing, const RackState* state) noexcept;
    bool renderAheadAllowed() const noexcept;
    enum class Probe { none, off, on, decidedOn, decidedOff };
    Probe probe = Probe::none;
    static constexpr double probeSeconds = 2.0, probeWarmup = 0.25, probeCpuMargin = 1.02, decidedSeconds = 60.0;
    static constexpr int probeRounds = 2;
    double probeLeft = 0.0, decidedLeft = 0.0, warmupLeft = 0.0;
    int probeRound = 0;
    double cpuOff = 0.0, cpuOn = 0.0, secondsOff = 0.0, secondsOn = 0.0;
    int overOff = 0, overOn = 0;
    std::uint64_t decidedState = 0;
    std::atomic<std::uint64_t> aheadCpuNs { 0 }; // the render-ahead thread's CPU time (the thread adds per block)
    std::uint64_t lastWorkersCpu = 0, lastAheadCpu = 0;
    void threadLoop (std::function<void()> onStart);
    bool producing() const noexcept;
    void wakeThread() noexcept;
    bool claimForCallback() noexcept; // waits while the thread renders; true when the callback holds the renderer
    void releaseClaim() noexcept;
    void renderJob (Entry& entry, const RackState& state, double startTick, int samples, bool playing, bool useWorkers) noexcept;
    void harvestMeters (Entry& entry) noexcept;
    void replayMeters (const Entry& entry, MeterBank* meters) noexcept;
    bool engage (const RackState& state, const renderahead::Block& block) noexcept;
    void disengage (const RackState* state, bool dropped) noexcept;
    void dropRing (const RackState& state) noexcept;
    bool computePartition (const RackState& state, const renderahead::Block& block, Partition& out) noexcept;
    void applyPartition (const RackState& state, Partition& next, bool moving) noexcept;
    void routeLiveEvents (LiveEventQueue* queue) noexcept;
    void renderLive (const RackState& state, const renderahead::Block& block, Entry& entry, float* left, float* right, MeterBank* meters) noexcept;
    void retireHeld (SnapshotExchange<RackState>& exchange) noexcept;
    void noteLoad (double ns, int samples, bool playing) noexcept;

    RackRenderer& live;
    std::unique_ptr<RackRenderer> ahead;
    WorkerPool* pool = nullptr;
    LiveInput* liveInput = nullptr;
    std::unique_ptr<LiveInput> aheadInput; // the flags the ahead renderer reads (no device input reaches it)
    std::unique_ptr<MeterBank> aheadMeters; // its meters, taken into the ring and replayed when the block is heard
    std::unique_ptr<HostTimeline> aheadTimeline;
    const renderahead::Targets* targets = nullptr;
    renderahead::Stats counters;

    // the partition in use and the one being worked out (the renderers hold pointers into `current`)
    std::unique_ptr<Partition> current, next;

    // the ring: entries in order, the thread writes at `writeIndex`, the callback reads at `readIndex`
    std::vector<std::unique_ptr<Entry>> entries;
    std::vector<float> audioPool;
    std::vector<std::uint8_t> flagPool;
    int entryCount = 0, targetFill = 0, stride = 0;
    std::atomic<int> writeIndex { 0 }, readIndex { 0 };
    int windowSamples = 0;
    double sampleRate = 44100.0;
    int maxBlock = 0;

    // who renders with `ahead`: 0 nobody, 1 the render-ahead thread, 2 the audio callback
    std::atomic<int> owner { 0 };
    std::atomic<bool> running { false };     // the thread may produce (engaged and not draining)
    std::atomic<bool> drain { false };       // the callback waits for the ring to empty, then syncs
    std::atomic<bool> sleeping { false };
    std::atomic<bool> quit { false }, halted { false };
    std::atomic<bool> engagedNow { false };
    std::atomic<double> pinned { 0.0 };
    std::unique_ptr<OsSemaphore> wake;
    std::thread worker;

    // the thread's model of the transport (written by whoever holds the claim)
    renderahead::TransportModel model;
    MarkerPlan modelMarkers;
    double nextPosition = 0.0;
    int nextDone = 0;
    std::atomic<bool> modelStop { true }; // nothing past the last planned block can be foreseen
    renderahead::Block jobBlock;      // the transport options of the jobs
    bool jobRecording = false, jobLinear = false;
    double jobPatternOrigin = -1.0;

    // snapshots: the callback's (live) state, the one the ahead renderer uses, the one waiting for a sync
    const RackState* liveState = nullptr;
    const RackState* held = nullptr;                 // still used by the ahead renderer (not retired yet)
    bool pendingSync = false;                        // the exchange's active state waits for the next sync
    std::atomic<const RackState*> aheadNext { nullptr }, aheadAck { nullptr };
    const RackState* aheadState = nullptr;           // the thread's / inline jobs' state

    // live events for channels of the ahead part (the audio thread pushes, the ahead renderer pops), and the live
    // renderer's own
    std::unique_ptr<LiveEventQueue> forward, localEvents;
    std::array<int, 64> promotedChannels {};
    int promotedCount = 0;
    bool promotionPending = false;

    // the engine's targets as last read
    std::array<int, renderahead::Targets::maxChannels> targetChannels {};
    std::array<int, renderahead::Targets::maxTracks> targetTracks {};
    int targetChannelCount = 0, targetTrackCount = 0;
    std::uint32_t targetVersion = ~0u, lastTweaks = 0, partitionTargets = ~0u;
    std::atomic<int> tweakHoldSamples { 0 };
    int editorPoll = 0;
    bool localPending = false; // live notes left in localEvents by a disengage
    bool engageBlocked = false;                      // nothing to render ahead in this state: retry when it changes
    std::uint64_t blockedSerial = 0;
    std::uint32_t blockedTargets = ~0u;
    int blockedLiveSeed = 0;
    int lastBlockSize = 0, stableSamples = 0; // a device whose block size changes: render-ahead waits until it holds
    // the load (wall time / block duration): the callback's while off, both parts' while on (the thread adds its jobs)
    bool alwaysEngage = false;
    bool stickyOn = false; // started once since playback started: syncs (edits) don't wait for the load again
    double loadAverage = 0.0;
    bool spiked = false;
    double idleFor = 0.0;
    std::atomic<std::uint64_t> aheadBusyNs { 0 }; // the thread's rendering time since the callback last looked
    // (the thread) the ahead part's load rendered serially (job time / block time): above serialLimit one thread can't
    // keep the window, the workers take part; a serial job now and then measures it again
    double serialLoad = 0.0;
    std::atomic<float> serialLoadSeen { 0.0f }; // (the callback reads it)
    // Back-off (the owner's rule: no project may get worse): render-ahead stops by itself, cleanly, when it cannot help —
    // its part needs the workers anyway (serial load at or above serialLimit for backoffHeavySeconds: it would only add a
    // thread and a second renderer's per-block work to a pool that is busy already), or the callback had to wait for
    // the thread or render in its place in more than backoffWaitShare of the blocks of the last backoffWindowSeconds
    // (the window is not absorbing anything). It stays off for backoffSeconds of playback, or until the project changes.
    static constexpr double backoffHeavySeconds = 1.0, backoffWaitShare = 0.05, backoffWindowSeconds = 2.0, backoffSeconds = 0.0;
    double heavyFor = 0.0, windowSeconds = 0.0, backoffLeft = 0.0;
    int windowBlocks = 0, windowWaits = 0;
    std::uint64_t backoffState = 0;
    bool backoffPending = false;
    int jobsSinceSerial = 0;
    static constexpr double serialLimit = 0.7;
    renderahead::TransportModel lastModel;
    bool lastFlagsSet = false;
    std::vector<float> scratchL, scratchR;
};
} // namespace lynx
