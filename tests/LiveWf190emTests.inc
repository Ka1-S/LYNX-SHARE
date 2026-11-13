// --- Round 190em: the clip-grid scene work, Capture MIDI and the map modes made to match Live 12.4.16 as the live
// check 190ei saw it (/mnt/e/lynx-work/live190ei/ANSWERS.md section C): Capture and Insert Scene (playing and stopped),
// the scene fields' drag rates, "n / d" signatures and Return to Default, Capture MIDI's tempo / into a playing clip,
// a key on a continuous control (tests/LiveWf190efMapTests.inc), the scene denominator through .als.

#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/ClipLauncher.h"
#include "core/SessionOps.h"

namespace live190em
{
using namespace lynx;

inline Clip patternClip (int pattern, std::int64_t length = 4 * ppq)
{
    Clip c;
    c.pattern = pattern;
    c.length = length;
    return c;
}

// Drums plays row 1 (slot named "Fill"), Keys plays row 0; row 1 is "Verse" (128 BPM, 3 / 4)
inline Song song()
{
    Song s;
    s.patterns.resize (3);
    s.playlistTracks.resize (3);
    launcher::setClip (s, 0, 0, patternClip (0));
    launcher::setClip (s, 0, 1, patternClip (1, 8 * ppq));
    launcher::setClip (s, 2, 0, patternClip (2));
    launcher::settingsAt (*s.clipGrid, 0, 1)->name = "Fill";
    s.clipGrid->scenes.resize (3);
    s.clipGrid->scenes[1].name = "Verse";
    s.clipGrid->scenes[1].tempo = 128.0;
    s.clipGrid->scenes[1].beatsPerBar = 3;
    return s;
}

inline scorelog::Entry played (double ms, int key, double lengthMs, double tick = -1.0)
{
    scorelog::Entry e;
    e.startMs = ms;
    e.endMs = ms + lengthMs;
    e.key = key;
    e.startTick = tick;
    return e;
}
} // namespace live190em

TEST (live190em_capture_scene_hands_playback_to_the_new_row)
{
    using namespace lynx;
    auto s = live190em::song();
    const auto drumsSlot = launcher::clipAt (*s.clipGrid, 0, 1)->gridSlot;
    const auto keysSlot = launcher::clipAt (*s.clipGrid, 2, 0)->gridSlot;
    std::vector<int> moved;
    // Verse selected, Drums plays row 1, Keys row 0
    const auto at = session::captureScene (s, 1, { 1, -1, 0 }, &moved);
    CHECK (at == 2);
    // a plain scene: no name, tempo and signature off (live check 190ei Q1b)
    CHECK (s.clipGrid->scenes[2] == ClipGridScene {});
    // the playing slots are now the new row's: the player follows its slot, so playback moves there with no restart
    CHECK (moved.size() == 2 && std::find (moved.begin(), moved.end(), (int) drumsSlot) != moved.end()
           && std::find (moved.begin(), moved.end(), (int) keysSlot) != moved.end());
    CHECK (launcher::clipAt (*s.clipGrid, 0, 2) != nullptr && launcher::clipAt (*s.clipGrid, 0, 2)->gridSlot == drumsSlot);
    CHECK (launcher::clipAt (*s.clipGrid, 2, 2) != nullptr && launcher::clipAt (*s.clipGrid, 2, 2)->gridSlot == keysSlot);
    // their copies sit in the old rows, the same clips with the same settings
    const auto* oldDrums = launcher::clipAt (*s.clipGrid, 0, 1);
    CHECK (oldDrums != nullptr && oldDrums->gridSlot != drumsSlot && oldDrums->length == 8 * ppq && oldDrums->pattern == 1);
    CHECK (launcher::settingsAt (*s.clipGrid, 0, 1)->name == "Fill" && launcher::settingsAt (*s.clipGrid, 0, 2)->name == "Fill");
    CHECK (launcher::clipAt (*s.clipGrid, 2, 0) != nullptr && launcher::clipAt (*s.clipGrid, 2, 0)->gridSlot != keysSlot);
    CHECK (launcher::clipAt (*s.clipGrid, 1, 2) == nullptr); // Bass played nothing
    // the scene that was below moved down
    CHECK (s.clipGrid->scenes.size() == 4 && s.clipGrid->scenes[1].name == "Verse");
}

TEST (live190em_capture_scene_stopped_copies_name_tempo_and_signature)
{
    using namespace lynx;
    auto s = live190em::song();
    s.clipGrid->scenes[1].colour = 0xFF3B8BE0u;
    s.clipGrid->scenes[1].beatUnit = 8;
    std::vector<int> moved { 7 };
    const auto at = session::captureScene (s, 1, { -1, -1, -1 }, &moved);
    CHECK (at == 2 && moved.empty());
    const auto& made = s.clipGrid->scenes[2];
    CHECK (made.name == "Verse" && made.tempo == 128.0 && made.beatsPerBar == 3 && made.beatUnit == 8 && made.colour == 0u);
    CHECK (launcher::clipAt (*s.clipGrid, 0, 2) == nullptr && launcher::clipAt (*s.clipGrid, 2, 2) == nullptr);
    // the grid full: nothing
    Song full;
    launcher::ensureGrid (full).scenes.resize ((std::size_t) launcher::maxScenes);
    CHECK (session::captureScene (full, 0, {}) == -1);
}

TEST (live190em_scene_signature_with_a_denominator)
{
    using namespace lynx;
    auto s = live190em::song();
    CHECK (session::setSceneSignature (s, 0, 6, 8) && s.clipGrid->scenes[0].beatsPerBar == 6 && s.clipGrid->scenes[0].beatUnit == 8);
    CHECK (session::setSceneSignature (s, 0, 7, 9) && s.clipGrid->scenes[0].beatUnit == 8); // the nearest of 1, 2, 4, 8, 16
    CHECK (session::setSceneSignature (s, 0, 5, 100) && s.clipGrid->scenes[0].beatUnit == 16);
    CHECK (session::setSceneSignature (s, 0, 5, 0) && s.clipGrid->scenes[0].beatUnit == 4);
    // Return to Default: the field off, the denominator back to 4
    CHECK (session::setSceneSignature (s, 0, 6, 8) && session::setSceneBeats (s, 0, 0));
    CHECK (s.clipGrid->scenes[0].beatsPerBar == 0 && s.clipGrid->scenes[0].beatUnit == 4);
    CHECK (session::setSceneTempo (s, 1, 0.0) && s.clipGrid->scenes[1].tempo == 0.0);
    CHECK (! session::setSceneSignature (s, -1, 4, 4));
    CHECK (session::sceneSignatureText (6, 8) == "6 / 8" && session::sceneSignatureText (4) == "4 / 4");
    CHECK (session::sceneBeatUnit (3) == 2 || session::sceneBeatUnit (3) == 4);
    CHECK (session::sceneBeatUnitStep (4, 1) == 8 && session::sceneBeatUnitStep (4, -2) == 1 && session::sceneBeatUnitStep (16, 3) == 16
           && session::sceneBeatUnitStep (1, -1) == 1);
    // a launch gives LYNX's n/4 meter the same bar length in quarter notes
    CHECK (session::sceneBarBeats (3, 4) == 3 && session::sceneBarBeats (6, 8) == 3 && session::sceneBarBeats (12, 8) == 6
           && session::sceneBarBeats (2, 2) == 4 && session::sceneBarBeats (1, 16) == 1 && session::sceneBarBeats (99, 1) == session::sceneBeatsMax);
}

TEST (live190em_scene_field_drag_rates_are_lives)
{
    using namespace lynx;
    // live check 190ei: from 120.00 a drag of 5 px gives +14, 10 px +29, 20 px +58, 40 px +112 (whole BPM)
    const std::pair<double, double> seen[] { { 5.0, 14.0 }, { 10.0, 29.0 }, { 20.0, 58.0 }, { 40.0, 112.0 } };
    for (const auto& [px, plus] : seen)
    {
        const auto bpm = session::sceneTempoDragged (120.0, px);
        CHECK (bpm == std::round (bpm));
        CHECK (std::abs ((bpm - 120.0) - plus) <= 2.0);
    }
    CHECK (session::sceneTempoDragged (120.0, -200.0) == minTempo && session::sceneTempoDragged (120.0, 1000.0) == maxTempo);
    CHECK (session::sceneTempoDragged (120.0, 0.0) == 120.0);
    // the numerator: 4 → 50 in 10 px (LYNX caps at 32 beats)
    CHECK (session::sceneBeatsDragged (4, 1.0) == 9 && session::sceneBeatsDragged (4, 10.0) == session::sceneBeatsMax);
    CHECK (session::sceneBeatsDragged (4, -10.0) == session::sceneBeatsMin);
    // the denominator: its own drag target, one step per 10 px
    CHECK (session::sceneUnitDragged (4, 9.0) == 4 && session::sceneUnitDragged (4, 10.0) == 8 && session::sceneUnitDragged (4, 25.0) == 16
           && session::sceneUnitDragged (4, -10.0) == 2 && session::sceneUnitDragged (4, -100.0) == 1);
}

TEST (live190em_scene_denominator_through_als)
{
    using namespace lynx;
    // (a song .als export takes: a channel and a MIDI clip with notes, as tests/Als181cTests.inc builds it)
    Song s;
    s.bpm = 120.0;
    ChannelDef keys;
    keys.name = "Keys";
    s.channels = { keys };
    Pattern p;
    p.name = "Riff";
    Note note;
    note.key = 60;
    note.length = ppq;
    p.notes = { note };
    p.lengthTicks = 4 * ppq;
    s.patterns = { p };
    CHECK (launcher::setClip (s, 0, 0, live190em::patternClip (0)) >= 0 && launcher::setClip (s, 0, 1, live190em::patternClip (0)) >= 0);
    session::setSceneSignature (s, 1, 6, 8);
    const auto out = exportAls (s);
    int n = 0, d = 0;
    CHECK (alsTimeSignature (alsTimeSignatureCode (6, 8), n, d) && n == 6 && d == 8);
    CHECK (out.xml.find ("<TimeSignatureId Value=\"" + std::to_string (alsTimeSignatureCode (6, 8)) + "\"") != std::string::npos);
    AlsImportOptions options;
    options.common.projectFolder = "E:/sets";
    options.common.fileExists = [] (const std::string&) { return true; };
    options.sessionOnPlaylist = false;
    std::string error;
    const auto back = importAls (out.bytes, options, &error);
    CHECK (back.has_value() && back->clipGrid && back->clipGrid->scenes.size() >= 2);
    if (! back)
        std::printf ("  (import: %s)\n", error.c_str());
    if (back && back->clipGrid && back->clipGrid->scenes.size() >= 2)
        CHECK (back->clipGrid->scenes[1].beatsPerBar == 6 && back->clipGrid->scenes[1].beatUnit == 8);
}

TEST (live190em_capture_midi_tempo_from_a_phrase_played_stopped)
{
    using namespace lynx;
    using live190em::played;
    // a s d f a s d f played evenly at 148 BPM (live check 190ei: Live set the song to 148.00)
    std::vector<scorelog::Entry> phrase;
    const auto beat = 60000.0 / 148.0;
    for (int i = 0; i < 8; ++i)
        phrase.push_back (played (1000.0 + i * beat + (i % 3 - 1) * 6.0, 60 + i % 4, beat * 0.5)); // (a human's ±6 ms)
    CHECK (std::abs (session::phraseTempo (phrase, 120.0) - 148.0) < 1.0);
    // the same at sixteenths: folded into [80, 160)
    std::vector<scorelog::Entry> fast;
    for (int i = 0; i < 16; ++i)
        fast.push_back (played (i * beat / 4.0, 60, 50.0));
    CHECK (std::abs (session::phraseTempo (fast, 120.0) - 148.0) < 0.5);
    // a slow phrase folds up; chords count once
    std::vector<scorelog::Entry> slow;
    for (int i = 0; i < 4; ++i)
    {
        slow.push_back (played (i * 1200.0, 60, 100.0));
        slow.push_back (played (i * 1200.0 + 10.0, 64, 100.0));
    }
    CHECK (std::abs (session::phraseTempo (slow, 120.0) - 100.0) < 0.5);
    // too little to tell: the fallback
    CHECK (session::phraseTempo ({ played (0.0, 60, 100.0), played (500.0, 62, 100.0) }, 133.0) == 133.0);
    CHECK (session::phraseTempo ({}, 133.0) == 133.0);
    // the notes at that tempo: one beat apart, two bars (as Live's 2-bar clip)
    std::int64_t length = 0;
    const auto notes = session::phraseNotes (phrase, 10000.0, session::phraseTempo (phrase, 120.0), 4, length);
    CHECK (notes.size() == 8 && length == 8 * ppq && notes[0].start == 0 && std::abs ((double) notes[4].start - 4.0 * ppq) < ppq / 8.0);
}

TEST (live190em_capture_midi_into_the_playing_clip)
{
    using namespace lynx;
    using live190em::played;
    // a 1-bar clip on track 0 (row 0), its pattern: a note on beat 1 and one on beat 3
    Song s;
    s.patterns.resize (1);
    Note a, b;
    a.start = 0;
    a.length = ppq / 2;
    a.key = 36;
    b.start = 2 * ppq;
    b.length = ppq / 2;
    b.key = 38;
    s.patterns[0].notes = { a, b };
    s.patterns[0].lengthTicks = 4 * ppq;
    s.playlistTracks.resize (2);
    launcher::setClip (s, 0, 0, live190em::patternClip (0));
    // played while the clip was in its third pass (clip time 2 bars + ...): f d s a, one per beat
    const auto bar = 4 * ppq;
    std::vector<scorelog::Entry> phrase;
    for (int i = 0; i < 4; ++i)
        phrase.push_back (played (i * 500.0, 65 - i, 250.0, 100.0 * bar + i * ppq)); // song ticks
    const auto shift = -98.0 * bar; // clock − song − anchor: song bar 100 is the clip's bar 2
    auto notes = session::clipTimeNotes (phrase, 5000.0, 120.0, shift);
    CHECK (notes.size() == 4 && notes[0].start == 2 * bar && notes[3].start == 2 * bar + 3 * ppq && notes[0].length == ppq / 2);
    session::setNotesChannel (notes, 3);
    CHECK (notes[0].channel == 3 && notes[3].channel == 3);
    CHECK (session::captureIntoClip (s, 0, 0, notes));
    const auto* clip = launcher::clipAt (*s.clipGrid, 0, 0);
    CHECK (clip != nullptr && clip->pattern == 0);
    // Start / loop moved to the bar just played (Live: Start 3.1.1, loop 1 bar); the loop unrolled before it
    CHECK (clip->offset == 2 * bar && clip->length == bar);
    const auto& p = s.patterns[0];
    CHECK (p.lengthTicks == 3 * bar);
    auto count = [&p] (int key) { return (int) std::count_if (p.notes.begin(), p.notes.end(), [key] (const Note& n) { return n.key == key; }); };
    CHECK (count (36) == 3 && count (38) == 3); // the old loop in bars 1, 2 and 3
    CHECK (count (65) == 1 && count (62) == 1);
    CHECK (std::any_of (p.notes.begin(), p.notes.end(), [&] (const Note& n) { return n.key == 62 && n.start == 2 * bar + 3 * ppq; }));
    // a pattern another clip plays too is copied first (the Playlist's clip keeps the old one)
    Song shared;
    shared.patterns.resize (1);
    shared.patterns[0].notes = { a };
    shared.playlistTracks.resize (1);
    launcher::setClip (shared, 0, 0, live190em::patternClip (0));
    Clip onPlaylist = live190em::patternClip (0);
    shared.playlist.push_back (onPlaylist);
    CHECK (session::captureIntoClip (shared, 0, 0, notes));
    CHECK (shared.patterns.size() == 2 && shared.patterns[0].notes.size() == 1 && launcher::clipAt (*shared.clipGrid, 0, 0)->pattern == 1);
    CHECK (shared.playlist[0].pattern == 0);
    // nothing there, or no notes: nothing
    CHECK (! session::captureIntoClip (s, 1, 0, notes));
    CHECK (! session::captureIntoClip (s, 0, 0, {}));
    // a phrase over two bars: a two-bar loop
    Song two;
    two.patterns.resize (1);
    two.playlistTracks.resize (1);
    launcher::setClip (two, 0, 0, live190em::patternClip (0));
    std::vector<Note> wide (2);
    wide[0].start = bar + 3 * ppq;
    wide[0].length = ppq;
    wide[1].start = 2 * bar + ppq;
    wide[1].length = ppq;
    CHECK (session::captureIntoClip (two, 0, 0, wide));
    CHECK (launcher::clipAt (*two.clipGrid, 0, 0)->offset == bar && launcher::clipAt (*two.clipGrid, 0, 0)->length == 2 * bar);
}
