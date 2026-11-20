// Round 190hp: three community-complaint slices of the 0.1.28 pain report (/mnt/e/lynx-work/pain190gc/REPORT.md §4).
//   B9  global MIDI links: the MIDI Map Mode's mappings "In every project (LYNX)" (core/RemoteMap.h, core/Preferences.h);
//   B16 mix snapshots (core/MixSnapshots.h);
//   B18 split at silence (core/StripSilence.h).
#include "core/MixSnapshots.h"
#include "core/MixerOps.h"
#include "core/Preferences.h"
#include "core/RemoteMap.h"
#include "core/StripSilence.h"

namespace c190hp
{
lynx::Song gridSong (int scenes)
{
    lynx::Song s;
    s.mixer.resize (4);
    s.clipGrid.emplace();
    s.clipGrid->scenes.resize ((std::size_t) scenes);
    return s;
}
} // namespace c190hp

// --- B9 ------------------------------------------------------------------------------------------------------------------

TEST (c190hp_transport_mapping_moves_to_every_project_and_back)
{
    using namespace lynx::remotemap;
    auto song = c190hp::gridSong (2);
    lynx::prefs::MidiPrefs prefs;
    CHECK (assignMidi (song, Target::of (lynx::RemoteAction::play), { 0, 118, -1 }) == Refusal::none);
    CHECK (assignMidi (song, Target::sceneLaunch (1), { 0, lynx::linkNoteBase + 44, 2 }) == Refusal::none);
    CHECK (assignMidi (song, Target::slot (0, 0), { 0, lynx::linkNoteBase + 36, -1 }) == Refusal::none);
    const auto all = rows (song, Mode::midi);
    CHECK (all.size() == 3);
    // clip slots and track stop buttons belong to one project
    CHECK (! canBeGlobal (Target::slot (0, 0)) && ! canBeGlobal (Target::trackStop (0)));
    CHECK (canBeGlobal (Target::of (lynx::RemoteAction::metronome)) && canBeGlobal (Target::sceneLaunch (0)));
    CHECK (! canMove (song, prefs, all[2], true));
    CHECK (! moveToEveryProject (song, prefs, all[2]) && song.remoteMappings.size() == 3);
    // Play and the scene into the settings (from the last row, so the first keeps its index)
    CHECK (moveToEveryProject (song, prefs, all[1]));
    CHECK (moveToEveryProject (song, prefs, all[0]));
    CHECK (song.remoteMappings.size() == 1 && prefs.globalMappings.size() == 2);
    CHECK (prefs.globalMappings[0].action == (int) lynx::RemoteAction::sceneLaunch && prefs.globalMappings[0].scene == 1 && prefs.globalMappings[0].port == 2);
    // the settings file keeps them; an older LYNX skips the new lines
    const auto text = prefs.toText();
    CHECK (text.find ("globalmap=2|0|300|2|1") != std::string::npos && text.find ("globalmap=5|0|118|-1|-1") != std::string::npos);
    CHECK (lynx::prefs::MidiPrefs::fromText (text).globalMappings == prefs.globalMappings);
    CHECK (lynx::prefs::MidiPrefs::fromText ("globalmap=1|0|36|-1|0\nglobalmap=3|0|36|-1|-1\nglobalmap=5|0|999|-1|-1\nglobalmap=2|0|10|-1|-1\n").globalMappings.empty());
    // a new project: the global rows are there, the mappings act (a CC above 64 presses Play)
    lynx::Song fresh = c190hp::gridSong (3);
    const auto g = globalRows (fresh, prefs);
    CHECK (g.size() == 2 && g[0].global && ! g[0].overridden && g[1].target == Target::of (lynx::RemoteAction::play));
    const auto active = activeGlobalMappings (fresh, prefs);
    CHECK (active.size() == 2);
    const auto fires = midiFires (fresh, active, 0, 118, 127, -1, -1);
    CHECK (fires.size() == 1 && fires[0].target == Target::of (lynx::RemoteAction::play) && fires[0].edge == Edge::press);
    CHECK (midiOf (fresh, prefs, Target::of (lynx::RemoteAction::play)).number == 118);
    std::uint64_t ccs[2] {}, notes[2] {};
    usedNumbers (active, ccs, notes);
    CHECK ((ccs[1] & (std::uint64_t { 1 } << (118 - 64))) != 0 && (notes[0] & (std::uint64_t { 1 } << 44)) != 0);
    // a project without scene 2 ignores that mapping; the port must match
    lynx::Song small = c190hp::gridSong (1);
    CHECK (activeGlobalMappings (small, prefs).size() == 1);
    CHECK (midiFires (fresh, active, 0, lynx::linkNoteBase + 44, 100, -1, -1).empty());
    CHECK (midiFires (fresh, active, 0, lynx::linkNoteBase + 44, 100, -1, 2).size() == 1);
    // the project's own mapping of the same control wins (and so does its own mapping of the same target)
    CHECK (assignMidi (fresh, Target::of (lynx::RemoteAction::stop), { 0, 118, -1 }) == Refusal::none);
    CHECK (activeGlobalMappings (fresh, prefs).size() == 1 && globalRows (fresh, prefs)[1].overridden);
    const auto own = midiFires (fresh, activeGlobalMappings (fresh, prefs), 0, 118, 127, -1, -1);
    CHECK (own.size() == 1 && own[0].target == Target::of (lynx::RemoteAction::stop));
    // back into this project only
    auto small2 = c190hp::gridSong (1);
    const auto rowsNow = globalRows (small2, prefs);
    CHECK (! canMove (small2, prefs, rowsNow[0], false)); // (that scene is not in this project)
    CHECK (moveToThisProject (small2, prefs, rowsNow[1]) && prefs.globalMappings.size() == 1 && small2.remoteMappings.size() == 1
           && small2.remoteMappings[0].action == lynx::RemoteAction::play);
    CHECK (removeGlobalRow (prefs, globalRows (small2, prefs)[0]) && prefs.globalMappings.empty());
}

TEST (c190hp_parameter_mapping_becomes_an_fl_global_link)
{
    using namespace lynx::remotemap;
    auto song = c190hp::gridSong (1);
    lynx::prefs::MidiPrefs prefs;
    const lynx::AutomationTarget master { lynx::AutomationTargetKind::mixerVolume, 0, 0, 0 };
    CHECK (assignMidi (song, Target::parameter (master), { 0, 7, -1 }) == Refusal::none);
    CHECK (setRange (song, rows (song, Mode::midi)[0], 0.1f, 0.8f));
    CHECK (moveToEveryProject (song, prefs, rows (song, Mode::midi)[0]));
    CHECK (song.midiLinks.empty() && prefs.globalLinks.size() == 1);
    const auto& l = prefs.globalLinks[0];
    CHECK (l.cc == 7 && l.channel == 0 && l.kind == (int) lynx::AutomationTargetKind::mixerVolume && l.min == 0.1f && l.max == 0.8f);
    // FL's global links: the same list, its range kept by the settings file
    const auto back = lynx::prefs::MidiPrefs::fromText (prefs.toText());
    CHECK (back.globalLinks.size() == 1 && std::abs (back.globalLinks[0].min - 0.1f) < 1.0e-6f && std::abs (back.globalLinks[0].max - 0.8f) < 1.0e-6f);
    const auto g = globalRows (song, prefs);
    CHECK (g.size() == 1 && g[0].link && g[0].hasRange && g[0].target == Target::parameter (master));
    CHECK (setGlobalRange (prefs, g[0], 0.8f, 0.1f) && prefs.globalLinks[0].min == 0.8f);
    CHECK (midiOf (song, prefs, Target::parameter (master)).number == 7);
    // a project link on the same controller overrides it
    CHECK (assignMidi (song, Target::parameter ({ lynx::AutomationTargetKind::tempo, 0, 0, 0 }), { 0, 7, -1 }) == Refusal::none);
    CHECK (globalRows (song, prefs)[0].overridden && ! midiOf (song, prefs, Target::parameter (master)).found);
    CHECK (moveToThisProject (song, prefs, globalRows (song, prefs)[0]) && prefs.globalLinks.empty() && song.midiLinks.size() == 2
           && song.midiLinks[1].min == 0.8f);
}

TEST (c190hp_global_links_of_sends_vcas_and_levels_survive_the_settings_file)
{
    // before round 190hp the reader kept only target kinds 1..8: a global link of a send, a VCA or a channel's Levels
    // adjustment went at the next start
    lynx::prefs::MidiPrefs prefs;
    for (const auto kind : { lynx::AutomationTargetKind::mixerSend, lynx::AutomationTargetKind::vcaLevel, lynx::AutomationTargetKind::channelAdjust })
        prefs.globalLinks.push_back ({ 20 + (int) kind, -1, (int) kind, 1, 0, 1 });
    CHECK (lynx::prefs::MidiPrefs::fromText (prefs.toText()).globalLinks == prefs.globalLinks);
    CHECK (lynx::prefs::MidiPrefs::fromText ("link=20|-1|12|1|0|0\n").globalLinks.empty()); // (an unknown kind still goes)
}

// --- B16 -----------------------------------------------------------------------------------------------------------------

TEST (c190hp_mix_snapshots_take_recall_and_compare)
{
    using namespace lynx;
    Song song;
    song.mixer.resize (5);
    song.mixer[1].volume = 0.5f;
    song.mixer[2].pan = -0.25f;
    song.setRoute (1, 3, true, 0.7f);
    song.mixer[2].effects.resize (1);
    CHECK (mixsnap::take (song) == 0 && song.mixSnapshots[0].name == "Snapshot 1" && song.mixSnapshotCurrent == 0);
    // B: another mix
    song.mixer[1].volume = 0.9f;
    song.mixer[2].pan = 0.5f;
    song.mixer[3].muted = true;
    song.setRoute (1, 3, false);
    song.setRoute (3, 1, true, 0.4f); // (the other way round: possible now that 1 no longer sends to 3)
    song.mixer[2].colour = 0xff112233; // (not part of a snapshot)
    CHECK (mixsnap::take (song) == 1 && song.mixSnapshotPrevious == 0 && song.mixSnapshots[1].name == "Snapshot 2");
    CHECK (mixsnap::matches (song, song.mixSnapshots[1]) && ! mixsnap::matches (song, song.mixSnapshots[0]));
    // recall A: levels, mute and the routes come back; the loop 3 -> 1 -> 3 cannot happen
    CHECK (mixsnap::recall (song, 0));
    CHECK (song.mixer[1].volume == 0.5f && song.mixer[2].pan == -0.25f && ! song.mixer[3].muted && song.routes (1, 3) && ! song.routes (3, 1));
    CHECK (song.mixer[2].colour == 0xff112233u && song.mixer[2].effects.size() == 1);
    CHECK (song.mixSnapshotCurrent == 0 && song.mixSnapshotPrevious == 1 && mixsnap::matches (song, song.mixSnapshots[0]));
    // Compare A/B: B, then A again
    CHECK (mixsnap::canCompare (song) && mixsnap::compareAB (song) && song.mixer[3].muted && song.routes (3, 1) && song.mixer[1].volume == 0.9f);
    CHECK (mixsnap::compareAB (song) && song.mixer[1].volume == 0.5f);
    // update, rename, remove, the cap
    song.mixer[4].volume = 0.1f;
    CHECK (mixsnap::update (song, 0) && mixsnap::matches (song, song.mixSnapshots[0]) && mixsnap::rename (song, 0, "Verse"));
    CHECK (mixsnap::remove (song, 1) && song.mixSnapshotPrevious == -1 && song.mixSnapshotCurrent == 0 && ! mixsnap::canCompare (song));
    while ((int) song.mixSnapshots.size() < maxMixSnapshots)
        CHECK (mixsnap::take (song) >= 0);
    CHECK (mixsnap::take (song) == -1 && (int) song.mixSnapshots.size() == maxMixSnapshots);
    CHECK (song.mixSnapshots[1].name == "Snapshot 1"); // (free names: "Verse" took the first one's place)
}

TEST (c190hp_mix_snapshots_follow_the_mixer_reordering)
{
    using namespace lynx;
    Song song;
    song.mixer.resize (5);
    song.mixer[1].volume = 0.3f;
    song.mixer[2].volume = 0.6f;
    song.setRoute (2, 4, true, 0.5f);
    mixsnap::take (song);
    mixerops::swapTracks (song, 1, 2); // the inserts change places: the snapshot follows them
    const auto& snap = song.mixSnapshots[0];
    const auto at = [&] (int t) { return std::find_if (snap.inserts.begin(), snap.inserts.end(), [t] (const MixSnapshot::Insert& e) { return e.track == t; }); };
    CHECK (at (1)->volume == 0.6f && at (2)->volume == 0.3f && at (1)->sends.size() == 1 && at (1)->sends[0].target == 4);
    CHECK (mixsnap::matches (song, snap));
    mixerops::deleteTrack (song, 4); // a deleted insert's entry and the sends to it go
    CHECK (at (1)->sends.empty() && std::none_of (snap.inserts.begin(), snap.inserts.end(), [] (const MixSnapshot::Insert& e) { return e.track == 4; }));
    song.mixer[1].volume = 1.0f;
    CHECK (mixsnap::recall (song, 0) && song.mixer[1].volume == 0.6f);
}

// --- B18 -----------------------------------------------------------------------------------------------------------------

TEST (c190hp_split_at_silence_finds_the_sound)
{
    using namespace lynx::stripsilence;
    const double rate = 48000.0;
    // 0.5 s silence, 1 s tone, 0.1 s pause (below the minimum silence), 0.5 s tone, 1 s silence, 0.25 s tone, 0.4 s silence
    std::vector<float> a;
    const auto add = [&] (double seconds, float level) {
        const auto n = (int) (seconds * rate);
        for (int i = 0; i < n; ++i)
            a.push_back (level * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * (double) a.size() / rate));
    };
    add (0.5, 0.0f);
    add (1.0, 0.5f);
    add (0.1, 0.001f);
    add (0.5, 0.5f);
    add (1.0, 0.0001f);
    add (0.25, 0.5f);
    add (0.4, 0.0f);
    Options o; // -40 dB, 200 ms, 10 ms pre, 50 ms post
    const auto kept = keptSpans (a.data(), nullptr, (std::int64_t) a.size(), rate, 0, (std::int64_t) a.size(), o);
    CHECK (kept.size() == 2);
    const auto near = [&] (std::int64_t frames, double seconds) { return std::abs ((double) frames / rate - seconds) < 0.012; };
    CHECK (near (kept[0].from, 0.5 - 0.010) && near (kept[0].to, 2.1 + 0.050));
    CHECK (near (kept[1].from, 3.1 - 0.010) && near (kept[1].to, 3.35 + 0.050));
    // a shorter minimum splits at the pause too; a lower threshold keeps the -80 dB hiss
    o.minSilenceMs = 50.0;
    CHECK (keptSpans (a.data(), a.data(), (std::int64_t) a.size(), rate, 0, (std::int64_t) a.size(), o).size() == 3);
    o.minSilenceMs = 200.0;
    o.thresholdDb = -90.0f;
    const auto hiss = keptSpans (a.data(), nullptr, (std::int64_t) a.size(), rate, 0, (std::int64_t) a.size(), o);
    CHECK (hiss.size() == 1 && near (hiss[0].from, 0.49) && near (hiss[0].to, 3.40));
    // all quiet; a range inside the file; long rolls join the pieces
    o.thresholdDb = -3.0f;
    CHECK (keptSpans (a.data(), nullptr, (std::int64_t) a.size(), rate, 0, (std::int64_t) a.size(), o).empty());
    o = {};
    const auto inner = keptSpans (a.data(), nullptr, (std::int64_t) a.size(), rate, (std::int64_t) (1.0 * rate), (std::int64_t) (3.0 * rate), o);
    CHECK (inner.size() == 1 && inner[0].from == (std::int64_t) (1.0 * rate));
    o.preRollMs = 600.0;
    o.postRollMs = 500.0;
    CHECK (keptSpans (a.data(), nullptr, (std::int64_t) a.size(), rate, 0, (std::int64_t) a.size(), o).size() == 1);
}

TEST (c190hp_split_at_silence_cuts_the_clip)
{
    using namespace lynx;
    std::vector<Clip> clips (2);
    auto& c = clips[1];
    c.kind = ClipKind::audio;
    c.start = 1000;
    c.offset = 200;
    c.length = 960;
    c.fadeIn = 30;
    c.fadeOut = 40;
    c.gain = 0.5f;
    const auto made = stripsilence::splitClip (clips, 1, { { 0, 100 }, { 300, 500 }, { 900, 960 } });
    CHECK (made == std::vector<int> ({ 1, 2, 3 }) && clips.size() == 4);
    CHECK (clips[1].start == 1000 && clips[1].offset == 200 && clips[1].length == 100 && clips[1].fadeIn == 30 && clips[1].fadeOut == 0);
    CHECK (clips[2].start == 1300 && clips[2].offset == 500 && clips[2].length == 200 && clips[2].fadeIn == 0 && clips[2].fadeOut == 0);
    CHECK (clips[3].start == 1900 && clips[3].offset == 1100 && clips[3].length == 60 && clips[3].fadeOut == 40 && clips[3].gain == 0.5f);
    // nothing to cut: the whole clip, or no piece
    CHECK (stripsilence::splitClip (clips, 0, { { 0, clips[0].length } }).empty());
    CHECK (stripsilence::splitClip (clips, 1, {}).empty() && clips.size() == 4);
}
