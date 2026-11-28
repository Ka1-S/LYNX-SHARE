// Round 190sg (issue #325): a sample dropped on an audio clip of the Playlist replaces its channel's sample, as FL 2026
// (checked live: every clip of that channel changes, no new clip; a clip that showed the whole old file shows the whole new
// one, a trimmed clip keeps its length, fades stay; Shift / several files / a pattern clip: no replacement).

namespace issue325
{
// two audio clip channels on inserts 3 and 4 (an effect on 3), channel 0's file "old.wav" (2 bars at 120 BPM) on three
// clips: whole at bar 1, whole at bar 5 with fades, trimmed to 1 bar at bar 9; channel 1 on one clip; a pattern clip
inline lynx::Song song()
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0;
    for (int i = 0; i < 2; ++i)
    {
        ChannelDef c;
        c.instrument = InstrumentKind::audioClip;
        c.name = i == 0 ? "old" : "other";
        c.samplePath = i == 0 ? "/x/old.wav" : "/x/other.wav";
        c.mixerTrack = 3 + i;
        c.colour = 0xff112233u + (std::uint32_t) i;
        c.volume = 0.5f;
        s.channels.push_back (c);
    }
    s.channels[0].sampler.slices = { 0.0f, 0.5f };
    s.channels[0].sampler.loopStart = 10;
    s.channels[0].sampler.loopEnd = 100;
    s.channels[0].sampler.stretchBeats = 8.0f;
    while (s.mixer.size() < 5)
        s.mixer.emplace_back();
    s.mixer[3].effects.push_back (makeEffectSlot (EffectKind::eq));
    const auto bar = 4 * ppq;
    const auto audio = [&] (int channel, int track, std::int64_t start, std::int64_t length) {
        Clip c;
        c.kind = ClipKind::audio;
        c.channel = channel;
        c.track = track;
        c.start = start;
        c.length = length;
        s.playlist.push_back (c);
        return (int) s.playlist.size() - 1;
    };
    audio (0, 0, 0, 2 * bar);
    const auto faded = audio (0, 1, 4 * bar, 2 * bar);
    s.playlist[(std::size_t) faded].fadeIn = ppq;
    s.playlist[(std::size_t) faded].fadeOut = 3 * ppq;
    audio (0, 2, 8 * bar, bar);
    audio (1, 3, 0, bar);
    Clip p;
    p.kind = ClipKind::pattern;
    p.pattern = 0;
    p.track = 4;
    p.length = bar;
    s.playlist.push_back (p);
    return s;
}
} // namespace issue325

TEST (issue325_sample_dropped_on_an_audio_clip_replaces_its_channels_sample)
{
    using namespace lynx;
    const auto bar = 4 * ppq;
    auto s = issue325::song();
    const auto before = s;
    // the new file lasts 1 bar (old: 2 bars, the channel stretched to 8 beats)
    CHECK (audioclips::replaceClipSample (s, 2, "/x/new.wav", "new", 2 * bar, bar));
    const auto& c = s.channels[0];
    CHECK (s.channels.size() == before.channels.size()); // (no new channel)
    CHECK (c.samplePath == "/x/new.wav" && c.name == "new" && c.instrument == InstrumentKind::audioClip);
    // the channel stays: insert (and so its effects), levels, colour
    CHECK (c.mixerTrack == 3 && c.volume == 0.5f && c.colour == before.channels[0].colour);
    CHECK (s.mixer[3].effects.size() == 1);
    // what described the old file goes: slices, loop points, the stretch
    CHECK (c.sampler.slices.empty() && c.sampler.loopStart == -1 && c.sampler.loopEnd == -1 && c.sampler.stretchBeats == 0.0f);
    // the other channel is untouched; no clip added or removed
    CHECK (s.channels[1] == before.channels[1]);
    CHECK (s.playlist.size() == before.playlist.size());
    // whole clips show the whole new file (FL), at their place; fades stay
    CHECK (s.playlist[0].start == 0 && s.playlist[0].length == bar && s.playlist[0].track == 0);
    CHECK (s.playlist[1].start == 4 * bar && s.playlist[1].length == bar && s.playlist[1].fadeIn == ppq && s.playlist[1].fadeOut == 3 * ppq);
    // a trimmed clip keeps its place and length (FL with a loop as long as the old one)
    CHECK (s.playlist[2].start == 8 * bar && s.playlist[2].length == bar && s.playlist[2].offset == 0);
    // the other channel's clip and the pattern clip are untouched
    CHECK (s.playlist[3] == before.playlist[3] && s.playlist[4] == before.playlist[4]);
    // auto name off: the channel keeps its name
    auto t = issue325::song();
    CHECK (audioclips::replaceClipSample (t, 0, "/x/new.wav", "new", 2 * bar, 4 * bar, false));
    CHECK (t.channels[0].name == "old" && t.channels[0].samplePath == "/x/new.wav");
    CHECK (t.playlist[0].length == 4 * bar && t.playlist[1].length == 4 * bar && t.playlist[2].length == bar);
}

TEST (issue325_fades_shorten_with_the_clip_and_unknown_lengths_change_nothing)
{
    using namespace lynx;
    const auto bar = 4 * ppq;
    auto s = issue325::song();
    // a new file of half a beat: the faded clip's fades fit in it
    CHECK (audioclips::replaceClipSample (s, 1, "/x/short.wav", "short", 2 * bar, ppq / 2));
    CHECK (s.playlist[1].length == ppq / 2 && s.playlist[1].fadeIn + s.playlist[1].fadeOut <= s.playlist[1].length);
    // lengths unknown (a file not read): only the sample changes
    auto u = issue325::song();
    CHECK (audioclips::replaceClipSample (u, 0, "/x/new.wav", "new", 0, 0));
    CHECK (u.playlist[0].length == 2 * bar && u.playlist[1].length == 2 * bar && u.channels[0].samplePath == "/x/new.wav");
    // a clip stretched by FL's per-clip stretch is whole at its stretched length
    auto v = issue325::song();
    v.playlist[0].stretch = 2.0;
    v.playlist[0].length = 4 * bar;
    CHECK (audioclips::replaceClipSample (v, 0, "/x/new.wav", "new", 2 * bar, bar));
    CHECK (v.playlist[0].length == 2 * bar);
    // a clip with an offset is not whole
    auto w = issue325::song();
    w.playlist[0].offset = ppq;
    CHECK (audioclips::replaceClipSample (w, 1, "/x/new.wav", "new", 2 * bar, bar));
    CHECK (w.playlist[0].length == 2 * bar && w.playlist[0].offset == ppq);
}

TEST (issue325_only_an_audio_clip_of_an_audio_clip_channel_takes_it)
{
    using namespace lynx;
    auto s = issue325::song();
    const auto before = s;
    CHECK (! audioclips::replaceClipSample (s, 4, "/x/new.wav", "new", 1, 1)); // a pattern clip (FL: nothing happens)
    CHECK (! audioclips::replaceClipSample (s, -1, "/x/new.wav", "new", 1, 1));
    CHECK (! audioclips::replaceClipSample (s, 99, "/x/new.wav", "new", 1, 1));
    CHECK (! audioclips::replaceClipSample (s, 0, "", "new", 1, 1));
    s.channels[0].instrument = InstrumentKind::sampler; // (a clip pointing at a Sampler: not an audio clip channel)
    CHECK (! audioclips::replaceClipSample (s, 0, "/x/new.wav", "new", 1, 1));
    s.channels[0].instrument = InstrumentKind::audioClip;
    CHECK (s.channels == before.channels && s.playlist == before.playlist);
    // the clips of the channel in the other clip lists follow too (a stored arrangement)
    s.arrangements.push_back (Arrangement { "Arrangement 2", { s.playlist[0] }, 0, 0, {} });
    CHECK (audioclips::replaceClipSample (s, 0, "/x/new.wav", "new", 2 * 4 * ppq, 4 * ppq));
    CHECK (s.arrangements.back().clips.front().length == 4 * ppq);
}
