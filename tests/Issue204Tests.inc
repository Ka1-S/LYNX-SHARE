// Round 190lq (issue #204): an audio clip channel played from the Piano roll / steps, as FL 2026 plays it (FL64 /R renders of
// the same notes: /mnt/e/lynx-work/issue204_190lq, docs in the round's HANDOFF): the whole clip whatever the note's length,
// overlapping notes layer (the second note no longer ends where the first one did), and the key transposes the clip from
// C5. Projects saved before the law (SamplerSettings::clipNoteLegacy) keep their earlier sound: each note cut at its end,
// at the clip's own pitch — but only that note's voice.

namespace i204
{
constexpr double rate = 48000.0;

// a 1 s sine (0.25, 440 Hz)
std::shared_ptr<lynx::SampleData> sine()
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = rate;
    d->left.resize ((std::size_t) rate);
    for (std::size_t i = 0; i < d->left.size(); ++i)
        d->left[i] = 0.25f * (float) std::sin (6.283185307179586 * 440.0 * (double) i / rate);
    d->right = d->left;
    return d;
}

struct N
{
    double beat, beats;
    int key;
    float velocity = 100.0f / 128.0f;
};

// Song mode, 120 BPM (a beat = 0.5 s): channel 0 = an audio clip channel on the sine playing `notes` once
lynx::Song song (const std::vector<N>& notes, bool legacy, lynx::InstrumentKind kind = lynx::InstrumentKind::audioClip, bool flVoice = false)
{
    auto s = lynx::Song::makeDefault();
    s.bpm = 120.0;
    s.mode = lynx::PlayMode::song;
    lynx::ChannelDef c;
    c.instrument = kind;
    c.name = "Sine clip";
    c.samplePath = "sine";
    c.volume = lynx::audioClipUnityVolume;
    c.sampler.clipNoteLegacy = legacy;
    if (flVoice) // FL's native voice as the .flp import stores it (the plugins' default records, Mod X / Y, PPQ 96, pan law)
    {
        const auto defaults = lynx::flvp::defaultSnapshot();
        c.sampler.flChannel.assign (90, 0);
        std::memcpy (c.sampler.flChannel.data(), defaults.data(), 85 * 4);
        std::memcpy (&c.sampler.flChannel[85], defaults.data() + 0x15c, 4);
        std::memcpy (&c.sampler.flChannel[86], defaults.data() + 0x160, 4);
        std::memcpy (&c.sampler.flChannel[87], defaults.data() + 0x168, 4);
        c.sampler.flChannel[88] = 96;
    }
    s.channels = { c };
    lynx::Pattern p;
    p.name = "Notes";
    for (const auto& n : notes)
    {
        lynx::Note note;
        note.start = (std::int64_t) std::llround (n.beat * lynx::ppq);
        note.length = (std::int64_t) std::llround (n.beats * lynx::ppq);
        note.key = n.key;
        note.velocity = n.velocity;
        p.notes.push_back (note);
    }
    p.sortNotes();
    p.lengthTicks = 16 * lynx::ppq; // (8 s: the pattern does not loop inside the render)
    s.patterns = { p };
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::pattern;
    clip.pattern = 0;
    clip.length = 16 * lynx::ppq; // (longer than the render: the pattern does not loop inside it)
    s.playlist = { clip };
    return s;
}

std::vector<float> render (const lynx::Song& s, double seconds)
{
    const auto data = sine();
    auto state = lynx::RackState::build (s, nullptr, rate, [data] (const std::string&) -> std::shared_ptr<const lynx::SampleData> { return data; });
    lynx::RackRenderer rr;
    rr.prepare (rate, 480);
    const auto total = (int) (seconds * rate);
    std::vector<float> l ((std::size_t) total), r ((std::size_t) total);
    double tick = 0.0;
    const auto tps = lynx::ticksPerSample (s.bpm, rate);
    for (int at = 0; at < total; at += 480)
    {
        const auto n = std::min (480, total - at);
        rr.process (state.get(), true, tick, n, l.data() + at, r.data() + at);
        tick += n * tps;
    }
    return l;
}

// the last 10 ms window above -60 dBFS (seconds)
double soundsUntil (const std::vector<float>& x)
{
    std::size_t last = 0;
    for (std::size_t w = 0; w + 480 <= x.size(); w += 480)
    {
        float peak = 0.0f;
        for (std::size_t i = w; i < w + 480; ++i)
            peak = std::max (peak, std::abs (x[i]));
        if (peak > 0.001f)
            last = w + 480;
    }
    return (double) last / rate;
}

float peak (const std::vector<float>& x, double from, double to)
{
    float p = 0.0f;
    for (auto i = (std::size_t) (from * rate); i < std::min (x.size(), (std::size_t) (to * rate)); ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}

// frequency from the zero crossings between `from` and `to` seconds
double hz (const std::vector<float>& x, double from, double to)
{
    int crossings = 0;
    double first = -1.0, last = -1.0;
    for (auto i = (std::size_t) (from * rate) + 1; i < std::min (x.size(), (std::size_t) (to * rate)); ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
        {
            const auto t = (double) i / rate;
            if (first < 0.0)
                first = t;
            last = t;
            ++crossings;
        }
    return crossings > 2 ? (crossings - 1) / 2.0 / (last - first) : 0.0;
}
} // namespace i204

// #204 item 1: a note shorter than the clip plays the whole clip (FL: 1.0 s of a 1 s clip for a 1/16 step and for a 3 s note)
TEST (issue204_clip_note_plays_the_whole_clip)
{
    using namespace i204;
    const auto shortNote = render (song ({ { 0.0, 0.25, 60 } }, false), 2.5);
    const auto longNote = render (song ({ { 0.0, 6.0, 60 } }, false), 2.5);
    std::printf ("issue204: a 1/16 note sounds %.3f s, a 3 s note %.3f s (clip 1 s)\n", soundsUntil (shortNote), soundsUntil (longNote));
    CHECK (soundsUntil (shortNote) > 0.98 && soundsUntil (shortNote) < 1.05);
    CHECK (soundsUntil (longNote) > 0.98 && soundsUntil (longNote) < 1.05); // (no loop, no DC hold past the sample)
    CHECK (std::abs (hz (shortNote, 0.1, 0.9) - 440.0) < 1.0);
}

// #204 item 2: two overlapping notes of one key layer; the second one plays its whole clip (it ended where the first did)
TEST (issue204_overlapping_clip_notes_layer)
{
    using namespace i204;
    // n1 at 0 for 0.5 s, n2 at 0.25 s for 1 s: n1 sounds 0…1 s, n2 0.25…1.25 s (in phase: 110 cycles later)
    const auto x = render (song ({ { 0.0, 1.0, 60 }, { 0.5, 2.0, 60 } }, false), 2.5);
    std::printf ("issue204: overlap peaks %.3f / %.3f / %.3f, sounds until %.3f s\n", peak (x, 0.05, 0.2), peak (x, 0.6, 0.9),
                 peak (x, 1.05, 1.2), soundsUntil (x));
    const auto one = peak (x, 0.05, 0.2);
    CHECK (one > 0.05f);
    CHECK (std::abs (peak (x, 0.6, 0.9) - 2.0f * one) < 0.05f * one); // both voices
    CHECK (std::abs (peak (x, 1.05, 1.2) - one) < 0.03f * one);       // the second voice alone, past the first note's end
    CHECK (soundsUntil (x) > 1.23 && soundsUntil (x) < 1.30);

    // the legacy law (projects saved before): each note cut at its own end, and only its own voice — n1 0…0.5 s, n2
    // 0.25…0.75 s: 0.1.29 ended both at 0.5 s (its note-off stopped every voice of the channel)
    const auto old = render (song ({ { 0.0, 1.0, 60 }, { 0.5, 1.0, 60 } }, true), 2.5);
    std::printf ("issue204: legacy overlap: %.3f after the first note's end, sounds until %.3f s (second note ends at 0.75 s)\n",
                 peak (old, 0.55, 0.7), soundsUntil (old));
    CHECK (std::abs (peak (old, 0.55, 0.7) - peak (old, 0.05, 0.2)) < 0.03f * peak (old, 0.05, 0.2)); // the second voice alone
    CHECK (soundsUntil (old) > 0.74 && soundsUntil (old) < 0.80);
}

// #204 item 3: the key transposes the clip from C5, resampled (FL: A5 = 740 Hz for 0.595 s, C4 = 220 Hz for 2 s)
TEST (issue204_clip_note_key_transposes)
{
    using namespace i204;
    const auto a5 = render (song ({ { 0.0, 0.5, 69 } }, false), 2.5);
    const auto c6 = render (song ({ { 0.0, 0.5, 72 } }, false), 2.5);
    const auto c4 = render (song ({ { 0.0, 0.5, 48 } }, false), 2.5);
    std::printf ("issue204: A5 %.1f Hz %.3f s, C6 %.1f Hz %.3f s, C4 %.1f Hz %.3f s\n", hz (a5, 0.05, 0.5), soundsUntil (a5),
                 hz (c6, 0.05, 0.45), soundsUntil (c6), hz (c4, 0.1, 1.5), soundsUntil (c4));
    CHECK (std::abs (hz (a5, 0.05, 0.5) - 739.99) < 1.5);
    CHECK (std::abs (hz (c6, 0.05, 0.45) - 880.0) < 1.5);
    CHECK (std::abs (hz (c4, 0.1, 1.5) - 220.0) < 1.0);
    CHECK (soundsUntil (c6) > 0.49 && soundsUntil (c6) < 0.53);
    CHECK (soundsUntil (c4) > 1.98 && soundsUntil (c4) < 2.05);

    // the legacy law: every key at the clip's own pitch
    const auto old = render (song ({ { 0.0, 0.5, 69 } }, true), 2.5);
    CHECK (std::abs (hz (old, 0.05, 0.2) - 440.0) < 1.0);
}

// FL 2026's renders of the same notes on an audio clip channel and on a Sampler of the same file are bit-identical (round
// 190lq: lengths, overlaps, keys, velocities 50 / 100 / 128). LYNX: the same, with FL's voice (.flp imports) and without
TEST (issue204_clip_notes_play_as_the_sampler)
{
    using namespace i204;
    using lynx::InstrumentKind;
    const std::vector<N> notes { { 0.0, 0.25, 60 }, { 2.0, 1.0, 60 }, { 2.5, 2.0, 60 }, { 4.0, 1.0, 60 }, { 4.5, 2.0, 67 },
                                 { 6.0, 0.5, 69 }, { 7.0, 0.5, 48 }, { 8.0, 0.5, 60, 50.0f / 128.0f }, { 9.0, 0.5, 61, 1.0f } };
    for (const auto flVoice : { false, true })
    {
        const auto clip = render (song (notes, false, InstrumentKind::audioClip, flVoice), 6.0);
        const auto sampler = render (song (notes, false, InstrumentKind::sampler, flVoice), 6.0);
        float diff = 0.0f, level = 0.0f;
        for (std::size_t i = 0; i < clip.size(); ++i)
        {
            diff = std::max (diff, std::abs (clip[i] - sampler[i]));
            level = std::max (level, std::abs (sampler[i]));
        }
        std::printf ("issue204: audio clip channel vs Sampler (%s voice): max difference %.3g, peak %.3f\n", flVoice ? "FL's" : "LYNX's",
                     (double) diff, (double) level);
        CHECK (level > 0.05f);
        CHECK (diff < 1.0e-6f);
    }
    // FL's voice: velocity 50 / 100 / 128 = 0.023 / 0.097 / 0.200 of a 0.25 sine at the channel's 10000 (FL's renders)
    const auto fl = render (song ({ { 0.0, 0.5, 60, 50.0f / 128.0f }, { 2.0, 0.5, 60 }, { 4.0, 0.5, 60, 1.0f } }, false, InstrumentKind::audioClip, true), 2.5);
    std::printf ("issue204: FL voice velocity 50 / 100 / 128: %.4f / %.4f / %.4f\n", (double) peak (fl, 0.1, 0.4), (double) peak (fl, 1.1, 1.4),
                 (double) peak (fl, 2.1, 2.4));
}
