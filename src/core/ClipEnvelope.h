#pragma once

// Audio clip envelopes (round 190gi; the owner's #24 YES, roadmap "CE"; slice B3 of /mnt/e/lynx-work/pain190gc/REPORT.md).
// The model is ClipEnvelope in core/Song.h: per audio clip a volume and a pan breakpoint lane in clip time (the clip's
// content ticks, Clip::offset's units), with FL's automation curve modes per segment (AutomationPoint, core/FlCurve).
//
// What it is in each program it meets:
// - FL Studio has no breakpoint envelope on an audio clip. FL's own way is the audio clip menu's Automate › Volume /
//   Panning (round 190dx: an automation clip of the clip channel's Levels adjustment, placed over the clip). The .flp
//   export writes each envelope exactly that way (bakeToAutomation), and the volume lane uses the Levels adjustment's own
//   gain law (levelAdjustGain: value v → multiplier v, so 1 = unity and the export is the same curve).
// - Ableton Live has clip envelopes: an audio clip's "Clip Gain" envelope (the track's MainSequencer
//   VolumeModulationTarget) and the Mixer's "Track Panning" modulation, in clip beats, values −1…1 [SCHEMA
//   realive190fm/schema-12402.txt AudioSequencer, ClipEnvelope{EnvelopeTarget/PointeeId, Automation/Events}; DATA: the
//   12 Suite demo set's vocal clips: −1 where the clip is silent, 1 elsewhere]. Live's laws were measured in round
//   190gu (1 kHz sine, /mnt/e/lynx-work/live190gu/s/ana/clipenv-g2.txt, the same in Re-Pitch and Complex) and are
//   applied by toLive / fromLive (round 190gx): Clip Gain = liveGainDb below (gain never above unity), Track Panning
//   = the constant-power law with +3 dB at the sides that LYNX's voicePan already is. Live ignores the clip envelopes
//   of an UNWARPED clip (it renders at unity; its Clip View has no Envelopes tab): the .als import leaves them out.
//
// Cost: a clip without an envelope (index −1, or every lane flat) adds nothing to its voice; the renderer hands the
// table to the instruments only when the song has envelopes at all.

#include "core/Song.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace lynx::clipenv
{
inline constexpr std::uint8_t volumeLane = 1, panLane = 2;
inline constexpr float volumeNeutral = 1.0f, panNeutral = 0.5f;

inline float neutralOf (std::uint8_t lane) noexcept { return lane == panLane ? panNeutral : volumeNeutral; }

// The gain of a volume value (0..1, 1 = unity): FL's Levels adjustment law below unity, (11^v − 1) / 10
inline float gainOf (float v) noexcept { return levelAdjustGain (std::clamp (v, 0.0f, 1.0f)); }
// The pan offset of a pan value (0..1, 0.5 = centre): −1…1, added to the clip's pan
inline float panOf (float v) noexcept { return std::clamp (v, 0.0f, 1.0f) * 2.0f - 1.0f; }

inline bool flat (const std::vector<AutomationPoint>& lane, float neutral) noexcept
{
    for (const auto& p : lane)
        if (std::abs (p.value - neutral) > 1.0e-6f)
            return false;
    return true;
}

inline bool active (const ClipEnvelope& e) noexcept { return ! flat (e.volume, volumeNeutral) || ! flat (e.pan, panNeutral); }

// The clip's envelope (null: none, an index out of range, or not an audio clip)
inline const ClipEnvelope* of (const Song& song, const Clip& c) noexcept
{
    if (c.kind != ClipKind::audio || c.envelope < 0 || c.envelope >= (int) song.clipEnvelopes.size())
        return nullptr;
    return &song.clipEnvelopes[(std::size_t) c.envelope];
}
inline const ClipEnvelope* activeOf (const Song& song, const Clip& c) noexcept
{
    const auto* e = of (song, c);
    return e != nullptr && active (*e) ? e : nullptr;
}

inline const std::vector<AutomationPoint>& laneOf (const ClipEnvelope& e, std::uint8_t lane) noexcept { return lane == panLane ? e.pan : e.volume; }
inline std::vector<AutomationPoint>& laneOf (ClipEnvelope& e, std::uint8_t lane) noexcept { return lane == panLane ? e.pan : e.volume; }

// A lane's value (0..1) at clip tick `tick` (an empty lane: its neutral value)
inline float valueAt (const ClipEnvelope& e, std::uint8_t lane, double tick) noexcept
{
    const auto& points = laneOf (e, lane);
    return points.empty() ? neutralOf (lane) : automationCurveAt (points, tick);
}
inline float gainAt (const ClipEnvelope& e, double tick) noexcept { return e.volume.empty() ? 1.0f : gainOf (automationCurveAt (e.volume, tick)); }
inline float panAt (const ClipEnvelope& e, double tick) noexcept { return e.pan.empty() ? 0.0f : panOf (automationCurveAt (e.pan, tick)); }

// Clip time of a song tick (where in its content the clip plays at `songTick`)
inline double clipTick (const Clip& c, double songTick) noexcept { return (double) c.offset + (double) c.offsetFraction + songTick - (double) c.start; }

// How many clips (every arrangement, the clip grid) use envelope `index`
inline int users (const Song& song, int index) noexcept
{
    int n = 0;
    song.forEachClipList ([&] (const std::vector<Clip>& list) {
        for (const auto& c : list)
            n += c.kind == ClipKind::audio && c.envelope == index ? 1 : 0;
    });
    return n;
}

// A lane as a new envelope starts it: flat at its neutral value from the clip's start to its end (clip time)
inline std::vector<AutomationPoint> defaultLane (const Clip& c, std::uint8_t lane)
{
    AutomationPoint a, b;
    a.tick = c.offset;
    b.tick = c.offset + std::max<std::int64_t> (1, c.length);
    a.value = b.value = neutralOf (lane);
    return { a, b };
}

// The clip's own envelope, to edit: a new one when it has none, a copy when other clips share it (copy on write)
inline ClipEnvelope& own (Song& song, Clip& c)
{
    if (c.envelope < 0 || c.envelope >= (int) song.clipEnvelopes.size())
    {
        song.clipEnvelopes.emplace_back();
        c.envelope = (int) song.clipEnvelopes.size() - 1;
    }
    else if (users (song, c.envelope) > 1)
    {
        auto copy = song.clipEnvelopes[(std::size_t) c.envelope];
        song.clipEnvelopes.push_back (std::move (copy));
        c.envelope = (int) song.clipEnvelopes.size() - 1;
    }
    return song.clipEnvelopes[(std::size_t) c.envelope];
}

// The clip menu's "Show volume envelope" / "Show pan envelope" (lane) and "Hide" (0): the lane drawn on the clip; a
// lane shown for the first time starts flat over the clip
inline void show (Song& song, Clip& c, std::uint8_t lane)
{
    if (c.kind != ClipKind::audio)
        return;
    if (lane == 0)
    {
        if (const auto* e = of (song, c); e == nullptr || e->shown == 0)
            return;
    }
    auto& e = own (song, c);
    e.shown = lane;
    if (lane != 0 && laneOf (e, lane).empty())
        laneOf (e, lane) = defaultLane (c, lane);
}

// "Reset": the lane flat again at its neutral value (kept shown)
inline void reset (Song& song, Clip& c, std::uint8_t lane)
{
    if (of (song, c) == nullptr)
        return;
    auto& e = own (song, c);
    laneOf (e, lane) = e.shown == lane ? defaultLane (c, lane) : std::vector<AutomationPoint> {};
}

// The clip's content scaled by `factor` (its stretch changed, a warp ÷2 / ×2): the points follow their audio
inline void scale (Song& song, Clip& c, double factor)
{
    if (! (factor > 0.0) || factor == 1.0 || of (song, c) == nullptr)
        return;
    auto& e = own (song, c);
    for (auto* lane : { &e.volume, &e.pan })
        for (auto& p : *lane)
            p.tick = (std::int64_t) std::llround ((double) p.tick * factor);
}

// A drag that stretches the clip from the state `fromIndex` had at the drag's start, by `factor` (FL's Shift+drag
// stretch, core/PlaylistTrackOps stretchAudioClip, called at every move): the points of the start state scaled into the
// clip's own entry (an entry only it uses is reused, so a drag adds one entry, not one per move)
inline void scaleFrom (Song& song, Clip& c, int fromIndex, double factor)
{
    if (fromIndex < 0 || fromIndex >= (int) song.clipEnvelopes.size() || ! (factor > 0.0))
        return;
    auto scaled = song.clipEnvelopes[(std::size_t) fromIndex];
    for (auto* lane : { &scaled.volume, &scaled.pan })
        for (auto& p : *lane)
            p.tick = (std::int64_t) std::llround ((double) p.tick * factor);
    if (c.envelope >= 0 && c.envelope < (int) song.clipEnvelopes.size() && c.envelope != fromIndex && users (song, c.envelope) == 1)
        song.clipEnvelopes[(std::size_t) c.envelope] = std::move (scaled);
    else
    {
        song.clipEnvelopes.push_back (std::move (scaled));
        c.envelope = (int) song.clipEnvelopes.size() - 1;
    }
}

// The clip's content moved by `ticks` (Make unique as sample: the new file starts where the clip started)
inline void shift (Song& song, Clip& c, double ticks)
{
    if (of (song, c) == nullptr || ticks == 0.0)
        return;
    auto& e = own (song, c);
    for (auto* lane : { &e.volume, &e.pan })
        for (auto& p : *lane)
            p.tick = (std::int64_t) std::llround ((double) p.tick + ticks);
}

// For the save: old index → new index of the envelopes clips use (−1 = unused), in the order of first use
inline std::vector<int> usedOrder (const Song& song, int* count = nullptr)
{
    std::vector<int> remap (song.clipEnvelopes.size(), -1);
    int next = 0;
    song.forEachClipList ([&] (const std::vector<Clip>& list) {
        for (const auto& c : list)
            if (c.kind == ClipKind::audio && c.envelope >= 0 && c.envelope < (int) remap.size() && remap[(std::size_t) c.envelope] < 0)
                remap[(std::size_t) c.envelope] = next++;
    });
    if (count != nullptr)
        *count = next;
    return remap;
}

inline bool anyActive (const Song& song) noexcept
{
    bool any = false;
    song.forEachClipList ([&] (const std::vector<Clip>& list) {
        for (const auto& c : list)
            any = any || activeOf (song, c) != nullptr;
    });
    return any;
}

// ---- Live (.als) ----------------------------------------------------------------------------------------------------
// Live's Clip Gain law (round 190gu renders, fitted in round 190gx; 31 measured values, every one within 0.01 dB):
// value L (−1…1) → dB = 20·(L − 1) from 1 down to −0.2 (gain 10^(L−1)); below, the larger of −50·L² + L − 21.8 (a
// quadratic that joins at −0.2: −24 dB, and holds to −0.94: −66.92 dB) and the line through Live's two lowest steps
// (−0.98: −69.4 dB, −0.96: −68.37 dB), which takes over at about −0.95; −1 is silence.
namespace livelaw
{
inline constexpr double quadA = -50.0, quadB = 1.0, quadC = -21.8; // dB = quadA·L² + quadB·L + quadC (−0.95 … −0.2)
inline constexpr double lineAt = -0.98, lineDb = -69.4, lineSlope = 51.35;   // the floor line through (−0.98, −69.4), (−0.96, −68.373)
inline constexpr double kneeHigh = -0.2;
}
inline double liveGainDb (double L) noexcept
{
    using namespace livelaw;
    if (! (L > -1.0))
        return -std::numeric_limits<double>::infinity();
    L = std::min (L, 1.0);
    if (L >= kneeHigh)
        return 20.0 * (L - 1.0);
    return std::max (quadA * L * L + quadB * L + quadC, lineDb + lineSlope * (L - lineAt));
}
inline double liveGain (double L) noexcept { return L > -1.0 ? std::pow (10.0, liveGainDb (L) / 20.0) : 0.0; }
// The inverse: the Live value of a gain (0 … 1; above unity → 1, at or below the floor line's end, about −70.4 dB → −1).
// dB is the max of two rising curves below −0.2, so L is the smaller of their two inverses.
inline double liveValueOfGain (double gain) noexcept
{
    using namespace livelaw;
    if (! (gain > 0.0))
        return -1.0;
    const auto db = 20.0 * std::log10 (gain);
    if (db >= -24.0)
        return std::min (1.0, 1.0 + db / 20.0);
    const auto disc = quadB * quadB - 4.0 * quadA * (quadC - db); // (quadA < 0: the root below the vertex)
    const auto onQuad = disc >= 0.0 ? (-quadB + std::sqrt (disc)) / (2.0 * quadA) : -1.0;
    const auto onLine = lineAt + (db - lineDb) / lineSlope;
    return std::clamp (std::min (onQuad, onLine), -1.0, kneeHigh);
}
// LYNX's volume lane value (0…1 on the Levels-adjustment law, gainOf) ↔ Live's Clip Gain value: the same gain
inline double volumeToLive (float v) noexcept { return v >= 1.0f ? 1.0 : liveValueOfGain ((double) gainOf (v)); }
inline float volumeFromLive (double L) noexcept
{
    if (L >= 1.0)
        return 1.0f;
    const auto g = liveGain (L);
    return (float) std::clamp (std::log (10.0 * g + 1.0) / std::log (11.0), 0.0, 1.0);
}
// The pan lane: LYNX's offset (2v − 1) is Live's Track Panning value, and both use the same constant-power law with
// +3 dB at the sides (voicePan: L = √2·cos((p+1)·π/4), R = √2·sin((p+1)·π/4); Live 190gu: ±1 → +3.01 dB / −100 dB)
inline double panToLive (float v) noexcept { return (double) std::clamp (v, 0.0f, 1.0f) * 2.0 - 1.0; }
inline float panFromLive (double L) noexcept { return (float) std::clamp ((L + 1.0) * 0.5, 0.0, 1.0); }
inline double toLive (std::uint8_t lane, float v) noexcept { return lane == panLane ? panToLive (v) : volumeToLive (v); }
inline float fromLive (std::uint8_t lane, double L) noexcept { return lane == panLane ? panFromLive (L) : volumeFromLive (L); }

// The two programs interpolate a lane in different units (Live: straight in its value; LYNX: straight in the lane's
// 0…1 value), so a volume ramp is cut into pieces until the two agree within `tolDb` at every piece's middle (gains
// under −60 dB are not compared). `inner (t0, t1, f)` is the source value at fraction f of the segment in the source's
// units, `mapped` turns it into the target's, `dbOfTarget` / `dbOfSource` give the gain of each. The pieces' ends after
// t0 are handed to `add (t, targetValue)` in order (t1 included).
template <typename SourceAt, typename Map, typename DbT, typename DbS, typename Add>
void subdivide (double t0, double t1, const SourceAt& sourceAt, const Map& mapped, const DbT& dbOfTarget, const DbS& dbOfSource, const Add& add,
                double tolDb = 0.05, int depth = 0)
{
    const auto a = mapped (sourceAt (t0)), b = mapped (sourceAt (t1));
    const auto tm = 0.5 * (t0 + t1);
    double off = 0.0;
    for (const auto f : { 0.25, 0.5, 0.75 })
    {
        const auto truth = dbOfSource (sourceAt (t0 + (t1 - t0) * f)), straight = dbOfTarget (a + (b - a) * f);
        off = std::max (off, std::abs (std::max (truth, -66.0) - std::max (straight, -66.0)));
    }
    if (depth < 12 && t1 > t0 && off > tolDb)
    {
        subdivide (t0, tm, sourceAt, mapped, dbOfTarget, dbOfSource, add, tolDb, depth + 1);
        subdivide (tm, t1, sourceAt, mapped, dbOfTarget, dbOfSource, add, tolDb, depth + 1);
        return;
    }
    add (t1, b);
}
inline double laneDb (float v) noexcept
{
    const auto g = gainOf (v);
    return g > 0.0f ? 20.0 * std::log10 ((double) g) : -200.0;
}
inline double liveDb (double L) noexcept { return L > -1.0 ? liveGainDb (L) : -200.0; }

// ---- FL (.flp): FL's Automate › Volume / Panning over the clip ------------------------------------------------------
// A lane's points between the clip's start and end, moved to the clip's own start (0 = the clip's first tick), with a
// point at each end; `map` turns a value into the automation's. The last point is repeated at the end with `after`
// (the parameter's neutral value): an FL automation clip holds its last value after it ends.
inline std::vector<AutomationPoint> lanePoints (const std::vector<AutomationPoint>& lane, const Clip& c, float (*map) (float), float after)
{
    std::vector<AutomationPoint> out;
    const auto from = (double) c.offset + (double) c.offsetFraction, length = (double) std::max<std::int64_t> (1, c.length);
    auto add = [&] (double t, float value, float tension, std::uint8_t mode) {
        AutomationPoint p;
        p.tick = (std::int64_t) std::llround (std::clamp (t, 0.0, length));
        p.value = std::clamp (map (value), 0.0f, 1.0f);
        p.tension = tension;
        p.mode = mode;
        out.push_back (p);
    };
    // the segment in progress at the clip's start: its curve from there on is approximated by the point's own
    std::size_t first = 0;
    while (first < lane.size() && (double) lane[first].tick <= from)
        ++first;
    const auto startTension = first > 0 && first < lane.size() ? lane[first - 1].tension : 0.0f;
    const auto startMode = first > 0 && first < lane.size() ? lane[first - 1].mode : (std::uint8_t) 0;
    add (0.0, automationCurveAt (lane, from), first == 0 ? 0.0f : startTension, first == 0 ? (std::uint8_t) 0 : startMode);
    for (std::size_t i = first; i < lane.size() && (double) lane[i].tick < from + length; ++i)
        add ((double) lane[i].tick - from, lane[i].value, lane[i].tension, lane[i].mode);
    add (length, automationCurveAt (lane, from + length), 0.0f, 0);
    auto end = out.back();
    end.value = after;
    out.push_back (end);
    return out;
}

// Every active envelope of the Playlist (current arrangement) becomes automation clips of its clip channel's Levels
// adjustment volume / pan (AutomationTargetKind::channelAdjust, FL's 0x11 / 0x10), on the clip's track, start and
// length, as FL's Automate menu places them; the clips lose their envelope. Returns the automation clips made.
inline int bakeToAutomation (Song& song)
{
    int made = 0;
    const auto count = song.playlist.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto c = song.playlist[i];
        const auto* e = activeOf (song, c);
        if (e == nullptr || c.channel < 0 || c.channel >= (int) song.channels.size())
            continue;
        const auto env = *e;
        for (const auto lane : { volumeLane, panLane })
        {
            const auto& points = laneOf (env, lane);
            if (flat (points, neutralOf (lane)))
                continue;
            Automation a;
            a.name = song.channels[(std::size_t) c.channel].name + (lane == volumeLane ? " - Clip volume" : " - Clip pan");
            a.target.kind = AutomationTargetKind::channelAdjust;
            a.target.index = c.channel;
            a.target.param = lane == volumeLane ? leveladjust::volume : leveladjust::pan;
            // volume: the multiplier's automation value is multiplier / 2 (0…200 %), so v → v / 2; pan: the offset's is
            // (offset + 1) / 2 = v
            a.points = lane == volumeLane ? lanePoints (points, c, [] (float v) { return v * 0.5f; }, 0.5f)
                                          : lanePoints (points, c, [] (float v) { return v; }, 0.5f);
            song.automations.push_back (std::move (a));
            Clip clip;
            clip.kind = ClipKind::automation;
            clip.automation = (int) song.automations.size() - 1;
            clip.track = c.track;
            clip.start = c.start;
            clip.length = std::max<std::int64_t> (1, c.length);
            song.playlist.push_back (clip);
            ++made;
        }
        song.playlist[i].envelope = -1;
    }
    return made;
}
} // namespace lynx::clipenv
