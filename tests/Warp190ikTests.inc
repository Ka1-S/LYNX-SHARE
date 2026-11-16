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

// the strongest frequency of x[from, to) between lo and hi Hz (Hann-windowed DFT scanned in 0.05 Hz steps): the pitch of a
// tone whose phase drifts (overlapping grains) where zero crossings would wobble
inline double peakFrequency (const std::vector<float>& x, long from, long to, double rate, double lo, double hi)
{
    const auto n = to - from;
    double best = lo, bestP = -1.0;
    for (double f = lo; f <= hi; f += 0.05)
    {
        double re = 0.0, im = 0.0;
        for (long i = 0; i < n; i += 2)
        {
            const auto w = 0.5 - 0.5 * std::cos (twoPi * (double) i / (double) n);
            re += w * x[(std::size_t) (from + i)] * std::cos (twoPi * f * (double) i / rate);
            im += w * x[(std::size_t) (from + i)] * std::sin (twoPi * f * (double) i / rate);
        }
        if (re * re + im * im > bestP)
        {
            bestP = re * re + im * im;
            best = f;
        }
    }
    return best;
}

// the power-weighted mean frequency of x[from, to) between lo and hi Hz (0.25 Hz steps): the pitch of grains whose phases
// differ (their amplitude modulation puts symmetric side bands around the tone)
inline double meanFrequency (const std::vector<float>& x, long from, long to, double rate, double lo, double hi)
{
    const auto n = to - from;
    double num = 0.0, den = 0.0;
    for (double f = lo; f <= hi; f += 0.25)
    {
        double re = 0.0, im = 0.0;
        for (long i = 0; i < n; i += 2)
        {
            const auto w = 0.5 - 0.5 * std::cos (twoPi * (double) i / (double) n);
            re += w * x[(std::size_t) (from + i)] * std::cos (twoPi * f * (double) i / rate);
            im += w * x[(std::size_t) (from + i)] * std::sin (twoPi * f * (double) i / rate);
        }
        num += f * (re * re + im * im);
        den += re * re + im * im;
    }
    return den > 0.0 ? num / den : 0.0;
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
                // the pitch stays (grains read at the source's own speed; short grains add their own buzz, a comb of lines
                // 1 / grain apart, so the pitch is read on the longer ones); a grain's window never steps
                if (grain >= 65.0f)
                    CHECK (std::abs (cents (meanFrequency (out->left, n / 4, 3 * n / 4, rate, 380.0, 500.0), 440.0)) < 20.0);
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
        CHECK (std::abs (cents (peakFrequency (up->left, n / 4, 3 * n / 4, 44100.0, 640.0, 680.0), 440.0 * std::pow (2.0, 7.0 / 12.0))) < 4.0);
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

// Issue #187 (round 190ik): a warped audio clip follows tempo automation (FL and Live both make it): its voices read the
// warped sample at the playing tempo (block by block, as the transport moves), keeping their pitch through their
// shifters when the channel keeps the pitch.
namespace warp190ik
{
// clicks (3 ms of 3 kHz) at the given seconds of a 4 s file at 44.1 kHz, or a steady 440 Hz tone
inline std::shared_ptr<const lynx::SampleData> clicks (const std::vector<double>& at)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = 44100.0;
    d->left.assign (44100 * 4, 0.0f);
    for (const auto t : at)
        for (int i = 0; i < 132; ++i)
            d->left[(std::size_t) (t * 44100.0) + (std::size_t) i] = (float) (0.8 * std::sin (twoPi * 3000.0 * i / 44100.0) * std::exp (-i / 40.0));
    d->right = d->left;
    return d;
}

// a song at 120 BPM: the warped clip of channel "a.wav" over `beats` beats from tick 0, and (ramp) a tempo automation clip
// from 120 to `endBpm` over the same beats
inline lynx::Song rampSong (double beats, bool keepPitch, bool ramp, double endBpm = 90.0, const std::vector<lynx::WarpPoint>& warp = {})
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.playlist.clear();
    song.mode = PlayMode::song;
    song.bpm = 120.0;
    ChannelDef c;
    c.name = "warped";
    c.instrument = InstrumentKind::audioClip;
    c.samplePath = "a.wav";
    c.volume = 10000.0f / 12800.0f;
    c.sampler.keepPitch = keepPitch;
    c.sampler.stretchMethod = StretchMethod::complex;
    c.sampler.stretchBeats = (float) beats;
    c.sampler.warp = warp;
    song.channels.push_back (c);
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = (int) song.channels.size() - 1;
    clip.track = 0;
    clip.start = 0;
    clip.length = (std::int64_t) std::llround (beats * ppq);
    song.playlist.push_back (clip);
    if (ramp)
    {
        Automation a;
        a.target.kind = AutomationTargetKind::tempo;
        a.points = { { 0, valueFromTempo (120.0) }, { clip.length, valueFromTempo (endBpm) } };
        song.automations.push_back (a);
        Clip t;
        t.kind = ClipKind::automation;
        t.automation = (int) song.automations.size() - 1;
        t.track = 1;
        t.start = 0;
        t.length = clip.length + ppq;
        song.playlist.push_back (t);
    }
    return song;
}

struct Rendered
{
    std::vector<float> left;
    std::vector<std::pair<long, double>> ticks; // (sample at a block's start, its tick)
    double sampleOfTick (double tick) const
    {
        for (std::size_t k = 1; k < ticks.size(); ++k)
            if (ticks[k].second >= tick)
            {
                const auto& a = ticks[k - 1];
                const auto& b = ticks[k];
                return (double) a.first + (double) (b.first - a.first) * (tick - a.second) / std::max (1e-9, b.second - a.second);
            }
        return -1.0;
    }
};

// the song rendered block by block as the engine plays it (the position moving with the tempo the rack played)
inline Rendered render (const lynx::Song& song, std::shared_ptr<const lynx::SampleData> data, double seconds)
{
    using namespace lynx;
    RackState::Factories factories;
    factories.samples = [data] (const std::string&) { return data; };
    factories.stretchedSamples = [data] (const std::string&, double ratio, StretchMethod method, double semitones, const StretchMap& map,
                                         const StretchOptions& options) {
        return std::shared_ptr<const SampleData> (map.empty() ? timeStretch (*data, ratio, method, semitones, options)
                                                              : timeStretchMapped (*data, map, method, semitones, options));
    };
    auto state = RackState::build (song, nullptr, 44100.0, factories);
    RackRenderer renderer;
    renderer.prepare (44100.0, 2048);
    MeterBank meters;
    LiveEventQueue live;
    Rendered out;
    const int block = 64;
    const auto blocks = (int) (seconds * 44100.0 / block);
    out.left.assign ((std::size_t) (blocks * block), 0.0f);
    std::vector<float> right (out.left.size());
    double tick = 0.0;
    for (int k = 0; k < blocks; ++k)
    {
        out.ticks.push_back ({ (long) k * block, tick });
        renderer.process (state.get(), true, tick, block, out.left.data() + k * block, right.data() + k * block, &live, &meters, nullptr);
        tick += block * ticksPerSample (renderer.currentBpm(), 44100.0);
    }
    out.ticks.push_back ({ (long) blocks * block, tick });
    return out;
}

// where the click nearest `around` (± 60 ms) peaks: the 1 ms RMS envelope's maximum
inline long clickAt (const std::vector<float>& x, long around)
{
    long best = -1;
    double bestE = 0.0;
    for (long i = std::max (0L, around - 2646); i < std::min ((long) x.size() - 44, around + 2646); i += 4)
    {
        double e = 0.0;
        for (long j = i; j < i + 44; ++j)
            e += (double) x[(std::size_t) j] * x[(std::size_t) j];
        if (e > bestE)
        {
            bestE = e;
            best = i;
        }
    }
    return best;
}
} // namespace warp190ik

TEST (issue187_warped_clip_stays_beat_locked_under_a_tempo_ramp)
{
    using namespace lynx;
    using namespace warp190ik;
    // one click per beat of the file's own 120 BPM; the song ramps 120 → 90 BPM over the clip's 8 beats
    std::vector<double> beatsAt;
    for (int b = 0; b < 8; ++b)
        beatsAt.push_back (0.5 * b);
    const auto data = clicks (beatsAt);
    const auto refShift = clickAt (render (rampSong (8.0, true, false), data, 4.2).left, 0); // (the click's own peak offset)
    for (const auto keepPitch : { true, false })
    {
        const auto r = render (rampSong (8.0, keepPitch, true), data, 5.2);
        CHECK (r.sampleOfTick (8.0 * ppq) > 44100.0 * 4.3); // (the ramp made the 8 beats last longer than 4 s)
        int locked = 0;
        for (int b = 1; b < 8; ++b)
        {
            const auto want = (long) std::llround (r.sampleOfTick ((double) b * ppq)) + refShift;
            const auto got = clickAt (r.left, want);
            locked += std::abs (got - want) <= 265; // 6 ms
        }
        CHECK (locked == 7);
        if (locked != 7)
            std::printf ("  issue187: keepPitch %d: %d of 7 beats locked\n", (int) keepPitch, locked);
    }
}

TEST (issue187_warp_markers_land_on_the_bar_lines_under_a_tempo_ramp)
{
    using namespace lynx;
    using namespace warp190ik;
    // a file whose tempo drifts: its warp markers put 0 / 0.9 / 2.1 / 3.0 s on beats 0 / 2 / 4 / 6 (bar lines at 0 and 4);
    // clicks sit on the markers. Under a 120 → 90 ramp each marker's click plays on its beat.
    const std::vector<WarpPoint> warp { { 0.0, 0.0 }, { 0.9, 2.0 }, { 2.1, 4.0 }, { 3.0, 6.0 } };
    const auto beats = 6.0 + 2.0 * (1.0 / 0.9); // (the file's 4 s, the last segment's tempo to its end)
    const auto data = clicks ({ 0.0, 0.9, 2.1, 3.0 });
    const auto refShift = clickAt (render (rampSong (beats, true, false, 90.0, warp), data, 4.5).left, 0);
    const auto r = render (rampSong (beats, true, true, 90.0, warp), data, 5.8);
    int onBeat = 0;
    for (const auto b : { 2.0, 4.0, 6.0 })
    {
        const auto want = (long) std::llround (r.sampleOfTick (b * ppq)) + refShift;
        onBeat += std::abs (clickAt (r.left, want) - want) <= 265;
    }
    CHECK (onBeat == 3);
    // the bar line (beat 4) is where the tempo automation has already slowed down: later than at a fixed 120 BPM
    CHECK (r.sampleOfTick (4.0 * ppq) > 2.0 * 44100.0 + 4410.0);
}

TEST (issue187_a_warped_clip_keeps_its_pitch_under_a_tempo_ramp)
{
    using namespace lynx;
    using namespace warp190ik;
    auto tone = std::make_shared<SampleData>();
    tone->sampleRate = 44100.0;
    for (int i = 0; i < 44100 * 4; ++i)
        tone->left.push_back ((float) (0.4 * std::sin (twoPi * 440.0 * i / 44100.0)));
    tone->right = tone->left;
    const auto keep = render (rampSong (8.0, true, true, 80.0), tone, 5.0);
    const auto resampled = render (rampSong (8.0, false, true, 80.0), tone, 5.0);
    // late in the ramp (about 85 BPM): the pitch-keeping clip stays at 440 Hz, the resampling one follows the tempo down
    const auto at = (long) keep.sampleOfTick (7.0 * ppq);
    CHECK (std::abs (cents (peakFrequency (keep.left, at - 8000, at + 8000, 44100.0, 300.0, 500.0), 440.0)) < 15.0);
    const auto bpmThere = 120.0 + (80.0 - 120.0) * 7.0 / 8.0;
    const auto atR = (long) resampled.sampleOfTick (7.0 * ppq);
    CHECK (std::abs (cents (peakFrequency (resampled.left, atR - 8000, atR + 8000, 44100.0, 300.0, 500.0), 440.0 * bpmThere / 120.0)) < 30.0);
}

TEST (issue187_fixed_tempo_renders_bit_identical_to_the_renderer_without_tempo_following)
{
    using namespace lynx;
    using namespace warp190ik;
    // at the song's own fixed tempo the tempo hook changes nothing: the same samples with it on and off (off = the renderer
    // before round 190ik), for a warped clip that keeps its pitch, one that resamples and a warped-marker one
    auto tone = std::make_shared<SampleData>();
    tone->sampleRate = 44100.0;
    for (int i = 0; i < 44100 * 4; ++i)
        tone->left.push_back ((float) (0.4 * std::sin (twoPi * 330.0 * i / 44100.0) * std::exp (-(i % 22050) / 6000.0)));
    tone->right = tone->left;
    const std::vector<WarpPoint> warp { { 0.0, 0.0 }, { 0.9, 2.0 }, { 2.1, 4.0 }, { 3.0, 6.0 } };
    for (const auto& song : { rampSong (8.0, true, false), rampSong (8.0, false, false), rampSong (6.0 + 2.0 / 0.9, true, false, 90.0, warp) })
    {
        tempofollow::setEnabled (false);
        const auto before = render (song, tone, 4.2);
        tempofollow::setEnabled (true);
        const auto after = render (song, tone, 4.2);
        CHECK (before.left == after.left);
        CHECK (std::abs (after.sampleOfTick (8.0 * ppq) - 4.0 * 44100.0) < 64.0 || song.channels.back().sampler.warp.size() > 0);
    }
    // under the ramp the hook is what makes the difference
    tempofollow::setEnabled (false);
    const auto off = render (rampSong (8.0, true, true), tone, 4.0);
    tempofollow::setEnabled (true);
    CHECK (off.left != render (rampSong (8.0, true, true), tone, 4.0).left);
}
