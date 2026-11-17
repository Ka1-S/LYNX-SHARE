// Round 190kl (issue #197, Peak Controller): the live display scrolls by the clock (core/LiveHistory.h), LYNX's extra
// LFO shapes (core/PeakShapes.h), and projects without them render and save exactly as before.
#include "core/LiveHistory.h"
#include "core/PeakShapes.h"

#include <set>

namespace u190kl
{
using H = lynx::livehistory::History<3, 240>;

// calls at the given times (ms): the columns added in all
inline int columnsFor (const std::vector<double>& times)
{
    H h;
    int added = 0;
    for (const auto t : times)
        added += h.advance (t, { 0.5f, 0.5f, 0.5f });
    return added;
}

// the Peak Controller's LFO output (2) after `blocks` blocks of silence, playing from bar 1 at 120 BPM
inline std::vector<float> lfoTrace (const lynx::EffectSlot& slot, int blocks, bool playing)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (48000.0);
    fx->reset();
    fx->setPlaying (playing);
    if (playing)
        fx->setSongPosition (0.0);
    std::vector<float> l (256, 0.0f), r (256, 0.0f), out;
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (l.begin(), l.end(), b % 8 == 0 ? 0.8f : 0.0f); // (a kick every 8 blocks: the peak moves too)
        std::fill (r.begin(), r.end(), b % 8 == 0 ? 0.8f : 0.0f);
        fx->process (l.data(), r.data(), 256, slot.params, 120.0);
        out.push_back (fx->controlOutput (0));
        out.push_back (fx->controlOutput (1));
        out.push_back (fx->controlOutput (2));
    }
    return out;
}

// the LFO as the Peak Controller computed it before round 190kl (FL's five shapes; REA_rodada168r §4), written out
// again here: the reference the default path must still match bit for bit
inline double oldWave (int shape, double phase, std::uint32_t period)
{
    constexpr auto twoPi = 6.283185307179586;
    double w = 0.0;
    switch (shape)
    {
        case 1: w = 1.0 - 2.0 * std::abs (2.0 * phase - 1.0); w = -w; break;
        case 2: w = phase < 0.5 ? 1.0 : -1.0; break;
        case 3: w = 2.0 * phase - 1.0; break;
        case 4:
        {
            auto h = period * 2654435761u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            w = (double) (h & 0xffffff) / (double) 0x800000 - 1.0;
            break;
        }
        default: w = std::sin (twoPi * phase); break;
    }
    return (w + 1.0) * 0.5;
}

inline double oldTension (double x, double t)
{
    if (std::abs (t) < 1e-9)
        return x;
    const auto k = (std::pow (1001.0, std::abs (t) / 128.0) - 1.0) * 0.1;
    const auto y = t > 0.0 ? k - (std::pow (k + 1.0, 1.0 - x) - 1.0) : std::pow (k + 1.0, x) - 1.0;
    return y / k;
}

// the old LFO output (no Ramp) after each 256-sample block at 48 kHz, 120 BPM, free running from phase 0
inline std::vector<float> oldFreeLfo (const lynx::EffectParams& p, int blocks)
{
    std::vector<float> out;
    double freePhase = 0.0;
    const auto beats = 120.0 / 60.0 * 256 / 48000.0;
    for (int b = 0; b < blocks; ++b)
    {
        const auto lfoAmountRaw = (std::clamp ((double) p[5], 0.0, 1.0) * 2.0 - 1.0) * 256.0;
        const auto lfoTension = (std::clamp ((double) p[6], 0.0, 1.0) * 2.0 - 1.0) * 128.0;
        const auto period = std::max (3.0 / 96.0, (std::pow (1001.0, (200.0 + std::clamp ((double) p[7], 0.0, 1.0) * 65336.0) / 65536.0) - 1.0) * 0.128);
        const auto shape = std::clamp ((int) std::lround (p[8] * 4.0f), 0, 4);
        freePhase += beats / period;
        const auto cycles = freePhase + std::clamp ((double) p[9], 0.0, 1.0);
        const auto whole = std::floor (cycles);
        const auto amount = (lfoAmountRaw < 0.0 ? -1.0 : 1.0) * (std::pow (6.0, std::abs (lfoAmountRaw) / 256.0) - 1.0) * 0.2;
        out.push_back ((float) std::clamp (p[4] + amount * oldTension (oldWave (shape, cycles - whole, (std::uint32_t) (std::int64_t) whole), lfoTension), 0.0, 1.0));
    }
    return out;
}
} // namespace u190kl

TEST (u190kl_live_display_scrolls_by_the_clock_whatever_the_timer_does)
{
    using namespace u190kl;
    // 10 s of calls: a steady 30 Hz timer, a jittery one, and one starved while a knob is dragged (calls every
    // 120-250 ms for 4 s, as when JUCE drops timer calls): the same number of columns, one per 33 ms
    std::vector<double> steady, jitter, starved;
    for (double t = 1000.0; t <= 11000.0; t += 33.0)
        steady.push_back (t);
    for (int i = 0; 1000.0 + i * 33.0 <= 11000.0; ++i)
        jitter.push_back (1000.0 + i * 33.0 + ((i * 7919) % 17 - 8)); // ±8 ms
    for (double t = 1000.0; t < 4000.0; t += 33.0)
        starved.push_back (t);
    for (double t = 4000.0; t < 8000.0; t += 120.0 + std::fmod (t, 130.0))
        starved.push_back (t);
    for (double t = 8000.0; t <= 11000.0; t += 33.0)
        starved.push_back (t);
    const auto expected = 1 + (int) std::floor ((11000.0 - 1000.0 + 16.5) / 33.0);
    const auto a = columnsFor (steady), b = columnsFor (jitter), c = columnsFor (starved);
    CHECK (std::abs (a - expected) <= 1);
    CHECK (std::abs (b - expected) <= 1);
    CHECK (std::abs (c - expected) <= 1);
    // a steady or jittery 30 Hz caller gets exactly one column per call (the clock sits half a column off its ticks):
    // no 0, 2, 0, 2 stutter
    {
        H h;
        for (std::size_t i = 0; i < jitter.size(); ++i)
            CHECK (h.advance (jitter[i], { 0.0f, 0.0f, 0.0f }) == 1);
    }
}

TEST (u190kl_live_display_fills_a_late_call_with_a_straight_line)
{
    u190kl::H h;
    CHECK (h.advance (0.0, { 0.0f, 0.0f, 0.0f }) == 1);
    CHECK (h.advance (33.0, { 0.0f, 0.0f, 0.0f }) == 1);
    // 4 columns late: 0.25, 0.5, 0.75, 1
    CHECK (h.advance (33.0 * 5, { 1.0f, 0.5f, 0.0f }) == 4);
    CHECK (h.count == 6);
    for (int k = 1; k <= 4; ++k)
    {
        CHECK (std::abs (h.at (1 + k, 0) - 0.25f * (float) k) < 1e-6f);
        CHECK (std::abs (h.at (1 + k, 1) - 0.125f * (float) k) < 1e-6f);
        CHECK (h.at (1 + k, 2) == 0.0f);
    }
    CHECK (h.advance (33.0 * 5 + 10.0, { 0.3f, 0.3f, 0.3f }) == 0); // (same column: nothing moves, no repaint)
    // a gap longer than the picture: a full picture, ending on the new value
    CHECK (h.advance (33.0 * 5 + 60000.0, { 0.9f, 0.9f, 0.9f }) == 240);
    CHECK (h.count == 240 && std::abs (h.at (239, 0) - 0.9f) < 1e-6f);
    // a clock that went back starts again
    CHECK (h.advance (10.0, { 0.1f, 0.1f, 0.1f }) == 1);
}

TEST (u190kl_extra_lfo_shapes_have_their_shape)
{
    namespace ps = lynx::peakshapes;
    for (int id = 1; id <= ps::count; ++id)
    {
        CHECK (std::string (ps::name (id)).size() > 0);
        CHECK (ps::idOf (ps::valueOf (id)) == id);
        for (int i = 0; i <= 1000; ++i)
            for (std::uint32_t period : { 0u, 1u, 7u, 123456u })
            {
                const auto v = ps::wave (id, i / 1000.0, period);
                CHECK (v >= -1e-12 && v <= 1.0 + 1e-12);
            }
    }
    CHECK (ps::idOf (0.0f) == 0 && ps::idOf (1.0f) == 0 && ps::valueOf (0) == 0.0f); // (unknown ids: FL's shape)
    auto w = [] (int id, double x) { return ps::wave (id, x, 3u); };
    CHECK (w (ps::rampDown, 0.0) == 1.0 && std::abs (w (ps::rampDown, 0.5) - 0.5) < 1e-12);
    CHECK (w (ps::expRise, 0.0) == 0.0 && w (ps::expRise, 0.5) < 0.2 && w (ps::expRise, 0.999) > 0.98);
    CHECK (w (ps::expFall, 0.0) == 1.0 && w (ps::expFall, 0.5) < 0.2);
    CHECK (std::abs (w (ps::triSkew, 0.25) - 1.0) < 1e-12 && w (ps::triSkew, 0.0) == 0.0 && std::abs (w (ps::triSkew, 0.625) - 0.5) < 1e-12);
    CHECK (std::abs (w (ps::halfSine, 0.5) - 1.0) < 1e-12 && w (ps::halfSine, 0.0) == 0.0);
    CHECK (w (ps::pulse25, 0.1) == 1.0 && w (ps::pulse25, 0.3) == 0.0);
    CHECK (w (ps::steps4, 0.3) == 1.0 / 3.0 && w (ps::steps4, 0.8) == 1.0 && w (ps::steps4, 0.1) == 0.0);
    CHECK (w (ps::steps8, 0.13) == 1.0 / 7.0 && w (ps::steps16, 0.999) == 1.0);
    {
        // steps: exactly 4 / 8 / 16 distinct levels per period
        for (const auto [id, n] : { std::pair { (int) ps::steps4, 4 }, { (int) ps::steps8, 8 }, { (int) ps::steps16, 16 } })
        {
            std::set<double> levels;
            for (int i = 0; i < 1000; ++i)
                levels.insert (w (id, i / 1000.0));
            CHECK ((int) levels.size() == n);
        }
    }
    // smooth random: continuous across the period boundary, and not the same value every period
    for (std::uint32_t p = 0; p < 50; ++p)
        CHECK (std::abs (ps::wave (ps::smoothRandom, 0.999999, p) - ps::wave (ps::smoothRandom, 0.0, p + 1)) < 1e-5);
    CHECK (ps::wave (ps::smoothRandom, 0.0, 1) != ps::wave (ps::smoothRandom, 0.0, 2));
    // random x4: four held values per period
    {
        std::set<double> held;
        for (int i = 0; i < 1000; ++i)
            held.insert (ps::wave (ps::randomX4, i / 1000.0, 9u));
        CHECK (held.size() == 4);
    }
}

TEST (u190kl_peak_controller_plays_the_extra_shape_and_fl_shapes_stay_bit_identical)
{
    using namespace u190kl;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::peakController);
    CHECK (slot.params[(std::size_t) lynx::peakshapes::extraShapeParam] == 0.0f);
    slot.params[5] = 0.9f; // LFO amount up
    slot.params[7] = 0.3f; // a fast LFO
    // every FL shape, free running (stopped), with and without tension: the LFO output equals the old code's exactly
    for (int shape = 0; shape <= 4; ++shape)
        for (const auto tension : { 0.5f, 0.8f, 0.2f })
        {
            auto s = slot;
            s.params[8] = (float) shape / 4.0f;
            s.params[6] = tension;
            const auto trace = lfoTrace (s, 400, false);
            const auto old = oldFreeLfo (s.params, 400);
            bool same = true;
            for (int b = 0; b < 400; ++b)
                same = same && trace[(std::size_t) (b * 3 + 2)] == old[(std::size_t) b];
            CHECK (same);
        }
    // an extra shape changes the LFO (and the sum), not the peak; FL's Shape value under it does not matter
    auto fl = slot;
    auto extra = slot;
    extra.params[(std::size_t) lynx::peakshapes::extraShapeParam] = lynx::peakshapes::valueOf (lynx::peakshapes::rampDown);
    auto extraOtherFl = extra;
    extraOtherFl.params[8] = 0.75f;
    const auto a = lfoTrace (fl, 300, true), b = lfoTrace (extra, 300, true), c = lfoTrace (extraOtherFl, 300, true);
    bool lfoDiffers = false, peakSame = true;
    for (int k = 0; k < 300; ++k)
    {
        lfoDiffers = lfoDiffers || a[(std::size_t) (k * 3 + 2)] != b[(std::size_t) (k * 3 + 2)];
        peakSame = peakSame && a[(std::size_t) (k * 3 + 1)] == b[(std::size_t) (k * 3 + 1)];
    }
    CHECK (lfoDiffers);
    CHECK (peakSame);
    CHECK (b == c);
    // ramp down while playing: the LFO falls through each period (song-locked like FL's shapes)
    {
        auto s = extra;
        s.params[6] = 0.5f; // no tension
        const auto t = lfoTrace (s, 40, true);
        int falls = 0, rises = 0;
        for (int k = 1; k < 40; ++k)
        {
            const auto d = t[(std::size_t) (k * 3 + 2)] - t[(std::size_t) ((k - 1) * 3 + 2)];
            falls += d < 0.0f;
            rises += d > 0.0f;
        }
        CHECK (falls > rises);
    }
}

TEST (u190kl_projects_without_extra_shapes_save_as_before)
{
    // the hidden parameter is not written while it is 0: a Peak Controller saves its twelve values, as before
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::peakController);
    CHECK (lynx::savedEffectParamCount (slot) == 12);
    slot.params[(std::size_t) lynx::peakshapes::extraShapeParam] = lynx::peakshapes::valueOf (lynx::peakshapes::steps8);
    CHECK (lynx::savedEffectParamCount (slot) == 13);
    // and the stored value reads back as the same id through the file's 4-decimal rounding
    const auto saved = (float) lynx::savedEffectParamValue (slot.params[12]);
    CHECK (lynx::peakshapes::idOf (saved) == lynx::peakshapes::steps8);
    for (int id = 1; id <= lynx::peakshapes::count; ++id)
        CHECK (lynx::peakshapes::idOf ((float) (std::round (lynx::peakshapes::valueOf (id) * 10000.0) / 10000.0)) == id);
    // FL's parameter list is unchanged (automation, links and the FL import see FL's twelve controls)
    CHECK (lynx::effectInfo (lynx::EffectKind::peakController).params.size() == 12);
}

TEST (u190kl_a_docked_window_placed_again_keeps_a_size_within_a_pixel)
{
    using lynx::dockstack::keepsSize;
    // Gross Beat at 150 %: 1047 units = 1570.5 px; made at 1570 px, placed at 1571: kept (no resize on the first move)
    CHECK (keepsSize (1570, 1137, 1047, 758, 1.5));
    CHECK (keepsSize (1571, 1137, 1047, 758, 1.5));
    // a real resize (one unit = 1.5 px) is not kept; nor one pixel at 100 %
    CHECK (! keepsSize (1570, 1137, 1048, 758, 1.5));
    CHECK (! keepsSize (1570, 1137, 1046, 758, 1.5));
    CHECK (! keepsSize (400, 300, 401, 300, 1.0));
    CHECK (keepsSize (400, 300, 400, 300, 1.0));
    // every size at the usual scales: the exact rounding is always kept, a one-unit change never is
    for (const auto scale : { 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 0.8, 0.9, 1.875 })
        for (int w = 50; w < 1300; w += 7)
        {
            const auto p = lynx::dockstack::physicalPlace (0, 0, w, 211, scale);
            CHECK (keepsSize (p.w, p.h, w, 211, scale));
            CHECK (! keepsSize (p.w, p.h, w + 1, 211, scale) || scale < 1.5); // (from 150 % a unit is 1.5 px or more)
        }
    // a window resized unit by unit (its edge dragged) never lags its exact size by a pixel or more
    for (const auto scale : { 0.8, 0.9, 1.1, 1.25, 1.5, 1.75 })
    {
        auto native = lynx::dockstack::physicalPlace (0, 0, 300, 200, scale).w;
        for (int w = 300; w < 700; ++w)
        {
            if (! keepsSize (native, (int) std::lround (200 * scale), w, 200, scale))
                native = lynx::dockstack::physicalPlace (0, 0, w, 200, scale).w;
            CHECK (std::abs (native - w * scale) < 1.0);
        }
    }
}
