// Round 190gz (issue #170): LYNX's "Link Playlist tracks to mixer tracks" (core/PlaylistOps.h autolink): an optional
// project switch that makes every Playlist track holding clips FL's own audio / instrument track of a mixer track (Track
// N with insert N where that changes nothing in the sound), followed by new content, recording and the file formats.

#include <cstring>

#include "core/FlpExport.h"
#include "core/FlpImport.h"
#include "core/LynxExtras.h"
#include "core/PlaylistOps.h"
#include "core/RackRenderer.h"
#include "core/TakeRecording.h"
#include "core/Templates.h"
#include "flp/Flp.h"

namespace trackauto190gz
{
// channels: 0 Kick (on the Master), 1 Synth (insert 9), 2 "Loop" audio (Master), 3 "Vox" audio (insert 12), 4 "Pad"
// audio (Master), 5 "Bell" audio (Master), 6 Hat (Master)
// tracks: 0 Kick's pattern, 1 Loop, 2 Kick + Synth pattern (mixed), 3 automation only, 4 Vox, 5 an audio track already
// linked to insert 6 (Pad on it), 6 Synth's pattern, 7 audio + pattern, 8 empty, 9 Bell (insert 10 carries an effect)
struct Setup
{
    lynx::Song song;
    Setup()
    {
        using namespace lynx;
        song.bpm = 120.0;
        song.mode = PlayMode::song;
        song.mixer = Song::defaultMixer (16);
        auto channel = [this] (const char* name, InstrumentKind kind, int mixer) {
            ChannelDef c;
            c.name = name;
            c.instrument = kind;
            c.mixerTrack = mixer;
            if (kind == InstrumentKind::audioClip)
                c.samplePath = std::string ("/t/") + name + ".wav";
            song.channels.push_back (c);
        };
        channel ("Kick", InstrumentKind::kick, 0);
        channel ("Synth", InstrumentKind::synth, 9);
        channel ("Loop", InstrumentKind::audioClip, 0);
        channel ("Vox", InstrumentKind::audioClip, 12);
        channel ("Pad", InstrumentKind::audioClip, 0);
        channel ("Bell", InstrumentKind::audioClip, 0);
        channel ("Hat", InstrumentKind::hat, 0);
        song.mixer[10].effects.push_back (makeEffectSlot (EffectKind::parametricEq));

        auto pattern = [this] (const char* name, std::vector<int> channels) {
            auto& p = song.patterns.emplace_back();
            p.name = name;
            for (const auto c : channels)
                for (int step : { 0, 4, 8, 12 })
                    p.toggleStep (c, step);
            return (int) song.patterns.size() - 1;
        };
        const auto kickOnly = pattern ("Kick", { 0 });
        const auto mixed = pattern ("Beat", { 0, 1 });
        const auto synthOnly = pattern ("Lead", { 1 });
        song.automations.emplace_back();

        clip (0, ClipKind::pattern, kickOnly);
        clip (1, ClipKind::audio, 2);
        clip (2, ClipKind::pattern, mixed);
        clip (3, ClipKind::automation, 0);
        clip (4, ClipKind::audio, 3);
        clip (5, ClipKind::audio, 4);
        clip (6, ClipKind::pattern, synthOnly);
        clip (7, ClipKind::audio, 2, 4 * ppq);
        clip (7, ClipKind::pattern, kickOnly, 8 * ppq);
        clip (9, ClipKind::audio, 5);
        song.playlistTracks.resize (10);
        song.playlistTracks[5].mode = PlaylistTrackMode::audio;
        song.playlistTracks[5].mixerTrack = 6;
        song.playlistTracks[5].name = "Pads";
    }
    int clip (int track, lynx::ClipKind kind, int source, std::int64_t start = 0)
    {
        lynx::Clip c;
        c.kind = kind;
        c.track = track;
        c.start = start;
        c.length = 4 * lynx::ppq;
        if (kind == lynx::ClipKind::pattern)
            c.pattern = source;
        else if (kind == lynx::ClipKind::audio)
            c.channel = source;
        else
            c.automation = source;
        song.playlist.push_back (c);
        return (int) song.playlist.size() - 1;
    }
    const lynx::PlaylistTrack& track (int t) const { return song.playlistTracks[(std::size_t) t]; }
};

// renders the song (a sine for every audio file) and returns the left channel
inline std::vector<float> render (const lynx::Song& song, double seconds = 2.0)
{
    using namespace lynx;
    auto data = std::make_shared<SampleData>();
    const int total = 44100 * 3;
    data->left.resize ((std::size_t) total);
    data->right.resize ((std::size_t) total);
    for (int i = 0; i < total; ++i)
        data->left[(std::size_t) i] = data->right[(std::size_t) i] = 0.3f * (float) std::sin (2.0 * 3.141592653589793 * 330.0 * i / 44100.0);
    auto state = RackState::build (song, nullptr, 44100.0, [&] (const std::string&) { return data; });
    RackRenderer renderer;
    renderer.prepare (44100.0, 2048);
    std::vector<float> out, l (256), r (256);
    const auto tps = ticksPerSample (song.bpm, 44100.0);
    for (int pos = 0; pos + 256 <= (int) (44100 * seconds); pos += 256)
    {
        renderer.process (state.get(), true, pos * tps, 256, l.data(), r.data());
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}
} // namespace trackauto190gz

TEST (trackauto190gz_off_by_default_and_new_projects)
{
    using namespace lynx;
    CHECK (! Song().linkTracksToMixer && ! Song::makeDefault().linkTracksToMixer);
    // the new projects' default: off, kept in ui-options.txt as newLinkTracks
    NewProjectDefaults d;
    CHECK (! d.linkTracks && d.toText().find ("newLinkTracks=0") != std::string::npos);
    d.linkTracks = true;
    NewProjectDefaults back;
    for (const auto* line : { "newLinkTracks=1" })
    {
        const std::string text (line);
        back.readKey (text.substr (0, text.find ('=')), text.substr (text.find ('=') + 1));
    }
    CHECK (back.linkTracks && d.toText().find ("newLinkTracks=1") != std::string::npos);
    auto song = Song::makeDefault();
    applyNewProjectDefaults (song, NewProjectDefaults {});
    CHECK (! song.linkTracksToMixer);
    auto linked = Song::makeDefault();
    applyNewProjectDefaults (linked, d);
    CHECK (linked.linkTracksToMixer);
    // the LYNX extras page lists it under the key it is kept with
    bool listed = false;
    for (const auto& item : extras::items)
        listed = listed || (item.id == "project.linkTracks" && item.settingsKey == "ui-options.txt:newLinkTracks");
    CHECK (listed);
}

TEST (trackauto190gz_switch_on_links_tracks_by_their_content)
{
    using namespace lynx;
    using autolink::Plan;
    trackauto190gz::Setup s;
    // the plans before anything changes
    CHECK (autolink::planFor (s.song, 0) == (Plan { Plan::Kind::instrument, 0, 0 }));
    CHECK (autolink::planFor (s.song, 1) == (Plan { Plan::Kind::audio, 0, -1 }));
    CHECK (autolink::planFor (s.song, 2).kind == Plan::Kind::none); // two instruments' patterns
    CHECK (autolink::planFor (s.song, 3).kind == Plan::Kind::none); // automation only
    CHECK (autolink::planFor (s.song, 4) == (Plan { Plan::Kind::audio, 12, -1 }));
    CHECK (autolink::planFor (s.song, 5).kind == Plan::Kind::none); // already an audio track
    CHECK (autolink::planFor (s.song, 6) == (Plan { Plan::Kind::instrument, 9, 1 }));
    CHECK (autolink::planFor (s.song, 7).kind == Plan::Kind::none); // audio and patterns
    CHECK (autolink::planFor (s.song, 8).kind == Plan::Kind::none); // empty
    CHECK (autolink::isNeutralFree (s.song, 1) && ! autolink::isNeutralFree (s.song, 9) && ! autolink::isNeutralFree (s.song, 10)
           && ! autolink::isNeutralFree (s.song, 6) && ! autolink::isNeutralFree (s.song, 0));

    const auto pads = s.track (5);
    CHECK (autolink::setOn (s.song, true) == 5); // (Tracks 1, 2, 5, 7 and 10)
    CHECK (s.song.linkTracksToMixer);
    // Track 1 (Kick's pattern, Kick on the Master): Kick's instrument track on insert 1, which takes Kick's name
    CHECK (s.track (0).mode == PlaylistTrackMode::instrument && s.track (0).channel == 0 && s.track (0).mixerTrack == 1);
    CHECK (s.song.channels[0].mixerTrack == 1 && s.song.mixer[1].name == "Kick" && s.track (0).name == "Kick");
    // Track 2 (the Loop sample on the Master): an audio track on insert 2, named after the sample, the channel routed there
    CHECK (s.track (1).mode == PlaylistTrackMode::audio && s.track (1).mixerTrack == 2 && s.song.channels[2].mixerTrack == 2);
    CHECK (s.track (1).name == "Loop" && s.song.mixer[2].name == "Loop");
    // Track 3 and 4 stay unassigned
    CHECK (s.track (2).mode == PlaylistTrackMode::unassigned && s.track (3).mode == PlaylistTrackMode::unassigned);
    // Track 5 (Vox already on insert 12): linked where it plays, insert 5 left alone
    CHECK (s.track (4).mode == PlaylistTrackMode::audio && s.track (4).mixerTrack == 12 && s.song.channels[3].mixerTrack == 12);
    CHECK (s.song.mixer[5].name == "Insert 5");
    // Track 6: an existing link is never changed
    CHECK (s.track (5) == pads);
    // Track 7 (Synth on insert 9): its instrument track on insert 9
    CHECK (s.track (6).mode == PlaylistTrackMode::instrument && s.track (6).channel == 1 && s.track (6).mixerTrack == 9);
    CHECK (s.song.mixer[9].name == "Insert 9"); // (an insert the user routed to keeps its name)
    CHECK (s.track (7).mode == PlaylistTrackMode::unassigned);
    // Track 10 (Bell on the Master, insert 10 carries an EQ): the first unused, neutral insert instead (3)
    CHECK (s.track (9).mode == PlaylistTrackMode::audio && s.track (9).mixerTrack == 3 && s.song.channels[5].mixerTrack == 3);
    CHECK (s.song.mixer[10].effects.size() == 1 && s.song.mixer[10].name == "Insert 10");
    // the headers' insert is the link's (simpletracks::insertOf's rule)
    CHECK (trackmodes::armableInsert (s.song, 1) == 2 && trackmodes::armableInsert (s.song, 0) == -1);

    // a second switch-on changes nothing; switching off keeps the links (they are FL track modes)
    const auto once = s.song;
    const auto routes = [] (const Song& song) {
        std::vector<int> r;
        for (const auto& c : song.channels)
            r.push_back (c.mixerTrack);
        return r;
    };
    CHECK (autolink::setOn (s.song, true) == 0 && s.song.playlistTracks == once.playlistTracks && routes (s.song) == routes (once)
           && s.song.mixer.size() == once.mixer.size());
    CHECK (autolink::setOn (s.song, false) == 0 && ! s.song.linkTracksToMixer && s.track (1).mixerTrack == 2);
}

TEST (trackauto190gz_switching_on_keeps_the_sound)
{
    // the links only route content from the Master through unused inserts at their defaults (and leave every routed
    // channel where it is): the mix is the same
    trackauto190gz::Setup s;
    s.song.playlist.erase (std::remove_if (s.song.playlist.begin(), s.song.playlist.end(),
                                           [] (const lynx::Clip& c) { return c.kind == lynx::ClipKind::automation; }),
                           s.song.playlist.end());
    s.song.automations.clear();
    const auto before = trackauto190gz::render (s.song);
    CHECK (lynx::autolink::setOn (s.song, true) == 5);
    const auto after = trackauto190gz::render (s.song);
    CHECK (before.size() == after.size() && ! before.empty());
    float peak = 0.0f, worst = 0.0f;
    for (std::size_t i = 0; i < before.size() && i < after.size(); ++i)
    {
        peak = std::max (peak, std::abs (before[i]));
        worst = std::max (worst, std::abs (before[i] - after[i]));
    }
    std::printf ("  render peak %.4f, largest difference after linking %.3g\n", peak, worst);
    CHECK (peak > 0.05f);    // (something plays)
    CHECK (worst < 1.0e-5f); // the same mix
}

TEST (trackauto190gz_new_content_follows_and_unassigned_stays)
{
    using namespace lynx;
    trackauto190gz::Setup s;
    autolink::setOn (s.song, true);
    // a first clip on empty Track 9 (Hat's pattern, Hat on the Master): linked; insert 9 is the Synth's, so the first
    // unused neutral insert (4)
    auto before = autolink::contentKinds (s.song);
    auto& hat = s.song.patterns.emplace_back();
    hat.name = "Hat";
    hat.toggleStep (6, 2);
    s.clip (8, ClipKind::pattern, (int) s.song.patterns.size() - 1);
    CHECK (autolink::followNewContent (s.song, before) == 1);
    CHECK (s.track (8).mode == PlaylistTrackMode::instrument && s.track (8).channel == 6 && s.track (8).mixerTrack == 4
           && s.song.channels[6].mixerTrack == 4);
    // a sample dropped on a track past the mixer's end: inserts up to its number are added and it takes its own
    before = autolink::contentKinds (s.song);
    ChannelDef c;
    c.name = "Strings";
    c.instrument = InstrumentKind::audioClip;
    s.song.channels.push_back (c);
    s.clip (19, ClipKind::audio, (int) s.song.channels.size() - 1);
    CHECK (autolink::followNewContent (s.song, before) == 1);
    CHECK (s.song.mixer.size() == 21 && s.track (19).mode == PlaylistTrackMode::audio && s.track (19).mixerTrack == 20
           && s.song.channels.back().mixerTrack == 20 && s.song.mixer[20].name == "Strings");
    // Track mode › Unassigned on a linked track: it stays unassigned through the next edits (its content did not change)
    trackmodes::unassign (s.song, 1);
    before = autolink::contentKinds (s.song);
    s.clip (1, ClipKind::audio, 2, 16 * ppq);
    CHECK (autolink::followNewContent (s.song, before) == 0 && s.track (1).mode == PlaylistTrackMode::unassigned);
    // an empty pattern clip links nothing; its first notes do
    auto& empty = s.song.patterns.emplace_back();
    empty.name = "Later";
    const auto later = (int) s.song.patterns.size() - 1;
    ChannelDef synth;
    synth.name = "Bass";
    synth.instrument = InstrumentKind::synth;
    s.song.channels.push_back (synth);
    const auto bass = (int) s.song.channels.size() - 1;
    before = autolink::contentKinds (s.song);
    s.clip (10, ClipKind::pattern, later);
    CHECK (autolink::followNewContent (s.song, before) == 0 && s.track (10).mode == PlaylistTrackMode::unassigned);
    before = autolink::contentKinds (s.song);
    s.song.patterns[(std::size_t) later].toggleStep (bass, 0);
    CHECK (autolink::followNewContent (s.song, before) == 1);
    CHECK (s.track (10).mode == PlaylistTrackMode::instrument && s.track (10).channel == bass && s.track (10).mixerTrack == 11);
    // switched off: nothing follows
    autolink::setOn (s.song, false);
    before = autolink::contentKinds (s.song);
    s.clip (11, ClipKind::audio, 5);
    CHECK (autolink::followNewContent (s.song, before) == 0);
}

TEST (trackauto190gz_refusals)
{
    using namespace lynx;
    using autolink::Plan;
    trackauto190gz::Setup s;
    // a channel whose insert another track is linked to: the track stays unassigned (its sound stays where it is)
    s.song.channels[4].mixerTrack = 6; // Pad plays through Track 6's insert
    s.clip (8, ClipKind::audio, 4, 8 * ppq);
    CHECK (autolink::planFor (s.song, 8).kind == Plan::Kind::none);
    // samples on two different inserts: none; the same insert: an audio track there
    s.song.channels[2].mixerTrack = 13;
    s.clip (11, ClipKind::audio, 2);
    s.clip (11, ClipKind::audio, 3, 8 * ppq);
    CHECK (autolink::planFor (s.song, 11).kind == Plan::Kind::none);
    s.song.channels[2].mixerTrack = 12;
    CHECK (autolink::planFor (s.song, 11) == (Plan { Plan::Kind::audio, 12, -1 }));
    // a channel that already has an instrument track
    s.song.playlistTracks[6].mode = PlaylistTrackMode::instrument;
    s.song.playlistTracks[6].channel = 1;
    s.song.playlistTracks[6].mixerTrack = 9;
    s.clip (12, ClipKind::pattern, 2);
    CHECK (autolink::planFor (s.song, 12).kind == Plan::Kind::none);
    // an insert reserved for another participant of an online session is never taken
    trackauto190gz::Setup t;
    SessionParticipant host, guest;
    host.tag = session::hostTag;
    guest.tag = 2;
    guest.insertFirst = 1;
    guest.insertCount = 2;
    t.song.session.participants = { host, guest };
    CHECK (! autolink::isNeutralFree (t.song, 1) && autolink::insertForTrack (t.song, 0) == 3);
    // a full mixer: nothing is linked, nothing added
    trackauto190gz::Setup full;
    full.song.mixer = Song::defaultMixer (maxMixerTracks - 1);
    for (int m = 1; m < (int) full.song.mixer.size(); ++m)
        full.song.mixer[(std::size_t) m].effects.push_back (makeEffectSlot (EffectKind::parametricEq));
    const auto inserts = full.song.mixer.size();
    CHECK (! autolink::linkTrack (full.song, 1) && full.song.mixer.size() == inserts && full.track (1).mode == PlaylistTrackMode::unassigned);
}

TEST (trackauto190gz_recording_goes_to_the_paired_track)
{
    using namespace lynx;
    trackauto190gz::Setup s;
    autolink::setOn (s.song, true);
    s.song.mixer[14].name = "Mic";
    // off: FL's rule (a post-EQ take of an insert no audio track is linked to plays through the Master, on a new track)
    auto off = s.song;
    off.linkTracksToMixer = false;
    CHECK (takes::playbackInsert (off, 14, RecordSource::postEq) == 0 && takes::takeTrack (off, 14, 0, 960) == 10);
    // on: insert 14's take goes to Track 14 (index 13), which becomes its audio track; the clip plays through insert 14
    CHECK (autolink::pairedTakeTrack (s.song, 14) == 13);
    CHECK (takes::playbackInsert (s.song, 14, RecordSource::postEq) == 14 && takes::takeTrack (s.song, 14, 0, 960) == 13);
    const auto parts = std::vector<takes::Part> { { 0, 960, 0, 0.0f } };
    takes::placeTakeClips (s.song, { { "/rec/Mic_Take1.wav", "Mic_Take1", 14, 14 } }, parts);
    const auto& take = s.song.playlist.back();
    CHECK (take.track == 13 && take.kind == ClipKind::audio && s.song.channels[(std::size_t) take.channel].mixerTrack == 14);
    CHECK (s.track (13).mode == PlaylistTrackMode::audio && s.track (13).mixerTrack == 14 && s.track (13).name == "Mic");
    // the next take of the same insert finds its audio track (free there) as before
    CHECK (takes::takeTrack (s.song, 14, 2000, 3000) == 13 && autolink::pairedTakeTrack (s.song, 14) == -1);
    // an insert whose paired track holds clips: FL's rule (a new track after the last one used)
    CHECK (autolink::pairedTakeTrack (s.song, 4) == -1); // (Track 4 holds an automation clip)
    CHECK (takes::playbackInsert (s.song, 4, RecordSource::postEq) == 0);
    // the linked audio track's insert records into it (Track 2: Loop on insert 2)
    CHECK (takes::playbackInsert (s.song, 2, RecordSource::postEq) == 2 && takes::takeTrack (s.song, 2, 8 * ppq, 9 * ppq) == 1);
}

TEST (trackauto190gz_flp_keeps_the_links)
{
    using namespace lynx;
    trackauto190gz::Setup s;
    autolink::setOn (s.song, true);
    flpexport::Options options;
    flpexport::Report report;
    const auto back = importFlp (flp::parseProject (flpexport::exportFlp (s.song, options, report)));
    CHECK (! back.linkTracksToMixer); // (the switch is LYNX's: .lynx only)
    CHECK (back.playlistTracks.size() >= 10);
    if (back.playlistTracks.size() >= 10)
    {
        const auto mode = [&back] (int t) { return back.playlistTracks[(std::size_t) t].mode; };
        const auto mixer = [&back] (int t) { return back.playlistTracks[(std::size_t) t].mixerTrack; };
        CHECK (mode (0) == PlaylistTrackMode::instrument && mixer (0) == 1);
        CHECK (mode (1) == PlaylistTrackMode::audio && mixer (1) == 2);
        CHECK (mode (2) == PlaylistTrackMode::unassigned);
        CHECK (mode (4) == PlaylistTrackMode::audio && mixer (4) == 12);
        CHECK (mode (6) == PlaylistTrackMode::instrument && mixer (6) == 9);
        CHECK (mode (9) == PlaylistTrackMode::audio && mixer (9) == 3);
        const auto ch = back.playlistTracks[0].channel;
        CHECK (ch >= 0 && ch < (int) back.channels.size() && back.channels[(std::size_t) ch].name == "Kick" && back.channels[(std::size_t) ch].mixerTrack == 1);
        CHECK (back.mixer.size() > 3 && back.mixer[2].name == "Loop");
    }
}
