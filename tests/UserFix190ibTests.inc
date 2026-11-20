// Round 190ib: user reports #165 (follow-up), #170 (rework), #181, #182, #183.
#include "core/EditorGestures.h"
#include "core/SoundFont.h"
#include "core/TypeAhead.h"

// #165 follow-up (karatmep232 on 0.1.28: "the pitch bend note doesn't do like the bending on the same note"). The guitar of
// his rude buster v4.flp is an FL SoundFont Player channel. LYNX Soundfont queued note-ons until its render() but applied
// slides at once, so a slide note on the step of a note (the case 46a8b6a61 fixed in the scheduler) still found no voice
// on that channel type and the note stayed flat.
TEST (userfix190ib_soundfont_note_glides_to_a_slide_note_on_its_own_step)
{
    using namespace lynx;
    const auto folder = std::filesystem::temp_directory_path() / ("lynx-190ib-sf-" + processTempTag());
    std::filesystem::create_directories (folder);
    const auto path = (folder / "font.sf2").string();
    {
        const auto bytes = sf::makeTestFont ({ { "Tone", 0, 0 } }, 1);
        std::ofstream out (path, std::ios::binary);
        out.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    }
    const auto keep = sf::FontLibrary::instance().getNow (path);
    flp::Project project;
    flp::Channel fc;
    fc.index = 0;
    fc.type = flp::ChannelType::instrument;
    fc.name = "084 (guitar)";
    fc.pluginName = "Fruity soundfont player";
    fc.pluginState = i173a::flSoundfontState6 ({ 1, 0, 0, 0, 0, -1, -1, -1, -1, -1, -128, -1, -1 }, path);
    project.channels = { fc };
    const auto c = importFlp (project, {}).channels.at (0);
    CHECK (c.instrument == InstrumentKind::soundfont);

    Note held;
    held.key = 60;
    held.length = ppq * 8;
    held.velocity = 0.8f;
    Note slide;
    slide.key = 62;
    slide.length = ppq / 4;
    slide.slide = true;
    const auto base = issue14::pitchAt (issue14::renderPattern (c, { held }, 0.0f, 1.5), 1.0);
    const auto slid = issue14::pitchAt (issue14::renderPattern (c, { slide, held }, 0.0f, 1.5), 1.0);
    std::printf ("  soundfont same-step slide: %.2f Hz -> %.2f Hz (%.1f cents)\n", base, slid, issue14::cents (base, slid));
    CHECK (base > 0.0);
    CHECK (std::abs (issue14::cents (base, slid) - 200.0) < 15.0);
    // a slide note inside the note (the case that always worked) lands on the same pitch
    slide.start = ppq / 2;
    const auto later = issue14::pitchAt (issue14::renderPattern (c, { held, slide }, 0.0f, 1.5), 1.0);
    CHECK (std::abs (issue14::cents (base, later) - 200.0) < 15.0);
    std::filesystem::remove_all (folder);
}

// #181: Ctrl+, (Cmd+, on macOS: the map's Ctrl is the Command key there) opens Settings from every panel, beside FL's own
// F10 / F11, with no other binding on it.
TEST (userfix190ib_ctrl_comma_opens_settings_everywhere)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    for (auto focus : { Context::global, Context::channelRack, Context::pianoRoll, Context::playlist, Context::mixer, Context::browser })
    {
        CHECK (map.find ({ ',', ctrl }, focus, false) == "view.settings");
        CHECK (map.find ({ ',', ctrl }, focus, true) == "view.settings"); // (a modified key: the typing piano leaves it)
        CHECK (map.find ({ f10 }, focus, false) == "view.midiSettings"); // FL's own keys stay
        CHECK (map.find ({ f11 }, focus, false) == "view.projectInfo");
    }
    CHECK (toString ({ ',', ctrl }) == "Ctrl+,");
    CHECK (parseChord ("Ctrl+,") == (Chord { ',', ctrl }));
    CHECK (letterOfKeyCode (',') == ',');
    CHECK (map.find ({ ',' }, Context::global, false).empty()); // a plain comma keeps no binding
}

// #183: a note the mouse places or drags in the Piano roll sounds while stopped, stays silent while the song / pattern
// plays, and sounds then too with the Piano roll's "Preview notes while playing" (off by default).
TEST (userfix190ib_piano_roll_previews_edited_notes_only_when_stopped_by_default)
{
    using lynx::gesture::previewsEditedNote;
    CHECK (previewsEditedNote (false, false));
    CHECK (! previewsEditedNote (true, false));
    CHECK (previewsEditedNote (true, true));
    CHECK (previewsEditedNote (false, true));
}

// #182: type-to-jump (core/TypeAhead.h, used by menus, dropdowns and lists through JUCE fix 37 and the Browser /
// Plugin manager key handlers)
TEST (userfix190ib_type_to_jump_first_letter_prefix_and_cycling)
{
    using namespace lynx::typeahead;
    // the Playlist's Edit menu of the issue's screenshot, as JUCE hands the items over (key text after a tab, an
    // accelerator mark before a letter)
    const std::vector<std::u32string> items = { U"Shift left\tShift+Left", U"Shift right\tShift+Right", U"Allow resizing from left",
                                                U"Nudge clips with mouse wheel", U"Dropping on track header locks to content",
                                                U"Drop audio on track headers", U"Mute\tAlt+M", U"Cu\x01t", U"Delete space\tCtrl+Del" };
    const auto text = [&items] (int i) { return items[(std::size_t) i]; };
    const auto n = (int) items.size();
    State st;
    int list = 0; // (the owner: any address)
    // "D" jumps to the first item starting with D, "D" again (a pause or not) to the next one, then around
    CHECK (find (st, &list, U'D', 1000, -1, n, text) == 4);
    CHECK (find (st, &list, U'd', 1100, 4, n, text) == 5);
    CHECK (find (st, &list, U'd', 1200, 5, n, text) == 8);
    CHECK (find (st, &list, U'd', 1300, 8, n, text) == 4);
    // a pause starts a new prefix; "dr" quickly narrows to "Dropping..." from where it is, "dro" + "p" + " " + "a"
    // to "Drop audio..."
    CHECK (find (st, &list, U'd', 5000, 0, n, text) == 4);
    CHECK (find (st, &list, U'r', 5100, 4, n, text) == 4);
    CHECK (find (st, &list, U'o', 5200, 4, n, text) == 4);
    CHECK (find (st, &list, U'p', 5300, 4, n, text) == 4);
    CHECK (find (st, &list, U' ', 5400, 4, n, text) == 5);
    CHECK (find (st, &list, U'a', 5500, 5, n, text) == 5);
    // nothing starts with "dropx": no jump
    CHECK (find (st, &list, U'x', 5600, 5, n, text) == -1);
    // the accelerator mark and the key text don't count: "cut" finds "Cu&t", "mute" ignores "Alt+M"
    CHECK (find (st, &list, U'c', 9000, 0, n, text) == 7);
    CHECK (find (st, &list, U'u', 9100, 7, n, text) == 7);
    CHECK (find (st, &list, U't', 9200, 7, n, text) == 7);
    CHECK (find (st, &list, U'm', 20000, 0, n, text) == 6);
    // another list starts a new prefix at once; a space or a control key alone never jumps
    int other = 0;
    CHECK (find (st, &other, U'n', 20100, -1, n, text) == 3);
    CHECK (find (st, &list, U' ', 30000, 0, n, text) == -1);
    CHECK (find (st, &list, U'\r', 30100, 0, n, text) == -1);
    CHECK (find (st, &list, U'a', 30200, 0, 0, text) == -1); // (an empty list)
    // case and Latin-1 capitals fold
    CHECK (key (U"  \x01\xC9toile\tCtrl+E") == U"\xE9toile");
}

// #170 (yannikthegenius, 2026-11-15, three screenshots): the default project, Pattern 1 (Clap, then Snare notes added) on
// Tracks 1-4. Round 190gz had made Track 1 Clap's instrument track on Clap's insert 2 ("Clap" in the Playlist, "Insert 2"
// in the Mixer), Snare kept playing through insert 4 (still sounding with Track 1 muted) and Tracks 2-4 stayed unlinked.
namespace userfix190ib
{
inline lynx::Song yannik()
{
    using namespace lynx;
    auto song = Song::makeDefault(); // Kick 1, Clap 2, Hat 3, Snare 4, Synth 5
    song.bpm = 130.0;
    song.mode = PlayMode::song;
    auto& p = song.patterns[0];
    p.notes.clear();
    for (int step : { 0, 1, 6 })
        p.toggleStep (1, step); // Clap
    p.toggleStep (3, 10);       // Snare (added after the link)
    const auto clip = [&song] (int track, int bar) {
        Clip c;
        c.kind = ClipKind::pattern;
        c.pattern = 0;
        c.track = track;
        c.start = (std::int64_t) bar * 4 * ppq;
        c.length = 4 * ppq;
        song.playlist.push_back (c);
    };
    clip (0, 0);
    clip (1, 0);
    clip (2, 1);
    clip (3, 0);
    clip (3, 1);
    song.playlistTracks.resize (16);
    // what 0.1.28 left: Track 1 = Clap's instrument track on insert 2
    song.linkTracksToMixer = true;
    auto& t = song.playlistTracks[0];
    t.mode = PlaylistTrackMode::instrument;
    t.channel = 1;
    t.mixerTrack = 2;
    t.name = "Clap";
    return song;
}

inline float peak (const std::vector<float>& x)
{
    float p = 0.0f;
    for (auto v : x)
        p = std::max (p, std::abs (v));
    return p;
}
} // namespace userfix190ib

TEST (userfix190ib_yannik_track_1_is_insert_1_and_its_mute_silences_what_it_plays)
{
    using namespace lynx;
    auto song = userfix190ib::yannik();
    // the reported state: Track 1's M (insert 2) leaves the Snare sounding through insert 4
    CHECK (autolink::linkedInsertOf (song, 0) == 2 && song.channels[3].mixerTrack == 4);
    const auto before = trackauto190gz::render (song, 2.0);
    // the switch off and on again (the reply tells how): one confirmation, then Track N = insert N
    autolink::setOn (song, false);
    const auto plan = autolink::planOn (song);
    CHECK (plan.changesRouting() && ! plan.changesSound());
    CHECK (plan.repeats == (std::vector<std::pair<int, int>> { { 1, 0 }, { 2, 0 }, { 3, 0 } }));
    const auto routedToOne = [&plan] (int channel) {
        return std::any_of (plan.changes.begin(), plan.changes.end(), [channel] (const autolink::Change& c) {
            return c.kind == autolink::Change::Kind::routeChannel && c.channel == channel && c.to == 1 && ! c.changesSound;
        });
    };
    CHECK (routedToOne (1) && routedToOne (3));
    CHECK (autolink::setOn (song, true) == 1);
    // Track 1 is insert 1: Clap and Snare go through it, one name in the Playlist and the Mixer
    CHECK (autolink::linkedInsertOf (song, 0) == 1 && song.playlistTracks[0].mode == PlaylistTrackMode::audio);
    CHECK (song.channels[1].mixerTrack == 1 && song.channels[3].mixerTrack == 1);
    CHECK (song.playlistTracks[0].name == "Clap" && song.mixer[1].name == "Clap");
    // Tracks 2-4 only repeat Pattern 1: unassigned, and the headers say they play through Track 1's insert
    const auto repeats = autolink::repeatedTracks (song);
    CHECK (repeats[1] == 0 && repeats[2] == 0 && repeats[3] == 0 && repeats[0] == -1);
    for (int t = 1; t < 4; ++t)
        CHECK (song.playlistTracks[(std::size_t) t].mode == PlaylistTrackMode::unassigned);
    // the sound is the same (all inserts were neutral)
    const auto after = trackauto190gz::render (song, 2.0);
    float worst = 0.0f;
    for (std::size_t i = 0; i < before.size() && i < after.size(); ++i)
        worst = std::max (worst, std::abs (before[i] - after[i]));
    CHECK (userfix190ib::peak (before) > 0.05f && worst < 1.0e-5f);
    // Track 1's M (insert 1) silences Clap and Snare, everywhere Pattern 1 plays
    auto muted = song;
    muted.mixer[1].muted = true;
    CHECK (userfix190ib::peak (trackauto190gz::render (muted, 2.0)) < 1.0e-4f);
    // Track 4's own LED (FL's track mute) silences its clips only: bar 2 still plays Track 3's clip
    auto led = song;
    led.playlistTracks[3].muted = true;
    const auto ledOut = trackauto190gz::render (led, 4.0);
    CHECK (userfix190ib::peak (ledOut) > 0.05f);
    // .flp: Track 1 is FL's audio track on insert 1 with Clap and Snare routed there; the repeats stay unassigned
    flpexport::Options options;
    flpexport::Report report;
    const auto back = importFlp (flp::parseProject (flpexport::exportFlp (song, options, report)));
    // (a file keeps the tracks up to the last one that differs from a plain track: the repeats may not be written)
    const auto modeOf = [&back] (int t) { return t < (int) back.playlistTracks.size() ? back.playlistTracks[(std::size_t) t].mode : PlaylistTrackMode::unassigned; };
    CHECK (! back.playlistTracks.empty());
    if (! back.playlistTracks.empty())
    {
        CHECK (back.playlistTracks[0].mode == PlaylistTrackMode::audio && back.playlistTracks[0].mixerTrack == 1);
        CHECK (modeOf (1) == PlaylistTrackMode::unassigned && modeOf (3) == PlaylistTrackMode::unassigned);
        CHECK (back.channels.size() > 3 && back.channels[1].mixerTrack == 1 && back.channels[3].mixerTrack == 1);
        CHECK (back.mixer.size() > 1 && back.mixer[1].name == "Clap");
    }
}

TEST (userfix190ib_names_follow_the_link_both_ways)
{
    using namespace lynx;
    auto song = userfix190ib::yannik();
    autolink::setOn (song, false);
    autolink::setOn (song, true);
    auto edit = [&song] (const std::function<void (Song&)>& fn) {
        const auto before = autolink::snapshot (song);
        fn (song);
        autolink::followNewContent (song, before);
    };
    edit ([] (Song& s) { s.playlistTracks[0].name = "Drums"; });
    CHECK (song.mixer[1].name == "Drums");
    edit ([] (Song& s) { s.mixer[1].name = "Perc"; s.mixer[1].colour = 0xFF336699; });
    CHECK (song.playlistTracks[0].name == "Perc" && song.playlistTracks[0].colour == 0xFF336699u);
    // a reset track name gives the insert its default name back
    edit ([] (Song& s) { s.playlistTracks[0].name.clear(); });
    CHECK (song.mixer[1].name == "Insert 1");
    // switched off: names are left alone
    autolink::setOn (song, false);
    edit ([] (Song& s) { s.playlistTracks[0].name = "Off"; });
    CHECK (song.mixer[1].name == "Insert 1");
}

TEST (userfix190ib_routing_that_would_change_the_sound_is_listed_and_new_content_skips_it)
{
    using namespace lynx;
    trackauto190gz::Setup s;
    // Kick (Track 1) and Hat (Track 9) share insert 7, which carries an EQ
    s.song.mixer[7].effects.push_back (makeEffectSlot (EffectKind::parametricEq));
    s.song.channels[0].mixerTrack = 7;
    s.song.channels[6].mixerTrack = 7;
    auto& hat = s.song.patterns.emplace_back();
    hat.name = "Hat";
    hat.toggleStep (6, 2);
    s.clip (8, ClipKind::pattern, (int) s.song.patterns.size() - 1);
    const auto plan = autolink::planOn (s.song);
    CHECK (plan.changesSound());
    CHECK (std::any_of (plan.changes.begin(), plan.changes.end(), [] (const autolink::Change& c) {
        return c.kind == autolink::Change::Kind::routeChannel && c.channel == 0 && c.from == 7 && c.to == 1 && c.changesSound;
    }));
    autolink::setOn (s.song, true);
    // Kick left the shared EQ insert for insert 1; Hat, alone on it then, keeps it: the EQ insert moves to 9
    CHECK (s.song.channels[0].mixerTrack == 1 && s.song.channels[6].mixerTrack == 9 && s.song.mixer[9].effects.size() == 1);

    // new content never moves a channel off an insert with effects that another track's instrument shares
    trackauto190gz::Setup t;
    autolink::setOn (t.song, true);
    t.song.mixer[14].effects.push_back (makeEffectSlot (EffectKind::parametricEq));
    const auto synth = [&t] (const char* name) {
        ChannelDef c;
        c.name = name;
        c.instrument = InstrumentKind::synth;
        c.mixerTrack = 14;
        t.song.channels.push_back (c);
        auto& p = t.song.patterns.emplace_back();
        p.name = name;
        p.toggleStep ((int) t.song.channels.size() - 1, 0);
        return (int) t.song.patterns.size() - 1;
    };
    t.clip (13, ClipKind::pattern, synth ("Pluck")); // (Pluck lives on Track 14, through the EQ insert)
    const auto bass = synth ("Bass");
    const auto before = autolink::snapshot (t.song);
    t.clip (11, ClipKind::pattern, bass);
    CHECK (autolink::followNewContent (t.song, before) == 0);
    CHECK (t.song.channels.back().mixerTrack == 14 && autolink::linkedInsertOf (t.song, 11) == -1);
}

TEST (userfix190ib_an_insert_with_settings_of_its_own_sends_into_the_track_s_insert)
{
    using namespace lynx;
    trackauto190gz::Setup s;
    // Track 1 plays Kick (Master) and Hat on insert 7 (an EQ only Hat uses): insert 7 sends into insert 1
    s.song.mixer[7].effects.push_back (makeEffectSlot (EffectKind::parametricEq));
    s.song.channels[6].mixerTrack = 7;
    auto& p = s.song.patterns[0];
    p.toggleStep (6, 2);
    s.song.playlist.erase (std::remove_if (s.song.playlist.begin(), s.song.playlist.end(),
                                           [] (const Clip& c) { return c.kind == ClipKind::automation; }),
                           s.song.playlist.end());
    s.song.automations.clear();
    const auto before = trackauto190gz::render (s.song);
    autolink::setOn (s.song, true);
    CHECK (s.song.channels[0].mixerTrack == 1 && s.song.channels[6].mixerTrack == 7);
    CHECK (! s.song.mixer[7].toMaster && s.song.routes (7, 1));
    const auto after = trackauto190gz::render (s.song);
    float worst = 0.0f;
    for (std::size_t i = 0; i < before.size() && i < after.size(); ++i)
        worst = std::max (worst, std::abs (before[i] - after[i]));
    std::printf ("  bus: largest difference %.3g\n", worst);
    CHECK (worst < 1.0e-5f);
}

// #170, also reported: "Link ... in new projects" was saved but read back as off at the next start
TEST (userfix190ib_new_projects_link_setting_is_read_back)
{
    using namespace lynx;
    NewProjectDefaults d;
    d.linkTracks = true;
    NewProjectDefaults back;
    std::size_t at = 0;
    const auto text = d.toText();
    while (at < text.size())
    {
        const auto end = text.find ('\n', at);
        const auto line = text.substr (at, end - at);
        const auto eq = line.find ('=');
        CHECK (eq != std::string::npos && NewProjectDefaults::ownsKey (line.substr (0, eq))); // (every saved key is read)
        if (eq != std::string::npos && NewProjectDefaults::ownsKey (line.substr (0, eq)))
            back.readKey (line.substr (0, eq), line.substr (eq + 1));
        at = end == std::string::npos ? text.size() : end + 1;
    }
    CHECK (back == d && back.linkTracks);
    CHECK (! NewProjectDefaults::ownsKey ("typingPiano"));
}

// --selftest's trackauto190gz_saved case: the default song, Pattern 1 (Kick, Clap, Hat) on Track 1, a Kick pattern on
// Track 2
TEST (userfix190ib_selftest_default_song_case)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = 0;
    song.playlist.push_back (clip);
    auto& kick = song.patterns.emplace_back();
    kick.name = "Kick";
    kick.toggleStep (0, 0);
    clip.pattern = (int) song.patterns.size() - 1;
    clip.track = 1;
    song.playlist.push_back (clip);
    autolink::Report report;
    autolink::setOn (song, true, &report);
    for (const auto& c : report.changes)
        std::printf ("  change kind %d track %d channel %d %d -> %d '%s' sound %d\n", (int) c.kind, c.track, c.channel, c.from, c.to, c.name.c_str(),
                     (int) c.changesSound);
    std::printf ("  track 1 mode %d insert %d; channels %d %d %d\n", (int) song.playlistTracks[0].mode, song.playlistTracks[0].mixerTrack,
                 song.channels[0].mixerTrack, song.channels[1].mixerTrack, song.channels[2].mixerTrack);
    CHECK (! song.playlistTracks.empty() && song.playlistTracks[0].mode == PlaylistTrackMode::audio && song.playlistTracks[0].mixerTrack == 1);
    CHECK (song.playlistTracks.size() < 2 || song.playlistTracks[1].mode == PlaylistTrackMode::unassigned);
    CHECK (song.channels[0].mixerTrack == 1 && song.channels[1].mixerTrack == 1 && song.channels[2].mixerTrack == 1);
}
