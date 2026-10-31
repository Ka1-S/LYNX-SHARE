// Round 167g: FL project-level .flp events LYNX's importer ignored (docs/analise/REA_rodada167g.md)

// §1 Main pitch: event 0x50, i16 cents, written by FL's project saver (FUN_00f40840) from the main pitch knob; FL's own
// command-line render of a sampler playing a 440 Hz sine follows it: 0 → 440.0 Hz, +300 → 523.3 Hz, −400 → 349.3 Hz.
TEST (rea167g_flp_main_pitch)
{
    for (const int cents : { 0, 300, -400, 695 })
    {
        FlpBuilder b;
        b.u8 (0x50);
        b.u16 ((std::uint16_t) (std::int16_t) cents);
        const auto p = flp::parseProject (b.bytes);
        CHECK (p.mainPitch == cents);
        const auto song = lynx::importFlp (p);
        CHECK (song.masterPitch == (float) cents);
    }
    // a word 0x50 inside a channel's events is not the project's (FL writes it only among the project's events)
    FlpBuilder c;
    c.u8 (0x40);
    c.u16 (0);
    c.u8 (0x50);
    c.u16 (100);
    CHECK (flp::parseProject (c.bytes).mainPitch == 0);

    if (realFlpDir.empty())
        return;
    const auto path = std::filesystem::path (realFlpDir) / "Demo projects" / "Demo songs" / "Electroconductor - Inner Days - Original.flp";
    if (! std::filesystem::exists (path))
        return;
    std::ifstream in (path, std::ios::binary);
    const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
    CHECK (lynx::importFlp (flp::parseProject (file)).masterPitch == -400.0f); // [DATA] 0x50 = 65136 (−400 cents)
}
