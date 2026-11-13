// --- Round 190bv: the clip grid as a place to work — recording into slots (core/SessionOps.h, the launcher's record
// commands), clips between slots and the Playlist, and the groove pool (core/Groove.h). (The .lynx round trips of all
// three are in the app's --selftest: the project file needs JUCE.)

#include "core/Groove.h"
#include "core/SessionOps.h"

namespace session190bv
{
using namespace lynx;
using grid181a::bar;
using grid181a::closeTo;

inline RecordedEvent ev (double tick, int key, bool on, int channel = 0, float velocity = 0.8f)
{
    RecordedEvent e;
    e.tick = tick;
    e.key = key;
    e.on = on;
    e.channel = channel;
    e.velocity = velocity;
    return e;
}

// A song with a grid and nothing in it but one clip far away (so the launcher exists), three tracks.
inline Song emptyGridSong()
{
    auto s = grid181a::patternSong();
    launcher::setClip (s, 2, 5, grid181a::patternClip (3));
    return s;
}

inline std::vector<launcher::RecordedTake> takesOf (launcher::Runtime& runtime)
{
    std::vector<launcher::RecordedTake> out;
    launcher::RecordedTake t;
    while (runtime.takes.pop (t))
        out.push_back (t);
    return out;
}
} // namespace session190bv

// --- when a recording starts and ends ----------------------------------------------------------------------------

TEST (session190bv_record_start_and_end_fall_on_the_quantisation_grid)
{
    using namespace lynx;
    using session190bv::bar;
    // asked in the middle of bar 1 with 1-bar quantisation: starts at bar 2
    CHECK (session::recordStart (bar * 0.4, bar) == (double) bar);
    CHECK (session::recordStart ((double) bar, bar) == (double) bar);        // on the point: there
    CHECK (session::recordStart (bar + 1.0, bar) == 2.0 * bar);
    CHECK (session::recordStart (100.3, 0.0) == 101.0);                      // no quantisation: at once (a whole tick)
    CHECK (session::recordStart (10.0, ppq / 3.0) == 320.0);                 // 1/8T
    // ended in the middle of bar 4: the clip is cut at bar 5 (3 whole bars from bar 2)
    CHECK (session::recordEnd (bar, 3.6 * bar, bar) == 4.0 * bar);
    // ended before one step has passed: at least one step long
    CHECK (session::recordEnd (bar, bar * 0.9, bar) == 2.0 * bar);
    CHECK (session::recordEnd (bar, (double) bar, bar) == 2.0 * bar);
    // exactly on a later point: there
    CHECK (session::recordEnd (bar, 3.0 * bar, bar) == 3.0 * bar);
    // no quantisation: where it was asked
    CHECK (session::recordEnd (100.0, 250.2, 0.0) == 251.0);
    CHECK (session::recordEnd (100.0, 100.0, 0.0) == 101.0);
    // a fixed length ends it whenever the end is asked
    CHECK (session::recordEnd (bar, 1.2 * bar, bar, 4.0 * bar) == 5.0 * bar);
    CHECK (session::recordEnd (bar, 9.0 * bar, bar, 2.0 * bar) == 3.0 * bar);
}

TEST (session190bv_player_records_into_a_slot_on_the_grid_points)
{
    using namespace lynx;
    using namespace session190bv;
    grid181a::Run run (emptyGridSong()); // global quantisation: 1 bar
    launcher::setClip (run.song, 0, 0, grid181a::patternClip (0));
    run.index = launcher::makeIndex (*run.song.clipGrid);
    run.launch (0, 0);
    run.until (bar * 1.5);
    CHECK (run.runtime.player.playingOn (0) >= 0);

    // the record button of slot (0, 1), pressed in the middle of bar 2: it waits for bar 3
    run.runtime.commands.push ({ launcher::Command::Type::record, 0, 1 });
    run.until (bar * 1.75);
    CHECK (run.runtime.player.recordingOn (0) == 1);
    CHECK (run.runtime.tracks[0].recordPending.load() == 1);
    CHECK (run.runtime.tracks[0].recording.load() == -1);
    CHECK (run.runtime.tracks[0].playing.load() == 0); // what plays goes on until the start
    run.until (bar * 2.25);
    CHECK (run.runtime.tracks[0].recording.load() == 1);
    CHECK (run.runtime.tracks[0].recordPending.load() == -1);
    CHECK (run.runtime.tracks[0].recordFrom.load() == 2.0 * bar);
    CHECK (run.runtime.player.playingOn (0) < 0); // the clip that played ended where the recording began
    CHECK (run.runtime.tracks[0].taken.load());
    CHECK (takesOf (run.runtime).empty()); // its end is not known yet
    // the playing clip's key sounded at bar 0 and bar 1, and not at bar 2
    const auto ons = run.onsOf (60);
    CHECK (ons.size() == 2);

    // ended by the clip's launch button in the middle of bar 5: cut at bar 6, then loop playback
    run.until (bar * 4.5);
    run.runtime.commands.push ({ launcher::Command::Type::recordEnd, 0, 1 });
    run.until (bar * 4.75);
    auto told = takesOf (run.runtime);
    CHECK (told.size() == 1);
    if (told.size() == 1)
    {
        CHECK (told[0].track == 0 && told[0].scene == 1);
        CHECK (told[0].from == 2.0 * bar);
        CHECK (told[0].until == 5.0 * bar);
        CHECK (told[0].thenPlay);
    }
    CHECK (run.runtime.tracks[0].recording.load() == 1); // still recording until the point

    // the UI makes the clip and launches it at the recording's end: it plays from bar 6 with its position 0 there
    Note n;
    n.key = 72;
    n.start = ppq;
    n.length = ppq / 2;
    session::placeRecordedPattern (run.song, 0, 1, { n }, 3 * bar);
    run.index = launcher::makeIndex (*run.song.clipGrid);
    run.schedule = makeScheduleIndex (run.song);
    run.runtime.commands.push ({ launcher::Command::Type::launch, 0, 1, 5.0 * bar });
    run.until (bar * 9.0);
    CHECK (run.runtime.tracks[0].recording.load() == -1);
    CHECK (run.runtime.player.recordingOn (0) == -1);
    CHECK (run.runtime.tracks[0].playing.load() == 1);
    const auto played = run.onsOf (72);
    CHECK (played.size() == 2);
    if (played.size() == 2)
    {
        CHECK (closeTo (played[0], 5.0 * bar + ppq));
        CHECK (closeTo (played[1], 8.0 * bar + ppq)); // the clip is three bars long and loops
    }
}

TEST (session190bv_recorded_clip_that_arrives_late_joins_in_phase)
{
    using namespace lynx;
    using namespace session190bv;
    grid181a::Run run (emptyGridSong());
    run.runtime.commands.push ({ launcher::Command::Type::record, 0, 0 });
    run.until (bar * 0.5);
    CHECK (run.runtime.tracks[0].recording.load() == 0); // asked on the point: recording from tick 0
    run.runtime.commands.push ({ launcher::Command::Type::recordEnd, 0, 1 });
    run.until (bar * 0.6);
    const auto told = takesOf (run.runtime);
    CHECK (told.size() == 1 && told[0].from == 0.0 && told[0].until == (double) bar);

    // the launch is asked at once, the clip itself reaches the audio thread only half a beat after the recording's end
    run.runtime.commands.push ({ launcher::Command::Type::launch, 0, 0, (double) bar });
    run.until (bar + ppq / 2.0);
    CHECK (run.runtime.tracks[0].playing.load() == -1);
    std::vector<Note> notes;
    for (int beat = 0; beat < 4; ++beat)
    {
        Note n;
        n.key = 80 + beat;
        n.start = beat * ppq;
        n.length = ppq / 4;
        notes.push_back (n);
    }
    session::placeRecordedPattern (run.song, 0, 0, notes, bar);
    run.index = launcher::makeIndex (*run.song.clipGrid);
    run.schedule = makeScheduleIndex (run.song);
    run.until (bar * 2.75); // (short of the third pass: the last block may run a little past the asked tick)
    CHECK (run.runtime.tracks[0].playing.load() == 0);
    // beat 1 of the first pass was missed; beats 2..4 play where they belong, and the next pass is whole
    CHECK (run.onsOf (80).size() == 1);
    CHECK (run.onsOf (81).size() == 2);
    if (run.onsOf (81).size() == 2 && run.onsOf (80).size() == 1)
    {
        CHECK (closeTo (run.onsOf (81)[0], bar + ppq));
        CHECK (closeTo (run.onsOf (80)[0], 2.0 * bar));
    }
}

TEST (session190bv_fixed_length_recording_ends_by_itself_and_plays_on)
{
    using namespace lynx;
    using namespace session190bv;
    grid181a::Run run (emptyGridSong());
    run.until (bar * 0.3);
    launcher::Command c { launcher::Command::Type::record, 1, 0 };
    c.at = 2.0 * bar; // two bars
    run.runtime.commands.push (c);
    run.until (bar * 0.4);
    // the end is known from the start: the UI is told at once
    const auto told = takesOf (run.runtime);
    CHECK (told.size() == 1);
    if (told.size() == 1)
        CHECK (told[0].from == (double) bar && told[0].until == 3.0 * bar && told[0].thenPlay);
    run.until (bar * 2.5);
    CHECK (run.runtime.tracks[1].recording.load() == 0);
    // an end asked later than the fixed one changes nothing
    run.runtime.commands.push ({ launcher::Command::Type::recordEnd, 1, 0 });
    run.until (bar * 3.5);
    CHECK (takesOf (run.runtime).empty());
    CHECK (run.runtime.tracks[1].recording.load() == -1);
    CHECK (run.runtime.player.recordingOn (1) == -1);
}

TEST (session190bv_stop_and_launch_end_a_recording_without_loop_playback)
{
    using namespace lynx;
    using namespace session190bv;
    {
        // a stop button: the recording ends on the next point, and nothing plays on
        grid181a::Run run (emptyGridSong());
        run.runtime.commands.push ({ launcher::Command::Type::record, 0, 0 });
        run.until (bar * 1.5);
        run.command (launcher::Command::Type::stopTrack, 0);
        run.until (bar * 1.8); // (more than a block later, so the command is read)
        const auto told = takesOf (run.runtime);
        CHECK (told.size() == 1 && told[0].from == 0.0 && told[0].until == 2.0 * bar && ! told[0].thenPlay);
        run.until (bar * 2.5);
        CHECK (run.runtime.tracks[0].recording.load() == -1);
    }
    {
        // another clip launched on the recording track: the recording ends where that clip starts
        auto song = emptyGridSong();
        launcher::setClip (song, 0, 3, grid181a::patternClip (1));
        grid181a::Run run (std::move (song));
        run.runtime.commands.push ({ launcher::Command::Type::record, 0, 0 });
        run.until (bar * 2.2);
        run.launch (0, 3);
        run.until (bar * 2.3);
        const auto told = takesOf (run.runtime);
        CHECK (told.size() == 1 && told[0].until == 3.0 * bar && ! told[0].thenPlay);
        run.until (bar * 3.5);
        CHECK (run.runtime.tracks[0].recording.load() == -1);
        CHECK (run.runtime.tracks[0].playing.load() == 3);
    }
    {
        // ended before it began (the button pressed again while it waits): cancelled, nothing was recorded
        grid181a::Run run (emptyGridSong());
        run.until (bar * 0.2);
        run.runtime.commands.push ({ launcher::Command::Type::record, 0, 0 });
        run.until (bar * 0.5);
        CHECK (run.runtime.tracks[0].recordPending.load() == 0);
        run.runtime.commands.push ({ launcher::Command::Type::recordEnd, 0, 1 });
        run.until (bar * 0.6);
        const auto told = takesOf (run.runtime);
        CHECK (told.size() == 1 && told[0].until <= told[0].from);
        run.until (bar * 1.5);
        CHECK (run.runtime.tracks[0].recording.load() == -1 && run.runtime.tracks[0].recordPending.load() == -1);
        CHECK (! run.runtime.tracks[0].taken.load());
    }
    {
        // the transport stops: the recording ends where it stopped
        grid181a::Run run (emptyGridSong());
        run.runtime.commands.push ({ launcher::Command::Type::record, 0, 0 });
        run.until (bar * 1.3);
        run.runtime.player.stopAll (&run.runtime);
        const auto told = takesOf (run.runtime);
        CHECK (told.size() == 1 && told[0].from == 0.0 && told[0].until >= bar * 1.3 && told[0].until < bar * 1.3 + 512.0 && ! told[0].thenPlay);
        CHECK (run.runtime.tracks[0].recording.load() == -1);
    }
    {
        // a slot that has a clip cannot be recorded into
        auto song = emptyGridSong();
        launcher::setClip (song, 0, 0, grid181a::patternClip (1));
        grid181a::Run run (std::move (song));
        run.runtime.commands.push ({ launcher::Command::Type::record, 0, 0 });
        run.until (bar * 1.0);
        CHECK (run.runtime.player.recordingOn (0) == -1);
    }
}

// --- what a take holds ----------------------------------------------------------------------------------------------

TEST (session190bv_take_notes_are_placed_from_the_recording_start)
{
    using namespace lynx;
    using namespace session190bv;
    const double from = 2.0 * bar, until = 4.0 * bar;
    std::vector<RecordedEvent> events {
        ev (from - 500.0, 50, true),  ev (from - 400.0, 50, false),           // long before the start: not in the clip
        ev (from - 60.0, 60, true),   ev (from + 300.0, 60, false),           // a downbeat played a little early: on tick 0
        ev (from + ppq, 62, true, 0, 0.5f), ev (from + ppq + 240.0, 62, false),
        ev (until - 100.0, 64, true),                                         // still held at the end: cut there
        ev (until + 50.0, 65, true),  ev (until + 90.0, 65, false),           // after the end: not in the clip
        ev (until + 200.0, 64, false),
    };
    const auto notes = session::takeNotes (events, from, until);
    CHECK (notes.size() == 3);
    if (notes.size() == 3)
    {
        CHECK (notes[0].key == 60 && notes[0].start == 0 && notes[0].length == 300);
        CHECK (notes[1].key == 62 && notes[1].start == ppq && notes[1].length == 240 && notes[1].velocity == 0.5f);
        CHECK (notes[2].key == 64 && notes[2].start == 2 * bar - 100 && notes[2].length == 100);
    }
    // record quantisation: starts to the grid, lengths as played; a start rounded onto the end is the next pass's downbeat
    std::vector<RecordedEvent> loose { ev (from + 250.0, 60, true), ev (from + 400.0, 60, false),
                                       ev (until - 30.0, 61, true), ev (until + 100.0, 61, false) };
    const auto quantised = session::takeNotes (loose, from, until, ppq / 4);
    CHECK (quantised.size() == 2);
    if (quantised.size() == 2)
    {
        CHECK (quantised[0].key == 61 && quantised[0].start == 0);
        CHECK (quantised[1].key == 60 && quantised[1].start == 240 && quantised[1].length == 150);
    }
    // a key struck twice without a release in between: the first note ends at the second strike
    std::vector<RecordedEvent> twice { ev (from, 60, true), ev (from + 480.0, 60, true), ev (from + 600.0, 60, false) };
    const auto two = session::takeNotes (twice, from, until);
    CHECK (two.size() == 2 && two[0].length == 480 && two[1].start == 480 && two[1].length == 120);
    CHECK (session::takeNotes (events, from, from).empty());
}

TEST (session190bv_overdub_notes_land_where_the_loop_was)
{
    using namespace lynx;
    using namespace session190bv;
    const double anchor = 3.0 * bar; // the playing clip's position 0
    // a one-bar clip in its third pass: a note played 1 beat into that pass, one across the loop's end, one still held
    std::vector<RecordedEvent> events { ev (anchor + 2.0 * bar + ppq, 60, true),        ev (anchor + 2.0 * bar + ppq + 200.0, 60, false),
                                        ev (anchor + 3.0 * bar - 100.0, 62, true),      ev (anchor + 3.0 * bar + 300.0, 62, false),
                                        ev (anchor + 3.0 * bar + 500.0, 64, true),
                                        ev (anchor - 50.0, 70, true),                   ev (anchor - 10.0, 70, false) };
    const auto notes = session::overdubNotes (events, anchor, bar);
    CHECK (notes.size() == 2);
    if (notes.size() == 2)
    {
        CHECK (notes[0].key == 60 && notes[0].start == ppq && notes[0].length == 200);
        CHECK (notes[1].key == 62 && notes[1].start == bar - 100 && notes[1].length == 100); // cut at the loop's end
    }
    // merged into the pattern: added to what is there, sorted; the same note again replaces itself
    Pattern p;
    Note old;
    old.key = 36;
    old.start = 0;
    p.notes.push_back (old);
    session::mergeNotes (p, notes);
    CHECK (p.notes.size() == 3);
    CHECK (p.notes[0].key == 36 && p.notes[1].key == 60 && p.notes[2].key == 62);
    session::mergeNotes (p, notes);
    CHECK (p.notes.size() == 3);
}

TEST (session190bv_recorded_clips_are_named_and_coloured)
{
    using namespace lynx;
    using namespace session190bv;
    auto s = grid181a::patternSong();
    s.patterns[0].name = "Pattern 1";
    s.playlistTracks[1].colour = 0xFF2A6FDB;
    Note n;
    const auto pattern = session::placeRecordedPattern (s, 1, 2, { n }, 2 * bar);
    CHECK (pattern == 4);
    CHECK (s.patterns[4].name == "Pattern 2"); // the next free pattern name
    CHECK (s.patterns[4].lengthTicks == 2 * bar);
    const auto* clip = launcher::clipAt (*s.clipGrid, 1, 2);
    CHECK (clip != nullptr && clip->pattern == 4 && clip->length == 2 * bar && clip->kind == ClipKind::pattern);
    CHECK (launcher::settingsAt (*s.clipGrid, 1, 2)->colour == 0xFF2A6FDB); // the track's colour
    CHECK (launcher::settingsAt (*s.clipGrid, 1, 2)->loop);
    CHECK (s.clipGrid->scenes.size() == 3);

    // audio: a channel for the take's file on the armed insert, the clip named after the file
    const auto channels = s.channels.size();
    if (s.mixer.size() < 8)
        s.mixer.resize (8); // (the armed insert exists)
    const auto channel = session::placeRecordedAudio (s, 0, 0, "/takes/Song_Insert 3_2.wav", "Song_Insert 3_2", 3, bar, 1234, 0.25f);
    CHECK (channel == (int) channels);
    CHECK (s.channels[(std::size_t) channel].instrument == InstrumentKind::audioClip && s.channels[(std::size_t) channel].mixerTrack == 3);
    CHECK (s.channels[(std::size_t) channel].samplePath == "/takes/Song_Insert 3_2.wav");
    const auto* audio = launcher::clipAt (*s.clipGrid, 0, 0);
    CHECK (audio != nullptr && audio->kind == ClipKind::audio && audio->channel == channel && audio->offset == 1234 && audio->offsetFraction == 0.25f
           && audio->length == bar);
    CHECK (launcher::settingsAt (*s.clipGrid, 0, 0)->name == "Song_Insert 3_2");

    // a new, empty MIDI clip: one bar; not over a clip
    CHECK (session::insertEmptyClip (s, 0, 0) == -1);
    const auto empty = session::insertEmptyClip (s, 2, 0);
    CHECK (empty >= 0 && s.patterns[(std::size_t) empty].notes.empty() && launcher::clipAt (*s.clipGrid, 2, 0)->length == bar);
    // nothing the grid cannot hold
    const auto patterns = s.patterns.size();
    CHECK (session::placeRecordedPattern (s, launcher::maxTracks, 0, {}, bar) == -1);
    CHECK (session::placeRecordedPattern (s, 0, 1, {}, 0) == -1);
    CHECK (s.patterns.size() == patterns);
}

// --- clips between slots and the Playlist -------------------------------------------------------------------------

TEST (session190bv_copy_cut_paste_duplicate_and_drag_between_slots)
{
    using namespace lynx;
    using namespace session190bv;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 0, 0, grid181a::patternClip (1, 2 * bar));
    {
        auto& slot = *launcher::settingsAt (*s.clipGrid, 0, 0);
        slot.name = "Lead";
        slot.colour = 0xFF112233;
        slot.mode = LaunchMode::toggle;
        slot.quant = LaunchQuant::quarter;
        slot.legato = true;
        slot.follow.enabled = true;
        slot.follow.a = FollowKind::any;
    }
    launcher::setClip (s, 1, 1, grid181a::patternClip (2));
    s.clipGrid->clips[0].uid = 77;

    // copy → paste onto another track: the clip and everything it keeps, the original untouched; the target is replaced
    const auto copied = session::copyOf (s, 0, 0);
    CHECK (copied.has_value());
    CHECK (! session::copyOf (s, 2, 2).has_value());
    CHECK (session::paste (s, 1, 1, *copied));
    {
        const auto* c = launcher::clipAt (*s.clipGrid, 1, 1);
        const auto* st = launcher::settingsAt (*s.clipGrid, 1, 1);
        CHECK (c != nullptr && c->pattern == 1 && c->length == 2 * bar && c->track == 1 && c->uid == 0);
        CHECK (st != nullptr && st->name == "Lead" && st->colour == 0xFF112233 && st->mode == LaunchMode::toggle && st->quant == LaunchQuant::quarter
               && st->legato && st->follow.enabled && st->follow.a == FollowKind::any && st->scene == 1);
        CHECK (launcher::clipAt (*s.clipGrid, 0, 0) != nullptr);
        CHECK (s.clipGrid->clips.size() == 2);
    }
    // duplicate: a copy in the slot below (replacing what is there), with a new scene when it was the last row
    CHECK (session::duplicate (s, 0, 0) == 1);
    CHECK (launcher::clipAt (*s.clipGrid, 0, 1) != nullptr && launcher::settingsAt (*s.clipGrid, 0, 1)->name == "Lead");
    CHECK (session::duplicate (s, 0, 1) == 2);
    CHECK (s.clipGrid->scenes.size() == 3);
    CHECK (session::duplicate (s, 2, 2) == -1); // an empty slot
    // cut: gone from its slot, pasted elsewhere
    const auto cutClip = session::cut (s, 0, 2);
    CHECK (cutClip.has_value() && launcher::clipAt (*s.clipGrid, 0, 2) == nullptr);
    CHECK (session::paste (s, 2, 0, *cutClip));
    CHECK (launcher::clipAt (*s.clipGrid, 2, 0) != nullptr);
    // drag: the clip moves (it stays the same clip); with the copy modifier the original stays
    CHECK (session::moveClip (s, 0, 0, 2, 2));
    CHECK (launcher::clipAt (*s.clipGrid, 0, 0) == nullptr);
    CHECK (launcher::clipAt (*s.clipGrid, 2, 2) != nullptr && launcher::clipAt (*s.clipGrid, 2, 2)->uid == 77);
    CHECK (session::moveClip (s, 2, 2, 0, 0, true));
    CHECK (launcher::clipAt (*s.clipGrid, 2, 2) != nullptr && launcher::clipAt (*s.clipGrid, 0, 0) != nullptr);
    CHECK (launcher::clipAt (*s.clipGrid, 0, 0)->uid == 0);
    CHECK (! session::moveClip (s, 0, 0, 0, 0));  // onto itself
    CHECK (! session::moveClip (s, 1, 4, 0, 0));  // nothing to drag
    // onto a slot without a stop button: the clip takes the slot
    launcher::setStopButton (s, 1, 3, false);
    CHECK (session::moveClip (s, 0, 0, 1, 3));
    CHECK (launcher::clipAt (*s.clipGrid, 1, 3) != nullptr);
    // every clip still has a slot of its own
    std::vector<int> ids;
    for (const auto& c : s.clipGrid->clips)
        ids.push_back (c.gridSlot);
    std::sort (ids.begin(), ids.end());
    CHECK (std::adjacent_find (ids.begin(), ids.end()) == ids.end());
}

TEST (session190bv_clips_go_between_the_grid_and_the_playlist)
{
    using namespace lynx;
    using namespace session190bv;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 0, 0, grid181a::patternClip (1, 2 * bar));
    launcher::settingsAt (*s.clipGrid, 0, 0)->name = "Lead";
    // slot → Playlist: a copy on the track and tick it was dropped on, one pass long; the slot keeps its clip
    CHECK (session::toPlaylist (s, 0, 0, 2, 3 * bar));
    CHECK (s.playlist.size() == 1);
    CHECK (s.playlist[0].gridSlot == -1 && s.playlist[0].track == 2 && s.playlist[0].start == 3 * bar && s.playlist[0].length == 2 * bar
           && s.playlist[0].pattern == 1);
    CHECK (launcher::clipAt (*s.clipGrid, 0, 0) != nullptr);
    CHECK (! session::toPlaylist (s, 1, 1, 0, 0)); // an empty slot

    // Playlist → slot: the part of the pattern the clip plays (its offset and length), copied or moved
    Clip part = grid181a::patternClip (2, bar / 2);
    part.start = 5 * bar;
    part.offset = bar / 4;
    part.track = 1;
    s.playlist.push_back (part);
    CHECK (session::fromPlaylist (s, 1, 1, 0));
    {
        const auto* c = launcher::clipAt (*s.clipGrid, 1, 0);
        CHECK (c != nullptr && c->pattern == 2 && c->start == 0 && c->offset == bar / 4 && c->length == bar / 2 && c->gridSlot >= 0);
        CHECK (s.playlist.size() == 2);
    }
    CHECK (session::fromPlaylist (s, 1, 2, 2, true));
    CHECK (s.playlist.size() == 1 && launcher::clipAt (*s.clipGrid, 2, 2) != nullptr);
    // an automation clip is not a clip a slot can hold
    Clip automation;
    automation.kind = ClipKind::automation;
    automation.length = bar;
    s.playlist.push_back (automation);
    CHECK (! session::fromPlaylist (s, 1, 0, 3));
    CHECK (! session::fromPlaylist (s, 9, 0, 3));

    // from the Browser: a pattern, or an audio channel made for the dropped file
    CHECK (session::putPattern (s, 0, 4, 3));
    CHECK (launcher::clipAt (*s.clipGrid, 0, 4)->pattern == 3 && launcher::clipAt (*s.clipGrid, 0, 4)->length == bar);
    CHECK (! session::putPattern (s, 0, 4, 99));
    ChannelDef audio;
    audio.instrument = InstrumentKind::audioClip;
    s.channels.push_back (audio);
    CHECK (session::putAudio (s, 1, 4, (int) s.channels.size() - 1, 3 * bar));
    CHECK (launcher::clipAt (*s.clipGrid, 1, 4)->kind == ClipKind::audio && launcher::clipAt (*s.clipGrid, 1, 4)->length == 3 * bar);
    CHECK (! session::putAudio (s, 1, 4, 0, bar)); // not an audio channel
}

// --- the groove pool ----------------------------------------------------------------------------------------------------

namespace session190bv
{
// A one-bar 16th groove: every second 16th `late` ticks late and at `soft` velocity.
inline Groove offbeatGroove (int late, float soft = 0.5f)
{
    Groove g;
    g.name = "test";
    g.base = GrooveBase::sixteenth;
    g.length = bar;
    for (int i = 0; i < 16; ++i)
        g.points.push_back ({ i * 240 + (i % 2 == 1 ? late : 0), i % 2 == 1 ? soft : 1.0f });
    return g;
}
} // namespace session190bv

TEST (session190bv_groove_timing_moves_notes_by_the_template_offset)
{
    using namespace lynx;
    using namespace session190bv;
    auto g = offbeatGroove (60);
    // notes on the grid: the on-beat 16ths stay, the off-beat ones take the template's offset
    CHECK (groove::apply (g, 1.0f, 0, 0.8f, 0).start == 0);
    CHECK (groove::apply (g, 1.0f, 240, 0.8f, 0).start == 300);
    CHECK (groove::apply (g, 1.0f, 480, 0.8f, 0).start == 480);
    CHECK (groove::apply (g, 1.0f, 720, 0.8f, 0).start == 780);
    // the template repeats with its length
    CHECK (groove::apply (g, 1.0f, bar + 240, 0.8f, 0).start == bar + 300);
    CHECK (groove::apply (g, 1.0f, 5 * bar + 3 * 240, 0.8f, 0).start == 5 * bar + 3 * 240 + 60);
    // Timing scales the move; the pool's amount scales it again, up to 130 %
    g.timing = 0.5f;
    CHECK (groove::apply (g, 1.0f, 240, 0.8f, 0).start == 270);
    CHECK (groove::apply (g, 0.5f, 240, 0.8f, 0).start == 255);
    g.timing = 1.0f;
    CHECK (groove::apply (g, 1.3f, 240, 0.8f, 0).start == 318);
    CHECK (groove::apply (g, 2.0f, 240, 0.8f, 0).start == 318); // no more than 130 %
    CHECK (groove::apply (g, 0.0f, 240, 0.8f, 0).start == 240);
    // a note off the grid keeps its own distance from it: it moves by its slot's offset
    CHECK (groove::apply (g, 1.0f, 250, 0.8f, 0).start == 310);
    CHECK (groove::apply (g, 1.0f, 230, 0.8f, 0).start == 290);
    // an early template note pulls notes earlier; never before the clip's start
    const auto early = offbeatGroove (-40);
    CHECK (groove::apply (early, 1.0f, 240, 0.8f, 0).start == 200);
    Groove first;
    first.base = GrooveBase::quarter;
    first.length = bar;
    first.points = { { bar - 30, 1.0f } }; // the template's last note is the first beat, played early
    CHECK (groove::apply (first, 1.0f, 0, 0.8f, 0).start == 0);
    CHECK (groove::apply (first, 1.0f, bar, 0.8f, 0).start == bar - 30);
    // a Base slot the template has no note in is left alone
    Groove sparse;
    sparse.base = GrooveBase::sixteenth;
    sparse.length = bar;
    sparse.points = { { 0, 1.0f }, { 500, 1.0f } };
    CHECK (groove::apply (sparse, 1.0f, 240, 0.8f, 0).start == 240);
    CHECK (groove::apply (sparse, 1.0f, 480, 0.8f, 0).start == 500);
    CHECK (groove::apply (Groove {}, 1.0f, 333, 0.8f, 0).start == 333); // an empty template
}

TEST (session190bv_groove_quantize_comes_before_the_template_and_base_sets_the_grid)
{
    using namespace lynx;
    using namespace session190bv;
    auto g = offbeatGroove (60);
    g.timing = 0.0f;
    // Quantize 100 %: onto the Base; 50 %: half way; 0 %: untouched. The pool's amount does not scale it.
    g.quantize = 1.0f;
    CHECK (groove::apply (g, 1.0f, 250, 0.8f, 0).start == 240);
    CHECK (groove::apply (g, 1.0f, 130, 0.8f, 0).start == 240);
    CHECK (groove::apply (g, 1.0f, 110, 0.8f, 0).start == 0);
    CHECK (groove::apply (g, 0.0f, 250, 0.8f, 0).start == 240);
    g.quantize = 0.5f;
    CHECK (groove::apply (g, 1.0f, 260, 0.8f, 0).start == 250);
    g.quantize = 0.0f;
    CHECK (groove::apply (g, 1.0f, 260, 0.8f, 0).start == 260);
    // quantised first, then moved by the template: a loose off-beat note ends exactly on the template's place
    g.quantize = 1.0f;
    g.timing = 1.0f;
    CHECK (groove::apply (g, 1.0f, 262, 0.8f, 0).start == 300);
    // Base: the same note against a coarser grid
    Groove quarters;
    quarters.base = GrooveBase::quarter;
    quarters.length = bar;
    quarters.quantize = 1.0f;
    quarters.timing = 0.0f;
    CHECK (groove::apply (quarters, 1.0f, 250, 0.8f, 0).start == 0);
    CHECK (groove::apply (quarters, 1.0f, 490, 0.8f, 0).start == 960);
    quarters.base = GrooveBase::eighthTriplet;
    CHECK (groove::apply (quarters, 1.0f, 300, 0.8f, 0).start == 320);
    CHECK (groove::baseTicks (GrooveBase::quarter) == 960 && groove::baseTicks (GrooveBase::eighth) == 480 && groove::baseTicks (GrooveBase::eighthTriplet) == 320
           && groove::baseTicks (GrooveBase::sixteenth) == 240 && groove::baseTicks (GrooveBase::sixteenthTriplet) == 160
           && groove::baseTicks (GrooveBase::thirtySecond) == 120);
}

TEST (session190bv_groove_velocity_follows_the_accents_and_reverses_when_negative)
{
    using namespace lynx;
    using namespace session190bv;
    auto g = offbeatGroove (0, 0.5f);
    // Velocity 0: untouched
    CHECK (groove::apply (g, 1.0f, 240, 0.8f, 0).velocity == 0.8f);
    // +100 %: the template's quiet notes play quieter by the same ratio, its loudest unchanged
    g.velocity = 1.0f;
    CHECK (std::abs (groove::apply (g, 1.0f, 0, 0.8f, 0).velocity - 0.8f) < 1.0e-6f);
    CHECK (std::abs (groove::apply (g, 1.0f, 240, 0.8f, 0).velocity - 0.4f) < 1.0e-6f);
    // +50 %: half way there
    g.velocity = 0.5f;
    CHECK (std::abs (groove::apply (g, 1.0f, 240, 0.8f, 0).velocity - 0.6f) < 1.0e-6f);
    // −100 %: reversed — the template's loud places play quietly and the quiet ones at full
    g.velocity = -1.0f;
    CHECK (std::abs (groove::apply (g, 1.0f, 0, 0.8f, 0).velocity - 0.4f) < 1.0e-6f);
    CHECK (std::abs (groove::apply (g, 1.0f, 240, 0.8f, 0).velocity - 0.8f) < 1.0e-6f);
    // the pool's amount scales it; a velocity never leaves 1…127
    g.velocity = 1.0f;
    CHECK (std::abs (groove::apply (g, 0.5f, 240, 0.8f, 0).velocity - 0.6f) < 1.0e-6f);
    auto silent = offbeatGroove (0, 0.0f);
    silent.velocity = 1.0f;
    CHECK (groove::apply (silent, 1.0f, 240, 0.8f, 0).velocity >= 1.0f / 127.0f - 1.0e-7f);
}

TEST (session190bv_groove_random_is_bounded_repeatable_and_differs_per_note_and_pass)
{
    using namespace lynx;
    using namespace session190bv;
    auto g = offbeatGroove (0);
    g.timing = 0.0f;
    g.random = 1.0f;
    const auto reach = groove::maxShift (g, 1.0f);
    CHECK (reach >= 60 && reach <= 62); // a quarter of the Base each way (+ rounding)
    std::int64_t low = 0, high = 0;
    auto differ = false;
    for (std::size_t note = 0; note < 64; ++note)
        for (std::int64_t pass = 0; pass < 8; ++pass)
        {
            const auto bits = groove::randomBits (1234, note, pass * bar);
            CHECK (bits == groove::randomBits (1234, note, pass * bar)); // the same every time it is asked
            const auto moved = groove::apply (g, 1.0f, 480, 0.8f, bits).start - 480;
            CHECK (std::abs (moved) <= reach);
            low = std::min (low, moved);
            high = std::max (high, moved);
            differ = differ || bits != groove::randomBits (1234, note, (pass + 1) * bar) || bits != groove::randomBits (1234, note + 1, pass * bar);
        }
    CHECK (low < -30 && high > 30); // it uses its range, both ways
    CHECK (differ);
    // Random 0: no bits matter
    g.random = 0.0f;
    CHECK (groove::apply (g, 1.0f, 480, 0.8f, 0xFFFFFFFFu).start == 480);
    // maxShift bounds every part together
    auto all = offbeatGroove (100);
    all.quantize = 1.0f;
    all.random = 0.5f;
    const auto bound = groove::maxShift (all, 1.3f);
    for (std::int64_t t = 0; t < 2 * bar; t += 7)
        CHECK (std::abs (groove::apply (all, 1.3f, t, 0.8f, (std::uint32_t) (t * 2654435761u)).start - t) <= bound);
}

TEST (session190bv_groove_plays_through_a_grid_clip_without_changing_its_notes)
{
    using namespace lynx;
    using namespace session190bv;
    auto song = grid181a::patternSong (ppq / 8);
    // pattern 0: four 16ths from tick 0 (keys 60..63)
    song.patterns[0].notes.clear();
    for (int i = 0; i < 4; ++i)
    {
        Note n;
        n.key = 60 + i;
        n.start = i * 240;
        n.length = 100;
        n.velocity = 0.8f;
        song.patterns[0].notes.push_back (n);
    }
    launcher::setClip (song, 0, 0, grid181a::patternClip (0));
    launcher::setClip (song, 1, 0, grid181a::patternClip (0)); // the same pattern on another track, no groove
    auto g = offbeatGroove (60, 0.5f);
    g.velocity = 1.0f;
    const auto id = groove::add (song.grooves, g);
    CHECK (id == 1);
    CHECK (groove::setClipGroove (song, 0, 0, id));
    CHECK (! groove::setClipGroove (song, 0, 0, 99));
    CHECK (! groove::setClipGroove (song, 2, 2, id));
    CHECK (groove::users (song, id) == 1);
    const auto written = song.patterns[0].notes;

    const auto play = [&] (int track) {
        auto run = std::make_unique<grid181a::Run> (song);
        run->launch (track, 0);
        run->until (2.0 * bar);
        return run;
    };
    {
        auto run = play (0);
        const auto on61 = run->onsOf (61), on60 = run->onsOf (60), on63 = run->onsOf (63);
        // (two passes; the last block may reach the third pass's downbeat)
        CHECK (on60.size() >= 2 && on61.size() == 2 && on63.size() == 2);
        if (on60.size() >= 2 && on61.size() == 2 && on63.size() == 2)
        {
            CHECK (closeTo (on60[0], 0.0) && closeTo (on60[1], bar));
            CHECK (closeTo (on61[0], 300.0) && closeTo (on61[1], bar + 300.0)); // 60 ticks late, in both passes
            CHECK (closeTo (on63[0], 780.0));
        }
        for (const auto& e : run->events)
            if (e.e.type == NoteEvent::Type::on && (e.e.key == 61 || e.e.key == 63))
                CHECK (std::abs (e.e.velocity - 0.4f) < 1.0e-6f); // the accents
        for (const auto& e : run->events)
            if (e.e.type == NoteEvent::Type::on && (e.e.key == 60 || e.e.key == 62))
                CHECK (std::abs (e.e.velocity - 0.8f) < 1.0e-6f);
    }
    {
        // the same pattern in the clip without a groove: as written
        auto run = play (1);
        const auto on61 = run->onsOf (61);
        CHECK (on61.size() == 2);
        if (on61.size() == 2)
            CHECK (closeTo (on61[0], 240.0) && closeTo (on61[1], bar + 240.0));
        for (const auto& e : run->events)
            if (e.e.type == NoteEvent::Type::on)
                CHECK (e.e.velocity == 0.8f);
    }
    CHECK (song.patterns[0].notes == written);

    // the pool's amount at 0: the groove is there and does nothing
    song.grooves.amount = 0.0f;
    {
        auto run = play (0);
        const auto on61 = run->onsOf (61);
        CHECK (on61.size() == 2);
        if (on61.size() == 2)
            CHECK (closeTo (on61[0], 240.0));
    }
    song.grooves.amount = 1.0f;

    // one groove, many clips: editing it changes every clip that uses it
    CHECK (groove::setClipGroove (song, 1, 0, id));
    groove::find (song.grooves, id)->timing = 0.5f;
    for (int track = 0; track < 2; ++track)
    {
        auto run = play (track);
        const auto on61 = run->onsOf (61);
        CHECK (on61.size() == 2);
        if (on61.size() == 2)
            CHECK (closeTo (on61[0], 270.0));
    }
    // removed from the pool: the clips play straight again
    CHECK (groove::remove (song, id));
    CHECK (! groove::remove (song, id));
    CHECK (launcher::settingsAt (*song.clipGrid, 0, 0)->groove == 0 && launcher::settingsAt (*song.clipGrid, 1, 0)->groove == 0);
    CHECK (groove::ofClip (song, *launcher::clipAt (*song.clipGrid, 0, 0)) == nullptr);
}

TEST (session190bv_groove_commit_writes_the_notes_and_frees_the_clip)
{
    using namespace lynx;
    using namespace session190bv;
    auto song = grid181a::patternSong();
    song.patterns[0].notes.clear();
    for (int i = 0; i < 8; ++i)
    {
        Note n;
        n.key = 60 + i;
        n.start = i * 240;
        n.velocity = 0.8f;
        song.patterns[0].notes.push_back (n);
    }
    launcher::setClip (song, 0, 0, grid181a::patternClip (0));
    Clip arranged = grid181a::patternClip (0);
    arranged.start = 4 * bar;
    song.playlist.push_back (arranged); // the Playlist plays the same pattern
    auto g = offbeatGroove (60, 0.5f);
    g.velocity = 1.0f;
    const auto id = groove::add (song.grooves, g);
    CHECK (! groove::commit (song, 0, 0)); // no groove on the clip
    groove::setClipGroove (song, 0, 0, id);

    // what plays before the commit
    grid181a::Run before (song);
    before.launch (0, 0);
    before.until (bar);

    const auto patterns = song.patterns.size();
    const auto original = song.patterns[0].notes;
    CHECK (groove::commit (song, 0, 0));
    // the pattern was shared with the Playlist: the clip got its own copy, the Playlist's pattern is as it was
    CHECK (song.patterns.size() == patterns + 1);
    CHECK (song.patterns[0].notes == original);
    const auto* clip = launcher::clipAt (*song.clipGrid, 0, 0);
    CHECK (clip != nullptr && clip->pattern == (int) patterns);
    CHECK (launcher::settingsAt (*song.clipGrid, 0, 0)->groove == 0); // the chooser is back on "None"
    const auto& notes = song.patterns[patterns].notes;
    CHECK (notes.size() == 8);
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const auto odd = notes[i].key % 2 == 1;
        CHECK (notes[i].start == (notes[i].key - 60) * 240 + (odd ? 60 : 0));
        CHECK (std::abs (notes[i].velocity - (odd ? 0.4f : 0.8f)) < 1.0e-6f);
    }
    CHECK (std::is_sorted (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; }));

    // and it sounds as it did with the groove on
    grid181a::Run after (song);
    after.launch (0, 0);
    after.until (bar);
    for (int key = 60; key < 68; ++key)
    {
        const auto a = before.onsOf (key), b = after.onsOf (key);
        CHECK (! a.empty() && a.size() == b.size()); // (the last block may reach the next pass's downbeat)
        if (! a.empty() && a.size() == b.size())
            CHECK (closeTo (a[0], b[0], 0.5));
    }
    // a pattern only this clip uses is written in place
    CHECK (groove::setClipGroove (song, 0, 0, id));
    const auto count = song.patterns.size();
    CHECK (groove::commit (song, 0, 0));
    CHECK (song.patterns.size() == count);
}

TEST (session190bv_groove_bank_is_lynx_own_and_well_formed)
{
    using namespace lynx;
    const auto& bank = groove::factoryBank();
    CHECK (bank.size() >= 36 && bank.size() <= 48);
    std::vector<std::string> names, folders;
    for (const auto& e : bank)
    {
        const auto& g = e.groove;
        names.push_back (g.name);
        folders.push_back (e.folder);
        CHECK (! g.name.empty());
        CHECK (! g.points.empty());
        CHECK (g.length >= 4 * ppq && g.length % groove::baseTicks (g.base) == 0);
        CHECK (std::is_sorted (g.points.begin(), g.points.end(), [] (const GroovePoint& a, const GroovePoint& b) { return a.tick < b.tick; }));
        for (const auto& p : g.points)
            CHECK (p.tick >= 0 && p.tick < g.length && p.velocity > 0.0f && p.velocity <= 1.0f);
        // every template note belongs to exactly one Base slot: one point per slot
        CHECK ((std::int64_t) g.points.size() == g.length / groove::baseTicks (g.base));
        // a groove must do something to a straight 32nd run, and never more than its bound
        auto moves = false;
        for (std::int64_t t = 0; t < g.length; t += 120)
        {
            const auto played = groove::apply (g, 1.0f, t + 7, 0.8f, 0);
            moves = moves || played.start != t + 7 || played.velocity != 0.8f;
            CHECK (std::abs (played.start - (t + 7)) <= groove::maxShift (g, 1.0f));
        }
        CHECK (moves);
    }
    std::sort (names.begin(), names.end());
    CHECK (std::adjacent_find (names.begin(), names.end()) == names.end()); // no two alike
    for (const auto* folder : { "Swing", "Feel", "Accent", "Utility" })
        CHECK (std::count (folders.begin(), folders.end(), std::string (folder)) >= 6);

    // Swing 16ths 66: the second 16th of every pair two thirds of the way through the pair
    const auto it = std::find_if (bank.begin(), bank.end(), [] (const groove::BankEntry& e) { return e.groove.name == "Swing 16ths 66"; });
    CHECK (it != bank.end());
    if (it != bank.end())
    {
        CHECK (groove::apply (it->groove, 1.0f, 0, 0.8f, 0).start == 0);
        CHECK (groove::apply (it->groove, 1.0f, 240, 0.8f, 0).start == 317); // 480 × 0.66
        CHECK (groove::apply (it->groove, 1.0f, 480, 0.8f, 0).start == 480);
    }
    // the plain grids quantise and move nothing else
    const auto straight = std::find_if (bank.begin(), bank.end(), [] (const groove::BankEntry& e) { return e.groove.name == "Straight 1/8"; });
    CHECK (straight != bank.end());
    if (straight != bank.end())
        CHECK (groove::apply (straight->groove, 1.0f, 500, 0.8f, 0).start == 480 && groove::apply (straight->groove, 1.0f, 500, 0.8f, 0).velocity == 0.8f);
}

TEST (session190bv_groove_extract_and_user_file_import)
{
    using namespace lynx;
    using namespace session190bv;
    // extract: one point per occupied slot, the loudest note's place and velocity
    Pattern p;
    const auto add = [&] (std::int64_t start, float velocity) {
        Note n;
        n.start = start;
        n.velocity = velocity;
        p.notes.push_back (n);
    };
    add (0, 0.9f);
    add (10, 0.4f);   // the same slot, quieter: ignored
    add (300, 0.6f);  // the second 16th, 60 late
    add (720, 1.0f);
    add (bar - 20, 0.7f); // just before the end: the first slot of the next pass, 20 early
    const auto g = groove::extract (p, bar, GrooveBase::sixteenth, "Mine");
    CHECK (g.name == "Mine" && g.length == bar && g.base == GrooveBase::sixteenth);
    CHECK (g.points.size() == 3);
    CHECK (groove::apply (g, 1.0f, 240, 0.8f, 0).start == 300);
    CHECK (groove::apply (g, 1.0f, 480, 0.8f, 0).start == 480); // nothing extracted there
    Song s;
    const auto id = groove::add (s.grooves, g);
    CHECK (groove::find (s.grooves, id) != nullptr && groove::find (s.grooves, id)->name == "Mine");
    CHECK (groove::add (s.grooves, g) == id + 1);

    // a groove file of the user's (the test's own text in that format, not a file of another program)
    const std::string agr = R"(<?xml version="1.0" encoding="UTF-8"?>
<Ableton MajorVersion="5">
  <Groove>
    <Name Value="My &amp; groove" />
    <Clip><Value><MidiClip Id="0" Time="0">
      <Loop><LoopStart Value="0" /><LoopEnd Value="2" /></Loop>
      <Notes><KeyTracks>
        <KeyTrack Id="0"><Notes>
          <MidiNoteEvent Time="0" Duration="0.25" Velocity="127" IsEnabled="true" />
          <MidiNoteEvent Time="0.3125" Duration="0.25" Velocity="64" IsEnabled="true" />
          <MidiNoteEvent Time="1" Duration="0.25" Velocity="100" IsEnabled="true" />
          <MidiNoteEvent Time="1.5" Duration="0.25" Velocity="90" IsEnabled="false" />
          <MidiNoteEvent Time="2.5" Duration="0.25" Velocity="90" IsEnabled="true" />
        </Notes></KeyTrack>
        <KeyTrack Id="1"><Notes>
          <MidiNoteEvent Time="1.01" Duration="0.25" Velocity="20" IsEnabled="true" />
        </Notes></KeyTrack>
      </KeyTracks></Notes>
    </MidiClip></Value></Clip>
    <Grid Value="3" />
    <QuantizationAmount Value="25" />
    <TimingAmount Value="80" />
    <RandomAmount Value="10" />
    <VelocityAmount Value="-50" />
  </Groove>
</Ableton>)";
    Groove read;
    std::string error;
    CHECK (groove::parseAgr (agr, read, &error));
    CHECK (read.name == "My & groove");
    CHECK (read.base == GrooveBase::sixteenth && read.length == 2 * ppq);
    CHECK (std::abs (read.quantize - 0.25f) < 1.0e-6f && std::abs (read.timing - 0.8f) < 1.0e-6f && std::abs (read.random - 0.1f) < 1.0e-6f
           && std::abs (read.velocity + 0.5f) < 1.0e-6f);
    CHECK (read.points.size() == 3); // the disabled note, the one past the loop and the quieter one of a shared slot are out
    if (read.points.size() == 3)
    {
        CHECK (read.points[0].tick == 0 && read.points[0].velocity == 1.0f);
        CHECK (read.points[1].tick == 300 && std::abs (read.points[1].velocity - 64.0f / 127.0f) < 1.0e-6f);
        CHECK (read.points[2].tick == 960 && std::abs (read.points[2].velocity - 100.0f / 127.0f) < 1.0e-6f);
    }
    Groove none;
    CHECK (! groove::parseAgr ("not xml at all <", none, &error));
    CHECK (! groove::parseAgr ("<Ableton><LiveSet/></Ableton>", none, &error));
    CHECK (! groove::parseAgr ("<Ableton><Groove><Name Value=\"x\"/></Groove></Ableton>", none, &error) && ! error.empty());
}

TEST (session190bv_projects_without_grooves_or_slot_recording_play_as_before)
{
    using namespace lynx;
    using namespace session190bv;
    // a grid clip with no groove, in a project with an (unused) pool: the same events as with no pool at all
    auto song = grid181a::patternSong();
    launcher::setClip (song, 0, 0, grid181a::patternClip (0));
    launcher::setClip (song, 1, 0, grid181a::patternClip (1));
    grid181a::Run plain (song);
    plain.command (launcher::Command::Type::launchScene, 0, 0);
    plain.until (4.0 * bar);
    groove::add (song.grooves, offbeatGroove (60));
    grid181a::Run pooled (song);
    pooled.command (launcher::Command::Type::launchScene, 0, 0);
    pooled.until (4.0 * bar);
    CHECK (plain.events.size() == pooled.events.size());
    for (std::size_t i = 0; i < plain.events.size() && i < pooled.events.size(); ++i)
        CHECK (plain.events[i].tick == pooled.events[i].tick && plain.events[i].e.key == pooled.events[i].e.key
               && plain.events[i].e.velocity == pooled.events[i].e.velocity && plain.events[i].e.type == pooled.events[i].e.type);
    CHECK (! plain.events.empty());
    // the model's defaults: nothing a project saved before this round has
    CHECK (Song {}.grooves == GroovePool {});
    CHECK (ClipGridSlot {}.groove == 0);
    CHECK (GroovePool {}.amount == 1.0f);
    // a Playlist clip never has a groove
    Clip playlist = grid181a::patternClip (0);
    CHECK (groove::ofClip (song, playlist) == nullptr);
}

// --- Round 190cy: what the live check of Live 12.4.16 (190cw) showed ------------------------------------------------

TEST (session190cy_second_record_button_ends_the_take_into_playback_and_records_the_new_slot)
{
    using namespace lynx;
    using namespace session190bv;
    grid181a::Run run (emptyGridSong()); // global quantisation: 1 bar
    run.runtime.commands.push ({ launcher::Command::Type::record, 0, 0 });
    run.until (bar * 1.5);
    CHECK (run.runtime.tracks[0].recording.load() == 0);

    // the record button of slot (0, 2) in the middle of bar 2: slot 0 ends at bar 3 and plays on, slot 2 records from there
    run.runtime.commands.push ({ launcher::Command::Type::record, 0, 2 });
    run.until (bar * 1.75);
    auto told = takesOf (run.runtime);
    CHECK (told.size() == 1);
    if (told.size() == 1)
        CHECK (told[0].track == 0 && told[0].scene == 0 && told[0].from == 0.0 && told[0].until == 2.0 * bar && told[0].thenPlay);
    CHECK (run.runtime.tracks[0].recording.load() == 0);     // the first take records up to its end
    CHECK (run.runtime.tracks[0].recordPending.load() == 2); // the clicked slot waits for it
    CHECK (run.runtime.player.recordingOn (0) == 0);

    // the UI makes the first clip and launches it at its end (as for the clip's own launch button)
    Note n;
    n.key = 74;
    n.start = 0;
    n.length = ppq / 2;
    session::placeRecordedPattern (run.song, 0, 0, { n }, 2 * bar);
    run.index = launcher::makeIndex (*run.song.clipGrid);
    run.schedule = makeScheduleIndex (run.song);
    run.runtime.commands.push ({ launcher::Command::Type::launch, 0, 0, 2.0 * bar });
    run.until (bar * 2.25);
    CHECK (run.runtime.tracks[0].recording.load() == 2);
    CHECK (run.runtime.tracks[0].recordPending.load() == -1);
    CHECK (run.runtime.tracks[0].recordFrom.load() == 2.0 * bar);
    CHECK (run.runtime.player.recordingOn (0) == 2);
    CHECK (run.runtime.tracks[0].playing.load() == 0); // the first take plays while the second records
    CHECK (takesOf (run.runtime).empty());

    // the second take ends by its own launch button: cut on the grid, told as any take
    run.until (bar * 3.5);
    run.runtime.commands.push ({ launcher::Command::Type::recordEnd, 0, 1 });
    run.until (bar * 3.75);
    told = takesOf (run.runtime);
    CHECK (told.size() == 1);
    if (told.size() == 1)
        CHECK (told[0].scene == 2 && told[0].from == 2.0 * bar && told[0].until == 4.0 * bar && told[0].thenPlay);
    const auto ons = run.onsOf (74);
    CHECK (ons.size() == 1 && closeTo (ons[0], 2.0 * bar)); // the first take plays from its end, its position 0 there
}

TEST (session190cy_second_record_button_edge_cases)
{
    using namespace lynx;
    using namespace session190bv;
    {
        // pressed while the first recording still waits for its start: the wait moves to the new slot
        grid181a::Run run (emptyGridSong());
        run.until (bar * 0.2);
        run.runtime.commands.push ({ launcher::Command::Type::record, 1, 0 });
        run.until (bar * 0.4);
        run.runtime.commands.push ({ launcher::Command::Type::record, 1, 3 });
        run.until (bar * 0.6);
        const auto told = takesOf (run.runtime);
        CHECK (told.size() == 1 && told[0].scene == 0 && told[0].until <= told[0].from); // cancelled
        CHECK (run.runtime.tracks[1].recordPending.load() == 3);
        run.until (bar * 1.5);
        CHECK (run.runtime.tracks[1].recording.load() == 3 && run.runtime.tracks[1].recordFrom.load() == (double) bar);
    }
    {
        // a third slot while one is already queued: refused and told back as cancelled; a stop drops the queued slot
        grid181a::Run run (emptyGridSong());
        run.runtime.commands.push ({ launcher::Command::Type::record, 0, 0 });
        run.until (bar * 1.3);
        run.runtime.commands.push ({ launcher::Command::Type::record, 0, 1 });
        run.runtime.commands.push ({ launcher::Command::Type::record, 0, 2 });
        run.until (bar * 1.5);
        auto told = takesOf (run.runtime);
        CHECK (told.size() == 2);
        if (told.size() == 2)
        {
            CHECK (told[0].scene == 0 && told[0].until == 2.0 * bar && told[0].thenPlay);
            CHECK (told[1].scene == 2 && told[1].until <= told[1].from);
        }
        CHECK (run.runtime.tracks[0].recordPending.load() == 1);
        run.command (launcher::Command::Type::stopTrack, 0);
        run.until (bar * 1.7);
        told = takesOf (run.runtime);
        CHECK (told.size() == 2);
        if (told.size() == 2)
        {
            CHECK (told[0].scene == 1 && told[0].until <= told[0].from); // the queued slot is given up
            CHECK (told[1].scene == 0 && told[1].until == 2.0 * bar && ! told[1].thenPlay); // the take stops at its end
        }
        run.until (bar * 2.5);
        CHECK (run.runtime.tracks[0].recording.load() == -1 && run.runtime.tracks[0].recordPending.load() == -1);
        CHECK (run.runtime.player.recordingOn (0) == -1);
    }
    {
        // a fixed length carries over to the queued slot: it ends by itself and plays on
        grid181a::Run run (emptyGridSong());
        run.runtime.commands.push ({ launcher::Command::Type::record, 0, 0 });
        run.until (bar * 0.5);
        launcher::Command c { launcher::Command::Type::record, 0, 1 };
        c.at = 2.0 * bar;
        run.runtime.commands.push (c);
        run.until (bar * 1.25);
        auto told = takesOf (run.runtime);
        CHECK (told.size() == 2);
        if (told.size() == 2)
        {
            CHECK (told[0].scene == 0 && told[0].until == (double) bar && told[0].thenPlay);
            CHECK (told[1].scene == 1 && told[1].from == (double) bar && told[1].until == 3.0 * bar && told[1].thenPlay);
        }
        run.until (bar * 3.5);
        CHECK (run.runtime.player.recordingOn (0) == -1);
    }
}

TEST (session190cy_recorded_clips_are_named_after_the_track_and_the_counter_reads_bars_beats_sixteenths)
{
    using namespace lynx;
    using namespace session190bv;
    auto s = grid181a::patternSong();
    CHECK (session::recordedClipName (s, "rec MIDI") == "rec MIDI 1");
    const auto first = session::placeRecordedPattern (s, 1, 0, {}, bar, session::recordedClipName (s, "rec MIDI"));
    CHECK (first >= 0 && s.patterns[(std::size_t) first].name == "rec MIDI 1");
    CHECK (session::recordedClipName (s, "rec MIDI") == "rec MIDI 2");
    // a slot name counts as taken too (an audio clip, a renamed slot)
    const auto second = session::placeRecordedPattern (s, 1, 1, {}, bar, "rec MIDI 2");
    CHECK (second >= 0);
    if (auto* grid = s.clipGrid ? &*s.clipGrid : nullptr; grid != nullptr)
        if (const auto* clip = launcher::clipAt (*grid, 1, 1))
            grid->slots[(std::size_t) clip->gridSlot].name = "rec MIDI 3";
    CHECK (session::recordedClipName (s, "rec MIDI") == "rec MIDI 4");
    CHECK (session::recordedClipName (s, "Bass") == "Bass 1");
    // no name given: a new pattern's name, as before (the Pattern workflow's convention)
    const auto plain = session::placeRecordedPattern (s, 2, 0, {}, bar);
    CHECK (plain >= 0 && s.patterns[(std::size_t) plain].name.rfind ("Pattern ", 0) == 0);

    CHECK (session::lengthCounter (0.0, lynx::Meter (4, 4)) == "0.0.0");
    CHECK (session::lengthCounter ((double) ppq, lynx::Meter (4, 4)) == "0.1.0");
    CHECK (session::lengthCounter (8.0 * ppq + 2.0 * ppq / 4.0, lynx::Meter (4, 4)) == "2.0.2");
    CHECK (session::lengthCounter (4.0 * ppq + 3.0 * ppq + 3.0 * ppq / 4.0 + 100.0, lynx::Meter (4, 4)) == "1.3.3");
    CHECK (session::lengthCounter (3.0 * ppq, lynx::Meter (3, 4)) == "1.0.0"); // 3/4
    CHECK (session::lengthCounter (-5.0, lynx::Meter (4, 4)) == "0.0.0");
}
