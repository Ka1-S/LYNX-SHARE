#pragma once

// Ableton Link support (round 190gl): the rules that tie LYNX's transport to a Link session, without Link itself (the
// network part is engine/AbletonLink.cpp, around Ableton's Link library in third_party/link). Pure functions and one
// small state machine, so the core tests check them; the audio thread runs them (no allocation, no locks).
//
// What the session-style DAW does (its manual, "Synchronizing with Link, Tempo Follower and MIDI", Live 12; shots in
// /mnt/e/lynx-work/link190gl/ev):
//   - the first peer in a session sets its tempo; any peer may change it later and every peer follows;
//   - beats and phase are aligned to the session over a quantum = the set's global launch quantisation;
//   - with another peer connected, Play waits until the session reaches the phase the song starts at (the position
//     display shows the wait as a progress bar);
//   - "Start Stop Sync" shares Play / Stop with the peers that have it on;
//   - a tempo change made by another peer overrides the set's tempo automation.
//
// LYNX's tempo rules (decided in round 190gl; the reference DAW's choice where it states one):
//   1. while Link is on, LYNX plays the session's tempo;
//   2. a tempo LYNX sets itself goes to the session: an edit of the project tempo (the tempo field, tap tempo, a scene
//      tempo, a MIDI-mapped tempo control), or LYNX's own tempo automation / FL tempo events while the song plays;
//   3. a tempo change made by another peer overrides LYNX's tempo automation until playback starts again (as the
//      reference DAW: "Tempo changes made by a Link session participant will override tempo automation");
//   4. an external MIDI clock LYNX follows wins: its tempo goes to the session (the reference DAW does the same with
//      its external sync), and LYNX does not move its position to the session's phase (the clock owns it).

#include <algorithm>
#include <cmath>
#include <string>

#include "core/Song.h"

namespace lynx::linksync
{
// The Link quantum in beats (quarter notes) for the clip grid's global launch quantisation: one bar = beatsPerBar
// beats. "None" and quantisations under one beat give 1 beat.
inline double quantumBeats (LaunchQuant globalQuant, int beatsPerBar) noexcept
{
    const auto bar = (double) std::max (1, beatsPerBar);
    switch (globalQuant)
    {
        case LaunchQuant::bars8: return 8.0 * bar;
        case LaunchQuant::bars4: return 4.0 * bar;
        case LaunchQuant::bars2: return 2.0 * bar;
        case LaunchQuant::bar: return bar;
        case LaunchQuant::half: return 2.0;
        case LaunchQuant::global:
        case LaunchQuant::none:
        case LaunchQuant::halfTriplet:
        case LaunchQuant::quarter:
        case LaunchQuant::quarterTriplet:
        case LaunchQuant::eighth:
        case LaunchQuant::eighthTriplet:
        case LaunchQuant::sixteenth:
        case LaunchQuant::sixteenthTriplet:
        case LaunchQuant::thirtySecond: return 1.0;
    }
    return 1.0;
}

// The phase of `beats` in [0, quantum) (negative beats too).
inline double phase (double beats, double quantum) noexcept
{
    if (quantum <= 0.0)
        return 0.0;
    const auto p = std::fmod (beats, quantum);
    return p < 0.0 ? p + quantum : (p >= quantum ? 0.0 : p);
}

// How far LYNX's clock is behind the session (beats; negative = ahead), taken on the phase only and wrapped into
// (-quantum / 2, quantum / 2]: the shortest way to the session's phase.
inline double phaseError (double sessionBeats, double localBeats, double quantum) noexcept
{
    if (quantum <= 0.0)
        return 0.0;
    auto e = phase (sessionBeats, quantum) - phase (localBeats, quantum);
    if (e > quantum / 2.0)
        e -= quantum;
    else if (e <= -quantum / 2.0)
        e += quantum;
    return e;
}

// The session beat at or after `fromBeat` whose phase is `targetPhase`: where a start waits for (tests and the
// position display's progress bar).
inline double nextBeatWithPhase (double fromBeat, double targetPhase, double quantum) noexcept
{
    if (quantum <= 0.0)
        return fromBeat;
    auto d = phase (targetPhase, quantum) - phase (fromBeat, quantum);
    if (d < 0.0)
        d += quantum;
    if (d >= quantum - 1.0e-9) // (the same phase, to rounding)
        d = 0.0;
    return fromBeat + d;
}

// How LYNX keeps its running clock on the session while it plays.
struct FollowLaw
{
    double deadBand = 0.0005;    // beats: closer than this, the session tempo as it is
    double jumpBeats = 0.05;     // beats: further than this, the clock jumps (about 25 ms at 120 BPM)
    double catchUpSeconds = 0.5; // a smaller error closes over about this long...
    double maxNudge = 0.03;      // ...with the tempo at most 3 % off the session's
};

struct Follow
{
    double bpm = 120.0; // the tempo to play this block at
    double jump = 0.0;  // != 0: move the clock by this many beats first (then play at the session tempo)
};

// `error` = phaseError (session, local): positive = LYNX is behind.
inline Follow follow (double sessionBpm, double error, const FollowLaw& law = {}) noexcept
{
    Follow f;
    f.bpm = sessionBpm;
    if (std::abs (error) > law.jumpBeats)
    {
        f.jump = error;
        return f;
    }
    if (std::abs (error) <= law.deadBand)
        return f;
    const auto extra = 60.0 * error / std::max (0.05, law.catchUpSeconds); // BPM that covers the error in time
    const auto limit = sessionBpm * law.maxNudge;
    f.bpm = sessionBpm + std::clamp (extra, -limit, limit);
    return f;
}

// Two tempos Link treats as one: peers exchange a tempo as whole microseconds per beat, so 110 BPM comes back from
// the network as 60e6 / 545455 = 109.99991 BPM. Within a microsecond per beat = the same tempo.
inline bool sameTempo (double a, double b) noexcept
{
    if (a <= 0.0 || b <= 0.0)
        return a == b;
    return std::abs (60.0e6 / a - 60.0e6 / b) <= 1.0;
}

// The tempo as shown and kept by LYNX: to a thousandth of a BPM (109.99991 from the network shows as 110.000).
inline double displayTempo (double bpm) noexcept
{
    return std::round (bpm * 1000.0) / 1000.0;
}

// Tempo arbitration between LYNX's own tempo and the session's (rules 1-4 above). Audio thread, once per block.
class TempoArbiter
{
public:
    struct Input
    {
        double sessionBpm = 120.0;  // the session's tempo now
        double projectBpm = 120.0;  // the project's tempo (Song::bpm, as the user set it)
        double songBpm = 120.0;     // the tempo the song plays at now without Link (its automation / tempo events)
        bool tempoMap = false;      // the song has tempo automation or tempo events
        bool playing = false;
        bool started = false;       // playback started at this block
        double externalBpm = 0.0;   // > 0: an external MIDI clock LYNX follows (rule 4)
    };
    struct Output
    {
        double propose = 0.0;       // > 0: commit this tempo to the session
        double play = 120.0;        // the tempo LYNX plays at (before the phase nudge)
    };

    // Link was switched on: the session starts from (or is joined with) these values
    void reset (const Input& in) noexcept
    {
        lastProject = in.projectBpm;
        lastSong = in.songBpm;
        lastSession = in.sessionBpm;
        overridden = false;
        remoteChanged = false;
    }

    Output step (const Input& in) noexcept
    {
        Output out;
        out.play = in.sessionBpm;
        // another peer's change: whatever the session says now that we did not propose (rule 3)
        if (! sameTempo (in.sessionBpm, lastSession))
        {
            remoteChanged = true;
            if (in.playing && in.tempoMap)
                overridden = true;
        }
        if (in.started)
            overridden = false; // (rule 3: until playback starts again)

        auto propose = 0.0;
        if (in.externalBpm > 0.0) // rule 4: the clock's tempo, whatever the peers set
            propose = in.externalBpm;
        else if (std::abs (in.projectBpm - lastProject) > eps) // rule 2: the project's tempo was edited
            propose = in.projectBpm;
        else if (in.tempoMap && in.playing && ! overridden && (std::abs (in.songBpm - lastSong) > eps || in.started))
            propose = in.songBpm;  // rule 2: the song's own tempo automation while it plays

        lastProject = in.projectBpm;
        lastSong = in.songBpm;
        if (propose > 0.0 && ! sameTempo (propose, in.sessionBpm))
        {
            out.propose = propose;
            out.play = propose;
            remoteChanged = false;
        }
        lastSession = out.play;
        return out;
    }

    bool automationOverridden() const noexcept { return overridden; }
    // a peer changed the tempo after LYNX last set it (the project takes that tempo when Link is switched off)
    bool changedByPeer() const noexcept { return remoteChanged; }

private:
    static constexpr double eps = 1.0e-6;
    double lastProject = 0.0, lastSong = 0.0, lastSession = 0.0;
    bool overridden = false, remoteChanged = false;
};

// The control-bar toggle's text, as the reference DAW: "Link" while off, then the number of peers ("1 Link",
// "2 Links"; "0 Links" alone). English; the UI translates the two forms.
inline std::string toggleText (bool enabled, int peers)
{
    if (! enabled)
        return "Link";
    return std::to_string (std::max (0, peers)) + (peers == 1 ? " Link" : " Links");
}
} // namespace lynx::linksync
