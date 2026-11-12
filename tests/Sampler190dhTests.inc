// Round 190dh: FL's Sampler page (Channel settings › Sampler) — the laws its controls apply (core/SamplerPage.h), the
// loop points of the Playback START / END knobs in the precompute, FL's loop record 0xFB and Content flags 0x8F in
// .flp import / export. FL facts in /mnt/e/lynx-work/sampler190dh/FL-FACTS.md.

#include "core/SamplerPage.h"

namespace sampler190dh
{
inline std::shared_ptr<lynx::SampleData> tone (int frames)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = 44100.0;
    for (int i = 0; i < frames; ++i)
        d->left.push_back (0.2f + 0.6f * std::sin (0.021f * (float) i) * (1.0f - (float) i / (float) frames));
    return d;
}

inline flp::Project oneSampler (const std::function<void (FlpBuilder&)>& events)
{
    FlpBuilder b;
    b.u8 (0x40);
    b.u16 (0);
    b.u8 (0x15);
    b.u8 (0);
    events (b);
    return flp::parseProject (b.bytes);
}

// FL's 0xFB loop record, version 2, as FL writes it (FL's own files: LeriQ - SHINE.flp, Remix a song.flp)
inline std::vector<std::uint8_t> loopRecord (std::int64_t a, std::int64_t b, bool useLoops)
{
    std::vector<std::uint8_t> r;
    auto put = [&r] (const auto v) {
        const auto* p = reinterpret_cast<const std::uint8_t*> (&v);
        r.insert (r.end(), p, p + sizeof (v));
    };
    put ((std::int32_t) 2);
    put (a);
    put (b);
    put (-1.0);
    put ((std::int32_t) -1);
    put ((std::uint8_t) 0);
    put (std::numeric_limits<std::int32_t>::max());
    put (std::numeric_limits<std::int32_t>::max());
    put ((std::uint8_t) (useLoops ? 1 : 0));
    put ((std::uint8_t) 0);
    return r;
}
} // namespace sampler190dh

// Ping pong loop (FL's PingPongBox, sample +0x14c): the loop's inside frames follow its end, reversed; it works on the
// loop points of the Playback knobs as on the file's own loop, and only with "Use loop points"
TEST (sampler190dh_ping_pong_loop)
{
    namespace pc = lynx::precompute;
    const auto in = sampler190dh::tone (1200);
    lynx::SamplerSettings s;
    // no loop in the file: the knobs' loop 300…500 (inclusive)
    s.loopStart = 300, s.loopEnd = 500;
    CHECK (pc::needed (s));
    auto out = pc::apply (*in, s);
    CHECK (out != nullptr && out->loopStart == 300 && out->loopEnd == 501 && (int) out->frames() == 1200);
    s.precompute.pingPong = true;
    out = pc::apply (*in, s);
    CHECK (out != nullptr && (int) out->frames() == 1200 + 199); // frames 499…301 appended after 500
    CHECK (out->left[501] == in->left[499] && out->left[699] == in->left[301] && out->left[700] == in->left[501]);
    CHECK (out->loopStart == 300 && out->loopEnd == 500 + 199 + 1);
    // "Use loop points" off: no loop, so nothing to bounce
    s.useLoopPoints = false;
    out = pc::apply (*in, s);
    CHECK (out != nullptr && (int) out->frames() == 1200 && out->loopStart < 0);
    // the knobs' loop replaces the file's
    auto looped = sampler190dh::tone (1200);
    looped->loopStart = 100, looped->loopEnd = 201;
    s = {};
    s.precompute.pingPong = true;
    out = pc::apply (*looped, s);
    CHECK (out != nullptr && out->loopStart == 100 && out->loopEnd == 200 + 99 + 1);
    s.loopStart = 600, s.loopEnd = 700;
    out = pc::apply (*looped, s);
    CHECK (out != nullptr && out->loopStart == 600 && out->loopEnd == 700 + 99 + 1 && out->left[701] == looped->left[699]);
    // a sampler with no loop override and no effect is not precomputed (old projects play as before)
    CHECK (! pc::needed (lynx::SamplerSettings {}));
}

// FL's loop knobs (FUN_01007150 / FUN_010074b0): START clamps to the sample and drags END after it; END never goes
// before START; Shift moves the whole loop
TEST (sampler190dh_loop_knobs)
{
    using namespace lynx::samplerpage;
    lynx::SamplerSettings s;
    CHECK ((shownLoop (s, -1, -1) == std::pair<std::int64_t, std::int64_t> (-1, -1)));
    CHECK ((shownLoop (s, 100, 201) == std::pair<std::int64_t, std::int64_t> (100, 200))); // (the file's end is exclusive)
    setLoopStart (s, 300, 1000, -1, -1);
    CHECK (s.loopStart == 300 && s.loopEnd == 301);
    setLoopEnd (s, 800, 1000, -1, -1);
    CHECK (s.loopStart == 300 && s.loopEnd == 800);
    setLoopEnd (s, 100, 1000, -1, -1);
    CHECK (s.loopEnd == 300);
    setLoopEnd (s, 800, 1000, -1, -1);
    setLoopStart (s, 900, 1000, -1, -1);
    CHECK (s.loopStart == 900 && s.loopEnd == 901);
    setLoopStart (s, 5000, 1000, -1, -1);
    CHECK (s.loopStart == 999 && s.loopEnd == 999);
    // Shift: the loop moves, its length kept, inside the sample
    s.loopStart = 200, s.loopEnd = 400;
    setLoopStart (s, 700, 1000, -1, -1, true);
    CHECK (s.loopStart == 700 && s.loopEnd == 900);
    setLoopEnd (s, 1500, 1000, -1, -1, true);
    CHECK (s.loopStart == 799 && s.loopEnd == 999);
    // from the file's loop: the first turn copies it, then edits it
    lynx::SamplerSettings f;
    setLoopEnd (f, 500, 1000, 100, 201);
    CHECK (f.loopStart == 100 && f.loopEnd == 500);
}

// Declicking mode (FL's DeclickModeSelect, .flp 0xD7 byte 0x53): FL's six items in FL's order, each to its cut release
TEST (sampler190dh_declicking_modes)
{
    using namespace lynx::samplerpage;
    const std::vector<std::string> fl = { "Out only (no bleeding)", "Transient (no bleeding)", "Transient (bleeding)",
                                          "Generic (bleeding)", "Smooth (bleeding)", "Crossfade (bleeding)" };
    CHECK (std::size (declickModeNames) == fl.size());
    for (std::size_t i = 0; i < fl.size(); ++i)
        CHECK (fl[i] == declickModeNames[i]);
    // each mode's cut release, measured on the sampler voice (a raised cosine; mode 1 a follower, mode 5 curved)
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 44100.0;
    data->left.assign (44100, 0.5f);
    for (int mode = 0; mode < 6; ++mode)
    {
        auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
        sampler->prepare (44100.0);
        lynx::ChannelDef def;
        def.instrument = lynx::InstrumentKind::sampler;
        def.sampler.declickMode = mode;
        sampler->configure (def, 120.0);
        sampler->noteOn (60, 1.0f);
        std::vector<float> l (12000), r (12000);
        sampler->render (l.data(), r.data(), 1000, 1.0f, 1.0f);
        sampler->cut();
        std::fill (l.begin(), l.end(), 0.0f);
        sampler->render (l.data(), r.data(), 12000, 1.0f, 1.0f);
        int frames = 0; // (one past the last sounding frame)
        for (int i = 0; i < 12000; ++i)
            if (std::fabs (l[(std::size_t) i]) > 1.0e-6f)
                frames = i + 1;
        const auto expected = (int) std::lround (declickReleaseMs[mode] * 44.1);
        // (mode 1 subtracts a follower: the voice sinks below 1e-6 before its 10 ms end, ~383 frames; mode 5's curve
        // reaches it a few frames early)
        if (mode == 1)
            CHECK (frames > expected / 2 && frames <= expected + 3);
        else
            CHECK (std::abs (frames - expected) <= 6);
    }
    // the .flp byte goes to the page's mode and back
    const auto p = sampler190dh::oneSampler ([] (FlpBuilder& b) {
        std::vector<std::uint8_t> d7 (0xA8, 0);
        d7[0x53] = 4;
        b.data (0xD7, d7);
    });
    const auto song = lynx::importFlp (p);
    CHECK (! song.channels.empty() && song.channels[0].sampler.declickMode == 4);
}

// FL's precompute order (FUN_00ff0db0, REA round 167h §2): crop, ping pong, reverse, fades, fade stereo, remove DC,
// polarity, normalize, crossfade, swap stereo, trim — checked where the order changes the result
TEST (sampler190dh_precomputed_effects_order)
{
    namespace pc = lynx::precompute;
    const auto n = 2000;
    auto in = std::make_shared<lynx::SampleData>();
    in->sampleRate = 44100.0;
    for (int i = 0; i < n; ++i)
    {
        in->left.push_back (0.3f + 0.4f * std::sin (0.05f * (float) i));
        in->right.push_back (0.1f * std::cos (0.03f * (float) i));
    }
    lynx::SamplerSettings s;
    // normalize comes after the fade out: the faded peak (not the file's) is brought to 1
    s.precompute.fadeOut = 512;
    s.precompute.normalize = true;
    auto out = pc::apply (*in, s);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max ({ peak, std::fabs (out->left[(std::size_t) i]), std::fabs (out->right[(std::size_t) i]) });
    CHECK (std::abs (peak - 1.0f) < 1e-6f);
    // remove DC comes after the fade in: the faded signal's mean is 0 (not the file's then faded)
    s = {};
    s.precompute.fadeIn = 256;
    s.precompute.removeDc = true;
    out = pc::apply (*in, s);
    double mean = 0.0;
    for (int i = 0; i < n; ++i)
        mean += out->left[(std::size_t) i];
    CHECK (std::abs (mean / n) < 1e-5);
    // swap stereo comes after fade stereo: the left output carries the right channel's fade (cos((N−1−i)·π/2/(N−1)))
    s = {};
    s.precompute.fadeStereo = true;
    s.precompute.swapStereo = true;
    out = pc::apply (*in, s);
    const auto a = 1.5707963267948966 / (double) (n - 1);
    CHECK (std::abs (out->left[300] - (float) (in->right[300] * std::cos ((double) (n - 1 - 300) * a))) < 1e-6f);
    // the fades run in playback order on a reversed sample (reverse before the fades, back at the end)
    s = {};
    s.reverse = true;
    s.precompute.fadeOut = 1024;
    out = pc::apply (*in, s);
    CHECK (out->left[100] == 0.0f && out->left[n - 1] != 0.0f);
    // trim comes last: it cuts the silence the fade out made (fade out 1024 silences all after the first quarter)
    s = {};
    s.precompute.fadeOut = 1024;
    s.precompute.trim = 64;
    out = pc::apply (*in, s);
    CHECK ((int) out->frames() < n / 4 + 2);
    // ping pong comes before the crossfade: the crossfade blends the bounced loop's end into its start
    auto looped = std::make_shared<lynx::SampleData> (*in);
    looped->loopStart = 400, looped->loopEnd = 801;
    s = {};
    s.precompute.pingPong = true;
    s.precompute.crossfade = 1024;
    out = pc::apply (*looped, s);
    const auto bounced = 800 + 399; // the loop's end after the bounce
    const auto c = (std::int64_t) ((bounced - 400 + 1) * 1024 / 2048);
    CHECK (out->loopStart == 400 + c && out->loopEnd == bounced + 1);
    // CROSSFADE above 0 switches "Use loop points" on (FL's FXAttackWheelChange)
    lynx::SamplerSettings x;
    x.useLoopPoints = false;
    lynx::samplerpage::turnCrossfade (x, 0);
    CHECK (! x.useLoopPoints);
    lynx::samplerpage::turnCrossfade (x, 300);
    CHECK (x.useLoopPoints && x.precompute.crossfade == 300);
}

// Time stretching: TIME × 2^(MUL/10000) is the length; PITCH is FL's stretch pitch, and in Resample mode turning it
// switches the mode to Auto; the Mode list keeps Resample and LYNX's engines
TEST (sampler190dh_time_stretching_values)
{
    using namespace lynx::samplerpage;
    lynx::SamplerSettings s;
    CHECK (stretchTime (s) == 0.0f);
    setStretchTime (s, 4.0f);
    CHECK (s.stretchBeats == 4.0f && stretchTime (s) == 4.0f);
    setStretchMul (s, 10000, 0.0); // ×2
    CHECK (std::abs (s.stretchBeats - 8.0f) < 1e-5f && std::abs (stretchTime (s) - 4.0f) < 1e-5f && s.stretchMul == 10000);
    setStretchMul (s, -20000, 0.0); // ×0.25
    CHECK (std::abs (s.stretchBeats - 1.0f) < 1e-5f);
    setStretchTime (s, 8.0f);
    CHECK (std::abs (s.stretchBeats - 2.0f) < 1e-5f);
    setStretchMul (s, 99999, 0.0);
    CHECK (s.stretchMul == 20000);
    // MUL without a TIME: the sample's own length (at the project tempo) times the factor
    lynx::SamplerSettings m;
    setStretchMul (m, 10000, 3.0);
    CHECK (std::abs (m.stretchBeats - 6.0f) < 1e-5f);
    setStretchTime (m, 0.0f);
    CHECK (m.stretchBeats == 0.0f);

    // PITCH: FL's stretch pitch inside the channel's pitch
    lynx::SamplerSettings p;
    p.pitch = 2.0f; // (the header's PITCH)
    turnStretchPitch (p, 300);
    CHECK (p.flStretchPitch == 300 && std::abs (p.pitch - 5.0f) < 1e-5f);
    CHECK (p.keepPitch && p.flStretchMode == 5 && p.stretchMethod == lynx::StretchMethod::general); // (Resample → Auto)
    turnStretchPitch (p, -1500);
    CHECK (p.flStretchPitch == -1200 && std::abs (p.pitch - (-10.0f)) < 1e-4f);
    lynx::SamplerSettings z;
    turnStretchPitch (z, 0); // (back at 0 in Resample: the mode stays)
    CHECK (! z.keepPitch);
    z.keepPitch = true;
    z.stretchMethod = lynx::StretchMethod::mono;
    turnStretchPitch (z, 100);
    CHECK (z.stretchMethod == lynx::StretchMethod::mono && z.flStretchMode == 0); // (a stretch mode is kept)

    // the Mode list
    CHECK (std::string (modeItems[modeIndex (lynx::SamplerSettings {})].name) == "Resample");
    lynx::SamplerSettings k;
    for (int i = 0; i < modeItemCount; ++i)
    {
        selectMode (k, i);
        if (! modeItems[i].header)
            CHECK (modeIndex (k) == i);
    }
    selectMode (k, 1);
    CHECK (! k.keepPitch);

    // the .flp's TIME and MUL come back as the page shows them, and go out the same way
    const auto proj = sampler190dh::oneSampler ([] (FlpBuilder& b) {
        std::vector<std::uint8_t> d7 (0xA8, 0);
        const std::int32_t pitch = 0, mul = 5000, mode = -1;
        const std::uint32_t time = 768; // 4 beats
        std::memcpy (d7.data() + 0x60, &time, 4);
        std::memcpy (d7.data() + 0x64, &pitch, 4);
        std::memcpy (d7.data() + 0x68, &mul, 4);
        std::memcpy (d7.data() + 0x6C, &mode, 4);
        b.data (0xD7, d7);
        b.utf16 (0xC4, u"C:\\samples\\loop.wav");
    });
    const auto song = lynx::importFlp (proj);
    CHECK (! song.channels.empty());
    if (! song.channels.empty())
    {
        const auto& t = song.channels[0].sampler;
        CHECK (t.stretchMul == 5000 && std::abs (stretchTime (t) - 4.0f) < 1e-4f && std::abs (t.stretchBeats - 4.0f * (float) std::sqrt (2.0)) < 1e-4f);
    }
}

// Content (FL's 0x8F bits) and the loop record 0xFB: imported, saved back by the export
TEST (sampler190dh_content_flags_and_loop_record)
{
    using namespace lynx::samplerpage;
    const auto p = sampler190dh::oneSampler ([] (FlpBuilder& b) {
        b.data (0xFB, sampler190dh::loopRecord (1200, 3400, true));
        b.u8 (0x8F);
        b.u32 (0x100 | 8 | 4 | 1); // keep on disk, use loop points, slice markers, resample (no load regions)
        b.utf16 (0xC4, u"C:\\samples\\loop.wav"); // (a sampler without a sample imports as LYNX's synth)
    });
    CHECK (p.channels.size() == 1);
    if (p.channels.empty())
        return;
    CHECK (p.channels[0].loopStart == 1200 && p.channels[0].loopEnd == 3400);
    auto song = lynx::importFlp (p);
    CHECK (! song.channels.empty());
    if (song.channels.empty())
        return;
    const auto& s = song.channels[0].sampler;
    CHECK (s.keepOnDisk && s.resampleOnLoad && ! s.loadRegions && s.loadSliceMarkers && s.useLoopPoints);
    CHECK (s.loopStart == 1200 && s.loopEnd == 3400 && contentFlags (s) == (0x100 | 8 | 4 | 1));
    // FL's own "unset" record (FL's demos): no loop points
    const auto unset = sampler190dh::oneSampler ([] (FlpBuilder& b) { b.data (0xFB, sampler190dh::loopRecord (-1, std::numeric_limits<std::int64_t>::max(), true)); });
    CHECK (! unset.channels.empty() && unset.channels[0].loopStart == -1);
    // round trip through the export (the sample file is not on disk here, so the import guessed a stand-in: make it the
    // sampler it is in FL)
    song.channels[0].instrument = lynx::InstrumentKind::sampler;
    song.channels[0].samplePath = "/samples/loop.wav";
    lynx::flpexport::Report report;
    const auto exported = lynx::flpexport::toProject (song, {}, report);
    bool found = false;
    for (const auto& c : exported.channels)
        if (c.loopStart == 1200 && c.loopEnd == 3400 && c.sampleFlags == (0x100 | 8 | 4 | 1))
            found = true;
    CHECK (found);
    // a LYNX sampler with the defaults exports what LYNX wrote before (load regions + use loop points), no loop record
    lynx::SamplerSettings d;
    CHECK (contentFlags (d) == (2 | 8));
    d.useLoopPoints = false;
    CHECK (contentFlags (d) == 2);
    setContentFlags (d, 0x10B);
    CHECK (d.keepOnDisk && d.resampleOnLoad && d.loadRegions && d.useLoopPoints && ! d.loadSliceMarkers);
    // FL greys Keep on disk while a loop plays
    lynx::SamplerSettings k;
    CHECK (keepOnDiskAvailable (k, false) && ! keepOnDiskAvailable (k, true));
    k.useLoopPoints = false;
    CHECK (keepOnDiskAvailable (k, true));
}

// SMP START / LENGTH: FL's end is relative, so moving the start keeps the length
TEST (sampler190dh_smp_start_and_length)
{
    using namespace lynx::samplerpage;
    lynx::SamplerSettings s;
    CHECK (lengthOf (s) == 1.0f);
    setLength (s, 0.5f);
    CHECK (s.regionStart == 0.0f && std::abs (s.regionEnd - 0.5f) < 1e-6f);
    setSmpStart (s, 0.5f);
    CHECK (std::abs (s.regionStart - 0.5f) < 1e-6f && std::abs (s.regionEnd - 0.75f) < 1e-6f && std::abs (lengthOf (s) - 0.5f) < 1e-6f);
    setSmpStart (s, 1.0f);
    CHECK (s.regionEnd == 1.0f && lengthOf (s) == 1.0f);
}
