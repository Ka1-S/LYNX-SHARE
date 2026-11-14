#include "core/AlsDevices.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/DynamicsSuite.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/LynxVerbs.h"
#include "core/XmlLite.h"

namespace lynx::alsdev
{
namespace
{
using xml::Element;

constexpr double root2 = 1.4142135623730951;

float unit (double v) noexcept { return (float) std::clamp (v, 0.0, 1.0); }
float linValue (double v, double lo, double hi) noexcept { return unit ((v - lo) / (hi - lo)); }
float choiceValue (int index, int count) noexcept { return count > 1 ? (float) std::clamp (index, 0, count - 1) / (float) (count - 1) : 0.0f; }

std::string text (const char* format, double a, double b = 0.0)
{
    char out[160];
    std::snprintf (out, sizeof (out), format, a, b);
    return out;
}

// A device parameter: its "Manual" value [DATA]
double manual (const Element& device, const std::string& name, double fallback) { return device.number (name + "/Manual", fallback); }
bool manualBool (const Element& device, const std::string& name, bool fallback) { return device.boolean (name + "/Manual", fallback); }
bool has (const Element& device, const std::string& name) { return (bool) device.path (name + "/Manual"); }

// The side-chain EQ of Compressor ("SideChainEq_On" …) and Glue ("SideChainEq/On" …) on Crest's first side-chain band.
// Types [DOC: low shelf, bell, high shelf, low pass, band pass, high pass; DATA: 0…5, order ASSUMED]
void sideChainEq (const Element& device, const std::string& stem, EffectParams& p, Notes& notes)
{
    using namespace dyn::crest;
    if (! manualBool (device, stem + "On", false))
        return;
    const auto mode = (int) manual (device, stem + "Mode", 5);
    const auto hz = manual (device, stem + "Freq", 200.0);
    const auto q = manual (device, stem + "Q", 0.7071);
    const auto gain = manual (device, stem + "Gain", 0.0);
    static constexpr facet::Shape shapes[6] { facet::Shape::lowShelf, facet::Shape::bell, facet::Shape::highShelf,
                                              facet::Shape::highCut, facet::Shape::bandPass, facet::Shape::lowCut };
    static constexpr Eq8Mode laws[6] { Eq8Mode::lowShelf, Eq8Mode::bell, Eq8Mode::highShelf, Eq8Mode::highCut12, Eq8Mode::bell, Eq8Mode::lowCut12 };
    const auto k = (std::size_t) std::clamp (mode, 0, 5);
    p[(std::size_t) scParam (0, 0)] = facet::shapeValue (shapes[k]);
    p[(std::size_t) scParam (0, 1)] = facet::hzValue (hz);
    p[(std::size_t) scParam (0, 2)] = facet::gainValue (facet::hasGain (shapes[k]) ? gain : 0.0);
    p[(std::size_t) scParam (0, 3)] = facet::qValue (facetQ (laws[k], q));
    p[(std::size_t) scParam (0, 4)] = facet::slopeValue (12);
    notes.approximated.push_back ("side-chain EQ (filter shapes fitted)");
}

void sideChainCommon (const Element& device, EffectParams& p, Notes& notes, SideInput& side)
{
    using namespace dyn::crest;
    side = sideInputOf (device);
    if (side.on)
    {
        p[scExternal] = side.trackId >= 0 ? 1.0f : 0.0f;
        p[scLevel] = linValue (dbOfGain (manual (device, "SideChain/RoutedInput/Volume", 1.0)), -36.0, 36.0);
        if (std::abs (manual (device, "SideChain/DryWet", 1.0) - 1.0) > 1.0e-6)
            notes.dropped.push_back ("side-chain dry/wet (the detector hears the side-chain input only)");
    }
}
} // namespace

// The side-chain input of Compressor / Glue [DATA: SideChain/OnOff, RoutedInput/Routable/Target "AudioIn/Track.<id>/
// <tap>", "AudioIn/None"]: the tap is PreFxOut, PostFxOut, PostMixerOut or a device inside the track
SideInput sideInputOf (const Element& device)
{
    SideInput s;
    s.on = manualBool (device, "SideChain/OnOff", false);
    if (! s.on)
        return s;
    const auto target = device.value ("SideChain/RoutedInput/Routable/Target");
    static constexpr std::string_view prefix = "AudioIn/Track.";
    if (target.rfind (prefix, 0) == 0)
    {
        const auto rest = target.substr (prefix.size());
        char* end = nullptr;
        const auto id = std::strtol (rest.c_str(), &end, 10);
        if (end != rest.c_str())
        {
            s.trackId = (int) id;
            s.otherTap = std::string_view (end) != "/PostMixerOut";
        }
    }
    return s;
}

double dbOfGain (double linear) noexcept { return linear > 0.0 ? std::max (-200.0, 20.0 * std::log10 (linear)) : -200.0; }
double gainOfDb (double db) noexcept { return std::pow (10.0, db / 20.0); }

// ================================================================================================ EQ Eight

double eq8CutSectionScale (double liveQ) noexcept
{
    return std::max (0.05, 1.0 + 0.2632 * std::log (std::max (liveQ, 1.0e-3) / 0.7071));
}

double eq8ShelfCookbookQ (double liveQ) noexcept
{
    const auto l = std::log (std::clamp (liveQ, 0.1, 18.0));
    return std::exp (((((-0.000248 * l + 0.002322) * l - 0.004732) * l - 0.036516) * l + 0.410151) * l - 0.200340);
}

double eq8AdaptiveQ (double q, double gainDb, double factor) noexcept
{
    return q * std::pow (std::clamp (factor, 1.0, 4.0), std::min (std::abs (gainDb), 60.0) - 6.0);
}

namespace
{
double facetQUnclamped (Eq8Mode mode, double liveQ, double adaptive = 1.0) noexcept
{
    const auto q = std::max (liveQ, 1.0e-3);
    switch (mode)
    {
        case Eq8Mode::bell: return root2 * q * adaptive;
        case Eq8Mode::notch:
        case Eq8Mode::lowCut12:
        case Eq8Mode::highCut12: return root2 * q;
        // Facet's shelf is the cookbook shelf with Qs = 0.7071·Qfacet^0.4697
        case Eq8Mode::lowShelf:
        case Eq8Mode::highShelf: return std::pow (eq8ShelfCookbookQ (q) * adaptive / std::sqrt (0.5), 1.0 / 0.4697);
        // with Facet's cut law "all sections" its Q is the scale of the Butterworth section Qs
        case Eq8Mode::lowCut48:
        case Eq8Mode::highCut48: return eq8CutSectionScale (q);
    }
    return q;
}
} // namespace

double facetQ (Eq8Mode mode, double liveQ, double adaptive) noexcept { return std::clamp (facetQUnclamped (mode, liveQ, adaptive), 0.025, 40.0); }

void setFacetBand (EffectParams& p, int b, const Eq8Band& band, double gainSign, int placement)
{
    using facet::Shape;
    Shape shape = Shape::bell;
    int slope = 12;
    switch (band.mode)
    {
        case Eq8Mode::lowCut48: shape = Shape::lowCut; slope = 48; break;
        case Eq8Mode::lowCut12: shape = Shape::lowCut; break;
        case Eq8Mode::lowShelf: shape = Shape::lowShelf; break;
        case Eq8Mode::bell: shape = Shape::bell; break;
        case Eq8Mode::notch: shape = Shape::notch; break;
        case Eq8Mode::highShelf: shape = Shape::highShelf; break;
        case Eq8Mode::highCut12: shape = Shape::highCut; break;
        case Eq8Mode::highCut48: shape = Shape::highCut; slope = 48; break;
    }
    p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (shape);
    p[(std::size_t) facet::param (b, 1)] = facet::hzValue (band.hz);
    p[(std::size_t) facet::param (b, 2)] = facet::gainValue (facet::hasGain (shape) ? gainSign * band.gainDb : 0.0);
    p[(std::size_t) facet::param (b, 3)] = facet::qValue (facetQ (band.mode, band.q, band.adaptive));
    p[(std::size_t) facet::param (b, 4)] = facet::slopeValue (slope);
    p[(std::size_t) (facet::powerParam0 + b)] = band.on ? 0.0f : 1.0f;
    p[(std::size_t) (facet::placementParam0 + b)] = facet::placementValue ((facet::Placement) placement);
}

bool eqEightToFacet (const Element& device, EffectSlot& slot, Notes& notes)
{
    if (! device.path ("Bands.0/ParameterA/Freq/Manual"))
        return false;
    slot = makeEffectSlot (EffectKind::facetEq);
    auto& p = slot.params;
    const auto readBand = [&device] (int b, const char* curve) {
        const auto stem = "Bands." + std::to_string (b) + "/" + curve + "/";
        Eq8Band band;
        band.on = manualBool (device, stem + "IsOn", false);
        band.mode = (Eq8Mode) std::clamp ((int) manual (device, stem + "Mode", 3), 0, 7);
        band.hz = manual (device, stem + "Freq", 1000.0);
        band.gainDb = manual (device, stem + "Gain", 0.0);
        band.q = manual (device, stem + "Q", 0.7071);
        return band;
    };
    // Scale [DATA: −2 … 2 = −200 … 200 %]: Facet's gain scale is 0 … 200 %, a negative one turns the gains over
    const auto scale = std::clamp (manual (device, "Scale", 1.0), -2.0, 2.0);
    const auto sign = scale < 0.0 ? -1.0 : 1.0;
    p[facet::scaleParam] = unit (std::abs (scale) / 2.0);
    p[facet::outputParam] = linValue (manual (device, "GlobalGain", 0.0), -24.0, 24.0); // ±12 dB in Live

    // 48 dB cuts [MEASURED 190cw r01–r03, 190cz Q 0.1 … 10]: every Butterworth section scaled with Q (Facet's "all
    // sections" cut law)
    p[facet::cutLawParam] = 1.0f;
    bool clampedQ = false;
    const auto noteBand = [&] (const Eq8Band& band) {
        const auto wanted = facetQUnclamped (band.mode, band.q, band.adaptive);
        clampedQ = clampedQ || (band.on && (wanted > 40.0 || wanted < 0.025));
        // the laws hold where they were measured (rounds 190cw / 190cz); outside, they are extrapolated and listed
        if (band.on && (band.mode == Eq8Mode::lowCut48 || band.mode == Eq8Mode::highCut48) && (band.q < 0.095 || band.q > 10.5))
            notes.approximated.push_back (text ("48 dB/oct cut at Q %.2f (Live's law measured at Q 0.1 … 10)", band.q));
    };
    // Adaptive Q [MEASURED 190cz: 9 bell and 9 shelf renders, exact]: Q × factor^(|gain| − 6), the gain after Scale; on
    // shelves when AdaptiveQAffectsShelves [DATA: true in 178 of 181 shipped devices]
    const auto adaptive = manualBool (device, "AdaptiveQ", false);
    const auto adaptiveFactor = device.number ("AdaptiveQFactor", 1.12);
    const auto shelvesToo = device.boolean ("AdaptiveQAffectsShelves", true);
    const auto adapt = [&] (Eq8Band band) {
        const auto shelf = band.mode == Eq8Mode::lowShelf || band.mode == Eq8Mode::highShelf;
        if (adaptive && (band.mode == Eq8Mode::bell || (shelf && shelvesToo)))
            band.adaptive = eq8AdaptiveQ (1.0, band.gainDb * std::abs (scale), adaptiveFactor);
        return band;
    };

    // Mode [DATA: 0 Stereo, 1 L/R, 2 M/S]: in Stereo only curve A sounds; in L/R and M/S curve A is the left / mid
    // channel's and curve B the right / side channel's
    const auto mode = std::clamp ((int) device.number ("Mode", 0), 0, 2);
    if (mode == 0)
    {
        for (int b = 0; b < facet::bands; ++b)
        {
            const auto band = adapt (readBand (b, "ParameterA"));
            setFacetBand (p, b, band, sign, (int) facet::Placement::stereo);
            noteBand (band);
        }
    }
    else
    {
        const auto placeA = mode == 1 ? facet::Placement::left : facet::Placement::mid;
        const auto placeB = mode == 1 ? facet::Placement::right : facet::Placement::side;
        int used = 0, left = 0;
        for (const auto* curve : { "ParameterA", "ParameterB" })
            for (int b = 0; b < 8; ++b)
            {
                const auto band = adapt (readBand (b, curve));
                if (! band.on)
                    continue;
                if (used >= facet::bands)
                {
                    ++left;
                    continue;
                }
                setFacetBand (p, used++, band, sign, (int) (std::string_view (curve) == "ParameterA" ? placeA : placeB));
                noteBand (band);
            }
        for (int b = used; b < facet::bands; ++b)
            p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (facet::Shape::off);
        if (left > 0)
            notes.dropped.push_back (text ("%.0f band(s) of the second channel curve (Facet EQ has 8 bands in all)", left));
    }
    if (clampedQ)
        notes.approximated.push_back ("a band's resonance beyond Facet EQ's Q range");
    return true;
}

// ================================================================================================ Compressor, Glue

double glueAttackMs (int index) noexcept
{
    static constexpr double ms[7] { 0.01, 0.1, 0.3, 1.0, 3.0, 10.0, 30.0 };
    return ms[std::clamp (index, 0, 6)];
}
double glueReleaseMs (int index) noexcept
{
    static constexpr double ms[7] { 100.0, 200.0, 400.0, 600.0, 800.0, 1200.0, 0.0 };
    return ms[std::clamp (index, 0, 6)];
}
double glueRatio (int index) noexcept
{
    static constexpr double r[3] { 2.0, 4.0, 10.0 };
    return r[std::clamp (index, 0, 2)];
}
double compressorLookaheadMs (int index) noexcept
{
    static constexpr double ms[3] { 0.0, 1.0, 10.0 };
    return ms[std::clamp (index, 0, 2)];
}

bool compressorToCrest (const Element& device, EffectSlot& slot, Notes& notes, SideInput& side)
{
    using namespace dyn::crest;
    if (! has (device, "Threshold") || ! has (device, "Ratio"))
        return false;
    // Model [DATA: 0 … 2; DOC: Peak, RMS, Expand]: Expand raises what is over the threshold, which no Crest style does
    const auto model = (int) manual (device, "Model", 0);
    if (model == 2)
        return false;
    slot = makeEffectSlot (EffectKind::crestCompressor);
    auto& p = slot.params;
    p[style] = styleValue (Style::clean);
    p[voicing] = 0.0f;
    if (model == 1)
        notes.approximated.push_back ("RMS detection (Crest's Clean style follows the peak level)");

    // Threshold: a linear gain [DATA: 0.000316 … 1.995 = −70 … +6 dB] → Crest's −60 … 0 dB
    const auto thresholdDbLive = dbOfGain (manual (device, "Threshold", 1.0));
    p[threshold] = linValue (thresholdDbLive, -60.0, 0.0);
    if (thresholdDbLive > 0.05 || thresholdDbLive < -60.05)
        notes.approximated.push_back (text ("threshold %.1f dB (Crest: -60 … 0 dB)", thresholdDbLive));
    // Ratio [DATA: 1 … the largest float = inf : 1] → 1 … 100 : 1, the top a true limit
    const auto r = manual (device, "Ratio", 4.0);
    p[ratio] = r >= 1.0e6 ? 1.0f : ratioValue (std::min (r, 100.0));
    if (r > 100.0 && r < 1.0e6)
        notes.approximated.push_back ("ratio above 100 : 1");
    // Attack [DATA: 0.01 … 1000 ms] → 0.005 … 250 ms; Release [1 … 3000 ms] → 10 … 2500 ms
    const auto attackLive = manual (device, "Attack", 2.0), releaseLive = manual (device, "Release", 50.0);
    p[attack] = attackValue (attackLive);
    p[release] = releaseValue (releaseLive);
    if (attackLive > 250.0)
        notes.approximated.push_back (text ("attack %.0f ms (Crest: up to 250 ms)", attackLive));
    if (releaseLive < 10.0 || releaseLive > 2500.0)
        notes.approximated.push_back (text ("release %.0f ms (Crest: 10 … 2500 ms)", releaseLive));
    p[autoRelease] = manualBool (device, "AutoReleaseControlOnOff", false) ? 1.0f : 0.0f;
    p[knee] = linValue (manual (device, "Knee", 6.0), 0.0, 72.0); // [DATA: 0 … 18 dB]
    p[range] = 1.0f;
    const auto la = compressorLookaheadMs ((int) manual (device, "LookAhead", 0));
    p[lookahead] = linValue (la, 0.0, 20.0);
    p[maxLookahead] = la <= 0.0 ? 0.0f : choiceValue (la <= 1.0 ? 1 : 4, 6); // off, 1, 2, 5, 10, 20 ms
    p[output] = linValue (manual (device, "Gain", 0.0), -36.0, 36.0); // [DATA: −36 … 36 dB]
    p[autoGain] = manualBool (device, "GainCompensation", false) ? 1.0f : 0.0f;
    p[mix] = unit (manual (device, "DryWet", 1.0));
    p[dryGain] = 0.0f;
    p[scListen] = device.boolean ("SideListen/Manual", false) ? 1.0f : 0.0f;
    if (! manualBool (device, "LogEnvelope", true))
        notes.dropped.push_back ("linear release envelope");
    sideChainCommon (device, p, notes, side);
    sideChainEq (device, "SideChainEq_", p, notes);
    return true;
}

bool glueToCrest (const Element& device, EffectSlot& slot, Notes& notes, SideInput& side)
{
    using namespace dyn::crest;
    if (! has (device, "Threshold") || ! has (device, "Makeup"))
        return false;
    slot = makeEffectSlot (EffectKind::crestCompressor);
    auto& p = slot.params;
    p[style] = styleValue (Style::bus);
    p[voicing] = 0.0f;
    p[threshold] = linValue (manual (device, "Threshold", 0.0), -60.0, 0.0); // [DATA: −40 … 0 dB]
    p[ratio] = ratioValue (glueRatio ((int) manual (device, "Ratio", 1)));
    p[attack] = attackValue (glueAttackMs ((int) manual (device, "Attack", 3)));
    const auto releaseIndex = (int) manual (device, "Release", 3);
    p[autoRelease] = releaseIndex >= 6 ? 1.0f : 0.0f;
    p[release] = releaseValue (releaseIndex >= 6 ? 600.0 : glueReleaseMs (releaseIndex));
    p[range] = linValue (glueRangeDb (manual (device, "Range", 70.0)), 0.0, 60.0);
    p[knee] = 0.0f;
    p[output] = linValue (manual (device, "Makeup", 0.0), -36.0, 36.0); // [DATA: 0 … 20 dB]
    p[autoGain] = 0.0f;
    p[mix] = unit (manual (device, "DryWet", 1.0));
    p[dryGain] = 0.0f;
    p[oversampling] = device.boolean ("Oversample", false) ? choiceValue (1, 4) : 0.0f; // 2x
    p[scListen] = device.boolean ("SideChainListen", false) ? 1.0f : 0.0f;
    if (manualBool (device, "PeakClipIn", false))
        notes.dropped.push_back ("Soft clip");
    sideChainCommon (device, p, notes, side);
    sideChainEq (device, "SideChainEq/", p, notes);
    // [MEASURED 190cz g01, round 190da] the side-chain EQ above holds: the rendered Glue (its preset's side-chain EQ on,
    // high pass 200 Hz, Q 0.71) weighted its detector as a 2nd-order high pass at 204.5 Hz, Q 0.70 (40 / 63 / 100 /
    // 160 / 250 Hz −28.2 / −20.5 / −12.8 / −5.8 / −1.6 dB); Crest with the translated band: −28.2 / −20.2 / −12.4 / −5.4 / −1.5
    return true;
}

double glueRangeDb (double liveRange) noexcept
{
    if (liveRange >= 50.0)
        return 60.0;
    return std::clamp (1.1727 * liveRange - 2.197, 0.0, 60.0);
}

// ================================================================================================ Utility

std::pair<double, double> utilityBalanceGains (double balance) noexcept
{
    const auto t = (std::clamp (balance, -1.0, 1.0) + 1.0) * 0.25 * 3.14159265358979323846;
    return { root2 * std::cos (t), root2 * std::sin (t) };
}

StereoMatrix utilityMatrix (const LiveUtility& u) noexcept
{
    const auto sl = u.phaseL ? -1.0 : 1.0, sr = u.phaseR ? -1.0 : 1.0;
    // the input stage: (l1, r1) in terms of (L, R)
    double a[2][2] { { sl, 0.0 }, { 0.0, sr } };
    if (u.channelMode == 0) { a[1][0] = sl; a[1][1] = 0.0; }
    else if (u.channelMode == 2) { a[0][0] = 0.0; a[0][1] = sr; }
    else if (u.channelMode == 3) { a[0][0] = 0.0; a[0][1] = sr; a[1][0] = sl; a[1][1] = 0.0; }
    // the stereo stage: out = M·gm ± S·gs; mid/side balance m [MEASURED 190cz u05–u13: mid × min(1, 2 − m), side ×
    // min(1, m), linear]
    double gm = 1.0, gs = u.midSideOn ? 1.0 : std::clamp (u.width, 0.0, 4.0);
    if (u.midSideOn)
    {
        const auto b = std::clamp (u.midSide, 0.0, 2.0);
        gm = std::min (1.0, 2.0 - b);
        gs = std::min (1.0, b);
    }
    if (u.mono)
        gs = 0.0;
    const double w[2][2] { { 0.5 * (gm + gs), 0.5 * (gm - gs) }, { 0.5 * (gm - gs), 0.5 * (gm + gs) } };
    StereoMatrix m;
    m.ll = w[0][0] * a[0][0] + w[0][1] * a[1][0];
    m.rl = w[0][0] * a[0][1] + w[0][1] * a[1][1];
    m.lr = w[1][0] * a[0][0] + w[1][1] * a[1][0];
    m.rr = w[1][0] * a[0][1] + w[1][1] * a[1][1];
    // the balance, last: constant power with a boost [MEASURED 190cw r15: +0.5 → L × 0.541, R × 1.307]
    const auto [gl, gr] = utilityBalanceGains (u.balance);
    m.ll *= gl;
    m.rl *= gl;
    m.lr *= gr;
    m.rr *= gr;
    return m;
}

bool utilityIsExact (const LiveUtility& u) noexcept
{
    const auto db = dbOfGain (u.gain);
    const auto gainOk = u.mute || u.gain <= 0.0 || (db > -24.0 && db <= 24.0);
    // [MEASURED 190cw r13–r17, 190cz u00–u13]: width M + w·S, the mid/side balance, the balance law after the width /
    // mono / mid-side stage and phase before channel mode are Live's; the matrix (balance included) must fit LYNX's ±2
    // coefficients
    const auto m = utilityMatrix (u);
    const auto fits = std::abs (m.ll) <= 2.0 && std::abs (m.rl) <= 2.0 && std::abs (m.lr) <= 2.0 && std::abs (m.rr) <= 2.0;
    return gainOk && fits && ! u.bassMono && ! u.dcFilter;
}

LiveUtility readUtility (const Element& device)
{
    LiveUtility u;
    // LegacyMode [DATA: false in every saved device]: the pre-Live-10 gain, in dB
    u.gain = device.boolean ("LegacyMode", false) ? gainOfDb (manual (device, "LegacyGain", 0.0)) : manual (device, "Gain", 1.0);
    u.mute = manualBool (device, "Mute", false);
    u.phaseL = manualBool (device, "PhaseInvertL", false);
    u.phaseR = manualBool (device, "PhaseInvertR", false);
    u.mono = manualBool (device, "Mono", false);
    u.bassMono = manualBool (device, "BassMono", false);
    u.dcFilter = manualBool (device, "DcFilter", false);
    u.channelMode = std::clamp ((int) manual (device, "ChannelMode", 1), 0, 3);
    u.width = manual (device, "StereoWidth", 1.0);
    u.midSideOn = device.boolean ("MidSideBalanceOn", false);
    u.midSide = manual (device, "MidSideBalance", 1.0);
    u.balance = std::clamp (manual (device, "Balance", 0.0), -1.0, 1.0);
    return u;
}

bool utilityToUtility (const Element& device, EffectSlot& slot, Notes& notes)
{
    if (! has (device, "Gain") && ! has (device, "LegacyGain"))
        return false;
    const auto u = readUtility (device);
    slot = makeEffectSlot (EffectKind::utility);
    auto& p = slot.params;
    // Gain: LYNX's ±24 dB, the bottom of the knob = mute
    const auto db = dbOfGain (u.gain);
    if (u.mute || u.gain <= 0.0)
        p[0] = 0.0f;
    else
    {
        p[0] = std::max (1.0e-4f, linValue (db, -24.0, 24.0));
        if (db > 24.0 || db <= -24.0)
            notes.approximated.push_back (text ("gain %.1f dB (Utility: -24 … +24 dB)", db));
    }
    // the balance is in the matrix (Live's constant-power law); LYNX's own balance stays centred
    p[1] = 0.5f;
    auto m = utilityMatrix (u);
    const auto fits = std::abs (m.ll) <= 2.0 && std::abs (m.rl) <= 2.0 && std::abs (m.lr) <= 2.0 && std::abs (m.rr) <= 2.0;
    p[2] = linValue (m.ll, -2.0, 2.0);
    p[3] = linValue (m.rl, -2.0, 2.0);
    p[4] = linValue (m.lr, -2.0, 2.0);
    p[5] = linValue (m.rr, -2.0, 2.0);
    if (! fits)
        notes.approximated.push_back (std::abs (u.balance) > 1.0e-6 ? text ("width %.0f %% with a balance (Utility's matrix coefficients reach 2)", u.width * 100.0)
                                                                    : text ("width %.0f %% (Utility's matrix reaches 300 %%)", u.width * 100.0));
    if (u.bassMono)
        notes.dropped.push_back (text ("Bass Mono (below %.0f Hz)", manual (device, "BassMonoFrequency", 120.0)));
    if (u.dcFilter)
        notes.dropped.push_back ("DC filter");
    return true;
}

std::optional<LiveUtility> liveUtilityOf (const EffectSlot& slot) noexcept
{
    if (slot.kind != EffectKind::utility)
        return std::nullopt;
    const auto& p = slot.params;
    LiveUtility u;
    if (std::abs (p[1] - 0.5f) > 1.0e-6f)
        return std::nullopt;
    if (p[0] <= 0.0f)
    {
        u.mute = true;
        u.gain = 1.0;
    }
    else
        u.gain = gainOfDb (((double) p[0] - 0.5) * 48.0);
    const auto near = [] (double a, double b) { return std::abs (a - b) < 1.0e-6; };
    const double ll = p[2] * 4.0 - 2.0, rl = p[3] * 4.0 - 2.0, lr = p[4] * 4.0 - 2.0, rr = p[5] * 4.0 - 2.0;
    for (int mode = 0; mode < 4; ++mode)
        for (int mono = 0; mono < 2; ++mono)
            for (int phase = 0; phase < 4; ++phase)
            {
                auto c = u;
                c.channelMode = mode;
                c.mono = mono != 0;
                c.phaseL = (phase & 1) != 0;
                c.phaseR = (phase & 2) != 0;
                if (! utilityIsExact (c))
                    continue;
                const auto m = utilityMatrix (c);
                if (near (m.ll, ll) && near (m.rl, rl) && near (m.lr, lr) && near (m.rr, rr))
                    return c;
            }
    return std::nullopt;
}

// ================================================================================================ Reverb

float atriumSizeOf (double size) noexcept
{
    const auto s = std::clamp (size, 0.222, 500.0);
    return unit (s <= 100.0 ? 0.5 * std::log (s / 0.222) / std::log (100.0 / 0.222) : 0.5 + 0.5 * std::log (s / 100.0) / std::log (5.0));
}
float atriumEarlyLateOf (double reflect, double diffuse) noexcept
{
    if (reflect <= 0.0 && diffuse <= 0.0)
        return 0.5f;
    return unit (std::atan2 (std::max (0.0, diffuse), std::max (0.0, reflect)) / (0.5 * 3.14159265358979323846));
}
double reverbInputLowHz (double centre, double width) noexcept { return centre * std::pow (2.0, -0.5 * width); }
double reverbInputHighHz (double centre, double width) noexcept { return centre * std::pow (2.0, 0.5 * width); }

bool reverbToAtrium (const Element& device, EffectSlot& slot, Notes& notes)
{
    if (! has (device, "DecayTime") || ! has (device, "PreDelay"))
        return false;
    slot = makeEffectSlot (EffectKind::atrium);
    auto& p = slot.params;
    p[0] = unit (manual (device, "MixDirect", 0.55));                      // Dry/Wet
    p[1] = linValue (manual (device, "PreDelay", 2.5), 0.0, 500.0);        // ms [DATA: 0.5 … 250]
    // Decay [DATA: 200 … 60000 ms; MEASURED 190cz r00–r04: Live's 1 kHz T30 ≈ 0.91 × the setting] → Atrium's decay
    // × 0.94 with the high damping below (a grid of Atrium decays and High mults against the T30 of Live's renders per
    // octave, 125 Hz … 8 kHz, at Decay 1 / 2.5 / 5 s: no octave further from Live than before, most 3 … 10 × closer)
    const auto decaySec = manual (device, "DecayTime", 1200.0) / 1000.0;
    p[2] = verbs::atrium::decayValue (reverbAtriumDecaySec (decaySec));
    // the input filter → High cut (100 … 15000 Hz) and Low cut (0 … 1000 Hz)
    const auto centre = manual (device, "BandFreq", 830.0), width = manual (device, "BandWidth", 5.85);
    p[3] = manualBool (device, "BandHighOn", false) ? linValue (reverbInputHighHz (centre, width), 100.0, 15000.0) : 1.0f;
    p[21] = manualBool (device, "BandLowOn", false) ? linValue (reverbInputLowHz (centre, width), 0.0, 1000.0) : 0.0f;
    p[4] = atriumEarlyLateOf (manual (device, "MixReflect", 1.0), manual (device, "MixDiffuse", 1.0));
    p[5] = atriumSizeOf (manual (device, "RoomSize", 100.0));
    // Chorus → the late lines' modulation (0.05 … 5 Hz; depth 0 … 1)
    const auto chorus = manualBool (device, "ChorusOn", false);
    p[7] = linValue (manual (device, "SizeModFreq", 0.02), 0.05, 5.0);     // Hz [DATA: 0.01 … 8]
    p[8] = chorus ? linValue (manual (device, "SizeModDepth", 0.02), 0.0, 4.0) : 0.0f; // [DATA: 0.01 … 4]
    // the diffusion network's shelves → the decay multipliers (Bass mult 0.5 … 2 X, High mult 0.1 … 1 X)
    const auto lowOn = manualBool (device, "ShelfLowOn", false), highOn = manualBool (device, "ShelfHighOn", false);
    p[9] = linValue (lowOn ? manual (device, "ShelfLoGain", 0.75) : 1.0, 0.5, 2.0);    // [DATA: 0.2 … 1]
    p[10] = linValue (manual (device, "ShelfLoFreq", 90.0), 100.0, 10000.0);
    p[11] = linValue (highOn ? reverbAtriumHighMult (manual (device, "ShelfHiGain", 0.7), decaySec) : 1.0, 0.1, 1.0); // [DATA: 0.2 … 1]
    p[12] = linValue (manual (device, "ShelfHiFreq", 4500.0), 100.0, 15000.0);
    // Spin → the early reflections' modulation
    const auto spin = manualBool (device, "SpinOn", false);
    p[15] = linValue (manual (device, "EarlyReflectModFreq", 0.3), 0.05, 5.0);         // Hz [DATA: 0.074 … 1.3]
    p[16] = spin ? linValue (manual (device, "EarlyReflectModDepth", 17.5), 0.0, 55.0) : 0.0f; // [DATA: 2 … 55]
    p[18] = linValue (manual (device, "AllPassGain", 0.6), 0.0, 0.96);                  // Density [DATA: 0.001 … 0.96]
    if (manualBool (device, "FreezeOn", false))
        notes.dropped.push_back ("Freeze");
    if (std::abs (manual (device, "StereoSeparation", 100.0) - 100.0) > 0.5)
        notes.dropped.push_back (text ("Stereo %.0f", manual (device, "StereoSeparation", 100.0)));
    if (std::abs (manual (device, "DiffuseDelay", 0.5) - 0.5) > 0.01)
        notes.dropped.push_back ("Shape");
    if (std::abs (manual (device, "AllPassSize", 0.4) - 0.4) > 0.01)
        notes.dropped.push_back ("Scale");
    if ((int) manual (device, "HighFilterType", 0) != 0)
        notes.approximated.push_back ("high filter as a low pass (Atrium damps with a decay multiplier)");
    // [MEASURED 190cz r00–r03: Live's tail energy grows with Decay, −16.7 / −11.5 / −8.3 dB at 1 / 2.5 / 5 s (fully
    // wet); Atrium's stays at −3.7 dB; Atrium has no wet level of its own]
    if (manual (device, "MixDirect", 0.55) > 0.0)
        notes.approximated.push_back ("wet level (Atrium's fully wet tail is louder than Live's: measured +13 / +8 / +4.5 dB at Decay 1 / 2.5 / 5 s)");
    return true;
}

double reverbAtriumDecaySec (double liveDecaySec) noexcept { return 0.94 * liveDecaySec; }

double reverbAtriumHighMult (double shelfHiGain, double liveDecaySec) noexcept
{
    const auto lossDb = -20.0 * std::log10 (std::clamp (shelfHiGain, 0.01, 1.0));
    return std::clamp (1.0 / (1.0 + 0.08 * lossDb * std::max (liveDecaySec, 0.0)), 0.1, 1.0);
}

// ================================================================================================ entry point

std::optional<Translated> effectFromLiveDevice (const Element& device, const Context& context)
{
    const auto tag = device.name();
    Translated t;
    bool ok = false;
    // (190cf) a Live parameter that drives a LYNX parameter by the law the translator used (automation, rack macros)
    const auto link = [&t] (const char* live, int param, std::function<float (double)> law) { t.links.push_back ({ live, param, std::move (law) }); };
    if (tag == "Eq8")
    {
        t.from = "EQ Eight";
        t.to = "Facet EQ";
        ok = eqEightToFacet (device, t.slot, t.notes);
        // in Stereo mode band k is Facet's band k: its frequency, gain and Q follow (L/R and M/S pack the bands)
        if (ok && std::clamp ((int) device.number ("Mode", 0), 0, 2) == 0)
        {
            const auto scale = std::clamp (manual (device, "Scale", 1.0), -2.0, 2.0);
            const auto sign = scale < 0.0 ? -1.0 : 1.0;
            const auto adaptive = manualBool (device, "AdaptiveQ", false);
            const auto factor = device.number ("AdaptiveQFactor", 1.12);
            const auto shelvesToo = device.boolean ("AdaptiveQAffectsShelves", true);
            for (int b = 0; b < facet::bands; ++b)
            {
                const auto stem = "Bands." + std::to_string (b) + "/ParameterA/";
                const auto mode = (Eq8Mode) std::clamp ((int) manual (device, stem + "Mode", 3), 0, 7);
                t.links.push_back ({ stem + "Freq", facet::param (b, 1), [] (double hz) { return facet::hzValue (hz); } });
                if (mode == Eq8Mode::bell || mode == Eq8Mode::lowShelf || mode == Eq8Mode::highShelf)
                    t.links.push_back ({ stem + "Gain", facet::param (b, 2), [sign] (double db) { return facet::gainValue (sign * db); } });
                // (190cx, 190da) a band under Adaptive Q keeps the factor of its stored gain (a moving gain leaves Q there)
                const auto shelf = mode == Eq8Mode::lowShelf || mode == Eq8Mode::highShelf;
                const auto narrow = adaptive && (mode == Eq8Mode::bell || (shelf && shelvesToo))
                                        ? eq8AdaptiveQ (1.0, manual (device, stem + "Gain", 0.0) * std::abs (scale), factor) : 1.0;
                t.links.push_back ({ stem + "Q", facet::param (b, 3), [mode, narrow] (double q) { return facet::qValue (facetQ (mode, q, narrow)); } });
            }
            link ("GlobalGain", facet::outputParam, [] (double db) { return linValue (db, -24.0, 24.0); });
        }
    }
    else if (tag == "Compressor2")
    {
        t.from = "Compressor";
        t.approximate = true;
        if ((int) manual (device, "Model", 0) == 2)
        {
            t.to = "Sentry Gate";
            ok = expanderToSentry (device, t);
        }
        else
        {
            using namespace dyn::crest;
            t.to = "Crest Compressor";
            ok = compressorToCrest (device, t.slot, t.notes, t.side);
            t.sideParam = scExternal;
            link ("Threshold", threshold, [] (double gain) { return linValue (dbOfGain (gain), -60.0, 0.0); });
            link ("Ratio", ratio, [] (double r) { return r >= 1.0e6 ? 1.0f : ratioValue (std::min (r, 100.0)); });
            link ("Attack", attack, [] (double ms) { return attackValue (ms); });
            link ("Release", release, [] (double ms) { return releaseValue (ms); });
            link ("Knee", knee, [] (double db) { return linValue (db, 0.0, 72.0); });
            link ("Gain", output, [] (double db) { return linValue (db, -36.0, 36.0); });
            link ("DryWet", mix, [] (double v) { return unit (v); });
        }
    }
    else if (tag == "GlueCompressor")
    {
        using namespace dyn::crest;
        t.from = "Glue Compressor";
        t.to = "Crest Compressor";
        t.approximate = true;
        ok = glueToCrest (device, t.slot, t.notes, t.side);
        t.sideParam = scExternal;
        link ("Threshold", threshold, [] (double db) { return linValue (db, -60.0, 0.0); });
        link ("Makeup", output, [] (double db) { return linValue (db, -36.0, 36.0); });
        link ("Range", range, [] (double db) { return linValue (glueRangeDb (db), 0.0, 60.0); });
        link ("DryWet", mix, [] (double v) { return unit (v); });
    }
    else if (tag == "StereoGain")
    {
        t.from = "Utility";
        t.to = "Utility";
        ok = utilityToUtility (device, t.slot, t.notes);
        if (ok && ! device.boolean ("LegacyMode", false))
            link ("Gain", 0, [] (double gain) { return gain <= 0.0 ? 0.0f : std::max (1.0e-4f, linValue (dbOfGain (gain), -24.0, 24.0)); });
        // (190cx) no Balance link: Live's balance (constant power with a boost) sits in the Utility's matrix, which no
        // single LYNX control moves — its automation / macros are listed as not carried instead of half-applied
    }
    else if (tag == "Reverb")
    {
        t.from = "Reverb";
        t.to = "Atrium";
        t.approximate = true;
        ok = reverbToAtrium (device, t.slot, t.notes);
        link ("MixDirect", 0, [] (double v) { return unit (v); });
        link ("PreDelay", 1, [] (double ms) { return linValue (ms, 0.0, 500.0); });
        link ("DecayTime", 2, [] (double ms) { return verbs::atrium::decayValue (reverbAtriumDecaySec (ms / 1000.0)); });
        link ("RoomSize", 5, [] (double size) { return atriumSizeOf (size); });
    }
    // ---- lot 2 (round 190cf) ----
    else if (tag == "Delay")
    {
        t.from = "Delay";
        t.to = "Echoform";
        t.approximate = true;
        ok = delayToEchoform (device, context, t);
    }
    else if (tag == "Echo")
    {
        t.from = "Echo";
        t.to = "Echoform";
        t.approximate = true;
        ok = echoToEchoform (device, context, t);
    }
    else if (tag == "MultibandDynamics")
    {
        t.from = "Multiband Dynamics";
        t.to = "Prism Multiband";
        t.approximate = true;
        ok = multibandToPrism (device, t);
    }
    else if (tag == "AutoFilter" || tag == "AutoFilter2")
    {
        t.from = "Auto Filter";
        t.to = "Filter";
        t.approximate = true;
        ok = autoFilterToFilter (device, t);
        if (ok && t.slot.kind == EffectKind::facetEq)
        {
            // (190fm) the notch: the modulation is worked out in the Filter's cutoff units, then moved onto the band's
            // frequency (Facet's own scale)
            t.to = "Facet EQ (notch)";
            Translated f;
            f.slot = makeEffectSlot (EffectKind::filter);
            f.slot.params[1] = filterCutoffValue (facet::hz (t.slot.params[(std::size_t) facet::param (0, 1)]));
            autoFilterModulation (device, context, f);
            if (f.modulator)
            {
                for (auto& v : f.modulator->curve)
                    v = facetHzOfFilterCutoff (v);
                f.modulator->param = facet::param (0, 1);
                t.modulator = std::move (f.modulator);
            }
        }
        else if (ok)
            autoFilterModulation (device, context, t); // (190ed)
        if (ok)
            autoFilterDrive (device, t); // (190fm) Drive: an Ember stage in front, in a rack
    }
    else if (tag == "Saturator")
    {
        t.from = "Saturator";
        t.to = "Ember Saturator";
        t.approximate = true;
        ok = saturatorToEmber (device, t);
    }
    else if (tag == "Overdrive")
    {
        t.from = "Overdrive";
        t.to = "Distortion";
        t.approximate = true;
        ok = overdriveToDistortion (device, t);
    }
    else if (tag == "Limiter")
    {
        t.from = "Limiter";
        t.to = "Apex Limiter";
        t.approximate = true;
        ok = limiterToApex (device, t);
    }
    else if (tag == "Gate")
    {
        t.from = "Gate";
        t.to = "Sentry Gate";
        t.approximate = true;
        ok = gateToSentry (device, t);
    }
    else if (tag == "ChannelEq")
    {
        t.from = "Channel EQ";
        t.to = "EQ";
        t.approximate = true;
        ok = channelEqToEq (device, t);
    }
    else if (tag == "Chorus2" || tag == "Chorus")
    {
        t.from = tag == "Chorus2" ? "Chorus-Ensemble" : "Chorus";
        t.to = "Chorus";
        t.approximate = true;
        ok = chorusToChorus (device, t);
    }
    else if (tag == "AutoPan")
    {
        t.from = "Auto Pan";
        t.to = "Auto-pan";
        t.approximate = true;
        ok = autoPanToAutoPan (device, context, t);
    }
    else if (tag == "Redux2")
    {
        t.from = "Redux";
        t.to = "Lo-fi";
        t.approximate = true;
        ok = reduxToLofi (device, t);
    }
    // ---- lot 3 (round 190ed) ----
    else if (tag == "FilterEQ3")
    {
        t.from = "EQ Three";
        t.to = "Prism Multiband";
        t.approximate = true;
        ok = eqThreeToPrism (device, t);
    }
    else if (tag == "DrumBuss")
    {
        t.from = "Drum Buss";
        if (context.drumBussRack) // round 190ek's rack: the A/B harness and its tests
        {
            t.to = "Drum Buss rack (Ember Saturator, Facet EQ, Crest Compressor, Shell Channel)"; // (round 190ek: Crest only with Comp on)
            t.approximate = true;
            ok = drumBussToRack (device, t);
        }
        else // round 190fk: LYNX's own drum-bus engine
        {
            t.to = "Anvil Bus";
            t.approximate = true;
            ok = drumBussToEngine (device, t);
        }
    }
    // ---- lot 4 (round 190fm) ----
    else if (tag == "Hybrid")
    {
        t.from = "Hybrid Reverb";
        t.to = "Hybrid Reverb rack (Atrium / Halo, Convolver, Facet EQ, Utility)";
        t.approximate = true;
        ok = hybridToRack (device, context, t);
    }
    else if (tag == "Roar")
    {
        t.from = "Roar";
        t.to = "Roar rack (Filter / Facet EQ, Ember Saturator / Lo-fi, Crest Compressor, Utility)";
        t.approximate = true;
        ok = roarToRack (device, t);
    }
    else if (tag == "GrainDelay")
    {
        t.from = "Grain Delay";
        t.to = "Echoform (Pitch mode)";
        t.approximate = true;
        ok = grainDelayToEchoform (device, context, t);
    }
    if (! ok)
        return std::nullopt;
    t.slot.enabled = device.boolean ("On/Manual", true);
    return t;
}

// ================================================================================================ Simpler

int simplerVoices (int index) noexcept
{
    static constexpr int voices[15] { 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 32 };
    return voices[std::clamp (index, 0, 14)];
}

SimplerRegion simplerRegion (double sampleStart, double sampleEnd, double start, double length, double loopLength) noexcept
{
    SimplerRegion r;
    const auto span = std::max (0.0, sampleEnd - sampleStart);
    r.start = sampleStart + std::clamp (start, 0.0, 1.0) * span;
    r.end = r.start + std::clamp (length, 0.0, 1.0) * (sampleEnd - r.start);
    r.loopStart = r.end - std::clamp (loopLength, 0.0, 1.0) * (r.end - r.start);
    return r;
}

SimplerResult applySimpler (const Element& device, ChannelDef& channel, double bpm, double fileSeconds, Notes& notes)
{
    SimplerResult result;
    auto& s = channel.sampler;
    const auto part = device.path ("Player/MultiSampleMap/SampleParts").child ("MultiSamplePart");
    const auto mode = (int) device.number ("Globals/PlaybackMode", 0); // 0 Classic, 1 One-Shot, 2 Slice [DATA + DOC]
    const auto frames = part.number ("SampleRef/DefaultDuration", 0), rate = part.number ("SampleRef/DefaultSampleRate", 0);
    const auto seconds = frames > 0 && rate > 0 ? frames / rate : fileSeconds;
    const auto singleSample = channel.zones.empty();

    // Transpose (semitones) and Detune (cents), on top of the root key the zone import applied
    const auto transpose = manual (device, "Pitch/TransposeKey", 0.0) + manual (device, "Pitch/TransposeFine", 0.0) / 100.0;
    if (transpose != 0.0)
    {
        const auto wanted = (double) s.pitch + transpose;
        s.pitch = (float) std::clamp (wanted, -24.0, 24.0);
        if (std::abs (wanted) > 24.0)
            notes.approximated.push_back (text ("transpose %.0f st (Sampler: -24 … +24)", wanted));
    }
    // Volume (dB) [DATA: −36 … 36, −12 in a new Simpler] on the Sampler's Gain (−24 … +24 dB), Pan (−1 … 1)
    if (has (device, "VolumeAndPan/Volume"))
    {
        const auto wanted = (double) s.gainDb + manual (device, "VolumeAndPan/Volume", 0.0);
        s.gainDb = (float) std::clamp (wanted, -24.0, 24.0);
        if (std::abs (wanted) > 24.0)
            notes.approximated.push_back (text ("volume %.1f dB (Sampler gain: -24 … +24 dB)", wanted));
    }
    channel.pan = (float) std::clamp ((double) channel.pan + manual (device, "VolumeAndPan/Panorama", 0.0), -1.0, 1.0);
    s.reverse = manualBool (device, "Player/Reverse", false);

    // Filter [DATA: Type 0 … 4; DOC: low pass, high pass, band pass, notch, morph] → the channel filter (12 dB/oct)
    const auto filter = device.path ("Filter/Slot/Value/SimplerFilter");
    if (manualBool (device, "Filter/IsOn", false) && filter)
    {
        const auto type = (int) manual (filter, "Type", 0);
        static constexpr int types[5] { 0, 2, 1, 3, 0 };
        s.filterType = types[std::clamp (type, 0, 4)];
        s.cutoff = (float) std::clamp (manual (filter, "Freq", 22000.0), 20.0, 20000.0);
        s.resonance = (float) std::clamp (manual (filter, "Res", 0.0), 0.0, 1.0); // [DATA: 0 … 1.25]
        const auto active = s.filterType != 0 || s.cutoff < 19999.0f || s.resonance > 0.0f;
        if (active)
            notes.approximated.push_back ("filter (12 dB/oct, no circuit type, drive or key tracking)");
        if (type == 4)
            notes.approximated.push_back ("Morph filter as a low pass");
        // the filter envelope: Amount in semitones [DATA: ±72] → octaves
        if (manualBool (filter, "Envelope/IsOn", false) && manual (filter, "Envelope/Amount", 0.0) != 0.0)
        {
            const auto env = filter.child ("Envelope");
            s.filterEnv.amount = (float) (manual (env, "Amount", 0.0) / 12.0);
            s.filterEnv.attack = (float) (manual (env, "AttackTime", 0.1) / 1000.0);
            s.filterEnv.decay = (float) (manual (env, "DecayTime", 600.0) / 1000.0);
            s.filterEnv.sustain = (float) std::clamp (manual (env, "SustainLevel", 0.0), 0.0, 1.0);
            s.filterEnv.release = (float) (manual (env, "ReleaseTime", 50.0) / 1000.0);
            notes.approximated.push_back ("filter envelope (straight segments)");
        }
    }
    // Pitch envelope: Amount in semitones [DATA: ±48]
    if (const auto env = device.path ("Pitch/Envelope/Slot/Value/SimplerPitchEnvelope");
        manualBool (device, "Pitch/Envelope/IsOn", false) && env && manual (env, "Amount", 0.0) != 0.0)
    {
        s.pitchEnv.amount = (float) manual (env, "Amount", 0.0);
        s.pitchEnv.attack = (float) (manual (env, "AttackTime", 0.1) / 1000.0);
        s.pitchEnv.decay = (float) (manual (env, "DecayTime", 600.0) / 1000.0);
        s.pitchEnv.sustain = (float) std::clamp (manual (env, "SustainLevel", 0.0), 0.0, 1.0);
        s.pitchEnv.release = (float) (manual (env, "ReleaseTime", 50.0) / 1000.0);
        notes.approximated.push_back ("pitch envelope (straight segments)");
    }
    // LFO [DATA: Type 0 … 5; DOC: sine, square, triangle, saw down, saw up, random] → the channel LFOs (sine, triangle,
    // pulse), one per destination with the device's amount
    if (const auto lfo = device.path ("Lfo/Slot/Value/SimplerLfo"); manualBool (device, "Lfo/IsOn", false) && lfo)
    {
        const auto volume = manual (device, "VolumeAndPan/VolumeLfoAmount", 0.0), pan = manual (device, "VolumeAndPan/PanoramaLfoAmount", 0.0);
        const auto cutoff = filter ? manual (filter, "ModByLfo", 0.0) : 0.0, pitch = manual (device, "Pitch/PitchLfoAmount", 0.0);
        if (volume != 0.0 || pan != 0.0 || cutoff != 0.0 || pitch != 0.0)
        {
            ChannelLfo l;
            const auto type = (int) manual (lfo, "Type", 0);
            static constexpr int shapes[6] { 0, 2, 1, 1, 1, 2 };
            l.shape = shapes[std::clamp (type, 0, 5)];
            l.period = (float) (1.0 / std::max (0.01, manual (lfo, "Frequency", 1.0)));
            l.attack = (float) (manual (lfo, "Attack", 0.1) / 1000.0);
            l.global = ! manualBool (lfo, "Retrigger", true);
            if (type >= 3)
                notes.approximated.push_back ("LFO shape (saw / random as triangle / pulse)");
            if ((int) manual (lfo, "RateType", 0) != 0)
                notes.approximated.push_back ("LFO synced rate (plays at its rate in Hz)");
            auto set = [&] (int target, double amount) {
                if (amount == 0.0)
                    return;
                s.lfo[(std::size_t) target] = l;
                s.lfo[(std::size_t) target].amount = (float) amount;
            };
            set (lfoVolume, std::clamp (volume, 0.0, 1.0));
            set (lfoPan, std::clamp (pan, -1.0, 1.0));
            set (lfoCutoff, cutoff / 12.0); // semitones [DATA: 0 … 24] → octaves
            if (pitch != 0.0)
                notes.dropped.push_back ("LFO to pitch");
        }
    }

    // Voices and Retrigger: one voice = each note cuts the one before
    const auto voices = simplerVoices ((int) device.number ("Globals/NumVoices", 5));
    channel.maxPolyphony = voices;
    if (voices == 1)
        s.cut = true;
    // Glide [DATA: PortamentoMode 0 … 2; DOC: off, portamento, glide], its time in ms → beats at the set's tempo
    if ((int) manual (device, "Globals/PortamentoMode", 0) != 0)
    {
        s.porta = true;
        s.portaBeats = (float) std::clamp (manual (device, "Globals/PortamentoTime", 50.0) / 1000.0 * bpm / 60.0, 0.001, 16.0);
        notes.approximated.push_back ("glide time (in beats at the set's tempo)");
    }
    if (manual (device, "Globals/SpreadAmount", 0.0) != 0.0)
        notes.dropped.push_back ("Spread");
    if (manual (device, "VolumeAndPan/PanoramaRnd", 0.0) != 0.0)
        notes.dropped.push_back ("random pan");
    // (velocity > volume is LYNX's own fixed law: listed once per device)
    notes.approximated.push_back ("velocity sensitivity (LYNX's own velocity law)");

    // The part's region, as fractions of the file (a single sample only: zones play whole files)
    const auto partStart = part.number ("SampleStart", 0), partEnd = part.number ("SampleEnd", frames);
    const auto region = simplerRegion (partStart, partEnd > partStart ? partEnd : frames,
                                       mode == 0 ? manual (device, "Player/LoopModulators/SampleStart", 0.0) : 0.0,
                                       mode == 0 ? manual (device, "Player/LoopModulators/SampleLength", 1.0) : 1.0,
                                       manual (device, "Player/LoopModulators/LoopLength", 1.0));
    if (frames > 0 && singleSample)
    {
        s.regionStart = (float) std::clamp (region.start / frames, 0.0, 1.0);
        s.regionEnd = (float) std::clamp (region.end / frames, (double) s.regionStart, 1.0);
        if (frames - region.end < 2.0)
            s.regionEnd = 1.0f;
    }
    else if (frames > 0 && (region.start > 1.0 || frames - region.end > 1.0))
        notes.dropped.push_back ("sample start / end (zones play the whole file)");

    if (mode == 0)
    {
        // Classic: Loop on = the end of the played region loops while the note is held
        if (manualBool (device, "Player/LoopModulators/LoopOn", false) && region.end > region.loopStart + 1.0)
        {
            result.loop = true;
            result.loopStart = (std::int64_t) std::llround (region.loopStart);
            result.loopEnd = (std::int64_t) std::llround (region.end);
            if (manual (device, "Player/LoopModulators/LoopFade", 0.0) > 0.0)
                notes.dropped.push_back ("loop crossfade");
        }
    }
    else if (mode == 1)
    {
        // One-Shot [DOC]: Trigger plays the whole region, Gate stops at note off with the fade out; no ADSR
        const auto gate = (int) manual (device, "VolumeAndPan/OneShotEnvelope/SustainMode", 0) == 1;
        const auto fadeOut = manual (device, "VolumeAndPan/OneShotEnvelope/FadeOutTime", 0.1) / 1000.0;
        s.attack = (float) std::clamp (manual (device, "VolumeAndPan/OneShotEnvelope/FadeInTime", 0.0) / 1000.0, 0.0, 2.0);
        s.decay = 0.0f;
        s.sustain = 1.0f;
        s.release = gate ? (float) std::clamp (fadeOut, 0.001, 4.0) : 0.0f;
        if (! gate && fadeOut > 0.01)
            notes.dropped.push_back ("one-shot fade out");
    }
    else if (seconds > 0.0 && singleSample)
    {
        // Slice [DATA: SlicingStyle 0 … 3; DOC: transient, beat, region, manual]: each slice on its own key from C1
        const auto style = (int) part.number ("SlicingStyle", 0);
        const auto from = frames > 0 ? region.start / frames : 0.0, to = frames > 0 ? region.end / frames : 1.0;
        std::vector<double> starts;
        const auto take = [&] (const char* list) {
            for (const auto& pt : part.path (list).children ("SlicePoint"))
                if (pt.hasAttr ("TimeInSeconds"))
                    starts.push_back (std::strtod (pt.attr ("TimeInSeconds").c_str(), nullptr) / seconds);
        };
        if (style == 2)
        {
            const auto n = std::clamp ((int) part.number ("SlicingRegions", 8), 1, 128);
            for (int k = 0; k < n; ++k)
                starts.push_back (from + (to - from) * k / n);
        }
        else
        {
            take (style == 3 ? "ManualSlicePoints" : style == 1 ? "BeatSlicePoints" : "SlicePoints");
            if (starts.empty() && style != 0)
                take ("SlicePoints");
        }
        std::sort (starts.begin(), starts.end());
        std::vector<float> slices;
        for (const auto v : starts)
            if (v >= from - 1.0e-9 && v < to && (slices.empty() || v > (double) slices.back() + 1.0e-6))
                slices.push_back ((float) v);
        // the audio before the first slice point is a slice of its own when it is long enough to be heard
        if (slices.empty() || ((double) slices.front() - from) * seconds > 0.05)
            slices.insert (slices.begin(), (float) from);
        s.slices = std::move (slices);
        s.release = 0.0f;
        result.keyShift = 60 - 36;
        notes.approximated.push_back (style == 0 && part.number ("SlicingThreshold", 100) < 100 ? "Slice mode (every stored transient; sensitivity not applied)"
                                                                                                 : "Slice mode (slices from the stored points)");
    }
    else
        notes.dropped.push_back (singleSample ? "Slice mode (the sample's length is not known)" : "Slice mode (more than one sample)");

    // Warp [DATA: SampleWarpProperties IsWarped, WarpMode, WarpMarkers]: the sample follows the set's tempo — the
    // whole file lasts its length in beats (one tempo: the first to the last marker)
    if (const auto warp = part.child ("SampleWarpProperties"); warp && warp.boolean ("IsWarped", false) && seconds > 0.0 && singleSample && mode != 2)
    {
        double s0 = 0, b0 = 0, s1 = 0, b1 = 0;
        bool first = true;
        for (const auto& m : warp.path ("WarpMarkers").children ("WarpMarker"))
        {
            const auto sec = std::strtod (m.attr ("SecTime").c_str(), nullptr), beat = std::strtod (m.attr ("BeatTime").c_str(), nullptr);
            if (first || sec < s0) { s0 = sec; b0 = beat; }
            if (first || sec > s1) { s1 = sec; b1 = beat; }
            first = false;
        }
        if (! first && s1 > s0 + 1.0e-6 && b1 > b0)
        {
            const auto warpMode = (int) warp.number ("WarpMode", 0);
            s.stretchBeats = (float) (seconds * (b1 - b0) / (s1 - s0));
            s.keepPitch = warpMode != 3;
            s.stretchMethod = warpMode == 0 ? StretchMethod::beats : warpMode == 1 ? StretchMethod::tones
                              : warpMode == 2 ? StretchMethod::texture : StretchMethod::complex;
            notes.approximated.push_back ("Warp (the whole sample at one tempo)");
        }
    }
    return result;
}
} // namespace lynx::alsdev
