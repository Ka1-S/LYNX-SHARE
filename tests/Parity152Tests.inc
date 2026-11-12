// round 152 (REA_rodada152): Prism Multiband ← Pro-MB — the measured detector's attack in the gain stage (slow attacks)

TEST (prism_gain_stage_attack_round152)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // [EXEC] Pro-MB, one band over the whole spectrum, threshold −30, 4 : 1, knee 0, range −30, release 20 %: the gain
    // after a 1 kHz step −30 → −6 dBFS (1 ms rms windows, D:\lynx-work\parity150\mb\att*.wav)
    struct Case { float attack; int ms[4]; double want[4]; };
    const Case cases[] {
        { 0.5f, { 10, 20, 50, 100 }, { -7.71, -11.67, -15.81, -16.92 } },
        { 0.75f, { 10, 50, 100, 200 }, { -1.95, -7.49, -11.55, -15.36 } },
        { 1.0f, { 50, 100, 200, 500 }, { -2.88, -5.25, -8.97, -14.72 } },
    };
    std::vector<float> x (48000 * 2);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) ((i < 48000 ? 0.0316 : 0.5) * std::sin (2.0 * dyn108::pi * 1000.0 * (double) i / 48000.0));
    for (const auto& c : cases)
        for (const bool gainStage : { true, false })
        {
            auto q = p147::wholeBand (-30.0, 4.0);
            q[(std::size_t) m::param (0, m::fAttack)] = c.attack;
            q[(std::size_t) m::param (0, m::fRelease)] = 0.2f;
            q[m::detectorLaw] = 1.0f;
            q[m::rev152] = gainStage ? 1.0f : 0.0f;
            const auto y = dyn108::run (EffectKind::prismMultiband, q, x);
            double err = 0.0;
            std::printf ("  Prism attack %.0f %% (%s):", c.attack * 100.0, gainStage ? "gain stage" : "level, round 150");
            for (int k = 0; k < 4; ++k)
            {
                const auto a = (std::size_t) (48000 + 48 * c.ms[k]);
                const auto got = dyn108::rmsDb (y, a, a + 48) - dyn108::rmsDb (x, a, a + 48);
                err = std::max (err, std::abs (got - c.want[k]));
                std::printf (" %d ms %.2f (Pro-MB %.2f)", c.ms[k], got, c.want[k]);
            }
            std::printf ("\n");
            if (gainStage)
                CHECK (err < 0.5);
        }
    // new instances use the gain-stage attack; a project saved before round 152 keeps round 150's
    auto slot = makeEffectSlot (EffectKind::prismMultiband);
    CHECK (slot.params[m::rev152] >= 0.5f);
    upgradeLoadedEffectParams (slot, (std::size_t) m::rev152);
    CHECK (slot.params[m::rev152] < 0.5f && m::measuredDetector (slot.params));
    CHECK ((int) effectInfo (EffectKind::prismMultiband).params.size() == m::count && m::rev152 == m::detectorLaw + 1);
}

TEST (prism_dynamic_narrow_bands_round152)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // [EXEC] Pro-MB Dynamic phase, one static band in the free layout (magnitude, dB, 48 kHz; D:\lynx-work\parity152\mb)
    struct Case { std::array<double, 5> band; double hz[5]; double db[5]; };
    const Case cases[] {
        { { 300.0, 900.0, 24, 24, -30 }, { 150.0, 300.0, 520.0, 900.0, 1800.0 }, { -0.83, -7.28, -13.34, -7.28, -0.82 } },
        { { 300.0, 600.0, 12, 12, -12 }, { 150.0, 300.0, 424.0, 600.0, 1200.0 }, { -1.57, -3.19, -3.52, -3.19, -1.56 } },
        { { 300.0, 600.0, 48, 48, -30 }, { 250.0, 300.0, 424.0, 600.0, 700.0 }, { -2.56, -7.37, -16.94, -7.38, -3.08 } },
        { { 300.0, 600.0, 12, 12, 6 }, { 150.0, 300.0, 424.0, 600.0, 1200.0 }, { 1.32, 2.60, 2.84, 2.60, 1.32 } },
        { { 300.0, 1200.0, 36, 36, -24 }, { 150.0, 300.0, 600.0, 1200.0, 2400.0 }, { -0.14, -5.88, -20.76, -5.88, -0.14 } },
        // 300 – 450 Hz is narrower than 0.8 octave: the upper edge moves to 522 Hz
        { { 300.0, 450.0, 48, 48, -30 }, { 300.0, 375.0, 420.0, 470.0, 600.0 }, { -7.28, -13.14, -13.07, -10.62, -3.51 } },
    };
    for (const auto& c : cases)
        for (const bool now : { true, false })
        {
            auto p = p150::freeBands ({ c.band });
            p[m::processingMode] = 0.5f; // Dynamic phase
            p[m::rev152] = now ? 1.0f : 0.0f;
            double err = 0.0, errTone = 0.0;
            std::printf ("  Prism dynamic %.0f-%.0f Hz %.0f dB/oct %+.0f dB (%s):", c.band[0], c.band[1], c.band[2], c.band[4], now ? "round 152" : "round 150");
            for (int k = 0; k < 5; ++k)
            {
                const auto curve = p150::curveDb (p, c.hz[k]);
                err = std::max (err, std::abs (curve - c.db[k]));
                if (now)
                    errTone = std::max (errTone, std::abs (p147::toneGain (p, c.hz[k]) - c.db[k]));
                std::printf (" %.0f Hz %.2f (%.2f)", c.hz[k], curve, c.db[k]);
            }
            std::printf ("\n");
            if (now)
                CHECK (err < 0.1 && errTone < 0.15);
        }
}

TEST (foil_plate_view_round152)
{
    using namespace lynx;
    // round 152: Foil Plate's appended View (Full | Plate): the Plate view is the Little Plate role's one-knob panel
    const auto& info = effectInfo (EffectKind::foilPlate);
    CHECK (info.params.size() == 17 && std::string (info.params[15].name) == "View" && info.params[15].defaultValue == 0.0f); // (16: Law, round 190bz)
    CHECK (formatEffectParam (EffectKind::foilPlate, 15, 1.0f, 120.0) == "Plate" && formatEffectParam (EffectKind::foilPlate, 15, 0.0f, 120.0) == "Full");
    const auto& l = plugui::layoutFor (EffectKind::foilPlate);
    for (const float view : { 0.0f, 1.0f })
    {
        auto p = makeEffectSlot (EffectKind::foilPlate).params;
        p[15] = view;
        std::map<std::string, int> shown;
        for (const auto& item : l.items)
        {
            CHECK (item.x >= 0.0f && item.y >= 0.0f && item.x + item.w <= (float) l.width + 0.5f && item.y + item.h <= (float) l.height + 0.5f);
            if (item.param != nullptr && plugui::isShown (EffectKind::foilPlate, item, 0, p))
                ++shown[item.param];
        }
        for (const auto* name : { "Decay", "Low cut", "Mix", "Infinite", "Voicing", "View" })
            CHECK (shown[name] == 1);
        CHECK ((shown["Size"] == 1) == (view < 0.5f) && (shown["Mod depth"] == 1));
    }
}

namespace p152
{
// Auto-pan's Rhythm voicing, digital, the full width, centred, with the round-152 trigger settings
inline lynx::EffectParams triggerPan (float trigger, float thresholdV, int divider)
{
    using namespace lynx;
    auto p = makeEffectSlot (EffectKind::autoPan).params;
    constexpr int rb = autopan::count;
    p[rb] = 1.0f;          // the Rhythm voicing
    p[rb + 4] = 180.0f / 210.0f;
    p[rb + 6] = 0.0f;      // smoothing 0 (the shortest glide)
    p[rb + 10] = 0.0f;     // digital
    p[rb + 20] = trigger;
    p[rb + 21] = thresholdV;
    p[rb + 22] = (float) (divider - 1) / 11.0f;
    return p;
}
// a quiet 1 kHz bed (−40 dBFS) with bursts (−6 dBFS, 50 ms) every 400 ms from 0.2 s; the pan (degrees, −90 = left)
// read from the bed 150 … 350 ms after each burst
inline std::vector<double> burstPans (const lynx::EffectParams& p, int bursts, std::vector<float>* flipAt = nullptr)
{
    using namespace lynx;
    auto fx = makeEffect (EffectKind::autoPan);
    fx->prepare (48000.0);
    const int n = (int) (48000.0 * (0.2 + 0.4 * bursts + 0.2));
    std::vector<float> a ((std::size_t) n), b;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / 48000.0, u = t - 0.2 - 0.4 * std::floor ((t - 0.2) / 0.4);
        const bool loud = t >= 0.2 && u < 0.05;
        a[(std::size_t) i] = (float) ((loud ? 0.5 : 0.01) * std::sin (2.0 * 3.14159265358979 * 1000.0 * t));
    }
    b = a;
    auto q = p;
    for (int at = 0; at < n; at += 256)
    {
        if (flipAt != nullptr) // the manual trigger pressed at these times (s)
            for (const auto when : *flipAt)
                if (when * 48000.0 >= at && when * 48000.0 < at + 256)
                    q[(std::size_t) autopan::count + 25] = q[(std::size_t) autopan::count + 25] >= 0.5f ? 0.0f : 1.0f;
        fx->process (a.data() + at, b.data() + at, std::min (256, n - at), q, 120.0);
    }
    std::vector<double> out;
    for (int k = 0; k < bursts; ++k)
    {
        const int from = (int) (48000.0 * (0.2 + 0.4 * k + 0.15)), to = (int) (48000.0 * (0.2 + 0.4 * k + 0.35));
        double sl = 0.0, sr = 0.0;
        for (int i = from; i < to; ++i)
        {
            sl += (double) a[(std::size_t) i] * a[(std::size_t) i];
            sr += (double) b[(std::size_t) i] * b[(std::size_t) i];
        }
        out.push_back (2.0 * std::atan2 (std::sqrt (sr), std::sqrt (sl)) * 180.0 / 3.14159265358979 - 90.0);
    }
    return out;
}
} // namespace p152

TEST (autopan_trigger_sources_round152)
{
    using namespace lynx;
    constexpr int rb = autopan::count;
    const auto& info = effectInfo (EffectKind::autoPan);
    CHECK (info.params.size() == (std::size_t) rb + 27 && std::string (info.params[rb + 26].name) == "Random steps");
    CHECK (formatEffectParam (EffectKind::autoPan, rb + 20, 0.5f, 120.0) == "ping-pong" && formatEffectParam (EffectKind::autoPan, rb + 22, 1.0f, 120.0) == "12");
    CHECK (formatEffectParam (EffectKind::autoPan, rb + 21, 1.0f, 120.0) == "off (manual only)");
    // Ping-pong, threshold −20 dB: each burst moves the pan to the other side (it starts on the left)
    auto pp = p152::burstPans (p152::triggerPan (0.5f, 40.0f / 60.0f, 1), 4);
    std::printf ("  Auto-pan ping-pong: %.1f %.1f %.1f %.1f deg\n", pp[0], pp[1], pp[2], pp[3]);
    CHECK (pp[0] > 40.0 && pp[1] < -40.0 && pp[2] > 40.0 && pp[3] < -40.0);
    // divider 2: every second burst
    auto pd = p152::burstPans (p152::triggerPan (0.5f, 40.0f / 60.0f, 2), 4);
    CHECK (pd[0] < -40.0 && pd[1] > 40.0 && pd[2] > 40.0 && pd[3] < -40.0);
    // a threshold above the bursts: nothing moves; the manual trigger still does
    auto ph = p152::burstPans (p152::triggerPan (0.5f, 1.0f, 1), 3);
    CHECK (ph[0] < -40.0 && ph[1] < -40.0 && ph[2] < -40.0);
    std::vector<float> presses { 0.62f };
    auto pm = p152::burstPans (p152::triggerPan (0.5f, 1.0f, 1), 3, &presses);
    CHECK (pm[0] < -40.0 && pm[1] > 40.0 && pm[2] > 40.0);
    // Random step: the pan lands in different places, within the width
    auto pr = p152::burstPans (p152::triggerPan (1.0f, 40.0f / 60.0f, 1), 6);
    double lo = 1e9, hi = -1e9;
    for (const auto d : pr) { lo = std::min (lo, d); hi = std::max (hi, d); CHECK (std::abs (d) <= 90.5); }
    CHECK (hi - lo > 40.0);
    // the Random source on the clock: reproducible, different places per step
    auto pc = p152::triggerPan (0.0f, 40.0f / 60.0f, 1);
    pc[rb + 26] = 1.0f;
    pc[rb + 2] = 1.0f;     // synced to beats
    pc[rb + 3] = (float) ((std::log2 (1.6) + 4.0) / 10.0); // 1.6 beats per period = 0.8 s at 120 BPM: 0.4 s per step
    pc[rb + 9] = 0.1875f;  // the steps change at 0.25 + 0.4·k s, between the read windows
    const auto r1 = p152::burstPans (pc, 5), r2 = p152::burstPans (pc, 5);
    double spread = 0.0;
    for (int k = 0; k < 5; ++k) { CHECK (std::abs (r1[(std::size_t) k] - r2[(std::size_t) k]) < 1e-9); spread = std::max (spread, std::abs (r1[(std::size_t) k] - r1[0])); }
    CHECK (spread > 10.0);
}
