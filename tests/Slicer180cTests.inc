// Round 180c, slice 2: Fruity Slicer as a generator (core/FruitySlicer.h, docs/analise/REA_rodada180c.md)

#include "core/FruitySlicer.h"

namespace slicer180c
{
lynx::SampleData ramp (int frames, double rate = 44100.0, bool stereo = false)
{
    lynx::SampleData d;
    d.sampleRate = rate;
    for (int i = 0; i < frames; ++i)
        d.left.push_back (0.5f + 0.25f * (float) std::sin (i * 0.01));
    if (stereo)
        d.right = d.left;
    return d;
}

lynx::fslicer::State threeSlices()
{
    auto s = lynx::fslicer::fresh ("C:\\loops\\beat.wav", 44100);
    s.slices.clear();
    for (int p : { 0, 4410, 8820 })
    {
        lynx::fslicer::Slice slice;
        slice.position = p;
        s.slices.push_back (slice);
    }
    s.slices[1].name = "Snare";
    return s;
}
} // namespace slicer180c

TEST (slicer180c_state_round_trip)
{
    using namespace lynx;
    auto s = slicer180c::threeSlices();
    s.fadeInMs = 3;
    s.fadeOutMs = 40;
    s.declick = 1;
    s.playToEnd = 1;
    s.firstNote = 48;
    s.slices[2].flags = 1;
    s.voiceBlock.assign (364, 7);
    const auto bytes = fslicer::write (s);
    const auto back = fslicer::parse (bytes);
    CHECK (back.has_value());
    if (! back)
        return;
    CHECK (*back == s);
    CHECK (fslicer::write (*back) == bytes);
    CHECK (back->slices[1].name == "Snare" && back->slices[2].reversed());
    // 0x20: the path, after the eight header fields
    CHECK (bytes.size() > 0x21 && bytes[0x20] == (std::uint8_t) s.path.size());
    CHECK (! fslicer::parse ({ 1, 2, 3 }).has_value());
}

TEST (slicer180c_fl_demo_states_round_trip)
{
    // every Fruity Slicer state in FL's demo projects: LYNX reads it and writes the same bytes (version 16)
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto demos = std::filesystem::path (realFlpDir) / "Demo projects";
    if (! std::filesystem::exists (demos))
        return;
    int seen = 0, v16 = 0;
    for (auto& entry : std::filesystem::recursive_directory_iterator (demos))
    {
        if (entry.path().extension() != ".flp")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        flp::Project project;
        try
        {
            project = flp::parseProject (bytes);
        }
        catch (...)
        {
            continue;
        }
        for (const auto& c : project.channels)
            if (c.pluginName == "Fruity Slicer")
            {
                ++seen;
                const auto state = fslicer::parse (c.pluginState);
                CHECK (state.has_value());
                if (state && state->version == 16)
                {
                    ++v16;
                    CHECK (fslicer::write (*state) == c.pluginState);
                }
                if (state)
                    CHECK (! state->slices.empty() && state->slices.front().position == 0);
            }
    }
    CHECK (seen >= 1);
    std::printf ("  fruity slicer: %d demo states, %d of version 16\n", seen, v16);
}

TEST (slicer180c_slice_lengths_and_sampler_table)
{
    using namespace lynx;
    const auto s = slicer180c::threeSlices();
    const auto lengths = fslicer::sliceLengths (s, 20000);
    CHECK ((lengths == std::vector<std::int64_t> { 4410, 4410, 20000 - 8820 }));

    ChannelDef def;
    auto t = s;
    t.firstNote = 48;
    t.playToEnd = 1;
    fslicer::configureSampler (def, t, 20000);
    CHECK (def.sampler.slices.size() == 3 && def.sampler.slices[1] == (float) (4410.0 / 20000.0));
    CHECK (def.sampler.sliceFirstKey == 48 && def.sampler.slicesToEnd);
}

TEST (slicer180c_fades_follow_fl)
{
    // FUN_00607ed0: t = (k − first + 1)/(n + 1); fade in linear above 50 ms, the fade out (11^(1−t) − 1)/10; at
    // 50 ms or less both are raised cosines
    using namespace lynx;
    auto flat = [] (int n) {
        SampleData d;
        d.sampleRate = 1000.0; // 1 frame = 1 ms
        d.left.assign ((std::size_t) n, 1.0f);
        return d;
    };
    auto a = flat (100);
    fslicer::fadeRegion (a, 0, 99, true);
    CHECK (std::fabs (a.left[0] - 1.0f / 101.0f) < 1e-6f && std::fabs (a.left[99] - 100.0f / 101.0f) < 1e-6f);

    auto b = flat (100);
    fslicer::fadeRegion (b, 0, 99, false);
    const auto w = [] (double t) { return ((double) (float) (std::exp ((double) (float) (1.0 - t) * std::log (11.0)) - 1.0)) * 0.1; };
    CHECK (std::fabs (b.left[0] - (float) w (1.0 / 101.0)) < 1e-6f);
    CHECK (std::fabs (b.left[50] - (float) w (51.0 / 101.0)) < 1e-6f);
    CHECK (b.left[99] < 0.02f && b.left[0] > 0.97f);

    auto c = flat (20); // 20 ms: raised cosine
    fslicer::fadeRegion (c, 0, 19, true);
    const auto cosine = [] (double t) { return (std::cos ((1.0 - t) * 3.141592653589793) + 1.0) * 0.5; };
    CHECK (std::fabs (c.left[9] - (float) cosine (10.0 / 21.0)) < 1e-6f);
}

TEST (slicer180c_prepare_reverses_declicks_and_fades)
{
    using namespace lynx;
    auto s = slicer180c::threeSlices();
    const auto sample = slicer180c::ramp (13230, 44100.0, true);

    // nothing to do: the sample comes back as it was
    auto same = fslicer::prepare (sample, s);
    CHECK (same->left == sample.left && same->right == sample.right);

    // a reversed slice is the same frames backwards; the others stay
    s.slices[1].flags = 1;
    auto rev = fslicer::prepare (sample, s);
    CHECK (rev->left[4410] == sample.left[8819] && rev->left[8819] == sample.left[4410]);
    CHECK (rev->left[100] == sample.left[100] && rev->left[9000] == sample.left[9000]);
    s.slices[1].flags = 0;

    // declick: each slice ends at silence; its start is untouched
    s.declick = 1;
    auto dc = fslicer::prepare (sample, s);
    CHECK (std::fabs (dc->left[4409]) < 1e-4f && std::fabs (dc->left[13229]) < 1e-4f);
    CHECK (dc->left[0] == sample.left[0] && dc->left[4410] == sample.left[4410]);
    CHECK (dc->left[3000] == sample.left[3000]); // before the last 28 ms
    s.declick = 0;

    // fades: 10 ms in (cosine: 441 frames = 10 ms) and 60 ms out (the L11 curve), per slice
    s.fadeInMs = 10;
    s.fadeOutMs = 60;
    auto fd = fslicer::prepare (sample, s);
    CHECK (fd->left[0] < 0.001f && fd->left[4410] < 0.001f);
    CHECK (fd->left[441] == sample.left[441]);
    CHECK (fd->left[4409] < 0.01f);
}
