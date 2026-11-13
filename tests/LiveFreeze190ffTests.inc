// Track freeze, Flatten / Bounce Track in Place (round 190ff; core/StandIns.h lynx::freeze, the Clip-launch workflow's
// Freeze Track as Live's): the channel with its volume and pan, through its insert's effects, rendered to audio that
// plays into the same insert while the instrument and the effects are not loaded. Uses the 190ay helpers
// (Freeze190ayTests.inc: the song, the renderer, the cut) — the stems are made the way sessionpack::renderFreeze makes
// them with insert > 0.

#include <tuple>

#include "core/ExportOptions.h"
#include "core/MixerOps.h"
#include "core/StandIns.h"

namespace
{
// 190ay's song with the Pad moved to insert 3: insert 1 (delay + reverb) is the Lead's own. The insert's fader, pan and
// EQ are off their defaults (they must stay live), and the lead's volume and the reverb's mix are automated (baked).
lynx::Song liveFreeze190ffSong()
{
    auto s = freeze190aySong();
    s.channels[2].mixerTrack = 3;
    auto& insert = s.mixer[1];
    insert.volume = 0.6f;
    insert.pan = -0.4f;
    insert.eq.gainDb[1] = 4.0f;
    // the lead's volume falls over the song, the reverb's mix rises (one clip each from the song's start)
    // (and the kick's insert pan, held at the centre: a lane the freeze leaves alone. The renderer works in shorter
    // chunks while any automation plays — RackRenderer, 128 samples instead of 512 — and some instruments sound a hair
    // different then, about -66 dB: the freeze's render songs keep the song's chunking, Song::automationChunks; the
    // rules test checks that the projection does too without this lane.)
    for (const auto& [target, from, to] : { std::tuple { lynx::AutomationTarget { lynx::AutomationTargetKind::channelVolume, 0, 0, 0 }, 0.7f, 0.4f },
                                            std::tuple { lynx::AutomationTarget { lynx::AutomationTargetKind::effectMix, 1, 1, 0 }, 0.3f, 1.0f },
                                            std::tuple { lynx::AutomationTarget { lynx::AutomationTargetKind::mixerPan, 2, 0, 0 }, 0.5f, 0.5f } })
    {
        lynx::Automation a;
        a.target = target;
        a.points = { { 0, from }, { 4 * 3840, to } };
        s.automations.push_back (a);
        lynx::Clip c;
        c.kind = lynx::ClipKind::automation;
        c.automation = (int) s.automations.size() - 1;
        c.track = 5;
        c.start = 0;
        c.length = 4 * 3840;
        s.playlist.push_back (c);
    }
    lynx::uids::normalize (s);
    return s;
}

// The delay compensation's latency at the Master (samples): an export trims it from the start (ExportJob trimPdc), and
// the live engine delays every path by it while an effect with latency plays.
int liveFreeze190ffLatency (const lynx::Song& song)
{
    return lynx::RackState::build (song, nullptr, freeze190ayRate, lynx::RackState::SampleProvider {})->pdc.total;
}

// a render as the export makes it: the latency's silence trimmed from the start
std::vector<float> liveFreeze190ffRender (const lynx::Song& song, std::int64_t ticks, const lynx::RackState::SampleProvider& provider, bool songMode)
{
    const auto latency = (std::size_t) liveFreeze190ffLatency (song);
    const auto tps = lynx::ticksPerSample (song.bpm, freeze190ayRate);
    auto all = freeze190ayRender (song, ticks + (std::int64_t) std::ceil ((double) latency * tps), provider, songMode);
    all.erase (all.begin(), all.begin() + (std::ptrdiff_t) std::min (all.size(), latency * 2));
    all.resize ((std::size_t) std::llround ((double) ticks / tps) * 2);
    return all;
}

// What sessionpack::renderFreeze (insert > 0) does.
void liveFreeze190ffFreeze (lynx::Song& song, int channel, Freeze190ayFiles& files)
{
    const auto tps = lynx::ticksPerSample (song.bpm, freeze190ayRate);
    const auto tail = lynx::standins::tailTicks (song);
    const auto insert = lynx::freeze::trackInsert (song, channel);
    const auto tag = "track" + std::to_string (channel);
    std::vector<lynx::StandIn> stems;
    {
        auto renderSong = lynx::standins::channelTimelineRender (song, channel, true);
        lynx::freeze::lighten (renderSong, channel, insert);
        std::int64_t music = 0;
        const auto exported = lynx::makeExportSong (renderSong, true, tail, music);
        const auto all = liveFreeze190ffRender (exported, music + tail, {}, true);
        files[tag + " whole"] = freeze190ayCut (all, tps, 0, music + tail);
        lynx::StandIn st;
        st.path = tag + " whole";
        st.lengthTicks = song.songLength() + tail;
        stems.push_back (st);
    }
    auto plan = lynx::standins::channelPatternRender (song, channel, true);
    lynx::freeze::lighten (plan.song, channel, insert);
    const auto all = liveFreeze190ffRender (plan.song, plan.end, {}, true);
    for (const auto& seg : plan.segments)
    {
        lynx::StandIn st;
        st.pattern = seg.patternUid;
        st.loop = true;
        st.path = tag + " loop" + std::to_string (seg.pattern);
        st.lengthTicks = seg.length;
        files[st.path] = freeze190ayCut (all, tps, seg.loopFrom, seg.length);
        stems.push_back (st);
    }
    lynx::freeze::apply (song, song.channels[(std::size_t) channel].uid, stems, "test", insert);
}

bool liveFreeze190ffDetached (const lynx::Song& s, lynx::AutomationTargetKind kind, int index)
{
    return std::none_of (s.automations.begin(), s.automations.end(), [&] (const lynx::Automation& a) { return a.target.kind == kind && a.target.index == index; });
}
} // namespace

TEST (livefreeze190ff_track_freeze_plays_like_the_live_track)
{
    // Song mode: the frozen track (lead + its volume automation + its insert's delay and reverb with the reverb's mix
    // automated) nulls against the live track; the insert's EQ, fader and pan act on the stem live.
    const auto song = liveFreeze190ffSong();
    CHECK (lynx::freeze::whyNotTrack (song, 0) == nullptr && lynx::freeze::trackInsert (song, 0) == 1);
    const auto end = song.songLength();
    const auto tps = lynx::ticksPerSample (song.bpm, freeze190ayRate);
    auto frames = [tps] (std::int64_t ticks) { return (std::size_t) std::llround ((double) ticks / tps); };

    auto frozen = song;
    Freeze190ayFiles files;
    liveFreeze190ffFreeze (frozen, 0, files);
    CHECK (frozen.channels[0].freeze.on() && frozen.channels[0].freeze.insert == 1 && lynx::freeze::trackFreezeHolds (frozen, 0));
    CHECK (lynx::freeze::frozenByInsert (frozen, 1) == 0 && lynx::freeze::frozenByInsert (frozen, 2) == -1);
    CHECK (frozen.mixer[1] == song.mixer[1]); // (the project keeps its effects: Unfreeze brings them back)
    auto provider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        auto it = files.find (path);
        return it != files.end() ? it->second : nullptr;
    };

    const auto played = lynx::standins::project (frozen, [] (const lynx::PluginRef&) { return false; });
    CHECK (played.channels[0].instrument == lynx::InstrumentKind::audioClip);
    CHECK (played.mixer[1].effects.empty()); // the effects are not built: no CPU, no RAM
    CHECK (played.mixer[1].volume == 0.6f && played.mixer[1].pan == -0.4f && played.mixer[1].eq == song.mixer[1].eq);
    CHECK (played.channels[0].volume == 1.0f && played.channels[0].pan == 0.0f && played.channels[0].mixerTrack == 1);
    CHECK (liveFreeze190ffDetached (played, lynx::AutomationTargetKind::channelVolume, 0));
    CHECK (liveFreeze190ffDetached (played, lynx::AutomationTargetKind::effectMix, 1));
    CHECK (played.mixer[3] == song.mixer[3] && played.channels[2] == song.channels[2]);

    const auto live = liveFreeze190ffRender (song, end, {}, true);
    const auto ice = liveFreeze190ffRender (played, end, provider, true);
    const auto whole = freeze190ayCompare (live, ice);
    const auto tail = freeze190ayCompare (live, ice, frames (2 * 3840 + 240), frames (3 * 3840));
    std::printf ("  track frozen, Song mode: peak %.4f, residual %.1f dB (whole song), %.1f dB (tail after the last note)\n", whole.peak, whole.dB(), tail.dB());
    CHECK (whole.peak > 0.05f && whole.dB() < -100.0);
    CHECK (tail.peak > 1.0e-4f && tail.dB() < -90.0);

    // the track alone: nothing masks the difference
    auto soloLive = song, soloIce = played;
    for (std::size_t c = 1; c < 3; ++c)
        soloLive.channels[c].muted = soloIce.channels[c].muted = true;
    const auto alone = freeze190ayCompare (liveFreeze190ffRender (soloLive, end, {}, true), liveFreeze190ffRender (soloIce, end, provider, true));
    std::printf ("  track frozen, the track alone: peak %.4f, residual %.1f dB\n", alone.peak, alone.dB());
    CHECK (alone.peak > 0.02f && alone.dB() < -100.0);

    // the mixer stays live: the insert's fader moved after the freeze changes both the same way
    auto louderLive = song, louderIce = played;
    louderLive.mixer[1].volume = louderIce.mixer[1].volume = 0.75f;
    const auto louder = freeze190ayCompare (liveFreeze190ffRender (louderLive, end, {}, true), liveFreeze190ffRender (louderIce, end, provider, true));
    std::printf ("  track frozen, the insert's fader moved afterwards: residual %.1f dB\n", louder.dB());
    CHECK (louder.dB() < -100.0);

    // Pattern mode: pattern A looping; the stem is the fourth pass of a loop (the earlier passes' delay and reverb tails
    // in it): the first pass differs (no earlier tail at all), the next ones less and less, from the fourth on the live
    // track and the stem null. With and without automation of what the stems hold (Pattern mode plays none of it: the
    // loop pass was rendered where the lanes have ended, as they hold in Pattern mode). Printed per pass.
    for (const bool automated : { true, false })
    {
        auto base = song;
        if (! automated)
        {
            base.automations.resize (2); // (the kick's pan lane stays: the renderer's chunking, see the song)
            for (int a = 0; a < 2; ++a)
                base.automations[(std::size_t) a].target = { lynx::AutomationTargetKind::mixerPan, 3, 0, 0 };
        }
        auto loopIce = base;
        Freeze190ayFiles loopFiles;
        liveFreeze190ffFreeze (loopIce, 0, loopFiles);
        auto loopProvider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
            auto it = loopFiles.find (path);
            return it != loopFiles.end() ? it->second : nullptr;
        };
        auto loopLive = base;
        loopLive.currentPattern = loopIce.currentPattern = 0;
        loopIce.mode = lynx::PlayMode::pattern;
        const auto loopPlayed = lynx::standins::project (loopIce, [] (const lynx::PluginRef&) { return false; });
        CHECK (loopPlayed.mixer[1].effects.empty());
        const auto l1 = liveFreeze190ffRender (loopLive, 5 * 3840, {}, false);
        const auto l2 = liveFreeze190ffRender (loopPlayed, 5 * 3840, loopProvider, false);
        for (int pass = 0; pass < 5; ++pass)
        {
            const auto body = freeze190ayCompare (l1, l2, frames (pass * 3840), frames ((pass + 1) * 3840));
            std::printf ("    Pattern mode%s, pass %d: peak %.4f, residual %.1f dB\n", automated ? " (automated volume and mix)" : "", pass + 1, body.peak, body.dB());
            if (pass >= 3)
                CHECK (body.dB() < -120.0);
        }
    }
}

TEST (livefreeze190ff_track_freeze_rules)
{
    auto song = liveFreeze190ffSong();
    CHECK (lynx::freeze::whyNotTrack (song, 0) == nullptr);
    CHECK (lynx::freeze::whyNotTrack (song, 1) == nullptr && lynx::freeze::trackInsert (song, 1) == 2); // the kick, insert 2
    // 190ay's song: lead and pad share insert 1 — its effects cannot be frozen with one of them
    auto shared = freeze190aySong();
    CHECK (lynx::freeze::whyNot (shared, 0) == nullptr && lynx::freeze::whyNotTrack (shared, 0) != nullptr);
    // a send from another insert into it
    auto sent = song;
    sent.mixer[2].sends.push_back ({ 1, 0.5f });
    CHECK (lynx::freeze::whyNotTrack (sent, 0) != nullptr);
    // an effect side-chained from another insert, or a Transmitter-style send out of it
    auto side = song;
    side.mixer[1].effects[0].sidechain = 2;
    CHECK (lynx::freeze::whyNotTrack (side, 0) != nullptr);
    auto out = song;
    out.mixer[1].effects[1].sendTracks[0] = 4;
    CHECK (lynx::freeze::whyNotTrack (out, 0) != nullptr);
    // an audio input on the insert
    auto input = song;
    input.mixer[1].inputChannel = 0;
    CHECK (lynx::freeze::whyNotTrack (input, 0) != nullptr);
    // straight to the Master: a track freeze is the channel freeze
    auto master = song;
    master.channels[0].mixerTrack = 0;
    CHECK (lynx::freeze::trackInsert (master, 0) == 0 && lynx::freeze::whyNotTrack (master, 0) == nullptr);

    // a track freeze that stops holding plays live, instrument and effects, bit for bit
    auto frozen = song;
    Freeze190ayFiles files;
    liveFreeze190ffFreeze (frozen, 0, files);
    auto joined = frozen;
    joined.channels[2].mixerTrack = 1; // the pad plays into the frozen insert now
    CHECK (! lynx::freeze::trackFreezeHolds (joined, 0) && lynx::freeze::frozenByInsert (joined, 1) == -1);
    const auto joinedPlayed = lynx::standins::project (joined, [] (const lynx::PluginRef&) { return false; });
    CHECK (joinedPlayed.channels[0].instrument == lynx::InstrumentKind::synth && ! joinedPlayed.channels[0].freeze.on());
    CHECK (joinedPlayed.mixer[1].effects.size() == 2);
    auto joinedLive = song;
    joinedLive.channels[2].mixerTrack = 1;
    const auto end = song.songLength();
    const auto same = freeze190ayCompare (liveFreeze190ffRender (joinedLive, end, {}, true), liveFreeze190ffRender (joinedPlayed, end, {}, true));
    CHECK (same.peak > 0.05f && same.worst == 0.0f);
    auto moved = frozen;
    moved.channels[0].mixerTrack = 4;
    CHECK (! lynx::freeze::trackFreezeHolds (moved, 0));

    // the insert's numbers follow a mixer move; deleted, the freeze plays live (-1)
    auto renumbered = frozen;
    lynx::mixerops::swapTracks (renumbered, 1, 6);
    CHECK (renumbered.channels[0].mixerTrack == 6 && renumbered.channels[0].freeze.insert == 6 && lynx::freeze::trackFreezeHolds (renumbered, 0));
    auto gone = frozen;
    CHECK (lynx::mixerops::deleteTrack (gone, 1));
    CHECK (gone.channels[0].freeze.insert == -1 && ! lynx::freeze::trackFreezeHolds (gone, 0));
    CHECK (! lynx::standins::project (gone, [] (const lynx::PluginRef&) { return false; }).channels[0].freeze.on());

    // the projection keeps the song's chunking when the freeze took its only automation away (Song::automationChunks)
    auto lanes = song;
    lanes.automations.resize (2);
    auto lanesFrozen = lanes;
    Freeze190ayFiles lanesFiles;
    liveFreeze190ffFreeze (lanesFrozen, 0, lanesFiles);
    const auto lanesPlayed = lynx::standins::project (lanesFrozen, [] (const lynx::PluginRef&) { return false; });
    CHECK (std::all_of (lanesPlayed.automations.begin(), lanesPlayed.automations.end(), [] (const lynx::Automation& a) { return a.target.kind == lynx::AutomationTargetKind::none; }));
    CHECK (lanesPlayed.automationChunks && ! lanes.automationChunks);
    auto lanesKickLive = lanes, lanesKickIce = lanesPlayed;
    lanesKickLive.channels[0].muted = lanesKickIce.channels[0].muted = true;
    const auto kick = freeze190ayCompare (liveFreeze190ffRender (lanesKickLive, song.songLength(), {}, true), liveFreeze190ffRender (lanesKickIce, song.songLength(), {}, true));
    std::printf ("  the other channels after the freeze took the song's last automation: residual %.1f dB\n", kick.dB());
    CHECK (kick.peak > 0.05f && kick.worst == 0.0f);

    // the render songs: the lead with its volume and pan, its insert at unity with its effects, the other inserts idle
    auto r = lynx::standins::channelTimelineRender (song, 0, true);
    lynx::freeze::lighten (r, 0, 1);
    CHECK (r.channels[0].volume == song.channels[0].volume && r.channels[0].pan == song.channels[0].pan && r.channels[0].mixerTrack == 1);
    CHECK (r.mixer[1].effects == song.mixer[1].effects && r.mixer[1].volume == lynx::unityFader && r.mixer[1].pan == 0.0f && ! r.mixer[1].eq.active());
    CHECK (r.channels[1].muted && r.channels[2].muted);
    CHECK (! liveFreeze190ffDetached (r, lynx::AutomationTargetKind::effectMix, 1) && ! liveFreeze190ffDetached (r, lynx::AutomationTargetKind::channelVolume, 0));

    // thawed / clear forget the insert too
    CHECK (lynx::freeze::thawed (frozen).channels[0].freeze == lynx::FreezeState {});
    auto cleared = frozen;
    lynx::freeze::clear (cleared, 0);
    CHECK (cleared.channels[0].freeze.insert == 0 && ! cleared.channels[0].freeze.on());
}

TEST (livefreeze190ff_flatten_keeps_the_frozen_sound)
{
    // Flatten (Bounce Track in Place on a frozen track): the lead becomes an audio clip channel playing the stem at its
    // clips' places, the notes and the insert's effects go; the arrangement then plays bit for bit what the frozen
    // track played, and so what the live track played.
    const auto song = liveFreeze190ffSong();
    auto frozen = song;
    Freeze190ayFiles files;
    liveFreeze190ffFreeze (frozen, 0, files);
    auto provider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        auto it = files.find (path);
        return it != files.end() ? it->second : nullptr;
    };
    const auto end = song.songLength();
    const auto icePlayed = lynx::standins::project (frozen, [] (const lynx::PluginRef&) { return false; });
    const auto ice = liveFreeze190ffRender (icePlayed, end, provider, true);
    const auto live = liveFreeze190ffRender (song, end, {}, true);

    auto flat = frozen;
    CHECK (lynx::freeze::flatten (flat, 0));
    const auto& lead = flat.channels[0];
    CHECK (lead.instrument == lynx::InstrumentKind::audioClip && ! lead.freeze.on() && lead.samplePath == "track0 whole");
    CHECK (lead.name == "Lead (Bounce)" && lead.uid == song.channels[0].uid && lead.mixerTrack == 1 && lead.volume == 1.0f);
    CHECK (flat.mixer[1].effects.empty() && flat.mixer[1].volume == 0.6f && flat.mixer[1].eq == song.mixer[1].eq);
    CHECK (liveFreeze190ffDetached (flat, lynx::AutomationTargetKind::channelVolume, 0) && liveFreeze190ffDetached (flat, lynx::AutomationTargetKind::effectMix, 1));
    for (const auto& p : flat.patterns)
        CHECK (std::none_of (p.notes.begin(), p.notes.end(), [] (const lynx::Note& n) { return n.channel == 0; }));
    // one audio clip per clip of the lead, at its place and on its track (A at 0, A at 1 bar... the late A from its
    // second beat), with tail clips between them; the pattern clips stay (the kick and the pad still play in them)
    int audio = 0, patterns = 0;
    for (const auto& c : flat.playlist)
    {
        audio += c.kind == lynx::ClipKind::audio && c.channel == 0;
        patterns += c.kind == lynx::ClipKind::pattern;
        if (c.kind == lynx::ClipKind::audio)
            CHECK (c.offset == c.start && c.length > 0);
    }
    std::printf ("  flatten: %d audio clips, %d pattern clips left\n", audio, patterns);
    CHECK (audio >= 4 && patterns == 5);
    CHECK (std::any_of (flat.playlist.begin(), flat.playlist.end(), [] (const lynx::Clip& c) {
        return c.kind == lynx::ClipKind::audio && c.start == 4 * 3840 && c.length == 2880 && c.track == 0;
    }));
    const auto flatAudio = liveFreeze190ffRender (flat, end, provider, true);
    const auto vsIce = freeze190ayCompare (ice, flatAudio), vsLive = freeze190ayCompare (live, flatAudio);
    std::printf ("  flattened vs frozen: residual %.1f dB; vs live: %.1f dB\n", vsIce.dB(), vsLive.dB());
    CHECK (vsIce.peak > 0.05f && vsIce.dB() < -120.0);
    CHECK (vsLive.dB() < -100.0);
    CHECK (flat.songLength() == end);

    // a channel freeze (no insert) flattens too: its volume, pan and insert stay live
    auto channelFrozen = freeze190aySong();
    Freeze190ayFiles channelFiles;
    freeze190ayFreeze (channelFrozen, 0, channelFiles);
    auto channelProvider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        auto it = channelFiles.find (path);
        return it != channelFiles.end() ? it->second : nullptr;
    };
    auto channelFlat = channelFrozen;
    CHECK (lynx::freeze::flatten (channelFlat, 0));
    CHECK (channelFlat.channels[0].volume == 0.6f && channelFlat.channels[0].pan == 0.3f && channelFlat.mixer[1].effects.size() == 2);
    const auto plain = freeze190aySong();
    const auto c1 = freeze190ayCompare (liveFreeze190ffRender (plain, plain.songLength(), {}, true), liveFreeze190ffRender (channelFlat, plain.songLength(), channelProvider, true));
    std::printf ("  channel freeze flattened vs live: residual %.1f dB\n", c1.dB());
    CHECK (c1.dB() < -100.0);

    // nothing to flatten: not frozen, or a track freeze that does not hold
    auto plainSong = song;
    CHECK (! lynx::freeze::flatten (plainSong, 0) && plainSong.channels == song.channels);
    auto joined = frozen;
    joined.channels[2].mixerTrack = 1;
    CHECK (! lynx::freeze::flatten (joined, 0));
}

namespace
{
// One instrument per pattern, one track per channel (a Clip-launch song): Lead (insert 1: delay + reverb, fader, pan
// and EQ set) on track 0 with a gap between its clips, Kick (insert 2) on track 1; an audio channel (a sine burst, its
// clips with gain and fades) on track 2, insert 3. Inserts from 4 on are unused.
lynx::Song liveFreeze190ffTracks (Freeze190ayFiles& files)
{
    auto s = liveFreeze190ffSong();
    s.patterns.clear();
    s.playlist.clear();
    s.automations.clear();
    s.channels.resize (2);
    for (int p = 0; p < 2; ++p)
    {
        auto& pattern = s.patterns.emplace_back();
        pattern.name = p == 0 ? "Lead" : "Kick";
        pattern.lengthTicks = 3840;
        for (int k = 0; k < 4; ++k)
            pattern.notes.push_back (freeze190ayNote (p, k * 960, p == 0 ? 600 : 200, p == 0 ? 55 + 3 * k : 36));
        pattern.notes.push_back (freeze190ayNote (p, 3600, 600, 70)); // (runs past the pattern's end: cut at a clip's end)
        pattern.sortNotes();
    }
    lynx::ChannelDef tone;
    tone.name = "Tone";
    tone.instrument = lynx::InstrumentKind::audioClip;
    tone.samplePath = "tone";
    tone.volume = 0.7f;
    tone.pan = -0.2f;
    tone.mixerTrack = 3;
    s.channels.push_back (tone);
    auto sine = std::make_shared<lynx::SampleData>();
    sine->sampleRate = freeze190ayRate;
    for (int i = 0; i < 48000; ++i)
    {
        const auto v = 0.4f * (float) std::sin (2.0 * 3.141592653589793 * 330.0 * i / freeze190ayRate) * (1.0f - (float) i / 48000.0f);
        sine->left.push_back (v);
        sine->right.push_back (v * 0.8f);
    }
    files["tone"] = sine;
    const std::int64_t leadAt[] = { 0, 3840, 3 * 3840 };
    for (const auto at : leadAt)
    {
        lynx::Clip c;
        c.pattern = 0;
        c.track = 0;
        c.start = at;
        c.length = at == 3840 ? 1920 : 3840; // (the second clip stops halfway)
        s.playlist.push_back (c);
    }
    for (int k = 0; k < 4; ++k)
    {
        lynx::Clip c;
        c.pattern = 1;
        c.track = 1;
        c.start = k * 3840;
        c.length = 3840;
        s.playlist.push_back (c);
    }
    for (int k = 0; k < 2; ++k)
    {
        lynx::Clip c;
        c.kind = lynx::ClipKind::audio;
        c.channel = 2;
        c.track = 2;
        c.start = 960 + k * 3840;
        c.length = 1920;
        c.offset = k * 240;
        c.gain = k == 0 ? 0.8f : 1.2f;
        c.fadeIn = 120;
        c.fadeOut = 240;
        s.playlist.push_back (c);
    }
    std::stable_sort (s.playlist.begin(), s.playlist.end(), [] (const lynx::Clip& a, const lynx::Clip& b) { return a.start < b.start; });
    lynx::uids::normalize (s);
    return s;
}

std::vector<int> liveFreeze190ffClipsOn (const lynx::Song& s, int track)
{
    std::vector<int> out;
    for (int i = 0; i < (int) s.playlist.size(); ++i)
        if (s.playlist[(std::size_t) i].track == track && s.playlist[(std::size_t) i].kind != lynx::ClipKind::automation)
            out.push_back (i);
    return out;
}

// what the export of a render song's time selection makes (ExportJob: Range::selection, the tail cut)
std::shared_ptr<const lynx::SampleData> liveFreeze190ffSelection (const lynx::Song& renderSong, std::int64_t& ticks,
                                                                   const lynx::RackState::SampleProvider& provider = {})
{
    lynx::exportopts::Options o;
    o.range = lynx::exportopts::Range::selection;
    o.tail = lynx::exportopts::TailMode::cut;
    std::int64_t music = 0, loop = 0;
    const auto song = lynx::exportopts::makeRenderSong (renderSong, o, 0, music, loop);
    ticks = music;
    const auto all = liveFreeze190ffRender (song, music, provider, true);
    return freeze190ayCut (all, lynx::ticksPerSample (song.bpm, freeze190ayRate), 0, music);
}
} // namespace

TEST (livefreeze190ff_bounce_to_new_track_and_consolidate)
{
    Freeze190ayFiles files;
    const auto song = liveFreeze190ffTracks (files);
    auto provider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        auto it = files.find (path);
        return it != files.end() ? it->second : nullptr;
    };
    const auto end = song.songLength();
    const auto tps = lynx::ticksPerSample (song.bpm, freeze190ayRate);
    auto frames = [tps] (std::int64_t ticks) { return (std::size_t) std::llround ((double) ticks / tps); };
    const auto live = liveFreeze190ffRender (song, end, provider, true);

    // Bounce to New Track: the Lead's clips, through its devices, onto a new track under it; the clips stay
    {
        lynx::freeze::BouncePlan plan;
        CHECK (lynx::freeze::planBounceToNewTrack (song, liveFreeze190ffClipsOn (song, 0), plan) == nullptr);
        CHECK (plan.channel == 0 && plan.insert == 1 && plan.track == 0 && plan.start == 0 && plan.end == 4 * 3840 && plan.clips.size() == 3);
        std::int64_t ticks = 0;
        files["bounce"] = liveFreeze190ffSelection (lynx::freeze::bounceRenderSong (song, plan), ticks, provider);
        CHECK (ticks == plan.end - plan.start);
        auto bounced = song;
        const auto channel = lynx::freeze::applyBounceToNewTrack (bounced, plan, "bounce", ticks);
        const auto& made = bounced.channels[3];
        CHECK (channel == 3 && made.instrument == lynx::InstrumentKind::audioClip && made.name == "Lead (Bounce)" && made.mixerTrack == 4);
        CHECK (bounced.mixer[4].volume == song.mixer[1].volume && bounced.mixer[4].pan == song.mixer[1].pan && bounced.mixer[4].eq == song.mixer[1].eq
               && bounced.mixer[4].effects.empty());
        CHECK (bounced.channels[0] == song.channels[0] && bounced.mixer[1] == song.mixer[1]);
        int onNew = 0, movedKick = 0, mutedLead = 0;
        for (const auto& c : bounced.playlist)
        {
            onNew += c.kind == lynx::ClipKind::audio && c.channel == 3 && c.track == 1 && c.start == 0 && c.length == 4 * 3840;
            movedKick += c.kind == lynx::ClipKind::pattern && c.pattern == 1 && c.track == 2;
            mutedLead += c.kind == lynx::ClipKind::pattern && c.pattern == 0 && c.muted;
        }
        CHECK (onNew == 1 && movedKick == 4 && mutedLead == 3); // (the source clips muted; the tracks below moved down one)
        // the song with the Lead's clips muted and the new track playing sounds as before over the bounced span (the new
        // track has the source's mixer)
        const auto d = freeze190ayCompare (live, liveFreeze190ffRender (bounced, end, provider, true), 0, frames (plan.end));
        std::printf ("  bounce to new track (sources muted) vs live: peak %.4f, residual %.1f dB\n", d.peak, d.dB());
        CHECK (d.peak > 0.05f && d.dB() < -100.0);
        // what may not be bounced: clips of two tracks, a track playing two channels
        auto both = liveFreeze190ffClipsOn (song, 0);
        both.push_back (liveFreeze190ffClipsOn (song, 1).front());
        CHECK (lynx::freeze::planBounceToNewTrack (song, both, plan) != nullptr);
        auto mixed = song;
        mixed.patterns[0].notes.push_back (freeze190ayNote (1, 0, 100, 40));
        CHECK (lynx::freeze::planBounceToNewTrack (mixed, liveFreeze190ffClipsOn (mixed, 0), plan) != nullptr);
        // an audio track bounces through its insert too
        CHECK (lynx::freeze::planBounceToNewTrack (song, liveFreeze190ffClipsOn (song, 2), plan) == nullptr && plan.channel == 2 && plan.insert == 3);
    }

    // Consolidate, pattern clips: the Lead's three clips (one cut halfway, a gap) become one clip of a new pattern
    {
        lynx::freeze::ConsolidatePlan plan;
        CHECK (lynx::freeze::planConsolidate (song, liveFreeze190ffClipsOn (song, 0), plan) == nullptr && ! plan.audio);
        auto joined = song;
        const auto pattern = lynx::freeze::consolidatePatternClips (joined, plan);
        CHECK (pattern == 2 && joined.patterns[2].lengthTicks == 4 * 3840 && liveFreeze190ffClipsOn (joined, 0).size() == 1);
        const auto d = freeze190ayCompare (live, liveFreeze190ffRender (joined, end, provider, true));
        std::printf ("  consolidated pattern clips vs live: residual %.1f dB\n", d.dB());
        CHECK (d.worst == 0.0f);
        auto mixedKinds = liveFreeze190ffClipsOn (song, 0);
        mixedKinds.push_back (liveFreeze190ffClipsOn (song, 2).front());
        CHECK (lynx::freeze::planConsolidate (song, mixedKinds, plan) != nullptr);
    }

    // Consolidate, audio clips: their audio as played (gain, fades, offset) rendered into one clip of a new channel with
    // the source's volume, pan and insert
    {
        lynx::freeze::ConsolidatePlan plan;
        CHECK (lynx::freeze::planConsolidate (song, liveFreeze190ffClipsOn (song, 2), plan) == nullptr && plan.audio && plan.channel == 2);
        std::int64_t ticks = 0;
        files["consolidated"] = liveFreeze190ffSelection (lynx::freeze::consolidateRenderSong (song, plan), ticks, provider);
        auto joined = song;
        const auto channel = lynx::freeze::applyAudioConsolidation (joined, plan, "consolidated", ticks);
        CHECK (channel == 3 && joined.channels[3].mixerTrack == 3 && joined.channels[3].volume == 0.7f && joined.channels[3].pan == -0.2f);
        CHECK (liveFreeze190ffClipsOn (joined, 2).size() == 1);
        const auto d = freeze190ayCompare (live, liveFreeze190ffRender (joined, end, provider, true));
        std::printf ("  consolidated audio clips vs live: residual %.1f dB\n", d.dB());
        CHECK (d.peak > 0.05f && d.dB() < -100.0);
    }
}
