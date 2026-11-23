// Round 190oz: FL <= 11 step grids (docs/analise/REA_rodada190oz.md). Before FL 11.5 the step sequencer's steps were
// saved as per-channel, per-pattern grids (0x41 inside a channel's events, then 0x5B / 0x48 / 0x49 / 0x4D / 0x4E / 0x57 /
// 0x58 / 0x5C word events = (value << 8) | step, and FL 1-3's 0x44 bit mask); FL's loader turns the steps that are on into
// step notes. FL's demo "Synth_dfr - Overcome - Sytrus" keeps its kick, snare, clap and hat steps that way.

namespace
{
FlpBuilder oldStepProject (const char* version, int swing = 0)
{
    FlpBuilder b (96);
    b.ascii (0xC7, version);
    b.u8 (0x0B); b.u8 ((std::uint8_t) swing);
    b.u8 (0x40); b.u16 (0);
    b.u8 (0x15); b.u8 (2);
    b.u8 (0x41); b.u16 (1);
    b.u8 (0x5B); b.u16 (0x100 | 0);           // step 0 on
    b.u8 (0x48); b.u16 ((86 << 8) | 4);       // step 4: velocity 86
    b.u8 (0x5B); b.u16 (0x100 | 4);           // step 4 on
    b.u8 (0x5C); b.u16 ((43 << 8) | 4);       // step 4: shift 43 / 48 of a step
    b.u8 (0x4D); b.u16 ((72 << 8) | 4);       // step 4: key 72
    b.u8 (0x49); b.u16 ((0 << 8) | 4);        // step 4: pan hard left
    b.u8 (0x4E); b.u16 ((130 << 8) | 4);      // step 4: fine pitch +10
    b.u8 (0x58); b.u16 ((40 << 8) | 4);       // step 4: Mod X
    b.u8 (0x57); b.u16 ((200 << 8) | 4);      // step 4: Mod Y
    b.u8 (0x5B); b.u16 (0x4100 | 9);          // step 9 on, porta (0x40)
    b.u8 (0x48); b.u16 ((92 << 8) | 20);      // a velocity on a step that is off: no note
    b.u8 (0x41); b.u16 (2);
    b.u8 (0x44); b.u16 (0x0011);              // FL 1-3 mask: steps 0 and 4
    b.ascii (0xC1, "Kick");
    return b;
}
} // namespace

TEST (flp_old_step_grids_become_step_notes)
{
    const auto b = oldStepProject ("11.1.0");
    const auto p = flp::parseProject (b.bytes);
    const flp::Pattern* one = nullptr;
    const flp::Pattern* two = nullptr;
    for (const auto& pat : p.patterns)
    {
        if (pat.index == 1)
            one = &pat;
        if (pat.index == 2)
            two = &pat;
    }
    CHECK (one != nullptr && two != nullptr);
    if (one == nullptr || two == nullptr)
        return;

    CHECK (one->notes.size() == 3);
    if (one->notes.size() == 3)
    {
        const auto& a = one->notes[0];
        CHECK (a.position == 0 && a.length == 0 && a.channel == 0 && a.key == 60 && a.velocity == 100 && a.pan == 64);
        CHECK (a.finePitch == 120 && a.release == 64 && a.modX == 128 && a.modY == 128 && a.flags == 0x4000 && a.extra == 0);
        const auto& s = one->notes[1]; // step 4 + 43 × 24 / 48 = 21 ticks
        CHECK (s.position == 4 * 24 + 21 && s.length == 0 && s.key == 72 && s.velocity == 86 && s.pan == 0);
        CHECK (s.finePitch == 130 && s.modX == 40 && s.modY == 200);
        const auto& q = one->notes[2];
        CHECK (q.position == 9 * 24 && q.porta() && q.velocity == 100);
    }
    CHECK (two->notes.size() == 2 && two->notes[0].position == 0 && two->notes[1].position == 96);
}

TEST (flp_old_step_grids_swing_and_versions)
{
    // FL's converter adds the swing offset (step ticks × swing >> 8, FUN_01311070) to the odd steps
    {
        const auto b = oldStepProject ("11.1.0", 128);
        const auto p = flp::parseProject (b.bytes);
        bool found = false;
        for (const auto& pat : p.patterns)
            if (pat.index == 1 && pat.notes.size() == 3)
            {
                found = true;
                CHECK (pat.notes[0].position == 0);                 // even step: none
                CHECK (pat.notes[1].position == 4 * 24 + 21);       // even step: none
                CHECK (pat.notes[2].position == 9 * 24 + 12);       // odd step: 24 × 128 >> 8
            }
        CHECK (found);
    }
    // a project from FL 11.5 on keeps its steps as notes: grids are not converted (FL's loader: version < 11.5.0)
    {
        const auto b = oldStepProject ("12.0.0");
        const auto p = flp::parseProject (b.bytes);
        for (const auto& pat : p.patterns)
            CHECK (pat.notes.empty());
    }
}

TEST (flp_old_step_grids_overcome_demo)
{
    // FL's own demo: the patterns that are only steps (Kick Snare, Kick, Claps, Hats...) now have their notes; the two
    // kick channels (FL channels 0 and 1, insert 7) play in pattern 2 on steps 0, 4, 8, 12
    const auto path = (std::filesystem::path (realFlpDir) / "Demo projects" / "Product demos" / "Synth_dfr - Overcome - Sytrus.flp").string();
    if (realFlpDir.empty() || ! std::filesystem::exists (path))
    {
        std::printf ("  (skipped: %s not found)\n", path.c_str());
        return;
    }
    std::ifstream in (path, std::ios::binary);
    const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const auto p = flp::parseProject (bytes);
    std::map<int, const flp::Pattern*> byIndex;
    for (const auto& pat : p.patterns)
        byIndex[pat.index] = &pat;
    CHECK (byIndex.count (2) == 1);
    if (byIndex.count (2) == 1)
    {
        const auto& notes = byIndex[2]->notes;
        CHECK (notes.size() == 8);
        std::vector<std::uint32_t> kick;
        for (const auto& n : notes)
            if (n.channel == 0)
                kick.push_back (n.position);
        CHECK ((kick == std::vector<std::uint32_t> { 0, 96, 192, 288 }));
    }
    // 177 steps on in the file, every one a note now (pattern 19 "Hats Crescendo": 29 of them, up to step 56)
    std::size_t steps = 0;
    for (const auto& pat : p.patterns)
        for (const auto& n : pat.notes)
            steps += n.flags == 0x4000 && n.length == 0 && n.release == 64 ? 1 : 0;
    CHECK (byIndex.count (19) == 1 && byIndex[19]->notes.size() == 29);
    CHECK (steps >= 177);
}
