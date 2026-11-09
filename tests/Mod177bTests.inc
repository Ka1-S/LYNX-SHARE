// Round 177b: LYNX's modulators (Song::modulators; RackState::modRoutes): relative, bipolar, tempo-synced or free;
// nothing is bound while there are none.
TEST (mod177b_shapes)
{
    lynx::Modulator m;
    CHECK (std::abs (m.output (0.25) - 1.0f) < 1.0e-4f); // sine: the top a quarter in
    CHECK (std::abs (m.output (0.75) + 1.0f) < 1.0e-4f);
    m.shape = lynx::ModShape::triangle;
    CHECK (std::abs (m.output (0.0)) < 1.0e-4f && std::abs (m.output (0.25) - 1.0f) < 1.0e-4f && std::abs (m.output (0.75) + 1.0f) < 1.0e-4f);
    m.shape = lynx::ModShape::square;
    CHECK (m.output (0.1) == 1.0f && m.output (0.6) == -1.0f);
    m.phase = 0.5f; // the phase moves the start
    CHECK (m.output (0.1) == -1.0f);
    m.phase = 0.0f;
    m.unipolar = true;
    CHECK (m.output (0.1) == 1.0f && m.output (0.6) == 0.0f);
    m.unipolar = false;
    m.shape = lynx::ModShape::random; // the same value for the same cycle, whatever played before
    const auto a = m.output (3.2), b = m.output (3.9), c = m.output (4.1);
    CHECK (a == b && a != c && a >= -1.0f && a <= 1.0f);
    m.shape = lynx::ModShape::smoothRandom; // continuous across the cycle edge
    CHECK (std::abs (m.output (4.0 - 1.0e-6) - m.output (4.0)) < 1.0e-3f);
}

TEST (mod177b_modulates_a_target_around_its_value)
{
    auto song = lanes177bSong();
    const lynx::AutomationTarget master { lynx::AutomationTargetKind::mixerVolume, 0, 0, 0 };
    song.mixer[0].volume = 0.5f;
    CHECK (lynx::RackState::build (song, nullptr, 48000.0)->modRoutes.empty()); // no modulator: nothing bound
    const auto still = lanes177bLevel (song, 0.0);

    lynx::Modulator m;
    m.shape = lynx::ModShape::square;
    m.beats = 4.0;
    m.routes = { { master, 0.3f } };
    song.modulators.push_back (m);
    CHECK (lynx::RackState::build (song, nullptr, 48000.0)->modRoutes.size() == 1);
    const auto up = lanes177bLevel (song, lynx::ppq * 0.5);   // first half of the cycle: +1 → 0.8
    const auto down = lanes177bLevel (song, lynx::ppq * 2.5); // second half: −1 → 0.2
    CHECK (up > still * 1.2f && down < still * 0.8f);

    // over an automation of the same target: it moves around the automation's value
    lynx::Automation a;
    a.target = master;
    a.points = { { 0, 0.2f, 0.0f }, { lynx::ppq * 16, 0.2f, 0.0f } };
    song.automations.push_back (a);
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::automation;
    clip.automation = 0;
    clip.length = lynx::ppq * 16;
    song.playlist.push_back (clip);
    const auto automatedUp = lanes177bLevel (song, lynx::ppq * 0.5); // 0.2 + 0.3
    CHECK (std::abs (automatedUp - still) < still * 0.05f);

    // not modulated: the tempo, a plugin's parameter
    CHECK (! lynx::Modulator::canModulate (song, { lynx::AutomationTargetKind::tempo, 0, 0, 0 }));
    CHECK (lynx::Modulator::canModulate (song, master));
    song.modulators[0].routes[0].depth = 0.0f; // depth 0: not bound
    CHECK (lynx::RackState::build (song, nullptr, 48000.0)->modRoutes.empty());
}
