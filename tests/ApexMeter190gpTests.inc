// Round 190gp (A4, the owner's decision): Apex Limiter's loudness / true-peak meter runs only while an editor shows the
// slot (ProcessTimes::metersWanted). The sound is the same either way, bit for bit; with no editor the loudness
// read-outs stay still and the gain reduction (the Mixer's meter) still moves; an editor opened counts its integrated
// loudness from then.

TEST (s190gp_apex_meter_only_while_shown)
{
    using namespace lynx;
    auto run = [] (bool shown, float* integrated, float* reduction) {
        Song song;
        song.bpm = 120.0;
        song.mixer = Song::defaultMixer (1);
        auto slot = makeEffectSlot (EffectKind::apexLimiter);
        slot.params[0] = 0.6f; // (some gain: it limits)
        song.mixer[1].effects.push_back (slot);
        auto data = std::make_shared<SampleData>();
        const int total = 44100 * 3;
        data->left.resize ((std::size_t) total);
        data->right.resize ((std::size_t) total);
        for (int i = 0; i < total; ++i)
            data->left[(std::size_t) i] = data->right[(std::size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * i / 44100.0);
        ChannelDef ch;
        ch.instrument = InstrumentKind::audioClip;
        ch.samplePath = "/apex/a.wav";
        ch.mixerTrack = 1;
        song.channels.push_back (ch);
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = 0;
        clip.length = (std::int64_t) (3.0 * 2.0 * ppq);
        song.playlist.push_back (clip);
        song.mode = PlayMode::song;
        auto state = RackState::build (song, nullptr, 44100.0, [&] (const std::string&) { return data; });
        RackRenderer renderer;
        renderer.prepare (44100.0, 2048);
        const auto key = (std::size_t) (1 * maxEffectSlots + 0);
        renderer.processTimes().metersWanted[key].store (shown ? 1 : 0);
        std::uint64_t h = 1469598103934665603ull;
        std::vector<float> l (256), r (256);
        const auto tps = ticksPerSample (song.bpm, 44100.0);
        for (int pos = 0; pos + 256 <= 44100 * 2; pos += 256)
        {
            renderer.process (state.get(), true, pos * tps, 256, l.data(), r.data());
            for (int i = 0; i < 256; ++i)
            {
                std::uint32_t bits;
                std::memcpy (&bits, &l[(std::size_t) i], 4);
                h = (h ^ bits) * 1099511628211ull;
            }
        }
        *integrated = renderer.processTimes().effectMeters[key * ProcessTimes::meterOutputs + 5].load();
        *reduction = renderer.processTimes().effectMeters[key * ProcessTimes::meterOutputs + 0].load();
        return h;
    };
    float shownLufs = 0.0f, hiddenLufs = 0.0f, shownGr = 0.0f, hiddenGr = 0.0f;
    const auto shown = run (true, &shownLufs, &shownGr);
    const auto hidden = run (false, &hiddenLufs, &hiddenGr);
    CHECK (shown == hidden);       // the same sound
    CHECK (shownGr > 0.0f);        // it limits ...
    CHECK (hiddenGr == shownGr);   // ... and the Mixer's meter moves either way
    std::printf ("  integrated %.2f LUFS with an editor, %.2f without; gain reduction %.3f\n", shownLufs, hiddenLufs, shownGr);
    CHECK (shownLufs != 0.0f && shownLufs > -40.0f && shownLufs < 10.0f); // the loudness meter counted while shown
    CHECK (hiddenLufs == 0.0f);    // not without an editor
}
