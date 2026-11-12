// --- Round 190ef: scene work in the clip grid (core/SessionOps.h): the reference's Duplicate on a scene, Capture and
// Insert Scene, the scene tempo / time signature fields, Capture MIDI into a slot, a Playlist clip into a slot.
// docs/analise/REA_rodada190ef.md.

#include "core/ClipLauncher.h"
#include "core/SessionOps.h"

namespace live190ef_scene
{
using namespace lynx;

inline Clip patternClip (int pattern, std::int64_t length = 4 * ppq)
{
    Clip c;
    c.pattern = pattern;
    c.length = length;
    return c;
}

// Three tracks, three patterns. Row 0: Drums, Bass. Row 1 "Verse": Drums (named, legato, 1/4), Keys; Bass has its stop
// button removed. Row 2: Bass, Keys. Row 1 has a colour, a tempo and a bar length.
inline Song song()
{
    Song s;
    s.patterns.resize (3);
    for (int p = 0; p < 3; ++p)
        s.patterns[(std::size_t) p].name = "P" + std::to_string (p);
    s.playlistTracks.resize (3);
    launcher::setClip (s, 0, 0, patternClip (0));
    launcher::setClip (s, 1, 0, patternClip (1));
    launcher::setClip (s, 0, 1, patternClip (0, 8 * ppq));
    launcher::setClip (s, 2, 1, patternClip (2));
    launcher::setClip (s, 1, 2, patternClip (1));
    launcher::setClip (s, 2, 2, patternClip (2));
    auto& slot = *launcher::settingsAt (*s.clipGrid, 0, 1);
    slot.name = "Fill";
    slot.legato = true;
    slot.quant = LaunchQuant::quarter;
    launcher::setStopButton (s, 1, 1, false);
    auto& grid = *s.clipGrid;
    grid.scenes.resize (3);
    grid.scenes[1].name = "Verse";
    grid.scenes[1].colour = 0xFF3B8BE0u;
    grid.scenes[1].tempo = 128.0;
    grid.scenes[1].beatsPerBar = 3;
    return s;
}

inline int clipsIn (const Song& s, int row)
{
    int n = 0;
    for (int t = 0; t < 3; ++t)
        n += launcher::clipAt (*s.clipGrid, t, row) != nullptr ? 1 : 0;
    return n;
}
} // namespace live190ef_scene

TEST (live190ef_scene_duplicate_copies_the_row_and_its_scene_below)
{
    using namespace lynx;
    auto s = live190ef_scene::song();
    const auto original = s;
    const auto at = session::duplicateScene (s, 1);
    CHECK (at == 2);
    CHECK (s.clipGrid->scenes.size() == 4);
    // the copy: same clips, same settings, same scene info
    CHECK (live190ef_scene::clipsIn (s, 2) == 2);
    const auto* drums = launcher::clipAt (*s.clipGrid, 0, 2);
    CHECK (drums != nullptr && drums->pattern == 0 && drums->length == 8 * ppq);
    const auto* settings = launcher::settingsAt (*s.clipGrid, 0, 2);
    CHECK (settings != nullptr && settings->name == "Fill" && settings->legato && settings->quant == LaunchQuant::quarter && settings->scene == 2);
    CHECK (! launcher::hasStopButton (*s.clipGrid, 1, 2)); // the removed stop button too
    CHECK (s.clipGrid->scenes[2].name == "Verse" && s.clipGrid->scenes[2].colour == 0xFF3B8BE0u && s.clipGrid->scenes[2].tempo == 128.0
           && s.clipGrid->scenes[2].beatsPerBar == 3);
    // the original row is untouched, the row that was below moved down one
    CHECK (live190ef_scene::clipsIn (s, 1) == 2 && launcher::settingsAt (*s.clipGrid, 0, 1)->name == "Fill");
    CHECK (live190ef_scene::clipsIn (s, 3) == 2 && launcher::clipAt (*s.clipGrid, 1, 3) != nullptr);
    // deleting the new row gives the grid back as it was (what an undo restores)
    CHECK (launcher::removeScene (s, 2));
    CHECK (live190ef_scene::clipsIn (s, 1) == 2 && live190ef_scene::clipsIn (s, 2) == 2 && s.clipGrid->scenes.size() == original.clipGrid->scenes.size());
    CHECK (s.clipGrid->scenes == original.clipGrid->scenes);
    CHECK (launcher::clipAt (*s.clipGrid, 1, 2) != nullptr && launcher::clipAt (*s.clipGrid, 0, 2) == nullptr);
}

TEST (live190ef_scene_duplicate_edges)
{
    using namespace lynx;
    auto s = live190ef_scene::song();
    // an empty row shown under the last scene: it becomes a scene and an empty copy follows
    const auto at = session::duplicateScene (s, 5);
    CHECK (at == 6 && s.clipGrid->scenes.size() == 7 && live190ef_scene::clipsIn (s, 6) == 0);
    CHECK (session::duplicateScene (s, -1) == -1);
    CHECK (session::duplicateScene (s, launcher::maxScenes - 1) == -1);
    // a project without a grid gets one
    Song empty;
    CHECK (session::duplicateScene (empty, 0) == 1 && empty.clipGrid && empty.clipGrid->scenes.size() == 2);
}

TEST (live190ef_scene_capture_copies_the_playing_clips_into_a_new_row)
{
    using namespace lynx;
    auto s = live190ef_scene::song();
    // Drums plays row 1 (the 8-beat "Fill"), Bass nothing, Keys row 2
    const auto at = session::captureScene (s, 0, { 1, -1, 2 });
    CHECK (at == 1);
    CHECK (s.clipGrid->scenes.size() == 4 && s.clipGrid->scenes[1] == ClipGridScene {}); // a new, plain scene
    CHECK (live190ef_scene::clipsIn (s, 1) == 2);
    const auto* drums = launcher::clipAt (*s.clipGrid, 0, 1);
    CHECK (drums != nullptr && drums->length == 8 * ppq && launcher::settingsAt (*s.clipGrid, 0, 1)->name == "Fill");
    CHECK (launcher::clipAt (*s.clipGrid, 1, 1) == nullptr); // Bass played nothing
    const auto* keys = launcher::clipAt (*s.clipGrid, 2, 1);
    CHECK (keys != nullptr && keys->pattern == 2);
    // the rows from the new one on moved down: Verse is row 2, the old row 2 is row 3
    CHECK (s.clipGrid->scenes[2].name == "Verse" && live190ef_scene::clipsIn (s, 2) == 2 && live190ef_scene::clipsIn (s, 3) == 2);
    CHECK (launcher::settingsAt (*s.clipGrid, 0, 2)->name == "Fill");
}

TEST (live190ef_scene_capture_needs_a_playing_clip)
{
    using namespace lynx;
    auto s = live190ef_scene::song();
    const auto before = s.clipGrid->scenes.size();
    CHECK (session::captureScene (s, 0, {}) == -1);
    CHECK (session::captureScene (s, 0, { -1, -1, -1 }) == -1);
    CHECK (session::captureScene (s, 0, { -1, 1, -1 }) == -1); // Bass "plays" a row where it has no clip
    CHECK (s.clipGrid->scenes.size() == before);
    Song none;
    CHECK (session::captureScene (none, 0, { 0 }) == -1);
    // under the last scene
    CHECK (session::captureScene (s, 2, { 0, 0, -1 }) == 3 && live190ef_scene::clipsIn (s, 3) == 2);
}

TEST (live190ef_scene_tempo_and_time_signature_fields)
{
    using namespace lynx;
    auto s = live190ef_scene::song();
    CHECK (session::setSceneTempo (s, 0, 92.5) && s.clipGrid->scenes[0].tempo == 92.5);
    CHECK (session::setSceneTempo (s, 0, 2000.0) && s.clipGrid->scenes[0].tempo == maxTempo);
    CHECK (session::setSceneTempo (s, 0, 1.0) && s.clipGrid->scenes[0].tempo == minTempo);
    CHECK (session::setSceneTempo (s, 0, 0.0) && s.clipGrid->scenes[0].tempo == 0.0); // Disable Scene Tempo
    CHECK (session::setSceneTempo (s, 0, std::nan ("")) && s.clipGrid->scenes[0].tempo == 0.0);
    CHECK (session::setSceneBeats (s, 0, 7) && s.clipGrid->scenes[0].beatsPerBar == 7);
    CHECK (session::setSceneBeats (s, 0, 99) && s.clipGrid->scenes[0].beatsPerBar == session::sceneBeatsMax);
    CHECK (session::setSceneBeats (s, 0, 0) && s.clipGrid->scenes[0].beatsPerBar == 0); // Disable Scene Time Signature
    CHECK (! session::setSceneTempo (s, -1, 120.0) && ! session::setSceneBeats (s, launcher::maxScenes, 4));
    // a row shown under the last scene becomes one
    CHECK (session::setSceneTempo (s, 5, 140.0) && s.clipGrid->scenes.size() == 6 && s.clipGrid->scenes[5].tempo == 140.0);
    CHECK (session::sceneTempoText (120.0) == "120.00" && session::sceneTempoText (92.456) == "92.46");
    CHECK (session::sceneSignatureText (3) == "3/4" && session::sceneSignatureText (0) == "1/4");
}

TEST (live190ef_scene_capture_into_a_slot_picks_the_slot)
{
    using namespace lynx;
    auto s = live190ef_scene::song();
    CHECK (session::captureSlot (s, 1, 1) == 1);  // Bass row 1 is empty (no clip; its missing stop button is no clip)
    CHECK (session::captureSlot (s, 0, 0) == 2);  // Drums rows 0 and 1 are taken: the first empty one below
    CHECK (session::captureSlot (s, 2, 1) == 3);  // Keys rows 1 and 2 are taken: past the last scene
    CHECK (session::captureSlot (s, 2, -4) == 0);
    Song none;
    CHECK (session::captureSlot (none, 0, 3) == 3);
    // the clip is placed there, named as given, in the track's colour
    s.playlistTracks[2].colour = 0xFF46C27Au;
    const auto row = session::captureSlot (s, 2, 1);
    std::vector<Note> notes (1);
    CHECK (session::placeRecordedPattern (s, 2, row, notes, 4 * ppq, "Keys 1") >= 0);
    CHECK (launcher::clipAt (*s.clipGrid, 2, 3) != nullptr && s.patterns.back().name == "Keys 1"
           && launcher::settingsAt (*s.clipGrid, 2, 3)->colour == 0xFF46C27Au);
}

TEST (live190ef_scene_captured_phrase_as_clip_notes)
{
    using namespace lynx;
    const auto bar = 4 * ppq;
    // played while the transport ran: the notes keep their place in the bar of the first one
    std::vector<scorelog::Entry> played (2);
    played[0].startTick = 2.0 * bar + ppq;
    played[0].startMs = 1000.0;
    played[0].endMs = 1250.0; // a half beat at 120 BPM
    played[0].key = 60;
    played[1].startTick = 3.0 * bar + 2 * ppq;
    played[1].startMs = 3000.0;
    played[1].endMs = 3500.0;
    played[1].key = 64;
    std::int64_t length = 0;
    auto notes = session::phraseNotes (played, 4000.0, 120.0, 4, length);
    CHECK (notes.size() == 2 && notes[0].start == ppq && notes[0].length == ppq / 2 && notes[0].key == 60);
    CHECK (notes[1].start == bar + 2 * ppq && notes[1].length == ppq);
    CHECK (length == 2 * bar);
    // played while stopped: the first note starts the clip, the timing kept; a note still held ends now
    std::vector<scorelog::Entry> stopped (2);
    stopped[0].startMs = 1000.0;
    stopped[0].endMs = 1500.0;
    stopped[1].startMs = 1500.0; // held
    stopped[1].key = 67;
    stopped[1].velocity = 0.5f;
    notes = session::phraseNotes (stopped, 2500.0, 120.0, 3, length);
    CHECK (notes.size() == 2 && notes[0].start == 0 && notes[0].length == ppq);
    CHECK (notes[1].start == ppq && notes[1].length == 2 * ppq && notes[1].velocity == 0.5f);
    CHECK (length == 3 * ppq); // one bar of 3/4
    // nothing: one empty bar
    notes = session::phraseNotes ({}, 0.0, 120.0, 4, length);
    CHECK (notes.empty() && length == bar);
}

TEST (live190ef_scene_playlist_clip_into_a_slot_is_a_copy)
{
    using namespace lynx;
    auto s = live190ef_scene::song();
    Clip c = live190ef_scene::patternClip (1, 2 * ppq);
    c.start = 5 * ppq;
    c.offset = ppq;
    c.track = 4;
    s.playlist.push_back (c);
    CHECK (session::fromPlaylist (s, 0, 1, 1, false));
    CHECK (s.playlist.size() == 1 && s.playlist[0].start == 5 * ppq); // the Playlist keeps it
    const auto* slot = launcher::clipAt (*s.clipGrid, 1, 1);
    CHECK (slot != nullptr && slot->pattern == 1 && slot->length == 2 * ppq && slot->offset == ppq && slot->start == 0 && slot->track == 1);
    CHECK (! session::fromPlaylist (s, 7, 1, 1, false)); // no such Playlist clip
}
