// Ableton Link (round 190gl): see engine/AbletonLink.h. This file is the only one that includes Link and its Asio.
//
// Link's README ("Other Build Systems") asks for these before the first Asio or Winsock header; this translation unit
// includes no other library before them, so they are set here rather than in the three build scripts.
#if defined(_WIN32)
 #ifndef LINK_PLATFORM_WINDOWS
  #define LINK_PLATFORM_WINDOWS 1
 #endif
 #ifndef INCL_EXTRA_HTON_FUNCTIONS
  #define INCL_EXTRA_HTON_FUNCTIONS 1
 #endif
 #ifndef _WIN32_WINNT
  #define _WIN32_WINNT 0x0601
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX 1
 #endif
#elif defined(__APPLE__)
 #ifndef LINK_PLATFORM_MACOSX
  #define LINK_PLATFORM_MACOSX 1
 #endif
 #ifndef LINK_PLATFORM_UNIX
  #define LINK_PLATFORM_UNIX 1
 #endif
#else
 #ifndef LINK_PLATFORM_LINUX
  #define LINK_PLATFORM_LINUX 1
 #endif
 #ifndef LINK_PLATFORM_UNIX
  #define LINK_PLATFORM_UNIX 1
 #endif
#endif
#ifndef ASIO_STANDALONE
 #define ASIO_STANDALONE 1
#endif
#ifndef ASIO_NO_TYPEID
 #define ASIO_NO_TYPEID 1
#endif
// (a namespace of LYNX's own for the bundled Asio, as Link's CMake names it: no clash with another copy)
#ifndef ASIO_VERSION_NAMESPACE
 #define ASIO_VERSION_NAMESPACE link_asio_1_38_2
#endif

#if defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wpedantic"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wsign-compare"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
 #pragma GCC diagnostic ignored "-Wextra"
#endif
#include <ableton/Link.hpp>
#include <ableton/link/HostTimeFilter.hpp>
#if defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

#include "engine/AbletonLink.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

namespace lynx
{
using Micros = std::chrono::microseconds;

struct AbletonLink::Impl
{
    explicit Impl (double bpm) : link (bpm) { filter.reset(); }
    ableton::Link link;
    ableton::link::HostTimeFilter<ableton::Link::Clock> filter; // audio thread (its points are reserved up front)
};

AbletonLink::AbletonLink() = default;

AbletonLink::~AbletonLink()
{
    impl.store (nullptr);
    if (owned != nullptr)
        owned->link.enable (false);
}

void AbletonLink::setEnabled (bool on, double projectBpm)
{
    if (on && owned == nullptr)
    {
        owned = std::make_unique<Impl> (std::clamp (projectBpm, 20.0, 999.0));
        auto* self = this;
        owned->link.setNumPeersCallback ([self] (std::size_t n) { self->numPeers.store ((int) n); });
        owned->link.enableStartStopSync (startStop.load());
        impl.store (owned.get());
    }
    if (owned == nullptr)
        return;
    if (on && ! owned->link.isEnabled())
    {
        // alone, the session keeps LYNX's tempo; joining a running session, its tempo wins (Link merges them)
        auto state = owned->link.captureAppSessionState();
        state.setTempo (std::clamp (projectBpm, 20.0, 999.0), owned->link.clock().micros());
        owned->link.commitAppSessionState (state);
        resets.fetch_add (1);
        peerTempo.store (false);
    }
    owned->link.enable (on);
    enabled.store (on);
    if (! on)
    {
        numPeers.store (0);
        shownTempo.store (0.0);
        progress.store (-1.0);
    }
}

void AbletonLink::setStartStopSync (bool on)
{
    startStop.store (on);
    if (owned != nullptr)
        owned->link.enableStartStopSync (on);
}

void AbletonLink::proposeTempo (double bpm)
{
    if (owned == nullptr || ! enabled.load() || bpm <= 0.0)
        return;
    auto state = owned->link.captureAppSessionState();
    if (std::abs (state.tempo() - bpm) < 1.0e-6)
        return;
    state.setTempo (bpm, owned->link.clock().micros());
    owned->link.commitAppSessionState (state);
}

void AbletonLink::deviceStarted (double sampleRate) noexcept
{
    rt.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    rt.sampleTime = 0;
    if (auto* p = impl.load())
        p->filter.reset(); // (reserved storage: no allocation)
}

int AbletonLink::beginBlock (Transport& transport, int numSamples, double outputLatencyMs) noexcept
{
    auto* p = impl.load (std::memory_order_acquire);
    const auto on = p != nullptr && enabled.load (std::memory_order_relaxed);
    if (! on)
    {
        if (rt.active)
        {
            rt.active = false;
            transport.linkBpm.store (0.0, std::memory_order_relaxed);
            // a start that was waiting goes ahead at once
            auto waiting = true;
            if (transport.linkWaiting.compare_exchange_strong (waiting, false))
                transport.playing.store (true, std::memory_order_relaxed);
        }
        return -1;
    }
    const auto costStart = std::chrono::steady_clock::now();
    const auto sr = rt.sampleRate;
    const auto hostTime = p->filter.sampleTimeToHostTime ((double) rt.sampleTime) + Micros ((std::int64_t) std::llround (outputLatencyMs * 1000.0));
    rt.sampleTime += numSamples;
    auto state = p->link.captureAudioSessionState();
    auto commit = false;
    const auto quantum = std::max (1.0, transport.linkQuantum.load (std::memory_order_relaxed));
    const auto positionBeats = transport.positionTicks.load (std::memory_order_relaxed) / (double) ppq;
    const auto playing = transport.playing.load (std::memory_order_relaxed);
    const auto external = transport.externalBpm.load (std::memory_order_relaxed);
    const auto peers = numPeers.load (std::memory_order_relaxed);
    const auto syncStartStop = startStop.load (std::memory_order_relaxed);
    auto startSample = -1;
    auto started = false;

    linksync::TempoArbiter::Input in;
    in.sessionBpm = state.tempo();
    in.projectBpm = transport.projectBpm.load (std::memory_order_relaxed);
    in.songBpm = transport.songTempoNow.load (std::memory_order_relaxed);
    if (in.songBpm <= 0.0)
        in.songBpm = in.projectBpm;
    in.tempoMap = transport.songTempoMap.load (std::memory_order_relaxed);
    in.externalBpm = external;

    // Link was just switched on (or on again): LYNX's state starts over
    if (const auto r = resets.load (std::memory_order_relaxed); ! rt.active || r != rt.seenResets)
    {
        rt.active = true;
        rt.seenResets = r;
        rt.pending = false;
        rt.running = playing;
        rt.steady = positionBeats;
        rt.sessionPlaying = state.isPlaying();
        rt.arbiter.reset (in);
        // already playing alone: the session takes LYNX's beat (with peers LYNX moves to theirs, below)
        if (playing)
        {
            state.requestBeatAtTime (positionBeats, hostTime, quantum);
            commit = true;
        }
    }

    // Start Stop Sync: a peer's start or stop since the last block (the UI presses Play / Stop, so everything a press
    // does happens; the start then lands on the peer's start time)
    if (syncStartStop && state.isPlaying() != rt.sessionPlaying)
    {
        if (state.isPlaying() && ! rt.running && ! rt.pending && ! playing)
        {
            remoteStart.store (true);
            rt.answeringPeer = true;
        }
        else if (! state.isPlaying() && (rt.running || rt.pending))
            remoteStop.store (true);
    }

    // a Stop pressed while the start was waiting
    if (rt.pending && ! transport.linkWaiting.load (std::memory_order_relaxed))
    {
        rt.pending = false;
        if (syncStartStop)
        {
            state.setIsPlaying (false, hostTime);
            commit = true;
        }
    }

    // Play pressed (by the user, or by the UI for a peer's start): the session decides when
    if (playing && ! rt.running && ! rt.pending)
    {
        rt.startBeat = positionBeats;
        if (syncStartStop && state.isPlaying() && rt.answeringPeer)
            state.requestBeatAtStartPlayingTime (positionBeats, quantum); // (a peer's start: on its start time)
        else if (syncStartStop && state.isPlaying())
            state.requestBeatAtTime (positionBeats, hostTime, quantum); // (joining peers that play: the next phase)
        else if (syncStartStop)
            state.setIsPlayingAndRequestBeatAtTime (true, hostTime, positionBeats, quantum);
        else
            state.requestBeatAtTime (positionBeats, hostTime, quantum);
        rt.answeringPeer = false;
        commit = true;
        const auto startAt = state.timeAtBeat (positionBeats, quantum);
        const auto halfSample = Micros ((std::int64_t) (0.5e6 / sr));
        if (external > 0.0 || startAt <= hostTime + halfSample || peers == 0)
        {
            rt.running = true;
            rt.steady = positionBeats;
            started = true;
        }
        else
        {
            transport.playing.store (false, std::memory_order_relaxed); // nothing plays until the session's phase
            transport.linkWaiting.store (true, std::memory_order_relaxed);
            rt.pending = true;
        }
    }
    else if (! playing && rt.running) // stopped (Stop, a marker pause, a peer's stop pressed by the UI)
    {
        rt.running = false;
        if (syncStartStop)
        {
            state.setIsPlaying (false, hostTime);
            commit = true;
        }
    }

    // a waiting start: this block if the session reaches the phase in it
    if (rt.pending)
    {
        const auto startAt = state.timeAtBeat (rt.startBeat, quantum);
        const auto offset = (double) (startAt - hostTime).count() * sr / 1.0e6;
        if (offset < (double) numSamples)
        {
            auto waiting = true;
            if (transport.linkWaiting.compare_exchange_strong (waiting, false))
            {
                startSample = std::clamp ((int) std::ceil (offset - 1.0e-6), 0, numSamples - 1);
                rt.running = true;
                started = true;
                rt.steady = rt.startBeat; // (at startSample)
            }
            rt.pending = false;
        }
    }

    // the tempo (core/LinkSync.h rules 1-4)
    in.playing = rt.running;
    in.started = started;
    const auto tempo = rt.arbiter.step (in);
    if (tempo.propose > 0.0)
    {
        state.setTempo (tempo.propose, hostTime);
        commit = true;
    }
    if (commit)
        p->link.commitAudioSessionState (state);
    rt.sessionPlaying = state.isPlaying();
    peerTempo.store (rt.arbiter.changedByPeer(), std::memory_order_relaxed);

    // the phase: LYNX's clock follows the session (nudged, or a jump when far off); an external clock owns it
    auto bpm = tempo.play;
    if (rt.running && ! started && external <= 0.0)
    {
        const auto error = linksync::phaseError (state.beatAtTime (hostTime, quantum), rt.steady, quantum);
        const auto follow = linksync::follow (tempo.play, error);
        if (follow.jump != 0.0)
        {
            transport.positionTicks.store (transport.positionTicks.load (std::memory_order_relaxed) + follow.jump * ppq, std::memory_order_relaxed);
            transport.userJumps.fetch_add (1, std::memory_order_relaxed);
            rt.steady += follow.jump;
        }
        bpm = follow.bpm;
    }
    transport.linkBpm.store (bpm, std::memory_order_relaxed);
    if (rt.running)
        rt.steady += (numSamples - std::max (0, startSample)) * bpm / (60.0 * sr);

    // what the UI shows
    shownTempo.store (state.tempo(), std::memory_order_relaxed);
    if (! rt.running && peers > 0)
    {
        const auto from = rt.pending ? rt.startBeat : positionBeats;
        progress.store (linksync::phase (state.beatAtTime (hostTime, quantum) - from, quantum) / quantum, std::memory_order_relaxed);
    }
    else
        progress.store (-1.0, std::memory_order_relaxed);

    const auto us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - costStart).count();
    statBlocks.fetch_add (1, std::memory_order_relaxed);
    statSum.store (statSum.load (std::memory_order_relaxed) + us, std::memory_order_relaxed);
    if (us > statMax.load (std::memory_order_relaxed))
        statMax.store (us, std::memory_order_relaxed);
    return startSample;
}

AbletonLink::Stats AbletonLink::stats() const noexcept
{
    Stats s;
    s.blocks = statBlocks.load();
    s.meanUs = s.blocks > 0 ? statSum.load() / (double) s.blocks : 0.0;
    s.maxUs = statMax.load();
    return s;
}

void AbletonLink::resetStats() noexcept
{
    statBlocks.store (0);
    statSum.store (0.0);
    statMax.store (0.0);
}

std::shared_ptr<void> AbletonLink::makeTestPeer (double bpm)
{
    auto peer = std::make_shared<ableton::Link> (std::clamp (bpm, 20.0, 999.0));
    peer->enableStartStopSync (true);
    peer->enable (true);
    auto state = peer->captureAppSessionState();
    state.setIsPlayingAndRequestBeatAtTime (true, peer->clock().micros(), 0.0, 4.0);
    peer->commitAppSessionState (state);
    return std::shared_ptr<void> (peer, peer.get());
}

// --- --linktest ------------------------------------------------------------------------------------------------------

std::string AbletonLink::selfTest (double seconds, bool ownPeer)
{
    std::ostringstream out;
    out << std::fixed;
    constexpr double rate = 48000.0;
    constexpr int frames = 256;
    seconds = std::clamp (seconds, 4.0, 600.0);

    // the other peer: in this process (it starts first, so the session takes its tempo), or one on the network
    std::unique_ptr<ableton::Link> peer;
    if (ownPeer)
    {
        peer = std::make_unique<ableton::Link> (97.0);
        peer->enableStartStopSync (true);
        peer->enable (true);
        auto state = peer->captureAppSessionState();
        state.setIsPlayingAndRequestBeatAtTime (true, peer->clock().micros(), 0.0, 4.0);
        peer->commitAppSessionState (state);
    }

    AbletonLink lynxLink;
    Transport t;
    t.sampleRate.store (rate);
    t.bpm.store (120.0);
    t.projectBpm.store (120.0);
    t.songTempoNow.store (120.0);
    t.linkQuantum.store (4.0);
    lynxLink.setStartStopSync (true);
    lynxLink.deviceStarted (rate);
    out << "link=Ableton Link 4.1.1 own-peer=" << (ownPeer ? 1 : 0) << " quantum=4 rate=48000 frames=256 lynx-tempo=120\n";

    // simulated callbacks in real time (the host clock must advance as the samples do)
    const auto clockStart = std::chrono::steady_clock::now();
    const auto blocks = (std::int64_t) (seconds * rate / frames);
    auto pressedPlay = false, waitLogged = false, peerTempoSet = false, ownTempoSet = false, startLogged = false;
    double playPressedAt = -1.0;
    double worstMs = 0.0, sumMs = 0.0;
    int phaseSamples = 0;
    std::vector<double> callUs;
    callUs.reserve ((std::size_t) blocks);
    for (std::int64_t b = 0; b < blocks; ++b)
    {
        const auto due = clockStart + std::chrono::microseconds ((std::int64_t) (b * frames * 1.0e6 / rate));
        std::this_thread::sleep_until (due);
        const auto now = (double) b * frames / rate;

        if (! lynxLink.isEnabled() && now >= 1.0) // LYNX joins the session the other peer runs (its tempo wins)
        {
            lynxLink.setEnabled (true, 120.0);
            out << std::setprecision (3) << "t=" << now << " Link switched on in LYNX\n";
        }
        if (! pressedPlay && now >= 3.0) // the user presses Play after the session has formed
        {
            t.playing.store (true);
            pressedPlay = true;
            playPressedAt = now;
            out << std::setprecision (3) << "t=" << now << " play pressed; peers=" << lynxLink.peers() << " position=0\n";
        }
        if (ownPeer && ! peerTempoSet && now >= 7.0) // the other peer changes the tempo: LYNX must follow
        {
            auto state = peer->captureAppSessionState();
            state.setTempo (133.0, peer->clock().micros());
            peer->commitAppSessionState (state);
            peerTempoSet = true;
            out << "t=" << now << " the other peer sets 133 BPM\n";
        }
        if (! ownTempoSet && now >= seconds * 0.7) // LYNX's project tempo is edited: the peers must follow
        {
            t.projectBpm.store (110.0);
            t.songTempoNow.store (110.0);
            t.bpm.store (110.0);
            ownTempoSet = true;
            out << "t=" << now << " LYNX's project tempo set to 110 BPM\n";
        }

        // the UI's part of Start Stop Sync: a peer's start / stop presses Play / Stop
        if (! ownPeer && lynxLink.takeRemoteStop())
        {
            t.playing.store (false);
            t.linkWaiting.store (false);
            out << std::setprecision (3) << "t=" << now << " a peer stopped the session: LYNX's Stop pressed\n";
        }
        if (! ownPeer && lynxLink.takeRemoteStart())
        {
            t.playing.store (true);
            out << std::setprecision (3) << "t=" << now << " a peer started the session: LYNX's Play pressed\n";
        }
        const auto callStart = std::chrono::steady_clock::now();
        const auto start = lynxLink.beginBlock (t, frames, 0.0);
        if (lynxLink.isEnabled())
            callUs.push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - callStart).count());
        if (t.linkWaiting.load() && ! waitLogged)
        {
            out << "t=" << now << " start waits for the session's phase (progress " << std::setprecision (3) << lynxLink.startProgress() << ")\n";
            waitLogged = true;
        }
        if (start >= 0)
        {
            t.playing.store (true);
            if (! startLogged)
                out << "t=" << now << " playback starts at sample " << start << " of the block (waited "
                    << std::setprecision (3) << now - playPressedAt << " s)\n";
            startLogged = true;
        }
        // the transport advances like the engine's
        const auto tps = t.ticksPerSample();
        if (t.playing.load())
        {
            const auto n = start >= 0 ? frames - start : frames;
            t.positionTicks.store (t.positionTicks.load() + n * tps);
        }

        // the alignment: the session's beat at this block's end against LYNX's position (both on the phase)
        if (t.playing.load() && b % 47 == 0)
        {
            auto* p = lynxLink.impl.load();
            const auto state = p->link.captureAppSessionState();
            const auto hostEnd = p->filter.sampleTimeToHostTime ((double) lynxLink.rt.sampleTime);
            const auto sessionBeat = state.beatAtTime (hostEnd, 4.0);
            const auto local = t.positionTicks.load() / (double) ppq;
            const auto errorBeats = linksync::phaseError (sessionBeat, local, 4.0);
            const auto errorMs = errorBeats * 60000.0 / std::max (1.0, state.tempo());
            if (now > 1.0 + (playPressedAt > 0 ? playPressedAt : 0) && now < seconds - 0.1)
            {
                // (the first second after a tempo change is the catch-up; counted, but reported apart)
                worstMs = std::max (worstMs, std::abs (errorMs));
                sumMs += std::abs (errorMs);
                ++phaseSamples;
            }
            if (b % (47 * 8) == 0)
            {
                out << std::setprecision (3) << "t=" << now << " peers=" << lynxLink.peers() << " session=" << state.tempo()
                    << " lynx=" << t.linkBpm.load() << " phase error=" << std::setprecision (2) << errorMs << " ms";
                if (peer != nullptr)
                {
                    const auto ps = peer->captureAppSessionState();
                    out << " peer tempo=" << std::setprecision (3) << ps.tempo()
                        << " peer-vs-lynx phase=" << std::setprecision (2)
                        << linksync::phaseError (ps.beatAtTime (hostEnd, 4.0), local, 4.0) * 60000.0 / std::max (1.0, ps.tempo()) << " ms";
                }
                out << "\n";
            }
        }
    }

    const auto stats = lynxLink.stats();
    out << std::setprecision (3) << "result: peers=" << lynxLink.peers() << " session tempo=" << lynxLink.sessionTempo();
    if (peer != nullptr)
        out << " peer tempo=" << peer->captureAppSessionState().tempo();
    out << "\nresult: phase error after the start: mean=" << (phaseSamples > 0 ? sumMs / phaseSamples : 0.0) << " ms worst=" << worstMs
        << " ms (" << phaseSamples << " samples)\n";
    out << "audio thread per block (beginBlock): mean us=" << stats.meanUs << " max us=" << stats.maxUs << " blocks=" << stats.blocks;
    if (! callUs.empty())
    {
        std::sort (callUs.begin(), callUs.end());
        const auto at = [&callUs] (double q) { return callUs[std::min (callUs.size() - 1, (std::size_t) (q * (double) callUs.size()))]; };
        out << " p50 us=" << at (0.5) << " p99 us=" << at (0.99) << " (block = " << frames * 1.0e6 / rate << " us)";
    }
    out << "\n";

    // Start Stop Sync: the other peer stops; LYNX's UI is asked to press Stop
    if (peer != nullptr)
    {
        auto state = peer->captureAppSessionState();
        state.setIsPlaying (false, peer->clock().micros());
        peer->commitAppSessionState (state);
        auto asked = false;
        for (int b = 0; b < 400 && ! asked; ++b)
        {
            std::this_thread::sleep_for (std::chrono::microseconds ((std::int64_t) (frames * 1.0e6 / rate)));
            lynxLink.beginBlock (t, frames, 0.0);
            asked = lynxLink.takeRemoteStop();
        }
        out << "start stop sync: the peer's stop " << (asked ? "reached LYNX (Stop pressed)" : "did NOT reach LYNX") << "\n";
        peer->enable (false);
    }
    lynxLink.setEnabled (false, 120.0);
    return out.str();
}
} // namespace lynx
