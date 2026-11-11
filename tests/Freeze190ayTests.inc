// Freeze (round 190ay; core/StandIns.h, lynx::freeze): a channel rendered to audio that plays in its place while its
// instrument is not loaded. The stems are made here the way sessionpack::renderFreeze makes them (the same songs, cut
// the same way), with the core's own renderer.

#include <cmath>
#include <map>

#include "core/Effects.h"
#include "core/RackOps.h"
#include "core/SessionIds.h"
#include "core/StandIns.h"

namespace
{
constexpr double freeze190ayRate = 48000.0;

lynx::Note freeze190ayNote (int channel, std::int64_t start, std::int64_t length, int key)
{
    lynx::Note n;
    n.channel = channel;
    n.start = start;
    n.length = length;
    n.key = key;
    return n;
}

// Lead (a saw through a filter with a long release: its tail rings on after the note) and Pad on insert 1, which has
// a delay and a reverb; a kick on insert 2. Patterns A (lead + kick), B (one short lead note, then its tail) and C (kick
// only). Playlist: A, A back to back, B, C (the lead only rings out), then A from its second beat.
lynx::Song freeze190aySong()
{
    auto s = lynx::Song::makeDefault();
    s.bpm = 120.0; // 25 samples per tick at 48 kHz
    s.mode = lynx::PlayMode::song;
    s.channels.clear();
    s.patterns.clear();
    s.playlist.clear();

    lynx::ChannelDef lead;
    lead.name = "Lead";
    lead.instrument = lynx::InstrumentKind::synth;
    lead.synth.release = 0.8f;
    lead.synth.cutoff = 1800.0f;
    lead.synth.resonance = 0.3f;
    lead.volume = 0.6f;
    lead.pan = 0.3f;
    lead.mixerTrack = 1;
    s.channels.push_back (lead);
    lynx::ChannelDef kick;
    kick.name = "Kick";
    kick.instrument = lynx::InstrumentKind::kick;
    kick.mixerTrack = 2;
    s.channels.push_back (kick);
    lynx::ChannelDef pad;
    pad.name = "Pad";
    pad.instrument = lynx::InstrumentKind::synth;
    pad.synth.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    pad.synth.release = 0.4f;
    pad.mixerTrack = 1;
    s.channels.push_back (pad);

    s.mixer[1].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::delay));
    s.mixer[1].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::reverb));

    s.patterns.emplace_back().name = "A";
    for (const auto& n : { freeze190ayNote (0, 0, 480, 48), freeze190ayNote (0, 960, 480, 55), freeze190ayNote (0, 1920, 960, 51),
                           freeze190ayNote (0, 3360, 480, 60), // (held to the pattern's end: its release rings into what follows)
                           freeze190ayNote (1, 0, 240, 36), freeze190ayNote (1, 1920, 240, 36), freeze190ayNote (2, 0, 3840, 60) })
        s.patterns[0].notes.push_back (n);
    s.patterns.emplace_back().name = "B";
    s.patterns[1].notes.push_back (freeze190ayNote (0, 0, 240, 43));
    s.patterns[1].notes.push_back (freeze190ayNote (2, 960, 960, 67));
    s.patterns.emplace_back().name = "C";
    s.patterns[2].notes.push_back (freeze190ayNote (1, 0, 240, 36));
    for (auto& p : s.patterns)
    {
        p.lengthTicks = 3840;
        p.sortNotes();
    }
    const int order[] = { 0, 0, 1, 2 };
    for (int i = 0; i < 4; ++i)
    {
        lynx::Clip c;
        c.pattern = order[i];
        c.track = i == 3 ? 1 : 0;
        c.start = i * 3840;
        c.length = 3840;
        s.playlist.push_back (c);
    }
    lynx::Clip late; // starts one beat into A, up to its end
    late.pattern = 0;
    late.start = 4 * 3840;
    late.offset = 960;
    late.length = 2880;
    s.playlist.push_back (late);
    lynx::uids::normalize (s);
    return s;
}

std::vector<float> freeze190ayRender (lynx::Song song, std::int64_t ticks, const lynx::RackState::SampleProvider& provider, bool songMode)
{
    song.mode = songMode ? lynx::PlayMode::song : lynx::PlayMode::pattern;
    auto state = lynx::RackState::build (song, nullptr, freeze190ayRate, provider);
    lynx::RackRenderer r;
    r.prepare (freeze190ayRate, 480);
    const auto tps = lynx::ticksPerSample (song.bpm, freeze190ayRate);
    const auto frames = (int) std::llround ((double) ticks / tps);
    std::vector<float> out ((std::size_t) frames * 2), l (480), rr (480);
    for (int pos = 0; pos < frames; pos += 480)
    {
        const auto n = std::min (480, frames - pos);
        r.process (state.get(), true, pos * tps, n, l.data(), rr.data());
        for (int i = 0; i < n; ++i)
            out[(std::size_t) (pos + i) * 2] = l[(std::size_t) i], out[(std::size_t) (pos + i) * 2 + 1] = rr[(std::size_t) i];
    }
    return out;
}

using Freeze190ayFiles = std::map<std::string, std::shared_ptr<const lynx::SampleData>>;

std::shared_ptr<const lynx::SampleData> freeze190ayCut (const std::vector<float>& all, double tps, std::int64_t from, std::int64_t length)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = freeze190ayRate;
    const auto a = (std::size_t) std::llround ((double) from / tps), n = (std::size_t) std::llround ((double) length / tps);
    for (std::size_t i = 0; i < n && (a + i) * 2 + 1 < all.size(); ++i)
        d->left.push_back (all[(a + i) * 2]), d->right.push_back (all[(a + i) * 2 + 1]);
    return d;
}

// What sessionpack::renderFreeze does: the whole song (as an export renders it, with the tail) and each pattern's loop
// pass, from the lightened render songs; the stems go onto the channel.
void freeze190ayFreeze (lynx::Song& song, int channel, Freeze190ayFiles& files, bool lighten = true)
{
    const auto tps = lynx::ticksPerSample (song.bpm, freeze190ayRate);
    const auto tail = lynx::standins::tailTicks (song);
    const auto tag = "ch" + std::to_string (channel);
    std::vector<lynx::StandIn> stems;
    {
        auto renderSong = lynx::standins::channelTimelineRender (song, channel);
        if (lighten)
            lynx::freeze::lighten (renderSong, channel);
        std::int64_t music = 0;
        const auto exported = lynx::makeExportSong (renderSong, true, tail, music);
        const auto all = freeze190ayRender (exported, music + tail, {}, true);
        files[tag + " whole"] = freeze190ayCut (all, tps, 0, music + tail);
        lynx::StandIn s;
        s.path = tag + " whole";
        s.lengthTicks = song.songLength() + tail;
        stems.push_back (s);
    }
    auto plan = lynx::standins::channelPatternRender (song, channel);
    if (! plan.segments.empty())
    {
        if (lighten)
            lynx::freeze::lighten (plan.song, channel);
        const auto all = freeze190ayRender (plan.song, plan.end, {}, true);
        for (const auto& seg : plan.segments)
        {
            lynx::StandIn s;
            s.pattern = seg.patternUid;
            s.loop = true;
            s.path = tag + " loop" + std::to_string (seg.pattern);
            s.lengthTicks = seg.length;
            files[s.path] = freeze190ayCut (all, tps, seg.loopFrom, seg.length);
            stems.push_back (s);
        }
    }
    lynx::freeze::apply (song, song.channels[(std::size_t) channel].uid, stems, "test");
}

// the worst difference between two renders over [from, to) frames, in dB below the first one's peak there
struct Freeze190ayDiff
{
    float peak = 0.0f, worst = 0.0f;
    double dB() const { return worst <= 0.0f ? -200.0 : 20.0 * std::log10 ((double) worst / (double) std::max (peak, 1.0e-12f)); }
};
Freeze190ayDiff freeze190ayCompare (const std::vector<float>& a, const std::vector<float>& b, std::size_t from = 0, std::size_t to = ~std::size_t { 0 })
{
    Freeze190ayDiff d;
    const auto end = std::min ({ a.size(), b.size(), to == ~std::size_t { 0 } ? to : to * 2 });
    for (std::size_t i = from * 2; i < end; ++i)
        d.worst = std::max (d.worst, std::abs (a[i] - b[i])), d.peak = std::max (d.peak, std::abs (a[i]));
    return d;
}
} // namespace

TEST (freeze190ay_frozen_channel_plays_like_its_instrument)
{
    // Song mode: the frozen lead, through its live insert effects (delay + reverb) and with its live volume and pan,
    // must null against the lead played by its instrument — over the whole song, in the stretch where only its tail
    // rings (pattern C) and where a pattern repeats back to back.
    const auto song = freeze190aySong();
    const auto end = song.songLength();
    const auto tps = lynx::ticksPerSample (song.bpm, freeze190ayRate);
    auto frames = [tps] (std::int64_t ticks) { return (std::size_t) std::llround ((double) ticks / tps); };

    auto frozen = song;
    Freeze190ayFiles files;
    freeze190ayFreeze (frozen, 0, files);
    CHECK (frozen.channels[0].freeze.on() && frozen.channels[0].freeze.stems.size() == 3); // the song, loops of A and B
    CHECK (frozen.channels[0].instrument == lynx::InstrumentKind::synth && frozen.channels[0].synth == song.channels[0].synth);
    auto provider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        auto it = files.find (path);
        return it != files.end() ? it->second : nullptr;
    };

    lynx::standins::Projected projected;
    const auto played = lynx::standins::project (frozen, [] (const lynx::PluginRef&) { return false; }, &projected);
    CHECK (played.channels[0].instrument == lynx::InstrumentKind::audioClip); // the instrument is not built at all
    CHECK (played.channels[1] == song.channels[1] && played.channels[2] == song.channels[2]);
    CHECK (projected.channels.empty() && projected.inserts.empty()); // (not a missing plugin: no stand-in badge)
    CHECK (played.songLength() == end);
    CHECK (played.channels[0].volume == song.channels[0].volume && played.channels[0].pan == song.channels[0].pan && played.channels[0].mixerTrack == 1);

    const auto live = freeze190ayRender (song, end, {}, true);
    const auto ice = freeze190ayRender (played, end, provider, true);
    const auto whole = freeze190ayCompare (live, ice);
    const auto tail = freeze190ayCompare (live, ice, frames (2 * 3840 + 240), frames (3 * 3840)); // pattern B after its one lead note: the tail
    const auto repeat = freeze190ayCompare (live, ice, frames (3840), frames (2 * 3840));    // the second A, over the first one's tail
    std::printf ("  frozen, Song mode: peak %.4f, residual %.1f dB (whole song), %.1f dB (tail after the last note), %.1f dB (repeated pattern)\n",
                 whole.peak, whole.dB(), tail.dB(), repeat.dB());
    CHECK (whole.peak > 0.05f && whole.dB() < -100.0);
    CHECK (tail.peak > 1.0e-4f && tail.dB() < -80.0);
    CHECK (repeat.dB() < -100.0);

    // the lead alone (the others muted): the stem itself against the instrument, nothing masking the difference
    auto soloLive = song, soloIce = played;
    for (std::size_t c = 1; c < 3; ++c)
        soloLive.channels[c].muted = soloIce.channels[c].muted = true;
    const auto alone = freeze190ayCompare (freeze190ayRender (soloLive, end, {}, true), freeze190ayRender (soloIce, end, provider, true));
    std::printf ("  frozen, Song mode, the channel alone: peak %.4f, residual %.1f dB\n", alone.peak, alone.dB());
    CHECK (alone.peak > 0.02f && alone.dB() < -100.0);

    // Pattern mode: pattern A looping. From the second pass on the loop stem is what the instrument plays (the first
    // pass of the stem already carries the previous pass's tail: known, printed).
    // The loop wrap is seamless (below -90 dB in the first 10 ticks of a pass, as in the rest of it): the stand-in
    // channel declicks in mode 0, where a clip voice ends with a sharp cut at its note's end (FL's renders, round
    // 189l) — the pass that starts there continues it sample for sample. Before that the ended voice, already at its
    // file's last frame, faded out its held last value over 3 ms into the next pass (-24.6 dB there). Checked dry, and
    // through the insert's delay and reverb.
    auto loopLive = song, loopIce = frozen;
    loopLive.currentPattern = loopIce.currentPattern = 0;
    loopIce.mode = lynx::PlayMode::pattern;
    loopLive.mixer[1].effects.clear();
    loopIce.mixer[1].effects.clear();
    const auto loopPlayed = lynx::standins::project (loopIce, [] (const lynx::PluginRef&) { return false; });
    const auto l1 = freeze190ayRender (loopLive, 4 * 3840, {}, false);
    const auto l2 = freeze190ayRender (loopPlayed, 4 * 3840, provider, false);
    const auto first = freeze190ayCompare (l1, l2, 0, frames (3840));
    std::printf ("  frozen, Pattern mode (looping, no insert effects): first pass %.1f dB\n", first.dB());
    for (int pass = 1; pass < 4; ++pass)
    {
        const auto body = freeze190ayCompare (l1, l2, frames (pass * 3840 + 10), frames ((pass + 1) * 3840));
        const auto seam = freeze190ayCompare (l1, l2, frames (pass * 3840), frames (pass * 3840 + 10));
        std::printf ("    pass %d: peak %.4f, residual %.1f dB; its first 10 ticks %.1f dB\n", pass + 1, body.peak, body.dB(), seam.dB());
        CHECK (body.peak > 0.05f && body.dB() < -100.0);
        CHECK (seam.dB() < -90.0);
    }
    {
        auto wetLive = song, wetIce = frozen;
        wetLive.currentPattern = wetIce.currentPattern = 0;
        wetIce.mode = lynx::PlayMode::pattern;
        const auto wetPlayed = lynx::standins::project (wetIce, [] (const lynx::PluginRef&) { return false; });
        const auto w1 = freeze190ayRender (wetLive, 4 * 3840, {}, false);
        const auto w2 = freeze190ayRender (wetPlayed, 4 * 3840, provider, false);
        for (int pass = 2; pass < 4; ++pass) // (from the third pass: the second still hears the first one's missing tail in the reverb)
        {
            const auto seam = freeze190ayCompare (w1, w2, frames (pass * 3840), frames (pass * 3840 + 10));
            const auto body = freeze190ayCompare (w1, w2, frames (pass * 3840 + 10), frames ((pass + 1) * 3840));
            std::printf ("    with the insert effects, pass %d: residual %.1f dB; its first 10 ticks %.1f dB\n", pass + 1, body.dB(), seam.dB());
            CHECK (seam.dB() < -90.0);
        }
    }

    // Song mode with nothing arranged plays nothing: the frozen channel adds no clip there (a stem clip would become the
    // arrangement) and stays silent like its instrument
    auto bareIce = frozen;
    bareIce.playlist.clear();
    const auto barePlayed = lynx::standins::project (bareIce, [] (const lynx::PluginRef&) { return false; });
    CHECK (barePlayed.playlist.empty() && barePlayed.channels[0].instrument == lynx::InstrumentKind::audioClip);
    const auto bareAudio = freeze190ayRender (barePlayed, 3 * 3840, provider, true);
    CHECK (freeze190ayCompare (bareAudio, bareAudio).peak == 0.0f);

    // two frozen channels at once, one of them the kick (another engine)
    auto both = song;
    Freeze190ayFiles bothFiles;
    freeze190ayFreeze (both, 0, bothFiles);
    freeze190ayFreeze (both, 1, bothFiles);
    auto bothProvider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        auto it = bothFiles.find (path);
        return it != bothFiles.end() ? it->second : nullptr;
    };
    const auto bothPlayed = lynx::standins::project (both, [] (const lynx::PluginRef&) { return false; });
    CHECK (bothPlayed.channels[0].instrument == lynx::InstrumentKind::audioClip && bothPlayed.channels[1].instrument == lynx::InstrumentKind::audioClip);
    const auto two = freeze190ayCompare (live, freeze190ayRender (bothPlayed, end, bothProvider, true));
    std::printf ("  two frozen channels (lead + kick): residual %.1f dB\n", two.dB());
    CHECK (two.dB() < -100.0);
}

TEST (freeze190ay_keys_played_on_a_frozen_channel_leave_its_audio_alone)
{
    // A key played live on a frozen channel (the typing keyboard, a MIDI input, a preview click) has no instrument to
    // play: it must neither sound nor stop the frozen audio (an audio clip's "off" would end it for the rest of the song).
    auto frozen = freeze190aySong();
    Freeze190ayFiles files;
    freeze190ayFreeze (frozen, 0, files);
    auto provider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        auto it = files.find (path);
        return it != files.end() ? it->second : nullptr;
    };
    const auto played = lynx::standins::project (frozen, [] (const lynx::PluginRef&) { return false; });
    const auto end = played.songLength();
    const auto quiet = freeze190ayRender (played, end, provider, true);

    auto state = lynx::RackState::build (played, nullptr, freeze190ayRate, provider);
    lynx::RackRenderer r;
    r.prepare (freeze190ayRate, 480);
    const auto tps = lynx::ticksPerSample (played.bpm, freeze190ayRate);
    const auto frames = (int) std::llround ((double) end / tps);
    std::vector<float> out ((std::size_t) frames * 2), l (480), rr (480);
    lynx::LiveEventQueue live;
    for (int pos = 0, block = 0; pos < frames; pos += 480, ++block)
    {
        if (block == 40 || block == 60) // a key down, then up, in the first pattern
        {
            lynx::NoteEvent e;
            e.type = block == 40 ? lynx::NoteEvent::Type::on : lynx::NoteEvent::Type::off;
            e.channel = 0;
            e.key = 64;
            e.velocity = 0.8f;
            live.push (e);
        }
        const auto n = std::min (480, frames - pos);
        r.process (state.get(), true, pos * tps, n, l.data(), rr.data(), &live);
        for (int i = 0; i < n; ++i)
            out[(std::size_t) (pos + i) * 2] = l[(std::size_t) i], out[(std::size_t) (pos + i) * 2 + 1] = rr[(std::size_t) i];
    }
    const auto d = freeze190ayCompare (quiet, out);
    std::printf ("  a key played on the frozen channel: residual %.1f dB\n", d.dB());
    CHECK (d.peak > 0.05f && d.worst == 0.0f);
}

TEST (freeze190ay_light_render_song_is_the_same_stem)
{
    // The render song without the other channels' instruments and without any insert effect gives the very same audio:
    // freezing one channel does not need the rest of the project loaded.
    auto a = freeze190aySong(), b = a;
    Freeze190ayFiles heavy, light;
    freeze190ayFreeze (a, 0, heavy, false);
    freeze190ayFreeze (b, 0, light, true);
    CHECK (heavy.size() == light.size() && heavy.size() == 3);
    for (const auto& [path, data] : heavy)
    {
        const auto it = light.find (path);
        CHECK (it != light.end());
        if (it == light.end())
            continue;
        CHECK (data->left == it->second->left && data->right == it->second->right);
        CHECK (! data->left.empty());
    }
    auto renderSong = lynx::standins::channelTimelineRender (a, 0);
    lynx::freeze::lighten (renderSong, 0);
    CHECK (renderSong.channels.size() == 3 && renderSong.mixer.size() == a.mixer.size());
    CHECK (renderSong.channels[0].instrument == lynx::InstrumentKind::synth && ! renderSong.channels[0].freeze.on());
    CHECK (renderSong.channels[1].muted && renderSong.channels[1].instrument == lynx::InstrumentKind::synth);
    CHECK (renderSong.mixer[1].effects.size() == 2 && renderSong.mixer[1].effects[0].kind == lynx::EffectKind::utility && ! renderSong.mixer[1].effects[0].enabled);
}

TEST (freeze190ay_rules)
{
    auto song = freeze190aySong();
    CHECK (lynx::freeze::whyNot (song, 0) == nullptr && lynx::freeze::whyNot (song, 1) == nullptr);
    CHECK (lynx::freeze::whyNot (song, -1) != nullptr && lynx::freeze::whyNot (song, 3) != nullptr);
    CHECK (! lynx::freeze::any (song) && ! lynx::standins::hasAny (song));
    // nothing frozen: the projection is the song itself
    CHECK (lynx::standins::project (song, [] (const lynx::PluginRef&) { return false; }).channels == song.channels);

    // what cannot be frozen, each with its reason
    auto audio = song;
    audio.channels[1].instrument = lynx::InstrumentKind::audioClip;
    CHECK (lynx::freeze::whyNot (audio, 1) != nullptr);
    auto layered = song;
    lynx::ChannelDef layer;
    layer.instrument = lynx::InstrumentKind::layer;
    layer.layer.children = { 0 };
    layered.channels.push_back (layer);
    CHECK (lynx::freeze::whyNot (layered, 0) != nullptr && lynx::freeze::whyNot (layered, 3) != nullptr && lynx::freeze::whyNot (layered, 2) == nullptr);
    auto performing = song;
    performing.performanceMode = true;
    CHECK (lynx::freeze::whyNot (performing, 0) != nullptr);
    auto unloaded = song;
    unloaded.channels[0].instrument = lynx::InstrumentKind::plugin;
    CHECK (lynx::freeze::whyNot (unloaded, 0) != nullptr);
    unloaded.channels[0].plugin.id = "VST3-Some Synth-0-0";
    CHECK (lynx::freeze::whyNot (unloaded, 0) == nullptr);
    unloaded.channels[0].plugin.outputs = { {}, { 1, 1 } }; // its second output plays on the next insert
    CHECK (lynx::freeze::whyNot (unloaded, 0) != nullptr);

    // frozen by uid (the channel may have moved while it rendered); the channel itself is untouched
    const auto before = song.channels[0];
    lynx::freeze::apply (song, song.channels[0].uid, { { 0, false, "whole", "", 100, "", "" } }, "source");
    CHECK (song.channels[0].freeze.on() && song.channels[0].freeze.source == "source" && lynx::freeze::any (song) && lynx::standins::hasAny (song));
    auto thawedChannel = song.channels[0];
    thawedChannel.freeze = {};
    CHECK (thawedChannel == before);
    lynx::freeze::apply (song, 0, { { 0, false, "x", "", 1, "", "" } }, "y"); // no uid: nothing
    CHECK (song.channels[0].freeze.source == "source" && ! song.channels[1].freeze.on());

    // a clone plays its own instrument; the exports' song has nothing frozen; Unfreeze
    const auto clone = lynx::cloneChannel (song, 0);
    CHECK (clone == 1 && ! song.channels[1].freeze.on() && song.channels[0].freeze.on());
    const auto thawed = lynx::freeze::thawed (song);
    CHECK (! lynx::freeze::any (thawed) && thawed.channels.size() == song.channels.size());
    lynx::freeze::clear (song, 0);
    CHECK (! lynx::freeze::any (song) && song.channels[0] == before);

    // a stem render never plays a frozen channel's audio: it renders from the instruments
    auto again = freeze190aySong();
    lynx::freeze::apply (again, again.channels[0].uid, { { 0, false, "whole", "", 100, "", "" } }, "source");
    CHECK (! lynx::standins::channelTimelineRender (again, 0).channels[0].freeze.on());
    CHECK (! lynx::standins::channelPatternRender (again, 0).song.channels[0].freeze.on());
    CHECK (! lynx::standins::insertTimelineRender (again, 1).channels[0].freeze.on());
}
