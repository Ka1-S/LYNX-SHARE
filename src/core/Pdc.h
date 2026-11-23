#pragma once

// Plugin delay compensation (PDC), in the "per-edge" model FL uses
// (docs/analise/REA_rodada2.md §1): in routing order,
//   inAlign[T] = max(latency of the instruments feeding T, outLat[S] of each S -> T)
//   outLat[T]  = inAlign[T] + sum of the latencies of T's enabled effects
// and each edge (instrument -> track, track -> track) delays by what's missing to reach
// the destination's inAlign. The longest path is never delayed.
// FL's manual track latency (MixerTrack::latencyMs, REA round 165f-mixer: FUN_010ff3b0) adds to outLat[T] (it may be
// negative), and a track whose latency compensation is bypassed "Always" gets no delay on its outputs (FUN_010ffca0) —
// its outLat still counts at the destination. "When recording or not playing" is planned like "Never"; the renderer
// skips those delays while the transport is stopped or recording.

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "core/Song.h"

namespace lynx
{
struct PdcPlan
{
    std::vector<int> channelDelay;             // per channel, in samples
    std::vector<std::vector<int>> sendDelay;   // [track][k]: k = 0 Master, k = 1 + i for sends[i], then one per
                                               // Fruity Send slot (fruitySendsOf order): k = 1 + sends.size() + j
    std::vector<int> inAlign, outLat;          // per track
    // [track][slot]: how late a slot's input is (the track's inAlign plus the active slots before it), in samples —
    // FL's per-slot delay that "Compensate automations" shifts the slot's automation by (REA round 168j)
    std::vector<std::vector<int>> slotInput;
    // [track][slot]: the delay (samples) each side input of a slot needs to line up with the slot's own input (round
    // 190hq, B13): k = 0 its side-chain source, 1 its second side-chain source, 2 / 3 a hosted effect's routed
    // Connections main / side source (sideSources). A side input is late by its insert's outLat; the slot's input by
    // slotInput. The plan raises the listening insert's inAlign when a source would arrive later than the slot's input,
    // so these are never negative. 0 = no delay (no source, a source bypassed "Always", or already in line).
    std::vector<std::vector<std::array<int, 4>>> sideDelay;
    // [track]: how late an insert's audio is when its fader, pan and routes act on it (its inAlign plus its active
    // slots' latencies; the manual latency delays nothing inside the insert) — Song::compensateMixerLevels (round 190hq)
    std::vector<int> levelLag;
    int total = 0;                              // latency at the Master output
    int currentLatency = 0;                     // (round 190my) FL's Current track's active slots' latency, whatever its source

    bool any() const noexcept;
};

// `channelLatency[c]` = latency of the channel's instrument; `slotLatency[t][s]` = of the slot's
// effect (0 if disabled). Vectors smaller than the Song count as 0.
// `sampleRate` converts the tracks' manual latencies (ms) to samples.
// `currentSource` (round 190my, DF-82): the mixer track FL's Current (C) track runs on (the selected one, 0 = the
// Master; −1 = none, as in a render that leaves C out). C's active slots' latency is added to that track's output latency
// (REA 190mv: FUN_010ff3b0 / FUN_010ffca0 add C's latency to its target's), after its fader: levelLag leaves it out.
PdcPlan computePdc (const Song& song, const std::vector<int>& channelLatency, const std::vector<std::vector<int>>& slotLatency,
                    double sampleRate = 44100.0, int currentSource = -1);

// The inserts a slot of insert `track` listens to besides its own input (round 190hq): {side-chain, second side-chain,
// routed Connections main source, routed Connections side source}; −1 = none (an insert ≥ 1 otherwise, never `track`).
std::array<int, 4> sideSources (const Song& song, int track, const EffectSlot& slot);

// A track's manual latency in samples (FL rounds ms × rate / 1000).
int manualLatencySamples (const MixerTrack& track, double sampleRate) noexcept;

// The latency the effect slots of insert `track` add to what passes through it (round 168x: a take tapped after the
// slots is that late against the device input): outLat − inAlign without the manual track latency, which delays
// nothing inside the insert. 0 for a track the plan doesn't have.
int insertEffectsLatency (const PdcPlan& plan, const Song& song, int track, double sampleRate) noexcept;

// Round 190qh (D2-MX9): the samples a live PDC change or an effect slot's on / off switch takes to move from the old
// sound to the new one. A linear crossfade of N samples between two signals a and b adds at most |b − a| / N ≤ 2 / N
// (full scale) to any step of the output: out(k+1) − out(k) = (1 − g)·Δa + g·Δb + (b − a)·Δg. N = rate / (100 π) keeps
// that added step under the steepest step of a full-scale 100 Hz sine (2π · 100 / rate): no click, in 3.2 ms
// (141 samples at 44.1 kHz, 153 at 48 kHz). tests/MixerQol190qhTests.inc measures it.
int switchFadeSamples (double sampleRate) noexcept;

// Stereo delay line with an integer delay, allocated on creation. process() does not allocate.
class DelayLine
{
public:
    explicit DelayLine (int delaySamples);
    // Round 190qh (D2-MX9, FL: a switched-off slot leaves the PDC at once): the line that takes over from `previous` (the
    // same edge's line in the snapshot before; nullptr = the edge had no delay) when a live edit changed this edge's
    // delay. It starts from what `previous` holds and moves from `previousDelay` to `delaySamples` with a crossfade of
    // `fadeSamples`: a shorter delay at once, a longer one as soon as the line holds that much audio (the old delay
    // plays until then). `delaySamples` may be 0: the edge needs no delay any more, and once moved the line lets the
    // audio through untouched. A first build (an export, a render) never makes one, so renders stay bit for bit.
    DelayLine (int delaySamples, std::shared_ptr<DelayLine> previous, int previousDelay, int fadeSamples);

    int length() const noexcept { return delay; }
    // (audio thread, round 190qh) a line whose move to no delay is over: it changes nothing any more
    bool passesThrough() const noexcept { return delay == 0 && ! moving; }
    void process (float* left, float* right, int n) noexcept; // in place

    // Delays `source * level` and adds it into `dest`, in a single pass. With a null source (silent
    // source) pushes silence; when the line is already all silence, does nothing.
    void mixInto (const float* sourceLeft, const float* sourceRight, float level, float* destLeft, float* destRight, int n) noexcept;

    // Empties the line (no allocation; nothing to do when it already holds only silence): used when a bypassed edge
    // goes back to being compensated, so it starts from silence instead of replaying what it held.
    void clear() noexcept;
    // Only silence inside (what comes out next is silence until something non-silent goes in).
    bool silent() const noexcept { return quiet >= capacity; }

    // (round 190qh, the UI thread's next build) the move from the line before is over: the audio took over its
    // history and the crossfade ended (a line made with the first constructor always is)
    bool settled() const noexcept { return settledFlag.load (std::memory_order_acquire); }
    // (the UI thread) lets the line before go once the audio no longer reads it
    void dropPrevious() noexcept;

private:
    int delay;      // what the line delays by (0: a line made by a live change that passes the audio through once moved)
    int capacity;   // the ring's length: the delay, or the longer of the two delays of a move
    std::vector<float> left, right; // planar ring
    int position = 0;
    int quiet = 0; // consecutive silent samples already written (up to capacity)
    // round 190qh: the move from the line before (audio thread, but `previous` and `from`, set by the UI thread)
    std::shared_ptr<DelayLine> previous;
    int from = 0;                 // the delay the line before played
    int wait = 0;                 // samples before the crossfade starts (a longer delay: the line fills first)
    int fadeLeft = 0, fadeLength = 0;
    bool moving = false;
    std::atomic<bool> adopted { true };     // the line before's history is in the ring (or there was none)
    std::atomic<bool> settledFlag { true };
    void adopt() noexcept;
    float historyAt (int age, int side) const noexcept; // the sample that went in `age` samples ago (1 = the last)
    void runMove (const float* sl, const float* sr, float level, float* dl, float* dr, bool add, int n) noexcept;
};
} // namespace lynx
