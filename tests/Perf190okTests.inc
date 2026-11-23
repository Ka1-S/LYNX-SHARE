// Round 190ok (0.1.31 perf round 2): safe overloads (core/OverloadGuard.h, FL's Settings › Audio › "Safe overloads",
// TOP25 P24).

#include <limits>

#include "core/OverloadGuard.h"

TEST (perf190ok_overload_guard_engages_only_when_audio_and_ui_both_starve)
{
    using namespace lynx;
    // 64 samples at 48 kHz: a callback every 1.333 ms
    constexpr double budget = 64.0 / 48.0;
    auto run = [&] (OverloadGuard& g, double& now, double seconds, double used, bool uiOk) {
        double yielded = 0.0, pulse = now;
        for (double end = now + seconds * 1000.0; now < end;)
        {
            if (uiOk && now - pulse >= 33.0)
                g.uiPulse (pulse = now);
            const auto y = g.afterCallback (now, used, budget);
            yielded += y;
            now += std::max (budget, used) + y;
        }
        return yielded;
    };
    {
        // overloaded audio, but the UI keeps its 30 Hz pulse (a PC with spare cores): never engaged
        OverloadGuard g;
        double now = 1000.0;
        g.uiPulse (now);
        CHECK (run (g, now, 3.0, 2.0 * budget, true) == 0.0);
        CHECK (! g.engaged() && g.timesEngaged() == 0);
    }
    {
        // the audio keeps up while the UI is late (a heavy screen): never engaged
        OverloadGuard g;
        double now = 1000.0;
        g.uiPulse (now);
        CHECK (run (g, now, 3.0, 0.7 * budget, false) == 0.0);
        CHECK (! g.engaged());
    }
    {
        // both: engaged within the first window or two; every callback then yields 1.5 times its processing time or more while the UI stays late (0.5 … 10 ms)
        OverloadGuard g;
        double now = 1000.0;
        g.uiPulse (now);
        const auto used = 2.0 * budget;
        run (g, now, 0.6, used, false);
        CHECK (g.engaged() && g.timesEngaged() == 1);
        const auto y = g.afterCallback (now, used, budget);
        CHECK (std::abs (y - used * g.params().yieldShare) < 1e-9);
        CHECK (std::abs (g.afterCallback (now, 20.0, budget) - g.params().maxYieldMs) < 1e-9);
        CHECK (std::abs (g.afterCallback (now, 0.1, budget) - g.params().minYieldMs) < 1e-9);
        // still overloaded with the UI fine again: stays engaged (it is the yield that keeps the UI fine), through its
        // probes (the probes' callbacks still miss their deadline)
        run (g, now, 5.0, used, true);
        CHECK (g.engaged() && g.timesEngaged() == 1 && g.yieldedMs() > 0.0);
        // the load drops: let go within the release time, and no yield after that
        run (g, now, (g.params().releaseMs + 2.0 * g.params().windowMs) / 1000.0, 0.5 * budget, true);
        CHECK (! g.engaged());
        CHECK (run (g, now, 1.0, 0.5 * budget, true) == 0.0);
        // a new overload with a starving UI engages it again
        run (g, now, 0.8, used, false);
        CHECK (g.engaged() && g.timesEngaged() == 2);
        // switched off (Settings): no yield at once
        g.setEnabled (false);
        CHECK (g.afterCallback (now, used, budget) == 0.0 && ! g.engaged());
        CHECK (run (g, now, 1.0, used, false) == 0.0);
    }
    {
        // the probe ends an overload that only the yield kept going: callbacks that miss their deadline only while
        // yielding (a stopped project's tails, slowed down in audio time) pass the probe, and the guard lets go
        OverloadGuard g;
        g.configure ("probeevery=2000"); // (off by default since round 190os)
        double now = 1000.0;
        g.uiPulse (now);
        run (g, now, 0.6, 2.0 * budget, false);
        CHECK (g.engaged());
        bool released = false;
        for (double end = now + 2.0 * g.params().probeEveryMs; now < end && ! released;)
        {
            // over the deadline while yielding, within it in the probe (no yield asked: the probe)
            const auto y = g.afterCallback (now, 0.5 * budget, budget);
            const auto over = g.afterCallback (now + 0.01, y > 0.0 ? 2.0 * budget : 0.5 * budget, budget);
            now += budget + y + over;
            released = ! g.engaged();
        }
        CHECK (released);
    }
    {
        // a device restart forgets the window
        OverloadGuard g;
        double now = 1000.0;
        g.uiPulse (now);
        run (g, now, 0.6, 3.0, false);
        CHECK (g.engaged());
        g.restart();
        CHECK (! g.engaged());
    }
    {
        // LYNX_SAFE_OVERLOADS's spec
        OverloadGuard g;
        g.configure ("share=1,max=8,bogus,window=100,late=50");
        CHECK (g.params().yieldShare == 1.0 && g.params().maxYieldMs == 8.0 && g.params().windowMs == 100.0 && g.params().uiLateMs == 50.0
               && g.params().minYieldMs == 0.5 && g.params().releaseMs == 500.0 && g.params().probeEveryMs == 0.0);
        g.configure ("stopgrace=1000,probeevery=2000");
        CHECK (g.params().stopGraceMs == 1000.0 && g.params().probeEveryMs == 2000.0);
    }
    // the sleep itself: about what was asked (a loose bound: the machine is shared)
    const auto t0 = std::chrono::steady_clock::now();
    OverloadGuard::sleepFor (2.0);
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    CHECK (ms >= 1.9 && ms < 200.0);
    OverloadGuard::sleepFor (0.0);
    OverloadGuard::sleepFor (-1.0);
}

// ---- D2: FL's generator smart disable (core/GeneratorSmartDisable.h; the plugin side: --selftest "generator smart disable")
#include "core/GeneratorSmartDisable.h"

TEST (perf190ok_generator_smart_disable_sleeps_after_4_s_of_silence_and_wakes_on_events)
{
    using namespace lynx;
    const auto hold = gensmart::holdSamples (48000.0);
    CHECK (hold == 192000);
    gensmart::State s;
    // 3.9 s of silence: still awake; an audible block starts the count again
    for (int i = 0; i < 3900 * 48 / 256; ++i)
        s.rendered (256, false, true);
    CHECK (! s.asleep (hold));
    s.rendered (256, false, false);
    CHECK (s.silentSamples == 0);
    // an event (a note sent) also starts it again, even on a silent block
    for (int i = 0; i < 100; ++i)
        s.rendered (256, false, true);
    s.rendered (256, true, true);
    CHECK (s.silentSamples == 0);
    // 4 s of continuous silence with nothing sent: asleep
    for (int i = 0; i * 256 < hold; ++i)
        s.rendered (256, false, true);
    CHECK (s.asleep (hold));
    s.wake();
    CHECK (! s.asleep (hold));
    // the silence test: -96 dB and under is silence, a NaN or anything louder is not
    std::vector<float> a (256, 1.0e-5f), b (256, -1.5e-5f);
    const float* both[2] { a.data(), b.data() };
    CHECK (gensmart::silent (both, 2, 256));
    b[100] = 2.0e-5f;
    CHECK (! gensmart::silent (both, 2, 256));
    b[100] = std::numeric_limits<float>::quiet_NaN();
    CHECK (! gensmart::silent (both, 2, 256));
    const float* none[1] { nullptr };
    CHECK (gensmart::silent (none, 1, 256));
}

TEST (perf190ok_channel_smart_disable_keeps_the_fl_flags_in_step)
{
    using namespace lynx;
    ChannelDef c;
    CHECK (! c.smartDisable); // FL's default for a new generator, and every project before round 190ok
    c.setSmartDisable (true);
    CHECK (c.smartDisable && c.flWrapperFlags == -1); // (not imported: no FL flags to keep)
    c.flWrapperFlags = 0x150;
    c.setSmartDisable (true);
    CHECK (c.flWrapperFlags == 0x170);
    c.setSmartDisable (false);
    CHECK (! c.smartDisable && c.flWrapperFlags == 0x150);
    ChannelDef d = c;
    d.smartDisable = true;
    CHECK (! (c == d)); // (undo and the rack rebuild see it)
}

// ---- D1: the smart-disable tip (core/SmartDisableTip.h)
#include "core/SmartDisableTip.h"

TEST (perf190ok_smart_disable_tip_only_for_heavy_projects_with_most_slots_off)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    for (auto& t : song.mixer) // (the default project's own effects out of the way)
        t.effects.clear();
    auto add = [&song] (int track, bool smart, bool enabled = true) {
        auto slot = makeEffectSlot (EffectKind::reverb);
        slot.smartDisable = smart;
        slot.enabled = enabled;
        song.mixer[(std::size_t) track].effects.push_back (slot);
    };
    for (int t = 1; t <= 9; ++t)
        add (t, false);
    add (10, true);
    add (11, false, false); // (a switched-off slot is not counted)
    const auto c = smarttip::count (song);
    CHECK (c.off == 9 && c.total == 10);
    CHECK (smarttip::due (c, 0.16));        // Exoplanet-like
    CHECK (! smarttip::due (c, 0.01));      // a light stopped project: no tip
    CHECK (! smarttip::due ({ 4, 77 }, 0.5)); // Thief: 4 of 77 off
    CHECK (! smarttip::due ({ 7, 7 }, 0.5));  // too few slots
    CHECK (! smarttip::due ({ 8, 17 }, 0.5)); // under half
    CHECK (smarttip::due ({ 116, 118 }, 0.161));
}

// ---- D3: fast maths for LYNX's own devices (core/FastMath.h), from EffectSlot::engineRev 2
#include "core/FastMath.h"

TEST (perf190ok_fastmath_is_as_accurate_as_the_c_library)
{
    using namespace lynx;
    double worst = 0.0;
    for (int i = -1200000; i <= 1200000; ++i)
    {
        const auto x = (float) i * 1.0e-5f;
        const auto exact = std::tanh ((double) x);
        const auto got = (double) fastmath::tanh (x);
        const auto ulp = (double) std::nextafter (std::fabs ((float) exact), 2.0f) - std::fabs ((float) exact);
        worst = std::max (worst, std::abs (got - exact) / std::max (ulp, 1.0e-45));
        CHECK (std::abs (got) <= 1.0);
    }
    CHECK (worst < 3.0); // (2.52 ulp at the most; glibc's tanhf 1.4)
    CHECK (fastmath::tanh (0.0f) == 0.0f && fastmath::tanh (100.0f) == 1.0f && fastmath::tanh (-100.0f) == -1.0f);
    CHECK (fastmath::tanh (-0.3f) == -fastmath::tanh (0.3f));
    double we = 0.0, wl = 0.0, wp = 0.0;
    for (int i = 1; i < 200000; ++i)
    {
        const auto x = -300.0 + i * 0.003;
        we = std::max (we, std::abs (fastmath::exp (x) / std::exp (x) - 1.0));
        const auto y = std::pow (10.0, -12.0 + i * 1.3e-4);
        wl = std::max (wl, std::abs (fastmath::log10 (y) - std::log10 (y)));
        wp = std::max (wp, std::abs (fastmath::pow (y, 0.37) / std::pow (y, 0.37) - 1.0));
    }
    CHECK (we < 1.0e-12 && wl < 1.0e-12 && wp < 1.0e-12);
    CHECK (fastmath::pow (0.0, 2.0) == 0.0 && std::isnan (fastmath::log2 (-1.0)) && fastmath::exp (-2000.0) < 1.0e-300);
}

TEST (perf190ok_ember_fast_curve_stays_within_float_rounding_of_the_old_one)
{
    using namespace lynx::mixsuite::ember;
    for (const auto s : { Style::tube, Style::tubeBiased, Style::fuzz, Style::tape, Style::console })
    {
        double worst = 0.0;
        for (int i = -40000; i <= 40000; ++i)
        {
            const auto x = (float) i * 2.5e-4f;
            worst = std::max (worst, (double) std::abs (shapeFast (s, x) - shape (s, x)));
        }
        CHECK (worst < 1.0e-6); // under -120 dB of full scale
    }
}

TEST (perf190ok_new_slots_take_revision_2_old_ones_render_as_before)
{
    using namespace lynx;
    // revision 2 reaches LYNX's own devices through makeEffect (slot); 0 and 1 (projects saved before) are the C library's
    // maths, the same samples; 2 is within float rounding of them
    for (const auto kind : { EffectKind::ember, EffectKind::prismMultiband, EffectKind::crestCompressor })
    {
        auto slot = makeEffectSlot (kind);
        CHECK (slot.engineRev == 2);
        if (kind == EffectKind::ember)
            slot.params[(std::size_t) mixsuite::ember::drive] = 0.6f;
        std::vector<std::vector<float>> outL, outR;
        for (const int rev : { 0, 1, 2 })
        {
            slot.engineRev = rev;
            auto fx = makeEffect (slot);
            fx->prepare (44100.0);
            std::vector<float> l (44100), r (44100);
            for (int i = 0; i < 44100; ++i)
            {
                l[(std::size_t) i] = 0.7f * (float) std::sin (i * 0.031) * (float) (i % 11025 < 6000 ? 1.0 : 0.05);
                r[(std::size_t) i] = 0.6f * (float) std::sin (i * 0.047 + 1.0);
            }
            for (int at = 0; at < 44100; at += 512)
                fx->process (l.data() + at, r.data() + at, std::min (512, 44100 - at), slot.params, 120.0);
            outL.push_back (l);
            outR.push_back (r);
        }
        CHECK (outL[0] == outL[1] && outR[0] == outR[1]);
        double diff = 0.0, peak = 0.0;
        for (std::size_t i = 0; i < outL[0].size(); ++i)
        {
            diff = std::max (diff, (double) std::abs (outL[2][i] - outL[0][i]));
            peak = std::max (peak, (double) std::abs (outL[0][i]));
        }
        CHECK (peak > 0.01);
        CHECK (diff < 1.0e-5 * std::max (1.0, peak)); // (under -100 dB)
    }
}
