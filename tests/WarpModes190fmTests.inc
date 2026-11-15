// Round 190fm: the warp modes' own settings (Live 12.4's Beats Preserve / Transient Loop Mode / Transient Envelope, Tones
// and Texture Grain Size; ALS_FORMAT §21), the O16 per-clip copy, the .lynx list form and the .als round trip.

#include "core/WarpModes.h"

namespace
{
// four 0.5 s pieces after a 0.1 s lead, each a 330 Hz tone that starts loud and decays (the 175a Beats test signal)
lynx::SampleData warp190fmPieces()
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
bool warp190fmSame (const lynx::SampleData& a, const lynx::SampleData& b)
{
    return a.left == b.left && a.right == b.right;
}
} // namespace

TEST (warp190fm_defaults_render_as_before)
{
    const auto in = warp190fmPieces();
    // Live's defaults are LYNX's render before 190fm: every method, with and without a source length in beats
    lynx::StretchOptions withBeats;
    withBeats.sourceBeats = 8.0;
    for (int m = 0; m < lynx::stretchMethodCount; ++m)
    {
        const auto method = (lynx::StretchMethod) m;
        CHECK (lynx::stretchOptionsKey ({}, method).empty());
        CHECK (lynx::stretchOptionsKey (withBeats, method).empty());
        const auto p0 = lynx::stretchdetail::presetFor (method), p1 = lynx::stretchdetail::presetFor (method, lynx::WarpModeSettings {});
        CHECK (p0.blockSeconds == p1.blockSeconds && p0.intervalSeconds == p1.intervalSeconds);
    }
    for (auto ratio : { 1.25, 0.8 })
    {
        const auto before = lynx::timeStretch (in, ratio, lynx::StretchMethod::beats);
        const auto after = lynx::timeStretch (in, ratio, lynx::StretchMethod::beats, 0.0, withBeats);
        CHECK (warp190fmSame (*before, *after));
    }
    const auto up = lynx::timeStretch (in, 1.25, lynx::StretchMethod::beats, 3.0);
    CHECK (warp190fmSame (*up, *lynx::timeStretch (in, 1.25, lynx::StretchMethod::beats, 3.0, withBeats)));
    // a sampler that never touched its settings asks for exactly the old options
    lynx::SamplerSettings s;
    CHECK (s.warpModes == lynx::WarpModeSettings {});
    CHECK (lynx::stretchOptionsKey (lynx::stretchOptionsOf (s), lynx::StretchMethod::beats).empty());
}

TEST (warp190fm_beats_preserve_grid)
{
    // Preserve 1/4 on a source of 4 beats stretched 2x: a cut at every beat of the output, mapped back into the source
    const long n = 40000, out = 80000;
    const auto cuts = lynx::stretchdetail::gridCuts (n, out, 4.0, lynx::stretchdetail::preserveDivisionBeats (2), [] (double y) { return y / 2.0; });
    CHECK (cuts.size() == 3);
    if (cuts.size() == 3)
        CHECK (cuts[0] == 10000 && cuts[1] == 20000 && cuts[2] == 30000);
    CHECK (lynx::stretchdetail::preserveDivisionBeats (0) == 4.0);
    CHECK (lynx::stretchdetail::preserveDivisionBeats (5) == 0.125);
    CHECK (lynx::stretchdetail::preserveDivisionBeats (6) == 0.0); // Transients
    // 1/32 on the same source: 31 cuts; the key carries the division and the source length
    CHECK (lynx::stretchdetail::gridCuts (n, out, 4.0, 0.125, [] (double y) { return y / 2.0; }).size() == 31);
    lynx::StretchOptions o;
    o.modes.beatsPreserve = 2;
    o.sourceBeats = 4.0;
    CHECK (lynx::stretchOptionsKey (o, lynx::StretchMethod::beats) == "|bp2@4");
    CHECK (lynx::stretchOptionsKey (o, lynx::StretchMethod::tones).empty()); // (Beats only)

    // the pieces start on the grid: on the test signal at 1.25x with Preserve 1/2 (0.5 s pieces = 1/2 note at 120 BPM
    // when the source spans 4.4 beats), output and source agree sample for sample inside each piece
    const auto in = warp190fmPieces();
    lynx::StretchOptions half;
    half.modes.beatsPreserve = 1;
    half.sourceBeats = (double) in.frames() / 44100.0 * 2.0; // 120 BPM
    const auto slow = lynx::timeStretch (in, 1.25, lynx::StretchMethod::beats, 0.0, half);
    const auto grid = lynx::stretchdetail::gridCuts ((long) in.frames(), (long) slow->frames(), half.sourceBeats, 2.0,
                                                     [&] (double y) { return y * (double) in.frames() / (double) slow->frames(); });
    CHECK (! grid.empty());
    int exact = 0;
    for (std::size_t k = 0; k + 1 < grid.size(); ++k)
    {
        const auto a = grid[k];
        const auto o0 = (long) std::llround ((double) a * (double) slow->frames() / (double) in.frames());
        double err = 0.0;
        for (long i = 800; i < 4000; ++i)
            err = std::max (err, (double) std::abs (slow->left[(std::size_t) (o0 + i)] - in.left[(std::size_t) (a + i)]));
        exact += err < 1e-6;
    }
    CHECK (exact == (int) grid.size() - 1);
}

TEST (warp190fm_beats_loop_modes_and_envelope)
{
    const auto in = warp190fmPieces();
    const auto attacks = lynx::stretchdetail::beatsCuts (in.left.data(), (long) in.frames(), in.sampleRate);
    CHECK (attacks.size() == 4);
    if (attacks.size() != 4)
        return;
    const auto fade = (long) std::lround (in.sampleRate * 0.016);
    const auto a = attacks[1], length = attacks[2] - a, o0 = (long) std::llround ((double) a * 1.5);
    const auto gapEnergy = [&] (const lynx::SampleData& s) {
        double e = 0.0;
        for (long i = length + fade; i < (long) std::llround ((double) attacks[2] * 1.5) - o0 - fade; ++i)
            e += (double) s.left[(std::size_t) (o0 + i)] * s.left[(std::size_t) (o0 + i)];
        return e;
    };
    lynx::StretchOptions off, fwd, env;
    off.modes.beatsLoop = 0;
    fwd.modes.beatsLoop = 1;
    env.modes.beatsEnvelope = 0.0f;
    const auto backForth = lynx::timeStretch (in, 1.5, lynx::StretchMethod::beats);
    const auto silent = lynx::timeStretch (in, 1.5, lynx::StretchMethod::beats, 0.0, off);
    const auto forward = lynx::timeStretch (in, 1.5, lynx::StretchMethod::beats, 0.0, fwd);
    const auto faded = lynx::timeStretch (in, 1.5, lynx::StretchMethod::beats, 0.0, env);
    // Loop Off: nothing after the piece's end (its fade into the following audio aside)
    CHECK (gapEnergy (*backForth) > 1.0);
    CHECK (gapEnergy (*silent) < 1e-9);
    // Loop Forward: the gap plays the piece's second half again (round 190gx: Live loops from the middle of the piece up
    // to 20 ms before its end; WarpLive190gxTests)
    const auto turn = length - (long) std::lround (in.sampleRate * lynx::stretchdetail::beatsTurnSeconds);
    const auto xf = (long) std::lround (in.sampleRate * 0.005);
    double err = 0.0;
    for (long j = xf; j < length / 4; ++j)
        err = std::max (err, (double) std::abs (forward->left[(std::size_t) (o0 + turn + j)] - in.left[(std::size_t) (a + turn / 2 + j)]));
    CHECK (err < 1e-6);
    // Transient Envelope 0: each piece's fill dies away quickly (the law: WarpLive190gxTests)
    CHECK (gapEnergy (*faded) < 0.05 * gapEnergy (*backForth));
    CHECK (lynx::stretchdetail::beatsEnvelopeGain (100.0f, 500, 44100.0) == 1.0f);
    CHECK (lynx::stretchOptionsKey (off, lynx::StretchMethod::beats) == "|bl0");
    CHECK (lynx::stretchOptionsKey (env, lynx::StretchMethod::beats) == "|be0");
    // transposed Beats honours them too (each piece read faster, then silence)
    const auto upOff = lynx::timeStretch (in, 1.5, lynx::StretchMethod::beats, 5.0, off);
    const auto upBack = lynx::timeStretch (in, 1.5, lynx::StretchMethod::beats, 5.0);
    CHECK (gapEnergy (*upOff) < 0.2 * gapEnergy (*upBack));
}

TEST (warp190fm_grain_sizes)
{
    lynx::WarpModeSettings w;
    w.tonesGrain = 60.0f;
    const auto t = lynx::stretchdetail::presetFor (lynx::StretchMethod::tones, w);
    CHECK (std::abs (t.blockSeconds - 0.08) < 1e-12);
    CHECK (std::abs (t.intervalSeconds - 0.08 / 6.0) < 1e-12); // (the overlap stays 6)
    w.textureGrain = 2.0f;
    CHECK (lynx::stretchdetail::presetFor (lynx::StretchMethod::texture, w).blockSeconds == 0.010); // (clamped)
    // a grain only changes its own mode
    CHECK (lynx::stretchdetail::presetFor (lynx::StretchMethod::complex, w).blockSeconds == lynx::stretchdetail::presetFor (lynx::StretchMethod::complex).blockSeconds);
    lynx::StretchOptions o;
    o.modes = w;
    CHECK (lynx::stretchOptionsKey (o, lynx::StretchMethod::tones) == "|tg60");
    CHECK (lynx::stretchOptionsKey (o, lynx::StretchMethod::texture) == "|xg2");
    // the render follows: another block, the same length
    const auto in = warp190fmPieces();
    const auto a = lynx::timeStretch (in, 1.25, lynx::StretchMethod::tones);
    const auto b = lynx::timeStretch (in, 1.25, lynx::StretchMethod::tones, 0.0, o);
    CHECK (a->frames() == b->frames());
    CHECK (! warp190fmSame (*a, *b));
}

TEST (warp190fm_lynx_list_round_trip)
{
    lynx::WarpModeSettings w;
    w.beatsPreserve = 3;
    w.beatsLoop = 1;
    w.beatsEnvelope = 47.5f;
    w.tonesGrain = 12.0f;
    w.textureGrain = 80.0f;
    w.textureFlux = 60.0f;
    w.proFormants = 30.0f;
    w.proEnvelope = 64.0f;
    w.complexPro = true;
    const auto list = lynx::warpmodes::toList (w);
    CHECK (lynx::warpmodes::fromList (list, list.size()) == w);
    CHECK (lynx::warpmodes::fromList (list, 8) == lynx::WarpModeSettings {}); // (a short list: the defaults)
    const std::array<double, 9> wild { 99, -4, 400, 0, 1000, -1, 101, 1, 0 };
    const auto c = lynx::warpmodes::fromList (wild, 9);
    CHECK (c.beatsPreserve == 6 && c.beatsLoop == 0 && c.beatsEnvelope == 100.0f && c.tonesGrain == 12.0f && c.textureGrain == 263.0f);
    CHECK (c.textureFlux == 0.0f && c.proFormants == 100.0f && c.proEnvelope == 8.0f && ! c.complexPro);
}

TEST (warp190fm_clip_view_edit_copies_a_shared_channel)
{
    // O16: two grid clips and a Playlist clip play one audio channel; an edit from the first grid clip's Clip View gives it
    // its own copy, the others keep the channel
    lynx::Song song;
    lynx::ChannelDef ch;
    ch.name = "Loop";
    ch.instrument = lynx::InstrumentKind::audioClip;
    ch.sampler.keepPitch = true;
    ch.sampler.stretchMethod = lynx::StretchMethod::beats;
    ch.sampler.stretchBeats = 8.0f;
    song.channels.push_back (ch);
    song.clipGrid.emplace();
    auto& grid = *song.clipGrid;
    grid.scenes.resize (2);
    for (int sc = 0; sc < 2; ++sc)
    {
        lynx::ClipGridSlot slot;
        slot.scene = sc;
        grid.slots.push_back (slot);
        lynx::Clip c;
        c.kind = lynx::ClipKind::audio;
        c.channel = 0;
        c.track = 0;
        c.length = 8 * lynx::ppq;
        c.gridSlot = sc;
        grid.clips.push_back (c);
    }
    lynx::Clip pl;
    pl.kind = lynx::ClipKind::audio;
    pl.channel = 0;
    pl.length = 8 * lynx::ppq;
    song.playlist.push_back (pl);
    CHECK (lynx::warpmodes::clipsOfChannel (song, 0) == 3);

    const auto own = lynx::warpmodes::ownChannelForGridClip (song, 0, 0);
    CHECK (own == 1);
    CHECK (song.channels.size() == 2 && song.channels[1].name == "Loop #2");
    CHECK (grid.clips[0].channel == 1 && grid.clips[1].channel == 0 && song.playlist[0].channel == 0);
    CHECK (lynx::warpmodes::setPreserve (song, own, 4) && lynx::warpmodes::setLoopMode (song, own, 0) && lynx::warpmodes::setEnvelope (song, own, 250.0));
    CHECK (song.channels[1].sampler.warpModes.beatsPreserve == 4 && song.channels[1].sampler.warpModes.beatsLoop == 0
           && song.channels[1].sampler.warpModes.beatsEnvelope == 100.0f);
    CHECK (song.channels[0].sampler.warpModes == lynx::WarpModeSettings {});
    // the copy is now the clip's alone: a second edit stays on it
    CHECK (lynx::warpmodes::ownChannelForGridClip (song, 0, 0) == 1 && song.channels.size() == 2);
    // Grain Size goes to the mode the channel plays
    CHECK (! lynx::warpmodes::setGrain (song, 1, 40.0)); // (Beats has none)
    CHECK (lynx::warpmodes::setMode (song, 1, (int) lynx::StretchMethod::texture) && lynx::warpmodes::setGrain (song, 1, 40.0));
    CHECK (song.channels[1].sampler.warpModes.textureGrain == 40.0f && song.channels[1].sampler.warpModes.tonesGrain == 30.0f);
    CHECK (lynx::warpmodes::setMode (song, 1, -1) && ! song.channels[1].sampler.keepPitch); // Re-Pitch
    CHECK (lynx::warpmodes::ownChannelForGridClip (song, 5, 0) == -1);
}

TEST (warp190fm_als_round_trip)
{
    // the 175a rich set's audio clip in Beats with every warp-mode setting changed and its file length recorded (so the
    // channel stretches offline): import → the channel's warpModes → export → the same members (Complex Pro too)
    using namespace lynx;
    for (const auto* mode : { "0", "6" })
    {
        auto xml = als175a::richSet();
        const std::string from = "</FileRef></SampleRef></AudioClip>";
        const std::string oldMode = "<WarpMode Value=\"4\" />";
        const auto at = xml.find (from), m = xml.find (oldMode);
        CHECK (at != std::string::npos && m != std::string::npos);
        if (at == std::string::npos || m == std::string::npos)
            return;
        xml.replace (at, from.size(), "</FileRef><DefaultDuration Value=\"96000\" /><DefaultSampleRate Value=\"48000\" /></SampleRef></AudioClip>");
        xml.replace (m, oldMode.size(), std::string ("<WarpMode Value=\"") + mode + "\" />" + als175a::v ("GranularityTones", "12")
                                            + als175a::v ("GranularityTexture", "80") + als175a::v ("FluctuationTexture", "60")
                                            + als175a::v ("TransientResolution", "3") + als175a::v ("TransientLoopMode", "1")
                                            + als175a::v ("TransientEnvelope", "40") + als175a::v ("ComplexProFormants", "30")
                                            + als175a::v ("ComplexProEnvelope", "64"));
        FlpImportReport report;
        const auto bytes = als175a::bytesOf (xml);
        const auto song = importAls (gzip::compressStored (bytes.data(), bytes.size()), als175a::options (&report), nullptr);
        CHECK (song.has_value());
        if (! song)
            return;
        const ChannelDef* ch = nullptr;
        for (const auto& c : song->playlist)
            if (c.kind == ClipKind::audio)
                ch = &song->channels[(std::size_t) c.channel];
        CHECK (ch != nullptr);
        if (ch == nullptr)
            return;
        const auto& w = ch->sampler.warpModes;
        CHECK (w.beatsPreserve == 3 && w.beatsLoop == 1 && w.beatsEnvelope == 40.0f && w.tonesGrain == 12.0f && w.textureGrain == 80.0f);
        CHECK (w.textureFlux == 60.0f && w.proFormants == 30.0f && w.proEnvelope == 64.0f);
        CHECK (w.complexPro == (mode[0] == '6'));
        CHECK (ch->sampler.stretchMethod == (mode[0] == '6' ? StretchMethod::complex : StretchMethod::beats));
        CHECK ((report.partialEffects.count ("Warp mode Complex Pro (plays as Complex; Formants and Envelope kept for export)") > 0) == (mode[0] == '6'));
        const auto out = exportAls (*song);
        for (const auto* member : { "<GranularityTones Value=\"12\"", "<GranularityTexture Value=\"80\"", "<FluctuationTexture Value=\"60\"",
                                    "<TransientResolution Value=\"3\"", "<TransientLoopMode Value=\"1\"", "<TransientEnvelope Value=\"40\"",
                                    "<ComplexProFormants Value=\"30\"", "<ComplexProEnvelope Value=\"64\"" })
            CHECK (out.xml.find (member) != std::string::npos);
        CHECK (out.xml.find (std::string ("<WarpMode Value=\"") + mode + "\"") != std::string::npos);
    }
}
