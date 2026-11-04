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
