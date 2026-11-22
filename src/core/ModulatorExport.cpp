#include "core/ModulatorExport.h"

#include <algorithm>
#include <cmath>

#include "core/FlCurve.h"

namespace lynx::modexport
{
namespace
{
constexpr double full = 4294967296.0; // the clip LFO's phase units per cycle

// the largest gap between FL's bent triangle at `tension` and a sine, over one cycle from the LFO's start
float sineGap (int tension) noexcept
{
    float worst = 0.0f;
    for (int k = 0; k < 256; ++k)
    {
        const auto c = k / 256.0;
        // the LFO starts mid-way up its rising ramp: φ = 3/4 of a cycle with no skew
        const auto at = (std::uint32_t) (std::uint64_t) std::fmod (0.75 * full + c * full, full);
        const auto y = AutomationLfo::shape (at, tension, 0, 0, false);
        worst = std::max (worst, std::abs (y - (float) std::sin (c * 6.283185307179586)));
    }
    return worst;
}

bool automatedElsewhere (const Song& song, const AutomationTarget& target)
{
    for (int a = 0; a < (int) song.automations.size(); ++a)
    {
        const auto& automation = song.automations[(std::size_t) a];
        if (! (automation.target == target))
            continue;
        if (automation.isLive())
            return true;
        for (const auto& c : song.playlist)
            if (c.kind == ClipKind::automation && c.automation == a)
                return true;
        for (const auto& t : song.playlistTracks)
            for (const auto& lane : t.lanes)
                if (lane.automation == a)
                    return true;
    }
    return false;
}

// the first Playlist track free over [0, length) (instrument tracks take no automation clips)
int freeTrack (const Song& song, std::int64_t length)
{
    for (int t = 0;; ++t)
    {
        if (t < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) t].mode == PlaylistTrackMode::instrument)
            continue;
        const auto busy = std::any_of (song.playlist.begin(), song.playlist.end(), [&] (const Clip& c) { return c.track == t && c.start < length && c.end() > 0; });
        if (! busy)
            return t;
    }
}

std::string describe (const Modulator& m) { return "Modulator '" + m.name + "' (" + Modulator::kindName (m.kind) + ")"; }
} // namespace

int sineTension() noexcept
{
    static const int best = [] {
        auto bestTension = 0;
        auto bestGap = sineGap (0);
        for (int t = -128; t <= 128; ++t)
            if (const auto g = sineGap (t); g < bestGap)
                bestGap = g, bestTension = t;
        return bestTension;
    }();
    return best;
}

Result bakeForFl (Song& song)
{
    Result result;
    if (song.modulators.empty())
        return result;
    const auto end = std::max<std::int64_t> (ppq * 4, song.songLength());
    const auto modulators = song.modulators;
    song.modulators.clear(); // (from here on the targets play their own values; macro targets are unlinked below)
    song.forEachTarget ([] (AutomationTarget& t) {
        if (t.kind == AutomationTargetKind::modulatorMacro)
            t = AutomationTarget {};
    });

    for (const auto& m : modulators)
        for (const auto& r : m.routes)
        {
            if (r.depth == 0.0f)
                continue;
            const auto where = describe (m);
            if (! m.deterministic())
            {
                result.lost.push_back (where + ": FL has no modulator that follows " + (m.kind == ModKind::follower ? "an insert's level"
                                                                                           : m.kind == ModKind::envelope ? "a channel's notes"
                                                                                           : m.kind == ModKind::keytrack || m.kind == ModKind::velocity ? "the last note"
                                                                                                                                                       : "a free-running rate")
                                       + "; the target keeps its own value");
                continue;
            }
            if (! Modulator::canModulate (song, r.target))
                continue; // (nothing it moved)
            if (automatedElsewhere (song, r.target))
            {
                result.lost.push_back (where + ": its target has its own automation in the project, which FL plays alone");
                continue;
            }
            const auto hosted = Modulator::isHostedTarget (song, r.target);
            if (hosted && r.base < 0.0f)
                continue;
            const auto base = hosted ? r.base : std::clamp (song.modulationBase (r.target), 0.0f, 1.0f);
            const auto valueAt = [&] (float y) { return std::clamp (base + r.depth * y, 0.0f, 1.0f); };

            Automation a;
            a.name = m.name + " - " + std::to_string ((int) std::lround (r.depth * 100.0f)) + "%";
            a.target = r.target;
            const auto stepTicks = std::max (1.0, m.beats * ppq);

            if (m.kind == ModKind::macro)
            {
                const auto y = m.unipolar ? m.macro : m.macro * 2.0f - 1.0f;
                a.points = { { 0, valueAt (y), 0.0f }, { end, valueAt (y), 0.0f } };
            }
            else if (m.kind == ModKind::lfo && m.shape != ModShape::random && m.shape != ModShape::smoothRandom)
            {
                // a flat clip at the centre with FL's clip LFO: added, tempo-synced, the phase with the song
                const auto centre = m.unipolar ? base + r.depth * 0.5f : base;
                const auto amount = m.unipolar ? r.depth * 0.5f : r.depth; // (the swing either way)
                a.points = { { 0, std::clamp (centre, 0.0f, 1.0f), 0.0f }, { end, std::clamp (centre, 0.0f, 1.0f), 0.0f } };
                auto& lfo = a.lfo;
                lfo.on = true;
                lfo.add = true;
                lfo.flags = 1 | 2;
                // apply(): value + 0.5 × output × level / 128: level = 256 × amount (FL's knob ends at ±128)
                const auto level = (int) std::lround (256.0f * amount);
                lfo.level = std::clamp (level, -128, 128);
                if (level != lfo.level)
                    result.notes.push_back (where + ": depth beyond FL's LFO level, played at " + std::to_string (lfo.level > 0 ? 50 : -50) + "%");
                lfo.speed = AutomationLfo::speedForBeats (m.beats);
                const auto played = 1.0 / std::max (1.0e-12, AutomationLfo::cyclesPerBeat (lfo.speed));
                if (std::abs (played - m.beats) > m.beats * 1.0e-3)
                    result.notes.push_back (where + ": its rate rounded to FL's LFO speed");
                auto phase = (double) m.phase;
                switch (m.shape)
                {
                    case ModShape::triangle: break; // (exact)
                    case ModShape::sine:
                        lfo.tension = sineTension();
                        result.notes.push_back (where + ": the sine plays as FL's LFO with tension " + std::to_string (lfo.tension) + " (a bent triangle)");
                        break;
                    case ModShape::sawUp: lfo.skew = 128; phase -= 0.5; break;
                    case ModShape::sawDown: lfo.skew = -128; break;
                    case ModShape::square:
                        lfo.tension = 128; // (FL's steepest bend: a square with short ramps)
                        result.notes.push_back (where + ": the square plays as FL's LFO at full tension (short ramps at its edges)");
                        break;
                    case ModShape::random:
                    case ModShape::smoothRandom: break;
                }
                phase -= std::floor (phase);
                lfo.phase = (std::uint32_t) (std::uint64_t) std::fmod (phase * full, full);
            }
            else
            {
                // steps, random, an LFO's random shapes: the values baked as points
                const auto smoothShape = m.kind == ModKind::lfo ? m.shape == ModShape::smoothRandom
                                                                : (m.kind == ModKind::steps ? m.glide > 0.0f : m.smooth > 0.0f);
                const auto perStep = smoothShape ? 8 : 1;
                if (smoothShape)
                    result.notes.push_back (where + ": its glide is drawn with 8 points a step");
                const auto stepsInSong = (std::int64_t) std::ceil ((double) end / stepTicks);
                if (stepsInSong * perStep > 20000)
                {
                    result.lost.push_back (where + ": too many steps for an automation clip");
                    continue;
                }
                for (std::int64_t k = 0; k <= stepsInSong * perStep; ++k)
                {
                    const auto at = (double) k / perStep; // in steps (cycles for an LFO)
                    const auto tick = (std::int64_t) std::llround (at * stepTicks);
                    if (tick > end)
                        break;
                    const auto y = m.kind == ModKind::lfo ? m.output (at + 1.0e-9) : m.stepOutput (at + 1.0e-9);
                    AutomationPoint p { tick, valueAt (y), 0.0f };
                    p.mode = smoothShape ? (std::uint8_t) flcurve::singleCurve : (std::uint8_t) flcurve::hold;
                    if (! a.points.empty() && a.points.back().tick == tick)
                        a.points.back() = p;
                    else
                        a.points.push_back (p);
                }
            }

            song.automations.push_back (std::move (a));
            Clip clip;
            clip.kind = ClipKind::automation;
            clip.automation = (int) song.automations.size() - 1;
            clip.start = 0;
            clip.length = end;
            clip.track = freeTrack (song, end);
            song.playlist.push_back (clip);
            ++result.clips;
        }
    return result;
}
} // namespace lynx::modexport
