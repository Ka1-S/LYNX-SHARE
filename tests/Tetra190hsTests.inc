// Round 190hs: Tetra (core/FmSynth.h), LYNX's own 4-operator FM synth, and Live's Operator imported onto it
// (core/AlsDevicesFm.cpp). Uses the .als builders of AlsDevices190edTests.inc. No data file is needed.

#include "core/FmSynth.h"
#include "core/ModelParams.h"
#include "core/NativeEngines.h"

namespace tetra190hs
{
namespace f = lynx::fm;

inline double rms (const std::vector<float>& x, int from, int to, int stride = 1, int offset = 0)
{
    double s = 0.0;
    int n = 0;
    for (int i = from; i < to; ++i, ++n)
        s += (double) x[(std::size_t) (i * stride + offset)] * x[(std::size_t) (i * stride + offset)];
    return n > 0 ? std::sqrt (s / n) : 0.0;
}
inline double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }
// the amplitude of the partial at f (one-bin DFT over whole cycles), from the left channel of an interleaved render
inline double partial (const std::vector<float>& x, int from, int to, double hz, double fs)
{
    std::complex<double> acc {};
    for (int i = from; i < to; ++i)
        acc += (double) x[(std::size_t) (2 * i)] * std::polar (1.0, -2.0 * 3.14159265358979 * hz * i / fs);
    return 2.0 * std::abs (acc) / (to - from);
}
inline std::vector<double> state() { return std::vector<double> (f::defaults().begin(), f::defaults().end()); }
inline std::vector<float> render (const std::vector<double>& s, int key, double seconds, double hold, double fs = 48000.0, float vel = 1.0f)
{
    f::RefNote n;
    n.start = 0;
    n.key = key;
    n.velocity = vel;
    n.release = hold > 0.0 ? (int) (hold * fs) : -1;
    return f::renderReference (s, { n }, fs, (int) (seconds * fs));
}
// operator k's parameter
inline int op (int k, int p) { return f::opBase (k) + p; }
} // namespace tetra190hs

TEST (tetra190hs_parameters_and_laws)
{
    using namespace lynx;
    namespace f = fm;
    // the automatable table: every parameter once, sorted, unique English names, defaults inside their ranges
    const auto& table = tetraEngineParams();
    CHECK ((int) table.size() == f::paramCount);
    std::set<std::string> names;
    for (int i = 0; i < (int) table.size(); ++i)
    {
        const auto& p = table[(std::size_t) i];
        CHECK (p.index == i && ! p.name.empty() && p.high > p.low);
        names.insert (p.name);
        const auto d = f::defaults()[(std::size_t) i];
        CHECK (d >= p.low - 1e-6 && d <= p.high + 1e-6);
        CHECK (! f::paramText (i, d).empty());
        CHECK (f::paramName (i) == p.name);
    }
    CHECK ((int) names.size() == f::paramCount);
    CHECK (modelparams::tableOf (InstrumentKind::tetra) != nullptr);
    CHECK (modelparams::findByName (InstrumentKind::tetra, "Filter Freq") != nullptr
           && modelparams::findByName (InstrumentKind::tetra, "Filter Freq")->index == f::g::filterFreq);
    // ratios [Operator's coarse 0 = ½, fine per mille], fixed frequencies, voices
    CHECK (f::ratio (0, 0.0) == 0.5 && f::ratio (3, 0.0) == 3.0 && std::fabs (f::ratio (1, 500.0) - 1.5) < 1e-12);
    CHECK (f::voiceLimit (0) == 1 && f::voiceLimit (5) == 6 && f::voiceLimit (14) == 32);
    CHECK (std::isinf (f::levelDb (0.00031622799)) && std::fabs (f::levelDb (0.5) + 6.0206) < 1e-3);
    // the segment shapes: straight at slope 0, ends fixed, bent either way
    CHECK (f::segment (0.0f, 1.0f) == 0.0f && std::fabs (f::segment (1.0f, 1.0f) - 1.0f) < 1e-6f && std::fabs (f::segment (0.3f, 0.0f) - 0.3f) < 1e-6f);
    CHECK (f::segment (0.25f, 1.0f) > 0.5f && f::segment (0.25f, -1.0f) < 0.25f);
    // the routings [190ei shots]: A is a carrier in every algorithm; 10 = four carriers, no modulation
    for (int a = 0; a < f::algorithmCount; ++a)
        CHECK ((f::routing (a).carriers & 1u) != 0);
    CHECK (f::routing (0).from[0] == 2 && f::routing (0).from[1] == 4 && f::routing (0).from[2] == 8 && f::routing (0).carriers == 1);
    CHECK (f::routing (4).carriers == 3 && f::routing (7).carriers == 5 && f::routing (8).carriers == 7 && f::routing (10).carriers == 15);
    for (int k = 0; k < f::opCount; ++k)
        CHECK (f::routing (10).from[(std::size_t) k] == 0);
    // a modulator always comes after what it feeds (the engine computes D, C, B, A in that order)
    for (int a = 0; a < f::algorithmCount; ++a)
        for (int k = 0; k < f::opCount; ++k)
            CHECK ((f::routing (a).from[(std::size_t) k] & ((1u << (k + 1)) - 1u)) == 0);
    // the user wave: peak-normalised partials
    std::array<float, f::harmonics> parts {};
    parts[0] = 1.0f;
    parts[2] = 1.0f;
    std::vector<float> t (f::waveTableSize + 1);
    f::buildUserWave (parts.data(), t.data());
    float peak = 0.0f;
    for (auto v : t)
        peak = std::max (peak, std::fabs (v));
    CHECK (std::fabs (peak - 1.0f) < 1e-5f);
}

TEST (tetra190hs_engine_sine_fm_and_envelopes)
{
    using namespace lynx;
    using namespace tetra190hs;
    namespace f = fm;
    const double fs = 48000.0;
    // the default patch: operator A, a sine at the key's pitch
    auto s = state();
    s[(std::size_t) op (0, f::op::envBase + f::env::attack)] = 0.1;
    auto out = render (s, 69, 1.0, -1.0, fs);
    const auto a440 = partial (out, 24000, 48000, 440.0, fs);
    CHECK (a440 > 0.05);
    CHECK (partial (out, 24000, 48000, 880.0, fs) < a440 * 1e-3); // a pure sine
    // FM: B (ratio 1) into A gives sidebands at the harmonics, more with B's level
    auto fmState = s;
    fmState[(std::size_t) op (1, f::op::on)] = 1.0;
    fmState[(std::size_t) op (1, f::op::level)] = std::pow (10.0, -24.0 / 20.0);
    const auto weak = render (fmState, 69, 1.0, -1.0, fs);
    fmState[(std::size_t) op (1, f::op::level)] = std::pow (10.0, -12.0 / 20.0);
    const auto strong = render (fmState, 69, 1.0, -1.0, fs);
    const auto sidebands = [&] (const std::vector<float>& x) {
        double e = 0.0;
        for (int h = 2; h <= 12; ++h)
            e += std::pow (partial (x, 24000, 48000, 440.0 * h, fs), 2.0);
        return std::sqrt (e);
    };
    const auto h2w = sidebands (weak), h2s = sidebands (strong);
    // (FM keeps the power: a deeper index moves more of it from the carrier into the sidebands)
    CHECK (h2w > a440 * 0.05 && h2s > h2w && partial (strong, 24000, 48000, 440.0, fs) < 0.5 * partial (weak, 24000, 48000, 440.0, fs));
    // algorithm 10: B is a second carrier, no sidebands
    fmState[(std::size_t) f::g::algorithm] = 10.0;
    fmState[(std::size_t) op (1, f::op::coarse)] = 2.0;
    fmState[(std::size_t) op (1, f::op::level)] = 1.0;
    const auto side = render (fmState, 69, 1.0, -1.0, fs);
    CHECK (std::fabs (db (partial (side, 24000, 48000, 880.0, fs)) - db (partial (side, 24000, 48000, 440.0, fs))) < 0.2);
    CHECK (partial (side, 24000, 48000, 1320.0, fs) < a440 * 1e-3);
    // fixed frequency: the key does not move it
    auto fixed = s;
    fixed[(std::size_t) op (0, f::op::fixed)] = 1.0;
    fixed[(std::size_t) op (0, f::op::fixedFreq)] = 300.0;
    fixed[(std::size_t) op (0, f::op::fixedMulti)] = 4.0;
    const auto fx = render (fixed, 40, 0.5, -1.0, fs);
    CHECK (partial (fx, 12000, 24000, f::fixedHz (300.0, 4), fs) > 0.05);
    // the envelope: decay to the sustain, release to silence, then the voice ends
    auto env = s;
    env[(std::size_t) op (0, f::op::envBase + f::env::decay)] = 100.0;
    env[(std::size_t) op (0, f::op::envBase + f::env::sustain)] = 0.5;
    env[(std::size_t) op (0, f::op::envBase + f::env::release)] = 100.0;
    const auto e = render (env, 69, 1.0, 0.5, fs);
    CHECK (std::fabs (db (rms (e, 12000, 23000, 2) * std::sqrt (2.0) / a440) + 6.02) < 0.5);
    CHECK (rms (e, 24000 + 6000, 48000, 2) < 1e-4);
    f::Engine engine;
    engine.prepare (fs);
    engine.setState (env.data());
    engine.note (69, 1.0f);
    std::vector<float> l (4800), r (4800);
    engine.render (l.data(), r.data(), 4800, 120.0, -1.0);
    CHECK (engine.activeVoices() == 1);
    engine.note (69, 0.0f);
    for (int i = 0; i < 10; ++i)
        engine.render (l.data(), r.data(), 4800, 120.0, -1.0);
    CHECK (engine.idle());
    // no NaN at the extremes: full feedback, every op at full level, high notes
    auto wild = s;
    for (int k = 0; k < f::opCount; ++k)
    {
        wild[(std::size_t) op (k, f::op::on)] = 1.0;
        wild[(std::size_t) op (k, f::op::level)] = 1.0;
        wild[(std::size_t) op (k, f::op::feedback)] = 100.0;
        wild[(std::size_t) op (k, f::op::coarse)] = 48.0;
        wild[(std::size_t) op (k, f::op::wave)] = (double) (k * 6);
    }
    wild[(std::size_t) f::g::filterOn] = 1.0;
    wild[(std::size_t) f::g::filterRes] = 1.25;
    wild[(std::size_t) f::g::shaperType] = 2.0;
    wild[(std::size_t) f::g::spread] = 100.0;
    const auto w = render (wild, 127, 0.5, 0.25, fs);
    CHECK (std::all_of (w.begin(), w.end(), [] (float v) { return std::isfinite (v) && std::fabs (v) < 64.0f; }));
}

TEST (tetra190hs_voices_glide_and_instrument)
{
    using namespace lynx;
    using namespace tetra190hs;
    namespace f = fm;
    const double fs = 48000.0;
    // the voice count: notes beyond it end the oldest
    auto s = state();
    s[(std::size_t) f::g::voices] = 1.0; // 2 voices
    f::Engine e;
    e.prepare (fs);
    e.setState (s.data());
    std::vector<float> l (480), r (480);
    for (const auto key : { 60, 64, 67 })
    {
        e.note (key, 1.0f);
        e.render (l.data(), r.data(), 480, 120.0, -1.0);
    }
    CHECK (e.activeVoices() == 2);
    // the instrument: plays the channel, takes automation (0…1 over the parameter's range) and goes idle
    auto inst = makeInstrument (InstrumentKind::tetra);
    CHECK (inst != nullptr);
    if (inst == nullptr)
        return;
    ChannelDef c;
    c.instrument = InstrumentKind::tetra;
    inst->prepare (fs);
    inst->configure (c, 120.0); // (an empty channel: the defaults)
    std::vector<float> L (24000, 0.0f), R (24000, 0.0f);
    CHECK (inst->idle());
    inst->noteOn (69, 1.0f);
    inst->render (L.data(), R.data(), 24000, 1.0f, 1.0f);
    const auto before = rms (L, 12000, 24000);
    CHECK (before > 0.05);
    const auto* level = modelparams::find (InstrumentKind::tetra, op (0, f::op::level));
    CHECK (level != nullptr);
    inst->setParameter (op (0, f::op::level), level->normalised (0.5f));
    std::fill (L.begin(), L.end(), 0.0f);
    inst->render (L.data(), R.data(), 24000, 1.0f, 1.0f);
    CHECK (std::fabs (db (rms (L, 12000, 24000) / before) + 6.02) < 0.3);
    inst->noteOff (69);
    for (int i = 0; i < 20; ++i)
        inst->render (L.data(), R.data(), 24000, 1.0f, 1.0f);
    CHECK (inst->idle());
    // the channel's values: an empty one reads the defaults; a set value round-trips
    CHECK (modelparams::rawValue (c, f::g::algorithm) == std::optional<float> (0.0f));
    CHECK (modelparams::setRawValue (c, f::g::filterFreq, 1234.0f) && (int) c.modelParams.size() == f::stateSize);
    CHECK (tetraState (c)[(std::size_t) f::g::filterFreq] == 1234.0);
    // a new channel from the plugin list needs nothing
    natives::FlAccess none;
    const auto made = natives::makeChannel (InstrumentKind::tetra, none);
    CHECK (made.has_value() && made->instrument == InstrumentKind::tetra && made->name == "Tetra");
}

TEST (tetra190hs_operator_imports_onto_tetra)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using namespace tetra190hs;
    namespace f = fm;
    using als190ed::par;
    using als190ed::flag;
    // operator A at ratio 2, B into it at −12 dB, the filter, an LFO destination B and a pan by key: values as Live's
    const auto extra = std::string ("<Shaper>") + par ("Type", 2) + par ("Drive", 6.0) + par ("DryWet", 50.0) + "</Shaper>";
    auto text = als190ed::operatorDevice (0, { 0.8, 0.251188643, 0.000316227757, 0.000316227757 }, 0, 2, extra);
    // (the builder's filter / LFO are off; switch them on with values)
    const auto at = text.find ("<Filter>");
    text.replace (at, std::string ("<Filter>" + flag ("OnOff", false) + "</Filter>").size(),
                  "<Filter>" + flag ("OnOff", true) + par ("Type", 1) + par ("Frequency", 880.0) + par ("Resonance", 0.6) + flag ("Slope", true) + "</Filter>");
    xml::Document doc;
    CHECK (doc.parse (text));
    ChannelDef c;
    Notes notes;
    CHECK (operatorToTetra (doc.root(), c, notes));
    CHECK (c.instrument == InstrumentKind::tetra && (int) c.modelParams.size() == f::stateSize);
    const auto v = [&] (int i) { return (double) c.modelParams[(std::size_t) i]; };
    CHECK (v (op (0, f::op::coarse)) == 2.0 && std::fabs (v (op (0, f::op::level)) - 0.8) < 1e-6 && std::fabs (v (op (1, f::op::level)) - 0.251188643) < 1e-6);
    CHECK (std::fabs (v (op (0, f::op::envBase + f::env::attack)) - 2.0) < 1e-6 && std::fabs (v (op (0, f::op::envBase + f::env::decay)) - 400.0) < 1e-3);
    CHECK (v (op (0, f::op::envBase + f::env::sustain)) == 0.5 && v (op (0, f::op::envBase + f::env::releaseSlope)) == 1.0);
    CHECK (v (f::g::filterOn) == 1.0 && v (f::g::filterType) == 1.0 && v (f::g::filterFreq) == 880.0 && v (f::g::filterSlope) == 1.0);
    CHECK (std::fabs (v (f::g::filterRes) - 0.6) < 1e-6 && std::fabs (v (f::g::volume) - 0.5) < 1e-6);
    CHECK (v (f::g::shaperType) == 2.0 && v (f::g::shaperDrive) == 6.0 && v (f::g::shaperMix) == 50.0);
    // the user wave's partials (the builder: 1, 0, 0.5)
    CHECK (v (f::harmonicsBase) == 1.0 && v (f::harmonicsBase + 1) == 0.0 && v (f::harmonicsBase + 2) == 0.5);
    CHECK (notes.dropped.empty());
    // what Tetra has no place for is listed
    auto more = als190ed::operatorDevice (0, { 1.0, 0.000316227757, 0.000316227757, 0.000316227757 });
    more.replace (more.find ("<Globals>"), 9, "<Globals>" + par ("PanoramaKeyScale", 20.0));
    more.replace (more.find ("<Lfo>"), 5, "<Lfo>" + flag ("LfoOn", true) + "<ModDst>" + par ("Amount", 50.0) + par ("Connection", 3) + "</ModDst>");
    xml::Document doc2;
    CHECK (doc2.parse (more));
    ChannelDef c2;
    Notes n2;
    CHECK (operatorToTetra (doc2.root(), c2, n2));
    CHECK (std::find (n2.dropped.begin(), n2.dropped.end(), "pan by key / random pan") != n2.dropped.end());
    CHECK (std::find (n2.dropped.begin(), n2.dropped.end(), "LFO destination B") != n2.dropped.end());
    // the imported channel plays
    auto inst = makeInstrument (c.instrument);
    inst->prepare (48000.0);
    inst->configure (c, 120.0);
    std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
    inst->noteOn (60, 1.0f);
    inst->render (l.data(), r.data(), 24000, 1.0f, 1.0f);
    CHECK (rms (l, 2400, 24000) > 1e-3);
    // not an Operator: false
    xml::Document doc3;
    CHECK (doc3.parse ("<Operator Id=\"1\"><Globals/></Operator>"));
    ChannelDef c3;
    Notes n3;
    CHECK (! operatorToTetra (doc3.root(), c3, n3));
}
