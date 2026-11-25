// Round 190qu (0.1.32): Discord #general 11-24 after 0.1.31.1.
//  - kainproctor: Playlist › "Move automation clips into their tracks' lanes" on a big FL import made the automations
//    "act weird or stop working completely" (core/LaneView.h): the lanes now play what the clips played; the way back
//    (lanes to clips), selected clips into lanes, a lane's time selection as a clip;
//  - sidthenugget: Track-only / Simple instrument tracks draw a new empty clip (core/TrackClips.h pencilMakesNewClip),
//    FL's exact Patterns › Split by channel (core/RackOps.cpp, checked live in FL 2026) and "Convert project to
//    Track-only" (core/TrackOnly.h convertProject), the Simple drop strip only on an empty project (core/SimpleTracks.h).

#include "core/LaneView.h"
#include "core/LayoutStyle.h"
#include "core/MidiDrop.h"
#include "core/PlaylistViewOptions.h"
#include "core/RackOps.h"
#include "core/SimpleTracks.h"
#include "core/TrackClips.h"
#include "core/TrackOnly.h"

namespace discord190qu
{
using namespace lynx;

// The first version of clipsToLanes (round 190iu, LYNX 0.1.31.1), kept here to measure what it did to a project.
inline std::vector<int> legacyMovable (const Song& song)
{
    std::vector<int> out;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.kind == ClipKind::automation && c.gridSlot < 0 && c.length > 0 && c.automation >= 0 && c.automation < (int) song.automations.size()
            && ! song.automations[(std::size_t) c.automation].isLive() && laneview::ownerTrack (song, song.automations[(std::size_t) c.automation].target) >= 0)
            out.push_back (i);
    }
    return out;
}
inline int legacyClipsToLanes (Song& song)
{
    auto clips = legacyMovable (song);
    std::vector<int> sources;
    int moved = 0;
    for (auto it = clips.rbegin(); it != clips.rend(); ++it)
    {
        const auto c = song.playlist[(std::size_t) *it];
        const auto owner = laneview::ownerTrack (song, song.automations[(std::size_t) c.automation].target);
        if (owner >= 0 && autolanes::clipToLane (song, *it, owner) >= 0)
        {
            ++moved;
            sources.push_back (c.automation);
        }
    }
    std::sort (sources.rbegin(), sources.rend());
    sources.erase (std::unique (sources.begin(), sources.end()), sources.end());
    for (const auto a : sources)
    {
        bool used = false;
        song.forEachClipList ([&] (const std::vector<Clip>& list) {
            for (const auto& k : list)
                used = used || (k.kind == ClipKind::automation && k.automation == a);
        });
        for (const auto& t : song.playlistTracks)
            for (const auto& l : t.lanes)
                used = used || l.automation == a;
        if (! used)
            song.removeAutomation (a);
    }
    return moved;
}

// What the engine puts on `target` at `tick` (RackRenderer's rule): where a clip of the target plays, the one started
// last (with its MIN / MAX and its link's scale); elsewhere a lane of the target on an unmuted track, or the value the
// clip that ended last ended on, or the control's own value. Only automations the engine plays from the first 1024
// slots count (RackState::maxAutomations).
inline float playedAt (const Song& song, const AutomationTarget& target, double tick)
{
    const auto count = std::min<int> ((int) song.automations.size(), RackState::maxAutomations);
    const auto lanes = laneview::laneFlags (song);
    const Clip* playing = nullptr;
    const Clip* ended = nullptr;
    for (const auto& c : song.playlist)
    {
        if (! laneview::clipPlays (song, c, lanes) || c.automation >= count || ! (laneview::drivenTarget (song.automations[(std::size_t) c.automation]) == target)
            || tick < (double) c.start)
            continue;
        if (tick < (double) c.end())
        {
            if (playing == nullptr || c.start > playing->start || (c.start == playing->start && c.automation < playing->automation))
                playing = &c;
        }
        else if (ended == nullptr || c.end() > ended->end() || (c.end() == ended->end() && c.start > ended->start))
            ended = &c;
    }
    if (playing != nullptr)
        return laneview::playedValue (song.automations[(std::size_t) playing->automation], tick - (double) playing->start + (double) playing->offset);
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        for (const auto& l : song.playlistTracks[(std::size_t) t].lanes)
            if (l.automation >= 0 && l.automation < count && ! song.isTrackMuted (t) && song.automations[(std::size_t) l.automation].target == target
                && ! song.automations[(std::size_t) l.automation].points.empty())
                return song.automations[(std::size_t) l.automation].valueAt (tick);
    if (ended != nullptr)
        return laneview::playedValue (song.automations[(std::size_t) ended->automation], (double) (ended->length + ended->offset));
    return song.targetValue (target);
}

struct Compare
{
    int targets = 0, samples = 0, off = 0; // samples more than 0.01 apart
    int undriven = 0;                      // samples a plugin parameter had its own value before (not compared)
    double worst = 0.0;
};
// `before` against `after` on every target the movable clips of `before` drive, every 1/4 beat to 4 bars after the end
// (not within 2 ticks of a clip edge: a lane jumps over one tick)
inline Compare compare (const Song& before, const Song& after)
{
    Compare out;
    std::vector<AutomationTarget> targets;
    std::vector<std::int64_t> edges;
    std::int64_t end = 0;
    for (const auto i : laneview::movableClips (before))
    {
        const auto& c = before.playlist[(std::size_t) i];
        const auto t = laneview::drivenTarget (before.automations[(std::size_t) c.automation]);
        if (std::find (targets.begin(), targets.end(), t) == targets.end())
            targets.push_back (t);
    }
    for (const auto& c : before.playlist)
        if (c.kind == ClipKind::automation)
        {
            edges.push_back (c.start);
            edges.push_back (c.end());
            end = std::max (end, c.end());
        }
    std::sort (edges.begin(), edges.end());
    out.targets = (int) targets.size();
    for (const auto& t : targets)
        for (std::int64_t tick = 0; tick < end + 16 * ppq; tick += ppq / 4)
        {
            const auto it = std::lower_bound (edges.begin(), edges.end(), tick - 2);
            if (it != edges.end() && *it <= tick + 2)
                continue;
            const auto a = playedAt (before, t, (double) tick), b = playedAt (after, t, (double) tick);
            if (a < 0.0f) // (a plugin's parameter before its first clip: the plugin's own value, which the song does not hold;
            {             //  the lane holds the first clip's start value there, as a lane in Live does)
                out.undriven += b >= 0.0f ? 1 : 0;
                continue;
            }
            ++out.samples;
            const auto d = std::abs ((double) a - (double) b);
            out.worst = std::max (out.worst, d);
            out.off += d > 0.01 ? 1 : 0;
        }
    return out;
}

inline Clip automationClip (int automation, int track, std::int64_t start, std::int64_t length, std::int64_t offset = 0)
{
    Clip c;
    c.kind = ClipKind::automation;
    c.automation = automation;
    c.track = track;
    c.start = start;
    c.length = length;
    c.offset = offset;
    return c;
}

// a song with the Lead on instrument track 2 (its insert linked), its volume automated by clips on track 5
inline Song leadSong()
{
    auto s = Song::makeDefault();
    s.playlist.clear();
    s.channels.clear();
    s.automations.clear();
    s.channels.push_back (mididrop::newChannel ("Lead"));
    trackmodes::makeInstrumentTrack (s, 2, 0);
    s.channels[0].volume = 0.62f;
    return s;
}
inline Automation ramp (float from, float to, std::int64_t length)
{
    Automation a;
    a.name = "Lead volume";
    a.target = { AutomationTargetKind::channelVolume, 0, 0, 0 };
    a.points = { { 0, from, 0.0f }, { length, to, 0.0f } };
    return a;
}
} // namespace discord190qu

TEST (discord190qu_lanes_hold_the_last_value_between_clips)
{
    using namespace lynx;
    using namespace discord190qu;
    auto s = leadSong();
    s.automations = { ramp (0.1f, 0.9f, 4 * ppq), ramp (0.3f, 0.5f, 4 * ppq) };
    s.automations[1].name = "Lead volume #2";
    s.playlist = { automationClip (0, 5, 4 * ppq, 4 * ppq), automationClip (1, 5, 16 * ppq, 4 * ppq) };
    const auto before = s;
    // the first version: between the clips the lane went back to the value it was made with (the first clip's start)
    {
        auto old = s;
        CHECK (legacyClipsToLanes (old) == 2);
        const auto r = compare (before, old);
        std::printf ("  legacy: %d targets, %d samples, %d off, worst %.3f\n", r.targets, r.samples, r.off, r.worst);
        CHECK (r.off > 0);
    }
    CHECK (laneview::clipsToLanes (s) == 2);
    const auto r = compare (before, s);
    std::printf ("  now: %d targets, %d samples, %d off, worst %.4f\n", r.targets, r.samples, r.off, r.worst);
    CHECK (r.samples > 0 && r.off == 0 && r.worst < 0.002);
    // between the clips: the first clip's end value (0.9), not its start; before the first: the channel's own volume
    const auto lane = s.playlistTracks[2].lanes.at (0).automation;
    CHECK (std::abs (s.automations[(std::size_t) lane].valueAt (12.0 * ppq) - 0.9f) < 1.0e-6f);
    CHECK (std::abs (s.automations[(std::size_t) lane].valueAt (ppq) - 0.62f) < 1.0e-6f);
    // the lane took the first automation's slot (nothing else uses it), the second automation went
    CHECK (s.automations.size() == 1 && lane == 0 && s.playlist.empty());
}

TEST (discord190qu_lanes_keep_min_max_scale_mute_and_overlaps)
{
    using namespace lynx;
    using namespace discord190qu;
    auto s = leadSong();
    // 0: FL's MIN / MAX knobs re-range it; 1: a link with a bent scale; 2: muted clip; 3: an LFO (stays a clip)
    auto reranged = ramp (0.0f, 1.0f, 8 * ppq);
    reranged.rangeMin = 0.2f;
    reranged.rangeMax = 0.6f;
    auto scaled = ramp (0.0f, 1.0f, 8 * ppq);
    scaled.name = "Lead volume #2";
    Automation::Link link;
    link.target = scaled.target;
    link.scale = { 0.0f, 0.05f, 0.2f, 0.6f, 1.0f };
    scaled.links = { link };
    auto lfo = ramp (0.5f, 0.5f, 4 * ppq);
    lfo.name = "Lead volume #4";
    lfo.lfo.on = true;
    s.automations = { reranged, scaled, ramp (0.9f, 0.9f, 4 * ppq), lfo };
    s.playlist = { automationClip (0, 5, 0, 8 * ppq), automationClip (1, 6, 4 * ppq, 8 * ppq), // (overlap: the later start wins)
                   automationClip (2, 5, 20 * ppq, 4 * ppq), automationClip (3, 7, 32 * ppq, 4 * ppq) };
    s.playlist[2].muted = true;
    const auto before = s;
    {
        auto old = s;
        legacyClipsToLanes (old);
        const auto r = compare (before, old);
        std::printf ("  legacy: %d samples, %d off, worst %.3f\n", r.samples, r.off, r.worst);
        CHECK (r.off > 0);
    }
    const auto movable = laneview::movableClips (s);
    CHECK ((movable == std::vector<int> { 0, 1 })); // (the muted clip and the LFO clip stay clips)
    CHECK (laneview::clipsToLanes (s) == 2);
    const auto r = compare (before, s);
    std::printf ("  now: %d samples, %d off, worst %.4f\n", r.samples, r.off, r.worst);
    CHECK (r.samples > 0 && r.off == 0 && r.worst < 0.01);
    CHECK (s.playlist.size() == 2 && s.playlist[0].muted && s.automations[(std::size_t) s.playlist[1].automation].lfo.on);
}

TEST (discord190qu_lanes_fit_the_engines_1024_slots)
{
    using namespace lynx;
    using namespace discord190qu;
    // a big FL project: its two volume clips' automations come first, 1100 other automations after them; the lane must
    // play from a slot the engine has (the first version appended it after all of them)
    auto s = leadSong();
    s.automations.push_back (ramp (0.2f, 0.8f, 4 * ppq)); // 0
    s.automations.push_back (ramp (0.4f, 0.4f, 4 * ppq)); // 1, the same control
    for (int k = 0; k < 1100; ++k)
    {
        Automation a;
        a.name = "Tempo " + std::to_string (k);
        a.target = { AutomationTargetKind::tempo, 0, 0, 0 };
        a.points = { { 0, 0.5f, 0.0f } };
        s.automations.push_back (a);
        s.playlist.push_back (automationClip (k + 2, 9, 200 * ppq + k, 1));
    }
    s.playlist.push_back (automationClip (0, 5, 0, 4 * ppq));
    s.playlist.push_back (automationClip (1, 5, 8 * ppq, 4 * ppq));
    {
        auto old = s;
        CHECK (legacyClipsToLanes (old) == 2);
        const auto lane = old.playlistTracks[2].lanes.at (0).automation;
        std::printf ("  legacy lane slot %d of %d\n", lane, (int) old.automations.size());
        CHECK (lane >= RackState::maxAutomations); // (never played)
    }
    CHECK (laneview::clipsToLanes (s) == 2);
    const auto lane = s.playlistTracks[2].lanes.at (0).automation;
    CHECK (lane < RackState::maxAutomations);
    // the renderer drives it (Song mode): the slot's value at bar 1 beat 3 (half way up the ramp)
    s.mode = PlayMode::song;
    auto state = RackState::build (s, nullptr, 48000.0);
    RackRenderer r;
    r.prepare (48000.0, 256);
    std::vector<float> left (256), right (256);
    r.process (state.get(), true, 2.0 * ppq, 256, left.data(), right.data());
    const auto v = r.automationValueForCheck (lane);
    std::printf ("  lane slot %d plays %.3f\n", lane, (double) v);
    CHECK (std::abs (v - 0.5f) < 0.01f);
}

TEST (discord190qu_lanes_back_to_clips_and_middle_ground)
{
    using namespace lynx;
    using namespace discord190qu;
    auto s = leadSong();
    s.automations = { ramp (0.1f, 0.9f, 4 * ppq), ramp (0.3f, 0.5f, 4 * ppq) };
    s.playlist = { automationClip (0, 5, 4 * ppq, 4 * ppq), automationClip (1, 5, 16 * ppq, 4 * ppq) };
    const auto original = s;
    // only the selected clip (the second): the lane plays the rest, the first clip still wins where it plays
    CHECK (laneview::clipsIntoLanes (s, { 1 }) == 1);
    CHECK (s.playlist.size() == 1 && s.playlistTracks[2].lanes.size() == 1);
    CHECK (compare (original, s).off == 0);
    // the time selection of the lane as a clip: nothing sounds different, the clip is drawn under the track
    const auto withLane = s;
    const auto made = laneview::laneRangeToClip (s, 2, 0, 15 * ppq, 19 * ppq);
    CHECK (made >= 0 && s.playlist[(std::size_t) made].start == 15 * ppq && s.playlist[(std::size_t) made].length == 4 * ppq);
    CHECK (made >= 0 && s.playlist[(std::size_t) made].track != 2 && s.playlistTracks[2].mode == PlaylistTrackMode::instrument);
    CHECK (compare (original, s).off == 0);
    CHECK (cliplanes::project (s, 20, true).ownerOf (s.playlist[(std::size_t) made].track) == 2);
    CHECK (laneview::laneRangeToClip (s, 2, 0, 5, 5) == -1 && laneview::laneRangeToClip (s, 9, 0, 0, ppq) == -1);
    // and back: every lane a clip again, the same values
    auto back = withLane;
    CHECK (laneview::laneCount (back) == 1 && autolanes::lanesToClips (back) == 1 && laneview::laneCount (back) == 0);
    CHECK (compare (original, back).off == 0);
}

TEST (discord190qu_lanes_on_fl_demos)
{
    using namespace lynx;
    using namespace discord190qu;
    if (realFlpDir.empty())
        return;
    const auto folder = std::filesystem::path (realFlpDir) / "Demo projects" / "Demo songs";
    if (! std::filesystem::exists (folder))
        return;
    std::vector<std::filesystem::path> files;
    for (auto& entry : std::filesystem::directory_iterator (folder))
        if (entry.path().extension() == ".flp")
            files.push_back (entry.path());
    std::sort (files.begin(), files.end());
    int projects = 0, movedTotal = 0;
    for (const auto& path : files)
    {
        std::ifstream in (path, std::ios::binary);
        const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
        auto song = importFlp (flp::parseProject (file));
        autolink::setOn (song, true); // (kainproctor's project: tracks linked to their inserts, so tracks own the controls)
        int clips = 0;
        for (const auto& c : song.playlist)
            clips += c.kind == ClipKind::automation ? 1 : 0;
        const auto before = song;
        auto old = song;
        const auto oldMoved = legacyClipsToLanes (old);
        const auto legacy = compare (before, old);
        int oldDead = 0;
        for (const auto& t : old.playlistTracks)
            for (const auto& l : t.lanes)
                oldDead += l.automation >= RackState::maxAutomations ? 1 : 0;
        const auto moved = laneview::clipsToLanes (song);
        const auto now = compare (before, song);
        int dead = 0, lanes = 0;
        for (const auto& t : song.playlistTracks)
            for (const auto& l : t.lanes)
            {
                ++lanes;
                dead += l.automation >= RackState::maxAutomations ? 1 : 0;
            }
        std::printf ("  %-52s %4d automation clips, %4d automations | legacy: %4d moved, %5d/%6d samples off (worst %.3f), %d lanes past slot 1024 | now: %4d moved into %3d lanes, %d/%d off (worst %.4f), %d past 1024\n",
                     path.filename().string().c_str(), clips, (int) before.automations.size(), oldMoved, legacy.off, legacy.samples, legacy.worst, oldDead, moved,
                     lanes, now.off, now.samples, now.worst, dead);
        if (now.undriven > 0)
            std::printf ("      (%d samples of plugin parameters before their first clip: they hold its start value now)\n", now.undriven);
        CHECK (now.off == 0);
        CHECK (dead == 0);
        ++projects;
        movedTotal += moved;
    }
    std::printf ("  %d demo projects, %d clips moved\n", projects, movedTotal);
}

TEST (discord190qu_split_by_channel_is_fl_s)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.patterns.resize (2);
    auto& p = song.patterns[0];
    p.name = "Pattern 1";
    p.colour = 0xff445566;
    p.notes.clear();
    for (int i = 0; i < 4; ++i) // (the Kick on steps 1 and 5, the HiHat on 3 and 7: FL's live check)
        p.notes.push_back ({ (std::int64_t) i * 2 * ticksPerStep, ticksPerStep, i % 2 == 0 ? 0 : 2, 60 });
    song.patterns[1].name = "After";
    Clip a, b, later;
    a.pattern = 0;
    b.pattern = 0;
    b.start = 4 * ppq;
    later.pattern = 1;
    song.playlist = { a, b, later };
    CHECK (splitPatternByChannel (song, 0) == 1 && song.patterns.size() == 3);
    // the pattern keeps the first channel's notes and is named after it; its clips stay
    CHECK (song.patterns[0].name == song.channels[0].name && song.patterns[0].notes.size() == 2 && song.patterns[0].notes[0].channel == 0);
    CHECK (song.patterns[1].name == song.channels[2].name && song.patterns[1].notes.size() == 2 && song.patterns[1].notes[0].channel == 2);
    CHECK (song.patterns[1].colour == 0xff445566u && song.patterns[0].lengthTicks == song.patterns[1].lengthTicks);
    CHECK (song.playlist[0].pattern == 0 && song.playlist[1].pattern == 0 && song.playlist.size() == 3); // (nothing placed)
    CHECK (song.playlist[2].pattern == 2 && song.patterns[2].name == "After"); // (a later pattern's clips follow)
    // one channel: nothing to split
    CHECK (splitPatternByChannel (song, 0) == 0 && splitPatternByChannel (song, 99) == 0);
}

TEST (discord190qu_convert_project_to_track_only)
{
    using namespace lynx;
    // two Synth channels in one pattern on track 0, placed twice; a one-channel pattern on track 1; a muted track
    auto s = Song::makeDefault();
    s.channels.clear();
    s.playlist.clear();
    s.patterns.clear();
    s.automations.clear();
    s.mixer = Song::defaultMixer (4);
    s.mode = PlayMode::song;
    for (int k = 0; k < 3; ++k)
    {
        ChannelDef c;
        c.name = k == 0 ? "Bass" : k == 1 ? "Keys" : "Pad";
        c.instrument = InstrumentKind::synth;
        c.mixerTrack = 0;
        s.channels.push_back (c);
    }
    Pattern both;
    both.name = "Verse";
    both.notes = { { 0, 2 * ppq, 0, 40 }, { ppq, 2 * ppq, 1, 64 } };
    Pattern pad;
    pad.name = "Pad";
    pad.notes = { { 0, 4 * ppq, 2, 72 } };
    s.patterns = { both, pad };
    const auto clipOf = [] (int pattern, int track, std::int64_t start) {
        Clip c;
        c.kind = ClipKind::pattern;
        c.pattern = pattern;
        c.track = track;
        c.start = start;
        c.length = 4 * ppq;
        return c;
    };
    s.playlist = { clipOf (0, 0, 0), clipOf (0, 0, 8 * ppq), clipOf (1, 1, 0), clipOf (1, 3, 8 * ppq) };
    s.playlistTracks.resize (4);
    s.playlistTracks[3].muted = true;
    const auto before = s;
    const auto report = trackonly::convertProject (s);
    std::printf ("  %d clips moved, %d patterns split, %d tracks made\n", report.clipsMoved, report.patternsSplit, report.tracksMade);
    CHECK (report.patternsSplit == 1 && report.tracksMade == 3 && report.clipsMoved == 6);
    // every channel on its own instrument track, its clips there and nowhere else
    for (int c = 0; c < 3; ++c)
    {
        const auto tracks = trackmodes::instrumentTracksOf (s, c);
        CHECK (tracks.size() == 1);
        if (tracks.empty())
            continue;
        CHECK (s.playlistTracks[(std::size_t) tracks[0]].ownClips);
        int mine = 0;
        for (const auto& clip : s.playlist)
            if (clip.track == tracks[0])
            {
                ++mine;
                for (const auto& n : s.patterns[(std::size_t) clip.pattern].notes)
                    CHECK (n.channel == c);
            }
        CHECK (mine == 2);
    }
    CHECK (s.patterns.size() == 3 && s.patterns[0].name == "Verse - Bass" && s.patterns[1].name == "Verse - Keys");
    // the clip of the muted track stays silent
    int muted = 0;
    for (const auto& clip : s.playlist)
        muted += clip.muted ? 1 : 0;
    CHECK (muted == 1);
    // the same notes play at the same times (the scheduler's view: every clip's notes in song time)
    const auto notesOf = [] (const Song& song) {
        std::vector<std::tuple<std::int64_t, int, int>> out;
        for (const auto& clip : song.playlist)
            if (clip.kind == ClipKind::pattern && ! clip.muted && ! song.isTrackMuted (clip.track))
                for (const auto& n : song.patterns[(std::size_t) clip.pattern].notes)
                    out.push_back ({ clip.start + n.start, n.channel, n.key });
        std::sort (out.begin(), out.end());
        return out;
    };
    CHECK (notesOf (before) == notesOf (s));
    // nothing left to do a second time
    const auto again = trackonly::convertProject (s);
    CHECK (again.clipsMoved == 0 && again.patternsSplit == 0);
}

TEST (discord190qu_pencil_new_clip_and_simple_strip)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.playlist.clear();
    s.channels.clear();
    s.channels.push_back (mididrop::newChannel ("Lead"));
    const auto layout = layoutstyle::current();
    const auto trackOnly = trackonly::enabled();
    layoutstyle::setCurrent (layoutstyle::Style::pattern);
    trackonly::setEnabled (false);
    CHECK (trackmodes::makeInstrumentTrack (s, 1, 0));
    CHECK (! s.playlistTracks[1].ownClips);
    CHECK (! trackclips::pencilMakesNewClip (s, 1)); // (FL's pencil: the channel's pattern)
    trackonly::setEnabled (true);
    CHECK (trackclips::pencilMakesNewClip (s, 1) && ! trackclips::pencilMakesNewClip (s, 0)); // (Track-only: a new clip on instrument tracks)
    CHECK (trackclips::defaultOn());
    trackonly::setEnabled (false);
    layoutstyle::setCurrent (layoutstyle::Style::simple);
    CHECK (trackclips::pencilMakesNewClip (s, 1));
    layoutstyle::setCurrent (layoutstyle::Style::pattern);
    CHECK (! trackclips::pencilMakesNewClip (s, 1));
    layoutstyle::setCurrent (layout);
    trackonly::setEnabled (trackOnly);

    // the Simple drop strip and the chooser's start row: an empty project only
    auto empty = Song::makeDefault();
    empty.playlist.clear();
    empty.playlistTracks.clear();
    if (empty.clipGrid)
        empty.clipGrid->clips.clear();
    empty.arrangements.clear();
    CHECK (! simpletracks::anyTrack (empty));
    CHECK (simpletracks::anyTrack (s));
    CHECK (! simpletracks::dropStripAlways);
}
