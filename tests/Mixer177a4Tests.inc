// --- Round 177a: a press on a mixer fader's rail steps towards the mouse, as FL (REA_rodada177a section 4.2) ---------------

TEST (mixer177a_a_rail_press_steps_towards_the_mouse_and_stops_there)
{
    using lynx::faderRailStepTowards;
    using lynx::faderRailStep;
    CHECK (std::abs (faderRailStepTowards (0.8f, 0.2f) - (0.8f - faderRailStep)) < 1e-6f);
    CHECK (std::abs (faderRailStepTowards (0.5f, 0.9f) - (0.5f + faderRailStep)) < 1e-6f);

    // FL's hint after 100 clicks below the cap: 100 % -> 30 % of 0 dB's position
    auto value = lynx::unityFader;
    for (int i = 0; i < 100; ++i)
        value = faderRailStepTowards (value, 0.0f);
    CHECK (std::abs (value / lynx::unityFader - 0.30f) < 0.005f);

    // held, it reaches the mouse and stays: never past it
    value = lynx::unityFader;
    for (int i = 0; i < 1000; ++i)
        value = faderRailStepTowards (value, 0.61f);
    CHECK (value == 0.61f);
}
