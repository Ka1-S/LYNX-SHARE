#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

#include "core/MarkerPlan.h"
#include "core/Song.h"

namespace lynx
{
// Transport state shared between the UI and the audio thread.
// Atomics only: the audio thread never blocks waiting for the UI.
// The position (in ticks, core PPQ) is written only by the audio thread.
struct Transport
{
    std::atomic<bool> playing { false };
    std::atomic<bool> metronome { false };
    // the metronome's sound in FL's Metronome sound menu order (core/MetronomeVoice.h, REA round 165z-a2): 0 Hi-hat,
    // 1 Tick (FL's default), 2 Beep, 3 Cowbell, 4 Pulse
    std::atomic<int> metronomeSound { 1 };
    // LYNX's metronome volume trim (0..1.25; 1 = FL's level). FL has none: its click is set by the mixer track it
    // plays through. Kept so a volume set before 0.1.20 still applies.
    std::atomic<float> metronomeVolume { 1.0f };
    // FL's Settings › Audio › Mixer › "Metronome track": 0 = "---" (the Master), n = insert n
    std::atomic<int> metronomeTrack { 0 };
    // FL's Settings › Audio › Mixer (REA_rodada167j §2–§3): "Play truncated notes on transport" (FL's default: on) and
    // "Reset plugins on transport" (FL's default: off)
    std::atomic<bool> playTruncatedOnTransport { true };
    std::atomic<bool> resetPluginsOnTransport { false };
    // raised each time the user moves the position (seek, rewind, quantized / marker jump): the renderer tells those from
    // the loop's wrap (round 167k, "Play truncated notes on transport" after a jump). Audio thread only.
    std::atomic<unsigned> userJumps { 0 };
    std::atomic<bool> recording { false };
    std::atomic<double> bpm { 130.0 };
    // External clock sync (round 165f): > 0 = the tempo LYNX follows from an incoming MIDI clock, over the song's
    // (written by the audio thread only)
    std::atomic<double> externalBpm { 0.0 };
    // Ableton Link (round 190gl, engine/AbletonLink.h): > 0 = the tempo LYNX plays at in a Link session (the session's,
    // nudged onto its phase), over the song's; an external clock still wins (written by the audio thread only)
    std::atomic<double> linkBpm { 0.0 };
    // Play was pressed in a Link session with other peers: playback waits for the session's phase (core/LinkSync.h); the
    // audio thread starts it, a Stop cancels it
    std::atomic<bool> linkWaiting { false };
    // the tempo the song plays at without an override (its automation and tempo events) and whether it has any:
    // written by the rack's processor each block, read by the Link session (which proposes it to the peers)
    std::atomic<double> songTempoNow { 0.0 };
    std::atomic<bool> songTempoMap { false };
    // the project's tempo (Song::bpm) and the Link quantum in beats (the clip grid's global launch quantisation,
    // core/LinkSync.h), published with the song
    std::atomic<double> projectBpm { 130.0 };
    std::atomic<double> linkQuantum { 4.0 };
    std::atomic<int> beatsPerBar { 4 };
    std::atomic<int> beatUnit { 4 }; // (round 190fg: the meter's denominator: the metronome clicks its beats)

    // Playlist time-signature changes for the metronome (song mode): the bar restarts at each
    // `signatureTick[i]` with `signatureLength[i]` ticks, clicked in beats of `signatureBeat[i]` ticks (round 190fg:
    // a 6/8 bar clicks six eighths). The UI writes under a "seqlock": odd version while writing; readers check the
    // version before and after.
    static constexpr int maxSignatures = 64;
    std::array<std::atomic<std::int64_t>, maxSignatures> signatureTick {}, signatureLength {}, signatureBeat {};
    std::atomic<int> signatureCount { 0 };
    std::atomic<std::uint32_t> signatureVersion { 0 };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<double> positionTicks { 0.0 };
    // Count-in before recording: ticks of pre-roll left. While > 0 playback has not started (`playing` is
    // false), the metronome clicks the count and the audio thread counts it down, then starts playing.
    std::atomic<double> countIn { 0.0 };
    std::atomic<bool> rewindRequested { false };
    std::atomic<double> seekRequest { -1.0 }; // < 0 = none; the audio thread applies it on the next block

    // Playlist loop region (song mode only). end <= start = disabled.
    std::atomic<bool> songMode { false };
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };
    std::atomic<double> songEnd { 0.0 }; // Song::songLength(): without a loop region Song mode loops there
    std::atomic<double> songStart { 0.0 }; // Performance mode: the song loops back to its Start marker (round 165m)

    // The Playlist markers' actions (core/MarkerPlan.h) for the audio thread, published by the UI under a seqlock
    // like the signatures above (odd version while writing; the audio thread keeps its last good copy).
    std::array<std::atomic<std::int64_t>, MarkerPlan::maxJumps> markerAt {}, markerTo {};
    std::array<std::atomic<int>, MarkerPlan::maxJumps> markerKind {};
    std::atomic<int> markerCount { 0 };
    std::atomic<std::int64_t> markerWrapTo { 0 }, markerStart { -1 }, markerSongEnd { 0 };
    std::atomic<std::uint32_t> markerVersion { 0 };

    void publishMarkers (const MarkerPlan& plan) noexcept
    {
        markerVersion.fetch_add (1, std::memory_order_acq_rel); // odd: writing
        const auto count = std::min (plan.count, MarkerPlan::maxJumps);
        for (int i = 0; i < count; ++i)
        {
            markerAt[(std::size_t) i].store (plan.jumps[(std::size_t) i].at, std::memory_order_relaxed);
            markerTo[(std::size_t) i].store (plan.jumps[(std::size_t) i].to, std::memory_order_relaxed);
            markerKind[(std::size_t) i].store ((int) plan.jumps[(std::size_t) i].kind, std::memory_order_relaxed);
        }
        markerCount.store (count, std::memory_order_relaxed);
        markerWrapTo.store (plan.wrapTo, std::memory_order_relaxed);
        markerStart.store (plan.start, std::memory_order_relaxed);
        markerSongEnd.store (plan.songEnd, std::memory_order_relaxed);
        markerVersion.fetch_add (1, std::memory_order_acq_rel); // even: ready
    }

    // audio thread: refreshes `plan` when a new table is complete (false = kept as it was)
    bool readMarkers (MarkerPlan& plan, std::uint32_t& version) const noexcept
    {
        const auto before = markerVersion.load (std::memory_order_acquire);
        if ((before & 1u) != 0 || before == version)
            return false;
        MarkerPlan copy;
        copy.count = std::clamp (markerCount.load (std::memory_order_relaxed), 0, MarkerPlan::maxJumps);
        for (int i = 0; i < copy.count; ++i)
            copy.jumps[(std::size_t) i] = { markerAt[(std::size_t) i].load (std::memory_order_relaxed),
                                            (MarkerJump::Kind) markerKind[(std::size_t) i].load (std::memory_order_relaxed),
                                            markerTo[(std::size_t) i].load (std::memory_order_relaxed) };
        copy.wrapTo = markerWrapTo.load (std::memory_order_relaxed);
        copy.start = markerStart.load (std::memory_order_relaxed);
        copy.songEnd = markerSongEnd.load (std::memory_order_relaxed);
        if (markerVersion.load (std::memory_order_acquire) != before)
            return false;
        plan = copy;
        version = before;
        return true;
    }

    // A jump asked by the user while playing (marker click, keyboard, MIDI "Song marker jump"): the audio thread
    // makes it when the position reaches `pendingJumpAt` (FL's "Quantize song jumps"; < 0 = none).
    std::atomic<double> pendingJumpAt { -1.0 }, pendingJumpTo { -1.0 };

    // The tempo that replaces the song's now: an external MIDI clock's, else the Link session's (0 = none).
    double overrideBpm() const noexcept
    {
        const auto external = externalBpm.load (std::memory_order_relaxed);
        return external > 0.0 ? external : linkBpm.load (std::memory_order_relaxed);
    }

    double ticksPerSample() const noexcept
    {
        const auto over = overrideBpm();
        return (over > 0.0 ? over : bpm.load (std::memory_order_relaxed)) * ppq / (60.0 * sampleRate.load (std::memory_order_relaxed));
    }

    double positionInBeats() const noexcept
    {
        return positionTicks.load (std::memory_order_relaxed) / ppq;
    }
};
} // namespace lynx
