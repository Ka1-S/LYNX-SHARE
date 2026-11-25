// Round 190iu (issues #186 and #191): wheel zones, temporary tools, MIDI drops, take clean-up.

#include "core/LaneView.h"
#include "core/MidiDrop.h"
#include "core/TrackOnly.h"
#include "core/TakeCleanup.h"
#include "core/TempTool.h"
#include "core/WheelZones.h"

namespace workflow190iu
{
// a two-track MIDI file: "Bass" (C2, one bar long) and an unnamed track (E4 at beat 2, MIDI channel 10)
inline lynx::midiimport::ParsedMidi twoTracks()
{
    using namespace lynx;
    midiimport::ParsedMidi m;
    m.trackNames = { "Bass", "" };
    m.events = { { 0, 0, 0x90, 36, 100 }, { 0, 4 * ppq, 0x80, 36, 0 }, { 1, ppq, 0x99, 64, 90 }, { 1, 2 * ppq, 0x89, 64, 0 } };
    return m;
}

inline lynx::midiimport::ParsedMidi oneTrack()
{
    using namespace lynx;
    midiimport::ParsedMidi m;
    m.trackNames = { "" };
    m.events = { { 0, 0, 0x90, 60, 100 }, { 0, ppq / 2, 0x80, 60, 0 }, { 0, ppq, 0x90, 62, 100 }, { 0, 3 * ppq / 2, 0x80, 62, 0 } };
    return m;
}
} // namespace workflow190iu

TEST (workflow190iu_wheel_zones_scroll_over_headers_zoom_over_arrangement)
{
    using namespace lynx::wheelzones;
    // off (the default, FL and Live): Ctrl zooms, the plain wheel scrolls, wherever the pointer is
    CHECK (! enabled());
    CHECK (! zooms (false, true, false) && ! zooms (false, false, false));
    CHECK (zooms (false, true, true) && zooms (false, false, true));
    CHECK (zooms (false, false, true, false)); // (a sideways wheel with Ctrl: as before)
    // on (#186): over the headers the plain wheel scrolls, over the arrangement it zooms; Ctrl swaps both
    CHECK (! zooms (true, true, false) && zooms (true, true, true));
    CHECK (zooms (true, false, false) && ! zooms (true, false, true));
    // a sideways wheel or swipe keeps scrolling sideways
    CHECK (! zooms (true, false, false, false) && ! zooms (true, true, true, false));
    // the global switch
    setEnabled (true);
    CHECK (zoomsHere (false, false) && ! zoomsHere (true, false));
    setEnabled (false);
    CHECK (! zoomsHere (false, false));
}

TEST (workflow190iu_temporary_tool_hold_tap_repeat_and_mouse)
{
    using namespace lynx;
    using temptool::Editor;
    temptool::Hold h;
    EditTool back = EditTool::pencil;
    // a quick tap: the tool stays
    CHECK (h.press (Editor::playlist, 'C', EditTool::pencil, EditTool::knife, 1000));
    CHECK (! h.release (1000 + temptool::holdMs - 1, EditTool::knife, back) && ! h.active());
    // held: back to the tool before on release; the key's auto-repeats change nothing
    CHECK (h.press (Editor::playlist, 'C', EditTool::pencil, EditTool::knife, 2000));
    CHECK (! h.press (Editor::playlist, 'C', EditTool::knife, EditTool::knife, 2500)); // (repeat)
    CHECK (h.release (2000 + temptool::holdMs, EditTool::knife, back) && back == EditTool::pencil);
    // a short press while the mouse is used is a temporary use too
    CHECK (h.press (Editor::pianoRoll, 'D', EditTool::brush, EditTool::eraser, 3000));
    h.mouseUsed();
    CHECK (h.release (3050, EditTool::eraser, back) && back == EditTool::brush);
    // another tool key during a hold: letting it go returns to the tool from before the first one
    CHECK (h.press (Editor::playlist, 'C', EditTool::lasso, EditTool::knife, 4000));
    CHECK (h.press (Editor::playlist, 'D', EditTool::knife, EditTool::eraser, 4100));
    CHECK (h.release (5000, EditTool::eraser, back) && back == EditTool::lasso);
    // a tool picked meanwhile (a click on the tool strip) stays
    CHECK (h.press (Editor::playlist, 'C', EditTool::pencil, EditTool::knife, 6000));
    CHECK (! h.release (7000, EditTool::zoom, back));
    // the key of the tool already in use: nothing to go back to
    CHECK (! h.press (Editor::playlist, 'P', EditTool::pencil, EditTool::pencil, 8000));
    CHECK (! h.release (9000, EditTool::pencil, back));
    // the switch is off by default (a LYNX extra: FL's tool keys only select)
    CHECK (! temptool::enabled());
}

TEST (workflow190iu_midi_drop_on_the_playlist_pattern_layout)
{
    using namespace lynx;
    // (on by default since 0.1.31.2, #287; off, only the Live-style arrangement takes them)
    CHECK (mididrop::playlistDrop() && mididrop::playlistTakes (true) && mididrop::playlistTakes (false));
    mididrop::setPlaylistDrop (false);
    CHECK (mididrop::playlistTakes (true) && ! mididrop::playlistTakes (false));
    mididrop::setPlaylistDrop (true);
    // an unassigned track: one pattern named after the file, a new channel per MIDI track, the clip at the drop
    {
        auto s = Song::makeDefault();
        s.playlist.clear();
        const auto channels = s.channels.size(), patterns = s.patterns.size(), clips = s.playlist.size();
        const auto r = mididrop::dropOnPlaylist (s, workflow190iu::twoTracks(), "groove", 2, 4 * ppq, false);
        CHECK (r.ok && r.patterns.size() == 1 && r.channels.size() == 2 && r.tracks == std::vector<int> { 2 });
        CHECK (s.channels.size() == channels + 2 && s.patterns.size() == patterns + 1 && s.playlist.size() == clips + 1);
        CHECK (s.channels[(std::size_t) r.channels[0]].name == "Bass" && s.channels[(std::size_t) r.channels[1]].name == "groove 2");
        CHECK (s.channels[(std::size_t) r.channels[0]].instrument == InstrumentKind::synth);
        const auto& p = s.patterns[(std::size_t) r.patterns[0]];
        CHECK (p.name == "groove" && p.notes.size() == 2);
        CHECK (p.notes[0].key == 36 && p.notes[0].start == 0 && p.notes[0].length == 4 * ppq && p.notes[0].channel == r.channels[0]);
        CHECK (p.notes[1].key == 64 && p.notes[1].start == ppq && p.notes[1].channel == r.channels[1]);
        const auto& clip = s.playlist.back();
        CHECK (clip.kind == ClipKind::pattern && clip.pattern == r.patterns[0] && clip.track == 2 && clip.start == 4 * ppq && clip.length >= 4 * ppq);
        CHECK (s.mode == PlayMode::song);
        // the track's mode stays (FL's Playlist: tracks and channels apart)
        CHECK (s.playlistTracks.size() <= 2 || s.playlistTracks[2].mode == PlaylistTrackMode::unassigned);
    }
    // an instrument track: every note to the track's channel, no new channel
    {
        auto s = Song::makeDefault();
        s.playlist.clear();
        const auto channel = trackmodes::addInstrumentTrack (s, mididrop::newChannel ("Keys"), 1);
        CHECK (channel >= 0);
        const auto channels = s.channels.size();
        const auto r = mididrop::dropOnPlaylist (s, workflow190iu::twoTracks(), "groove", 1, 0, false);
        CHECK (r.ok && s.channels.size() == channels && r.channels == std::vector<int> { channel });
        const auto& p = s.patterns[(std::size_t) r.patterns[0]];
        CHECK (p.notes.size() == 2 && p.notes[0].channel == channel && p.notes[1].channel == channel);
    }
    // a file without notes: refused, nothing changed
    {
        auto s = Song::makeDefault();
        s.playlist.clear();
        const auto before = s.patterns.size();
        midiimport::ParsedMidi empty;
        const auto r = mididrop::dropOnPlaylist (s, empty, "x", 0, 0, false);
        CHECK (! r.ok && r.error == "no notes" && s.patterns.size() == before);
    }
}

TEST (workflow190iu_midi_drop_live_style_arrangement_and_slots)
{
    using namespace lynx;
    // the Clip-launch arrangement: an empty track becomes an instrument track of a new channel; each further MIDI track
    // of the file on its own track below (Live)
    {
        auto s = Song::makeDefault();
        s.playlist.clear();
        const auto r = mididrop::dropOnPlaylist (s, workflow190iu::twoTracks(), "groove", 0, 0, true);
        CHECK (r.ok && r.tracks.size() == 2 && r.tracks[0] == 0 && r.tracks[1] == 1 && r.patterns.size() == 2);
        CHECK (mididrop::channelUnder (s, 0, true) == r.channels[0] && mididrop::channelUnder (s, 1, true) == r.channels[1]);
        CHECK (s.playlistTracks[0].mode == PlaylistTrackMode::instrument && s.channels[(std::size_t) r.channels[0]].name == "Bass");
        // a second file on the same track: the track's instrument, no new channel
        const auto channels = s.channels.size();
        const auto again = mididrop::dropOnPlaylist (s, workflow190iu::oneTrack(), "lead", 0, 8 * ppq, true);
        CHECK (again.ok && s.channels.size() == channels && again.channels[0] == r.channels[0]);
        CHECK (s.patterns[(std::size_t) again.patterns[0]].name == "lead" && s.playlist.back().start == 8 * ppq);
    }
    // an audio track takes no MIDI
    {
        auto s = Song::makeDefault();
        s.playlist.clear();
        CHECK (trackmodes::makeAudioTrack (s, 0));
        const auto patterns = s.patterns.size();
        const auto r = mididrop::dropOnPlaylist (s, workflow190iu::oneTrack(), "x", 0, 0, true);
        CHECK (! r.ok && r.error == "audio track" && s.patterns.size() == patterns);
        CHECK (! mididrop::dropOnSlot (s, workflow190iu::oneTrack(), "x", 0, 0).ok);
    }
    // a slot: one clip with every MIDI track merged on the track's (new) instrument
    {
        auto s = Song::makeDefault();
        s.playlist.clear();
        const auto r = mididrop::dropOnSlot (s, workflow190iu::twoTracks(), "groove", 3, 2);
        CHECK (r.ok && r.channels.size() == 1 && r.tracks == std::vector<int> { 3 });
        CHECK (s.clipGrid.has_value());
        const auto* clip = s.clipGrid ? launcher::clipAt (*s.clipGrid, 3, 2) : nullptr;
        CHECK (clip != nullptr && clip != nullptr && clip->kind == ClipKind::pattern && clip->pattern == r.patterns[0]);
        const auto& p = s.patterns[(std::size_t) r.patterns[0]];
        CHECK (p.notes.size() == 2 && p.notes[0].channel == r.channels[0] && p.notes[1].channel == r.channels[0]);
        CHECK (s.playlistTracks[3].mode == PlaylistTrackMode::instrument && s.channels[(std::size_t) r.channels[0]].name == "groove");
        // the next file into the next slot of the same track: the same instrument
        const auto next = mididrop::dropOnSlot (s, workflow190iu::oneTrack(), "lead", 3, 3);
        CHECK (next.ok && next.channels[0] == r.channels[0]);
    }
}

TEST (workflow190iu_take_cleanup_deletes_takes_and_envelopes)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.playlist.clear();
    // two takes (audio clip channels), one placed; two recorded envelopes, one placed
    const auto take = [&s] (const char* name) {
        ChannelDef c;
        c.name = name;
        c.instrument = InstrumentKind::audioClip;
        s.channels.push_back (c);
        return (int) s.channels.size() - 1;
    };
    const auto t1 = take ("Take 1"), t2 = take ("Take 2");
    Clip a;
    a.kind = ClipKind::audio;
    a.channel = t1;
    a.track = 0;
    a.length = ppq;
    s.playlist.push_back (a);
    Automation e1, e2;
    e1.name = "Env 1";
    e2.name = "Env 2";
    s.automations.push_back (e1);
    s.automations.push_back (e2);
    Clip c;
    c.kind = ClipKind::automation;
    c.automation = 1;
    c.track = 1;
    c.length = ppq;
    s.playlist.push_back (c);
    CHECK (takecleanup::automationUses (s, 1) == 1 && takecleanup::automationUses (s, 0) == 0);
    CHECK (takecleanup::unusedAutomations (s) == std::vector<int> { 0 });

    // deleting a placed take removes its clips with it; anything else is refused
    auto deleted = s;
    CHECK (takecleanup::deleteAudioClip (deleted, t1));
    CHECK (deleted.channels.size() == s.channels.size() - 1);
    CHECK (std::none_of (deleted.playlist.begin(), deleted.playlist.end(), [] (const Clip& k) { return k.kind == ClipKind::audio; }));
    CHECK (! takecleanup::deleteAudioClip (deleted, -1) && ! takecleanup::deleteAudioClip (deleted, 999));
    {
        auto other = s;
        other.channels[(std::size_t) t1].instrument = InstrumentKind::synth; // (not a take)
        CHECK (! takecleanup::deleteAudioClip (other, t1));
    }
    // deleting a placed envelope removes its clip; the clips of the later ones are renumbered
    auto env = s;
    CHECK (takecleanup::deleteAutomation (env, 0));
    CHECK (env.automations.size() == 1 && env.automations[0].name == "Env 2");
    CHECK (std::any_of (env.playlist.begin(), env.playlist.end(), [] (const Clip& k) { return k.kind == ClipKind::automation && k.automation == 0; }));
    CHECK (takecleanup::deleteAutomation (env, 0) && env.automations.empty());
    CHECK (std::none_of (env.playlist.begin(), env.playlist.end(), [] (const Clip& k) { return k.kind == ClipKind::automation; }));
    // the unused ones at once: the placed envelope stays
    auto unused = s;
    CHECK (takecleanup::deleteUnusedAutomations (unused) == 1 && unused.automations.size() == 1 && unused.automations[0].name == "Env 2");
    CHECK (takecleanup::automationUses (unused, 0) == 1);
    (void) t2;
}

TEST (workflow190iu_track_only_mode_gives_each_new_instrument_a_track)
{
    using namespace lynx;
    CHECK (! trackonly::enabled()); // off by default
    auto s = Song::makeDefault();
    s.playlist.clear();
    // a project made with the rack: two instruments, one with an instrument track already, one audio clip channel
    s.channels.clear();
    s.channels.push_back (mididrop::newChannel ("Lead"));
    s.channels.push_back (mididrop::newChannel ("Pad"));
    ChannelDef take;
    take.instrument = InstrumentKind::audioClip;
    s.channels.push_back (take);
    CHECK (trackmodes::makeInstrumentTrack (s, 0, 0));
    CHECK (trackonly::withoutTrack (s) == 1); // (Pad; the audio clip channel never gets one)
    // the edit that adds channels: only those from `channelsBefore` on get a track
    const auto before = s.channels.size();
    s.channels.push_back (mididrop::newChannel ("Bass"));
    CHECK (trackonly::follow (s, before) == 1);
    const auto bassTracks = trackmodes::instrumentTracksOf (s, (int) before);
    CHECK (bassTracks.size() == 1 && s.playlistTracks[(std::size_t) bassTracks[0]].mode == PlaylistTrackMode::instrument);
    CHECK (trackmodes::instrumentTracksOf (s, 1).empty()); // (Pad stays as it was)
    // "give every instrument its own track"
    CHECK (trackonly::assignAll (s) == 1 && trackonly::withoutTrack (s) == 0);
    CHECK (trackmodes::instrumentTracksOf (s, 2).empty());
    // active only in the Pattern layout with the switch on
    trackonly::setEnabled (true);
    CHECK (trackonly::active() == (layoutstyle::current() == layoutstyle::Style::pattern));
    trackonly::setEnabled (false);
    CHECK (! trackonly::active());
}

TEST (workflow190iu_automation_clips_move_into_their_tracks_lanes)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.playlist.clear();
    s.channels.clear();
    s.channels.push_back (mididrop::newChannel ("Lead"));
    CHECK (trackmodes::makeInstrumentTrack (s, 2, 0));
    const auto insert = s.channels[0].mixerTrack;
    CHECK (insert > 0);
    // an envelope of the Lead's volume (owned by track 2), one of the tempo (no owner)
    Automation vol;
    vol.name = "Lead volume";
    vol.target = { AutomationTargetKind::channelVolume, 0, 0, 0 };
    vol.points = { { 0, 0.2f, 0.0f }, { 4 * ppq, 0.9f, 0.0f } };
    Automation tempo;
    tempo.name = "Tempo";
    tempo.target = { AutomationTargetKind::tempo, 0, 0, 0 };
    tempo.points = { { 0, 0.5f, 0.0f } };
    s.automations = { vol, tempo };
    const auto clipOf = [] (int automation, int track) {
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = automation;
        c.track = track;
        c.start = 8 * ppq;
        c.length = 4 * ppq;
        return c;
    };
    s.playlist.push_back (clipOf (0, 5));
    s.playlist.push_back (clipOf (1, 6));
    CHECK (laneview::ownerTrack (s, vol.target) == 2 && laneview::ownerTrack (s, tempo.target) == -1);
    // the Master and an unlinked insert have no owner track either
    CHECK (laneview::ownerTrack (s, { AutomationTargetKind::mixerVolume, 0, 0, 0 }) == -1);
    CHECK (laneview::ownerTrack (s, { AutomationTargetKind::mixerVolume, insert, 0, 0 }) == 2); // (the channel's own insert)
    CHECK (laneview::movableClips (s) == std::vector<int> { 0 });
    const auto valueMid = s.automations[0].valueAt (2.0 * ppq);
    CHECK (laneview::clipsToLanes (s) == 1);
    // the clip went into a lane under track 2; the tempo clip stayed; the leftover automation went with its clip
    CHECK (s.playlist.size() == 1 && s.playlist[0].automation >= 0 && s.automations[(std::size_t) s.playlist[0].automation].name == "Tempo");
    CHECK (s.playlistTracks.size() > 2 && s.playlistTracks[2].lanes.size() == 1);
    const auto lane = s.playlistTracks[2].lanes[0].automation;
    CHECK (lane >= 0 && lane < (int) s.automations.size() && s.automations[(std::size_t) lane].target == vol.target);
    CHECK (s.automations.size() == 2); // (tempo + the lane; the old Lead volume automation is gone)
    // the lane plays the clip's curve where the clip was
    CHECK (std::abs (s.automations[(std::size_t) lane].valueAt (10.0 * ppq) - valueMid) < 0.02f);
    CHECK (laneview::movableClips (s).empty());
}
