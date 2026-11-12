// --- Live's own devices in a set → LYNX's own (round 190bq, core/AlsDevices.h): the unit laws of each translation
// (frequency, Q, gain, ratio, times, dry/wet) against the values the device stores, and the import report. The device
// elements are written by the tests with the member names Live saves; no file of Live's is used. What these tests
// prove is each conversion as stated in AlsDevices.h; which statements about Live are [ASSUMED] is listed in the
// round's handoff for an A/B in Live.

#include <complex>

#include "core/AlsDevices.h"
#include "core/AlsImport.h"
#include "core/DynamicsSuite.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/LynxVerbs.h"
#include "core/XmlLite.h"

namespace als190bq
{
using als175a::v;

inline std::string num (double x)
{
    char text[64];
    std::snprintf (text, sizeof (text), "%.9g", x);
    return text;
}
// a device parameter as Live saves it
inline std::string par (const std::string& name, const std::string& value) { return "<" + name + "><Manual Value=\"" + value + "\" /></" + name + ">"; }
inline std::string par (const std::string& name, double value) { return par (name, num (value)); }
inline std::string flag (const std::string& name, bool on) { return par (name, std::string (on ? "true" : "false")); }

inline std::string eqBand (int b, bool on, int mode, double hz, double gain, double q, bool onB = false, int modeB = 3, double hzB = 1000, double gainB = 0,
                           double qB = 0.7071)
{
    const auto curve = [] (const char* name, bool isOn, int m, double f, double g, double qq) {
        return std::string ("<") + name + ">" + flag ("IsOn", isOn) + par ("Mode", m) + par ("Freq", f) + par ("Gain", g) + par ("Q", qq) + "</" + name + ">";
    };
    return "<Bands." + std::to_string (b) + ">" + curve ("ParameterA", on, mode, hz, gain, q) + curve ("ParameterB", onB, modeB, hzB, gainB, qB) +
           "</Bands." + std::to_string (b) + ">";
}

inline lynx::xml::Element parse (lynx::xml::Document& doc, const std::string& text)
{
    doc.parse (text);
    return doc.root();
}

// dB of an analog transfer function b(s) / a(s) (coefficients of s², s, 1) at f / f0
inline double analogDb (double b2, double b1, double b0, double a2, double a1, double a0, double ratio)
{
    const std::complex<double> s (0.0, ratio);
    return 20.0 * std::log10 (std::abs ((b2 * s * s + b1 * s + b0) / (a2 * s * s + a1 * s + a0)));
}

// LYNX's Utility laws (core/Effects.cpp): gain ±24 dB, matrix coefficients −2 … 2
inline float utilDb (float v) { return (v - 0.5f) * 48.0f; }
inline float utilCoef (float v) { return v * 4.0f - 2.0f; }

inline bool hasNote (const std::vector<std::string>& notes, const std::string& part)
{
    for (const auto& n : notes)
        if (n.find (part) != std::string::npos)
            return true;
    return false;
}
inline bool reportHas (const std::map<std::string, int>& list, const std::string& part)
{
    for (const auto& [key, count] : list)
        if (key.find (part) != std::string::npos)
            return true;
    return false;
}

inline std::string simpler (const std::string& globals, const std::string& extra, const std::string& partExtra = {}, const std::string& player = {})
{
    return "<OriginalSimpler Id=\"0\"><On><Manual Value=\"true\" /></On>" + v ("UserName", "") + "<Player><MultiSampleMap><SampleParts>"
           "<MultiSamplePart Id=\"0\">" + v ("RootKey", "60") + v ("Detune", "0") + v ("Volume", "1") + v ("SampleStart", "0") + v ("SampleEnd", "88199") +
           "<KeyRange>" + v ("Min", "0") + v ("Max", "127") + "</KeyRange><VelocityRange>" + v ("Min", "1") + v ("Max", "127") + "</VelocityRange>" +
           "<SustainLoop>" + v ("Start", "0") + v ("End", "88199") + v ("Mode", "1") + "</SustainLoop>" +
           "<SampleRef><FileRef>" + v ("RelativePathType", "1") + v ("RelativePath", "Samples/Imported/vocal.wav") + v ("Path", "D:/old/vocal.wav") +
           "</FileRef>" + v ("DefaultDuration", "88200") + v ("DefaultSampleRate", "44100") + "</SampleRef>" + partExtra +
           "</MultiSamplePart></SampleParts></MultiSampleMap>" + player + "</Player>" + extra + "<Globals>" + globals + "</Globals></OriginalSimpler>";
}

// one MIDI track (Id 10) with `devices`, one audio track (Id 20) with `audioDevices`
inline std::string deviceSet (const std::string& devices, const std::string& audioDevices = {}, const std::string& clipNotes = {})
{
    using als175a::mixer;
    using als175a::name;
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12117\" Creator=\"test\">\n<LiveSet><Tracks>";
    s += "<MidiTrack Id=\"10\">" + name ("Keys") + v ("Color", "3") + v ("TrackGroupId", "-1") + "<DeviceChain>" + mixer (100, 1.0, 0.0, true) +
         "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting><MainSequencer><ClipTimeable><ArrangerAutomation><Events>" +
         (clipNotes.empty() ? std::string() :
              "<MidiClip Id=\"0\" Time=\"0\">" + v ("CurrentStart", "0") + v ("CurrentEnd", "4") + "<Loop>" + v ("LoopStart", "0") + v ("LoopEnd", "4") +
              v ("StartRelative", "0") + v ("LoopOn", "false") + v ("OutMarker", "4") + "</Loop><Notes><KeyTracks>" + clipNotes + "</KeyTracks></Notes></MidiClip>") +
         "</Events></ArrangerAutomation></ClipTimeable></MainSequencer><DeviceChain><Devices>" + devices + "</Devices></DeviceChain></DeviceChain></MidiTrack>";
    s += "<AudioTrack Id=\"20\">" + name ("Bus") + v ("Color", "20") + v ("TrackGroupId", "-1") + "<DeviceChain>" + mixer (200, 1.0, 0.0, true) +
         "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting><DeviceChain><Devices>" + audioDevices +
         "</Devices></DeviceChain></DeviceChain></AudioTrack>";
    s += "</Tracks><MainTrack><DeviceChain>" + mixer (900, 1.0, 0.0, true, {}, "<Tempo><Manual Value=\"120\" /></Tempo><TimeSignature><Manual Value=\"201\" /></TimeSignature>") +
         "</DeviceChain></MainTrack></LiveSet>\n</Ableton>\n";
    return s;
}
} // namespace als190bq

TEST (als190bq_eq_eight_q_laws_match_the_analog_prototypes)
{
    using namespace lynx;
    using alsdev::Eq8Mode;
    // the Q conversions: bell and notch √2·Q, 12 dB cuts Q / 0.7071, shelves (Q / 0.7071)^(1 / 0.4697), 48 dB cuts √2·Q⁴
    CHECK (std::abs (alsdev::facetQ (Eq8Mode::bell, 0.7071) - 1.0) < 1e-3);
    CHECK (std::abs (alsdev::facetQ (Eq8Mode::notch, 2.0) - 2.8284) < 1e-3);
    CHECK (std::abs (alsdev::facetQ (Eq8Mode::lowCut12, 0.7071) - 1.0) < 1e-3 && std::abs (alsdev::facetQ (Eq8Mode::highCut12, 2.0) - 2.8284) < 1e-3);
    CHECK (std::abs (alsdev::facetQ (Eq8Mode::lowShelf, 0.7071) - 1.0) < 1e-3 && std::abs (alsdev::facetQ (Eq8Mode::highShelf, 1.0) - std::pow (1.41421356, 1.0 / 0.4697)) < 1e-3);
    CHECK (std::abs (alsdev::facetQ (Eq8Mode::lowCut48, 0.7071) - 1.41421356 * 0.25) < 1e-3);
    CHECK (alsdev::facetQ (Eq8Mode::bell, 18.0) < 40.0 && alsdev::facetQ (Eq8Mode::lowCut48, 18.0) == 40.0 && alsdev::facetQ (Eq8Mode::bell, 0.0) >= 0.025);

    auto params = makeEffectSlot (EffectKind::facetEq).params;
    const auto band = [&params] (Eq8Mode mode, double hz, double gain, double q) {
        alsdev::Eq8Band b;
        b.mode = mode;
        b.hz = hz;
        b.gainDb = gain;
        b.q = q;
        for (int k = 0; k < facet::bands; ++k)
            params[(std::size_t) facet::param (k, 0)] = facet::shapeValue (facet::Shape::off);
        alsdev::setFacetBand (params, 0, b, 1.0, (int) facet::Placement::stereo);
    };
    const double ratios[] = { 0.125, 0.25, 0.5, 0.8, 1.0, 1.25, 2.0, 4.0, 8.0 };
    double worst = 0.0;

    // Bell: (s² + s·A/Q + 1) / (s² + s/(A·Q) + 1), A = 10^(gain/40), Q = the stored Q
    for (const auto& [hz, gain, q] : { std::array<double, 3> { 1000.0, 6.0, 2.0 }, { 200.0, -9.0, 0.7071 }, { 5000.0, 12.0, 4.5 }, { 80.0, 3.0, 0.3 } })
    {
        band (Eq8Mode::bell, hz, gain, q);
        const auto a = std::pow (10.0, gain / 40.0);
        for (const auto r : ratios)
            worst = std::max (worst, std::abs (facet::analogDb (params, 0, hz * r) - als190bq::analogDb (1, a / q, 1, 1, 1 / (a * q), 1, r)));
    }
    std::printf ("  bell: worst %.4f dB\n", worst);
    CHECK (worst < 0.02);

    // Low shelf (cookbook): A·(s² + (√A/Q)s + A) / (A·s² + (√A/Q)s + 1); high shelf: A·(A·s² + (√A/Q)s + 1) / (s² + (√A/Q)s + A)
    worst = 0.0;
    for (const auto& [hz, gain, q] : { std::array<double, 3> { 100.0, 6.0, 0.7071 }, { 300.0, -8.0, 1.0 }, { 150.0, 4.0, 0.5 } })
    {
        const auto a = std::pow (10.0, gain / 40.0), m = std::sqrt (a) / q;
        band (Eq8Mode::lowShelf, hz, gain, q);
        for (const auto r : ratios)
            worst = std::max (worst, std::abs (facet::analogDb (params, 0, hz * r) - als190bq::analogDb (a, a * m, a * a, a, m, 1, r)));
        band (Eq8Mode::highShelf, hz * 20.0, gain, q);
        for (const auto r : ratios)
            worst = std::max (worst, std::abs (facet::analogDb (params, 0, hz * 20.0 * r) - als190bq::analogDb (a * a, a * m, a, 1, m, a, r)));
    }
    std::printf ("  shelves: worst %.4f dB\n", worst);
    CHECK (worst < 0.05);

    // 12 dB cuts: s² / (s² + s/Q + 1) and 1 / (s² + s/Q + 1); notch: (s² + 1) / (s² + s/Q + 1)
    worst = 0.0;
    for (const auto q : { 0.7071, 0.5, 2.0, 5.0 })
    {
        band (Eq8Mode::lowCut12, 120.0, 0.0, q);
        for (const auto r : ratios)
            worst = std::max (worst, std::abs (facet::analogDb (params, 0, 120.0 * r) - als190bq::analogDb (1, 0, 0, 1, 1 / q, 1, r)));
        band (Eq8Mode::highCut12, 8000.0, 0.0, q);
        for (const auto r : ratios)
            worst = std::max (worst, std::abs (facet::analogDb (params, 0, 8000.0 * r) - als190bq::analogDb (0, 0, 1, 1, 1 / q, 1, r)));
        band (Eq8Mode::notch, 1000.0, 0.0, q);
        for (const auto r : { 0.25, 0.5, 0.8, 1.25, 2.0, 4.0 })
            worst = std::max (worst, std::abs (facet::analogDb (params, 0, 1000.0 * r) - als190bq::analogDb (1, 0, 1, 1, 1 / q, 1, r)));
    }
    std::printf ("  12 dB cuts and notch: worst %.4f dB\n", worst);
    CHECK (worst < 0.02);

    // 48 dB cuts (four equal 12 dB sections in series): the same level at the cut frequency and the same stop-band
    // slope; the corner is rounder in Live — the differences two octaves down and around the corner are printed and
    // bounded (measured 2026-11-11: 1.62 dB and 4.52 dB at the worst Q of the three)
    double atCut = 0.0, stop = 0.0, corner = 0.0;
    for (const auto q : { 0.7071, 1.0, 0.5 })
    {
        band (Eq8Mode::lowCut48, 100.0, 0.0, q);
        atCut = std::max (atCut, std::abs (facet::analogDb (params, 0, 100.0) - 4.0 * als190bq::analogDb (1, 0, 0, 1, 1 / q, 1, 1.0)));
        stop = std::max (stop, std::abs (facet::analogDb (params, 0, 25.0) - 4.0 * als190bq::analogDb (1, 0, 0, 1, 1 / q, 1, 0.25)));
        for (double r = 0.5; r <= 8.0; r *= 1.05946)
            corner = std::max (corner, std::abs (facet::analogDb (params, 0, 100.0 * r) - 4.0 * als190bq::analogDb (1, 0, 0, 1, 1 / q, 1, r)));
    }
    std::printf ("  48 dB cut: at the cut frequency %.3f dB, two octaves into the stop band %.3f dB, corner %.2f dB\n", atCut, stop, corner);
    CHECK (atCut < 0.05 && stop < 2.0 && corner < 5.0);
}

TEST (als190bq_eq_eight_becomes_facet_eq)
{
    using namespace lynx;
    xml::Document doc;
    std::string bands;
    bands += als190bq::eqBand (0, true, 1, 40.0, 0.0, 0.7071);       // low cut 12
    bands += als190bq::eqBand (1, true, 2, 200.0, -3.0, 0.7071);     // low shelf
    bands += als190bq::eqBand (2, true, 3, 1000.0, 6.0, 2.0);        // bell
    bands += als190bq::eqBand (3, false, 3, 2500.0, 4.0, 1.0);       // bell, switched off
    bands += als190bq::eqBand (4, true, 4, 3000.0, 0.0, 6.0);        // notch
    bands += als190bq::eqBand (5, true, 5, 8000.0, 2.5, 0.7071);     // high shelf
    bands += als190bq::eqBand (6, true, 6, 15000.0, 0.0, 0.7071);    // high cut 12
    bands += als190bq::eqBand (7, true, 7, 18000.0, 0.0, 0.7071);    // high cut 48
    const auto device = "<Eq8 Id=\"1\"><On><Manual Value=\"false\" /></On>" + als190bq::v ("Mode", "0") + als190bq::par ("GlobalGain", -6.0) +
                        als190bq::par ("Scale", -0.5) + bands + als190bq::flag ("AdaptiveQ", true) + "</Eq8>";
    const auto t = alsdev::effectFromLiveDevice (als190bq::parse (doc, device));
    CHECK (t.has_value());
    if (! t)
        return;
    const auto& p = t->slot.params;
    CHECK (t->slot.kind == EffectKind::facetEq && ! t->slot.enabled && t->from == "EQ Eight" && t->to == "Facet EQ" && ! t->approximate);
    using facet::Shape;
    const Shape shapes[8] { Shape::lowCut, Shape::lowShelf, Shape::bell, Shape::bell, Shape::notch, Shape::highShelf, Shape::highCut, Shape::highCut };
    const double hz[8] { 40, 200, 1000, 2500, 3000, 8000, 15000, 18000 };
    for (int b = 0; b < 8; ++b)
    {
        CHECK (facet::shape (p[(std::size_t) facet::param (b, 0)]) == shapes[b]);
        CHECK (std::abs (facet::hz (p[(std::size_t) facet::param (b, 1)]) / hz[b] - 1.0) < 1e-4);
        CHECK (facet::bandPower (p, b) == (b != 3));
        CHECK (facet::placement (p[(std::size_t) (facet::placementParam0 + b)]) == facet::Placement::stereo);
    }
    CHECK (facet::slopeDb (p[(std::size_t) facet::param (0, 4)]) == 12 && facet::slopeDb (p[(std::size_t) facet::param (7, 4)]) == 48);
    // Scale −50 %: Facet's gain scale 50 % with the gains turned over; output −6 dB
    CHECK (std::abs (facet::gainScale (p[facet::scaleParam]) - 0.5) < 1e-6);
    CHECK (std::abs (facet::gainDb (p[(std::size_t) facet::param (1, 2)]) - 3.0) < 1e-4 && std::abs (facet::gainDb (p[(std::size_t) facet::param (2, 2)]) + 6.0) < 1e-4);
    CHECK (std::abs (facet::gainDb (p[(std::size_t) facet::param (4, 2)])) < 1e-6); // (a notch has no gain)
    CHECK (std::abs (facet::outputDb (p[facet::outputParam]) + 6.0) < 1e-4);
    CHECK (std::abs (facet::q (p[(std::size_t) facet::param (2, 3)]) - 2.0 * 1.41421356) < 1e-3);
    // the whole curve at 1 kHz: the bell's −6 dB × 50 % plus the output, and what the neighbours add there
    CHECK (als190bq::hasNote (t->notes.approximated, "Adaptive Q") && als190bq::hasNote (t->notes.approximated, "48 dB/oct"));
    CHECK (t->notes.dropped.empty());

    // M/S: curve A = mid, curve B = side, the bands that are on, in order; a ninth is listed
    std::string ms;
    for (int b = 0; b < 8; ++b)
        ms += als190bq::eqBand (b, b < 5, 3, 100.0 * (b + 1), 1.0, 1.0, b < 4, 3, 150.0 * (b + 1), -1.0, 1.0);
    doc.parse ("<Eq8>" + als190bq::v ("Mode", "2") + als190bq::par ("GlobalGain", 0.0) + als190bq::par ("Scale", 1.0) + ms + "</Eq8>");
    const auto side = alsdev::effectFromLiveDevice (doc.root());
    CHECK (side.has_value());
    if (side)
    {
        const auto& q = side->slot.params;
        for (int b = 0; b < 8; ++b)
            CHECK (facet::placement (q[(std::size_t) (facet::placementParam0 + b)]) == (b < 5 ? facet::Placement::mid : facet::Placement::side));
        CHECK (std::abs (facet::hz (q[(std::size_t) facet::param (5, 1)]) / 150.0 - 1.0) < 1e-4 && std::abs (facet::gainDb (q[(std::size_t) facet::param (5, 2)]) + 1.0) < 1e-4);
        CHECK (als190bq::hasNote (side->notes.dropped, "1 band(s) of the second channel curve"));
    }
    doc.parse ("<Eq8>" + als190bq::v ("Mode", "1") + als190bq::eqBand (0, true, 3, 500.0, 2.0, 1.0, true, 3, 700.0, 2.0, 1.0) + "</Eq8>");
    const auto lr = alsdev::effectFromLiveDevice (doc.root());
    CHECK (lr && facet::placement (lr->slot.params[(std::size_t) facet::placementParam0]) == facet::Placement::left &&
           facet::placement (lr->slot.params[(std::size_t) (facet::placementParam0 + 1)]) == facet::Placement::right &&
           facet::shape (lr->slot.params[(std::size_t) facet::param (2, 0)]) == Shape::off);

    // an element without the band data (another version, a stub): not translated — the importer drops and lists it
    doc.parse ("<Eq8 Id=\"2\">" + als190bq::v ("UserName", "") + "</Eq8>");
    CHECK (! alsdev::effectFromLiveDevice (doc.root()).has_value());
    doc.parse ("<Limiter Id=\"2\">" + als190bq::par ("Gain", 1.0) + "</Limiter>");
    CHECK (! alsdev::effectFromLiveDevice (doc.root()).has_value());
}

TEST (als190bq_compressor_becomes_crest)
{
    using namespace lynx;
    using namespace lynx::dyn::crest;
    using als190bq::flag;
    using als190bq::par;
    xml::Document doc;
    const auto sideChain = [] (bool on, const std::string& target, double volume) {
        return "<SideChain>" + flag ("OnOff", on) + "<RoutedInput><Routable>" + als190bq::v ("Target", target) + "</Routable>" + par ("Volume", volume) +
               "</RoutedInput>" + par ("DryWet", 1.0) + "</SideChain>";
    };
    const auto device = "<Compressor2 Id=\"3\"><On><Manual Value=\"true\" /></On>" + par ("Threshold", 0.316227766) + par ("Ratio", 4.0) + par ("Attack", 2.0) +
                        par ("Release", 50.0) + flag ("AutoReleaseControlOnOff", true) + par ("Gain", 3.0) + flag ("GainCompensation", false) +
                        par ("DryWet", 0.7) + par ("Model", 1) + flag ("LogEnvelope", true) + par ("Knee", 6.0) + par ("LookAhead", 2) +
                        sideChain (true, "AudioIn/Track.20/PostFxOut", 1.99526231) + flag ("SideChainEq_On", true) + par ("SideChainEq_Mode", 5) +
                        par ("SideChainEq_Freq", 200.0) + par ("SideChainEq_Q", 0.7071) + par ("SideChainEq_Gain", 0.0) + "</Compressor2>";
    const auto t = alsdev::effectFromLiveDevice (als190bq::parse (doc, device));
    CHECK (t.has_value());
    if (! t)
        return;
    const auto& p = t->slot.params;
    CHECK (t->slot.kind == EffectKind::crestCompressor && t->approximate && t->from == "Compressor");
    CHECK (styleOf (p) == Style::clean);
    CHECK (std::abs (thresholdDb (p[threshold]) + 10.0) < 1e-3);          // 0.3162 linear = −10 dB
    CHECK (std::abs (ratioOf (p[ratio]) - 4.0) < 1e-3);
    CHECK (std::abs (attackMs (p[attack]) - 2.0) < 1e-3 && std::abs (releaseMs (p[release]) - 50.0) < 1e-2);
    CHECK (p[autoRelease] == 1.0f && p[autoGain] == 0.0f);
    CHECK (std::abs (kneeDb (p[knee]) - 6.0) < 1e-4 && std::abs (rangeDb (p[range]) - 60.0) < 1e-6);
    CHECK (std::abs (lookaheadMs (p[lookahead]) - 10.0) < 1e-4 && maxLookaheadMs (p[maxLookahead]) == 10.0);
    CHECK (std::abs (trimDb (p[output]) - 3.0) < 1e-4 && std::abs (p[mix] - 0.7f) < 1e-6);
    CHECK (t->side.on && t->side.trackId == 20 && t->side.otherTap && p[scExternal] == 1.0f);
    CHECK (std::abs (scLevelDb (p[scLevel]) - 6.0) < 1e-3);               // 1.995 linear = +6 dB
    CHECK (facet::shape (p[(std::size_t) scParam (0, 0)]) == facet::Shape::lowCut && std::abs (facet::hz (p[(std::size_t) scParam (0, 1)]) / 200.0 - 1.0) < 1e-4);
    CHECK (std::abs (facet::q (p[(std::size_t) scParam (0, 3)]) - 1.0) < 1e-3);
    CHECK (als190bq::hasNote (t->notes.approximated, "RMS detection") && als190bq::hasNote (t->notes.approximated, "side-chain EQ"));

    // the ends: inf : 1 is Crest's true limit; values outside Crest's ranges are clamped and listed
    doc.parse ("<Compressor2>" + par ("Threshold", 1.99526231) + par ("Ratio", "340282326356119256160033759537265639424") + par ("Attack", 1000.0) +
               par ("Release", 1.0) + par ("Gain", 0.0) + par ("DryWet", 1.0) + par ("Model", 0) + par ("Knee", 0.0) + par ("LookAhead", 1) +
               flag ("LogEnvelope", false) + sideChain (true, "AudioIn/None", 1.0) + "</Compressor2>");
    const auto ends = alsdev::effectFromLiveDevice (doc.root());
    CHECK (ends.has_value());
    if (ends)
    {
        const auto& e = ends->slot.params;
        CHECK (e[ratio] == 1.0f && ratioOf (e[ratio]) >= 1.0e9);
        CHECK (std::abs (thresholdDb (e[threshold])) < 1e-6 && std::abs (attackMs (e[attack]) - 250.0) < 1e-2 && std::abs (releaseMs (e[release]) - 10.0) < 1e-3);
        CHECK (std::abs (lookaheadMs (e[lookahead]) - 1.0) < 1e-4 && maxLookaheadMs (e[maxLookahead]) == 1.0);
        CHECK (als190bq::hasNote (ends->notes.approximated, "threshold 6.0 dB") && als190bq::hasNote (ends->notes.approximated, "attack 1000 ms") &&
               als190bq::hasNote (ends->notes.approximated, "release 1 ms") && als190bq::hasNote (ends->notes.dropped, "linear release"));
        CHECK (ends->side.on && ends->side.trackId == -1 && e[scExternal] == 0.0f);
    }
    // Expand has no Crest style: the device is not translated (dropped and listed, as before)
    doc.parse ("<Compressor2>" + par ("Threshold", 0.5) + par ("Ratio", 2.0) + par ("Model", 2) + "</Compressor2>");
    CHECK (! alsdev::effectFromLiveDevice (doc.root()).has_value());

    // Glue: the stepped controls
    CHECK (alsdev::glueAttackMs (0) == 0.01 && alsdev::glueAttackMs (3) == 1.0 && alsdev::glueAttackMs (6) == 30.0);
    CHECK (alsdev::glueReleaseMs (0) == 100.0 && alsdev::glueReleaseMs (3) == 600.0 && alsdev::glueReleaseMs (5) == 1200.0 && alsdev::glueReleaseMs (6) == 0.0);
    CHECK (alsdev::glueRatio (0) == 2.0 && alsdev::glueRatio (1) == 4.0 && alsdev::glueRatio (2) == 10.0);
    doc.parse ("<GlueCompressor>" + par ("Threshold", -17.14) + par ("Range", 25.0) + par ("Makeup", 4.0) + par ("Attack", 5) + par ("Ratio", 2) +
               par ("Release", 1) + par ("DryWet", 0.5) + flag ("PeakClipIn", true) + als190bq::v ("Oversample", "true") + "</GlueCompressor>");
    const auto glue = alsdev::effectFromLiveDevice (doc.root());
    CHECK (glue.has_value());
    if (glue)
    {
        const auto& g = glue->slot.params;
        CHECK (styleOf (g) == Style::bus && glue->from == "Glue Compressor" && glue->approximate);
        CHECK (std::abs (thresholdDb (g[threshold]) + 17.14) < 1e-3 && std::abs (ratioOf (g[ratio]) - 10.0) < 1e-3);
        CHECK (std::abs (attackMs (g[attack]) - 10.0) < 1e-2 && std::abs (releaseMs (g[release]) - 200.0) < 1e-1 && g[autoRelease] == 0.0f);
        CHECK (std::abs (rangeDb (g[range]) - 25.0) < 1e-4 && std::abs (trimDb (g[output]) - 4.0) < 1e-4 && std::abs (g[mix] - 0.5f) < 1e-6);
        CHECK (oversamplingOf (g[oversampling]) == 2 && g[autoGain] == 0.0f);
        CHECK (als190bq::hasNote (glue->notes.dropped, "Soft clip"));
    }
    doc.parse ("<GlueCompressor>" + par ("Threshold", 0.0) + par ("Range", 70.0) + par ("Makeup", 0.0) + par ("Attack", 3) + par ("Ratio", 1) +
               par ("Release", 6) + par ("DryWet", 1.0) + "</GlueCompressor>");
    const auto automatic = alsdev::effectFromLiveDevice (doc.root());
    CHECK (automatic && automatic->slot.params[autoRelease] == 1.0f && std::abs (rangeDb (automatic->slot.params[range]) - 60.0) < 1e-6 &&
           std::abs (ratioOf (automatic->slot.params[ratio]) - 4.0) < 1e-3);
}

TEST (als190bq_utility_is_the_same_maths)
{
    using namespace lynx;
    using als190bq::flag;
    using als190bq::par;
    // the matrix: out L = ll·L + rl·R, out R = lr·L + rr·R
    const auto is = [] (const alsdev::StereoMatrix& m, double ll, double rl, double lr, double rr) {
        return std::abs (m.ll - ll) < 1e-12 && std::abs (m.rl - rl) < 1e-12 && std::abs (m.lr - lr) < 1e-12 && std::abs (m.rr - rr) < 1e-12;
    };
    alsdev::LiveUtility u;
    CHECK (is (alsdev::utilityMatrix (u), 1, 0, 0, 1) && alsdev::utilityIsExact (u));
    u.mono = true;
    CHECK (is (alsdev::utilityMatrix (u), 0.5, 0.5, 0.5, 0.5) && alsdev::utilityIsExact (u));
    u = {};
    u.width = 0.0;
    CHECK (is (alsdev::utilityMatrix (u), 0.5, 0.5, 0.5, 0.5) && alsdev::utilityIsExact (u));
    u.width = 2.0; // side doubled: L' = 1.5 L − 0.5 R
    CHECK (is (alsdev::utilityMatrix (u), 1.5, -0.5, -0.5, 1.5) && ! alsdev::utilityIsExact (u));
    u = {};
    u.phaseL = true;
    CHECK (is (alsdev::utilityMatrix (u), -1, 0, 0, 1) && alsdev::utilityIsExact (u));
    u.phaseR = true;
    CHECK (is (alsdev::utilityMatrix (u), -1, 0, 0, -1));
    u = {};
    u.channelMode = 0; // Left on both sides
    CHECK (is (alsdev::utilityMatrix (u), 1, 0, 1, 0) && alsdev::utilityIsExact (u));
    u.channelMode = 2; // Right on both sides
    CHECK (is (alsdev::utilityMatrix (u), 0, 1, 0, 1));
    u.channelMode = 3; // Swap
    CHECK (is (alsdev::utilityMatrix (u), 0, 1, 1, 0));
    u.phaseL = true;
    CHECK (! alsdev::utilityIsExact (u)); // (their order is not confirmed)
    u = {};
    u.midSideOn = true;
    u.midSide = 0.0; // mid only
    CHECK (is (alsdev::utilityMatrix (u), 0.5, 0.5, 0.5, 0.5));
    u.midSide = 2.0; // side only
    CHECK (is (alsdev::utilityMatrix (u), 0.5, -0.5, -0.5, 0.5));
    u = {};
    u.balance = 0.2;
    CHECK (! alsdev::utilityIsExact (u));
    u = {};
    u.bassMono = true;
    CHECK (! alsdev::utilityIsExact (u));

    // the device → LYNX's Utility, and LYNX's Utility doing what Live's settings say, sample for sample
    xml::Document doc;
    const auto device = [] (double gain, bool mute, bool pl, bool pr, int mode, double width, bool mono, double balance, bool bass = false) {
        return "<StereoGain Id=\"4\"><On><Manual Value=\"true\" /></On>" + flag ("PhaseInvertL", pl) + flag ("PhaseInvertR", pr) + par ("ChannelMode", mode) +
               par ("StereoWidth", width) + par ("MidSideBalance", 1.0) + als190bq::v ("MidSideBalanceOn", "false") + flag ("Mono", mono) + flag ("BassMono", bass) +
               par ("BassMonoFrequency", 120.0) + par ("Balance", balance) + par ("Gain", gain) + par ("LegacyGain", 0.0) + flag ("Mute", mute) +
               flag ("DcFilter", false) + als190bq::v ("LegacyMode", "false") + "</StereoGain>";
    };
    struct Case
    {
        double gain;
        bool mute, pl, pr;
        int mode;
        double width;
        bool mono;
    };
    const Case cases[] = { { 1.41253757, false, false, false, 1, 1.0, false }, { 0.5, false, true, false, 1, 1.0, false }, { 1.0, false, false, false, 3, 1.0, false },
                           { 2.0, false, false, false, 0, 1.0, false }, { 0.25, false, false, false, 2, 1.0, true }, { 1.0, true, false, false, 1, 1.0, false },
                           { 0.0, false, false, false, 1, 1.0, false }, { 1.0, false, false, true, 1, 0.0, false } };
    double worst = 0.0;
    for (const auto& c : cases)
    {
        const auto t = alsdev::effectFromLiveDevice (als190bq::parse (doc, device (c.gain, c.mute, c.pl, c.pr, c.mode, c.width, c.mono, 0.0)));
        CHECK (t.has_value());
        if (! t)
            continue;
        CHECK (t->slot.kind == EffectKind::utility && ! t->approximate && t->notes.approximated.empty() && t->notes.dropped.empty());
        const auto live = alsdev::readUtility (doc.root());
        CHECK (alsdev::utilityIsExact (live));
        const auto m = alsdev::utilityMatrix (live);
        const auto g = live.mute ? 0.0 : live.gain;
        float l[4] { 1.0f, 0.0f, 0.5f, -0.25f }, r[4] { 0.0f, 1.0f, -0.3f, 0.75f };
        const float inL[4] { 1.0f, 0.0f, 0.5f, -0.25f }, inR[4] { 0.0f, 1.0f, -0.3f, 0.75f };
        auto fx = makeEffect (t->slot);
        fx->prepare (48000.0);
        fx->process (l, r, 4, t->slot.params, 120.0);
        for (int i = 0; i < 4; ++i)
        {
            worst = std::max (worst, std::abs ((double) l[i] - g * (m.ll * inL[i] + m.rl * inR[i])));
            worst = std::max (worst, std::abs ((double) r[i] - g * (m.lr * inL[i] + m.rr * inR[i])));
        }
        // and back, for the export: the same settings
        const auto back = alsdev::liveUtilityOf (t->slot);
        CHECK (back.has_value());
        if (back)
        {
            const auto bm = alsdev::utilityMatrix (*back);
            CHECK (is (bm, m.ll, m.rl, m.lr, m.rr) && back->mute == (g == 0.0));
            CHECK (back->mute || std::abs (back->gain / live.gain - 1.0) < 1e-5);
        }
    }
    std::printf ("  Utility through LYNX's effect vs the device's maths: worst %.2e\n", worst);
    CHECK (worst < 1e-6);

    // outside the exact part: kept, and listed
    auto t = alsdev::effectFromLiveDevice (als190bq::parse (doc, device (56.2341309, false, false, false, 1, 1.0, false, 0.0)));
    CHECK (t && std::abs (als190bq::utilDb (t->slot.params[0]) - 24.0f) < 1e-4 && als190bq::hasNote (t->notes.approximated, "gain 35.0 dB"));
    t = alsdev::effectFromLiveDevice (als190bq::parse (doc, device (0.01, false, false, false, 1, 1.0, false, 0.0)));
    CHECK (t && t->slot.params[0] > 0.0f && als190bq::hasNote (t->notes.approximated, "gain -40.0 dB")); // (never the knob's "mute")
    t = alsdev::effectFromLiveDevice (als190bq::parse (doc, device (1.0, false, false, false, 1, 4.0, false, -0.5, true)));
    CHECK (t && als190bq::hasNote (t->notes.approximated, "balance") && als190bq::hasNote (t->notes.approximated, "width 400 %") &&
           als190bq::hasNote (t->notes.dropped, "Bass Mono (below 120 Hz)"));
    CHECK (t && std::abs (t->slot.params[1] - 0.25f) < 1e-6);
    t = alsdev::effectFromLiveDevice (als190bq::parse (doc, device (1.0, false, false, false, 1, 0.5, false, 0.0)));
    CHECK (t && als190bq::hasNote (t->notes.approximated, "width 50 %") && std::abs (als190bq::utilCoef (t->slot.params[2]) - 0.75f) < 1e-6 &&
           std::abs (als190bq::utilCoef (t->slot.params[3]) - 0.25f) < 1e-6);
    // a LYNX Utility with a balance or a free matrix has no exact Live form: not exported
    auto slot = makeEffectSlot (EffectKind::utility);
    CHECK (alsdev::liveUtilityOf (slot).has_value());
    slot.params[1] = 0.7f;
    CHECK (! alsdev::liveUtilityOf (slot).has_value());
    slot.params[1] = 0.5f;
    slot.params[3] = 0.6f;
    CHECK (! alsdev::liveUtilityOf (slot).has_value());
}

TEST (als190bq_reverb_becomes_atrium)
{
    using namespace lynx;
    using als190bq::flag;
    using als190bq::par;
    CHECK (std::abs (alsdev::atriumSizeOf (100.0) - 0.5f) < 1e-6 && alsdev::atriumSizeOf (0.222) == 0.0f && alsdev::atriumSizeOf (500.0) == 1.0f);
    CHECK (alsdev::atriumSizeOf (10.0) < 0.5f && alsdev::atriumSizeOf (10.0) > alsdev::atriumSizeOf (1.0) && alsdev::atriumSizeOf (250.0) > 0.5f);
    CHECK (std::abs (alsdev::atriumEarlyLateOf (1.0, 1.0) - 0.5f) < 1e-6 && alsdev::atriumEarlyLateOf (1.0, 0.0) == 0.0f &&
           std::abs (alsdev::atriumEarlyLateOf (0.0, 1.0) - 1.0f) < 1e-6);
    CHECK (std::abs (alsdev::reverbInputLowHz (800.0, 2.0) - 400.0) < 1e-9 && std::abs (alsdev::reverbInputHighHz (800.0, 2.0) - 1600.0) < 1e-9);

    xml::Document doc;
    const auto device = "<Reverb Id=\"5\"><On><Manual Value=\"true\" /></On>" + par ("PreDelay", 25.0) + flag ("BandHighOn", true) + flag ("BandLowOn", true) +
                        par ("BandFreq", 800.0) + par ("BandWidth", 4.0) + flag ("SpinOn", false) + par ("EarlyReflectModFreq", 0.3) +
                        par ("EarlyReflectModDepth", 17.5) + par ("DiffuseDelay", 0.5) + flag ("ShelfHighOn", true) + par ("HighFilterType", 0) +
                        par ("ShelfHiFreq", 4500.0) + par ("ShelfHiGain", 0.7) + flag ("ShelfLowOn", false) + par ("ShelfLoFreq", 90.0) + par ("ShelfLoGain", 0.75) +
                        flag ("ChorusOn", true) + par ("SizeModFreq", 1.04) + par ("SizeModDepth", 2.0) + par ("DecayTime", 4000.0) + par ("AllPassGain", 0.48) +
                        par ("AllPassSize", 0.4) + flag ("FreezeOn", true) + par ("RoomSize", 100.0) + par ("StereoSeparation", 120.0) + par ("MixReflect", 1.0) +
                        par ("MixDiffuse", 1.0) + par ("MixDirect", 0.35) + "</Reverb>";
    const auto t = alsdev::effectFromLiveDevice (als190bq::parse (doc, device));
    CHECK (t.has_value());
    if (! t)
        return;
    const auto& p = t->slot.params;
    CHECK (t->slot.kind == EffectKind::atrium && t->approximate && t->from == "Reverb" && t->to == "Atrium");
    CHECK (std::abs (p[0] - 0.35f) < 1e-6);                                    // dry/wet
    CHECK (std::abs (500.0 * p[1] - 25.0) < 1e-3);                             // predelay, ms
    CHECK (std::abs (verbs::atrium::decaySec (p[2]) - 4.0) < 1e-3);            // decay, s
    CHECK (std::abs ((100.0 + 14900.0 * p[3]) - 3200.0) < 0.5);                // input high edge 800 · 2² Hz
    CHECK (std::abs (1000.0 * p[21] - 200.0) < 0.1);                           // input low edge 800 / 2² Hz
    CHECK (std::abs (p[4] - 0.5f) < 1e-6 && std::abs (p[5] - 0.5f) < 1e-6);
    CHECK (std::abs ((0.05 + 4.95 * p[7]) - 1.04) < 1e-4 && std::abs (p[8] - 0.5f) < 1e-6);   // chorus rate Hz, depth
    CHECK (std::abs ((0.5 + 1.5 * p[9]) - 1.0) < 1e-6);                        // low shelf off: bass multiplier 1 X
    CHECK (std::abs ((0.1 + 0.9 * p[11]) - 0.7) < 1e-6 && std::abs ((100.0 + 14900.0 * p[12]) - 4500.0) < 0.5);
    CHECK (p[16] == 0.0f && std::abs (p[18] - 0.5f) < 1e-6);                   // spin off; density 0.48 of 0.96
    CHECK (als190bq::hasNote (t->notes.dropped, "Freeze") && als190bq::hasNote (t->notes.dropped, "Stereo 120") && ! als190bq::hasNote (t->notes.dropped, "Shape"));
    doc.parse ("<Reverb>" + par ("PreDelay", 2.5) + par ("DecayTime", 60000.0) + par ("RoomSize", 500.0) + flag ("BandHighOn", false) + flag ("BandLowOn", false) + "</Reverb>");
    const auto longest = alsdev::effectFromLiveDevice (doc.root());
    CHECK (longest && std::abs (verbs::atrium::decaySec (longest->slot.params[2]) - 60.0) < 0.05 && longest->slot.params[3] == 1.0f &&
           longest->slot.params[21] == 0.0f && longest->slot.params[5] == 1.0f);
}

TEST (als190bq_simpler_settings)
{
    using namespace lynx;
    using als190bq::flag;
    using als190bq::par;
    using als190bq::v;
    for (int k = 0; k < 8; ++k)
        CHECK (alsdev::simplerVoices (k) == k + 1);
    CHECK (alsdev::simplerVoices (8) == 10 && alsdev::simplerVoices (11) == 16 && alsdev::simplerVoices (14) == 32);
    // Classic mode's region: start 25 % of the part, length 50 % of the rest, the last half of that looping
    const auto region = alsdev::simplerRegion (1000.0, 9000.0, 0.25, 0.5, 0.5);
    CHECK (std::abs (region.start - 3000.0) < 1e-9 && std::abs (region.end - 6000.0) < 1e-9 && std::abs (region.loopStart - 4500.0) < 1e-9);

    xml::Document doc;
    const auto envelope = [] (double a, double d, double s, double r) {
        return "<Envelope>" + par ("AttackTime", a) + par ("DecayTime", d) + par ("SustainLevel", s) + par ("ReleaseTime", r) + "</Envelope>";
    };
    const auto filter = "<Filter>" + flag ("IsOn", true) + "<Slot><Value><SimplerFilter Id=\"0\">" + par ("Type", 1) + par ("Freq", 800.0) + par ("Res", 0.4) +
                        "<Envelope>" + flag ("IsOn", true) + par ("Amount", 24.0) + par ("AttackTime", 10.0) + par ("DecayTime", 300.0) + par ("SustainLevel", 0.25) +
                        par ("ReleaseTime", 80.0) + "</Envelope>" + par ("ModByLfo", 6.0) + "</SimplerFilter></Value></Slot></Filter>";
    const auto lfo = "<Lfo>" + flag ("IsOn", true) + "<Slot><Value><SimplerLfo Id=\"0\">" + par ("Type", 2) + par ("Frequency", 4.0) + par ("RateType", 0) +
                     par ("Attack", 250.0) + flag ("Retrigger", true) + "</SimplerLfo></Value></Slot></Lfo>";
    const auto pitch = "<Pitch>" + par ("TransposeKey", -12.0) + par ("TransposeFine", 25.0) + par ("PitchLfoAmount", 0.0) + "<Envelope>" + flag ("IsOn", true) +
                       "<Slot><Value><SimplerPitchEnvelope Id=\"0\">" + par ("Amount", 12.0) + par ("AttackTime", 0.1) + par ("DecayTime", 150.0) +
                       par ("SustainLevel", 0.0) + par ("ReleaseTime", 50.0) + "</SimplerPitchEnvelope></Value></Slot></Envelope></Pitch>";
    const auto volume = "<VolumeAndPan>" + par ("Volume", -12.0) + par ("Panorama", 0.2) + par ("VolumeLfoAmount", 0.5) + par ("PanoramaLfoAmount", 0.0) +
                        envelope (5.0, 600.0, 0.5, 250.0) + "<OneShotEnvelope>" + par ("FadeInTime", 3.0) + par ("SustainMode", 1) + par ("FadeOutTime", 40.0) +
                        "</OneShotEnvelope></VolumeAndPan>";
    const auto loopOn = "<LoopModulators>" + par ("SampleStart", 0.0) + par ("SampleLength", 1.0) + flag ("LoopOn", true) + par ("LoopLength", 0.25) +
                        par ("LoopFade", 0.0) + "</LoopModulators>" + flag ("Reverse", false);
    const auto globals = [] (int mode, int voices, int porta) {
        return v ("NumVoices", std::to_string (voices)) + par ("PortamentoMode", porta) + par ("PortamentoTime", 250.0) + par ("SpreadAmount", 0.0) +
               v ("PlaybackMode", std::to_string (mode));
    };

    // Classic: everything on a sampler channel that already has its sample
    {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.samplePath = "x.wav";
        alsdev::Notes notes;
        const auto r = alsdev::applySimpler (als190bq::parse (doc, als190bq::simpler (globals (0, 0, 2), filter + lfo + pitch + volume, {}, loopOn)), c, 120.0, 0.0, notes);
        const auto& s = c.sampler;
        CHECK (std::abs (s.pitch + 11.75f) < 1e-5 && std::abs (s.gainDb + 12.0f) < 1e-5 && std::abs (c.pan - 0.2f) < 1e-6);
        CHECK (s.filterType == 2 && std::abs (s.cutoff - 800.0f) < 1e-3 && std::abs (s.resonance - 0.4f) < 1e-6);   // high pass
        CHECK (std::abs (s.filterEnv.amount - 2.0f) < 1e-6 && std::abs (s.filterEnv.attack - 0.01f) < 1e-6 && std::abs (s.filterEnv.decay - 0.3f) < 1e-6 &&
               std::abs (s.filterEnv.sustain - 0.25f) < 1e-6 && std::abs (s.filterEnv.release - 0.08f) < 1e-6);        // 24 st = 2 octaves; ms → s
        CHECK (std::abs (s.pitchEnv.amount - 12.0f) < 1e-6 && std::abs (s.pitchEnv.decay - 0.15f) < 1e-6);
        CHECK (std::abs (s.lfo[lfoVolume].amount - 0.5f) < 1e-6 && std::abs (s.lfo[lfoVolume].period - 0.25f) < 1e-6 && s.lfo[lfoVolume].shape == 1 &&
               std::abs (s.lfo[lfoVolume].attack - 0.25f) < 1e-6 && ! s.lfo[lfoVolume].global);                        // 4 Hz triangle, 250 ms fade in
        CHECK (std::abs (s.lfo[lfoCutoff].amount - 0.5f) < 1e-6 && s.lfo[lfoPan].amount == 0.0f && s.lfo[lfoPitch].amount == 0.0f);
        CHECK (c.maxPolyphony == 1 && s.cut && s.porta && std::abs (s.portaBeats - 0.5f) < 1e-6);                     // 250 ms at 120 bpm
        CHECK (r.loop && r.loopStart == 66149 && r.loopEnd == 88199 && r.keyShift == 0);                               // the last quarter of 0 … 88199
        CHECK (als190bq::hasNote (notes.approximated, "filter (12 dB/oct") && als190bq::hasNote (notes.approximated, "glide time") &&
               als190bq::hasNote (notes.approximated, "velocity sensitivity"));
    }
    // One-Shot, Gate: fade in / fade out are the envelope; Trigger: the whole sample
    {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.samplePath = "x.wav";
        alsdev::Notes notes;
        alsdev::applySimpler (als190bq::parse (doc, als190bq::simpler (globals (1, 5, 0), volume)), c, 120.0, 0.0, notes);
        CHECK (std::abs (c.sampler.attack - 0.003f) < 1e-6 && std::abs (c.sampler.release - 0.04f) < 1e-6 && c.sampler.sustain == 1.0f && c.maxPolyphony == 6 &&
               ! c.sampler.cut && ! c.sampler.porta);
        std::string trigger = volume;
        trigger.replace (trigger.find ("<SustainMode><Manual Value=\"1\""), 30, "<SustainMode><Manual Value=\"0\"");
        ChannelDef d;
        d.samplePath = "x.wav";
        alsdev::Notes more;
        alsdev::applySimpler (als190bq::parse (doc, als190bq::simpler (globals (1, 5, 0), trigger)), d, 120.0, 0.0, more);
        CHECK (d.sampler.release == 0.0f && als190bq::hasNote (more.dropped, "one-shot fade out"));
    }
    // Slice: the stored points as fractions of the 2 s file (the first 0.5 s is a slice of its own), notes up 24 keys
    {
        ChannelDef c;
        c.samplePath = "x.wav";
        alsdev::Notes notes;
        const auto points = v ("SlicingStyle", "0") + v ("SlicingThreshold", "100") + "<SlicePoints><SlicePoint TimeInSeconds=\"0.5\" Rank=\"0\" />"
                            "<SlicePoint TimeInSeconds=\"1.0\" Rank=\"0\" /><SlicePoint TimeInSeconds=\"1.5\" Rank=\"0\" /></SlicePoints>";
        const auto r = alsdev::applySimpler (als190bq::parse (doc, als190bq::simpler (globals (2, 5, 0), volume, points)), c, 120.0, 0.0, notes);
        CHECK (c.sampler.slices.size() == 4 && r.keyShift == 24 && ! r.loop);
        if (c.sampler.slices.size() == 4)
            CHECK (c.sampler.slices[0] == 0.0f && std::abs (c.sampler.slices[1] - 0.25f) < 1e-6 && std::abs (c.sampler.slices[3] - 0.75f) < 1e-6);
        CHECK (als190bq::hasNote (notes.approximated, "Slice mode"));
        // by region: 8 equal slices
        ChannelDef e;
        e.samplePath = "x.wav";
        alsdev::Notes n2;
        alsdev::applySimpler (als190bq::parse (doc, als190bq::simpler (globals (2, 5, 0), volume, v ("SlicingStyle", "2") + v ("SlicingRegions", "8"))), e, 120.0, 0.0, n2);
        CHECK (e.sampler.slices.size() == 8 && std::abs (e.sampler.slices[4] - 0.5f) < 1e-4);
    }
    // Warp: 2 s that hold 4 beats play as 4 beats at the set's tempo, pitch kept, Live's mode
    {
        ChannelDef c;
        c.samplePath = "x.wav";
        alsdev::Notes notes;
        const auto warp = "<SampleWarpProperties><WarpMarkers><WarpMarker Id=\"0\" SecTime=\"0\" BeatTime=\"0\" /><WarpMarker Id=\"1\" SecTime=\"1\" BeatTime=\"2\" />"
                          "</WarpMarkers>" + v ("WarpMode", "1") + v ("IsWarped", "true") + "</SampleWarpProperties>";
        alsdev::applySimpler (als190bq::parse (doc, als190bq::simpler (globals (0, 5, 0), volume, warp)), c, 120.0, 0.0, notes);
        CHECK (std::abs (c.sampler.stretchBeats - 4.0f) < 1e-5 && c.sampler.keepPitch && c.sampler.stretchMethod == StretchMethod::tones);
        CHECK (als190bq::hasNote (notes.approximated, "Warp"));
    }
}

TEST (als190bq_a_set_with_live_devices_opens_on_lynx_effects)
{
    using namespace lynx;
    using als190bq::flag;
    using als190bq::par;
    using als190bq::v;
    const auto globals = v ("NumVoices", "5") + par ("PortamentoMode", 0) + v ("PlaybackMode", "2");
    const auto points = v ("SlicingStyle", "2") + v ("SlicingRegions", "4");
    const auto simpler = als190bq::simpler (globals, "<VolumeAndPan>" + par ("Volume", -6.0) + par ("Panorama", 0.0) + "</VolumeAndPan>", points);
    const auto eq = "<Eq8 Id=\"1\"><On><Manual Value=\"true\" /></On>" + v ("Mode", "0") + par ("GlobalGain", 0.0) + par ("Scale", 1.0) +
                    als190bq::eqBand (0, true, 3, 1000.0, 3.0, 1.0) + "</Eq8>";
    const auto comp = "<Compressor2 Id=\"2\"><On><Manual Value=\"true\" /></On>" + par ("Threshold", 0.1) + par ("Ratio", 2.0) + par ("Attack", 10.0) +
                      par ("Release", 100.0) + par ("Gain", 0.0) + par ("DryWet", 1.0) + par ("Model", 0) + par ("Knee", 6.0) + par ("LookAhead", 0) +
                      "<SideChain>" + flag ("OnOff", true) + "<RoutedInput><Routable>" + v ("Target", "AudioIn/Track.20/PostMixerOut") + "</Routable>" +
                      par ("Volume", 1.0) + "</RoutedInput>" + par ("DryWet", 1.0) + "</SideChain></Compressor2>";
    const auto utility = "<StereoGain Id=\"3\"><On><Manual Value=\"true\" /></On>" + par ("Gain", 0.5) + par ("ChannelMode", 1) + par ("StereoWidth", 1.0) +
                         flag ("Mono", true) + par ("Balance", 0.0) + "</StereoGain>";
    const auto reverb = "<Reverb Id=\"4\"><On><Manual Value=\"false\" /></On>" + par ("PreDelay", 10.0) + par ("DecayTime", 2000.0) + par ("MixDirect", 1.0) + "</Reverb>";
    const auto unknown = "<Erosion Id=\"5\">" + v ("UserName", "") + "</Erosion><MidiArpeggiator Id=\"6\">" + v ("UserName", "") + "</MidiArpeggiator>";
    const auto notes = "<KeyTrack Id=\"0\"><Notes>" + als175a::midiNote (0, 0.5, 100) + "</Notes>" + v ("MidiKey", "36") + "</KeyTrack><KeyTrack Id=\"1\"><Notes>" +
                       als175a::midiNote (1, 0.5, 100) + "</Notes>" + v ("MidiKey", "38") + "</KeyTrack>";
    xml::Document doc;
    CHECK (doc.parse (als190bq::deviceSet (simpler + eq + comp + unknown, utility + reverb, notes)));
    FlpImportReport report;
    auto options = als175a::options (&report);
    options.common.fileExists = [] (const std::string& p) { return p.find ("vocal.wav") != std::string::npos; };
    std::string error;
    const auto song = importAlsXml (doc, options, &error);
    CHECK (song.has_value());
    if (! song)
    {
        std::printf ("  import error: %s\n", error.c_str());
        return;
    }
    // inserts: Master, Keys, Bus
    CHECK (song->mixer.size() == 3);
    const auto& keys = song->mixer[1].effects;
    const auto& bus = song->mixer[2].effects;
    CHECK (keys.size() == 2 && keys[0].kind == EffectKind::facetEq && keys[1].kind == EffectKind::crestCompressor);
    CHECK (bus.size() == 2 && bus[0].kind == EffectKind::utility && bus[1].kind == EffectKind::atrium && ! bus[1].enabled && bus[0].enabled);
    if (keys.size() == 2)
    {
        CHECK (std::abs (facet::gainDb (keys[0].params[(std::size_t) facet::param (0, 2)]) - 3.0) < 1e-4);
        CHECK (keys[1].sidechain == 2 && keys[1].params[dyn::crest::scExternal] == 1.0f);            // the Bus track's insert
        CHECK (std::abs (dyn::crest::thresholdDb (keys[1].params[dyn::crest::threshold]) + 20.0) < 1e-3);
    }
    if (bus.size() == 2)
        CHECK (std::abs (als190bq::utilDb (bus[0].params[0]) + 6.0206f) < 1e-3 && std::abs (verbs::atrium::decaySec (bus[1].params[2]) - 2.0) < 1e-3);
    // the report: exact pairs under converted, fitted pairs and every note under "kept but approximated", the rest as before
    CHECK (report.converted.count ("EQ Eight -> Facet EQ") == 1 && report.converted.count ("Utility -> Utility") == 1 && report.converted.count ("Simpler") == 1);
    CHECK (als190bq::reportHas (report.partialEffects, "Compressor -> Crest Compressor") && als190bq::reportHas (report.partialEffects, "Reverb -> Atrium"));
    CHECK (report.droppedEffects.count ("Erosion") == 1 && report.droppedEffects.size() == 1 && report.unsupported.count ("MidiArpeggiator") == 1);
    CHECK (als190bq::reportHas (report.approximated, "Simpler: Slice mode") && als190bq::reportHas (report.approximated, "Simpler: velocity sensitivity"));
    // the Simpler: −6 dB, 4 slices, and its notes moved from C1 up to LYNX's slice keys
    const ChannelDef* sampler = nullptr;
    for (const auto& c : song->channels)
        if (c.name == "Keys")
            sampler = &c;
    CHECK (sampler != nullptr && sampler->instrument == InstrumentKind::sampler && sampler->sampler.slices.size() == 4 &&
           std::abs (sampler->sampler.gainDb + 6.0f) < 1e-5 && sampler->maxPolyphony == 6);
    bool moved = false;
    for (const auto& p : song->patterns)
        if (p.notes.size() == 2)
            moved = (p.notes[0].key == 60 && p.notes[1].key == 62) || (p.notes[0].key == 62 && p.notes[1].key == 60);
    CHECK (moved);

    // a Simpler as today's tests write it (no playback-mode data) keeps the old import: its own loop, nothing added
    const auto old = "<OriginalSimpler Id=\"0\"><Player><MultiSampleMap><SampleParts><MultiSamplePart Id=\"0\">" + v ("RootKey", "60") + v ("Volume", "1") +
                     "<KeyRange>" + v ("Min", "0") + v ("Max", "127") + "</KeyRange><VelocityRange>" + v ("Min", "1") + v ("Max", "127") + "</VelocityRange>" +
                     "<SampleRef><FileRef>" + v ("RelativePathType", "1") + v ("RelativePath", "Samples/Imported/vocal.wav") + v ("Path", "D:/old/vocal.wav") +
                     "</FileRef></SampleRef></MultiSamplePart></SampleParts></MultiSampleMap></Player></OriginalSimpler>";
    CHECK (doc.parse (als190bq::deviceSet (old)));
    FlpImportReport plain;
    options.common.report = &plain;
    const auto before = importAlsXml (doc, options, &error);
    CHECK (before.has_value());
    if (before)
        for (const auto& c : before->channels)
            if (c.name == "Keys")
                CHECK (c.sampler == SamplerSettings {} && c.maxPolyphony == 0 && c.pan == 0.0f && plain.approximated.empty());
}
