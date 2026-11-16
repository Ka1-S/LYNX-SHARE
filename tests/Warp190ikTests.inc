// Round 190ik (BUILD-LIST 8): Complex Pro (formant-keeping transposition, offline), Texture (granular, Grain Size and Flux)
// and the pitch-aware Beats detector, fitted black-box on Ableton Live 12.4's renders (/mnt/e/lynx-work/warp190ik,
// docs/analise/REA_rodada190ik.md). Old projects keep the old laws bit for bit (warpLaw / beatsLaw Classic and Measured).

#include "OnsetBank190ik.h"
#include "core/TimeStretch.h"
#include "core/WarpEngines.h"
#include "core/WarpModes.h"

namespace warp190ik
{
constexpr double twoPi = 2.0 * 3.141592653589793;

inline lynx::SampleData sine (double hz, double seconds, double rate = 44100.0, double level = 0.5)
{
    lynx::SampleData d;
    d.sampleRate = rate;
    for (long i = 0; i < (long) (seconds * rate); ++i)
        d.left.push_back ((float) (level * std::sin (twoPi * hz * (double) i / rate)));
    d.right = d.left;
    return d;
}

// a sung-like tone at 200 Hz whose spectral envelope has formants at 700 and 1200 Hz
inline lynx::SampleData vowel (double seconds = 2.0, double rate = 44100.0)
{
    lynx::SampleData d;
    d.sampleRate = rate;
    const auto envelope = [] (double f) {
        return 1.0 / (1.0 + std::pow ((f - 700.0) / 60.0, 2.0)) + 0.5 / (1.0 + std::pow ((f - 1200.0) / 70.0, 2.0)) + 0.01;
    };
    for (long i = 0; i < (long) (seconds * rate); ++i)
    {
        double v = 0.0;
        for (int k = 1; k * 200.0 < 5000.0; ++k)
            v += envelope (k * 200.0) * std::sin (twoPi * 200.0 * k * (double) i / rate);
        d.left.push_back ((float) (0.2 * v));
    }
    d.right = d.left;
    return d;
}

// frequency from the zero crossings of x[from, to)
inline double frequency (const std::vector<float>& x, long from, long to, double rate)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (long i = from + 1; i < to; ++i)
        if (x[(std::size_t) i - 1] < 0.0f && x[(std::size_t) i] >= 0.0f)
        {
            const auto t = (double) (i - 1) + (double) x[(std::size_t) i - 1] / (double) (x[(std::size_t) i - 1] - x[(std::size_t) i]);
            if (first < 0.0)
                first = t;
            last = t;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0.0;
}

inline double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

// the largest step between neighbouring samples in x[from, to), relative to a sine of `hz` at `level`'s steepest step
inline double roughness (const std::vector<float>& x, long from, long to, double hz, double rate, double level)
{
    double worst = 0.0;
    for (long i = from + 1; i < to; ++i)
        worst = std::max (worst, (double) std::abs (x[(std::size_t) i] - x[(std::size_t) i - 1]));
    return worst / (level * twoPi * hz / rate);
}

// the amplitude-weighted mean frequency of the harmonics of f0 (200…5000 Hz) over x[from, from + n)
inline double centroid (const std::vector<float>& x, long from, long n, double f0, double rate)
{
    double num = 0.0, den = 0.0;
    for (int k = 1; k * f0 < 5000.0; ++k)
    {
        double re = 0.0, im = 0.0;
        for (long i = 0; i < n; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (twoPi * (double) i / (double) n);
            const auto ph = twoPi * k * f0 * (double) i / rate;
            re += w * x[(std::size_t) (from + i)] * std::cos (ph);
            im += w * x[(std::size_t) (from + i)] * std::sin (ph);
        }
        const auto p = re * re + im * im;
        num += k * f0 * p;
        den += p;
    }
    return den > 0.0 ? num / den : 0.0;
}

inline lynx::StretchOptions pro (float formants = 100.0f, float envelope = 128.0f)
{
    lynx::StretchOptions o;
    o.modes.complexPro = true;
    o.modes.proFormants = formants;
    o.modes.proEnvelope = envelope;
    return o;
}

inline lynx::StretchOptions texture (float grain = 65.0f, float flux = 25.0f)
{
    lynx::StretchOptions o;
    o.modes.textureGrain = grain;
    o.modes.textureFlux = flux;
    return o;
}
} // namespace warp190ik

TEST (warp190ik_complex_pro_length_pitch_and_no_clicks)
{
    using namespace lynx;
    using namespace warp190ik;
    for (const auto rate : { 44100.0, 48000.0 })
        for (const auto ratio : { 0.8, 1.0, 1.5 })
            for (const auto st : { -5.0, 7.0 })
            {
                const auto in = sine (330.0, 2.0, rate);
                const auto out = timeStretch (in, ratio, StretchMethod::complex, st, pro());
                const auto expected = (long) std::llround ((double) in.frames() * ratio);
                CHECK ((long) out->frames() == expected);
                const auto n = (long) out->frames();
                const auto f = frequency (out->left, n / 4, 3 * n / 4, rate);
                CHECK (std::abs (cents (f, 330.0 * std::pow (2.0, st / 12.0))) < 3.0);
                CHECK (roughness (out->left, (long) (0.1 * rate), n - (long) (0.1 * rate), 330.0 * std::pow (2.0, st / 12.0), rate, 0.5) < 1.6);
            }
}

TEST (warp190ik_complex_pro_keeps_the_formants_by_the_formants_amount)
{
    using namespace lynx;
    using namespace warp190ik;
    const auto in = vowel();
    const auto rate = in.sampleRate;
    const long from = 22050, n = 22050;
    const auto source = centroid (in.left, from, n, 200.0, rate);
    const auto up = std::pow (2.0, 7.0 / 12.0);
    const auto kept = timeStretch (in, 1.0, StretchMethod::complex, 7.0, pro (100.0f));
    const auto moved = timeStretch (in, 1.0, StretchMethod::complex, 7.0, pro (0.0f));
    const auto half = timeStretch (in, 1.0, StretchMethod::complex, 7.0, pro (50.0f));
    const auto cKept = centroid (kept->left, from, n, 200.0 * up, rate);
    const auto cMoved = centroid (moved->left, from, n, 200.0 * up, rate);
    const auto cHalf = centroid (half->left, from, n, 200.0 * up, rate);
    CHECK (std::abs (cKept / source - 1.0) < 0.15);  // Formants 100: the envelope stays
    CHECK (cMoved / source > 1.35);                   // Formants 0: it moves with the pitch (×1.5)
    CHECK (cHalf > cKept && cHalf < cMoved);          // 50: between
    // a coarse Envelope (8) keeps less of the formants than the default
    const auto coarse = timeStretch (in, 1.0, StretchMethod::complex, 7.0, pro (100.0f, 8.0f));
    CHECK (centroid (coarse->left, from, n, 200.0 * up, rate) > cKept);
}

TEST (warp190ik_complex_pro_is_deterministic_and_mapped)
{
    using namespace lynx;
    using namespace warp190ik;
    const auto in = vowel (1.5);
    const auto a = timeStretch (in, 1.3, StretchMethod::complex, 4.0, pro());
    const auto b = timeStretch (in, 1.3, StretchMethod::complex, 4.0, pro());
    CHECK (a->left == b->left && a->right == b->right);
    // along a warp map: the map's length, and the same pitch
    const auto frames = (double) in.frames();
    const StretchMap map { { 0.0, 0.0 }, { frames * 0.5, frames * 0.4 }, { frames, frames * 1.2 } };
    const auto m = timeStretchMapped (in, map, StretchMethod::complex, 4.0, pro());
    CHECK ((long) m->frames() == (long) std::llround (frames * 1.2));
    const auto pure = timeStretchMapped (sine (330.0, 1.5), map, StretchMethod::complex, 4.0, pro());
    CHECK (std::abs (cents (frequency (pure->left, 20000, 50000, 44100.0), 330.0 * std::pow (2.0, 4.0 / 12.0))) < 5.0);
}

TEST (warp190ik_complex_pro_classic_law_is_complex_with_the_old_real_time_pitch)
{
    using namespace lynx;
    using namespace warp190ik;
    const auto in = vowel (1.0);
    auto classic = pro();
    classic.modes.warpLaw = WarpModeSettings::warpLawClassic;
    // Classic: the transposition is not rendered offline (the voice shifts it, as before 190ik), so the stretch is Complex's
    CHECK (! stretchBakesPitch (StretchMethod::complex, classic.modes));
    CHECK (stretchBakesPitch (StretchMethod::complex, pro().modes) && ! stretchBakesPitch (StretchMethod::complex, StretchOptions {}.modes));
    CHECK (timeStretch (in, 1.25, StretchMethod::complex, 5.0, classic)->left == timeStretch (in, 1.25, StretchMethod::complex)->left);
    // no transposition: Complex Pro stretches as Complex (Live: the two render alike, W2 cpro-def vs W1 cplx-def)
    CHECK (timeStretch (in, 1.25, StretchMethod::complex, 0.0, pro())->left == timeStretch (in, 1.25, StretchMethod::complex)->left);
}

TEST (warp190ik_texture_grains_flux_and_determinism)
{
    using namespace lynx;
    using namespace warp190ik;
    for (const auto rate : { 44100.0, 48000.0 })
    {
        // at the source's speed with no flux the grains read where they play: the source back, its level raised by the
        // two sine windows' sum (1 … √2, as Live's renders: power kept for uncorrelated grains)
        const auto in = sine (440.0, 1.0, rate);
        const auto same = timeStretch (in, 1.0, StretchMethod::texture, 0.0, texture (65.0f, 0.0f));
        CHECK (same->frames() == in.frames());
        double worst = 0.0;
        const auto period = 0.065 * rate;
        for (long i = (long) period; i < (long) in.left.size() - (long) period; ++i)
        {
            const auto u = std::fmod ((double) i, period) / (2.0 * period); // (where the later of the two grains is)
            const auto gain = std::sin (3.141592653589793 * u) + std::cos (3.141592653589793 * u);
            worst = std::max (worst, std::abs ((double) same->left[(std::size_t) i] - gain * in.left[(std::size_t) i]));
        }
        CHECK (worst < 1e-3);
        for (const auto ratio : { 0.5, 1.5 })
            for (const auto grain : { 10.0f, 65.0f, 263.0f })
            {
                const auto out = timeStretch (in, ratio, StretchMethod::texture, 0.0, texture (grain, 50.0f));
                const auto n = (long) out->frames();
                CHECK (n == (long) std::llround ((double) in.frames() * ratio));
                // the pitch stays (grains read at the source's own speed); a grain's window never steps
                CHECK (std::abs (cents (frequency (out->left, n / 4, 3 * n / 4, rate), 440.0)) < 4.0);
                CHECK (roughness (out->left, 0, n, 440.0, rate, 0.5) < 1.6);
                const auto again = timeStretch (in, ratio, StretchMethod::texture, 0.0, texture (grain, 50.0f));
                CHECK (again->left == out->left);
            }
    }
    // a transposition resamples each grain (Live's transposed Texture): the pitch moves, the length stays
    {
        const auto in = sine (440.0, 1.0);
        const auto up = timeStretch (in, 1.25, StretchMethod::texture, 7.0, texture (65.0f, 0.0f));
        const auto n = (long) up->frames();
        CHECK (n == (long) std::llround ((double) in.frames() * 1.25));
        CHECK (std::abs (cents (frequency (up->left, n / 4, 3 * n / 4, 44100.0), 440.0 * std::pow (2.0, 7.0 / 12.0))) < 4.0);
        CHECK (stretchBakesPitch (StretchMethod::texture, texture().modes));
    }
    // Flux scatters the grains (another render), Grain Size changes them
    const auto in = vowel (1.0);
    const auto f0 = timeStretch (in, 1.5, StretchMethod::texture, 0.0, texture (65.0f, 0.0f));
    const auto f100 = timeStretch (in, 1.5, StretchMethod::texture, 0.0, texture (65.0f, 100.0f));
    const auto g30 = timeStretch (in, 1.5, StretchMethod::texture, 0.0, texture (30.0f, 0.0f));
    CHECK (f0->left != f100->left && f0->left != g30->left);
    CHECK (std::abs (warpengine::textureGrainPeriodMs (65.0) - 65.0) < 1e-9 && warpengine::textureFluxSpreadMs (65.0, 0.0) == 0.0);
}

TEST (warp190ik_texture_classic_law_is_the_spectral_stretch_as_before)
{
    using namespace lynx;
    using namespace warp190ik;
    const auto in = vowel (1.0);
    auto classic = texture (80.0f, 60.0f);
    classic.modes.warpLaw = WarpModeSettings::warpLawClassic;
    const auto preset = stretchdetail::presetFor (StretchMethod::texture, classic.modes);
    CHECK (timeStretch (in, 1.3, StretchMethod::texture, 0.0, classic)->left == stretchdetail::stretchWith (in, 1.3, preset)->left);
    // keys: the Classic law keeps 190fm's, the Measured one keys grain and flux; Complex Pro its Formants / Envelope
    StretchOptions oldDefault;
    oldDefault.modes.warpLaw = WarpModeSettings::warpLawClassic;
    CHECK (stretchOptionsKey (oldDefault, StretchMethod::texture).empty());
    CHECK (stretchOptionsKey (StretchOptions {}, StretchMethod::texture) == "|gx65f25");
    CHECK (stretchOptionsKey (pro (30.0f, 64.0f), StretchMethod::complex) == "|pro30,64");
    CHECK (stretchOptionsKey (StretchOptions {}, StretchMethod::complex).empty());
}

TEST (warp190ik_pitch_aware_detector_gate_on_the_onset_bank)
{
    using namespace lynx;
    std::map<std::string, std::array<onsetbank190ik::Score, 3>> per;
    for (const auto& c : onsetbank190ik::bank())
        for (int law = 0; law < 3; ++law)
        {
            const auto cuts = stretchdetail::beatsCutsOfLaw (c.signal.data(), (long) c.signal.size(), c.sampleRate, law);
            std::vector<double> t;
            for (const auto x : cuts)
                t.push_back ((double) x / c.sampleRate);
            const auto s = onsetbank190ik::score (c.onsets, t);
            auto& p = per[c.category][(std::size_t) law];
            p.tp += s.tp;
            p.fp += s.fp;
            p.fn += s.fn;
            // the older laws are the older cuts
            if (law < 2)
                CHECK (cuts == stretchdetail::beatsCuts (c.signal.data(), (long) c.signal.size(), c.sampleRate, law == 1));
        }
    // the gate: nothing worse in any category, the tonal ones much better
    for (const auto& [category, s] : per)
        CHECK (s[2].f() >= s[1].f());
    CHECK (per["legato"][2].f() >= 0.9 && per["legato"][2].f() > per["legato"][1].f() + 0.3);
    CHECK (per["sung"][2].f() > per["sung"][1].f());
    CHECK (per["glides"][2].fp <= per["glides"][1].fp);
    CHECK (per["lowglide"][2].fp < per["lowglide"][1].fp); // (glides no longer cut where the level detector's envelope wobbles)
    CHECK (per["drums"][2].f() == 1.0);
}

TEST (warp190ik_beats_pitch_aware_law_cuts_a_legato_step)
{
    using namespace lynx;
    // one held tone that steps a fifth up at 0.6 s: the Measured law hears no attack there, PitchAware cuts it
    constexpr double rate = 44100.0;
    std::vector<float> x ((std::size_t) (1.2 * rate));
    double phase = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const auto t = (double) i / rate;
        phase += 2.0 * 3.141592653589793 * (t < 0.6 ? 220.0 : 330.0) / rate;
        double v = 0.0;
        for (int k = 1; k <= 8; ++k)
            v += std::sin (k * phase) / k;
        x[i] = (float) (0.2 * v * std::min (1.0, t / 0.01));
    }
    const auto measured = stretchdetail::beatsCutsOfLaw (x.data(), (long) x.size(), rate, WarpModeSettings::beatsLawMeasured);
    const auto aware = stretchdetail::beatsCutsOfLaw (x.data(), (long) x.size(), rate, WarpModeSettings::beatsLawPitchAware);
    const auto near = [&] (const std::vector<long>& cuts) {
        return std::any_of (cuts.begin(), cuts.end(), [&] (long c) { return std::abs ((double) c / rate - 0.6) < 0.025; });
    };
    CHECK (! near (measured));
    CHECK (near (aware));
    // the Beats render follows its law's cuts: the two laws differ only on such material
    SampleData in;
    in.sampleRate = rate;
    in.left = x;
    in.right = x;
    StretchOptions m, a;
    m.modes.beatsLaw = WarpModeSettings::beatsLawMeasured;
    CHECK (timeStretch (in, 1.3, StretchMethod::beats, 0.0, m)->left != timeStretch (in, 1.3, StretchMethod::beats, 0.0, a)->left);
}

TEST (warp190ik_als_complex_pro_transposes_offline_and_round_trips)
{
    using namespace lynx;
    auto xml = als175a::richSet(); // (its audio clip: WarpMode 4, PitchCoarse 2)
    const std::string from = "</FileRef></SampleRef></AudioClip>";
    const std::string oldMode = "<WarpMode Value=\"4\" />";
    const auto at = xml.find (from), m = xml.find (oldMode);
    CHECK (at != std::string::npos && m != std::string::npos);
    if (at == std::string::npos || m == std::string::npos)
        return;
    xml.replace (at, from.size(), "</FileRef><DefaultDuration Value=\"96000\" /><DefaultSampleRate Value=\"48000\" /></SampleRef></AudioClip>");
    xml.replace (m, oldMode.size(), "<WarpMode Value=\"6\" />" + als175a::v ("ComplexProFormants", "40") + als175a::v ("ComplexProEnvelope", "96")
                                        + als175a::v ("FluctuationTexture", "70"));
    FlpImportReport report;
    const auto bytes = als175a::bytesOf (xml);
    const auto song = importAls (gzip::compressStored (bytes.data(), bytes.size()), als175a::options (&report), nullptr);
    CHECK (song.has_value());
    if (! song)
        return;
    const Clip* clip = nullptr;
    for (const auto& c : song->playlist)
        if (c.kind == ClipKind::audio)
            clip = &c;
    CHECK (clip != nullptr);
    if (clip == nullptr)
        return;
    const auto& ch = song->channels[(std::size_t) clip->channel];
    // the transposition is the channel's offline one (formants kept), the clip plays untransposed
    CHECK (ch.sampler.warpModes.complexPro && ch.sampler.warpModes.warpLaw == WarpModeSettings::warpLawMeasured);
    CHECK (ch.sampler.stretchPitch == 2.0f && clip->pitch == 0.0f && ch.sampler.bakesPitch());
    CHECK (ch.sampler.warpModes.proFormants == 40.0f && ch.sampler.warpModes.proEnvelope == 96.0f && ch.sampler.warpModes.textureFlux == 70.0f);
    CHECK (report.partialEffects.empty() || report.partialEffects.count ("Warp mode Complex Pro (plays as Complex; Formants and Envelope kept for export)") == 0);
    const auto out = exportAls (*song);
    for (const auto* member : { "<WarpMode Value=\"6\"", "<PitchCoarse Value=\"2\"", "<ComplexProFormants Value=\"40\"", "<ComplexProEnvelope Value=\"96\"",
                                "<FluctuationTexture Value=\"70\"" })
        CHECK (out.xml.find (member) != std::string::npos);
}

TEST (warp190ik_clip_view_transposes_complex_pro_offline)
{
    using namespace lynx;
    Song song;
    ChannelDef ch;
    ch.instrument = InstrumentKind::audioClip;
    ch.sampler.keepPitch = true;
    ch.sampler.stretchBeats = 8.0f;
    song.channels.push_back (ch);
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = 0;
    clip.length = 8 * ppq;
    launcher::setClip (song, 0, 0, clip);
    launcher::ensureGrid (song).scenes.resize (1);
    CHECK (warpmodes::gridAudioClip (song, 0, 0) != nullptr);
    if (warpmodes::gridAudioClip (song, 0, 0) == nullptr)
        return;
    CHECK (warpmodes::setComplexPro (song, 0));
    CHECK (warpmodes::setClipTranspose (song, 0, 0, 0, 5.0f));
    CHECK (song.channels[0].sampler.stretchPitch == 5.0f && warpmodes::gridAudioClip (song, 0, 0)->pitch == 0.0f);
    // back to Classic (a project saved before 190ik): the transposition plays in real time again (the Pitch knob)
    CHECK (warpmodes::setWarpLaw (song, 0, 0) && song.channels[0].sampler.stretchPitch == 0.0f && song.channels[0].sampler.pitch == 5.0f);
    CHECK (warpmodes::setClipTranspose (song, 0, 0, 0, 3.0f) && warpmodes::gridAudioClip (song, 0, 0)->pitch == 3.0f);
    // Texture transposes offline too (its grains resampled); a mode that does not (Tones) gets the transposition on its knob
    CHECK (warpmodes::setWarpLaw (song, 0, 1) && warpmodes::setClipTranspose (song, 0, 0, 0, 4.0f) && song.channels[0].sampler.stretchPitch == 4.0f);
    CHECK (warpmodes::setMode (song, 0, (int) StretchMethod::texture) && song.channels[0].sampler.stretchPitch == 4.0f);
    CHECK (warpmodes::setMode (song, 0, (int) StretchMethod::tones) && song.channels[0].sampler.stretchPitch == 0.0f);
}

// Render-ahead (round 190gq) stays bit-exact with the new engines in the part rendered ahead: a Complex Pro channel with an
// offline transposition and a granular Texture channel, stretched through the rack's stretch factory.
TEST (warp190ik_render_ahead_is_bit_exact_with_complex_pro_and_texture)
{
    using namespace lynx;
    auto song = ra190gq::makeSong (4);
    const auto firstAudio = (int) song.channels.size();
    for (int k = 0; k < 2; ++k)
    {
        ChannelDef c;
        c.name = k == 0 ? "Pro" : "Texture";
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = k == 0 ? "pro.wav" : "tex.wav";
        c.mixerTrack = 5 + k;
        c.volume = 0.3f;
        c.sampler.keepPitch = true;
        c.sampler.stretchBeats = 6.0f;
        if (k == 0)
        {
            c.sampler.stretchMethod = StretchMethod::complex;
            c.sampler.warpModes.complexPro = true;
            c.sampler.stretchPitch = 5.0f;
        }
        else
            c.sampler.stretchMethod = StretchMethod::texture;
        song.channels.push_back (c);
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = firstAudio + k;
        clip.track = 6 + k;
        clip.length = ppq * 6;
        song.playlist.push_back (clip);
    }
    RackState::Factories factories;
    factories.samples = [] (const std::string& path) {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = 44100.0;
        const auto f = path == "pro.wav" ? 220.0 : 330.0;
        for (int i = 0; i < 44100 * 3; ++i)
            d->left.push_back ((float) (0.5 * std::sin (2.0 * 3.141592653589793 * f * i / 44100.0) * std::exp (-(i % 11025) / 4000.0)));
        d->right = d->left;
        return std::shared_ptr<const SampleData> (d);
    };
    factories.stretchedSamples = [&factories] (const std::string& path, double ratio, StretchMethod method, double semitones, const StretchMap& map,
                                               const StretchOptions& options) {
        const auto data = factories.samples (path);
        return std::shared_ptr<const SampleData> (map.empty() ? timeStretch (*data, ratio, method, semitones, options)
                                                              : timeStretchMapped (*data, map, method, semitones, options));
    };
    const int blocks = 1400;
    auto state = RackState::build (song, nullptr, 44100.0, factories);
    RackRenderer plain;
    plain.prepare (44100.0, 2048);
    MeterBank meters;
    LiveEventQueue live;
    ra190gq::Callback cb0 (song, 64, 0.0);
    std::vector<float> l0, r0, l1, r1;
    cb0.run (blocks, l0, r0, [&] (double start, int n, double, float* a, float* b, int) {
        plain.process (state.get(), true, start, n, a, b, &live, &meters, nullptr);
    });
    ra190gq::AheadRig rig (song, 0, {});
    rig.exchange.publish (RackState::build (song, nullptr, 44100.0, factories));
    ra190gq::Callback cb1 (song, 64, 0.0);
    cb1.run (blocks, l1, r1, [&] (double start, int n, double tps, float* a, float* b, int) {
        std::this_thread::sleep_for (std::chrono::microseconds (100));
        rig.block (cb1, start, n, tps, a, b);
    });
    CHECK (ra190gq::same (l0, l1) && ra190gq::same (r0, r1));
    CHECK (rig.ahead.stats().engagements.load() >= 1);
    CHECK (ra190gq::energy (l0.data() + 44100, 44100) > 1.0);
}
