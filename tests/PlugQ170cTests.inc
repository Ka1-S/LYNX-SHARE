// Round 170c (REA_rodada170c): plugin quality — Ember's Tube curve (issue: "the first mode sounds garbly").

TEST (ember_round170c_tube_has_headroom_and_less_intermodulation)
{
    using namespace lynx::mixsuite::ember;
    using namespace mixsuite96;
    // the curve: unit slope at 0, still asymmetric (even harmonics), 6 dB more headroom than the old one
    CHECK (std::abs ((shape (Style::tube, 1e-3f) - shape (Style::tube, -1e-3f)) / 2e-3f - 1.0f) < 1e-3f);
    CHECK (std::abs (shape (Style::tube, 2.0f)) < std::abs (shape (Style::tube, -2.0f)));
    CHECK (shape (Style::tube, 1.0f) > shape (Style::tubeBiased, 1.0f) + 0.05f);
    // the voicings that used the old curve keep it
    CHECK (voiceStyle (1) == Style::tubeBiased && voiceStyle (4) == Style::tubeBiased && voiceStyle (0) == Style::tube);

    // +10.4 dB of drive (the video's setting), auto gain on: a −12 dBFS 1 kHz tone
    auto p = defaults (lynx::EffectKind::ember);
    p[drive] = (float) (10.4 / 36.0);
    const auto y = run (lynx::EffectKind::ember, p, sine (1000.0, 0.25, 32768));
    const auto f1 = amplitude (y, 1000.0, 16384, 16384), h2 = amplitude (y, 2000.0, 16384, 16384), h3 = amplitude (y, 3000.0, 16384, 16384);
    std::printf ("  Ember Tube +10.4 dB: H2 %.1f dB, H3 %.1f dB\n", db (h2 / f1), db (h3 / f1));
    CHECK (db (h2 / f1) < -26.0 && db (h2 / f1) > -40.0); // was −19 dB
    CHECK (h2 > h3);                                       // the 2nd harmonic leads (tube character)
    // two tones (50 Hz −8 dBFS + 3 kHz −20 dBFS): the bass's sidebands around 3 kHz (the "garble")
    std::vector<float> tt (32768);
    for (std::size_t i = 0; i < tt.size(); ++i)
        tt[i] = (float) (0.4 * std::sin (2.0 * 3.141592653589793 * 50.0 * (double) i / 44100.0) + 0.1 * std::sin (2.0 * 3.141592653589793 * 3000.0 * (double) i / 44100.0));
    const auto yt = run (lynx::EffectKind::ember, p, tt);
    const auto carrier = amplitude (yt, 3000.0, 16384, 16384);
    double side = 0.0;
    for (int k = 1; k <= 3; ++k)
        side = std::max ({ side, amplitude (yt, 3000.0 + 50.0 * k, 16384, 16384), amplitude (yt, 3000.0 - 50.0 * k, 16384, 16384) });
    std::printf ("  Ember Tube +10.4 dB: strongest 3 kHz sideband %.1f dB\n", db (side / carrier));
    CHECK (db (side / carrier) < -14.0);
}

TEST (echoform_round170c_controls_bypassed_by_hybrid_or_tape_are_idle)
{
    using lynx::EffectKind;
    const auto& layout = lynx::plugui::layoutFor (EffectKind::echoform);
    auto p = lynx::makeEffectSlot (EffectKind::echoform).params;
    const auto find = [&] (const char* name) -> const lynx::plugui::LayoutItem* {
        for (const auto& item : layout.items)
            if (item.param != nullptr && std::string (item.param) == name && lynx::plugui::isValueControl (item.type)) return &item;
        return nullptr;
    };
    const auto* drive = find ("Drive");
    const auto* mix = find ("Mix");
    const auto* feedback = find ("Feedback");
    CHECK (drive != nullptr && mix != nullptr && feedback != nullptr);
    if (drive == nullptr || mix == nullptr || feedback == nullptr) return;
    // the default sound (Hybrid off, no tape voicing): everything live
    CHECK (! lynx::plugui::isIdle (EffectKind::echoform, *drive, p));
    // Hybrid clean / analog: the colour controls do nothing, mix and feedback still work
    p[(std::size_t) lynx::plugui::paramIndex (EffectKind::echoform, "Hybrid")] = 0.5f;
    CHECK (lynx::plugui::isIdle (EffectKind::echoform, *drive, p));
    CHECK (! lynx::plugui::isIdle (EffectKind::echoform, *mix, p) && ! lynx::plugui::isIdle (EffectKind::echoform, *feedback, p));
    // a tape voicing: the same
    p[(std::size_t) lynx::plugui::paramIndex (EffectKind::echoform, "Hybrid")] = 0.0f;
    p[(std::size_t) lynx::plugui::paramIndex (EffectKind::echoform, "Tape voicing")] = 1.0f / 3.0f;
    CHECK (lynx::plugui::isIdle (EffectKind::echoform, *drive, p));
    int idle = 0;
    for (const auto& item : layout.items)
        idle += item.idleParam != nullptr ? 1 : 0;
    CHECK (idle >= 15);
}

TEST (atrium_round170c_early_mod_rate_and_depth_move_the_early_taps)
{
    using lynx::EffectKind;
    auto p = mixsuite96::defaults (EffectKind::atrium);
    const auto depth = lynx::presets::paramByName (EffectKind::atrium, "Early mod depth");
    const auto rate = lynx::presets::paramByName (EffectKind::atrium, "Early mod rate");
    CHECK (depth >= 0 && rate >= 0);
    if (depth < 0 || rate < 0)
        return;
    // a sustained tone through the early section: fixed taps leave it a pure 2 kHz line, moving taps spread it
    std::vector<float> x (44100);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (0.25 * std::sin (2.0 * 3.141592653589793 * 2000.0 * (double) i / 44100.0));
    p[(std::size_t) depth] = 0.0f;
    const auto still = mixsuite96::run (EffectKind::atrium, p, x);
    p[(std::size_t) depth] = 1.0f;
    p[(std::size_t) rate] = 0.5f;
    const auto moving = mixsuite96::run (EffectKind::atrium, p, x);
    double diff = 0.0, ref = 0.0;
    bool finite = true;
    for (std::size_t i = 22050; i < x.size(); ++i)
    {
        diff += (double) (moving[i] - still[i]) * (moving[i] - still[i]);
        ref += (double) still[i] * still[i];
        finite = finite && std::isfinite (moving[i]);
    }
    std::printf ("  Atrium early mod: difference %.1f dB\n", 10.0 * std::log10 ((diff + 1e-30) / (ref + 1e-30)));
    CHECK (finite);
    CHECK (diff > ref * 1e-4); // the controls now do something (were read by nothing)
    // depth 0 = the old sound, bit for bit (presets / substitutes without early modulation are unchanged)
    p[(std::size_t) depth] = 0.0f;
    const auto again = mixsuite96::run (EffectKind::atrium, p, x);
    CHECK (again == still || [&] { double d = 0; for (std::size_t i = 0; i < x.size(); ++i) d = std::max (d, (double) std::abs (again[i] - still[i])); return d < 1e-6; }());
}
