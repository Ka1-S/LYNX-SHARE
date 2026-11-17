// Live's own devices → LYNX's, lot 6 (round 190ko; core/AlsDevices.h has the list and the evidence tags): the three
// Live 12 devices that were still dropped — Meld (an instrument) → Braid, Spectral Time → Timeprism, Spectral Resonator
// → Harmonia. Same rules as the earlier lots: each conversion is a law in a unit, what is fitted or has no counterpart
// becomes a note for the import report, and only the device's saved state is read. The laws were measured on Live
// 12.4's own renders of probe sets (round 190ko, /mnt/e/lynx-work/meld190ko: sets m1, m2, s1 and 190kk's k1 presets).

#include "core/AlsDevices.h"

#include <algorithm>
#include <cmath>

#include "core/Braid.h"
#include "core/Effects.h"
#include "core/Harmonia.h"
#include "core/Song.h"
#include "core/Timeprism.h"
#include "core/XmlLite.h"

namespace lynx::alsdev
{
namespace
{
using xml::Element;

// a value as Live stores it: a parameter's Manual, a plain Value (the menus), "true" / "false"
bool readValue (const Element& device, const std::string& name, double& out)
{
    auto e = device.path (name);
    if (! e)
        return false;
    auto v = e.path ("Manual") ? e.child ("Manual").attr ("Value") : e.attr ("Value");
    if (v == "true" || v == "false")
    {
        out = v == "true" ? 1.0 : 0.0;
        return true;
    }
    try
    {
        out = std::stod (v);
        return std::isfinite (out);
    }
    catch (...)
    {
        return false;
    }
}
double value (const Element& device, const std::string& name, double fallback)
{
    double v = fallback;
    return readValue (device, name, v) ? v : fallback;
}
bool has (const Element& device, const std::string& name) { return (bool) device.path (name); }
} // namespace

// ================================================================================================ Meld → Braid

bool meldToBraid (const Element& device, ChannelDef& channel, Notes& notes)
{
    if (device.name() != "InstrumentMeld")
        return false;
    auto state = braidDefaults();
    // Braid keeps Meld's units: every automatable value is copied as Live stores it [DATA: InstrumentMeld's elements]
    for (int i = 0; i < braid::paramCount; ++i)
    {
        double v = 0.0;
        if (readValue (device, braid::liveName (i), v))
            state[(std::size_t) i] = (float) v;
    }
    // the modulation matrix: 50 rows in a fixed order (A's 25 targets, then B's), 19 amounts each [DATA: 142 Core Library
    // Melds, every one with the same row names in the same order]
    int row = 0;
    if (const auto mc = device.path ("ModulationConnections"))
        for (const auto& c : mc.children())
        {
            if (row >= braid::targetCount)
                break;
            const auto name = c.value ("TargetName");
            if (! name.empty() && name != braid::targetName (row))
                notes.approximated.push_back ("matrix row \"" + name + "\" read as \"" + braid::targetName (row) + "\"");
            for (int s = 0; s < braid::sourceCount; ++s)
                state[(std::size_t) braid::matrixIndex (row, s)] = (float) c.number ("ModulationAmounts." + std::to_string (s), 0.0);
            ++row;
        }
    // the wheel: Pitch Mod ← Pitch Bend carries the bend range (LYNX's channel range holds whole semitones) [MEASURED m1]
    const auto bend = state[(std::size_t) braid::matrixIndex (braid::tgt::pitch, braid::src::pitchBend)];
    if (std::abs (bend) > 1.0e-6f)
        channel.misc.pitchRange = std::clamp ((int) std::lround (std::abs (bend) * 48.0f), 1, 48);
    // scale awareness follows Live's Scale Mode (the set's scale): Braid plays chromatic degrees
    if (device.boolean ("MeldVoice_UseScale/Manual", false) || device.boolean ("MeldVoice_UseScale/Value", false))
        notes.approximated.push_back ("Use Current Scale (degrees play as semitones; the set's scale is not applied to Braid)");
    for (int k = 0; k < 2; ++k)
    {
        const std::string e = k == 0 ? "A" : "B";
        if (state[(std::size_t) (braid::engineBase (k) + braid::eng::on)] < 0.5f)
            continue;
        const auto osc = (int) std::lround (state[(std::size_t) (braid::engineBase (k) + braid::eng::oscType)]);
        const auto flt = (int) std::lround (state[(std::size_t) (braid::engineBase (k) + braid::eng::filterType)]);
        notes.approximated.push_back ("engine " + e + ": the " + braid::oscTypeName (osc) + " oscillator and the " + braid::filterTypeName (flt)
                                      + " filter as LYNX's own algorithms (fitted to Live's renders)");
    }
    channel.instrument = InstrumentKind::braid;
    channel.modelParams = std::move (state);
    channel.pluginState.clear();
    channel.samplePath.clear();
    return true;
}

std::vector<std::pair<std::string, int>> braidAutomatable (const Element& device)
{
    std::vector<std::pair<std::string, int>> out;
    for (int i = 0; i < braid::paramCount; ++i)
        if (const auto e = device.path (braid::liveName (i)))
            if (const auto id = e.path ("AutomationTarget").attr ("Id"); ! id.empty())
                out.emplace_back (id, i);
    return out;
}

// ================================================================================================ Spectral Time → Timeprism

bool spectralTimeToTimeprism (const Element& device, Translated& out)
{
    namespace t = timeprism;
    if (! has (device, "Freezer_On") || ! has (device, "Delay_On"))
        return false;
    out.slot = makeEffectSlot (EffectKind::timeprism);
    auto& p = out.slot.params;
    const auto unit = [] (double x) { return (float) std::clamp (x, 0.0, 1.0); };
    const auto onOff = [] (double x) { return x >= 0.5 ? 1.0f : 0.0f; };
    struct Map
    {
        const char* live;
        int param;
        double fallback;
        std::function<float (double)> law;
    };
    const Map maps[] {
        { "Freezer_On", t::freezerOn, 0.0, onOff },
        { "Freezer_FreezeOn", t::freeze, 0.0, onOff },
        { "Freezer_MainMode", t::freezeMode, 0.0, onOff },
        { "Freezer_RetriggerMode", t::retrigger, 1.0, onOff },
        { "Freezer_SyncIntervalUnits", t::syncUnits, 0.0, onOff },
        { "Freezer_SyncIntervalSeconds", t::interval, 0.5, [] (double s) { return t::logValue (s, 0.02, 10.0); } },
        { "Freezer_SyncIntervalBeats", t::intervalBeats, 6.0, [] (double i) { return t::stepValue ((int) std::lround (i), 22); } },
        { "Freezer_FadeType", t::fadeType, 0.0, onOff },
        { "Freezer_FadeIn", t::fadeIn, 0.0056, [] (double s) { return t::logValue (s, 0.002, 10.0); } },
        { "Freezer_FadeOut", t::fadeOut, 0.04, [] (double s) { return t::logValue (s, 0.02, 10.0); } },
        { "Freezer_CrossfadePercent", t::crossfade, 0.0, unit },
        { "Freezer_Sensitivity", t::sensitivity, 0.5, unit },
        { "Delay_On", t::delayOn, 1.0, onOff },
        { "Delay_TimeUnit", t::timeUnit, 0.0, [] (double i) { return t::stepValue ((int) std::lround (i), 5); } },
        { "Delay_TimeSeconds", t::time, 0.25, [] (double s) { return t::logValue (s, 0.01, 3.0); } },
        { "Delay_TimeDivisions", t::divisions, 15.0, [] (double i) { return t::stepValue ((int) std::lround (i), 22); } },
        { "Delay_TimeSixteenths", t::sixteenths, 3.0, [] (double i) { return t::stepValue ((int) std::lround (i) - 1, 16); } },
        { "Delay_Feedback", t::feedback, 0.5, unit },
        { "Delay_Tilt", t::tilt, 0.0, [] (double x) { return t::linValue (x, -2.0, 2.0); } },
        { "Delay_Spray", t::spray, 0.0, [] (double x) { return t::linValue (x, 0.0, 0.4); } },
        { "Delay_Mask", t::mask, 0.0, [] (double x) { return t::linValue (x, -1.0, 1.0); } },
        { "Delay_StereoSpread", t::stereo, 0.0, unit },
        { "Delay_FrequencyShift", t::shift, 0.0, [] (double x) { return t::linValue (x, -400.0, 400.0); } },
        { "Delay_DryWet", t::delayMix, 0.5, unit },
        { "InputSendGain", t::input, 1.0, unit },
        { "Resolution", t::resolution, 3.0, [] (double i) { return t::stepValue ((int) std::lround (i), 4); } },
        { "DryWet", t::mix, 1.0, unit },
        { "ProcessingOrder", t::order, 0.0, onOff },
    };
    for (const auto& m : maps)
    {
        p[(std::size_t) m.param] = m.law (value (device, m.live, m.fallback));
        out.links.push_back ({ m.live, m.param, m.law });
    }
    if (device.boolean ("ZeroLatencyDry/Value", false))
        out.notes.dropped.push_back ("Zero Dry Signal Latency (the dry signal is delayed with the wet one)");
    if (t::on (p[t::freezerOn]) && ! t::on (p[t::freezeMode]) && t::on (p[t::freeze]))
        out.notes.approximated.push_back ("a manual freeze saved as on: LYNX freezes at the first sound it hears");
    if (t::on (p[t::freezerOn]) && t::on (p[t::freezeMode]) && ! t::on (p[t::retrigger]))
        out.notes.approximated.push_back ("Onsets: LYNX's own onset detector (Sensitivity fitted)");
    if (t::on (p[t::delayOn]) && (std::abs (value (device, "Delay_Spray", 0.0)) > 1.0e-4))
        out.notes.approximated.push_back ("Spray: LYNX's own random spread of the delay times (Live's pattern differs)");
    return true;
}

// ================================================================================================ Spectral Resonator → Harmonia

bool spectralResonatorToHarmonia (const Element& device, Translated& out)
{
    namespace h = harmonia;
    if (! has (device, "PartialFrequencies_FrequencyHz") || ! has (device, "DecayDamping_DecayTime"))
        return false;
    // the quality gate (round 190ko): on Live's renders of the shipped presets, a decay of 15 s or longer, eight unison copies
    // spread wider than 10 %, or the granular modulation on a decay of 5 s or more scored worse than leaving the device out —
    // those stay dropped and listed, as before (the k1 presets Metallic Tail, Drum Snow, Dripping Grains)
    const auto decay = value (device, "DecayDamping_DecayTime", 1.0);
    if (decay >= 15.0 || ((int) std::lround (value (device, "Global_Unison", 0.0)) >= 3 && value (device, "Global_UnisonAmount", 0.0) >= 0.1)
        || ((int) std::lround (value (device, "Modulation_ModMode", 0.0)) == 3 && decay >= 5.0))
        return false;
    out.slot = makeEffectSlot (EffectKind::harmonia);
    auto& p = out.slot.params;
    const auto unit = [] (double x) { return (float) std::clamp (x, 0.0, 1.0); };
    // Freq: in Hz, or as a note (Live's Frequency Control Mode) [DATA: FrequencyDialMode 0 / 1; MEASURED s1 r01]
    const auto dialNote = value (device, "PartialFrequencies_FrequencyDialMode", 0.0) >= 0.5;
    const auto note = value (device, "PartialFrequencies_FrequencyNote", 45.0);
    const auto hz = dialNote ? 440.0 * std::exp2 ((note - 69.0) / 12.0) : value (device, "PartialFrequencies_FrequencyHz", 110.0);
    p[h::frequency] = h::frequencyValue (hz);
    if (dialNote)
        out.links.push_back ({ "PartialFrequencies_FrequencyNote", h::frequency,
                               [] (double n) { return h::frequencyValue (440.0 * std::exp2 ((n - 69.0) / 12.0)); } });
    else
        out.links.push_back ({ "PartialFrequencies_FrequencyHz", h::frequency, [] (double f) { return h::frequencyValue (f); } });
    struct Map
    {
        const char* live;
        int param;
        double fallback;
        std::function<float (double)> law;
    };
    const Map maps[] {
        { "PartialFrequencies_Shift", h::shift, 0.0, [] (double s) { return h::linValue (s, -48.0, 48.0); } },
        { "PartialFrequencies_PartialStretch", h::stretch, 0.0, [] (double s) { return h::linValue (s, -1.0, 1.0); } },
        { "DecayDamping_DecayTime", h::decay, 1.0, [] (double s) { return h::logValue (s, 0.001, 20.0); } },
        { "DecayDamping_HighFreqDamp", h::hfDamp, 0.0, unit },
        { "DecayDamping_LowFreqDamp", h::lfDamp, 0.0, unit },
        { "Modulation_ModMode", h::modMode, 0.0, [] (double m) { return h::stepValue ((int) std::lround (m), 4); } },
        { "Modulation_ModRate", h::modRate, 0.1, unit },
        { "Modulation_PitchModAmount", h::pitchMod, 0.0, [] (double s) { return h::linValue (s, 0.0, 4.0); } },
        { "Global_NumHarmonics", h::harmonics, 256.0, [] (double n) { return h::harmonicValue ((int) std::lround (n)); } },
        { "Global_Unison", h::unison, 0.0, [] (double u) { return h::stepValue ((int) std::lround (u), 4); } },
        { "Global_UnisonAmount", h::unisonAmount, 0.0, unit },
        { "Global_InputSend", h::input, 0.0, [] (double db) { return h::linValue (db, -24.0, 24.0); } },
        { "Global_DryWet", h::mix, 0.5, unit },
    };
    for (const auto& m : maps)
    {
        p[(std::size_t) m.param] = m.law (value (device, m.live, m.fallback));
        out.links.push_back ({ m.live, m.param, m.law });
    }
    if (value (device, "Global_PitchMode", 0.0) >= 0.5)
        out.notes.dropped.push_back ("MIDI pitch mode (the side-chain notes tune Live's resonator; Harmonia keeps its own frequency)");
    if (device.boolean ("Global_UseScale/Manual", false) || device.boolean ("PartialFrequencies_QuantizeToScaleOrTuning/Manual", false))
        out.notes.dropped.push_back ("Use Current Scale / Quantize (the partials stay where Freq and Stretch put them)");
    if (value (device, "Modulation_ModMode", 0.0) >= 0.5)
        out.notes.approximated.push_back ("the partials' modulation (chorus / wander / granular): LYNX's own random patterns");
    return true;
}
} // namespace lynx::alsdev
