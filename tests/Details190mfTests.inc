// Round details5_190mf (0.1.30, details pass 5): FL's normal meter law as the Pattern layout's default mixer meter scale.

#include "core/MeterBallistics.h"
#include "core/MeterScale.h"
#include "core/LoopKeywords.h"
#include "core/LayoutStyle.h"
#include "core/GrossBeat.h"
#include "core/FruityMultiband.h"
#include "core/Effects.h"

#include <algorithm>
#include <cmath>

// FL's normal law (capture 190lx A1, the hint bar's dB at every 8 px of FL's Master meter, H = 346 px, bottom 555):
// the default in the Pattern layout; Clip-launch / Simple keep the straight axis; the alternate option wins everywhere
TEST (details190mf_fl_normal_meter_law)
{
    using namespace lynx::meterscale;
    namespace ls = lynx::layoutstyle;
    ls::setCurrent (ls::Style::pattern);
    alternate() = false;
    CHECK (current() == Law::flNormal);
    // FL's ticks: 0 dB at y 262, -6 dB at y 314 (the Master meter's two internal ticks)
    CHECK (std::abs (positionOfDb (0.0f, Law::flNormal) - (555.0f - 262.0f) / 346.0f) < 0.004f);
    CHECK (std::abs (positionOfDb (-6.0f, Law::flNormal) - (555.0f - 314.0f) / 346.0f) < 0.004f);
    // every hint reading of the capture within 0.25 px (y, dB)
    static const float readings[][2] = { { 216, 5.19f }, { 248, 1.59f }, { 264, -0.23f }, { 304, -4.81f }, { 352, -10.44f },
                                         { 400, -16.38f }, { 448, -22.96f }, { 496, -31.25f }, { 528, -39.98f }, { 544, -48.70f } };
    for (auto& r : readings)
        CHECK (std::abs (positionOfDb (r[1], Law::flNormal) * 346.0f - (555.0f - r[0])) < 0.25f);
    // the top is about +6 dB, silence the bottom; the hint's reading is the inverse
    CHECK (std::abs (dbAtPosition (1.0f, Law::flNormal) - 5.97f) < 0.05f);
    CHECK (position (0.0f, Law::flNormal) == 0.0f);
    float last = -1.0f;
    for (float db = -80.0f; db <= 5.5f; db += 0.5f)
    {
        const auto p = positionOfDb (db, Law::flNormal);
        CHECK (p > last && p >= 0.0f && p <= 1.0f);
        CHECK (std::abs (dbAtPosition (p, Law::flNormal) - db) < 0.01f);
        CHECK (std::abs (position (std::pow (10.0f, db / 20.0f), Law::flNormal) - p) < 1.0e-5f);
        last = p;
    }
    // the marks beside a tall Pattern meter are FL's (every 3 dB, +3 … -39), never overlapping
    const auto tall = lynx::meters::scaleMarks (600.0f, 11.0f);
    CHECK (tall.front() == 3 && std::find (tall.begin(), tall.end(), -48) == tall.end());
    for (auto db : { 3, 0, -3, -6, -9, -12, -18, -24 })
        CHECK (std::find (tall.begin(), tall.end(), db) != tall.end());
    for (std::size_t i = 1; i < tall.size(); ++i)
        CHECK ((lynx::meters::scalePosition ((float) tall[i - 1]) - lynx::meters::scalePosition ((float) tall[i])) * 600.0f >= 11.0f - 1e-3f);
    // the other layouts keep their straight axis; the alternate option is one switch for every layout
    ls::setCurrent (ls::Style::clipLaunch);
    CHECK (current() == Law::straight && std::abs (lynx::meters::scalePosition (-60.0f)) < 1.0e-6f);
    ls::setCurrent (ls::Style::simple);
    CHECK (current() == Law::straight);
    alternate() = true;
    CHECK (current() == Law::flAlternate);
    ls::setCurrent (ls::Style::pattern);
    CHECK (current() == Law::flAlternate);
    alternate() = false;
    // round 190lx's bool forms are unchanged
    CHECK (std::abs (positionOfDb (-60.0f, false)) < 1.0e-6f && std::abs (positionOfDb (0.0f, true) - (555.0f - 262.0f) / 346.0f) < 0.004f);
}

// BandLab's loop rows start with the length in bars (DS-41): the file's length at the loop's own tempo
TEST (details190mf_loop_bars)
{
    using lynx::loopkeys::loopBars;
    CHECK (loopBars (8.0, 120.0) == 4);           // 16 beats
    CHECK (loopBars (6.4429, 149.0) == 4);        // BandLab's "4 bars · … · 149bpm" (16 beats = 6.443 s)
    CHECK (loopBars (2.0, 120.0) == 1);
    CHECK (loopBars (0.9, 120.0) == 0);           // under half a bar: no length shown
    CHECK (loopBars (8.0, 0.0) == 0 && loopBars (0.0, 120.0) == 0 && loopBars (-1.0, 120.0) == 0);
    CHECK (loopBars (6.0, 120.0, 3) == 4);        // 12 beats in 3/4
    CHECK (loopBars (8.3, 120.0) == 4);           // a tail of a few ms rounds to the bar
}

// Gross Beat's "Slide remaining points" (DP-6): the later points move with the dragged one, inside the bar, in order;
// the bar's end stays
TEST (details190mf_gross_beat_slide_points)
{
    using namespace lynx::grossbeat;
    std::vector<EditPoint> pts { { 0.0, 1.0f, 0.0f, 2 }, { 1.0, 0.5f, 0.0f, 2 }, { 2.0, 0.2f, 0.0f, 2 }, { 3.0, 0.8f, 0.0f, 2 }, { 4.0, 1.0f, 0.0f, 2 } };
    auto a = pts;
    slidePoint (a, 1, 1.5, 0.4f);
    CHECK (a[1].x == 1.5 && a[1].y == 0.4f && a[2].x == 2.5 && a[3].x == 3.5 && a[4].x == 4.0);
    auto b = pts;
    slidePoint (b, 1, 2.5, 0.5f); // later points pile up at the bar's end
    CHECK (b[1].x == 2.5 && b[2].x == 3.5 && b[3].x == 4.0 && b[4].x == 4.0);
    auto c = pts;
    slidePoint (c, 2, 0.5, 0.2f); // not before the point before it; the later ones move left with it
    CHECK (c[2].x == 1.0 && c[3].x == 2.0 && c[4].x == 4.0);
    auto d = pts;
    slidePoint (d, 0, 3.0, 0.3f); // the first point only up and down
    CHECK (d[0].x == 0.0 && d[0].y == 0.3f && d[1].x == 1.0);
    auto e = pts;
    movePoint (e, 1, 1.5, 0.5f); // (slide off: between its neighbours, the others stay)
    CHECK (e[1].x == 1.5 && e[2].x == 2.0);
}

// Gross Beat's Step editing (DP-6, capture 190mf A1): a flat hold from x0 to x1, the points inside replaced, back to the
// old level at x1; a step from 0 moves the first point; one to the bar's end sets the last
TEST (details190mf_gross_beat_step_draw)
{
    using namespace lynx::grossbeat;
    const std::vector<EditPoint> flat { { 0.0, 1.0f, 0.0f, 2 }, { 1.0, 0.8f, 0.0f, 2 }, { 1.5, 0.6f, 0.0f, 2 }, { 4.0, 1.0f, 0.0f, 2 } };
    auto a = flat;
    stepDraw (a, 0.5, 2.0, 0.25f);
    CHECK (a.size() == 4 && a[1].x == 0.5 && a[1].y == 0.25f && a[1].type == 2 && a[2].x == 2.0 && a[3].x == 4.0);
    Slot s;
    setPoints (s, a);
    CHECK (std::abs (levelAt (s, 1.0) - 0.25f) < 1.0e-4f && std::abs (levelAt (s, 1.9) - 0.25f) < 1.0e-4f);
    auto b = flat;
    stepDraw (b, 0.0, 0.5, 0.5f); // from the bar's start: the first point takes the level
    CHECK (b.front().x == 0.0 && b.front().y == 0.5f && b[1].x == 0.5);
    auto c = flat;
    stepDraw (c, 3.0, 4.0, 0.3f); // to the bar's end
    CHECK (c.back().x == 4.0 && c.back().y == 0.3f && c[c.size() - 2].x == 3.0);
    auto d = flat; // a staircase: cell after cell
    for (int k = 0; k < 4; ++k)
        stepDraw (d, 2.0 + 0.125 * k, 2.125 + 0.125 * k, 0.1f * (float) k);
    int inRange = 0;
    for (const auto& p : d)
        inRange += p.x >= 2.0 - 1.0e-9 && p.x <= 2.5 + 1.0e-9 ? 1 : 0;
    CHECK (inRange == 5); // four step starts and the return at 2.5
}

// Fruity Multiband Compressor's band meters (DP-29): measured only while shown, the audio bit-identical either way
TEST (details190mf_multiband_band_meters)
{
    lynx::EffectParams params {};
    auto plain = lynx::flmb::make (nullptr), metered = lynx::flmb::make (nullptr);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::fruityMultiband);
    params = slot.params;
    plain->prepare (44100.0);
    metered->prepare (44100.0);
    metered->setMetersShown (true);
    CHECK (plain->controlOutput (0) == 0.0f && plain->controlOutput (2) == 1.0f);
    bool same = true;
    for (int block = 0; block < 40; ++block)
    {
        std::vector<float> l1 (256), r1 (256);
        for (int i = 0; i < 256; ++i)
            l1[(std::size_t) i] = r1[(std::size_t) i] = 0.9f * std::sin (0.02f * (float) (block * 256 + i));
        auto l2 = l1, r2 = r1;
        metered->setMetersShown (true);
        plain->process (l1.data(), r1.data(), 256, params, 120.0);
        metered->process (l2.data(), r2.data(), 256, params, 120.0);
        same = same && l1 == l2 && r1 == r2;
    }
    CHECK (same);
    float any = 0.0f;
    for (int b = 0; b < 3; ++b)
    {
        any = std::max (any, metered->controlOutput (b * 3));
        CHECK (metered->controlOutput (b * 3 + 2) > 0.0f && metered->controlOutput (b * 3 + 2) <= 1.0f);
    }
    CHECK (any > 0.05f); // (a 140 Hz sine lands in the low band)
    CHECK (plain->controlOutput (0) == 0.0f); // (not shown: nothing measured)
}
