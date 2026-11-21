// Round 190nu (GitHub discussion #105, part 2): key regions on the Piano roll ruler (FL's Key/Scale markers), the Chord
// stamp's Scale degree chord, the regions in .flp and MIDI files, and the Chord helper's pinned relationships
// (core/KeyRegions.h, core/ModalHelper.h favourites).
#include <fstream>
#include <iterator>

#include "core/KeyRegions.h"

namespace keys190nu
{
using namespace lynx::keyregions;
inline State key (int root, const char* scale) { return { State::key, { root, flScaleNamed (scale) } }; }
inline std::vector<int> chordOn (const Degrees& d, int clicked, DegreeSize size = DegreeSize::triad, int inversion = 0)
{
    int root = clicked;
    auto iv = diatonicChord (d, clicked, size, inversion, root);
    for (auto& i : iv)
        i += root;
    return iv;
}
} // namespace keys190nu

TEST (keys190nu_markers_and_spans)
{
    using namespace keys190nu;
    lynx::Pattern p;
    CHECK (! hasKeyMarkers (p));
    CHECK (spans (p, 3840).size() == 1 && spans (p, 3840)[0].state.kind == State::editor);
    // FL's check (190nu): C Major (Ionian) at bar 1, C Minor Natural (Aeolian) at bar 3
    addKeyMarker (p, 0, key (0, "Major (Ionian)"));
    addKeyMarker (p, 2 * 4 * lynx::ppq, key (0, "Minor Natural (Aeolian)"));
    CHECK (p.markers.size() == 2 && p.markers[0].name == "C Major (Ionian)" && p.markers[1].name == "C Minor Natural (Aeolian)");
    CHECK (p.markers[1].action == lynx::Marker::keyScale && p.markers[1].numerator == 0);
    const auto bar = 4 * lynx::ppq;
    CHECK (stateAt (p, bar).value == (lynx::projectkey::Key { 0, majorScale() }));
    CHECK (stateAt (p, 2 * bar).value == (lynx::projectkey::Key { 0, minorScale() }));
    const auto s = spans (p, 4 * bar);
    CHECK (s.size() == 2 && s[0].from == 0 && s[0].to == 2 * bar && s[1].to == 4 * bar);
    CHECK (degreesOf (s[1].state.value).contains (63) && ! degreesOf (s[1].state.value).contains (64)); // (E♭ in, E out)
    // a key marker at the same tick takes the new key; FL renames it after the key (C → A: "A Minor Natural (Aeolian)")
    addKeyMarker (p, 2 * bar, key (9, "Minor Natural (Aeolian)"));
    CHECK (p.markers.size() == 2 && p.markers[1].name == "A Minor Natural (Aeolian)");
    // a typed name stays
    p.markers[1].name = "Chorus";
    setState (p.markers[1], key (9, "Minor Harmonic"));
    CHECK (p.markers[1].name == "Chorus" && keyOf (p.markers[1])->flScale == flScaleNamed ("Minor Harmonic"));
    // Default / Black notes
    setState (p.markers[0], { State::none, {} });
    CHECK (stateAt (p, 0).kind == State::none && p.markers[0].name == "Black notes" && ! keyOf (p.markers[0]));
    setState (p.markers[0], { State::editor, {} });
    CHECK (stateAt (p, 0).kind == State::editor && p.markers[0].keyFlScale == defaultScale);
}

TEST (keys190nu_key_region_over_a_time_selection)
{
    using namespace keys190nu;
    const auto bar = 4 * lynx::ppq;
    lynx::Pattern p;
    p.markers.push_back ({ bar / 2, "Verse", 0, 4 }); // a plain marker stays where it is
    // esiowry's example: bars 1-2 C Ionian, bar 3 C natural minor, then back
    addKeyRegion (p, 0, 2 * bar, key (0, "Major (Ionian)"));
    CHECK (p.markers.size() == 3); // (Verse, C Ionian at 0, and a Default one at bar 3 bringing back the editor's scale)
    addKeyRegion (p, 2 * bar, 3 * bar, key (0, "Minor Natural (Aeolian)"));
    const auto s = spans (p, 5 * bar);
    // C Ionian [0, 2 bars), C Aeolian [2, 3), then what applied there before: Default (the editor's scale) from bar 3
    CHECK (stateAt (p, bar).value == (lynx::projectkey::Key { 0, majorScale() }));
    CHECK (stateAt (p, 2 * bar + 1).value == (lynx::projectkey::Key { 0, minorScale() }));
    CHECK (stateAt (p, 3 * bar).kind == State::editor);
    CHECK (std::any_of (p.markers.begin(), p.markers.end(), [] (const lynx::Marker& m) { return m.name == "Verse"; }));
    // a region over keys inside replaces them; the key at its end comes back
    lynx::Pattern q;
    addKeyMarker (q, 0, key (7, "Major (Ionian)"));
    addKeyMarker (q, bar, key (2, "Other Dorian"));
    addKeyRegion (q, bar / 2, 2 * bar, key (5, "Other Lydian"));
    CHECK (stateAt (q, bar).value.root == 5 && stateAt (q, 2 * bar).value.root == 2 && stateAt (q, 0).value.root == 7);
    int keyMarkers = 0;
    for (const auto& m : q.markers)
        keyMarkers += isKeyMarker (m) ? 1 : 0;
    CHECK (keyMarkers == 3); // (G at 0, F Lydian at bar ½, D Dorian brought back at bar 2; the one at bar 1 went)
    // a region whose end already has the same key adds no marker there
    lynx::Pattern r;
    addKeyRegion (r, 0, bar, { State::editor, {} });
    CHECK (r.markers.size() == 1);
}

TEST (keys190nu_fl_fields)
{
    using namespace keys190nu;
    // the masks FL 2026 wrote (190nu, ref/flkeys.flp): bit 11 − pitch class of each key out of the scale
    CHECK (flOutOfScaleMask ({ 0, majorScale() }) == 1322);
    CHECK (flOutOfScaleMask ({ 9, minorScale() }) == 1322);
    CHECK (flOutOfScaleMask ({ 0, flScaleNamed ("Minor Harmonic") }) == 1190);
    CHECK (flOutOfScaleMask ({ 0, flScaleNamed ("Major Pentatonic") }) == 1387);
    // FL's own A minor marker carried scale index 0 with the A minor mask: the mask decides
    const auto a = keyFromFl (9, 0, 1322);
    CHECK (a && a->root == 9 && a->flScale == minorScale());
    CHECK (keyFromFl (0, 7, 1190)->flScale == 7);
    CHECK (keyFromFl (0, 4, 0)->flScale == 4); // (no mask: the index)
    CHECK (! keyFromFl (0, -1, 0));
    // export → import through FL's format keeps every key marker
    auto song = lynx::Song::makeDefault();
    auto& p = song.patterns[(std::size_t) song.currentPattern];
    addKeyMarker (p, 0, key (2, "Other Dorian"));
    addKeyMarker (p, 4 * lynx::ppq, key (0, "Minor Harmonic"));
    addKeyMarker (p, 8 * lynx::ppq, { State::none, {} });
    p.notes.push_back ([] { lynx::Note n; n.key = 60; n.length = 96; return n; }());
    lynx::flpexport::Report report;
    const auto project = flp::parseProject (lynx::flpexport::exportFlp (song, {}, report));
    const auto back = lynx::importFlp (project);
    bool found = false;
    for (const auto& bp : back.patterns)
        if (hasKeyMarkers (bp))
        {
            found = true;
            CHECK (stateAt (bp, 0).value == (lynx::projectkey::Key { 2, flScaleNamed ("Other Dorian") }));
            CHECK (stateAt (bp, 4 * lynx::ppq).value == (lynx::projectkey::Key { 0, flScaleNamed ("Minor Harmonic") }));
            CHECK (stateAt (bp, 8 * lynx::ppq).kind == State::none);
            CHECK (bp.markers[0].name == "D Other Dorian");
        }
    CHECK (found);
    // the project FL 2026 saved in round 190nu (four key markers), when it is on this machine
    if (std::ifstream in ("/mnt/e/lynx-work/chord2_190nu/ref/flkeys.flp", std::ios::binary); in)
    {
        const std::vector<std::uint8_t> data ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const auto flSong = lynx::importFlp (flp::parseProject (data));
        std::vector<std::string> names;
        for (const auto& fp : flSong.patterns)
            for (const auto& m : fp.markers)
                if (isKeyMarker (m) && keyOf (m))
                    names.push_back (lynx::projectkey::name (*keyOf (m)));
        CHECK (names == (std::vector<std::string> { "C Major (Ionian)", "A Minor Natural (Aeolian)", "C Minor Harmonic", "C Major Pentatonic" }));
    }
}

TEST (keys190nu_midi_signatures)
{
    using namespace keys190nu;
    using lynx::projectkey::Key;
    CHECK (midiSignature ({ 0, majorScale() }) == std::make_pair (0, 0));
    CHECK (midiSignature ({ 9, minorScale() }) == std::make_pair (0, 1));
    CHECK (midiSignature ({ 7, majorScale() }) == std::make_pair (1, 0));
    CHECK (midiSignature ({ 6, majorScale() }) == std::make_pair (6, 0));
    CHECK (midiSignature ({ 5, majorScale() }) == std::make_pair (-1, 0));
    CHECK (midiSignature ({ 2, flScaleNamed ("Other Dorian") }) == std::make_pair (0, 1));     // (C major's notes, minor third)
    CHECK (midiSignature ({ 7, flScaleNamed ("Other Mixolydian") }) == std::make_pair (0, 0)); // (C major's notes)
    CHECK (midiSignature ({ 0, flScaleNamed ("Minor Harmonic") }) == std::make_pair (-3, 1));  // (as C minor)
    CHECK (midiSignature ({ 0, flScaleNamed ("Minor Pentatonic") }) == std::make_pair (-3, 1));
    CHECK (keyFromMidiSignature (-3, 1) == (Key { 0, minorScale() }));
    CHECK (keyFromMidiSignature (2, 0) == (Key { 2, majorScale() }));
    CHECK (keyFromMidiSignature (-1, 0) == (Key { 5, majorScale() }));
    // LYNX's marker meta event keeps the exact scale
    const Key dorian { 2, flScaleNamed ("Other Dorian") };
    CHECK (midiKeyText (dorian) == "Key: D Other Dorian");
    CHECK (keyFromMidiText ("Key: D Other Dorian") == dorian);
    CHECK (keyFromMidiText ("Key: C# Minor Harmonic") == (Key { 1, flScaleNamed ("Minor Harmonic") }));
    CHECK (! keyFromMidiText ("Chorus") && ! keyFromMidiText ("Key: H Major"));
    // the key changes a pattern gives a MIDI file: the project key first, a repeat left out, a Black notes span starts none
    lynx::Pattern p;
    addKeyMarker (p, 4 * lynx::ppq, key (0, "Major (Ionian)"));
    addKeyMarker (p, 8 * lynx::ppq, key (0, "Minor Harmonic"));
    addKeyMarker (p, 12 * lynx::ppq, { State::none, {} });
    addKeyMarker (p, 16 * lynx::ppq, { State::editor, {} });
    const auto changes = keyChanges (p, Key { 0, majorScale() }, 20 * lynx::ppq);
    CHECK (changes.size() == 3 && changes[0].first == 0 && changes[1].first == 8 * lynx::ppq && changes[2].first == 16 * lynx::ppq);
    CHECK (keyChanges (p, std::nullopt, 20 * lynx::ppq).front().first == 4 * lynx::ppq);
}

TEST (keys190nu_midi_import_makes_key_markers)
{
    using namespace keys190nu;
    lynx::midiimport::ParsedMidi midi;
    midi.trackNames = { "Keys" };
    midi.events.push_back ({ 0, 0, 0x90, 60, 100 });
    midi.events.push_back ({ 0, 96, 0x80, 60, 0 });
    midi.keys.push_back ({ 0, 2, flScaleNamed ("Other Dorian") });
    midi.keys.push_back ({ 8 * lynx::ppq, 0, minorScale() });
    // a new project: the key at the start is the project key, the later one a key marker
    lynx::Song song;
    std::string error;
    lynx::midiimport::Options o;
    o.newProject = true;
    CHECK (lynx::midiimport::importInto (song, midi, o, "Imported", error));
    CHECK (lynx::projectkey::get (song) == (lynx::projectkey::Key { 2, flScaleNamed ("Other Dorian") }));
    const auto& p = song.patterns[(std::size_t) song.currentPattern];
    CHECK (p.markers.size() == 1 && stateAt (p, 8 * lynx::ppq).value == (lynx::projectkey::Key { 0, minorScale() }));
    // into a project in another key: a key marker at the start too
    auto other = lynx::Song::makeDefault();
    lynx::projectkey::set (other, lynx::projectkey::Key { 0, majorScale() });
    o.newProject = false;
    CHECK (lynx::midiimport::importInto (other, midi, o, "Imported", error));
    CHECK (other.patterns.back().markers.size() == 2);
}

TEST (keys190nu_song_key_markers_follow_the_clips)
{
    using namespace keys190nu;
    auto song = lynx::Song::makeDefault();
    song.patterns.resize (1);
    auto& p = song.patterns[0];
    addKeyMarker (p, 0, key (0, "Major (Ionian)"));
    addKeyMarker (p, 2 * lynx::ppq, key (9, "Minor Natural (Aeolian)"));
    p.notes.push_back ([] { lynx::Note n; n.key = 60; n.length = 96; return n; }());
    song.mode = lynx::PlayMode::song;
    song.playlist.clear();
    lynx::Clip a;
    a.kind = lynx::ClipKind::pattern;
    a.pattern = 0;
    a.start = 16 * lynx::ppq;
    a.length = 4 * lynx::ppq;
    song.playlist.push_back (a);
    lynx::Clip b = a; // starts inside the A minor region
    b.start = 32 * lynx::ppq;
    b.offset = 3 * lynx::ppq;
    b.length = lynx::ppq;
    song.playlist.push_back (b);
    const auto marks = songKeyMarkers (song);
    CHECK (marks.size() == 3);
    CHECK (marks[0].tick == 16 * lynx::ppq && marks[1].tick == 18 * lynx::ppq && marks[2].tick == 32 * lynx::ppq);
    CHECK (keyOf (marks[2])->root == 9);
    song.mode = lynx::PlayMode::pattern;
    song.currentPattern = 0;
    CHECK (songKeyMarkers (song).size() == 2);
}

TEST (keys190nu_scale_degree_chord)
{
    using namespace keys190nu;
    const Degrees cIonian { 0, { 0, 2, 4, 5, 7, 9, 11 } };
    // esiowry's rule: C gives I (C major), D gives ii (D minor), E gives iii (E minor)…
    CHECK (chordOn (cIonian, 60) == (std::vector<int> { 60, 64, 67 }));
    CHECK (chordOn (cIonian, 62) == (std::vector<int> { 62, 65, 69 }));
    CHECK (chordOn (cIonian, 64) == (std::vector<int> { 64, 67, 71 }));
    CHECK (chordOn (cIonian, 71) == (std::vector<int> { 71, 74, 77 })); // vii°
    CHECK (chordOn (cIonian, 61) == (std::vector<int> { 60, 64, 67 })); // (C♯: the nearest degree, down on a tie)
    // 7th, 9th, 6th
    CHECK (chordOn (cIonian, 62, DegreeSize::seventh) == (std::vector<int> { 62, 65, 69, 72 }));
    CHECK (chordOn (cIonian, 60, DegreeSize::ninth) == (std::vector<int> { 60, 64, 67, 71, 74 }));
    CHECK (chordOn (cIonian, 60, DegreeSize::sixth) == (std::vector<int> { 60, 64, 67, 69 }));
    // inversions move the lowest notes up an octave
    CHECK (chordOn (cIonian, 60, DegreeSize::triad, 1) == (std::vector<int> { 64, 67, 72 }));
    CHECK (chordOn (cIonian, 60, DegreeSize::triad, 2) == (std::vector<int> { 67, 72, 76 }));
    CHECK (chordOn (cIonian, 60, DegreeSize::triad, 3) == (std::vector<int> { 67, 72, 76 })); // (a triad has two)
    CHECK (chordOn (cIonian, 62, DegreeSize::seventh, 3) == (std::vector<int> { 72, 74, 77, 81 }));
    // the borrowed chords of esiowry's second region: C natural minor gives i, ii°, ♭III…
    const Degrees cAeolian { 0, { 0, 2, 3, 5, 7, 8, 10 } };
    CHECK (chordOn (cAeolian, 60) == (std::vector<int> { 60, 63, 67 }));
    CHECK (chordOn (cAeolian, 68) == (std::vector<int> { 68, 72, 75 })); // ♭VI = A♭ major
    // numerals
    int root = 0;
    auto shape = diatonicChord (cIonian, 62, DegreeSize::triad, 0, root);
    std::vector<int> keys;
    for (const auto i : shape)
        keys.push_back (root + i);
    CHECK (degreeNumeral (cIonian, root, keys, DegreeSize::triad) == "ii");
    shape = diatonicChord (cIonian, 71, DegreeSize::triad, 0, root);
    keys.clear();
    for (const auto i : shape)
        keys.push_back (root + i);
    CHECK (degreeNumeral (cIonian, root, keys, DegreeSize::triad) == "vii\xc2\xb0");
    shape = diatonicChord (cIonian, 67, DegreeSize::seventh, 1, root);
    keys.clear();
    for (const auto i : shape)
        keys.push_back (root + i);
    CHECK (degreeNumeral (cIonian, root, keys, DegreeSize::seventh) == "V7");
    // flats for flat keys (C minor's A♭), sharps otherwise
    CHECK (prefersFlats (cAeolian) && ! prefersFlats (cIonian) && prefersFlats ({ 5, { 0, 2, 4, 5, 7, 9, 11 } }) && ! prefersFlats ({ 9, { 0, 2, 3, 5, 7, 8, 10 } }));
    // pentatonic (5 notes): still every other degree
    const Degrees pent { 0, { 0, 2, 4, 7, 9 } };
    CHECK (chordOn (pent, 60) == (std::vector<int> { 60, 64, 69 }));
    // no scale: nothing
    root = 0;
    CHECK (diatonicChord ({}, 60, DegreeSize::triad, 0, root).empty());
}

TEST (keys190nu_chord_helper_favourites)
{
    using namespace lynx::modalhelp;
    // a pinned relationship holds no key: C major's iv (Fm) and G major's iv (Cm) are the same favourite
    const auto inC = analyse ({ 60, 64, 67 });
    const auto inG = analyse ({ 67, 71, 74 });
    CHECK (inC.valid && inG.valid);
    const auto keysC = relatedKeys (inC), keysG = relatedKeys (inG);
    CHECK (! keysC.empty() && ! keysG.empty());
    CHECK (favouriteId (inC.key, keysC[0]) == favouriteId (inG.key, keysG[0])); // (the parallel minor)
    const auto chordsC = borrowedChords (inC, keysC[0]), chordsG = borrowedChords (inG, keysG[0]);
    CHECK (favouriteId (inC.key, chordsC[0]) == favouriteId (inG.key, chordsG[0]));
    CHECK (favouriteId (inC.key, chordsC[0]) == "C:maj:5:" + std::to_string ((int) Quality::minor)); // (iv)
    // pinned first keeps the ranking among the pinned and among the others
    std::vector<int> list { 1, 2, 3, 4, 5, 6 };
    CHECK (pinnedFirst (list, [] (int v) { return v == 5 || v == 2; }) == 2);
    CHECK (list == (std::vector<int> { 2, 5, 1, 3, 4, 6 }));
    // saved as one line
    const std::vector<std::string> ids { "C:maj:5:1", "K:maj:0:5" };
    CHECK (favouritesText (ids) == "C:maj:5:1;K:maj:0:5");
    CHECK (favouritesFrom ("C:maj:5:1;K:maj:0:5;;C:maj:5:1") == ids);
    CHECK (favouritesFrom ("").empty());
}

TEST (rollup031s3_fl_scale_index_order)
{
    // rollup 0.1.31 step 3 (round 190ny): FL 2026's saves with "Other Blues", "Other Blues Nonatonic", "Other Locrian",
    // "Other Locrian Super", "Other Phrygian", "Other Phrygian Dominant" key markers: FL's 0xA8 index follows its menu (Blues
    // 17, Blues Nonatonic 18, Locrian 27, Locrian Super 28, Phrygian 32, Phrygian Dominant 33); LYNX's list has those pairs
    // the other way, so the .flp import / export convert (notetools::flMenuScaleIndex), .lynx keeps LYNX's index
    using namespace keys190nu;
    const auto& list = lynx::notetools::flReferenceScales();
    const std::pair<int, const char*> fl[] = { { 17, "Other Blues" }, { 18, "Other Blues Nonatonic" }, { 27, "Other Locrian" },
                                               { 28, "Other Locrian Super" }, { 32, "Other Phrygian" }, { 33, "Other Phrygian Dominant" },
                                               { 7, "Minor Harmonic" }, { 4, "Major Pentatonic" }, { 20, "Other Dorian" } };
    for (const auto& [index, name] : fl)
        CHECK (std::string (list[(std::size_t) lynx::notetools::flMenuScaleIndex (index)].name) == name);
    for (int i = -3; i < (int) list.size(); ++i)
        CHECK (lynx::notetools::flMenuScaleIndex (lynx::notetools::flMenuScaleIndex (i)) == i);

    // export: each marker and the project's scale with FL's index, and back to the same key
    auto song = lynx::Song::makeDefault();
    auto& p = song.patterns[(std::size_t) song.currentPattern];
    for (std::size_t k = 0; k < 6; ++k)
        addKeyMarker (p, (std::int64_t) k * 4 * lynx::ppq, key (0, fl[k].second));
    p.notes.push_back ([] { lynx::Note n; n.key = 60; n.length = 96; return n; }());
    song.scaleHighlight = { flScaleNamed ("Other Blues"), 0 };
    lynx::flpexport::Report report;
    const auto project = flp::parseProject (lynx::flpexport::exportFlp (song, {}, report));
    CHECK (project.scaleHighlight == 17);
    std::vector<int> written;
    for (const auto& fp : project.patterns)
        for (const auto& m : fp.markers)
            if (m.type == lynx::Marker::keyScale)
                written.push_back (m.keyScale);
    CHECK (written == (std::vector<int> { 17, 18, 27, 28, 32, 33 }));
    const auto back = lynx::importFlp (project);
    CHECK (back.scaleHighlight.scale == flScaleNamed ("Other Blues"));
    std::vector<std::string> names;
    for (const auto& bp : back.patterns)
        for (const auto& m : bp.markers)
            if (isKeyMarker (m) && keyOf (m))
                names.push_back (list[(std::size_t) keyOf (m)->flScale].name);
    CHECK (names == (std::vector<std::string> { "Other Blues", "Other Blues Nonatonic", "Other Locrian", "Other Locrian Super", "Other Phrygian", "Other Phrygian Dominant" }));

    // FL's own saves (this machine): the index alone (converted) names the key its mask shows
    for (const auto* path : { "/mnt/e/lynx-work/rollup031/s3/fl/fl-save1-blues-locrian-phrygian-nonatonic.flp",
                              "/mnt/e/lynx-work/rollup031/s3/fl/fl-save2-locriansuper-phrygiandominant.flp" })
        if (std::ifstream in (path, std::ios::binary); in)
        {
            const std::vector<std::uint8_t> data ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
            const auto parsed = flp::parseProject (data);
            int keys = 0;
            for (const auto& fp : parsed.patterns)
                for (const auto& m : fp.markers)
                    if (m.type == lynx::Marker::keyScale && m.keyScale >= 0)
                    {
                        ++keys;
                        const lynx::projectkey::Key byIndex { m.keyRoot, lynx::notetools::flMenuScaleIndex (m.keyScale) };
                        CHECK (flOutOfScaleMask (byIndex) == m.keyMask);
                    }
            CHECK (keys == 4);
        }
}
