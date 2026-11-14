// Round 190gj (P9): "Split mixer tracks" in one render. Each insert's stem is the render of makeBounceSong (song,
// insert); exportopts::planStemTap / StemMixer make the same samples from the taps of one render of the whole song.
// The engine side (RenderStream, the files) is checked by the app's --stemexport against the old per-stem renders.
#include "core/ExportOptions.h"

namespace s190gj
{
constexpr double rate = 48000.0;

// the test song: drums and a synth on inserts 1-5, a bus (6) and a reverb return (7), delay compensation from manual
// track latencies, a channel on the Master itself, an automated insert (its renders take 128-sample chunks)
lynx::Song song()
{
    auto s = lynx::Song::makeDefault();
    s.mixer = lynx::Song::defaultMixer (7); // the Master and inserts 1-7
    for (std::size_t i = 0; i < s.channels.size(); ++i)
        s.channels[i].mixerTrack = (int) i + 1;
    auto hat2 = s.channels[2];
    hat2.name = "Hat on the Master";
    hat2.mixerTrack = 0;
    hat2.pan = -0.4f;
    s.channels.push_back (hat2);
    auto& p = s.patterns[0];
    for (int step : { 1, 5, 9, 13 })
        p.toggleStep ((int) s.channels.size() - 1, step);

    lynx::EffectSlot comp;
    comp.kind = lynx::EffectKind::compressor;
    s.mixer[1].effects.push_back (comp);
    s.mixer[2].toMaster = false;
    s.mixer[2].sends = { lynx::MixerSend { 6, 1.0f } };
    s.mixer[3].sends = { lynx::MixerSend { 7, 0.5f } };
    s.mixer[3].latencyMs = 3.0f;
    s.mixer[3].volume = 0.6f;
    s.mixer[3].pan = 0.3f;
    lynx::EffectSlot delay;
    delay.kind = lynx::EffectKind::delay;
    s.mixer[4].effects.push_back (delay);
    s.mixer[4].latencyMs = 5.0f;
    s.mixer[4].sends = { lynx::MixerSend { 6, 0.7f } };
    s.mixer[4].masterLevel = 0.8f;
    auto keyed = comp;
    keyed.sidechain = 1; // the synth ducks under the kick: insert 1 does not feed insert 5
    s.mixer[5].effects.push_back (keyed);
    lynx::EffectSlot drive;
    drive.kind = lynx::EffectKind::distortion;
    s.mixer[6].effects.push_back (drive);
    lynx::EffectSlot verb;
    verb.kind = lynx::EffectKind::reverb;
    s.mixer[7].effects.push_back (verb);

    lynx::Automation a;
    a.target = { lynx::AutomationTargetKind::mixerPan, 3 };
    a.points = { { 0, 0.2f, 0.0f }, { 8 * lynx::ppq, 0.9f, 0.0f } };
    s.automations.push_back (a);
    s.playlist.clear();
    for (int bar = 0; bar < 2; ++bar)
    {
        lynx::Clip c;
        c.pattern = 0;
        c.start = bar * 4 * lynx::ppq;
        c.length = 4 * lynx::ppq;
        s.playlist.push_back (c);
    }
    lynx::Clip ac;
    ac.kind = lynx::ClipKind::automation;
    ac.automation = 0;
    ac.length = 8 * lynx::ppq;
    ac.track = 1;
    s.playlist.push_back (ac);
    s.mode = lynx::PlayMode::song;
    return s;
}

// the Master's output of a render of `s` (blocks of 512 from the start, playing), with the taps of `taps` per block
std::vector<float> render (const lynx::Song& s, int blocks, lynx::RackRenderer::RenderTaps* taps = nullptr,
                           const std::function<void (int)>& afterBlock = {})
{
    auto state = lynx::RackState::build (s, nullptr, rate);
    lynx::RackRenderer rr;
    rr.prepare (rate, 512);
    rr.setRenderTaps (taps);
    std::vector<float> out ((std::size_t) blocks * 1024);
    std::vector<float> l (512), r (512);
    const auto tps = lynx::ticksPerSample (s.bpm, rate);
    for (int b = 0; b < blocks; ++b)
    {
        if (taps != nullptr)
            for (auto& d : taps->data)
                std::fill (d.begin(), d.end(), 0.0f);
        rr.process (state.get(), true, b * 512 * tps, 512, l.data(), r.data());
        std::copy (l.begin(), l.end(), out.begin() + (std::ptrdiff_t) b * 1024);
        std::copy (r.begin(), r.end(), out.begin() + (std::ptrdiff_t) b * 1024 + 512);
        if (afterBlock)
            afterBlock (b);
    }
    return out;
}
} // namespace s190gj

TEST (stems190gj_plan_says_which_stems_one_render_makes)
{
    const auto s = s190gj::song();
    const std::vector<int> channels (s.channels.size(), 0);
    std::vector<std::vector<int>> slots (s.mixer.size());
    for (std::size_t t = 0; t < s.mixer.size(); ++t)
        slots[t].assign (s.mixer[t].effects.size(), 0);
    auto plan = [&] (const lynx::Song& song, int t) { return lynx::exportopts::planStemTap (song, t, channels, slots, s190gj::rate); };

    for (int t : { 1, 2, 3, 4, 6, 7 })
        CHECK (plan (s, t).ok);
    // the synth's compressor listens to the kick, which its own render silences
    CHECK (! plan (s, 5).ok && plan (s, 5).why.find ("insert 1 ") != std::string::npos);
    CHECK (! plan (s, 0).ok && ! plan (s, 8).ok);
    // insert 1's stem: itself, at 0 dB, delayed to the Master's alignment (5 ms of insert 4's latency, at 48 kHz)
    const auto one = plan (s, 1);
    CHECK (one.inputs.size() == 1 && one.inputs[0].track == 1 && one.inputs[0].level == 1.0f && one.inputs[0].delay == 240);
    CHECK (one.latency == 240);
    // the bus: its own output and insert 4's, which feeds it and goes to the Master too (its own render hears both)
    const auto bus = plan (s, 6);
    CHECK (bus.inputs.size() == 2);
    CHECK (std::any_of (bus.inputs.begin(), bus.inputs.end(), [] (const auto& in) { return in.track == 4 && in.level == 0.8f; }));
    // insert 2 goes only to the bus: its stem render sends it to the Master at 0 dB
    CHECK (plan (s, 2).inputs.size() == 1 && plan (s, 2).inputs[0].track == 2);

    // what makes a stem need its own render
    auto muted = s;
    muted.mixer[1].muted = true;
    CHECK (! plan (muted, 1).ok && plan (muted, 2).ok);
    auto soloed = s;
    soloed.mixer[3].solo = true;
    CHECK (! plan (soloed, 1).ok);
    auto masterAuto = s;
    masterAuto.automations[0].target = { lynx::AutomationTargetKind::mixerVolume, 0 };
    CHECK (! plan (masterAuto, 1).ok);
    auto routeAuto = s;
    routeAuto.automations[0].target = { lynx::AutomationTargetKind::mixerSend, 1, 0 };
    CHECK (! plan (routeAuto, 1).ok && plan (routeAuto, 3).ok);
    auto peakCtl = s;
    lynx::EffectSlot peak;
    peak.kind = lynx::EffectKind::peakController;
    peakCtl.mixer[3].effects.push_back (peak);
    CHECK (! plan (peakCtl, 1).ok);   // insert 3 is silenced in insert 1's render: its controller would stop
    auto sources = s;
    sources.mixer[6].soloSources = false; // FL's plain solo: the bus alone, without what feeds it
    CHECK (! plan (sources, 6).ok);
}

TEST (stems190gj_taps_of_one_render_equal_each_stems_own_render)
{
    const auto s = s190gj::song();
    const std::vector<int> channels (s.channels.size(), 0);
    std::vector<std::vector<int>> slots (s.mixer.size());
    for (std::size_t t = 0; t < s.mixer.size(); ++t)
        slots[t].assign (s.mixer[t].effects.size(), 0);
    constexpr int blocks = 420; // 4.5 s: the two bars and the effects' tails

    std::vector<lynx::exportopts::StemTap> plans;
    std::vector<int> stems;
    lynx::RackRenderer::RenderTaps taps;
    taps.capacity = 512;
    taps.wanted.assign (s.mixer.size(), 0);
    taps.data.assign (s.mixer.size(), std::vector<float> (1024, 0.0f));
    taps.wanted[0] = 1;
    for (int t = 1; t < (int) s.mixer.size(); ++t)
        if (auto p = lynx::exportopts::planStemTap (s, t, channels, slots, s190gj::rate); p.ok)
        {
            plans.push_back (p);
            stems.push_back (t);
            for (const auto& in : p.inputs)
                taps.wanted[(std::size_t) in.track] = 1;
        }
    CHECK (stems.size() == 6);

    std::vector<lynx::exportopts::StemMixer> mixers;
    for (const auto& p : plans)
        mixers.emplace_back (p);
    std::vector<std::vector<float>> made (stems.size(), std::vector<float> ((std::size_t) blocks * 1024));
    float masterPeak = 0.0f;
    s190gj::render (s, blocks, &taps, [&] (int b) {
        masterPeak = std::max (masterPeak, std::abs (taps.data[0][100]));
        for (std::size_t k = 0; k < stems.size(); ++k)
        {
            std::vector<const float*> l, r;
            for (const auto& in : plans[k].inputs)
            {
                l.push_back (taps.left (in.track));
                r.push_back (taps.right (in.track));
            }
            auto* out = made[k].data() + (std::size_t) b * 1024;
            mixers[k].mix (taps.left (0), taps.right (0), l.data(), r.data(), out, out + 512, 512);
        }
    });

    for (std::size_t k = 0; k < stems.size(); ++k)
    {
        const auto own = s190gj::render (lynx::makeBounceSong (s, stems[k]), blocks);
        std::size_t differ = 0, sounding = 0;
        for (std::size_t i = 0; i < own.size(); ++i)
        {
            differ += own[i] != made[k][i] ? 1 : 0; // (±0 compare equal: the graph's output adds +0 to both)
            sounding += own[i] != 0.0f ? 1 : 0;
        }
        CHECK (differ == 0);
        CHECK (sounding > 1000);
        if (differ != 0)
            std::printf ("  stem of insert %d: %zu samples differ\n", stems[k], differ);
    }
}
