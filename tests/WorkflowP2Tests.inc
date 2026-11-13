// Workflow P2 tests (docs/WORKFLOW_FL.md section 3.2, P2). Included at the end of tests.cpp.

#include "core/History.h"
#include "core/MixerOps.h"
#include "core/NoteTools.h"
#include "core/SearchQuery.h"

namespace p2test
{
lynx::Note note (std::int64_t start, int key, std::int64_t length = lynx::ticksPerStep, int channel = 0, float velocity = 0.78f)
{
    lynx::Note n;
    n.start = start;
    n.key = key;
    n.length = length;
    n.channel = channel;
    n.velocity = velocity;
    return n;
}

std::vector<int> all (const lynx::Pattern& p)
{
    std::vector<int> out;
    for (int i = 0; i < (int) p.notes.size(); ++i)
        out.push_back (i);
    return out;
}

std::vector<int> stepsOf (const lynx::Pattern& p, int channel)
{
    std::vector<int> out;
    for (const auto& n : p.notes)
        if (n.channel == channel)
            out.push_back ((int) (n.start / lynx::ticksPerStep));
    std::sort (out.begin(), out.end());
    return out;
}
} // namespace p2test

TEST (p2_note_levels_roundtrip)
{
    using namespace lynx::notetools;
    lynx::Note n;
    for (int l = 0; l < levelCount; ++l)
    {
        const auto level = (Level) l;
        CHECK (std::abs (levelValue (n, level) - levelDefault (level)) < 1e-6f);
        setLevelValue (n, level, 0.25f);
        CHECK (std::abs (levelValue (n, level) - 0.25f) < 1e-5f);
    }
    CHECK (std::abs (n.pan + 0.5f) < 1e-5f && std::abs (n.finePitch + 60.0f) < 1e-3f);
    setLevelValue (n, Level::velocity, 3.0f); // clamped
    CHECK (n.velocity == 1.0f);
    CHECK (levelIsBipolar (Level::pan) && ! levelIsBipolar (Level::velocity));
}

TEST (p2_scale_snap)
{
    using namespace lynx::notetools;
    // C major (scale 1, root 0)
    CHECK (inScale (60, 0, 1) && ! inScale (61, 0, 1) && inScale (61, 0, 0));
    CHECK (snapToScale (61, 0, 1) == 60);  // C# -> C (tie goes down)
    CHECK (snapToScale (66, 0, 1) == 65);  // F# -> F
    CHECK (snapToScale (63, 0, 1) == 62);  // D# -> D
    CHECK (snapToScale (61, 0, 0) == 61);  // no scale
    // A minor pentatonic (scale 5, root 9): A C D E G
    CHECK (snapToScale (70, 9, 5) == 69);  // A# -> A
    CHECK (snapToScale (71, 9, 5) == 72);  // B -> C (one up beats two down)
    lynx::Pattern p;
    p.notes = { p2test::note (0, 61), p2test::note (0, 66) };
    snapNotesToScale (p, { 0, 1 }, 0, 1);
    CHECK (p.notes[0].key == 60 && p.notes[1].key == 65);
}

TEST (p2_strum)
{
    using namespace lynx::notetools;
    lynx::Pattern p;
    p.notes = { p2test::note (0, 64, 960), p2test::note (0, 60, 960), p2test::note (0, 67, 960), p2test::note (960, 72, 480, 0) };
    StrumOptions o;
    o.spread = 30;
    o.velocityStep = 0.05f;
    strum (p, p2test::all (p), o);
    // low to high: 60 at 0, 64 at 30, 67 at 60; the ends stay at 960
    CHECK (p.notes[1].start == 0 && p.notes[0].start == 30 && p.notes[2].start == 60);
    CHECK (p.notes[0].start + p.notes[0].length == 960 && p.notes[2].length == 900);
    CHECK (std::abs (p.notes[2].velocity - 0.88f) < 1e-5f);
    CHECK (p.notes[3].start == 960); // a single note is not a chord

    lynx::Pattern q;
    q.notes = { p2test::note (0, 60), p2test::note (0, 64), p2test::note (480, 60), p2test::note (480, 64) };
    o.alternate = true;
    o.velocityStep = 0.0f;
    o.keepEnds = false;
    strum (q, p2test::all (q), o);
    CHECK (q.notes[0].start == 0 && q.notes[1].start == 30);     // first chord: up
    CHECK (q.notes[3].start == 480 && q.notes[2].start == 510);  // second chord: down
    CHECK (q.notes[2].length == lynx::ticksPerStep);             // lengths kept
}

TEST (p2_arpeggiate)
{
    using namespace lynx::notetools;
    lynx::Pattern p;
    p.notes = { p2test::note (0, 60, 960), p2test::note (0, 64, 960), p2test::note (0, 67, 960) };
    ArpeggiateOptions o;
    o.step = 240;
    o.gate = 0.5f;
    auto added = arpeggiate (p, p2test::all (p), o);
    CHECK (added.size() == 4 && p.notes.size() == 4);
    CHECK (p.notes[0].key == 60 && p.notes[1].key == 64 && p.notes[2].key == 67 && p.notes[3].key == 60);
    CHECK (p.notes[3].start == 720 && p.notes[0].length == 120);

    lynx::Pattern q;
    q.notes = { p2test::note (0, 60, 960), p2test::note (0, 64, 960) };
    o.direction = ArpeggiateOptions::upDown;
    o.octaves = 2;
    o.step = 120;
    arpeggiate (q, p2test::all (q), o);
    // run 60 64 72 76, then back down 72 64
    std::vector<int> keys;
    for (const auto& n : q.notes)
        keys.push_back (n.key);
    CHECK ((keys == std::vector<int> { 60, 64, 72, 76, 72, 64, 60, 64 }));

    lynx::Pattern single;
    single.notes = { p2test::note (0, 60, 960) };
    o.octaves = 1;
    CHECK (arpeggiate (single, { 0 }, o).empty() && single.notes.size() == 1); // nothing to arpeggiate
}

TEST (p2_flip)
{
    using namespace lynx::notetools;
    lynx::Pattern p;
    p.notes = { p2test::note (0, 60, 240), p2test::note (480, 67, 480) }; // span 0..960, keys 60..67
    flip (p, { 0, 1 }, true, false);
    CHECK (p.notes[0].start == 720 && p.notes[1].start == 0);
    flip (p, { 0, 1 }, false, true);
    CHECK (p.notes[0].key == 67 && p.notes[1].key == 60);
}

TEST (p2_randomise_is_deterministic_and_bounded)
{
    using namespace lynx::notetools;
    lynx::Pattern a;
    for (int i = 0; i < 32; ++i)
        a.notes.push_back (p2test::note (i * 240, 60, 240, 0, 0.5f));
    auto b = a;
    RandomiseOptions o;
    o.velocity = 0.2f;
    o.timing = 20;
    o.seed = 7;
    randomise (a, p2test::all (a), o);
    randomise (b, p2test::all (b), o);
    bool same = true, changed = false, bounded = true;
    for (std::size_t i = 0; i < a.notes.size(); ++i)
    {
        same = same && a.notes[i].velocity == b.notes[i].velocity && a.notes[i].start == b.notes[i].start;
        changed = changed || a.notes[i].velocity != 0.5f;
        bounded = bounded && std::abs (a.notes[i].velocity - 0.5f) <= 0.2f + 1e-6f && std::abs (a.notes[i].start - (std::int64_t) i * 240) <= 20
                  && a.notes[i].start >= 0;
    }
    CHECK (same && changed && bounded);
    o.seed = 8;
    randomise (b, p2test::all (b), o);
    CHECK (b.notes[3].velocity != a.notes[3].velocity || b.notes[5].velocity != a.notes[5].velocity);

    // one-sided (FL's Bipolar off, live check 190cq): velocities only go up
    lynx::Pattern c;
    c.notes = { p2test::note (0, 60, 240, 0, 0.5f), p2test::note (240, 60, 240, 0, 0.5f), p2test::note (480, 60, 240, 0, 0.5f) };
    o.bipolar = false;
    o.timing = 0;
    randomise (c, p2test::all (c), o);
    CHECK (c.notes[0].velocity >= 0.5f && c.notes[1].velocity >= 0.5f && c.notes[2].velocity >= 0.5f);
}

TEST (p2_scale_levels_and_limit)
{
    using namespace lynx::notetools;
    lynx::Pattern p;
    p.notes = { p2test::note (0, 60, 240, 0, 0.8f), p2test::note (480, 60, 240, 0, 0.8f), p2test::note (960, 60, 240, 0, 0.8f) };
    scaleLevels (p, p2test::all (p), Level::velocity, 1.0f, 0.0f, 0.0f); // fade out
    CHECK (std::abs (p.notes[0].velocity - 0.8f) < 1e-5f && std::abs (p.notes[1].velocity - 0.4f) < 1e-5f && p.notes[2].velocity == 0.0f);
    p.notes[0].pan = 0.5f;
    scaleLevels (p, { 0 }, Level::pan, 0.5f, 0.5f, 0.0f); // pan scales around the centre
    CHECK (std::abs (p.notes[0].pan - 0.25f) < 1e-5f);

    lynx::Pattern q;
    q.notes = { p2test::note (0, 30, 240, 0, 0.1f), p2test::note (0, 100, 240, 0, 0.95f), p2test::note (0, 60, 240, 0, 0.5f) };
    LimitOptions o;
    o.lowKey = 48;
    o.highKey = 72;
    o.minVelocity = 0.2f;
    o.maxVelocity = 0.9f;
    limit (q, p2test::all (q), o);
    CHECK (q.notes[0].key == 54 && q.notes[1].key == 64 && q.notes[2].key == 60); // moved by octaves (pitch class kept)
    CHECK (q.notes[0].velocity == 0.2f && q.notes[1].velocity == 0.9f && q.notes[2].velocity == 0.5f);
    o.lowKey = 60;
    o.highKey = 64; // narrower than an octave: moved by octaves as far as needed, then clamped (FL's law, round 190bw:
                    // 54 goes one octave up to 66 and is clamped to the top)
    limit (q, p2test::all (q), o);
    CHECK (q.notes[0].key == 64 && q.notes[1].key == 64);
}

TEST (p2_euclidean_and_fill)
{
    using lynx::euclideanRhythm;
    auto text = [] (const std::vector<bool>& r) {
        std::string s;
        for (auto b : r)
            s += b ? 'x' : '.';
        return s;
    };
    CHECK (text (euclideanRhythm (3, 8)) == "x..x..x.");
    CHECK (text (euclideanRhythm (4, 16)) == "x...x...x...x...");
    CHECK (text (euclideanRhythm (5, 8)) == "x.x.xx.x");
    CHECK (text (euclideanRhythm (3, 8, 1)) == ".x..x..x");
    CHECK (text (euclideanRhythm (3, 8, -1)) == "..x..x.x");
    CHECK (text (euclideanRhythm (0, 4)) == "...." && text (euclideanRhythm (9, 4)) == "xxxx" && euclideanRhythm (2, 0).empty());

    lynx::Pattern p;
    lynx::fillEverySteps (p, 1, 4, 16);
    CHECK ((p2test::stepsOf (p, 1) == std::vector<int> { 0, 4, 8, 12 }));
    lynx::fillEverySteps (p, 1, 2, 16, 2); // adds 2, 6, 10, 14; keeps the others
    CHECK (p2test::stepsOf (p, 1).size() == 8);

    // a rhythm replaces the step notes but keeps notes off the grid and other channels
    p.notes.push_back (p2test::note (100, 60, 60, 1));
    p.notes.push_back (p2test::note (0, 60, 240, 2));
    p.sortNotes();
    lynx::setStepRhythm (p, 1, euclideanRhythm (3, 8), 16);
    CHECK ((p2test::stepsOf (p, 1) == std::vector<int> { 0, 0, 3, 6, 8, 11, 14 })); // step 0 holds the off-grid note too
    CHECK (p2test::stepsOf (p, 2).size() == 1);

    lynx::Pattern r;
    r.notes = { p2test::note (0, 60, 240, 0), p2test::note (15 * lynx::ticksPerStep, 62, 240, 0), p2test::note (0, 60, 240, 1) };
    lynx::rotateSteps (r, 0, 1, 16);
    CHECK ((p2test::stepsOf (r, 0) == std::vector<int> { 0, 1 }) && p2test::stepsOf (r, 1) == std::vector<int> { 0 });
    lynx::rotateSteps (r, 0, -2, 16);
    CHECK ((p2test::stepsOf (r, 0) == std::vector<int> { 14, 15 }));
    CHECK (lynx::notesInStep (r, 0, 14).size() == 1 && lynx::notesInStep (r, 0, 3).empty());
}

TEST (p2_pattern_clone_and_move)
{
    auto song = lynx::Song::makeDefault();
    song.patterns.push_back ({});
    song.patterns[1].name = "B";
    lynx::Clip c0, c1;
    c0.pattern = 0;
    c1.pattern = 1;
    song.playlist = { c0, c1 };
    const auto clone = lynx::clonePattern (song, 0);
    CHECK (clone == 1 && song.patterns.size() == 3 && song.currentPattern == 1);
    CHECK (song.patterns[1].notes.size() == song.patterns[0].notes.size() && song.patterns[1].name == song.patterns[0].name + " #2");
    CHECK (song.playlist[0].pattern == 0 && song.playlist[1].pattern == 2); // "B" moved down one
    CHECK (lynx::clonePattern (song, 0) == 1 && song.patterns[1].name == song.patterns[0].name + " #3");

    CHECK (lynx::movePattern (song, 3, -1)); // "B" up
    CHECK (song.patterns[2].name == "B" && song.playlist[1].pattern == 2);
    CHECK (! lynx::movePattern (song, 0, -1) && ! lynx::movePattern (song, 3, 1));
    song.currentPattern = 0;
    CHECK (lynx::movePattern (song, 0, 1) && song.currentPattern == 1 && song.playlist[0].pattern == 1);
}

TEST (p2_channel_sort_and_unused)
{
    auto song = lynx::Song::makeDefault();
    song.channels.resize (4);
    song.channels[0].name = "kick";
    song.channels[1].name = "Bass";
    song.channels[2].name = "clap";
    song.channels[3].name = "Arp";
    song.channels[0].mixerTrack = 3;
    song.channels[1].mixerTrack = 1;
    song.channels[2].mixerTrack = 0;
    song.channels[3].mixerTrack = 2;
    CHECK ((lynx::channelSortOrder (song, lynx::ChannelSort::name) == std::vector<int> { 3, 1, 2, 0 }));
    CHECK ((lynx::channelSortOrder (song, lynx::ChannelSort::insert) == std::vector<int> { 1, 3, 0, 2 })); // Master last
    song.channels[0].colour = 0xff3d8fd1u; // blue
    song.channels[1].colour = 0xffd94a4fu; // red
    song.channels[2].colour = 0;
    song.channels[3].colour = 0xff3fae6au; // green
    CHECK ((lynx::channelSortOrder (song, lynx::ChannelSort::colour) == std::vector<int> { 1, 3, 0, 2 }));

    for (auto& p : song.patterns)
        p.notes.clear();
    song.patterns[0].notes = { p2test::note (0, 60, 240, 1) };
    lynx::Clip audio;
    audio.kind = lynx::ClipKind::audio;
    audio.channel = 3;
    song.playlist = { audio };
    CHECK ((lynx::unusedChannels (song) == std::vector<int> { 0, 2 }));
}

TEST (p2_mixer_swap_and_move)
{
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (4);
    song.mixer[1].name = "one";
    song.mixer[2].name = "two";
    song.channels.resize (2);
    song.channels[0].mixerTrack = 1;
    song.channels[1].mixerTrack = 2;
    song.setRoute (1, 3, true, 0.5f);
    lynx::EffectSlot comp;
    comp.kind = lynx::EffectKind::compressor;
    comp.sidechain = 1;
    song.mixer[4].effects = { comp };
    lynx::Automation a;
    a.target = { lynx::AutomationTargetKind::mixerVolume, 2 };
    song.automations = { a };

    lynx::mixerops::swapTracks (song, 1, 2);
    CHECK (song.mixer[1].name == "two" && song.mixer[2].name == "one");
    CHECK (song.channels[0].mixerTrack == 2 && song.channels[1].mixerTrack == 1);
    CHECK (song.routes (2, 3) && ! song.routes (1, 3) && song.mixer[4].effects[0].sidechain == 2);
    CHECK (song.automations[0].target.index == 1);

    std::vector<int> selection { 2, 3 };
    CHECK (lynx::mixerops::moveTracks (song, selection, -1) && (selection == std::vector<int> { 1, 2 }));
    CHECK (song.mixer[1].name == "one" && song.mixer[3].name == "two");
    CHECK (! lynx::mixerops::moveTracks (song, selection, -1)); // the Master never moves
    selection = { 0 };
    CHECK (! lynx::mixerops::moveTracks (song, selection, 1));
}

TEST (p2_mixer_link_route_sidechain)
{
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (3);
    song.channels.resize (3);
    auto inserts = lynx::mixerops::linkChannels (song, { 0, 1, 2 }, 2, true);
    CHECK ((inserts == std::vector<int> { 2, 3, 4 }) && song.mixer.size() == 5); // one insert added
    inserts = lynx::mixerops::linkChannels (song, { 0, 2 }, 1, false);
    CHECK ((inserts == std::vector<int> { 1, 1 }) && (lynx::mixerops::channelsOn (song, 1) == std::vector<int> { 0, 2 }));

    CHECK (lynx::mixerops::routeTo (song, { 2, 3 }, 4, false) == 2 && song.routes (2, 4) && song.routes (2, 0));
    CHECK (lynx::mixerops::routeTo (song, { 2 }, 1, true) == 1 && song.routes (2, 1) && ! song.routes (2, 4) && ! song.routes (2, 0));
    CHECK (lynx::mixerops::routeTo (song, { 4 }, 3, false) == 0); // 3 -> 4 already: a cycle

    lynx::EffectSlot comp;
    comp.kind = lynx::EffectKind::compressor;
    song.mixer[1].effects = { comp };
    CHECK (lynx::mixerops::sidechainTo (song, 3, 1) == 1 && song.mixer[1].effects[0].sidechain == 3);
    CHECK (song.routes (3, 1) && song.mixer[3].sends.back().level == 0.0f);
    const auto order = song.mixerOrder();
    CHECK (std::find (order.begin(), order.end(), 3) < std::find (order.begin(), order.end(), 1));
}

TEST (p2_effect_slot_move_and_switch)
{
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (2);
    for (auto kind : { lynx::EffectKind::eq, lynx::EffectKind::delay, lynx::EffectKind::reverb })
    {
        lynx::EffectSlot s;
        s.kind = kind;
        song.mixer[1].effects.push_back (s);
    }
    lynx::Automation a;
    a.target = { lynx::AutomationTargetKind::effectParam, 1, 0, 3 };
    song.automations = { a };
    CHECK (lynx::mixerops::moveEffectSlot (song, 1, 0, 2));
    CHECK (song.mixer[1].effects[0].kind == lynx::EffectKind::delay && song.mixer[1].effects[2].kind == lynx::EffectKind::eq);
    CHECK (song.automations[0].target.slot == 2);
    CHECK (! lynx::mixerops::moveEffectSlot (song, 1, 0, 5));
    lynx::mixerops::setAllEffectsEnabled (song, 1, false);
    CHECK (! lynx::mixerops::anyEffectEnabled (song, 1));
    lynx::mixerops::setAllEffectsEnabled (song, 1, true);
    CHECK (lynx::mixerops::anyEffectEnabled (song, 1));
}

TEST (p2_history_labels)
{
    const auto before = lynx::Song::makeDefault();
    auto after = before;
    after.patterns[0].notes.push_back (p2test::note (0, 60));
    CHECK (lynx::describeChangeText (before, after) == "Add notes: " + before.patterns[0].name);
    after = before;
    after.patterns[0].notes[0].velocity = 0.1f;
    CHECK (lynx::describeChangeText (before, after) == "Edit notes: " + before.patterns[0].name);
    after = before;
    after.channels[1].muted = true;
    CHECK (lynx::describeChangeText (before, after) == "Mute: " + before.channels[1].name);
    after = before;
    after.channels[1].volume = 0.2f;
    CHECK (lynx::describeChangeText (before, after) == "Channel level: " + before.channels[1].name);
    after = before;
    after.channels.pop_back();
    CHECK (std::string (lynx::describeChange (before, after).text) == "Delete channel");
    after = before;
    after.bpm = 99.0;
    CHECK (lynx::describeChangeText (before, after) == "Tempo");
    after = before;
    after.mixer[1].volume = 0.3f;
    CHECK (lynx::describeChangeText (before, after) == "Mixer level: " + before.mixer[1].name);
    after = before;
    after.playlist.push_back ({});
    CHECK (lynx::describeChangeText (before, after) == "Add clips");
    CHECK (lynx::describeChangeText (before, before) == "Edit");
}

TEST (p2_browser_search)
{
    using lynx::search::matches;
    CHECK (matches ("", "anything.wav"));
    CHECK (matches ("kick", "Big KICK 01.wav") && ! matches ("kick", "snare.wav"));
    CHECK (matches ("kick 808", "808 Kick.wav") && ! matches ("kick 808", "909 Kick.wav"));
    CHECK (matches ("kick AND 808", "kick_808.wav"));
    CHECK (matches ("kick OR snare", "snare.wav") && matches ("kick OR snare", "kick.wav") && ! matches ("kick OR snare", "hat.wav"));
    CHECK (! matches ("kick -loop", "kick loop.wav") && matches ("kick -loop", "kick one.wav"));
    CHECK (! matches ("kick NOT loop", "kick loop.wav"));
    CHECK (matches ("\"808 kick\"", "my 808 kick.wav") && ! matches ("\"808 kick\"", "kick 808.wav"));
    CHECK (matches ("k*k.wav", "kick.wav") && ! matches ("k*k.wav", "kick.flac") && matches ("h?t*", "hat 01.wav"));
    CHECK (lynx::search::wildcardMatch ("*", "") && ! lynx::search::wildcardMatch ("?", ""));
}

TEST (p2_record_commit_take)
{
    lynx::Pattern p;
    p.notes = { p2test::note (0, 60, 240, 0), p2test::note (0, 60, 240, 1) };
    std::vector<int> cleared;
    // overdub with record quantise to steps
    lynx::commitTake (p, { p2test::note (250, 62, 100, 0), p2test::note (130, 64, 100, 0) }, lynx::RecordMode::overdub, lynx::ticksPerStep, cleared);
    CHECK (p.notes.size() == 4 && p.notes[2].start == 240 && p.notes[3].start == 240);
    CHECK (p.notes[0].start == 0 && cleared.empty());

    // replace: the channel's old notes go once, notes from later in the same take stay
    lynx::commitTake (p, { p2test::note (480, 65, 100, 0) }, lynx::RecordMode::replace, 1, cleared);
    CHECK (p.notes.size() == 2 && (cleared == std::vector<int> { 0 }));
    lynx::commitTake (p, { p2test::note (720, 67, 100, 0) }, lynx::RecordMode::replace, 1, cleared);
    CHECK (p.notes.size() == 3 && p2test::stepsOf (p, 1).size() == 1);
}

TEST (p2_record_song_mode_clip_and_count_in)
{
    auto song = lynx::Song::makeDefault();
    song.playlist.clear();
    const auto bar = (std::int64_t) lynx::ppq * song.beatsPerBar;
    CHECK (lynx::placePatternClipAt (song, 0, bar + 100));
    CHECK (song.playlist.size() == 1 && song.playlist[0].start == bar && song.playlist[0].track == 0);
    CHECK (! lynx::placePatternClipAt (song, 0, bar + 200)); // already covered
    song.patterns.emplace_back().name = "Other";
    lynx::Clip other;
    other.pattern = 1;
    other.start = 3 * bar;
    other.length = bar;
    song.playlist.push_back (other);
    CHECK (lynx::placePatternClipAt (song, 0, 3 * bar));
    CHECK (song.playlist.back().track == 1); // track 0 is busy there
    CHECK (lynx::countInLength (song, 2) == 2 * bar && lynx::countInLength (song, 0) == 0);
}

TEST (p2_ripple_and_time_range)
{
    std::vector<lynx::Clip> clips (4);
    clips[0] = { 0, 0, 0, 100 };
    clips[1] = { 0, 0, 100, 50 };
    clips[2] = { 0, 0, 200, 50 };
    clips[3] = { 0, 1, 120, 50 }; // another track
    lynx::noteedit::rippleDelete (clips, { 1 });
    CHECK (clips.size() == 3 && clips[1].start == 150 && clips[2].start == 120);

    lynx::noteedit::rippleResize (clips, 0, 140);
    CHECK (clips[0].length == 140 && clips[1].start == 190 && clips[2].start == 120);

    std::vector<lynx::Clip> t (2);
    t[0] = { 0, 0, 0, 400 };
    t[1] = { 0, 1, 150, 50 };
    const auto moved = lynx::noteedit::moveTimeRange (t, 100, 300, 500, false);
    // clip 0 was cut at 100 and 300; its middle part and clip 1 moved by 500
    CHECK (t.size() == 4 && moved.size() == 2);
    CHECK (t[0].start == 0 && t[0].length == 100);
    bool middle = false, tail = false;
    for (const auto& c : t)
    {
        middle = middle || (c.track == 0 && c.start == 600 && c.length == 200 && c.offset == 100);
        tail = tail || (c.track == 0 && c.start == 300 && c.length == 100 && c.offset == 300);
    }
    CHECK (middle && tail && t[1].start == 650);

    std::vector<lynx::Clip> u (1);
    u[0] = { 0, 0, 100, 100 };
    const auto copies = lynx::noteedit::moveTimeRange (u, 0, 400, -1000, true);
    CHECK (copies.size() == 1 && u.size() == 2 && u[1].start == 100 && u[0].start == 100); // clamped: the selection starts at 0
}

TEST (p2_key_map_bindings)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ f8, none }, Context::global, false) == "view.pluginPicker"); // F8 = FL's Plugin picker since round 169k
    CHECK (map.find ({ numpad3, none }, Context::playlist, true) == "pattern.select3");
    CHECK (map.find ({ 'C', alt }, Context::playlist, false) == "pattern.clone");
    CHECK (map.find ({ 'C', alt }, Context::pianoRoll, false) == "pianoRoll.colourSelected"); // FL's piano roll: Alt+C = colour (round 153)
    CHECK (map.find ({ 'C', alt }, Context::channelRack, false) == "rack.clone"); // the rack wins while focused
    CHECK (map.find ({ 'A', alt }, Context::pianoRoll, false) == "pianoRoll.arpeggiate");
    CHECK (map.find ({ '4', shift }, Context::playlist, true) == "playlist.zoomAll");
    CHECK (map.find ({ left, alt }, Context::mixer, false) == "mixer.moveLeft");
    CHECK (map.find ({ 'L', ctrl | shift }, Context::mixer, false) == "mixer.linkChannelsFrom");
    CHECK (map.find ({ 'I', ctrl }, Context::global, false) == "record.waitForInput");
    CHECK (toString ({ numpad7, none }) == "Numpad 7" && (parseChord ("Numpad 7") == Chord { numpad7, none }));
}

TEST (p2_playlist_tracks_and_chop)
{
    auto song = lynx::Song::makeDefault();
    song.playlist.clear();
    lynx::Clip a, b, c;
    a.track = 0; a.length = 100;
    b.track = 1; b.length = 100;
    c.track = 2; c.length = 100;
    song.playlist = { a, b, c };
    song.playlistTracks.resize (2);
    song.playlistTracks[1].name = "B";

    lynx::noteedit::insertTrack (song, 1);
    CHECK (song.playlist[0].track == 0 && song.playlist[1].track == 2 && song.playlist[2].track == 3);
    CHECK (song.playlistTracks[2].name == "B" && song.playlistTracks[1].name.empty());

    lynx::noteedit::swapTracks (song, 2, 3);
    CHECK (song.playlist[1].track == 3 && song.playlist[2].track == 2 && song.playlistTracks[3].name == "B");

    lynx::noteedit::removeTrack (song, 2);
    CHECK (song.playlist.size() == 2 && song.playlist[1].track == 2 && song.playlistTracks[2].name == "B");

    lynx::noteedit::soloTrack (song, 1, 4);
    CHECK (! song.playlistTracks[1].muted && song.playlistTracks[0].muted && song.playlistTracks[3].muted);
    lynx::noteedit::soloTrack (song, 1, 4); // again: everything plays
    CHECK (! song.playlistTracks[0].muted && ! song.playlistTracks[3].muted);

    std::vector<lynx::Clip> clips (1);
    clips[0].start = 100;
    clips[0].length = 1000;
    const auto pieces = lynx::noteedit::chopClip (clips, 0, 480);
    CHECK (pieces.size() == 3 && clips[0].length == 380 && clips[1].start == 480 && clips[1].length == 480 && clips[2].start == 960
           && clips[2].length == 140 && clips[2].offset == 860);

    lynx::Pattern steps, roll;
    steps.notes = { p2test::note (0, 60), p2test::note (480, 60) };
    roll.notes = { p2test::note (0, 64) };
    CHECK (! lynx::hasPianoRollNotes (steps) && lynx::hasPianoRollNotes (roll));
}
