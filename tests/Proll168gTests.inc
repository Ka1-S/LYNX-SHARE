// Round 168g: FL's Piano roll menu items (docs/analise/REA_rodada168g.md): Select one / more at random, Select odd,
// Insert / Slice & insert / Delete space, Trim, Open / Save score (.fsc), and the keys of the key map
namespace proll168g
{
lynx::Pattern pattern (std::initializer_list<std::pair<std::int64_t, std::int64_t>> notes, int channel = 0)
{
    lynx::Pattern p;
    int key = 60;
    for (const auto& [start, length] : notes)
    {
        lynx::Note n;
        n.start = start;
        n.length = length;
        n.channel = channel;
        n.key = key++;
        p.notes.push_back (n);
    }
    return p;
}

std::vector<int> all (const lynx::Pattern& p)
{
    std::vector<int> out;
    for (int i = 0; i < (int) p.notes.size(); ++i)
        out.push_back (i);
    return out;
}
} // namespace proll168g

TEST (proll168g_select_random_adds_one_or_half_of_the_channel)
{
    // FUN_00eb2510: Shift+R adds one not-yet-selected note, Shift+M adds count / 2 of them; the selection grows
    const auto p = proll168g::pattern ({ { 0, 10 }, { 100, 10 }, { 200, 10 }, { 300, 10 }, { 400, 10 }, { 500, 10 }, { 600, 10 } });
    const auto one = lynx::noteedit::selectRandomMore (p, proll168g::all (p), { 2 }, false, 7);
    CHECK (one.size() == 2 && std::find (one.begin(), one.end(), 2) != one.end());
    const auto more = lynx::noteedit::selectRandomMore (p, proll168g::all (p), { 2 }, true, 7);
    CHECK (more.size() == 1 + 3); // 7 / 2 = 3 rounds
    // every note already selected: nothing to add
    CHECK (lynx::noteedit::selectRandomMore (p, proll168g::all (p), proll168g::all (p), true, 3).size() == 7);
    // only the channel's notes are candidates
    const auto some = lynx::noteedit::selectRandomMore (p, { 0, 1 }, {}, true, 11);
    CHECK (some.size() == 1 && some[0] <= 1);
}

TEST (proll168g_select_odd_takes_the_notes_on_odd_steps)
{
    // FUN_00eb2b70: ((start + step / 2) / step) odd, added to the selection
    const auto s = lynx::ticksPerStep;
    const auto p = proll168g::pattern ({ { 0, 10 }, { s, 10 }, { 2 * s, 10 }, { 3 * s - s / 4, 10 }, { 3 * s + s / 2, 10 } });
    const auto odd = lynx::noteedit::selectOddSteps (p, proll168g::all (p), { 0 }, s);
    CHECK ((odd == std::vector<int> { 0, 1, 3 })); // 3s+s/2 rounds to step 4 (even)
}

TEST (proll168g_insert_and_delete_space_move_the_later_notes)
{
    const auto s = lynx::ticksPerStep;
    // insert [4s, 6s): notes at or after 4s move right by 2s; a note crossing 4s stays (no slice)
    auto p = proll168g::pattern ({ { 0, s }, { 3 * s, 2 * s }, { 4 * s, s }, { 8 * s, s } });
    auto moved = lynx::noteedit::insertSpace (p, proll168g::all (p), 4 * s, 6 * s, false);
    CHECK ((moved == std::vector<int> { 2, 3 }));
    CHECK (p.notes[1].start == 3 * s && p.notes[1].length == 2 * s);
    CHECK (p.notes[2].start == 6 * s && p.notes[3].start == 10 * s);

    // Slice & insert: the crossing note is cut at 4s and its tail moves too
    p = proll168g::pattern ({ { 3 * s, 2 * s } });
    moved = lynx::noteedit::insertSpace (p, proll168g::all (p), 4 * s, 6 * s, true);
    CHECK (p.notes.size() == 2 && moved.size() == 1);
    CHECK (p.notes[0].start == 3 * s && p.notes[0].length == s);
    CHECK (p.notes[1].start == 6 * s && p.notes[1].length == s && p.notes[1].key == p.notes[0].key);

    // Delete [2s, 4s): cut at both edges, the inside removed, the rest moves left by 2s
    p = proll168g::pattern ({ { 0, s }, { s, 2 * s }, { 2 * s, s }, { 3 * s, 2 * s }, { 6 * s, s } });
    moved = lynx::noteedit::deleteSpace (p, proll168g::all (p), 2 * s, 4 * s);
    p.sortNotes();
    std::vector<std::pair<std::int64_t, std::int64_t>> got;
    for (const auto& n : p.notes)
        got.push_back ({ n.start / s, n.length / s });
    CHECK ((got == std::vector<std::pair<std::int64_t, std::int64_t>> { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 4, 1 } }));
    CHECK (moved.size() == 2);

    // Trim [2s, 4s): only the selection is left, at the start
    p = proll168g::pattern ({ { 0, s }, { 2 * s, s }, { 3 * s, s }, { 5 * s, s } });
    lynx::noteedit::trimToSelection (p, 0, 2 * s, 4 * s, 16 * s);
    p.sortNotes();
    CHECK (p.notes.size() == 2 && p.notes[0].start == 0 && p.notes[1].start == s);

    // another channel's notes are left alone (only the given indices)
    p = proll168g::pattern ({ { 4 * s, s } });
    p.notes.push_back (p.notes[0]);
    p.notes[1].channel = 3;
    lynx::noteedit::insertSpace (p, { 0 }, 0, s, false);
    CHECK (p.notes[0].start == 5 * s && p.notes[1].start == 4 * s);
}

TEST (proll168g_score_round_trips_through_an_fsc)
{
    // Save score as / Open score: an .fsc as FL writes it from the piano roll (format 0x10, 0x41 = 0, 24-byte 0xE0
    // records with channel 0), read back by the same reader as FL's own scores
    std::vector<lynx::Note> notes (3);
    notes[0].start = 0, notes[0].length = 480, notes[0].key = 60, notes[0].velocity = 100.0f / 128.0f;
    notes[1].start = 960, notes[1].length = 240, notes[1].key = 67, notes[1].pan = -0.5f, notes[1].slide = true, notes[1].colour = 3;
    notes[2].start = 1920, notes[2].length = 960, notes[2].key = 72, notes[2].muted = true, notes[2].porta = true, notes[2].finePitch = 30.0f;
    for (auto& n : notes)
        n.channel = 5;
    const auto bytes = lynx::prtools::writeScore (notes);
    REA167K_REQUIRE (bytes.size() > 22 && bytes[0] == 'F' && bytes[1] == 'L' && bytes[2] == 'h' && bytes[3] == 'd');
    CHECK (bytes[8] == 0x10 && bytes[9] == 0); // the header's format: score
    const auto score = lynx::prtools::parseScore (bytes);
    REA167K_REQUIRE (score.has_value() && score->notes.size() == 3);
    const auto back = lynx::prtools::notesFromScore (*score, 2);
    REA167K_REQUIRE (back.size() == 3);
    for (std::size_t i = 0; i < 3; ++i)
    {
        CHECK (back[i].start == notes[i].start && back[i].length == notes[i].length && back[i].key == notes[i].key);
        CHECK (back[i].channel == 2);
        CHECK (std::abs (back[i].velocity - notes[i].velocity) < 0.01f && std::abs (back[i].pan - notes[i].pan) < 0.02f);
        CHECK (back[i].slide == notes[i].slide && back[i].muted == notes[i].muted && back[i].porta == notes[i].porta);
        CHECK (back[i].colour == notes[i].colour && std::abs (back[i].finePitch - notes[i].finePitch) < 0.5f);
    }
    // the record's channel field is 0 (FUN_012dfdb0 (list, 0)) and the flags carry 0x4000 (+ 8 = slide)
    std::size_t e0 = 0;
    for (std::size_t i = 22; i + 1 < bytes.size(); ++i)
        if (bytes[i] == 0xE0 && bytes[i + 1] == 72) // 3 records = 72 bytes
            e0 = i + 2;
    REA167K_REQUIRE (e0 > 0);
    CHECK (bytes[e0 + 6] == 0 && bytes[e0 + 7] == 0);
    CHECK (bytes[e0 + 4] == 0x00 && bytes[e0 + 5] == 0x40);
    CHECK (bytes[e0 + 24 + 4] == 0x08 && bytes[e0 + 24 + 5] == 0x40);
}

TEST (proll168g_fl_scores_open_as_notes)
{
    // every FL factory score in Data/Patches/Scores reads and opens as notes on the channel (when FL's Data is given)
    if (realFlpDir.empty())
        return;
    const auto folder = std::filesystem::path (realFlpDir) / "Patches" / "Scores";
    if (! std::filesystem::exists (folder))
        return;
    int files = 0, read = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (folder))
    {
        if (entry.path().extension() != ".fsc")
            continue;
        ++files;
        std::ifstream in (entry.path(), std::ios::binary);
        std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const auto score = lynx::prtools::parseScore (bytes);
        if (! score.has_value())
            continue;
        const auto notes = lynx::prtools::notesFromScore (*score, 1);
        if (notes.size() == score->notes.size() && std::all_of (notes.begin(), notes.end(), [] (const lynx::Note& n) { return n.channel == 1; }))
            ++read;
    }
    CHECK (files > 500 && read == files);
}

TEST (proll168g_piano_roll_keys_are_fl_s)
{
    using namespace lynx::keys;
    KeyMap map;
    const auto pr = Context::pianoRoll;
    CHECK (map.find ({ 'B', alt }, pr, false) == "pianoRoll.gridHighlights");
    CHECK (map.find ({ 'N', alt }, pr, false) == "pianoRoll.backgroundWave");
    CHECK (map.find ({ 'R', shift }, pr, false) == "pianoRoll.selectRandomOne");
    CHECK (map.find ({ 'M', shift }, pr, false) == "pianoRoll.selectRandomMore");
    CHECK (map.find ({ 'O', shift }, pr, false) == "pianoRoll.selectOddSteps");
    CHECK (map.find ({ 'N', shift }, pr, false) == "pianoRoll.stampNone");
    CHECK (map.find ({ insert, ctrl }, pr, false) == "pianoRoll.insertSpace");
    CHECK (map.find ({ insert, (std::uint8_t) (ctrl | alt) }, pr, false) == "pianoRoll.sliceInsertSpace");
    CHECK (map.find ({ del, ctrl }, pr, false) == "pianoRoll.deleteSpace");
    CHECK (map.find ({ 'I', ctrl }, pr, false) == "pianoRoll.insertControllerValue");
    CHECK (map.find ({ 'M', ctrl }, pr, false) == "pianoRoll.importMidi");
    CHECK (map.find ({ 'M', (std::uint8_t) (ctrl | shift) }, pr, false) == "pianoRoll.exportMidi");
    CHECK (map.find ({ 'V', (std::uint8_t) (ctrl | shift) }, pr, false) == "pianoRoll.pasteMidiClipboard");
    CHECK (map.find ({ 'T', alt }, pr, false) == "pianoRoll.markerAdd");
    CHECK (map.find ({ 'T', ctrl }, pr, false) == "pianoRoll.markerAuto");
    CHECK (map.find ({ 'T', (std::uint8_t) (shift | alt) }, pr, false) == "pianoRoll.markerSignature");
    // outside the Piano roll the global keys stay (metronome, wait for input, quick audio recording)
    CHECK (map.find ({ 'M', ctrl }, Context::playlist, false) == "transport.metronome");
    CHECK (map.find ({ 'I', ctrl }, Context::global, false) == "record.waitForInput");
    CHECK (map.conflicts().empty());
}

TEST (proll168g_flp_pattern_markers_stay_with_their_pattern)
{
    // FL writes a pattern's Piano roll markers between its 0x41 and the next section (Edlan & Ella Noël's pattern 10:
    // 0x94 = 11 << 24 | 384 "Loop: Bar", 6 << 24 | 1536 "Length"; Sid De La Cruz's patterns: type 8 signatures); the
    // Playlist's come after the arrangement / playlist events. Before round 168g they all went to the Playlist.
    FlpBuilder b (96);
    b.ascii (0xC7, "25.1.2.4893");
    b.u8 (0x41); b.u16 (10);
    b.utf16 (0xC1, u"Halftime");
    b.u8 (0x94); b.u32 (11u << 24 | 384);
    b.u8 (0x21); b.u8 (4);
    b.u8 (0x22); b.u8 (4);
    b.utf16 (0xCD, u"Loop: Bar");
    b.u8 (0x94); b.u32 (8u << 24 | 768);
    b.u8 (0x21); b.u8 (3);
    b.u8 (0x22); b.u8 (4);
    b.utf16 (0xCD, u"3/4");
    b.u8 (0x41); b.u16 (11);
    b.u8 (0x63); b.u16 (0); // the arrangement
    b.u8 (0x94); b.u32 (1920);
    b.utf16 (0xCD, u"Chorus");
    const auto p = flp::parseProject (b.bytes);
    REA167K_REQUIRE (p.arrangements.size() == 1 && p.arrangements[0].markers.size() == 1);
    CHECK (p.arrangements[0].markers[0].position == 1920 && p.arrangements[0].markers[0].name.find ("Chorus") == 0);
    const auto it = std::find_if (p.patterns.begin(), p.patterns.end(), [] (const flp::Pattern& x) { return x.index == 10; });
    REA167K_REQUIRE (it != p.patterns.end() && it->markers.size() == 2);
    CHECK (it->markers[0].type == 11 && it->markers[0].position == 384 && it->markers[0].name.find ("Loop: Bar") == 0);
    CHECK (it->markers[1].type == 8 && it->markers[1].numerator == 3 && it->markers[1].denominator == 4);

    const auto song = lynx::importFlp (p);
    CHECK (song.markers().size() == 1);
    const auto pit = std::find_if (song.patterns.begin(), song.patterns.end(), [] (const lynx::Pattern& x) { return x.name == "Halftime"; });
    REA167K_REQUIRE (pit != song.patterns.end() && pit->markers.size() == 2);
    CHECK (pit->markers[0].tick == 384 * lynx::ppq / 96 && pit->markers[0].action == 11 && pit->markers[0].name == "Loop: Bar");
    CHECK (pit->markers[1].tick == 768 * lynx::ppq / 96 && pit->markers[1].isSignature() && pit->markers[1].numerator == 3);
}

TEST (proll168g_pattern_markers_add_auto_signature_and_follow_the_space)
{
    using namespace lynx::noteedit;
    lynx::Pattern p;
    const auto bar = 4 * lynx::ppq;
    CHECK (addPatternAutoMarker (p, bar) == 0 && p.markers[0].name == "Auto");
    CHECK (addPatternAutoMarker (p, bar) == 0 && p.markers.size() == 1); // one already there: kept
    addPatternAutoMarker (p, 0);
    CHECK (p.markers.size() == 2 && p.markers[0].tick == 0 && p.markers[0].name == "Auto #2");

    // a 3/4 change over [bar, 2 bars): the old 4/4 comes back at its end; names are FL's "%d/%d"
    addPatternSignatureChange (p, bar + 10, 2 * bar, 3, 4, lynx::Meter (4, 4));
    REA167K_REQUIRE (p.markers.size() == 4);
    CHECK (patternSignatureAt (p, bar + lynx::ppq, lynx::Meter (4, 4)) == std::make_pair (3, 4));
    CHECK (patternSignatureAt (p, 2 * bar, lynx::Meter (4, 4)) == std::make_pair (4, 4));
    CHECK (patternSignatureAt (p, 0, lynx::Meter (4, 4)) == std::make_pair (4, 4));
    const auto sig = std::find_if (p.markers.begin(), p.markers.end(), [] (const lynx::Marker& m) { return m.isSignature() && m.numerator == 3; });
    CHECK (sig != p.markers.end() && sig->tick == bar && sig->name == "3/4");

    // Insert / Delete space move the markers like the notes (FUN_00e3b2b0); the ones inside a deleted space go
    lynx::Pattern q;
    addPatternMarker (q, { bar, "A", 0, 4 });
    addPatternMarker (q, { 3 * bar, "B", 0, 4 });
    insertSpace (q, {}, bar, 2 * bar, false);
    CHECK (q.markers[0].tick == 2 * bar && q.markers[1].tick == 4 * bar);
    deleteSpace (q, {}, 0, 3 * bar);
    REA167K_REQUIRE (q.markers.size() == 1);
    CHECK (q.markers[0].name == "B" && q.markers[0].tick == bar);
}
