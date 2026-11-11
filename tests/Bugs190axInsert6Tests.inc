// Round 190ax, bug 1: an audio clip on a Playlist track linked to mixer Insert 6 made no sound on Insert 6 (round 190w's
// project shots.lynx). The link, the insert and the channel's route were right: the clip's channel was a Sampler, not
// an audio clip channel, and a Sampler drops the clip's voice key (audioClipKey, 128 + the clip's index: outside every
// key region). The app made such channels itself: a file dropped on an audio clip channel's Channel rack row turned it
// into a Sampler. core/PlaylistOps.h audioclips::repairChannels (project load) / loadSampleInto (the Channel rack).

#include "core/PlaylistOps.h"
#include <cmath>
#include <map>
#include <memory>

namespace b190ax1
{
constexpr double rate = 48000.0;

struct Files
{
    std::map<std::string, std::shared_ptr<lynx::SampleData>> map;
    lynx::RackState::SampleProvider provider()
    {
        return [this] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
            const auto it = map.find (path);
            return it == map.end() ? nullptr : it->second;
        };
    }
};

// the song's peak over `seconds` (the Master's output)
float peak (const lynx::Song& song, Files& files, double seconds = 1.0)
{
    auto state = lynx::RackState::build (song, nullptr, rate, files.provider());
    lynx::RackRenderer rr;
    rr.prepare (rate, 512);
    const auto total = (int) (seconds * rate);
    std::vector<float> l ((std::size_t) total), r ((std::size_t) total);
    double tick = 0.0;
    const auto tps = lynx::ticksPerSample (song.bpm, rate);
    for (int at = 0; at < total; at += 512)
    {
        const auto n = std::min (512, total - at);
        rr.process (state.get(), true, tick, n, l.data() + at, r.data() + at);
        tick += n * tps;
    }
    float p = 0.0f;
    for (std::size_t i = 0; i < l.size(); ++i)
        p = std::max ({ p, std::abs (l[i]), std::abs (r[i]) });
    return p;
}

// 190w's project in short: channel 0 plays a pattern on Insert 1, channel 1 holds a loop routed to Insert 6 and has an
// audio clip on Playlist track 0, an audio track linked to Insert 6
lynx::Song song190w (Files& files, lynx::InstrumentKind loopKind)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = rate;
    d->left.resize ((std::size_t) (2.0 * rate));
    for (std::size_t i = 0; i < d->left.size(); ++i)
        d->left[i] = 0.25f * (float) std::sin (6.283185307179586 * 441.0 * (double) i / rate);
    d->right = d->left;
    files.map["loop"] = d;

    auto s = lynx::Song::makeDefault();
    s.bpm = 130.0;
    s.mode = lynx::PlayMode::song;
    while (s.mixer.size() < 17)
        s.mixer.emplace_back();
    lynx::ChannelDef kick;
    kick.instrument = lynx::InstrumentKind::kick;
    kick.mixerTrack = 1;
    lynx::ChannelDef loop;
    loop.name = "DL_Electroloop";
    loop.instrument = loopKind;
    loop.samplePath = "loop";
    loop.volume = 0.78f;
    loop.mixerTrack = 6;
    s.channels = { kick, loop };
    s.patterns = { lynx::Pattern {} };
    s.playlistTracks.resize (1);
    s.playlistTracks[0].mode = lynx::PlaylistTrackMode::audio;
    s.playlistTracks[0].mixerTrack = 6;
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::audio;
    clip.channel = 1;
    clip.track = 0;
    clip.length = 4 * lynx::ppq;
    s.playlist = { clip };
    return s;
}
} // namespace b190ax1

TEST (b190ax_an_audio_clip_on_a_sampler_channel_was_silent_and_is_repaired)
{
    using namespace b190ax1;
    Files files;
    // the reference: the clip on an audio clip channel sounds, and it sounds on Insert 6 (only that insert rendered)
    const auto good = song190w (files, lynx::InstrumentKind::audioClip);
    const auto goodPeak = peak (lynx::makeBounceSong (good, 6), files);
    CHECK (goodPeak > 0.1f);
    CHECK (peak (lynx::makeBounceSong (good, 2), files) == 0.0f); // (nothing plays on Insert 2)

    // the bug: the same clip on a Sampler channel plays nothing at all, on Insert 6 or anywhere
    auto bad = song190w (files, lynx::InstrumentKind::sampler);
    CHECK (peak (lynx::makeBounceSong (bad, 6), files) == 0.0f);
    CHECK (peak (bad, files) == 0.0f);

    // the repair project load runs: the channel becomes the audio clip channel it is used as, in place
    CHECK (lynx::audioclips::repairChannels (bad) == 1);
    CHECK (bad.channels.size() == 2 && bad.channels[1].instrument == lynx::InstrumentKind::audioClip);
    CHECK (bad.channels[1].mixerTrack == 6 && bad.channels[1].samplePath == "loop" && bad.playlist[0].channel == 1);
    CHECK (bad.channels == good.channels && bad.playlist == good.playlist);
    CHECK (peak (lynx::makeBounceSong (bad, 6), files) == goodPeak);
    CHECK (lynx::audioclips::repairChannels (bad) == 0); // nothing left to do
}

TEST (b190ax_repair_keeps_a_samplers_notes_and_other_channels)
{
    using namespace b190ax1;
    Files files;
    // the Sampler also plays notes: it stays a Sampler with its notes; the clips move to an audio clip copy
    auto s = song190w (files, lynx::InstrumentKind::sampler);
    lynx::Note n;
    n.channel = 1;
    s.patterns[0].notes.push_back (n);
    lynx::Arrangement other;
    other.clips = s.playlist; // a stored arrangement's clip of the same channel follows
    s.arrangements = { lynx::Arrangement {}, other };
    s.currentArrangement = 0;
    CHECK (lynx::audioclips::repairChannels (s) == 1);
    CHECK (s.channels.size() == 3 && s.channels[1].instrument == lynx::InstrumentKind::sampler);
    CHECK (s.channels[2].instrument == lynx::InstrumentKind::audioClip && s.channels[2].samplePath == "loop" && s.channels[2].mixerTrack == 6);
    CHECK (s.playlist[0].channel == 2 && s.arrangements[1].clips[0].channel == 2 && s.patterns[0].notes[0].channel == 1);
    CHECK (peak (lynx::makeBounceSong (s, 6), files) > 0.1f);
    CHECK (lynx::audioclips::repairChannels (s) == 0);

    // left alone: a Sampler no clip uses, a Sampler without a file, a channel that is no Sampler, a pattern clip
    auto t = song190w (files, lynx::InstrumentKind::sampler);
    t.playlist[0].kind = lynx::ClipKind::pattern;
    t.playlist[0].pattern = 0;
    const auto before = t;
    CHECK (lynx::audioclips::repairChannels (t) == 0 && t.channels == before.channels && t.playlist == before.playlist);
    t = song190w (files, lynx::InstrumentKind::sampler);
    t.channels[1].samplePath.clear();
    CHECK (lynx::audioclips::repairChannels (t) == 0 && t.channels[1].instrument == lynx::InstrumentKind::sampler);
    t = song190w (files, lynx::InstrumentKind::synth);
    CHECK (lynx::audioclips::repairChannels (t) == 0 && t.channels[1].instrument == lynx::InstrumentKind::synth);
}

TEST (b190ax_a_file_loaded_into_an_audio_clip_channel_keeps_it_one)
{
    // the Channel rack's file drop on a row (ChannelRackComponent::assignSampleNow)
    lynx::ChannelDef c;
    c.name = "Old";
    c.instrument = lynx::InstrumentKind::audioClip;
    c.samplePath = "old.wav";
    lynx::audioclips::loadSampleInto (c, "new.wav", "new");
    CHECK (c.instrument == lynx::InstrumentKind::audioClip && c.samplePath == "new.wav" && c.name == "new");

    // as before for the others: a drum or a synth becomes a Sampler, a Granulizer / Wave Traveller keeps its engine and name
    for (const auto kind : { lynx::InstrumentKind::kick, lynx::InstrumentKind::synth, lynx::InstrumentKind::sampler })
    {
        lynx::ChannelDef d;
        d.name = "Old";
        d.instrument = kind;
        lynx::audioclips::loadSampleInto (d, "new.wav", "new");
        CHECK (d.instrument == lynx::InstrumentKind::sampler && d.samplePath == "new.wav" && d.name == "new");
    }
    for (const auto kind : { lynx::InstrumentKind::granulizer, lynx::InstrumentKind::wavetraveller })
    {
        lynx::ChannelDef d;
        d.name = "Old";
        d.instrument = kind;
        lynx::audioclips::loadSampleInto (d, "new.wav", "new");
        CHECK (d.instrument == kind && d.samplePath == "new.wav" && d.name == "Old");
    }
}
