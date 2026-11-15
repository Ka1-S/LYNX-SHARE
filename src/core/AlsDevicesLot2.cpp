// Live's own devices → LYNX's, lot 2 (round 190cf; core/AlsDevices.h has the list and the evidence tags). Same rules
// as lot 1: each conversion is a law in a unit, what is fitted or has no counterpart becomes a note for the import
// report, and only the device's saved state is read. Every translator also records which Live parameter drives which
// LYNX parameter (ParamLink), so automation and rack macros can follow.

#include "core/AlsDevices.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/ApexLimiter.h"
#include "core/DynamicsSuite.h"
#include "core/EchoSuite.h"
#include "core/Effects.h"
#include "core/MixSuite.h"
#include "core/XmlLite.h"

namespace lynx::alsdev
{
namespace
{
using xml::Element;

constexpr double pi = 3.141592653589793;

float unit (double v) noexcept { return (float) std::clamp (v, 0.0, 1.0); }
float linValue (double v, double lo, double hi) noexcept { return unit ((v - lo) / (hi - lo)); }
float logValue (double v, double lo, double hi) noexcept { return unit (std::log (std::clamp (v, lo, hi) / lo) / std::log (hi / lo)); }
float onOff (bool on) noexcept { return on ? 1.0f : 0.0f; }

std::string text (const char* format, double a, double b = 0.0)
{
    char out[200];
    std::snprintf (out, sizeof (out), format, a, b);
    return out;
}

double manual (const Element& device, const std::string& name, double fallback) { return device.number (name + "/Manual", fallback); }
bool manualBool (const Element& device, const std::string& name, bool fallback) { return device.boolean (name + "/Manual", fallback); }
bool has (const Element& device, const std::string& name) { return (bool) device.path (name + "/Manual"); }

// Sets a LYNX parameter from a Live parameter through its law and records the route
struct Put
{
    const Element& device;
    Translated& out;
    void operator() (const std::string& live, int param, std::function<float (double)> law, double fallback) const
    {
        out.slot.params[(std::size_t) param] = law (manual (device, live, fallback));
        out.links.push_back ({ live, param, std::move (law) });
    }
};

// The side-chain EQ of Gate ("SideChainEq/…") as Sentry's side-chain high / low pass: only those two shapes exist there
void sentrySideChain (const Element& device, Translated& out)
{
    using namespace dyn::sentry;
    auto& p = out.slot.params;
    out.side = sideInputOf (device);
    out.sideParam = scExternal;
    if (out.side.on)
    {
        p[scExternal] = out.side.trackId >= 0 ? 1.0f : 0.0f;
        const auto gain = linValue (dbOfGain (manual (device, "SideChain/RoutedInput/Volume", 1.0)), -36.0, 36.0);
        p[scGainL] = gain;
        p[scGainR] = gain;
        if (std::abs (manual (device, "SideChain/DryWet", 1.0) - 1.0) > 1.0e-6)
            out.notes.dropped.push_back ("side-chain dry/wet (the detector hears the side-chain input only)");
    }
    const auto stem = std::string (device.path ("SideChainEq/On") ? "SideChainEq/" : "SideChainEq_");
    if (manualBool (device, stem + "On", false))
    {
        // Mode [DOC: low shelf, bell, high shelf, low pass, band pass, high pass; DATA 0 … 5, order ASSUMED as in lot 1]
        const auto mode = (int) manual (device, stem + "Mode", 5);
        const auto hz = manual (device, stem + "Freq", 200.0);
        if (mode == 3)
            p[lowPass] = filterValue (hz);
        else if (mode == 5)
            p[highPass] = filterValue (hz);
        else
            out.notes.dropped.push_back ("side-chain EQ shape (Sentry's side chain has a high pass and a low pass)");
    }
}

// One side of an echo in Echoform: a note value when one has that length, else the time in ms at the set's tempo
void echoSide (Translated& out, bool right, bool synced, double beats, double ms, const Context& context, bool& fixedMs)
{
    using namespace echosuite::echoform;
    auto& p = out.slot.params;
    const auto sync = right ? syncR : syncL, note = right ? noteR : noteL, time = right ? timeR : timeL;
    if (synced)
    {
        if (const auto n = echoformNoteOf (beats))
        {
            p[sync] = echoformSyncValue (n->first);
            p[note] = echoformNoteValue (n->second);
            return;
        }
        ms = beats * 60000.0 / std::max (context.bpm, 1.0);
        fixedMs = true;
    }
    p[sync] = echoformSyncValue (0);
    p[time] = timeValue (ms);
}

// A band-pass given as centre and width in octaves [ASSUMED unit, as lot 1's reverb input filter] on Echoform's cuts
void echoBandPass (Translated& out, double centreHz, double widthOct)
{
    using namespace echosuite::echoform;
    out.slot.params[lowCut] = linValue (reverbInputLowHz (centreHz, widthOct), 10.0, 2000.0);
    out.slot.params[highCut] = linValue (reverbInputHighHz (centreHz, widthOct), 200.0, 20000.0);
}

float modRateValue (double hz) noexcept // Echoform: 0.01 + 9.99 v^2.3277 Hz
{
    return unit (std::pow (std::clamp ((hz - 0.01) / 9.99, 0.0, 1.0), 1.0 / 2.3277));
}
} // namespace

// ================================================================================================ shared laws

double hzOfLiveNote (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

double liveBeatRateBeats (int index) noexcept
{
    // in bars of 4/4, then × 4 quarter notes
    static constexpr double bars[22] { 1.0 / 64, 1.0 / 48, 1.0 / 32, 1.0 / 24, 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 3.0 / 16, 1.0 / 4, 5.0 / 16,
                                       1.0 / 3,  3.0 / 8,  1.0 / 2,  3.0 / 4,  1.0,      1.5,      2.0,     3.0,     4.0,      6.0,     8.0 };
    return 4.0 * bars[std::clamp (index, 0, 21)];
}

int delaySixteenths (int index) noexcept
{
    static constexpr int n[8] { 1, 2, 3, 4, 5, 6, 8, 16 };
    return n[std::clamp (index, 0, 7)];
}

std::optional<std::pair<int, int>> echoformNoteOf (double beats) noexcept
{
    for (int kind = 1; kind <= 3; ++kind)
        for (int index = 0; index < 10; ++index)
            if (std::abs (echosuite::echoform::noteBeats (index, kind) - beats) < 1.0e-6 * std::max (1.0, beats))
                return std::pair<int, int> { kind, index };
    return std::nullopt;
}
float echoformSyncValue (int kind) noexcept { return (float) (std::clamp (kind, 0, 3) + 1) / 4.0f; }
float echoformNoteValue (int index) noexcept { return ((float) std::clamp (index, 0, 9) + 0.5f) / 10.0f; }

float echoformMixOf (double dryWet, bool equalLoudness) noexcept
{
    const auto x = std::clamp (dryWet, 0.0, 1.0);
    if (equalLoudness || x <= 0.0 || x >= 1.0)
        return (float) x;
    return unit (2.0 / pi * std::atan (x / (1.0 - x)));
}

double autoFilterX (double resonance, bool legacy) noexcept
{
    if (legacy)
        return std::clamp (resonance, 0.0, 0.9);
    // [MEASURED 190cz: the new device's x at 27 resonances, from the fitted Q of each render; 1.25 renders as 1]
    static constexpr double r[] { 0.0, 0.05, 0.1, 0.125, 0.2, 0.25, 0.3, 0.375, 0.4, 0.5, 0.6, 0.625, 0.7, 0.75,
                                  0.8, 0.85, 0.875, 0.9, 0.91, 0.92, 0.93, 0.95, 0.96, 0.97, 0.98, 0.99, 1.0 };
    static constexpr double x[] { 0.0, 0.04993, 0.09987, 0.12484, 0.19968, 0.24956, 0.29943, 0.37420, 0.39913, 0.49875,
                                  0.59833, 0.62321, 0.69784, 0.74757, 0.79729, 0.84699, 0.87184, 0.89668, 0.90165,
                                  0.90662, 0.91158, 0.92152, 0.92649, 0.93145, 0.93642, 0.94139, 0.94635 };
    const auto v = std::clamp (resonance, 0.0, 1.0);
    std::size_t k = 1;
    while (k + 1 < std::size (r) && r[k] < v)
        ++k;
    return x[k - 1] + (x[k] - x[k - 1]) * (v - r[k - 1]) / (r[k] - r[k - 1]);
}

double filterQOfResonance (double resonance, bool legacy) noexcept
{
    const auto x = autoFilterX (resonance, legacy);
    return 0.5 + 2.0 * x / (1.0 - x);
}

float filterTypeValue (int type) noexcept { return ((float) std::clamp (type, 0, 2) + 0.5f) / 3.0f; }
float filterCutoffValue (double hz) noexcept { return logValue (hz, 20.0, 20000.0); }
float filterQValue (double q) noexcept { return logValue (q, 0.5, 12.0); }
float lofiRateValue (double hz) noexcept
{
    const auto hold = 44100.0 / std::clamp (hz, 1.0, 44100.0); // samples held
    return unit (std::cbrt (std::max (0.0, hold - 1.0) / 128.0));
}
float lofiBitsValue (double bits) noexcept
{
    const auto levels = std::pow (2.0, std::clamp (bits, 1.0, 16.0));
    return unit (std::sqrt (std::max (0.0, 65536.0 / levels - 1.0) / 2048.0));
}
float eqGainValue (double db) noexcept { return linValue (db, -18.0, 18.0); }
float eqMidValue (double hz) noexcept { return logValue (hz, 200.0, 5000.0); }
float chorusRateValue (double hz) noexcept { return logValue (hz, 0.05, 5.0); }

PrismAction prismAbove (double slope) noexcept
{
    PrismAction a;
    if (std::abs (slope) < 1.0e-4)
        return a;
    a.active = true;
    if (slope < 0.0) // downward compression: the output rises 1 + slope = 1 / ratio dB per dB
    {
        a.limit = slope <= -0.9999;
        a.ratio = a.limit ? 100.0 : 1.0 / (1.0 + slope);
    }
    else // upward expansion: Prism lifts by (1 − 1 / ratio) dB per dB over the threshold
    {
        a.expand = a.upward = true;
        a.limit = slope >= 0.9999;
        a.ratio = a.limit ? 100.0 : 1.0 / (1.0 - slope);
    }
    return a;
}

PrismAction prismBelow (double slope) noexcept
{
    PrismAction a;
    if (std::abs (slope) < 1.0e-4)
        return a;
    a.active = true;
    if (slope > 0.0) // upward compression: lifted by slope = 1 − 1 / ratio dB per dB under the threshold
    {
        a.upward = true;
        a.limit = slope >= 0.9999;
        a.ratio = a.limit ? 100.0 : 1.0 / (1.0 - slope);
    }
    else // downward expansion: lowered by −slope = ratio − 1 dB per dB under the threshold
    {
        a.expand = true;
        a.ratio = 1.0 - slope;
    }
    return a;
}

float prismAttackValue (double ms) noexcept { return unit (std::pow (std::clamp ((ms - 0.11) / 89.0, 0.0, 1.0), 1.0 / 3.9)); }
float prismReleaseValue (double ms) noexcept { return unit (std::cbrt (std::clamp ((ms - 130.0) / 3500.0, 0.0, 1.0))); }
float sentryAttackValue (double ms) noexcept { return unit (std::pow (std::clamp (ms / 1000.0, 0.0, 1.0), 0.25)); }
float sentryHoldValue (double ms) noexcept { return unit (std::sqrt (std::clamp (ms / 250.0, 0.0, 1.0))); }
float sentryReleaseValue (double ms) noexcept { return unit (std::cbrt (std::clamp (ms / 5000.0, 0.0, 1.0))); }

double limiterLookaheadMs (int index) noexcept
{
    static constexpr double ms[3] { 1.5, 3.0, 6.0 };
    return ms[std::clamp (index, 0, 2)];
}
double gateLookaheadMs (int index) noexcept
{
    static constexpr double ms[3] { 0.0, 1.5, 10.0 };
    return ms[std::clamp (index, 0, 2)];
}

double arpRateBeats (int index) noexcept
{
    static constexpr double denominators[14] { 128, 96, 64, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1 };
    return 4.0 / denominators[std::clamp (index, 0, 13)];
}

int arpDirectionOf (int style, bool& exact) noexcept
{
    // Up, Down, UpDown, DownUp, Up & Down, Down & Up, Converge, Diverge, Con & Diverge, Pinky Up, Pinky UpDown,
    // Thumb Up, Thumb UpDown, Play Order, Chord Trigger, Random, Random Other, Random Once
    exact = style == 0 || style == 1 || style == 2 || style == 4 || style == 15;
    switch (style)
    {
        case 0: return 1;
        case 1: return 2;
        case 2: case 3: return 3;
        case 4: case 5: return 5;
        case 15: case 16: case 17: return 4;
        default: return 1;
    }
}

// ================================================================================================ Delay, Echo

bool delayToEchoform (const Element& device, const Context& context, Translated& out)
{
    using namespace echosuite::echoform;
    if (! has (device, "Feedback") || ! has (device, "DelayLine_TimeL"))
        return false;
    out.slot = makeEffectSlot (EffectKind::echoform);
    auto& p = out.slot.params;
    const Put put { device, out };
    p[mode] = modeValue (Mode::clean);
    const auto pingPong = manualBool (device, "DelayLine_PingPong", false);
    p[style] = styleValue (pingPong ? Style::pingPong : Style::dual);
    // each side: Sync + one of 1, 2, 3, 4, 5, 6, 8, 16 sixteenths with an offset of up to a third, or a time in seconds
    // [DATA: TimeL 0.001 … 5]; Link = the right side follows the left
    const auto linked = manualBool (device, "DelayLine_Link", false);
    bool fixedMs = false;
    for (const auto right : { false, true })
    {
        const std::string side = right && ! linked ? "R" : "L";
        const auto synced = manualBool (device, "DelayLine_Sync" + side, true);
        const auto beats = 0.25 * delaySixteenths ((int) manual (device, "DelayLine_SyncedSixteenth" + side, 2))
                           * (1.0 + manual (device, "DelayLine_Offset" + side, 0.0));
        echoSide (out, right, synced, beats, manual (device, "DelayLine_Time" + side, 0.375) * 1000.0, context, fixedMs);
    }
    if (fixedMs)
        out.notes.approximated.push_back (text ("a synced time with no note value in Echoform (5 sixteenths or an offset): held in ms at %.4g BPM", context.bpm));
    put ("Feedback", feedback, [] (double v) { return unit (v / 2.0); }, 0.5); // [DATA 0 … 0.95] → loop gain 0 … 2
    p[freeze] = onOff (manualBool (device, "Freeze", false));
    if (manualBool (device, "Filter_On", false))
    {
        echoBandPass (out, manual (device, "Filter_Frequency", 1000.0), manual (device, "Filter_Bandwidth", 8.0));
        out.notes.approximated.push_back ("filter (the band-pass as a low cut and a high cut)");
    }
    else
    {
        p[lowCut] = 0.0f;
        p[highCut] = 1.0f;
    }
    p[modRate] = modRateValue (manual (device, "Modulation_Frequency", 0.5));
    const auto timeMod = manual (device, "Modulation_AmountTime", 0.0);
    p[modDepth] = unit (timeMod);
    if (timeMod > 1.0e-4)
        out.notes.approximated.push_back ("time modulation depth (fitted)");
    if (manual (device, "Modulation_AmountFilter", 0.0) > 1.0e-4)
        out.notes.dropped.push_back ("filter modulation");
    // DryWetMode [DATA 0 / 1; ASSUMED 1 = Equal-loudness, the option of the Dry/Wet control's menu]
    const auto equalLoudness = (int) device.number ("DryWetMode", 1) == 1;
    put ("DryWet", mix, [equalLoudness] (double v) { return echoformMixOf (v, equalLoudness); }, 0.5);
    if (! equalLoudness)
        out.notes.approximated.push_back ("linear dry/wet (Echoform mixes at equal power: the same balance, up to 3 dB louder)");
    if ((int) manual (device, "DelayLine_SmoothingMode", 0) != 0)
        out.notes.dropped.push_back ("transition mode (how a time change sounds)");
    return true;
}

bool echoToEchoform (const Element& device, const Context& context, Translated& out)
{
    using namespace echosuite::echoform;
    if (! has (device, "Feedback") || ! has (device, "Delay_TimeL"))
        return false;
    out.slot = makeEffectSlot (EffectKind::echoform);
    auto& p = out.slot.params;
    const Put put { device, out };
    p[mode] = modeValue (Mode::clean);
    // ChannelMode [DATA 0 … 2; DOC: Stereo, Ping Pong, Mid/Side]
    const auto channelMode = (int) manual (device, "ChannelMode", 0);
    p[style] = styleValue (channelMode == 1 ? Style::pingPong : Style::dual);
    if (channelMode == 2)
        out.notes.dropped.push_back ("Mid/Side mode (the two lines stay left and right)");
    // each side: Sync + a mode [DATA 0 … 3; strings: Notes, Triplet, Dotted, 16th; order ASSUMED] with a division
    // [DATA −6 … 0 = 1/64 … 1/1] or 1 … 16 sixteenths, an offset of up to a third; or a time in seconds
    const auto linked = manualBool (device, "Delay_TimeLink", false);
    bool fixedMs = false;
    for (const auto right : { false, true })
    {
        const std::string side = right && ! linked ? "R" : "L";
        const auto synced = manualBool (device, "Delay_Sync" + side, true);
        const auto syncMode = (int) manual (device, "Delay_SyncMode" + side, 0);
        auto beats = 4.0 * std::pow (2.0, manual (device, "Delay_SyncedDivision" + side, -3.0));
        if (syncMode == 1)
            beats *= 2.0 / 3.0;
        else if (syncMode == 2)
            beats *= 1.5;
        else if (syncMode == 3)
            beats = 0.25 * manual (device, "Delay_SyncedSixteenth" + side, 3.0);
        beats *= 1.0 + manual (device, "Delay_Offset" + side, 0.0);
        echoSide (out, right, synced, beats, manual (device, "Delay_Time" + side, 0.375) * 1000.0, context, fixedMs);
    }
    if (fixedMs)
        out.notes.approximated.push_back (text ("a synced time with no note value in Echoform: held in ms at %.4g BPM", context.bpm));
    put ("Feedback", feedback, [] (double v) { return unit (v / 2.0); }, 0.5); // [DATA 0 … 1.5]
    if (manualBool (device, "FeedbackInvert", false))
    {
        p[phaseL] = 1.0f;
        p[phaseR] = 1.0f;
        out.notes.approximated.push_back ("inverted feedback (the echoes are inverted; every second repeat differs)");
    }
    put ("InputGain", inputLevel, [] (double db) { return linValue (db, -24.0, 24.0); }, 0.0);
    put ("OutputGain", output, [] (double gain) { return linValue (dbOfGain (gain), -18.0, 18.0); }, 1.0);
    if (manualBool (device, "Filter_On", false))
    {
        p[lowCut] = linValue (manual (device, "Filter_HighPassFrequency", 50.0), 10.0, 2000.0);
        p[highCut] = linValue (manual (device, "Filter_LowPassFrequency", 5000.0), 200.0, 20000.0);
        if (manual (device, "Filter_HighPassResonance", 0.0) > 0.01 || manual (device, "Filter_LowPassResonance", 0.0) > 0.01)
            out.notes.dropped.push_back ("filter resonance");
    }
    else
    {
        p[lowCut] = 0.0f;
        p[highCut] = 1.0f;
    }
    const auto timeMod = manual (device, "Modulation_AmountDelay", 0.0);
    p[modRate] = modRateValue (manual (device, "Modulation_Frequency", 2.0));
    p[modDepth] = unit (timeMod);
    if (timeMod > 1.0e-4)
        out.notes.approximated.push_back ("time modulation (rate in Hz, depth fitted)");
    if (manual (device, "Modulation_AmountFilter", 0.0) > 1.0e-4)
        out.notes.dropped.push_back ("filter modulation");
    if (manual (device, "Modulation_EnvelopeMix", 0.0) > 0.03)
        out.notes.dropped.push_back ("envelope follower in the modulation");
    if (manual (device, "Reverb_Level", 0.0) > 1.0e-4)
        out.notes.dropped.push_back ("Reverb");
    if (manualBool (device, "Noise_On", false) && manual (device, "Noise_Amount", 0.0) > 1.0e-4)
        out.notes.dropped.push_back ("Noise");
    if (manualBool (device, "Wobble_On", false) && manual (device, "Wobble_Amount", 0.0) > 1.0e-4)
        out.notes.dropped.push_back ("Wobble");
    if (manualBool (device, "Gate_On", false))
        out.notes.dropped.push_back ("Gate");
    if (manualBool (device, "Ducking_On", false))
        out.notes.dropped.push_back ("Ducking (Echoform's ducking is one amount, not a threshold and a release)");
    if (std::abs (manual (device, "StereoWidth", 1.0) - 1.0) > 0.01)
        out.notes.dropped.push_back (text ("Stereo width %.0f %%", manual (device, "StereoWidth", 1.0) * 100.0));
    const auto equalLoudness = (int) device.number ("DryWetMode", 1) == 1;
    put ("DryWet", mix, [equalLoudness] (double v) { return echoformMixOf (v, equalLoudness); }, 0.5);
    if (! equalLoudness)
        out.notes.approximated.push_back ("linear dry/wet (Echoform mixes at equal power: the same balance, up to 3 dB louder)");
    return true;
}

// ================================================================================================ Multiband Dynamics

bool multibandToPrism (const Element& device, Translated& out)
{
    using namespace dyn::prism;
    if (! has (device, "SplitLowMid") || ! has (device, "AboveThresholdMid"))
        return false;
    out.slot = makeEffectSlot (EffectKind::prismMultiband);
    auto& p = out.slot.params;
    const Put put { device, out };
    // the bands in use: Low and High exist only while their split is on [DATA: SplitLowMidOn / SplitMidHighOn]
    std::vector<const char*> names;
    if (manualBool (device, "SplitLowMidOn", true))
        names.push_back ("Low");
    names.push_back ("Mid");
    if (manualBool (device, "SplitMidHighOn", true))
        names.push_back ("High");
    p[bandCountParam] = bandCountValue ((int) names.size());
    int k = 0;
    if (manualBool (device, "SplitLowMidOn", true))
        put ("SplitLowMid", crossoverParam (k++), [] (double hz) { return crossoverValue (hz); }, 120.0);
    if (manualBool (device, "SplitMidHighOn", true))
        put ("SplitMidHigh", crossoverParam (k++), [] (double hz) { return crossoverValue (hz); }, 2500.0);

    // Amount scales every band's gain change, Time every attack and release [DOC]
    const auto amount = std::clamp (manual (device, "GlobalAmount", 1.0), 0.0, 1.0);
    const auto timeScale = manual (device, "GlobalTime", 1.0);
    const auto softKnee = device.boolean ("SoftKnee", true);
    bool bothActions = false, clampedTimes = false;
    for (std::size_t b = 0; b < names.size(); ++b)
    {
        const std::string n = names[b];
        const auto band = (int) b;
        // the band's input gain comes before its detector [ASSUMED]: the same as a threshold lower by it and a level
        // higher by it
        const auto in = manual (device, "InputGain" + n, 0.0);
        const auto above = prismAbove (manual (device, "AboveRatio" + n, 0.0) * amount);
        const auto below = prismBelow (manual (device, "BelowRatio" + n, 0.0) * amount);
        const auto& action = above.active || ! below.active ? above : below;
        const auto useBelow = ! above.active && below.active;
        bothActions = bothActions || (above.active && below.active);
        put ((useBelow ? "BelowThreshold" : "AboveThreshold") + n, param (band, fThreshold),
             [in] (double db) { return thresholdValue (db - in); }, -20.0);
        p[(std::size_t) param (band, fRatio)] = action.limit ? 1.0f : dyn::crest::ratioValue (action.ratio);
        p[(std::size_t) param (band, fMode)] = onOff (action.expand);
        p[(std::size_t) param (band, fRange)] = ! action.active ? 0.5f : (action.upward ? 1.0f : 0.0f); // ±30 dB, 0 = none
        p[(std::size_t) param (band, fKnee)] = softKnee ? linValue (6.0, 0.0, 48.0) : 0.0f; // [ASSUMED 6 dB]
        const auto attack = manual (device, "Attack" + n, 20.0) * timeScale, release = manual (device, "Release" + n, 200.0) * timeScale;
        p[(std::size_t) param (band, fAttack)] = prismAttackValue (attack);
        p[(std::size_t) param (band, fRelease)] = prismReleaseValue (release);
        clampedTimes = clampedTimes || attack > 89.2 || release < 129.0 || release > 3631.0;
        put ("Gain" + n, param (band, fLevel), [in] (double db) { return linValue (db + in, -30.0, 30.0); }, 0.0);
        p[(std::size_t) param (band, fEnabled)] = onOff (manualBool (device, "Active" + n, true));
        p[(std::size_t) param (band, fSolo)] = onOff (manualBool (device, "Solo" + n, false));
    }
    put ("OutputGain", output, [] (double db) { return linValue (db, -36.0, 36.0); }, 0.0);
    p[mix] = 1.0f;
    out.side = sideInputOf (device);
    out.sideParam = scExternal;
    if (out.side.on)
        p[scExternal] = out.side.trackId >= 0 ? 1.0f : 0.0f;
    if (bothActions)
        out.notes.dropped.push_back ("a band's below-threshold action next to its above-threshold one (a Prism band holds one)");
    if (clampedTimes)
        out.notes.approximated.push_back ("attack / release beyond Prism's range (attack to 89 ms, release 130 … 3630 ms)");
    if (! device.boolean ("EnvelopeIsPeak", false))
        out.notes.approximated.push_back ("RMS detection (Prism follows the peak level)");
    return true;
}

// ================================================================================================ Auto Filter

bool autoFilterToFilter (const Element& device, Translated& out)
{
    const auto newer = device.name() == "AutoFilter2";
    if (! has (device, newer ? "Filter_Frequency" : "Cutoff"))
        return false;
    // the filter type [DOC: low-pass, high-pass, band-pass, notch, morph, …; DATA old 0 … 4, new 0 … 9]: LYNX's Filter has
    // the first three; (round 190fm, autoFilterPlan) the notch on Facet EQ, Morph at its nearest corner, DJ as a low / high
    // pass; the rest (comb, resampling, notch + low pass, vowel) stay listed
    const auto liveType = (int) manual (device, newer ? "Filter_Type" : "FilterType", 0);
    const auto morph = manual (device, newer ? "Filter_Morph" : "Morph", 0.0);
    const auto plan = autoFilterPlan (liveType, newer, morph, newer ? manual (device, "Filter_DjControl", 0.0) : 0.0);
    if (plan.type < 0)
        return false;
    if (liveType == 4)
        out.notes.approximated.push_back (text (plan.exact ? "Morph %.2f at its corner, the " : "Morph %.2f as its nearest corner, the ", morph)
                                          + (plan.type == 0 ? "low pass" : plan.type == 1 ? "high pass" : plan.type == 2 ? "band pass" : "notch"));
    else if (newer && std::abs (morph) > 1.0e-3)
        out.notes.dropped.push_back (text ("Morph %.2f on the ", morph) + plan.name + " filter (needs a live check)");
    if (plan.type == 3)
    {
        autoFilterNotchToFacet (device, out);
        return true;
    }
    const auto type = plan.type;
    out.slot = makeEffectSlot (EffectKind::filter);
    auto& p = out.slot.params;
    const Put put { device, out };
    p[0] = filterTypeValue (type);
    if (plan.djHz > 0.0)
    {
        // [ASSUMED] the DJ type: the cutoff from DjControl, low pass below 0, high pass above
        put ("Filter_DjControl", 1, [type] (double c) { return filterCutoffValue (autoFilterDjHz (type == 0 ? std::min (c, 0.0) : std::max (c, 0.0))); }, 0.0);
        out.notes.approximated.push_back (text ("DJ filter %.2f as a ", manual (device, "Filter_DjControl", 0.0)) + (type == 0 ? "low" : "high")
                                          + text (" pass at %.0f Hz", plan.djHz));
    }
    else if (newer)
        put ("Filter_Frequency", 1, [] (double hz) { return filterCutoffValue (hz); }, 1000.0); // Hz [DATA 20 … 20000]
    else
        put ("Cutoff", 1, [] (double note) { return filterCutoffValue (hzOfLiveNote (note)); }, 127.0); // a pitch [DATA 20 … 135]
    // resonance → Q [MEASURED 190cz: both tags are cookbook biquads at the cutoff, Q = 0.5 + 2x / (1 − x); low, high and
    // band pass share it; Q does not depend on the cutoff]
    const auto legacy = ! newer;
    put (newer ? "Filter_Resonance" : "Resonance", 2, [legacy] (double r) { return filterQValue (filterQOfResonance (r, legacy)); }, 0.0);
    if (newer)
        put ("DryWet", 3, [] (double v) { return unit (v); }, 1.0);
    else
        p[3] = 1.0f;
    const auto resonance = manual (device, newer ? "Filter_Resonance" : "Resonance", 0.0);
    const auto q = filterQOfResonance (resonance, legacy);
    if (q > 12.005)
        out.notes.approximated.push_back (text ("resonance %.2f as Q 12 (Live: Q %.1f; the Filter reaches 12)", resonance, q));
    // the band pass: Live's peaks at its Q (the cookbook "constant skirt" form, +7.92 dB at 0.5), the Filter's at 0 dB
    // [MEASURED 190cz a07 / b07]
    if (type == 2)
        out.notes.approximated.push_back (text ("band pass %.1f dB lower (Live's band pass peaks at its Q, the Filter's at 0 dB)", dbOfGain (std::min (q, 12.0))));
    // Output [MEASURED 190cw r18, 190cz a05: a linear gain, 0.5 = −6.02 dB, 0.25 = −12.04 dB at every frequency; DATA: 1
    // in every saved device]
    const auto outputGain = newer ? manual (device, "Output", 1.0) : 1.0;
    if (std::abs (outputGain - 1.0) > 1.0e-3)
        out.notes.dropped.push_back (text ("output gain %.1f dB", dbOfGain (outputGain)));
    // (190ed) the LFO, the envelope follower and its side chain: autoFilterModulation (lot 3) — a Peak Controller linked
    // to the cutoff, or the importer's line where no link can be made
    // (round 190fm) the drive and the circuit: autoFilterDrive (lot 4), after the modulation
    // Slope [MEASURED 190cz a08 / a30–a33 / b08 / b30–b33: on = a second cookbook stage at the same cutoff with Q2 =
    // 0.5 + x / (1 + x), 0.003 dB] → the Filter's own 24 dB/oct slope (the same pair, filterSecondStageQ); high and
    // band pass at 24 dB/oct take the same law [ASSUMED: only the low pass was rendered]
    const auto steep = newer ? manual (device, "Filter_Slope", 0.0) > 0.5 : manualBool (device, "Slope", false);
    p[4] = steep ? 1.0f : 0.0f;
    if (steep && type != 0)
        out.notes.approximated.push_back (type == 1 ? "24 dB/oct high pass (Live's measured on the low pass)" : "24 dB/oct band pass (Live's measured on the low pass)");
    return true;
}

// ================================================================================================ Saturator, Overdrive

const SaturatorFit& saturatorFit (int type) noexcept
{
    // [MEASURED 190cz x00–x71 vs Ember's own curves, round 190da (ana/satfit.py, satgate.py): Live's static curve
    // (100 Hz 0 dBFS sine) at drive 0 and +12 dB — a memoryless curve after a gain, so any drive between follows — and
    // the levels of 0 dBFS / −12 dBFS sines. Per type the Ember curve (with Live's flat small-signal response),
    // drive and output offsets with the least curve error that is no worse than the old mapping in any of these
    // numbers at any drive 0 … 12 dB; Analog Clip alone has no such fit (the old tube curve is 4.8 dB too loud at
    // +12 dB, the fit below at most 0.7 dB off, but 0.6 dB louder than the old one on −12 dBFS signals at low drive).
    // residual errors in /mnt/e/lynx-work/als190cf/HANDOFF.md § Round 190da]
    static constexpr SaturatorFit fits[8] {
        { "Analog Clip", 2, 0, 0.5, 0.2 },   // transistor
        { "Soft Sine", 2, 0, 4.0, 0.2 },     // transistor
        { "Bass Shaper", 1, 0, -1.5, 0.1 },  // tape
        { "Medium Curve", 2, 0, 7.5, 0.1 },  // transistor
        { "Hard Curve", 1, 0, 17.0, 0.2 },   // tape
        { "Sinoid Fold", 0, 26, -6.5, 9.9 }, // the Wavefold character
        { "Digital Clip", 2, 0, 0.0, 0.0 },  // transistor, as before (no fit beat it in every number)
        { "Waveshaper", 1, 0, 2.0, -2.0 },   // tape
    };
    return fits[std::clamp (type, 0, 7)];
}

bool saturatorToEmber (const Element& device, Translated& out)
{
    using namespace mixsuite::ember;
    if (! has (device, "PreDrive") || ! has (device, "PostDrive"))
        return false;
    out.slot = makeEffectSlot (EffectKind::ember);
    auto& p = out.slot.params;
    const Put put { device, out };
    // Type [DATA 0 … 7; the order seen in Live 12.4's menu, round 190cw]: the Ember curve and the drive / output offsets
    // that fit Live's curve best (saturatorFit)
    const auto type = std::clamp ((int) manual (device, "Type", 0), 0, 7);
    const auto& fit = saturatorFit (type);
    static const char* const styleNames[4] { "tube", "tape", "transistor", "fuzz" };
    p[style] = (float) fit.style / (float) (styles - 1);
    // round 190fj: the fits above were made on the Classic tape curve (Bass Shaper, Hard Curve); the measured magnetic
    // core is the reference tape saturator's law, not Live's static curves
    p[(std::size_t) mixsuite::ember::law] = 0.0f;
    if (fit.character > 0)
        p[(std::size_t) characterParam (0)] = (float) fit.character / (float) characters;
    out.notes.approximated.push_back (std::string ("curve ") + fit.name + " as Ember's "
                                      + (fit.character > 0 ? std::string (characterName (fit.character)) + " character" : std::string (styleNames[fit.style])));
    // Drive [DATA −36 … 36 dB; MEASURED 190cz: a gain in dB before the curve] + the type's offset → Ember's drive (0 …
    // 36 dB); what is under 0 dB is its input gain
    const auto driveDb = manual (device, "PreDrive", 0.0) + fit.driveDb;
    const auto offset = fit.driveDb;
    put ("PreDrive", drive, [offset] (double db) { return linValue (db + offset, 0.0, 36.0); }, 0.0);
    if (driveDb < 0.0)
        p[inputGain] = linValue (driveDb, -24.0, 24.0);
    const auto outputDb = fit.outputDb;
    put ("PostDrive", output, [outputDb] (double db) { return linValue (db + outputDb, -18.0, 18.0); }, 0.0); // [DATA −36 … 0 dB]
    put ("DryWet", mix, [] (double v) { return unit (v); }, 1.0);
    p[autoGain] = 0.0f; // Live's device has no level compensation
    if (manualBool (device, "ColorOn", false) && (std::abs (manual (device, "BaseDrive", 0.0)) > 0.05 || std::abs (manual (device, "ColorDepth", 0.0)) > 0.05))
        out.notes.dropped.push_back ("Color (the two shaping filters)");
    if ((int) manual (device, "PostClip", 0) != 0)
        out.notes.dropped.push_back ("output clip");
    if (manualBool (device, "PreDcFilter", false))
        out.notes.dropped.push_back ("DC filter");
    if (driveDb < -24.0)
        out.notes.approximated.push_back (text ("drive %.1f dB (Ember's input: down to -24 dB)", driveDb - offset));
    if (driveDb > 36.0)
        out.notes.approximated.push_back (text ("drive %.1f dB (Ember's drive: up to %.1f dB for this curve)", driveDb - offset, 36.0 - offset));
    return true;
}

bool overdriveToDistortion (const Element& device, Translated& out)
{
    if (! has (device, "Drive") || ! has (device, "MidFreq"))
        return false;
    out.slot = makeEffectSlot (EffectKind::distortion);
    const Put put { device, out };
    // Drive, Tone and Dry/Wet are percentages [DATA 0 … 100]; their laws inside Live's device are not known [ASSUMED:
    // straight onto the Distortion's 0 … 36 dB and 800 Hz … 20 kHz]
    put ("Drive", 0, [] (double v) { return unit (v / 100.0); }, 50.0);
    put ("Tone", 1, [] (double v) { return unit (v / 100.0); }, 50.0);
    put ("DryWet", 2, [] (double v) { return unit (v / 100.0); }, 50.0);
    out.slot.params[3] = 0.5f;
    if (manual (device, "BandWidth", 9.0) < 8.99)
        out.notes.dropped.push_back (text ("band-pass before the drive (%.0f Hz, width %.1f)", manual (device, "MidFreq", 1000.0), manual (device, "BandWidth", 9.0)));
    if (std::abs (manual (device, "PreserveDynamics", 0.5) - 0.5) > 0.01)
        out.notes.dropped.push_back ("Dynamics");
    return true;
}

// ================================================================================================ Limiter, Gate, Expand

bool limiterToApex (const Element& device, Translated& out)
{
    using namespace apex;
    if (! has (device, "Ceiling") || ! has (device, "Gain"))
        return false;
    out.slot = makeEffectSlot (EffectKind::apexLimiter);
    auto& p = out.slot.params;
    const Put put { device, out };
    const auto gain = manual (device, "Gain", 0.0);
    put ("Gain", pGain, [] (double db) { return linValue (db, 0.0, 30.0); }, 0.0);           // [DATA −24 … 24 dB]
    if (gain < -0.05)
        out.notes.dropped.push_back (text ("gain %.1f dB (Apex's gain starts at 0 dB)", gain));
    put ("Ceiling", pCeiling, [] (double db) { return linValue (db, -30.0, 0.0); }, -0.3);   // [DATA −24 … 0 dB]
    put ("Release", pRelease, [] (double ms) { return releaseValue (ms); }, 300.0);          // ms [DATA 0.01 … 3000]
    const auto release = manual (device, "Release", 300.0);
    if (release > 1000.5 || release < 0.999)
        out.notes.approximated.push_back (text ("release %.0f ms (Apex: 1 … 1000 ms)", release));
    p[pStyle] = 0.0f; // Clean
    if (manualBool (device, "AutoRelease", false))
        out.notes.approximated.push_back ("auto release (Apex's own two-stage release at the set time)");
    const auto la = limiterLookaheadMs ((int) manual (device, "Lookahead", 1));
    p[pLookahead] = linValue (la, 0.0, maxLookaheadMs);
    if (la > maxLookaheadMs)
        out.notes.approximated.push_back (text ("look-ahead %.1f ms (Apex: up to 5 ms)", la));
    // Mode [DATA 0 … 2; strings: Soft Clip, True Peak; ASSUMED Standard, Soft Clip, True Peak]
    const auto limiterMode = (int) manual (device, "Mode", 0);
    p[pTruePeak] = onOff (limiterMode == 2);
    if (limiterMode == 1)
        out.notes.dropped.push_back ("Soft Clip mode");
    p[pLink] = unit (manual (device, "LinkAmount", 1.0));
    if ((int) manual (device, "Routing", 0) != 0)
        out.notes.dropped.push_back ("Mid/Side routing");
    if (manualBool (device, "Maximize", false))
        out.notes.dropped.push_back ("Maximize");
    return true;
}

bool gateToSentry (const Element& device, Translated& out)
{
    using namespace dyn::sentry;
    if (! has (device, "Threshold") || ! has (device, "Hold") || ! has (device, "Return"))
        return false;
    out.slot = makeEffectSlot (EffectKind::sentryGate);
    auto& p = out.slot.params;
    const Put put { device, out };
    p[style] = styleValue (Style::classic);
    // Threshold: a linear gain [DATA 0.000316 … 1.995] → −60 … 0 dB
    const auto thresholdDbLive = dbOfGain (manual (device, "Threshold", 0.01));
    put ("Threshold", threshold, [] (double gain) { return linValue (dbOfGain (gain), -60.0, 0.0); }, 0.01);
    if (thresholdDbLive > 0.05 || thresholdDbLive < -60.05)
        out.notes.approximated.push_back (text ("threshold %.1f dB (Sentry: -60 … 0 dB)", thresholdDbLive));
    p[ratio] = 1.0f; // a gate: everything under the threshold goes down to the floor
    put ("Attack", attack, [] (double ms) { return sentryAttackValue (ms); }, 0.02);   // ms [DATA 0.02 … 150]
    put ("Hold", hold, [] (double ms) { return sentryHoldValue (ms); }, 20.0);         // ms [DATA 1 … 1500]
    put ("Release", release, [] (double ms) { return sentryReleaseValue (ms); }, 100.0); // ms [DATA 0.1 … 3000]
    if (manual (device, "Hold", 20.0) > 250.5)
        out.notes.approximated.push_back (text ("hold %.0f ms (Sentry: up to 250 ms)", manual (device, "Hold", 20.0)));
    // Floor [DATA "Gain" −75 … 0 dB; Live 12.4 shows −75 as "−inf dB", round 190cw] → Sentry's range 0 … 100 dB; the
    // bottom is a full gate: Sentry's deepest floor (−100 dB)
    put ("Gain", range, [] (double db) { return db <= -74.95 ? 1.0f : linValue (-db, 0.0, 100.0); }, -40.0);
    p[knee] = 0.0f;
    out.notes.approximated.push_back (text ("Return %.1f dB (Sentry's Classic style has its own hysteresis)", manual (device, "Return", 0.0)));
    const auto la = gateLookaheadMs ((int) manual (device, "LookAhead", 1));
    p[lookahead] = linValue (la, 0.0, 10.0);
    p[lookaheadOn] = onOff (la > 0.0);
    p[ducking] = onOff ((int) manual (device, "FlipMode", 0) != 0); // Flip: the gate lets through what is under the threshold
    p[scListen] = onOff (device.boolean ("SideListen/Manual", false));
    sentrySideChain (device, out);
    return true;
}

bool expanderToSentry (const Element& device, Translated& out)
{
    using namespace dyn::sentry;
    if (! has (device, "Threshold") || ! has (device, "ExpansionRatio") || (int) manual (device, "Model", 0) != 2)
        return false;
    out.slot = makeEffectSlot (EffectKind::sentryGate);
    auto& p = out.slot.params;
    const Put put { device, out };
    // Expand [DOC]: what is over the threshold comes out (ratio) dB louder per dB — Sentry's Upward style lifts by
    // (ratio − 1) dB per dB over its upward threshold, the same curve
    p[style] = styleValue (Style::upward);
    const auto thresholdDbLive = dbOfGain (manual (device, "Threshold", 1.0));
    put ("Threshold", upThreshold, [] (double gain) { return linValue (dbOfGain (gain), -30.0, 0.0); }, 1.0);
    if (thresholdDbLive > 0.05 || thresholdDbLive < -30.05)
        out.notes.approximated.push_back (text ("threshold %.1f dB (Sentry's upward threshold: -30 … 0 dB)", thresholdDbLive));
    put ("ExpansionRatio", upRatio, [] (double r) { return linValue (r, 1.0, 3.0); }, 1.15); // [DATA 1 … 2]
    put ("Attack", attack, [] (double ms) { return sentryAttackValue (ms); }, 2.0);
    put ("Release", release, [] (double ms) { return sentryReleaseValue (ms); }, 50.0);
    put ("Knee", knee, [] (double db) { return linValue (db, 0.0, 30.0); }, 6.0);
    put ("Gain", output, [] (double db) { return linValue (db, -24.0, 24.0); }, 0.0);
    const auto la = compressorLookaheadMs ((int) manual (device, "LookAhead", 0));
    p[lookahead] = linValue (la, 0.0, 10.0);
    p[lookaheadOn] = onOff (la > 0.0);
    if (std::abs (manual (device, "DryWet", 1.0) - 1.0) > 1.0e-6)
        out.notes.dropped.push_back ("dry/wet");
    if (manualBool (device, "GainCompensation", false))
        out.notes.dropped.push_back ("makeup");
    out.notes.approximated.push_back ("how fast the lifted gain falls back (Sentry's Upward style has its own release)");
    sentrySideChain (device, out);
    return true;
}

// ================================================================================================ Channel EQ, Chorus

bool channelEqToEq (const Element& device, Translated& out)
{
    if (! has (device, "LowShelfGain") || ! has (device, "MidFrequency"))
        return false;
    out.slot = makeEffectSlot (EffectKind::eq);
    const Put put { device, out };
    // the gains are linear factors [DATA: 0.18 … 5.6 = ±15 dB, mid 0.25 … 4 = ±12 dB] → the EQ's ±18 dB
    const auto gainLaw = [] (double gain) { return eqGainValue (dbOfGain (gain)); };
    put ("LowShelfGain", 0, gainLaw, 1.0);
    put ("MidGain", 1, gainLaw, 1.0);
    put ("MidFrequency", 2, [] (double hz) { return eqMidValue (hz); }, 1500.0); // Hz [DATA 120 … 7500]
    put ("HighShelfGain", 3, gainLaw, 1.0);
    put ("Gain", 4, gainLaw, 1.0);
    const auto mid = manual (device, "MidFrequency", 1500.0);
    if (mid < 199.0 || mid > 5005.0)
        out.notes.approximated.push_back (text ("mid frequency %.0f Hz (EQ: 200 Hz … 5 kHz)", mid));
    if (manualBool (device, "HighpassOn", false))
        out.notes.dropped.push_back ("80 Hz high pass");
    return true;
}

bool chorusToChorus (const Element& device, Translated& out)
{
    const auto newer = device.name() == "Chorus2";
    if (! has (device, newer ? "Rate" : "ModFreq") || ! has (device, "DryWet"))
        return false;
    out.slot = makeEffectSlot (EffectKind::chorus);
    auto& p = out.slot.params;
    const Put put { device, out };
    if (newer)
    {
        put ("Rate", 0, [] (double hz) { return chorusRateValue (hz); }, 0.9);          // Hz [DATA 0.1 … 15]
        // Amount is a share [DATA 0 … 1] of a depth Live does not store [ASSUMED: all of it = 4 ms]
        put ("Amount", 1, [] (double v) { return unit (v * 0.5); }, 0.5);
        put ("DryWet", 2, [] (double v) { return unit (v); }, 0.5);
        put ("Feedback", 3, [] (double v) { return unit (v); }, 0.0);
        // Mode [DATA 0 … 2; DOC: Classic, Ensemble, Vibrato]
        const auto chorusMode = (int) manual (device, "Mode", 0);
        if (chorusMode == 1)
            out.notes.approximated.push_back ("Ensemble mode (three voices) as a chorus");
        if (chorusMode == 2)
        {
            p[2] = 1.0f;
            out.links.erase (std::remove_if (out.links.begin(), out.links.end(), [] (const ParamLink& l) { return l.live == "DryWet"; }), out.links.end());
            out.notes.approximated.push_back ("Vibrato mode as a chorus heard wet only");
        }
        if (manual (device, "Rate", 0.9) > 5.0)
            out.notes.approximated.push_back (text ("rate %.1f Hz (Chorus: up to 5 Hz)", manual (device, "Rate", 0.9)));
        if (manualBool (device, "InvertFeedback", false) && manual (device, "Feedback", 0.0) > 1.0e-4)
            out.notes.dropped.push_back ("inverted feedback");
        if (std::abs (manual (device, "Width", 1.0) - 1.0) > 0.01)
            out.notes.dropped.push_back (text ("Width %.0f %%", manual (device, "Width", 1.0) * 100.0));
        if (manual (device, "Warmth", 0.0) > 0.01)
            out.notes.dropped.push_back ("Warmth");
        if (manualBool (device, "HighpassEnabled", false))
            out.notes.dropped.push_back ("high pass on the chorus signal");
        if (std::abs (manual (device, "OutputGain", 1.0) - 1.0) > 0.01)
            out.notes.dropped.push_back (text ("output gain %.1f dB", dbOfGain (manual (device, "OutputGain", 1.0))));
    }
    else
    {
        put ("ModFreq", 0, [] (double hz) { return chorusRateValue (hz); }, 1.0);       // Hz [DATA 0.03 … 10]
        put ("ModAmount", 1, [] (double ms) { return linValue (ms, 0.0, 8.0); }, 1.0);   // ms [DATA 0 … 6.5]
        put ("DryWet", 2, [] (double v) { return unit (v); }, 0.5);
        put ("Feedback", 3, [] (double v) { return unit (v); }, 0.0);
        out.notes.dropped.push_back ("the two delay times and their high pass (the Chorus has its own delay)");
        if ((int) manual (device, "FeedbackSign", 1) == 0 && manual (device, "Feedback", 0.0) > 1.0e-4)
            out.notes.dropped.push_back ("inverted feedback");
    }
    return true;
}

// ================================================================================================ Auto Pan, Redux

bool autoPanToAutoPan (const Element& device, const Context& context, Translated& out)
{
    if (! has (device, "Lfo/Frequency") || ! has (device, "Lfo/LfoAmount"))
        return false;
    // Phase [DATA 0 … 360°]: the right channel's LFO against the left's. 180° is a panner; near 0° / 360° both sides
    // move together (a tremolo), which LYNX's Auto-pan cannot do: the device then stays untranslated
    const auto phase = manual (device, "Lfo/Phase", 180.0);
    if (std::abs (phase - 180.0) > 1.0)
        return false;
    out.slot = makeEffectSlot (EffectKind::autoPan);
    auto& p = out.slot.params;
    const Put put { device, out };
    // RateType [DATA 0 / 1; 1 where the synced rate was changed: 0 = Hz, 1 = synced]
    const auto synced = (int) manual (device, "Lfo/RateType", 0) == 1;
    if (synced)
    {
        p[autopan::source] = 0.0f; // the song-locked LFO: a period in beats (v · 64)
        p[0] = unit (liveBeatRateBeats ((int) manual (device, "Lfo/BeatRate", 4)) / 64.0);
    }
    else
    {
        p[autopan::source] = 0.5f; // the free LFO: a rate in Hz
        put ("Lfo/Frequency", autopan::rate, [] (double hz) { return autopan::rateValue (hz); }, 1.0); // Hz [DATA 0.05 … 90]
        if (manual (device, "Lfo/Frequency", 1.0) > 10.0)
            out.notes.approximated.push_back (text ("rate %.1f Hz (Auto-pan: up to 10 Hz)", manual (device, "Lfo/Frequency", 1.0)));
        p[0] = unit (std::max (context.bpm, 1.0) / 60.0 / std::max (manual (device, "Lfo/Frequency", 1.0), 0.01) / 64.0);
    }
    put ("Lfo/LfoAmount", 1, [] (double v) { return unit (0.5 + 0.5 * v); }, 1.0); // depth: ±100 % around the centre
    p[2] = 0.0f;
    p[3] = 0.5f;
    out.notes.approximated.push_back ("LFO shape (Auto-pan moves in a triangle)");
    if (manual (device, "Lfo/Offset", 0.0) > 0.5)
        out.notes.dropped.push_back ("LFO offset");
    if ((int) manual (device, "Lfo/StereoMode", 0) != 0)
        out.notes.dropped.push_back ("Spin");
    if (manualBool (device, "Lfo/LfoInvert", false))
        out.notes.dropped.push_back ("Invert");
    return true;
}

bool reduxToLofi (const Element& device, Translated& out)
{
    if (! has (device, "SampleRate") || ! has (device, "BitDepth"))
        return false;
    out.slot = makeEffectSlot (EffectKind::lofi);
    const Put put { device, out };
    put ("SampleRate", 0, [] (double hz) { return lofiRateValue (hz); }, 40000.0); // Hz [DATA 20 … 40000]
    put ("BitDepth", 1, [] (double bits) { return lofiBitsValue (bits); }, 16.0);  // bits [DATA 1 … 16]
    put ("DryWet", 2, [] (double v) { return unit (v); }, 1.0);
    const auto rate = manual (device, "SampleRate", 40000.0);
    if (rate < 43000.0)
        out.notes.approximated.push_back (text ("sample rate %.0f Hz (Lo-fi holds whole samples: 44100 / n Hz, down to 342 Hz)", rate));
    if (manual (device, "BitDepth", 16.0) < 5.0)
        out.notes.approximated.push_back (text ("%.0f bits (Lo-fi: down to 5 bits)", manual (device, "BitDepth", 16.0)));
    if (manual (device, "Jitter", 0.0) > 1.0e-4)
        out.notes.dropped.push_back ("Jitter");
    if (manual (device, "QuantizerShape", 0.0) > 1.0e-4 || device.boolean ("QuantizerDcShift", false))
        out.notes.dropped.push_back ("quantiser shape / DC shift");
    if (device.boolean ("EnablePreFilter", false) || device.boolean ("EnablePostFilter", false))
        out.notes.dropped.push_back ("pre / post filter");
    return true;
}

// ================================================================================================ MIDI effects

bool arpeggiatorToArp (const Element& device, const Context& context, ArpSettings& arp, Notes& notes)
{
    if (! has (device, "Mode") && ! device.path ("Mode"))
        return false;
    const auto number = [&device] (const char* name, double fallback) {
        return has (device, name) ? manual (device, name, fallback) : device.number (name, fallback);
    };
    const auto flag = [&device] (const char* name) { return has (device, name) ? manualBool (device, name, false) : device.boolean (name, false); };
    const auto style = (int) number ("Mode", 0);
    if (style == 14) // Chord Trigger: repeats the held chord, no arpeggio
        return false;
    bool exact = true;
    arp = ArpSettings {};
    arp.direction = arpDirectionOf (style, exact);
    if (! exact)
        notes.approximated.push_back (text ("style %.0f (the nearest of up, down, up-down, random)", style));
    if (flag ("SyncState"))
        arp.stepBeats = (float) arpRateBeats ((int) number ("SyncedRate", 8));
    else
    {
        arp.stepBeats = (float) (number ("FreeRate", 100.0) * std::max (context.bpm, 1.0) / 60000.0); // ms [DATA 10 … 1000]
        notes.approximated.push_back (text ("free rate held in beats at %.4g BPM", context.bpm));
    }
    arp.gate = (float) std::clamp (number ("Gate", 50.0) / 100.0, 0.01, 128.0 / 48.0); // % of the step [DATA 1 … 200]
    // Steps × Distance [DATA 0 … 8, −24 … 24 st]: the pattern again, transposed; with an octave it is the range
    const auto steps = (int) number ("TransposeSteps", 0);
    const auto distance = (int) number ("TransposeDistance", 12);
    arp.range = 1;
    if (steps > 0)
    {
        arp.range = std::clamp (steps + 1, 1, 4);
        if (distance != 12 || steps > 3)
            notes.approximated.push_back (text ("%.0f steps of %+.0f semitones (the range is whole octaves upwards, up to 4)", steps, distance));
    }
    if ((int) number ("RepeatCount", 0) != 0)
        notes.dropped.push_back ("Repeats");
    if ((int) number ("Retrigger", 0) != 0)
        notes.dropped.push_back ("Retrigger");
    if (flag ("VelocitySwitch"))
        notes.dropped.push_back ("velocity decay");
    if (flag ("Hold"))
        notes.dropped.push_back ("Hold");
    if ((int) number ("PatternOffset", 0) != 0)
        notes.dropped.push_back ("Offset");
    if ((int) number ("Groove", 0) != 0)
        notes.dropped.push_back ("Groove");
    if ((int) number ("TransposeMode", 0) != 0)
        notes.dropped.push_back ("transposition scale");
    return true;
}

std::vector<NoteEdit> applyNoteEffect (const NoteEffect& effect, const NoteEdit& note)
{
    std::vector<NoteEdit> out;
    switch (effect.kind)
    {
        case NoteEffect::Kind::transpose:
        {
            auto n = note;
            n.key = note.key + effect.semitones;
            if (n.key >= 0 && n.key <= 127)
                out.push_back (n);
            break;
        }
        case NoteEffect::Kind::velocity:
        {
            auto n = note;
            const auto in = std::clamp ((double) note.velocity * 127.0, 1.0, 127.0);
            const auto lo = (double) effect.lowest, hi = (double) std::min (127, effect.lowest + effect.range);
            if (effect.mode == 1 && (in < lo - 0.5 || in > hi + 0.5))
                break; // Gate: notes outside the range are not played
            if (effect.mode == 2)
                n.velocity = (float) effect.outHigh / 127.0f; // Fixed
            else
            {
                const auto x = hi > lo ? (std::clamp (in, lo, hi) - lo) / (hi - lo) : 1.0;
                n.velocity = (float) std::clamp ((effect.outLow + x * (effect.outHigh - effect.outLow)) / 127.0, 1.0 / 127.0, 1.0);
            }
            out.push_back (n);
            break;
        }
        case NoteEffect::Kind::chord:
        {
            out.push_back (note);
            for (const auto& [semis, factor] : effect.chord)
            {
                auto n = note;
                n.key = note.key + semis;
                n.velocity = std::clamp (note.velocity * factor, 1.0f / 127.0f, 1.0f);
                const auto doubled = std::any_of (out.begin(), out.end(), [&n] (const NoteEdit& o) { return o.key == n.key; });
                if (n.key >= 0 && n.key <= 127 && ! doubled)
                    out.push_back (n);
            }
            break;
        }
        case NoteEffect::Kind::length:
        {
            auto n = note;
            n.lengthBeats = effect.lengthBeats;
            out.push_back (n);
            break;
        }
    }
    return out;
}

std::optional<NoteEffect> noteEffectFromLiveDevice (const Element& device, const Context& context, Notes& notes)
{
    const auto tag = device.name();
    NoteEffect e;
    if (tag == "MidiPitcher")
    {
        if (! has (device, "Pitch"))
            return std::nullopt;
        e.kind = NoteEffect::Kind::transpose;
        e.name = "Pitch";
        e.semitones = (int) std::lround (manual (device, "Pitch", 0.0)); // semitones [DATA −128 … 128]
        if (device.boolean ("UseCurrentScale", false) || (int) manual (device, "Lowest", 0) != 0 || (int) manual (device, "Range", 127) != 127)
            notes.dropped.push_back ("scale degrees / key range");
        return e;
    }
    if (tag == "MidiVelocity")
    {
        if (! has (device, "MaxOut") && ! has (device, "MinOut"))
            return std::nullopt;
        // Operation [DATA 0 … 2; DOC: Velocity, Release Velocity, Both; order ASSUMED]: release velocities are not touched
        if ((int) manual (device, "Operation", 0) == 1)
            return std::nullopt;
        e.kind = NoteEffect::Kind::velocity;
        e.name = "Velocity";
        e.mode = std::clamp ((int) manual (device, "Mode", 0), 0, 2); // [DOC: Clip, Gate, Fixed]
        e.lowest = std::clamp ((int) manual (device, "Lowest", 1), 0, 127);
        e.range = std::clamp ((int) manual (device, "Range", 127), 0, 127);
        e.outLow = std::clamp ((int) manual (device, "MinOut", 1), 0, 127);
        e.outHigh = std::clamp ((int) manual (device, "MaxOut", 127), 0, 127);
        if (std::abs (manual (device, "Drive", 0.0)) > 0.005 || std::abs (manual (device, "Compand", 0.0)) > 0.005)
            notes.dropped.push_back ("Drive / Compand curve (the velocities map in a straight line)");
        if (manual (device, "Random", 0.0) > 0.5)
            notes.dropped.push_back ("Random");
        return e;
    }
    if (tag == "MidiChord")
    {
        if (! has (device, "Shift1"))
            return std::nullopt;
        e.kind = NoteEffect::Kind::chord;
        e.name = "Chord";
        bool chance = false;
        for (int k = 1; k <= 6; ++k)
        {
            const auto n = std::to_string (k);
            const auto shift = (int) std::lround (manual (device, "Shift" + n, 0.0));
            if (shift == 0)
                continue;
            e.chord.push_back ({ shift, (float) std::clamp (manual (device, "Velocity" + n, 1.0), 0.01, 2.0) });
            chance = chance || manual (device, "Chance" + n, 1.0) < 0.999;
        }
        if (chance)
            notes.dropped.push_back ("Chance (every chord note always plays)");
        if (std::abs (manual (device, "Strum", 0.0)) > 0.5)
            notes.dropped.push_back ("Strum");
        if (device.boolean ("UseCurrentScale", false))
            notes.dropped.push_back ("scale degrees");
        return e;
    }
    if (tag == "MidiNoteLength")
    {
        // Mode [DATA a switch; DOC: the trigger, Note On or Note Off; ASSUMED off = Note On]. Only a free time on Note On
        // can be written into the notes
        if (! has (device, "TimeLength") || manualBool (device, "Mode", false) || manualBool (device, "SyncState", false))
            return std::nullopt;
        e.kind = NoteEffect::Kind::length;
        e.name = "Note Length";
        const auto ms = manual (device, "TimeLength", 100.0) * manual (device, "Gate", 100.0) / 100.0; // ms × Gate %
        e.lengthBeats = ms * std::max (context.bpm, 1.0) / 60000.0;
        notes.approximated.push_back (text ("length %.0f ms held in beats at %.4g BPM", ms, context.bpm));
        return e;
    }
    return std::nullopt;
}
} // namespace lynx::alsdev
