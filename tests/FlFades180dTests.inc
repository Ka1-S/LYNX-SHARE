// --- Round 180d: FL's clip fades: the on bits of +0x38 and the curve values (FL renders of one clip with each value) --------

#include <cstring>

TEST (render180d_fl_clip_fades_play_only_when_their_bit_is_on)
{
    using namespace lynx;
    flp::Project p;
    p.tempo = 120.0;
    flp::Channel channel;
    channel.name = "Clip";
    p.channels.push_back (channel);
    flp::Arrangement a;
    flp::PlaylistItem item;
    item.kind = flp::PlaylistItem::Kind::channelClip;
    item.length = 960;
    item.fadeInMs = 1234.5f;
    item.fadeInTension = -0.5f;
    item.fadeOutMs = 200.0f;
    item.fadeOutTension = -812.933f;
    a.items.push_back (item);
    p.arrangements.push_back (a);

    auto bytes = flp::writeProject (p);
    {
        const auto back = flp::parseProject (bytes).arrangements.at (0).items.at (0);
        CHECK (back.fadeInMs == 1234.5f && back.fadeOutMs == 200.0f);
        CHECK (back.fadeInTension == -0.5f && back.fadeOutTension == -812.933f);
    }

    // the same record with each value of +0x38: the fade's length stays in the file, FL does not play it
    std::size_t record = 0;
    for (std::size_t i = 0; i + 4 <= bytes.size() && record == 0; ++i)
        if (std::memcmp (bytes.data() + i, &item.fadeInMs, 4) == 0)
            record = i - 0x24;
    CHECK (record > 0);
    const auto flags = [&] (std::uint8_t value) {
        bytes[record + 0x38] = value;
        return flp::parseProject (bytes).arrangements.at (0).items.at (0);
    };
    const auto none = flags (0);
    CHECK (none.fadeInMs == 0.0f && none.fadeOutMs == 0.0f && none.fadeInTension == 0.0f && none.fadeOutTension == 0.0f);
    const auto in = flags (1);
    CHECK (in.fadeInMs == 1234.5f && in.fadeOutMs == 0.0f && in.fadeOutTension == 0.0f);
    const auto out = flags (2);
    CHECK (out.fadeInMs == 0.0f && out.fadeOutMs == 200.0f && out.fadeOutTension == -812.933f);
}

TEST (render180d_export_turns_on_the_clip_fades_it_writes)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    ChannelDef audio;
    audio.instrument = InstrumentKind::audioClip;
    audio.samplePath = "C:/x.wav";
    song.channels.push_back (audio);
    Clip c;
    c.kind = ClipKind::audio;
    c.channel = (int) song.channels.size() - 1;
    c.track = 2;
    c.length = ppq * 8;
    c.fadeIn = ppq;
    c.fadeOutTension = -18.4f; // no fade-out: nothing to keep
    song.playlist.clear();
    song.playlist.push_back (c);
    c.start = ppq * 16;
    c.fadeIn = 0;
    c.fadeOut = ppq * 2;
    c.fadeOutTension = -18.4f;
    song.playlist.push_back (c);

    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    std::vector<flp::PlaylistItem> items;
    for (const auto& item : project.arrangements.at (0).items)
        if (item.kind == flp::PlaylistItem::Kind::channelClip)
            items.push_back (item);
    CHECK (items.size() == 2);
    CHECK (items[0].fadeInMs > 0.0f && items[0].fadeOutMs == 0.0f && items[0].fadeOutTension == 0.0f);
    CHECK (items[1].fadeInMs == 0.0f && items[1].fadeOutMs > 0.0f && items[1].fadeOutTension == -18.4f);
}

TEST (render180d_new_clips_fade_on_fl_s_curve_and_linear_ones_stay_linear)
{
    // FL's measured fade-in: 58.33 ms, 0.158 at 15 ms and 0.506 at 29 ms
    using namespace lynx;
    CHECK (std::abs (clipops::crossfadeCurve (15.5f / 58.33f) - 0.158f) < 0.02f);
    CHECK (std::abs (clipops::crossfadeCurve (29.5f / 58.33f) - 0.506f) < 0.02f);

    auto levelAt = [] (bool linear) {
        lynx::Song s;
        s.bpm = 120.0;
        s.mode = lynx::PlayMode::song;
        s.mixer = lynx::Song::defaultMixer (1);
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::audioClip;
        c.samplePath = "x.wav";
        c.volume = 1.0f;
        c.mixerTrack = 1;
        s.channels.push_back (c);
        s.patterns.emplace_back();
        lynx::Clip clip;
        clip.kind = lynx::ClipKind::audio;
        clip.channel = 0;
        clip.length = lynx::ppq * 4;
        clip.fadeIn = lynx::ppq; // 0.5 s at 120 BPM
        clip.linearFades = linear ? 3 : 0;
        s.playlist = { clip };
        auto provider = [] (const std::string&) {
            auto d = std::make_shared<lynx::SampleData>();
            d->left.assign (48000 * 4, 1.0f);
            return std::shared_ptr<const lynx::SampleData> (d);
        };
        auto state = lynx::RackState::build (s, nullptr, 48000.0, provider);
        lynx::RackRenderer r;
        r.prepare (48000.0, 512);
        std::vector<float> all, l (512), rr (512);
        const auto tps = lynx::ticksPerSample (s.bpm, 48000.0);
        for (double tick = 0.0; tick < lynx::ppq * 1.0; tick += 512 * tps)
        {
            r.process (state.get(), true, tick, 512, l.data(), rr.data());
            all.insert (all.end(), l.begin(), l.end());
        }
        return all[6000] / all[23999 - 512]; // a quarter into the fade, against nearly its end
    };
    const auto curve = levelAt (false), straight = levelAt (true);
    CHECK (std::abs (straight - 0.25f / 0.98f) < 0.03f);
    CHECK (std::abs (curve - 0.146f) < 0.03f);
}

TEST (render180d_fade_tension_matches_fl_render)
{
    using namespace lynx;
    CHECK (clipops::fadeTensionCurve (0.3f, 0.0f) == 0.3f);
    CHECK (clipops::fadeTensionCurve (0.3f, 0.001f) == 0.3f); // FL rounds it to 0.01
    for (const auto t : { -5000.0f, -50.0f, 50.0f, 5000.0f })
    {
        CHECK (std::abs (clipops::fadeTensionCurve (0.0f, t)) < 1e-5f);
        CHECK (std::abs (clipops::fadeTensionCurve (1.0f, t) - 1.0f) < 1e-5f);
        // a fade out with t and a fade in with -t add up to 1 (FL's mirrored crossfade tensions)
        CHECK (std::abs (clipops::fadeTensionCurve (0.4f, t) + clipops::fadeTensionCurve (0.6f, -t) - 1.0f) < 1e-4f);
    }
    // FL's 200 ms fade-out at -812.933, 62 ms into a 262 ms clip: measured gain at each time (10 ms window centres)
    const std::pair<float, float> measured[] = { { 85.0f, 0.807f }, { 105.0f, 0.464f }, { 135.0f, 0.133f }, { 165.0f, 0.029f } };
    for (const auto& [ms, gain] : measured)
    {
        const auto y = clipops::crossfadeCurve ((262.0f - ms) / 200.0f);
        CHECK (std::abs (clipops::fadeTensionCurve (y, -812.933f) - gain) < 0.035f);
    }
}
