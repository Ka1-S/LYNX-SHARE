// Live's Wavetable → Helix, LYNX's own wavetable synth (round 190ja, core/WavetableSynth.h). Helix keeps Wavetable's
// units, so every parameter is copied as Live stores it; Live's wave tables (Live's content) map onto the closest LYNX
// table and the report says so; the rest is listed. Evidence tags as in core/AlsDevices.h: [DATA] read in saved sets
// (271 Core Library presets, the 19 shipped sets, 190ja's render sets), [MEASURED] Live renders.

#include "core/AlsDevices.h"

#include <algorithm>
#include <cmath>

#include "core/Song.h"
#include "core/WavetableSynth.h"
#include "core/XmlLite.h"

namespace lynx::alsdev
{
using xml::Element;

namespace
{
// Live's names of Helix's automatable values, in Helix's order [DATA: InstrumentVector's elements]
std::vector<std::string> liveNames()
{
    std::vector<std::string> n;
    for (int k = 1; k <= 2; ++k)
    {
        const auto o = "Voice_Oscillator" + std::to_string (k) + "_";
        for (const char* s : { "On", "Pitch_Transpose", "Pitch_Detune", "Wavetables_WavePosition", "Effects_EffectMode", "Effects_Effect1",
                               "Effects_Effect2", "Pan", "Gain" })
            n.push_back (o + s);
    }
    for (const char* s : { "On", "Tone", "Gain", "Transpose" })
        n.push_back (std::string ("Voice_SubOscillator_") + s);
    for (int k = 1; k <= 2; ++k)
        for (const char* s : { "On", "Type", "CircuitLpHp", "CircuitBpNoMo", "Slope", "Frequency", "Resonance", "Drive", "Morph" })
            n.push_back ("Voice_Filter" + std::to_string (k) + "_" + s);
    n.push_back ("Voice_Global_FilterRouting");
    for (const char* e : { "AmpEnvelope", "Envelope2", "Envelope3" })
    {
        const auto b = std::string ("Voice_Modulators_") + e + "_";
        for (const char* s : { "Times_Attack", "Times_Decay", "Times_Release", "Slopes_Attack", "Slopes_Decay", "Slopes_Release" })
            n.push_back (b + s);
        n.push_back (b + (std::string (e) == "AmpEnvelope" ? "Sustain" : "Values_Sustain"));
        n.push_back (b + "LoopMode");
        if (std::string (e) != "AmpEnvelope")
            for (const char* s : { "Values_Initial", "Values_Peak", "Values_Final" })
                n.push_back (b + s);
    }
    for (int k = 1; k <= 2; ++k)
    {
        const auto b = "Voice_Modulators_Lfo" + std::to_string (k) + "_";
        for (const char* s : { "Retrigger", "Shape_Type", "Shape_Amount", "Shape_Shaping", "Shape_PhaseOffset", "Time_Sync", "Time_Rate",
                               "Time_SyncedRate", "Time_AttackTime" })
            n.push_back (b + s);
    }
    for (const char* s : { "Voice_Modulators_TimeScale", "Voice_Modulators_Amount", "Voice_Unison_Mode", "Voice_Unison_VoiceCount", "Voice_Unison_Amount",
                           "Voice_Global_Transpose", "Voice_Global_Glide", "Volume", "MonoPoly", "PolyVoices", "HiQ" })
        n.push_back (s);
    return n;
}

// a value as Live stores it: a parameter's Manual, a plain Value (the menus), "true" / "false"
bool read (const Element& device, const std::string& name, double& out)
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
} // namespace

const std::vector<std::string>& helixLiveNames()
{
    static const auto n = liveNames();
    return n;
}

bool wavetableToHelix (const Element& device, ChannelDef& channel, Notes& notes)
{
    if (device.name() != "InstrumentVector" || ! device.path ("Voice_Oscillator1_On"))
        return false;
    auto state = helixDefaults();
    const auto& names = helixLiveNames();
    for (int i = 0; i < wt::paramCount && i < (int) names.size(); ++i)
    {
        double v = 0.0;
        if (read (device, names[(std::size_t) i], v))
            state[(std::size_t) i] = (float) v;
    }
    // filter types above Morph (an older set) play as Morph's neighbour [DATA: 1 of 271 presets stores Type 5]
    for (int k = 0; k < 2; ++k)
        if (auto& t = state[(std::size_t) (wt::filterBase (k) + wt::flt::type)]; t > 4.0f)
        {
            notes.approximated.push_back ("filter " + std::to_string (k + 1) + " type " + std::to_string ((int) t) + " (played as Morph)");
            t = 4.0f;
        }
    // the tables: Live's own content, mapped onto LYNX's (SpriteName = the table's name in Live's browser) [DATA]
    for (int k = 0; k < 2; ++k)
    {
        const auto name = device.value ("SpriteName" + std::to_string (k + 1));
        const auto user = device.path ("UserSprite" + std::to_string (k + 1) + "/Value");
        const bool userTable = user && ! user.children().empty();
        const bool on = state[(std::size_t) wt::oscBase (k)] > 0.5f;
        const auto m = wt::matchLiveTable (userTable ? std::string() : name);
        state[(std::size_t) wt::tableBase + (std::size_t) k] = (float) m.table;
        if (! on)
            continue;
        if (userTable)
            notes.dropped.push_back ("oscillator " + std::to_string (k + 1) + "'s user wave table (LYNX table \"" + wt::tableName (m.table) + "\" plays)");
        else if (! m.note.empty())
            notes.approximated.push_back ("oscillator " + std::to_string (k + 1) + ": " + m.note);
    }
    // a set LYNX exported names its LYNX tables in the device's info text: they come back exactly (round 190ja)
    if (const auto info = device.value ("Annotation"); info.rfind ("LYNX Helix tables: ", 0) == 0)
    {
        const auto list = info.substr (19);
        const auto bar = list.find (" | ");
        const std::string names[2] { list.substr (0, bar), bar == std::string::npos ? std::string() : list.substr (bar + 3) };
        for (int k = 0; k < 2; ++k)
            if (const auto t = wt::tableByName (names[k]); t >= 0)
            {
                state[(std::size_t) wt::tableBase + (std::size_t) k] = (float) t;
                notes.approximated.erase (std::remove_if (notes.approximated.begin(), notes.approximated.end(), [k] (const std::string& n) {
                                              return n.rfind ("oscillator " + std::to_string (k + 1) + ": ", 0) == 0;
                                          }),
                                          notes.approximated.end());
            }
    }
    // the modulation matrix: 52 rows in a fixed order (their names follow the effect modes), 13 amounts each [DATA]
    int row = 0;
    if (const auto mc = device.path ("ModulationConnections"))
        for (const auto& c : mc.children())
        {
            if (row >= wt::targetCount)
                break;
            for (int s = 0; s < wt::sourceCount; ++s)
                state[(std::size_t) wt::matrixIndex (row, s)] = (float) c.number ("ModulationAmounts." + std::to_string (s), 0.0);
            ++row;
        }
    // the wheel: Pitch ← Pitch Bend is Live's bend range (1 = 48 semitones [DATA: default 1/24 = 2 st]); LYNX's channel
    // range carries it
    const auto bend = state[(std::size_t) wt::matrixIndex (wt::tgt::pitch, wt::src::pitchBend)];
    if (std::abs (bend) > 1.0e-6f)
    {
        const auto st = std::abs (bend) * 48.0f;
        channel.misc.pitchRange = std::clamp ((int) std::lround (st), 1, 48);
        if (std::abs (st - std::round (st)) > 0.05f || bend < 0.0f)
            notes.approximated.push_back ("pitch bend range (whole semitones, upwards)");
    }
    if (device.boolean ("HiQ/Value", false) || state[(std::size_t) wt::hiQ] > 0.5f)
        notes.approximated.push_back ("HiQ (Helix is always band-limited)");

    channel.instrument = InstrumentKind::helix;
    channel.modelParams = std::move (state);
    channel.pluginState.clear();
    channel.samplePath.clear();
    return true;
}
} // namespace lynx::alsdev
