// Round 167h: the Sampler's precomputed effects, loop flags, declicking and "Fast declick for cut groups"
// (docs/analise/REA_rodada167h.md)

#include "core/SamplePrecompute.h"
#define REA167H_REQUIRE(x) \
    do \
    { \
        CHECK (x); \
        if (! (x)) \
            return; \
    } while (false)

namespace rea167h
{
// one FL sampler channel with the given channel events (FL's order: 0x40, 0x15, then the rest)
inline flp::Project channelProject (const std::function<void (FlpBuilder&)>& events)
{
    FlpBuilder b;
    b.u8 (0x40);
    b.u16 (0);
    b.u8 (0x15);
    b.u8 (0);
    events (b);
    return flp::parseProject (b.bytes);
}

inline std::shared_ptr<lynx::SampleData> ramp (int frames, bool stereo)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = 44100.0;
    for (int i = 0; i < frames; ++i)
    {
        d->left.push_back (0.25f + 0.5f * std::sin (0.01f * (float) i));
        if (stereo)
            d->right.push_back (-0.1f + 0.3f * std::cos (0.013f * (float) i));
    }
    return d;
}
} // namespace rea167h

// §1 the events: 0x46 bits, 0x4C / 0x4B fades, 0xD7 bytes 0x09 / 0x50 / 0x51 / 0x53 and i32 0x54 / 0x58, 0x8F bit 3,
// 0x13, 0x14 — each to the Sampler control the channel form's handlers write [RTTI + DECOMP]
TEST (rea167h_flp_sampler_precompute_events)
{
    const auto p = rea167h::channelProject ([] (FlpBuilder& b) {
        b.u8 (0x46);
        b.u16 (0x103); // fade stereo, reverse, swap stereo
        b.u8 (0x4C);
        b.u16 (280);
        b.u8 (0x4B);
        b.u16 (1024);
        b.u8 (0x8F);
        b.u32 (10); // load regions + use loop points
        b.u8 (0x14);
        b.u8 (1);
        std::vector<std::uint8_t> d7 (0xA8, 0);
        d7[0x09] = 1;
        d7[0x50] = 1;
        d7[0x51] = 1;
        d7[0x53] = 3;
        d7[0x54] = 0x00; // crossfade 1024
        d7[0x55] = 0x04;
        d7[0x58] = 0x00; // trim 256
        d7[0x59] = 0x01;
        b.data (0xD7, d7);
    });
    REA167H_REQUIRE (p.channels.size() == 1);
    const auto& c = p.channels[0];
    CHECK (c.fxFlags == 0x103 && c.fadeIn == 280 && c.fadeOut == 1024 && c.sampleFlags == 10 && c.pingPong);
    CHECK (c.removeDc && c.normalize && c.reversePolarity && c.declickMode == 3 && c.crossfade == 1024 && c.trim == 256);
    CHECK (c.useLoopPoints());

    flp::Channel none;
    CHECK (! none.useLoopPoints()); // FL's default: no 0x8F and no 0x13 = off
    none.loopPointsOld = true;
    CHECK (none.useLoopPoints());
    none.sampleFlags = 2; // 0x8F wins over 0x13
    CHECK (! none.useLoopPoints());

    // the import
    auto song = lynx::importFlp (p);
    REA167H_REQUIRE (! song.channels.empty());
    const auto& s = song.channels[0].sampler;
    CHECK (s.reverse && s.precompute.fadeStereo && s.precompute.swapStereo && s.precompute.fadeIn == 280 && s.precompute.fadeOut == 1024);
    CHECK (s.precompute.removeDc && s.precompute.normalize && s.precompute.reversePolarity && s.precompute.crossfade == 1024);
    CHECK (s.precompute.trim == 256 && s.precompute.pingPong && s.useLoopPoints && s.declickMode == 3);

    // 0x28 "Fast declick for cut groups" (FL saves it after the Playlist tracks)
    FlpBuilder f;
    f.u8 (0x28);
    f.u8 (1);
    CHECK (lynx::importFlp (flp::parseProject (f.bytes)).fastDeclick);
    CHECK (! lynx::importFlp (flp::parseProject (FlpBuilder().bytes)).fastDeclick);
}

// §2 FL's region: the end is a fraction of what follows the start (FUN_00ff0db0) — FL's demos hold ends below starts
TEST (rea167h_flp_region_end_is_relative)
{
    const auto p = rea167h::channelProject ([] (FlpBuilder& b) {
        std::vector<std::uint8_t> d7 (0xA8, 0);
        const double start = 0.6244, end = 0.1384, offset = 0.0;
        std::memcpy (d7.data() + 0x80, &start, 8);
        std::memcpy (d7.data() + 0x88, &end, 8);
        std::memcpy (d7.data() + 0x90, &offset, 8);
        b.data (0xD7, d7);
    });
    const auto song = lynx::importFlp (p);
    REA167H_REQUIRE (! song.channels.empty());
    CHECK (std::abs (song.channels[0].sampler.regionStart - 0.6244f) < 1e-6f);
    CHECK (std::abs (song.channels[0].sampler.regionEnd - (float) (0.6244 + 0.1384 * (1.0 - 0.6244))) < 1e-6f);
}

// §2 the precompute, effect by effect, against FL's formulas
TEST (rea167h_precompute_matches_fl_formulas)
{
    namespace pc = lynx::precompute;
    const auto in = rea167h::ramp (2000, true);
    const auto n = 2000;
    lynx::SamplerSettings s;
    CHECK (! pc::needed (s) && pc::apply (*in, s) == nullptr);

    // fade in 280 / fade out 1024: 1 − (N−1−i)·a/(256(N−1)), 1 − i·b/(256(N−1)), floored at 0
    s.precompute.fadeIn = 280;
    auto out = pc::apply (*in, s);
    REA167H_REQUIRE (out != nullptr && (int) out->frames() == n && out->cropped);
    for (int i : { 0, 100, 1000, 1999 })
    {
        const auto g = std::max (0.0, 1.0 - (double) (n - 1 - i) * 280.0 / (256.0 * (n - 1)));
        CHECK (std::abs (out->left[(std::size_t) i] - (float) (in->left[(std::size_t) i] * g)) < 1e-6f);
    }
    s.precompute = {};
    s.precompute.fadeOut = 1024;
    out = pc::apply (*in, s);
    CHECK (out->left[600] == 0.0f && std::abs (out->left[100] - in->left[100] * (float) (1.0 - 100.0 * 4.0 / 1999.0)) < 1e-6f);

    // normalize: the peak of both channels becomes 1; remove DC: each channel's mean 0; polarity: −x
    s.precompute = {};
    s.precompute.normalize = true;
    out = pc::apply (*in, s);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max ({ peak, std::fabs (out->left[(std::size_t) i]), std::fabs (out->right[(std::size_t) i]) });
    CHECK (std::abs (peak - 1.0f) < 1e-6f);
    s.precompute = {};
    s.precompute.removeDc = true;
    s.precompute.reversePolarity = true;
    out = pc::apply (*in, s);
    double meanL = 0.0, meanR = 0.0;
    for (int i = 0; i < n; ++i)
        meanL += out->left[(std::size_t) i], meanR += out->right[(std::size_t) i];
    CHECK (std::abs (meanL / n) < 1e-6 && std::abs (meanR / n) < 1e-6);
    CHECK (out->left[5] < 0.0f); // (the ramp starts above its mean: removing DC then flipping makes it negative)

    // swap stereo; fade stereo: L × cos(iπ/2/(N−1)), R × cos((N−1−i)π/2/(N−1)); a mono sample becomes stereo
    s.precompute = {};
    s.precompute.swapStereo = true;
    out = pc::apply (*in, s);
    CHECK (out->left[10] == in->right[10] && out->right[10] == in->left[10]);
    s.precompute = {};
    s.precompute.fadeStereo = true;
    const auto mono = rea167h::ramp (2000, false);
    out = pc::apply (*mono, s);
    REA167H_REQUIRE (out->isStereo());
    CHECK (std::abs (out->left[500] - mono->left[500] * (float) std::cos (500.0 * 1.5707963267948966 / 1999.0)) < 1e-6f);
    CHECK (std::abs (out->right[500] - mono->left[500] * (float) std::cos (1499.0 * 1.5707963267948966 / 1999.0)) < 1e-6f);

    // trim: silence below t/32768 cut at both ends
    auto padded = std::make_shared<lynx::SampleData>();
    padded->left.assign (300, 0.001f);
    for (int i = 0; i < 500; ++i)
        padded->left.push_back (0.5f);
    padded->left.insert (padded->left.end(), 200, 0.0f);
    s.precompute = {};
    s.precompute.trim = 256; // −42 dB
    out = pc::apply (*padded, s);
    CHECK ((int) out->frames() == 500 && out->left.front() == 0.5f && out->left.back() == 0.5f);

    // the reverse is processed in playback order: a fade in on a reversed sample fades the file's end... in
    s.precompute = {};
    s.reverse = true;
    s.precompute.fadeIn = 1024;
    out = pc::apply (*in, s);
    // (played backwards: the file's last quarter is the playback's first, silent; frame 100 is near the playback's end)
    CHECK (out->left[1990] == 0.0f && std::abs (out->left[100] - in->left[100] * (float) (1.0 - 100.0 * 4.0 / 1999.0)) < 1e-6f);

    // ping pong: the loop's inside frames follow its end reversed; crossfade 1024 blends half the loop
    auto looped = rea167h::ramp (1000, false);
    looped->loopStart = 200;
    looped->loopEnd = 601; // inclusive end 600
    s = {};
    s.precompute.pingPong = true;
    out = pc::apply (*looped, s);
    CHECK ((int) out->frames() == 1000 + 399 && out->left[601] == looped->left[599] && out->left[999] == looped->left[201]);
    CHECK (out->loopStart == 200 && out->loopEnd == 600 + 399 + 1);
    s = {};
    s.precompute.crossfade = 1024;
    out = pc::apply (*looped, s);
    const auto c = (401 * 1024) / 2048; // 200 frames
    CHECK (out->loopStart == 200 + c && out->loopEnd == 601);
    CHECK (std::abs (out->left[600 - c + 1 + 50] - (float) (looped->left[600 - c + 1 + 50] * (1.0 - 50.0 / c) + looped->left[250] * (50.0 / c))) < 1e-6f);
    // "Use loop points" off: the file's loop is not used
    s.useLoopPoints = false;
    out = pc::apply (*looped, s);
    CHECK (out->loopStart == 500 && out->loopEnd == 1000); // (FL: with no loop the crossfade loops the whole sample first)
}

// §2 the region is cut out and the Sampler maps its Start / End knobs into it; FL's cut edges are declicked
TEST (rea167h_precompute_region_crop_and_play)
{
    namespace pc = lynx::precompute;
    auto in = std::make_shared<lynx::SampleData>();
    in->sampleRate = 44100.0;
    in->left.assign (4410, 0.5f);
    lynx::SamplerSettings s;
    s.regionStart = 0.25f;
    s.regionEnd = 0.75f;
    CHECK (! pc::needed (s)); // a LYNX channel's region alone stays a voice region
    s.flChannel.assign (90, 0);
    REA167H_REQUIRE (pc::needed (s));
    const auto out = pc::apply (*in, s);
    REA167H_REQUIRE (out != nullptr);
    const auto s0 = (int) std::nearbyint (0.25 * 4409.0), s1 = s0 + (int) std::nearbyint (0.5 * 4409.0); // FL: s0 + round(span)
    CHECK ((int) out->frames() == s1 - s0 + 1);
    CHECK (std::abs (out->cropFrom - (double) s0 / 4409.0) < 1e-9 && std::abs (out->cropTo - (double) s1 / 4409.0) < 1e-9);
    // declick in: 22 frames (0.5 ms at 44.1 kHz), the edge frame near 0, the middle untouched; out: 176 frames
    CHECK (std::abs (out->left.front()) < 0.05f && out->left[22] == 0.5f && out->left[1000] == 0.5f);
    CHECK (std::abs (out->left.back()) < 0.05f && out->left[out->frames() - 177] == 0.5f);

    // the sampler plays the whole cut region from the knobs' start
    s.flChannel.clear();
    s.precompute.normalize = true;
    const auto processed = pc::apply (*in, s);
    auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, processed);
    sampler->prepare (44100.0);
    lynx::ChannelDef def;
    def.instrument = lynx::InstrumentKind::sampler;
    def.sampler = s;
    sampler->configure (def, 120.0);
    sampler->noteOn (60, 1.0f);
    std::vector<float> l (4410), r (4410);
    sampler->render (l.data(), r.data(), 4410, 1.0f, 1.0f);
    int sounding = 0;
    for (auto v : l)
        sounding += std::fabs (v) > 1e-4f ? 1 : 0;
    CHECK (std::abs (sounding - (int) processed->frames()) <= 24); // (less the declicked edges' near-zero frames)
}

// §3 a cut voice fades out with FL's declicker: a raised cosine over the channel's declicking release (10 ms for
// "Out only"), 2 ms with "Fast declick for cut groups" — (1 + cos(kπ/(n+1)))/2
TEST (rea167h_cut_declick_times)
{
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 44100.0;
    data->left.assign (44100, 0.5f);
    auto measure = [&] (int mode, bool fast) {
        auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
        sampler->prepare (44100.0);
        lynx::ChannelDef def;
        def.instrument = lynx::InstrumentKind::sampler;
        def.sampler.declickMode = mode;
        sampler->configure (def, 120.0);
        sampler->setFastDeclick (fast);
        sampler->noteOn (60, 1.0f);
        std::vector<float> l (1000), r (1000);
        sampler->render (l.data(), r.data(), 1000, 1.0f, 1.0f);
        const auto level = l[999];
        sampler->cut();
        std::fill (l.begin(), l.end(), 0.0f);
        sampler->render (l.data(), r.data(), 1000, 1.0f, 1.0f);
        int frames = 0;
        while (frames < 1000 && std::fabs (l[(std::size_t) frames]) > 0.0f)
            ++frames;
        return std::pair { frames, l[(std::size_t) std::min (frames / 2, 999)] / level };
    };
    const auto [outOnly, mid] = measure (0, false);
    CHECK (std::abs (outOnly - 441) <= 2);
    CHECK (std::abs (mid - 0.5f) < 0.02f); // a raised cosine is at half way at its middle
    CHECK (std::abs (measure (0, true).first - 88) <= 2);
    CHECK (std::abs (measure (2, false).first - 882) <= 2);
    CHECK (measure (4, false).first == 1000); // 100 ms: still sounding after 1000 frames
}

// FL's release shapes of the declicking modes (FUN_00977870): mode 1 subtracts a follower (shape 1, the signal sinks
// to zero, 10 ms), mode 5 bends the 200 ms raised cosine with curve 5: at its middle 1 − (√6 − 1) / 5 = 0.710
TEST (rea167h_cut_release_shapes)
{
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 44100.0;
    data->left.assign (44100, 0.5f);
    auto run = [&] (int mode, int frames) {
        auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
        sampler->prepare (44100.0);
        lynx::ChannelDef def;
        def.instrument = lynx::InstrumentKind::sampler;
        def.sampler.declickMode = mode;
        sampler->configure (def, 120.0);
        sampler->noteOn (60, 1.0f);
        std::vector<float> l (1000), r (1000);
        sampler->render (l.data(), r.data(), 1000, 1.0f, 1.0f);
        const auto level = l[999];
        sampler->cut();
        std::vector<float> out ((std::size_t) frames), r2 ((std::size_t) frames);
        sampler->render (out.data(), r2.data(), frames, 1.0f, 1.0f);
        for (auto& v : out)
            v /= level;
        return out;
    };
    const auto one = run (1, 1000);
    // the model (FUN_00977870 shape 1): f += c_k (x − f), y = (x − f)·w_k over n = 441 frames, the window over the last 44
    {
        double f = 0.0;
        const int n = 441, m = 44;
        for (int k = 1; k <= n; ++k)
        {
            const auto c = std::min (1.0, 2.0 / 44100.0 * std::exp ((double) k * std::log (22050.0) / (double) n));
            f += c * (1.0 - f);
            const auto w = k > n - m ? 0.5 * (1.0 + std::cos ((double) (k - (n - m)) * 3.141592653589793 / (double) (m + 1))) : 1.0;
            if (k == 1 || k == 300 || k == 380)
                CHECK (std::abs (one[(std::size_t) (k - 1)] - (float) ((1.0 - f) * w)) < 0.01f);
        }
    }
    CHECK (std::fabs (one[460]) == 0.0f); // silent after 10 ms
    const auto five = run (5, 8820);
    CHECK (std::abs (five[4410] - 0.710f) < 0.01f);
    CHECK (five[8819] < 0.01f);
}

// FL's start offset (0xD7 f64 at 0x90) is a fraction of the played region: region 0.25 … 0.75 with offset 0.4 starts
// at 0.25 + 0.4·0.5 = 0.45 of the file [FL render, REA_rodada167h §4]
TEST (rea167h_start_offset_is_in_the_region)
{
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 44100.0;
    data->left.assign (44100, 0.0f);
    std::fill (data->left.begin() + 22050, data->left.end(), 1.0f);
    auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
    sampler->prepare (44100.0);
    lynx::ChannelDef def;
    def.instrument = lynx::InstrumentKind::sampler;
    def.sampler.regionStart = 0.25f;
    def.sampler.regionEnd = 0.75f;
    def.sampler.startOffset = 0.4f;
    sampler->configure (def, 120.0);
    sampler->noteOn (60, 1.0f);
    std::vector<float> l (4000), r (4000);
    sampler->render (l.data(), r.data(), 4000, 1.0f, 1.0f);
    int first = 0;
    while (first < 4000 && std::fabs (l[(std::size_t) first]) < 1.0e-4f)
        ++first;
    std::printf ("  rea167h first %d\n", first);
    CHECK (std::abs (first - 2205) <= 2);

    // and it is imported apart from the region
    const auto project = rea167h::channelProject ([] (FlpBuilder& b) {
        std::vector<std::uint8_t> d7 (0xA8, 0);
        const double start = 0.25, end = 0.5, offset = 0.5;
        std::memcpy (d7.data() + 0x80, &start, 8);
        std::memcpy (d7.data() + 0x88, &end, 8);
        std::memcpy (d7.data() + 0x90, &offset, 8);
        b.data (0xD7, d7);
    });
    const auto song = lynx::importFlp (project);
    REA167H_REQUIRE (! song.channels.empty());
    CHECK (std::abs (song.channels[0].sampler.regionStart - 0.25f) < 1e-6f);
    CHECK (std::abs (song.channels[0].sampler.startOffset - 0.5f) < 1e-6f);
}
