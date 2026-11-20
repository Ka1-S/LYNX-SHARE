// Braid (round 190ko, core/Braid.h): parameters, names, texts, Live's element names, the channel's values and the
// model-parameter table. The DSP is in core/Braid.cpp.

#include "core/Braid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

#include "core/ModelParams.h"
#include "core/Song.h"

namespace lynx
{
namespace braid
{
namespace
{
struct Spec
{
    std::string name, live; // LYNX's English name, Live's element name
    float low, high;
    bool stepped;
    double value; // a new device's value (Live's default Meld)
};

void engineSpecs (std::vector<Spec>& s, int k)
{
    const std::string e = k == 0 ? "A " : "B ", L = k == 0 ? "MeldVoice_EngineA_" : "MeldVoice_EngineB_";
    const auto add = [&] (const char* name, const char* live, float lo, float hi, bool stepped, double v) {
        s.push_back ({ e + name, L + live, lo, hi, stepped, v });
    };
    add ("On", "On", 0, 1, true, 1);
    add ("Osc Type", "Oscillator_OscillatorType", 0, 24, true, 0);
    add ("Osc Macro 1", "Oscillator_Macro1", 0, 1, false, 0);
    add ("Osc Macro 2", "Oscillator_Macro2", 0, 1, false, 0);
    add ("Transpose", "Oscillator_Pitch_Transpose", -12, 12, true, 0);
    add ("Octave", "Oscillator_Pitch_TransposeOctaves", -3, 3, true, 0);
    add ("Scale Degrees", "Oscillator_Pitch_TransposeScaleDegrees", -48, 48, true, 0);
    add ("Detune", "Oscillator_Pitch_Detune", -0.5f, 0.5f, false, 0);
    add ("Key Track", "Oscillator_Pitch_Keytracking", 0, 1, true, 1);
    add ("Osc Reset", "Oscillator_Reset", 0, 1, true, 1);
    add ("Filter On", "Filter_On", 0, 1, true, 1);
    add ("Filter Type", "Filter_FilterType", 0, 16, true, 0);
    add ("Filter Freq", "Filter_Frequency", 20, 20480, false, 20480);
    add ("Filter Macro 1", "Filter_Macro1", 0, 1, false, 0);
    add ("Filter Macro 2", "Filter_Macro2", 0, 1, false, 0);
    add ("LFO 1 Type", "Lfo1_GeneratorType", 0, 5, true, 0);
    add ("LFO 1 Sync", "Lfo1_Sync", 0, 1, true, 0);
    add ("LFO 1 Retrigger", "Lfo1_Retrigger", 0, 1, true, 1);
    add ("LFO 1 Rate", "Lfo1_Rate", 0.01f, 200, false, 1);
    add ("LFO 1 Sync Rate", "Lfo1_SyncedRate", 0, 21, true, 15);
    add ("LFO 1 Phase", "Lfo1_PhaseOffset", 0, 360, false, 0);
    add ("LFO 1 Macro 1", "Lfo1_GeneratorMacro1", 0, 1, false, 0);
    add ("LFO 1 Macro 2", "Lfo1_GeneratorMacro2", 0, 1, false, 0);
    add ("LFO 1 FX 1", "Lfo1_Transformer1Type", 0, 18, true, 2);
    add ("LFO 1 FX 1 Macro", "Lfo1_Transformer1Macro", 0, 1, false, 0);
    add ("LFO 1 FX 2", "Lfo1_Transformer2Type", 0, 18, true, 11);
    add ("LFO 1 FX 2 Macro", "Lfo1_Transformer2Macro", 0, 1, false, 0);
    add ("LFO 2 Wave", "Lfo2_Waveform", 0, 5, true, 0);
    add ("LFO 2 Sync", "Lfo2_Sync", 0, 1, true, 0);
    add ("LFO 2 Rate", "Lfo2_Rate", 0.01f, 200, false, 6);
    add ("LFO 2 Sync Rate", "Lfo2_SyncedRate", 0, 21, true, 12);
    add ("LFO 2 Retrigger", "Lfo2_Retrigger", 0, 1, true, 0);
    add ("LFO 2 Phase", "Lfo2_PhaseOffset", 0, 360, false, 0);
    add ("Amp Attack", "AmpEnvelope_Times_Attack", 0, 40, false, 0.001);
    add ("Amp Decay", "AmpEnvelope_Times_Decay", 0.0015f, 40, false, 0.6);
    add ("Amp Release", "AmpEnvelope_Times_Release", 0.0015f, 40, false, 0.05);
    add ("Amp Attack Slope", "AmpEnvelope_Slopes_Attack", -1, 1, false, 0);
    add ("Amp Decay Slope", "AmpEnvelope_Slopes_Decay", -1, 1, false, 0.5);
    add ("Amp Release Slope", "AmpEnvelope_Slopes_Release", -1, 1, false, 0.5);
    add ("Amp Sustain", "AmpEnvelope_Sustain", 0, 1, false, 1);
    add ("Amp Loop", "AmpEnvelope_LoopMode", 0, 3, true, 0);
    add ("Mod Attack", "FilterEnvelope_Times_Attack", 0, 40, false, 0.001);
    add ("Mod Decay", "FilterEnvelope_Times_Decay", 0.0015f, 40, false, 0.6);
    add ("Mod Release", "FilterEnvelope_Times_Release", 0.0015f, 40, false, 0.05);
    add ("Mod Initial", "FilterEnvelope_Values_Initial", 0, 1, false, 0);
    add ("Mod Peak", "FilterEnvelope_Values_Peak", 0, 1, false, 1);
    add ("Mod Sustain", "FilterEnvelope_Values_Sustain", 0, 1, false, 0);
    add ("Mod Final", "FilterEnvelope_Values_Final", 0, 1, false, 0);
    add ("Mod Attack Slope", "FilterEnvelope_Slopes_Attack", -1, 1, false, 0);
    add ("Mod Decay Slope", "FilterEnvelope_Slopes_Decay", -1, 1, false, 0.5);
    add ("Mod Release Slope", "FilterEnvelope_Slopes_Release", -1, 1, false, 0.5);
    add ("Mod Loop", "FilterEnvelope_LoopMode", 0, 3, true, 0);
    add ("Tone", "ToneFilter", -1, 1, false, 0);
    add ("Pan", "Pan", -1, 1, false, 0);
    add ("Volume", "Volume", -50, 24, false, -3);
    add ("Glide Mode", "GlideMode", 0, 1, true, 0);
    add ("Glide Time", "GlideTime", 0, 20, false, 0);
}

const std::vector<Spec>& specs()
{
    static const auto list = [] {
        std::vector<Spec> s;
        engineSpecs (s, 0);
        engineSpecs (s, 1);
        s.push_back ({ "B Delay", "MeldVoice_EngineBDelay", 0, 1, false, 0 });
        s.push_back ({ "Drive", "MeldVoice_Drive", 0, 1, false, 0 });
        s.push_back ({ "Limiter", "MeldVoice_LimiterOn", 0, 1, true, 0 });
        s.push_back ({ "Link Envelopes", "MeldVoice_LinkAmpEnvelopes", 0, 1, true, 0 });
        s.push_back ({ "Spread", "MeldVoice_VoiceSpreadAmount", 0, 1, false, 0 });
        s.push_back ({ "Volume", "Volume", 0, 1.99526238f, false, 0.5011858344 });
        s.push_back ({ "Poly", "MonoPoly", 0, 1, true, 1 });
        s.push_back ({ "Legato", "MonoLegato", 0, 1, true, 1 });
        s.push_back ({ "Voices", "PolyVoices", 0, 6, true, 5 });
        s.push_back ({ "Stacked Voices", "UnisonVoices", 0, 2, true, 0 });
        return s;
    }();
    return list;
}

std::string fmt (const char* f, double v)
{
    char b[64];
    std::snprintf (b, sizeof b, f, v);
    return b;
}

// [DATA 190ko: Live 12.4's menus, read from its device in the GUI session; LYNX's own names where they describe the same thing]
constexpr const char* oscNames[oscTypeCount] { "Basic Shapes", "Dual Basic Shapes", "Noisy Shapes", "Square Sync", "Square 5th", "Sub",
                                               "Swarm Sine", "Swarm Triangle", "Swarm Saw", "Swarm Square", "Harmonic Fm", "Fold Fm",
                                               "Squelch", "Simple Fm", "Chip", "Shepard's Pi", "Tarp", "Extratone", "Noise Loop",
                                               "Filtered Noise", "Bitgrunge", "Crackle", "Rain", "Bubble", "Chord" };
constexpr const char* oscMacros[oscTypeCount][2] { { "Shape", "Tone" }, { "Shape", "Detune" }, { "Shape", "Rough" }, { "Freq 1", "Freq 2" },
                                                   { "5th Amt", "P Width" }, { "Tone", "Aux" }, { "Motion", "Spacing" }, { "Motion", "Spacing" },
                                                   { "Motion", "Spacing" }, { "Motion", "Spacing" }, { "Amount", "Ratio" }, { "Amount", "Shape" },
                                                   { "Amount", "Feedback" }, { "Amount", "Ratio" }, { "Tone", "Rate" }, { "Rate", "Width" },
                                                   { "Decay", "Tone" }, { "Pitch", "Env Amt" }, { "Rate", "Fade" }, { "Freq", "Width" },
                                                   { "Freq", "Mult" }, { "Density", "Intensity" }, { "Tone", "Rate" }, { "Density", "Spread" },
                                                   { "Shape", "Inversion" } };
constexpr const char* filterNames[filterTypeCount] { "SVF 12dB", "SVF 24dB", "LP 12dB MS2", "HP 12dB MS2", "BP 12dB OSR", "LP Crunch 12dB",
                                                     "LP Switched Res", "Filther", "Eq Peak", "Eq Notch", "Phaser", "Redux", "Vowel",
                                                     "Comb +", "Comb -", "Plate Resonator", "Membrane Resonator" };
constexpr const char* filterMacros[filterTypeCount][2] { { "Q", "L-B-H-N" }, { "Q", "L-B-H-N" }, { "Q", "Drive" }, { "Q", "Drive" },
                                                         { "Q", "Drive" }, { "Q", "Drive" }, { "Q", "Lofi" }, { "Q", "Drive" },
                                                         { "Boost", "Q" }, { "Cut", "Q" }, { "Feedb", "Spread" }, { "Crush", "Lofi" },
                                                         { "Q", "Morph" }, { "Feedb", "Damp" }, { "Feedb", "Damp" }, { "Q", "Ratio" },
                                                         { "Q", "Damp" } };
constexpr const char* genNames[lfoGenCount] { "Basic Shapes", "Ramp", "Wander", "Alternate", "Euclid", "Pulsate" };
constexpr const char* fxNames[lfoFxCount] { "None", "Offset", "Attenuverter", "Gate", "Skew Unipolar", "Skew Bipolar", "Unipolarizer",
                                            "Quantizer", "S&H", "Independent S&H", "Clipper", "Fade In", "Fade Out", "Slew Down", "Slew Up",
                                            "Slew Up & Down", "Trig Env", "Comparator", "Scrambler" };
constexpr const char* wave2Names[lfo2WaveCount] { "Sine", "Tri", "Saw Up", "Saw Down", "Rectangle", "Random S&H" };
constexpr const char* sourceNames[sourceCount] { "A Env", "A M Env", "A LFO 1", "A LFO 1 FX", "A LFO 2", "B Env", "B M Env", "B LFO 1",
                                                 "B LFO 1 FX", "B LFO 2", "Vel", "Pitch", "PB", "Note PB", "Press", "Mod W", "Rand",
                                                 "Slide", "Spread" };
constexpr const char* targetNames[tgt::perEngine] { "Detune", "Pitch Mod", "Pitch Quant", "Osc Macro 1", "Osc Macro 2", "Filter Freq",
                                                    "Filter Macro 1", "Filter Macro 2", "LFO 1 Rate", "LFO 1 Macro 1", "LFO 1 Macro 2",
                                                    "LFO 1 FX 1 Macro", "LFO 1 FX 2 Macro", "LFO 2 Rate", "Amp Attack", "Amp Decay",
                                                    "Amp Release", "Amp Sustain", "Mod Attack", "Mod Decay", "Mod Release", "Mod Sustain",
                                                    "Tone Filter", "Pan", "Volume" };
} // namespace

const char* oscTypeName (int t) noexcept { return oscNames[std::clamp (t, 0, oscTypeCount - 1)]; }
const char* oscMacroName (int t, int m) noexcept { return oscMacros[std::clamp (t, 0, oscTypeCount - 1)][m & 1]; }
const char* filterTypeName (int t) noexcept { return filterNames[std::clamp (t, 0, filterTypeCount - 1)]; }
const char* filterMacroName (int t, int m) noexcept { return filterMacros[std::clamp (t, 0, filterTypeCount - 1)][m & 1]; }
const char* lfoGenName (int t) noexcept { return genNames[std::clamp (t, 0, lfoGenCount - 1)]; }
const char* lfoFxName (int t) noexcept { return fxNames[std::clamp (t, 0, lfoFxCount - 1)]; }
const char* lfo2WaveName (int t) noexcept { return wave2Names[std::clamp (t, 0, lfo2WaveCount - 1)]; }

const std::array<double, stateSize>& defaults() noexcept
{
    static const auto d = [] {
        std::array<double, stateSize> v {};
        const auto& s = specs();
        for (std::size_t i = 0; i < s.size(); ++i)
            v[i] = s[i].value;
        // Live's default Meld's matrix [DATA: Defaults/Instruments/Meld.adv]: both engines' pitch follows the bend (7 st) and
        // MPE note pitch, volume the velocity, and the voice spread detunes and pans them apart
        for (int k = 0; k < 2; ++k)
        {
            const auto t = k * tgt::perEngine;
            v[(std::size_t) matrixIndex (t + tgt::pitch, src::pitchBend)] = 0.1458333284;
            v[(std::size_t) matrixIndex (t + tgt::pitch, src::notePitchBend)] = 1.0;
            v[(std::size_t) matrixIndex (t + tgt::volume, src::velocity)] = 0.5;
            v[(std::size_t) matrixIndex (t + tgt::detune, src::spread)] = k == 0 ? 0.5 : -0.5;
            v[(std::size_t) matrixIndex (t + tgt::pan, src::spread)] = k == 0 ? 0.5 : -0.5;
        }
        return v;
    }();
    return d;
}

std::string paramName (int index)
{
    const auto& s = specs();
    return index >= 0 && index < (int) s.size() ? s[(std::size_t) index].name : std::string();
}
std::string liveName (int index)
{
    const auto& s = specs();
    return index >= 0 && index < (int) s.size() ? s[(std::size_t) index].live : std::string();
}
std::string sourceName (int source) { return source >= 0 && source < sourceCount ? sourceNames[source] : ""; }
std::string targetName (int target)
{
    if (target < 0 || target >= targetCount)
        return {};
    return std::string (target < tgt::perEngine ? "A " : "B ") + targetNames[target % tgt::perEngine];
}

std::string paramText (int index, double v)
{
    if (index >= 0 && index < 2 * eng::stride)
    {
        switch (index % eng::stride)
        {
            case eng::on:
            case eng::keytrack:
            case eng::oscReset:
            case eng::filterOn:
            case eng::lfo1Sync:
            case eng::lfo1Retrig:
            case eng::lfo2Sync:
            case eng::lfo2Retrig: return v >= 0.5 ? "On" : "Off";
            case eng::oscType: return oscTypeName ((int) std::lround (v));
            case eng::filterType: return filterTypeName ((int) std::lround (v));
            case eng::lfo1Gen: return lfoGenName ((int) std::lround (v));
            case eng::lfo1Fx1:
            case eng::lfo1Fx2: return lfoFxName ((int) std::lround (v));
            case eng::lfo2Wave: return lfo2WaveName ((int) std::lround (v));
            case eng::filterFreq: return v >= 1000.0 ? fmt ("%.2f kHz", v / 1000.0) : fmt ("%.0f Hz", v);
            case eng::lfo1Rate:
            case eng::lfo2Rate: return fmt ("%.2f Hz", v);
            case eng::lfo1Phase:
            case eng::lfo2Phase: return fmt ("%.0f deg", v);
            case eng::transpose: return fmt ("%+.0f st", v);
            case eng::octaves: return fmt ("%+.0f oct", v);
            case eng::degrees: return fmt ("%+.0f", v);
            case eng::detune: return fmt ("%+.0f ct", v * 100.0);
            case eng::ampAttack:
            case eng::ampDecay:
            case eng::ampRelease:
            case eng::modAttack:
            case eng::modDecay:
            case eng::modRelease:
            case eng::glideTime: return v < 1.0 ? fmt ("%.0f ms", v * 1000.0) : fmt ("%.2f s", v);
            case eng::volume: return fmt ("%.1f dB", v);
            case eng::pan: return std::abs (v) < 0.005 ? std::string ("C") : fmt (v < 0 ? "%.0fL" : "%.0fR", std::abs (v) * 50.0);
            case eng::ampLoop:
            case eng::modLoop:
            {
                static const char* const m[4] { "None", "Trigger", "Loop", "AD Loop" };
                return m[std::clamp ((int) std::lround (v), 0, 3)];
            }
            case eng::glideMode: return v >= 0.5 ? "Glissando" : "Portamento";
            default: return fmt ("%.0f%%", v * 100.0);
        }
    }
    switch (index)
    {
        case limiter:
        case linkAmp:
        case legato: return v >= 0.5 ? "On" : "Off";
        case monoPoly: return v >= 0.5 ? "Poly" : "Mono";
        case polyVoices: return fmt ("%.0f", (double) polyVoiceCount ((int) std::lround (v)));
        case stacked: return fmt ("%.0f", std::pow (2.0, std::lround (v)));
        case volume: return fmt ("%.1f dB", 20.0 * std::log10 (std::max (v, 1.0e-6)));
        default: return fmt ("%.0f%%", v * 100.0);
    }
}
} // namespace braid

std::vector<float> braidDefaults()
{
    const auto& d = braid::defaults();
    return std::vector<float> (d.begin(), d.end());
}

std::vector<double> braidState (const ChannelDef& channel)
{
    const auto& d = braid::defaults();
    std::vector<double> s (d.begin(), d.end());
    for (std::size_t i = 0; i < s.size() && i < channel.modelParams.size(); ++i)
        if (std::isfinite (channel.modelParams[i]))
            s[i] = channel.modelParams[i];
    return s;
}

const std::vector<EngineParam>& braidEngineParams()
{
    static const auto list = [] {
        std::vector<EngineParam> v;
        const auto& s = braid::specs();
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            EngineParam p;
            p.index = (int) i;
            p.name = s[i].name;
            p.low = s[i].low;
            p.high = s[i].high;
            p.stepped = s[i].stepped;
            v.push_back (std::move (p));
        }
        return v;
    }();
    return list;
}

namespace modelparams
{
namespace
{
std::optional<float> getBraid (const ChannelDef& c, const EngineParam& p)
{
    if (c.instrument != InstrumentKind::braid || p.index < 0 || p.index >= braid::paramCount)
        return std::nullopt;
    return p.index < (int) c.modelParams.size() ? c.modelParams[(std::size_t) p.index] : (float) braid::defaults()[(std::size_t) p.index];
}

bool setBraid (ChannelDef& c, const EngineParam& p, float raw)
{
    if (c.instrument != InstrumentKind::braid || p.index < 0 || p.index >= braid::paramCount)
        return false;
    if ((int) c.modelParams.size() < braid::stateSize)
    {
        const auto d = braidDefaults();
        const auto have = c.modelParams.size();
        c.modelParams.resize ((std::size_t) braid::stateSize);
        std::copy (d.begin() + (std::ptrdiff_t) have, d.end(), c.modelParams.begin() + (std::ptrdiff_t) have);
    }
    c.modelParams[(std::size_t) p.index] = raw;
    return true;
}
} // namespace

Table braidModelTable() { return { &braidEngineParams(), &getBraid, &setBraid }; }
} // namespace modelparams
} // namespace lynx
