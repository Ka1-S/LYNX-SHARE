// Live's own devices → LYNX's, lot 3 (round 190ed; core/AlsDevices.h has the list and the evidence tags). The same
// rules as lots 1 and 2: each conversion is a law in a unit, what is fitted or has no counterpart becomes a note for the
// import report, only the device's saved state is read, and each translator records which Live parameter drives
// which LYNX parameter (ParamLink) so automation and rack macros follow. Round 190ek refit the laws to Live 12.4's renders
// (round 190ei): [MEASURED] = read off those renders, [FITTED] = LYNX's stages fitted to them, [ASSUMED] = still a guess.

#include "core/AlsDevices.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

#include "core/AnvilBus.h"
#include "core/DynamicsSuite.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/MixSuite.h"
#include "core/Rack.h"
#include "core/XmlLite.h"

namespace lynx::alsdev
{
namespace
{
using xml::Element;

float unit (double v) noexcept { return (float) std::clamp (v, 0.0, 1.0); }
float linValue (double v, double lo, double hi) noexcept { return unit ((v - lo) / (hi - lo)); }
float logValue (double v, double lo, double hi) noexcept { return unit (std::log (std::clamp (v, lo, hi) / lo) / std::log (hi / lo)); }

std::string text (const char* format, double a, double b = 0.0, double c = 0.0)
{
    char out[240];
    std::snprintf (out, sizeof (out), format, a, b, c);
    return out;
}

double manual (const Element& device, const std::string& name, double fallback) { return device.number (name + "/Manual", fallback); }
bool manualBool (const Element& device, const std::string& name, bool fallback) { return device.boolean (name + "/Manual", fallback); }
bool has (const Element& device, const std::string& name) { return (bool) device.path (name + "/Manual"); }

// Live's floor of a linear gain control (−70 dB) is shown as −inf [DATA: 0.0003162277571 at the bottom of the ranges]
bool isFloor (double gain) noexcept { return gain <= 0.000317; }
} // namespace

// ================================================================================================ EQ Three

bool eqThreeToPrism (const Element& device, Translated& out)
{
    using namespace dyn::prism;
    if (! has (device, "GainLo") || ! has (device, "FreqLo") || ! has (device, "FreqHi"))
        return false;
    out.slot = makeEffectSlot (EffectKind::prismMultiband);
    auto& p = out.slot.params;
    p[bandCountParam] = bandCountValue (3);
    // the crossover sum (Live's EQ Three splits the signal into three bands and sums them [DOC]): Prism's minimum-phase
    // mode, the all-pass compensated crossover tree; 24 / 48 dB/oct [DOC; DATA Slope 0 … 1, ASSUMED 0 = 24]
    p[processingMode] = 1.0f;
    const auto slopeDb = manual (device, "Slope", 1.0) > 0.5 ? 48 : 24;
    out.links.push_back ({ "FreqLo", crossoverParam (0), [] (double hz) { return crossoverValue (hz); } });
    out.links.push_back ({ "FreqHi", crossoverParam (1), [] (double hz) { return crossoverValue (hz); } });
    auto lo = std::clamp (manual (device, "FreqLo", 250.0), 30.0, 18000.0), hi = std::clamp (manual (device, "FreqHi", 2500.0), 30.0, 18000.0);
    if (hi < lo)
        std::swap (lo, hi);
    p[(std::size_t) crossoverParam (0)] = crossoverValue (lo);
    p[(std::size_t) crossoverParam (1)] = crossoverValue (hi);
    p[slope] = (float) (slopeDb == 48 ? 1.0 : 0.5);
    for (int k = 0; k < 2; ++k)
        p[(std::size_t) (xoverSlope0 + k)] = crossoverSlopeValue (slopeDb);
    // no dynamics: each band only a level (its range 0 dB, ratio 1 : 1)
    static const char* const gains[3] { "GainLo", "GainMid", "GainHi" };
    static const char* const ons[3] { "LowOn", "MidOn", "HighOn" };
    std::array<bool, 3> sounds {};
    bool clamped = false;
    for (int b = 0; b < 3; ++b)
    {
        const auto gain = manual (device, gains[b], 1.0);
        sounds[(std::size_t) b] = manualBool (device, ons[b], true) && ! isFloor (gain);
        p[(std::size_t) param (b, fRange)] = 0.5f;
        p[(std::size_t) param (b, fRatio)] = 0.0f;
        p[(std::size_t) param (b, fEnabled)] = 1.0f;
        // a linear gain [DATA: 0.000316 … 1.995 = −70 … +6 dB] → Prism's band level (−30 … +30 dB)
        const auto db = dbOfGain (gain);
        p[(std::size_t) param (b, fLevel)] = linValue (db, -30.0, 30.0);
        clamped = clamped || (db < -30.0 && ! isFloor (gain));
        out.links.push_back ({ gains[b], param (b, fLevel), [] (double g) { return linValue (dbOfGain (g), -30.0, 30.0); } });
    }
    const auto alive = (int) std::count (sounds.begin(), sounds.end(), true);
    if (alive < 3)
        for (int b = 0; b < 3; ++b)
            p[(std::size_t) param (b, fSolo)] = sounds[(std::size_t) b] ? 1.0f : 0.0f;
    if (alive == 0)
    {
        p[output] = 0.0f; // Prism's lowest output (−36 dB): no setting of Prism is silent
        out.notes.approximated.push_back ("all three bands off: Prism's output at -36 dB (Live: silence)");
    }
    if (clamped)
        out.notes.approximated.push_back ("a band gain under -30 dB (Prism's lowest band level)");
    p[mix] = 1.0f;
    out.notes.approximated.push_back (text ("the bands as Prism's minimum-phase crossover sum at %.0f dB/oct (Live's filters: law assumed)", slopeDb));
    return true;
}

// ================================================================================================ Drum Buss

// The laws below were fitted to Live 12.4's renders of round 190ei (one Drum Buss per track: noise, a 1 kHz sine
// staircase, an impulse and a drum loop through it; LYNX's rack run on the same inputs, round 190ek's error table)
namespace
{
double interpolate (const double* xs, const double* ys, int n, double x) noexcept
{
    if (x <= xs[0])
        return ys[0];
    for (int i = 1; i < n; ++i)
        if (x <= xs[i])
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    return ys[n - 1];
}
constexpr double driveAmounts[4] { 0.0, 0.25, 0.5, 1.0 };
} // namespace

double drumBussDriveGainDb (int type, double drive) noexcept
{
    // the small-signal gain over the neutral device [MEASURED, 1 kHz at −60 dBFS: +1.97 dB at drive 0 for every type;
    // type 0: +3.92 / +8.91 / +11.52, type 1: +5.73 / +9.48 / +16.98, type 2: +5.94 / +7.75 / +14.23 dB at 25 / 50 / 100 %]
    static constexpr double gains[3][4] { { 0.0, 1.95, 6.94, 9.55 }, { 0.0, 3.76, 7.51, 15.01 }, { 0.0, 3.97, 5.78, 12.26 } };
    return interpolate (driveAmounts, gains[std::clamp (type, 0, 2)], 4, std::clamp (drive, 0.0, 1.0));
}

DrumBussDriveStage drumBussDriveStage (int type, double drive) noexcept
{
    // Ember's voicing, drive and output per type and amount (core/AlsDevices.h drumbuss)
    DrumBussDriveStage st;
    const auto t = std::clamp (type, 0, 2);
    const auto d = std::clamp (drive, 0.0, 1.0);
    st.driveDb = interpolate (driveAmounts, drumbuss::emberDriveDb[t], 4, d);
    st.eqDb = drumbuss::typeEqDb[t] * d;
    // the analog prototype's gain at 1 kHz (a bell for Medium, a cookbook low shelf for Hard), so the measured 1 kHz levels hold
    const auto s = std::complex<double> (0.0, 1000.0 / drumbuss::typeEqHz[t]);
    const auto q = drumbuss::typeEqQ[t];
    const auto A = std::pow (10.0, st.eqDb / 40.0);
    const auto h = t == 2 ? A * (s * s + std::sqrt (A) / q * s + A) / (A * s * s + std::sqrt (A) / q * s + 1.0) : (s * s + s * A / q + 1.0) / (s * s + s / (A * q) + 1.0);
    st.outputDb = drumbuss::emberOutputDb - (t == 0 ? 0.0 : 20.0 * std::log10 (std::abs (h)));
    return st;
}

BoomBands drumBussBoom (double amount, double hz, double decay) noexcept
{
    // [FITTED on the four renders at amount 0.5, rms 0.24 … 0.78 dB below 600 Hz: a bell at the Boom frequency and a
    // resonant 12 dB/oct high pass] decay 0.2: bell +13.75 dB, Q 3.32, high pass Q 0.581 at 1.470 × f; decay 1.0: bell
    // +18.65 dB, Q 2.18, high pass Q 0.476 at 0.816 × f; linear in the decay between (and beyond, held to sane ends).
    // The amount [ASSUMED]: a resonance added in proportion to it (the bell's linear gain − 1 ∝ the amount: 0.5 = the
    // fit, 1.0 = +24.1 dB at decay 1), the high pass sliding down from 0 to its place as the amount rises to 0.5
    BoomBands b;
    const auto a = std::clamp (amount, 0.0, 1.0);
    const auto x = (std::clamp (decay, 0.0, 1.0) - 0.2) / 0.8;
    b.hz = std::clamp (hz, 30.0, 90.0);
    const auto atHalf = std::pow (10.0, (13.75 + (18.65 - 13.75) * x) / 20.0) - 1.0;
    b.bellDb = std::clamp (20.0 * std::log10 (1.0 + atHalf * a / 0.5), 0.0, 30.0);
    b.bellQ = std::clamp (3.32 + (2.18 - 3.32) * x, 1.0, 6.0);
    b.cutQ = std::clamp (0.581 + (0.476 - 0.581) * x, 0.3, 1.5);
    b.cutHz = std::max (10.0, b.hz * std::clamp (1.470 + (0.816 - 1.470) * x, 0.5, 2.0) * std::min (1.0, a / 0.5));
    return b;
}

double drumBussCrunchDb (double crunch) noexcept
{
    // [FITTED on Crunch 0.5: +6.28 dB between a low shelf at 95 Hz and a high shelf at 9041 Hz, rms 0.07 dB; ASSUMED the
    // added band in proportion to the amount: 20·log10 (1 + 2.14 · crunch)]
    return 20.0 * std::log10 (1.0 + 2.14 * std::clamp (crunch, 0.0, 1.0));
}

namespace
{
// a Facet EQ band from an analog prototype: a bell's textbook Q, a cookbook shelf Q, a 12 dB/oct cut's Q
void facetBand (EffectParams& p, int b, facet::Shape shape, double hz, double db, double q, int slope = 12)
{
    p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (shape);
    p[(std::size_t) facet::param (b, 1)] = facet::hzValue (hz);
    p[(std::size_t) facet::param (b, 2)] = facet::gainValue (facet::hasGain (shape) ? db : 0.0);
    // Facet's prototypes (core/AlsDevices.h, EQ Eight): bell and 12 dB cut Q = Qfacet / √2; a shelf's cookbook Q =
    // 0.7071 · Qfacet^0.4697
    const auto shelf = shape == facet::Shape::lowShelf || shape == facet::Shape::highShelf;
    p[(std::size_t) facet::param (b, 3)] = facet::qValue (shelf ? std::pow (q / 0.7071, 1.0 / 0.4697) : std::sqrt (2.0) * q);
    p[(std::size_t) facet::param (b, 4)] = facet::slopeValue (slope);
    p[(std::size_t) (facet::powerParam0 + b)] = 0.0f;
}
} // namespace

bool drumBussToRack (const Element& device, Translated& out)
{
    namespace em = mixsuite::ember;
    namespace sh = mixsuite::shell;
    namespace cr = dyn::crest;
    if (! has (device, "DriveAmount") || ! has (device, "TransientShaping") || ! has (device, "BoomAmount"))
        return false;

    // 1. Ember "Drive": the input trim, the drive (its own curve per type and the gain the type gives each amount: Soft /
    // Medium / Hard [DOC order; DATA DriveType 0 … 2]), Damping as its 6 dB/oct high cut [MEASURED: a first-order low
    // pass, −3 dB at 1020 / 6662 Hz for 1 / 6 kHz], no level compensation
    auto ember = makeEffectSlot (EffectKind::ember);
    auto& e = ember.params;
    const auto type = std::clamp ((int) manual (device, "DriveType", 0), 0, 2);
    const auto driveAmount = std::clamp (manual (device, "DriveAmount", 0.2), 0.0, 1.0);
    const auto stage = drumBussDriveStage (type, driveAmount);
    const auto stageAt = [type] (double d) { return drumBussDriveStage (type, d); };
    e[em::voice] = (float) stage.voice / (float) em::voices;
    e[em::autoGain] = 0.0f;
    e[em::drive] = linValue (stage.driveDb, 0.0, 36.0);
    e[em::output] = linValue (stage.outputDb, -18.0, 18.0);
    const auto trimDb = dbOfGain (manual (device, "InputTrim", 1.0));
    e[em::inputGain] = linValue (trimDb, -24.0, 24.0);
    const auto damping = std::clamp (manual (device, "DampingFrequency", 20000.0), 500.0, 20000.0);
    e[em::wideCuts] = 1.0f;
    e[em::highCutWide] = damping >= 19999.0 ? 1.0f : logValue (damping, 1000.0, 20000.0);
    e[em::mix] = 1.0f;

    // 2. Facet EQ "Colour": the device's own colour, there at every setting [MEASURED at all-zero settings: a "smile",
    // +5 dB around 150 Hz and 4 kHz, +1.85 dB between; FITTED as three bells and the output, rms 0.14 dB], Crunch's mid band
    // [MEASURED 0.5: +6.3 dB 300 Hz … 4 kHz] as two shelves under the output's lift, Boom's resonance as a bell and a
    // resonant high pass
    auto colour = makeEffectSlot (EffectKind::facetEq);
    auto& c = colour.params;
    facetBand (c, 0, facet::Shape::bell, drumbuss::lowBellHz, drumbuss::lowBellDb, drumbuss::lowBellQ);
    facetBand (c, 1, facet::Shape::bell, drumbuss::highBellHz, drumbuss::highBellDb, drumbuss::highBellQ);
    facetBand (c, 6, facet::Shape::bell, drumbuss::dipBellHz, drumbuss::dipBellDb, drumbuss::dipBellQ);
    facetBand (c, 7, type == 2 ? facet::Shape::lowShelf : facet::Shape::bell, drumbuss::typeEqHz[type], stage.eqDb, drumbuss::typeEqQ[type]);
    const auto crunch = std::clamp (manual (device, "CrunchAmount", 0.0), 0.0, 1.0);
    const auto crunchDb = drumBussCrunchDb (crunch);
    facetBand (c, 4, facet::Shape::lowShelf, drumbuss::crunchLowHz, -crunchDb, drumbuss::crunchLowQ);
    facetBand (c, 5, facet::Shape::highShelf, drumbuss::crunchHighHz, -crunchDb, drumbuss::crunchHighQ);
    const auto boomAmount = std::clamp (manual (device, "BoomAmount", 0.0), 0.0, 1.0);
    const auto boomHz = std::clamp (manual (device, "BoomFrequency", 50.0), 30.0, 90.0);
    const auto boomDecay = std::clamp (manual (device, "BoomDecay", 0.5), 0.0, 1.0);
    const auto boom = drumBussBoom (boomAmount, boomHz, boomDecay);
    facetBand (c, 2, facet::Shape::bell, boom.hz, boom.bellDb, boom.bellQ);
    facetBand (c, 3, facet::Shape::lowCut, boom.cutHz, 0.0, boom.cutQ);
    c[facet::outputParam] = (float) ((std::clamp (drumbuss::colourGainDb + crunchDb, -24.0, 24.0) + 24.0) / 48.0);

    // 3. Crest "Comp" (only when on): Live's fixed drum compressor [MEASURED on the staircase: about 4 : 1 above
    // −21 dBFS, +11 dB of make-up]
    const auto compOn = device.boolean ("EnableCompression/Manual", device.boolean ("EnableCompression", false));
    EffectSlot comp;
    if (compOn)
    {
        comp = makeEffectSlot (EffectKind::crestCompressor);
        auto& k = comp.params;
        k[cr::style] = cr::styleValue (cr::Style::clean);
        k[cr::voicing] = 0.0f;
        k[cr::threshold] = linValue (drumbuss::compThresholdDb, -60.0, 0.0);
        k[cr::ratio] = cr::ratioValue (drumbuss::compRatio);
        k[cr::knee] = linValue (drumbuss::compKneeDb, 0.0, 72.0);
        k[cr::range] = 1.0f;
        k[cr::attack] = cr::attackValue (drumbuss::compAttackMs);
        k[cr::release] = cr::releaseValue (drumbuss::compReleaseMs);
        k[cr::autoRelease] = 0.0f;
        k[cr::lookahead] = 0.0f;
        k[cr::maxLookahead] = 0.0f;
        k[cr::output] = linValue (drumbuss::compMakeupDb, -36.0, 36.0);
        k[cr::autoGain] = 0.0f;
        k[cr::mix] = 1.0f;
        k[cr::dryGain] = 0.0f;
    }

    // 4. Shell Channel "Shape": the transient shaper (−1 … +1: Amount = |t|, Invert below 0) and the output gain
    auto shell = makeEffectSlot (EffectKind::shellChannel);
    auto& s = shell.params;
    const auto transients = std::clamp (manual (device, "TransientShaping", 0.0), -1.0, 1.0);
    s[sh::tsOn] = std::abs (transients) > 1.0e-4 ? 1.0f : 0.0f;
    s[sh::tsAmount] = (float) std::abs (transients);
    s[sh::tsInvert] = transients < 0.0 ? 1.0f : 0.0f;
    s[sh::lfOn] = 0.0f;
    s[sh::hfOn] = 0.0f;
    s[sh::lmcOn] = 0.0f;
    const auto outDb = dbOfGain (manual (device, "OutputGain", 1.0));
    s[sh::output] = linValue (outDb, -20.0, 20.0);
    s[sh::outputMix] = 1.0f;

    // the rack: "Drum Buss" chain (Ember → Facet → Crest → Shell) and, when the device is not fully wet or a macro moves
    // Dry/Wet, a "Dry" chain beside it [MEASURED: out = 0.5 · input + 0.5 · wet at 50 %, the dry branch the raw input]
    out.slot = rack::makeRackSlot (RackDoc::Kind::audioEffect);
    auto& doc = out.slot.patcher;
    if (auto* r = rack::rackOf (doc); r != nullptr && ! r->chains.empty())
        r->chains.front().name = "Drum Buss";
    const auto add = [&doc] (EffectSlot&& fx, const char* label) {
        PatchNode node;
        node.effect = std::move (fx);
        node.label = label;
        return rack::addDevice (doc, 0, std::move (node));
    };
    const auto emberNode = add (std::move (ember), "Drive");
    const auto colourNode = add (std::move (colour), "Colour");
    const auto compNode = compOn ? add (std::move (comp), "Comp") : 0;
    const auto shellNode = add (std::move (shell), "Shape");
    if (emberNode < 0 || colourNode < 0 || compNode < 0 || shellNode < 0)
        return false;
    const auto dryWet = std::clamp (manual (device, "DryWet", 1.0), 0.0, 1.0);
    // (a macro on Dry/Wet: KeyMidi on channel 16 [DATA]; an envelope on it without a dry chain stays listed)
    const auto wetMoves = (int) device.number ("DryWet/KeyMidi/Channel", -1) == 16;
    int dry = -1;
    if (dryWet < 0.9999 || wetMoves)
    {
        dry = rack::addChain (doc, "Dry");
        if (auto* r = rack::rackOf (doc); r != nullptr && dry >= 0)
        {
            r->chains[0].volume = (float) dryWet;
            r->chains[(std::size_t) dry].volume = (float) (1.0 - dryWet);
        }
    }

    // the macros, named after Live's controls; each macro's 0 … 127 is its Live control's whole range (Live's
    // parameters reach them through `links`), mapped onto the LYNX controls between their values at the two ends
    auto* r = rack::rackOf (doc);
    if (r == nullptr)
        return false;
    const auto valueOf = [&doc] (int node, int param) { return doc.nodes[(std::size_t) node].effect.params[(std::size_t) param]; };
    struct Target
    {
        int node, param;
        float min, max;
    };
    const auto mapMacro = [&doc] (int macro, const char* name, std::initializer_list<Target> targets) {
        for (const auto& t : targets)
        {
            RackMapping m;
            m.node = t.node;
            m.param = t.param;
            m.min = t.min;
            m.max = t.max;
            rack::map (doc, macro, m);
        }
        rack::rackOf (doc)->macros[(std::size_t) macro].name = name;
    };
    // a mapping's line through the control's value now at the macro's place now (`t`), so the rack sounds as imported
    // (the patcher's macro wires drive the controls) and a macro move follows the measured law in a straight line: from
    // its value at Live's 0, or — when that line would leave 0 … 1 (the wires' range) — back from its value at Live's 1
    const auto line = [&valueOf] (int node, int param, float atZero, double t, float atOne) {
        const auto now = (double) valueOf (node, param);
        Target out { node, param, atZero, atOne };
        if (t < 0.01)
            return out;
        if (const auto top = atZero + (now - atZero) / t; top >= 0.0 && top <= 1.0)
            out.max = (float) top;
        else if (t < 0.99)
            out.min = (float) std::clamp (now - (atOne - now) * t / (1.0 - t), 0.0, 1.0);
        return out;
    };
    const auto lo = stageAt (0.0), hi = stageAt (1.0);
    const auto g0 = facet::gainValue (0.0);
    mapMacro (dbDrive, "Drive", { line (emberNode, em::drive, linValue (lo.driveDb, 0.0, 36.0), driveAmount, linValue (hi.driveDb, 0.0, 36.0)),
                                  line (emberNode, em::output, linValue (lo.outputDb, -18.0, 18.0), driveAmount, linValue (hi.outputDb, -18.0, 18.0)),
                                  line (colourNode, facet::param (7, 2), g0, driveAmount, facet::gainValue (hi.eqDb)) });
    const auto top = drumBussCrunchDb (1.0);
    const auto outZero = (float) ((drumbuss::colourGainDb + 24.0) / 48.0);
    mapMacro (dbCrunch, "Crunch", { line (colourNode, facet::param (4, 2), g0, crunch, facet::gainValue (-top)),
                                    line (colourNode, facet::param (5, 2), g0, crunch, facet::gainValue (-top)),
                                    line (colourNode, facet::outputParam, outZero, crunch, (float) ((std::clamp (drumbuss::colourGainDb + top, -24.0, 24.0) + 24.0) / 48.0)) });
    mapMacro (dbDamping, "Damping", { { emberNode, em::highCutWide, 0.0f, 1.0f } });
    mapMacro (dbTransients, "Transients", { { shellNode, sh::tsAmount, 0.0f, 1.0f } });
    const auto boomTop = drumBussBoom (1.0, boomHz, boomDecay);
    mapMacro (dbBoom, "Boom", { line (colourNode, facet::param (2, 2), g0, boomAmount, facet::gainValue (boomTop.bellDb)),
                                line (colourNode, facet::param (3, 1), facet::hzValue (10.0), boomAmount, facet::hzValue (boomTop.cutHz)) });
    mapMacro (dbBoomFreq, "Boom Freq", { line (colourNode, facet::param (2, 1), facet::hzValue (30.0), (boomHz - 30.0) / 60.0, facet::hzValue (90.0)) });
    mapMacro (dbTrim, "Trim", { { emberNode, em::inputGain, 0.0f, 1.0f } });
    mapMacro (dbOutput, "Output", { { shellNode, sh::output, 0.0f, 1.0f } });
    if (dry >= 0)
    {
        RackMapping wet, dryMap;
        wet.target = dryMap.target = RackMapping::Target::chainVolume;
        wet.chain = 0;
        dryMap.chain = dry;
        wet.min = dryMap.max = 0.0f;
        wet.max = dryMap.min = rack::volumeValue (1.0f);
        rack::map (doc, dbDryWet, wet);
        rack::map (doc, dbDryWet, dryMap);
        r = rack::rackOf (doc);
        r->macros[(std::size_t) dbDryWet].name = "Dry/Wet";
    }
    // each macro where Live's control is now (nothing moves)
    r = rack::rackOf (doc);
    const auto at = [&r] (int macro, double v) { r->macros[(std::size_t) macro].value = (float) (127.0 * std::clamp (v, 0.0, 1.0)); };
    at (dbDrive, driveAmount);
    at (dbCrunch, crunch);
    at (dbDamping, valueOf (emberNode, em::highCutWide));
    at (dbTransients, std::abs (transients));
    at (dbBoom, boomAmount);
    at (dbBoomFreq, (boomHz - 30.0) / 60.0);
    at (dbTrim, valueOf (emberNode, em::inputGain));
    at (dbOutput, valueOf (shellNode, sh::output));
    if (dry >= 0)
        at (dbDryWet, (double) rack::volumeValue ((float) dryWet) / rack::volumeValue (1.0f));
    r->shownMacros = dry >= 0 ? 10 : 8;
    r->name = "Drum Buss";
    rack::compile (doc);

    // Live's parameters → the macros (the macro's 0..1 is the Live control's range)
    out.links.push_back ({ "DriveAmount", dbDrive, [] (double v) { return unit (v); } });
    out.links.push_back ({ "CrunchAmount", dbCrunch, [] (double v) { return unit (v); } });
    out.links.push_back ({ "DampingFrequency", dbDamping, [] (double hz) { return hz >= 19999.0 ? 1.0f : logValue (hz, 1000.0, 20000.0); } });
    if (transients >= 0.0)
        out.links.push_back ({ "TransientShaping", dbTransients, [] (double t) { return unit (t); } }); // (the positive side: Invert stays off)
    out.links.push_back ({ "BoomAmount", dbBoom, [] (double v) { return unit (v); } });
    out.links.push_back ({ "BoomFrequency", dbBoomFreq, [] (double hz) { return unit ((hz - 30.0) / 60.0); } });
    out.links.push_back ({ "InputTrim", dbTrim, [] (double g) { return linValue (dbOfGain (g), -24.0, 24.0); } });
    out.links.push_back ({ "OutputGain", dbOutput, [] (double g) { return linValue (dbOfGain (g), -20.0, 20.0); } });
    if (dry >= 0)
        out.links.push_back ({ "DryWet", dbDryWet, [] (double x) {
                                   return (float) std::clamp ((double) rack::volumeValue ((float) std::clamp (x, 0.0, 1.0)) / rack::volumeValue (1.0f), 0.0, 1.0);
                               } });

    // the report: what is a fit, what has no counterpart (a dedicated drum-bus engine would carry these)
    static const char* const typeNames[3] { "Soft", "Medium", "Hard" };
    out.notes.approximated.push_back (std::string ("drive type ") + typeNames[type] + " as Ember's " + em::voiceName (stage.voice) + " voicing at the measured drive gain"
                                      + (type == 0 ? std::string() : std::string (" and the type's EQ (fitted at 100 %, in proportion below)")));
    if (type == 0 && driveAmount > 0.25)
        out.notes.approximated.push_back ("drive type Soft folds the loudest peaks over (a wave folder): the voicing clips them instead");
    out.notes.approximated.push_back ("the device's own colour (EQ and soft clipping at every setting) as Facet EQ bands and Ember's curve");
    if (s[sh::tsOn] > 0.5f)
        out.notes.approximated.push_back ("transients as Shell Channel's transient shaper (Live also lifts the tail)");
    if (boomAmount > 1.0e-4)
        out.notes.approximated.push_back ("Boom as a resonant bell and high pass (fitted at amount 0.5; no ringing tail of its own)");
    if (crunch > 1.0e-4)
        out.notes.approximated.push_back ("Crunch as its mid-band level (fitted; its distortion is not carried)");
    if (compOn)
        out.notes.approximated.push_back ("the compressor as Crest Compressor at about 4 : 1 (fitted)");
    if (dry >= 0)
        out.notes.approximated.push_back ("Dry/Wet as the rack's dry and wet chains");
    if (damping < 1000.0)
        out.notes.approximated.push_back (text ("damping %.0f Hz as 1 kHz (Ember's high cut: from 1 kHz)", damping));
    if (trimDb < -24.0)
        out.notes.approximated.push_back (text ("input trim %.1f dB (Ember's input: down to -24 dB)", trimDb));
    if (std::abs (outDb) > 20.0)
        out.notes.approximated.push_back (text ("output %.1f dB (Shell Channel's output: +/-20 dB)", outDb));
    if (transients < 0.0)
        out.notes.approximated.push_back ("negative transients: automation / macros of Transients are not carried");
    return true;
}

// Round 190fk (owner decision O20): Drum Buss onto LYNX's own drum-bus engine, Anvil Bus (core/AnvilBus.h), whose laws
// were fitted to the same renders of Live 12.4 (rounds 190ei / 190fk). Every control maps one to one; Live's parameters
// reach the engine's through `links`, so envelopes and rack macros on them are carried. The rack above stays for the
// A/B harness and its tests (Context::drumBussRack); projects that already hold the rack keep it as it is.
bool drumBussToEngine (const Element& device, Translated& out)
{
    namespace an = anvil;
    if (! has (device, "DriveAmount") || ! has (device, "TransientShaping") || ! has (device, "BoomAmount"))
        return false;
    out.slot = makeEffectSlot (EffectKind::anvilBus);
    auto& p = out.slot.params;
    const auto type = std::clamp ((int) manual (device, "DriveType", 0), 0, 2);
    p[an::drive] = unit (manual (device, "DriveAmount", 0.2));
    p[an::driveType] = an::typeValue ((an::Type) type);
    p[an::crunch] = unit (manual (device, "CrunchAmount", 0.0));
    const auto dampHz = std::clamp (manual (device, "DampingFrequency", 20000.0), 500.0, 20000.0);
    const auto dampOf = [] (double hz) { return hz >= 19999.0 ? 1.0f : an::dampValue (hz); };
    p[an::damp] = dampOf (dampHz);
    p[an::transients] = an::transientsValue (manual (device, "TransientShaping", 0.0));
    p[an::boom] = unit (manual (device, "BoomAmount", 0.0));
    p[an::boomFreq] = an::boomHzValue (manual (device, "BoomFrequency", 50.0));
    p[an::boomDecay] = unit (manual (device, "BoomDecay", 0.5));
    p[an::comp] = device.boolean ("EnableCompression/Manual", device.boolean ("EnableCompression", false)) ? 1.0f : 0.0f;
    const auto trimDb = dbOfGain (manual (device, "InputTrim", 1.0));
    const auto outDb = dbOfGain (manual (device, "OutputGain", 1.0));
    p[an::trim] = an::trimValue (trimDb);
    p[an::output] = an::outputValue (outDb);
    p[an::dryWet] = unit (manual (device, "DryWet", 1.0));

    out.links.push_back ({ "DriveAmount", an::drive, [] (double v) { return unit (v); } });
    out.links.push_back ({ "DriveType", an::driveType, [] (double v) { return an::typeValue ((an::Type) std::clamp ((int) std::lround (v), 0, 2)); } });
    out.links.push_back ({ "CrunchAmount", an::crunch, [] (double v) { return unit (v); } });
    out.links.push_back ({ "DampingFrequency", an::damp, dampOf });
    out.links.push_back ({ "TransientShaping", an::transients, [] (double t) { return an::transientsValue (t); } });
    out.links.push_back ({ "BoomAmount", an::boom, [] (double v) { return unit (v); } });
    out.links.push_back ({ "BoomFrequency", an::boomFreq, [] (double hz) { return an::boomHzValue (hz); } });
    out.links.push_back ({ "BoomDecay", an::boomDecay, [] (double v) { return unit (v); } });
    out.links.push_back ({ "EnableCompression", an::comp, [] (double v) { return v >= 0.5 ? 1.0f : 0.0f; } });
    out.links.push_back ({ "InputTrim", an::trim, [] (double g) { return an::trimValue (dbOfGain (g)); } });
    out.links.push_back ({ "OutputGain", an::output, [] (double g) { return an::outputValue (dbOfGain (g)); } });
    out.links.push_back ({ "DryWet", an::dryWet, [] (double v) { return unit (v); } });

    // the report: what the engine holds as a fit rather than a measurement at that very setting
    const auto drive = (double) p[an::drive];
    if (drive > 0.0 && drive != 0.25 && drive != 0.5 && drive != 1.0)
        out.notes.approximated.push_back ("drive between the measured amounts (0, 25, 50, 100 %): curve and gain interpolated");
    if (type != 0 && drive > 0.0)
        out.notes.approximated.push_back ("the drive type's EQ fitted at 100 % and taken in proportion below");
    if (p[an::boom] > 0.0f && std::abs (p[an::boom] - 0.5f) > 1.0e-3f)
        out.notes.approximated.push_back ("Boom fitted at amount 0.5; other amounts scale its resonance");
    if (p[an::crunch] > 0.0f && std::abs (p[an::crunch] - 0.5f) > 1.0e-3f)
        out.notes.approximated.push_back ("Crunch fitted at 0.5; other amounts scale its band");
    if (outDb > 12.0)
        out.notes.approximated.push_back (text ("output %.1f dB (Anvil Bus: up to +12 dB)", outDb));
    if (device.boolean ("BoomAudition/Manual", false))
        out.notes.dropped.push_back ("Boom audition (the boom heard alone): Anvil Bus plays the whole device");
    return true;
}

// ================================================================================================ Auto Filter modulation

float peakAmountValue (double k) noexcept
{
    const auto raw = (k < 0.0 ? -1.0 : 1.0) * 256.0 * std::log (1.0 + std::min (std::abs (k), 2.0) / 0.4) / std::log (6.0);
    return unit ((raw / 256.0 + 1.0) * 0.5);
}

float peakLfoAmountValue (double k) noexcept
{
    const auto raw = (k < 0.0 ? -1.0 : 1.0) * 256.0 * std::log (1.0 + std::min (std::abs (k), 1.0) / 0.2) / std::log (6.0);
    return unit ((raw / 256.0 + 1.0) * 0.5);
}

float peakLfoSpeedValue (double beats) noexcept
{
    const auto raw = 65536.0 * std::log (std::max (beats, 3.0 / 96.0) / 0.128 + 1.0) / std::log (1001.0);
    return unit ((raw - 200.0) / 65336.0);
}

float peakDecayValue (double ms, double bpm) noexcept
{
    // per quarter note the peak falls by perBeat (1 = the whole range): 1 / (the release in quarter notes)
    const auto perBeat = 60000.0 / (std::max (ms, 0.1) * std::max (bpm, 1.0));
    const auto d = 128.0 * std::log (perBeat / 0.0192 + 1.0) / std::log (10001.0);
    return unit ((d - 5.0) / 123.0);
}

CutoffModulation cutoffModulation (double c0, double lfoDepth, double envDepth, bool turnOver) noexcept
{
    // the output o = E'·peak + L'·(w + 1)/2 (bases 0) and the cutoff = y0 + S·o: S = ±(2L + |E|) carries both at full
    // scale without clipping the output; a negative E (or `turnOver`, an LFO alone) turns the curve over, and the LFO
    // with it (half a cycle later)
    CutoffModulation m;
    const auto L = std::abs (lfoDepth), E = envDepth;
    const auto sign = E < 0.0 || (turnOver && std::abs (E) < 1.0e-12) ? -1.0 : 1.0;
    const auto span = std::max (2.0 * L + std::abs (E), 1.0e-6);
    const auto S = sign * span;
    m.invert = sign < 0.0;
    m.lfoAmount = peakLfoAmountValue (2.0 * L / span);
    m.peakAmount = peakAmountValue (std::abs (E) / span);
    const auto y0 = c0 - sign * L;
    constexpr int points = 33;
    for (int i = 0; i < points; ++i)
        m.curve.push_back (unit (y0 + S * (double) i / (double) (points - 1)));
    return m;
}

double autoFilter2SyncedBeats (int index) noexcept
{
    // the same 22 note values as the older device's BeatRate, in the other order: 8 bars at 0 … 1/64 at 21 [MEASURED
    // 190ei: index 9 = 1.5 beats = 3/8 (the older table's 12 = 21 − 9), its triplet / dotted 1.0 / 2.25 beats; BIN: the
    // engine holds the same 22 labels for both]
    return liveBeatRateBeats (21 - std::clamp (index, 0, 21));
}

double autoFilter2LfoBeats (int timeMode, double hz, double seconds, int syncedRate, double sixteenths, double bpm, bool* synced) noexcept
{
    // Lfo_TimeMode [MEASURED 190ei at 120 BPM: 0 Hz (1 Hz → 1.000 s), 1 time (0.8 s → 0.799 s), 2 synced (index 9 →
    // 0.750 s), 3 triplet (× 2/3: 0.500 s), 4 dotted (× 1.5: 1.126 s), 5 sixteenths (3 → 0.375 s)]
    const auto beatsPerSecond = std::max (bpm, 1.0) / 60.0;
    const auto mode = std::clamp (timeMode, 0, 5);
    if (synced != nullptr)
        *synced = mode >= 2;
    switch (mode)
    {
        case 1: return std::max (seconds, 0.001) * beatsPerSecond;
        case 2: return autoFilter2SyncedBeats (syncedRate);
        case 3: return autoFilter2SyncedBeats (syncedRate) * 2.0 / 3.0;
        case 4: return autoFilter2SyncedBeats (syncedRate) * 1.5;
        case 5: return std::max (sixteenths, 1.0) * 0.25;
        default: return beatsPerSecond / std::max (hz, 0.01);
    }
}

void autoFilterModulation (const Element& device, const Context& context, Translated& out)
{
    const auto newer = device.name() == "AutoFilter2";
    // the depths in the Filter's cutoff units (0..1 = 20 Hz … 20 kHz, log: one unit = log2 (1000) octaves)
    constexpr double octavesPerUnit = 9.965784284662087;
    // [MEASURED 190ei, the cutoff tracked as the resonant peak of the output] the older device: LFO Amount [DATA 0 … 30]
    // in semitones of its cutoff (12 → ±12.04, 30 → ±29.80); the envelope's ModHub [DATA −127 … 127] 0.94 semitone per
    // unit at a full-scale peak (Δ = 0.94 · ModHub · peak: 56.4 st per unit of sine peak at ±60, linear in the peak's
    // amplitude). The new one: Lfo_Amount [DATA 0 … 1] 94 semitones per 1.0 (0.5 → ±47 st) and Envelope_Amount [DATA
    // −1 … 1] 96 semitones per 1.0 at a full-scale peak (0.5 → 47.8 st per unit of peak, −0.5 the mirror)
    const auto lfoRaw = manual (device, newer ? "Lfo_Amount" : "LfoAmount", 0.0);
    const auto envRaw = manual (device, newer ? "Envelope_Amount" : "ModHub", 0.0);
    const auto lfoSemitones = newer ? std::clamp (lfoRaw, 0.0, 1.0) * af2LfoSemitonesPerUnit : lfoRaw;
    const auto envSemitones = newer ? std::clamp (envRaw, -1.0, 1.0) * af2EnvelopeSemitonesPerUnit : envRaw * legacyEnvelopeSemitonesPerUnit;
    const auto lfoDepth = lfoSemitones / 12.0 / octavesPerUnit;
    const auto envDepth = envSemitones / 12.0 / octavesPerUnit;
    const auto lfoOn = (newer || manualBool (device, "Lfo/IsOn", true)) && std::abs (lfoDepth) > 1.0e-5;
    const auto envOn = std::abs (envDepth) > 1.0e-5;
    if (! lfoOn && ! envOn)
        return;
    // the shape [MEASURED 190ei on the older device at 1 Hz: 0 sine (0 rising at song time 0), 1 square (+ first), 2
    // triangle (from the bottom), 3 saw up, 4 saw down, 5 and 6 random steps four per cycle; DATA new 0 … 7: ASSUMED the
    // same first six] → the controller's sine 0, triangle 1 (it starts at its top: half a cycle later), square 2, saw 3
    // (rising), random 4
    const auto type = (int) manual (device, newer ? "Lfo_Waveform" : "Lfo/Type", 0);
    const auto random = type == 5 || (type == 6 && ! newer);
    // a falling saw alone (no envelope) is the rising one with the curve turned over
    const auto fallingSaw = lfoOn && ! envOn && type == 4;
    const auto m = cutoffModulation (out.slot.params[1], lfoOn ? lfoDepth : 0.0, envOn ? envDepth : 0.0, fallingSaw);
    Translated::Modulator mod;
    mod.controller = makeEffectSlot (EffectKind::peakController);
    auto& p = mod.controller.params;
    p[0] = 0.0f;                                         // Base
    p[1] = envOn ? m.peakAmount : 0.5f;                  // Amount (0.5 = none)
    p[2] = 0.5f;                                         // Tension (linear)
    p[4] = 0.0f;                                         // LFO base
    p[5] = lfoOn ? m.lfoAmount : 0.5f;                   // LFO amount (0.5 = none)
    p[6] = 0.5f;                                         // LFO tension (linear)
    p[10] = 0.0f;                                        // Mute off: the audio passes
    p[11] = 0.0f;                                        // Ramp off
    mod.param = 1;
    mod.curve = m.curve;
    if (envOn)
    {
        // the follower's release → the peak's linear fall from full scale; its attack: the peak rises at once
        const auto release = newer ? manual (device, "Envelope_Release", 0.25) * 1000.0 : manual (device, "Release", 200.0); // [DATA s / ms]
        const auto attack = newer ? manual (device, "Envelope_Attack", 0.001) * 1000.0 : manual (device, "Attack", 6.0);
        p[3] = peakDecayValue (release, context.bpm);
        mod.notes.push_back (text ("envelope follower: amount %.2f as %.1f semitones at a full-scale peak (measured), release %.0f ms as a straight fall",
                                   envRaw, envSemitones, release));
        if (attack > 1.0)
            mod.notes.push_back (text ("envelope follower attack %.1f ms (the Peak Controller rises at once)", attack));
        if (manualBool (device, "SideChain/OnOff", false))
        {
            const auto side = sideInputOf (device);
            mod.sideTrackId = side.trackId;
            mod.sideOther = side.trackId < 0;
        }
    }
    if (lfoOn)
    {
        // the rate: Hz or a time (fixed at the set's tempo) or a note value [DATA: older RateType 0 / 1, BeatRate 0 … 21]
        double beats = 1.0;
        bool synced = false;
        if (newer)
            beats = autoFilter2LfoBeats ((int) manual (device, "Lfo_TimeMode", 0), manual (device, "Lfo_Frequency", 1.0), manual (device, "Lfo_Time", 1.0),
                                         (int) manual (device, "Lfo_SyncedRate", 9), manual (device, "Lfo_Sixteenth", 4.0), context.bpm, &synced);
        else
        {
            synced = (int) manual (device, "Lfo/RateType", 0) == 1;
            beats = synced ? liveBeatRateBeats ((int) manual (device, "Lfo/BeatRate", 4)) : context.bpm / 60.0 / std::max (manual (device, "Lfo/Frequency", 1.0), 0.01);
        }
        // random steps: four per cycle of the rate [MEASURED 190ei at 1 Hz: a new value every 0.25 s] = one per period
        // of the controller's random
        p[7] = peakLfoSpeedValue (random ? beats / 4.0 : beats);
        static constexpr int shapes[6] { 0, 2, 1, 3, 3, 4 };
        p[8] = (float) (random ? 4 : shapes[std::clamp (type, 0, 5)]) / 4.0f;
        // the phase offset (degrees), the triangle's half cycle and the half cycle a turned-over curve needs
        auto phase = manual (device, newer ? "Lfo_PhaseOffset" : "Lfo/Offset", 0.0) / 360.0;
        if (type == 2)
            phase += 0.5;
        if (m.invert && ! fallingSaw)
            phase += 0.5;
        p[9] = (float) (phase - std::floor (phase));
        mod.notes.push_back (text ("LFO: amount %.2f as %.1f semitones either side (measured)", lfoRaw, lfoSemitones));
        if (! synced)
            mod.notes.push_back (text ("LFO rate %.2f Hz held as a note length at the set's tempo", beats > 0.0 ? context.bpm / 60.0 / beats : 0.0));
        if (type == 4 && ! fallingSaw)
            mod.notes.push_back ("LFO falling saw as a rising one (the envelope follower shares the curve)");
        if (newer && type > 5)
            mod.notes.push_back ("LFO shape as a sine");
        if (std::abs (manual (device, newer ? "Lfo_Spin" : "Lfo/Spin", 0.0)) > 1.0e-4
            || (newer ? manual (device, "Lfo_Phase", 0.0) : manual (device, "Lfo/Phase", 0.0)) > 1.0e-3)
            mod.notes.push_back ("LFO stereo offset between left and right (one cutoff for both sides)");
    }
    mod.what = lfoOn && envOn ? "LFO and envelope follower" : (lfoOn ? "LFO" : "envelope follower");
    out.modulator = std::move (mod);
}

// ================================================================================================ Operator, Drift

double operatorRatioSemitones (int coarse, double fine) noexcept
{
    const auto c = coarse <= 0 ? 0.5 : (double) std::min (coarse, 48);
    return 12.0 * std::log2 (c * (1.0 + std::clamp (fine, 0.0, 1000.0) / 1000.0));
}

void setOscillatorPitch (SynthOscillator& osc, double semitones) noexcept
{
    const auto whole = std::round (semitones);
    auto st = (int) whole;
    osc.detune = (float) std::clamp ((semitones - whole) * 100.0, -100.0, 100.0);
    osc.octave = std::clamp (st / 12, -3, 3); // (toward zero: ±11 semitones stay in the octave)
    osc.semitones = std::clamp (st - 12 * osc.octave, -11, 11);
}

namespace
{
// the synth's level and the channel volume for a linear gain (the channel's curve tops at 1 = 0 dB)
void setLevel (ChannelDef& channel, double gain)
{
    const auto g = std::clamp (gain, 0.0, 1.0);
    channel.volume = g <= 0.0 ? 0.0f : (float) (std::log (10.0 * g + 1.0) / std::log (11.0));
}

// Operator's envelope [DATA: ms and linear levels] as ADSR seconds; anything beyond a plain ADSR goes to the notes
void operatorEnvelope (const Element& env, SynthSettings& s, Notes& notes)
{
    s.attack = (float) (manual (env, "AttackTime", 0.1) / 1000.0);
    s.decay = (float) (manual (env, "DecayTime", 600.0) / 1000.0);
    s.sustain = (float) std::clamp (manual (env, "SustainLevel", 1.0), 0.0, 1.0);
    s.release = (float) (manual (env, "ReleaseTime", 50.0) / 1000.0);
    if (! isFloor (manual (env, "AttackLevel", 0.0)) || manual (env, "DecayLevel", 1.0) < 0.999 || ! isFloor (manual (env, "ReleaseLevel", 0.0)))
        notes.approximated.push_back ("envelope start / peak / end levels (the synth's envelope runs 0 → 1 → sustain → 0)");
    if (std::abs (manual (env, "AttackSlope", 0.0)) > 1.0e-3 || std::abs (manual (env, "DecaySlope", 1.0) - 1.0) > 1.0e-3 || std::abs (manual (env, "ReleaseSlope", 1.0) - 1.0) > 1.0e-3)
        notes.approximated.push_back ("envelope slopes (straight segments)");
    if ((int) manual (env, "LoopMode", 0) != 0)
        notes.dropped.push_back ("envelope loop mode");
}
} // namespace

bool operatorToSynth (const Element& device, ChannelDef& channel, double bpm, Notes& notes)
{
    if (! device.path ("Operator.0/Envelope") || ! device.path ("Globals/Algorithm"))
        return false;
    const auto algorithm = (int) manual (device, "Globals/Algorithm", 0);
    // which operators sound: on, above Live's floor
    std::array<bool, 4> sounding {};
    for (int k = 0; k < 4; ++k)
    {
        const auto stem = "Operator." + std::to_string (k) + "/";
        sounding[(std::size_t) k] = manualBool (device, stem + "IsOn", true) && ! isFloor (manual (device, stem + "Volume", 1.0));
    }
    // A is a carrier in every algorithm [DOC]; with B … D silent it is a plain oscillator. Algorithm 10 = four carriers,
    // no modulation [DOC: the last algorithm, all side by side]. Any other case is FM: not translated.
    std::vector<int> carriers;
    if (! sounding[1] && ! sounding[2] && ! sounding[3])
        carriers = { 0 };
    else if (algorithm == 10)
        for (int k = 0; k < 4; ++k)
            if (sounding[(std::size_t) k])
                carriers.push_back (k);
    if (carriers.empty() || ! sounding[(std::size_t) carriers.front()])
        return false;
    // waveforms: 0 = sine, 21 = white noise, 22 = User (its 64 harmonics) [DATA: 0, 15, 20, 21, 22 seen; 21 is operator A
    // of the shipped "White Noise" preset, alone; the User wave holds the harmonic table; the others' order is not
    // settled: not translated]
    for (const auto k : carriers)
    {
        const auto wave = (int) manual (device, "Operator." + std::to_string (k) + "/WaveForm", 0);
        if (wave != 0 && wave != 21 && wave != 22)
            return false;
        if (manual (device, "Operator." + std::to_string (k) + "/Feedback", 0.0) > 0.5 && carriers.size() > 1)
            return false;
    }

    channel.instrument = InstrumentKind::synth;
    auto& s = channel.synth;
    s = SynthSettings {};
    for (auto& o : s.osc)
        o.level = 0.0f;
    bool userUsed = false, differentEnvelopes = false;
    for (std::size_t i = 0; i < carriers.size() && i < 3; ++i)
    {
        const auto k = carriers[i];
        const auto stem = "Operator." + std::to_string (k) + "/";
        auto& o = s.osc[i];
        const auto wave = (int) manual (device, stem + "WaveForm", 0);
        if (wave == 22)
        {
            if (userUsed)
                return false; // one harmonic table per synth
            userUsed = true;
            o.wave = Waveform::spectral;
            for (int h = 0; h < 64 && h < SynthSettings::spectrumSize; ++h)
                s.spectrum[(std::size_t) h] = (float) std::clamp (device.number (stem + "UserHarmonics/Harmonics." + std::to_string (h), h == 0 ? 1.0 : 0.0), 0.0, 1.0);
        }
        else
            o.wave = wave == 21 ? Waveform::noise : Waveform::sine;
        setOscillatorPitch (o, operatorRatioSemitones ((int) manual (device, stem + "Tune/Coarse", 1), manual (device, stem + "Tune/Fine", 0.0))
                                   + manual (device, "Globals/Transpose", 0.0));
        o.level = (float) std::clamp (manual (device, stem + "Volume", 1.0), 0.0, 1.0);
        if (manualBool (device, stem + "Tune/FixedFrequencyOn", false))
            notes.dropped.push_back ("fixed-frequency operator (it follows the note here)");
        if (i == 0)
            operatorEnvelope (device.path (stem + "Envelope"), s, notes);
        else
            differentEnvelopes = differentEnvelopes || std::abs (manual (device, stem + "Envelope/DecayTime", 0.0) - s.decay * 1000.0) > 1.0
                                 || std::abs (manual (device, stem + "Envelope/AttackTime", 0.0) - s.attack * 1000.0) > 1.0;
        if (manual (device, stem + "Feedback", 0.0) > 0.5)
            notes.dropped.push_back ("operator feedback");
    }
    if (carriers.size() > 3)
        notes.dropped.push_back ("the fourth carrier (the synth has three oscillators)");
    if (differentEnvelopes)
        notes.approximated.push_back ("one envelope for every oscillator (operator A's)");
    // the filter [DATA: Filter/OnOff, Type 0 … 4 (low, high, band pass, notch, morph: the Auto Filter's list ASSUMED),
    // Frequency Hz, Resonance 0 … 1.25, Slope (24 dB/oct: the synth's 4-pole ladder ASSUMED), FrequencyKeyScale %]
    if (manualBool (device, "Filter/OnOff", false))
    {
        static constexpr int types[5] { 0, 2, 1, 3, 0 };
        s.filterType = types[std::clamp ((int) manual (device, "Filter/Type", 0), 0, 4)];
        s.cutoff = (float) std::clamp (manual (device, "Filter/Frequency", 20000.0), 20.0, 20000.0);
        s.resonance = (float) std::clamp (manual (device, "Filter/Resonance", 0.0) / 1.25, 0.0, 1.0);
        s.ladder = manualBool (device, "Filter/Slope", false) && s.filterType == 0;
        s.keyTrack = (float) std::clamp (manual (device, "Filter/FrequencyKeyScale", 0.0) / 100.0, 0.0, 2.0);
        notes.approximated.push_back ("filter on the synth's filter (resonance and key tracking laws assumed)");
        if (std::abs (manual (device, "Filter/EnvelopeAmount", 0.0)) > 0.5)
            notes.dropped.push_back ("filter envelope");
        if (manual (device, "Filter/Drive", 0.0) > 0.01 || (int) manual (device, "Filter/CircuitLpHp", 0) != 0)
            notes.dropped.push_back ("filter circuit / drive");
    }
    else
        s.cutoff = 20000.0f;
    // voices: Live's polyphony index; glide [DATA: PortamentoOn, PortamentoTime ms] at the set's tempo
    s.porta = manualBool (device, "Globals/PortamentoOn", false);
    if (s.porta)
        s.portaBeats = (float) (manual (device, "Globals/PortamentoTime", 50.0) / 1000.0 * bpm / 60.0);
    setLevel (channel, manual (device, "Globals/Volume", 1.0));
    channel.pan = (float) std::clamp (manual (device, "Globals/Panorama", 0.0), -1.0, 1.0);
    if (manual (device, "Globals/SpreadAmount", 0.0) > 0.5)
        notes.dropped.push_back ("spread");
    if (std::abs (manual (device, "Globals/Tone", 0.7) - 0.7) > 0.01)
        notes.dropped.push_back ("tone (the anti-alias filter)");
    // the LFO and the pitch envelope reach an operator only when both its own switch and the section's are on [DATA]
    bool lfo = false, pitchEnv = false;
    for (const auto k : carriers)
    {
        const auto stem = "Operator." + std::to_string (k) + "/";
        lfo = lfo || (manualBool (device, "Lfo/LfoOn", false) && manualBool (device, stem + "LfoOn", true));
        pitchEnv = pitchEnv || (manualBool (device, "PitchEnv/PitchEnvOn", false) && manualBool (device, stem + "PitchEnvOn", true));
    }
    if (lfo)
        notes.dropped.push_back ("LFO");
    if (pitchEnv)
        notes.dropped.push_back ("pitch envelope");
    notes.approximated.push_back (std::string (carriers.size() == 1 ? "operator A alone" : "the carriers of algorithm 11") + " as the built-in synth's oscillators (level not matched to Live's)");
    return true;
}

bool driftToSynth (const Element& device, ChannelDef& channel, double bpm, Notes& notes)
{
    if (! has (device, "Oscillator1_Type") || ! has (device, "Filter_Frequency") || ! has (device, "Envelope1_Attack"))
        return false;
    channel.instrument = InstrumentKind::synth;
    auto& s = channel.synth;
    s = SynthSettings {};
    // oscillator 1 [DOC order: sine, triangle, shark tooth, saturated, saw, pulse, rectangle; DATA 0 … 6] and 2 [DOC:
    // sine, triangle, saturated, saw, rectangle; DATA 0 … 4] as the synth's nearest waves; octaves [DATA −2 … 3 / −3 … 2]
    static constexpr Waveform waves1[7] { Waveform::sine, Waveform::triangle, Waveform::saw, Waveform::sine, Waveform::saw, Waveform::square, Waveform::square };
    static constexpr Waveform waves2[5] { Waveform::sine, Waveform::triangle, Waveform::sine, Waveform::saw, Waveform::square };
    const auto t1 = std::clamp ((int) manual (device, "Oscillator1_Type", 0), 0, 6);
    const auto t2 = std::clamp ((int) manual (device, "Oscillator2_Type", 0), 0, 4);
    const auto transpose = manual (device, "Global_Transpose", 0.0);
    s.osc[0].wave = waves1[t1];
    setOscillatorPitch (s.osc[0], 12.0 * manual (device, "Oscillator1_Transpose", 0.0) + transpose);
    s.osc[0].level = manualBool (device, "Mixer_OscillatorOn1", true) ? (float) std::clamp (manual (device, "Mixer_OscillatorGain1", 1.0), 0.0, 1.0) : 0.0f;
    s.osc[1].wave = waves2[t2];
    setOscillatorPitch (s.osc[1], 12.0 * manual (device, "Oscillator2_Transpose", 0.0) + manual (device, "Oscillator2_Detune", 0.0) + transpose);
    s.osc[1].level = manualBool (device, "Mixer_OscillatorOn2", true) ? (float) std::clamp (manual (device, "Mixer_OscillatorGain2", 1.0), 0.0, 1.0) : 0.0f;
    s.osc[2].wave = Waveform::noise;
    s.osc[2].level = manualBool (device, "Mixer_NoiseOn", true) ? (float) std::clamp (manual (device, "Mixer_NoiseLevel", 0.0), 0.0, 1.0) : 0.0f;
    if (t1 == 2 || t1 == 3 || t2 == 2)
        notes.approximated.push_back ("shark tooth / saturated waves as the synth's saw / sine");
    if (std::abs (manual (device, "Oscillator1_Shape", 0.0)) > 1.0e-3)
        notes.dropped.push_back ("oscillator 1 shape");
    // the filter: low pass [DOC: type I 12 dB/oct, II 24 dB/oct ladder; DATA Filter_Type 0 … 1], key tracking, the high pass
    s.filterType = 0;
    s.ladder = (int) manual (device, "Filter_Type", 0) == 1;
    s.cutoff = (float) std::clamp (manual (device, "Filter_Frequency", 20000.0), 20.0, 20000.0);
    s.resonance = (float) std::clamp (manual (device, "Filter_Resonance", 0.0), 0.0, 1.0);
    s.keyTrack = (float) std::clamp (manual (device, "Filter_Tracking", 0.0), 0.0, 1.0);
    if (manual (device, "Filter_HiPassFrequency", 10.0) > 20.0)
        notes.dropped.push_back ("the high pass before the filter");
    // envelope 1 = the amp envelope [DOC] in seconds [DATA]
    s.attack = (float) manual (device, "Envelope1_Attack", 0.001);
    s.decay = (float) manual (device, "Envelope1_Decay", 0.6);
    s.sustain = (float) std::clamp (manual (device, "Envelope1_Sustain", 1.0), 0.0, 1.0);
    s.release = (float) manual (device, "Envelope1_Release", 0.6);
    // voice mode [MEASURED 190ei, the menu: 0 Poly, 1 Mono, 2 Stereo, 3 Unison] and glide (s) at the set's tempo
    const auto voiceMode = (int) manual (device, "Global_VoiceMode", 0);
    s.mono = voiceMode == 1;
    if (voiceMode == 2)
        notes.dropped.push_back ("stereo voices");
    else if (voiceMode == 3)
        notes.dropped.push_back ("unison voices");
    const auto glide = manual (device, "Global_Glide", 0.0);
    s.porta = glide > 0.001;
    s.portaBeats = (float) (glide * bpm / 60.0);
    setLevel (channel, manual (device, "Global_Volume", 0.5));
    // what the built-in synth has no place for
    notes.dropped.push_back ("modulation (envelope 2, LFO, cycling envelope and the modulation matrix)");
    if (manual (device, "Global_DriftDepth", 0.0) > 1.0e-3)
        notes.dropped.push_back ("drift (the random detune of each voice)");
    notes.approximated.push_back ("oscillators, filter and amp envelope on the built-in synth (level not matched to Live's)");
    return true;
}
} // namespace lynx::alsdev
