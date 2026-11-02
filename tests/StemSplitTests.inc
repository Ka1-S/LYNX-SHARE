// --- Extract stems from sample (issue #27) ---------------------------------------------------------------------------

#include "core/StemSplit.h"

TEST (stemsplit_overlap_add_rebuilds_the_signal)
{
    using namespace lynx::stemsplit;
    // lengths around the segment borders: shorter than one, exactly one, one more frame, several
    for (const std::int64_t frames : { (std::int64_t) 1, (std::int64_t) 1000, segmentFrames, segmentFrames + 1, segmentFrames * 3 + 17 })
    {
        std::vector<float> left ((std::size_t) frames), right ((std::size_t) frames);
        for (std::int64_t i = 0; i < frames; ++i)
        {
            left[(std::size_t) i] = (float) std::sin ((double) i * 0.01);
            right[(std::size_t) i] = (float) std::cos ((double) i * 0.003);
        }
        // a fake model: stem s is the input times (s + 1)
        std::int64_t calls = 0;
        double last = 0.0;
        Stems stems;
        const auto ok = separate (left, right, [&] (const float* in, float* out) {
            ++calls;
            for (int s = 0; s < numStems; ++s)
                for (std::int64_t i = 0; i < 2 * segmentFrames; ++i)
                    out[s * 2 * segmentFrames + i] = in[i] * (float) (s + 1);
            return true;
        }, [&] (double p) { last = p; }, stems);
        CHECK (ok);
        CHECK (calls == numSegments (frames));
        CHECK (last == 1.0);
        for (int s = 0; s < numStems; ++s)
        {
            CHECK ((std::int64_t) stems[(std::size_t) s][0].size() == frames);
            double worst = 0.0;
            for (std::int64_t i = 0; i < frames; ++i)
            {
                worst = std::max (worst, (double) std::abs (stems[(std::size_t) s][0][(std::size_t) i] - left[(std::size_t) i] * (float) (s + 1)));
                worst = std::max (worst, (double) std::abs (stems[(std::size_t) s][1][(std::size_t) i] - right[(std::size_t) i] * (float) (s + 1)));
            }
            CHECK (worst < 1.0e-3);
        }
    }
}

TEST (stemsplit_cancel_stops_at_the_segment)
{
    using namespace lynx::stemsplit;
    std::vector<float> mono ((std::size_t) segmentFrames * 3, 0.1f);
    int calls = 0;
    Stems stems;
    CHECK (! separate (mono, mono, [&] (const float*, float*) { return ++calls < 2; }, {}, stems));
    CHECK (calls == 2);
}

TEST (stemsplit_clips_are_aligned_on_new_tracks)
{
    using namespace lynx;
    Song song;
    ChannelDef channel;
    channel.name = "song";
    channel.instrument = InstrumentKind::audioClip;
    channel.samplePath = "/a/song.wav";
    channel.sampler.stretchBeats = 16.0f;
    song.channels.push_back (channel);

    Clip source;
    source.kind = ClipKind::audio;
    source.channel = (int) song.channels.size() - 1;
    source.track = 2;
    source.start = 960;
    source.length = 4800;
    source.offset = 96;
    source.gain = 0.5f;
    song.playlist.push_back (source);
    Clip below = source;
    below.track = 3;
    song.playlist.push_back (below);

    const std::array<std::string, stemsplit::numStems> paths { "/d.wav", "/b.wav", "/i.wav", "/v.wav" };
    const auto before = song;
    CHECK (! stemsplit::addStemClips (song, 5, "/a/song.wav", paths));    // the clip is gone
    CHECK (! stemsplit::addStemClips (song, 0, "/other.wav", paths));     // another file now
    CHECK (song.playlist.size() == before.playlist.size() && song.channels.size() == before.channels.size());

    CHECK (stemsplit::addStemClips (song, 0, "/a/song.wav", paths));
    CHECK (song.playlist.size() == before.playlist.size() + 4);
    CHECK (song.playlist[0].track == 2 && song.playlist[0].channel == source.channel); // the source stays
    CHECK (song.playlist[1].track == 7);                                               // the clip below moved down
    for (int s = 0; s < 4; ++s)
    {
        const auto& clip = song.playlist[before.playlist.size() + (std::size_t) s];
        const auto& stem = song.channels[(std::size_t) clip.channel];
        CHECK (clip.track == 3 + s);
        CHECK (clip.start == 960 && clip.length == 4800 && clip.offset == 96 && clip.gain == 0.5f);
        CHECK (stem.samplePath == paths[(std::size_t) s]);
        CHECK (stem.sampler.stretchBeats == 16.0f);
        CHECK (stem.instrument == InstrumentKind::audioClip);
    }
    CHECK (song.channels[(std::size_t) song.playlist.back().channel].name == "song - vocals");
}
