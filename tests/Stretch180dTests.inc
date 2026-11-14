// --- Round 180d: FL's TIME stretches the region after its Trim, not the whole file (REA FUN_00d26890, FUN_00ff0db0) ----------

TEST (render180d_time_stretches_the_trimmed_region)
{
    using namespace lynx;
    // 1 s of file: 0.1 s of silence, 0.3 s of sound, 0.6 s of silence
    SampleData d;
    d.sampleRate = 1000.0;
    d.left.assign (1000, 0.0f);
    for (int i = 100; i < 400; ++i)
        d.left[(std::size_t) i] = 0.5f;
    SamplerSettings s;
    CHECK (precompute::stretchedFrames (d, s) == 1000);
    s.precompute.trim = 256; // 0.0078: the silence goes
    CHECK (precompute::stretchedFrames (d, s) == 300);
    s.precompute.trim = 0;
    s.regionEnd = 0.5f; // FL's Length: half the file
    CHECK (std::llabs (precompute::stretchedFrames (d, s) - 500) <= 1);
    s.precompute.trim = 256; // (the region's cut edges get FL's declick: two frames of sound fall under the trim)
    CHECK (std::llabs (precompute::stretchedFrames (d, s) - 300) <= 2);
    s.regionStart = 0.2f; // starts inside the sound
    s.regionEnd = 1.0f;
    CHECK (precompute::stretchedFrames (d, s) == 200);
}

TEST (render180d_a_trimmed_clip_plays_what_it_draws)
{
    using namespace lynx;
    // 1 s of file at 44.1 kHz: 0.1 s of silence, 0.3 s of a 440 Hz tone, 0.6 s of silence; Trim on
    constexpr double rate = 44100.0;
    auto sample = std::make_shared<SampleData>();
    sample->sampleRate = rate;
    sample->left.assign (44100, 0.0f);
    for (int i = 4410; i < 17640; ++i)
        sample->left[(std::size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 440.0 * i / rate);
    const std::shared_ptr<const SampleData> shared = sample;

    for (const auto stretched : { false, true })
    {
        Song s;
        s.bpm = 120.0;
        s.mode = PlayMode::song;
        s.mixer = Song::defaultMixer (1);
        ChannelDef c;
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = "x.wav";
        c.mixerTrack = 1;
        c.sampler.precompute.trim = 256;
        if (stretched) // TIME: 2 beats at 120 BPM
            c.sampler.stretchBeats = 2.0f, c.sampler.keepPitch = true;
        s.channels.push_back (c);
        s.patterns.emplace_back();

        // drawn: the played span (the tone), at the file's speed or spread over TIME
        const auto span = clipops::clipSpan (c, shared);
        CHECK (std::llabs (span.first - 4410) <= 2 && std::llabs (span.frames - 13230) <= 2);
        const auto drawnTicks = clipops::fileTicks (c, (double) span.frames, rate, s.bpm);
        CHECK (stretched ? drawnTicks == 2 * ppq : std::llabs (drawnTicks - (std::int64_t) std::llround (0.3 * 2.0 * ppq)) <= 1);
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = 0;
        clip.length = drawnTicks + ppq;
        s.playlist = { clip };

        RackState::Factories f;
        f.samples = [shared] (const std::string&) { return shared; };
        f.stretchedSamples = [shared] (const std::string&, double ratio, StretchMethod m, double, const StretchMap&, const StretchOptions&) {
            return std::shared_ptr<const SampleData> (timeStretch (*shared, ratio, m));
        };
        long spanIn = -1, spanOut = -1;
        f.stretchedSpans = [&spanIn, &spanOut] (const std::string&, const std::string&, const std::shared_ptr<const SampleData>& span, double ratio,
                                                StretchMethod m, double st, const StretchOptions&) {
            auto out = std::shared_ptr<const SampleData> (timeStretchSpan (*span, ratio, m, st));
            spanIn = (long) span->frames(), spanOut = out != nullptr ? (long) out->frames() : -1;
            return out;
        };
        auto state = RackState::build (s, nullptr, rate, f);
        RackRenderer r;
        r.prepare (rate, 441);
        std::vector<float> out, l (441), rr (441);
        const auto tps = ticksPerSample (s.bpm, rate);
        for (double tick = 0.0; tick + 441 * tps < (double) clip.length; tick += 441 * tps) // (not into the song's loop back to the start)
        {
            r.process (state.get(), true, tick, 441, l.data(), rr.data());
            out.insert (out.end(), l.begin(), l.end());
        }
        // played: the tone from the clip's start (the leading silence is trimmed), as long as drawn (5 ms RMS windows above
        // 0.1; the tone is 0.35). After a stretched span the clip is silent, as in FL (render p62): the span ends at zero.
        constexpr std::size_t window = 220;
        std::size_t firstLoud = out.size(), lastLoud = 0;
        for (std::size_t w = 0; w + window <= out.size(); w += window)
        {
            double sum = 0.0;
            for (std::size_t i = w; i < w + window; ++i)
                sum += (double) out[i] * out[i];
            if (std::sqrt (sum / window) > 0.1)
                firstLoud = std::min (firstLoud, w), lastLoud = w + window;
        }
        const auto playedTicks = (double) (lastLoud - firstLoud) / rate * s.bpm / 60.0 * ppq;
        std::fprintf (stderr, "  [r180d] %s: drawn %lld ticks, played %.0f ticks (first sound at %zu)\n", stretched ? "stretched" : "plain",
                      (long long) drawnTicks, playedTicks, firstLoud);
        std::fprintf (stderr, "  [r180d]   span %ld -> %ld frames; level at 0.5 / 0.9 / 1.1 / 1.4 s: %.3f %.3f %.3f %.3f\n", spanIn, spanOut,
                      out.size() > 22050 ? std::abs (out[22050]) : -1.0f, out.size() > 39690 ? std::abs (out[39690]) : -1.0f,
                      out.size() > 48510 ? std::abs (out[48510]) : -1.0f,
                      out.size() > 61740 ? std::abs (out[61740]) : -1.0f);
        // (a stretched span may ramp in over its first 5 ms window on some standard libraries: Ubuntu CI measured 220;
        // the exact onset is checked frame by frame in render180d_a_stretched_trimmed_clip_starts_on_time)
        CHECK (firstLoud <= (stretched ? window : 0));
        CHECK (std::abs (playedTicks - (double) drawnTicks) < 0.03 * (double) drawnTicks);
        if (stretched) // nothing held after the span (no DC to the clip's end)
        {
            CHECK (out.size() > 52920 && std::abs (out[52920]) < 1.0e-3f && std::abs (out[out.size() - 1]) < 1.0e-3f);
            std::fprintf (stderr, "  [r180d]   after the span: %.6f at 1.2 s, %.6f at the end\n", out.size() > 52920 ? out[52920] : -1.0f, out.back());
        }
    }
}

TEST (render180d_time_stretches_what_the_precomputed_effects_leave)
{
    // FL trims after its precomputed effects (FUN_00ff0db0): Thief's Importer Ride, faded out by Precomputed fade out 807,
    // trims to 0.38 s of its 1.30 s, and TIME stretches that (FL renders: 2x slower at TIME 367, round 180d)
    using namespace lynx;
    SampleData d;
    d.sampleRate = 1000.0;
    d.left.assign (1000, 0.5f);
    SamplerSettings s;
    s.precompute.trim = 256;
    CHECK (precompute::stretchedFrames (d, s) == 1000);
    s.precompute.fadeOut = 807;
    const auto processed = precompute::apply (d, s);
    CHECK (processed != nullptr);
    const auto faded = precompute::stretchedFrames (d, s);
    CHECK (processed != nullptr && faded == (std::int64_t) processed->frames());
    CHECK (faded < 1000);
    std::fprintf (stderr, "  [r180d] a constant second faded out by 807 trims to %lld frames\n", (long long) faded);
    auto shared = std::make_shared<const SampleData> (d);
    CHECK (precompute::stretchedFrames (shared, s) == faded);
    CHECK (precompute::stretchedFrames (shared, s) == faded); // the cached answer
    s.precompute.fadeOut = 0;
    CHECK (precompute::stretchedFrames (shared, s) == 1000); // another setting, another answer
}

TEST (render180d_a_stretched_trimmed_clip_starts_on_time)
{
    // Stretching the whole file and trimming after it kept the stretcher's smear of the silence before the tone: the tone
    // started 966 frames late at this ratio, 1848 with a ratio 0.01 % off (round 180d, bl16). Like FL, the trimmed span
    // alone is stretched now (RackState::Factories::stretchedSpans; unset here: stretched in place): the tone starts at once.
    using namespace lynx;
    constexpr double rate = 44100.0;
    auto sample = std::make_shared<SampleData>();
    sample->sampleRate = rate;
    sample->left.assign (44100, 0.0f);
    for (int i = 4410; i < 17640; ++i)
        sample->left[(std::size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 440.0 * i / rate);
    const std::shared_ptr<const SampleData> shared = sample;

    auto onset = [&] (float stretchBeats) {
        Song s;
        s.bpm = 120.0;
        s.mode = PlayMode::song;
        s.mixer = Song::defaultMixer (1);
        ChannelDef c;
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = "x.wav";
        c.mixerTrack = 1;
        c.sampler.precompute.trim = 256;
        c.sampler.stretchBeats = stretchBeats;
        c.sampler.keepPitch = true;
        s.channels.push_back (c);
        s.patterns.emplace_back();
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = 0;
        clip.length = 8 * ppq;
        s.playlist = { clip };
        RackState::Factories f;
        f.samples = [shared] (const std::string&) { return shared; };
        f.stretchedSamples = [shared] (const std::string&, double ratio, StretchMethod m, double, const StretchMap&, const StretchOptions&) {
            return std::shared_ptr<const SampleData> (timeStretch (*shared, ratio, m));
        };
        auto state = RackState::build (s, nullptr, rate, f);
        RackRenderer r;
        r.prepare (rate, 441);
        std::vector<float> l (441), rr (441);
        const auto tps = ticksPerSample (s.bpm, rate);
        long frame = 0;
        for (double tick = 0.0; tick < 4.0 * ppq; tick += 441 * tps, frame += 441)
        {
            r.process (state.get(), true, tick, 441, l.data(), rr.data());
            for (int i = 0; i < 441; ++i)
                if (std::abs (l[(std::size_t) i]) > 0.02f)
                    return frame + i;
        }
        return -1L;
    };
    const auto now = onset (2.0f);
    std::fprintf (stderr, "  [r180d] tone onset of a stretched trimmed clip: %ld frames\n", now);
    CHECK (now >= 0 && now < 441);
}
