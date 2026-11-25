// --- REA round 143 (part A): FL's piano roll Tools menu, Playlist track / clip menus, mixer track menu ---------

#include "core/ClipOps.h"
#include "core/MixerOps.h"
#include "core/NoteTools.h"

TEST (rea143_piano_roll_tools_have_fls_keys)
{
    using namespace lynx::keys;
    KeyMap map;
    // FL's Tools menu keys (EXEC capture of FL 2026's piano roll): Alt+Q quantize, Alt+U chop, Alt+F flam, Alt+W claw,
    // Alt+O LFO, Alt+L articulate, Alt+E riff machine, Alt+P chord progression (Alt+A/S/Y/R/X/K were there already)
    const std::pair<int, const char*> keys[] = { { 'Q', "pianoRoll.quantize" }, { 'U', "pianoRoll.chop" }, { 'F', "pianoRoll.flam" },
                                                 { 'W', "pianoRoll.claw" }, { 'O', "pianoRoll.lfo" }, { 'L', "pianoRoll.articulate" },
                                                 { 'E', "pianoRoll.riffMachine" }, { 'P', "pianoRoll.chordProgression" },
                                                 { 'A', "pianoRoll.arpeggiate" }, { 'K', "pianoRoll.limit" } };
    for (const auto& [key, id] : keys)
        CHECK (map.find ({ key, alt }, Context::pianoRoll, false) == id);
    CHECK (map.find ({ 'L', alt }, Context::mixer, false) == "mixer.selectLinkedChannels"); // other contexts keep theirs
    CHECK (map.conflicts().empty());
}

TEST (rea143_euclidean_humanize_repeat_and_select_by_velocity)
{
    using namespace lynx::notetools;
    // the Euclidean rhythm: E(3,8) = x..x..x. ; E(5,16) has 5 hits; rotation moves the pattern
    std::string e38;
    for (int s = 0; s < 8; ++s)
        e38 += euclideanHit (8, 3, 0, s) ? 'x' : '.';
    CHECK (e38 == "x..x..x.");
    int hits = 0;
    for (int s = 0; s < 16; ++s)
        hits += euclideanHit (16, 5, 0, s) ? 1 : 0;
    CHECK (hits == 5 && ! euclideanHit (8, 0, 0, 0) && euclideanHit (4, 9, 0, 3)); // no pulses: silence; pulses > steps: every step
    CHECK (euclideanHit (8, 3, 1, 2) && ! euclideanHit (8, 3, 1, 0));

    lynx::Pattern p;
    lynx::Note old;
    old.channel = 2;
    old.key = 38;
    p.notes.push_back (old);
    EuclideanOptions o;
    o.steps = 8;
    o.pulses = 3;
    o.stepTicks = lynx::ticksPerStep;
    o.length = 16 * lynx::ticksPerStep; // two cycles
    o.channel = 2;
    o.key = 38;
    o.accent = 0.2f;
    const auto made = euclidean (p, { 0 }, o);
    CHECK (made.size() == 6 && p.notes.size() == 6); // the replaced note is gone
    CHECK (p.notes[0].start == 0 && p.notes[1].start == 3 * lynx::ticksPerStep && p.notes[3].start == 8 * lynx::ticksPerStep);
    CHECK (p.notes[0].velocity > p.notes[1].velocity && p.notes[3].velocity > p.notes[4].velocity); // accent per cycle
    CHECK (p.notes[2].key == 38 && p.notes[2].channel == 2 && p.notes[2].length == lynx::ticksPerStep);

    // humanize: a chord moves together, never before 0, velocities stay in range; the same seed gives the same result
    lynx::Pattern h;
    for (int k : { 60, 64, 67 })
    {
        lynx::Note n;
        n.start = lynx::ppq;
        n.key = k;
        h.notes.push_back (n);
    }
    auto h2 = h;
    HumanizeOptions ho;
    ho.timing = 40;
    ho.velocity = 0.3f;
    humanize (h, { 0, 1, 2 }, ho);
    humanize (h2, { 0, 1, 2 }, ho);
    CHECK (h.notes[0].start == h.notes[1].start && h.notes[1].start == h.notes[2].start);
    CHECK (std::abs (h.notes[0].start - lynx::ppq) <= 40 && h.notes[0].start == h2.notes[0].start && h.notes[2].velocity == h2.notes[2].velocity);

    // note repeater: a beat-long note at 1/16 becomes four, each softer
    lynx::Pattern r;
    lynx::Note n;
    n.length = lynx::ppq;
    n.velocity = 1.0f;
    r.notes.push_back (n);
    RepeatOptions ro;
    ro.rate = lynx::ppq / 4;
    ro.decay = 0.5f;
    ro.pitchStep = 1;
    const auto all = repeatNotes (r, { 0 }, ro);
    CHECK (all.size() == 4 && r.notes.size() == 4 && r.notes[0].length == lynx::ppq / 8);
    CHECK (r.notes[3].start == 3 * lynx::ppq / 4 && std::abs (r.notes[3].velocity - 0.125f) < 1.0e-6f && r.notes[3].key == 63);

    // select by velocity
    lynx::Pattern v;
    for (float vel : { 0.1f, 0.4f, 0.6f, 0.9f })
    {
        lynx::Note x;
        x.velocity = vel;
        v.notes.push_back (x);
    }
    CHECK ((selectByVelocity (v, { 0, 1, 2, 3 }, 0.5f, 1.0f) == std::vector<int> { 2, 3 }));
    CHECK ((selectLoudest (v, { 0, 1, 2, 3 }, 0.25f, true) == std::vector<int> { 3 }));
    CHECK ((selectLoudest (v, { 0, 1, 2, 3 }, 0.5f, false) == std::vector<int> { 0, 1 }));
    CHECK (selectLoudest (v, {}, 0.5f, true).empty());
}

TEST (rea143_playlist_track_merge_and_clip_source)
{
    using namespace lynx;
    Song s;
    s.patterns.resize (2);
    auto clip = [&s] (int track, std::int64_t start, std::int64_t length, std::int64_t offset, int pattern) {
        Clip c;
        c.track = track;
        c.start = start;
        c.length = length;
        c.offset = offset;
        c.pattern = pattern;
        s.playlist.push_back (c);
    };
    // track 0: three pieces of pattern 0 that continue each other, then a gap, then two more that continue
    clip (0, 0, 100, 0, 0);
    clip (0, 100, 100, 100, 0);
    clip (0, 200, 50, 200, 0);
    clip (0, 400, 100, 0, 0);
    clip (0, 500, 100, 100, 0);
    clip (1, 0, 100, 0, 0); // another track: untouched
    CHECK (clipops::mergeTrackClips (s, 0) == 3);
    int onTrack = 0;
    std::int64_t longest = 0;
    for (const auto& c : s.playlist)
        if (c.track == 0)
        {
            ++onTrack;
            longest = std::max (longest, c.length);
        }
    CHECK (onTrack == 2 && longest == 250 && s.playlist.size() == 3);
    CHECK (clipops::mergeTrackClips (s, 0) == 0);

    // "Select source pattern": the chosen clips play pattern 1; other kinds are left alone
    clipops::setClipSource (s, { 0, 1 }, ClipKind::pattern, 1);
    CHECK (s.playlist[0].pattern == 1 && s.playlist[1].pattern == 1);
    clipops::setClipSource (s, { 0 }, ClipKind::pattern, 7); // no such pattern: unchanged
    CHECK (s.playlist[0].pattern == 1);
}

TEST (rea143_mixer_track_menu_operations)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (8);
    auto& a = s.mixer[2];
    a.volume = 0.3f;
    a.pan = -0.5f;
    a.stereo = 0.7f;
    a.invertPhase = true;
    a.muted = true;
    a.eq.gainDb[0] = 6.0f;
    a.name = "Drums";
    s.setRoute (2, 3, true);
    s.setRoute (4, 3, true);
    s.setRoute (5, 2, true);
    a.effects.resize (2);
    a.effects[1].row = 4;
    a.chainBlend = 0.5f;
    a.colour = 0xffd03030;
    a.slotRows = 14;
    // "Reset selected track(s) to default": the controls, the effects (FL's Default.fst), the name and the colour
    mixerops::resetToDefault (s, { 2 });
    CHECK (a.volume == unityFader && a.pan == 0.0f && a.stereo == 0.0f && ! a.invertPhase && ! a.muted && a.eq == TrackEq {});
    CHECK (a.effects.empty() && a.chainBlend == 1.0f);
    // the name and colour too (FL 2026: "HI TOM" became "Insert 3", red the default colour), not the routing
    CHECK (a.name.empty() && a.colour == 0 && a.slotRows == 0 && s.routes (2, 3));
    // the "Select" submenu's lists
    CHECK ((mixerops::destinationsOf (s, 2) == std::vector<int> { 0, 3 }));
    CHECK ((mixerops::sourcesOf (s, 3) == std::vector<int> { 2, 4 }));
    CHECK ((mixerops::sourcesOf (s, 2) == std::vector<int> { 5 }));
    // "Unroute tracks routed to the selected track(s)"
    CHECK (mixerops::unrouteInto (s, { 3 }) == 2 && ! s.routes (2, 3) && ! s.routes (4, 3) && s.routes (2, 0) && s.routes (5, 2));
    // "Reset selected track(s) routing": only to the Master
    s.mixer[5].toMaster = false;
    s.mixer[5].masterLevel = 0.5f;
    mixerops::resetRouting (s, { 5, 0 });
    CHECK (s.mixer[5].sends.empty() && s.mixer[5].toMaster && s.mixer[5].masterLevel == 1.0f);
    // "Insert multiple...": blank inserts at the place, the rest moves right (the reset above cleared insert 2's name)
    s.mixer[2].name = "Drums";
    const auto before = s.mixer.size();
    CHECK (mixerops::insertTracks (s, 2, 3) == 3 && s.mixer.size() == before + 3 && s.mixer[5].name == "Drums");
}
