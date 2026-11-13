// Round 190ev (issue #152, 0.1.26 on Windows 11: "flickering windows; selecting the window behind does not bring it over
// the one in front; the Channel rack almost never comes to the front"): the stacking rules of the windows docked in the
// main window (core/DockStack.h). The native side (child windows, the click hook) is checked by --plugwintest's dock mode.
#include "core/DockStack.h"

#include <set>
#include <string>

TEST (win190ev_a_click_in_a_plugin_view_belongs_to_its_docked_window)
{
    using lynx::dockstack::clickedWindow;
    const std::set<std::string> docked { "frame", "editor" };
    auto isDocked = [&docked] (const std::string& w) { return docked.count (w) > 0; };
    // the plugin's own view, two levels under the docked frame, under the main window
    CHECK (clickedWindow (std::vector<std::string> { "knob", "view", "frame", "main" }, isDocked) == 2);
    // a click on the docked window's own native window: index 0 (JUCE's own mouse path takes it)
    CHECK (clickedWindow (std::vector<std::string> { "editor", "main" }, isDocked) == 0);
    // a click on a panel or on the main window: no docked window
    CHECK (clickedWindow (std::vector<std::string> { "rack", "main" }, isDocked) == -1);
    CHECK (clickedWindow (std::vector<std::string> {}, isDocked) == -1);
}

TEST (win190ev_a_clicked_docked_window_rises_above_panels_but_not_above_overlays)
{
    using lynx::dockstack::needsRaise;
    // bottom to top: Playlist, Channel rack, a channel's settings, Mixer, a dancer that keeps in front, the wizard
    const std::vector<std::string> stack { "playlist", "rack", "settings", "mixer", "dancer", "wizard" };
    const std::set<std::string> inFront { "dancer", "wizard" };
    auto staysAbove = [&inFront] (const std::string& w) { return inFront.count (w) > 0; };
    CHECK (needsRaise (stack, std::string ("settings"), staysAbove)); // the Mixer covers it
    CHECK (needsRaise (stack, std::string ("rack"), staysAbove));
    CHECK (! needsRaise (stack, std::string ("mixer"), staysAbove)); // only the dancer and the wizard above it
    CHECK (! needsRaise (stack, std::string ("wizard"), staysAbove));
    CHECK (! needsRaise (stack, std::string ("gone"), staysAbove)); // not among the siblings
    // a hidden sibling above covers nothing
    const std::set<std::string> hiddenToo { "dancer", "wizard", "mixer" };
    CHECK (! needsRaise (stack, std::string ("settings"), [&hiddenToo] (const std::string& w) { return hiddenToo.count (w) > 0; }));
}

TEST (win190ev_the_desktop_list_is_reordered_only_when_it_differs)
{
    using lynx::dockstack::listMatches;
    // JUCE's list of desktop windows: the main window, then the docked children, then the owned windows
    std::vector<std::string> list { "main", "playlist", "rack", "plugin", "dialog" };
    auto indexOf = [&list] (const std::string& w) {
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i] == w)
                return (int) i;
        return -1;
    };
    const std::vector<std::string> screen { "playlist", "rack", "plugin", "dialog" };
    CHECK (listMatches (std::string ("main"), screen, indexOf));
    // the main window came to the front of JUCE's list (a click in it): the list must be put back
    list = { "playlist", "rack", "plugin", "dialog", "main" };
    CHECK (! listMatches (std::string ("main"), screen, indexOf));
    // two children swapped in the list against the screen
    list = { "main", "rack", "playlist", "plugin", "dialog" };
    CHECK (! listMatches (std::string ("main"), screen, indexOf));
    // a window missing from the list
    list = { "main", "playlist", "plugin", "dialog" };
    CHECK (! listMatches (std::string ("main"), screen, indexOf));
}

TEST (win190ev_docked_panels_do_not_fade_where_the_window_alpha_flashes)
{
    using lynx::dockstack::panelFades;
    // Windows: a docked panel shows and hides at once (the alpha fade flashed it twice); a plain panel still fades
    CHECK (! panelFades (true, true, true));
    CHECK (panelFades (true, false, true));
    // elsewhere round 190i's fade stays; Animations off: never
    CHECK (panelFades (true, true, false));
    CHECK (! panelFades (false, true, false) && ! panelFades (false, false, true));
}

// --- (2) "input 2 (mono) on insert 7: the take lands on insert 8 or 10; soloing 7 is silent, soloing 8 / 10 is heard" ---
#include "core/TakeRecording.h"

namespace
{
lynx::Song win190evMixer (int inserts)
{
    auto song = lynx::Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    song.playlistTracks.clear();
    song.mixer = lynx::Song::defaultMixer (inserts);
    return song;
}
} // namespace

TEST (win190ev_a_take_plays_where_fl_routes_it)
{
    using lynx::RecordSource;
    using lynx::takes::playbackInsert;
    auto song = win190evMixer (12);
    // FL (FLEngine FUN_012aa880): a Post EQ take of insert 7 (FL's default position; 7 goes to Master) plays through
    // Master: it holds 7's effects already. LYNX used to give it a new free insert (the reporter's 8 or 10).
    CHECK (playbackInsert (song, 7, RecordSource::postEq) == 0);
    for (const auto point : { RecordSource::externalAndMixer, RecordSource::postEffects, RecordSource::postLevel, RecordSource::postTrack })
        CHECK (playbackInsert (song, 7, point) == 0);
    // "External input only": back through insert 7 itself
    CHECK (playbackInsert (song, 7, RecordSource::externalInput) == 7);
    // insert 7 goes to buses 9 and 3, not to Master: the lowest-numbered of them; nowhere at all: Master
    song.mixer[7].toMaster = false;
    song.mixer[7].sends = { { 9, 1.0f }, { 3, 0.5f } };
    CHECK (playbackInsert (song, 7, RecordSource::postEq) == 3);
    song.mixer[7].sends.clear();
    CHECK (playbackInsert (song, 7, RecordSource::postEq) == 0);
    // a Playlist audio track linked to insert 7: the take plays through 7 whatever its position
    song.playlistTracks.resize (3);
    song.playlistTracks[2].mode = lynx::PlaylistTrackMode::audio;
    song.playlistTracks[2].mixerTrack = 7;
    CHECK (playbackInsert (song, 7, RecordSource::postEq) == 7);
    // no insert (Settings' "Record input audio" without armed inserts): a new one, as before
    CHECK (playbackInsert (song, 0, RecordSource::externalInput) == -1);
    CHECK (playbackInsert (song, 40, RecordSource::postEq) == -1);
}

TEST (win190ev_a_take_on_master_leaves_its_track_unlinked)
{
    using namespace lynx::takes;
    auto song = win190evMixer (12);
    const auto inserts = song.mixer.size();
    const std::vector<Part> parts { { 1920, 3840, 0, 0.0f } };
    // Post EQ take of insert 7: its clip's channel goes to Master, no insert is added, the track stays unassigned
    placeTakeClips (song, { { "/rec/Vox.wav", "Vox", 0, 7 } }, parts);
    CHECK (song.channels.size() == 1 && song.channels[0].mixerTrack == 0);
    CHECK (song.mixer.size() == inserts);
    CHECK (song.playlist.size() == 1);
    const auto track = song.playlist.empty() ? -1 : song.playlist[0].track;
    CHECK (track >= 0 && (track >= (int) song.playlistTracks.size() || song.playlistTracks[(std::size_t) track].mode == lynx::PlaylistTrackMode::unassigned));
    // External input only on insert 7: through 7, its track linked to 7 (the next take of 7 lands there)
    placeTakeClips (song, { { "/rec/Gtr.wav", "Gtr", 7, 7 } }, { { 7680, 3840, 0, 0.0f } });
    CHECK (song.channels.size() == 2 && song.channels[1].mixerTrack == 7);
    const auto gtrTrack = song.playlist.back().track;
    CHECK (gtrTrack >= 0 && gtrTrack < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) gtrTrack].mode == lynx::PlaylistTrackMode::audio
           && song.playlistTracks[(std::size_t) gtrTrack].mixerTrack == 7);
    // a file of no insert still gets a new one
    placeTakeClips (song, { { "/rec/In.wav", "In", -1, -1 } }, { { 15360, 3840, 0, 0.0f } });
    CHECK (song.channels.size() == 3 && song.channels[2].mixerTrack > 0 && song.channels[2].mixerTrack != 7);
}

TEST (win190ev_crash_journals_keep_master_and_read_old_files)
{
    using namespace lynx::takes;
    Journal j;
    j.wav = "C:/Music/LYNX/Recordings/Vox.wav";
    j.ticksPerSample = 0.04;
    for (const int through : { -1, 0, 7 })
    {
        j.insert = through;
        Journal k;
        CHECK (parseJournal (journalText (j), k) && k.insert == through);
    }
    // a journal of 0.1.26 (only "insert", 0 = a new insert)
    Journal old;
    CHECK (parseJournal ("lynxrec=1\nwav=C:/a.wav\nticksPerSample=0.04\ninsert=0\n", old) && old.insert == -1);
    CHECK (parseJournal ("lynxrec=1\nwav=C:/a.wav\nticksPerSample=0.04\ninsert=5\n", old) && old.insert == 5);
}

TEST (win190ev_soloing_the_recording_insert_keeps_its_input_and_take_audible)
{
    auto song = win190evMixer (12);
    song.mixer[7].solo = true;
    const auto audible = song.soloAudible();
    // the take's clip on Master (where FL puts it) and insert 7 itself are heard with 7 soloed; the new insert LYNX used
    // to give the clip (8) was not: "soloing 7 is silent, soloing 8 / 10 is heard"
    CHECK (audible[0] && audible[7]);
    CHECK (! audible[8] && ! audible[10]);

    // insert 7's input (input 2, mono), armed and recording, soloed: heard at the Master
    lynx::RackRenderer renderer;
    lynx::LiveInput input;
    std::vector<float> in1 (256, 0.0f), in2 (256, 0.5f), l (256), r (256);
    auto& x = song.mixer[7];
    x.inputChannel = 1;
    x.inputStereo = false;
    renderer.prepare (48000.0, 512);
    renderer.setLiveInput (&input);
    input.channels[0] = in1.data();
    input.channels[1] = in2.data();
    input.numChannels = 2;
    input.armed[7].store (true);
    input.recording.store (true);
    // (the peak of the last blocks: a solo change ramps the inserts' levels over the first ones)
    auto peak = [&] {
        auto state = lynx::RackState::build (song, nullptr, 48000.0);
        float p = 0.0f;
        for (int block = 0; block < 40; ++block)
        {
            renderer.process (state.get(), true, 0.0, 256, l.data(), r.data());
            if (block >= 36)
                for (int i = 0; i < 256; ++i)
                    p = std::max ({ p, std::abs (l[(std::size_t) i]), std::abs (r[(std::size_t) i]) });
        }
        return p;
    };
    CHECK (peak() > 0.05f);
    // soloing another insert silences it
    x.solo = false;
    song.mixer[8].solo = true;
    CHECK (peak() < 1.0e-4f);
}

// --- (3) coordinator's decision (round 190ev): new inserts monitor their input "When armed", as FL ---------------
#include "core/LiveInput.h"

TEST (win190ev_new_inserts_monitor_when_armed_old_projects_keep_auto)
{
    using lynx::InputMonitor;
    // a new insert (and every insert of a new project) defaults to FL's "When armed": the input is heard while the insert
    // is armed, playing back too (the reporter's armed, soloed insert 7 during playback)
    CHECK (lynx::MixerTrack {}.monitor == InputMonitor::whenArmed);
    const auto fresh = lynx::Song::makeDefault();
    CHECK (fresh.mixer.size() > 7 && fresh.mixer[7].monitor == InputMonitor::whenArmed);
    for (const auto& t : lynx::Song::defaultMixer (12))
        CHECK (t.monitor == InputMonitor::whenArmed);
    CHECK (lynx::inputMonitored (InputMonitor::whenArmed, true, true, false));
    // .lynx: "When armed" is written (it is not the key-less "Auto") and read back; a project saved before (no key)
    // opens with "Auto" as it was saved; every stored value as stored
    CHECK (lynx::monitorKeyWritten (InputMonitor::whenArmed) && ! lynx::monitorKeyWritten (InputMonitor::automatic));
    CHECK (lynx::monitorFromKey (0, false) == InputMonitor::automatic);
    for (const auto m : { InputMonitor::off, InputMonitor::whenArmed, InputMonitor::automatic, InputMonitor::on })
        CHECK (lynx::monitorFromKey ((int) m, lynx::monitorKeyWritten (m)) == m);
    CHECK (lynx::monitorFromKey (9, true) == InputMonitor::on && lynx::monitorFromKey (-2, true) == InputMonitor::off);
}
