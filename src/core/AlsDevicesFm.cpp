// Live's Operator → Tetra, LYNX's own FM synth (round 190hs, core/FmSynth.h). Tetra keeps Operator's units, so every
// parameter Tetra has is copied as Live stores it; the rest is listed in the import report. Evidence tags as in
// core/AlsDevices.h: [DATA] read in saved sets (Core Library presets and 190hs's render set), [MEASURED] Live renders.

#include "core/AlsDevices.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/FmSynth.h"
#include "core/Song.h"
#include "core/XmlLite.h"

namespace lynx::alsdev
{
using xml::Element;

namespace
{
double manual (const Element& device, const std::string& name, double fallback) { return device.number (name + "/Manual", fallback); }
bool manualBool (const Element& device, const std::string& name, bool fallback) { return device.boolean (name + "/Manual", fallback); }
bool has (const Element& device, const std::string& name) { return (bool) device.path (name + "/Manual"); }

// one value: the parameter's Manual when the set has it, the engine's default otherwise
struct Reader
{
    const Element& device;
    std::vector<float>& state;
    void put (int index, const std::string& path, double scale = 1.0)
    {
        if (has (device, path))
            state[(std::size_t) index] = (float) (manual (device, path, 0.0) * scale);
    }
    void flag (int index, const std::string& path)
    {
        if (has (device, path))
            state[(std::size_t) index] = manualBool (device, path, false) ? 1.0f : 0.0f;
    }
    void envelope (int base, const std::string& path)
    {
        static const char* names[fm::env::count] { "AttackTime", "AttackLevel", "AttackSlope", "DecayTime", "DecayLevel", "DecaySlope", "SustainLevel",
                                                   "ReleaseTime", "ReleaseLevel", "ReleaseSlope", "LoopMode", "LoopTime", "RepeatTime", "TimeVelScale" };
        for (int e = 0; e < fm::env::count; ++e)
            put (base + e, path + "/" + names[e]);
    }
};

bool nonZero (const Element& device, const std::string& path, double neutral = 0.0)
{
    return has (device, path) && std::abs (manual (device, path, neutral) - neutral) > 1.0e-4;
}
} // namespace

bool operatorToTetra (const Element& device, ChannelDef& channel, Notes& notes)
{
    if (! device.path ("Operator.0/Envelope") || ! device.path ("Globals/Algorithm"))
        return false;
    auto state = tetraDefaults();
    Reader r { device, state };
    for (int k = 0; k < fm::opCount; ++k)
    {
        const auto b = fm::opBase (k);
        const auto s = "Operator." + std::to_string (k) + "/";
        r.flag (b + fm::op::on, s + "IsOn");
        r.put (b + fm::op::level, s + "Volume");
        r.put (b + fm::op::wave, s + "WaveForm");
        r.put (b + fm::op::coarse, s + "Tune/Coarse");
        r.put (b + fm::op::fine, s + "Tune/Fine");
        r.flag (b + fm::op::fixed, s + "Tune/FixedFrequencyOn");
        r.put (b + fm::op::fixedFreq, s + "Tune/FixedFrequency");
        r.put (b + fm::op::fixedMulti, s + "Tune/FixedFrequencyMulti");
        r.put (b + fm::op::feedback, s + "Feedback");
        r.put (b + fm::op::phase, s + "Phase");
        r.flag (b + fm::op::retrig, s + "Retrigger");
        r.put (b + fm::op::velScale, s + "VelScale");
        r.put (b + fm::op::keyScale, s + "KeyScale");
        r.flag (b + fm::op::lfoOn, s + "LfoOn");
        r.flag (b + fm::op::pitchEnvOn, s + "PitchEnvOn");
        r.envelope (b + fm::op::envBase, s + "Envelope");
        // the user wave's partials [DATA: UserHarmonics/Harmonics.0 … 63, 0 … 1]
        for (int h = 0; h < fm::harmonics; ++h)
        {
            const auto path = s + "UserHarmonics/Harmonics." + std::to_string (h);
            if (device.path (path))
                state[(std::size_t) (fm::harmonicsBase + k * fm::harmonics + h)] = (float) std::clamp (device.number (path, 0.0), 0.0, 1.0);
        }
        const auto on = manualBool (device, s + "IsOn", k == 0);
        if (on && nonZero (device, s + "Tune/VelCoarseScale"))
            notes.dropped.push_back (std::string ("operator ") + "ABCD"[k] + ": Osc < Vel (frequency by velocity)");
        if (on && (int) manual (device, s + "WaveForm", 0) == fm::waveUser && ! device.boolean (s + "UserHarmonics/Normalize", true))
            notes.approximated.push_back (std::string ("operator ") + "ABCD"[k] + ": user wave normalised (Live's Normalize is off)");
    }
    // global [DATA: Globals/…]
    r.put (fm::g::algorithm, "Globals/Algorithm");
    r.put (fm::g::volume, "Globals/Volume");
    r.put (fm::g::pan, "Globals/Panorama");
    r.put (fm::g::transpose, "Globals/Transpose");
    r.put (fm::g::tone, "Globals/Tone");
    r.put (fm::g::spread, "Globals/SpreadAmount");
    if (device.path ("Globals/NumVoices"))
        state[(std::size_t) fm::g::voices] = (float) device.number ("Globals/NumVoices", 5.0);
    if (device.path ("Globals/RetriggerMode"))
        state[(std::size_t) fm::g::retrigger] = device.boolean ("Globals/RetriggerMode", true) ? 1.0f : 0.0f;
    r.flag (fm::g::glideOn, "Globals/PortamentoOn");
    r.put (fm::g::glideTime, "Globals/PortamentoTime");
    r.put (fm::g::envTime, "EnvScale/EnvTime");
    // LFO, pitch envelope, filter, shaper
    r.flag (fm::g::lfoOn, "Lfo/LfoOn");
    r.put (fm::g::lfoType, "Lfo/LfoType");
    r.put (fm::g::lfoRange, "Lfo/LfoRange");
    r.put (fm::g::lfoRate, "Lfo/LfoRate");
    r.put (fm::g::lfoSynced, "Lfo/LfoRateSynced");
    r.flag (fm::g::lfoRetrig, "Lfo/LfoRetrigger");
    r.put (fm::g::lfoAmount, "Lfo/LfoAmount");
    r.put (fm::g::lfoAmountA, "Lfo/LfoAmountA");
    r.flag (fm::g::lfoFilter, "Filter/LfoOn");
    r.envelope (fm::g::lfoEnv, "Lfo/LfoEnv");
    r.flag (fm::g::penvOn, "PitchEnv/PitchEnvOn");
    r.put (fm::g::penvAmountA, "PitchEnv/PitchEnvAmountA");
    r.put (fm::g::penvAmount, "PitchEnv/PitchEnv/EnvelopeAmount");
    r.envelope (fm::g::penv, "PitchEnv/PitchEnv");
    r.flag (fm::g::filterOn, "Filter/OnOff");
    r.put (fm::g::filterType, "Filter/Type");
    r.flag (fm::g::filterSlope, "Filter/Slope");
    r.put (fm::g::filterCircuit, "Filter/CircuitLpHp");
    r.put (fm::g::filterCircuitBp, "Filter/CircuitBpNoMo");
    r.put (fm::g::filterFreq, "Filter/Frequency");
    r.put (fm::g::filterRes, "Filter/Resonance");
    r.put (fm::g::filterMorph, "Filter/Morph");
    r.put (fm::g::filterDrive, "Filter/Drive");
    r.put (fm::g::filterVel, "Filter/FrequencyVelScale");
    r.put (fm::g::filterKey, "Filter/FrequencyKeyScale");
    r.put (fm::g::filterEnvAmount, "Filter/EnvelopeAmount");
    r.envelope (fm::g::filterEnv, "Filter/Envelope");
    r.put (fm::g::shaperType, "Shaper/Type");
    r.put (fm::g::shaperDrive, "Shaper/Drive");
    r.put (fm::g::shaperMix, "Shaper/DryWet");
    // what Tetra has no place for (only when it changes the sound)
    if (nonZero (device, "Globals/PanoramaKeyScale") || nonZero (device, "Globals/PanoramaRndScale"))
        notes.dropped.push_back ("pan by key / random pan");
    if (nonZero (device, "EnvScale/EnvTimeKeyScale"))
        notes.dropped.push_back ("envelope time by key");
    if (manualBool (device, "Lfo/LfoOn", false))
    {
        if (nonZero (device, "Lfo/LfoRateKeyScale"))
            notes.dropped.push_back ("LFO rate by key");
        if (nonZero (device, "Lfo/LfoAmountVelScale"))
            notes.dropped.push_back ("LFO amount by velocity");
        if (nonZero (device, "Lfo/ModDst/Amount") && (int) manual (device, "Lfo/ModDst/Connection", 0) != 0)
            notes.dropped.push_back ("LFO destination B");
    }
    if (manualBool (device, "PitchEnv/PitchEnvOn", false) && nonZero (device, "PitchEnv/ModDst/Amount") && (int) manual (device, "PitchEnv/ModDst/Connection", 0) != 0)
        notes.dropped.push_back ("pitch envelope destination B");
    bool midiMod = false;
    for (const auto* dst : { "MidiCtrl/KeyDst", "MidiCtrl/VelDst", "MidiCtrl/MidiCtrl.0", "MidiCtrl/MidiCtrl.1", "MidiCtrl/MidiCtrl.2" })
        for (int c = 0; c < 2; ++c)
        {
            const auto path = std::string (dst) + "/ModConnections." + std::to_string (c);
            if (std::abs (device.number (path + "/Amount", 0.0)) > 1.0e-4 && device.number (path + "/Connection", 0.0) != 0.0)
                midiMod = true;
        }
    if (midiMod)
        notes.dropped.push_back ("MIDI modulation routings (key, velocity, wheel, pressure, bend)");
    if (device.boolean ("Globals/LegacyMode", false))
        notes.approximated.push_back ("legacy filter mode (the current filter plays)");

    channel.instrument = InstrumentKind::tetra;
    channel.modelParams = std::move (state);
    channel.pluginState.clear();
    channel.samplePath.clear();
    // the wheel's range [DATA: Globals/PitchBendRange, semitones]
    if (device.path ("Globals/PitchBendRange"))
        channel.misc.pitchRange = std::clamp ((int) std::lround (manual (device, "Globals/PitchBendRange", 5.0)), 0, 48);
    return true;
}
} // namespace lynx::alsdev
