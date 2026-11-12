// Live's own devices → LYNX's, lot 3 (round 190ed; core/AlsDevices.h has the list and the evidence tags). The same
// rules as lots 1 and 2: each conversion is a law in a unit, what is fitted or has no counterpart becomes a note for the
// import report, only the device's saved state is read, and each translator records which Live parameter drives
// which LYNX parameter (ParamLink) so automation and rack macros follow. No Live render of these devices exists yet:
// every law that is not plain unit maths is tagged [ASSUMED] and the round's handoff lists the renders that settle it.

#include "core/AlsDevices.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/DynamicsSuite.h"
#include "core/Effects.h"
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

double drumBussDriveDb (double drive) noexcept { return 24.0 * std::clamp (drive, 0.0, 1.0); }
double drumBussBoomLfHz (double boomHz) noexcept { return std::clamp (boomHz, 30.0, 90.0) / 0.72; }

bool drumBussToRack (const Element& device, Translated& out)
{
    namespace em = mixsuite::ember;
    namespace sh = mixsuite::shell;
    if (! has (device, "DriveAmount") || ! has (device, "TransientShaping") || ! has (device, "BoomAmount"))
        return false;

    // Ember: input trim, drive (Soft / Medium / Hard [DOC order; DATA DriveType 0 … 2]), damping as its high cut, no
    // level compensation
    auto ember = makeEffectSlot (EffectKind::ember);
    auto& e = ember.params;
    static constexpr int styles[3] { 0, 1, 2 }; // tube, tape, transistor [ASSUMED: soft, medium, hard]
    const auto type = std::clamp ((int) manual (device, "DriveType", 0), 0, 2);
    e[em::style] = (float) styles[type] / (float) (em::styles - 1);
    e[em::autoGain] = 0.0f;
    e[em::drive] = linValue (drumBussDriveDb (manual (device, "DriveAmount", 0.2)), 0.0, 36.0);
    const auto trimDb = dbOfGain (manual (device, "InputTrim", 1.0));
    e[em::inputGain] = linValue (trimDb, -24.0, 24.0);
    // Damping [DATA 500 … 20000 Hz] → Ember's high cut (2 … 20 kHz; its wide range 1 … 20 kHz below that, off at 20 kHz)
    const auto damping = std::clamp (manual (device, "DampingFrequency", 20000.0), 500.0, 20000.0);
    const auto wide = damping < 2000.0;
    e[em::wideCuts] = wide ? 1.0f : 0.0f;
    e[em::highCut] = damping >= 19999.0 ? 1.0f : logValue (damping, 2000.0, 20000.0);
    e[em::highCutWide] = damping >= 19999.0 ? 1.0f : logValue (damping, 1000.0, 20000.0);
    e[em::mix] = 1.0f;

    // Shell Channel: the transient shaper (−1 … +1: Amount = |t|, Invert below 0), Boom as the low enhancer (its pitch =
    // the enhancer's corner, Amount as its amount), Crunch as the high enhancer, the Compressor as the listen-mic
    // compressor, the output gain
    auto shell = makeEffectSlot (EffectKind::shellChannel);
    auto& s = shell.params;
    const auto transients = std::clamp (manual (device, "TransientShaping", 0.0), -1.0, 1.0);
    s[sh::tsOn] = std::abs (transients) > 1.0e-4 ? 1.0f : 0.0f;
    s[sh::tsAmount] = (float) std::abs (transients);
    s[sh::tsInvert] = transients < 0.0 ? 1.0f : 0.0f;
    const auto boom = std::clamp (manual (device, "BoomAmount", 0.0), 0.0, 1.0);
    s[sh::lfOn] = boom > 1.0e-4 ? 1.0f : 0.0f;
    s[sh::lfFreq] = sh::lfValue (drumBussBoomLfHz (manual (device, "BoomFrequency", 50.0)));
    s[sh::lfAmount] = (float) boom;
    const auto crunch = std::clamp (manual (device, "CrunchAmount", 0.0), 0.0, 1.0);
    s[sh::hfOn] = crunch > 1.0e-4 ? 1.0f : 0.0f;
    s[sh::hfAmount] = (float) crunch;
    if (device.boolean ("EnableCompression/Manual", device.boolean ("EnableCompression", false)))
    {
        s[sh::lmcOn] = 1.0f;
        s[sh::lmcComp] = 0.25f; // [ASSUMED: Live's fixed drum compressor as a light listen-mic compression]
    }
    const auto outDb = dbOfGain (manual (device, "OutputGain", 1.0));
    s[sh::output] = linValue (outDb, -20.0, 20.0);
    s[sh::outputMix] = 1.0f;

    // the rack: "Drum Buss" chain (Ember → Shell) and, when the device is not fully wet or a macro moves Dry/Wet, a "Dry"
    // chain beside it (the chains' volumes: dry 1 − x, wet x [ASSUMED linear])
    out.slot = rack::makeRackSlot (RackDoc::Kind::audioEffect);
    auto& doc = out.slot.patcher;
    if (auto* r = rack::rackOf (doc); r != nullptr && ! r->chains.empty())
        r->chains.front().name = "Drum Buss";
    PatchNode en, sn;
    en.effect = std::move (ember);
    en.label = "Drive";
    sn.effect = std::move (shell);
    sn.label = "Shape";
    const auto emberNode = rack::addDevice (doc, 0, std::move (en));
    const auto shellNode = rack::addDevice (doc, 0, std::move (sn));
    if (emberNode < 0 || shellNode < 0)
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

    // the macros, each over the whole range of its LYNX control (a macro at v sets the control to v), named after
    // Live's controls; Live's own parameters reach them through `links`
    auto* r = rack::rackOf (doc);
    if (r == nullptr)
        return false;
    struct M
    {
        int macro;
        const char* name;
        int node, param;
    };
    const M macros[] { { dbDrive, "Drive", emberNode, em::drive },          { dbCrunch, "Crunch", shellNode, sh::hfAmount },
                       { dbDamping, "Damping", emberNode, wide ? em::highCutWide : em::highCut },
                       { dbTransients, "Transients", shellNode, sh::tsAmount }, { dbBoom, "Boom", shellNode, sh::lfAmount },
                       { dbBoomFreq, "Boom Freq", shellNode, sh::lfFreq },   { dbTrim, "Trim", emberNode, em::inputGain },
                       { dbOutput, "Output", shellNode, sh::output } };
    for (const auto& m : macros)
    {
        RackMapping target;
        target.node = m.node;
        target.param = m.param;
        rack::map (doc, m.macro, target);
        r = rack::rackOf (doc);
        r->macros[(std::size_t) m.macro].name = m.name;
    }
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
    // each macro where its control is now (nothing moves)
    const auto valueOf = [&doc] (int node, int param) { return doc.nodes[(std::size_t) node].effect.params[(std::size_t) param]; };
    for (const auto& m : macros)
        r->macros[(std::size_t) m.macro].value = valueOf (m.node, m.param) * 127.0f;
    if (dry >= 0)
        r->macros[(std::size_t) dbDryWet].value = (float) (127.0 * std::clamp ((double) rack::volumeValue ((float) dryWet) / rack::volumeValue (1.0f), 0.0, 1.0));
    r->shownMacros = dry >= 0 ? 10 : 8;
    r->name = "Drum Buss";
    rack::compile (doc);

    // Live's parameters → the macros (the macro's 0..1 is its control's 0..1)
    out.links.push_back ({ "DriveAmount", dbDrive, [] (double v) { return linValue (drumBussDriveDb (v), 0.0, 36.0); } });
    out.links.push_back ({ "CrunchAmount", dbCrunch, [] (double v) { return unit (v); } });
    if (wide)
        out.links.push_back ({ "DampingFrequency", dbDamping, [] (double hz) { return hz >= 19999.0 ? 1.0f : logValue (hz, 1000.0, 20000.0); } });
    else
        out.links.push_back ({ "DampingFrequency", dbDamping, [] (double hz) { return hz >= 19999.0 ? 1.0f : logValue (hz, 2000.0, 20000.0); } });
    if (transients >= 0.0)
        out.links.push_back ({ "TransientShaping", dbTransients, [] (double t) { return unit (t); } }); // (the positive side: Invert stays off)
    out.links.push_back ({ "BoomAmount", dbBoom, [] (double v) { return unit (v); } });
    out.links.push_back ({ "BoomFrequency", dbBoomFreq, [] (double hz) { return sh::lfValue (drumBussBoomLfHz (hz)); } });
    out.links.push_back ({ "InputTrim", dbTrim, [] (double g) { return linValue (dbOfGain (g), -24.0, 24.0); } });
    out.links.push_back ({ "OutputGain", dbOutput, [] (double g) { return linValue (dbOfGain (g), -20.0, 20.0); } });
    if (dry >= 0)
        out.links.push_back ({ "DryWet", dbDryWet, [] (double x) {
                                   return (float) std::clamp ((double) rack::volumeValue ((float) std::clamp (x, 0.0, 1.0)) / rack::volumeValue (1.0f), 0.0, 1.0);
                               } });

    // the report: what is a fit, what has no counterpart
    out.notes.approximated.push_back (std::string ("drive as Ember's ") + (type == 0 ? "tube" : (type == 1 ? "tape" : "transistor"))
                                      + " style (Soft / Medium / Hard; drive law assumed)");
    if (s[sh::tsOn] > 0.5f)
        out.notes.approximated.push_back ("transients as Shell Channel's transient shaper");
    if (boom > 1.0e-4)
        out.notes.approximated.push_back ("Boom as Shell Channel's low enhancer (its decay is not applied)");
    if (crunch > 1.0e-4)
        out.notes.approximated.push_back ("Crunch as Shell Channel's high enhancer (a lift of the highs, not a distortion)");
    if (s[sh::lmcOn] > 0.5f)
        out.notes.approximated.push_back ("the compressor as Shell Channel's listen-mic compressor");
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

CutoffModulation cutoffModulation (double c0, double lfoDepth, double envDepth) noexcept
{
    // the output o = E'·peak + L'·(w + 1)/2 (bases 0) and the cutoff = y0 + S·o: S = ±(2L + |E|) carries both at full
    // scale without clipping the output; a negative E turns the curve over, and the LFO with it (half a cycle later)
    CutoffModulation m;
    const auto L = std::abs (lfoDepth), E = envDepth;
    const auto sign = E < 0.0 ? -1.0 : 1.0;
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

void autoFilterModulation (const Element& device, const Context& context, Translated& out)
{
    const auto newer = device.name() == "AutoFilter2";
    // the depths in the Filter's cutoff units (0..1 = 20 Hz … 20 kHz, log: one unit = log2 (1000) octaves)
    constexpr double octavesPerUnit = 9.965784284662087;
    // the older device: LFO Amount [DATA 0 … 30] and the envelope's ModHub [DATA −127 … 127] in semitones of its cutoff
    // (a pitch) [ASSUMED]; the new one: Lfo_Amount [DATA 0 … 1] and Envelope_Amount [DATA −1 … 1] as fractions of the
    // whole range [ASSUMED]
    const auto lfoRaw = manual (device, newer ? "Lfo_Amount" : "LfoAmount", 0.0);
    const auto envRaw = manual (device, newer ? "Envelope_Amount" : "ModHub", 0.0);
    const auto lfoDepth = newer ? std::clamp (lfoRaw, 0.0, 1.0) * 0.5 : lfoRaw / 12.0 / octavesPerUnit;
    const auto envDepth = newer ? std::clamp (envRaw, -1.0, 1.0) : envRaw / 12.0 / octavesPerUnit;
    const auto lfoOn = (newer || manualBool (device, "Lfo/IsOn", true)) && std::abs (lfoDepth) > 1.0e-5;
    const auto envOn = std::abs (envDepth) > 1.0e-5;
    if (! lfoOn && ! envOn)
        return;
    const auto c0 = (double) out.slot.params[1];
    const auto m = cutoffModulation (c0, lfoOn ? lfoDepth : 0.0, envOn ? envDepth : 0.0);
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
        mod.notes.push_back (text ("envelope follower: amount %.0f as %.2f octaves at full scale, release %.0f ms as a straight fall (law assumed)", envRaw,
                                   envDepth * octavesPerUnit, release));
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
        // the rate: Hz (fixed at the set's tempo) or a synced note value [DATA: older RateType 0 / 1, BeatRate 0 … 21; the
        // new device's Lfo_TimeMode 0 = Hz, 2 = synced: ASSUMED]
        double beats = 1.0;
        bool synced = false;
        if (newer)
        {
            const auto mode = (int) manual (device, "Lfo_TimeMode", 0);
            synced = mode == 2;
            beats = synced ? liveBeatRateBeats ((int) manual (device, "Lfo_SyncedRate", 4)) : context.bpm / 60.0 / std::max (manual (device, "Lfo_Frequency", 1.0), 0.01);
            if (mode != 0 && mode != 2)
                mod.notes.push_back (text ("LFO time mode %.0f (taken as Hz)", mode));
        }
        else
        {
            synced = (int) manual (device, "Lfo/RateType", 0) == 1;
            beats = synced ? liveBeatRateBeats ((int) manual (device, "Lfo/BeatRate", 4)) : context.bpm / 60.0 / std::max (manual (device, "Lfo/Frequency", 1.0), 0.01);
        }
        p[7] = peakLfoSpeedValue (beats);
        // the shape [DOC order: sine, square, triangle, saw up, saw down, S&H; DATA older Type 0 … 6, new 0 … 7: ASSUMED
        // the same first six] → the controller's sine 0, triangle 1, square 2, saw 3, random 4
        const auto type = (int) manual (device, newer ? "Lfo_Waveform" : "Lfo/Type", 0);
        static constexpr int shapes[6] { 0, 2, 1, 3, 3, 4 };
        p[8] = (float) shapes[std::clamp (type, 0, 5)] / 4.0f;
        // the phase offset (degrees) and the half cycle a turned-over curve needs
        const auto phase = manual (device, newer ? "Lfo_PhaseOffset" : "Lfo/Offset", 0.0) / 360.0 + (m.invert ? 0.5 : 0.0);
        p[9] = (float) (phase - std::floor (phase));
        mod.notes.push_back (text ("LFO: amount %.2f as %.2f octaves either side (law assumed)", lfoRaw, lfoDepth * octavesPerUnit));
        if (! synced)
            mod.notes.push_back (text ("LFO rate %.2f Hz held as a note length at the set's tempo", beats > 0.0 ? context.bpm / 60.0 / beats : 0.0));
        if (type == 4)
            mod.notes.push_back ("LFO falling saw as a rising one");
        if (type > 5)
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
    // voice mode [DATA Global_VoiceMode 0 … 2, ASSUMED 0 poly, 1 mono, 2 stereo / unison] and glide (s) at the set's tempo
    const auto voiceMode = (int) manual (device, "Global_VoiceMode", 0);
    s.mono = voiceMode == 1;
    if (voiceMode == 2)
        notes.dropped.push_back ("stereo / unison voices");
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
