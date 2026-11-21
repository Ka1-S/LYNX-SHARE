// Round 190ja: Helix (core/WavetableSynth.h), LYNX's own wavetable synth, its tables (core/WavetableTables.cpp), Live's
// Wavetable imported onto it (core/AlsDevicesWt.cpp) and exported back (core/AlsExport.cpp). Uses the .als builders of
// AlsDevices190edTests.inc. No data file is needed.

#include "core/AlsExport.h"
#include "core/GeneratorPresets.h"
#include "core/ModelParams.h"
#include "core/NativeEngines.h"
#include "core/WavetableSynth.h"

namespace helix190ja
{
namespace w = lynx::wt;

inline double rms (const std::vector<float>& x, int from, int to, int stride = 2, int offset = 0)
{
    double s = 0.0;
    int n = 0;
    for (int i = from; i < to; ++i, ++n)
        s += (double) x[(std::size_t) (i * stride + offset)] * x[(std::size_t) (i * stride + offset)];
    return n > 0 ? std::sqrt (s / n) : 0.0;
}
inline double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }
// the amplitude of the partial at hz (one-bin DFT, Hann window) from the left channel of an interleaved render
inline double partial (const std::vector<float>& x, int from, int to, double hz, double fs)
{
    std::complex<double> acc {};
    double wsum = 0.0;
    for (int i = from; i < to; ++i)
    {
        const auto wv = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * (i - from) / (to - from));
        acc += wv * (double) x[(std::size_t) (2 * i)] * std::polar (1.0, -2.0 * 3.14159265358979 * hz * i / fs);
        wsum += wv;
    }
    return 2.0 * std::abs (acc) / wsum;
}
// the energy outside the harmonics of f0 against the energy on them (dB), from a direct DFT on a coarse grid
inline double nonHarmonicDb (const std::vector<float>& x, int from, int to, double f0, double fs)
{
    double harm = 0.0, other = 0.0;
    for (double hz = f0 / 8.0; hz < fs * 0.5 - 200.0; hz += f0 / 8.0)
    {
        const auto a = partial (x, from, to, hz, fs);
        const auto h = hz / f0;
        if (std::abs (h - std::round (h)) < 0.01)
            harm += a * a;
        else if (std::abs (h - std::round (h)) > 0.2)
            other += a * a;
    }
    return 10.0 * std::log10 (std::max (other, 1e-30) / std::max (harm, 1e-30));
}
// Helix's defaults with the law-test base: no filter, no velocity on the amp, full sustain, volume 1, table Shapes
inline std::vector<double> base()
{
    std::vector<double> s (w::defaults().begin(), w::defaults().end());
    s[(std::size_t) (w::filterBase (0) + w::flt::on)] = 0.0;
    s[(std::size_t) w::oscBase (0) + w::osc::effectMode] = w::effectNone;
    s[(std::size_t) w::matrixIndex (w::tgt::amp, w::src::velocity)] = 0.0;
    s[(std::size_t) w::envBase (0) + w::env::sustain] = 1.0;
    s[(std::size_t) w::volume] = 1.0;
    return s;
}
inline std::vector<float> render (const std::vector<double>& s, int key, double seconds, double fs = 48000.0, float vel = 1.0f)
{
    w::RefNote n;
    n.key = key;
    n.velocity = vel;
    n.release = (int) (seconds * 0.8 * fs);
    return w::renderReference (s, { n }, fs, (int) (seconds * fs));
}
// a counted zero-crossing frequency over [from, to) of the left channel
inline double frequency (const std::vector<float>& x, int from, int to, double fs)
{
    int first = -1, last = -1, count = 0;
    double firstAt = 0.0, lastAt = 0.0;
    for (int i = from + 1; i < to; ++i)
    {
        const auto a = x[(std::size_t) (2 * i - 2)], b = x[(std::size_t) (2 * i)];
        if (a < 0.0f && b >= 0.0f)
        {
            const auto at = (double) (i - 1) + a / (a - b);
            if (first < 0)
                first = i, firstAt = at;
            last = i, lastAt = at;
            ++count;
        }
    }
    return count > 1 ? (count - 1) * fs / (lastAt - firstAt) : 0.0;
}
} // namespace helix190ja

TEST (helix190ja_parameters_and_laws)
{
    using namespace lynx;
    namespace w = wt;
    // the automatable table: every parameter once, unique English names, defaults inside their ranges
    const auto& table = helixEngineParams();
    // (round 190mi: Live's values, then Helix+'s automatable values after Live's state; 190ml: then Helix+ 2's; 190mt:
    // then round 3's)
    const auto plusEnd = w::paramCount + w::ext::autoEnd - 1;
    const auto plus2End = plusEnd + w::ext2::autoEnd - 1;
    const auto plus3End = plus2End + w::ext3::autoEnd - 1;
    CHECK ((int) table.size() == plus3End + w::ext4::autoEnd - 1); // (190oc: round 4's after)
    std::set<std::string> names;
    for (int i = 0; i < (int) table.size(); ++i)
    {
        const auto& p = table[(std::size_t) i];
        const auto index = i < w::paramCount ? i
                           : i < plusEnd     ? w::extBase + 1 + (i - w::paramCount)
                           : i < plus2End    ? w::ext2Base + 1 + (i - plusEnd)
                           : i < plus3End    ? w::ext3Base + 1 + (i - plus2End)
                                             : w::ext4Base + 1 + (i - plus3End);
        CHECK (p.index == index && ! p.name.empty() && p.high > p.low);
        names.insert (p.name);
        const auto d = w::defaults()[(std::size_t) index];
        CHECK (d >= p.low - 1e-6 && d <= p.high + 1e-6);
        CHECK (! w::paramText (index, d).empty());
        CHECK (w::paramName (index) == p.name);
    }
    CHECK (names.size() == table.size());
    CHECK (modelparams::tableOf (InstrumentKind::helix) != nullptr);
    CHECK (modelparams::findByName (InstrumentKind::helix, "Filter 1 Freq") != nullptr
           && modelparams::findByName (InstrumentKind::helix, "Filter 1 Freq")->index == w::filterBase (0) + w::flt::freq);
    // the layout: parameters, two tables, the matrix (13 × 52) [DATA: Live's InstrumentVector]
    CHECK (w::tableBase == w::paramCount && w::matrixBase == w::paramCount + 2 && w::liveStateSize == w::matrixBase + 52 * 13);
    CHECK (w::sourceName (w::src::slide) == "Slide" && w::sourceName (w::src::notePitchBend) == "Note PB");
    CHECK (w::targetName (w::tgt::oscFx1, w::effectModern) == "Osc 1 Warp" && w::targetName (w::tgt::oscFx2 + 6, 0, w::effectFm) == "Osc 2 FM Amt");
    CHECK (w::targetName (w::tgt::glide) == "Glide" && w::targetName (w::tgt::filterFreq + 4) == "Filter 2 Freq");
    // Live's frequency knob: 20 Hz … 20.48 kHz over ten octaves
    CHECK (std::fabs (w::filterHz (0.0) - 20.0) < 1e-9 && std::fabs (w::filterHz (1.0) - 20480.0) < 1e-6 && std::fabs (w::filterNorm (640.0) - 0.5) < 1e-9);
    CHECK (w::polyVoiceCount (0) == 2 && w::polyVoiceCount (6) == 8);
    CHECK (w::segment (0.0f, 0.7f) == 0.0f && std::fabs (w::segment (1.0f, -0.7f) - 1.0f) < 1e-6f && std::fabs (w::segment (0.3f, 0.0f) - 0.3f) < 1e-6f);
    CHECK (w::segment (0.3f, 0.5f) > 0.3f && w::segment (0.3f, -0.5f) < 0.3f);
    CHECK (std::fabs (w::fmRatio (0.0) - 1.0) < 1e-12 && w::fmRatio (1.0) > w::fmRatio (0.5) && w::syncRatio (0.0) == 1.0);
    // a new channel from the plugin list needs nothing; the channel's values round-trip
    natives::FlAccess none;
    const auto made = natives::makeChannel (InstrumentKind::helix, none);
    CHECK (made.has_value() && made->instrument == InstrumentKind::helix && made->name == "Helix");
    ChannelDef c;
    c.instrument = InstrumentKind::helix;
    CHECK (modelparams::rawValue (c, w::routing) == std::optional<float> (0.0f));
    CHECK (modelparams::setRawValue (c, w::filterBase (1) + w::flt::freq, 1234.0f) && (int) c.modelParams.size() == w::stateSize);
    CHECK (helixState (c)[(std::size_t) (w::filterBase (1) + w::flt::freq)] == 1234.0);
}

TEST (helix190ja_tables_are_band_limited_and_lynx_own)
{
    using namespace lynx;
    namespace w = wt;
    CHECK (w::tableCount() >= 40);
    const auto cats = w::tableCategories();
    CHECK (cats.size() == 11 && cats.front() == "Basics");
    std::set<std::string> names;
    for (int t = 0; t < w::tableCount(); ++t)
    {
        names.insert (w::tableName (t));
        const auto& tb = w::table (t);
        CHECK (tb.name == w::tableName (t) && tb.category == w::tableCategory (t));
        CHECK (&w::table (t) == w::tableIfBuilt (t)); // (built once, kept)
        // every mip: the frame's length, a guard sample; one gain for the table (a shape's own ±1, an additive table's
        // loudest frame at 1, a noise table at a set RMS)
        float peak = 0.0f;
        for (int m = 0; m < w::mipCount; ++m)
        {
            const auto len = w::mipLength (m);
            for (int f = 0; f < w::frameCount; ++f) // (every frame inside the table's own data: equal frames are shared, 190js)
                CHECK (tb.frame (m, f) >= tb.pool.data() && tb.frame (m, f) + len + 1 <= tb.pool.data() + tb.pool.size());
            for (int f = 0; f < w::frameCount; f += 21)
            {
                const auto* fr = tb.frame (m, f);
                CHECK (fr[len] == fr[0]);
                for (int i = 0; i < len; ++i)
                    peak = std::max (peak, std::abs (fr[i]));
            }
        }
        CHECK (peak > 0.3f && peak < 2.5f);
    }
    CHECK ((int) names.size() == w::tableCount());
    // a mip holds no partial above its limit: the DFT of a bright frame (Shapes' square, the last frame) at mip 5
    {
        const auto& tb = w::table (w::tableByName ("Shapes"));
        const int m = 5, len = w::mipLength (m), top = w::mipPartials (m);
        const auto* fr = tb.frame (m, w::frameCount - 1);
        double inBand = 0.0, outBand = 0.0;
        for (int h = 1; h < len / 2; ++h)
        {
            std::complex<double> acc {};
            for (int i = 0; i < len; ++i)
                acc += (double) fr[i] * std::polar (1.0, -2.0 * 3.14159265358979 * h * i / len);
            (h <= top ? inBand : outBand) += std::norm (acc);
        }
        CHECK (10.0 * std::log10 (outBand / inBand + 1e-30) < -100.0);
    }
    // the frames' spectra are deterministic (recipes, no state) and Shapes starts on a sine
    std::vector<float> a (16), b (16), ph (16);
    w::tableSpectrum (w::tableByName ("Shapes"), 0, a.data(), ph.data(), 16);
    w::tableSpectrum (w::tableByName ("Shapes"), 0, b.data(), nullptr, 16);
    CHECK (a == b && a[0] > 0.9f && a[1] < 1e-6f && a[2] < 1e-6f);
    // Live's names map onto LYNX tables (never Live's data): the match and its note
    const auto m = w::matchLiveTable ("Basic Shapes");
    CHECK (std::string (w::tableName (m.table)) == "Shapes" && m.note.find ("LYNX's own table") != std::string::npos);
    const auto unknown = w::matchLiveTable ("Some Unknown Sync Thing");
    CHECK (std::string (w::tableName (unknown.table)) == "Sync Sweep" && ! unknown.note.empty());
    CHECK (w::tableByName ("Shapes") >= 0 && w::tableByName ("No Such Table") == -1);
    CHECK (w::matchLiveTable (w::liveTableFor (w::tableByName ("Shapes"))).table == w::tableByName ("Shapes"));
}

// 190js: a frame bit for bit equal to another of the same length is kept once (the values the oscillator reads are the
// floats one array per mip held: renders and every frame checked identical against the old layout); RAM per table
TEST (helix190js_tables_keep_equal_frames_once)
{
    using namespace lynx;
    namespace w = wt;
    std::size_t full = 0; // one array per mip, as before 190js
    for (int m = 0; m < w::mipCount; ++m)
        full += (std::size_t) w::frameCount * (std::size_t) (w::mipLength (m) + 1) * sizeof (float);
    // Harmonic Ladder's partials stop at 64: its four 4096-sample mips (1023 … 361 partials) hold the same frames, and so do
    // its two 2048 and its two 1024 ones; mips of different lengths never share
    const auto& ladder = w::table (w::tableByName ("Harmonic Ladder"));
    bool shared = true;
    for (int f = 0; f < w::frameCount; ++f)
        shared = shared && ladder.frame (0, f) == ladder.frame (3, f) && ladder.frame (4, f) == ladder.frame (5, f)
                 && ladder.frame (6, f) == ladder.frame (7, f) && ladder.frame (3, f) != ladder.frame (4, f);
    CHECK (shared);
    CHECK (ladder.bytes() < full / 3);
    // Pulse Sweep steps through 9 widths: 9 frames per mip, not 64
    const auto& steps = w::table (w::tableByName ("Pulse Sweep"));
    std::set<const float*> distinct;
    for (int f = 0; f < w::frameCount; ++f)
        distinct.insert (steps.frame (0, f));
    CHECK (distinct.size() == 9);
    // a full-band, ever-changing table keeps every frame (Shapes)
    CHECK (w::table (w::tableByName ("Shapes")).bytes() > full / 10 * 9);
    // all the tables: under 80 % of the old layout (≈ 74 %)
    std::size_t all = 0;
    for (int t = 0; t < w::tableCount(); ++t)
        all += w::table (t).bytes();
    CHECK (all < full * (std::size_t) w::tableCount() / 5 * 4);
}

TEST (helix190ja_tuning_and_levels)
{
    using namespace lynx;
    using namespace helix190ja;
    const double fs = 48000.0;
    // A4 on the sine frame: 440 Hz to a hundredth of a cent, at 44.1 / 48 / 96 kHz
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto x = render (base(), 69, 1.0, rate);
        const auto hz = frequency (x, (int) (0.1 * rate), (int) (0.7 * rate), rate);
        CHECK (std::fabs (1200.0 * std::log2 (hz / 440.0)) < 0.01);
    }
    // transpose + detune (+7 st, +25 cents), the global transpose (+12)
    auto s = base();
    s[(std::size_t) (w::oscBase (0) + w::osc::transpose)] = 7.0;
    s[(std::size_t) (w::oscBase (0) + w::osc::detune)] = 0.25;
    auto x = render (s, 69, 1.0);
    CHECK (std::fabs (1200.0 * std::log2 (frequency (x, 4800, 33600, fs) / 440.0) - 725.0) < 0.02);
    s = base();
    s[(std::size_t) w::transpose] = 12.0;
    x = render (s, 57, 1.0);
    CHECK (std::fabs (frequency (x, 4800, 33600, fs) - 440.0) < 0.01);
    // high and low notes stay in tune (C1 … C8)
    for (const int key : { 24, 48, 84, 108 })
    {
        const auto want = 440.0 * std::pow (2.0, (key - 69) / 12.0);
        x = render (base(), key, 1.0);
        CHECK (std::fabs (1200.0 * std::log2 (frequency (x, 4800, 33600, fs) / want)) < 0.05);
    }
    // gain and volume are linear amplitudes; the sine's level is the table's
    s = base();
    const auto full = rms (render (s, 60, 0.5), 4800, 19200);
    s[(std::size_t) (w::oscBase (0) + w::osc::gain)] = 0.5;
    CHECK (std::fabs (db (rms (render (s, 60, 0.5), 4800, 19200) / full) + 6.02) < 0.05);
    s = base();
    s[(std::size_t) w::volume] = 0.25;
    CHECK (std::fabs (db (rms (render (s, 60, 0.5), 4800, 19200) / full) + 12.04) < 0.05);
    // pan (Live's law [MEASURED 190ja a3, a4]): hard left silences the right and doubles the left (+6 dB over the
    // centre's −3 dB); at 0.5 the far side is at half, the near one at 1.207 × the centre's 0.707
    s = base();
    s[(std::size_t) (w::oscBase (0) + w::osc::pan)] = -1.0;
    x = render (s, 60, 0.5);
    CHECK (rms (x, 4800, 19200, 2, 1) < 1e-6 && std::fabs (db (rms (x, 4800, 19200) / full) - 6.02) < 0.05);
    s[(std::size_t) (w::oscBase (0) + w::osc::pan)] = 0.5;
    x = render (s, 60, 0.5);
    CHECK (std::fabs (db (rms (x, 4800, 19200) / full) - db (0.5 / 0.70710678)) < 0.05);
    CHECK (std::fabs (db (rms (x, 4800, 19200, 2, 1) / full) - db (0.70710678 * (1.0 + std::sqrt (0.5)) / 0.70710678)) < 0.05);
}

TEST (helix190ja_no_aliasing_at_high_notes)
{
    using namespace lynx;
    using namespace helix190ja;
    const double fs = 48000.0;
    // the saw frame (Shapes at 2/3) up to C8: everything outside the harmonics stays below −55 dB (C4) / −70 dB (C7, C8)
    auto s = base();
    s[(std::size_t) (w::oscBase (0) + w::osc::position)] = 2.0 / 3.0;
    for (const auto [key, limit] : { std::pair { 72, -55.0 }, std::pair { 96, -70.0 }, std::pair { 108, -70.0 } })
    {
        const auto x = render (s, key, 0.5);
        const auto f0 = 440.0 * std::pow (2.0, (key - 69) / 12.0);
        const auto v = nonHarmonicDb (x, 2400, 2400 + 9600, f0, fs);
        std::printf ("  saw at key %d: %.1f dB outside the harmonics\n", key, v);
        CHECK (v < limit);
    }
    // the square (the table's brightest frame) at C7, and the phase effects at C7: still clean
    s[(std::size_t) (w::oscBase (0) + w::osc::position)] = 1.0;
    CHECK (nonHarmonicDb (render (s, 96, 0.5), 2400, 12000, 2093.0, fs) < -70.0);
    struct Fx
    {
        int mode;
        double e1, e2, limit;
    };
    for (const auto fx : { Fx { w::effectClassic, 0.6, 0.0, -50.0 }, Fx { w::effectModern, 0.8, 0.0, -50.0 }, Fx { w::effectFm, 0.0, 0.5, -50.0 },
                           Fx { w::effectClassic, 0.0, 0.6, -40.0 }, Fx { w::effectModern, 0.0, 0.5, -40.0 } })
    {
        auto e = base();
        e[(std::size_t) (w::oscBase (0) + w::osc::position)] = 2.0 / 3.0;
        e[(std::size_t) (w::oscBase (0) + w::osc::effectMode)] = fx.mode;
        e[(std::size_t) (w::oscBase (0) + w::osc::effect1)] = fx.e1;
        e[(std::size_t) (w::oscBase (0) + w::osc::effect2)] = fx.e2;
        const auto v = nonHarmonicDb (render (e, 96, 0.5), 2400, 12000, 2093.0, fs);
        if (v >= fx.limit)
            std::printf ("  effect %d (%.1f, %.1f): %.1f dB outside the harmonics\n", fx.mode, fx.e1, fx.e2, v);
        CHECK (v < fx.limit);
    }
}

TEST (helix190ja_deterministic_and_state_round_trip)
{
    using namespace lynx;
    using namespace helix190ja;
    // the same state and notes give the same samples, bit for bit (unison with random phases and Random Note included)
    auto s = base();
    s[(std::size_t) w::unisonMode] = w::unisonRandomNote;
    s[(std::size_t) w::unisonVoices] = 5.0;
    s[(std::size_t) w::unisonAmount] = 0.7;
    s[(std::size_t) w::matrixIndex (w::tgt::pitch, w::src::random)] = 0.02;
    s[(std::size_t) (w::lfoBase (0) + w::lfo::type)] = 4.0;
    s[(std::size_t) w::matrixIndex (w::tgt::oscPos, w::src::lfo1)] = 0.4;
    const auto a = render (s, 60, 1.0), b = render (s, 60, 1.0);
    CHECK (a == b && rms (a, 4800, 38400) > 0.05);
    // a preset (the generator's state as text) round-trips every value and plays the same
    ChannelDef c;
    c.instrument = InstrumentKind::helix;
    c.modelParams.assign (s.begin(), s.end());
    c.modelParams[(std::size_t) w::tableBase] = (float) w::tableByName ("Vowels");
    c.modelParams[(std::size_t) w::matrixIndex (w::tgt::filterFreq, w::src::env2)] = -0.375f;
    const auto text = genpresets::toText ("Wide Vowels", c);
    const auto back = genpresets::fromText (text);
    CHECK (back.has_value() && back->kind == InstrumentKind::helix && back->sound.modelParams == c.modelParams);
    if (back)
    {
        const auto sa = helixState (c), sb = helixState (back->sound);
        CHECK (render (sa, 64, 0.5) == render (sb, 64, 0.5));
    }
}

TEST (helix190ja_modulation_mpe_and_voices)
{
    using namespace lynx;
    using namespace helix190ja;
    const double fs = 48000.0;
    // Pitch ← Velocity: 1 = 48 semitones (the Pitch row's unit [DATA: Pitch ← Pitch Bend 1/24 = 2 st])
    auto s = base();
    s[(std::size_t) w::matrixIndex (w::tgt::pitch, w::src::velocity)] = 0.25;
    w::RefNote n;
    n.key = 45;
    n.velocity = 1.0f;
    auto x = w::renderReference (s, { n }, fs, 24000);
    CHECK (std::fabs (1200.0 * std::log2 (frequency (x, 4800, 24000, fs) / 220.0)) < 0.1); // (A2 + 12 st)
    // the instrument: MPE per-note pitch (Pitch ← Note PB 1), slide moves the position through the matrix, pressure too
    auto inst = makeInstrument (InstrumentKind::helix);
    CHECK (inst != nullptr);
    if (inst == nullptr)
        return;
    ChannelDef c;
    c.instrument = InstrumentKind::helix;
    c.modelParams.assign (s.begin(), s.end());
    c.modelParams[(std::size_t) w::matrixIndex (w::tgt::pitch, w::src::velocity)] = 0.0f;
    c.modelParams[(std::size_t) w::matrixIndex (w::tgt::oscPos, w::src::slide)] = 1.0f;
    inst->prepare (fs);
    inst->configure (c, 120.0);
    std::vector<float> L (24000, 0.0f), R (24000, 0.0f), I (48000);
    auto interleave = [&] {
        for (int i = 0; i < 24000; ++i)
            I[(std::size_t) (2 * i)] = L[(std::size_t) i], I[(std::size_t) (2 * i + 1)] = R[(std::size_t) i];
    };
    inst->noteOn (69, 1.0f);
    inst->render (L.data(), R.data(), 24000, 1.0f, 1.0f);
    interleave();
    CHECK (std::fabs (frequency (I, 4800, 24000, fs) - 440.0) < 0.05);
    const auto sineSecond = partial (I, 4800, 24000, 880.0, fs);
    inst->noteExpression (69, 1, 1200.0f, 0.0f); // +12 semitones on this note only
    std::fill (L.begin(), L.end(), 0.0f), std::fill (R.begin(), R.end(), 0.0f);
    inst->render (L.data(), R.data(), 24000, 1.0f, 1.0f);
    interleave();
    CHECK (std::fabs (frequency (I, 4800, 24000, fs) - 880.0) < 0.1);
    inst->noteExpression (69, 1, 0.0f, 0.0f);
    inst->voiceProperty (69, 3, 0.6f); // slide 0.6 → position 0.6 (mostly the saw frame)
    std::fill (L.begin(), L.end(), 0.0f), std::fill (R.begin(), R.end(), 0.0f);
    inst->render (L.data(), R.data(), 24000, 1.0f, 1.0f);
    interleave();
    CHECK (partial (I, 4800, 24000, 880.0, fs) > 50.0 * std::max (sineSecond, 1e-6)); // (the sine frame had no 2nd partial)
    inst->noteOff (69);
    for (int i = 0; i < 20; ++i)
        inst->render (L.data(), R.data(), 24000, 1.0f, 1.0f);
    CHECK (inst->idle());
    // automation (0…1 over the parameter's range)
    const auto* gain = modelparams::find (InstrumentKind::helix, w::oscBase (0) + w::osc::gain);
    CHECK (gain != nullptr && std::fabs (gain->raw (0.5f) - 0.5f) < 1e-6f);
    // voices: the cap steals; Mono plays one voice, legato
    w::Engine e;
    e.prepare (fs);
    auto v = base();
    v[(std::size_t) w::polyVoices] = 0.0; // 2 voices
    e.setState (v.data());
    std::vector<float> l (480), r (480);
    for (const auto key : { 60, 64, 67 })
    {
        e.note (key, 1.0f);
        e.render (l.data(), r.data(), 480, 120.0, -1.0);
    }
    CHECK (e.activeVoices() == 2);
    v[(std::size_t) w::monoPoly] = 0.0;
    e.setState (v.data());
    e.reset();
    for (const auto key : { 60, 64, 67 })
    {
        e.note (key, 1.0f);
        e.render (l.data(), r.data(), 480, 120.0, -1.0);
    }
    CHECK (e.activeVoices() == 1);
}

TEST (helix190ja_wavetable_imports_and_round_trips)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    namespace w = wt;
    // a Wavetable as Live stores it: parameters (Manual), menus (Value), tables by name, the 52 × 13 matrix
    auto par = [] (const std::string& n, const std::string& v) { return "<" + n + "><Manual Value=\"" + v + "\" /></" + n + ">"; };
    auto val = [] (const std::string& n, const std::string& v) { return "<" + n + " Value=\"" + v + "\" />"; };
    std::string d = "<InstrumentVector Id=\"0\">" + par ("On", "true") + val ("SpriteName1", "Basic Shapes") + val ("SpriteName2", "FOF") +
                    "<UserSprite1><Value /></UserSprite1><UserSprite2><Value /></UserSprite2><ModulationConnections>";
    for (int t = 0; t < 52; ++t)
    {
        d += "<ModulationConnectionsForInstrumentVector>" + val ("TargetName", w::targetName (t, 3, 1));
        for (int k = 0; k < 13; ++k)
            d += val ("ModulationAmounts." + std::to_string (k), t == w::tgt::filterFreq && k == w::src::env2 ? "0.5"
                                                                 : t == w::tgt::pitch && k == w::src::pitchBend ? "0.25" : "0");
        d += "</ModulationConnectionsForInstrumentVector>";
    }
    d += "</ModulationConnections>" + par ("Voice_Oscillator1_On", "true") + par ("Voice_Oscillator1_Wavetables_WavePosition", "0.375") +
         val ("Voice_Oscillator1_Effects_EffectMode", "3") + par ("Voice_Oscillator1_Effects_Effect1", "-0.25") + par ("Voice_Oscillator2_On", "true") +
         val ("Voice_Oscillator2_Effects_EffectMode", "1") + par ("Voice_Filter1_Frequency", "880") + par ("Voice_Filter1_Type", "2") +
         par ("Voice_Modulators_Envelope2_Values_Peak", "0.75") + par ("Voice_Modulators_Lfo1_Retrigger", "false") + val ("Voice_Unison_Mode", "4") +
         val ("Voice_Unison_VoiceCount", "6") + val ("Voice_Global_FilterRouting", "2") + val ("MonoPoly", "0") + par ("Volume", "0.5") + "</InstrumentVector>";
    xml::Document doc;
    CHECK (doc.parse (d));
    ChannelDef c;
    Notes notes;
    CHECK (wavetableToHelix (doc.root(), c, notes));
    CHECK (c.instrument == InstrumentKind::helix && (int) c.modelParams.size() == w::stateSize);
    const auto v = [&] (int i) { return (double) c.modelParams[(std::size_t) i]; };
    CHECK (v (w::oscBase (0) + w::osc::position) == 0.375 && v (w::oscBase (0) + w::osc::effectMode) == 3.0 && v (w::oscBase (0) + w::osc::effect1) == -0.25);
    CHECK (v (w::oscBase (1) + w::osc::on) == 1.0 && v (w::oscBase (1) + w::osc::effectMode) == 1.0);
    CHECK (v (w::filterBase (0) + w::flt::freq) == 880.0 && v (w::filterBase (0) + w::flt::type) == 2.0);
    CHECK (v (w::envBase (1) + w::env::peak) == 0.75 && v (w::lfoBase (0) + w::lfo::retrigger) == 0.0);
    CHECK (v (w::unisonMode) == 4.0 && v (w::unisonVoices) == 6.0 && v (w::routing) == 2.0 && v (w::monoPoly) == 0.0 && v (w::volume) == 0.5);
    CHECK (v (w::matrixIndex (w::tgt::filterFreq, w::src::env2)) == 0.5 && v (w::matrixIndex (w::tgt::oscPos, w::src::modWheel)) == 0.0);
    CHECK (std::string (w::tableName ((int) v (w::tableBase))) == "Shapes");
    CHECK (c.misc.pitchRange == 12); // (Pitch ← Pitch Bend 0.25 = 12 semitones)
    CHECK (std::any_of (notes.approximated.begin(), notes.approximated.end(), [] (const std::string& n) { return n.find ("\"FOF\"") != std::string::npos; }));
    // a user wave table is listed
    auto user = d;
    user.replace (user.find ("<UserSprite1><Value /></UserSprite1>"), 36, "<UserSprite1><Value><SampleRef /></Value></UserSprite1>");
    xml::Document doc2;
    CHECK (doc2.parse (user));
    ChannelDef c2;
    Notes n2;
    CHECK (wavetableToHelix (doc2.root(), c2, n2) && ! n2.dropped.empty());
    // the imported channel plays
    auto inst = makeInstrument (c.instrument);
    inst->prepare (48000.0);
    inst->configure (c, 120.0);
    std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
    inst->noteOn (60, 1.0f);
    inst->render (l.data(), r.data(), 24000, 1.0f, 1.0f);
    CHECK (helix190ja::rms (l, 2400, 24000, 1) > 1e-3);
    // not a Wavetable: false
    xml::Document doc3;
    CHECK (doc3.parse ("<Operator Id=\"1\"><Globals/></Operator>"));
    ChannelDef c3;
    Notes n3;
    CHECK (! wavetableToHelix (doc3.root(), c3, n3));

    // .als round trip: a song with a Helix channel → Live 12 set (a Wavetable device) → imported again: every value back
    Song song;
    song.bpm = 120.0;
    ChannelDef h;
    h.name = "Lead";
    h.instrument = InstrumentKind::helix;
    h.modelParams = c.modelParams;
    h.modelParams[(std::size_t) w::tableBase + 1] = (float) w::tableByName ("Fold Saw"); // (no Live table maps onto it: the info text carries it)
    h.modelParams[(std::size_t) (w::lfoBase (1) + w::lfo::rate)] = 3.25f;
    h.misc.pitchRange = 7;
    song.channels.push_back (h);
    Pattern p;
    Note note;
    note.channel = 0, note.key = 60, note.start = 0, note.length = ppq;
    p.notes.push_back (note);
    song.patterns.push_back (p);
    Clip clip;
    clip.pattern = 0, clip.start = 0, clip.length = 4 * ppq, clip.track = 0;
    song.playlist.push_back (clip);
    const auto out = exportAls (song);
    CHECK (out.xml.find ("<InstrumentVector") != std::string::npos);
    CHECK (std::none_of (out.notes.begin(), out.notes.end(), [] (const std::string& n) { return n.find ("Lead") != std::string::npos; }));
    AlsImportOptions options;
    std::string error;
    const auto again = importAls (out.bytes, options, &error);
    CHECK (again.has_value());
    if (! again)
        return;
    const ChannelDef* back = nullptr;
    for (const auto& ch : again->channels)
        if (ch.instrument == InstrumentKind::helix)
            back = &ch;
    CHECK (back != nullptr);
    if (back == nullptr)
        return;
    CHECK ((int) back->modelParams.size() == w::stateSize);
    int differ = 0;
    for (int i = 0; i < w::stateSize; ++i)
    {
        auto want = h.modelParams[(std::size_t) i];
        if (i == w::matrixIndex (w::tgt::pitch, w::src::pitchBend))
            want = 7.0f / 48.0f; // (the channel's range goes out through the matrix)
        if (std::fabs (back->modelParams[(std::size_t) i] - want) > 1e-6f * std::max (1.0f, std::fabs (want)))
        {
            if (differ < 5)
                std::printf ("  round trip: value %d (%s) %g -> %g\n", i, i < w::paramCount ? w::paramName (i).c_str() : "", want,
                             back->modelParams[(std::size_t) i]);
            ++differ;
        }
    }
    CHECK (differ == 0 && back->misc.pitchRange == 7);
}
