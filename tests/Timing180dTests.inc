// --- Round 180d: notes start on their exact frame (180c: Fruity Slicer notes 1 frame late from 2.25 s on) -----------------

namespace r180dtiming
{
// Renders `notes` eighth notes of a one-frame click on `instrument` in song mode, in blocks of `block` frames with the
// transport's own tick accumulation (AudioEngine: position += n · ticksPerSample), and returns each note's first frame.
inline std::vector<long> onsets (lynx::InstrumentKind instrument, int notes, int block, double rate, double bpm)
{
    using namespace lynx;
    auto click = std::make_shared<SampleData>();
    click->sampleRate = rate;
    click->left.assign (4000, 0.0f);
    click->left[0] = 1.0f;
    Song s;
    s.bpm = bpm;
    s.mode = PlayMode::song;
    s.mixer = Song::defaultMixer (1);
    ChannelDef c;
    c.instrument = instrument;
    c.samplePath = "click.wav";
    c.mixerTrack = 1;
    s.channels.push_back (c);
    Pattern p;
    for (int i = 0; i < notes; ++i)
    {
        Note n;
        n.start = (std::int64_t) i * ppq / 2;
        n.length = ppq / 4;
        p.notes.push_back (n);
    }
    s.patterns.push_back (p);
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = 0;
    clip.length = (std::int64_t) notes * ppq / 2;
    s.playlist = { clip };
    RackState::Factories f;
    f.samples = [click] (const std::string&) { return std::shared_ptr<const SampleData> (click); };
    auto state = RackState::build (s, nullptr, rate, f);
    RackRenderer r;
    r.prepare (rate, block);
    std::vector<float> out, l ((std::size_t) block), rr ((std::size_t) block);
    const auto tps = ticksPerSample (bpm, rate);
    const auto total = (double) notes * ppq / 2 + ppq;
    for (double tick = 0.0; tick < total; tick += block * tps)
    {
        r.process (state.get(), true, tick, block, l.data(), rr.data());
        out.insert (out.end(), l.begin(), l.end());
    }
    std::vector<long> at;
    bool inside = false;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const auto loud = std::abs (out[i]) > 1.0e-3f;
        if (loud && ! inside)
            at.push_back ((long) i);
        inside = loud && (at.empty() || (long) i - at.back() < 50);
    }
    return at;
}
} // namespace r180dtiming

TEST (render180d_notes_start_on_their_exact_frame)
{
    using namespace lynx;
    // 120 BPM: an eighth note is 11025 frames at 44.1 kHz; 140 BPM at 48 kHz: 10285.714…, FL rounds each to its frame
    for (const auto& [rate, bpm] : { std::pair { 44100.0, 120.0 }, std::pair { 48000.0, 140.0 } })
        for (const auto block : { 512, 441, 64 })
            for (const auto kind : { InstrumentKind::sampler, InstrumentKind::audioClip })
            {
                const auto at = r180dtiming::onsets (kind, 64, block, rate, bpm);
                const auto eighth = rate * 60.0 / bpm / 2.0;
                int late = 0, first = -1;
                for (std::size_t i = 0; i < std::min<std::size_t> (at.size(), 64); ++i)
                    if (at[i] != (long) std::nearbyint ((double) i * eighth))
                        late += 1, first = first < 0 ? (int) i : first;
                if (late > 0 || at.size() < 64)
                    std::fprintf (stderr, "  [r180d] %s %.0f Hz %.0f BPM block %d: %zu onsets, %d off (first: note %d at %ld, exact %ld)\n",
                                  kind == InstrumentKind::sampler ? "sampler" : "audio clip", rate, bpm, block, at.size(), late, first,
                                  first >= 0 ? at[(std::size_t) first] : -1L, first >= 0 ? (long) std::nearbyint (first * eighth) : -1L);
                CHECK (at.size() >= 64 && late == 0); // (the render runs a beat past the last note)
            }
}
