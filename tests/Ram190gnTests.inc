// --- Round 190gn: RAM (A1 the keep-pitch samplers' shifters from a stock, A6 the renderer's buffers by the song) ------

namespace r190gn
{
// a stereo test sample: two partials and a little noise, `seconds` long
std::shared_ptr<lynx::SampleData> tone (double seconds, double rate = 44100.0)
{
    auto s = std::make_shared<lynx::SampleData>();
    s->sampleRate = rate;
    std::uint32_t noise = 190u;
    const auto n = (std::size_t) (seconds * rate);
    for (std::size_t i = 0; i < n; ++i)
    {
        noise = noise * 1664525u + 1013904223u;
        const auto t = (double) i / rate;
        const auto e = std::exp (-2.0 * t);
        s->left.push_back ((float) (e * (0.4 * std::sin (2.0 * 3.141592653589793 * 220.0 * t) + 0.1 * std::sin (2.0 * 3.141592653589793 * 1250.0 * t)))
                           + 0.01f * ((float) (noise >> 8) / 16777216.0f - 0.5f));
        s->right.push_back ((float) (e * 0.35 * std::sin (2.0 * 3.141592653589793 * 330.0 * t)));
    }
    return s;
}

// a song of `channels` keep-pitch samplers (Stretch, 4 beats) playing `keys` (a chord per bar, notes of `length` ticks)
// on pattern 0, one pattern clip of `bars` bars; channel c's notes are moved up by c semitones
lynx::Song song (int channels, const std::vector<int>& keys, std::int64_t length, int bars, lynx::StretchMethod method = lynx::StretchMethod::general)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (4);
    s.bpm = 128.0;
    for (int c = 0; c < channels; ++c)
    {
        ChannelDef ch;
        ch.instrument = InstrumentKind::sampler;
        ch.samplePath = "tone.wav";
        ch.sampler.keepPitch = true;
        ch.sampler.stretchBeats = 4.0f;
        ch.sampler.stretchMethod = method;
        ch.mixerTrack = 1 + c % 4;
        s.channels.push_back (ch);
    }
    Pattern p;
    p.lengthTicks = (std::int64_t) ppq * 4;
    for (int c = 0; c < channels; ++c)
        for (const auto k : keys)
        {
            Note n;
            n.start = 0;
            n.length = length;
            n.channel = c;
            n.key = k + c;
            p.notes.push_back (n);
        }
    s.patterns.push_back (p);
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = 0;
    clip.start = 0;
    clip.length = (std::int64_t) ppq * 4 * bars;
    s.playlist.push_back (clip);
    s.mode = PlayMode::song;
    return s;
}

lynx::RackState::Factories factories (std::shared_ptr<lynx::SampleData> sample)
{
    lynx::RackState::Factories f;
    f.samples = [sample] (const std::string&) { return std::shared_ptr<const lynx::SampleData> (sample); };
    f.stretchedSamples = [sample] (const std::string&, double ratio, lynx::StretchMethod m, double, const lynx::StretchMap&) {
        return std::shared_ptr<const lynx::SampleData> (lynx::timeStretch (*sample, ratio, m));
    };
    return f;
}

// the song rendered from its start for `seconds` (blocks of 256), the left then right samples
std::vector<float> render (const lynx::RackState& state, double seconds)
{
    lynx::RackRenderer r;
    r.prepare (44100.0, 256);
    std::vector<float> out, l (256), rr (256);
    const auto tps = lynx::ticksPerSample (state.song.bpm, 44100.0);
    double tick = 0.0;
    for (int b = 0; b < (int) (seconds * 44100.0 / 256.0); ++b)
    {
        r.process (&state, true, tick, 256, l.data(), rr.data(), nullptr, nullptr);
        out.insert (out.end(), l.begin(), l.end());
        out.insert (out.end(), rr.begin(), rr.end());
        tick += 256.0 * tps;
    }
    return out;
}

int held (const lynx::RackState& state)
{
    int n = 0;
    for (const auto& i : state.instruments)
        n += i != nullptr ? i->pitchShiftersHeld() : 0;
    return n;
}
} // namespace r190gn

// A1: a voice whose shifter comes from the stock sounds bit for bit like one whose shifter was made with the rack (the
// same song rendered with the shifters planned by the song and with all 8 made up front, as before)
TEST (ram190gn_planned_shifters_sound_the_same_as_eight_up_front)
{
    using namespace lynx;
    const auto sample = r190gn::tone (1.5);
    for (const auto method : { StretchMethod::general, StretchMethod::transients })
    {
        // a three-note chord, transposed (every voice shifted), on two channels, notes shorter than the sample
        const auto s = r190gn::song (2, { 60 + 3, 60 + 7, 60 - 5 }, (std::int64_t) ppq / 2, 3, method);
        shifterstock::setPlanned (false);
        const auto before = RackState::build (s, nullptr, 44100.0, r190gn::factories (sample));
        shifterstock::setPlanned (true);
        const auto after = RackState::build (s, nullptr, 44100.0, r190gn::factories (sample));
        CHECK (r190gn::held (*before) == 16);
        // the analysis: a 2 s copy (4 beats at 128 BPM ≈ 1.9 s) per bar of 1.9 s: the chords overlap their neighbours
        const auto planned = r190gn::held (*after);
        CHECK (planned >= 6 && planned < 16);
        const auto a = r190gn::render (*before, 7.0), b = r190gn::render (*after, 7.0);
        CHECK (a.size() == b.size());
        CHECK (std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
        float peak = 0.0f;
        for (const auto x : a)
            peak = std::max (peak, std::abs (x));
        CHECK (peak > 0.01f);
        CHECK (after->shifterStock != nullptr && after->shifterStock->missed() == 0);
    }
}

// A1: a voice past the planned shifters (a live note) takes a fresh one from the stock and keeps it; the result is the
// same as with its own made at build time, and the stock is refilled off the audio thread
TEST (ram190gn_a_live_note_takes_a_stock_shifter)
{
    using namespace lynx;
    const auto sample = r190gn::tone (1.0);
    auto s = r190gn::song (1, { 64 }, (std::int64_t) ppq / 4, 1);
    s.patterns[0].notes.clear(); // nothing in the song: no shifter planned
    const auto play = [&] (bool plannedNow) {
        shifterstock::setPlanned (plannedNow);
        auto state = RackState::build (s, nullptr, 44100.0, r190gn::factories (sample));
        shifterstock::setPlanned (true);
        const auto before = r190gn::held (*state);
        RackRenderer r;
        r.prepare (44100.0, 256);
        LiveEventQueue live;
        for (const auto key : { 67, 71, 55 }) // a chord, transposed
        {
            NoteEvent e;
            e.channel = 0;
            e.key = key;
            e.velocity = 0.8f;
            live.push (e);
        }
        std::vector<float> out, l (256), rr (256);
        for (int b = 0; b < 200; ++b)
        {
            r.process (state.get(), false, 0.0, 256, l.data(), rr.data(), &live, nullptr);
            out.insert (out.end(), l.begin(), l.end());
            out.insert (out.end(), rr.begin(), rr.end());
        }
        return std::make_tuple (out, before, r190gn::held (*state), state->shifterStock);
    };
    const auto [eight, eightBefore, eightAfter, eightStock] = play (false);
    const auto [stock, stockBefore, stockAfter, theStock] = play (true);
    CHECK (eightBefore == 8 && eightAfter == 8);
    CHECK (stockBefore == 0 && stockAfter == 3);
    CHECK (eight.size() == stock.size() && std::memcmp (eight.data(), stock.data(), eight.size() * sizeof (float)) == 0);
    CHECK (theStock != nullptr);
    if (theStock != nullptr)
    {
        CHECK (theStock->missed() == 0 && theStock->handedOut() == 3);
        CHECK (theStock->ready (0) == ShifterStock::liveReserve - 3);
        shifterstock::topUpAll(); // (the engine's timer)
        CHECK (theStock->ready (0) == ShifterStock::liveReserve);
    }
}

// A1: the song analysis — a channel playing one short note per bar holds one shifter; a sample that loops keeps all 8;
// a Layer's notes count for its children; a channel without notes holds none (the live reserve is in the stock)
TEST (ram190gn_song_analysis_bounds_the_voices)
{
    using namespace lynx;
    const auto shortSample = r190gn::tone (0.2);
    // 4 beats of 0.2 s at 128 BPM: the stretched copy lasts ~1.9 s, the notes are a bar (1.9 s) apart: at most 2 at once
    {
        const auto s = r190gn::song (1, { 62 }, (std::int64_t) ppq / 4, 4);
        const auto state = RackState::build (s, nullptr, 44100.0, r190gn::factories (shortSample));
        CHECK (r190gn::held (*state) >= 1 && r190gn::held (*state) <= 2);
    }
    {
        auto s = r190gn::song (1, { 62 }, (std::int64_t) ppq / 4, 4);
        s.patterns[0].notes.clear();
        const auto state = RackState::build (s, nullptr, 44100.0, r190gn::factories (shortSample));
        CHECK (r190gn::held (*state) == 0);
        CHECK (state->shifterStock != nullptr && state->shifterStock->ready (0) == ShifterStock::liveReserve);
    }
    {
        // the file's own loop (played while the note lasts; keep pitch without a tempo stretch keeps the file as it
        // is): no end known, 8
        auto looped = r190gn::tone (0.2);
        looped->loopStart = 100, looped->loopEnd = 4000;
        auto s = r190gn::song (1, { 62 }, (std::int64_t) ppq / 4, 4);
        s.channels[0].sampler.useLoopPoints = true;
        s.channels[0].sampler.stretchBeats = 0.0f;
        const auto state = RackState::build (s, nullptr, 44100.0, r190gn::factories (looped));
        CHECK (r190gn::held (*state) == 8);
    }
    {
        // a Layer plays the channel: its 6-note chords count
        auto s = r190gn::song (1, { 62 }, (std::int64_t) ppq / 4, 4);
        for (auto& n : s.patterns[0].notes)
            n.channel = 1;
        ChannelDef layer;
        layer.instrument = InstrumentKind::layer;
        layer.layer.children = { 0 };
        s.channels.push_back (layer);
        for (const auto k : { 64, 65, 67, 69, 71 })
        {
            Note n;
            n.channel = 1;
            n.key = k;
            n.length = (std::int64_t) ppq / 4;
            s.patterns[0].notes.push_back (n);
        }
        const auto state = RackState::build (s, nullptr, 44100.0, r190gn::factories (shortSample));
        CHECK (state->instruments[0] != nullptr && state->instruments[0]->pitchShiftersHeld() >= 6);
    }
}

// A6: the renderer's channel / insert buffers follow the songs it plays: the minimum for a small song, grown (off the
// audio thread, reserveFor) for a bigger one, never smaller than the state being played; a renderer without
// reserveFor grows them on the rendering thread
TEST (ram190gn_renderer_buffers_follow_the_song)
{
    using namespace lynx;
    const auto make = [] (int channels, int inserts) {
        Song s;
        s.mixer = Song::defaultMixer (inserts);
        for (int c = 0; c < channels; ++c)
        {
            ChannelDef ch;
            ch.instrument = InstrumentKind::synth;
            ch.mixerTrack = inserts > 0 ? 1 + c % inserts : 0;
            s.channels.push_back (ch);
        }
        Pattern p;
        p.lengthTicks = ppq * 4;
        for (int c = 0; c < channels; ++c)
        {
            Note n;
            n.channel = c;
            n.key = 48 + c % 24;
            n.length = ppq;
            p.notes.push_back (n);
        }
        s.patterns.push_back (p);
        s.mode = PlayMode::pattern;
        s.currentPattern = 0;
        return RackState::build (s, nullptr, 44100.0);
    };
    const auto small = make (4, 16);
    const auto big = make (150, 300);
    {
        RackRenderer r;
        r.prepare (44100.0, 512);
        r.reserveFor (*small);
        CHECK (r.bufferChannels() == RackRenderer::minBufferChannels && r.bufferTracks() == RackRenderer::minBufferTracks);
        r.reserveFor (*big);
        CHECK (r.bufferChannels() == 160 && r.bufferTracks() == 320);
        std::vector<float> l (512), rr (512);
        float peak = 0.0f;
        for (int b = 0; b < 20; ++b)
        {
            r.process (big.get(), true, b * 512.0 * ticksPerSample (big->song.bpm, 44100.0), 512, l.data(), rr.data());
            for (const auto x : l)
                peak = std::max (peak, std::abs (x));
        }
        CHECK (peak > 0.0f && std::isfinite (peak));
        r.reserveFor (*small); // (never smaller)
        CHECK (r.bufferChannels() == 160 && r.bufferTracks() == 320);
    }
    {
        // without reserveFor: the same audio, the buffers grown at the first block of the big song
        RackRenderer a, b;
        a.prepare (44100.0, 512);
        b.prepare (44100.0, 512);
        b.reserveFor (*big);
        std::vector<float> la (512), ra (512), lb (512), rb (512);
        auto same = true;
        for (int k = 0; k < 20; ++k)
        {
            const auto tick = k * 512.0 * ticksPerSample (big->song.bpm, 44100.0);
            a.process (big.get(), true, tick, 512, la.data(), ra.data());
            b.process (big.get(), true, tick, 512, lb.data(), rb.data());
            same = same && std::memcmp (la.data(), lb.data(), sizeof (float) * 512) == 0 && std::memcmp (ra.data(), rb.data(), sizeof (float) * 512) == 0;
        }
        CHECK (same);
        CHECK (a.bufferChannels() >= 150 && a.bufferTracks() >= 301);
    }
}
