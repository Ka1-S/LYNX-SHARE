// --- .als lot 3 refitted to Live's renders (round 190ek; the renders and saves of round 190ei, Live 12.4): Auto Filter's
// modulation laws, Drift's voice modes, VST3 automation by the plugin's own parameter id, the Drum Buss rack's stages, and
// the measured numbers EQ Three / Operator / Drift already met. The device elements are written by the tests with the
// member names Live saves; no file of Live's is used. Each number below quotes the measurement it pins.

#include <map>

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

namespace als190ek
{
using als175a::v;
using als190bq::flag;
using als190bq::hasNote;
using als190bq::par;
using als190cf::near;
using als190cf::on;

// the new Auto Filter: low pass at 1 kHz, its LFO and envelope
inline std::string autoFilter2 (double lfoAmount, int timeMode, double envAmount = 0.0, int waveform = 0)
{
    return "<AutoFilter2 Id=\"5\">" + on() + par ("Filter_Type", 0) + par ("Filter_Slope", 0) + par ("Filter_Frequency", 1000.0) + par ("Filter_Resonance", 0.0) +
           par ("Lfo_Amount", lfoAmount) + par ("Lfo_Waveform", waveform) + par ("Lfo_TimeMode", timeMode) + par ("Lfo_Frequency", 1.0) + par ("Lfo_Time", 0.8) +
           par ("Lfo_SyncedRate", 9) + par ("Lfo_Sixteenth", 3) + par ("Lfo_Phase", 0.0) + par ("Lfo_PhaseOffset", 0.0) + par ("Lfo_Spin", 0.0) +
           par ("Envelope_Amount", envAmount) + par ("Envelope_Attack", 0.001) + par ("Envelope_Release", 0.25) + par ("DryWet", 1.0) + par ("Output", 1.0) + "</AutoFilter2>";
}

// the older one at C6 with a 1 Hz LFO of `type`
inline std::string autoFilterAt1Hz (double lfoAmount, int type, double modHub = 0.0)
{
    return "<AutoFilter Id=\"5\">" + on() + par ("FilterType", 0) + par ("Cutoff", 83.213) + par ("Resonance", 0.0) + flag ("Slope", false) + par ("ModHub", modHub) +
           par ("Attack", 6.0) + par ("Release", 200.0) + par ("LfoAmount", lfoAmount) + "<Lfo>" + par ("Type", type) + par ("Frequency", 1.0) + par ("RateType", 0) +
           par ("BeatRate", 9) + par ("Spin", 0.0) + par ("Phase", 0.0) + par ("Offset", 0.0) + flag ("IsOn", true) + "</Lfo></AutoFilter>";
}

// a Drum Buss with every control at the all-zero setting but those in `values` (and `extra`, e.g. EnableCompression)
inline std::string drumBuss (double drive, int type, const std::map<std::string, double>& values = {}, const std::string& extra = {})
{
    std::map<std::string, double> p { { "TransientShaping", 0.0 }, { "BoomAmount", 0.0 },  { "BoomFrequency", 50.0 },     { "BoomDecay", 0.5 },
                                      { "CrunchAmount", 0.0 },     { "DampingFrequency", 20000.0 }, { "InputTrim", 1.0 }, { "OutputGain", 1.0 }, { "DryWet", 1.0 } };
    for (const auto& [k, x] : values)
        p[k] = x;
    std::string s = "<DrumBuss Id=\"4\">" + on() + par ("DriveAmount", drive) + par ("DriveType", type);
    for (const auto& [k, x] : p)
        s += par (k.c_str(), x);
    return s + extra + "</DrumBuss>";
}

// the rack's output for a sine at `hz`, `db` dBFS peak: its fundamental in dBFS (after the rack's latency, steady state)
inline double sineThrough (const lynx::PatcherDoc& doc, double hz, double db)
{
    auto g = als190ed::graphOf (doc);
    const auto amp = std::pow (10.0, db / 20.0);
    const int n = 48000;
    std::vector<float> l (512), r (512), out;
    for (int at = 0; at < n; at += 512)
    {
        for (int i = 0; i < 512; ++i)
            l[(std::size_t) i] = r[(std::size_t) i] = (float) (amp * std::sin (2.0 * 3.141592653589793 * hz * (at + i) / 48000.0));
        g->process (l.data(), r.data(), nullptr, nullptr, l.data(), r.data(), 512, 120.0);
        out.insert (out.end(), l.begin(), l.end());
    }
    // lock-in over the last 0.5 s
    double re = 0.0, im = 0.0;
    for (int i = n / 2; i < n; ++i)
    {
        re += out[(std::size_t) i] * std::cos (2.0 * 3.141592653589793 * hz * i / 48000.0);
        im += out[(std::size_t) i] * std::sin (2.0 * 3.141592653589793 * hz * i / 48000.0);
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / (n / 2));
}

inline const lynx::EffectSlot* nodeOf (const lynx::PatcherDoc& doc, lynx::EffectKind kind)
{
    const auto* r = lynx::rack::rackOf (doc);
    if (r == nullptr || r->chains.empty())
        return nullptr;
    for (const auto n : r->chains[0].nodes)
        if (doc.nodes[(std::size_t) n].effect.kind == kind)
            return &doc.nodes[(std::size_t) n].effect;
    return nullptr;
}
} // namespace als190ek

TEST (als190ek_auto_filter_rates_and_depths_are_live_s)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190ek::near;
    // the new device's synced table is the older one turned round [MEASURED: index 9 = 1.5 beats]: 0 = 8 bars, 21 = 1/64
    CHECK (near (autoFilter2SyncedBeats (9), 1.5) && near (autoFilter2SyncedBeats (0), 32.0) && near (autoFilter2SyncedBeats (21), 0.0625));
    CHECK (near (autoFilter2SyncedBeats (12), liveBeatRateBeats (9)) && near (autoFilter2SyncedBeats (15), 0.5)); // a quarter note, an eighth
    // Lfo_TimeMode at 120 BPM [MEASURED periods 1.000 / 0.799 / 0.750 / 0.500 / 1.126 / 0.375 s]
    bool synced = true;
    CHECK (near (autoFilter2LfoBeats (0, 1.0, 0.8, 9, 3, 120.0, &synced), 2.0) && ! synced);
    CHECK (near (autoFilter2LfoBeats (1, 1.0, 0.8, 9, 3, 120.0, &synced), 1.6) && ! synced);
    CHECK (near (autoFilter2LfoBeats (2, 1.0, 0.8, 9, 3, 120.0, &synced), 1.5) && synced);
    CHECK (near (autoFilter2LfoBeats (3, 1.0, 0.8, 9, 3, 120.0), 1.0) && near (autoFilter2LfoBeats (4, 1.0, 0.8, 9, 3, 120.0), 2.25));
    CHECK (near (autoFilter2LfoBeats (5, 1.0, 0.8, 9, 3, 120.0, &synced), 0.75) && synced);
    constexpr double unitsPerSemitone = 1.0 / 12.0 / 9.965784284662087;
    // Lfo_Amount 0.5 → ±47 semitones [MEASURED 46.9 / 47.5], time mode 1 (0.8 s)
    {
        const auto t = als190ed::translate (als190ek::autoFilter2 (0.5, 1));
        CHECK (t.has_value() && t->modulator.has_value());
        if (t && t->modulator)
        {
            const auto c0 = (double) t->slot.params[1];
            const auto& m = *t->modulator;
            CHECK (near (m.curve.front(), c0 - 47.0 * unitsPerSemitone, 1.0e-4) && near (m.curve.back(), c0 + 47.0 * unitsPerSemitone, 1.0e-4));
            CHECK (near (m.controller.params[7], peakLfoSpeedValue (1.6), 1.0e-6) && m.controller.params[8] == 0.0f);
            CHECK (als190ek::hasNote (m.notes, "LFO rate 1.25 Hz held"));
        }
    }
    // Envelope_Amount 0.5 → 48 semitones at a full-scale peak [MEASURED 47.8 per unit of peak]
    {
        const auto t = als190ed::translate (als190ek::autoFilter2 (0.0, 0, 0.5));
        CHECK (t && t->modulator && near (t->modulator->curve.back() - t->modulator->curve.front(), 48.0 * unitsPerSemitone, 1.0e-4));
    }
    // the older device's ModHub: 0.94 semitone per unit at a full-scale peak [MEASURED at ±60: 56.4 semitones per unit of
    // peak, linear in the peak from −36 to −9 dBFS]; 30 here, so the curve stays inside the cutoff's range
    {
        const auto t = als190ed::translate (als190ek::autoFilterAt1Hz (0.0, 0, 30.0));
        CHECK (t && t->modulator && near (t->modulator->curve.back() - t->modulator->curve.front(), 28.2 * unitsPerSemitone, 1.0e-4));
    }
    // shapes [MEASURED on the older device at 1 Hz]: 6 = random steps, four a cycle (as 5); the triangle starts at its
    // bottom (the controller's at its top: half a cycle on); a falling saw alone = the rising one, curve turned over
    {
        const auto r6 = als190ed::translate (als190ek::autoFilterAt1Hz (12.0, 6));
        const auto r5 = als190ed::translate (als190ek::autoFilterAt1Hz (12.0, 5));
        CHECK (r6 && r6->modulator && r6->modulator->controller.params[8] == 1.0f && near (r6->modulator->controller.params[7], peakLfoSpeedValue (0.5), 1.0e-6));
        CHECK (r5 && r5->modulator && r5->modulator->controller.params[8] == 1.0f && ! als190ek::hasNote (r6->modulator->notes, "as a sine"));
        const auto tri = als190ed::translate (als190ek::autoFilterAt1Hz (12.0, 2));
        CHECK (tri && tri->modulator && tri->modulator->controller.params[8] == 0.25f && near (tri->modulator->controller.params[9], 0.5, 1.0e-6));
        const auto down = als190ed::translate (als190ek::autoFilterAt1Hz (12.0, 4));
        CHECK (down && down->modulator && down->modulator->curve.front() > down->modulator->curve.back() && near (down->modulator->controller.params[9], 0.0, 1.0e-6) &&
               ! als190ek::hasNote (down->modulator->notes, "falling saw as a rising one"));
        const auto up = als190ed::translate (als190ek::autoFilterAt1Hz (12.0, 3));
        CHECK (up && up->modulator && up->modulator->curve.front() < up->modulator->curve.back());
        // the LFO amount of the older device stays semitones as written [MEASURED 12 → ±12.04]
        CHECK (near (up->modulator->curve.back() - up->modulator->curve.front(), 24.0 * unitsPerSemitone, 1.0e-4));
    }
}

TEST (als190ek_drift_voice_modes_and_the_synths_measured_laws)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190ek::par;
    // Global_VoiceMode [MEASURED, the menu]: 0 Poly, 1 Mono, 2 Stereo, 3 Unison
    const auto drift = [] (int voiceMode, int t1, int t2, int filter) {
        return "<Drift Id=\"8\">" + als190ek::on() + par ("Oscillator1_Type", t1) + par ("Oscillator2_Type", t2) + par ("Filter_Type", filter) +
               par ("Filter_Frequency", 1000.0) + par ("Envelope1_Attack", 0.01) + par ("Envelope1_Decay", 0.2) + par ("Envelope1_Sustain", 0.6) +
               par ("Envelope1_Release", 0.4) + par ("Global_VoiceMode", voiceMode) + "</Drift>";
    };
    const auto run = [] (const std::string& text, ChannelDef& c, Notes& n) {
        xml::Document doc;
        return doc.parse (text) && driftToSynth (doc.root(), c, 120.0, n);
    };
    for (int mode = 0; mode < 4; ++mode)
    {
        ChannelDef c;
        Notes n;
        CHECK (run (drift (mode, 0, 0, 0), c, n));
        CHECK (c.synth.mono == (mode == 1));
        CHECK (als190ek::hasNote (n.dropped, "stereo voices") == (mode == 2) && als190ek::hasNote (n.dropped, "unison voices") == (mode == 3));
    }
    // osc 1 types 0 … 6 and osc 2 types 0 … 4 [MEASURED: osc 2's renders = osc 1's types 0, 1, 3, 4, 6], the filter
    // slopes [MEASURED: I 12 dB/oct, II 24 dB/oct], envelope 1 in seconds [MEASURED: peak at 10 ms, sustain 0.6]
    static constexpr Waveform one[7] { Waveform::sine, Waveform::triangle, Waveform::saw, Waveform::sine, Waveform::saw, Waveform::square, Waveform::square };
    static constexpr int twoAsOne[5] { 0, 1, 3, 4, 6 };
    for (int t = 0; t < 7; ++t)
    {
        ChannelDef c;
        Notes n;
        CHECK (run (drift (0, t, t < 5 ? t : 0, t % 2), c, n) && c.synth.osc[0].wave == one[t] && c.synth.ladder == (t % 2 == 1));
        if (t < 5)
            CHECK (c.synth.osc[1].wave == one[twoAsOne[t]]);
        CHECK (als190ek::near (c.synth.attack, 0.01, 1.0e-6) && als190ek::near (c.synth.sustain, 0.6, 1.0e-6) && als190ek::near (c.synth.release, 0.4, 1.0e-6));
    }
    // Operator [MEASURED, the wave menu: 0 Sine … 21 Noise White, 22 User; algorithm 10 = four carriers]: wave 21 alone is the
    // synth's noise; the fourth carrier of algorithm 10 is listed (three oscillators)
    {
        xml::Document doc;
        CHECK (doc.parse (als190ed::operatorDevice (0, { 1.0, 0.000316227757, 0.000316227757, 0.000316227757 }, 21)));
        ChannelDef c;
        Notes n;
        CHECK (operatorToSynth (doc.root(), c, 120.0, n) && c.synth.osc[0].wave == Waveform::noise);
    }
    {
        xml::Document doc;
        CHECK (doc.parse (als190ed::operatorDevice (10, { 1.0, 1.0, 1.0, 1.0 })));
        ChannelDef c;
        Notes n;
        CHECK (operatorToSynth (doc.root(), c, 120.0, n) && c.synth.osc[2].level > 0.0f && als190ek::hasNote (n.dropped, "the fourth carrier"));
    }
}

TEST (als190ek_vst3_automation_follows_the_plugin_s_own_ids)
{
    using namespace lynx;
    using als175a::v;
    // a Live 12.4 save's layout [MEASURED, round 190ei]: kHs Gain's "Gain" (ParamID 1, range 0.0316 … 31.62, plain values in
    // the envelope), "mode" (ParamID 414, 0 … 1), then unused entries with ParameterId −1
    const auto param = [] (int id, const char* name, int pid, double value, double lo, double hi, int target) {
        return "<PluginFloatParameter Id=\"" + std::to_string (id) + "\">" + v ("ParameterName", name) + v ("ParameterId", std::to_string (pid)) +
               v ("VisualIndex", std::to_string (id)) + "<ParameterValue><Manual Value=\"" + als190ed::num (value) + "\" /><MidiControllerRange><Min Value=\"" +
               als190ed::num (lo) + "\" /><Max Value=\"" + als190ed::num (hi) + "\" /></MidiControllerRange><AutomationTarget Id=\"" + std::to_string (target) +
               "\" /></ParameterValue></PluginFloatParameter>";
    };
    auto vst3 = als175a::vst3Device ("00");
    vst3.insert (vst3.rfind ("</PluginDevice>"), "<ParameterList>" + param (0, "Gain", 1, 1.0, 0.0316227749, 31.622776, 801) + param (1, "mode", 414, 0.0, 0.0, 1.0, 802) +
                                                    param (2, "", -1, 0.1234567687, 0.0, 1.0, 803) + "</ParameterList>");
    const auto envelopes = als190cf::envelope (0, 801, 0.2486121505, 28.52) + als190cf::envelope (1, 802, 0.0, 1.0);
    FlpImportReport report;
    const auto song = als190ed::import (als190ed::set ({}, vst3, {}, envelopes), report);
    CHECK (song.has_value());
    if (! song)
        return;
    int bus = -1;
    for (int i = 0; i < (int) song->mixer.size(); ++i)
        bus = song->mixer[(std::size_t) i].name == "Bus" ? i : bus;
    CHECK (bus > 0 && song->mixer[(std::size_t) std::max (bus, 0)].effects.size() == 1);
    if (bus <= 0 || song->mixer[(std::size_t) bus].effects.size() != 1)
        return;
    // slot 0 = ParamID 1, slot 1 = ParamID 414; the plain values over the saved range
    const auto* gain = als190ed::automationOn (*song, { AutomationTargetKind::effectParam, bus, 0, 0 });
    const auto* mode = als190ed::automationOn (*song, { AutomationTargetKind::effectParam, bus, 0, 1 });
    CHECK (gain != nullptr && gain->points.size() == 2 && mode != nullptr && mode->points.size() == 2);
    if (gain != nullptr && gain->points.size() == 2)
        CHECK (als190ek::near (gain->points[0].value, (0.2486121505 - 0.0316227749) / (31.622776 - 0.0316227749), 1.0e-5) &&
               als190ek::near (gain->points[1].value, (28.52 - 0.0316227749) / (31.622776 - 0.0316227749), 1.0e-5));
    if (mode != nullptr && mode->points.size() == 2)
        CHECK (als190ek::near (mode->points[0].value, 0.0) && als190ek::near (mode->points[1].value, 1.0));
    // the slots go with the plugin's state (FL's record 4), so the host puts each one on the plugin's own parameter
    const auto& foreign = song->mixer[(std::size_t) bus].effects[0].plugin.foreign;
    CHECK (flVst3ParamSlots (foreign.state) == (std::vector<std::int32_t> { 1, 414 }));
    CHECK (flVst3SlotPositions (flVst3ParamSlots (foreign.state), { 414u, 1u, 7u }) == (std::vector<int> { 1, 0, 2 })); // (7: the next free slot)
    std::vector<std::uint8_t> component, controller;
    CHECK (splitFlVst3State (foreign.state, component, controller) && component.size() == 1);
    CHECK (report.unsupported.count ("Automation of a VST3 plugin's parameter (kHs Gain: Live keeps the plugin's own id, not its index)") == 0);
    CHECK (report.approximated.count ("Automation of a VST3 parameter with its own range (kHs Gain Gain: Live's plain values mapped linearly between the range's ends)") == 1);
    CHECK (report.approximated.count ("Automation of a VST3 parameter with its own range (kHs Gain mode: Live's plain values mapped linearly between the range's ends)") == 0);
}

TEST (als190ek_drum_buss_rack_is_fitted_to_live)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    namespace em = mixsuite::ember;
    namespace sh = mixsuite::shell;
    namespace cr = dyn::crest;
    using als190ek::near;
    using als190ek::par;
    // the drive's small-signal gain [MEASURED, 1 kHz at −60 dBFS, over the all-zero device's +1.97 dB]
    CHECK (near (drumBussDriveGainDb (0, 1.0), 9.55, 1.0e-9) && near (drumBussDriveGainDb (1, 1.0), 15.01, 1.0e-9) && near (drumBussDriveGainDb (2, 0.5), 5.78, 1.0e-9));
    CHECK (near (drumBussDriveGainDb (1, 0.75), (7.51 + 15.01) / 2.0, 1.0e-9) && near (drumBussDriveGainDb (2, 0.0), 0.0));
    // Boom at amount 0.5 [FITTED]: decay 1 → +18.65 dB, Q 2.18, the high pass Q 0.476 at 0.816 f; decay 0.2 → +13.75 dB
    const auto b1 = drumBussBoom (0.5, 50.0, 1.0), b02 = drumBussBoom (0.5, 80.0, 0.2), none = drumBussBoom (0.0, 50.0, 1.0);
    CHECK (near (b1.bellDb, 18.65, 1.0e-9) && near (b1.bellQ, 2.18, 1.0e-9) && near (b1.cutHz, 40.8, 1.0e-6) && near (b1.cutQ, 0.476, 1.0e-9));
    CHECK (near (b02.bellDb, 13.75, 1.0e-9) && near (b02.cutHz, 80.0 * 1.470, 1.0e-6) && none.bellDb == 0.0 && none.cutHz == 10.0);
    // Crunch 0.5 → +6.3 dB in its band [MEASURED 6.28 … 6.36]
    CHECK (near (drumBussCrunchDb (0.5), 6.32, 0.01) && drumBussCrunchDb (0.0) == 0.0);

    // the chain: Ember → Facet EQ → (Crest with Comp on) → Shell Channel
    const auto plain = als190ed::translate (als190ek::drumBuss (0.0, 0));
    const auto comp = als190ed::translate (als190ek::drumBuss (0.0, 0, {}, als190ek::flag ("EnableCompression", true)));
    CHECK (plain && rack::isRack (plain->slot) && comp && rack::isRack (comp->slot));
    if (! plain || ! comp || ! rack::isRack (plain->slot) || ! rack::isRack (comp->slot))
        return;
    const auto* r = rack::rackOf (plain->slot.patcher);
    CHECK (r->chains.size() == 1 && r->chains[0].nodes.size() == 3 && rack::rackOf (comp->slot.patcher)->chains[0].nodes.size() == 4);
    const auto* ember = als190ek::nodeOf (plain->slot.patcher, EffectKind::ember);
    const auto* colour = als190ek::nodeOf (plain->slot.patcher, EffectKind::facetEq);
    const auto* crest = als190ek::nodeOf (comp->slot.patcher, EffectKind::crestCompressor);
    CHECK (ember != nullptr && colour != nullptr && crest != nullptr && als190ek::nodeOf (plain->slot.patcher, EffectKind::shellChannel) != nullptr);
    if (ember == nullptr || colour == nullptr || crest == nullptr)
        return;
    CHECK (ember->params[em::autoGain] == 0.0f && ember->params[em::wideCuts] == 1.0f && ember->params[em::highCutWide] == 1.0f);
    CHECK (facet::shape (colour->params[(std::size_t) facet::param (0, 0)]) == facet::Shape::bell && near (facet::gainDb (colour->params[(std::size_t) facet::param (0, 2)]), drumbuss::lowBellDb, 0.01) &&
           near (facet::hz (colour->params[(std::size_t) facet::param (1, 1)]), drumbuss::highBellHz, 1.0) &&
           near (facet::gainDb (colour->params[(std::size_t) facet::param (6, 2)]), drumbuss::dipBellDb, 0.01) &&
           near (facet::outputDb (colour->params[facet::outputParam]), drumbuss::colourGainDb, 0.01));
    CHECK (near (cr::thresholdDb (crest->params[cr::threshold]), drumbuss::compThresholdDb, 0.01) && near (cr::ratioOf (crest->params[cr::ratio]), 4.0, 0.01) &&
           near (cr::trimDb (crest->params[cr::output]), drumbuss::compMakeupDb, 0.01));

    // the levels it gives [MEASURED in Live, 1 kHz staircase]: all zero +1.97 dB small-signal and −0.93 dBFS out at 0 dBFS;
    // Comp on +13.1 dB small-signal and about 4 : 1 above −21 dBFS (−12 → −4.96, 0 → −2.04 dBFS)
    CHECK (near (als190ek::sineThrough (plain->slot.patcher, 1000.0, -60.0) + 60.0, 1.97, 0.3));
    CHECK (near (als190ek::sineThrough (plain->slot.patcher, 1000.0, 0.0), -0.93, 1.0));
    CHECK (near (als190ek::sineThrough (comp->slot.patcher, 1000.0, -60.0) + 60.0, 13.1, 1.0));
    CHECK (near (als190ek::sineThrough (comp->slot.patcher, 1000.0, -12.0), -4.96, 1.5));
    // the drive types' small-signal gain at 100 % [MEASURED +11.52 / +16.98 / +14.23 dB]
    static constexpr double full[3] { 11.52, 16.98, 14.23 };
    for (int type = 0; type < 3; ++type)
    {
        const auto t = als190ed::translate (als190ek::drumBuss (1.0, type));
        CHECK (t && near (als190ek::sineThrough (t->slot.patcher, 1000.0, -60.0) + 60.0, full[type], 0.5));
    }
    // Boom 0.5 at 50 Hz, decay 1 [MEASURED +14.1 dB at 50 Hz over the all-zero device]; damping 1 kHz [MEASURED −3 dB at
    // 1020 Hz, −12.1 dB at 4 kHz over it]; Crunch 0.5 [MEASURED +6.3 dB at 1 kHz over it]
    const auto ref50 = als190ek::sineThrough (plain->slot.patcher, 50.0, -40.0), ref1k = als190ek::sineThrough (plain->slot.patcher, 1000.0, -40.0),
               ref4k = als190ek::sineThrough (plain->slot.patcher, 4000.0, -40.0);
    const auto boom = als190ed::translate (als190ek::drumBuss (0.0, 0, { { "BoomAmount", 0.5 }, { "BoomDecay", 1.0 } }));
    CHECK (boom && near (als190ek::sineThrough (boom->slot.patcher, 50.0, -40.0) - ref50, 14.1, 2.0));
    const auto damp = als190ed::translate (als190ek::drumBuss (0.0, 0, { { "DampingFrequency", 1000.0 } }));
    CHECK (damp && near (als190ek::sineThrough (damp->slot.patcher, 1000.0, -40.0) - ref1k, -3.0, 0.7) &&
           near (als190ek::sineThrough (damp->slot.patcher, 4000.0, -40.0) - ref4k, -12.1, 1.0));
    const auto crunch = als190ed::translate (als190ek::drumBuss (0.0, 0, { { "CrunchAmount", 0.5 } }));
    CHECK (crunch && near (als190ek::sineThrough (crunch->slot.patcher, 1000.0, -40.0) - ref1k, 6.3, 0.5));

    // the macros: each sits where Live's control is, and setting it there changes nothing; Drive at 127 gives the 100 % drive
    const auto mid = als190ed::translate (als190ek::drumBuss (0.5, 1, { { "CrunchAmount", 0.3 }, { "BoomAmount", 0.25 }, { "BoomFrequency", 70.0 } }));
    CHECK (mid && rack::isRack (mid->slot));
    if (mid && rack::isRack (mid->slot))
    {
        auto doc = mid->slot.patcher;
        const auto before = doc;
        const auto* m = rack::rackOf (doc);
        CHECK (near (m->macros[dbDrive].value, 63.5, 1.0e-3) && near (m->macros[dbCrunch].value, 0.3 * 127.0, 1.0e-3) && m->macros[dbBoom].mappings.size() == 2);
        for (const auto k : { dbDrive, dbCrunch, dbDamping, dbBoom, dbBoomFreq, dbTrim, dbOutput })
            rack::setMacro (doc, k, rack::rackOf (doc)->macros[(std::size_t) k].value);
        bool same = true;
        for (std::size_t n = 0; n < doc.nodes.size(); ++n)
            for (std::size_t p = 0; p < doc.nodes[n].effect.params.size(); ++p)
                same = same && near (doc.nodes[n].effect.params[p], before.nodes[n].effect.params[p], 1.0e-5);
        CHECK (same);
        rack::setMacro (doc, dbDrive, 127.0f);
        const auto* e = als190ek::nodeOf (doc, EffectKind::ember);
        CHECK (e != nullptr && near (em::driveDb (e->params[em::drive]), drumBussDriveStage (1, 1.0).driveDb, 0.05));
        CHECK (als190cf::linkOf (*mid, "DriveAmount") != nullptr && near (als190cf::linkOf (*mid, "DriveAmount")->value (0.5), 0.5, 1.0e-6));
    }
    CHECK (als190ek::hasNote (comp->notes.approximated, "Crest Compressor at about 4 : 1") && als190ek::hasNote (plain->notes.approximated, "own colour"));
}

TEST (als190ek_eq_three_meets_the_renders)
{
    using namespace lynx;
    using als190ek::near;
    // [MEASURED in Live] defaults flat within 0.31 dB; GainLo −12 dB → −11.98 at 31 Hz, GainMid +6 → +6.00 at 1 kHz (Slope
    // 48); the low band off → −90 dB at 31 Hz (Slope 1), the mid band off → −78 dB at 790 Hz
    const auto through = [] (const std::string& text, double hz) {
        const auto t = als190ed::translate (text);
        if (! t)
            return 99.0;
        auto fx = makeEffect (t->slot);
        fx->prepare (48000.0);
        std::vector<float> l (512), r (512), out;
        for (int at = 0; at < 48000; at += 512)
        {
            for (int i = 0; i < 512; ++i)
                l[(std::size_t) i] = r[(std::size_t) i] = (float) (0.1 * std::sin (2.0 * 3.141592653589793 * hz * (at + i) / 48000.0));
            fx->process (l.data(), r.data(), 512, t->slot.params, 120.0);
            out.insert (out.end(), l.begin(), l.end());
        }
        double re = 0.0, im = 0.0;
        for (int i = 24000; i < 48000; ++i)
        {
            re += out[(std::size_t) i] * std::cos (2.0 * 3.141592653589793 * hz * i / 48000.0);
            im += out[(std::size_t) i] * std::sin (2.0 * 3.141592653589793 * hz * i / 48000.0);
        }
        return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / 24000.0 / 0.1);
    };
    for (const double hz : { 31.0, 250.0, 1000.0, 2500.0, 10000.0 })
        CHECK (near (through (als190ed::eqThree (1.0, 1.0, 1.0, true, true, true), hz), 0.0, 0.35));
    CHECK (near (through (als190ed::eqThree (0.251189, 1.995262, 1.0, true, true, true), 31.0), -11.98, 0.15));
    CHECK (near (through (als190ed::eqThree (0.251189, 1.995262, 1.0, true, true, true), 1000.0), 6.0, 0.2));
    CHECK (through (als190ed::eqThree (1.0, 1.0, 1.0, false, true, true), 31.0) < -60.0);
    CHECK (through (als190ed::eqThree (1.0, 1.0, 1.0, true, false, true), 790.0) < -30.0);
}
