// Round 190hz (issue #157): DirectWave's state as an editable document (core/DirectWaveState.h), FL's zone menu
// actions and new-zone defaults (docs/analise/REA_rodada190hz_dw.md).

namespace dw190hz
{
inline std::vector<std::uint8_t> readAll (const std::filesystem::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return std::vector<std::uint8_t> { std::istreambuf_iterator<char> (in), {} };
}

// the 0xD5 event of an FL event file (.fst / .flp): the first one
inline std::vector<std::uint8_t> stateEvent (const std::vector<std::uint8_t>& b)
{
    std::size_t i = 0;
    while (i + 8 <= b.size() && std::memcmp (b.data() + i, "FLdt", 4) != 0)
        ++i;
    i += 8;
    while (i < b.size())
    {
        const auto id = b[i++];
        std::size_t n = id < 64 ? 1 : id < 128 ? 2 : id < 192 ? 4 : 0;
        if (id >= 192)
            for (int shift = 0; i < b.size(); shift += 7)
            {
                const auto c = b[i++];
                n |= (std::size_t) (c & 0x7f) << shift;
                if (c < 128)
                    break;
            }
        if (i + n > b.size())
            break;
        if (id == 0xD5)
            return std::vector<std::uint8_t> (b.begin() + (std::ptrdiff_t) i, b.begin() + (std::ptrdiff_t) (i + n));
        i += n;
    }
    return {};
}

// a program with three zones as FL's "Add sample(s)" makes them
inline lynx::dw::Doc threeZones()
{
    using namespace lynx::dw;
    auto doc = freshDoc();
    auto p = program (doc, 0);
    p.zones.push_back (newZone ("C:\\s\\a.wav", "a", 44100, 1, 44100.0f, 16, 48, 0, 127));
    p.zones.push_back (newZone ("C:\\s\\b.wav", "b", 88200, 2, 44100.0f, 16, 60, 0, 127));
    p.zones.push_back (newZone ("C:\\s\\c.wav", "c", 1000, 1, 48000.0f, 32, 72, 0, 127));
    setProgram (doc, 0, p);
    return doc;
}
} // namespace dw190hz

TEST (directwave_doc_writes_every_fl_state_back_byte_for_byte)
{
    using namespace lynx::dw;
    // a state LYNX made: read and written back the same, and an unchanged program set back changes nothing
    const auto made = writeDoc (dw190hz::threeZones());
    const auto again = readDoc (made);
    CHECK (again.has_value());
    if (again)
    {
        CHECK (writeDoc (*again) == made);
        auto doc = *again;
        setProgram (doc, 0, program (doc, 0));
        CHECK (writeDoc (doc) == made);
    }
    CHECK (! readDoc ({ 1, 2, 3 }).has_value());
    CHECK (! readDoc ({ 39, 0, 0, 0 }).has_value());

    // every DirectWave channel of FL's demo projects (round 9 counted 57)
    if (realFlpDir.empty())
        return;
    int states = 0, exact = 0, unchanged = 0;
    for (auto& entry : std::filesystem::recursive_directory_iterator (realFlpDir))
    {
        if (entry.path().extension() != ".flp")
            continue;
        const auto project = flp::parseProject (dw190hz::readAll (entry.path()));
        for (const auto& c : project.channels)
        {
            if (c.pluginName != "DirectWave" || c.pluginState.empty())
                continue;
            ++states;
            const auto doc = readDoc (c.pluginState);
            if (! doc)
                continue;
            exact += writeDoc (*doc) == c.pluginState ? 1 : 0;
            auto edited = *doc;
            for (int p = 0; p < programCount (edited); ++p)
                setProgram (edited, p, program (edited, p));
            unchanged += writeDoc (edited) == c.pluginState ? 1 : 0;
        }
    }
    std::printf ("  %d DirectWave states: %d written back exactly, %d unchanged by setProgram\n", states, exact, unchanged);
    CHECK (states >= 57 && exact == states && unchanged == states);
}

TEST (directwave_fresh_state_is_fl_new_directwave)
{
    using namespace lynx::dw;
    const auto doc = freshDoc();
    CHECK (programCount (doc) == 128);
    CHECK (program (doc, 5).name == "---" && program (doc, 5).zones.empty());
    CHECK (slotProgram (doc, 0) == 0 && slotProgram (doc, 15) == 15);
    // FL's own new DirectWave: the plugin database's preset (Patches/Plugin database/Generators/Sampler/DirectWave.fst)
    if (realFlpDir.empty())
        return;
    const auto file = std::filesystem::path (realFlpDir) / "Patches" / "Plugin database" / "Generators" / "Sampler" / "DirectWave.fst";
    if (! std::filesystem::exists (file))
        return;
    const auto fl = dw190hz::stateEvent (dw190hz::readAll (file));
    CHECK (! fl.empty() && writeDoc (doc) == fl);
}

TEST (directwave_program_edits_rewrite_only_their_fields)
{
    using namespace lynx::dw;
    auto doc = dw190hz::threeZones();
    auto p = program (doc, 0);
    CHECK (p.zones.size() == 3);
    if (p.zones.size() != 3)
        return;
    // the zones read back with FL's new-zone values (the zone reset 0x180035870)
    const auto& a = p.zones[0];
    CHECK (a.name == "a" && a.path == "C:\\s\\a.wav" && a.root == 48 && a.highVel == 127 && a.keyTrack == 100);
    CHECK (a.gain == 1.0f && a.pan == 0.5f && a.pitch == 0.5f && a.amp[1] == 0.5f && a.amp[2] == 1.0f && a.amp[3] == 0.25f);
    CHECK (a.sends[2] == 0.5f && a.sends[3] == 1.0f && a.filter[0].type == 0 && a.filter[1].res == 0.5f);
    CHECK (a.mods[0].src == 2 && a.mods[0].dst == 2 && a.mods[0].amount == 1.0f && a.mods[1].dst == 34 && a.mods[2].src == 12);
    CHECK (a.beats == 8 && p.zones[1].beats == 16 && p.zones[2].beats == 4); // 1 s → 2 halves → 2·4; 2 s → 4·4; short → 1·4
    CHECK (a.origin == 0 && p.zones[2].origin == 2);
    // the program's zone count follows the zones
    auto state = writeDoc (doc);
    CHECK (parseState (state).has_value() && parseState (state)->programs.size() == 1);
    // a field edit changes that field's bytes only
    auto q = p;
    q.zones[1].root = 64;
    q.zones[1].name = "renamed";
    q.voiceMode = 1;
    q.name = "Test";
    setProgram (doc, 0, q);
    const auto back = program (doc, 0);
    CHECK (back.zones[1].root == 64 && back.zones[1].name == "renamed" && back.voiceMode == 1 && back.name == "Test");
    CHECK (back.zones[0].root == 48 && back.zones[2].root == 72);
    // delete and reorder: the remaining records keep their bytes
    auto r = back;
    std::swap (r.zones[0], r.zones[2]);
    r.zones.pop_back();
    setProgram (doc, 0, r);
    const auto after = program (doc, 0);
    CHECK (after.zones.size() == 2 && after.zones[0].name == "c" && after.zones[1].name == "renamed");
    CHECK (parseState (writeDoc (doc)).has_value());
}

TEST (directwave_zone_menu_actions_as_fl)
{
    using namespace lynx::dw;
    // Automap (0x180013d50): roots 48, 60, 72 → 0…59, 60…71, 72…127
    auto p = program (dw190hz::threeZones(), 0);
    auto z = p.zones;
    automap (z, { true, true, true });
    CHECK (z[0].lowKey == 0 && z[0].highKey == 59 && z[1].lowKey == 60 && z[1].highKey == 71 && z[2].lowKey == 72 && z[2].highKey == 127);
    // equal roots share the range; a zone without a sample keeps its keys but its root still ends the others' ranges,
    // and its low key counts for the lowest start
    z = p.zones;
    z[1].root = 48;
    z[2].lowKey = 5;
    z[2].highKey = 9;
    automap (z, { true, true, false });
    CHECK (z[0].lowKey == 48 && z[0].highKey == 71 && z[1].lowKey == 48 && z[1].highKey == 71 && z[2].lowKey == 0 && z[2].highKey == 9);
    // Remap as drumkit: roots 60, 61, 62, one key each, the first down to 0
    z = p.zones;
    remapAsDrumkit (z, { true, true, true });
    CHECK (z[0].root == 60 && z[1].root == 61 && z[2].root == 62);
    CHECK (z[0].lowKey == 0 && z[0].highKey == 60 && z[1].lowKey == 61 && z[1].highKey == 61 && z[2].lowKey == 62 && z[2].highKey == 127);
    // Copy to all / selected zones: not into locked zones
    z = p.zones;
    z[0].keyTrack = 50;
    z[2].output = 1; // locked
    copyToZones (z, 0, CopyKeyTracking, {});
    CHECK (z[1].keyTrack == 50 && z[2].keyTrack == 100);
    z[2].output = 0;
    copyToZones (z, 0, CopyRootKey, { false, false, true });
    CHECK (z[1].root == 60 && z[2].root == 48);
    // names and WAV chunks
    CHECK (zoneNameOf ("C:\\Samples\\Piano C4.wav") == "Piano C4" && zoneNameOf ("/a/b.c/kick") == "b" /* (FL cuts at the last dot first) */ && zoneNameOf ("x.y.wav") == "x.y");
    std::vector<std::uint8_t> wav { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E' };
    auto chunk = [&wav] (const char* id, std::vector<std::uint8_t> body) {
        wav.insert (wav.end(), id, id + 4);
        const auto n = (std::uint32_t) body.size();
        wav.insert (wav.end(), (const std::uint8_t*) &n, (const std::uint8_t*) &n + 4);
        wav.insert (wav.end(), body.begin(), body.end());
    };
    std::vector<std::uint8_t> smpl (60, 0);
    smpl[12] = 62;  // unity note
    smpl[28] = 1;   // one loop
    smpl[44] = 100; // start
    smpl[48] = 200; // end
    chunk ("smpl", smpl);
    const auto info = readWavInfo (wav);
    CHECK (info && info->root == 62 && info->loopMode == 2 && info->loopA == 100 && info->loopB == 201);
}

TEST (directwave_program_and_bank_files)
{
    using namespace lynx::dw;
    auto doc = dw190hz::threeZones();
    const auto dwp = writeProgramFile (doc, 0);
    CHECK (dwp.size() > 8 && std::memcmp (dwp.data(), "DwPr", 4) == 0 && dwp[4] == 38);
    const auto loaded = readProgramFile (dwp);
    CHECK (loaded && loaded->zones.size() == 3 && loaded->zones[1].name == "b");
    // into another program record: its own index stays, the zones are new records
    if (loaded)
    {
        loadProgram (doc, 7, *loaded);
        const auto p7 = program (doc, 7);
        CHECK (p7.index == 7 && p7.zones.size() == 3 && p7.zones[2].root == 72);
        CHECK (writeProgramFile (doc, 7).size() == dwp.size());
    }
    const auto dwb = writeBankFile (doc);
    const auto bank = readBankFile (dwb);
    CHECK (bank && writeDoc (*bank) == writeDoc (doc));
    CHECK (! readProgramFile (dwb) && ! readBankFile (dwp));
    // FL's own .dwp files (versions 36 and 37)
    if (realFlpDir.empty())
        return;
    int files = 0, read = 0;
    for (auto& entry : std::filesystem::recursive_directory_iterator (std::filesystem::path (realFlpDir) / "Patches"))
        if (entry.path().extension() == ".dwp")
        {
            ++files;
            const auto p = readProgramFile (dw190hz::readAll (entry.path()));
            read += p && ! p->zones.empty() ? 1 : 0;
        }
    std::printf ("  %d .dwp files in FL's Patches, %d read with zones\n", files, read);
    CHECK (read == files);
}

TEST (directwave_slot_program_picks_the_played_program)
{
    using namespace lynx::dw;
    auto doc = dw190hz::threeZones();
    auto p = program (doc, 0);
    // program 3 gets one zone; MIDI slot 0 still plays program 0
    auto three = program (doc, 3);
    three.zones.push_back (p.zones[0]);
    three.zones.back().origin = -1;
    three.zones.back().name = "three";
    setProgram (doc, 3, three);
    auto st = parseState (writeDoc (doc));
    CHECK (st && playedProgram (*st) && playedProgram (*st)->index == 0);
    setSlotProgram (doc, 0, 3);
    st = parseState (writeDoc (doc));
    CHECK (st && playedProgram (*st) && playedProgram (*st)->index == 3 && playedProgram (*st)->zones[0].name == "three");
    // a slot on an empty program: the first program with zones, as before
    setSlotProgram (doc, 0, 9);
    st = parseState (writeDoc (doc));
    CHECK (st && playedProgram (*st) && playedProgram (*st)->index == 0);
}

TEST (directwave_program_from_sfz)
{
    using namespace lynx::dw;
    const auto parsed = lynx::sfz::parse ("<control> default_path=s\\ \n<group> lovel=1 hivel=100 loop_mode=loop_continuous\n"
                                          "<region> sample=a.wav lokey=c3 hikey=b3 pitch_keycenter=c4 tune=-50 volume=-6 pan=50\n"
                                          "<region> sample=b.wav key=72 loop_mode=one_shot transpose=2\n<region> sample=missing.wav key=10\n");
    const auto p = programFromSfz (parsed, "/k", [] (const std::string& path) { return path.find ("missing") == std::string::npos ? path : std::string(); });
    CHECK (p.zones.size() == 2);
    if (p.zones.size() != 2)
        return;
    const auto& a = p.zones[0];
    CHECK (a.path == "/k/s/a.wav" && a.name == "a" && a.lowKey == 48 && a.highKey == 59 && a.root == 60 && a.lowVel == 1 && a.highVel == 100);
    CHECK (std::fabs (a.pitch - (0.5f - 50.0f / 2400.0f)) < 1e-6f && std::fabs (a.gain * a.gain - 0.501187f) < 1e-4f && a.pan == 0.75f && a.loopMode == 2);
    const auto& b = p.zones[1];
    CHECK (b.lowKey == 72 && b.highKey == 72 && b.root == 72 && b.coarse == 2 && b.loopMode == 1);
}

TEST (directwave_new_zone_is_fl_new_zone_byte_for_byte)
{
    using namespace lynx::dw;
    // FL 2026's own save after "Add sample(s)..." of our 16-bit mono tone (52920 frames at 44.1 kHz) in a DirectWave
    // program (round 190hz FL session, /mnt/e/lynx-work/dwave190hz/fl/dw_1.flp: the zone record FL wrote, its 912 bytes)
    const char* const fl =
        "030000008403000000000000f401000019000000000000003c007f007f000000000000803f0000003f0000000000020000f501000004000000000000"
        "00746f6e65f60100002300000000000000453a5c6c796e782d776f726b5c6477617665313930687a5c666c5c746f6e652e776176f701000028000000"
        "00000000b8ce000000000000010000000800000000442c470000000000000000000000000000000010000000f801000008000000000000000000003f"
        "00006400f90100000e0000000000000000000000003f0000003f0000803ffa01000030000000000000000000003f0000003f0000003f0000803f0000"
        "000000000000000000000000000000000000000000000000000000000000fb010000140000000000000000000000c8e3f13e0000003f000000000000"
        "0000fb010000140000000000000000000000c8e3f13e0000003f0000000000000000fc01000002000000000000000000fd0100001000000000000000"
        "000000000000003f0000803f0000803efe010000090000000000000000000000000000803fff010000090000000000000000000000000000803f0002"
        "0000090000000000000000000000000000803f010200000900000000000000000000003f0000803f020200001000000000000000000000000000003f"
        "0000003f0000803e020200001000000000000000000000000000003f0000003f0000803e03020000140000000000000000000000cdcccc3d0000803f"
        "000000000000000003020000140000000000000000000000cdcc0c3f0000803f0000000000000000040200000800000000000000020002000000803f"
        "040200000800000000000000030022000000403f0402000008000000000000000c0001000000003f040200000800000000000000000000000000003f"
        "040200000800000000000000000000000000003f040200000800000000000000000000000000003f040200000800000000000000000000000000003f"
        "040200000800000000000000000000000000003f040200000800000000000000000000000000003f040200000800000000000000000000000000003f"
        "040200000800000000000000000000000000003f040200000800000000000000000000000000003f040200000800000000000000000000000000003f"
        "040200000800000000000000000000000000003f040200000800000000000000000000000000003f040200000800000000000000000000000000003f"
        "040000000000000000000000";
    std::vector<std::uint8_t> expected;
    for (std::size_t i = 0; fl[i] != 0 && fl[i + 1] != 0; i += 2)
        expected.push_back ((std::uint8_t) std::stoi (std::string (fl + i, 2), nullptr, 16));
    auto doc = freshDoc();
    auto p = program (doc, 0);
    p.zones.push_back (newZone ("E:\\lynx-work\\dwave190hz\\fl\\tone.wav", zoneNameOf ("E:\\lynx-work\\dwave190hz\\fl\\tone.wav"), 52920, 1, 44100.0f, 16, 60, 0, 127));
    setProgram (doc, 0, p);
    const auto state = writeDoc (doc);
    // the zone record inside the state: find its header (id 3) by the expected bytes
    const auto at = std::search (state.begin(), state.end(), expected.begin(), expected.end());
    CHECK (expected.size() == 912 && at != state.end());
}
