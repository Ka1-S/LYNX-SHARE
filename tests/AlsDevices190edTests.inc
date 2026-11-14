// --- Live's own devices in a set → LYNX's own, lot 3 (round 190ed, core/AlsDevices.h): EQ Three, Drum Buss, Auto
// Filter's LFO / envelope follower / side chain, Operator and Drift, and the envelopes of racks, devices inside racks and
// plugins. The device elements are written by the tests with the member names Live saves; no file of Live's is used.
// Which statements about Live are [ASSUMED] is listed in the round's handoff.

#include "core/AlsDevices.h"
#include "core/AlsImport.h"
#include "core/DynamicsSuite.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/FlWrapper.h"
#include "core/MixSuite.h"
#include "core/Patcher.h"
#include "core/Rack.h"
#include "core/XmlLite.h"

namespace als190ed
{
using als175a::v;
using als190bq::flag;
using als190bq::hasNote;
using als190bq::num;
using als190bq::par;
using als190bq::reportHas;
using als190cf::near;
using als190cf::on;
using als190cf::target;

inline std::optional<lynx::alsdev::Translated> translate (const std::string& text, double bpm = 120.0) { return als190cf::translate (text, bpm); }
// (round 190fk) a Drum Buss as round 190ek's rack: the importer's default is now Anvil Bus (core/AnvilBus.h)
inline std::optional<lynx::alsdev::Translated> translateRack (const std::string& text)
{
    lynx::xml::Document doc;
    doc.parse (text);
    lynx::alsdev::Context context;
    context.drumBussRack = true;
    return lynx::alsdev::effectFromLiveDevice (doc.root(), context);
}

// a set: one MIDI track (Id 10) with `midiDevices` and `midiEnvelopes`, one audio track (Id 20) with `audioDevices` and
// `audioEnvelopes`, a second audio track (Id 30, "Kick") with no devices
inline std::string set (const std::string& midiDevices, const std::string& audioDevices, const std::string& midiEnvelopes = {}, const std::string& audioEnvelopes = {})
{
    using als175a::mixer;
    using als175a::name;
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12117\" Creator=\"test\">\n<LiveSet><Tracks>";
    s += "<MidiTrack Id=\"10\">" + name ("Keys") + v ("Color", "3") + v ("TrackGroupId", "-1") + "<DeviceChain>" + mixer (100, 1.0, 0.0, true) +
         "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting><DeviceChain><Devices>" + midiDevices +
         "</Devices></DeviceChain></DeviceChain><AutomationEnvelopes><Envelopes>" + midiEnvelopes + "</Envelopes></AutomationEnvelopes></MidiTrack>";
    s += "<AudioTrack Id=\"20\">" + name ("Bus") + v ("Color", "20") + v ("TrackGroupId", "-1") + "<DeviceChain>" + mixer (200, 1.0, 0.0, true) +
         "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting><DeviceChain><Devices>" + audioDevices +
         "</Devices></DeviceChain></DeviceChain><AutomationEnvelopes><Envelopes>" + audioEnvelopes + "</Envelopes></AutomationEnvelopes></AudioTrack>";
    s += "<AudioTrack Id=\"30\">" + name ("Kick") + v ("Color", "21") + v ("TrackGroupId", "-1") + "<DeviceChain>" + mixer (300, 1.0, 0.0, true) +
         "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting><DeviceChain><Devices></Devices></DeviceChain></DeviceChain></AudioTrack>";
    s += "</Tracks><MainTrack><DeviceChain>" + mixer (900, 1.0, 0.0, true, {}, "<Tempo><Manual Value=\"120\" /></Tempo><TimeSignature><Manual Value=\"201\" /></TimeSignature>") +
         "</DeviceChain></MainTrack></LiveSet>\n</Ableton>\n";
    return s;
}

inline std::optional<lynx::Song> import (const std::string& text, lynx::FlpImportReport& report)
{
    lynx::xml::Document doc;
    if (! doc.parse (text))
        return std::nullopt;
    auto options = als175a::options (&report);
    std::string error;
    auto song = lynx::importAlsXml (doc, options, &error);
    if (! song)
        std::printf ("  import error: %s\n", error.c_str());
    return song;
}

inline const lynx::Automation* automationOn (const lynx::Song& song, const lynx::AutomationTarget& t)
{
    for (const auto& a : song.automations)
        if (a.target == t)
            return &a;
    return nullptr;
}

inline std::string eqThree (double lo, double mid, double hi, bool lowOn, bool midOn, bool highOn, double freqLo = 250.0, double freqHi = 2500.0, double slope = 1.0)
{
    return "<FilterEQ3 Id=\"3\">" + on() + par ("GainLo", lo) + par ("GainMid", mid) + par ("GainHi", hi) + par ("FreqLo", freqLo) + par ("FreqHi", freqHi) +
           flag ("LowOn", lowOn) + flag ("MidOn", midOn) + flag ("HighOn", highOn) + par ("Slope", slope) + "</FilterEQ3>";
}

inline std::string drumBuss (const std::string& extra)
{
    return "<DrumBuss Id=\"4\">" + on() + extra + "</DrumBuss>";
}

// the older Auto Filter: low pass at `note`, its LFO amount, rate, envelope amount
inline std::string autoFilter (double note, double lfoAmount, double modHub, const std::string& extra = {})
{
    return "<AutoFilter Id=\"5\">" + on() + par ("FilterType", 0) + par ("Cutoff", note) + par ("Resonance", 0.0) + flag ("Slope", false) + par ("ModHub", modHub) +
           par ("Attack", 6.0) + par ("Release", 200.0) + par ("LfoAmount", lfoAmount) + "<Lfo>" + par ("Type", 0) + par ("Frequency", 1.0) + par ("RateType", 1) +
           par ("BeatRate", 9) + par ("Spin", 0.0) + par ("Phase", 0.0) + par ("Offset", 0.0) + flag ("IsOn", true) + "</Lfo>" + extra + "</AutoFilter>";
}

inline std::string sideChain (int track)
{
    return "<SideChain><OnOff><Manual Value=\"true\" /></OnOff><RoutedInput><Routable>" + v ("Target", "AudioIn/Track." + std::to_string (track) + "/PostMixerOut") +
           "</Routable></RoutedInput></SideChain>";
}

// a peak controller run over `seconds` of a constant-amplitude input at 120 BPM, stopped: the last value of `output`
inline float controllerOutput (const lynx::EffectSlot& slot, float amplitude, int output, double seconds = 0.05)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (48000.0);
    fx->setPlaying (false);
    std::vector<float> l (256, amplitude), r (256, amplitude);
    for (int b = 0; b < (int) (seconds * 48000.0 / 256.0); ++b)
    {
        std::fill (l.begin(), l.end(), amplitude);
        std::fill (r.begin(), r.end(), amplitude);
        fx->process (l.data(), r.data(), 256, slot.params, 120.0);
    }
    return fx->controlOutput (output);
}

inline std::unique_ptr<lynx::patcher::Graph> graphOf (const lynx::PatcherDoc& doc)
{
    auto g = std::make_unique<lynx::patcher::Graph> (doc, 48000.0, lynx::patcher::builtInFactories (48000.0));
    g->prepare (48000.0);
    return g;
}

inline std::string operatorDevice (int algorithm, const std::array<double, 4>& volumes, int waveA = 0, int coarseA = 1, const std::string& extra = {})
{
    std::string s = "<Operator Id=\"6\">" + on();
    for (int k = 0; k < 4; ++k)
    {
        const auto n = std::to_string (k);
        s += "<Operator." + n + "><Envelope>" + par ("AttackTime", 2.0) + par ("AttackLevel", 0.000316227757) + par ("DecayTime", 400.0) + par ("DecayLevel", 1.0) +
             par ("SustainLevel", 0.5) + par ("ReleaseTime", 300.0) + par ("ReleaseLevel", 0.000316227757) + par ("AttackSlope", 0.0) + par ("DecaySlope", 1.0) +
             par ("ReleaseSlope", 1.0) + par ("LoopMode", 0) + "</Envelope><Tune>" + par ("Coarse", k == 0 ? coarseA : 1) + par ("Fine", 0.0) +
             flag ("FixedFrequencyOn", false) + "</Tune>" + par ("Volume", volumes[(std::size_t) k]) + flag ("IsOn", true) + par ("WaveForm", k == 0 ? waveA : 0) +
             par ("Feedback", 0.0) + flag ("LfoOn", true) + flag ("PitchEnvOn", true) + "<UserHarmonics>";
        for (int h = 0; h < 64; ++h)
            s += v (("Harmonics." + std::to_string (h)).c_str(), h == 0 ? "1" : (h == 2 ? "0.5" : "0"));
        s += "</UserHarmonics></Operator." + n + ">";
    }
    s += "<Globals>" + par ("Algorithm", algorithm) + par ("Transpose", 0.0) + par ("Volume", 0.5) + par ("Panorama", 0.0) + par ("SpreadAmount", 0.0) +
         par ("Tone", 0.7) + flag ("PortamentoOn", false) + par ("PortamentoTime", 50.0) + "</Globals>" + "<Filter>" + flag ("OnOff", false) + "</Filter><Lfo>" +
         flag ("LfoOn", false) + "</Lfo><PitchEnv>" + flag ("PitchEnvOn", false) + "</PitchEnv>" + extra + "</Operator>";
    return s;
}
} // namespace als190ed

TEST (als190ed_eq_three_becomes_prism)
{
    using namespace lynx;
    using namespace lynx::dyn::prism;
    using als190ed::near;
    // −6 dB low, +3 dB mid, 0 dB high, 48 dB/oct at 300 Hz / 3 kHz
    const auto t = als190ed::translate (als190ed::eqThree (0.501187, 1.412538, 1.0, true, true, true, 300.0, 3000.0, 1.0));
    CHECK (t.has_value() && t->slot.kind == EffectKind::prismMultiband && t->from == "EQ Three" && t->approximate);
    if (! t)
        return;
    const auto& p = t->slot.params;
    CHECK (bandCount (p) == 3 && phaseOf (p[processingMode]) == Phase::minimum);
    CHECK (near (crossoverHz (p[(std::size_t) crossoverParam (0)]), 300.0, 0.5) && near (crossoverHz (p[(std::size_t) crossoverParam (1)]), 3000.0, 3.0));
    CHECK (crossoverSlopeDbOct (p, 0) == 48 && crossoverSlopeDbOct (p, 1) == 48);
    CHECK (near (levelDb (p[(std::size_t) param (0, fLevel)]), -6.0, 0.01) && near (levelDb (p[(std::size_t) param (1, fLevel)]), 3.0, 0.01) &&
           near (levelDb (p[(std::size_t) param (2, fLevel)]), 0.0, 0.01));
    CHECK (p[(std::size_t) param (0, fSolo)] == 0.0f && p[(std::size_t) param (1, fSolo)] == 0.0f && near (rangeDb (p[(std::size_t) param (0, fRange)]), 0.0, 1.0e-6));
    // the static response: each band's level in its middle, flat where the gains are 0 dB
    const double flat[3] { 0.0, 0.0, 0.0 };
    const double levels[3] { -6.0, 3.0, 0.0 };
    CHECK (std::abs (responseDb (p, flat, 48000.0, 1000.0)) < 0.05 && std::abs (responseDb (p, flat, 48000.0, 60.0)) < 0.05);
    CHECK (near (responseDb (p, levels, 48000.0, 40.0), -6.0, 0.2) && near (responseDb (p, levels, 48000.0, 950.0), 3.0, 0.2) &&
           near (responseDb (p, levels, 48000.0, 14000.0), 0.0, 0.2));
    // the links: gains (linear → dB) and the crossovers
    const auto* gain = als190cf::linkOf (*t, "GainMid");
    const auto* freq = als190cf::linkOf (*t, "FreqHi");
    CHECK (gain != nullptr && freq != nullptr);
    if (gain != nullptr && freq != nullptr)
        CHECK (near (levelDb (gain->value (2.0)), 6.02, 0.01) && near (crossoverHz (freq->value (5000.0)), 5000.0, 5.0));
    // a kill switch: the bands that still sound are soloed, the killed one is gone; −inf counts as killed
    const auto killed = als190ed::translate (als190ed::eqThree (1.0, 1.0, 0.000316227757, false, true, true, 250.0, 2500.0, 0.0));
    CHECK (killed.has_value());
    if (killed)
    {
        const auto& k = killed->slot.params;
        CHECK (k[(std::size_t) param (0, fSolo)] == 0.0f && k[(std::size_t) param (1, fSolo)] == 1.0f && k[(std::size_t) param (2, fSolo)] == 0.0f);
        CHECK (crossoverSlopeDbOct (k, 0) == 24);
        const double zero[3] { 0.0, 0.0, 0.0 };
        CHECK (responseDb (k, zero, 48000.0, 30.0) < -20.0 && std::abs (responseDb (k, zero, 48000.0, 790.0)) < 1.0 && responseDb (k, zero, 48000.0, 16000.0) < -20.0);
        CHECK (als190bq::hasNote (killed->notes.approximated, "minimum-phase crossover sum at 24 dB/oct"));
    }
    // every band off: Prism's lowest output, listed
    const auto silent = als190ed::translate (als190ed::eqThree (1.0, 1.0, 1.0, false, false, false));
    CHECK (silent.has_value() && silent && als190bq::hasNote (silent->notes.approximated, "all three bands off"));
}

TEST (als190ed_drum_buss_becomes_a_rack)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    namespace em = mixsuite::ember;
    namespace sh = mixsuite::shell;
    using als190ed::near;
    using als190ed::par;
    // (round 190ek: the chain refitted to Live's renders — Ember → Facet EQ → Crest → Shell Channel; als190ek tests hold the
    // measured numbers) Drive 50 % medium, damping 6 kHz, transients +0.4, boom 0.3 at 60 Hz, trim −6 dB, output −3 dB,
    // 100 % wet, compressor on
    const auto extra = als190ed::flag ("EnableCompression", true) + par ("DriveAmount", 0.5) + par ("DriveType", 1) + par ("CrunchAmount", 0.0) +
                       par ("DampingFrequency", 6000.0) + par ("TransientShaping", 0.4) + par ("BoomFrequency", 60.0) + par ("BoomAmount", 0.3) +
                       par ("BoomDecay", 1.0) + par ("InputTrim", 0.501187) + par ("OutputGain", 0.707946) + par ("DryWet", 1.0);
    const auto t = als190ed::translateRack (als190ed::drumBuss (extra));
    CHECK (t.has_value() && t->slot.kind == EffectKind::patcher && rack::isRack (t->slot));
    if (! t || ! rack::isRack (t->slot))
        return;
    const auto& doc = t->slot.patcher;
    const auto* r = rack::rackOf (doc);
    CHECK (r->chains.size() == 1 && r->chains[0].nodes.size() == 4 && r->name == "Drum Buss");
    if (r->chains.size() != 1 || r->chains[0].nodes.size() != 4)
        return;
    const auto& e = doc.nodes[(std::size_t) r->chains[0].nodes[0]].effect;
    const auto& f = doc.nodes[(std::size_t) r->chains[0].nodes[1]].effect;
    const auto& c = doc.nodes[(std::size_t) r->chains[0].nodes[2]].effect;
    const auto& s = doc.nodes[(std::size_t) r->chains[0].nodes[3]].effect;
    CHECK (e.kind == EffectKind::ember && f.kind == EffectKind::facetEq && c.kind == EffectKind::crestCompressor && s.kind == EffectKind::shellChannel);
    const auto stage = drumBussDriveStage (1, 0.5);
    CHECK (near (em::driveDb (e.params[em::drive]), stage.driveDb, 0.01) && e.params[em::autoGain] == 0.0f);
    CHECK (near (em::highCutWideHz (e.params[em::highCutWide]), 6000.0, 1.0) && e.params[em::wideCuts] == 1.0f);
    CHECK (near (e.params[em::inputGain], (float) ((-6.0 + 24.0) / 48.0), 1.0e-4));
    CHECK (s.params[sh::tsOn] == 1.0f && near (s.params[sh::tsAmount], 0.4, 1.0e-6) && s.params[sh::tsInvert] == 0.0f);
    const auto boom = drumBussBoom (0.3, 60.0, 1.0);
    CHECK (near (facet::hz (f.params[(std::size_t) facet::param (2, 1)]), 60.0, 0.1) && near (facet::gainDb (f.params[(std::size_t) facet::param (2, 2)]), boom.bellDb, 0.01));
    CHECK (s.params[sh::lfOn] == 0.0f && s.params[sh::hfOn] == 0.0f && s.params[sh::lmcOn] == 0.0f && near (sh::gainDb (s.params[sh::output]), -3.0, 0.01));
    // the macros sit where Live's controls are; Live's parameters reach the controls through them
    CHECK (r->macros[dbDrive].name == "Drive" && near (r->macros[dbDrive].value, 63.5f, 1.0e-3) && r->macros[dbDrive].mappings.size() == 3);
    CHECK (r->macros[dbBoomFreq].mappings.size() == 1 && r->macros[dbBoomFreq].mappings[0].param == facet::param (2, 1));
    const auto* drive = als190cf::linkOf (*t, "DriveAmount");
    const auto* trans = als190cf::linkOf (*t, "TransientShaping");
    CHECK (drive != nullptr && drive->param == dbDrive && near (drive->value (1.0), 1.0, 1.0e-6) && trans != nullptr && trans->param == dbTransients);
    CHECK (als190cf::linkOf (*t, "DryWet") == nullptr); // (no dry chain: nothing moves Dry/Wet)
    CHECK (als190bq::hasNote (t->notes.approximated, "drive type Medium") && als190bq::hasNote (t->notes.approximated, "Boom as a resonant bell"));
    // it runs: a 100 Hz sine through the rack comes out, no NaN
    {
        auto g = als190ed::graphOf (doc);
        double energy = 0.0;
        bool finite = true;
        for (int b = 0; b < 40; ++b)
        {
            std::vector<float> l (512), rr (512);
            for (int i = 0; i < 512; ++i)
                l[(std::size_t) i] = rr[(std::size_t) i] = 0.25f * (float) std::sin (2.0 * 3.141592653589793 * 100.0 * (b * 512 + i) / 48000.0);
            g->process (l.data(), rr.data(), nullptr, nullptr, l.data(), rr.data(), 512, 120.0);
            for (int i = 0; i < 512; ++i)
            {
                finite = finite && std::isfinite (l[(std::size_t) i]);
                if (b >= 20)
                    energy += (double) l[(std::size_t) i] * l[(std::size_t) i];
            }
        }
        CHECK (finite && energy > 1.0);
    }
    // 41 % wet and a negative transient: a dry chain beside, the chains' volumes 0.59 / 0.41, Invert on, the macro on Dry/Wet
    const auto wet = als190ed::translateRack (als190ed::drumBuss (par ("DriveAmount", 0.0) + par ("DriveType", 2) + par ("DampingFrequency", 1200.0) +
                                                              par ("TransientShaping", -0.45) + par ("BoomAmount", 0.0) + par ("DryWet", 0.41)));
    CHECK (wet.has_value() && wet && rack::isRack (wet->slot));
    if (wet && rack::isRack (wet->slot))
    {
        const auto* w = rack::rackOf (wet->slot.patcher);
        CHECK (w->chains.size() == 2 && near (w->chains[0].volume, 0.41, 1.0e-6) && near (w->chains[1].volume, 0.59, 1.0e-6) && w->chains[1].nodes.empty());
        const auto& we = wet->slot.patcher.nodes[(std::size_t) w->chains[0].nodes[0]].effect;
        const auto& ws = wet->slot.patcher.nodes[(std::size_t) w->chains[0].nodes[2]].effect;
        CHECK (we.kind == EffectKind::ember && we.params[em::wideCuts] == 1.0f && near (em::highCutWideHz (we.params[em::highCutWide]), 1200.0, 1.0));
        CHECK (ws.kind == EffectKind::shellChannel && ws.params[sh::tsInvert] == 1.0f && near (ws.params[sh::tsAmount], 0.45, 1.0e-6));
        CHECK (w->macros[dbDryWet].mappings.size() == 2 && als190cf::linkOf (*wet, "DryWet") != nullptr && als190cf::linkOf (*wet, "TransientShaping") == nullptr);
        CHECK (als190bq::hasNote (wet->notes.approximated, "negative transients"));
    }
}

TEST (als190ed_peak_controller_laws)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190ed::near;
    // the amounts come back through the controller's own processing: peak k × a constant input level, LFO base + k × wave
    for (const double k : { 0.25, 0.5, 1.0 })
    {
        auto slot = makeEffectSlot (EffectKind::peakController);
        slot.params[0] = 0.0f;
        slot.params[1] = peakAmountValue (k);
        slot.params[2] = 0.5f;
        slot.params[5] = 0.5f; // no LFO
        CHECK (near (als190ed::controllerOutput (slot, 0.5f, 1), 0.5 * k, 2.0e-3));
    }
    // the LFO's period: a square wave (shape 2) at a quarter-note period changes sign every 0.25 s at 120 BPM
    {
        auto slot = makeEffectSlot (EffectKind::peakController);
        slot.params[1] = 0.5f;
        slot.params[4] = 0.0f;
        slot.params[5] = peakLfoAmountValue (1.0);
        slot.params[6] = 0.5f;
        slot.params[7] = peakLfoSpeedValue (1.0);
        slot.params[8] = 0.5f; // square
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        fx->setPlaying (false);
        std::vector<float> l (48), r (48);
        std::vector<float> outs;
        for (int b = 0; b < 2000; ++b) // 2 s in 1 ms blocks
        {
            fx->process (l.data(), r.data(), 48, slot.params, 120.0);
            outs.push_back (fx->controlOutput (2));
        }
        int flips = 0;
        for (std::size_t i = 1; i < outs.size(); ++i)
            flips += (outs[i] > 0.5f) != (outs[i - 1] > 0.5f) ? 1 : 0;
        CHECK (flips >= 7 && flips <= 9); // 2 s / 0.25 s = 8 half periods
        CHECK (near (*std::max_element (outs.begin(), outs.end()), 1.0, 1.0e-3));
    }
    // the decay: a peak of 1 falls to 0 over the release time (200 ms at 120 BPM → 0.4 quarter notes)
    {
        auto slot = makeEffectSlot (EffectKind::peakController);
        slot.params[0] = 0.0f;
        slot.params[1] = peakAmountValue (1.0);
        slot.params[2] = 0.5f;
        slot.params[3] = peakDecayValue (200.0, 120.0);
        slot.params[5] = 0.5f;
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        fx->setPlaying (false);
        std::vector<float> l (48, 1.0f), r (48, 1.0f);
        fx->process (l.data(), r.data(), 48, slot.params, 120.0);
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        float at100 = -1.0f;
        for (int b = 1; b <= 100; ++b)
        {
            fx->process (l.data(), r.data(), 48, slot.params, 120.0);
            if (b == 100)
                at100 = fx->controlOutput (1);
        }
        CHECK (near (at100, 0.5, 0.02)); // halfway after 100 ms
    }
    // the curve: c0 = 0.5, an LFO of ±0.1 and an envelope of +0.2 → from 0.4 over a span of 0.4; a negative envelope turns
    // the curve over (and the LFO with it)
    const auto up = cutoffModulation (0.5, 0.1, 0.2);
    CHECK (up.curve.size() == 33 && near (up.curve.front(), 0.4, 1.0e-6) && near (up.curve.back(), 0.8, 1.0e-6) && ! up.invert);
    const auto down = cutoffModulation (0.5, 0.1, -0.2);
    CHECK (near (down.curve.front(), 0.6, 1.0e-6) && near (down.curve.back(), 0.2, 1.0e-6) && down.invert);
    const auto clipped = cutoffModulation (0.95, 0.1, 0.0);
    CHECK (near (clipped.curve.front(), 0.85, 1.0e-6) && clipped.curve.back() == 1.0f);
}

TEST (als190ed_auto_filter_modulation_is_a_linked_peak_controller)
{
    using namespace lynx;
    using als190ed::near;
    // Bus: an Auto Filter at C6 (note 84 ≈ 1047 Hz) with a synced LFO (1 bar … index 9 = a quarter note) of 12 semitones
    // and an envelope follower of +24 semitones; Keys: an Auto Filter whose envelope listens to Kick (track 30)
    const auto bus = als190ed::autoFilter (84.0, 12.0, 24.0);
    const auto keys = als190ed::autoFilter (60.0, 0.0, 36.0, als190ed::sideChain (30));
    FlpImportReport report;
    const auto song = als190ed::import (als190ed::set (keys, bus), report);
    CHECK (song.has_value());
    if (! song)
        return;
    int keysInsert = -1, busInsert = -1, kickInsert = -1;
    for (int i = 0; i < (int) song->mixer.size(); ++i)
    {
        keysInsert = song->mixer[(std::size_t) i].name == "Keys" ? i : keysInsert;
        busInsert = song->mixer[(std::size_t) i].name == "Bus" ? i : busInsert;
        kickInsert = song->mixer[(std::size_t) i].name == "Kick" ? i : kickInsert;
    }
    CHECK (busInsert > 0 && keysInsert > 0 && kickInsert > 0);
    if (busInsert <= 0 || keysInsert <= 0 || kickInsert <= 0)
        return;
    // the bus: the controller in front of the filter, the link from its output 0 to the cutoff
    const auto& fx = song->mixer[(std::size_t) busInsert].effects;
    CHECK (fx.size() == 2 && fx[0].kind == EffectKind::peakController && fx[1].kind == EffectKind::filter);
    const Automation* link = nullptr;
    for (const auto& a : song->automations)
        if (a.isLive() && a.target == AutomationTarget { AutomationTargetKind::effectParam, busInsert, 1, 1 })
            link = &a;
    CHECK (link != nullptr && link->source.track == busInsert && link->source.slot == 0 && link->source.output == 0 && link->curve.size() == 33);
    if (link != nullptr && fx.size() == 2)
    {
        // ±1 octave of LFO and +2 octaves of envelope in the cutoff's units (log2 1000 octaves per unit)
        const double perOct = 1.0 / 9.965784284662087;
        const double c0 = fx[1].params[1];
        // (round 190ek: ModHub is 0.94 semitone per unit at a full-scale peak [MEASURED])
        CHECK (near (link->curve.front(), c0 - perOct, 1.0e-4) && near (link->curve.back(), c0 - perOct + (2.0 + 2.0 * 0.94) * perOct, 1.0e-4));
        // the controller: no base, a quarter-note LFO, the envelope's amount and release
        CHECK (fx[0].params[0] == 0.0f && fx[0].params[4] == 0.0f && fx[0].params[10] == 0.0f && fx[0].params[11] == 0.0f);
        CHECK (near (fx[0].params[7], alsdev::peakLfoSpeedValue (1.0), 1.0e-6) && near (fx[0].params[3], alsdev::peakDecayValue (200.0, 120.0), 1.0e-6));
    }
    CHECK (report.partialEffects.count ("Auto Filter LFO and envelope follower -> Peak Controller linked to the Filter's cutoff") == 1);
    CHECK (report.partialEffects.count ("Auto Filter envelope follower -> Peak Controller linked to the Filter's cutoff") == 1);
    CHECK (! als190ed::reportHas (report.partialEffects, "LFO on the cutoff (not applied)"));
    // the side chain: the controller at the end of Kick's insert, the link to Keys' filter
    const auto& kick = song->mixer[(std::size_t) kickInsert].effects;
    const auto& keysFx = song->mixer[(std::size_t) keysInsert].effects;
    CHECK (kick.size() == 1 && kick[0].kind == EffectKind::peakController && keysFx.size() == 1 && keysFx[0].kind == EffectKind::filter);
    const Automation* side = nullptr;
    for (const auto& a : song->automations)
        if (a.isLive() && a.target == AutomationTarget { AutomationTargetKind::effectParam, keysInsert, 0, 1 })
            side = &a;
    CHECK (side != nullptr && side->source.track == kickInsert && side->source.slot == 0);

    // inside a rack no link can be made: listed
    const auto rack = "<AudioEffectGroupDevice Id=\"7\">" + als190ed::on() + als175a::v ("UserName", "Wobble") + "<Branches><AudioEffectBranch Id=\"0\"><Name>" +
                      als175a::v ("EffectiveName", "Filter") + als175a::v ("UserName", "") + "</Name>" + als175a::v ("IsSoloed", "false") +
                      "<DeviceChain><AudioToAudioDeviceChain><Devices>" + als190ed::autoFilter (84.0, 12.0, 0.0) + "</Devices></AudioToAudioDeviceChain></DeviceChain>" +
                      als190cf::range ("BranchSelectorRange", 0, 127, 0, 127) + als190cf::branchMixer (1.0, 0.0) + "</AudioEffectBranch></Branches>" +
                      als190cf::macros ({}, {}) + "</AudioEffectGroupDevice>";
    FlpImportReport rackReport;
    const auto racked = als190ed::import (als190ed::set ({}, rack), rackReport);
    CHECK (racked.has_value() && rackReport.partialEffects.count ("Auto Filter: LFO on the cutoff inside a rack (not applied)") == 1);
}

TEST (als190ed_operator_and_drift_become_the_synth)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190ed::near;
    using als190ed::par;
    // the ratio as the synth's pitch: 0.5 = −12, 3 = +19.02, 1 + 500/1000 = +7.02
    CHECK (near (operatorRatioSemitones (0, 0.0), -12.0) && near (operatorRatioSemitones (3, 0.0), 19.0196, 1.0e-3) && near (operatorRatioSemitones (1, 500.0), 7.0196, 1.0e-3));
    SynthOscillator o;
    setOscillatorPitch (o, 19.0196);
    CHECK (o.octave == 1 && o.semitones == 7 && near (o.detune, 1.96, 0.01));
    setOscillatorPitch (o, -12.0);
    CHECK (o.octave == -1 && o.semitones == 0 && near (o.detune, 0.0, 1.0e-6));
    // operator A alone (B … D at −inf), a sine at ratio 2
    {
        xml::Document doc;
        CHECK (doc.parse (als190ed::operatorDevice (0, { 0.8, 0.000316227757, 0.000316227757, 0.000316227757 }, 0, 2)));
        ChannelDef c;
        Notes notes;
        CHECK (operatorToSynth (doc.root(), c, 120.0, notes));
        CHECK (c.instrument == InstrumentKind::synth && c.synth.osc[0].wave == Waveform::sine && c.synth.osc[0].octave == 1 && c.synth.osc[0].semitones == 0 &&
               near (c.synth.osc[0].level, 0.8, 1.0e-6) && c.synth.osc[1].level == 0.0f && c.synth.osc[2].level == 0.0f);
        CHECK (near (c.synth.attack, 0.002, 1.0e-6) && near (c.synth.decay, 0.4, 1.0e-6) && near (c.synth.sustain, 0.5, 1.0e-6) && near (c.synth.release, 0.3, 1.0e-6));
        CHECK (near (channelGain (c.volume), 0.5, 1.0e-4) && near (c.synth.cutoff, 20000.0, 1.0e-3));
    }
    // FM in use (B sounding into A in algorithm 1): not translated
    {
        xml::Document doc;
        CHECK (doc.parse (als190ed::operatorDevice (0, { 1.0, 0.3, 0.000316227757, 0.000316227757 })));
        ChannelDef c;
        Notes notes;
        CHECK (! operatorToSynth (doc.root(), c, 120.0, notes));
    }
    // algorithm 11: carriers side by side; A's User wave = its harmonic table
    {
        xml::Document doc;
        CHECK (doc.parse (als190ed::operatorDevice (10, { 1.0, 0.5, 0.25, 0.000316227757 }, 22)));
        ChannelDef c;
        Notes notes;
        CHECK (operatorToSynth (doc.root(), c, 120.0, notes));
        CHECK (c.synth.osc[0].wave == Waveform::spectral && c.synth.spectrum[0] == 1.0f && c.synth.spectrum[2] == 0.5f && c.synth.spectrum[1] == 0.0f);
        CHECK (c.synth.osc[1].wave == Waveform::sine && near (c.synth.osc[1].level, 0.5, 1.0e-6) && near (c.synth.osc[2].level, 0.25, 1.0e-6));
        CHECK (als190bq::hasNote (notes.approximated, "the carriers of algorithm 11"));
    }
    // Drift: saw (type 4) an octave down, rectangle +7 st (detune 7), noise; the 24 dB filter at 2 kHz, the amp envelope, mono
    {
        const auto drift = "<Drift Id=\"8\">" + als190ed::on() + par ("Oscillator1_Type", 4) + par ("Oscillator1_Shape", 0.0) + par ("Oscillator1_Transpose", -1) +
                           par ("Oscillator2_Type", 4) + par ("Oscillator2_Detune", 7.0) + par ("Oscillator2_Transpose", 0) + par ("Mixer_OscillatorGain1", 0.7) +
                           als190ed::flag ("Mixer_OscillatorOn1", true) + par ("Mixer_OscillatorGain2", 0.5) + als190ed::flag ("Mixer_OscillatorOn2", true) +
                           par ("Mixer_NoiseLevel", 0.1) + als190ed::flag ("Mixer_NoiseOn", true) + par ("Filter_Type", 1) + par ("Filter_Frequency", 2000.0) +
                           par ("Filter_Resonance", 0.3) + par ("Filter_Tracking", 0.5) + par ("Filter_HiPassFrequency", 10.0) + par ("Envelope1_Attack", 0.01) +
                           par ("Envelope1_Decay", 0.2) + par ("Envelope1_Sustain", 0.6) + par ("Envelope1_Release", 0.4) + par ("Global_VoiceMode", 1) +
                           par ("Global_Glide", 0.05) + par ("Global_Volume", 0.5) + par ("Global_Transpose", 0) + par ("Global_DriftDepth", 0.07) + "</Drift>";
        xml::Document doc;
        CHECK (doc.parse (drift));
        ChannelDef c;
        Notes notes;
        CHECK (driftToSynth (doc.root(), c, 120.0, notes));
        CHECK (c.synth.osc[0].wave == Waveform::saw && c.synth.osc[0].octave == -1 && near (c.synth.osc[0].level, 0.7, 1.0e-6));
        CHECK (c.synth.osc[1].wave == Waveform::square && c.synth.osc[1].semitones == 7 && c.synth.osc[2].wave == Waveform::noise && near (c.synth.osc[2].level, 0.1, 1.0e-6));
        CHECK (c.synth.ladder && near (c.synth.cutoff, 2000.0, 1.0e-3) && near (c.synth.resonance, 0.3, 1.0e-6) && near (c.synth.keyTrack, 0.5, 1.0e-6));
        CHECK (near (c.synth.attack, 0.01, 1.0e-6) && near (c.synth.sustain, 0.6, 1.0e-6) && c.synth.mono && c.synth.porta && near (c.synth.portaBeats, 0.1, 1.0e-6));
        CHECK (als190bq::hasNote (notes.dropped, "drift (the random detune") && als190bq::hasNote (notes.dropped, "modulation (envelope 2"));
    }
    // through the importer: Operator with FM stays unsupported with its reason; the plain one is a fitted synth channel
    FlpImportReport report;
    const auto song = als190ed::import (als190ed::set (als190ed::operatorDevice (0, { 1.0, 0.000316227757, 0.000316227757, 0.000316227757 }), {}), report);
    CHECK (song.has_value() && report.partialEffects.count ("Operator -> built-in synth (settings fitted to LYNX's engine)") == 1);
    FlpImportReport fmReport;
    const auto fm = als190ed::import (als190ed::set (als190ed::operatorDevice (0, { 1.0, 0.5, 0.000316227757, 0.000316227757 }), {}), fmReport);
    CHECK (fm.has_value() && fmReport.unsupported.count ("Operator (FM synthesis: no LYNX engine for it yet)") == 1);
}

TEST (als190ed_envelopes_of_racks_and_plugins_follow)
{
    using namespace lynx;
    using als190ed::near;
    using als190ed::par;
    using als190ed::target;
    using als175a::v;
    // Bus: an Audio Effect Rack with macro 0 (target 700) mapped on a Saturator's drive and automated, the Saturator's
    // Dry/Wet automated (701: a free macro carries it), a nested rack whose macro 2 is automated (702); a VST2 plugin whose
    // parameter 3 is automated (703); a VST3 plugin whose parameter is automated (704: carried by its id since round 190ek)
    const auto saturator = "<Saturator Id=\"0\">" + als190ed::on() + v ("UserName", "") + target ("PreDrive", 6.0, 600, als190cf::mapped (0, 0.0, 12.0)) +
                           par ("Type", 0) + par ("PostDrive", 0.0) + target ("DryWet", 0.5, 701) + "</Saturator>";
    const auto innerMacros = [] {
        auto m = als190cf::macros ({}, { 0, 0, 64 });
        const auto at = m.find ("<MacroControls.2>");
        m.replace (at, std::string ("<MacroControls.2>").size(), "<MacroControls.2><AutomationTarget Id=\"702\" />");
        return m;
    }();
    const auto inner = "<AudioEffectGroupDevice Id=\"9\">" + als190ed::on() + v ("UserName", "Inner") + "<Branches><AudioEffectBranch Id=\"0\"><Name>" +
                       v ("EffectiveName", "Empty") + v ("UserName", "") + "</Name>" + v ("IsSoloed", "false") +
                       "<DeviceChain><AudioToAudioDeviceChain><Devices></Devices></AudioToAudioDeviceChain></DeviceChain>" +
                       als190cf::range ("BranchSelectorRange", 0, 127, 0, 127) + als190cf::branchMixer (1.0, 0.0) + "</AudioEffectBranch></Branches>" + innerMacros +
                       "</AudioEffectGroupDevice>";
    auto outerMacros = als190cf::macros ({ "Drive" }, { 64 });
    outerMacros.replace (outerMacros.find ("<MacroControls.0>"), std::string ("<MacroControls.0>").size(), "<MacroControls.0><AutomationTarget Id=\"700\" />");
    const auto rack = "<AudioEffectGroupDevice Id=\"5\">" + als190ed::on() + v ("UserName", "Dirt") + "<Branches><AudioEffectBranch Id=\"0\"><Name>" +
                      v ("EffectiveName", "Chain") + v ("UserName", "") + "</Name>" + v ("IsSoloed", "false") + "<DeviceChain><AudioToAudioDeviceChain><Devices>" +
                      saturator + inner + "</Devices></AudioToAudioDeviceChain></DeviceChain>" + als190cf::range ("BranchSelectorRange", 0, 127, 0, 127) +
                      als190cf::branchMixer (1.0, 0.0) + "</AudioEffectBranch></Branches>" + outerMacros + "</AudioEffectGroupDevice>";
    const auto params = [] (int pid, int target, double value) {
        return "<ParameterList><PluginFloatParameter Id=\"0\">" + v ("ParameterName", "Gain") + v ("ParameterId", std::to_string (pid)) + v ("VisualIndex", "0") +
               "<ParameterValue><Manual Value=\"" + als190ed::num (value) + "\" /><AutomationTarget Id=\"" + std::to_string (target) +
               "\" /></ParameterValue></PluginFloatParameter></ParameterList>";
    };
    auto vst2 = als175a::vst2Device ("00");
    vst2.insert (vst2.rfind ("</PluginDevice>"), params (3, 703, 0.25));
    auto vst3 = als175a::vst3Device ("00");
    vst3.insert (vst3.rfind ("</PluginDevice>"), params (12345678, 704, 0.5));
    // Keys: an Instrument Rack (one chain: a VST2 instrument plugin) whose macro 1 is automated (710)
    auto keyMacros = als190cf::macros ({}, { 0, 32 });
    keyMacros.replace (keyMacros.find ("<MacroControls.1>"), std::string ("<MacroControls.1>").size(), "<MacroControls.1><AutomationTarget Id=\"710\" />");
    auto instrument = als175a::vst2Device ("00");
    instrument.replace (instrument.find ("<VstPluginInfo Id=\"0\">"), std::string ("<VstPluginInfo Id=\"0\">").size(), "<VstPluginInfo Id=\"0\">" + v ("NumAudioInputs", "0"));
    const auto keysRack = "<InstrumentGroupDevice Id=\"6\">" + als190ed::on() + "<Branches><InstrumentBranch Id=\"0\"><Name>" + v ("EffectiveName", "Synth") +
                          "</Name><DeviceChain><MidiToAudioDeviceChain><Devices>" + instrument + "</Devices></MidiToAudioDeviceChain></DeviceChain>" +
                          als190cf::range ("BranchSelectorRange", 0, 127, 0, 127) + "<ZoneSettings>" + als190cf::range ("KeyRange", 0, 127, 0, 127) +
                          als190cf::range ("VelocityRange", 1, 127, 1, 127) + "</ZoneSettings>" + als190cf::branchMixer (1.0, 0.0) + "</InstrumentBranch></Branches>" +
                          keyMacros + "</InstrumentGroupDevice>";
    const auto busEnvelopes = als190cf::envelope (0, 700, 0.0, 127.0) + als190cf::envelope (1, 701, 0.2, 0.8) + als190cf::envelope (2, 702, 127.0, 0.0) +
                              als190cf::envelope (3, 703, 0.1, 0.9) + als190cf::envelope (4, 704, 0.0, 1.0);
    const auto keysEnvelopes = als190cf::envelope (5, 710, 0.0, 63.5);
    FlpImportReport report;
    const auto song = als190ed::import (als190ed::set (keysRack, rack + vst2 + vst3, keysEnvelopes, busEnvelopes), report);
    CHECK (song.has_value());
    if (! song)
        return;
    int bus = -1;
    for (int i = 0; i < (int) song->mixer.size(); ++i)
        bus = song->mixer[(std::size_t) i].name == "Bus" ? i : bus;
    CHECK (bus > 0 && song->mixer[(std::size_t) bus].effects.size() == 3);
    if (bus <= 0 || song->mixer[(std::size_t) bus].effects.size() != 3)
        return;
    const auto& slot = song->mixer[(std::size_t) bus].effects[0];
    const auto* r = rack::rackOf (slot.patcher);
    CHECK (r != nullptr);
    if (r == nullptr)
        return;
    // macro 0: its own envelope, 0 … 127 → 0 … 1
    const auto* m0 = als190ed::automationOn (*song, { AutomationTargetKind::effectParam, bus, 0, 0 });
    CHECK (m0 != nullptr && m0->points.size() == 2 && m0 != nullptr && near (m0->points[0].value, 0.0) && near (m0->points[1].value, 1.0));
    // the Saturator's Dry/Wet: the first free macro (1), over Ember's mix, at its value now (0.5), excluded from Rand
    int bound = -1;
    for (int k = 0; k < rackMacroCount; ++k)
        if (r->macros[(std::size_t) k].name == "Saturator Dry Wet")
            bound = k;
    CHECK (bound == 1 && r->macros[1].mappings.size() == 1 && r->macros[1].mappings[0].param == mixsuite::ember::mix && near (r->macros[1].value, 0.5f * 127.0f, 1.0e-3) &&
           r->macros[1].excludeFromRandom && r->macros[1].excludeFromVariations);
    const auto* mix = als190ed::automationOn (*song, { AutomationTargetKind::effectParam, bus, 0, 1 });
    CHECK (mix != nullptr && mix->points.size() == 2);
    if (mix != nullptr && mix->points.size() == 2)
        CHECK (near (mix->points[0].value, 0.2, 1.0e-6) && near (mix->points[1].value, 0.8, 1.0e-6));
    // the nested rack's macro 2: an outer free macro (2) mapped over the inner macro's whole range
    CHECK (r->macros[2].mappings.size() == 1 && r->macros[2].mappings[0].param == 2 && near (r->macros[2].value, 64.0f, 1.0e-3));
    const auto* nested = als190ed::automationOn (*song, { AutomationTargetKind::effectParam, bus, 0, 2 });
    CHECK (nested != nullptr && nested->points.size() == 2 && nested != nullptr && near (nested->points[0].value, 1.0) && near (nested->points[1].value, 0.0));
    CHECK (r->shownMacros >= 4);
    // the bound macro really moves the Saturator's mix in the rack
    {
        auto doc = slot.patcher;
        rack::setMacro (doc, 1, 127.0f);
        int ember = -1;
        for (int n = 0; n < (int) doc.nodes.size(); ++n)
            if (! doc.nodes[(std::size_t) n].generator && doc.nodes[(std::size_t) n].effect.kind == EffectKind::ember)
                ember = n;
        CHECK (ember >= 0 && near (doc.nodes[(std::size_t) ember].effect.params[mixsuite::ember::mix], 1.0, 1.0e-6));
    }
    // the VST2 plugin's parameter 3; the VST3 one listed once, without the generic line
    const auto* gain = als190ed::automationOn (*song, { AutomationTargetKind::effectParam, bus, 1, 3 });
    CHECK (gain != nullptr && gain->points.size() == 2 && gain != nullptr && near (gain->points[0].value, 0.1) && near (gain->points[1].value, 0.9));
    // (round 190ek) the VST3 one is carried by the plugin's own id: slot 0, its state holding that id
    const auto* v3 = als190ed::automationOn (*song, { AutomationTargetKind::effectParam, bus, 2, 0 });
    CHECK (v3 != nullptr && v3->points.size() == 2 && v3 != nullptr && near (v3->points[0].value, 0.0) && near (v3->points[1].value, 1.0));
    CHECK (flVst3ParamSlots (song->mixer[(std::size_t) bus].effects[2].plugin.foreign.state) == (std::vector<std::int32_t> { 12345678 }));
    CHECK (report.unsupported.count ("Automation of a VST3 plugin's parameter (kHs Gain: Live keeps the plugin's own id, not its index)") == 0);
    CHECK (report.unsupported.count ("Automation of a device parameter (Bus)") == 0);
    // the Instrument Rack's macro 1 on the Keys channel: 0 … 63.5 → 0 … 0.5
    int keys = -1;
    for (int c = 0; c < (int) song->channels.size(); ++c)
        keys = song->channels[(std::size_t) c].name == "Keys" ? c : keys;
    CHECK (keys >= 0 && song->channels[(std::size_t) std::max (keys, 0)].instrument == InstrumentKind::patcher);
    const auto* macro1 = als190ed::automationOn (*song, { AutomationTargetKind::channelParam, keys, 0, 1 });
    CHECK (macro1 != nullptr && macro1->points.size() == 2 && macro1 != nullptr && near (macro1->points[1].value, 0.5));
    CHECK (report.unsupported.count ("Automation of a device parameter (Keys)") == 0);
}
