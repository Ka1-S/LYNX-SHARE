#pragma once

// Smart Tempo (round 190pf, research190oe P12): the project's tempo follows a free recording. A LYNX extra in the
// manner of Logic Pro's Smart Tempo "Adapt" (Apple's guide: the project tempo adapts to a recording made without the
// metronome; the recording keeps its timing and the bar and beat grid bends to it). FL users ask for "a real tempo lane
// that recalibrates the grid" (research190oe F6). The audio clip menu's LYNX extras › "Make the project tempo follow this
// clip..." tracks the clip's beats, shows the bars and their tempo in a preview with a Smooth amount, then writes tempo
// markers (core/TempoMarkers.h: a tempo automation clip, so .flp exports, older LYNX and FL play the same tempo map), one
// per bar, glide off, and leaves the clip unwarped so its audio plays exactly as recorded. MIDI recorded afterwards lands
// on the bent grid like any other tempo map. Live's "Keep" counterpart (the project keeps its tempo, the audio is
// warped) is LYNX's Auto-warp (core/AutoWarp.h).
//
// Clean-room: the beat tracker is LYNX's own (Ellis 2007 dynamic programming over core/TempoDetect's onset curve, as
// core/AutoWarp does, run twice: a loose pass for the local tempo, then a tight one that follows it; each beat moved onto
// the attack under it, core/TimeStretch's detector). Patent desk check (/mnt/e/lynx-work/190pf/HANDOFF.md): Apple's
// US 11,625,216 B2 claims tempo / downbeat analysis of a chosen subset of several tracks summed DURING a multitrack
// recording, its result stored for every track; Apple's US 7,952,012 B2 claims an audio file with its own variable tempo
// (a ramp) independent of the global tempo. Design rules kept here: ONE clip, analysed after it was recorded; the clip
// gets no tempo of its own (it is unwarped, the PROJECT tempo changes).
//
// Header only, no JUCE; the analysis reads audio (a worker thread in the UI). Tested in tests/ChordLane190pfTests.inc.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/Song.h"
#include "core/TempoDetect.h"
#include "core/TempoMap.h"
#include "core/TempoMarkers.h"
#include "core/TimeStretch.h"

namespace lynx::smarttempo
{
// --- the analysis --------------------------------------------------------------------------------------------------------
struct Options
{
    int beatsPerBar = 4;     // pulses per bar (the meter's numerator where the clip starts)
    int downbeatShift = 0;   // the bars start this many beats after the first beat (the preview's "Bar 1 starts on beat")
    double tempoFactor = 1.0; // 0.5 / 2: the detector's pulse taken at half / double speed (the preview's Half / Double)
};

struct Analysis
{
    std::vector<double> beats; // seconds in the analysed audio, increasing
    std::vector<double> bars;  // seconds of the downbeats (every beatsPerBar-th beat from the first downbeat)
    double bpm = 0.0;          // the detector's overall tempo (after tempoFactor)
    bool ok() const noexcept { return bars.size() >= 2; }
};

namespace detail
{
// Ellis' dynamic programming beat tracker over `curve` (frames of `hop` seconds) with the beat period `tau(i)` (frames) at
// frame i; `tightness` is the penalty on a beat interval away from it. The beats, in frames.
template <typename Tau>
inline std::vector<long> track (const std::vector<float>& curve, Tau&& tau, double tightness)
{
    const auto n = (long) curve.size();
    std::vector<double> score ((std::size_t) n, 0.0);
    std::vector<long> from ((std::size_t) n, -1);
    double norm = 0.0;
    for (const auto v : curve)
        norm = std::max (norm, (double) v);
    norm = norm > 0.0 ? 1.0 / norm : 1.0;
    for (long i = 0; i < n; ++i)
    {
        const auto t = std::max (2.0, tau (i));
        double best = 0.0;
        long arg = -1;
        for (auto j = i - (long) std::ceil (1.6 * t); j <= i - (long) std::floor (0.6 * t); ++j)
        {
            if (j < 0)
                continue;
            const auto lg = std::log ((double) (i - j) / t);
            const auto v = score[(std::size_t) j] - tightness * lg * lg;
            if (arg < 0 || v > best)
            {
                best = v;
                arg = j;
            }
        }
        score[(std::size_t) i] = (double) curve[(std::size_t) i] * norm + (arg >= 0 ? best : 0.0);
        from[(std::size_t) i] = arg;
    }
    std::vector<long> beats;
    if (n == 0)
        return beats;
    long end = n - 1;
    for (auto i = std::max (0L, n - (long) std::ceil (tau (n - 1))); i < n; ++i)
        if (score[(std::size_t) i] > score[(std::size_t) end])
            end = i;
    for (auto i = end; i >= 0; i = from[(std::size_t) i])
        beats.push_back (i);
    std::reverse (beats.begin(), beats.end());
    return beats;
}
} // namespace detail

// The beats and bars of mono audio. Short or pulseless audio: an Analysis without bars.
inline Analysis analyse (const float* mono, long frames, double sampleRate, const Options& o = {})
{
    Analysis a;
    if (mono == nullptr || frames < 16 || ! (sampleRate > 0.0))
        return a;
    tempodetect::Options d;
    d.snapToLength = false;
    const auto detected = tempodetect::detect (mono, nullptr, (std::size_t) frames, sampleRate, d);
    if (! (detected.bpm > 0.0))
        return a;
    a.bpm = detected.bpm * std::clamp (o.tempoFactor, 0.25, 4.0);
    const auto hop = (double) std::max<long long> (16, std::llround (0.005 * sampleRate)) / sampleRate;
    const auto curve = tempodetect::onsetCurve (mono, nullptr, (std::size_t) frames, sampleRate, hop);
    if (curve.size() < 8)
        return a;
    const auto tau0 = 60.0 / detected.bpm / hop; // (the detector's own pulse: half / double are taken from its beats below)

    // pass 1: the overall tempo, loosely (a tempo that drifts by a fifth still tracks)
    const auto loose = detail::track (curve, [tau0] (long) { return tau0; }, 8.0);
    // the local beat period: the median of the nine intervals around each beat of pass 1, between them interpolated
    std::vector<double> at, period;
    for (std::size_t k = 1; k < loose.size(); ++k)
    {
        std::vector<double> win;
        for (auto j = k > 4 ? k - 4 : 1; j <= std::min (loose.size() - 1, k + 4); ++j)
            win.push_back ((double) (loose[j] - loose[j - 1]));
        std::nth_element (win.begin(), win.begin() + (long) win.size() / 2, win.end());
        const auto m = win[win.size() / 2];
        // (a beat the loose pass skipped or doubled shows as an interval near 2x or 0.5x: the overall period instead)
        at.push_back (0.5 * (double) (loose[k] + loose[k - 1]));
        period.push_back (m > 1.5 * tau0 || m < 0.66 * tau0 ? tau0 : m);
    }
    const auto tauAt = [&at, &period, tau0] (long i) {
        if (at.empty())
            return tau0;
        if ((double) i <= at.front())
            return period.front();
        if ((double) i >= at.back())
            return period.back();
        const auto it = std::upper_bound (at.begin(), at.end(), (double) i);
        const auto k = (std::size_t) (it - at.begin());
        const auto f = ((double) i - at[k - 1]) / std::max (1.0, at[k] - at[k - 1]);
        return period[k - 1] + f * (period[k] - period[k - 1]);
    };
    // pass 2: tight, following the local tempo
    const auto tight = detail::track (curve, tauAt, 100.0);

    // each beat onto the attack within a sixth of its beat (the onset curve's peaks are broad)
    const auto attacks = stretchdetail::detectAttacks (mono, frames, sampleRate, 6.0);
    std::vector<double> attackSeconds;
    for (const auto x : attacks)
        attackSeconds.push_back ((double) x / sampleRate);
    double peak = 0.0;
    for (long i = 0; i < frames; ++i)
        peak = std::max (peak, (double) std::abs (mono[i]));
    // the first and last sound (beats the tracker put in leading or trailing silence go)
    double first = 0.0, last = (double) frames / sampleRate;
    if (! attackSeconds.empty())
    {
        first = attackSeconds.front();
        last = attackSeconds.back();
    }
    std::size_t next = 0;
    for (const auto b : tight)
    {
        const auto local = tauAt (b) * hop;
        auto t = (double) b * hop;
        if (t < first - 0.5 * local || t > last + 0.5 * local)
            continue;
        while (next < attackSeconds.size() && attackSeconds[next] < t - local / 6.0)
            ++next;
        auto bestD = local / 6.0;
        for (auto k = next; k < attackSeconds.size() && attackSeconds[k] <= t + local / 6.0; ++k)
            if (std::abs (attackSeconds[k] - t) < bestD)
            {
                bestD = std::abs (attackSeconds[k] - t);
                t = attackSeconds[k];
            }
        if (a.beats.empty() || t > a.beats.back() + 0.3 * local)
            a.beats.push_back (t);
    }
    // half / double: every other beat, or a beat between each two
    if (o.tempoFactor < 0.75 && a.beats.size() > 2)
    {
        std::vector<double> half;
        for (std::size_t k = 0; k < a.beats.size(); k += 2)
            half.push_back (a.beats[k]);
        a.beats = std::move (half);
    }
    else if (o.tempoFactor > 1.5 && a.beats.size() > 1)
    {
        std::vector<double> twice;
        for (std::size_t k = 0; k + 1 < a.beats.size(); ++k)
        {
            twice.push_back (a.beats[k]);
            twice.push_back (0.5 * (a.beats[k] + a.beats[k + 1]));
        }
        twice.push_back (a.beats.back());
        a.beats = std::move (twice);
    }
    const auto per = std::max (1, o.beatsPerBar);
    for (auto k = (std::size_t) std::max (0, o.downbeatShift); k < a.beats.size(); k += (std::size_t) per)
        a.bars.push_back (a.beats[k]);
    return a;
}

// --- the tempo map of the markers ----------------------------------------------------------------------------------------
// The project's tempo as the tempo markers set it: a start tempo, then each marker's (held, or ramped into: glide). Smart
// Tempo works on it only when nothing else moves the tempo (blockedBy).
struct Map
{
    double startBpm = 120.0;
    std::vector<tempomarkers::Mark> marks; // sorted by tick

    static Map of (const Song& song) { return { tempomarkers::startTempo (song), tempomarkers::list (song) }; }

    // seconds from tick 0 to `tick`
    double seconds (double tick) const noexcept
    {
        double s = 0.0, at = 0.0, bpm = startBpm;
        for (std::size_t i = 0; i < marks.size() && at < tick; ++i)
        {
            const auto t = (double) marks[i].tick;
            const auto to = std::min (t, tick);
            if (to > at)
                s += segment (at, to, t, bpm, marks[i]);
            at = std::max (at, std::min (t, tick));
            if (t <= tick)
                bpm = marks[i].bpm;
        }
        if (tick > at)
            s += (tick - at) / (double) ppq * 60.0 / bpm;
        return s;
    }

    // the tick at `secs` seconds from tick 0 (bisection: the map is monotonic)
    double tickAt (double secs) const noexcept
    {
        double lo = 0.0, hi = (double) ppq;
        while (seconds (hi) < secs && hi < 1.0e12)
            hi *= 2.0;
        for (int i = 0; i < 64; ++i)
        {
            const auto mid = 0.5 * (lo + hi);
            (seconds (mid) < secs ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

private:
    // the seconds of [a, b) inside the segment that ends at marker `m` (at tick `end`), from tempo `bpm`
    static double segment (double a, double b, double end, double bpm, const tempomarkers::Mark& m) noexcept
    {
        if (! m.glide)
            return (b - a) / (double) ppq * 60.0 / bpm;
        // a straight ramp in BPM over the segment: ∫ dt / bpm(t), bpm linear in ticks — the segment starts where the
        // previous marker is, which the caller passes as the start of [a, b) only at the first piece; that is the case
        // for every call here (the walk visits segments whole or cut at the end)
        const auto len = std::max (1.0, end - a);
        const auto b0 = bpm, b1 = m.bpm;
        const auto f = (b - a) / len;
        const auto bb = b0 + (b1 - b0) * f;
        const auto sec = [] (double ticks) { return ticks / (double) ppq * 60.0; };
        if (std::abs (b1 - b0) < 1.0e-9)
            return sec (b - a) / b0;
        return sec (len) / (b1 - b0) * std::log (bb / b0);
    }
};

// Whether something other than the tempo markers moves the tempo (a tempo automation clip of its own, pattern Tempo
// events): Smart Tempo then writes nothing (the preview says so)
inline bool blockedBy (const Song& song)
{
    if (tempomap::hasTempoEvents (song))
        return true;
    const auto markers = tempomarkers::automationIndex (song);
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::automation && c.automation >= 0 && c.automation < (int) song.automations.size() && c.automation != markers
            && song.automations[(std::size_t) c.automation].target.kind == AutomationTargetKind::tempo)
            return true;
    return false;
}

// --- the plan: what "follow this clip" writes ------------------------------------------------------------------------------
struct Plan
{
    bool ok = false;
    std::int64_t clipStart = 0, clipLength = 0; // the clip's new place (moved by at most half a bar) and length
    std::int64_t firstBar = 0;                  // the bar line the first downbeat lands on
    std::vector<tempomarkers::Mark> marks;      // one per bar (the last tempo then holds)
    std::vector<double> rawBpm;                 // the bars' tempos before smoothing (the preview's graph)
    double maxErrorMs = 0.0;                    // the farthest a bar line ends from its downbeat (0 without smoothing)
    bool unwarp = false;                        // the clip's channel was stretched to the tempo: it plays unstretched
};

// The seconds the clip's offset skips in its file (the scheduler's rule: offset ticks at the project tempo)
inline double offsetSeconds (const Song& song, const Clip& clip) noexcept
{
    return ((double) clip.offset + (double) clip.offsetFraction) * 60.0 / (std::max (1.0, song.bpm) * ppq);
}

// Weighted moving average of the bar tempos over `radius` bars on each side (triangular weights)
inline std::vector<double> smoothed (const std::vector<double>& bpm, int radius)
{
    if (radius <= 0)
        return bpm;
    std::vector<double> out (bpm.size());
    for (int i = 0; i < (int) bpm.size(); ++i)
    {
        double sum = 0.0, w = 0.0;
        for (int j = std::max (0, i - radius); j <= std::min ((int) bpm.size() - 1, i + radius); ++j)
        {
            const auto k = (double) (radius + 1 - std::abs (i - j));
            sum += k * bpm[(std::size_t) j];
            w += k;
        }
        out[(std::size_t) i] = sum / w;
    }
    return out;
}

// What following clip `clipIndex` writes: `bars` = the downbeats in seconds of the clip's FILE (Analysis::bars),
// `fileSeconds` = the file's length; `smooth` 0..1 (0: every bar line on its downbeat; 1: tempos averaged over four bars
// each side).
inline Plan plan (const Song& song, int clipIndex, const std::vector<double>& bars, double fileSeconds, double smooth = 0.0)
{
    Plan p;
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size() || bars.size() < 2 || blockedBy (song))
        return p;
    const auto& clip = song.playlist[(std::size_t) clipIndex];
    if (clip.kind != ClipKind::audio || clip.channel < 0 || clip.channel >= (int) song.channels.size())
        return p;
    p.unwarp = song.channels[(std::size_t) clip.channel].sampler.stretchBeats > 0.0f || std::abs (clip.stretch - 1.0) > 1.0e-9;
    const auto map = Map::of (song);
    const auto off = offsetSeconds (song, clip);
    // the downbeats after the clip's start, in seconds from it
    std::vector<double> rel;
    for (const auto b : bars)
        if (b - off >= 0.0)
            rel.push_back (b - off);
    if (rel.size() < 2)
        return p;
    const auto clipSeconds = map.seconds ((double) clip.start);
    const auto d0 = clipSeconds + rel.front(); // where the first downbeat sounds now
    // the bar line nearest to it (the clip moves by at most half a bar; never before the song's start)
    std::int64_t best = -1;
    double bestD = 1.0e300;
    for (auto t = song.barAt (std::max<std::int64_t> (0, clip.start - 1)).tick;; )
    {
        const auto ts = map.seconds ((double) t);
        if (ts - rel.front() >= 0.0 && std::abs (ts - d0) < bestD)
        {
            bestD = std::abs (ts - d0);
            best = t;
        }
        if (ts > d0 + 1.0e-9 && best >= 0)
            break;
        const auto len = std::max<std::int64_t> (1, song.barAt (t).length);
        t += len;
        if (t > clip.start + 64 * (std::int64_t) ppq * 16)
            break;
    }
    if (best < 0)
        return p;
    p.firstBar = best;
    p.clipStart = std::max<std::int64_t> (0, (std::int64_t) std::llround (map.tickAt (map.seconds ((double) best) - rel.front())));

    // a tempo per bar: the bar's quarter notes over the time between its downbeat and the next one
    std::vector<std::int64_t> barTicks { best };
    std::vector<double> quarters;
    for (std::size_t k = 0; k + 1 < rel.size(); ++k)
    {
        const auto len = std::max<std::int64_t> (1, song.barAt (barTicks.back()).length);
        quarters.push_back ((double) len / (double) ppq);
        p.rawBpm.push_back (std::clamp (60.0 * quarters.back() / std::max (1.0e-6, rel[k + 1] - rel[k]), minTempo, maxTempo));
        barTicks.push_back (barTicks.back() + len);
    }
    const auto bpm = smoothed (p.rawBpm, (int) std::lround (std::clamp (smooth, 0.0, 1.0) * 4.0));
    for (std::size_t k = 0; k < bpm.size(); ++k)
        p.marks.push_back ({ barTicks[k], bpm[k], false });

    // the new map: the markers before the first bar line stay, the rest are replaced
    Map next;
    next.startBpm = map.startBpm;
    for (const auto& m : map.marks)
        if (m.tick < best)
            next.marks.push_back (m);
    for (const auto& m : p.marks)
        next.marks.push_back (m);
    // how far the bar lines end from their downbeats (smoothing)
    const auto start = next.seconds ((double) p.clipStart);
    for (std::size_t k = 0; k < barTicks.size() && k < rel.size(); ++k)
        p.maxErrorMs = std::max (p.maxErrorMs, 1000.0 * std::abs (next.seconds ((double) barTicks[k]) - start - rel[k]));
    // the clip lasts what it played before (unwarped: no longer than its file from the offset)
    auto playedSeconds = map.seconds ((double) clip.end()) - clipSeconds;
    if (p.unwarp || fileSeconds > 0.0)
        playedSeconds = std::min (playedSeconds, std::max (0.0, fileSeconds - off));
    if (p.unwarp)
        playedSeconds = std::max (0.0, fileSeconds - off);
    p.clipLength = std::max<std::int64_t> (1, (std::int64_t) std::llround (next.tickAt (start + playedSeconds)) - p.clipStart);
    p.ok = ! p.marks.empty();
    return p;
}

// Writes the plan (inside AudioEngine::editSong: one undo step): the tempo markers from its first bar line on are replaced
// by its own, the clip takes its place and length and plays unwarped. False when the song changed under the plan.
inline bool apply (Song& song, int clipIndex, const Plan& p)
{
    if (! p.ok || clipIndex < 0 || clipIndex >= (int) song.playlist.size() || song.playlist[(std::size_t) clipIndex].kind != ClipKind::audio)
        return false;
    // the old markers from the first bar line on go (the last one would take the automation with it: those go after)
    for (int i = (int) tempomarkers::list (song).size() - 1; i >= 0; --i)
        if (tempomarkers::list (song)[(std::size_t) i].tick >= p.firstBar)
            tempomarkers::remove (song, i);
    for (const auto& m : p.marks)
        tempomarkers::add (song, m.tick, m.bpm, false);
    auto& clip = song.playlist[(std::size_t) clipIndex];
    clip.start = p.clipStart;
    clip.length = p.clipLength;
    clip.stretch = 1.0;
    if (p.unwarp && clip.channel >= 0 && clip.channel < (int) song.channels.size())
    {
        auto& s = song.channels[(std::size_t) clip.channel].sampler;
        s.stretchBeats = 0.0f;
        s.warp.clear();
    }
    return true;
}
} // namespace lynx::smarttempo
