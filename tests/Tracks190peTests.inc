// --- Round 190pe (research190oe P20 and P11, 0.1.32): track versions (core/TrackVersions.h) and the import of tracks from
// another project (core/TrackImport.h).

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/Comping.h"
#include "core/PlaylistOps.h"
#include "flp/FlpWrite.h"
#include "core/DawProject.h"
#include "core/FlpExport.h"
#include "core/FlpImport.h"
#include "core/Gzip.h"
#include "core/NoteEdit.h"
#include "core/RackRenderer.h"
#include "core/TakeCleanup.h"
#include "core/TrackImport.h"
#include "core/TrackVersions.h"

namespace tracks190pe
{
constexpr double rate = 48000.0;

// "kick.wav" a 60 Hz burst on every beat, "vox.wav" a steady 330 Hz sine, "bass.wav" 110 Hz
inline std::shared_ptr<const lynx::SampleData> sample (const std::string& path)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = rate;
    d->left.resize ((std::size_t) (rate * 4.0));
    const auto hz = path == "kick.wav" ? 60.0 : path == "bass.wav" ? 110.0 : 330.0;
    for (std::size_t i = 0; i < d->left.size(); ++i)
    {
        const auto t = (double) i / rate;
        d->left[i] = (float) (0.25 * std::sin (2.0 * 3.14159265358979 * hz * t) * (path == "kick.wav" ? std::exp (-t * 8.0) : 1.0));
    }
    return d;
}

inline std::vector<float> render (const lynx::Song& s, int frames)
{
    auto state = lynx::RackState::build (s, nullptr, rate, [] (const std::string& p) { return sample (p); });
    lynx::RackRenderer r;
    r.prepare (rate, 512);
    std::vector<float> all, l (512), rr (512);
    const auto tps = lynx::ticksPerSample (s.bpm, rate);
    for (int done = 0; done < frames; done += 512)
    {
        r.process (state.get(), true, done * tps, 512, l.data(), rr.data());
        all.insert (all.end(), l.begin(), l.end());
    }
    all.resize ((std::size_t) frames);
    return all;
}

inline lynx::ChannelDef channel (const char* name, lynx::InstrumentKind kind, const char* sample, int insert)
{
    lynx::ChannelDef c;
    c.name = name;
    c.instrument = kind;
    c.samplePath = sample;
    c.mixerTrack = insert;
    return c;
}

// Track 0 "Drums": a pattern of the Kick sampler (insert 1). Track 1 "Vox": an audio clip (insert 2, an EQ on it) and a
// volume lane. Track 2: an automation clip of the Vox insert's volume. 120 BPM.
inline lynx::Song song()
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.mixer = Song::defaultMixer (8);
    s.channels = { channel ("Kick", InstrumentKind::sampler, "kick.wav", 1), channel ("Vox", InstrumentKind::audioClip, "vox.wav", 2) };
    s.patterns.clear();
    Pattern p;
    p.name = "Beat";
    for (int b = 0; b < 4; ++b)
    {
        Note n;
        n.start = b * ppq;
        n.length = ppq / 2;
        n.channel = 0;
        p.notes.push_back (n);
    }
    s.patterns.push_back (p);
    Clip pc;
    pc.pattern = 0;
    pc.track = 0;
    pc.start = 0;
    pc.length = 4 * ppq;
    s.playlist.push_back (pc);
    Clip ac;
    ac.kind = ClipKind::audio;
    ac.channel = 1;
    ac.track = 1;
    ac.start = 0;
    ac.length = 8 * ppq;
    s.playlist.push_back (ac);
    auto eq = makeEffectSlot (EffectKind::eq);
    eq.params[0] = 0.25f;
    s.mixer[2].effects.push_back (eq);
    s.mixer[2].name = "Vox bus";
    s.playlistTracks.resize (3);
    s.playlistTracks[0].name = "Drums";
    s.playlistTracks[1].name = "Vox";
    // a lane of the Vox insert's volume
    Automation lane;
    lane.name = "Vox volume";
    lane.target = { AutomationTargetKind::mixerVolume, 2, 0, 0 };
    lane.points = { { 0, 0.8f }, { 8 * ppq, 0.4f } };
    s.automations.push_back (lane);
    s.playlistTracks[1].lanes.push_back ({ 0, 0 });
    // an automation clip of the Kick's volume on track 2
    Automation clipAuto;
    clipAuto.name = "Kick volume";
    clipAuto.target = { AutomationTargetKind::channelVolume, 0, 0, 0 };
    clipAuto.points = { { 0, 1.0f }, { 4 * ppq, 0.5f } };
    s.automations.push_back (clipAuto);
    Clip au;
    au.kind = ClipKind::automation;
    au.automation = 1;
    au.track = 2;
    au.length = 4 * ppq;
    s.playlist.push_back (au);
    return s;
}

// the clips of a track, sorted (the order of Song::playlist is not part of the content)
inline std::vector<lynx::Clip> clipsOf (const lynx::Song& s, int track)
{
    std::vector<lynx::Clip> out;
    for (const auto& c : s.playlist)
        if (c.track == track)
            out.push_back (c);
    std::sort (out.begin(), out.end(), [] (const lynx::Clip& a, const lynx::Clip& b) {
        return a.start != b.start ? a.start < b.start : a.kind != b.kind ? a.kind < b.kind : a.pattern < b.pattern;
    });
    return out;
}

inline double peak (const std::vector<float>& x)
{
    double p = 0.0;
    for (auto v : x)
        p = std::max (p, (double) std::abs (v));
    return p;
}
} // namespace tracks190pe

TEST (tracks190pe_versions_switch_swaps_clips_lanes_and_chain_exactly)
{
    using namespace lynx;
    namespace tv = trackversions;
    auto s = tracks190pe::song();
    const auto before = s;
    const auto voxClips = tracks190pe::clipsOf (s, 1);
    CHECK (tv::count (s, 1) == 0 && tv::chainInsert (s, 1) == 2);

    // a duplicate: the same content, its own copies, active at once
    const auto v = tv::add (s, 1, tv::Start::duplicate, "Take B", "Take A");
    CHECK (v == 1 && tv::count (s, 1) == 2 && tv::active (s, 1) == 1 && tv::nameOf (s, 1, 0) == "Take A");
    CHECK (tracks190pe::clipsOf (s, 1).size() == voxClips.size());
    CHECK (s.playlistTracks[1].lanes.size() == 1 && s.playlistTracks[1].lanes[0].automation != 0); // (a copy of the curve)
    CHECK (s.mixer[2].effects.size() == 1 && s.mixer[2].effects[0].params[0] == 0.25f);
    // version B changes: its clip shorter, its EQ another setting, its lane gone
    for (auto& c : s.playlist)
        if (c.track == 1 && c.kind == ClipKind::audio)
            c.length = 2 * ppq;
    s.mixer[2].effects[0].params[0] = 0.75f;
    const auto laneB = s.playlistTracks[1].lanes;
    s.playlistTracks[1].lanes.clear();
    const auto bClips = tracks190pe::clipsOf (s, 1);

    // back to A: exactly what was there
    CHECK (tv::switchTo (s, 1, 0));
    CHECK (tracks190pe::clipsOf (s, 1) == voxClips);
    CHECK (s.playlistTracks[1].lanes == before.playlistTracks[1].lanes);
    CHECK (s.mixer[2].effects.size() == 1 && s.mixer[2].effects[0].params[0] == 0.25f);
    // the other tracks never moved
    CHECK (tracks190pe::clipsOf (s, 0) == tracks190pe::clipsOf (before, 0));
    CHECK (tracks190pe::clipsOf (s, 2) == tracks190pe::clipsOf (before, 2));
    // and B again
    CHECK (tv::switchTo (s, 1, 1));
    CHECK (tracks190pe::clipsOf (s, 1) == bClips);
    CHECK (s.mixer[2].effects[0].params[0] == 0.75f && s.playlistTracks[1].lanes.empty());
    CHECK (! tv::switchTo (s, 1, 1)); // (already active)

    // removing the active one: A plays again, the track has no versions any more, B's lane curve went with it
    const auto automationsWithB = s.automations.size();
    CHECK (tv::remove (s, 1, 1));
    CHECK (tv::count (s, 1) == 0 && tracks190pe::clipsOf (s, 1) == voxClips && s.mixer[2].effects[0].params[0] == 0.25f);
    CHECK (s.automations.size() == automationsWithB - 1 + (laneB.empty() ? 1 : 0) || s.automations.size() == automationsWithB);
}

TEST (tracks190pe_versions_render_only_the_active_one)
{
    using namespace lynx;
    namespace tv = trackversions;
    auto s = tracks190pe::song();
    s.automations.clear(); // (no automation: the comparison is of the clips alone)
    s.playlistTracks[1].lanes.clear();
    s.playlist.erase (std::remove_if (s.playlist.begin(), s.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::automation; }), s.playlist.end());
    // version 2 of the Vox track is empty: only the drums sound
    tv::add (s, 1, tv::Start::empty, "Empty", "Full");
    auto drumsOnly = tracks190pe::song();
    drumsOnly.automations.clear();
    drumsOnly.playlistTracks[1].lanes.clear();
    drumsOnly.playlist.erase (std::remove_if (drumsOnly.playlist.begin(), drumsOnly.playlist.end(), [] (const Clip& c) { return c.track != 0; }),
                              drumsOnly.playlist.end());
    const auto frames = 48000 * 2;
    const auto a = tracks190pe::render (s, frames), b = tracks190pe::render (drumsOnly, frames);
    CHECK (tracks190pe::peak (a) > 0.01);
    CHECK (a == b);
    // the full one active: the render is the song without versions, sample for sample
    tv::switchTo (s, 1, 0);
    auto plain = tracks190pe::song();
    plain.automations.clear();
    plain.playlistTracks[1].lanes.clear();
    plain.playlist.erase (std::remove_if (plain.playlist.begin(), plain.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::automation; }), plain.playlist.end());
    CHECK (tracks190pe::render (s, frames) == tracks190pe::render (plain, frames));
    // keepActiveOnly (the exports): the same sound, no versions left
    auto stripped = s;
    CHECK (tv::keepActiveOnly (stripped) == 1 && ! tv::any (stripped));
    CHECK (tracks190pe::render (stripped, frames) == tracks190pe::render (plain, frames));
}

TEST (tracks190pe_versions_follow_renumbering_and_promote)
{
    using namespace lynx;
    namespace tv = trackversions;
    auto s = tracks190pe::song();
    tv::add (s, 0, tv::Start::duplicate, "B", "A"); // the Drums track: its pattern copied (only it plays the pattern)
    CHECK (s.patterns.size() == 2 && s.patterns[1].name == "Beat (B)");
    const auto stored = s.playlistTracks[0].versions[0].clips;
    CHECK (stored.size() == 1 && stored[0].pattern == 0);
    // a track moved above it: the stored clips follow the track (Song::forEachClipList visits them)
    noteedit::insertTrack (s, 0);
    CHECK (s.playlistTracks[1].versions.size() == 2 && s.playlistTracks[1].versions[0].clips[0].track == 1);
    // the pattern of version A removed: its stored clip goes with it, B's pattern takes index 0
    s.removePattern (0);
    CHECK (s.playlistTracks[1].versions[0].clips.empty());
    CHECK (tracks190pe::clipsOf (s, 1).size() == 1 && tracks190pe::clipsOf (s, 1)[0].pattern == 0);

    // promote: bars 2-3 of version A replace that part of the active one
    auto p = tracks190pe::song();
    tv::add (p, 1, tv::Start::empty, "Empty", "Full");
    CHECK (tracks190pe::clipsOf (p, 1).empty());
    const auto bar = 4 * ppq;
    CHECK (tv::promote (p, 1, 0, bar / 2, bar + bar / 2) == 1);
    const auto now = tracks190pe::clipsOf (p, 1);
    CHECK (now.size() == 1 && now[0].start == bar / 2 && now[0].length == bar && now[0].offset == bar / 2);
    CHECK (tv::promote (p, 1, 1, 0, bar) == -1); // (the active one)
}

TEST (tracks190pe_versions_lynx_round_trip_and_older_copies)
{
    using namespace lynx;
    namespace tv = trackversions;
    auto s = tracks190pe::song();
    tv::add (s, 1, tv::Start::duplicate, "Take B", "Take A");
    s.mixer[2].effects[0].params[0] = 0.5f;
    tv::switchTo (s, 1, 0);
    // (the save itself is in --selftest tracks190pe: ProjectFile is not in the test binary); the copy for older LYNX and the
    // exports keep the active version, the same sound
    auto copy = s;
    CHECK (tv::keepActiveOnly (copy) == 1);
    CHECK (tracks190pe::clipsOf (copy, 1) == tracks190pe::clipsOf (s, 1) && copy.mixer[2].effects == s.mixer[2].effects);
    CHECK (takecleanup::unusedAutomations (copy).empty());
    // the .flp export: only the active version, with its line
    flpexport::Report report;
    flpexport::Options o;
    o.lynxOnly = flpexport::LynxOnly::substitute;
    const auto project = flpexport::toProject (s, o, report);
    CHECK (std::any_of (report.notes.begin(), report.notes.end(), [] (const std::string& n) { return n.find ("Track versions: 1 version") == 0; }));
    flpexport::Report plainReport;
    flpexport::toProject (copy, o, plainReport);
    CHECK (std::none_of (plainReport.notes.begin(), plainReport.notes.end(), [] (const std::string& n) { return n.find ("Track versions") == 0; }));
    // the file is the one of the song with the active version alone, byte for byte
    CHECK (flp::writeProject (project, o.write) == flp::writeProject (flpexport::toProject (copy, o, plainReport), o.write));
    // the .als and DAWproject exports say it too
    const auto als = exportAls (s);
    CHECK (std::any_of (als.notes.begin(), als.notes.end(), [] (const std::string& n) { return n.find ("Track versions") == 0; }));
    const auto daw = exportDawProject (s);
    CHECK (std::any_of (daw.notes.begin(), daw.notes.end(), [] (const std::string& n) { return n.find ("Track versions") == 0; }));
}

TEST (tracks190pe_versions_with_take_lanes_and_arrangements)
{
    using namespace lynx;
    namespace tv = trackversions;
    // a comp track (0) with two take lanes (1, 2): a version of the comp holds the comp's clips; the lanes stay
    Song s;
    s.bpm = 120.0;
    s.mixer = Song::defaultMixer (4);
    s.channels = { tracks190pe::channel ("Take 1", InstrumentKind::audioClip, "vox.wav", 1), tracks190pe::channel ("Take 2", InstrumentKind::audioClip, "bass.wav", 1) };
    s.playlistTracks.resize (3);
    s.playlistTracks[0].compTrack = true;
    s.playlistTracks[1].grouped = s.playlistTracks[2].grouped = true;
    for (int lane = 0; lane < 2; ++lane)
    {
        Clip c;
        c.kind = ClipKind::audio;
        c.channel = lane;
        c.track = lane + 1;
        c.length = 4 * ppq;
        s.playlist.push_back (c);
    }
    CHECK (comp::swipe (s, 1, 0, 2 * ppq));
    const auto comp1 = tracks190pe::clipsOf (s, 0);
    CHECK (! comp1.empty());
    CHECK (tv::add (s, 0, tv::Start::empty, "Comp 2", "Comp 1") == 1);
    CHECK (comp::swipe (s, 2, 0, 4 * ppq));
    const auto comp2 = tracks190pe::clipsOf (s, 0);
    CHECK (tracks190pe::clipsOf (s, 1).size() == 1 && tracks190pe::clipsOf (s, 2).size() == 1); // (the lanes as they were)
    CHECK (tv::switchTo (s, 0, 0) && tracks190pe::clipsOf (s, 0) == comp1);
    CHECK (tv::switchTo (s, 0, 1) && tracks190pe::clipsOf (s, 0) == comp2);
    CHECK (comp::groupOf (s, 1).comp == 0);

    // another arrangement shown: the versions can't be switched; a removed arrangement takes its versions with it
    const auto other = s.addArrangement ("Second", false);
    CHECK (other == 1 && s.currentArrangement == 1 && ! tv::inCurrentArrangement (s, 0) && ! tv::switchTo (s, 0, 0));
    s.selectArrangement (0);
    CHECK (tv::inCurrentArrangement (s, 0));
    arrangeops::move (s, 0, 1);
    CHECK (s.playlistTracks[0].versionArrangement == 1 && tv::inCurrentArrangement (s, 0));
    s.selectArrangement (0);
    s.removeArrangement (1);
    CHECK (tv::count (s, 0) == 0);
}

TEST (tracks190pe_import_two_als_tracks_into_an_flp_project)
{
    using namespace lynx;
    // the other project: round 175a's Live set (Keys: a VST3 instrument, a looped clip, volume automation, a send to the
    // Reverb return; Vocal: a warped clip, a VST2 and an EQ Eight insert, routed to the Band group)
    const auto xmlBytes = als175a::bytesOf (als175a::richSet());
    const auto source = importAls (gzip::compressStored (xmlBytes.data(), xmlBytes.size()), als175a::options (nullptr));
    CHECK (source.has_value());
    if (! source)
        return;
    const auto list = trackimport::listTracks (*source);
    int keys = -1, vocal = -1;
    for (const auto& t : list)
    {
        if (t.name == "Keys")
            keys = t.track;
        if (t.name == "Vocal")
            vocal = t.track;
    }
    CHECK (keys >= 0 && vocal >= 0);

    // the open project: an FL project (an exported, read back .flp)
    auto made = tracks190pe::song();
    flpexport::Report fr;
    flpexport::Options fo;
    fo.lynxOnly = flpexport::LynxOnly::substitute;
    auto dest = importFlp (flp::parseProject (flp::writeProject (flpexport::toProject (made, fo, fr), fo.write)));
    const auto before = dest;

    trackimport::Options o;
    o.at = 0;
    const auto r = trackimport::importTracks (dest, *source, { keys, vocal }, o);
    CHECK (r.ok && r.tracks.size() >= 2);

    // what was there is unchanged: the old channels, patterns, automations, clips and inserts with content
    CHECK (dest.channels.size() > before.channels.size());
    CHECK (std::equal (before.channels.begin(), before.channels.end(), dest.channels.begin()));
    CHECK (std::equal (before.patterns.begin(), before.patterns.end(), dest.patterns.begin()));
    CHECK (std::equal (before.automations.begin(), before.automations.end(), dest.automations.begin()));
    for (int t = 0; t < (int) before.mixer.size(); ++t)
        if (std::find (r.inserts.begin(), r.inserts.end(), t) == r.inserts.end())
            CHECK (dest.mixer[(std::size_t) t] == before.mixer[(std::size_t) t]);
    std::vector<Clip> old;
    for (const auto& c : dest.playlist)
        if (std::find (r.tracks.begin(), r.tracks.end(), c.track) == r.tracks.end())
            old.push_back (c);
    CHECK (old == before.playlist);
    // (the rows above the new ones; the new tracks take empty rows under the last used one)
    for (int t = 0; t < (int) before.playlistTracks.size() && t < r.tracks[0]; ++t)
        CHECK (dest.playlistTracks[(std::size_t) t] == before.playlistTracks[(std::size_t) t]);

    // what came in: Keys' notes, Vocal's warped audio clip, the effects chain, the routing to the group and the return
    const auto* keysTrack = &dest.playlistTracks[(std::size_t) r.tracks[0]];
    const auto* vocalTrack = &dest.playlistTracks[(std::size_t) r.tracks[1]];
    CHECK (keysTrack->name == "Keys" && vocalTrack->name == "Vocal");
    int notes = 0, audioClips = 0;
    const Clip* vocalClip = nullptr;
    for (const auto& c : dest.playlist)
    {
        if (c.track == r.tracks[0] && c.kind == ClipKind::pattern)
            notes += (int) dest.patterns[(std::size_t) c.pattern].notes.size();
        if (c.track == r.tracks[1] && c.kind == ClipKind::audio)
            ++audioClips, vocalClip = &c;
    }
    CHECK (notes >= 2);
    CHECK (audioClips == 1 && vocalClip != nullptr);
    if (vocalClip != nullptr)
    {
        const auto& ch = dest.channels[(std::size_t) vocalClip->channel];
        const ChannelDef* src = nullptr;
        for (const auto& c : source->channels)
            if (c.instrument == InstrumentKind::audioClip && c.name == ch.name)
                src = &c;
        CHECK (src != nullptr && ch.sampler.warp == src->sampler.warp && ch.samplePath == src->samplePath && ch.sampler.keepPitch);
        CHECK (std::abs (vocalClip->stretch - 1.875) < 1e-9 && std::abs (vocalClip->pitch - 2.0f) < 1e-6);
        // its insert: the VST2 (Live's EQ Eight has no LYNX equivalent: the .als import's report), routed to the group's
        // new insert and the Reverb return's, which came too
        const auto ins = ch.mixerTrack;
        CHECK (ins > 0 && std::find (r.inserts.begin(), r.inserts.end(), ins) != r.inserts.end());
        CHECK (dest.mixer[(std::size_t) ins].effects.size() == 1 && dest.mixer[(std::size_t) ins].effects[0].plugin.foreign.format == ForeignPlugin::Format::vst2);
        CHECK (r.inserts.size() == 4); // Keys, Vocal, the Band group and the Reverb return
        int routedTo = 0;
        for (const auto& s : dest.mixer[(std::size_t) ins].sends)
            if (std::find (r.inserts.begin(), r.inserts.end(), s.target) != r.inserts.end())
                ++routedTo;
        CHECK (routedTo == 2);
    }
    // the automation of Keys' volume came along (a lane or a clip driving one of the new inserts)
    CHECK (r.automations >= 1);
    for (std::size_t a = before.automations.size(); a < dest.automations.size(); ++a)
    {
        const auto& t = dest.automations[a].target;
        CHECK (t.kind == AutomationTargetKind::none || std::find (r.inserts.begin(), r.inserts.end(), t.index) != r.inserts.end()
               || std::find (r.channels.begin(), r.channels.end(), t.index) != r.channels.end());
    }
    // the new tracks are under the old ones
    CHECK (r.tracks[0] >= (int) before.playlistTracks.size() || r.tracks[0] > 2);
}

TEST (tracks190pe_import_without_inserts_or_automation_and_at_a_time)
{
    using namespace lynx;
    const auto source = tracks190pe::song();
    auto dest = tracks190pe::song();
    trackimport::Options o;
    o.withInserts = false;
    o.withAutomation = false;
    o.at = 8 * ppq;
    const auto r = trackimport::importTracks (dest, source, { 1 }, o);
    CHECK (r.ok && r.tracks.size() == 1 && r.tracks[0] == 3 && r.inserts.empty() && r.automations == 0);
    CHECK (r.channels.size() == 1 && dest.channels[(std::size_t) r.channels[0]].mixerTrack == 0);
    const auto in = tracks190pe::clipsOf (dest, 3);
    CHECK (in.size() == 1 && in[0].start == 8 * ppq && in[0].kind == ClipKind::audio);
    CHECK (dest.playlistTracks[3].lanes.empty());

    // with everything: the Vox bus (its EQ), the lane and the automation clip of the Kick when the drums come too
    auto full = tracks190pe::song();
    const auto all = trackimport::importTracks (full, source, { 0, 1 }, {});
    CHECK (all.inserts.size() == 2);
    const auto vox = full.channels[(std::size_t) all.channels[1]];
    CHECK (vox.name == "Vox" && full.mixer[(std::size_t) vox.mixerTrack].name == "Vox bus" && full.mixer[(std::size_t) vox.mixerTrack].effects.size() == 1);
    CHECK (full.playlistTracks[(std::size_t) all.tracks[1]].lanes.size() == 1);
    const auto lane = full.automations[(std::size_t) full.playlistTracks[(std::size_t) all.tracks[1]].lanes[0].automation];
    CHECK (lane.target.kind == AutomationTargetKind::mixerVolume && lane.target.index == vox.mixerTrack);
    // the Kick's automation clip lives on track 2 of the source: it came on a track of its own, driving the new Kick
    CHECK (all.tracks.size() == 3);
    const auto kickAuto = tracks190pe::clipsOf (full, all.tracks[2]);
    CHECK (kickAuto.size() == 1 && full.automations[(std::size_t) kickAuto[0].automation].target.index == all.channels[0]);
    // twice: a second copy (new channels, new inserts), nothing merged into the first
    const auto again = trackimport::importTracks (full, source, { 1 }, {});
    CHECK (again.channels.size() == 1 && again.channels[0] != all.channels[1] && again.inserts.size() == 1 && again.inserts[0] != vox.mixerTrack);
    // a missing sample's path goes back on its channel, for the Missing files window
    auto lost = source;
    lost.channels[1].samplePath.clear();
    CHECK (trackimport::restoreMissingPaths (lost, { { "D:/old/vox.wav", { "Audio clip: Vox" } } }) == 1 && lost.channels[1].samplePath == "D:/old/vox.wav");
}

TEST (tracks190pe_import_brings_versions_and_take_lanes)
{
    using namespace lynx;
    namespace tv = trackversions;
    auto source = tracks190pe::song();
    tv::add (source, 1, tv::Start::duplicate, "B", "A");
    // a folder: track 3 heads tracks 4 and 5
    source.playlistTracks.resize (6);
    source.playlistTracks[3].name = "Folder";
    source.playlistTracks[4].grouped = source.playlistTracks[5].grouped = true;
    CHECK ((trackimport::expandSelection (source, { 3 }) == std::vector<int> { 3, 4, 5 }));
    auto dest = tracks190pe::song();
    const auto r = trackimport::importTracks (dest, source, { 1 }, {});
    CHECK (r.ok && r.tracks.size() >= 1);
    const auto& t = dest.playlistTracks[(std::size_t) r.tracks[0]];
    CHECK (t.versions.size() == 2 && t.activeVersion == 1 && t.versions[0].clips.size() == 1 && t.versions[0].hasChain);
    CHECK (t.versions[0].clips[0].track == r.tracks[0] && t.versions[0].clips[0].channel == r.channels[0]);
    // and it switches there
    CHECK (tv::switchTo (dest, r.tracks[0], 0));
}
