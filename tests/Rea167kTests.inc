// Round 167k: FL's transport options after a jump, "Reset plugins on transport" for generators, the gap table keys
// (docs/analise/REA_rodada167k.md)
#define REA167K_REQUIRE(x) \
    do { if (! (x)) { CHECK (x); return; } } while (false)

namespace rea167k
{
// counts the transport resets it gets
struct CountingInstrument final : lynx::Instrument
{
    int resets = 0, panics = 0;
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void panic() noexcept override { ++panics; }
    void transportReset() noexcept override { ++resets; }
    void render (float*, float*, int, float, float) noexcept override {}
};
} // namespace rea167k

TEST (rea167k_truncated_notes_chase_after_a_user_jump_not_the_loop_wrap)
{
    // FL's set-position (FUN_011e7a10 → FUN_011e78a0 → FUN_011e7470 with the catch-up flag): a note crossing the new
    // position plays after the user moves the position while playing; the loop's wrap is no set-position
    auto song = lynx::Song::makeDefault();
    song.playlist.clear();
    song.mode = lynx::PlayMode::pattern;
    REA167K_REQUIRE (song.channels.size() > 4 && ! song.patterns.empty());
    song.patterns[0].notes.clear();
    lynx::Note n;
    n.start = 0;
    n.length = 16 * lynx::ppq;
    n.key = 60;
    n.channel = 4; // the Synth
    song.patterns[0].notes.push_back (n);
    song.currentPattern = 0;
    const double rate = 48000.0, tps = lynx::ticksPerSample (song.bpm, rate);
    // plays 4 blocks from 0, jumps to beat 2 (a user jump when `user`), then measures 1.0 … 1.5 s after the jump
    auto tail = [&] (bool truncated, bool user) {
        auto state = lynx::RackState::build (song, nullptr, rate);
        lynx::RackRenderer renderer;
        renderer.prepare (rate, 512);
        unsigned jumps = 0;
        renderer.setTransportOptions (truncated, false, jumps);
        std::vector<float> l (512), r (512);
        double tick = 0.0;
        for (int b = 0; b < 4; ++b, tick += 512 * tps)
            renderer.process (state.get(), true, tick, 512, l.data(), r.data());
        tick = 2.0 * lynx::ppq;
        if (user)
            renderer.setTransportOptions (truncated, false, ++jumps);
        float m = 0.0f;
        for (int b = 0; b < (int) (1.5 * rate / 512); ++b, tick += 512 * tps)
        {
            renderer.process (state.get(), true, tick, 512, l.data(), r.data());
            if (b * 512 > rate)
                for (auto v : l)
                    m = std::max (m, std::abs (v));
        }
        return m;
    };
    CHECK (tail (true, true) > 1e-3f);   // chased: the note sounds on
    CHECK (tail (false, true) < 1e-5f);  // the option off: the jump released it
    CHECK (tail (true, false) < 1e-5f);  // a wrap (no user jump): released, not chased
}

TEST (rea167k_reset_plugins_on_transport_reaches_the_generators)
{
    // FL resets every plugin it registered (FUN_012b8620 → FUN_012b5210), generators too, when playback starts or the
    // position moves; nothing with the option off
    auto song = lynx::Song::makeDefault();
    song.playlist.clear();
    song.mode = lynx::PlayMode::pattern;
    REA167K_REQUIRE (song.channels.size() > 1);
    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    auto counting = std::make_shared<rea167k::CountingInstrument>();
    const_cast<lynx::RackState&> (*state).instruments[1] = counting; // (the test swaps one generator)
    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 256);
    std::vector<float> l (256), r (256);
    const auto tps = lynx::ticksPerSample (song.bpm, 48000.0);
    renderer.setTransportOptions (true, false);
    renderer.process (state.get(), true, 0.0, 256, l.data(), r.data());
    CHECK (counting->resets == 0);
    renderer.process (state.get(), false, 0.0, 256, l.data(), r.data());
    renderer.setTransportOptions (true, true);
    renderer.process (state.get(), true, 0.0, 256, l.data(), r.data()); // start
    CHECK (counting->resets == 1);
    renderer.process (state.get(), true, 256 * tps, 256, l.data(), r.data()); // plays on
    CHECK (counting->resets == 1);
    renderer.process (state.get(), true, 4.0 * lynx::ppq, 256, l.data(), r.data()); // jumped
    CHECK (counting->resets == 2);
}

TEST (rea167k_triangular_law_reaches_sampler_channels)
{
    // FL's render of FPC pads with the triangular law (k_gen_tri.flp): pan −0.5 keeps the left at the centre level and
    // halves the right (circular: 1.3066 / 0.5412); 3x Osc keeps its circular pan (LYNX's rack gains, unchanged)
    auto data = std::make_shared<lynx::SampleData>();
    data->left.assign (48000, 0.5f);
    auto level = [&] (bool triangular, float pan, float& left, float& right) {
        auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
        REA167K_REQUIRE (sampler != nullptr);
        sampler->prepare (48000.0);
        sampler->setPanTriangular (triangular);
        sampler->noteOn (60, 1.0f);
        // the rack's circular gains for the channel pan (RackRenderer panGains, unity volume)
        const auto angle = (pan + 1.0f) * 0.25f * 3.14159265f;
        std::vector<float> l (4096), r (4096);
        sampler->render (l.data(), r.data(), 4096, std::cos (angle) * 1.41421356f, std::sin (angle) * 1.41421356f);
        left = std::abs (l[2048]);
        right = std::abs (r[2048]);
    };
    float l0 = 0, r0 = 0, l = 0, r = 0;
    level (true, 0.0f, l0, r0);
    REA167K_REQUIRE (l0 > 1e-4f);
    level (true, -0.5f, l, r);
    CHECK (std::abs (l / l0 - 1.0f) < 1e-3f && std::abs (r / r0 - 0.5f) < 1e-3f);
    level (true, 0.75f, l, r);
    CHECK (std::abs (l / l0 - 0.25f) < 1e-3f && std::abs (r / r0 - 1.0f) < 1e-3f);
    level (false, -0.5f, l, r);
    CHECK (std::abs (l / l0 - 1.3066f) < 2e-3f && std::abs (r / r0 - 0.5412f) < 2e-3f);
}

TEST (rea167k_undo_backwards_key)
{
    // FL's Edit › "Undo backwards" Ctrl+Alt+Z (EditUndoBackwardsAction, shortcut 49242); Ctrl+Z stays "Undo"
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ 'Z', ctrl | alt }, Context::global, false) == "edit.undoBackwards");
    CHECK (map.find ({ 'Z', ctrl }, Context::global, false) == "edit.undo");
}

namespace rea167k
{
// one audio channel with a 2 s ramp (frame i = i / 96000) at 48 kHz: 4 beats at 120 BPM; one clip of 4 beats
inline lynx::Song rampSong()
{
    lynx::Song s;
    s.bpm = 120.0;
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::audioClip;
    c.samplePath = "ramp";
    c.volume = 1.0f;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::audio;
    clip.channel = 0;
    clip.length = 4 * lynx::ppq;
    s.playlist.push_back (clip);
    s.mode = lynx::PlayMode::song;
    return s;
}

// the left output at `beats` (from the start, 256-sample blocks)
inline float rampLevelAt (const lynx::Song& s, double beats)
{
    auto provider = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->sampleRate = 48000.0;
        d->left.resize (96000);
        for (std::size_t i = 0; i < d->left.size(); ++i)
            d->left[i] = (float) i / 96000.0f;
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, provider);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    std::vector<float> l (256), rr (256);
    const auto tps = lynx::ticksPerSample (120.0, 48000.0);
    const auto blocks = (int) (beats * 24000.0 / 256.0);
    for (int b = 0; b <= blocks; ++b)
        r.process (state.get(), true, (double) b * 256.0 * tps, 256, l.data(), rr.data());
    return std::abs (l[255]);
}
} // namespace rea167k

TEST (rea167k_clip_pitch_and_reverse_keys)
{
    // FL 2026's keys 7 / 8 / 9 (FUN_00e2d7b0, FUN_00e2db70 → FUN_01065520): the selected audio clips only
    using namespace lynx;
    auto s = rea167k::rampSong();
    Clip pattern;
    pattern.length = ppq;
    s.playlist.push_back (pattern);
    CHECK (clipops::pitchAudioClips (s, { 0, 1 }, 1));
    CHECK (s.playlist[0].pitch == 1.0f && s.playlist[1].pitch == 0.0f);
    for (int i = 0; i < 20; ++i)
        clipops::pitchAudioClips (s, { 0 }, 1);
    CHECK (s.playlist[0].pitch == 12.0f); // clamped at +12
    for (int i = 0; i < 30; ++i)
        clipops::pitchAudioClips (s, { 0 }, -1);
    CHECK (s.playlist[0].pitch == -12.0f);
    CHECK (! clipops::pitchAudioClips (s, { 1 }, 1)); // a pattern clip: nothing
    CHECK (! clipops::pitchAudioClips (s, {}, 1));

    // reverse: the shown part of the file mirrors (start' = file − min(end, file))
    s.playlist[0].pitch = 0.0f;
    s.playlist[0].offset = ppq;
    s.playlist[0].length = 2 * ppq;
    CHECK (clipops::reverseAudioClips (s, { 0, 1 }, [] (int) { return (std::int64_t) (4 * ppq); }));
    CHECK (s.playlist[0].reversed && s.playlist[0].offset == ppq && ! s.playlist[1].reversed);
    s.playlist[0].offset = 0;
    s.playlist[0].length = 3 * ppq;
    clipops::reverseAudioClips (s, { 0 }, [] (int) { return (std::int64_t) (4 * ppq); });
    CHECK (! s.playlist[0].reversed && s.playlist[0].offset == ppq);
    // a clip longer than the file: start' clamps at 0
    s.playlist[0].offset = ppq;
    s.playlist[0].length = 8 * ppq;
    clipops::reverseAudioClips (s, { 0 }, [] (int) { return (std::int64_t) (4 * ppq); });
    CHECK (s.playlist[0].offset == 0);

    // the clip's own pitch keeps its length (FL render c_p3: 2 s at 522.5 Hz): the speed is the channel's
    const auto& ch = s.channels[0];
    Clip up;
    up.pitch = 12.0f;
    CHECK (clipops::clipSpeed (ch, up) == 1.0);
    auto pitched = ch;
    pitched.sampler.pitch = 12.0f;
    CHECK (std::abs (clipops::clipSpeed (pitched, up) - 2.0) < 1e-9); // (the channel's pitch knob still resamples)
    CHECK (clipops::playsBackwards (ch, Clip { .reversed = true }) && ! clipops::playsBackwards (ch, Clip {}));

    // the keys (Playlist focused)
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ '7', none }, Context::playlist, false) == "playlist.pitchClipsDown");
    CHECK (map.find ({ '8', none }, Context::playlist, false) == "playlist.pitchClipsUp");
    CHECK (map.find ({ '9', none }, Context::playlist, false) == "playlist.reverseClips");
}

TEST (rea167k_clip_pitch_and_reverse_play)
{
    using namespace lynx;
    auto s = rea167k::rampSong();
    const auto forward = rea167k::rampLevelAt (s, 1.0); // reads frame ~24000: 0.25
    REA167K_REQUIRE (forward > 0.0f);
    s.playlist[0].reversed = true; // reads 96000 − 24000: 0.75
    CHECK (std::abs (rea167k::rampLevelAt (s, 1.0) / forward - 3.0f) < 0.02f);
    // a reversed channel and a reversed clip play forwards
    s.playlist[0].pitch = 0.0f;
    s.playlist[0].reversed = true;
    s.channels[0].sampler.reverse = true;
    CHECK (std::abs (rea167k::rampLevelAt (s, 1.0) / forward - 1.0f) < 0.02f);
}

TEST (rea167k_flp_clip_pitch_and_reverse_import)
{
    // the playlist item's +0x3C (pitch, semitones) and +0x48 bit 0 (reversed) in 80-byte records (25.2.5, 0xA9 = 7)
    FlpBuilder b (96);
    b.ascii (0xC7, "25.2.5.5319");
    b.u8 (0xA9); b.u32 (7);
    b.u8 (0x9C); b.u32 (120000);
    b.u8 (0x40); b.u16 (0);
    b.u8 (0x15); b.u8 (4);
    b.utf16 (0xC4, u"C:\\samples\\ramp.wav");
    std::vector<std::uint8_t> items (160, 0);
    for (int k = 0; k < 2; ++k)
    {
        auto* r = items.data() + 80 * k;
        std::uint32_t pos = (std::uint32_t) (k * 384), len = 384;
        std::uint16_t base = 20480, idx = 0, rv = 499;
        const float gain = 1.0f, pitch = k == 0 ? 3.0f : -5.0f;
        const std::uint32_t flags = k == 0 ? 1u : 0u;
        const double stretch = 1.0;
        std::memcpy (r + 0, &pos, 4);
        std::memcpy (r + 4, &base, 2);
        std::memcpy (r + 6, &idx, 2);
        std::memcpy (r + 8, &len, 4);
        std::memcpy (r + 12, &rv, 2);
        std::memcpy (r + 0x34, &gain, 4);
        std::memcpy (r + 0x3C, &pitch, 4);
        std::memcpy (r + 0x40, &stretch, 8);
        std::memcpy (r + 0x48, &flags, 4);
    }
    b.data (0xE9, items);
    const auto p = flp::parseProject (b.bytes);
    REA167K_REQUIRE (p.arrangements.size() == 1 && p.arrangements[0].items.size() == 2);
    const auto& a = p.arrangements[0].items[0];
    const auto& c = p.arrangements[0].items[1];
    CHECK (a.kind == flp::PlaylistItem::Kind::channelClip && a.pitch == 3.0f && a.reversed);
    CHECK (c.pitch == -5.0f && ! c.reversed);

    lynx::FlpImportOptions o;
    o.fileExists = [] (const std::string&) { return true; };
    o.sampleSeconds = [] (const std::string&) { return 2.0; };
    const auto song = lynx::importFlp (p, o);
    int audio = 0;
    for (const auto& clip : song.playlist)
        if (clip.kind == lynx::ClipKind::audio)
        {
            CHECK ((clip.start == 0 && clip.pitch == 3.0f && clip.reversed) || (clip.start > 0 && clip.pitch == -5.0f && ! clip.reversed));
            ++audio;
        }
    CHECK (audio == 2);
}

TEST (rea167k_time_marker_keys)
{
    using namespace lynx;
    // the keys: FL's ShortCut values 32852 / 16468 / 41044 / 8276 (Alt+T, Ctrl+T, Shift+Alt+T, Shift+T), Playlist focused
    {
        using namespace lynx::keys;
        KeyMap map;
        CHECK (map.conflicts().empty());
        CHECK (map.find ({ 'T', alt }, Context::playlist, false) == "playlist.markerAdd");
        CHECK (map.find ({ 'T', ctrl }, Context::playlist, false) == "playlist.markerAuto");
        CHECK (map.find ({ 'T', shift | alt }, Context::playlist, false) == "playlist.markerSignature");
        CHECK (map.find ({ 'T', shift }, Context::playlist, false) == "playlist.markerLoop");
        CHECK (map.find ({ 'T', ctrl }, Context::global, false) == "view.typingPiano"); // elsewhere: the typing piano
    }

    // Ctrl+T (FUN_00e546a0): "Auto", "Auto #2"...; a marker already there is kept
    Song s;
    s.markers().clear();
    CHECK (markers::addAuto (s, 0) == 0 && s.markers()[0].name == "Auto");
    markers::addAuto (s, ppq);
    markers::addAuto (s, 2 * ppq);
    REA167K_REQUIRE (s.markers().size() == 3);
    CHECK (s.markers()[1].name == "Auto #2" && s.markers()[2].name == "Auto #3");
    CHECK (markers::addAuto (s, ppq) == 1 && s.markers().size() == 3);
    CHECK (s.markers()[1].action == Marker::none && ! s.markers()[1].isSignature());

    // Shift+T (FUN_0122bb80): one song Loop marker; the same place again takes it away
    s.markers().clear();
    markers::placeSongLoop (s, 4 * ppq);
    REA167K_REQUIRE (s.markers().size() == 1);
    CHECK (s.markers()[0].action == Marker::songLoop && s.markers()[0].tick == 4 * ppq);
    markers::placeSongLoop (s, 8 * ppq);
    CHECK (s.markers().size() == 1 && s.markers()[0].tick == 8 * ppq);
    markers::placeSongLoop (s, 8 * ppq);
    CHECK (s.markers().empty());

    // Shift+Alt+T (FUN_0122bd20): at the selection's start (on a beat), the old signature back at its end
    s.markers().clear();
    s.playlist.clear();
    Clip c;
    c.length = 64 * ppq;
    s.playlist.push_back (c);
    markers::addSignatureChange (s, 16 * ppq + 5, 24 * ppq, 3, 4);
    REA167K_REQUIRE (s.markers().size() == 2);
    CHECK (s.markers()[0].tick == 16 * ppq && s.markers()[0].numerator == 3 && s.markers()[0].denominator == 4 && s.markers()[0].name == "3/4");
    CHECK (s.markers()[1].tick == 24 * ppq && s.markers()[1].numerator == 4 && s.markers()[1].denominator == 4);
    CHECK (markers::signatureAt (s, 20 * ppq) == std::make_pair (3, 4) && markers::signatureAt (s, 30 * ppq) == std::make_pair (4, 4));
    // no selection: only the change
    s.markers().clear();
    markers::addSignatureChange (s, 8 * ppq, -1, 7, 8);
    CHECK (s.markers().size() == 1 && s.markers()[0].numerator == 7 && s.markers()[0].denominator == 8);
}

TEST (rea167k_mixer_menu_keys)
{
    // FL's mixer track menu (EEMixer DFM ShortCut): F2 rename and color, S solo selected, Alt+R / Shift+Alt+R disk
    // recording renders; the Mixer focused (F2 elsewhere stays the pattern's rename)
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ f2, none }, Context::mixer, false) == "mixer.rename");
    CHECK (map.find ({ f2, none }, Context::global, false) == "pattern.rename");
    CHECK (map.find ({ 'S', none }, Context::mixer, false) == "mixer.soloSelected");
    CHECK (map.find ({ 'R', alt }, Context::mixer, false) == "mixer.renderArmed");
    CHECK (map.find ({ 'R', shift | alt }, Context::mixer, false) == "mixer.renderSelected");
}

TEST (rea167k_select_time_keys)
{
    // FL's Playlist › Select › time: Ctrl+Enter around the selection, Ctrl+Left / Ctrl+Right the previous / next time
    // span of the same length (EESelAroundSelAction / EESelPrevAction / EESelNextAction)
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ enter, ctrl }, Context::playlist, false) == "playlist.selectTimeAround");
    CHECK (map.find ({ left, ctrl }, Context::playlist, false) == "playlist.timePrevious");
    CHECK (map.find ({ right, ctrl }, Context::playlist, false) == "playlist.timeNext");
}

namespace rea167k
{
// a 2 s 440 Hz sine on an audio clip channel at 48 kHz (4 beats at 120 BPM), one clip of 6 beats: the rendered left
// channel over 3 s
inline std::vector<float> renderSine (const lynx::Song& s)
{
    auto provider = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->sampleRate = 48000.0;
        d->left.resize (96000);
        for (std::size_t i = 0; i < d->left.size(); ++i)
            d->left[i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * (double) i / 48000.0);
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, provider);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    std::vector<float> l (256), rr (256), out;
    const auto tps = lynx::ticksPerSample (120.0, 48000.0);
    for (int b = 0; b < 3 * 48000 / 256; ++b)
    {
        r.process (state.get(), true, (double) b * 256.0 * tps, 256, l.data(), rr.data());
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}

inline double frequencyOf (const std::vector<float>& x, double from, double to)
{
    std::vector<std::size_t> up;
    for (auto i = (std::size_t) (from * 48000.0); i + 1 < (std::size_t) (to * 48000.0) && i + 1 < x.size(); ++i)
        if (x[i] < 0.0f && x[i + 1] >= 0.0f)
            up.push_back (i);
    return up.size() > 2 ? (double) (up.size() - 1) / ((double) (up.back() - up.front()) / 48000.0) : 0.0;
}

inline float peakOf (const std::vector<float>& x, double from, double to)
{
    float m = 0.0f;
    for (auto i = (std::size_t) (from * 48000.0); i < (std::size_t) (to * 48000.0) && i < x.size(); ++i)
        m = std::max (m, std::abs (x[i]));
    return m;
}
} // namespace rea167k

TEST (rea167k_clip_pitch_keeps_the_length)
{
    // FL 2026 renders (r167k/fl/clip: a 2 s 440 Hz file, channel in Resample): clip pitch +3 → 522.5 Hz, −5 → 329.2 Hz,
    // and the audio still ends at 2 s
    using namespace lynx;
    auto s = rea167k::rampSong();
    s.playlist[0].length = 6 * ppq;
    const auto base = rea167k::renderSine (s);
    CHECK (std::abs (rea167k::frequencyOf (base, 0.3, 0.8) - 440.0) < 1.0);
    for (auto [semitones, hz] : { std::pair { 3.0f, 523.25 }, { -5.0f, 329.63 } })
    {
        s.playlist[0].pitch = semitones;
        const auto x = rea167k::renderSine (s);
        CHECK (std::abs (rea167k::frequencyOf (x, 0.3, 0.8) - hz) < 2.0);
        CHECK (rea167k::peakOf (x, 1.80, 1.95) > 0.2f);    // still playing near the end of the file
        CHECK (rea167k::peakOf (x, 2.15, 2.9) < 0.05f);    // and not after it
    }
}

TEST (rea167k_audio_clip_channels_keep_precomputed_effects)
{
    // FL renders r167k/fl/clip/p_*.flp: an audio clip channel's Reverse (0x46 bit 1), Fade in / out (0x4C / 0x4B),
    // Normalize and Reverse polarity play in FL; LYNX dropped them for audio clip channels
    FlpBuilder b (96);
    b.ascii (0xC7, "25.2.5.5319");
    b.u8 (0x40); b.u16 (0);
    b.u8 (0x15); b.u8 (4);
    b.utf16 (0xC4, u"C:\\samples\\ramp.wav");
    b.u8 (0x46); b.u16 (2);
    b.u8 (0x4C); b.u16 (256);
    b.u8 (0x4B); b.u16 (128);
    std::vector<std::uint8_t> params (168, 0);
    params[0x50] = 1;
    params[0x51] = 1;
    b.data (0xD7, params);
    lynx::FlpImportOptions o;
    o.fileExists = [] (const std::string&) { return true; };
    o.sampleSeconds = [] (const std::string&) { return 2.0; };
    const auto song = lynx::importFlp (flp::parseProject (b.bytes), o);
    REA167K_REQUIRE (! song.channels.empty() && song.channels[0].instrument == lynx::InstrumentKind::audioClip);
    const auto& s = song.channels[0].sampler;
    CHECK (s.reverse && s.precompute.fadeIn == 256 && s.precompute.fadeOut == 128 && s.precompute.normalize && s.precompute.reversePolarity);

    // and they play: the ramp normalized (0.25 → 0.25 / max) with its polarity reversed
    auto ramp = rea167k::rampSong();
    const auto plain = rea167k::rampLevelAt (ramp, 1.0);
    ramp.channels[0].sampler.precompute.normalize = true;
    CHECK (std::abs (rea167k::rampLevelAt (ramp, 1.0) / plain - 96000.0f / 95999.0f) < 0.01f);
    ramp.channels[0].sampler.precompute.normalize = false;
    ramp.channels[0].sampler.precompute.fadeIn = 256; // a ramp over the whole file: much quieter at beat 1
    CHECK (rea167k::rampLevelAt (ramp, 1.0) < 0.5f * plain);
}

// 167h leftover "declicker attacks": FL's sampler voices start their declicker without the attack (FUN_00978070 is
// called with 0 by every voice start, FUN_00d277d0 / FUN_00d27cf0), so a note starts at full level in every mode; at
// the sample's end the held last frame fades with the mode's release: 10 / 10 (follower) / 20 / 20 / 100 / 200 ms
// [FL render r167k/fl/dk: ends 438 / 369 / 877 / 877 / 4 388 / 8 782 frames after the sample]
TEST (rea167k_sample_end_uses_the_declicking_release)
{
    // FL's native sampler voice, as an FL project's Sampler channel gets it (the default articulator records, Mod X
    // 256, 96 PPQ)
    lynx::ChannelDef flDef;
    flDef.instrument = lynx::InstrumentKind::sampler;
    {
        const auto defaults = lynx::flvp::defaultSnapshot();
        flDef.sampler.flChannel.assign (90, 0);
        for (int k = 0; k < 5; ++k)
            std::memcpy (flDef.sampler.flChannel.data() + 17 * k, defaults.data() + 0x44 * k, 68);
        flDef.sampler.flChannel[85] = 256;
        flDef.sampler.flChannel[88] = 96;
    }
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 44100.0;
    data->left.assign (13230, 0.5f);
    auto run = [&] (int mode) {
        auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
        sampler->prepare (44100.0);
        auto def = flDef;
        def.sampler.declickMode = mode;
        sampler->configure (def, 120.0);
        sampler->noteOn (60, 1.0f);
        std::vector<float> l (22050), r (22050);
        sampler->render (l.data(), r.data(), 22050, 1.0f, 1.0f);
        return l;
    };
    auto tail = [] (const std::vector<float>& l) {
        int last = 0;
        for (int i = 0; i < (int) l.size(); ++i)
            if (std::fabs (l[(std::size_t) i]) > 1e-5f / 0.1948f * std::fabs (l[100]))
                last = i;
        return last - 13230;
    };
    for (int mode = 0; mode <= 5; ++mode)
    {
        const auto l = run (mode);
        CHECK (l[0] > 0.0f && std::abs (l[0] - l[100]) < 1e-6f); // no attack ramp
    }
    CHECK (std::abs (tail (run (0)) - 438) <= 12);
    CHECK (std::abs (tail (run (2)) - 877) <= 12);
    CHECK (std::abs (tail (run (3)) - 877) <= 12);
    CHECK (std::abs (tail (run (4)) - 4388) <= 30);
    CHECK (std::abs (tail (run (5)) - 8782) <= 60);
    CHECK (tail (run (1)) < 441);
    const auto five = run (5);
    CHECK (std::abs (five[13230 + 4410] / five[100] - 0.710f) < 0.02f);
}
