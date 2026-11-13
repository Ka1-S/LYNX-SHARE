// Round 190bw: FL's per-point curve modes in automation clips (13 modes: Single curve, Double curve, Hold, Stairs,
// Smooth stairs, Pulse, Wave, Single curve 2, Double curve 2, Half sine, Smooth, Single curve 3, Double curve 3).
// The law: FL's envelope evaluator [DECOMP FLEngine 0xb9def0; the same shared library as the WaveShaper map,
// REA_rodada51 §2.4]. FL keeps a segment's mode and tension on the point where it ends; LYNX on the point where it
// starts, the tension with the opposite sign.
#include "core/FlpExport.h"
#include "flp/FlpWrite.h"
namespace curves190bw
{
using namespace lynx;

// FL's Round (to the nearest, ties to even)
double roundFl (double v) { return std::nearbyint (v); }

// The documented law written out again, apart from core/FlCurve: the fraction 0…1 at x for FL's mode and tension t.
double law (int mode, double t, double x)
{
    const auto pi = 3.14159265358979323846;
    const auto k = std::pow (1000001.0, std::abs (t)) - 1.0;
    const auto single = [&] (double u) {
        if (t == 0.0)
            return u;
        return t > 0.0 ? 1.0 - (std::pow (k + 1.0, 1.0 - u) - 1.0) / k : (std::pow (k + 1.0, u) - 1.0) / k;
    };
    // the step / period count of modes 3…6
    auto n = (int) roundFl (0.001 * (double) (float) (std::pow (1000001.0, (double) (float) (1.0 - 0.6 * std::abs (t))) - 1.0));
    const auto side = t < 0.0 ? -1.0 : 1.0;
    const auto six = [&] (double u) { // single curve 2
        const auto v = t > 0.0 ? 1.0 - u : u;
        return u - (std::pow (v, 6.0) - v) * t;
    };
    const auto a = std::max (std::abs (t) * 0.9, 1.0 / 65536.0);
    const auto p = 1.0 - std::pow (1.0 - a, 3.0);
    const auto hyper = [&] (double u) { // single curve 3 (0 → 1): a hyperbola through both ends
        if (t == 0.0)
            return u;
        const auto q = (1.0 - p) / p;
        return t > 0.0 ? 1.0 + q - q / (1.0 - p * (1.0 - u)) : q / (1.0 - p * u) - q;
    };
    switch (mode)
    {
        case flcurve::singleCurve: return single (x);
        case flcurve::doubleCurve: return x < 0.5 ? single (2.0 * x) * 0.5 : 1.0 - single (2.0 * (1.0 - x)) * 0.5;
        case flcurve::hold: return 0.0;
        case flcurve::stairs:
            if (t >= 0.0)
                ++n;
            return roundFl (x * (n + 1) - side * 0.5) / ((double) (n + 1) - side);
        case flcurve::smoothStairs:
        {
            const auto d = x * (n + 1) - 0.5;
            const auto i = roundFl (d - 0.5);
            auto f = (d - i) * 2.0 - 1.0; // −1…1 inside a step
            f = t < 0.0 ? (1.0 - std::pow (1.0 - std::abs (f), 3.0)) * (f < 0.0 ? -1.0 : 1.0) : f * f * f;
            return (i + f * 0.5 + 1.0) / (double) (n + 1);
        }
        case flcurve::pulse: return (double) ((long long) roundFl (x * 2.0 * (n + 1) - side * 0.5) & 1);
        case flcurve::wave:
            if (t < 0.0)
                return (std::sin ((x * (n + 0.5) + 0.75) * 2.0 * pi) + 1.0) * 0.5;
            if (n == 0)
                return x;
            {
                const auto d = x * (n + 0.5) - 0.5;
                return std::abs ((d - roundFl (d - 0.5)) * 2.0 - 1.0);
            }
        case flcurve::singleCurve2: return six (x);
        case flcurve::doubleCurve2: return x < 0.5 ? six (2.0 * x) * 0.5 : 1.0 - six (2.0 * (1.0 - x)) * 0.5;
        case flcurve::halfSine:
            return x - (x - (t > 0.0 ? std::sin (x * pi * 0.5) : 1.0 - std::cos (x * pi * 0.5))) * std::abs (t);
        case flcurve::singleCurve3: return hyper (x);
        case flcurve::doubleCurve3: return x < 0.5 ? hyper (2.0 * x) * 0.5 : 1.0 - hyper (2.0 * (1.0 - x)) * 0.5;
        default: return x;
    }
}

const float tensions[] = { -1.0f, -0.725f, -0.4f, -0.28f, 0.0f, 0.28f, 0.5f, 0.65f, 1.0f };

// two points 0.2 → 0.8 over one bar, the segment in FL's `mode` with FL's tension `t`
std::vector<AutomationPoint> segment (int mode, float t)
{
    return { { 0, 0.2f, -t, (std::uint8_t) mode }, { 3840, 0.8f, 0.0f } };
}
} // namespace curves190bw

TEST (curves190bw_every_mode_follows_fls_law)
{
    using namespace curves190bw;
    int compared = 0;
    double worst = 0.0;
    for (int m = 0; m < flcurve::modeCount; ++m)
    {
        if (m == flcurve::smooth) // (needs its neighbours: the next tests)
            continue;
        for (const auto t : tensions)
        {
            const auto pts = segment (m, t);
            for (int tick = 7; tick < 3840; tick += 61)
            {
                const auto x = tick / 3840.0;
                const auto want = 0.2 + 0.6 * law (m, (double) t, x);
                const auto got = (double) automationCurveAt (pts, (double) tick);
                auto d = std::abs (got - want);
                if (m >= flcurve::stairs && m <= flcurve::pulse && d > 1e-4)
                {
                    // (a step edge can land on either side of a sample: the neighbours' levels)
                    d = std::min (std::abs (got - (0.2 + 0.6 * law (m, (double) t, x - 1e-4))), std::abs (got - (0.2 + 0.6 * law (m, (double) t, x + 1e-4))));
                }
                worst = std::max (worst, d);
                CHECK (d < 2e-5);
                ++compared;
            }
            // the ends: the left point's level at its tick, the right point's from its tick on
            CHECK (automationCurveAt (pts, 0.0) == 0.2f && automationCurveAt (pts, 3840.0) == 0.8f && automationCurveAt (pts, 9000.0) == 0.8f);
        }
    }
    std::printf ("  automation clip modes vs FL's law: %d values, worst difference %.2g\n", compared, worst);

    // values worked by hand
    CHECK (automationCurveAt (segment (flcurve::hold, 0.3f), 3839.0) == 0.2f);                      // Hold: the level until the next point
    CHECK (std::abs (automationCurveAt (segment (flcurve::stairs, 1.0f), 1000.0) - 0.2f) < 1e-6f);  // Stairs at full tension: one step, half way
    CHECK (std::abs (automationCurveAt (segment (flcurve::stairs, 1.0f), 2900.0) - 0.8f) < 1e-6f);
    CHECK (std::abs (automationCurveAt (segment (flcurve::singleCurve2, 1.0f), 1920.0) - (0.2f + 0.6f * (0.5f - (1.0f / 64.0f - 0.5f)))) < 1e-6f);
    CHECK (std::abs (automationCurveAt (segment (flcurve::halfSine, 1.0f), 1920.0) - (0.2f + 0.6f * 0.70710678f)) < 1e-6f);
    CHECK (std::abs (automationCurveAt (segment (flcurve::wave, -1.0f), 1920.0) - (0.2f + 0.6f * 0.5f)) < 1e-6f); // half a sine period, at its middle
    // a falling segment mirrors: the same fraction of the way down
    std::vector<AutomationPoint> down { { 0, 0.9f, -0.5f, (std::uint8_t) flcurve::singleCurve3 }, { 960, 0.1f, 0.0f } };
    CHECK (std::abs ((double) automationCurveAt (down, 240.0) - (0.9 - 0.8 * law (flcurve::singleCurve3, 0.5, 0.25))) < 1e-6);
}

TEST (curves190bw_smooth_takes_its_slopes_from_smooth_neighbours)
{
    using namespace curves190bw;
    const auto S = (std::uint8_t) flcurve::smooth;
    // three points on a line, both segments Smooth: the first starts flat (nothing before it) and ends with the line's
    // slope, 2x² − x³; the second mirrors it
    std::vector<AutomationPoint> line { { 0, 0.0f, 0.0f, S }, { 1000, 0.5f, 0.0f, S }, { 2000, 1.0f, 0.0f } };
    for (const auto x : { 0.1, 0.25, 0.5, 0.8 })
    {
        CHECK (std::abs ((double) automationCurveAt (line, x * 1000.0) - 0.5 * (2.0 * x * x - x * x * x)) < 1e-6);
        const auto u = 1.0 - x;
        CHECK (std::abs ((double) automationCurveAt (line, 1000.0 + x * 1000.0) - (1.0 - 0.5 * (2.0 * u * u - u * u * u))) < 1e-6);
    }
    // a lone Smooth segment has flat ends on both sides: the smoothstep 3x² − 2x³
    std::vector<AutomationPoint> lone { { 0, 0.0f, 0.0f, S }, { 1000, 1.0f, 0.0f, 0 }, { 2000, 0.0f, 0.0f } };
    CHECK (std::abs ((double) automationCurveAt (lone, 250.0) - (3.0 * 0.0625 - 2.0 * 0.015625)) < 1e-6);
    // …and so has a peak between two Smooth segments (the slopes disagree: no overshoot)
    std::vector<AutomationPoint> peak { { 0, 0.0f, 0.0f, S }, { 1000, 1.0f, 0.0f, S }, { 2000, 0.0f, 0.0f } };
    float top = 0.0f;
    for (int tick = 0; tick <= 2000; tick += 5)
        top = std::max (top, automationCurveAt (peak, (double) tick));
    CHECK (top == 1.0f && std::abs ((double) automationCurveAt (peak, 250.0) - (3.0 * 0.0625 - 2.0 * 0.015625)) < 1e-6);
    // the tension pulls the curve back to the straight line (full tension: straight)
    auto half = lone;
    half[0].tension = 0.5f;
    CHECK (std::abs ((double) automationCurveAt (half, 250.0) - (0.25 + 0.5 * (3.0 * 0.0625 - 2.0 * 0.015625 - 0.25))) < 1e-6);
    half[0].tension = -1.0f;
    CHECK (std::abs (automationCurveAt (half, 250.0) - 0.25f) < 1e-6f);
    // a flat Smooth segment stays flat
    std::vector<AutomationPoint> flat { { 0, 0.4f, 0.0f, S }, { 500, 0.4f, 0.0f, S }, { 900, 0.9f, 0.0f } };
    CHECK (automationCurveAt (flat, 250.0) == 0.4f);
}

TEST (curves190bw_matches_fls_waveshaper_engine_point_for_point)
{
    // FL's WaveShaper map (core/FruityShapers, null-tested against FL's own output in REA_rodada51) runs the same
    // envelope library: a map and an automation with the same points, modes and tensions give the same curve. Mixed
    // maps: Smooth runs of unequal lengths, Smooth next to other modes, every mode after a Smooth point.
    using namespace curves190bw;
    int compared = 0, worst = 0;
    struct P { float x, y, t; int mode; };
    std::vector<std::vector<P>> maps {
        { { 0.0f, 0.1f, 0.0f, 0 }, { 0.2f, 0.7f, 0.0f, 10 }, { 0.55f, 0.35f, 0.0f, 10 }, { 0.7f, 0.5f, 0.0f, 10 }, { 1.0f, 0.95f, 0.0f, 10 } },
        { { 0.0f, 0.0f, 0.0f, 0 }, { 0.3f, 0.4f, 0.3f, 10 }, { 0.5f, 0.6f, -0.6f, 10 }, { 1.0f, 1.0f, 0.0f, 10 } },
        { { 0.0f, 0.9f, 0.0f, 0 }, { 0.25f, 0.5f, 0.0f, 10 }, { 0.5f, 0.3f, 0.4f, 0 }, { 0.75f, 0.2f, 0.0f, 10 }, { 1.0f, 0.0f, 0.0f, 2 } },
    };
    for (int m = 0; m < flcurve::modeCount; ++m) // every mode between two Smooth segments
        maps.push_back ({ { 0.0f, 0.2f, 0.0f, 0 }, { 0.3f, 0.5f, 0.0f, 10 }, { 0.6f, 0.9f, m % 2 == 0 ? 0.45f : -0.45f, m }, { 1.0f, 0.4f, 0.0f, 10 } });

    for (const auto& map : maps)
    {
        std::vector<flshape::WsPoint> ws;
        std::vector<AutomationPoint> pts;
        const auto span = 96000.0; // ticks for the map's whole time
        for (std::size_t i = 0; i < map.size(); ++i)
        {
            ws.push_back ({ i == 0 ? 0.0f : map[i].x - map[i - 1].x, map[i].y, map[i].t, (std::uint8_t) map[i].mode });
            if (i > 0) // FL's mode and tension of point i: LYNX's point i − 1, the tension negated
            {
                pts.back().mode = (std::uint8_t) map[i].mode;
                pts.back().tension = -map[i].t;
            }
            pts.push_back ({ (std::int64_t) std::llround ((double) map[i].x * span), map[i].y, 0.0f });
        }
        const auto table = flshape::shaperTable (ws, false);
        const int n = (int) table.size() - 3;
        for (int i = 1; i < n; i += 53)
        {
            const auto x = (double) i / n;
            const auto got = std::nearbyint ((double) automationCurveAt (pts, x * span) * 32767.0);
            auto d = (int) std::abs (got - (double) table[(std::size_t) i + 1]);
            if (d > 3) // (a step of Hold / Stairs / Pulse or a point may fall one table entry apart)
                d = (int) std::min (std::abs (got - (double) table[(std::size_t) i]), std::abs (got - (double) table[(std::size_t) i + 2]));
            worst = std::max (worst, d);
            CHECK (d <= 3);
            ++compared;
        }
    }
    std::printf ("  automation clips vs FL's WaveShaper engine: %d values over %d maps, worst %d / 32767\n", compared, (int) maps.size(), worst);
}

TEST (curves190bw_older_projects_keep_their_bend)
{
    using namespace curves190bw;
    // a bent segment of a project saved before the curve modes: LYNX's own power law, to the last bit
    std::vector<AutomationPoint> old { { 0, 0.1f, 0.6f, AutomationPoint::legacyCurve }, { 960, 0.9f, -0.35f, AutomationPoint::legacyCurve }, { 1920, 0.3f, 0.0f } };
    for (int tick = 1; tick < 1920; tick += 37)
    {
        const auto& a = old[tick < 960 ? 0 : 1];
        const auto& b = old[tick < 960 ? 1 : 2];
        auto t = (float) ((double) (tick - a.tick) / 960.0);
        t = std::pow (t, std::pow (2.0f, a.tension * 3.0f));
        CHECK (automationCurveAt (old, (double) tick) == a.value + (b.value - a.value) * t);
    }
    // straight segments are the same in both laws
    std::vector<AutomationPoint> straight { { 0, 0.0f }, { 960, 1.0f } };
    CHECK (automationCurveAt (straight, 480.0) == 0.5f && straight[0].mode == flcurve::singleCurve);
    // bending it by hand makes it a Single curve; picking a mode keeps the tension (FL: "change point curve")
    setAutomationSegmentTension (old, 0, 0.25f);
    CHECK (old[0].mode == flcurve::singleCurve && old[0].tension == 0.25f);
    CHECK (automationPointMode (old, 2) == -1); // (the second segment is still the old law: no mode ticked)
    setAutomationPointMode (old, 2, flcurve::stairs);
    CHECK (old[1].mode == flcurve::stairs && old[1].tension == -0.35f && automationPointMode (old, 2) == flcurve::stairs);
    CHECK (automationPointMode (old, 0) == -1 && automationPointMode (old, 1) == flcurve::singleCurve);
    setAutomationPointMode (old, 0, flcurve::pulse); // the first point ends no segment: nothing changes
    CHECK (old[0].mode == flcurve::singleCurve);
}

TEST (curves190bw_new_and_deleted_points_keep_the_curves_as_fl_does)
{
    using namespace curves190bw;
    // FL keeps a segment's curve on the point where it ends, so a new point leaves the curve on the part after it and
    // takes the editor's last used mode and tension for the part before it [DECOMP FLEngine 0xcce3b0]
    std::vector<AutomationPoint> pts { { 0, 0.0f, 0.5f, (std::uint8_t) flcurve::stairs }, { 1000, 1.0f, 0.0f } };
    const auto at = insertAutomationPoint (pts, { 400, 0.3f }, flcurve::hold, -0.2f);
    CHECK (at == 1 && pts.size() == 3);
    CHECK (pts[0].mode == flcurve::hold && pts[0].tension == -0.2f);       // 0 → new point: the last used mode
    CHECK (pts[1].mode == flcurve::stairs && pts[1].tension == 0.5f);      // new point → 1000: the split segment's curve
    // deleting it hands the following segment's curve to the joined one [DECOMP 0xcd3180 → 0xb9c6f0]
    eraseAutomationPoint (pts, 1);
    CHECK (pts.size() == 2 && pts[0].mode == flcurve::stairs && pts[0].tension == 0.5f);
    // a point after the last one: the new last segment takes the given curve; before the first: a plain segment
    CHECK (insertAutomationPoint (pts, { 2000, 0.5f }, flcurve::wave, 0.1f) == 2 && pts[1].mode == flcurve::wave && pts[1].tension == 0.1f);
    CHECK (insertAutomationPoint (pts, { -5, 0.5f }, flcurve::wave, 0.1f) == 0 && pts[0].mode == 0 && pts[0].tension == 0.0f && pts[1].mode == flcurve::stairs);
    eraseAutomationPoint (pts, 3); // the last point goes: the segment that ended at it has no curve left
    CHECK (pts.size() == 3 && pts[2].mode == 0 && pts[2].tension == 0.0f);
    eraseAutomationPoint (pts, 0);
    CHECK (pts.size() == 2 && pts[0].mode == flcurve::stairs);
}

TEST (curves190bw_flp_round_trip_carries_mode_and_tension)
{
    using namespace curves190bw;
    // FL's point record in event 0xEA: f64 Δx, f64 value, f32 tension, u8 mode, 3 bytes; the mode and tension of the
    // segment that ENDS at the point
    flp::Project p;
    flp::Channel c;
    c.index = 0;
    c.type = flp::ChannelType::automationClip;
    c.name = "Modes";
    c.hasLevels = true;
    c.pan = 0;
    c.volume = 12800;
    c.automation.push_back ({ 0.0, 0.5, 0.0f, 0 });
    for (int m = 0; m < flcurve::modeCount; ++m)
        c.automation.push_back ({ 2.0 * (m + 1), (m % 2 == 0) ? 0.9 : 0.1, tensions[(std::size_t) m % std::size (tensions)], (std::uint8_t) m });
    c.automationLfo.present = true;
    c.automationLfo.points = { { 0.0, 1.0, 0.0f, 0 }, { 8.0, 0.25, -0.4f, 3 }, { 16.0, 0.75, 0.3f, 10 } };
    p.channels.push_back (c);
    const auto file = flp::writeProject (p);
    const auto event = lfo177b::automationEvent (file);
    CHECK (event.size() == 21 + 14 * 24 + 112 + 2 * 24);
    for (int m = 0; m < flcurve::modeCount && event.size() >= 21 + 14 * 24; ++m)
        CHECK (event[21 + (std::size_t) (m + 1) * 24 + 20] == m); // the mode byte of point m + 1

    const auto read = flp::parseProject (file);
    auto song = importFlp (read, fx171e::importOptions());
    CHECK (song.automations.size() == 1);
    if (song.automations.size() != 1)
        return;
    const auto& a = song.automations[0];
    CHECK (a.points.size() == 14); // (no helper points: Hold is a mode now)
    for (int m = 0; m < flcurve::modeCount && a.points.size() == 14; ++m)
    {
        const auto& from = a.points[(std::size_t) m];
        CHECK (from.mode == m && from.tension == -tensions[(std::size_t) m % std::size (tensions)]);
        CHECK (automationPointMode (a.points, m + 1) == m);
        // …and it plays FL's law
        const auto x = 0.37;
        const auto y0 = (double) from.value, y1 = (double) a.points[(std::size_t) m + 1].value;
        if (m != flcurve::smooth && ! (m >= flcurve::stairs && m <= flcurve::pulse))
            CHECK (std::abs ((double) a.valueAt ((double) from.tick + x * 2.0 * ppq) - (y0 + (y1 - y0) * law (m, (double) -from.tension, x))) < 2e-5);
    }
    CHECK (a.lfo.amp.size() == 3 && a.lfo.amp[0].mode == flcurve::stairs && a.lfo.amp[0].tension == 0.4f && a.lfo.amp[1].mode == flcurve::smooth
           && a.lfo.amp[1].tension == -0.3f);

    // back out: the same modes and tensions, bit for bit
    flpexport::Options options;
    flpexport::Report report;
    const auto again = flp::parseProject (flpexport::exportFlp (song, options, report));
    const flp::Channel* out = nullptr;
    for (const auto& ch : again.channels)
        if (ch.type == flp::ChannelType::automationClip)
            out = &ch;
    CHECK (out != nullptr && out->automation.size() == 14);
    for (std::size_t i = 0; out != nullptr && i < out->automation.size() && i < c.automation.size(); ++i)
        CHECK (out->automation[i].curve == c.automation[i].curve && out->automation[i].tension == c.automation[i].tension
               && out->automation[i].position == c.automation[i].position && std::abs (out->automation[i].value - c.automation[i].value) < 1e-6);
    CHECK (out != nullptr && out->automationLfo.points.size() == 3 && out->automationLfo.points[1].curve == 3 && out->automationLfo.points[1].tension == -0.4f
           && out->automationLfo.points[2].curve == 10 && out->automationLfo.points[2].tension == 0.3f);

    // a second trip gives the same song (nothing drifts)
    const auto song2 = importFlp (again, fx171e::importOptions());
    CHECK (song2.automations.size() == 1 && song2.automations[0].points == a.points);

    // an older project's bend goes out as a Single curve with the same tension; a mode FL does not have reads as one
    Song legacy = song;
    legacy.automations[0].points = { { 0, 0.2f, 0.5f, AutomationPoint::legacyCurve }, { 960, 0.8f, 0.0f } };
    const auto lp = flp::parseProject (flpexport::exportFlp (legacy, options, report));
    for (const auto& ch : lp.channels)
        if (ch.type == flp::ChannelType::automationClip)
            CHECK (ch.automation.size() == 2 && ch.automation[1].curve == 0 && ch.automation[1].tension == -0.5f);
    auto odd = p;
    odd.channels[0].automation[3].curve = 77;
    const auto oddSong = importFlp (flp::parseProject (flp::writeProject (odd)), fx171e::importOptions());
    CHECK (oddSong.automations.size() == 1 && oddSong.automations[0].points[2].mode == flcurve::singleCurve);
}

TEST (curves190bw_an_automation_clip_renders_every_mode)
{
    // A clip automating the Master volume over two bars, once per mode: the rendered level follows the mode's curve.
    // Each block's peak against the same song without the automation gives the gain that was applied; it must lie
    // inside the gains the curve takes around that block.
    using namespace curves190bw;
    const double sr = 48000.0;
    const auto bar = (std::int64_t) ppq * 4;
    const auto base = [&] {
        auto song = Song::makeDefault();
        song.bpm = 120.0;
        song.mode = PlayMode::song;
        for (int b = 0; b < 2; ++b)
            song.playlist.push_back ({ 0, 0, bar * b, bar, 0 });
        return song;
    };
    const auto render = [&] (const Song& song) {
        auto state = RackState::build (song, nullptr, sr);
        RackRenderer r;
        r.prepare (sr, 256);
        std::vector<float> l (256), rr (256), peaks;
        const auto tps = ticksPerSample (song.bpm, sr);
        for (double tick = 0.0; tick < (double) (bar * 2); tick += 256 * tps)
        {
            r.process (state.get(), true, tick, 256, l.data(), rr.data());
            float peak = 0.0f;
            for (auto v : l)
                peak = std::max (peak, std::abs (v));
            peaks.push_back (peak);
        }
        return peaks;
    };
    auto flat = base();
    Automation unity;
    unity.target = { AutomationTargetKind::mixerVolume, 0 };
    unity.points = { { 0, 0.8f }, { bar * 2, 0.8f } };
    flat.automations.push_back (unity);
    Clip ac;
    ac.kind = ClipKind::automation;
    ac.automation = 0;
    ac.track = 1;
    ac.length = bar * 2;
    flat.playlist.push_back (ac);
    const auto reference = render (flat);
    const auto tps = ticksPerSample (120.0, sr);

    int modesHeard = 0;
    for (int m = 0; m < flcurve::modeCount; ++m)
    {
        auto song = flat;
        auto& pts = song.automations[0].points;
        // 0.3 → 0.8 over the two bars in mode m (Smooth between two Smooth neighbours is covered by the curve tests)
        pts = { { 0, 0.3f, m == flcurve::pulse || m == flcurve::stairs || m == flcurve::smoothStairs || m == flcurve::wave ? -0.75f : 0.6f, (std::uint8_t) m },
                { bar * 2, 0.8f, 0.0f } };
        const auto got = render (song);
        CHECK (got.size() == reference.size());
        int used = 0, inside = 0;
        for (std::size_t b = 4; b + 4 < got.size() && b < reference.size(); ++b)
        {
            if (reference[b] < 0.02f)
                continue;
            ++used;
            const auto gain = got[b] / reference[b];
            // the gains the curve takes from three blocks before to one after (the mixer smooths its volume)
            auto lo = 1.0e9f, hi = 0.0f;
            for (double tick = ((double) b - 3.0) * 256.0 * tps; tick <= ((double) b + 2.0) * 256.0 * tps; tick += 4.0)
            {
                const auto g = faderGain (song.automations[0].valueAt (tick)) / faderGain (0.8f);
                lo = std::min (lo, g);
                hi = std::max (hi, g);
            }
            inside += gain >= lo * 0.9f - 0.01f && gain <= hi * 1.1f + 0.01f;
        }
        CHECK (used > 50);
        CHECK (inside >= used - used / 50); // (2 % of the blocks may sit on a jump of Stairs / Pulse)
        if (inside < used - used / 50)
            std::printf ("  mode %d (%s): %d of %d blocks follow the curve\n", m, flcurve::modeName (m), inside, used);
        modesHeard += used > 50 && inside >= used - used / 50;
    }
    CHECK (modesHeard == flcurve::modeCount);
    // Hold plays the first level until the next point, then jumps: quieter than the straight ramp near the end
    auto hold = flat, ramp = flat;
    hold.automations[0].points = { { 0, 0.2f, 0.0f, (std::uint8_t) flcurve::hold }, { bar * 2, 0.8f, 0.0f } };
    ramp.automations[0].points = { { 0, 0.2f, 0.0f, 0 }, { bar * 2, 0.8f, 0.0f } };
    const auto h = render (hold), r2 = render (ramp);
    double sumHold = 0.0, sumRamp = 0.0;
    for (std::size_t b = h.size() / 2; b < h.size(); ++b)
        sumHold += h[b], sumRamp += r2[b];
    CHECK (sumRamp > 0.0 && sumHold < sumRamp * 0.5);
}

// --- Piano roll tools (round 190bw, part 2) -------------------------------------------------------------------

// FL's score flipper, "Preserve start times" [REA FLEngine 0xdee8d0 and the extents pass at 0xdee5c0]: off, a note's
// new start is (last end) - (start - first start) - length; on, it is (last start) - (start - first start), so the
// starts stay where starts were whatever the lengths are.
TEST (curves190bw_flip_preserves_the_start_times)
{
    using namespace lynx;
    using namespace lynx::notetools;
    Pattern p;
    const auto note = [] (std::int64_t start, int key, std::int64_t length) { Note n; n.start = start; n.key = key; n.length = length; return n; };
    p.notes = { note (0, 60, 96), note (96, 64, 24), note (240, 67, 48) };
    const std::vector<int> all { 0, 1, 2 };

    auto q = p;
    flip (q, all, true, false); // the whole extents mirrored: the span is 0..288
    CHECK (q.notes[0].start == 192 && q.notes[1].start == 168 && q.notes[2].start == 0);

    q = p;
    flip (q, all, true, false, true); // the starts mirrored between the first (0) and the last start (240)
    CHECK (q.notes[0].start == 240 && q.notes[1].start == 144 && q.notes[2].start == 0);
    CHECK (q.notes[0].length == 96 && q.notes[1].length == 24 && q.notes[2].length == 48);
    CHECK (q.notes[0].key == 60 && q.notes[1].key == 64 && q.notes[2].key == 67);

    flip (q, all, true, false, true); // its own inverse
    CHECK (q.notes[0].start == 0 && q.notes[1].start == 96 && q.notes[2].start == 240);

    q = p;
    flip (q, all, false, true, true); // the option only concerns the horizontal flip
    CHECK (q.notes[0].start == 0 && q.notes[0].key == 67 && q.notes[1].key == 63 && q.notes[2].key == 60);
}

// FL's limiter / transposer [REA FLEngine 0xded3b0]: scale restriction first (nearest scale key; a tie goes up with
// "Above", down with "Under", up-down-up... with "Alternate"), then the range (nearest octave that fits, or with
// "Wrap to top / bottom" the lowest octave of the range for notes above it and the highest for notes below), a clamp,
// then the transposition folded by octaves onto the keyboard.
TEST (curves190bw_limit_follows_fls_limiter)
{
    using namespace lynx;
    using namespace lynx::notetools;
    const auto note = [] (int key) { Note n; n.key = key; n.length = 24; return n; };
    const auto run = [&] (std::vector<int> keys, const LimitOptions& o) {
        Pattern p;
        for (auto k : keys)
            p.notes.push_back (note (k));
        std::vector<int> all;
        for (int i = 0; i < (int) keys.size(); ++i)
            all.push_back (i);
        limit (p, all, o);
        std::vector<int> out;
        for (const auto& n : p.notes)
            out.push_back (n.key);
        return out;
    };

    LimitOptions o;
    o.lowKey = 0;
    o.highKey = 127;
    o.scale = limitRestrictionIndex ("Major", LimitRestriction::scale); // Major, on C
    // C# and D# and F# sit exactly between two scale keys; A# too. E stays.
    CHECK (run ({ 61, 63, 64, 66, 70 }, o) == (std::vector<int> { 62, 64, 64, 67, 71 }));
    o.snap = LimitOptions::under;
    CHECK (run ({ 61, 63, 64, 66, 70 }, o) == (std::vector<int> { 60, 62, 64, 65, 69 }));
    o.snap = LimitOptions::alternate; // only the tied notes count: up, down, up, down
    CHECK (run ({ 61, 63, 64, 66, 70 }, o) == (std::vector<int> { 62, 62, 64, 67, 69 }));

    o = {};
    o.lowKey = 0;
    o.highKey = 127;
    o.scale = limitRestrictionIndex ("Minor pentatonic", LimitRestriction::scale); // on A (9): A C D E G; F (65) is one from E and two from G, B (71) one from C
    o.root = 9;
    CHECK (run ({ 65, 71, 66 }, o) == (std::vector<int> { 64, 72, 67 }));

    o = {};
    o.lowKey = 48;
    o.highKey = 72;
    CHECK (run ({ 30, 100, 60, 73, 47 }, o) == (std::vector<int> { 54, 64, 60, 61, 59 })); // the nearest octave that fits
    o.wrap = true; // above: into 48..59; below: into 61..72
    CHECK (run ({ 30, 100, 60, 73, 47, 96 }, o) == (std::vector<int> { 66, 52, 60, 49, 71, 48 }));

    o = {};
    o.lowKey = 60;
    o.highKey = 64; // narrower than an octave: octaves first, then the clamp
    CHECK (run ({ 54, 75, 62 }, o) == (std::vector<int> { 64, 63, 62 }));

    o = {};
    o.lowKey = 0;
    o.highKey = 127;
    o.transpose = 7;
    CHECK (run ({ 60, 125, 0 }, o) == (std::vector<int> { 67, 120, 7 })); // 132 folds down an octave
    o.transpose = -12;
    CHECK (run ({ 60, 5 }, o) == (std::vector<int> { 48, 5 })); // -7 folds up an octave

    // the order: scale, range, transposition (the transposition may leave the scale and the range)
    o = {};
    o.lowKey = 60;
    o.highKey = 71;
    o.scale = limitRestrictionIndex ("Major", LimitRestriction::scale);
    o.transpose = 1;
    CHECK (run ({ 49 }, o) == (std::vector<int> { 63 })); // C#3 -> D (50) -> 62 -> +1
}

// FL's level scaling [REA FLEngine 0xdf0a40 / 0xdf0900]: Center, Multiply, Offset and Tension. The expected values
// are the law worked out separately in double precision (FL keeps the bend's parameters in single precision).
TEST (curves190bw_level_scaling_follows_fls_law)
{
    using namespace lynx;
    using namespace lynx::notetools;
    struct Case { double x, center, multiply, offset, tension, expected; };
    const Case cases[] = {
        { 0.25, 0.0, 1.0, 0.0, 0.0, 0.25 },                 // FL's defaults change nothing
        { 0.5, 0.0, 1.0, 0.0, 0.5, 0.8490524701655657 },    // tension up
        { 0.5, 0.0, 1.0, 0.0, -0.5, 0.15094752983443427 },  // tension down: the mirror image
        { 0.25, 0.0, 1.0, 0.0, 1.0, 0.8230387047047818 },
        { 0.8, 0.5, 0.5, 0.0, 0.0, 0.65 },                  // halved around the centre
        { 0.2, 0.5, 1.0, 0.1, 0.3, 0.1929459259302101 },    // below the centre: bent away from it, plus the offset
        { 0.9, 0.25, 2.0, -0.2, -0.7, 0.8314339767072285 }, // the reach is the longer side (0.75)
        { 0.6, 1.0, 1.0, 0.0, 0.4, 0.28604890907218294 },   // centre at the top
        { 0.3, 0.5, -1.0, 0.0, 0.0, 0.7 },                  // a negative multiplier mirrors around the centre
        { 0.5, 0.5, 2.0, 0.0, 0.9, 0.5 },                   // a value on the centre stays there
        { 0.1, 0.0, 2.0, 0.9, 0.0, 1.0 },                   // limited to the range
    };
    for (const auto& c : cases)
        CHECK (std::abs (flLevelScale (c.x, c.center, c.multiply, c.offset, c.tension) - c.expected) < 2e-6);

    // on the notes: velocity with tension, the multiplier ramping through time (LYNX's extra)
    Pattern p;
    for (int i = 0; i < 3; ++i)
    {
        Note n;
        n.start = i * 96;
        n.velocity = 0.5f;
        p.notes.push_back (n);
    }
    scaleLevels (p, { 0, 1, 2 }, Level::velocity, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    CHECK (std::abs (p.notes[0].velocity - 0.8490525f) < 1e-5f);
    CHECK (std::abs (p.notes[1].velocity - 0.8490525f * 0.5f) < 1e-5f);
    CHECK (p.notes[2].velocity == 0.0f);

    // without tension and with the centre on the level's centre line it is the tool as it always was
    Pattern a, b;
    for (int i = 0; i < 4; ++i)
    {
        Note n;
        n.start = i * 48;
        n.pan = -0.6f + 0.4f * (float) i;
        a.notes.push_back (n);
    }
    b = a;
    scaleLevels (a, { 0, 1, 2, 3 }, Level::pan, 0.5f, 1.5f, 0.05f);
    scaleLevels (b, { 0, 1, 2, 3 }, Level::pan, 0.5f, 1.5f, 0.05f, 0.5f, 0.0f);
    for (int i = 0; i < 4; ++i)
        CHECK (std::abs (a.notes[(std::size_t) i].pan - b.notes[(std::size_t) i].pan) < 1e-5f);
}

// Randomise: Mod X, Mod Y and Release strengths (FL's randomizer has the three beside velocity, pan and pitch). The
// release draw comes last, so a seed gives the older amounts the values it always gave.
TEST (curves190bw_randomise_reaches_mod_and_release)
{
    using namespace lynx;
    using namespace lynx::notetools;
    Pattern p;
    for (int i = 0; i < 8; ++i)
    {
        Note n;
        n.start = i * 24;
        n.release = 0.5f;
        n.modX = 0.5f;
        n.modY = 0.5f;
        p.notes.push_back (n);
    }
    std::vector<int> all { 0, 1, 2, 3, 4, 5, 6, 7 };
    RandomiseOptions o;
    o.velocity = 0.3f;
    o.seed = 7;
    auto before = p;
    randomise (before, all, o);
    o.release = 0.4f;
    o.modX = 0.2f;
    o.modY = 0.1f;
    auto after = p;
    randomise (after, all, o);
    int movedRelease = 0, movedX = 0, movedY = 0;
    for (std::size_t i = 0; i < 8; ++i)
    {
        CHECK (after.notes[i].velocity == before.notes[i].velocity); // the same draw as without the new amounts
        CHECK (before.notes[i].release == 0.5f && before.notes[i].modX == 0.5f);
        CHECK (std::abs (after.notes[i].release - 0.5f) <= 0.4f + 1e-6f && std::abs (after.notes[i].modX - 0.5f) <= 0.2f + 1e-6f
               && std::abs (after.notes[i].modY - 0.5f) <= 0.1f + 1e-6f);
        movedRelease += after.notes[i].release != 0.5f;
        movedX += after.notes[i].modX != 0.5f;
        movedY += after.notes[i].modY != 0.5f;
    }
    CHECK (movedRelease >= 6 && movedX >= 6 && movedY >= 6);
}

// Round 190ct (live FL check 190cq, shot curves190bw-05f): the limiter's list is FL's System/Config/Chords.map —
// (none), 66 chords in six groups, 22 scales — and the allowed keys are marked FL's way (octaves −1…10, steps added).
TEST (curves190ct_limit_list_is_fls_chords_and_scales)
{
    using namespace lynx;
    using namespace lynx::notetools;
    const auto& list = limitRestrictions();
    CHECK (list.size() == 89);
    int chords = 0, scaleCount = 0, groups = 0;
    for (const auto& e : list)
    {
        chords += e.kind == LimitRestriction::chord;
        scaleCount += e.kind == LimitRestriction::scale;
        groups += e.groupStart && e.kind == LimitRestriction::chord;
    }
    CHECK (chords == 66 && scaleCount == 22 && groups == 6);
    CHECK (list[0].kind == LimitRestriction::none && std::string (list[0].name) == "(none)");
    CHECK (std::string (list[1].name) == "Major" && list[1].kind == LimitRestriction::chord && list[1].groupStart);
    CHECK (std::string (list[9].name) == "tri" && std::string (list[10].name) == "6" && list[10].groupStart);
    CHECK (std::string (list[66].name) == "m-Maj13" && std::string (list[67].name) == "Major" && list[67].kind == LimitRestriction::scale
           && list[67].groupStart);
    CHECK (std::string (list[88].name) == "Locrian");
    const char* flScales[] = { "Major", "Major pentatonic", "Major bebop", "Minor Harmonic", "Minor Melodic", "Minor pentatonic",
                               "Minor Natural (Aeolian)", "Minor Neapolitan", "Minor Hungarian", "Whole tone", "Diminished",
                               "Dominant bebop", "Jap in sen", "Blues", "Arabic", "Enigmatic", "Neapolitan", "Dorian", "Phrygian",
                               "Lydian", "Mixolydian", "Locrian" };
    for (int i = 0; i < 22; ++i)
        CHECK (limitRestrictionIndex (flScales[i], LimitRestriction::scale) == 67 + i);
    CHECK (limitRestrictionIndex ("Major", LimitRestriction::chord) == 1 && limitRestrictionIndex ("nope", LimitRestriction::chord) == 0);

    const auto pitchClasses = [] (int root, int index) {
        const auto allowed = limitAllowedKeys (root, index);
        std::set<int> pcs;
        for (int k = 24; k < 120; ++k) // away from the ends
            if (allowed[(std::size_t) k])
                pcs.insert (k % 12);
        return pcs;
    };
    CHECK (pitchClasses (0, limitRestrictionIndex ("Major", LimitRestriction::scale)) == (std::set<int> { 0, 2, 4, 5, 7, 9, 11 }));
    CHECK (pitchClasses (0, limitRestrictionIndex ("Major", LimitRestriction::chord)) == (std::set<int> { 0, 4, 7 }));
    CHECK (pitchClasses (2, limitRestrictionIndex ("9", LimitRestriction::chord)) == (std::set<int> { 2, 6, 9, 0, 4 })); // D 9th
    CHECK (pitchClasses (0, limitRestrictionIndex ("Jap in sen", LimitRestriction::scale)) == (std::set<int> { 0, 1, 5, 7, 10 }));
    // FL's marking at the bottom: m-Maj13 on B reaches pitch class G# only through its +21 step, so key 8 (two
    // octaves below the first B it starts from) stays out while 20 is in
    const auto mm13 = limitAllowedKeys (11, limitRestrictionIndex ("m-Maj13", LimitRestriction::chord));
    CHECK (! mm13[8] && mm13[20] && mm13[11] && mm13[128] && ! mm13[129]);
    const auto none = limitAllowedKeys (0, 0);
    CHECK (std::all_of (none.begin(), none.end(), [] (bool b) { return b; }));

    // a chord restriction in the limiter: C major triad, Above / Under on a tie
    Pattern p;
    for (int k : { 61, 62, 65, 66, 69 })
    {
        Note n;
        n.key = k;
        n.length = 24;
        p.notes.push_back (n);
    }
    std::vector<int> all { 0, 1, 2, 3, 4 };
    LimitOptions o;
    o.lowKey = 0;
    o.highKey = 127;
    o.scale = limitRestrictionIndex ("Major", LimitRestriction::chord);
    auto q = p;
    limit (q, all, o);
    CHECK (q.notes[0].key == 60 && q.notes[1].key == 64 && q.notes[2].key == 64 && q.notes[3].key == 67 && q.notes[4].key == 67);
    o.snap = LimitOptions::under;
    q = p;
    limit (q, all, o);
    CHECK (q.notes[1].key == 60 && q.notes[0].key == 60 && q.notes[4].key == 67);
}

// Round 190ct (live FL check 190cq, shot curves190bw-07b): FL's Bipolar box covers every level. Off, the levels only
// go up (pan to the right, pitch up) and timing only later; on, they spread both ways, the same draws as before.
TEST (curves190ct_randomise_bipolar_off_moves_every_level_up)
{
    using namespace lynx;
    using namespace lynx::notetools;
    Pattern p;
    for (int i = 0; i < 32; ++i)
    {
        Note n;
        n.start = 960 + i * 96;
        n.length = 48;
        n.velocity = 0.5f;
        n.pan = 0.0f;
        n.release = 0.5f;
        n.finePitch = 0.0f;
        n.modX = 0.5f;
        n.modY = 0.5f;
        p.notes.push_back (n);
    }
    std::vector<int> all;
    for (int i = 0; i < 32; ++i)
        all.push_back (i);
    RandomiseOptions o;
    o.velocity = 0.4f;
    o.pan = 0.5f;
    o.finePitch = 0.5f;
    o.modX = 0.4f;
    o.modY = 0.4f;
    o.release = 0.4f;
    o.timing = 40;
    o.seed = 11;

    auto both = p;
    randomise (both, all, o);
    o.bipolar = false;
    auto up = p;
    randomise (up, all, o);

    int downBoth = 0, upBoth = 0, wrong = 0, moved = 0;
    for (std::size_t i = 0; i < p.notes.size(); ++i)
    {
        const auto& b = both.notes[i];
        const auto& u = up.notes[i];
        downBoth += (b.velocity < 0.5f) + (b.pan < 0.0f) + (b.finePitch < 0.0f) + (b.modX < 0.5f) + (b.modY < 0.5f) + (b.release < 0.5f);
        upBoth += (b.velocity > 0.5f) + (b.pan > 0.0f) + (b.finePitch > 0.0f) + (b.modX > 0.5f) + (b.modY > 0.5f) + (b.release > 0.5f);
        wrong += (u.velocity < 0.5f) + (u.pan < 0.0f) + (u.finePitch < 0.0f) + (u.modX < 0.5f) + (u.modY < 0.5f) + (u.release < 0.5f)
                 + (u.start < p.notes[i].start);
        moved += (u.velocity > 0.5f) + (u.pan > 0.0f) + (u.finePitch > 0.0f) + (u.modX > 0.5f) + (u.modY > 0.5f) + (u.release > 0.5f)
                 + (u.start > p.notes[i].start);
        // pan moves as far as its bipolar draw, only to the right
        CHECK (std::abs (u.pan - std::abs (b.pan)) < 1e-6f);
        CHECK (u.velocity <= 0.9f + 1e-6f && u.pan <= 1.0f && u.finePitch <= 60.0f + 1e-3f);
    }
    CHECK (wrong == 0);
    CHECK (moved >= 32 * 7 - 8);
    CHECK (downBoth > 40 && upBoth > 40); // Bipolar on: both ways
}
