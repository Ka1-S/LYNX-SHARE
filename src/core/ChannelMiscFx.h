#pragma once

// FL's Misc tab behaviours that act on a channel's notes (REA round 146b; docs/analise/REA_rodada146.md): the time
// knobs' scale, velocity / key tracking of the pan, the echo delay. Our own code from FL's observed behaviour (renders of
// FL's Sampler render test with one record changed).

#include <array>
#include <utility>
#include <string>
#include <vector>

#include "core/Scheduler.h"
#include "core/Song.h"

namespace lynx::channelmisc
{
// FL's time knobs (Misc › Time › Shift and Gate, the arpeggiator's Time, the porta Slide; REA_rodada6 §4): the value
// v (0…) is (27^(v/1024) − 1)·48/26 sixteenths of a step, i.e. this many beats (1024 = one step).
double flTimeKnobBeats (int v) noexcept;
// The time-knob value for `beats` (rounded), for LYNX's own knobs that show FL's scale.
int flTimeKnobValue (double beats) noexcept;

// The pan a note gets from the channel's velocity and key tracking (added to the note's own pan, clamped):
// key: (key − (120 − mid)) / 48 · pan (FL's "Mid" for keys, as FL plays it [EXEC]); velocity: 0.275 · pan ·
// (v(velocity) − v(mid)), v = FL's velocity → volume curve (REA_rodada76 §1) [EXEC: fitted on FL's renders].
float trackedPan (const ChannelMisc& misc, int key, float velocity, float notePan) noexcept;
// The note's Mod X / Y (0..1, 0.5 = none) after the Vol / Key tracking's Mod X / Y knobs (REA round 151).
std::pair<float, float> trackedModulation (const ChannelMisc& misc, int key, float velocity, float modX, float modY) noexcept;

// FL's chord list (System/Config/Chords.map: "name,short name,interval,interval…" per line, line 0 the header): the
// chord `index` as semitones above its root (0 first). Empty when the index is not in the text.
std::vector<int> chordFromFlMap (const std::string& mapText, int index);

// The echo delay of a channel, after its arpeggiator: every "on" event is repeated `echoes` times (and its "off"
// with the same delay), echo k at feedback^k of the volume, k·pan and k·pitch away. Fixed capacity, no allocation.
class NoteEcho
{
public:
    static constexpr int maxPending = 512;
    static constexpr int maxEvents = 1024;

    // `in` are the channel's events of the block (offsets within it); `out` gets them with the echoes due in the block,
    // sorted. `gains[i]` is the volume factor of out[i] (1 for the channel's own notes).
    void process (int channel, const NoteEvent* in, int inCount, double startTick, double tps, int numSamples,
                  const EchoSettings& settings, NoteEvent* out, float* gains, int& outCount) noexcept;
    void reset() noexcept { pendingCount = 0; heldCount = 0; }

    std::array<NoteEvent, maxEvents> input {}, output {};
    std::array<float, maxEvents> outputGains {};

private:
    struct Pending
    {
        double tick = 0.0;
        NoteEvent event;
        float gain = 1.0f;
    };
    struct Held // a note that is on, and the keys its echoes play
    {
        int key = 0;
        double onTick = 0.0;
        std::uint32_t noteId = 0; // the note's identity (round 190ej): its "off" finds it, its echoes' ids derive from it
        std::array<int, 32> echoKeys {}; // (fat mode: the second, mirrored series after the first)
        int count = 0;
    };
    void schedule (const Pending& p) noexcept;

    std::array<Pending, maxPending> pending {};
    int pendingCount = 0;
    std::array<Held, 64> held {};
    int heldCount = 0;
};
} // namespace lynx::channelmisc
