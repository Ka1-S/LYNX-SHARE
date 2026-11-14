// Round 190gl: Ableton Link — LYNX's rules for a Link session (core/LinkSync.h), its saved switches (MidiPrefs) and the
// toolbar's Link toggle group (core/ToolbarLayout.h). The network part (engine/AbletonLink.cpp) is checked by the app's
// --linktest route with a second peer. Included at the end of tests.cpp.

#include "core/LinkSync.h"
#include "core/Preferences.h"
#include "core/ToolbarLayout.h"

TEST (link190gl_quantum_is_the_global_launch_quantisation)
{
    using namespace lynx;
    using linksync::quantumBeats;
    CHECK (quantumBeats (LaunchQuant::bar, 4) == 4.0);
    CHECK (quantumBeats (LaunchQuant::bar, 3) == 3.0);
    CHECK (quantumBeats (LaunchQuant::bars2, 4) == 8.0);
    CHECK (quantumBeats (LaunchQuant::bars4, 4) == 16.0);
    CHECK (quantumBeats (LaunchQuant::bars8, 4) == 32.0);
    CHECK (quantumBeats (LaunchQuant::half, 4) == 2.0);
    // none and anything under one beat: one beat
    CHECK (quantumBeats (LaunchQuant::none, 4) == 1.0);
    CHECK (quantumBeats (LaunchQuant::quarter, 4) == 1.0);
    CHECK (quantumBeats (LaunchQuant::sixteenth, 4) == 1.0);
    CHECK (quantumBeats (LaunchQuant::bar, 0) == 1.0); // (a broken bar length never gives 0)
}

TEST (link190gl_phase_and_error_wrap_the_short_way)
{
    using namespace lynx::linksync;
    CHECK (std::abs (phase (5.5, 4.0) - 1.5) < 1e-12);
    CHECK (std::abs (phase (-0.5, 4.0) - 3.5) < 1e-12);
    CHECK (phase (8.0, 4.0) == 0.0);
    // the session at beat 4.1, LYNX at 7.95: 0.15 behind across the bar line (not 3.85 ahead)
    CHECK (std::abs (phaseError (4.1, 7.95, 4.0) - 0.15) < 1e-9);
    CHECK (std::abs (phaseError (7.95, 4.1, 4.0) + 0.15) < 1e-9);
    // half a quantum: one direction only
    CHECK (std::abs (phaseError (2.0, 0.0, 4.0) - 2.0) < 1e-9);
    CHECK (std::abs (phaseError (0.0, 2.0, 4.0) - 2.0) < 1e-9);
    CHECK (phaseError (1.0, 1.0, 0.0) == 0.0);
}

TEST (link190gl_start_waits_for_the_matching_phase)
{
    using namespace lynx::linksync;
    // LYNX starts at bar 1 (phase 0): the session at beat 9.3 of a 4-beat quantum -> beat 12
    CHECK (std::abs (nextBeatWithPhase (9.3, 0.0, 4.0) - 12.0) < 1e-9);
    // LYNX starts on the bar's 2nd beat (song beat 5 = phase 1): the next session beat with phase 1 after 9.3 is 13
    CHECK (std::abs (nextBeatWithPhase (9.3, 5.0, 4.0) - 13.0) < 1e-9);
    // already there: no wait
    CHECK (std::abs (nextBeatWithPhase (12.0, 0.0, 4.0) - 12.0) < 1e-9);
    CHECK (std::abs (nextBeatWithPhase (12.0 - 1e-12, 0.0, 4.0) - (12.0 - 1e-12)) < 1e-9);
}

TEST (link190gl_follow_nudges_small_errors_and_jumps_big_ones)
{
    using namespace lynx::linksync;
    const FollowLaw law;
    // inside the dead band: the session tempo as it is
    auto f = follow (120.0, 0.0002, law);
    CHECK (f.bpm == 120.0 && f.jump == 0.0);
    // behind by 0.01 beat: a little faster, never more than 3 %
    f = follow (120.0, 0.01, law);
    CHECK (f.jump == 0.0 && f.bpm > 120.0 && f.bpm <= 120.0 * 1.03 + 1e-9);
    CHECK (std::abs (f.bpm - (120.0 + 60.0 * 0.01 / 0.5)) < 1e-9);
    // ahead by 0.04 beat: slower, clamped
    f = follow (120.0, -0.04, law);
    CHECK (f.jump == 0.0 && std::abs (f.bpm - 120.0 * 0.97) < 1e-9);
    // further than the jump threshold: a jump by the error, at the session tempo
    f = follow (98.0, 0.3, law);
    CHECK (f.jump == 0.3 && f.bpm == 98.0);
    f = follow (98.0, -1.7, law);
    CHECK (f.jump == -1.7);
}

TEST (link190gl_tempo_rules)
{
    using namespace lynx::linksync;
    TempoArbiter a;
    TempoArbiter::Input in;
    in.sessionBpm = 97.0; // joined a session at 97 BPM: its tempo wins (rule 1)
    in.projectBpm = 120.0;
    in.songBpm = 120.0;
    a.reset (in);
    auto out = a.step (in);
    CHECK (out.propose == 0.0 && out.play == 97.0);

    // rule 2: the project tempo edited -> proposed, and played at once
    in.projectBpm = 125.0;
    in.songBpm = 125.0;
    out = a.step (in);
    CHECK (out.propose == 125.0 && out.play == 125.0);
    in.sessionBpm = 125.0; // (the session took it)
    out = a.step (in);
    CHECK (out.propose == 0.0 && out.play == 125.0 && ! a.changedByPeer());

    // a peer changes the tempo: LYNX follows, and remembers that a peer set it
    in.sessionBpm = 133.0;
    out = a.step (in);
    CHECK (out.propose == 0.0 && out.play == 133.0 && a.changedByPeer());

    // rule 2: the song's tempo automation while it plays goes to the session
    in.tempoMap = true;
    in.playing = true;
    in.started = true;
    in.songBpm = 140.0;
    out = a.step (in);
    CHECK (out.propose == 140.0 && ! a.automationOverridden());
    in.started = false;
    in.sessionBpm = 140.0;
    in.songBpm = 141.0;
    out = a.step (in);
    CHECK (out.propose == 141.0);
    in.sessionBpm = 141.0;
    a.step (in);

    // rule 3: a peer's change overrides the automation until playback starts again
    in.sessionBpm = 90.0;
    out = a.step (in);
    CHECK (out.play == 90.0 && a.automationOverridden());
    in.songBpm = 150.0; // the automation moves on: not proposed
    out = a.step (in);
    CHECK (out.propose == 0.0 && out.play == 90.0);
    // ...but an edit of the project tempo still is
    in.projectBpm = 100.0;
    out = a.step (in);
    CHECK (out.propose == 100.0);
    in.sessionBpm = 100.0;
    a.step (in);
    // the next start ends the override: the automation's tempo goes out again
    in.started = true;
    in.songBpm = 151.0;
    out = a.step (in);
    CHECK (out.propose == 151.0 && ! a.automationOverridden());
    in.started = false;
    in.sessionBpm = 151.0;
    a.step (in);

    // stopped: automation values (a seek) are not proposed
    in.playing = false;
    in.songBpm = 77.0;
    out = a.step (in);
    CHECK (out.propose == 0.0);

    // rule 4: an external MIDI clock LYNX follows wins over the peers
    in.externalBpm = 128.0;
    out = a.step (in);
    CHECK (out.propose == 128.0 && out.play == 128.0);
    in.sessionBpm = 128.0;
    out = a.step (in);
    CHECK (out.propose == 0.0);
    in.sessionBpm = 110.0; // a peer moves it: the clock's tempo goes out again
    out = a.step (in);
    CHECK (out.propose == 128.0);
}

TEST (link190gl_toggle_text_as_the_reference)
{
    using lynx::linksync::toggleText;
    CHECK (toggleText (false, 3) == "Link");
    CHECK (toggleText (true, 0) == "0 Links");
    CHECK (toggleText (true, 1) == "1 Link");
    CHECK (toggleText (true, 2) == "2 Links");
}

TEST (link190gl_prefs_roundtrip_and_old_files)
{
    using namespace lynx::prefs;
    MidiPrefs p;
    CHECK (! p.link && ! p.linkStartStopSync); // off by default (the reference DAW: Start Stop Sync off)
    CHECK (p.toText().find ("abletonLink") == std::string::npos); // (unchanged files while off)
    p.link = true;
    p.linkStartStopSync = true;
    const auto back = MidiPrefs::fromText (p.toText());
    CHECK (back.link && back.linkStartStopSync);
    CHECK (back == p);
    // the controller links keep their own "link=" lines (round 190bt): they are not Ableton Link's switch
    const auto both = MidiPrefs::fromText ("link=7|0|4|2|0|0\nabletonLink=1\n");
    CHECK (both.link && ! both.linkStartStopSync && both.globalLinks.size() == 1 && both.globalLinks[0].cc == 7);
    const auto mixed = MidiPrefs::fromText (both.toText());
    CHECK (mixed == both);
    CHECK (MidiPrefs::fromText ("link=7|0|4|2|0|0\n").globalLinks.size() == 1 && ! MidiPrefs::fromText ("link=7|0|4|2|0|0\n").link);
    // a file of an older version: both off
    const auto old = MidiPrefs::fromText ("velocity=linear\npickup=0\nfootPedal=1\n");
    CHECK (! old.link && ! old.linkStartStopSync);
}

TEST (link190gl_toolbar_group_hidden_in_fl_layout_first_in_order)
{
    using namespace lynx::toolbarlayout;
    auto l = Layout::makeDefault();
    CHECK (! l.isShown (Group::link));             // FL has no Link: hidden on a new install (Pattern workflow)
    CHECK (l.entries.front().group == Group::link); // left of the transport, as the reference DAW's control bar
    CHECK (std::string (groupId (Group::link)) == "link");
    // an older toolbar line gets it hidden, in front
    Layout old;
    old.orderFromString ("transport,master,record,snap,windows,-multilink,file,-key,chord,meters,output");
    CHECK (old.entries.front().group == Group::link && ! old.isShown (Group::link));
    // shown by hand, it stays shown
    old.setShown (Group::link, true);
    Layout again;
    again.orderFromString (old.orderToString());
    CHECK (again.isShown (Group::link));
}
