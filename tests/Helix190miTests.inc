// Round 190mi: Helix+ (core/WavetableSynth.h), LYNX's own extension of Helix after Live Wavetable's values: second
// warps, unison count / blend / width / stack, noise, Comb / Formant / Phaser filters, macros, drawn LFOs, the effects
// rack, the factory bank (core/HelixBank.cpp). Uses Helix190jaTests.inc's helpers (helix190ja::). No data file.

#include "core/LynxBanks.h"

namespace helix190mi
{
namespace w = lynx::wt;
using helix190ja::db;
using helix190ja::rms;

inline std::uint64_t hash (const std::vector<float>& x)
{
    std::uint64_t h = 1469598103934665603ull;
    for (auto f : x)
    {
        std::uint32_t b;
        std::memcpy (&b, &f, 4);
        h = (h ^ b) * 1099511628211ull;
    }
    return h;
}
// a Live-layout patch (the 778 values of a project before 190mi) with some of Live's own features in use
inline std::vector<double> livePatch (int variant)
{
    auto s = helix190ja::base();
    s.resize ((std::size_t) w::liveStateSize);
    s[(std::size_t) w::tableBase] = (double) w::tableByName ("Analog Saw");
    s[(std::size_t) w::volume] = 0.3;
    s[(std::size_t) w::unisonMode] = w::unisonClassic + variant % 3;
    s[(std::size_t) w::unisonVoices] = 3 + variant;
    s[(std::size_t) w::unisonAmount] = 0.4;
    s[(std::size_t) w::oscBase (0) + w::osc::effectMode] = variant % 4;
    s[(std::size_t) w::oscBase (0) + w::osc::effect1] = 0.3;
    s[(std::size_t) w::oscBase (0) + w::osc::effect2] = 0.4;
    s[(std::size_t) w::filterBase (0) + w::flt::on] = 1.0;
    s[(std::size_t) w::filterBase (0) + w::flt::freq] = 1500.0;
    s[(std::size_t) w::matrixIndex (w::tgt::filterFreq, w::src::lfo1)] = 0.3;
    return s;
}
// the patch with Helix+'s block (revision `rev`, every value at its default)
inline std::vector<double> plus (std::vector<double> s, double rev = 1.0)
{
    s.resize ((std::size_t) w::stateSize);
    for (int i = w::extBase; i < w::stateSize; ++i)
        s[(std::size_t) i] = w::defaults()[(std::size_t) i];
    s[(std::size_t) w::extBase] = rev;
    return s;
}
inline void x (std::vector<double>& s, int extIndex, double v) { s[(std::size_t) (w::extBase + extIndex)] = v; }
inline std::vector<float> note (const std::vector<double>& s, int key = 48, double seconds = 0.5, double fs = 48000.0)
{
    return helix190ja::render (s, key, seconds, fs);
}
inline bool finite (const std::vector<float>& v)
{
    return std::all_of (v.begin(), v.end(), [] (float f) { return std::isfinite (f) && std::fabs (f) < 8.0f; });
}
inline double sideDifference (const std::vector<float>& v)
{
    double d = 0.0;
    for (std::size_t i = 0; i + 1 < v.size(); i += 2)
        d += std::fabs ((double) v[i] - v[i + 1]);
    return d;
}
} // namespace helix190mi

TEST (helix190mi_layout_and_parameters)
{
    using namespace lynx;
    namespace w = wt;
    // Live's values stay where they were; Helix+'s block follows them
    CHECK (w::liveStateSize == 778 && w::extBase == w::liveStateSize && w::ext2Base == w::extBase + w::ext::count); // (190ml: Helix+ 2 follows)
    CHECK (w::ext::points == w::ext::autoEnd && w::ext::macroCells == w::ext::points + 2 * w::ext::lfoPointsSize);
    std::set<int> cells;
    for (int t = 0; t < w::allTargetCount; ++t)
        for (int s = 0; s < w::allSourceCount; ++s)
        {
            const auto i = w::cellIndex (t, s);
            CHECK (i >= w::matrixBase && i < w::stateSize && (i < w::liveStateSize || i >= w::extBase + w::ext::macroCells));
            cells.insert (i);
        }
    CHECK ((int) cells.size() == w::allTargetCount * w::allSourceCount);
    CHECK (w::cellIndex (3, 4) == w::matrixIndex (3, 4));
    // the parameter table stays sorted by index (findEngineParam) and finds Helix+'s values
    const auto& t = helixEngineParams();
    CHECK (std::is_sorted (t.begin(), t.end(), [] (const EngineParam& a, const EngineParam& b) { return a.index < b.index; }));
    const auto* drive = findEngineParam (t, w::extBase + w::ext::distDrive);
    CHECK (drive != nullptr && drive->name == "Distortion Drive" && w::automatable (drive->index));
    CHECK (! w::automatable (w::extBase) && ! w::automatable (w::tableBase) && ! w::automatable (w::extBase + w::ext::points));
    CHECK (modelparams::findByName (InstrumentKind::helix, "Osc 1 Warp 2 Amount") != nullptr);
    // revision: 0 in the defaults (a project before 190mi reads its missing block so), the current one for a new channel
    CHECK (w::defaults()[(std::size_t) w::extBase] == 0.0 && helixDefaults()[(std::size_t) w::extBase] == (float) w::currentRevision);
    CHECK ((int) helixDefaults().size() == w::stateSize);
    // a Helix+ value set on an old channel makes the block live; one of Live's values leaves it as it was
    ChannelDef old;
    old.instrument = InstrumentKind::helix;
    old.modelParams.assign ((std::size_t) w::liveStateSize, 0.25f);
    auto a = old;
    CHECK (modelparams::setRawValue (a, w::volume, 0.5f) && (int) a.modelParams.size() == w::stateSize && a.modelParams[(std::size_t) w::extBase] == 0.0f);
    CHECK (a.modelParams[(std::size_t) w::liveStateSize - 1] == 0.25f);
    CHECK (modelparams::setRawValue (a, w::extBase + w::ext::macro, 0.7f) && a.modelParams[(std::size_t) w::extBase] == (float) w::currentRevision);
    // texts and names
    CHECK (w::paramText (w::extBase + w::ext::warpMode, w::warp::fm) == "FM <- Osc" && w::paramText (w::extBase + w::ext::unisonCount, 0) == "As device");
    CHECK (w::paramText (w::extBase + w::ext::delayTime, 5) == "1/8 D" && w::paramText (w::extBase + w::ext::filterType, 3) == "Formant");
    CHECK (w::sourceName (w::src::macro1 + 3) == "Macro 4" && w::targetName (w::tgt::noiseLevel) == "Noise Level");
    // every knob a source can be dropped on names a row
    int mapped = 0;
    for (const auto& p : t)
        if (const auto row = w::targetOfParam (p.index); row >= 0)
        {
            ++mapped;
            CHECK (row < w::allTargetCount && ! w::targetName (row).empty());
        }
    CHECK (mapped >= 50);
    CHECK (w::targetOfParam (w::filterBase (1) + w::flt::freq) == w::tgt::filterFreq + w::tgt::filterStride);
    CHECK (w::targetOfParam (w::extBase + w::ext::warpAmount + w::ext::warpStride) == w::tgt::warpAmount + 1);
    CHECK (w::targetOfParam (w::routing) == -1);
    // the instrument takes Helix+'s automation (by the engine's own index) and plays
    auto inst = makeHelixInstrument();
    inst->prepare (48000.0);
    ChannelDef h;
    h.instrument = InstrumentKind::helix;
    h.modelParams = helixDefaults();
    h.modelParams[(std::size_t) (w::extBase + w::ext::distOn)] = 1.0f;
    inst->configure (h, 120.0);
    inst->setParameter (w::extBase + w::ext::distDrive, 0.8f);
    inst->setParameter (w::extBase + w::ext::points, 0.5f); // (not automatable: ignored)
    inst->setParameter (w::stateSize + 5, 0.5f);
    inst->noteOn (60, 0.8f);
    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
    inst->render (l.data(), r.data(), 4800, 1.0f, 1.0f);
    CHECK (helix190mi::finite (l) && helix190ja::rms (l, 0, 4800, 1) > 1e-3);
}

TEST (helix190mi_old_projects_bit_identical)
{
    using namespace helix190mi;
    for (int variant = 0; variant < 6; ++variant)
    {
        const auto live = livePatch (variant);
        const auto before = hash (note (live));
        // revision 1 with every Helix+ value at its default: as before
        CHECK (hash (note (plus (live, 1.0))) == before);
        // revision 0 ignores the block, whatever it holds (every effect on, warps, 16 voices, filters, drawn LFOs)
        auto junk = plus (live, 0.0);
        for (int i = 1; i < w::ext::autoEnd; ++i)
            junk[(std::size_t) (w::extBase + i)] = (i % 3 == 0) ? 1.0 : 0.6 + 0.01 * i;
        x (junk, w::ext::unisonCount, 16);
        x (junk, w::ext::warpMode, w::warp::fm);
        x (junk, w::ext::filterType, w::xfilter::combPlus);
        junk[(std::size_t) w::cellIndex (w::tgt::oscPos, w::src::macro1)] = 1.0;
        CHECK (hash (note (junk)) == before);
        // and with revision 1 the same block changes the sound
        junk[(std::size_t) w::extBase] = 1.0;
        CHECK (hash (note (junk)) != before);
    }
}

TEST (helix190mi_warps)
{
    using namespace helix190mi;
    // the phase laws: inside 0…1, ends kept where the mode keeps them
    for (int m = w::warp::off; m < w::warp::count; ++m)
        for (double a : { 0.0, 0.3, 1.0 })
        {
            CHECK (w::warpStretch (m, a, 2.0) >= 1.0 && w::warpStretch (m, a, 2.0) <= 64.0);
            for (double xx = 0.0; xx < 1.0; xx += 0.01)
            {
                const auto y = w::warpPhase (m, a, xx);
                CHECK (y >= 0.0 && y <= 1.0);
            }
        }
    CHECK (std::fabs (w::warpPhase (w::warp::bendPlus, 0.7, 0.5) - 0.5) < 1e-12 && w::warpPhase (w::warp::bendPlus, 0.7, 0.25) > 0.25);
    CHECK (w::warpPhase (w::warp::bendMinus, 0.7, 0.25) < 0.25);
    CHECK (std::fabs (w::warpPhase (w::warp::asymPlus, 1.0, 0.98) - 0.5) < 1e-9 && w::warpPhase (w::warp::pwm, 1.0, 0.5) == 1.0);
    CHECK (std::fabs (w::warpPhase (w::warp::sync, 1.0, 0.5) - 0.0) < 1e-9 && std::fabs (w::warpPhase (w::warp::sync, 0.0, 0.3) - 0.3) < 1e-12);
    CHECK (w::warpStretch (w::warp::fm, 1.0, 1.0) > w::warpStretch (w::warp::fm, 0.2, 1.0));
    // each warp changes a saw, plays clean (finite, no fold-over: the energy between the harmonics stays far below them)
    auto base = plus (livePatch (0));
    base[(std::size_t) w::unisonMode] = w::unisonNone;
    base[(std::size_t) w::oscBase (0) + w::osc::effectMode] = w::effectNone;
    base[(std::size_t) w::filterBase (0) + w::flt::on] = 0.0;
    base[(std::size_t) w::matrixIndex (w::tgt::filterFreq, w::src::lfo1)] = 0.0;
    base[(std::size_t) w::oscBase (1) + w::osc::on] = 0.0;
    base[(std::size_t) w::tableBase + 1] = (double) w::tableByName ("Shapes"); // (a sine at position 0: the FM / AM / RM modulator)
    base[(std::size_t) w::oscBase (1) + w::osc::transpose] = 12.0;
    const auto plain = note (base, 45, 0.4);
    for (int m = 1; m < w::warp::count; ++m)
    {
        auto s = base;
        x (s, w::ext::warpMode, m);
        x (s, w::ext::warpAmount, 0.6);
        const auto out = note (s, 45, 0.4);
        CHECK (finite (out) && hash (out) != hash (plain));
        CHECK (rms (out, 4800, 14400) > 0.01);
        const auto alias = helix190ja::nonHarmonicDb (out, 4800, 14400, 110.0, 48000.0);
        // the smooth warps stay clean; the ones that cut an edge into the cycle (Sync, PWM's gap, Mirror's turn, Flip,
        // Crush's steps) fold a little, as in every warp synth (measured 190mi: −31 … −40 dB at A2)
        const bool edges = m == w::warp::crush || m == w::warp::flip || m == w::warp::sync || m == w::warp::windowSync || m == w::warp::pwm
                           || m == w::warp::mirror;
        CHECK (alias < (edges ? -28.0 : -40.0));
        if (alias >= -40.0)
            std::printf ("  warp %d: %.1f dB between the harmonics\n", m, alias);
    }
    // FM / AM / RM read oscillator 2 even while it is silent; with no table there is nothing to read
    auto fm = base;
    x (fm, w::ext::warpMode, w::warp::fm);
    x (fm, w::ext::warpAmount, 0.5);
    auto fm2 = fm;
    fm2[(std::size_t) w::oscBase (1) + w::osc::transpose] = 7.0; // (an inharmonic modulator: energy between the harmonics)
    CHECK (helix190ja::nonHarmonicDb (note (fm2, 45, 0.4), 4800, 14400, 110.0, 48000.0) > helix190ja::nonHarmonicDb (note (fm, 45, 0.4), 4800, 14400, 110.0, 48000.0) + 10.0);
    // the amount row of the matrix (Helix+'s target) moves the warp: Macro 1 → Warp 2 amount
    auto mac = base;
    x (mac, w::ext::warpMode, w::warp::bendPlus);
    mac[(std::size_t) w::cellIndex (w::tgt::warpAmount, w::src::macro1)] = 1.0;
    const auto still = note (mac, 45, 0.3);
    x (mac, w::ext::macro, 0.8);
    CHECK (hash (note (mac, 45, 0.3)) != hash (still));
    auto direct = base;
    x (direct, w::ext::warpMode, w::warp::bendPlus);
    x (direct, w::ext::warpAmount, 0.8);
    CHECK (hash (note (direct, 45, 0.3)) == hash (note (mac, 45, 0.3))); // (the same amount, by the knob or the macro)
}

TEST (helix190mi_unison_noise_filters)
{
    using namespace helix190mi;
    auto s = plus (livePatch (0));
    s[(std::size_t) w::oscBase (0) + w::osc::effectMode] = w::effectNone;
    s[(std::size_t) w::filterBase (0) + w::flt::on] = 0.0;
    s[(std::size_t) w::matrixIndex (w::tgt::filterFreq, w::src::lfo1)] = 0.0;
    s[(std::size_t) w::unisonMode] = w::unisonClassic;
    s[(std::size_t) w::unisonVoices] = 8;
    const auto eight = note (s, 48);
    // 16 voices: a different sound at about the same level
    auto sixteen = s;
    x (sixteen, w::ext::unisonCount, 16);
    const auto u16 = note (sixteen, 48);
    CHECK (finite (u16) && hash (u16) != hash (eight) && std::fabs (db (rms (u16, 4800, 19200)) - db (rms (eight, 4800, 19200))) < 3.0);
    // Width 0: both sides the same
    auto mono = s;
    x (mono, w::ext::unisonWidth, 0.0);
    CHECK (sideDifference (note (mono, 48)) < 1e-3 && sideDifference (eight) > 1.0);
    // Blend 0 with an odd count: only the centre voice, in the middle → a plain saw (no beating), at the summed level
    auto blend = s;
    s[(std::size_t) w::unisonVoices] = 5;
    blend[(std::size_t) w::unisonVoices] = 5;
    x (blend, w::ext::unisonBlend, 0.0);
    const auto b0 = note (blend, 48);
    CHECK (finite (b0) && sideDifference (b0) < 1e-3);
    const auto blendDb = db (rms (b0, 4800, 19200)) - db (rms (note (s, 48), 4800, 19200));
    CHECK (std::fabs (blendDb) < 4.0);
    if (std::fabs (blendDb) >= 4.0)
        std::printf ("  blend 0 vs 1: %.2f dB\n", blendDb);
    // Stack +1 oct: the octave partial grows (no detune, so the stacked voices sit on the octave exactly)
    s[(std::size_t) w::unisonAmount] = 0.0;
    s[(std::size_t) w::tableBase] = (double) w::tableByName ("Shapes"); // (a sine: no octave of its own)
    auto stack = s;
    x (stack, w::ext::unisonStack, 1);
    const auto st = note (stack, 48);
    const auto f0 = 130.8128;
    CHECK (helix190ja::partial (note (s, 48), 4800, 19200, 2 * f0, 48000.0) < 1e-3 && helix190ja::partial (st, 4800, 19200, 2 * f0, 48000.0) > 0.03);
    const std::vector<float> stRight (st.begin() + 1, st.end()); // (the right side at the even indices)
    CHECK (helix190ja::partial (stRight, 4800, 19200, 2 * f0, 48000.0) > 0.03); // (both sides hear the octave)
    // noise: alone it sounds; dark has less top than bright
    auto n = plus (livePatch (0));
    n[(std::size_t) w::oscBase (0) + w::osc::on] = 0.0;
    n[(std::size_t) w::filterBase (0) + w::flt::on] = 0.0;
    CHECK (rms (note (n), 4800, 19200) == 0.0);
    x (n, w::ext::noiseOn, 1.0);
    x (n, w::ext::noiseColour, 0.1);
    const auto dark = note (n);
    x (n, w::ext::noiseColour, 0.9);
    const auto bright = note (n);
    CHECK (rms (dark, 4800, 19200) > 1e-3 && rms (bright, 4800, 19200) > 1e-3 && finite (bright));
    CHECK (helix190ja::partial (bright, 4800, 19200, 9000.0, 48000.0) > 4.0 * helix190ja::partial (dark, 4800, 19200, 9000.0, 48000.0));
    // filters: Comb + rings at its frequency's harmonics on noise; Formant and Phaser play clean
    x (n, w::ext::noiseColour, 0.5);
    n[(std::size_t) w::matrixIndex (w::tgt::filterFreq, w::src::lfo1)] = 0.0;
    n[(std::size_t) w::filterBase (0) + w::flt::on] = 1.0;
    n[(std::size_t) w::filterBase (0) + w::flt::freq] = 400.0;
    n[(std::size_t) w::filterBase (0) + w::flt::res] = 1.2;
    x (n, w::ext::filterType, w::xfilter::combPlus);
    const auto comb = note (n);
    CHECK (finite (comb) && helix190ja::partial (comb, 4800, 19200, 800.0, 48000.0) > 3.0 * helix190ja::partial (comb, 4800, 19200, 600.0, 48000.0));
    x (n, w::ext::filterType, w::xfilter::combMinus); // (negative feedback: the peaks at the odd half-multiples)
    const auto combMinus = note (n);
    CHECK (finite (combMinus) && helix190ja::partial (combMinus, 4800, 19200, 600.0, 48000.0) > 3.0 * helix190ja::partial (combMinus, 4800, 19200, 800.0, 48000.0));
    for (int type : { w::xfilter::formant, w::xfilter::phaser })
        for (int route : { w::routeSerial, w::routeParallel, w::routeSplit })
        {
            auto f = plus (livePatch (1));
            f[(std::size_t) w::routing] = route;
            f[(std::size_t) w::filterBase (1) + w::flt::on] = 1.0;
            x (f, w::ext::filterType, type);
            x (f, w::ext::filterType + 1, w::xfilter::combMinus);
            const auto out = note (f);
            CHECK (finite (out) && rms (out, 4800, 19200) > 1e-3);
        }
    const auto v = w::formantHz (0.0);
    CHECK (v[0] < v[1] && v[1] < v[2] && w::formantHz (1.0)[1] < w::formantHz (0.5)[1]);
}

TEST (helix190mi_drawn_lfo_and_macros)
{
    using namespace helix190mi;
    const double tri[] { 3, 0.0, 0.0, 0.0, 0.25, 1.0, 0.0, 0.75, -1.0, 0.0 };
    CHECK (w::drawnLfo (tri, 0.0) == 0.0f && std::fabs (w::drawnLfo (tri, 0.25) - 1.0f) < 1e-6f && std::fabs (w::drawnLfo (tri, 0.75) + 1.0f) < 1e-6f);
    CHECK (std::fabs (w::drawnLfo (tri, 0.5)) < 1e-6f && std::fabs (w::drawnLfo (tri, 0.875) + 0.5f) < 1e-6f && std::fabs (w::drawnLfo (tri, 1.25) - 1.0f) < 1e-6f);
    const double curved[] { 2, 0.0, 0.0, 0.8, 0.5, 1.0, 0.0 };
    CHECK (w::drawnLfo (curved, 0.25) > 0.6f); // (a bent segment rises fast first)
    const double one[] { 1, 0.3, -0.4, 0.0 };
    const double none[] { 0 };
    CHECK (std::fabs (w::drawnLfo (one, 0.9) + 0.4f) < 1e-6f && w::drawnLfo (none, 0.4) == 0.0f);
    // the drawn shape plays: LFO 1 → pitch, the drawn square differs from the sine
    auto s = plus (livePatch (0));
    s[(std::size_t) w::matrixIndex (w::tgt::oscPitch, w::src::lfo1)] = 0.2;
    const auto sine = note (s);
    x (s, w::ext::lfoDraw, 1.0);
    CHECK (hash (note (s)) != hash (sine) && finite (note (s)));
    // the default drawn shape is the triangle: as Live's triangle LFO
    auto liveTri = plus (livePatch (0));
    liveTri[(std::size_t) w::matrixIndex (w::tgt::oscPitch, w::src::lfo1)] = 0.2;
    liveTri[(std::size_t) w::lfoBase (0) + w::lfo::type] = 1;
    const auto a = note (liveTri), b = note (s);
    double diff = 0.0, ref = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff += std::fabs ((double) a[i] - b[i]), ref += std::fabs ((double) a[i]);
    CHECK (diff < 1e-3 * ref);
    // macros into Live's rows: Macro 2 → Osc 1 Pitch (0.125 × 24 st = +3 st at Macro 2 = 1)
    auto m = helix190ja::base();
    m = plus (m);
    m[(std::size_t) w::cellIndex (w::tgt::oscPitch, w::src::macro1 + 1)] = 0.125;
    const auto f0 = helix190ja::frequency (note (m, 57), 4800, 19200, 48000.0);
    x (m, w::ext::macro + 1, 1.0);
    const auto f1 = helix190ja::frequency (note (m, 57), 4800, 19200, 48000.0);
    CHECK (std::fabs (f0 - 220.0) < 0.5 && std::fabs (f1 - 220.0 * std::pow (2.0, 3.0 / 12.0)) < 0.5);
}

TEST (helix190mi_effects_rack)
{
    using namespace helix190mi;
    auto s = plus (livePatch (0));
    const auto dry = note (s, 48, 1.0);
    for (int fx : { w::ext::distOn, w::ext::chorusOn, w::ext::delayOn, w::ext::reverbOn })
    {
        auto f = s;
        x (f, fx, 1.0);
        const auto out = note (f, 48, 1.0);
        CHECK (finite (out) && hash (out) != hash (dry));
        if (fx == w::ext::distOn)
            for (int mode = 0; mode < 4; ++mode)
            {
                x (f, w::ext::distMode, mode);
                CHECK (finite (note (f, 48, 0.5)));
            }
    }
    // the delay repeats after the note: 1/4 at 120 BPM = 0.5 s; the engine stays busy until the tail dies
    lynx::wt::Engine e;
    e.prepare (48000.0);
    auto d = s;
    x (d, w::ext::delayOn, 1.0);
    x (d, w::ext::delayTime, 6);
    x (d, w::ext::delayMix, 0.5);
    x (d, w::ext::delayFeedback, 0.3);
    d[(std::size_t) w::envBase (0) + w::env::release] = 0.01;
    e.setState (d.data());
    std::vector<float> l (48000 * 8, 0.0f), r (48000 * 8, 0.0f);
    e.note (60, 1.0f);
    e.render (l.data(), r.data(), 4800, 120.0, -1.0);
    e.note (60, 0.0f);
    bool busyAfterVoices = false;
    for (int pos = 4800; pos < (int) l.size(); pos += 480)
    {
        e.render (l.data() + pos, r.data() + pos, 480, 120.0, -1.0);
        if (pos > 9600 && e.activeVoices() == 0 && ! e.idle())
            busyAfterVoices = true;
    }
    const auto echo = rms (std::vector<float> (l.begin() + 24000, l.begin() + 28800), 0, 4800, 1);
    const auto gap = rms (std::vector<float> (l.begin() + 12000, l.begin() + 21600), 0, 9600, 1);
    CHECK (busyAfterVoices && echo > 4.0 * gap && echo > 1e-3);
    CHECK (e.idle()); // (silent at the end: the tail ended)
}

TEST (helix190mi_factory_bank)
{
    using namespace lynx;
    const auto& bank = banks::bankOf (InstrumentKind::helix);
    CHECK (bank.size() >= 9 && bank.front().name == "Init");
    // a new Helix channel is the bank's first preset: Helix's own defaults with Helix+ live
    natives::FlAccess none;
    const auto made = natives::makeChannel (InstrumentKind::helix, none);
    CHECK (made && made->modelParams == helixDefaults() && made->name == "Helix");
    std::set<std::string> names;
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        ChannelDef c;
        c.instrument = InstrumentKind::helix;
        const auto loaded = banks::withPreset (c, i);
        CHECK (loaded && names.insert (bank[(std::size_t) i].name).second && ! bank[(std::size_t) i].tags.empty());
        if (! loaded)
            continue;
        CHECK ((int) loaded->modelParams.size() == wt::stateSize && loaded->modelParams[(std::size_t) wt::extBase] >= 1.0f);
        const auto state = helixState (*loaded);
        for (int t = 0; t < 2; ++t)
            CHECK (state[(std::size_t) wt::tableBase + (std::size_t) t] >= 0.0);
        std::vector<wt::RefNote> notes { { 0, 48, 0.9f, 48000 }, { 0, 55, 0.9f, 48000 }, { 0, 60, 0.9f, 48000 } };
        wt::HelixUserData data; // (190ml: a preset's user tables and sample)
        (void) wt::decodeUserData (loaded->pluginState, data);
        const auto out = wt::renderReference (state, notes, 48000.0, 48000 * 2, 256, 120.0, &data);
        auto peak = 0.0f;
        for (auto f : out)
            peak = std::max (peak, std::fabs (f));
        CHECK (helix190mi::finite (out) && peak > 0.02f && peak < 1.5f);
        std::printf ("  %-18s peak %.2f\n", bank[(std::size_t) i].name.c_str(), peak);
    }
}

TEST (helix190mi_als_export_note)
{
    using namespace lynx;
    // a Helix+ preset goes to Live as Wavetable (Live's values) with a note naming what stays behind; Helix's own
    // default (nothing of Helix+ used) goes without one
    for (const auto* preset : { "Growl FM", "Init" })
    {
        Song song;
        song.bpm = 120.0;
        ChannelDef h;
        h.name = "Bass";
        h.instrument = InstrumentKind::helix;
        const auto& bank = banks::bankOf (InstrumentKind::helix);
        for (int i = 0; i < (int) bank.size(); ++i)
            if (bank[(std::size_t) i].name == preset)
                if (auto loaded = banks::withPreset (h, i))
                    h.modelParams = loaded->modelParams;
        song.channels.push_back (h);
        Pattern p;
        Note note;
        note.channel = 0, note.key = 48, note.start = 0, note.length = ppq;
        p.notes.push_back (note);
        song.patterns.push_back (p);
        Clip clip;
        clip.pattern = 0, clip.start = 0, clip.length = 4 * ppq, clip.track = 0;
        song.playlist.push_back (clip);
        const auto out = exportAls (song);
        CHECK (out.xml.find ("<InstrumentVector") != std::string::npos);
        const bool noted = std::any_of (out.notes.begin(), out.notes.end(), [] (const std::string& n) { return n.find ("Helix+") != std::string::npos; });
        CHECK (noted == (std::string (preset) == "Growl FM"));
    }
}
