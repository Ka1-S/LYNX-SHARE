// Round 190ni: LYNX Model (core/physmod/LynxModel.h, core/physmod/ModalBank.h), LYNX's own physical-modelling
// instrument: the compiled-in tables, the key / velocity interpolation, the modal core's SIMD paths, the mallet's
// physics behind each knob, voices, the instrument, its bank, catalog entry and preset files. No data file is needed.

#include "core/CpuFeatures.h"
#include "core/GeneratorPresets.h"
#include "core/LynxBanks.h"
#include "core/ModelParams.h"
#include "core/NativeEngines.h"
#include "core/PluginCatalog.h"
#include "core/physmod/LynxModel.h"

namespace lynxmodel190ni
{
namespace lm = lynx::lm;
constexpr double fs = 48000.0;

inline std::vector<double> state (int model)
{
    const auto v = lm::presetValues (model);
    return std::vector<double> (v.begin(), v.end());
}
inline std::vector<float> render (const std::vector<double>& s, int key, double seconds, float vel = 0.8f, double hold = -1.0, double rate = fs)
{
    lm::RefNote n;
    n.key = key;
    n.velocity = vel;
    n.release = hold > 0.0 ? (int) (hold * rate) : -1;
    return lm::renderReference (s, { n }, rate, (int) (seconds * rate));
}
inline double rms (const std::vector<float>& x, double t0, double t1, double rate = fs)
{
    double s = 0.0;
    int n = 0;
    for (int i = (int) (t0 * rate); i < (int) (t1 * rate) && 2 * i < (int) x.size(); ++i, ++n)
        s += (double) x[(std::size_t) (2 * i)] * x[(std::size_t) (2 * i)];
    return n > 0 ? std::sqrt (s / n) : 0.0;
}
// the amplitude of the partial at hz (one-bin DFT), left channel
inline double partial (const std::vector<float>& x, double t0, double t1, double hz, double rate = fs)
{
    std::complex<double> acc {};
    const int a = (int) (t0 * rate), b = (int) (t1 * rate);
    for (int i = a; i < b; ++i)
        acc += (double) x[(std::size_t) (2 * i)] * std::polar (1.0, -2.0 * 3.14159265358979 * hz * i / rate);
    return 2.0 * std::abs (acc) / (b - a);
}
// the strongest partial's frequency within +-60 cents of hz (fine DFT scan)
inline double peakNear (const std::vector<float>& x, double t0, double t1, double hz, double rate = fs)
{
    double best = 0.0, bestHz = hz;
    for (double c = -60.0; c <= 60.0; c += 0.5)
    {
        const auto f = hz * std::pow (2.0, c / 1200.0);
        if (const auto a = partial (x, t0, t1, f, rate); a > best)
        {
            best = a;
            bestHz = f;
        }
    }
    return bestHz;
}
// the first `t` seconds' spectral centroid over the bins 100 Hz ... 16 kHz (DFT on a 100 Hz grid)
inline double centroid (const std::vector<float>& x, double t0, double t1)
{
    double num = 0.0, den = 0.0;
    for (double f = 100.0; f < 16000.0; f += 100.0)
    {
        const auto a = partial (x, t0, t1, f);
        num += a * a * f;
        den += a * a;
    }
    return den > 0.0 ? num / den : 0.0;
}
inline double hz (double key) { return 440.0 * std::pow (2.0, (key - 69.0) / 12.0); }
inline bool finite (const std::vector<float>& x)
{
    return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v) && (v == 0.0f || std::abs (v) > 1e-30f); });
}
} // namespace lynxmodel190ni

TEST (lynxmodel190ni_tables)
{
    using namespace lynx;
    for (int m = 0; m < lm::modelCount; ++m)
    {
        const auto& t = lm::builtInTable (m);
        CHECK (t.model == m);
        CHECK (t.anchors.size() >= 10);                 // (every recorded key and layer)
        CHECK (lm::builtInTableBytes (m) <= 25 * 1024); // (the brief: ~25 KB per instrument at most)
        CHECK (std::any_of (t.provenance.begin(), t.provenance.end(), [] (std::uint8_t b) { return b != 0; }));
        for (std::size_t i = 0; i < t.anchors.size(); ++i)
        {
            const auto& a = t.anchors[i];
            CHECK (a.count >= 3 && a.count <= 44 && a.contact > 0.0f && a.contact < 0.01f); // (44: calibrate.py MAX_LANES, 190nl)
            CHECK (i == 0 || a.key >= t.anchors[i - 1].key);
            bool main = false;
            for (int k = 0; k < a.count; ++k)
                main = main || std::abs (t.modes[(std::size_t) (a.first + k)].ratio - 1.0f) < 0.001f;
            CHECK (main); // (the tuned main mode sits at the key)
        }
        // a damaged copy is refused with a reason
        std::string err;
        CHECK (! lm::parseTable (nullptr, 0, &err) && ! err.empty());
        const std::uint8_t junk[48] = { 'L', 'P', 'M', '2' };
        CHECK (! lm::parseTable (junk, sizeof (junk), &err));
    }
    CHECK (std::string (lm::modelName (lm::tubularBells)) == "Tubular Bells" && std::string (lm::modelId (lm::kalimba)) == "kalimba");
}

TEST (lynxmodel190ni_parse_rejects_damage)
{
    using namespace lynx;
    // a minimal valid table: one anchor, two modes
    std::vector<std::uint8_t> d (40 + 32 + 20, 0);
    std::memcpy (d.data(), "LPM1", 4);
    const auto put16 = [&d] (std::size_t o, std::uint16_t v) { d[o] = (std::uint8_t) v; d[o + 1] = (std::uint8_t) (v >> 8); };
    const auto putf = [&d] (std::size_t o, float v) { std::memcpy (d.data() + o, &v, 4); };
    put16 (4, 1); put16 (6, 0); put16 (8, 1); put16 (10, 2);
    putf (16, 0.25f); putf (20, 0.5f); putf (24, 0.95f); putf (28, 0.0f);
    putf (40, 60.0f); putf (44, 1.0f); putf (48, -20.0f); putf (52, 0.0005f);
    put16 (56, 0xd000); put16 (58, 0x6800); put16 (60, 0x3c00); put16 (62, 0x4900); // (-32 dB, 2048 Hz, Q 1, 10 ms)
    put16 (64, 0); put16 (66, 2);
    putf (72, 1.0f); put16 (76, 0x0000); put16 (78, 0x3c00); d[80] = 1;   // ratio 1, 0 dB, 1 s
    putf (82, 4.0f); put16 (86, 0xcc00); put16 (88, 0x3800); d[90] = 2;   // ratio 4, -16 dB, 0.5 s
    std::string err;
    const auto t = lm::parseTable (d.data(), d.size(), &err);
    CHECK (t.has_value());
    if (t)
    {
        CHECK (t->anchors.size() == 1 && t->modes.size() == 2 && t->modes[1].ratio == 4.0f && t->modes[1].shape == 2);
        CHECK (std::abs (t->modes[1].ampDb + 16.0f) < 1e-3f && std::abs (t->modes[1].t60 - 0.5f) < 1e-3f);
        CHECK (std::abs (t->anchors[0].noiseHz - 2048.0f) < 1.0f && std::abs (t->anchors[0].noiseMs - 10.0f) < 0.01f);
        // its note: the main mode at the key, the second four times higher and 16 dB down
        const auto n = lm::interpolate (*t, 60.0f, 1.0f);
        CHECK (n.count == 2 && std::abs (n.hz[0] - (float) lynxmodel190ni::hz (60)) < 0.01f && std::abs (n.hz[1] / n.hz[0] - 4.0f) < 1e-4f);
        CHECK (std::abs (20.0f * std::log10 (n.amp[1] / n.amp[0]) + 16.0f) < 0.01f);
    }
    auto bad = d;
    bad.pop_back();
    CHECK (! lm::parseTable (bad.data(), bad.size(), &err)); // truncated
    bad = d;
    put16 (66, 3);
    CHECK (! lm::parseTable (d.data(), d.size(), &err)); // a mode range past the end
    put16 (66, 2);
    putf (72, -1.0f);
    CHECK (! lm::parseTable (d.data(), d.size(), &err)); // a negative ratio
}

TEST (lynxmodel190ni_interpolation)
{
    using namespace lynx;
    using namespace lynxmodel190ni;
    const auto& t = lm::builtInTable (lm::marimba);
    // at a recorded key: the main mode lands on the key's frequency (the recording's tuning offset is removed)
    const auto k0 = t.anchors.front().key;
    const auto a = lm::interpolate (t, k0, 1.0f);
    CHECK (std::abs (1200.0 * std::log2 (a.mainHz / hz (k0))) < 0.5);
    // between two recorded keys: in tune too, its modes between theirs (transposed with the key)
    const auto mid = (t.anchors.front().key + t.anchors[3].key) / 2.0f + 0.5f;
    const auto b = lm::interpolate (t, mid, 0.7f);
    CHECK (b.count > 4 && std::abs (1200.0 * std::log2 (b.mainHz / hz (mid))) < 1.0);
    for (int k = 1; k < b.count; ++k)
        CHECK (b.hz[(std::size_t) k] >= b.hz[(std::size_t) k - 1]); // (frequency order)
    // louder layers are louder; outside the layers the level follows the velocity
    const auto soft = lm::interpolate (t, 72.0f, 0.3f), loud = lm::interpolate (t, 72.0f, 1.0f), softer = lm::interpolate (t, 72.0f, 0.1f);
    CHECK (loud.gain > soft.gain && soft.gain > softer.gain);
    CHECK (softer.contact > soft.contact * 0.99f); // (a softer stroke: a longer contact)
    // beyond the recorded keys: still in tune
    const auto hi = lm::interpolate (t, t.anchors.back().key + 5.0f, 0.8f);
    CHECK (std::abs (1200.0 * std::log2 (hi.mainHz / hz (t.anchors.back().key + 5.0f))) < 0.5);
    // every model: a note in the middle of its range has its modes and no mode above 20 kHz below the key
    for (int m = 0; m < lm::modelCount; ++m)
    {
        const auto& tm = lm::builtInTable (m);
        const auto key = std::round ((tm.anchors.front().key + tm.anchors.back().key) / 2.0f);
        const auto n = lm::interpolate (tm, key, 0.8f);
        CHECK (n.count >= 5 && n.count <= lm::maxModes);
        CHECK (std::abs (1200.0 * std::log2 (n.mainHz / hz (key))) < 1.0);
    }
}

TEST (lynxmodel190ni_mode_shapes)
{
    using namespace lynx;
    // free-free beam: mode 1 has nodes at 22.4 % and 77.6 %, mode 2 at the centre; the ends move the most
    CHECK (std::abs (lm::modeShape (0, 1, 0.2242f)) < 0.01f && std::abs (lm::modeShape (0, 1, 0.7758f)) < 0.01f);
    CHECK (std::abs (lm::modeShape (0, 2, 0.5f)) < 0.01f);
    CHECK (std::abs (lm::modeShape (0, 1, 0.0f)) > 1.9f && std::abs (lm::modeShape (0, 1, 0.5f)) > 1.0f);
    // clamped-free tine: no motion at the clamp, the most at the tip
    CHECK (std::abs (lm::modeShape (1, 1, 0.0f)) < 1e-4f && std::abs (lm::modeShape (1, 1, 1.0f)) > 1.9f);
    // high orders stay finite (asymptotic form)
    for (int k = 1; k <= 14; ++k)
        CHECK (std::isfinite (lm::modeShape (0, k, 0.37f)) && std::abs (lm::modeShape (0, k, 0.37f)) < 2.5f);
    CHECK (lm::modeShape (0, 0, 0.3f) == 1.0f); // (no law)
}

TEST (lynxmodel190ni_simd_paths_same_bits)
{
    using namespace lynx;
    std::uint32_t r = 12345u;
    const auto rnd = [&r] { r = r * 1664525u + 1013904223u; return (float) (r >> 8) / 16777216.0f; };
    // (every group in use, and one fewer: the AVX path runs groups in pairs, an odd count ends on a single group)
    for (const int groups : { lm::maxModes / lm::laneGroup, lm::maxModes / lm::laneGroup - 1 })
    {
        lm::ModalBank a;
        a.groups = groups;
        for (int k = 0; k < groups * lm::laneGroup; ++k)
        {
            const auto w = 0.02 + 2.5 * rnd(), rr = 0.999 + 0.000999 * rnd();
            a.pr[k] = (float) (rr * std::cos (w));
            a.pi[k] = (float) (rr * std::sin (w));
            a.drive[k] = rnd() - 0.5f;
            a.re[k] = rnd() - 0.5f;
            a.im[k] = rnd() - 0.5f;
        }
        auto b = a;
        float force[lm::maxBlock], outA[lm::maxBlock] {}, outB[lm::maxBlock] {};
        for (int i = 0; i < lm::maxBlock; ++i)
            force[i] = rnd();
        lm::runModes (a, force, outA, lm::maxBlock);
        lm::runModesBaseline (b, force, outB, lm::maxBlock);
        lm::runModes (a, nullptr, outA, 100);
        lm::runModesBaseline (b, nullptr, outB, 100);
        CHECK (std::memcmp (outA, outB, sizeof (outA)) == 0);
        CHECK (std::memcmp (a.re, b.re, sizeof (a.re)) == 0 && std::memcmp (a.im, b.im, sizeof (a.im)) == 0);
    }
    // a whole note on both paths: the same bits
    const auto s = lynxmodel190ni::state (lm::vibraphone);
    const auto fast = lynxmodel190ni::render (s, 72, 0.5);
    cpu::setLevelForTest (cpu::sse2);
    const auto base = lynxmodel190ni::render (s, 72, 0.5);
    cpu::setLevelForTest (cpu::avx2);
    CHECK (fast == base);
}

TEST (lynxmodel190ni_note_physics)
{
    using namespace lynx;
    using namespace lynxmodel190ni;
    for (int m = 0; m < lm::modelCount; ++m)
    {
        const auto& t = lm::builtInTable (m);
        const int key = (int) std::round ((t.anchors.front().key + t.anchors.back().key) / 2.0f);
        const auto x = render (state (m), key, 1.5);
        CHECK (finite (x));
        CHECK (rms (x, 0.0, 0.2) > 1e-3);                  // it sounds
        CHECK (rms (x, 1.0, 1.5) < rms (x, 0.02, 0.3));    // and decays
        float peak = 0.0f;
        for (auto v : x)
            peak = std::max (peak, std::abs (v));
        CHECK (peak < 1.0f); // (the tables' level: no clipping at velocity 0.8)
        // the main mode at the key (in tune within 3 cents; the bells' strike note is a virtual pitch: skipped)
        if (m != lm::tubularBells)
        {
            const auto f = peakNear (x, 0.05, 0.85, hz (key));
            CHECK (std::abs (1200.0 * std::log2 (f / hz (key))) < 3.0);
        }
    }
    // the marimba's tube: where the table has a build-up, the fundamental grows over the first milliseconds
    const auto s = state (lm::marimba);
    for (const int key : { 60, 72, 84 })
        if (lm::interpolate (lm::builtInTable (lm::marimba), (float) key, 0.8f).riseMs > 2.0f)
        {
            const auto x = render (s, key, 0.1);
            CHECK (partial (x, 0.0005, 0.004, hz (key)) < partial (x, 0.02, 0.04, hz (key)));
        }
    // determinism, and the same note at 44.1 and 96 kHz: in tune there too
    CHECK (render (s, 72, 0.4) == render (s, 72, 0.4));
    for (const double rate : { 44100.0, 96000.0 })
    {
        const auto x = render (s, 72, 0.6, 0.8f, -1.0, rate);
        CHECK (finite (x));
        CHECK (std::abs (1200.0 * std::log2 (peakNear (x, 0.05, 0.55, hz (72), rate) / hz (72))) < 3.0);
    }
    // the note ends: the engine goes idle once the bar is quiet
    lm::Engine e;
    e.prepare (lynxmodel190ni::fs);
    const auto v = lm::presetValues (lm::xylophone);
    e.setValues (v.data());
    e.note (84, 0.8f);
    std::vector<float> l (256), r (256);
    int blocks = 0;
    while (! e.idle() && blocks < 48000 * 30 / 256)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e.render (l.data(), r.data(), 256);
        ++blocks;
    }
    CHECK (e.idle() && blocks < 48000 * 20 / 256);
}

TEST (lynxmodel190ni_knobs_follow_the_physics)
{
    using namespace lynx;
    using namespace lynxmodel190ni;
    namespace p = lm::p;
    const auto base = state (lm::marimba);
    const auto with = [&base] (int i, double v) {
        auto s = base;
        s[(std::size_t) i] = v;
        return s;
    };
    const int key = 72;
    const auto ref = render (base, key, 1.2);
    // Hardness: a harder mallet (shorter contact) gives a brighter attack and a louder upper mode
    const auto hard = render (with (p::hardness, 0.9), key, 1.2), soft = render (with (p::hardness, 0.1), key, 1.2);
    CHECK (centroid (hard, 0.0, 0.05) > centroid (ref, 0.0, 0.05) && centroid (ref, 0.0, 0.05) > centroid (soft, 0.0, 0.05));
    // Damping: shorter rings
    const auto damped = render (with (p::damping, 0.9), key, 1.2), free = render (with (p::damping, 0.1), key, 1.2);
    CHECK (rms (damped, 0.6, 1.0) < 0.5 * rms (ref, 0.6, 1.0) && rms (free, 0.6, 1.0) > 1.5 * rms (ref, 0.6, 1.0));
    // Position: at the fundamental's node (u = 0.224) the fundamental nearly vanishes, at the centre it is strongest
    const auto& t = lm::builtInTable (lm::marimba);
    const auto node = (0.7758 - t.posU0) / (t.posU1 - t.posU0);
    const auto atNode = render (with (p::position, node), key, 1.0), centre = render (with (p::position, 0.0), key, 1.0);
    CHECK (partial (atNode, 0.1, 0.6, hz (key)) < 0.2 * partial (centre, 0.1, 0.6, hz (key)));
    // Resonator: the resonated fundamental louder
    const auto res = render (with (p::resonator, 1.0), key, 1.0);
    CHECK (partial (res, 0.05, 0.3, hz (key)) > 2.0 * partial (ref, 0.05, 0.3, hz (key)));
    // Tone: brighter spectrum
    const auto bright = render (with (p::tone, 1.0), key, 1.0);
    CHECK (centroid (bright, 0.0, 0.2) > centroid (ref, 0.0, 0.2));
    // Release: a short release stops the note at note off; Ring lets it ring on
    const auto shortRel = render (with (p::release, 0.0), key, 1.0, 0.8f, 0.1), ring = render (with (p::release, 1.0), key, 1.0, 0.8f, 0.1);
    CHECK (rms (shortRel, 0.4, 0.8) < 0.01 * rms (ring, 0.4, 0.8));
    // Velocity: no response at 0, the recorded response at 0.5
    const auto flatSoft = render (with (p::velocity, 0.0), key, 0.5, 0.2f), flatLoud = render (with (p::velocity, 0.0), key, 0.5, 1.0f);
    CHECK (std::abs (20.0 * std::log10 (rms (flatSoft, 0.0, 0.4) / rms (flatLoud, 0.0, 0.4))) < 0.01);
    CHECK (rms (render (base, key, 0.5, 0.2f), 0.0, 0.4) < 0.5 * rms (render (base, key, 0.5, 1.0f), 0.0, 0.4));
    // Volume: dB
    const auto quiet = render (with (p::volume, -6.0), key, 0.5);
    CHECK (std::abs (20.0 * std::log10 (rms (quiet, 0.0, 0.4) / rms (ref, 0.0, 0.4)) + 6.0) < 0.05);
    // Noise: off takes the strike noise out of the first milliseconds
    const auto quietNoise = render (with (p::noise, -40.0), key, 0.2), loudNoise = render (with (p::noise, 12.0), key, 0.2);
    CHECK (rms (loudNoise, 0.0, 0.02) > rms (quietNoise, 0.0, 0.02));
    // Motor (vibraphone): the resonated fundamental beats at the motor's rate
    auto vib = state (lm::vibraphone);
    vib[(std::size_t) p::release] = 1.0;
    auto mot = vib;
    mot[(std::size_t) p::motor] = 6.0;
    const auto still = render (vib, 72, 1.5), moving = render (mot, 72, 1.5);
    const auto wobble = [] (const std::vector<float>& x) {
        // the fundamental's 20 ms levels over 0.3 ... 1.3 s: spread around their trend
        std::vector<double> lv;
        for (double t0 = 0.3; t0 < 1.3; t0 += 0.02)
            lv.push_back (20.0 * std::log10 (partial (x, t0, t0 + 0.02, hz (72)) + 1e-12));
        double dev = 0.0;
        for (std::size_t i = 1; i + 1 < lv.size(); ++i)
            dev += std::abs (lv[i] - 0.5 * (lv[i - 1] + lv[i + 1]));
        return dev / (double) lv.size();
    };
    CHECK (wobble (moving) > 3.0 * wobble (still) + 0.05);
}

TEST (lynxmodel190ni_voices)
{
    using namespace lynx;
    lm::Engine e;
    e.prepare (lynxmodel190ni::fs);
    const auto v = lm::presetValues (lm::tubularBells); // (long rings)
    e.setValues (v.data());
    std::vector<float> l (256), r (256);
    for (int k = 0; k < 40; ++k)
    {
        e.note (60 + k % 24, 0.9f);
        e.render (l.data(), r.data(), 64);
    }
    CHECK (e.activeVoices() == lm::maxVoices);
    for (int k = 0; k < lm::maxVoices; ++k)
        CHECK (e.modesOfVoice (k) > 0 && e.modesOfVoice (k) <= lm::maxModes);
    e.allNotesOff();
    e.reset();
    CHECK (e.idle());
    CHECK (lm::Engine::footprintBytes() < 128 * 1024); // (32 voices of 32 modes: well under the 2 MB budget per instance)
}

TEST (lynxmodel190ni_instrument_and_catalog)
{
    using namespace lynx;
    namespace p = lm::p;
    // the channel's parameter table: every knob once, unique names, defaults inside their ranges, texts
    const auto& table = lynxModelEngineParams();
    CHECK ((int) table.size() == lm::paramCount);
    std::set<std::string> names;
    for (int i = 0; i < lm::paramCount; ++i)
    {
        const auto& prm = table[(std::size_t) i];
        CHECK (prm.index == i && prm.high > prm.low && ! prm.name.empty());
        names.insert (prm.name);
        for (int m = 0; m < lm::modelCount; ++m)
        {
            const auto d = lm::presetValues (m)[(std::size_t) i];
            CHECK (d >= prm.low && d <= prm.high);
            CHECK (! lm::paramText (i, d).empty());
        }
    }
    CHECK ((int) names.size() == lm::paramCount);
    CHECK (lm::paramText (p::motor, 0.0) == "Off" && lm::paramText (p::release, 1.0) == "Ring" && lm::paramText (p::model, 4) == "Tubular Bells");
    CHECK (modelparams::find (InstrumentKind::lynxModel, p::hardness) != nullptr);

    // the instrument plays a channel's values
    ChannelDef c;
    c.instrument = InstrumentKind::lynxModel;
    c.modelParams = lynxModelPreset (lm::glockenspiel);
    auto inst = makeInstrument (InstrumentKind::lynxModel, nullptr);
    CHECK (inst != nullptr);
    if (inst)
    {
        inst->prepare (lynxmodel190ni::fs);
        inst->configure (c, 120.0);
        inst->noteOn (84, 0.8f);
        std::vector<float> l (4800), r (4800);
        inst->render (l.data(), r.data(), 4800, 1.0f, 1.0f);
        double s = 0.0;
        for (auto x : l)
            s += (double) x * x;
        CHECK (s > 1e-4 && ! inst->idle());
        inst->setParameter (p::volume, 0.0f); // (0…1 → −40 dB)
        inst->panic();
        CHECK (inst->idle());
    }
    // its bank: the six models first-class, the Marimba first (a new channel)
    const auto& bank = banks::bankOf (InstrumentKind::lynxModel);
    CHECK (bank.size() >= 6 && bank[0].name == "Marimba");
    std::set<int> models;
    for (const auto& pr : bank)
    {
        ChannelDef x;
        x.instrument = InstrumentKind::lynxModel;
        pr.apply (x);
        CHECK ((int) x.modelParams.size() == lm::stateSize);
        models.insert ((int) x.modelParams[(std::size_t) p::model]);
    }
    CHECK ((int) models.size() == lm::modelCount);
    natives::FlAccess noFl; // (no FL Studio: the bank's first preset)
    noFl.fileExists = [] (const std::string&) { return false; };
    noFl.readFile = [] (const std::string&) { return std::vector<std::uint8_t> {}; };
    noFl.listPresets = [] (const std::string&) { return std::vector<std::string> {}; };
    const auto fresh = natives::makeChannel (InstrumentKind::lynxModel, noFl);
    CHECK (fresh.has_value() && fresh->instrument == InstrumentKind::lynxModel && fresh->modelParams.size() == (std::size_t) lm::stateSize);
    // the catalog: a LYNX-own engine in Synth special
    const auto* engine = natives::instrumentEngine (InstrumentKind::lynxModel);
    CHECK (engine != nullptr && std::string (engine->name) == "LYNX Model");
    CHECK (plugincatalog::flCategory ("LYNX Model", true) == "Synth special");
    // the user's presets: a saved LYNX Model sound reads back identically
    ChannelDef saved;
    saved.instrument = InstrumentKind::lynxModel;
    saved.modelParams = lynxModelPreset (lm::kalimba);
    saved.modelParams[(std::size_t) p::hardness] = 0.73f;
    CHECK (genpresets::kindId (InstrumentKind::lynxModel) == "lynxmodel" && genpresets::canSave (InstrumentKind::lynxModel));
    const auto back = genpresets::fromText (genpresets::toText ("Thumb", saved));
    CHECK (back.has_value() && back->kind == InstrumentKind::lynxModel && back->sound.modelParams == saved.modelParams);
}
