#pragma once

// Ableton Link (round 190gl): LYNX as a peer of a Link session — the tempo shared with every peer, beats and phase
// aligned over a quantum (the clip grid's global launch quantisation), Play waiting for the session's phase, and the
// optional Start Stop Sync. Ableton's Link library (GPL-2.0-or-later, used under the GPLv3; third_party/link) does the
// networking; core/LinkSync.h holds LYNX's rules and is what the tests check.
//
// Threads: the message thread switches Link on / off and the options; the audio thread runs beginBlock() once per
// device callback (Link's audio-thread API: no allocation, no locks); Link's own threads call back with the number of
// peers. Nothing of Link exists until it is first switched on: tests, --snapshot and renders never open a socket.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "core/LinkSync.h"
#include "engine/Transport.h"

namespace lynx
{
class AbletonLink
{
public:
    AbletonLink();
    ~AbletonLink();
    AbletonLink (const AbletonLink&) = delete;
    AbletonLink& operator= (const AbletonLink&) = delete;

    // --- message thread ---
    // Joins or leaves the network session. On joining, a session of LYNX alone takes `projectBpm`; a running one keeps
    // its tempo (the first peer sets it).
    void setEnabled (bool on, double projectBpm);
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }
    void setStartStopSync (bool on);
    bool startStopSync() const noexcept { return startStop.load (std::memory_order_relaxed); }
    int peers() const noexcept { return numPeers.load (std::memory_order_relaxed); }
    // The session's tempo as the audio thread last saw it (0 = Link off or not running yet).
    double sessionTempo() const noexcept { return shownTempo.load (std::memory_order_relaxed); }
    // While the transport is stopped (or waits for the session) with another peer: how far the session is through the
    // quantum, counted to where Play would start (0..1, the reference DAW's progress bar); -1 = nothing to show.
    double startProgress() const noexcept { return progress.load (std::memory_order_relaxed); }
    // A peer changed the tempo since LYNX last set it (the project takes it when Link is switched off).
    bool tempoChangedByPeer() const noexcept { return peerTempo.load (std::memory_order_relaxed); }
    // A tempo the user set (sent at once, without waiting for the next block).
    void proposeTempo (double bpm);
    // Start Stop Sync: a peer started / stopped the session. The UI presses Play / Stop as the user would (true once).
    bool takeRemoteStart() noexcept { return remoteStart.exchange (false); }
    bool takeRemoteStop() noexcept { return remoteStop.exchange (false); }

    // --- audio thread ---
    void deviceStarted (double sampleRate) noexcept;
    // Before the block: the session's tempo and phase move the transport (its `linkBpm`, a jump of `positionTicks`,
    // a start that waits). Returns the sample of this block where playback starts (the engine splits the block there
    // and sets `playing`), or -1. `outputLatencyMs` = when the block's first sample is heard after the callback.
    int beginBlock (Transport& transport, int numSamples, double outputLatencyMs) noexcept;

    // the audio-thread cost of beginBlock (tests / --linktest / the CPU tooltip)
    struct Stats
    {
        std::uint64_t blocks = 0;
        double meanUs = 0.0, maxUs = 0.0;
    };
    Stats stats() const noexcept;
    void resetStats() noexcept;

    // LYNX --linktest report.txt [seconds] ["peer"]: Link switched on with simulated audio callbacks (48 kHz, 256 frames,
    // real time) for `seconds` (default 20). With "peer", a second Link peer in this process starts at another tempo and
    // phase, so the two must converge; without it, a peer on the network (LinkHut) is expected. The report gives the
    // peers, the tempo and phase error over time, the start that waited for the phase and the audio-thread cost.
    static std::string selfTest (double seconds, bool ownPeer);
    // tests / shots: another Link peer in this process at `bpm` (playing, with Start Stop Sync), until the handle goes
    static std::shared_ptr<void> makeTestPeer (double bpm);

private:
    struct Impl;
    std::unique_ptr<Impl> owned;           // created on the first setEnabled (true); kept until the destructor
    std::atomic<Impl*> impl { nullptr };   // what the audio thread reads
    std::atomic<bool> enabled { false }, startStop { false }, peerTempo { false };
    std::atomic<bool> remoteStart { false }, remoteStop { false };
    std::atomic<int> numPeers { 0 };
    std::atomic<double> shownTempo { 0.0 }, progress { -1.0 };
    std::atomic<std::uint32_t> resets { 0 }; // the message thread asks the audio thread to start over (Link switched on)

    // audio thread
    struct Rt
    {
        bool active = false;      // Link was on in the last block
        std::uint32_t seenResets = 0;
        bool running = false;     // LYNX's transport runs on the session
        bool pending = false;     // Play waits for the session's phase
        double startBeat = 0.0;   // where the waiting start begins (beats)
        double steady = 0.0;      // LYNX's running clock (beats; never wraps: loops and jumps don't break the phase)
        bool sessionPlaying = false;
        bool answeringPeer = false; // the UI was asked to press Play for a peer's start
        double sampleRate = 44100.0;
        std::int64_t sampleTime = 0;
        linksync::TempoArbiter arbiter;
    } rt;

    std::atomic<std::uint64_t> statBlocks { 0 };
    std::atomic<double> statSum { 0.0 }, statMax { 0.0 };
};
} // namespace lynx
