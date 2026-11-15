// Round 190gx: LYNX's Beats warp mode against Ableton Live 12.4's W1 renders (round 190gu; analysis in
// /mnt/e/lynx-work/livefacts190gx/warp/REPORT.md): Transient Envelope = an exponential decay after a piece's own end
// (Live: 0 → 0.560 dB/ms, 50 → 0.0886 dB/ms), Loop Off = a 10 ms fade ending at the piece's end, Back-and-forth and
// Forward turn 20 ms before a transient piece's end and stay in the piece's second half, a Preserve grid piece turns at
// its very end.

namespace
{
// one 330 Hz tone piece per 0.5 s after a 0.1 s lead, each starting loud and decaying (the 190fm signal), at 44.1 kHz
lynx::SampleData warp190gxPieces()
{
    constexpr double rate = 44100.0;
    constexpr long piece = 22050, lead = 4410;
    lynx::SampleData in;
    in.sampleRate = rate;
    in.left.assign ((std::size_t) (lead + 4 * piece), 0.0f);
    for (long k = 0; k < 4; ++k)
        for (long i = 0; i < piece; ++i)
            in.left[(std::size_t) (lead + k * piece + i)] = (float) (0.8 * std::exp (-3.0 * (double) i / piece)
                                                                    * std::sin (2.0 * 3.141592653589793 * 330.0 * (double) i / rate));
    in.right = in.left;
    return in;
}
} // namespace

TEST (warp190gx_transient_envelope_law)
{
    using lynx::stretchdetail::beatsEnvelopeGain;
    constexpr double sr = 48000.0;
    // unity up to the piece's own end, and with the knob at 100
    CHECK (beatsEnvelopeGain (0.0f, 0, sr) == 1.0f && beatsEnvelopeGain (0.0f, -100, sr) == 1.0f);
    CHECK (beatsEnvelopeGain (100.0f, 48000, sr) == 1.0f);
    // Live's slopes: 0 → 0.5604 dB/ms, 50 → 0.0886 dB/ms (measured on every piece of the drum loop and the voice)
    const auto dbPerMs = [&] (float e) { return -20.0 * std::log10 ((double) beatsEnvelopeGain (e, 4800, sr)) / 100.0; };
    CHECK (std::abs (dbPerMs (0.0f) - 0.5604) < 0.002);
    CHECK (std::abs (dbPerMs (50.0f) - 0.0886) < 0.0005);
    // monotonic in the knob, any rate
    CHECK (beatsEnvelopeGain (25.0f, 4800, sr) > beatsEnvelopeGain (0.0f, 4800, sr));
    CHECK (beatsEnvelopeGain (75.0f, 4800, sr) > beatsEnvelopeGain (50.0f, 4800, sr));
    CHECK (std::abs (beatsEnvelopeGain (0.0f, 441, 44100.0) - beatsEnvelopeGain (0.0f, 480, 48000.0)) < 1e-6f);
}

TEST (warp190gx_beats_fill_laws)
{
    const auto in = warp190gxPieces();
    const auto attacks = lynx::stretchdetail::beatsCuts (in.left.data(), (long) in.frames(), in.sampleRate);
    CHECK (attacks.size() == 4);
    if (attacks.size() != 4)
        return;
    const auto sr = in.sampleRate;
    const auto a = attacks[1], length = attacks[2] - a, o0 = (long) std::llround ((double) a * 1.5);
    const auto turn = length - (long) std::lround (sr * lynx::stretchdetail::beatsTurnSeconds);
    const auto mid = turn / 2;
    lynx::StretchOptions off, fwd, env;
    off.modes.beatsLoop = 0;
    fwd.modes.beatsLoop = 1;
    env.modes.beatsEnvelope = 0.0f;
    const auto backForth = lynx::timeStretch (in, 1.5, lynx::StretchMethod::beats);
    const auto silent = lynx::timeStretch (in, 1.5, lynx::StretchMethod::beats, 0.0, off);
    const auto forward = lynx::timeStretch (in, 1.5, lynx::StretchMethod::beats, 0.0, fwd);
    const auto faded = lynx::timeStretch (in, 1.5, lynx::StretchMethod::beats, 0.0, env);
    const auto at = [] (const lynx::SampleData& s, long i) { return (double) s.left[(std::size_t) i]; };
    const auto src = [&] (long i) { return (double) in.left[(std::size_t) i]; };
    double err = 0.0;
    // Back-and-forth: straight up to the turn, then the reading mirrors there (inside the piece's second half)
    for (long m = 0; m < 4000; ++m)
        err = std::max (err, std::abs (at (*backForth, o0 + turn + m) - src (a + turn - 1 - m)));
    CHECK (err < 1e-6);
    err = 0.0;
    for (long i = 2000; i < turn; i += 7)
        err = std::max (err, std::abs (at (*backForth, o0 + i) - src (a + i)));
    CHECK (err < 1e-6);
    // Forward: after a 5 ms crossfade the second half again from its middle
    const auto xf = (long) std::lround (sr * 0.005);
    err = 0.0;
    for (long m = xf; m < 4000; ++m)
        err = std::max (err, std::abs (at (*forward, o0 + turn + m) - src (a + mid + m)));
    CHECK (err < 1e-6);
    // Loop Off: a linear fade over the piece's last 10 ms, silence from its end to the next piece; the next piece starts
    // at full level (its predecessor ended on its own)
    const auto fadeOff = (long) std::lround (sr * 0.010);
    err = 0.0;
    for (long i = length - fadeOff; i < length; ++i)
        err = std::max (err, std::abs (at (*silent, o0 + i) - src (a + i) * (double) (length - i) / (double) fadeOff));
    CHECK (err < 1e-6);
    const auto next = (long) std::llround ((double) attacks[2] * 1.5);
    double gap = 0.0;
    for (long i = o0 + length; i < next; ++i)
        gap += at (*silent, i) * at (*silent, i);
    CHECK (gap == 0.0);
    err = 0.0;
    for (long i = 0; i < 200; ++i)
        err = std::max (err, std::abs (at (*silent, next + i) - src (attacks[2] + i)));
    CHECK (err < 1e-6);
    // Transient Envelope 0: unchanged up to the piece's end (after the previous piece's 16 ms crossfade tail), then Live's
    // 15.5 ms decay on the fill
    err = 0.0;
    for (long i = (long) std::lround (sr * 0.016); i < length; i += 3)
        err = std::max (err, std::abs (at (*faded, o0 + i) - at (*backForth, o0 + i)));
    CHECK (err < 1e-6);
    const auto after = (long) std::lround (sr * 0.010); // 10 ms into the fill: −5.6 dB
    const auto ratio = at (*faded, o0 + length + after) / at (*backForth, o0 + length + after);
    CHECK (std::abs (20.0 * std::log10 (std::abs (ratio)) + 5.604) < 0.05);
}

TEST (warp190gx_grid_pieces_turn_at_their_end)
{
    // Preserve 1/2 (grid cuts): Live fills from the piece's very end (no turn before it)
    const auto in = warp190gxPieces();
    lynx::StretchOptions half;
    half.modes.beatsPreserve = 1;
    half.sourceBeats = (double) in.frames() / 44100.0 * 2.0;
    const auto slow = lynx::timeStretch (in, 1.5, lynx::StretchMethod::beats, 0.0, half);
    const auto toSource = [&] (double y) { return y * (double) in.frames() / (double) slow->frames(); };
    const auto grid = lynx::stretchdetail::gridCuts ((long) in.frames(), (long) slow->frames(), half.sourceBeats, 2.0, toSource);
    CHECK (grid.size() >= 2);
    if (grid.size() < 2)
        return;
    const auto a = grid[0], length = grid[1] - a, o0 = (long) std::llround ((double) a * (double) slow->frames() / (double) in.frames());
    double err = 0.0;
    for (long m = 0; m < 3000; ++m)
        err = std::max (err, std::abs ((double) slow->left[(std::size_t) (o0 + length + m)] - (double) in.left[(std::size_t) (a + length - 1 - m)]));
    CHECK (err < 0.02); // (the mirror sample by sample, within one frame's step of the tone)
}
