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

// §2 The state FL saves for the next opening: 0x43 current pattern (FL's number), 0x09 SONG / PAT, 0x0A Project info ›
// Show on opening, 0x64 the open arrangement — all written by FL's saver FUN_00f40840 [DECOMP].
namespace rea167g
{
inline std::vector<std::uint8_t> stateProject (int currentPattern, int songMode, int showInfo, int currentArrangement)
{
    FlpBuilder b;
    if (currentPattern >= 0)
    {
        b.u8 (0x43);
        b.u16 ((std::uint16_t) currentPattern);
    }
    if (songMode >= 0)
    {
        b.u8 (0x09);
        b.u8 ((std::uint8_t) songMode);
    }
    if (showInfo >= 0)
    {
        b.u8 (0x0A);
        b.u8 ((std::uint8_t) showInfo);
    }
    b.utf16 (0xC3, u"Read me");
    // FL's patterns 1…3: pattern 1 has 3 notes, 2 has 1, 3 has none (not imported)
    for (int pat = 1; pat <= 3; ++pat)
    {
        b.u8 (0x41);
        b.u16 ((std::uint16_t) pat);
        std::vector<std::uint8_t> notes;
        for (int n = 0; n < (pat == 1 ? 3 : pat == 2 ? 1 : 0); ++n)
        {
            const auto r = noteRecord ((std::uint32_t) n * 96, 0, 48, (std::uint16_t) (60 + pat), 100);
            notes.insert (notes.end(), r.begin(), r.end());
        }
        if (! notes.empty())
            b.data (0xE0, notes);
    }
    b.u8 (0x40);
    b.u16 (0);
    b.u8 (0x15);
    b.u8 (0);
    for (int a = 0; a < 2; ++a)
    {
        b.u8 (0x63);
        b.u16 ((std::uint16_t) a);
        b.utf16 (0xF1, a == 0 ? u"Song" : u"Intro");
    }
    if (currentArrangement >= 0)
    {
        b.u8 (0x64);
        b.u16 ((std::uint16_t) currentArrangement);
    }
    return b.bytes;
}
} // namespace rea167g

TEST (rea167g_flp_saved_state)
{
    {
        const auto p = flp::parseProject (rea167g::stateProject (2, 1, 1, 1));
        CHECK (p.currentPattern == 2 && p.songMode == 1 && p.showInfo && p.currentArrangement == 1);
        const auto song = lynx::importFlp (p);
        CHECK (song.patterns.size() == 2);
        CHECK (song.currentPattern == 1);            // FL's pattern 2, not the fullest (pattern 1)
        CHECK (song.mode == lynx::PlayMode::song);   // SONG although the Playlist is empty
        CHECK (song.info.showOnOpen);
        CHECK (song.currentArrangement == 1);        // "Intro", not the first one
    }
    {
        const auto song = lynx::importFlp (flp::parseProject (rea167g::stateProject (1, 0, 0, 0)));
        CHECK (song.currentPattern == 0 && song.mode == lynx::PlayMode::pattern && ! song.info.showOnOpen);
        CHECK (song.currentArrangement == 0);
    }
    {
        // the saved pattern was empty (not imported) → the fullest; no 0x09 / 0x0A / 0x64 (older files) → as before
        const auto p = flp::parseProject (rea167g::stateProject (3, -1, -1, -1));
        CHECK (p.currentPattern == 3 && p.songMode == -1 && ! p.showInfo && p.currentArrangement == -1);
        const auto song = lynx::importFlp (p);
        CHECK (song.currentPattern == 0 && song.mode == lynx::PlayMode::pattern && song.currentArrangement == 0);
    }
    {
        const auto p = flp::parseProject (rea167g::stateProject (1000, -1, -1, -1)); // out of FL's 1…999
        CHECK (p.currentPattern == 0);
    }

    if (realFlpDir.empty())
        return;
    // [DATA] "Mehran Abbasi - With You": 0x43 = 46, 0x09 = 1, 0x0A = 1; "Asher Postman - Future House": 0x64 = 1
    const auto load = [] (const char* name) {
        std::ifstream in (std::filesystem::path (realFlpDir) / "Demo projects" / "Demo songs" / name, std::ios::binary);
        return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), {});
    };
    const auto mehran = load ("Mehran Abbasi - With You.flp");
    if (! mehran.empty())
    {
        const auto p = flp::parseProject (mehran);
        CHECK (p.currentPattern == 46 && p.songMode == 1 && p.showInfo);
        const auto song = lynx::importFlp (p);
        const auto* selected = p.findPattern (46);
        CHECK (selected != nullptr);
        if (selected != nullptr && song.currentPattern < (int) song.patterns.size())
            CHECK (song.patterns[(std::size_t) song.currentPattern].notes.size() == selected->notes.size() && ! selected->notes.empty());
        CHECK (song.mode == lynx::PlayMode::song && song.info.showOnOpen);
    }
    const auto asher = load ("Asher Postman - Future House.flp");
    if (! asher.empty())
        CHECK (lynx::importFlp (flp::parseProject (asher)).currentArrangement == 1);
}
