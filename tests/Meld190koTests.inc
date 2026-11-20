// --- round 190ko: the last Live 12 devices that were still dropped — Meld → Braid (LYNX's two-engine macro synth),
// Spectral Time → Timeprism, Spectral Resonator → Harmonia (LYNX's spectral effects). Laws measured on Live 12.4's renders
// (/mnt/e/lynx-work/meld190ko RESULTS.md). The device elements are written by the tests with the member names Live saves.

#include <complex>

#include "core/Braid.h"
#include "core/Harmonia.h"
#include "core/Timeprism.h"

namespace meld190ko
{
using als190cf::on;
using als190cf::par;
using als190cf::v;

// a Meld element: the given members (Live's names, values as stored) and a 50-row matrix with one amount set
inline std::string meld (const std::vector<std::pair<std::string, double>>& values, int row = -1, int source = -1, double amount = 0.0,
                         const std::string& extra = {})
{
    std::string s = "<InstrumentMeld Id=\"0\">" + on();
    int id = 7000;
    for (const auto& [k, x] : values)
        s += "<" + k + "><Manual Value=\"" + als190bq::num (x) + "\" /><AutomationTarget Id=\"" + std::to_string (id++) + "\" /></" + k + ">";
    s += extra + "<ModulationConnections>";
    for (int t = 0; t < lynx::braid::targetCount; ++t)
    {
        s += "<ModulationConnectionsForMeld Id=\"" + std::to_string (t) + "\">" + v ("TargetName", lynx::braid::targetName (t));
        for (int i = 0; i < lynx::braid::sourceCount; ++i)
            s += v (("ModulationAmounts." + std::to_string (i)).c_str(), t == row && i == source ? als190bq::num (amount) : std::string ("0"));
        s += "</ModulationConnectionsForMeld>";
    }
    return s + "</ModulationConnections></InstrumentMeld>";
}

// one Braid note rendered (mid channel), `seconds` long, note-off at `release`
inline std::vector<float> play (const std::vector<double>& state, int key = 60, double seconds = 1.0, double release = 0.5)
{
    lynx::braid::RefNote n;
    n.key = key;
    n.release = (int) (release * 48000.0);
    const auto out = lynx::braid::renderReference (state, { n }, 48000.0, (int) (seconds * 48000.0));
    std::vector<float> m (out.size() / 2);
    for (std::size_t i = 0; i < m.size(); ++i)
        m[i] = 0.5f * (out[2 * i] + out[2 * i + 1]);
    return m;
}
inline double rms (const std::vector<float>& x, double t0, double t1)
{
    double s = 0.0;
    const auto a = (std::size_t) (t0 * 48000.0), b = std::min (x.size(), (std::size_t) (t1 * 48000.0));
    for (auto i = a; i < b; ++i)
        s += (double) x[i] * x[i];
    return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
}
// the amplitude of frequency hz in x over [t0, t1] (lock-in)
inline double toneAt (const std::vector<float>& x, double hz, double t0, double t1)
{
    std::complex<double> acc;
    const auto a = (std::size_t) (t0 * 48000.0), b = std::min (x.size(), (std::size_t) (t1 * 48000.0));
    for (auto i = a; i < b; ++i)
        acc += (double) x[i] * std::exp (std::complex<double> (0.0, -2.0 * 3.141592653589793 * hz * (double) i / 48000.0));
    return b > a ? 2.0 * std::abs (acc) / (double) (b - a) : 0.0;
}
// an effect slot run over a buffer (latency removed)
inline std::pair<std::vector<float>, std::vector<float>> runSlot (const lynx::EffectSlot& slot, std::vector<float> l, std::vector<float> r)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (48000.0);
    const auto lat = fx->latencySamples();
    const auto n = l.size();
    l.resize (n + (std::size_t) lat, 0.0f);
    r.resize (n + (std::size_t) lat, 0.0f);
    for (std::size_t s = 0; s < l.size(); s += 256)
        fx->process (l.data() + s, r.data() + s, (int) std::min<std::size_t> (256, l.size() - s), slot.params, 120.0);
    l.erase (l.begin(), l.begin() + lat);
    r.erase (r.begin(), r.begin() + lat);
    return { l, r };
}
} // namespace meld190ko

TEST (meld190ko_meld_imports_onto_braid_with_its_values_matrix_and_automation)
{
    using namespace lynx;
    namespace b = braid;
    const auto text = meld190ko::meld ({ { "MeldVoice_EngineA_Oscillator_OscillatorType", 8 }, { "MeldVoice_EngineA_Oscillator_Macro1", 0.25 },
                                         { "MeldVoice_EngineB_Filter_Frequency", 1234.5 }, { "MeldVoice_Drive", 0.4 }, { "Volume", 0.7 },
                                         { "MeldVoice_EngineA_On", 1 }, { "MeldVoice_EngineB_On", 0 } },
                                       b::tgt::perEngine + b::tgt::filterFreq, b::src::velocity, 0.5);
    xml::Document doc;
    CHECK (doc.parse (text));
    ChannelDef ch;
    alsdev::Notes notes;
    CHECK (alsdev::meldToBraid (doc.root(), ch, notes));
    CHECK (ch.instrument == InstrumentKind::braid && (int) ch.modelParams.size() == b::stateSize);
    if ((int) ch.modelParams.size() != b::stateSize)
        return;
    CHECK (ch.modelParams[(std::size_t) (b::engineBase (0) + b::eng::oscType)] == 8.0f);
    CHECK (ch.modelParams[(std::size_t) (b::engineBase (0) + b::eng::oscMacro1)] == 0.25f);
    CHECK (std::abs (ch.modelParams[(std::size_t) (b::engineBase (1) + b::eng::filterFreq)] - 1234.5f) < 1e-3f);
    CHECK (ch.modelParams[(std::size_t) b::drive] == 0.4f && ch.modelParams[(std::size_t) b::volume] == 0.7f);
    CHECK (ch.modelParams[(std::size_t) b::matrixIndex (b::tgt::perEngine + b::tgt::filterFreq, b::src::velocity)] == 0.5f);
    // members the element lacks keep Live's default Meld (A's volume −3 dB, the default matrix is replaced by the set's)
    CHECK (ch.modelParams[(std::size_t) (b::engineBase (0) + b::eng::volume)] == -3.0f);
    // every automatable member with a target id is offered to the importer's envelopes, in Live's units
    const auto autos = alsdev::braidAutomatable (doc.root());
    CHECK (autos.size() == 7);
    // the names: Live's element names round-trip through Braid's list
    for (int i = 0; i < b::paramCount; ++i)
        CHECK (! b::liveName (i).empty() && ! b::paramName (i).empty());
    CHECK (b::liveName (b::engineBase (1) + b::eng::modFinal) == "MeldVoice_EngineB_FilterEnvelope_Values_Final");
    CHECK (b::targetName (b::tgt::perEngine + b::tgt::volume) == "B Volume" && b::targetName (b::tgt::pitch) == "A Pitch Mod");
}

TEST (meld190ko_braid_plays_every_oscillator_and_filter_bounded_and_deterministic)
{
    using namespace lynx;
    namespace b = braid;
    std::vector<double> base (b::defaults().begin(), b::defaults().end());
    base[(std::size_t) (b::engineBase (1) + b::eng::on)] = 0.0;
    // every oscillator type sounds, stays finite and bounded, and the same state plays the same samples
    for (int t = 0; t < b::oscTypeCount; ++t)
        for (const double m : { 0.0, 0.5, 1.0 })
        {
            auto s = base;
            s[(std::size_t) (b::engineBase (0) + b::eng::oscType)] = t;
            s[(std::size_t) (b::engineBase (0) + b::eng::oscMacro1)] = m;
            s[(std::size_t) (b::engineBase (0) + b::eng::oscMacro2)] = 1.0 - m;
            const auto x = meld190ko::play (s, 60, 0.6, 0.4);
            bool finite = true;
            float peak = 0.0f;
            for (const auto v : x)
                finite = finite && std::isfinite (v), peak = std::max (peak, std::abs (v));
            CHECK (finite && peak < 2.0f);
            CHECK ((t == 21 && m == 0.0) || meld190ko::rms (x, 0.05, 0.4) > 1.0e-4); // (Crackle at Density 0: a click now and then)
            CHECK (x == meld190ko::play (s, 60, 0.6, 0.4));
        }
    for (int f = 0; f < b::filterTypeCount; ++f)
        for (const double m : { 0.0, 1.0 })
        {
            auto s = base;
            s[(std::size_t) (b::engineBase (0) + b::eng::oscMacro1)] = 0.66; // (a saw)
            s[(std::size_t) (b::engineBase (0) + b::eng::filterType)] = f;
            s[(std::size_t) (b::engineBase (0) + b::eng::filterFreq)] = 1000.0;
            s[(std::size_t) (b::engineBase (0) + b::eng::filterMacro1)] = m;
            s[(std::size_t) (b::engineBase (0) + b::eng::filterMacro2)] = m;
            const auto x = meld190ko::play (s, 48, 0.5, 0.3);
            bool finite = true;
            float peak = 0.0f;
            for (const auto v : x)
                finite = finite && std::isfinite (v), peak = std::max (peak, std::abs (v));
            CHECK (finite && peak < 4.0f);
        }
    // the voice ends after its release (both envelopes done): the engine goes idle
    b::Engine e;
    e.prepare (48000.0);
    e.setValues (base.data());
    e.note (60, 1.0f);
    std::vector<float> l (4800), r (4800);
    e.render (l.data(), r.data(), 4800, 120.0, -1.0);
    CHECK (! e.idle() && e.activeVoices() == 1);
    e.note (60, 0.0f);
    for (int i = 0; i < 10; ++i)
        e.render (l.data(), r.data(), 4800, 120.0, -1.0);
    CHECK (e.idle());
}

TEST (meld190ko_spectral_time_and_resonator_import_onto_timeprism_and_harmonia)
{
    using namespace lynx;
    namespace t = timeprism;
    namespace h = harmonia;
    const std::string st = "<Spectral Id=\"1\">" + meld190ko::on() + meld190ko::par ("Freezer_On", 0) + meld190ko::par ("Freezer_FreezeOn", 0) + meld190ko::par ("Freezer_MainMode", 1) +
                           meld190ko::par ("Freezer_RetriggerMode", 1) + meld190ko::par ("Freezer_SyncIntervalUnits", 0) + meld190ko::par ("Freezer_SyncIntervalBeats", 6) +
                           meld190ko::par ("Freezer_SyncIntervalSeconds", 0.5) + meld190ko::par ("Freezer_FadeType", 0) + meld190ko::par ("Freezer_FadeIn", 0.0056) +
                           meld190ko::par ("Freezer_CrossfadePercent", 0) + meld190ko::par ("Freezer_FadeOut", 0.04) + meld190ko::par ("Freezer_Sensitivity", 0.5) +
                           meld190ko::par ("Delay_On", 1) + meld190ko::par ("Delay_TimeSeconds", 0.25) + meld190ko::par ("Delay_TimeUnit", 0) + meld190ko::par ("Delay_TimeSixteenths", 3) +
                           meld190ko::par ("Delay_TimeDivisions", 15) + meld190ko::par ("Delay_Feedback", 0.6) + meld190ko::par ("Delay_Tilt", 1.0) + meld190ko::par ("Delay_Spray", 0.1) +
                           meld190ko::par ("Delay_Mask", -0.5) + meld190ko::par ("Delay_StereoSpread", 0.25) + meld190ko::par ("Delay_FrequencyShift", 100.0) + meld190ko::par ("Delay_DryWet", 1.0) +
                           meld190ko::par ("InputSendGain", 1) + meld190ko::par ("Resolution", 3) + meld190ko::par ("DryWet", 1) + meld190ko::par ("ProcessingOrder", 0) + "</Spectral>";
    auto tr = als190cf::translate (st);
    CHECK (tr && tr->slot.kind == EffectKind::timeprism && tr->to == "Timeprism" && tr->links.size() == 28);
    if (tr)
    {
        const auto* p = tr->slot.params.data();
        CHECK (std::abs (t::timeSeconds (p[t::time]) - 0.25) < 1e-4 && std::abs (t::tiltOf (p[t::tilt]) - 1.0) < 1e-5);
        CHECK (std::abs (t::sprayOf (p[t::spray]) - 0.1) < 1e-6 && std::abs (t::maskOf (p[t::mask]) + 0.5) < 1e-6);
        CHECK (std::abs (t::shiftHz (p[t::shift]) - 100.0) < 1e-3 && t::resolutionOf (p[t::resolution]) == 3 && p[t::feedback] == 0.6f);
        CHECK (std::abs (t::delaySeconds (p, 120.0) - 0.25) < 1e-4);
    }
    const std::string sr = "<Transmute Id=\"1\">" + meld190ko::on() + meld190ko::par ("PartialFrequencies_FrequencyDialMode", 1) + meld190ko::par ("PartialFrequencies_FrequencyHz", 300) +
                           meld190ko::par ("PartialFrequencies_FrequencyNote", 57) + meld190ko::par ("PartialFrequencies_Shift", 7) +
                           meld190ko::par ("PartialFrequencies_PartialStretch", 0.5) + meld190ko::par ("DecayDamping_DecayTime", 2.0) + meld190ko::par ("DecayDamping_HighFreqDamp", 0.3) +
                           meld190ko::par ("DecayDamping_LowFreqDamp", 0.2) + meld190ko::par ("Modulation_ModMode", 2) + meld190ko::par ("Modulation_ModRate", 0.1) +
                           meld190ko::par ("Modulation_PitchModAmount", 1.0) + meld190ko::par ("Global_PitchMode", 0) + meld190ko::par ("Global_NumHarmonics", 64) +
                           meld190ko::par ("Global_Unison", 2) + meld190ko::par ("Global_UnisonAmount", 0.2) + meld190ko::par ("Global_InputSend", -6) + meld190ko::par ("Global_DryWet", 0.5) +
                           "</Transmute>";
    tr = als190cf::translate (sr);
    CHECK (tr && tr->slot.kind == EffectKind::harmonia && tr->to == "Harmonia" && tr->links.size() == 14);
    if (tr)
    {
        const auto* p = tr->slot.params.data();
        // Freq as a note (dial mode 1): MIDI 57 = 220 Hz, whatever the stored Hz says
        CHECK (std::abs (h::frequencyHz (p[h::frequency]) - 220.0) < 0.05);
        CHECK (std::abs (h::shiftSemis (p[h::shift]) - 7.0) < 1e-4 && h::harmonicCount (p[h::harmonics]) == 64 && h::unisonVoices (p[h::unison]) == 5);
        CHECK (std::abs (h::decaySeconds (p[h::decay]) - 2.0) < 1e-3 && std::abs (h::inputDb (p[h::input]) + 6.0) < 1e-4 && h::modeOf (p[h::modMode]) == 2);
    }
    CHECK (effectInfo (EffectKind::timeprism).params.size() == (std::size_t) t::count);
    CHECK (effectInfo (EffectKind::harmonia).params.size() == (std::size_t) h::count);
}

TEST (meld190ko_envelopes_on_meld_and_the_spectral_devices_move_lynx_parameters)
{
    using namespace lynx;
    namespace b = braid;
    namespace t = timeprism;
    // a set: the MIDI track holds a Meld whose A Osc Macro 1 (target 7000) and B Filter Freq (7001) are under envelopes; the
    // audio track a Spectral Time whose Delay_Feedback (8000) is
    const auto meldText = meld190ko::meld ({ { "MeldVoice_EngineA_Oscillator_Macro1", 0.2 }, { "MeldVoice_EngineB_Filter_Frequency", 2000.0 } });
    const std::string st = "<Spectral Id=\"1\">" + meld190ko::on() + meld190ko::par ("Freezer_On", 0) + meld190ko::par ("Delay_On", 1) +
                           "<Delay_Feedback><Manual Value=\"0.3\" /><AutomationTarget Id=\"8000\" /></Delay_Feedback>" +
                           meld190ko::par ("Delay_TimeSeconds", 0.25) + "</Spectral>";
    const auto text = als190ed::set (meldText, st, als190cf::envelope (1, 7000, 0.0, 1.0) + als190cf::envelope (2, 7001, 20.0, 20480.0),
                                     als190cf::envelope (3, 8000, 0.25, 0.75));
    FlpImportReport report;
    const auto song = als190ed::import (text, report);
    CHECK (song.has_value());
    if (! song)
        return;
    int keys = -1;
    for (int c = 0; c < (int) song->channels.size(); ++c)
        keys = song->channels[(std::size_t) c].name == "Keys" ? c : keys;
    CHECK (keys >= 0 && song->channels[(std::size_t) std::max (keys, 0)].instrument == InstrumentKind::braid);
    const auto macro = b::engineBase (0) + b::eng::oscMacro1, freq = b::engineBase (1) + b::eng::filterFreq;
    const auto* a1 = als190ed::automationOn (*song, { AutomationTargetKind::channelParam, keys, 0, macro });
    CHECK (a1 != nullptr && a1->points.size() == 2 && als190cf::near (a1->points[0].value, 0.0) && als190cf::near (a1->points[1].value, 1.0));
    // the automation value is the parameter's 0..1 over Braid's range (Live's Hz are Braid's own units)
    const auto* a2 = als190ed::automationOn (*song, { AutomationTargetKind::channelParam, keys, 0, freq });
    const auto& fp = braidEngineParams()[(std::size_t) freq];
    CHECK (a2 != nullptr && a2->points.size() == 2 && als190cf::near (a2->points[1].value, (20480.0 - fp.low) / (fp.high - fp.low), 1.0e-4));
    // the Spectral Time's feedback: on the Bus insert's Timeprism
    bool found = false;
    for (const auto& a : song->automations)
        if (a.target.kind == AutomationTargetKind::effectParam && a.target.param == t::feedback && a.points.size() == 2)
            found = als190cf::near (a.points[0].value, 0.25) && als190cf::near (a.points[1].value, 0.75);
    CHECK (found);
    CHECK (report.unsupported.count ("Automation of a device parameter (Keys)") == 0);
}

TEST (meld190ko_measured_laws_of_the_three_engines)
{
    using namespace lynx;
    namespace b = braid;
    namespace t = timeprism;
    namespace h = harmonia;
    // --- Braid: a sine engine at 0 dB peaks at −9 dBFS before Meld's −6 dB volume (Live: −15.1 dBFS) [m1 o00]
    std::vector<double> s (b::defaults().begin(), b::defaults().end());
    s[(std::size_t) (b::engineBase (1) + b::eng::on)] = 0.0;
    s[(std::size_t) (b::engineBase (0) + b::eng::volume)] = 0.0;
    s[(std::size_t) (b::engineBase (0) + b::eng::filterOn)] = 0.0;
    for (int tg = 0; tg < b::targetCount; ++tg)
        for (int src = 0; src < b::sourceCount; ++src)
            s[(std::size_t) b::matrixIndex (tg, src)] = 0.0;
    auto x = meld190ko::play (s, 60, 1.0, 0.9);
    CHECK (std::abs (20.0 * std::log10 (meld190ko::toneAt (x, 261.6256, 0.2, 0.8)) - (-15.05)) < 0.3);
    // Shape 1 = a square: odd harmonics only, the 3rd 9.5 dB under the first [m1 o00 m1-0]
    s[(std::size_t) (b::engineBase (0) + b::eng::oscMacro1)] = 1.0;
    x = meld190ko::play (s, 60, 1.0, 0.9);
    const auto h1 = meld190ko::toneAt (x, 261.6256, 0.2, 0.8), h2 = meld190ko::toneAt (x, 523.25, 0.2, 0.8), h3 = meld190ko::toneAt (x, 784.88, 0.2, 0.8);
    CHECK (std::abs (20.0 * std::log10 (h3 / h1) - (-9.5)) < 0.5 && 20.0 * std::log10 (h2 / h1 + 1e-12) < -60.0);
    // Velocity → Volume 50 % takes 6 dB away at velocity 1 and nothing at 127 [m1 pG]
    s[(std::size_t) (b::engineBase (0) + b::eng::oscMacro1)] = 0.0;
    s[(std::size_t) b::matrixIndex (b::tgt::volume, b::src::velocity)] = 0.5;
    const auto at = [&] (float vel) {
        b::RefNote n;
        n.velocity = vel;
        n.release = 40000;
        const auto o = b::renderReference (s, { n }, 48000.0, 48000);
        std::vector<float> m (o.size() / 2);
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] = o[2 * i];
        return meld190ko::toneAt (m, 261.6256, 0.2, 0.8);
    };
    CHECK (std::abs (20.0 * std::log10 (at (1.0f / 127.0f) / at (1.0f)) - (-6.0)) < 0.2);
    // --- Timeprism: a 250 ms delay of an impulse is one impulse 12 hops of 1024 later (Ultra) [s1 t00 / t08]
    {
        auto slot = makeEffectSlot (EffectKind::timeprism);
        slot.params[t::time] = t::logValue (0.25, 0.01, 3.0);
        slot.params[t::feedback] = 0.0f, slot.params[t::delayMix] = 1.0f, slot.params[t::mix] = 1.0f, slot.params[t::input] = 1.0f;
        slot.params[t::resolution] = t::stepValue (3, 4);
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        l[4800] = r[4800] = 1.0f;
        auto [ol, orr] = meld190ko::runSlot (slot, l, r);
        int peak = 0;
        for (int i = 0; i < (int) ol.size(); ++i)
            peak = std::abs (ol[(std::size_t) i]) > std::abs (ol[(std::size_t) peak]) ? i : peak;
        CHECK (peak - 4800 == 12288 && std::abs (ol[(std::size_t) peak] - 1.0f) < 0.01f);
        // the mixes are equal power: 0.707 of the dry at 50 %
        CHECK (std::abs (t::Engine::dryLaw (0.5) - 0.70711) < 1e-4 && std::abs (t::Engine::wetLaw (0.5) - 0.70711) < 1e-4);
        // Tilt 1 delays the 20 kHz end by about a second, Mask 0.5 bends it to x^1.5 [s1 t03 / t04]
        CHECK (std::abs (t::tiltOffset (1.0, 1.0, 0.0) - 1.0) < 1e-9 && std::abs (t::tiltOffset (0.25, 1.0, 0.5) - 0.125) < 1e-9);
        CHECK (std::abs (t::tiltOffset (0.25, -1.0, 0.0) - 0.75) < 1e-9);
        // Notes: index 15 = one bar, 3 = 1/24 bar [s1 t10]
        CHECK (std::abs (t::divisionBeats (15) - 4.0) < 1e-9 && std::abs (t::divisionBeats (3) - 4.0 / 24.0) < 1e-9);
    }
    // --- Harmonia: the partials' T60 laws [s1 r03]
    CHECK (std::abs (h::partialT60 (1.0, 40, 0.5, 0.0) - 0.31) < 0.02 && std::abs (h::partialT60 (1.0, 5, 1.0, 0.0) - 0.32) < 0.02);
    CHECK (std::abs (h::partialT60 (1.0, 1, 0.0, 1.0) - 0.256) < 0.01 && h::partialT60 (1.0, 11, 0.0, 1.0) == 1.0);
    // Stretch: 1 = odd harmonics, −½ = half-harmonic steps [s1 r04]
    CHECK (h::partialRatio (2, 1.0) == 3.0 && h::partialRatio (3, -0.5) == 2.0);
    {
        auto slot = makeEffectSlot (EffectKind::harmonia);
        slot.params[h::frequency] = h::frequencyValue (110.0), slot.params[h::decay] = h::logValue (1.0, 0.001, 20.0);
        slot.params[h::stretch] = 0.5f, slot.params[h::shift] = 0.5f, slot.params[h::hfDamp] = 0.0f, slot.params[h::lfDamp] = 0.0f;
        slot.params[h::mix] = 1.0f, slot.params[h::harmonics] = 1.0f, slot.params[h::unison] = 0.0f, slot.params[h::input] = 0.5f;
        slot.params[h::modMode] = 0.0f;
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        l[4800] = r[4800] = 1.0f;
        auto [ol, orr] = meld190ko::runSlot (slot, l, r);
        // an impulse rings every harmonic of 110 Hz, about −49 dB each, and nothing between them
        const auto a2 = meld190ko::toneAt (ol, 220.0, 0.12, 0.32), a3 = meld190ko::toneAt (ol, 330.0, 0.12, 0.32), mid = meld190ko::toneAt (ol, 275.0, 0.12, 0.32);
        CHECK (std::abs (20.0 * std::log10 (a2) - 20.0 * std::log10 (a3)) < 1.5 && 20.0 * std::log10 (a2 / (mid + 1e-12)) > 20.0);
        CHECK (20.0 * std::log10 (a2) > -60.0 && 20.0 * std::log10 (a2) < -44.0);
    }
}
