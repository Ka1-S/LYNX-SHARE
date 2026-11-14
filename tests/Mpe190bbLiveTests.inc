// --- Round 190bb (issue #108): MPE stage 1 — a played note's expression reaches a built-in instrument's voice ------

namespace mpe190bb
{
// the frequency of x[from, to) from its positive-going zero crossings
inline double frequency (const std::vector<float>& x, std::size_t from, std::size_t to, double rate)
{
    long first = -1, last = -1;
    auto crossings = 0;
    for (auto i = from + 1; i < to && i < x.size(); ++i)
        if (x[i - 1] <= 0.0f && x[i] > 0.0f)
        {
            if (first < 0)
                first = (long) i;
            else
                ++crossings, last = (long) i;
        }
    return crossings > 0 ? crossings * rate / (double) (last - first) : 0.0;
}

// A sampler channel playing a sine: one live note, then (optionally) its pitch expression changing half-way.
// Returns the frequency heard in the first and in the second half.
inline std::pair<double, double> playLive (bool expression, float cents, float laterCents, bool sendLater, float fineTune = 0.0f)
{
    auto sample = s162::sine (440.0, 4.0);
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    s.bpm = 120.0;
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::sampler;
    c.samplePath = "sine.wav";
    c.misc.fineTune = fineTune;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    lynx::RackState::Factories f;
    f.samples = [sample] (const std::string&) { return std::shared_ptr<const lynx::SampleData> (sample); };
    f.stretchedSamples = [sample] (const std::string&, double ratio, lynx::StretchMethod m, double, const lynx::StretchMap&, const lynx::StretchOptions&) {
        return std::shared_ptr<const lynx::SampleData> (lynx::timeStretch (*sample, ratio, m));
    };
    constexpr double rate = 44100.0;
    auto state = lynx::RackState::build (s, nullptr, rate, f);
    lynx::RackRenderer r;
    r.prepare (rate, 441);
    lynx::LiveEventQueue live;
    lynx::NoteEvent e;
    e.channel = 0;
    e.key = 60;
    e.velocity = 0.8f;
    e.expression = expression;
    e.cents = cents;
    live.push (e);
    std::vector<float> out, l (441), rr (441);
    for (int b = 0; b < 100; ++b)
    {
        if (b == 50 && sendLater)
        {
            lynx::NoteEvent x;
            x.type = lynx::NoteEvent::Type::voiceProp;
            x.expression = true;
            x.channel = 0;
            x.key = 60;
            x.prop = (std::uint8_t) lynx::vfx::Prop::pitch;
            x.velocity = laterCents;
            live.push (x);
        }
        r.process (state.get(), false, 0.0, 441, l.data(), rr.data(), &live, nullptr);
        out.insert (out.end(), l.begin(), l.end());
    }
    return { frequency (out, 4410, 19845, rate), frequency (out, 26460, 41895, rate) };
}
} // namespace mpe190bb

TEST (mpe190bb_built_in_sampler_follows_a_played_notes_bend)
{
    const auto semitone = std::exp2 (1.0 / 12.0);
    const auto plain = mpe190bb::playLive (false, 0.0f, 0.0f, false);
    CHECK (plain.first > 100.0 && std::abs (plain.first - plain.second) < 0.5);
    // a live note that is not an MPE note ignores the event's cents, as before
    const auto ignored = mpe190bb::playLive (false, 100.0f, 0.0f, false);
    CHECK (std::abs (ignored.first - plain.first) < 0.5 && std::abs (ignored.second - plain.second) < 0.5);
    // an MPE note starts at its own bend
    const auto bent = mpe190bb::playLive (true, 100.0f, 0.0f, false);
    CHECK (std::abs (bent.first / plain.first - semitone) < 0.002 && std::abs (bent.second / plain.first - semitone) < 0.002);
    // … and follows it while it sounds: +100 cents, then −200
    const auto moved = mpe190bb::playLive (true, 100.0f, -200.0f, true);
    CHECK (std::abs (moved.first / plain.first - semitone) < 0.002);
    CHECK (std::abs (moved.second / plain.first - 1.0 / (semitone * semitone)) < 0.002);
    // the channel's fine tune stays under the note's bend
    const auto tuned = mpe190bb::playLive (true, 0.0f, 100.0f, true, 100.0f);
    CHECK (std::abs (tuned.first / plain.first - semitone) < 0.002 && std::abs (tuned.second / plain.first - semitone * semitone) < 0.002);
}
