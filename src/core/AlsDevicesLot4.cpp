// Live's own devices → LYNX's, lot 4 (round 190fm; core/AlsDevices.h has the list and the evidence tags). The same rules
// as lots 1 – 3: each conversion is a law in a unit, what is fitted or has no counterpart becomes a note for the import
// report, only the device's saved state is read, and each translator records which Live parameter drives which LYNX
// parameter (ParamLink) so automation and rack macros follow. No Live render of these devices exists yet: every law that
// is not plain unit maths is [ASSUMED] and listed in the round's report with the settings to render. Menu orders come from
// Live's own Core Library presets read on this machine ([DATA]: a preset's name against the value it stores); nothing of
// those files (or of Live's impulse responses) is in LYNX: an impulse response is read at import time from the user's own
// files only.

#include "core/AlsDevices.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <tuple>

#include "core/ConvolverFx.h"
#include "core/DynamicsSuite.h"
#include "core/EchoSuite.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/FlSamplers.h"
#include "core/LynxVerbs.h"
#include "core/MixSuite.h"
#include "core/Rack.h"
#include "core/XmlLite.h"

namespace lynx::alsdev
{
namespace
{
using xml::Element;

constexpr double pi = 3.14159265358979323846;

float unit (double v) noexcept { return (float) std::clamp (v, 0.0, 1.0); }
float linValue (double v, double lo, double hi) noexcept { return unit ((v - lo) / (hi - lo)); }

std::string text (const char* format, double a, double b = 0.0, double c = 0.0)
{
    char out[240];
    std::snprintf (out, sizeof (out), format, a, b, c);
    return out;
}

double manual (const Element& device, const std::string& name, double fallback) { return device.number (name + "/Manual", fallback); }
bool manualBool (const Element& device, const std::string& name, bool fallback) { return device.boolean (name + "/Manual", fallback); }
bool has (const Element& device, const std::string& name) { return (bool) device.path (name + "/Manual"); }
// a parameter that moves with a rack macro of the set (KeyMidi on channel 16 [DATA, lot 3]) or carries an envelope cannot
// be seen from here; a macro on it is read by the importer through `links`
bool macroOn (const Element& device, const std::string& name) { return (int) device.number (name + "/KeyMidi/Channel", -1) == 16; }

// One Facet EQ band (Facet's prototypes, core/AlsDevices.h: bell / notch / 12 dB cut Q = √2 · Q, a shelf's cookbook Q =
// 0.7071 · Qfacet^0.4697)
void facetBand (EffectParams& p, int b, facet::Shape shape, double hz, double db, double q, int slope = 12)
{
    p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (shape);
    p[(std::size_t) facet::param (b, 1)] = facet::hzValue (hz);
    p[(std::size_t) facet::param (b, 2)] = facet::gainValue (facet::hasGain (shape) ? db : 0.0);
    const auto shelf = shape == facet::Shape::lowShelf || shape == facet::Shape::highShelf;
    p[(std::size_t) facet::param (b, 3)] = facet::qValue (shelf ? std::pow (q / 0.7071, 1.0 / 0.4697) : std::sqrt (2.0) * q);
    p[(std::size_t) facet::param (b, 4)] = facet::slopeValue (slope);
    p[(std::size_t) (facet::powerParam0 + b)] = 0.0f;
}

// A Utility slot holding a 2 × 2 matrix (and a gain in dB)
EffectSlot utilitySlot (const StereoMatrix& m, double gainDb = 0.0)
{
    auto u = makeEffectSlot (EffectKind::utility);
    auto& p = u.params;
    p[0] = std::max (1.0e-4f, linValue (gainDb, -24.0, 24.0));
    p[1] = 0.5f;
    p[2] = linValue (m.ll, -2.0, 2.0);
    p[3] = linValue (m.rl, -2.0, 2.0);
    p[4] = linValue (m.lr, -2.0, 2.0);
    p[5] = linValue (m.rr, -2.0, 2.0);
    return u;
}
StereoMatrix widthMatrix (double width) noexcept
{
    LiveUtility u;
    u.width = width;
    return utilityMatrix (u);
}

// Building a rack: devices on chains, macros whose straight line passes through each control's imported value (the
// patcher's macro wires drive the controls, so a line that missed it would change the sound — lot 3's rule)
struct RackBuilder
{
    PatcherDoc& doc;

    RackDoc& rack() { return *rack::rackOf (doc); }
    int add (int chain, EffectSlot&& fx, const std::string& label)
    {
        PatchNode node;
        node.effect = std::move (fx);
        node.label = label;
        return rack::addDevice (doc, chain, std::move (node));
    }
    float valueOf (int node, int param) const { return doc.nodes[(std::size_t) node].effect.params[(std::size_t) param]; }
    // the mapping min / max for a control that is `now` at the macro's place `t` and should be `atZero` / `atOne` at the
    // ends: from `atZero` when the line through `now` stays in 0 … 1, else back from `atOne`
    static std::pair<float, float> line (float atZero, float now, double t, float atOne)
    {
        if (t <= 0.01)
            return { now, atOne };
        if (t >= 0.99)
            return { atZero, now };
        if (const auto top = atZero + (now - atZero) / t; top >= 0.0 && top <= 1.0)
            return { atZero, (float) top };
        return { (float) std::clamp (now - (atOne - now) * t / (1.0 - t), 0.0, 1.0), atOne };
    }
    void mapParam (int macro, int node, int param, float atZero, float atOne, double t)
    {
        RackMapping m;
        m.node = node;
        m.param = param;
        std::tie (m.min, m.max) = line (atZero, valueOf (node, param), t, atOne);
        rack::map (doc, macro, m);
    }
    // a chain's volume between two gains (the mapping runs in the volume control's own 0..1)
    void mapVolume (int macro, int chain, float gainAtZero, float gainAtOne, double t)
    {
        RackMapping m;
        m.target = RackMapping::Target::chainVolume;
        m.chain = chain;
        std::tie (m.min, m.max) = line (rack::volumeValue (gainAtZero), rack::volumeValue (rack().chains[(std::size_t) chain].volume), t,
                                        rack::volumeValue (gainAtOne));
        rack::map (doc, macro, m);
    }
    void macro (int k, const char* name, double t)
    {
        auto& m = rack().macros[(std::size_t) k];
        m.name = name;
        m.value = (float) (127.0 * std::clamp (t, 0.0, 1.0));
    }
};

// A nested rack as a device of a chain
int addRackNode (RackBuilder& outer, int chain, PatcherDoc&& inner, const std::string& label)
{
    rack::compile (inner);
    auto slot = makeEffectSlot (EffectKind::patcher);
    // the node's parameters k = the inner rack's macros (0..1), as the outer rack's mappings read and drive them
    if (const auto* r = rack::rackOf (inner))
        for (int k = 0; k < rackMacroCount && k < (int) slot.params.size(); ++k)
            slot.params[(std::size_t) k] = r->macros[(std::size_t) k].value / 127.0f;
    slot.patcher = std::move (inner);
    return outer.add (chain, std::move (slot), label);
}

// The dry / wet pair of a rack (chain 0 the wet path): a "Dry" chain beside it when the device is not fully wet or a
// macro moves its Dry/Wet; the macro `k` crossfades them. [ASSUMED: Live's dry / wet as a linear crossfade, as Drum
// Buss measured in lot 3]. Returns the dry chain (−1 = none)
int dryWetChains (RackBuilder& b, const Element& device, const std::string& live, double dryWet, int k, Translated& out)
{
    if (dryWet >= 0.9999 && ! macroOn (device, live))
        return -1;
    const auto dry = rack::addChain (b.doc, "Dry");
    if (dry < 0)
        return -1;
    b.rack().chains[0].volume = (float) dryWet;
    b.rack().chains[(std::size_t) dry].volume = (float) (1.0 - dryWet);
    b.mapVolume (k, 0, 0.0f, 1.0f, dryWet);
    b.mapVolume (k, dry, 1.0f, 0.0f, dryWet);
    b.macro (k, "Dry/Wet", dryWet);
    out.links.push_back ({ live, k, [] (double v) { return unit (v); } });
    return dry;
}

std::string fileNameOf (const Element& sampleRef)
{
    auto p = sampleRef.value ("FileRef/RelativePath");
    if (p.empty())
        p = sampleRef.value ("FileRef/Path");
    if (const auto slash = p.find_last_of ("/\\"); slash != std::string::npos)
        p = p.substr (slash + 1);
    return p;
}

std::vector<std::uint8_t> readFile (const std::string& path)
{
    std::ifstream in (std::filesystem::path ((const char8_t*) path.c_str()), std::ios::binary);
    if (! in)
        return {};
    return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}

// little-endian writers for the Convolver's state
void putI (std::vector<std::uint8_t>& b, std::int32_t v)
{
    for (int k = 0; k < 4; ++k)
        b.push_back ((std::uint8_t) (((std::uint32_t) v >> (8 * k)) & 0xff));
}
void putD (std::vector<std::uint8_t>& b, double v)
{
    std::uint64_t u;
    std::memcpy (&u, &v, 8);
    for (int k = 0; k < 8; ++k)
        b.push_back ((std::uint8_t) ((u >> (8 * k)) & 0xff));
}
void putF (std::vector<std::uint8_t>& b, float v)
{
    std::uint32_t u;
    std::memcpy (&u, &v, 4);
    for (int k = 0; k < 4; ++k)
        b.push_back ((std::uint8_t) ((u >> (8 * k)) & 0xff));
}
// a flat FL envelope map (two points at `level`), the form the Convolver decodes (ConvolverFx.h kernelSupported)
void flatMap (std::vector<std::uint8_t>& b, int version, double span, double level)
{
    putI (b, version);
    putI (b, 3);
    putI (b, 2);
    putD (b, 0.0), putD (b, level), putF (b, 0.0f), putI (b, 0);
    putD (b, span), putD (b, level), putF (b, 0.0f), putI (b, 0x02000000);
    putI (b, 0);
    for (int k = 0; k < 4; ++k)
        putI (b, -1);
}
} // namespace

// ================================================================================================ Hybrid Reverb

double hybridPredelayMs (bool synced, double seconds, double sixteenths, double bpm) noexcept
{
    return synced ? std::max (sixteenths, 0.0) * 15000.0 / std::max (bpm, 1.0) : std::max (seconds, 0.0) * 1000.0;
}

int hybridEqSlopeDb (int index) noexcept { return 6 * (std::clamp (index, 0, 9) + 1); }

float haloSizeForDecay (double decaySec, double feedback, double shiftSemis, double diffusion) noexcept
{
    // Halo (core/LynxVerbs.cpp HaloFx, its Wide mode: RT scale 1): RT = (1.5 + 12.5 · size) · (plain + (shifted − plain) ·
    // f) · (0.55 + 0.45 · d³), plain = 1 + 3.3 · fb³, shifted = 1 + 1.2 · fb^2.5, f = min (1, |shift| / 2), d = diffusion / 0.92
    const auto fb = std::clamp (feedback, 0.0, 1.0), f = std::min (1.0, std::abs (shiftSemis) / 2.0);
    const auto mult = (1.0 + 3.3 * fb * fb * fb) + ((1.0 + 1.2 * std::pow (fb, 2.5)) - (1.0 + 3.3 * fb * fb * fb)) * f;
    const auto dn = std::clamp (diffusion / 0.92, 0.0, 1.0);
    const auto scale = mult * (0.55 + 0.45 * dn * dn * dn);
    return unit ((decaySec / std::max (scale, 1.0e-3) - 1.5) / 12.5);
}

std::optional<ImpulseAudio> decodeImpulse (const std::vector<std::uint8_t>& b)
{
    if (b.size() >= 12 && std::memcmp (b.data(), "RIFF", 4) == 0)
    {
        const auto a = convolver::decodeAudio (b);
        if (! a || a->l.empty())
            return std::nullopt;
        ImpulseAudio out;
        out.l = a->l;
        out.r = a->r.empty() ? a->l : a->r;
        out.rate = a->rate;
        return out;
    }
    // AIFF / AIFC: big-endian chunks; COMM (channels, frames, bits, 80-bit rate[, compression]) and SSND
    if (b.size() < 12 || std::memcmp (b.data(), "FORM", 4) != 0 || (std::memcmp (b.data() + 8, "AIFF", 4) != 0 && std::memcmp (b.data() + 8, "AIFC", 4) != 0))
        return std::nullopt;
    const auto be16 = [&b] (std::size_t o) { return (int) (std::int16_t) (std::uint16_t) ((b[o] << 8) | b[o + 1]); };
    const auto be32 = [&b] (std::size_t o) { return (std::uint32_t) b[o] << 24 | (std::uint32_t) b[o + 1] << 16 | (std::uint32_t) b[o + 2] << 8 | b[o + 3]; };
    int channels = 0, bits = 0;
    std::uint32_t frames = 0;
    double rate = 0.0;
    std::string compression = "NONE";
    std::size_t data = 0, dataEnd = 0;
    for (std::size_t o = 12; o + 8 <= b.size();)
    {
        const auto size = (std::size_t) be32 (o + 4);
        const auto body = o + 8;
        if (body + size > b.size())
            break;
        if (std::memcmp (b.data() + o, "COMM", 4) == 0 && size >= 18)
        {
            channels = be16 (body);
            frames = be32 (body + 2);
            bits = be16 (body + 6);
            const auto exponent = (int) (((b[body + 8] & 0x7f) << 8) | b[body + 9]);
            std::uint64_t mantissa = 0;
            for (int k = 0; k < 8; ++k)
                mantissa = mantissa << 8 | b[body + 10 + (std::size_t) k];
            rate = std::ldexp ((double) mantissa, exponent - 16383 - 63);
            if (size >= 22)
                compression.assign ((const char*) b.data() + body + 18, 4);
        }
        else if (std::memcmp (b.data() + o, "SSND", 4) == 0 && size >= 8)
        {
            data = body + 8 + be32 (body);
            dataEnd = body + size;
        }
        o = body + size + (size & 1);
    }
    const bool little = compression == "sowt";
    const bool asFloat = compression == "fl32" || compression == "FL32";
    if (asFloat)
        bits = 32;
    if (channels < 1 || frames == 0 || rate <= 0.0 || data == 0 || data > dataEnd || (! little && ! asFloat && compression != "NONE")
        || (bits != 8 && bits != 16 && bits != 24 && bits != 32))
        return std::nullopt;
    const auto bytesPer = (std::size_t) bits / 8;
    const auto count = std::min ((std::size_t) frames, (dataEnd - data) / (bytesPer * (std::size_t) channels));
    ImpulseAudio out;
    out.rate = rate;
    out.l.resize (count);
    out.r.resize (count);
    for (std::size_t i = 0; i < count; ++i)
        for (int c = 0; c < std::min (channels, 2); ++c)
        {
            const auto at = data + (i * (std::size_t) channels + (std::size_t) c) * bytesPer;
            std::uint32_t u = 0;
            for (std::size_t k = 0; k < bytesPer; ++k)
                u = u << 8 | b[at + (little ? bytesPer - 1 - k : k)];
            float v = 0.0f;
            if (asFloat)
                std::memcpy (&v, &u, 4);
            else
            {
                const auto shift = 32 - bits;
                v = (float) ((double) (std::int32_t) (u << shift) / 2147483648.0);
            }
            (c == 0 ? out.l : out.r)[i] = v;
        }
    if (channels == 1)
        out.r = out.l;
    return out;
}

void shapeImpulse (ImpulseAudio& ir, double attackSec, double decaySec, double size)
{
    if (ir.l.empty() || ir.rate <= 0.0)
        return;
    // size: the response stretched (resampled, its pitch moves with it)
    if (size > 0.0 && std::abs (size - 1.0) > 1.0e-3)
    {
        const auto n = (std::size_t) std::max (1.0, std::round ((double) ir.l.size() * size));
        for (auto* ch : { &ir.l, &ir.r })
        {
            std::vector<float> s (n);
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto x = (double) i / size;
                const auto k = (std::size_t) x;
                const auto f = (float) (x - (double) k);
                const auto a = k < ch->size() ? (*ch)[k] : 0.0f, c = k + 1 < ch->size() ? (*ch)[k + 1] : 0.0f;
                s[i] = a + (c - a) * f;
            }
            *ch = std::move (s);
        }
    }
    const auto attack = std::max (attackSec, 0.0) * ir.rate;
    const auto fall = decaySec < 19.99 ? std::max (decaySec, 0.001) * ir.rate : 0.0;
    for (std::size_t i = 0; i < ir.l.size(); ++i)
    {
        double g = 1.0;
        if (attack > 1.0 && (double) i < attack)
            g *= (double) i / attack;
        if (fall > 0.0)
            g *= std::pow (10.0, -3.0 * (double) i / fall); // −60 dB at the decay time
        ir.l[i] = (float) (ir.l[i] * g);
        ir.r[i] = (float) (ir.r[i] * g);
    }
    if (fall > 0.0) // what is under −90 dB is cut off
        if (const auto keep = (std::size_t) std::ceil (1.5 * fall); keep < ir.l.size())
            ir.l.resize (keep), ir.r.resize (keep);
}

EffectSlot convolverSlotOf (const ImpulseAudio& ir, double predelayMs, bool normalise)
{
    const auto wav = makeWavFloat ({ ir.l, ir.r }, (int) std::lround (ir.rate));
    // a version-15 Fruity Convolver state (the layout ConvolverFx.cpp parseState reads): dry 0, wet 1000 (unity), no
    // separation; the embedded WAV; a neutral editor header; four flat time envelopes and a flat EQ curve; normalise
    // flag; the knobs page, pre-delay ((7^x − 1) · 83.33 ms), blend, speed, EQ amount
    std::vector<std::uint8_t> s;
    putI (s, 0xf0003);
    for (const int v : { 0, 1000, 0, 0 })
        putI (s, v);
    s.push_back (0);
    s.push_back (0);
    putI (s, (std::int32_t) wav.size());
    s.insert (s.end(), wav.begin(), wav.end());
    const auto secs = (double) ir.l.size() / ir.rate;
    putD (s, -1.0), putD (s, -1.0), putD (s, 0.0), putD (s, secs), putF (s, 0.02f);
    for (int k = 0; k < 4; ++k)
        flatMap (s, 6, secs, 0.5);
    flatMap (s, 3, 1.0, 0.5);
    s.push_back (0), s.push_back (0), s.push_back (1), s.push_back (normalise ? 1 : 0);
    putI (s, 0);
    const auto pre = (int) std::lround (1000.0 * std::log (1.0 + std::clamp (predelayMs, 0.0, 500.0) / 83.333333333333329) / std::log (7.0));
    for (const int v : { 0, std::clamp (pre, 0, 1000), 0, 0, 1000 })
        putI (s, v);
    auto slot = makeEffectSlot (EffectKind::convolver);
    convolver::toParams ({ 0, 1000, 0, 0 }, slot.params);
    slot.state = convolver::packBlob (s, {});
    return slot;
}

bool hybridToRack (const Element& device, const Context& context, Translated& out)
{
    if (! has (device, "Algorithm_Decay") || ! has (device, "Routing"))
        return false;
    auto& notes = out.notes;
    const auto routing = (HybridRouting) std::clamp ((int) manual (device, "Routing", 0), 0, 3);
    const auto algorithm = (HybridAlgorithm) std::clamp ((int) manual (device, "Algorithm_Type", 0), 0, 4);
    const auto dryWet = std::clamp (manual (device, "DryWet", 1.0), 0.0, 1.0);
    const auto blend = std::clamp (manual (device, "ConvoAlgoBlend", 0.5), 0.0, 1.0);
    const auto useAlgo = routing != HybridRouting::convolution;
    auto useConvo = routing != HybridRouting::algorithm;
    static const char* const algoNames[5] { "Dark Hall", "Quartz", "Shimmer", "Tides", "Prism" };

    // the pre-delay (before both halves [ASSUMED])
    const auto synced = manualBool (device, "PreDelay_Sync", false);
    auto predelay = hybridPredelayMs (synced, manual (device, "PreDelay_Time", 0.01), manual (device, "PreDelay_Sixteenth", 2.0), context.bpm);
    if (predelay > 500.0)
    {
        notes.approximated.push_back (text ("pre-delay %.0f ms held at 500 ms (the most LYNX's reverbs take)", predelay));
        predelay = 500.0;
    }
    else if (synced)
        notes.approximated.push_back (text ("synced pre-delay held at %.1f ms (the set's tempo)", predelay));
    if (manual (device, synced ? "PreDelay_FeedbackSixteenth" : "PreDelay_FeedbackTime", 0.0) > 1.0e-4)
        notes.dropped.push_back ("pre-delay feedback");

    // ---- the convolution half: the impulse response when the user's file is on this machine (never shipped)
    std::optional<ImpulseAudio> ir;
    if (useConvo)
    {
        const auto ref = device.path ("ImpulseResponseHandler/SampleSlot/Value/SampleRef");
        const auto refR = device.path ("ImpulseResponseHandler/SampleSlotTrueStereo/Value/SampleRef");
        const auto name = ref ? fileNameOf (ref) : std::string();
        const auto path = ref && context.findFile ? context.findFile (ref) : std::string();
        if (! path.empty())
            ir = decodeImpulse (readFile (path));
        if (ir && refR)
        {
            // a second file for the right channel (Live's true-stereo slot): its first channel becomes the right IR
            if (const auto pathR = context.findFile (refR); ! pathR.empty())
                if (const auto right = decodeImpulse (readFile (pathR)); right && right->rate == ir->rate)
                {
                    ir->r = right->l;
                    const auto n = std::max (ir->l.size(), ir->r.size());
                    ir->l.resize (n), ir->r.resize (n);
                    notes.approximated.push_back ("true-stereo impulse response as a left and a right response (no cross paths)");
                }
        }
        if (! ir)
        {
            if (routing == HybridRouting::convolution)
                return false; // nothing left to play: the importer lists the device
            notes.dropped.push_back ("the convolution half: its impulse response " + (name.empty() ? std::string ("(none)") : "\"" + name + "\"")
                                     + (path.empty() ? " is not on this machine" : " could not be read"));
            useConvo = false;
        }
        else
        {
            if (manualBool (device, "Convolution_IrPostProcessingOn", device.boolean ("Convolution_IrPostProcessingOn", true)))
            {
                const auto attack = device.number ("Convolution_IrAttackTime", 0.0), decay = device.number ("Convolution_IrDecayTime", 20.0),
                           size = device.number ("Convolution_IrSize", 1.0);
                shapeImpulse (*ir, attack, decay, size);
                if (attack > 1.0e-4 || decay < 19.99 || std::abs (size - 1.0) > 1.0e-3)
                    notes.approximated.push_back (text ("impulse response shaping (attack %.3f s, decay %.2f s, size %.2f): fade-in, fall to -60 dB and stretch", attack, decay, size));
            }
            notes.approximated.push_back ("impulse response \"" + name + "\" level normalised (the Convolver's own normalisation)");
        }
    }

    // ---- the algorithm half: Atrium for Dark Hall / Quartz / Tides / Prism, Halo for Shimmer
    const auto decay = std::clamp (manual (device, "Algorithm_Decay", 3.5), 0.1, 60.0);
    const auto size = std::clamp (manual (device, "Algorithm_Size", 0.5), 0.0, 1.0);
    const auto damping = std::clamp (manual (device, "Algorithm_Damping", 0.5), 0.0, 1.0);
    const auto diffusion = std::clamp (manual (device, "Algorithm_Diffusion", 1.0), 0.0, 1.0);
    const auto modulation = std::clamp (manual (device, "Algorithm_Modulation", 0.5), 0.0, 1.0);
    const bool halo = algorithm == HybridAlgorithm::shimmer;
    EffectSlot verb;
    double shimmerFb = 0.0, shiftSemis = 0.0;
    if (useAlgo)
    {
        if (halo)
        {
            // Shimmer [ASSUMED]: PitchShift (±12 st) = Halo's Shift (±12 st, the same range), the Shimmer amount = the
            // shifted share of the feedback, the decay through Halo's Size (its RT law), damping as its high cut
            verb = makeEffectSlot (EffectKind::halo);
            auto& p = verb.params;
            shiftSemis = std::clamp (manual (device, "Algorithm_PitchShift", 12.0), -12.0, 12.0);
            shimmerFb = std::clamp (manual (device, "Algorithm_Shimmer", 0.5), 0.0, 1.0);
            p[0] = 1.0f;
            p[1] = (float) ((shiftSemis + 12.0) / 24.0);
            p[2] = (float) shimmerFb;
            p[3] = (float) diffusion;
            p[4] = haloSizeForDecay (decay, shimmerFb, shiftSemis, diffusion);
            p[5] = 0.0f;
            p[6] = (float) (1.0 - 0.8 * damping);
            p[8] = (float) modulation;
            const auto shortest = 1.5 * 0.55, reached = (1.5 + 12.5 * p[4]);
            if (decay < shortest || std::abs (reached / std::max (decay, 0.1) - 1.0) > 0.5)
                notes.approximated.push_back (text ("decay %.2f s through Halo's size (Halo's tail is %.1f s or longer)", decay, shortest));
            if (predelay > 5.0)
                notes.dropped.push_back (text ("pre-delay %.0f ms of the shimmer (Halo has none)", predelay));
        }
        else
        {
            verb = makeEffectSlot (EffectKind::atrium);
            auto& p = verb.params;
            p[0] = 1.0f;
            p[1] = routing == HybridRouting::serial && useConvo ? 0.0f : linValue (predelay, 0.0, 500.0); // (serial: the Convolver delays both)
            p[2] = verbs::atrium::decayValue (decay);   // [ASSUMED: Live's Decay = the RT60 Atrium aims at]
            p[3] = 1.0f;                                // no high cut (the EQ section does it)
            p[4] = 1.0f;                                // late only: the early part is the convolution's
            p[5] = (float) size;                        // [ASSUMED 0 … 1 onto Atrium's late size]
            p[8] = (float) modulation;
            p[11] = (float) (1.0 - damping);            // high mult 1 … 0.1 [ASSUMED]
            p[18] = (float) diffusion;
            if (algorithm == HybridAlgorithm::darkHall)
            {
                const auto mult = manual (device, "Algorithm_BassMultiplier", 1.0), xover = manual (device, "Algorithm_BassCrossover", 440.0);
                p[9] = linValue (mult, 0.5, 2.0);
                p[10] = linValue (xover, 100.0, 10000.0);
                if (mult < 0.499 || mult > 2.001)
                    notes.approximated.push_back (text ("bass multiplier %.2f held at %.1f (Atrium: 0.5 … 2)", mult, std::clamp (mult, 0.5, 2.0)));
                if (xover < 99.0)
                    notes.approximated.push_back (text ("bass crossover %.0f Hz held at 100 Hz", xover));
            }
            else if (algorithm == HybridAlgorithm::prism)
            {
                // [ASSUMED] Prism's two multipliers and their crossover = Atrium's bass / high multipliers at that split
                const auto lo = manual (device, "Algorithm_Prism_LowMultiplier", 1.0), hi = manual (device, "Algorithm_Prism_HighMultiplier", 1.0);
                const auto xover = manual (device, "Algorithm_Prism_CrossoverFrequency", 800.0);
                p[9] = linValue (lo, 0.5, 2.0);
                p[10] = linValue (xover, 100.0, 10000.0);
                p[11] = linValue (hi, 0.1, 1.0);
                p[12] = linValue (xover, 100.0, 15000.0);
                if (lo < 0.499 || lo > 2.001 || hi > 1.001)
                    notes.approximated.push_back (text ("Prism multipliers low %.2f / high %.2f held at Atrium's 0.5 … 2 / 0.1 … 1", lo, hi));
                if (std::abs (damping - 0.5) > 0.01)
                    notes.dropped.push_back ("damping (Prism's multipliers take Atrium's high multiplier)");
            }
            else if (algorithm == HybridAlgorithm::quartz)
            {
                if (std::abs (manual (device, "Algorithm_Quartz_LowDamping", 0.5) - 0.5) > 0.01 || std::abs (manual (device, "Algorithm_Quartz_Distance", 0.5) - 0.5) > 0.01)
                    notes.dropped.push_back ("Quartz low damping / distance");
            }
            else if (algorithm == HybridAlgorithm::tides)
                notes.dropped.push_back ("Tides (its moving spectral modulation: amount, rate, waveform, phase)");
            if (std::abs (manual (device, "Algorithm_Shape", 0.5) - 0.5) > 0.01)
                notes.dropped.push_back ("Shape");
        }
        notes.approximated.push_back (std::string ("algorithm ") + algoNames[(int) algorithm] + " as " + (halo ? "Halo" : "Atrium"));
        if (manual (device, "Algorithm_Delay", 0.0) > 1.0e-4)
            notes.dropped.push_back ("the algorithm's own delay");
        if (manualBool (device, "Algorithm_Freeze", false) || manualBool (device, "Algorithm_FreezeIn", false))
            notes.dropped.push_back ("Freeze");
    }

    // ---- the EQ section [ASSUMED: band type 0 a cut (its slope), 1 a shelf; Peak 1 / 2 cookbook bells; gains linear;
    // PreAlgo = in front of the algorithm, else on the whole wet signal]
    EffectSlot eq;
    bool eqUsed = false;
    const auto eqPre = manualBool (device, "Eq_PreAlgo", false);
    if (manualBool (device, "Eq_On", true))
    {
        eq = makeEffectSlot (EffectKind::facetEq);
        auto& p = eq.params;
        const auto band = [&] (int b, const char* stem, bool low) {
            const auto shelf = (int) manual (device, std::string ("Eq_") + stem + "BandType", 0) == 1;
            const auto hz = manual (device, std::string ("Eq_") + stem + "BandFrequency", low ? 80.0 : 12000.0);
            const auto db = dbOfGain (manual (device, std::string ("Eq_") + stem + "BandGain", 1.0));
            if (shelf && std::abs (db) < 0.01)
                return;
            if (shelf)
                facetBand (p, b, low ? facet::Shape::lowShelf : facet::Shape::highShelf, hz, db, 0.7071);
            else
                facetBand (p, b, low ? facet::Shape::lowCut : facet::Shape::highCut, hz, 0.0, 0.7071,
                           hybridEqSlopeDb ((int) manual (device, std::string ("Eq_") + stem + "BandSlope", 4)));
            eqUsed = true;
        };
        band (0, "Low", true);
        band (3, "High", false);
        for (int k = 1; k <= 2; ++k)
        {
            const auto stem = "Eq_Peak" + std::to_string (k);
            const auto db = dbOfGain (manual (device, stem + "Gain", 1.0));
            if (std::abs (db) < 0.01)
                continue;
            facetBand (p, k, facet::Shape::bell, manual (device, stem + "Frequency", k == 1 ? 700.0 : 1500.0), db, manual (device, stem + "Q", 0.7071));
            eqUsed = true;
        }
        if (eqUsed)
            notes.approximated.push_back ("EQ section as Facet EQ (band shapes and slopes fitted)");
    }

    // ---- stereo and the rest
    const auto width = std::clamp (manual (device, "StereoWidth", 1.0), 0.0, 2.0);
    const auto widthOn = std::abs (width - 1.0) > 1.0e-3 || macroOn (device, "StereoWidth");
    if (manualBool (device, "BassMono", false))
        notes.dropped.push_back ("Bass Mono");
    if (const auto vintage = (int) manual (device, "Vintage", 0); vintage > 0)
    {
        static const char* const names[5] { "Off", "Subtle", "Old", "Older", "Extreme" };
        notes.dropped.push_back (std::string ("Vintage ") + names[std::clamp (vintage, 0, 4)]);
    }
    const auto send = std::clamp (manual (device, "Send", 1.0), 0.0, 1.0);

    // ---- the rack: chain 0 the wet path, an inner rack where the two halves meet, a dry chain beside
    out.slot = rack::makeRackSlot (RackDoc::Kind::audioEffect);
    RackBuilder b { out.slot.patcher };
    b.rack().chains.front().name = "Hybrid Reverb";
    b.rack().name = "Hybrid Reverb";
    int verbNode = -1, eqNode = -1, widthNode = -1, blendNode = -1;
    const bool both = useAlgo && useConvo;
    bool eqPlaced = false;
    if (both)
    {
        // Serial: the Convolver, then the algorithm fed by Send; Parallel: both from the input. The blend crossfades the
        // convolution and the algorithm [ASSUMED linear: 0 convolution only, 1 algorithm only — DATA: the "Throat
        // Texture" presets (vocal IRs) sit at 0]
        if (routing == HybridRouting::serial)
            b.add (0, convolverSlotOf (*ir, predelay, true), "Convolution");
        auto inner = rack::makeRack (RackDoc::Kind::audioEffect);
        RackBuilder ib { inner };
        ib.rack().chains.front().name = "Convolution";
        if (routing == HybridRouting::parallel)
            ib.add (0, convolverSlotOf (*ir, predelay, true), "Convolution");
        const auto algoChain = rack::addChain (inner, "Algorithm");
        if (routing == HybridRouting::serial && send < 0.9999)
            ib.add (algoChain, utilitySlot ({}, dbOfGain (send)), "Send");
        if (eqUsed && eqPre)
        {
            ib.add (algoChain, std::move (eq), "EQ");
            eqPlaced = true;
        }
        const auto innerVerb = ib.add (algoChain, std::move (verb), halo ? "Shimmer" : "Algorithm");
        (void) innerVerb;
        ib.rack().chains[0].volume = (float) (1.0 - blend);
        ib.rack().chains[(std::size_t) algoChain].volume = (float) blend;
        ib.mapVolume (0, 0, 1.0f, 0.0f, blend);
        ib.mapVolume (0, algoChain, 0.0f, 1.0f, blend);
        ib.macro (0, "Blend", blend);
        ib.rack().shownMacros = 8;
        blendNode = addRackNode (b, 0, std::move (inner), "Blend");
        notes.approximated.push_back (text (routing == HybridRouting::serial ? "serial: the Convolver feeds the algorithm (send %.0f %%), blend %.0f %% as a crossfade"
                                                                             : "parallel: blend %.0f %% as a crossfade of the two halves", routing == HybridRouting::serial ? 100.0 * send : 100.0 * blend, 100.0 * blend));
    }
    else if (useConvo)
        b.add (0, convolverSlotOf (*ir, predelay, true), "Convolution");
    else
    {
        if (eqUsed && eqPre)
        {
            eqNode = b.add (0, std::move (eq), "EQ");
            eqPlaced = true;
        }
        verbNode = b.add (0, std::move (verb), halo ? "Shimmer" : "Algorithm");
    }
    if (eqUsed && ! eqPlaced)
        eqNode = b.add (0, std::move (eq), "EQ");
    if (widthOn)
        widthNode = b.add (0, utilitySlot (widthMatrix (width)), "Width");
    (void) eqNode;
    const auto dry = dryWetChains (b, device, "DryWet", dryWet, hrDryWet, out);

    // the macros: Live's controls over their whole range (the algorithm's when the algorithm stands alone in the chain)
    if (verbNode >= 0)
    {
        if (halo)
        {
            const auto sizeAt = [&] (double d) { return haloSizeForDecay (d, shimmerFb, shiftSemis, diffusion); };
            b.mapParam (hrDecay, verbNode, 4, sizeAt (0.1), sizeAt (60.0), (decay - 0.1) / 59.9);
            b.mapParam (hrDiffusion, verbNode, 3, 0.0f, 1.0f, diffusion);
            b.mapParam (hrModulation, verbNode, 8, 0.0f, 1.0f, modulation);
            b.mapParam (hrDamping, verbNode, 6, 1.0f, 0.2f, damping);
        }
        else
        {
            b.mapParam (hrDecay, verbNode, 2, verbs::atrium::decayValue (0.1), verbs::atrium::decayValue (60.0), (decay - 0.1) / 59.9);
            b.mapParam (hrSize, verbNode, 5, 0.0f, 1.0f, size);
            b.mapParam (hrDiffusion, verbNode, 18, 0.0f, 1.0f, diffusion);
            b.mapParam (hrModulation, verbNode, 8, 0.0f, 1.0f, modulation);
            if (algorithm != HybridAlgorithm::prism)
                b.mapParam (hrDamping, verbNode, 11, 1.0f, 0.0f, damping);
            b.macro (hrSize, "Size", size);
            out.links.push_back ({ "Algorithm_Size", hrSize, [] (double v) { return unit (v); } });
            if (algorithm != HybridAlgorithm::prism)
            {
                b.macro (hrDamping, "Damping", damping);
                out.links.push_back ({ "Algorithm_Damping", hrDamping, [] (double v) { return unit (v); } });
            }
        }
        if (halo)
        {
            b.macro (hrDamping, "Damping", damping);
            out.links.push_back ({ "Algorithm_Damping", hrDamping, [] (double v) { return unit (v); } });
        }
        b.macro (hrDecay, "Decay", (decay - 0.1) / 59.9);
        b.macro (hrDiffusion, "Diffusion", diffusion);
        b.macro (hrModulation, "Modulation", modulation);
        out.links.push_back ({ "Algorithm_Decay", hrDecay, [] (double s) { return unit ((s - 0.1) / 59.9); } });
        out.links.push_back ({ "Algorithm_Diffusion", hrDiffusion, [] (double v) { return unit (v); } });
        out.links.push_back ({ "Algorithm_Modulation", hrModulation, [] (double v) { return unit (v); } });
        notes.approximated.push_back ("the Decay macro moves the reverb's decay in a straight line between its ends (Live's law in between not carried)");
    }
    if (blendNode >= 0)
    {
        b.mapParam (hrBlend, blendNode, 0, 0.0f, 1.0f, blend);
        b.macro (hrBlend, "Blend", blend);
        out.links.push_back ({ "ConvoAlgoBlend", hrBlend, [] (double v) { return unit (v); } });
    }
    if (widthNode >= 0)
    {
        // the width's matrix is linear in the width: L→L = R→R = (1 + w) / 2, R→L = L→R = (1 − w) / 2 (0 … 2 → the macro)
        const auto lo = widthMatrix (0.0), hi = widthMatrix (2.0);
        b.mapParam (hrWidth, widthNode, 2, linValue (lo.ll, -2.0, 2.0), linValue (hi.ll, -2.0, 2.0), width / 2.0);
        b.mapParam (hrWidth, widthNode, 3, linValue (lo.rl, -2.0, 2.0), linValue (hi.rl, -2.0, 2.0), width / 2.0);
        b.mapParam (hrWidth, widthNode, 4, linValue (lo.lr, -2.0, 2.0), linValue (hi.lr, -2.0, 2.0), width / 2.0);
        b.mapParam (hrWidth, widthNode, 5, linValue (lo.rr, -2.0, 2.0), linValue (hi.rr, -2.0, 2.0), width / 2.0);
        b.macro (hrWidth, "Width", width / 2.0);
        out.links.push_back ({ "StereoWidth", hrWidth, [] (double v) { return unit (v / 2.0); } });
    }
    b.rack().shownMacros = 8;
    rack::compile (out.slot.patcher);
    if (dry >= 0)
        notes.approximated.push_back (text ("dry/wet %.0f %% as a dry chain beside the reverb (a linear crossfade)", 100.0 * dryWet));
    return true;
}

// ================================================================================================ Roar

const char* roarShaperName (int type) noexcept
{
    static const char* const names[12] { "Soft Sine", "Digital Clip", "Bit Crusher", "Diode Clipper", "Tube Preamp", "Half Wave Rectifier",
                                         "Full Wave Rectifier", "Polynomial", "Fractal", "Tri Fold", "Noise Injection", "Shards" };
    return names[std::clamp (type, 0, 11)];
}

RoarShaperFit roarShaperFit (int type) noexcept
{
    using K = RoarShaperFit::Kind;
    switch (type)
    {
        case 0: return { K::ember, saturatorFit (1).style, 0 };  // Soft Sine: Saturator's Soft Sine fit (transistor)
        case 1: return { K::ember, saturatorFit (6).style, 0 };  // Digital Clip: Saturator's Digital Clip fit (transistor)
        case 2: return { K::lofi, 0, 0 };                        // Bit Crusher: Lo-fi's bit reduction
        case 3: return { K::ember, 2, 18 };                      // Diode Clipper: the Soft clip character
        case 4: return { K::ember, 0, 0 };                       // Tube Preamp: the tube style
        case 6: return { K::ember, 2, 27 };                      // Full Wave Rectifier: the Octave rectifier character (|x|)
        case 7: return { K::ember, 1, 0 };                       // Polynomial: the tape style (a smooth odd curve)
        case 8: return { K::ember, 2, 26 };                      // Fractal: the Wavefold character
        case 9: return { K::ember, 2, 26 };                      // Tri Fold: the Wavefold character (a sine fold, not a triangle)
        default: return { K::none, 0, 0 };                       // Half Wave Rectifier, Noise Injection, Shards
    }
}

double roarDriveDb (double amount) noexcept { return 36.0 * std::clamp (amount, 0.0, 1.0); }

namespace
{
// Roar's stage filter as LYNX devices (empty = none or no counterpart); `name` for the report
std::vector<EffectSlot> roarFilter (const Element& device, const std::string& stem, Notes& notes)
{
    std::vector<EffectSlot> out;
    if (! has (device, stem + "Filter_Frequency") || ! manualBool (device, stem + "Filter_On", true))
        return out;
    auto type = std::clamp ((int) manual (device, stem + "Filter_Type", 0), 0, 8);
    const auto hz = std::clamp (manual (device, stem + "Filter_Frequency", 16000.0), 20.0, 20000.0);
    const auto res = std::clamp (manual (device, stem + "Filter_Resonance", 0.1), 0.0, 1.0);
    // [ASSUMED] the resonance → Q by Auto Filter's (new device) measured law
    const auto q = filterQOfResonance (res, false);
    if (type == (int) RoarFilter::morph)
    {
        // [ASSUMED] Morph's corners as Auto Filter's: 0 low, 0.25 band, 0.5 high, 0.75 notch pass
        const auto plan = autoFilterPlan (4, false, manual (device, stem + "Filter_Morph", 0.0), 0.0);
        type = plan.type == 0 ? 0 : plan.type == 2 ? 1 : plan.type == 1 ? 2 : 3;
        notes.approximated.push_back ("stage filter Morph at its nearest corner");
    }
    if (type == (int) RoarFilter::lowPass && hz >= 19900.0)
        return out; // open
    if (type <= (int) RoarFilter::highPass)
    {
        auto f = makeEffectSlot (EffectKind::filter);
        static constexpr int lynxType[3] { 0, 2, 1 }; // low, band, high → the Filter's 0 low, 1 high, 2 band
        f.params[0] = filterTypeValue (lynxType[type]);
        f.params[1] = filterCutoffValue (hz);
        f.params[2] = filterQValue (std::min (q, 12.0));
        f.params[3] = 1.0f;
        f.params[4] = 0.0f;
        out.push_back (std::move (f));
    }
    else if (type == (int) RoarFilter::notch || type == (int) RoarFilter::peak)
    {
        auto f = makeEffectSlot (EffectKind::facetEq);
        if (type == (int) RoarFilter::notch)
            facetBand (f.params, 0, facet::Shape::notch, hz, 0.0, q);
        else
            facetBand (f.params, 0, facet::Shape::bell, hz, dbOfGain (manual (device, stem + "Filter_PeakGain", 1.0)), q);
        out.push_back (std::move (f));
    }
    else
    {
        static const char* const names[3] { "Comb", "Resampling", "type 8" };
        notes.dropped.push_back (std::string ("stage filter ") + names[std::clamp (type - 6, 0, 2)]);
    }
    return out;
}

// One stage of Roar on a chain: its filter (before or after the shaper) and its shaper; returns the shaper's node (−1)
int roarStage (RackBuilder& b, int chain, const Element& device, int k, double inputDb, Notes& notes)
{
    namespace em = mixsuite::ember;
    const auto stem = "Stage" + std::to_string (k) + "_";
    auto filters = roarFilter (device, stem, notes);
    const auto pre = manualBool (device, stem + "Filter_PreOn", false);
    const auto place = [&] {
        for (auto& f : filters)
            b.add (chain, std::move (f), "Stage " + std::to_string (k) + " Filter");
        filters.clear();
    };
    if (pre)
        place();
    int node = -1;
    if (manualBool (device, stem + "Shaper_On", true))
    {
        const auto type = std::clamp ((int) manual (device, stem + "Shaper_Type", 0), 0, 11);
        const auto fit = roarShaperFit (type);
        const auto amount = std::clamp (manual (device, stem + "Shaper_Amount", 0.0), 0.0, 1.0);
        const auto trimDb = dbOfGain (manual (device, stem + "Shaper_Trim", 1.0));
        if (fit.kind == RoarShaperFit::Kind::ember)
        {
            auto e = makeEffectSlot (EffectKind::ember);
            auto& p = e.params;
            p[em::style] = (float) fit.style / (float) (em::styles - 1);
            if (fit.character > 0)
                p[(std::size_t) em::characterParam (0)] = (float) fit.character / (float) em::characters;
            p[em::autoGain] = 0.0f;
            p[(std::size_t) em::law] = 0.0f; // (rollup 0.1.28, 190fm x 190fj: the shaper fits are on the Classic curves, as Saturator's)
            p[em::drive] = linValue (roarDriveDb (amount), 0.0, 36.0);
            p[em::inputGain] = linValue (inputDb, -24.0, 24.0);
            p[em::output] = linValue (trimDb, -18.0, 18.0);
            p[em::mix] = 1.0f;
            node = b.add (chain, std::move (e), "Stage " + std::to_string (k));
            notes.approximated.push_back (std::string ("stage ") + std::to_string (k) + " shaper " + roarShaperName (type) + " as Ember's "
                                          + (fit.character > 0 ? std::string (em::characterName (fit.character)) + " character"
                                                             : std::string (fit.style == 0 ? "tube" : fit.style == 1 ? "tape" : "transistor") + " curve"));
            if (std::abs (trimDb) > 18.0)
                notes.approximated.push_back (text ("stage trim %.1f dB held at ±18 dB", trimDb));
        }
        else if (fit.kind == RoarShaperFit::Kind::lofi)
        {
            // [ASSUMED] Amount 0 … 1 as 16 … 1 bits
            auto l = makeEffectSlot (EffectKind::lofi);
            l.params[0] = lofiRateValue (44100.0);
            l.params[1] = lofiBitsValue (16.0 - 15.0 * amount);
            l.params[2] = 1.0f;
            b.add (chain, std::move (l), "Stage " + std::to_string (k));
            notes.approximated.push_back (text ("stage %.0f shaper Bit Crusher as Lo-fi at %.0f bits", (double) k, 16.0 - 15.0 * amount));
            if (std::abs (inputDb) > 0.05 || std::abs (trimDb) > 0.05)
                notes.dropped.push_back ("the bit crusher stage's input / trim gain");
        }
        else
            notes.dropped.push_back (std::string ("stage ") + std::to_string (k) + " shaper " + roarShaperName (type) + " (no LYNX counterpart)");
        if (std::abs (manual (device, stem + "Shaper_Bias", 0.0)) > 1.0e-3)
            notes.dropped.push_back ("stage " + std::to_string (k) + " bias");
    }
    if (! pre)
        place();
    return node;
}
} // namespace

bool roarToRack (const Element& device, Translated& out)
{
    namespace em = mixsuite::ember;
    namespace cr = dyn::crest;
    if (! has (device, "Stage1_Shaper_Amount") || ! has (device, "Output_DryWet"))
        return false;
    auto& notes = out.notes;
    auto routing = (RoarRouting) std::clamp ((int) device.number ("Input_RoutingMode", 1), 0, 5);
    const auto inputDb = dbOfGain (manual (device, "Input_InputGain", 1.0));
    const auto dryWet = std::clamp (manual (device, "Output_DryWet", 1.0), 0.0, 1.0);
    const auto blend = std::clamp (manual (device, "Input_Blend", 0.5), 0.0, 1.0);
    if (routing == RoarRouting::feedback)
        notes.dropped.push_back ("the Feedback routing's loop (its stages run in series)");

    out.slot = rack::makeRackSlot (RackDoc::Kind::audioEffect);
    RackBuilder b { out.slot.patcher };
    b.rack().chains.front().name = "Roar";
    b.rack().name = "Roar";

    // the input tone [ASSUMED: a tilt of ±12 dB × ToneAmount around ToneFrequency]
    const auto tone = std::clamp (manual (device, "Input_ToneAmount", 0.0), -1.0, 1.0);
    if (std::abs (tone) > 1.0e-3)
    {
        auto f = makeEffectSlot (EffectKind::facetEq);
        facetBand (f.params, 0, facet::Shape::tiltShelf, std::clamp (manual (device, "Input_ToneFrequency", 180.0), 20.0, 20000.0), 12.0 * tone, 0.7071);
        b.add (0, std::move (f), "Tone");
        notes.approximated.push_back (text ("input tone %.2f as a ±12 dB tilt", tone));
    }
    if (manualBool (device, "Input_ColorOn", false))
        notes.dropped.push_back ("Color");

    std::array<int, 3> shapers { -1, -1, -1 };
    const auto stageOn = [&device] (int k) { return manualBool (device, "Stage" + std::to_string (k) + "_On", true); };
    if (routing == RoarRouting::single || routing == RoarRouting::serial || routing == RoarRouting::feedback)
    {
        const int last = routing == RoarRouting::single ? 1 : 2;
        for (int k = 1; k <= last; ++k)
            if (stageOn (k))
                shapers[(std::size_t) k - 1] = roarStage (b, 0, device, k, k == 1 ? inputDb : 0.0, notes);
        if (std::abs (inputDb) > 0.05 && ! stageOn (1))
            notes.dropped.push_back ("input gain (stage 1 is off)");
    }
    else
    {
        // Parallel / Multi Band / Mid Side: the stages on the chains of an inner rack
        auto inner = rack::makeRack (RackDoc::Kind::audioEffect);
        RackBuilder ib { inner };
        const int count = routing == RoarRouting::multiBand ? 3 : 2;
        static const char* const names[3][3] { { "Stage 1", "Stage 2", "" }, { "Low", "Mid", "High" }, { "Mid", "Side", "" } };
        const auto set = routing == RoarRouting::parallel ? 0 : routing == RoarRouting::multiBand ? 1 : 2;
        ib.rack().chains.front().name = names[set][0];
        for (int c = 1; c < count; ++c)
            rack::addChain (inner, names[set][c]);
        const auto lowMid = std::clamp (manual (device, "Input_LowMidCrossover", 200.0), 30.0, 18000.0);
        const auto midHigh = std::clamp (manual (device, "Input_MidHighCrossover", 2000.0), lowMid, 18000.0);
        const auto xover = [] (double hpHz, double lpHz) {
            // LYNX's crossover band (Linkwitz-Riley 24 dB/oct [ASSUMED Live's split]): 30 · 2^(9.0589 v) Hz
            auto x = makeEffectSlot (EffectKind::crossover);
            const auto v = [] (double hz) { return unit (std::log2 (hz / 30.0) / 9.0589); };
            x.params[0] = hpHz > 0.0 ? 1.0f : 0.0f;
            x.params[1] = v (std::max (hpHz, 30.0));
            x.params[2] = 3.0f / 7.0f;
            x.params[3] = lpHz > 0.0 ? 1.0f : 0.0f;
            x.params[4] = v (std::max (lpHz, 30.0));
            x.params[5] = 3.0f / 7.0f;
            return x;
        };
        for (int c = 0; c < count; ++c)
        {
            if (routing == RoarRouting::multiBand)
                ib.add (c, xover (c == 0 ? 0.0 : c == 1 ? lowMid : midHigh, c == 0 ? lowMid : c == 1 ? midHigh : 0.0), "Band");
            else if (routing == RoarRouting::midSide)
            {
                // mid = (L + R) / 2 on both sides, side = (L − R) / 2 and its negative: the two chains sum back to L, R
                StereoMatrix m;
                m.ll = 0.5, m.rl = c == 0 ? 0.5 : -0.5, m.lr = c == 0 ? 0.5 : -0.5, m.rr = 0.5;
                ib.add (c, utilitySlot (m), c == 0 ? "Mid" : "Side");
            }
            if (stageOn (c + 1))
                shapers[(std::size_t) c] = roarStage (ib, c, device, c + 1, inputDb, notes);
        }
        if (routing == RoarRouting::parallel)
        {
            // [ASSUMED] Blend: 0.5 both at unity, toward 0 stage 1 alone, toward 1 stage 2 alone
            ib.rack().chains[0].volume = (float) std::min (1.0, 2.0 * (1.0 - blend));
            ib.rack().chains[1].volume = (float) std::min (1.0, 2.0 * blend);
            notes.approximated.push_back (text ("parallel blend %.0f %% as the two chains' volumes", 100.0 * blend));
        }
        else if (std::abs (blend - 0.5) > 0.01)
            notes.dropped.push_back ("Blend");
        if (routing == RoarRouting::multiBand)
            notes.approximated.push_back (text ("multi band split at %.0f / %.0f Hz with 24 dB/oct crossovers", lowMid, midHigh));
        if (routing == RoarRouting::midSide)
            notes.approximated.push_back ("mid / side stages (each side's shaper sees the side and its negative)");
        // the macros of the inner rack: the stages' drive (outer macros reach them through the rack node's parameters)
        for (int c = 0; c < count; ++c)
            if (shapers[(std::size_t) c] >= 0)
            {
                const auto amount = std::clamp (manual (device, "Stage" + std::to_string (c + 1) + "_Shaper_Amount", 0.0), 0.0, 1.0);
                ib.mapParam (c, shapers[(std::size_t) c], em::drive, 0.0f, 1.0f, amount);
                ib.macro (c, names[set][c], amount);
            }
        ib.rack().shownMacros = 8;
        const auto node = addRackNode (b, 0, std::move (inner), routing == RoarRouting::parallel ? "Parallel" : routing == RoarRouting::multiBand ? "Multi Band" : "Mid Side");
        for (int c = 0; c < count; ++c)
            if (shapers[(std::size_t) c] >= 0)
            {
                const auto amount = std::clamp (manual (device, "Stage" + std::to_string (c + 1) + "_Shaper_Amount", 0.0), 0.0, 1.0);
                b.mapParam (rrDrive1 + c, node, c, 0.0f, 1.0f, amount);
                b.macro (rrDrive1 + c, (std::string ("Drive ") + std::to_string (c + 1)).c_str(), amount);
                out.links.push_back ({ "Stage" + std::to_string (c + 1) + "_Shaper_Amount", rrDrive1 + c, [] (double v) { return unit (v); } });
                shapers[(std::size_t) c] = -2; // (macro made)
            }
    }
    // the drive macros of stages on the main chain
    for (int k = 0; k < 3; ++k)
        if (shapers[(std::size_t) k] >= 0)
        {
            const auto amount = std::clamp (manual (device, "Stage" + std::to_string (k + 1) + "_Shaper_Amount", 0.0), 0.0, 1.0);
            b.mapParam (rrDrive1 + k, shapers[(std::size_t) k], em::drive, 0.0f, 1.0f, amount);
            b.macro (rrDrive1 + k, k == 0 ? "Drive 1" : k == 1 ? "Drive 2" : "Drive 3", amount);
            out.links.push_back ({ "Stage" + std::to_string (k + 1) + "_Shaper_Amount", rrDrive1 + k, [] (double v) { return unit (v); } });
        }

    // the output: compression [ASSUMED: Crest Clean, 4 : 1, threshold −36 dB × amount, 10 / 100 ms], then the gain
    const auto comp = std::clamp (manual (device, "Output_CompressionAmount", 0.0), 0.0, 1.0);
    if (comp > 1.0e-3)
    {
        auto c = makeEffectSlot (EffectKind::crestCompressor);
        auto& k = c.params;
        k[cr::style] = cr::styleValue (cr::Style::clean);
        k[cr::threshold] = linValue (-36.0 * comp, -60.0, 0.0);
        k[cr::ratio] = cr::ratioValue (4.0);
        k[cr::knee] = linValue (6.0, 0.0, 72.0);
        k[cr::range] = 1.0f;
        k[cr::attack] = cr::attackValue (10.0);
        k[cr::release] = cr::releaseValue (100.0);
        k[cr::autoRelease] = 0.0f;
        k[cr::lookahead] = 0.0f;
        k[cr::output] = linValue (0.0, -36.0, 36.0);
        k[cr::autoGain] = 0.0f;
        k[cr::mix] = 1.0f;
        b.add (0, std::move (c), "Compressor");
        notes.approximated.push_back (text ("compression %.0f %% as Crest Compressor (4 : 1 from %.0f dB)", 100.0 * comp, -36.0 * comp));
        if (manualBool (device, "Output_CompressorHighpassFilterOn", true))
            notes.dropped.push_back ("the compressor's side-chain high pass");
    }
    const auto outDb = dbOfGain (manual (device, "Output_OutputGain", 1.0));
    int outNode = -1;
    if (std::abs (outDb) > 0.05 || macroOn (device, "Output_OutputGain"))
    {
        outNode = b.add (0, utilitySlot ({}, outDb), "Output");
        if (outDb < -24.0 || outDb > 24.0)
            notes.approximated.push_back (text ("output %.1f dB held at ±24 dB", outDb));
        b.mapParam (rrOutput, outNode, 0, linValue (-48.0, -24.0, 24.0), linValue (12.0, -24.0, 24.0), (outDb + 48.0) / 60.0);
        b.macro (rrOutput, "Output", (outDb + 48.0) / 60.0);
        out.links.push_back ({ "Output_OutputGain", rrOutput, [] (double g) { return unit ((dbOfGain (g) + 48.0) / 60.0); } });
    }
    dryWetChains (b, device, "Output_DryWet", dryWet, rrDryWet, out);

    // what Roar does that the rack does not
    bool modulated = false;
    for (const auto& child : device.children())
        if (const auto n = std::string (child.name()); n.rfind ("ModulationMatrix_", 0) == 0 && std::abs (std::atof (child.attr ("Value").c_str())) > 1.0e-6)
            modulated = true;
    if (modulated)
        notes.dropped.push_back ("modulation (the LFOs, envelope, noise and the matrix)");
    if (manual (device, "Feedback_FeedbackAmount", 0.0) > 1.0e-4)
        notes.dropped.push_back ("feedback");
    b.rack().shownMacros = 8;
    rack::compile (out.slot.patcher);
    return true;
}

// ================================================================================================ Grain Delay

bool grainDelayToEchoform (const Element& device, const Context& context, Translated& out)
{
    using namespace echosuite::echoform;
    if (! has (device, "Feedback") || ! has (device, "Pitch") || ! has (device, "MsDelay"))
        return false;
    out.slot = makeEffectSlot (EffectKind::echoform);
    auto& p = out.slot.params;
    auto& notes = out.notes;
    // the delay line with a pitch shifter on the echoes: Echoform's Pitch mode, one time for both sides
    p[mode] = modeValue (Mode::pitch);
    p[style] = styleValue (Style::single);
    p[lowCut] = 0.0f;
    p[highCut] = 1.0f;
    // the time: synced = one of the Delay's buttons (1, 2, 3, 4, 5, 6, 8, 16 sixteenths [ASSUMED the same list: DATA
    // BeatDelayEnum 0 … 7]) with its offset (± a third), else MsDelay [DATA 1 … 128 ms]
    const auto synced = manualBool (device, "SyncMode", false);
    const auto beats = 0.25 * delaySixteenths ((int) manual (device, "BeatDelayEnum", 1)) * (1.0 + manual (device, "BarDelayOffset", 0.0));
    const auto ms = std::clamp (manual (device, "MsDelay", 40.0), 1.0, 128.0);
    bool fixedMs = false;
    for (const auto right : { false, true })
    {
        const auto sync = right ? syncR : syncL, note = right ? noteR : noteL, time = right ? timeR : timeL;
        auto t = ms;
        if (synced)
        {
            if (const auto n = echoformNoteOf (beats))
            {
                p[sync] = echoformSyncValue (n->first);
                p[note] = echoformNoteValue (n->second);
                continue;
            }
            t = beats * 60000.0 / std::max (context.bpm, 1.0);
            fixedMs = true;
        }
        p[sync] = echoformSyncValue (0);
        p[time] = timeValue (t);
    }
    if (fixedMs)
        notes.approximated.push_back (text ("a synced time with no note value in Echoform: held in ms at %.4g BPM", context.bpm));
    if (! synced)
        out.links.push_back ({ "MsDelay", timeL, [] (double v) { return timeValue (std::clamp (v, 1.0, 128.0)); } });
    // feedback [DATA 0 … 0.95] → the loop gain (0.5 = unity)
    p[feedback] = unit (manual (device, "Feedback", 0.5) / 2.0);
    out.links.push_back ({ "Feedback", feedback, [] (double v) { return unit (v / 2.0); } });
    // pitch [DATA −36 … 12 st] → Echoform's ±24
    const auto pitchSt = manual (device, "Pitch", 0.0);
    p[pitch] = unit ((pitchSt + 24.0) / 48.0);
    p[pitchDetune] = 0.5f;
    out.links.push_back ({ "Pitch", pitch, [] (double v) { return unit ((v + 24.0) / 48.0); } });
    if (pitchSt < -24.0)
        notes.approximated.push_back (text ("pitch %.1f st held at -24 st", pitchSt));
    // dry / wet [ASSUMED linear] → Echoform's equal-power mix at the same balance
    p[mix] = echoformMixOf (std::clamp (manual (device, "NewDryWet", 0.5), 0.0, 1.0), false);
    out.links.push_back ({ "NewDryWet", mix, [] (double v) { return echoformMixOf (std::clamp (v, 0.0, 1.0), false); } });
    notes.approximated.push_back ("grains as Echoform's two-head pitch shifter (a 50 ms window; every repeat shifted by the same pitch)");
    notes.approximated.push_back (text ("grain frequency %.1f Hz (Echoform's window is fixed)", manual (device, "Freq", 60.0)));
    if (manual (device, "Spray", 0.0) > 0.01)
        notes.dropped.push_back (text ("spray %.0f ms", manual (device, "Spray", 0.0)));
    if (manual (device, "RandomPitch", 0.0) > 0.01)
        notes.dropped.push_back ("random pitch");
    return true;
}

// ================================================================================================ Auto Filter types

double autoFilterDjHz (double c) noexcept
{
    const auto a = std::clamp (std::abs (c), 0.0, 1.0);
    return c < 0.0 ? 20000.0 * std::pow (0.001, a) : 20.0 * std::pow (1000.0, a);
}

AutoFilterPlan autoFilterPlan (int liveType, bool newer, double morph, double djControl) noexcept
{
    AutoFilterPlan p;
    static const char* const newNames[10] { "Low-pass", "High-pass", "Band-pass", "Notch", "Morph", "DJ", "Comb", "Resampling", "Notch + LP", "Vowel" };
    static const char* const oldNames[5] { "Low-pass", "High-pass", "Band-pass", "Notch", "Morph" };
    if (liveType < 0 || liveType > (newer ? 9 : 4))
        return p;
    p.name = newer ? newNames[liveType] : oldNames[liveType];
    if (liveType <= 3)
        p.type = liveType;
    else if (liveType == 4)
    {
        // the corners [DATA LegacyFilterType]: 0 low, 0.25 band, 0.5 high, 0.75 notch, 1 low again
        static constexpr int corner[5] { 0, 2, 1, 3, 0 };
        const auto x = std::clamp (morph, 0.0, 1.0) * 4.0;
        const auto k = (int) std::lround (x);
        p.type = corner[std::clamp (k, 0, 4)];
        p.exact = std::abs (x - (double) k) < 0.04;
    }
    else if (liveType == 5)
    {
        p.type = djControl < 0.0 ? 0 : 1;
        p.djHz = autoFilterDjHz (djControl);
        p.exact = false;
    }
    return p;
}

void autoFilterNotchToFacet (const Element& device, Translated& out)
{
    const auto newer = device.name() == "AutoFilter2";
    out.slot = makeEffectSlot (EffectKind::facetEq);
    auto& p = out.slot.params;
    const auto hz = newer ? manual (device, "Filter_Frequency", 1000.0) : hzOfLiveNote (manual (device, "Cutoff", 100.0));
    const auto legacy = ! newer;
    const auto q = filterQOfResonance (manual (device, newer ? "Filter_Resonance" : "Resonance", 0.0), legacy);
    facetBand (p, 0, facet::Shape::notch, hz, 0.0, q);
    // [ASSUMED] the notch's Q by the low pass's measured law; the frequency and Q follow automation / macros
    if (newer)
        out.links.push_back ({ "Filter_Frequency", facet::param (0, 1), [] (double v) { return facet::hzValue (std::clamp (v, 20.0, 20000.0)); } });
    else
        out.links.push_back ({ "Cutoff", facet::param (0, 1), [] (double n) { return facet::hzValue (hzOfLiveNote (n)); } });
    out.links.push_back ({ newer ? "Filter_Resonance" : "Resonance", facet::param (0, 3), [legacy] (double r) { return facet::qValue (std::sqrt (2.0) * filterQOfResonance (r, legacy)); } });
    out.notes.approximated.push_back ("notch as a Facet EQ notch band (its Q by the low pass's law)");
    if (newer && manual (device, "DryWet", 1.0) < 0.9999)
        out.notes.dropped.push_back (text ("dry/wet %.0f %% (Facet EQ has no mix)", 100.0 * manual (device, "DryWet", 1.0)));
}

float facetHzOfFilterCutoff (float filterValue) noexcept
{
    return facet::hzValue (20.0 * std::pow (1000.0, (double) std::clamp (filterValue, 0.0f, 1.0f)));
}

void autoFilterDrive (const Element& device, Translated& out)
{
    namespace em = mixsuite::ember;
    const auto newer = device.name() == "AutoFilter2";
    const auto driveDb = newer ? 24.0 * std::clamp (manual (device, "Filter_Drive", 0.0), 0.0, 1.0) : std::clamp (manual (device, "Drive", 0.0), 0.0, 24.0);
    const auto circuit = (int) manual (device, newer ? "Filter_Circuit" : "CircuitLpHp", 0);
    if (driveDb < 0.05)
    {
        if (circuit != 0)
            out.notes.dropped.push_back (text ("filter circuit %.0f (its own character; LYNX's filter is clean)", circuit));
        return;
    }
    // [ASSUMED] the drive as a gain into a soft clipper (Ember's transistor curve) in front of a clean filter
    auto e = makeEffectSlot (EffectKind::ember);
    e.params[em::style] = 2.0f / (float) (em::styles - 1);
    e.params[em::autoGain] = 0.0f;
    e.params[em::drive] = linValue (driveDb, 0.0, 36.0);
    e.params[em::output] = 0.5f;
    e.params[em::mix] = 1.0f;
    auto filter = std::move (out.slot);
    out.slot = rack::makeRackSlot (RackDoc::Kind::audioEffect);
    RackBuilder b { out.slot.patcher };
    b.rack().chains.front().name = "Auto Filter";
    b.rack().name = "Auto Filter";
    const auto driveNode = b.add (0, std::move (e), "Drive");
    const auto filterNode = b.add (0, std::move (filter), "Filter");
    // every linked parameter (and a modulated one) becomes a macro spanning the control's whole range: links and the
    // Peak Controller's link reach the filter through it
    std::vector<int> params;
    for (const auto& l : out.links)
        if (std::find (params.begin(), params.end(), l.param) == params.end())
            params.push_back (l.param);
    if (out.modulator && std::find (params.begin(), params.end(), out.modulator->param) == params.end())
        params.push_back (out.modulator->param);
    const auto& info = effectInfo (b.doc.nodes[(std::size_t) filterNode].effect.kind);
    int k = 0;
    for (const auto param : params)
    {
        if (k >= rackMacroCount - 1)
            break;
        const auto now = b.valueOf (filterNode, param);
        RackMapping m;
        m.node = filterNode;
        m.param = param;
        rack::map (b.doc, k, m);
        b.macro (k, param < (int) info.params.size() ? info.params[(std::size_t) param].name : "Filter", now);
        for (auto& l : out.links)
            if (l.param == param)
                l.param = k;
        if (out.modulator && out.modulator->param == param)
            out.modulator->param = k;
        ++k;
    }
    b.mapParam (k, driveNode, em::drive, 0.0f, linValue (24.0, 0.0, 36.0), driveDb / 24.0);
    b.macro (k, "Drive", driveDb / 24.0);
    if (newer)
        out.links.push_back ({ "Filter_Drive", k, [] (double v) { return unit (v); } });
    else
        out.links.push_back ({ "Drive", k, [] (double db) { return unit (db / 24.0); } });
    b.rack().shownMacros = k + 1 > 8 ? 16 : 8;
    rack::compile (out.slot.patcher);
    out.to = "Auto Filter rack (Ember Saturator, " + out.to + ")";
    out.notes.approximated.push_back (text ("drive %.1f dB as Ember's soft clipping in front of the filter (circuit %.0f's own character not carried)", driveDb, circuit));
}
} // namespace lynx::alsdev
