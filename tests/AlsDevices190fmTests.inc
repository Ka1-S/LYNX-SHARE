// --- Live's own devices in a set → LYNX's own, lot 4 (round 190fm, core/AlsDevices.h): Hybrid Reverb, Roar, Grain Delay
// and Auto Filter's other filter types. The device elements are written by the tests with the member names Live saves.
// Two tests read files of Live's own install when this machine has one (read-only, skipped otherwise): an impulse
// response and Live's Core Library device presets, through the same translators. Which statements about Live are
// [ASSUMED] is listed in the round's report (/mnt/e/lynx-work/realive190fm/sub-dev.md).

#include "core/AlsDevices.h"
#include "core/AlsImport.h"
#include "core/ConvolverFx.h"
#include "core/DynamicsSuite.h"
#include "core/EchoSuite.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/FlSamplers.h"
#include "core/Gzip.h"
#include "core/LynxVerbs.h"
#include "core/MixSuite.h"
#include "core/Patcher.h"
#include "core/Rack.h"
#include "core/XmlLite.h"

namespace als190fm
{
using als190bq::flag;
using als190bq::hasNote;
using als190bq::par;
using als190cf::near;
using als190cf::on;

inline std::string val (const std::string& name, double value) { return "<" + name + " Value=\"" + als190bq::num (value) + "\" />"; }

inline std::optional<lynx::alsdev::Translated> translate (const std::string& text, std::function<std::string (const lynx::xml::Element&)> find = {}, double bpm = 120.0)
{
    lynx::xml::Document doc;
    doc.parse (text);
    lynx::alsdev::Context context;
    context.bpm = bpm;
    context.findFile = std::move (find);
    return lynx::alsdev::effectFromLiveDevice (doc.root(), context);
}

inline std::string hybrid (int routing, int algorithm, const std::string& extra, const std::string& irPath = "Samples/Test/ir.wav")
{
    return "<Hybrid Id=\"7\">" + on() + par ("Routing", routing) + par ("Algorithm_Type", algorithm) + extra +
           "<ImpulseResponseHandler><SampleSlot><Value><SampleRef><FileRef><RelativePath Value=\"" + irPath +
           "\" /><Path Value=\"/nowhere/" + irPath + "\" /></FileRef></SampleRef></Value></SampleSlot></ImpulseResponseHandler></Hybrid>";
}

inline std::string roar (int routing, const std::string& extra)
{
    return "<Roar Id=\"8\">" + on() + val ("Input_RoutingMode", routing) + par ("Stage1_Shaper_Amount", 0.5) + extra + "</Roar>";
}

inline const lynx::RackDoc* rackOf (const lynx::alsdev::Translated& t) { return lynx::rack::isRack (t.slot) ? lynx::rack::rackOf (t.slot.patcher) : nullptr; }
inline const lynx::EffectSlot& nodeOf (const lynx::PatcherDoc& doc, const lynx::RackDoc& r, int chain, int k)
{
    return doc.nodes[(std::size_t) r.chains[(std::size_t) chain].nodes[(std::size_t) k]].effect;
}

// a stereo float WAV in the temp folder (the user's own impulse response)
inline std::string writeIr (const std::string& name, const std::vector<float>& l, const std::vector<float>& r, int rate)
{
    const auto path = (std::filesystem::temp_directory_path() / ("lynx190fm_" + processTempTag() + "_" + name)).string();
    const auto bytes = lynx::makeWavFloat ({ l, r }, rate);
    std::ofstream (path, std::ios::binary).write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    return path;
}

// the rack's output energy for a 0.25 sine at `hz` (after a settling half)
inline double energyThrough (const lynx::PatcherDoc& doc, double hz = 200.0, bool* finite = nullptr)
{
    auto g = als190ed::graphOf (doc);
    double energy = 0.0;
    bool ok = true;
    for (int b = 0; b < 40; ++b)
    {
        std::vector<float> l (512), rr (512);
        for (int i = 0; i < 512; ++i)
            l[(std::size_t) i] = rr[(std::size_t) i] = 0.25f * (float) std::sin (2.0 * 3.141592653589793 * hz * (b * 512 + i) / 48000.0);
        g->process (l.data(), rr.data(), nullptr, nullptr, l.data(), rr.data(), 512, 120.0);
        for (int i = 0; i < 512; ++i)
        {
            ok = ok && std::isfinite (l[(std::size_t) i]) && std::isfinite (rr[(std::size_t) i]);
            if (b >= 20)
                energy += (double) l[(std::size_t) i] * l[(std::size_t) i];
        }
    }
    if (finite != nullptr)
        *finite = ok;
    return energy;
}

inline const char* liveRoot() { return "/mnt/c/ProgramData/Ableton/Live 12 Trial/Resources"; }
} // namespace als190fm

TEST (als190fm_hybrid_reverb_algorithm_rack)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190fm::near;
    using als190fm::par;
    using als190fm::flag;
    // Dark Hall on its own (Routing 2), half wet, a low cut and a peak in the EQ, width 150 %
    const auto t = als190fm::translate (als190fm::hybrid (2, 0, par ("Algorithm_Decay", 3.5) + par ("Algorithm_Size", 0.4) + par ("Algorithm_Damping", 0.3) +
                                                                    par ("Algorithm_Diffusion", 0.8) + par ("Algorithm_Modulation", 0.25) +
                                                                    par ("Algorithm_BassMultiplier", 1.5) + par ("Algorithm_BassCrossover", 440.0) +
                                                                    par ("DryWet", 0.5) + flag ("Eq_On", true) + flag ("Eq_PreAlgo", false) +
                                                                    par ("Eq_LowBandType", 0) + par ("Eq_LowBandFrequency", 80.0) + par ("Eq_LowBandSlope", 4) +
                                                                    par ("Eq_HighBandType", 1) + par ("Eq_HighBandGain", 1.0) + par ("Eq_Peak1Gain", 2.0) +
                                                                    par ("Eq_Peak1Frequency", 700.0) + par ("Eq_Peak1Q", 0.7071) + par ("Eq_Peak2Gain", 1.0) +
                                                                    par ("StereoWidth", 1.5) + flag ("PreDelay_Sync", false) + par ("PreDelay_Time", 0.02)));
    CHECK (t.has_value() && t->from == "Hybrid Reverb" && t->approximate);
    const auto* r = t ? als190fm::rackOf (*t) : nullptr;
    CHECK (r != nullptr);
    if (r == nullptr)
        return;
    CHECK (r->name == "Hybrid Reverb" && r->chains.size() == 2 && r->chains[0].nodes.size() == 3 && r->chains[1].nodes.empty());
    CHECK (near (r->chains[0].volume, 0.5, 1.0e-6) && near (r->chains[1].volume, 0.5, 1.0e-6));
    const auto& doc = t->slot.patcher;
    const auto& verb = als190fm::nodeOf (doc, *r, 0, 0);
    const auto& eq = als190fm::nodeOf (doc, *r, 0, 1);
    const auto& width = als190fm::nodeOf (doc, *r, 0, 2);
    CHECK (verb.kind == EffectKind::atrium && eq.kind == EffectKind::facetEq && width.kind == EffectKind::utility);
    // Atrium: fully wet, the decay as its RT, the size, high mult 1 − damping, diffusion, bass multiplier 0.5 … 2
    CHECK (verb.params[0] == 1.0f && near (verbs::atrium::decaySec (verb.params[2]), 3.5, 0.02) && near (verb.params[5], 0.4, 1.0e-6));
    CHECK (near (verb.params[11], 0.7, 1.0e-6) && near (verb.params[18], 0.8, 1.0e-6) && near (verb.params[8], 0.25, 1.0e-6));
    CHECK (near (0.5 + 1.5 * verb.params[9], 1.5, 1.0e-5) && near (100.0 + 9900.0 * verb.params[10], 440.0, 0.01) && near (500.0 * verb.params[1], 20.0, 0.01));
    // the EQ: band 1 a 30 dB/oct low cut at 80 Hz (slope index 4 [ASSUMED 6 dB a step]), band 2 a +6 dB bell at 700 Hz; the
    // flat high shelf is left out
    CHECK (facet::shape (eq.params[(std::size_t) facet::param (0, 0)]) == facet::Shape::lowCut && near (facet::hz (eq.params[(std::size_t) facet::param (0, 1)]), 80.0, 0.1));
    CHECK (facet::slopeDb (eq.params[(std::size_t) facet::param (0, 4)]) == 30);
    CHECK (facet::shape (eq.params[(std::size_t) facet::param (1, 0)]) == facet::Shape::bell && near (facet::gainDb (eq.params[(std::size_t) facet::param (1, 2)]), 6.02, 0.02));
    CHECK (facet::shape (eq.params[(std::size_t) facet::param (3, 0)]) == facet::Shape::off);
    // width 1.5: L→L = 1.25, R→L = −0.25
    CHECK (near (width.params[2], (1.25 + 2.0) / 4.0, 1.0e-6) && near (width.params[3], (-0.25 + 2.0) / 4.0, 1.0e-6));
    // macros: Dry/Wet (two chain volumes), Decay, Size, Diffusion, Modulation, Damping, Width; links from Live's parameters
    CHECK (r->macros[hrDryWet].name == "Dry/Wet" && r->macros[hrDryWet].mappings.size() == 2);
    CHECK (r->macros[hrDecay].name == "Decay" && r->macros[hrDecay].mappings.size() == 1 && r->macros[hrWidth].mappings.size() == 4);
    CHECK (near (r->macros[hrDecay].value, 127.0 * (3.4 / 59.9), 1.0e-3));
    // each macro's line passes through the imported value (the wires drive the controls: nothing moves at import)
    for (const auto& m : r->macros)
        for (const auto& map : m.mappings)
            if (map.target == RackMapping::Target::param)
                CHECK (near (rack::mappedValue (map, m.value), doc.nodes[(std::size_t) map.node].effect.params[(std::size_t) map.param], 2.0e-4));
    const auto* decay = als190cf::linkOf (*t, "Algorithm_Decay");
    CHECK (decay != nullptr && decay->param == hrDecay && near (decay->value (60.0), 1.0, 1.0e-6));
    CHECK (als190cf::linkOf (*t, "StereoWidth") != nullptr && als190cf::linkOf (*t, "DryWet") != nullptr);
    CHECK (als190fm::hasNote (t->notes.approximated, "algorithm Dark Hall as Atrium"));
    bool finite = false;
    CHECK (als190fm::energyThrough (doc, 200.0, &finite) > 0.5 && finite);

    // Shimmer → Halo: the pitch shift, the shimmer amount as the shifted feedback, the decay through Halo's size
    const auto sh = als190fm::translate (als190fm::hybrid (2, 2, par ("Algorithm_Decay", 8.0) + par ("Algorithm_Shimmer", 0.4) + par ("Algorithm_PitchShift", -12.0) +
                                                                     par ("Algorithm_Diffusion", 0.92) + par ("DryWet", 1.0) + flag ("Eq_On", false)));
    const auto* sr = sh ? als190fm::rackOf (*sh) : nullptr;
    CHECK (sr != nullptr && sr->chains.size() == 1 && sr->chains[0].nodes.size() == 1);
    if (sr != nullptr)
    {
        const auto& h = als190fm::nodeOf (sh->slot.patcher, *sr, 0, 0);
        CHECK (h.kind == EffectKind::halo && h.params[1] == 0.0f && near (h.params[2], 0.4, 1.0e-6) && h.params[0] == 1.0f);
        // Halo's RT law at that size: (1.5 + 12.5 · size) · mult (diffusion 0.92: factor 1)
        const auto fb = 0.4, mult = 1.0 + 1.2 * std::pow (fb, 2.5);
        CHECK (near ((1.5 + 12.5 * h.params[4]) * mult, 8.0, 1.0e-3));
        CHECK (als190cf::linkOf (*sh, "DryWet") == nullptr);
    }
    // Prism: its two multipliers and crossover on Atrium's bass / high multipliers
    const auto pr = als190fm::translate (als190fm::hybrid (2, 4, par ("Algorithm_Decay", 2.0) + par ("Algorithm_Prism_LowMultiplier", 1.2) + par ("Algorithm_Prism_HighMultiplier", 0.5) +
                                                                     par ("Algorithm_Prism_CrossoverFrequency", 2000.0) + flag ("Eq_On", false)));
    if (const auto* pk = pr ? als190fm::rackOf (*pr) : nullptr; pk != nullptr)
    {
        const auto& a = als190fm::nodeOf (pr->slot.patcher, *pk, 0, 0);
        CHECK (a.kind == EffectKind::atrium && near (0.5 + 1.5 * a.params[9], 1.2, 1.0e-5) && near (0.1 + 0.9 * a.params[11], 0.5, 1.0e-5));
        CHECK (near (100.0 + 14900.0 * a.params[12], 2000.0, 0.01));
    }
    else
        CHECK (false);
    // Convolution alone with no file on this machine: nothing to play, the device is not translated (the importer lists it)
    CHECK (! als190fm::translate (als190fm::hybrid (3, 0, par ("Algorithm_Decay", 2.0) + par ("DryWet", 1.0))).has_value());
    // Parallel with no file: the algorithm alone, the convolution half listed with the file's name
    const auto half = als190fm::translate (als190fm::hybrid (1, 1, par ("Algorithm_Decay", 2.0) + par ("DryWet", 1.0) + par ("Algorithm_Quartz_Distance", 0.8)));
    CHECK (half.has_value() && als190fm::hasNote (half->notes.dropped, "\"ir.wav\" is not on this machine") && als190fm::hasNote (half->notes.dropped, "Quartz"));
}

TEST (als190fm_hybrid_reverb_convolution)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190fm::near;
    using als190fm::par;
    using als190fm::flag;
    // AIFF (big-endian 16-bit, two channels) and AIFC "sowt" (little-endian) decode as the same samples
    {
        const auto aiff = [] (bool sowt) {
            std::vector<std::uint8_t> b;
            const auto be32 = [&b] (std::uint32_t v) { for (int k = 3; k >= 0; --k) b.push_back ((std::uint8_t) (v >> (8 * k))); };
            const auto be16 = [&b] (int v) { b.push_back ((std::uint8_t) ((v >> 8) & 0xff)); b.push_back ((std::uint8_t) (v & 0xff)); };
            b.insert (b.end(), { 'F', 'O', 'R', 'M' });
            be32 (0);
            b.insert (b.end(), { 'A', 'I', 'F', (std::uint8_t) (sowt ? 'C' : 'F') });
            b.insert (b.end(), { 'C', 'O', 'M', 'M' });
            be32 (sowt ? 22 : 18);
            be16 (2);
            be32 (3);
            be16 (16);
            // 44100 Hz as an 80-bit extended: exponent 16383 + 15, mantissa 44100 << 48
            be16 (16383 + 15);
            be32 (44100u << 16);
            be32 (0);
            if (sowt)
                b.insert (b.end(), { 's', 'o', 'w', 't' });
            b.insert (b.end(), { 'S', 'S', 'N', 'D' });
            be32 (8 + 12);
            be32 (0);
            be32 (0);
            for (const int s : { 16384, -16384, 8192, 0, -32768, 32767 })
            {
                if (sowt)
                {
                    b.push_back ((std::uint8_t) (s & 0xff));
                    b.push_back ((std::uint8_t) ((s >> 8) & 0xff));
                }
                else
                    be16 (s);
            }
            return b;
        };
        for (const auto sowt : { false, true })
        {
            const auto a = decodeImpulse (aiff (sowt));
            CHECK (a.has_value() && a->l.size() == 3 && near (a->rate, 44100.0, 1.0e-9));
            if (a)
                CHECK (near (a->l[0], 0.5, 1.0e-6) && near (a->r[0], -0.5, 1.0e-6) && near (a->l[1], 0.25, 1.0e-6) && near (a->l[2], -1.0, 1.0e-6));
        }
        CHECK (! decodeImpulse ({ 1, 2, 3 }).has_value());
    }
    // shaping: a fade-in over the attack, −60 dB at the decay time, a stretch by the size
    {
        ImpulseAudio ir;
        ir.rate = 1000.0;
        ir.l.assign (1000, 1.0f);
        ir.r = ir.l;
        auto a = ir;
        shapeImpulse (a, 0.1, 0.5, 1.0);
        CHECK (near (a.l[50], 0.5 * std::pow (10.0, -3.0 * 0.05 / 0.5), 1.0e-6) && near (a.l[200], std::pow (10.0, -3.0 * 0.2 / 0.5), 1.0e-6) && a.l.size() == 750);
        auto s = ir;
        shapeImpulse (s, 0.0, 20.0, 2.0);
        CHECK (s.l.size() == 2000 && near (s.r[1500], 1.0, 1.0e-6));
    }
    // the Convolver slot: its state parses, holds the IR (embedded), dry off / wet on, the pre-delay knob ((7^x − 1) · 83.33 ms)
    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
    for (int i = 0; i < 4800; ++i)
    {
        const auto env = (float) std::exp (-i / 800.0);
        l[(std::size_t) i] = env * (float) std::sin (i * 0.37);
        r[(std::size_t) i] = env * (float) std::sin (i * 0.41);
    }
    {
        ImpulseAudio ir;
        ir.l = l;
        ir.r = r;
        ir.rate = 48000.0;
        const auto slot = convolverSlotOf (ir, 100.0, true);
        std::vector<std::uint8_t> fl, file;
        CHECK (slot.kind == EffectKind::convolver && convolver::unpackBlob (slot.state, fl, file) && file.empty());
        const auto st = convolver::parseState (fl);
        CHECK (st.has_value() && convolver::kernelSupported (*st));
        if (st)
        {
            const auto audio = convolver::decodeAudio (st->wav);
            CHECK (audio.has_value() && audio->l.size() == 4800 && near (audio->rate, 48000.0, 1.0e-9) && near (audio->r[100], r[100], 1.0e-7));
            CHECK (st->params[0] == 0 && st->params[1] == 1000 && st->flags[3] == 1);
            CHECK (near ((std::pow (7.0, st->knobs[1] / 1000.0) - 1.0) * 83.333333, 100.0, 0.3));
        }
        CHECK (slot.params[0] == 0.0f && slot.params[1] == 1.0f && near (slot.params[2], 0.5, 1.0e-6));
    }
    // a set's IR found on this machine: parallel (an inner rack: Convolution / Algorithm chains crossfaded by the blend) and
    // serial (the Convolver first, the algorithm fed by Send)
    const auto path = als190fm::writeIr ("ir.wav", l, r, 48000);
    const auto find = [&path] (const xml::Element& ref) { return ref.value ("FileRef/RelativePath") == "Samples/Test/ir.wav" ? path : std::string(); };
    const std::string common = par ("Algorithm_Decay", 2.0) + par ("DryWet", 1.0) + par ("ConvoAlgoBlend", 0.3) + flag ("Eq_On", false) + par ("Send", 0.5) +
                               "<Convolution_IrPostProcessingOn Value=\"false\" />";
    const auto par1 = als190fm::translate (als190fm::hybrid (1, 0, common), find);
    const auto* pr = par1 ? als190fm::rackOf (*par1) : nullptr;
    CHECK (pr != nullptr && pr->chains.size() == 1 && pr->chains[0].nodes.size() == 1);
    if (pr != nullptr && pr->chains[0].nodes.size() == 1)
    {
        const auto& blendNode = als190fm::nodeOf (par1->slot.patcher, *pr, 0, 0);
        const auto* inner = rack::rackOf (blendNode.patcher);
        CHECK (blendNode.kind == EffectKind::patcher && inner != nullptr && inner->chains.size() == 2);
        if (inner != nullptr && inner->chains.size() == 2)
        {
            CHECK (near (inner->chains[0].volume, 0.7, 1.0e-6) && near (inner->chains[1].volume, 0.3, 1.0e-6));
            CHECK (als190fm::nodeOf (blendNode.patcher, *inner, 0, 0).kind == EffectKind::convolver);
            CHECK (als190fm::nodeOf (blendNode.patcher, *inner, 1, 0).kind == EffectKind::atrium);
            CHECK (inner->macros[0].name == "Blend" && inner->macros[0].mappings.size() == 2 && near (blendNode.params[0], 0.3, 1.0e-6));
        }
        CHECK (pr->macros[hrBlend].name == "Blend" && als190cf::linkOf (*par1, "ConvoAlgoBlend") != nullptr);
        bool finite = false;
        CHECK (als190fm::energyThrough (par1->slot.patcher, 300.0, &finite) > 0.01 && finite);
    }
    const auto ser = als190fm::translate (als190fm::hybrid (0, 0, common), find);
    const auto* sr = ser ? als190fm::rackOf (*ser) : nullptr;
    CHECK (sr != nullptr && sr->chains[0].nodes.size() == 2);
    if (sr != nullptr && sr->chains[0].nodes.size() == 2)
    {
        CHECK (als190fm::nodeOf (ser->slot.patcher, *sr, 0, 0).kind == EffectKind::convolver);
        const auto& blendNode = als190fm::nodeOf (ser->slot.patcher, *sr, 0, 1);
        const auto* inner = rack::rackOf (blendNode.patcher);
        CHECK (inner != nullptr && inner->chains.size() == 2 && inner->chains[0].nodes.empty() && inner->chains[1].nodes.size() == 2);
        if (inner != nullptr && inner->chains.size() == 2 && inner->chains[1].nodes.size() == 2)
        {
            const auto& send = als190fm::nodeOf (blendNode.patcher, *inner, 1, 0);
            CHECK (send.kind == EffectKind::utility && near (send.params[0], (-6.0206 + 24.0) / 48.0, 1.0e-4));
            CHECK (als190fm::nodeOf (blendNode.patcher, *inner, 1, 1).params[1] == 0.0f); // (the Convolver's pre-delay serves both)
        }
    }
    // Convolution alone: the Convolver is the whole wet chain
    const auto conv = als190fm::translate (als190fm::hybrid (3, 0, common), find);
    const auto* cr = conv ? als190fm::rackOf (*conv) : nullptr;
    CHECK (cr != nullptr && cr->chains[0].nodes.size() == 1 && als190fm::nodeOf (conv->slot.patcher, *cr, 0, 0).kind == EffectKind::convolver);
    std::filesystem::remove (path);

    // an impulse response of Live's own install on this machine (read-only; skipped when Live is not installed)
    const auto live = std::string (als190fm::liveRoot()) + "/Builtin/Samples/Hybrid/ImpulseResponses/Hybrid_Early_Reflections_Ableton Studio Mid.aif";
    if (std::filesystem::exists (live))
    {
        std::ifstream in (live, std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const auto a = decodeImpulse (bytes);
        CHECK (a.has_value() && a->l.size() > 1000 && (a->rate == 44100.0 || a->rate == 48000.0 || a->rate == 96000.0));
        if (a)
        {
            double peak = 0.0;
            for (const auto v : a->l)
                peak = std::max (peak, (double) std::abs (v));
            CHECK (peak > 0.01 && peak <= 1.0);
        }
    }
}

TEST (als190fm_roar_becomes_a_rack)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190fm::near;
    using als190fm::par;
    using als190fm::flag;
    namespace em = mixsuite::ember;
    // Serial: stage 1 Diode Clipper (the Soft clip character) with a post low pass at 4 kHz, stage 2 Bit Crusher; tone,
    // compression, output −6 dB, 80 % wet
    const auto t = als190fm::translate (als190fm::roar (1, par ("Stage1_Shaper_Type", 3) + par ("Stage1_Shaper_Trim", 0.5) + flag ("Stage1_Filter_On", true) +
                                                               par ("Stage1_Filter_Type", 0) + par ("Stage1_Filter_Frequency", 4000.0) + par ("Stage1_Filter_Resonance", 0.1) +
                                                               flag ("Stage1_Filter_PreOn", false) + par ("Stage2_Shaper_Type", 2) + par ("Stage2_Shaper_Amount", 0.4) +
                                                               flag ("Stage2_Filter_On", false) + par ("Input_InputGain", 2.0) + par ("Input_ToneAmount", 0.5) +
                                                               par ("Input_ToneFrequency", 500.0) + par ("Output_CompressionAmount", 0.25) +
                                                               par ("Output_OutputGain", 0.5) + par ("Output_DryWet", 0.8)));
    const auto* r = t ? als190fm::rackOf (*t) : nullptr;
    CHECK (r != nullptr && r->name == "Roar" && r->chains.size() == 2);
    if (r == nullptr || r->chains.size() != 2)
        return;
    const auto& doc = t->slot.patcher;
    CHECK (r->chains[0].nodes.size() == 6); // tone, stage 1, its filter, stage 2, compressor, output
    if (r->chains[0].nodes.size() == 6)
    {
        const auto& tone = als190fm::nodeOf (doc, *r, 0, 0);
        const auto& s1 = als190fm::nodeOf (doc, *r, 0, 1);
        const auto& f1 = als190fm::nodeOf (doc, *r, 0, 2);
        const auto& s2 = als190fm::nodeOf (doc, *r, 0, 3);
        const auto& comp = als190fm::nodeOf (doc, *r, 0, 4);
        const auto& outGain = als190fm::nodeOf (doc, *r, 0, 5);
        CHECK (tone.kind == EffectKind::facetEq && facet::shape (tone.params[(std::size_t) facet::param (0, 0)]) == facet::Shape::tiltShelf);
        CHECK (near (facet::gainDb (tone.params[(std::size_t) facet::param (0, 2)]), 6.0, 1.0e-3));
        CHECK (s1.kind == EffectKind::ember && em::characterIndex (s1.params[(std::size_t) em::characterParam (0)]) == 18);
        CHECK (near (em::driveDb (s1.params[em::drive]), roarDriveDb (0.5), 0.01) && near (em::inputDb (s1.params[em::inputGain]), 6.02, 0.01));
        CHECK (near (s1.params[em::output], (float) ((-6.0206 + 18.0) / 36.0), 1.0e-4) && s1.params[em::autoGain] == 0.0f);
        CHECK (f1.kind == EffectKind::filter && near (f1.params[1], filterCutoffValue (4000.0), 1.0e-6) && f1.params[0] == filterTypeValue (0));
        CHECK (s2.kind == EffectKind::lofi && near (s2.params[1], lofiBitsValue (16.0 - 15.0 * 0.4), 1.0e-6));
        CHECK (comp.kind == EffectKind::crestCompressor && near (comp.params[dyn::crest::threshold], (float) ((-9.0 + 60.0) / 60.0), 1.0e-5));
        CHECK (outGain.kind == EffectKind::utility && near (outGain.params[0], (float) ((-6.0206 + 24.0) / 48.0), 1.0e-4));
    }
    CHECK (near (r->chains[0].volume, 0.8, 1.0e-6) && near (r->chains[1].volume, 0.2, 1.0e-6));
    CHECK (r->macros[rrDrive1].name == "Drive 1" && r->macros[rrDrive1].mappings.size() == 1 && r->macros[rrDrive2].mappings.empty());
    CHECK (r->macros[rrOutput].name == "Output" && r->macros[rrDryWet].name == "Dry/Wet");
    for (const auto& m : r->macros)
        for (const auto& map : m.mappings)
            if (map.target == RackMapping::Target::param)
                CHECK (near (rack::mappedValue (map, m.value), doc.nodes[(std::size_t) map.node].effect.params[(std::size_t) map.param], 2.0e-4));
    const auto* drive = als190cf::linkOf (*t, "Stage1_Shaper_Amount");
    CHECK (drive != nullptr && drive->param == rrDrive1 && als190cf::linkOf (*t, "Output_OutputGain") != nullptr);
    CHECK (als190fm::hasNote (t->notes.approximated, "Diode Clipper as Ember's Soft clip character") && als190fm::hasNote (t->notes.approximated, "Bit Crusher as Lo-fi"));
    CHECK (als190fm::hasNote (t->notes.dropped, "side-chain high pass"));
    bool finite = false;
    CHECK (als190fm::energyThrough (doc, 200.0, &finite) > 1.0 && finite);

    // Parallel: an inner rack, stage 1 / stage 2 chains at Blend 0.25 (stage 1 at unity, stage 2 at half)
    const auto p = als190fm::translate (als190fm::roar (2, par ("Stage1_Shaper_Type", 0) + par ("Stage2_Shaper_Type", 4) + par ("Stage2_Shaper_Amount", 0.2) +
                                                               par ("Input_Blend", 0.25) + par ("Output_DryWet", 1.0) + par ("Output_CompressionAmount", 0.0) +
                                                               "<ModulationMatrix_Stage1Amount_Env Value=\"0.5\" />"));
    const auto* pr = p ? als190fm::rackOf (*p) : nullptr;
    CHECK (pr != nullptr && pr->chains.size() == 1 && pr->chains[0].nodes.size() == 1);
    if (pr != nullptr && pr->chains[0].nodes.size() == 1)
    {
        const auto& node = als190fm::nodeOf (p->slot.patcher, *pr, 0, 0);
        const auto* inner = rack::rackOf (node.patcher);
        CHECK (inner != nullptr && inner->chains.size() == 2 && near (inner->chains[0].volume, 1.0, 1.0e-6) && near (inner->chains[1].volume, 0.5, 1.0e-6));
        if (inner != nullptr && inner->chains.size() == 2)
        {
            CHECK (als190fm::nodeOf (node.patcher, *inner, 1, 0).kind == EffectKind::ember);
            CHECK (em::styleOf (als190fm::nodeOf (node.patcher, *inner, 1, 0).params[em::style]) == em::Style::tube);
        }
        CHECK (pr->macros[rrDrive2].mappings.size() == 1 && near (node.params[1], 0.2, 1.0e-6));
    }
    CHECK (p.has_value() && als190fm::hasNote (p->notes.dropped, "modulation"));
    // Multi Band: three chains, each behind a crossover band (Linkwitz-Riley 24 dB/oct) at the two crossovers
    const auto mb = als190fm::translate (als190fm::roar (3, par ("Input_LowMidCrossover", 200.0) + par ("Input_MidHighCrossover", 2000.0) + par ("Output_DryWet", 1.0) +
                                                                par ("Stage3_Shaper_Type", 10)));
    const auto* mr = mb ? als190fm::rackOf (*mb) : nullptr;
    if (mr != nullptr && mr->chains[0].nodes.size() == 1)
    {
        const auto& node = als190fm::nodeOf (mb->slot.patcher, *mr, 0, 0);
        const auto* inner = rack::rackOf (node.patcher);
        CHECK (inner != nullptr && inner->chains.size() == 3);
        if (inner != nullptr && inner->chains.size() == 3)
        {
            const auto& mid = als190fm::nodeOf (node.patcher, *inner, 1, 0);
            CHECK (mid.kind == EffectKind::crossover && mid.params[0] == 1.0f && mid.params[3] == 1.0f);
            CHECK (near (30.0 * std::pow (2.0, 9.0589 * mid.params[1]), 200.0, 0.01) && near (30.0 * std::pow (2.0, 9.0589 * mid.params[4]), 2000.0, 0.1));
            CHECK (inner->chains[2].nodes.size() == 1); // (stage 3 Noise Injection: no counterpart, its crossover band stays)
        }
        CHECK (als190fm::hasNote (mb->notes.dropped, "Noise Injection"));
    }
    else
        CHECK (false);
    // Mid Side: mid and side matrices whose chains sum back to the input (stages left neutral: amount 0, no filters)
    const auto ms = als190fm::translate (als190fm::roar (4, par ("Stage1_Shaper_Amount", 0.0) + par ("Stage2_Shaper_Amount", 0.0) + flag ("Stage1_Filter_On", false) +
                                                                flag ("Stage2_Filter_On", false) + par ("Output_DryWet", 1.0)));
    CHECK (ms.has_value() && ms && als190fm::rackOf (*ms) != nullptr);
    if (const auto* mr2 = ms ? als190fm::rackOf (*ms) : nullptr; mr2 != nullptr && mr2->chains[0].nodes.size() == 1)
    {
        const auto& node = als190fm::nodeOf (ms->slot.patcher, *mr2, 0, 0);
        const auto* inner = rack::rackOf (node.patcher);
        CHECK (inner != nullptr && inner->chains.size() == 2);
        if (inner != nullptr && inner->chains.size() == 2)
        {
            const auto& side = als190fm::nodeOf (node.patcher, *inner, 1, 0);
            CHECK (side.kind == EffectKind::utility && near (side.params[3], (-0.5 + 2.0) / 4.0, 1.0e-6) && near (side.params[5], (0.5 + 2.0) / 4.0, 1.0e-6));
        }
    }
}

TEST (als190fm_grain_delay_becomes_echoform)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190fm::near;
    using als190fm::par;
    using als190fm::flag;
    namespace ef = echosuite::echoform;
    const auto t = als190fm::translate ("<GrainDelay Id=\"9\">" + als190fm::on() + flag ("SyncMode", false) + par ("MsDelay", 40.0) + par ("Feedback", 0.5) +
                                        par ("Pitch", 7.0) + par ("NewDryWet", 0.6) + par ("Spray", 20.0) + par ("Freq", 30.0) + par ("RandomPitch", 0.0) +
                                        par ("BeatDelayEnum", 1) + par ("BarDelayOffset", 0.0) + "</GrainDelay>");
    CHECK (t.has_value() && t->slot.kind == EffectKind::echoform && t->to == "Echoform (Pitch mode)");
    if (! t)
        return;
    const auto& p = t->slot.params;
    CHECK (ef::modeOf (p[ef::mode]) == ef::Mode::pitch && near (ef::sideMs (p, false, 120.0), 40.0, 0.05) && near (ef::sideMs (p, true, 120.0), 40.0, 0.05));
    CHECK (near (ef::feedbackGain (p[ef::feedback]), 0.5, 1.0e-4) && near (ef::pitchSemis (p[ef::pitch]), 7.0, 1.0e-4));
    CHECK (near (p[ef::mix], echoformMixOf (0.6, false), 1.0e-6));
    CHECK (als190cf::linkOf (*t, "Pitch") != nullptr && als190cf::linkOf (*t, "Feedback") != nullptr && als190cf::linkOf (*t, "MsDelay") != nullptr);
    CHECK (als190fm::hasNote (t->notes.dropped, "spray 20 ms") && als190fm::hasNote (t->notes.approximated, "grain frequency 30.0 Hz"));
    // synced: 4 sixteenths = a quarter note in Echoform's note sync; a pitch under −24 st held there
    const auto s = als190fm::translate ("<GrainDelay Id=\"9\">" + als190fm::on() + flag ("SyncMode", true) + par ("MsDelay", 1.0) + par ("Feedback", 0.0) +
                                        par ("Pitch", -30.0) + par ("NewDryWet", 1.0) + par ("BeatDelayEnum", 3) + par ("BarDelayOffset", 0.0) + "</GrainDelay>");
    CHECK (s.has_value() && s && near (ef::sideMs (s->slot.params, false, 120.0), 500.0, 0.05) && als190fm::hasNote (s->notes.approximated, "held at -24 st"));
}

TEST (als190fm_auto_filter_other_types)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190fm::near;
    using als190fm::par;
    using als190fm::flag;
    // the plan: notch, Morph at its corners (exact at the four, nearest between), DJ by sign, the rest none
    CHECK (autoFilterPlan (3, true, 0.0, 0.0).type == 3 && autoFilterPlan (0, false, 0.5, 0.0).type == 0);
    CHECK (autoFilterPlan (4, false, 0.25, 0.0).type == 2 && autoFilterPlan (4, false, 0.25, 0.0).exact);
    CHECK (autoFilterPlan (4, false, 0.5, 0.0).type == 1 && autoFilterPlan (4, true, 0.75, 0.0).type == 3 && autoFilterPlan (4, true, 1.0, 0.0).type == 0);
    CHECK (autoFilterPlan (4, false, 0.6, 0.0).type == 1 && ! autoFilterPlan (4, false, 0.6, 0.0).exact);
    CHECK (autoFilterPlan (5, true, 0.0, -0.5).type == 0 && near (autoFilterPlan (5, true, 0.0, -0.5).djHz, 20000.0 * std::sqrt (0.001), 0.01));
    CHECK (autoFilterPlan (5, true, 0.0, 0.5).type == 1 && near (autoFilterDjHz (0.5), 20.0 * std::sqrt (1000.0), 0.01));
    for (const int none : { 6, 7, 8, 9 })
        CHECK (autoFilterPlan (none, true, 0.0, 0.0).type == -1);
    CHECK (autoFilterPlan (5, false, 0.0, 0.0).type == -1); // (the older device has five types)
    // a notch (new device): Facet EQ band 1, its frequency and Q links
    const auto notch = als190fm::translate ("<AutoFilter2 Id=\"5\">" + als190fm::on() + par ("Filter_Type", 3) + par ("Filter_Frequency", 700.0) +
                                            par ("Filter_Resonance", 0.5) + par ("Filter_Slope", 0) + par ("Filter_Drive", 0.0) + par ("Filter_Circuit", 0) +
                                            par ("DryWet", 1.0) + par ("Lfo_Amount", 0.0) + par ("Envelope_Amount", 0.0) + "</AutoFilter2>");
    CHECK (notch.has_value() && notch && notch->slot.kind == EffectKind::facetEq && notch->to == "Facet EQ (notch)");
    if (notch)
    {
        const auto& p = notch->slot.params;
        CHECK (facet::shape (p[(std::size_t) facet::param (0, 0)]) == facet::Shape::notch && near (facet::hz (p[(std::size_t) facet::param (0, 1)]), 700.0, 0.05));
        CHECK (near (facet::q (p[(std::size_t) facet::param (0, 3)]), std::sqrt (2.0) * filterQOfResonance (0.5, false), 1.0e-3));
        const auto* f = als190cf::linkOf (*notch, "Filter_Frequency");
        CHECK (f != nullptr && f->param == facet::param (0, 1) && near (f->value (700.0), p[(std::size_t) facet::param (0, 1)], 1.0e-6));
        CHECK (! notch->modulator.has_value());
    }
    // the older device's notch (FilterType 3) and its Morph at 0.75 (notch) / 0.5 (high pass) / 0.3 (nearest: band pass)
    const auto older = [] (int type, double morph) {
        return als190fm::translate ("<AutoFilter Id=\"6\">" + als190fm::on() + par ("FilterType", type) + par ("Morph", morph) + par ("Cutoff", 81.0) +
                                    par ("Resonance", 0.2) + flag ("Slope", false) + par ("Drive", 0.0) + par ("CircuitLpHp", 0) + "</AutoFilter>");
    };
    CHECK (older (3, 0.0).has_value() && older (3, 0.0)->slot.kind == EffectKind::facetEq);
    CHECK (older (4, 0.75).has_value() && older (4, 0.75)->slot.kind == EffectKind::facetEq);
    const auto morphHp = older (4, 0.5);
    CHECK (morphHp.has_value() && morphHp->slot.kind == EffectKind::filter && morphHp->slot.params[0] == filterTypeValue (1));
    CHECK (morphHp.has_value() && als190fm::hasNote (morphHp->notes.approximated, "Morph 0.50 at its corner, the high pass"));
    const auto morphNear = older (4, 0.3);
    CHECK (morphNear.has_value() && morphNear->slot.params[0] == filterTypeValue (2) && als190fm::hasNote (morphNear->notes.approximated, "nearest corner, the band pass"));
    // DJ: a low pass for negative values, the cutoff from DjControl; its link follows DjControl
    const auto dj = als190fm::translate ("<AutoFilter2 Id=\"5\">" + als190fm::on() + par ("Filter_Type", 5) + par ("Filter_DjControl", -0.5) +
                                         par ("Filter_Frequency", 10000.0) + par ("Filter_Resonance", 0.0) + par ("Filter_Slope", 1) + par ("DryWet", 1.0) + "</AutoFilter2>");
    CHECK (dj.has_value() && dj && dj->slot.kind == EffectKind::filter && dj->slot.params[0] == filterTypeValue (0));
    if (dj)
    {
        CHECK (near (dj->slot.params[1], filterCutoffValue (632.456), 1.0e-4));
        const auto* l = als190cf::linkOf (*dj, "Filter_DjControl");
        CHECK (l != nullptr && l->param == 1 && als190cf::linkOf (*dj, "Filter_Frequency") == nullptr);
    }
    // comb, resampling, notch + low pass, vowel: not translated (listed by the importer)
    CHECK (! als190fm::translate ("<AutoFilter2 Id=\"5\">" + als190fm::on() + par ("Filter_Type", 6) + par ("Filter_Frequency", 100.0) + "</AutoFilter2>").has_value());
    // Drive: an Ember stage in front of the filter in a rack; the filter's linked parameters and the LFO's Peak Controller
    // reach it through the rack's macros
    const auto driven = als190fm::translate ("<AutoFilter2 Id=\"5\">" + als190fm::on() + par ("Filter_Type", 0) + par ("Filter_Frequency", 1000.0) +
                                             par ("Filter_Resonance", 0.3) + par ("Filter_Slope", 0) + par ("Filter_Drive", 0.5) + par ("Filter_Circuit", 2) +
                                             par ("DryWet", 1.0) + par ("Lfo_Amount", 0.25) + par ("Lfo_TimeMode", 0) + par ("Lfo_Frequency", 1.0) +
                                             par ("Envelope_Amount", 0.0) + "</AutoFilter2>");
    const auto* dr = driven ? als190fm::rackOf (*driven) : nullptr;
    CHECK (dr != nullptr && dr->chains[0].nodes.size() == 2 && driven->to.rfind ("Auto Filter rack", 0) == 0);
    if (dr != nullptr && dr->chains[0].nodes.size() == 2)
    {
        namespace em = mixsuite::ember;
        const auto& e = als190fm::nodeOf (driven->slot.patcher, *dr, 0, 0);
        const auto& f = als190fm::nodeOf (driven->slot.patcher, *dr, 0, 1);
        CHECK (e.kind == EffectKind::ember && near (em::driveDb (e.params[em::drive]), 12.0, 0.01) && f.kind == EffectKind::filter);
        const auto* freq = als190cf::linkOf (*driven, "Filter_Frequency");
        CHECK (freq != nullptr && freq->param >= 0 && freq->param < rackMacroCount);
        if (freq != nullptr && freq->param >= 0 && freq->param < rackMacroCount)
        {
            const auto& m = dr->macros[(std::size_t) freq->param];
            CHECK (m.mappings.size() == 1 && m.mappings[0].param == 1 && near (m.value / 127.0, f.params[1], 1.0e-6));
        }
        CHECK (driven->modulator.has_value() && driven->modulator && driven->modulator->param == freq->param);
        CHECK (als190cf::linkOf (*driven, "Filter_Drive") != nullptr && als190fm::hasNote (driven->notes.approximated, "drive 12.0 dB"));
    }
    // a notch under an LFO: the Peak Controller's curve moved onto Facet's frequency scale
    const auto wobble = als190fm::translate ("<AutoFilter2 Id=\"5\">" + als190fm::on() + par ("Filter_Type", 3) + par ("Filter_Frequency", 1000.0) +
                                             par ("Filter_Resonance", 0.3) + par ("DryWet", 1.0) + par ("Lfo_Amount", 0.25) + par ("Lfo_TimeMode", 0) +
                                             par ("Lfo_Frequency", 1.0) + par ("Envelope_Amount", 0.0) + "</AutoFilter2>");
    CHECK (wobble.has_value() && wobble && wobble->modulator.has_value());
    if (wobble && wobble->modulator)
    {
        const auto& c = wobble->modulator->curve;
        CHECK (wobble->modulator->param == facet::param (0, 1) && ! c.empty());
        // the curve's middle is the notch's frequency (1 kHz) on Facet's scale
        CHECK (near (facet::hz (c[c.size() / 2]), 1000.0, 30.0));
    }
}

TEST (als190fm_lot4_import_and_live_presets)
{
    using namespace lynx;
    using als190fm::par;
    using als190fm::flag;
    // through the importer: the three devices on a track become insert effects; the report lists the pairs as fitted
    FlpImportReport report;
    const auto song = als190ed::import (als190ed::set ({}, als190fm::roar (1, par ("Output_DryWet", 1.0)) +
                                                           "<GrainDelay Id=\"9\">" + als190fm::on() + flag ("SyncMode", false) + par ("MsDelay", 40.0) +
                                                           par ("Feedback", 0.5) + par ("Pitch", 7.0) + par ("NewDryWet", 0.6) + "</GrainDelay>" +
                                                           als190fm::hybrid (2, 0, par ("Algorithm_Decay", 2.0) + par ("DryWet", 0.4))),
                                        report);
    CHECK (song.has_value());
    CHECK (als190bq::reportHas (report.partialEffects, "Roar -> Roar rack") && als190bq::reportHas (report.partialEffects, "Grain Delay -> Echoform"));
    CHECK (als190bq::reportHas (report.partialEffects, "Hybrid Reverb -> Hybrid Reverb rack"));
    CHECK (! als190bq::reportHas (report.droppedEffects, "Roar") && ! als190bq::reportHas (report.droppedEffects, "Grain Delay") && ! als190bq::reportHas (report.droppedEffects, "Hybrid"));

    // Live's own Core Library presets on this machine (read-only; skipped when Live is not installed): every Hybrid
    // Reverb, Roar, Grain Delay and Auto Filter preset through the translators — Hybrid's impulse responses found in
    // Live's Builtin folder
    const auto devices = std::string (als190fm::liveRoot()) + "/Core Library/Devices/Audio Effects";
    if (! std::filesystem::exists (devices))
        return;
    std::map<std::string, std::pair<int, int>> counts; // tag → translated, total
    int withIr = 0;
    for (const auto* folder : { "Hybrid Reverb", "Roar", "Grain Delay", "Auto Filter" })
        for (const auto& e : std::filesystem::recursive_directory_iterator (devices + "/" + folder))
        {
            if (e.path().extension() != ".adv")
                continue;
            std::ifstream in (e.path(), std::ios::binary);
            const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
            const auto raw = gzip::isGzip (bytes.data(), bytes.size()) ? gzip::decompress (bytes.data(), bytes.size()) : std::optional<std::vector<std::uint8_t>> (bytes);
            xml::Document doc;
            if (! raw || ! doc.parse (std::string (raw->begin(), raw->end())))
                continue;
            // (Live 12.4 writes an .adv with the device right under <Ableton>)
            const auto wrapped = doc.root().path ("AbletonDevicePreset/Device");
            for (const auto& d : wrapped ? wrapped.children() : doc.root().children())
            {
                alsdev::Context context;
                context.findFile = [] (const xml::Element& ref) {
                    const auto p = std::string (als190fm::liveRoot()) + "/Builtin/" + ref.value ("FileRef/RelativePath");
                    return std::filesystem::exists (p) ? p : std::string();
                };
                const auto t = alsdev::effectFromLiveDevice (d, context);
                auto& c = counts[std::string (d.name())];
                ++c.second;
                if (t)
                {
                    ++c.first;
                    if (std::string (d.name()) == "Hybrid" && ! als190fm::hasNote (t->notes.dropped, "the convolution half"))
                        ++withIr;
                    if (rack::isRack (t->slot))
                    {
                        bool finite = false;
                        als190fm::energyThrough (t->slot.patcher, 300.0, &finite);
                        CHECK (finite);
                    }
                }
            }
        }
    for (const auto& [tag, c] : counts)
        std::printf ("  Core Library %s: %d of %d presets translated\n", tag.c_str(), c.first, c.second);
    std::printf ("  Hybrid presets with their impulse response loaded: %d\n", withIr);
    CHECK (counts["Hybrid"].first == counts["Hybrid"].second || counts["Hybrid"].second == 0 || counts["Hybrid"].first > counts["Hybrid"].second * 9 / 10);
    CHECK (counts["Roar"].first == counts["Roar"].second);
    CHECK (counts["GrainDelay"].first == counts["GrainDelay"].second);
}

TEST (als190fm_live_device_presets_open_as_effects)
{
    using namespace lynx;
    // a preset written here: an .adv (gzip XML, AbletonDevicePreset) holding a Grain Delay → Echoform
    {
        const std::string text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12402\"><AbletonDevicePreset>"
                                 "<OverwriteProtectionNumber Value=\"3077\" /><Device><GrainDelay Id=\"0\">" + als190fm::on() + als190fm::par ("MsDelay", 40.0) +
                                 als190fm::par ("Feedback", 0.3) + als190fm::par ("Pitch", 0.0) + als190fm::par ("NewDryWet", 0.5) +
                                 "<UserName Value=\"Mine\" /></GrainDelay></Device></AbletonDevicePreset></Ableton>";
        const std::vector<std::uint8_t> plain (text.begin(), text.end());
        FlpImportReport report;
        AlsImportOptions options;
        options.common.report = &report;
        std::string error;
        const auto p = importLiveDevicePreset (plain, options, &error);
        CHECK (p.has_value() && p->slot.kind == EffectKind::echoform && p->name == "Mine" && ! p->rack);
        CHECK (als190bq::reportHas (report.partialEffects, "Grain Delay -> Echoform"));
        // an instrument preset and a device with no counterpart: refused with the reason
        const std::string synth = "<?xml version=\"1.0\"?><Ableton><AbletonDevicePreset><Device><Operator Id=\"0\" /></Device></AbletonDevicePreset></Ableton>";
        CHECK (! importLiveDevicePreset (std::vector<std::uint8_t> (synth.begin(), synth.end()), options, &error).has_value() && error.find ("instrument") != std::string::npos);
        const std::string vocoder = "<?xml version=\"1.0\"?><Ableton><AbletonDevicePreset><Device><Vocoder Id=\"0\" /></Device></AbletonDevicePreset></Ableton>";
        CHECK (! importLiveDevicePreset (std::vector<std::uint8_t> (vocoder.begin(), vocoder.end()), options, &error).has_value() && error.find ("no LYNX counterpart") != std::string::npos);
        const std::string set = "<?xml version=\"1.0\"?><Ableton><LiveSet /></Ableton>";
        CHECK (! importLiveDevicePreset (std::vector<std::uint8_t> (set.begin(), set.end()), options, &error).has_value());
    }
    // Live's own presets on this machine (read-only, skipped without Live): a Roar .adv and every Audio Effect Rack .adg
    const auto lib = std::string (als190fm::liveRoot()) + "/Core Library";
    if (! std::filesystem::exists (lib))
        return;
    const auto readAll = [] (const std::filesystem::path& p) {
        std::ifstream in (p, std::ios::binary);
        return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    };
    if (const auto roar = lib + "/Devices/Audio Effects/Roar/Basic Parallel.adv"; std::filesystem::exists (roar))
    {
        const auto p = importLiveDevicePreset (readAll (roar));
        CHECK (p.has_value() && rack::isRack (p->slot));
    }
    int racks = 0, opened = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator (lib + "/Racks/Audio Effect Racks"))
        if (e.path().extension() == ".adg")
        {
            ++racks;
            if (const auto p = importLiveDevicePreset (readAll (e.path())); p && p->rack && rack::isRack (p->slot))
                ++opened;
        }
    std::printf ("  Core Library Audio Effect Racks opened: %d of %d\n", opened, racks);
    CHECK (racks == 0 || opened == racks);
}
