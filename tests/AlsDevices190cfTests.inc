// --- Live's own devices in a set → LYNX's own, lot 2 (round 190cf, core/AlsDevices.h): the unit laws of each
// translation against LYNX's own engine laws, the import report, automation and rack macros carried onto the
// translated devices, and Live's MIDI effects. The device elements are written by the tests with the member names
// Live saves; no file of Live's is used. Which statements about Live are [ASSUMED] is listed in the round's handoff.

#include "core/AlsDevices.h"
#include "core/AlsImport.h"
#include "core/Biquad.h"
#include "core/ApexLimiter.h"
#include "core/DynamicsSuite.h"
#include "core/EchoSuite.h"
#include "core/Effects.h"
#include "core/MixSuite.h"
#include "core/Rack.h"
#include "core/XmlLite.h"

namespace als190cf
{
using als175a::v;
using als190bq::flag;
using als190bq::hasNote;
using als190bq::par;
using als190bq::reportHas;

inline std::string on (bool value = true) { return std::string ("<On><Manual Value=\"") + (value ? "true" : "false") + "\" /></On>"; }
// a rack's members as Live saves them (only what the import reads)
inline std::string keyMidi (int macro)
{
    return "<KeyMidi>" + v ("PersistentKeyString", "") + v ("IsNote", "false") + v ("Channel", "16") + v ("NoteOrController", std::to_string (macro)) +
           v ("LowerRangeNote", "-1") + v ("UpperRangeNote", "-1") + v ("ControllerMapMode", "0") + "</KeyMidi>";
}
inline std::string range (const char* tag, int lo, int hi, int flo, int fhi)
{
    return std::string ("<") + tag + ">" + v ("Min", std::to_string (lo)) + v ("Max", std::to_string (hi)) + v ("CrossfadeMin", std::to_string (flo)) +
           v ("CrossfadeMax", std::to_string (fhi)) + "</" + tag + ">";
}
inline std::string branchMixer (double volume, double pan)
{
    return "<MixerDevice><Speaker><Manual Value=\"true\" /></Speaker><Volume>" + v ("Manual", std::to_string (volume)) + "<MidiControllerRange>" +
           v ("Min", "0.0003162277571") + v ("Max", "1") + "</MidiControllerRange></Volume><Panorama>" + v ("Manual", std::to_string (pan)) + "</Panorama></MixerDevice>";
}
inline std::string macros (const std::vector<std::string>& names, const std::vector<int>& values)
{
    std::string s;
    for (int k = 0; k < 16; ++k)
    {
        const auto n = std::to_string (k);
        s += "<MacroControls." + n + ">" + v ("Manual", std::to_string (k < (int) values.size() ? values[(std::size_t) k] : 0)) + "</MacroControls." + n + ">";
        s += v (("MacroDisplayNames." + n).c_str(), k < (int) names.size() ? names[(std::size_t) k] : "Macro " + std::to_string (k + 1));
        s += v (("MacroDefaults." + n).c_str(), "-1");
        s += v (("ExcludeMacroFromRandomization." + n).c_str(), "false");
        s += v (("ExcludeMacroFromSnapshots." + n).c_str(), "false");
    }
    s += "<ChainSelector>" + v ("Manual", "0") + "<MidiControllerRange>" + v ("Min", "0") + v ("Max", "127") + "</MidiControllerRange></ChainSelector>";
    s += v ("NumVisibleMacroControls", "4");
    return s;
}
// a parameter with its automation target id and, optionally, a macro mapping over a range
inline std::string target (const std::string& name, double value, int id, const std::string& mapping = {})
{
    return "<" + name + ">" + mapping + "<Manual Value=\"" + als190bq::num (value) + "\" /><AutomationTarget Id=\"" + std::to_string (id) + "\" /></" + name + ">";
}
inline std::string mapped (int macro, double lo, double hi)
{
    return keyMidi (macro) + "<MidiControllerRange>" + v ("Min", als190bq::num (lo)) + v ("Max", als190bq::num (hi)) + "</MidiControllerRange>";
}
inline std::optional<lynx::alsdev::Translated> translate (const std::string& text, double bpm = 120.0)
{
    lynx::xml::Document doc;
    doc.parse (text);
    lynx::alsdev::Context context;
    context.bpm = bpm;
    return lynx::alsdev::effectFromLiveDevice (doc.root(), context);
}
inline const lynx::alsdev::ParamLink* linkOf (const lynx::alsdev::Translated& t, const std::string& live)
{
    for (const auto& l : t.links)
        if (l.live == live)
            return &l;
    return nullptr;
}
inline bool near (double a, double b, double tolerance = 1.0e-3) { return std::abs (a - b) <= tolerance; }

// one MIDI track (Id 10) with `devices` and a clip of `clipNotes`, one audio track (Id 20) with `audioDevices` and
// `envelopes` (its automation)
inline std::string set (const std::string& devices, const std::string& audioDevices = {}, const std::string& clipNotes = {}, const std::string& envelopes = {})
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
         "</Devices></DeviceChain></DeviceChain><AutomationEnvelopes><Envelopes>" + envelopes + "</Envelopes></AutomationEnvelopes></AudioTrack>";
    s += "</Tracks><MainTrack><DeviceChain>" + mixer (900, 1.0, 0.0, true, {}, "<Tempo><Manual Value=\"120\" /></Tempo><TimeSignature><Manual Value=\"201\" /></TimeSignature>") +
         "</DeviceChain></MainTrack></LiveSet>\n</Ableton>\n";
    return s;
}
inline std::string envelope (int id, int pointee, double first, double last)
{
    return "<AutomationEnvelope Id=\"" + std::to_string (id) + "\"><EnvelopeTarget>" + v ("PointeeId", std::to_string (pointee)) +
           "</EnvelopeTarget><Automation><Events><FloatEvent Id=\"1\" Time=\"-63072000\" Value=\"" + als190bq::num (first) + "\" /><FloatEvent Id=\"2\" Time=\"8\" Value=\"" +
           als190bq::num (last) + "\" /></Events></Automation></AutomationEnvelope>";
}
inline std::string keyTrack (int id, int key, const std::string& notes)
{
    return "<KeyTrack Id=\"" + std::to_string (id) + "\"><Notes>" + notes + "</Notes>" + v ("MidiKey", std::to_string (key)) + "</KeyTrack>";
}
inline std::string delay (const std::string& extra)
{
    return "<Delay Id=\"1\">" + on() + extra + "</Delay>";
}
} // namespace als190cf

TEST (als190cf_shared_laws)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190cf::near;
    // the older Auto Filter's cutoff is a pitch: 69 = 440 Hz, its ends 20 / 135 = the manual's 26 Hz / 19.9 kHz
    CHECK (near (hzOfLiveNote (69.0), 440.0) && near (hzOfLiveNote (20.0), 25.96, 0.01) && near (hzOfLiveNote (135.0), 19912.1, 0.5));
    // synced LFO rates in quarter notes: 1/64 … 8 bars, 1/16 at the default index 4
    CHECK (near (liveBeatRateBeats (0), 0.0625) && near (liveBeatRateBeats (4), 0.25) && near (liveBeatRateBeats (9), 1.0) && near (liveBeatRateBeats (21), 32.0));
    CHECK (delaySixteenths (0) == 1 && delaySixteenths (4) == 5 && delaySixteenths (6) == 8 && delaySixteenths (7) == 16);
    // Echoform's note sync: every kind / note value comes back through the engine's own readers
    for (int kind = 0; kind <= 3; ++kind)
        CHECK (echosuite::echoform::syncKind (echoformSyncValue (kind)) == kind);
    for (int index = 0; index < 10; ++index)
        CHECK (echosuite::echoform::noteIndex (echoformNoteValue (index)) == index);
    const auto sixteenth = echoformNoteOf (0.25), dottedEighth = echoformNoteOf (0.75), bar = echoformNoteOf (4.0);
    CHECK (sixteenth && sixteenth->first == 1 && near (echosuite::echoform::noteBeats (sixteenth->second, 1), 0.25));
    CHECK (dottedEighth && dottedEighth->first == 2 && near (echosuite::echoform::noteBeats (dottedEighth->second, 2), 0.75));
    CHECK (bar && bar->first == 1 && ! echoformNoteOf (1.25) && ! echoformNoteOf (0.2601));
    // dry/wet: equal-loudness is Echoform's own law; linear keeps the wet : dry ratio
    CHECK (echoformMixOf (0.3, true) == 0.3f && echoformMixOf (0.0, false) == 0.0f && echoformMixOf (1.0, false) == 1.0f && near (echoformMixOf (0.5, false), 0.5));
    {
        float dry = 0.0f, wet = 0.0f;
        echosuite::equalPowerMix (echoformMixOf (0.25, false), dry, wet);
        CHECK (near (wet / dry, 0.25 / 0.75, 1.0e-4));
    }
    // the basic effects' inverse laws, read back through the texts LYNX shows
    const auto shown = [] (EffectKind kind, int param, float value) { return formatEffectParam (kind, param, value, 120.0); };
    CHECK (shown (EffectKind::filter, 0, filterTypeValue (0)) == "Low-pass" && shown (EffectKind::filter, 0, filterTypeValue (1)) == "High-pass" &&
           shown (EffectKind::filter, 0, filterTypeValue (2)) == "Band-pass");
    CHECK (shown (EffectKind::filter, 1, filterCutoffValue (1000.0)) == "1000 Hz" && shown (EffectKind::filter, 1, filterCutoffValue (hzOfLiveNote (69.0))) == "440 Hz");
    // (190da) resonance → Q = 0.5 + 2x / (1 − x), measured on both Auto Filter tags (190cz): the older x = min(r, 0.9),
    // the new one's x from its table; the Filter's top is 12
    CHECK (shown (EffectKind::filter, 2, filterQValue (2.0)) == "Q 2.00" && shown (EffectKind::filter, 2, filterQValue (filterQOfResonance (0.0, true))) == "Q 0.50" &&
           shown (EffectKind::filter, 2, filterQValue (filterQOfResonance (0.5, true))) == "Q 2.50" &&
           shown (EffectKind::filter, 2, filterQValue (filterQOfResonance (0.5, false))) == "Q 2.49" &&
           shown (EffectKind::filter, 2, filterQValue (filterQOfResonance (0.75, true))) == "Q 6.50" &&
           shown (EffectKind::filter, 2, filterQValue (filterQOfResonance (0.75, false))) == "Q 6.42" &&
           shown (EffectKind::filter, 2, filterQValue (filterQOfResonance (1.25, true))) == "Q 12.00");
    // the measured points: older device r 0 … 0.875 (0.875 → 14.5; 0.9 and above → 18.5), the new one 27 points to 35.78 at 1
    for (const auto& [r, q] : { std::array<double, 2> { 0.05, 0.6053 }, { 0.25, 1.1667 }, { 0.375, 1.7 }, { 0.625, 3.8333 }, { 0.875, 14.5 }, { 0.95, 18.5 }, { 1.25, 18.5 } })
        CHECK (std::abs (filterQOfResonance (r, true) / q - 1.0) < 1e-4);
    for (const auto& [r, q] : { std::array<double, 2> { 0.05, 0.6051 }, { 0.2, 0.9990 }, { 0.3, 1.3548 }, { 0.6, 3.4792 }, { 0.8, 8.3662 }, { 0.9, 17.8572 },
                                { 0.95, 23.9835 }, { 0.99, 32.6222 }, { 1.0, 35.7807 }, { 1.25, 35.7807 } })
        CHECK (std::abs (filterQOfResonance (r, false) / q - 1.0) < 2e-3);
    // the 24 dB stage (190cz a30–a33): Q2 = 0.5 + x / (1 + x) — r 0 → 0.5 / 0.5, r 0.5 → 2.50 / 0.833
    CHECK (std::abs (filterSecondStageQ (0.5) - 0.5) < 1e-9 && std::abs (filterSecondStageQ (2.5) - (0.5 + 0.5 / 1.5)) < 1e-9);
    CHECK (shown (EffectKind::filter, 4, 0.0f) == "12 dB/oct" && shown (EffectKind::filter, 4, 1.0f) == "24 dB/oct" &&
           makeEffectSlot (EffectKind::filter).params[4] == 0.0f);
    CHECK (shown (EffectKind::lofi, 0, lofiRateValue (22050.0)) == "22050 Hz" && shown (EffectKind::lofi, 0, lofiRateValue (44100.0)) == "44100 Hz" &&
           shown (EffectKind::lofi, 0, lofiRateValue (11025.0)) == "11025 Hz");
    CHECK (shown (EffectKind::lofi, 1, lofiBitsValue (8.0)) == "8.0 bits" && shown (EffectKind::lofi, 1, lofiBitsValue (16.0)) == "16.0 bits" &&
           shown (EffectKind::lofi, 1, lofiBitsValue (12.0)) == "12.0 bits");
    CHECK (shown (EffectKind::eq, 0, eqGainValue (6.0)) == "+6.0 dB" && shown (EffectKind::eq, 3, eqGainValue (-15.0)) == "-15.0 dB" &&
           shown (EffectKind::eq, 2, eqMidValue (1500.0)) == "1500 Hz");
    CHECK (shown (EffectKind::chorus, 0, chorusRateValue (0.9)) == "0.90 Hz");
    // Prism's and Sentry's times come back in ms
    CHECK (near (dyn::prism::attackMs (prismAttackValue (20.0)), 20.0, 0.01) && near (dyn::prism::releaseMs (prismReleaseValue (300.0)), 300.0, 0.05));
    CHECK (near (dyn::sentry::attackMs (sentryAttackValue (5.0)), 5.0, 0.01) && near (dyn::sentry::holdMs (sentryHoldValue (20.0)), 20.0, 0.01) &&
           near (dyn::sentry::releaseMs (sentryReleaseValue (100.0)), 100.0, 0.05));
    // Multiband Dynamics' slopes as Prism actions
    const auto compress = prismAbove (-0.75), limit = prismAbove (-1.0), expandUp = prismAbove (0.5), none = prismAbove (0.0);
    CHECK (compress.active && ! compress.expand && ! compress.upward && near (compress.ratio, 4.0) && limit.limit && ! none.active);
    CHECK (expandUp.active && expandUp.expand && expandUp.upward && near (expandUp.ratio, 2.0));
    const auto lift = prismBelow (0.5), flat = prismBelow (1.0), expandDown = prismBelow (-3.0);
    CHECK (lift.active && ! lift.expand && lift.upward && near (lift.ratio, 2.0) && flat.limit && expandDown.expand && ! expandDown.upward && near (expandDown.ratio, 4.0));
    CHECK (near (limiterLookaheadMs (0), 1.5) && near (limiterLookaheadMs (2), 6.0) && near (gateLookaheadMs (0), 0.0) && near (gateLookaheadMs (2), 10.0));
    // Arpeggiator: 1/8 at the default index 8, 1/16 at 6; styles
    CHECK (near (arpRateBeats (8), 0.5) && near (arpRateBeats (6), 0.25) && near (arpRateBeats (13), 4.0) && near (arpRateBeats (0), 0.03125));
    bool exact = false;
    CHECK (arpDirectionOf (0, exact) == 1 && exact && arpDirectionOf (1, exact) == 2 && exact && arpDirectionOf (2, exact) == 3 && exact);
    CHECK (arpDirectionOf (4, exact) == 5 && exact && arpDirectionOf (15, exact) == 4 && exact && arpDirectionOf (5, exact) == 5 && ! exact && arpDirectionOf (7, exact) == 1 && ! exact);
}

TEST (als190cf_delay_and_echo_become_echoform)
{
    using namespace lynx;
    using namespace lynx::echosuite::echoform;
    using als190cf::near;
    using als190cf::par;
    using als190cf::flag;
    using als190cf::v;
    // synced: 3 sixteenths left (a dotted eighth), 4 right (a quarter); ping-pong off; filter 1 kHz, 4 octaves wide
    const auto synced = als190cf::translate (als190cf::delay (flag ("DelayLine_Link", false) + flag ("DelayLine_PingPong", false) + flag ("DelayLine_SyncL", true) +
                                                              flag ("DelayLine_SyncR", true) + par ("DelayLine_TimeL", 0.375) + par ("DelayLine_TimeR", 0.375) +
                                                              par ("DelayLine_SyncedSixteenthL", 2) + par ("DelayLine_SyncedSixteenthR", 3) + par ("DelayLine_OffsetL", 0.0) +
                                                              par ("DelayLine_OffsetR", 0.0) + par ("Feedback", 0.5) + flag ("Freeze", false) + flag ("Filter_On", true) +
                                                              par ("Filter_Frequency", 1000.0) + par ("Filter_Bandwidth", 4.0) + par ("Modulation_Frequency", 0.5) +
                                                              par ("Modulation_AmountTime", 0.0) + par ("Modulation_AmountFilter", 0.0) + par ("DryWet", 0.3) + v ("DryWetMode", "1")));
    CHECK (synced.has_value());
    if (synced)
    {
        const auto& p = synced->slot.params;
        CHECK (synced->slot.kind == EffectKind::echoform && synced->approximate && synced->from == "Delay" && synced->to == "Echoform");
        CHECK (modeOf (p[mode]) == Mode::clean && styleOf (p[style]) == Style::dual);
        // the times follow the tempo: 375 / 500 ms at 120 BPM, 750 / 1000 ms at 60
        CHECK (near (sideMs (p, false, 120.0), 375.0) && near (sideMs (p, true, 120.0), 500.0) && near (sideMs (p, false, 60.0), 750.0) && near (sideMs (p, true, 60.0), 1000.0));
        CHECK (near (feedbackGain (p[feedback]), 0.5) && p[mix] == 0.3f && p[lynx::echosuite::echoform::freeze] == 0.0f && p[modDepth] == 0.0f && near (modRateHz (p[modRate]), 0.5, 1.0e-3));
        // the band-pass: two octaves either side of 1 kHz
        CHECK (near (lowCutHz (p[lowCut]), 250.0, 0.5) && near (highCutHz (p[highCut]), 4000.0, 0.5));
        CHECK (als190cf::hasNote (synced->notes.approximated, "filter") && synced->notes.dropped.empty());
        // the routes for automation and macros, with the same laws
        const auto* fb = als190cf::linkOf (*synced, "Feedback");
        const auto* dw = als190cf::linkOf (*synced, "DryWet");
        CHECK (fb != nullptr && fb->param == feedback && near (feedbackGain (fb->value (0.9)), 0.9, 1.0e-6));
        CHECK (dw != nullptr && dw->param == mix && dw->value (0.8) == 0.8f);
    }
    // linked, ping-pong, 5 sixteenths: no note value → the time in ms at the set's tempo, reported; linear dry/wet
    const auto five = als190cf::translate (als190cf::delay (flag ("DelayLine_Link", true) + flag ("DelayLine_PingPong", true) + flag ("DelayLine_SyncL", true) +
                                                            flag ("DelayLine_SyncR", false) + par ("DelayLine_TimeL", 0.375) + par ("DelayLine_TimeR", 0.1) +
                                                            par ("DelayLine_SyncedSixteenthL", 4) + par ("DelayLine_SyncedSixteenthR", 3) + par ("DelayLine_OffsetL", 0.0) +
                                                            par ("Feedback", 0.25) + flag ("Freeze", true) + flag ("Filter_On", false) + par ("Modulation_AmountTime", 0.5) +
                                                            par ("Modulation_AmountFilter", 0.3) + par ("DryWet", 0.25) + v ("DryWetMode", "0") + par ("DelayLine_SmoothingMode", 1)),
                                           100.0);
    CHECK (five.has_value());
    if (five)
    {
        const auto& p = five->slot.params;
        CHECK (styleOf (p[style]) == Style::pingPong && syncKind (p[syncL]) == 0 && syncKind (p[syncR]) == 0);
        CHECK (near (sideMs (p, false, 100.0), 750.0, 0.01) && near (sideMs (p, true, 100.0), 750.0, 0.01)); // 5 sixteenths at 100 BPM, both sides (Link)
        CHECK (p[lynx::echosuite::echoform::freeze] == 1.0f && p[lowCut] == 0.0f && p[highCut] == 1.0f && p[modDepth] == 0.5f);
        float dry = 0.0f, wet = 0.0f;
        echosuite::equalPowerMix (p[mix], dry, wet);
        CHECK (near (wet / dry, 1.0 / 3.0, 1.0e-4));
        CHECK (als190cf::hasNote (five->notes.approximated, "held in ms at 100 BPM") && als190cf::hasNote (five->notes.approximated, "linear dry/wet") &&
               als190cf::hasNote (five->notes.approximated, "time modulation") && als190cf::hasNote (five->notes.dropped, "filter modulation") &&
               als190cf::hasNote (five->notes.dropped, "transition mode"));
    }
    // free times in seconds, with an offset on a synced side
    const auto freeTime = als190cf::translate (als190cf::delay (flag ("DelayLine_Link", false) + flag ("DelayLine_SyncL", false) + flag ("DelayLine_SyncR", true) +
                                                                par ("DelayLine_TimeL", 0.0221) + par ("DelayLine_SyncedSixteenthR", 3) + par ("DelayLine_OffsetR", 0.1) +
                                                                par ("Feedback", 0.0) + par ("DryWet", 1.0)));
    CHECK (freeTime.has_value());
    if (freeTime)
        CHECK (near (sideMs (freeTime->slot.params, false, 120.0), 22.1, 0.01) && near (sideMs (freeTime->slot.params, true, 120.0), 550.0, 0.01) &&
               freeTime->slot.params[mix] == 1.0f);
    // a stub (another Live version) is not translated
    CHECK (! als190cf::translate ("<Delay Id=\"1\">" + v ("UserName", "") + "</Delay>").has_value());

    // Echo: a triplet eighth left, 6 sixteenths right, feedback 80 %, input +6 dB, output −6 dB, its filter's two cuts
    const auto echo = als190cf::translate ("<Echo Id=\"2\">" + als190cf::on() + flag ("Delay_SyncL", true) + flag ("Delay_SyncR", true) + par ("Delay_TimeL", 0.375) +
                                           par ("Delay_TimeR", 0.375) + par ("Delay_SyncedDivisionL", -3) + par ("Delay_SyncModeL", 1) + par ("Delay_SyncedSixteenthR", 6) +
                                           par ("Delay_SyncModeR", 3) + par ("Delay_OffsetL", 0.0) + par ("Delay_OffsetR", 0.0) + flag ("Delay_TimeLink", false) +
                                           par ("Feedback", 0.8) + par ("ChannelMode", 1) + par ("InputGain", 6.0) + par ("OutputGain", 0.501187234) +
                                           flag ("Filter_On", true) + par ("Filter_HighPassFrequency", 200.0) + par ("Filter_LowPassFrequency", 5000.0) +
                                           par ("Reverb_Level", 0.4) + flag ("Noise_On", true) + par ("Noise_Amount", 0.3) + flag ("Gate_On", true) +
                                           par ("StereoWidth", 1.0) + par ("DryWet", 0.5) + v ("DryWetMode", "1") + "</Echo>");
    CHECK (echo.has_value());
    if (echo)
    {
        const auto& p = echo->slot.params;
        CHECK (styleOf (p[style]) == Style::pingPong && near (sideMs (p, false, 120.0), 500.0 / 3.0, 0.01) && near (sideMs (p, true, 120.0), 750.0, 0.01));
        CHECK (syncKind (p[syncL]) == 3 && syncKind (p[syncR]) == 2); // a triplet, a dotted quarter
        CHECK (near (feedbackGain (p[feedback]), 0.8) && near (inputLevelDb (p[inputLevel]), 6.0, 1.0e-3) && near (outputDb (p[output]), -6.0, 1.0e-3));
        CHECK (near (lowCutHz (p[lowCut]), 200.0, 0.5) && near (highCutHz (p[highCut]), 5000.0, 0.5) && p[mix] == 0.5f);
        CHECK (als190cf::hasNote (echo->notes.dropped, "Reverb") && als190cf::hasNote (echo->notes.dropped, "Noise") && als190cf::hasNote (echo->notes.dropped, "Gate"));
    }
}

TEST (als190cf_multiband_dynamics_becomes_prism)
{
    using namespace lynx;
    using namespace lynx::dyn::prism;
    using als190cf::near;
    using als190cf::par;
    using als190cf::flag;
    using als190cf::v;
    const auto band = [] (const std::string& n, double aboveThr, double aboveRatio, double belowThr, double belowRatio, double in, double out, bool active = true) {
        return par ("AboveThreshold" + n, aboveThr) + par ("AboveRatio" + n, aboveRatio) + par ("BelowThreshold" + n, belowThr) + par ("BelowRatio" + n, belowRatio) +
               par ("InputGain" + n, in) + par ("Gain" + n, out) + par ("Attack" + n, 20.0) + par ("Release" + n, 300.0) + flag ("Active" + n, active) + flag ("Solo" + n, false);
    };
    // Low: 4 : 1 above −20 dB; Mid: lifted 0.5 dB per dB below −40 dB, 6 dB of input gain and 2 dB of output;
    // High: both actions (above wins, the other is listed), switched off
    const auto t = als190cf::translate ("<MultibandDynamics Id=\"1\">" + als190cf::on() + par ("SplitLowMid", 120.0) + par ("SplitMidHigh", 2500.0) + flag ("SplitLowMidOn", true) +
                                        flag ("SplitMidHighOn", true) + v ("SoftKnee", "false") + v ("EnvelopeIsPeak", "true") + par ("OutputGain", 3.0) +
                                        par ("GlobalAmount", 1.0) + par ("GlobalTime", 1.0) + band ("Low", -20.0, -0.75, -80.0, 0.0, 0.0, 0.0) +
                                        band ("Mid", -10.0, 0.0, -40.0, 0.5, 6.0, 2.0) + band ("High", -12.0, -1.0, -50.0, -1.0, 0.0, -3.0, false) + "</MultibandDynamics>");
    CHECK (t.has_value());
    if (! t)
        return;
    const auto& p = t->slot.params;
    CHECK (t->slot.kind == EffectKind::prismMultiband && bandCount (p) == 3);
    const auto xo = crossovers (p);
    CHECK (near (xo[0], 120.0, 0.05) && near (xo[1], 2500.0, 0.5) && near (trimDb (p[output]), 3.0, 1.0e-3));
    // the static curves, on Prism's own curve function: 3 dB less per 4 dB over the threshold
    const auto low4 = bandStaticDb (p, 0, -16.0), low8 = bandStaticDb (p, 0, -12.0), lowUnder = bandStaticDb (p, 0, -30.0);
    std::printf ("  Multiband low band (4 : 1 above -20 dB): %.3f dB at 4 dB over, %.3f at 8 dB over, %.3f under\n", low4, low8, lowUnder);
    CHECK (near (low4, -3.0, 0.35) && near (low8, -6.0, 0.7) && near (lowUnder, 0.0, 1.0e-6));
    // Mid: threshold −40 − 6 dB of input gain = −46 dB at Prism's input; 0.5 dB up per dB under it; level +8 dB
    // (input gain + output gain), which the band's static curve includes
    const auto midUnder = bandStaticDb (p, 1, -52.0) - 8.0, midOver = bandStaticDb (p, 1, -30.0) - 8.0;
    std::printf ("  Multiband mid band (lift 0.5 dB/dB below -46 dB): %.3f dB at 6 dB under, %.3f over\n", midUnder, midOver);
    CHECK (near (thresholdDb (p[(std::size_t) param (1, fThreshold)]), -46.0, 0.01) && near (midUnder, 3.0, 0.35) && near (midOver, 0.0, 1.0e-4));
    CHECK (near (levelDb (p[(std::size_t) param (1, fLevel)]), 8.0, 1.0e-3) && near (levelDb (p[(std::size_t) param (2, fLevel)]), -3.0, 1.0e-3));
    // High: a limit above −12 dB, the band bypassed
    CHECK (isInfiniteRatio (p[(std::size_t) param (2, fRatio)]) && p[(std::size_t) param (2, fEnabled)] == 0.0f && p[(std::size_t) param (0, fEnabled)] == 1.0f);
    CHECK (near (attackMs (p[(std::size_t) param (0, fAttack)]), 20.0, 0.01) && near (releaseMs (p[(std::size_t) param (0, fRelease)]), 300.0, 0.05));
    CHECK (als190cf::hasNote (t->notes.dropped, "below-threshold action") && ! als190cf::hasNote (t->notes.approximated, "RMS"));
    // Amount 50 % halves every slope: 4 : 1 becomes 1.6 : 1 (0.375 dB per dB)
    const auto half = als190cf::translate ("<MultibandDynamics Id=\"1\">" + als190cf::on() + par ("SplitLowMid", 120.0) + par ("SplitMidHigh", 2500.0) + flag ("SplitLowMidOn", true) +
                                           flag ("SplitMidHighOn", false) + v ("SoftKnee", "false") + par ("OutputGain", 0.0) + par ("GlobalAmount", 0.5) +
                                           par ("GlobalTime", 2.0) + band ("Low", -20.0, -0.75, -80.0, 0.0, 0.0, 0.0) + band ("Mid", -10.0, 0.0, -40.0, 0.0, 0.0, 0.0) +
                                           "</MultibandDynamics>");
    CHECK (half.has_value());
    if (half)
    {
        const auto& q = half->slot.params;
        CHECK (bandCount (q) == 2 && near (ratioOf (q[(std::size_t) param (0, fRatio)]), 1.6, 0.01) && near (attackMs (q[(std::size_t) param (0, fAttack)]), 40.0, 0.02));
        CHECK (near (rangeDb (q[(std::size_t) param (1, fRange)]), 0.0, 1.0e-6) && als190cf::hasNote (half->notes.approximated, "RMS"));
        const auto* thr = als190cf::linkOf (*half, "AboveThresholdLow");
        CHECK (thr != nullptr && thr->param == param (0, fThreshold) && near (thresholdDb (thr->value (-30.0)), -30.0, 0.01));
    }
}

TEST (als190cf_limiter_gate_and_expand)
{
    using namespace lynx;
    using als190cf::near;
    using als190cf::par;
    using als190cf::flag;
    using als190cf::v;
    const auto limiter = als190cf::translate ("<Limiter Id=\"1\">" + als190cf::on() + par ("Gain", 9.0) + par ("Ceiling", -0.3) + par ("Release", 300.0) + flag ("AutoRelease", true) +
                                              par ("LinkAmount", 1.0) + par ("Lookahead", 2) + par ("Routing", 0) + par ("Mode", 2) + flag ("Maximize", false) + "</Limiter>");
    CHECK (limiter.has_value());
    if (limiter)
    {
        const auto& p = limiter->slot.params;
        CHECK (limiter->slot.kind == EffectKind::apexLimiter && near (apex::gainDb (p[apex::pGain]), 9.0, 1.0e-4) && near (apex::ceilingDb (p[apex::pCeiling]), -0.3, 1.0e-4));
        CHECK (near (apex::releaseMs (p[apex::pRelease]), 300.0, 0.05) && p[apex::pTruePeak] == 1.0f && p[apex::pLink] == 1.0f && near (apex::lookaheadMs (p[apex::pLookahead]), 5.0));
        CHECK (apex::style (p[apex::pStyle]) == apex::Style::clean && als190cf::hasNote (limiter->notes.approximated, "look-ahead 6.0 ms") &&
               als190cf::hasNote (limiter->notes.approximated, "auto release"));
        const auto* ceiling = als190cf::linkOf (*limiter, "Ceiling");
        CHECK (ceiling != nullptr && ceiling->param == apex::pCeiling && near (apex::ceilingDb (ceiling->value (-6.0)), -6.0, 1.0e-4));
    }
    const auto cut = als190cf::translate ("<Limiter Id=\"1\">" + als190cf::on() + par ("Gain", -4.5) + par ("Ceiling", -1.0) + par ("Release", 3000.0) + par ("Lookahead", 0) +
                                          par ("Mode", 1) + flag ("Maximize", true) + "</Limiter>");
    CHECK (cut && cut->slot.params[apex::pGain] == 0.0f && als190cf::hasNote (cut->notes.dropped, "gain -4.5 dB") && als190cf::hasNote (cut->notes.dropped, "Soft Clip") &&
           als190cf::hasNote (cut->notes.dropped, "Maximize") && als190cf::hasNote (cut->notes.approximated, "release 3000 ms"));

    // Gate: threshold −40 dB (0.01), floor −40 dB, 1.5 ms look-ahead, the side chain from track 20 through a high pass
    const auto gate = als190cf::translate ("<Gate Id=\"2\">" + als190cf::on() + par ("Threshold", 0.01) + par ("Attack", 0.02) + par ("Hold", 20.0) + par ("Release", 100.0) +
                                           par ("Return", 3.0) + par ("Gain", -40.0) + par ("FlipMode", 0) + par ("LookAhead", 1) + "<SideChain>" + flag ("OnOff", true) +
                                           "<RoutedInput><Routable>" + v ("Target", "AudioIn/Track.20/PostMixerOut") + "</Routable>" + par ("Volume", 1.0) + "</RoutedInput>" +
                                           par ("DryWet", 1.0) + "</SideChain><SideChainEq>" + flag ("On", true) + par ("Mode", 5) + par ("Freq", 200.0) + par ("Q", 0.7) +
                                           par ("Gain", 0.0) + "</SideChainEq></Gate>");
    CHECK (gate.has_value());
    if (gate)
    {
        using namespace dyn::sentry;
        const auto& p = gate->slot.params;
        CHECK (gate->slot.kind == EffectKind::sentryGate && styleOf (p[style]) == Style::classic && near (thresholdDb (p[threshold]), -40.0, 1.0e-3));
        CHECK (near (rangeDb (p[range]), 40.0, 1.0e-3) && near (holdMs (p[hold]), 20.0, 0.01) && near (releaseMs (p[release]), 100.0, 0.05) && near (attackMs (p[attack]), 0.02, 1.0e-3));
        CHECK (near (lookaheadMs (p[lookahead]), 1.5, 1.0e-4) && p[lookaheadOn] == 1.0f && p[ducking] == 0.0f && p[ratio] == 1.0f);
        CHECK (gate->side.on && gate->side.trackId == 20 && ! gate->side.otherTap && gate->sideParam == scExternal && p[scExternal] == 1.0f);
        CHECK (near (filterHz (p[highPass]), 200.0, 0.05) && als190cf::hasNote (gate->notes.approximated, "Return 3.0 dB"));
        // far under the threshold the gate is down by its floor
        CHECK (near (staticGainDb (p, -80.0), -40.0, 0.01));
    }
    // (190cx) Gain −75 is "−inf dB" in Live 12.4: a full gate (Sentry's deepest floor); −74 is a 74 dB floor
    for (const auto& [gain, floor] : { std::array<double, 2> { -75.0, 100.0 }, { -74.0, 74.0 }, { 0.0, 0.0 } })
    {
        const auto g = als190cf::translate ("<Gate Id=\"2\">" + als190cf::on() + par ("Threshold", 0.01) + par ("Hold", 20.0) + par ("Return", 0.0) + par ("Gain", gain) + "</Gate>");
        CHECK (g && near (dyn::sentry::rangeDb (g->slot.params[dyn::sentry::range]), floor, 1.0e-3));
        if (g && gain <= -75.0)
            CHECK (dyn::sentry::staticGainDb (g->slot.params, -80.0) <= -99.99);
    }
    // Compressor in Expand mode: 1 : 1.5 over −18 dB → Sentry's Upward style, the same static curve
    const auto expand = als190cf::translate ("<Compressor2 Id=\"3\">" + als190cf::on() + par ("Threshold", 0.125892541) + par ("Ratio", 4.0) + par ("ExpansionRatio", 1.5) +
                                             par ("Attack", 2.0) + par ("Release", 50.0) + par ("Gain", 0.0) + par ("DryWet", 1.0) + par ("Model", 2) + par ("Knee", 0.0) +
                                             par ("LookAhead", 0) + "</Compressor2>");
    CHECK (expand.has_value());
    if (expand)
    {
        using namespace dyn::sentry;
        const auto& p = expand->slot.params;
        CHECK (expand->slot.kind == EffectKind::sentryGate && expand->to == "Sentry Gate" && styleOf (p[style]) == Style::upward);
        CHECK (near (upThresholdDb (p[upThreshold]), -18.0, 1.0e-3) && near (upRatioOf (p[upRatio]), 1.5, 1.0e-4));
        const auto over6 = staticGainDb (p, -12.0), under = staticGainDb (p, -30.0);
        std::printf ("  Expand 1 : 1.5 over -18 dB on Sentry Upward: %+.3f dB at 6 dB over, %+.3f under\n", over6, under);
        CHECK (near (over6, 3.0, 0.01) && near (under, 0.0, 0.01));
    }
    // Peak / RMS still go to Crest (lot 1)
    const auto peak = als190cf::translate ("<Compressor2 Id=\"3\">" + als190cf::on() + par ("Threshold", 0.1) + par ("Ratio", 2.0) + par ("Model", 0) + "</Compressor2>");
    CHECK (peak && peak->slot.kind == EffectKind::crestCompressor && peak->sideParam == dyn::crest::scExternal && als190cf::linkOf (*peak, "Threshold") != nullptr);
}

TEST (als190cf_filter_saturation_eq_chorus_pan_redux)
{
    using namespace lynx;
    using als190cf::near;
    using als190cf::par;
    using als190cf::flag;
    using als190cf::v;
    const auto shown = [] (const EffectSlot& slot, int param) { return formatEffectParam (slot.kind, param, slot.params[(std::size_t) param], 120.0); };
    // the older Auto Filter: high-pass at pitch 81 (880 Hz), resonance 0.75, an LFO and a 24 dB slope → listed
    const auto filter = als190cf::translate ("<AutoFilter Id=\"1\">" + als190cf::on() + par ("FilterType", 1) + par ("Cutoff", 81.0) + par ("Resonance", 0.75) + flag ("Slope", true) +
                                             par ("LfoAmount", 5.0) + par ("ModHub", 0.0) + par ("Drive", 0.0) + par ("CircuitLpHp", 0) + "</AutoFilter>");
    CHECK (filter.has_value());
    if (filter)
    {
        CHECK (filter->slot.kind == EffectKind::filter && shown (filter->slot, 0) == "High-pass" && shown (filter->slot, 1) == "880 Hz" && shown (filter->slot, 2) == "Q 6.50");
        CHECK (! als190cf::hasNote (filter->notes.approximated, "resonance") && shown (filter->slot, 4) == "24 dB/oct");
        // (round 190ed) the LFO is no longer dropped: a Peak Controller linked to the cutoff carries it
        CHECK (filter->slot.params[3] == 1.0f && filter->modulator.has_value() && filter->modulator->what == "LFO" &&
               als190cf::hasNote (filter->notes.approximated, "24 dB/oct high pass (Live's measured on the low pass)"));
        const auto* cutoff = als190cf::linkOf (*filter, "Cutoff");
        CHECK (cutoff != nullptr && cutoff->param == 1 && formatEffectParam (EffectKind::filter, 1, cutoff->value (69.0), 120.0) == "440 Hz");
    }
    // a notch / morph filter has no LYNX Filter type: not translated (dropped and listed as before)
    CHECK (! als190cf::translate ("<AutoFilter Id=\"1\">" + als190cf::on() + par ("FilterType", 3) + par ("Cutoff", 81.0) + par ("Resonance", 0.0) + "</AutoFilter>").has_value());
    const auto filter2 = als190cf::translate ("<AutoFilter2 Id=\"1\">" + als190cf::on() + par ("Filter_Type", 0) + par ("Filter_Frequency", 8000.0) + par ("Filter_Resonance", 0.0) +
                                              par ("Lfo_Amount", 0.0) + par ("Envelope_Amount", 0.0) + par ("DryWet", 0.5) + "</AutoFilter2>");
    CHECK (filter2 && shown (filter2->slot, 0) == "Low-pass" && shown (filter2->slot, 1) == "8000 Hz" && filter2->slot.params[3] == 0.5f && filter2->notes.dropped.empty() &&
           shown (filter2->slot, 4) == "12 dB/oct" && filter2->notes.approximated.empty());
    // above the Filter's Q 12 (new device r 1 = Q 35.8) and the band pass (Live's peaks at Q) are listed
    const auto sharp = als190cf::translate ("<AutoFilter2 Id=\"1\">" + als190cf::on() + par ("Filter_Type", 2) + par ("Filter_Frequency", 1000.0) +
                                            par ("Filter_Resonance", 1.0) + par ("Filter_Slope", 1) + "</AutoFilter2>");
    CHECK (sharp && shown (sharp->slot, 2) == "Q 12.00" && als190cf::hasNote (sharp->notes.approximated, "resonance 1.00 as Q 12 (Live: Q 35.8") &&
           als190cf::hasNote (sharp->notes.approximated, "band pass 21.6 dB lower") && als190cf::hasNote (sharp->notes.approximated, "24 dB/oct band pass"));

    // Saturator: 12 dB of drive, Hard Curve (type 4: the order seen in Live 12.4, round 190cw), −6 dB out, 80 % wet
    const auto sat = als190cf::translate ("<Saturator Id=\"2\">" + als190cf::on() + par ("PreDrive", 12.0) + par ("Type", 4) + flag ("ColorOn", true) + par ("BaseDrive", 0.0) +
                                          par ("ColorDepth", 0.0) + par ("PostClip", 1) + par ("PostDrive", -6.0) + par ("DryWet", 0.8) + "</Saturator>");
    CHECK (sat.has_value());
    if (sat)
    {
        using namespace mixsuite::ember;
        const auto& p = sat->slot.params;
        // Hard Curve [190cz fit]: Ember's tape at +17 dB more drive, +0.2 dB output
        CHECK (sat->slot.kind == EffectKind::ember && near (driveDb (p[drive]), 29.0, 1.0e-4) && styleOf (p[style]) == Style::tape && p[mix] == 0.8f && p[autoGain] == 0.0f);
        CHECK (mixsuite::paramText (EffectKind::ember, output, p[output]) == "-5.8 dB");
        CHECK (als190cf::hasNote (sat->notes.approximated, "Hard Curve as Ember's tape") && als190cf::hasNote (sat->notes.dropped, "output clip") &&
               ! als190cf::hasNote (sat->notes.dropped, "Color"));
        const auto* d = als190cf::linkOf (*sat, "PreDrive");
        CHECK (d != nullptr && near (driveDb (d->value (0.0)), 17.0, 1.0e-4));
    }
    // the curve table (190cz fit, ana/satfit.py): 0 Analog Clip, 1 Soft Sine, 2 Bass Shaper, 3 Medium Curve, 4 Hard Curve,
    // 5 Sinoid Fold, 6 Digital Clip, 7 Waveshaper → the Ember curve and drive offset that fit Live's curve best
    const char* const curveNotes[8] { "Analog Clip as Ember's transistor", "Soft Sine as Ember's transistor", "Bass Shaper as Ember's tape", "Medium Curve as Ember's transistor",
                                      "Hard Curve as Ember's tape", "Sinoid Fold as Ember's Wavefold character", "Digital Clip as Ember's transistor", "Waveshaper as Ember's tape" };
    const double offsets[8] { 0.5, 4.0, -1.5, 7.5, 17.0, -6.5, 0.0, 2.0 };
    for (int type = 0; type < 8; ++type)
    {
        const auto t = als190cf::translate ("<Saturator Id=\"2\">" + als190cf::on() + par ("PreDrive", 12.0) + par ("Type", type) + par ("PostDrive", 0.0) + par ("DryWet", 1.0) + "</Saturator>");
        CHECK (t && als190cf::hasNote (t->notes.approximated, curveNotes[type]));
        CHECK (t && near (mixsuite::ember::driveDb (t->slot.params[mixsuite::ember::drive]), 12.0 + offsets[type], 1.0e-4));
        CHECK (t && (type == 5) == (mixsuite::ember::characterIndex (t->slot.params[(std::size_t) mixsuite::ember::characterParam (0)]) == 26));
    }
    const auto quiet = als190cf::translate ("<Saturator Id=\"2\">" + als190cf::on() + par ("PreDrive", -6.0) + par ("Type", 0) + par ("PostDrive", 0.0) + par ("DryWet", 1.0) + "</Saturator>");
    CHECK (quiet && quiet->slot.params[mixsuite::ember::drive] == 0.0f && near (mixsuite::ember::inputDb (quiet->slot.params[mixsuite::ember::inputGain]), -5.5, 1.0e-4));

    // Overdrive: the percentages straight onto the Distortion; the band-pass is listed
    const auto od = als190cf::translate ("<Overdrive Id=\"3\">" + als190cf::on() + par ("MidFreq", 1200.0) + par ("BandWidth", 5.0) + par ("Drive", 50.0) + par ("DryWet", 80.0) +
                                         par ("Tone", 50.0) + par ("PreserveDynamics", 0.5) + "</Overdrive>");
    CHECK (od && od->slot.kind == EffectKind::distortion && shown (od->slot, 0) == "+18.0 dB" && shown (od->slot, 2) == "80%" && shown (od->slot, 3) == "+0.0 dB" &&
           als190cf::hasNote (od->notes.dropped, "band-pass before the drive (1200 Hz"));

    // Channel EQ: linear gains → dB
    const auto ceq = als190cf::translate ("<ChannelEq Id=\"4\">" + als190cf::on() + flag ("HighpassOn", true) + par ("LowShelfGain", 1.99526231) + par ("MidGain", 0.5) +
                                          par ("MidFrequency", 1500.0) + par ("HighShelfGain", 1.0) + par ("Gain", 1.41253754) + "</ChannelEq>");
    CHECK (ceq && ceq->slot.kind == EffectKind::eq && shown (ceq->slot, 0) == "+6.0 dB" && shown (ceq->slot, 1) == "-6.0 dB" && shown (ceq->slot, 2) == "1500 Hz" &&
           shown (ceq->slot, 3) == "+0.0 dB" && shown (ceq->slot, 4) == "+3.0 dB" && als190cf::hasNote (ceq->notes.dropped, "80 Hz high pass"));

    // Chorus-Ensemble (Classic) and the older Chorus
    const auto chorus = als190cf::translate ("<Chorus2 Id=\"5\">" + als190cf::on() + par ("Mode", 0) + par ("Rate", 0.9) + par ("Amount", 0.5) + par ("Feedback", 0.25) +
                                             par ("Width", 1.0) + par ("Warmth", 0.0) + par ("OutputGain", 1.0) + par ("DryWet", 0.4) + "</Chorus2>");
    CHECK (chorus && chorus->slot.kind == EffectKind::chorus && shown (chorus->slot, 0) == "0.90 Hz" && shown (chorus->slot, 1) == "2.0 ms" && shown (chorus->slot, 2) == "40%" &&
           shown (chorus->slot, 3) == "25%" && chorus->from == "Chorus-Ensemble" && chorus->notes.dropped.empty());
    const auto vibrato = als190cf::translate ("<Chorus2 Id=\"5\">" + als190cf::on() + par ("Mode", 2) + par ("Rate", 6.0) + par ("Amount", 1.0) + par ("DryWet", 0.4) + "</Chorus2>");
    CHECK (vibrato && vibrato->slot.params[2] == 1.0f && als190cf::linkOf (*vibrato, "DryWet") == nullptr && als190cf::hasNote (vibrato->notes.approximated, "Vibrato") &&
           als190cf::hasNote (vibrato->notes.approximated, "rate 6.0 Hz"));
    const auto old = als190cf::translate ("<Chorus Id=\"6\">" + als190cf::on() + par ("ModFreq", 1.5) + par ("ModAmount", 2.0) + par ("Feedback", 0.0) + par ("DryWet", 0.5) + "</Chorus>");
    CHECK (old && shown (old->slot, 0) == "1.50 Hz" && shown (old->slot, 1) == "2.0 ms" && old->from == "Chorus");

    // Auto Pan: a panner (phase 180°) only
    const auto lfo = [&] (double phase, int rateType) {
        return "<AutoPan Id=\"7\">" + als190cf::on() + "<Lfo>" + par ("Type", 0) + par ("Frequency", 2.0) + par ("RateType", rateType) + par ("BeatRate", 9) + par ("StereoMode", 0) +
               par ("Phase", phase) + par ("Offset", 0.0) + par ("LfoAmount", 0.5) + flag ("LfoInvert", false) + "</Lfo></AutoPan>";
    };
    const auto pan = als190cf::translate (lfo (180.0, 1));
    CHECK (pan && pan->slot.kind == EffectKind::autoPan && autopan::sourceOf (pan->slot.params[autopan::source]) == autopan::Source::fl &&
           near (pan->slot.params[0] * 64.0, 1.0, 1.0e-5) && shown (pan->slot, 1) == "+50%"); // a quarter note per cycle, half depth
    const auto freePan = als190cf::translate (lfo (180.0, 0));
    CHECK (freePan && autopan::sourceOf (freePan->slot.params[autopan::source]) == autopan::Source::lfo && near (autopan::rateHz (freePan->slot.params[autopan::rate]), 2.0, 1.0e-3));
    CHECK (! als190cf::translate (lfo (0.0, 1)).has_value() && ! als190cf::translate (lfo (360.0, 0)).has_value()); // a tremolo: not translated

    // Redux: 11025 Hz, 8 bits
    const auto redux = als190cf::translate ("<Redux2 Id=\"8\">" + als190cf::on() + par ("SampleRate", 11025.0) + par ("Jitter", 0.3) + par ("BitDepth", 8) + par ("DryWet", 1.0) + "</Redux2>");
    CHECK (redux && redux->slot.kind == EffectKind::lofi && shown (redux->slot, 0) == "11025 Hz" && shown (redux->slot, 1) == "8.0 bits" && shown (redux->slot, 2) == "100%" &&
           als190cf::hasNote (redux->notes.dropped, "Jitter"));
}

TEST (als190cf_midi_effects)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190cf::near;
    using als190cf::par;
    using als190cf::flag;
    using als190cf::v;
    // the note laws
    NoteEffect pitch;
    pitch.kind = NoteEffect::Kind::transpose;
    pitch.semitones = -12;
    const auto down = applyNoteEffect (pitch, { 60, 0.5f, 1.0 });
    CHECK (down.size() == 1 && down[0].key == 48 && down[0].velocity == 0.5f && applyNoteEffect (pitch, { 5, 0.5f, 1.0 }).empty());
    NoteEffect velocity;
    velocity.kind = NoteEffect::Kind::velocity;
    velocity.outLow = 40;
    velocity.outHigh = 100;
    const auto soft = applyNoteEffect (velocity, { 60, 1.0f / 127.0f, 1.0 }), loud = applyNoteEffect (velocity, { 60, 1.0f, 1.0 }), mid = applyNoteEffect (velocity, { 60, 64.0f / 127.0f, 1.0 });
    CHECK (soft.size() == 1 && near (soft[0].velocity * 127.0, 40.0, 1.0e-3) && near (loud[0].velocity * 127.0, 100.0, 1.0e-3) && near (mid[0].velocity * 127.0, 70.0, 1.0e-3));
    velocity.mode = 1; // Gate
    velocity.lowest = 50;
    velocity.range = 50;
    CHECK (applyNoteEffect (velocity, { 60, 30.0f / 127.0f, 1.0 }).empty() && applyNoteEffect (velocity, { 60, 75.0f / 127.0f, 1.0 }).size() == 1);
    velocity.mode = 2; // Fixed
    CHECK (near (applyNoteEffect (velocity, { 60, 0.2f, 1.0 })[0].velocity * 127.0, 100.0, 1.0e-3));
    NoteEffect chord;
    chord.kind = NoteEffect::Kind::chord;
    chord.chord = { { 4, 1.0f }, { 7, 0.5f }, { 4, 1.0f } };
    const auto triad = applyNoteEffect (chord, { 60, 0.8f, 2.0 });
    CHECK (triad.size() == 3 && triad[0].key == 60 && triad[1].key == 64 && triad[2].key == 67 && near (triad[2].velocity, 0.4) && triad[2].lengthBeats == 2.0);

    // the devices
    xml::Document doc;
    Context context;
    Notes notes;
    doc.parse ("<MidiArpeggiator Id=\"1\">" + als190cf::on() + par ("Mode", 5) + flag ("SyncState", true) + par ("SyncedRate", 6) + par ("FreeRate", 20.0) + par ("Gate", 61.0) +
               par ("TransposeSteps", 1) + par ("TransposeDistance", 12) + par ("RepeatCount", 0) + par ("Retrigger", 2) + flag ("Hold", false) + "</MidiArpeggiator>");
    ArpSettings arp;
    CHECK (arpeggiatorToArp (doc.root(), context, arp, notes));
    CHECK (arp.direction == 5 && near (arp.stepBeats, 0.25) && near (arp.gate, 0.61, 1.0e-6) && arp.range == 2 && arp.enabled());
    CHECK (als190cf::hasNote (notes.approximated, "style 5") && als190cf::hasNote (notes.dropped, "Retrigger"));
    doc.parse ("<MidiArpeggiator Id=\"1\">" + v ("UserName", "") + "</MidiArpeggiator>");
    CHECK (! arpeggiatorToArp (doc.root(), context, arp, notes));
    doc.parse ("<MidiNoteLength Id=\"1\">" + als190cf::on() + flag ("Mode", false) + flag ("SyncState", false) + par ("TimeLength", 250.0) + par ("Gate", 50.0) + "</MidiNoteLength>");
    notes = {};
    const auto length = noteEffectFromLiveDevice (doc.root(), context, notes);
    CHECK (length && length->kind == NoteEffect::Kind::length && near (length->lengthBeats, 0.25, 1.0e-9)); // 125 ms at 120 BPM
    doc.parse ("<MidiNoteLength Id=\"1\">" + als190cf::on() + flag ("Mode", true) + par ("TimeLength", 250.0) + "</MidiNoteLength>");
    CHECK (! noteEffectFromLiveDevice (doc.root(), context, notes));
    doc.parse ("<MidiScale Id=\"1\">" + als190cf::on() + par ("Base", 0) + "</MidiScale>");
    CHECK (! noteEffectFromLiveDevice (doc.root(), context, notes));

    // a set: Pitch −12, Velocity onto 40 … 100, an Arpeggiator, then the instrument — and a Scale, which stays unsupported
    const auto globals = v ("NumVoices", "5") + par ("PortamentoMode", 0) + v ("PlaybackMode", "0");
    const auto simpler = als190bq::simpler (globals, "<VolumeAndPan>" + par ("Volume", 0.0) + par ("Panorama", 0.0) + "</VolumeAndPan>");
    const auto pitcher = "<MidiPitcher Id=\"1\">" + als190cf::on() + par ("Pitch", -12.0) + par ("Lowest", 0) + par ("Range", 127) + "</MidiPitcher>";
    const auto velo = "<MidiVelocity Id=\"2\">" + als190cf::on() + par ("Drive", 0.25) + par ("Compand", 0.0) + par ("Random", 0.0) + par ("Mode", 0) + par ("Operation", 0) +
                      par ("MaxOut", 100) + par ("MinOut", 40) + par ("Range", 127) + par ("Lowest", 1) + "</MidiVelocity>";
    const auto arpeggio = "<MidiArpeggiator Id=\"3\">" + als190cf::on() + par ("Mode", 0) + flag ("SyncState", true) + par ("SyncedRate", 8) + par ("Gate", 50.0) +
                          par ("TransposeSteps", 0) + par ("TransposeDistance", 12) + "</MidiArpeggiator>";
    const auto scale = "<MidiScale Id=\"4\">" + als190cf::on() + v ("UserName", "") + par ("Base", 0) + "</MidiScale>";
    const auto clip = als190cf::keyTrack (0, 60, als175a::midiNote (0, 0.5, 127)) + als190cf::keyTrack (1, 64, als175a::midiNote (1, 0.5, 1));
    CHECK (doc.parse (als190cf::set (pitcher + velo + arpeggio + scale + simpler, {}, clip)));
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
    const ChannelDef* keys = nullptr;
    for (const auto& c : song->channels)
        if (c.name == "Keys")
            keys = &c;
    CHECK (keys != nullptr && keys->instrument == InstrumentKind::sampler && keys->arp.direction == 1 && near (keys->arp.stepBeats, 0.5) && near (keys->arp.gate, 0.5));
    bool found = false;
    for (const auto& p : song->patterns)
        if (p.notes.size() == 2)
        {
            found = true;
            CHECK (p.notes[0].key == 48 && near (p.notes[0].velocity * 127.0, 100.0, 1.0e-3) && p.notes[1].key == 52 && near (p.notes[1].velocity * 127.0, 40.0, 1.0e-3));
            CHECK (p.notes[0].length == ticksPerStep * 2 && p.notes[1].start == ppq);
        }
    CHECK (found);
    CHECK (report.converted.count ("Arpeggiator -> channel arpeggiator") == 1 && als190cf::reportHas (report.approximated, "Pitch (MIDI effect): written into the notes") &&
           als190cf::reportHas (report.approximated, "Velocity (MIDI effect): written into the notes") && als190cf::reportHas (report.approximated, "Velocity: Drive / Compand"));
    CHECK (report.unsupported.count ("MidiScale") == 1 && report.unsupported.size() == 1 && report.converted.count ("Simpler") == 1);

    // the same effects at the head of a one-chain Instrument Rack's chain reach the channel too; a Chord behind the
    // Arpeggiator cannot be written into the notes and stays unsupported
    const auto chordDevice = "<MidiChord Id=\"5\">" + als190cf::on() + v ("UserName", "") + par ("Shift1", 7.0) + par ("Velocity1", 1.0) + par ("Chance1", 1.0) + "</MidiChord>";
    const auto rack = "<InstrumentGroupDevice Id=\"6\">" + als190cf::on() + "<Branches><InstrumentBranch Id=\"0\"><Name>" + v ("EffectiveName", "Keys") +
                      "</Name><DeviceChain><MidiToAudioDeviceChain><Devices>" + pitcher + arpeggio + chordDevice + simpler +
                      "</Devices></MidiToAudioDeviceChain></DeviceChain>" + als190cf::range ("BranchSelectorRange", 0, 127, 0, 127) + "<ZoneSettings>" +
                      als190cf::range ("KeyRange", 0, 127, 0, 127) + als190cf::range ("VelocityRange", 1, 127, 1, 127) + "</ZoneSettings>" + als190cf::branchMixer (1.0, 0.0) +
                      "</InstrumentBranch></Branches>" + als190cf::macros ({}, {}) + "</InstrumentGroupDevice>";
    CHECK (doc.parse (als190cf::set (rack, {}, clip)));
    FlpImportReport rackReport;
    options.common.report = &rackReport;
    const auto racked = importAlsXml (doc, options, &error);
    CHECK (racked.has_value());
    if (racked)
    {
        const ChannelDef* channel = nullptr;
        for (const auto& c : racked->channels)
            if (c.name == "Keys")
                channel = &c;
        CHECK (channel != nullptr && channel->instrument == InstrumentKind::patcher && channel->arp.direction == 1);
        bool moved = false;
        for (const auto& p : racked->patterns)
            if (p.notes.size() == 2)
                moved = p.notes[0].key == 48 && p.notes[1].key == 52;
        CHECK (moved && rackReport.converted.count ("Arpeggiator -> channel arpeggiator") == 1 && rackReport.unsupported.count ("MidiChord") == 1 &&
               rackReport.unsupported.count ("MidiPitcher") == 0 && rackReport.unsupported.count ("MidiArpeggiator") == 0);
    }
}

TEST (als190cf_automation_and_macros_follow_the_translated_device)
{
    using namespace lynx;
    using als190cf::near;
    using als190cf::par;
    using als190cf::flag;
    using als190cf::v;
    // an audio track: a Delay whose Feedback (target 555) is automated 0.2 → 0.8, and an EQ Eight band gain (556) −6 → +6 dB
    const auto delay = als190cf::delay (flag ("DelayLine_Link", true) + flag ("DelayLine_SyncL", true) + par ("DelayLine_TimeL", 0.375) + par ("DelayLine_SyncedSixteenthL", 3) +
                                        par ("DelayLine_OffsetL", 0.0) + als190cf::target ("Feedback", 0.5, 555) + par ("DryWet", 0.5) + v ("DryWetMode", "1"));
    const auto eq = "<Eq8 Id=\"2\">" + als190cf::on() + v ("Mode", "0") + par ("GlobalGain", 0.0) + par ("Scale", 1.0) + "<Bands.0><ParameterA>" + flag ("IsOn", true) +
                    par ("Mode", 3) + par ("Freq", 1000.0) + als190cf::target ("Gain", 3.0, 556) + par ("Q", 1.0) + "</ParameterA></Bands.0></Eq8>";
    // an Audio Effect Rack: a Saturator whose Dry/Wet is on macro 1 over 20 … 80 %, its drive on macro 2 over 0 … 18 dB,
    // and its On switch on macro 3 (no LYNX control: listed)
    const auto saturator = "<Saturator Id=\"0\">" + std::string ("<On>") + als190cf::keyMidi (2) + "<Manual Value=\"true\" /></On>" + v ("UserName", "") +
                           als190cf::target ("PreDrive", 6.0, 600, als190cf::mapped (1, 0.0, 18.0)) + par ("Type", 0) + par ("PostDrive", 0.0) +
                           als190cf::target ("DryWet", 1.0, 601, als190cf::mapped (0, 0.2, 0.8)) + "</Saturator>";
    const auto rack = "<AudioEffectGroupDevice Id=\"5\">" + als190cf::on() + v ("UserName", "Dirt") + "<Branches><AudioEffectBranch Id=\"0\"><Name>" +
                      v ("EffectiveName", "Saturator") + v ("UserName", "") + "</Name>" + v ("IsSoloed", "false") + "<DeviceChain><AudioToAudioDeviceChain><Devices>" + saturator +
                      "</Devices></AudioToAudioDeviceChain></DeviceChain>" + als190cf::range ("BranchSelectorRange", 0, 127, 0, 127) + als190cf::branchMixer (1.0, 0.0) +
                      "</AudioEffectBranch></Branches>" + als190cf::macros ({ "Mix", "Drive", "Power" }, { 127, 0, 127 }) + "</AudioEffectGroupDevice>";
    const auto envelopes = als190cf::envelope (0, 555, 0.2, 0.8) + als190cf::envelope (1, 556, -6.0, 6.0) + als190cf::envelope (2, 999, 0.0, 1.0);
    xml::Document doc;
    CHECK (doc.parse (als190cf::set ({}, delay + eq + rack, {}, envelopes)));
    FlpImportReport report;
    auto options = als175a::options (&report);
    std::string error;
    const auto song = importAlsXml (doc, options, &error);
    CHECK (song.has_value());
    if (! song)
    {
        std::printf ("  import error: %s\n", error.c_str());
        return;
    }
    CHECK (song->mixer.size() == 3 && song->mixer[2].effects.size() == 3);
    if (song->mixer.size() != 3 || song->mixer[2].effects.size() != 3)
        return;
    const auto& fx = song->mixer[2].effects;
    CHECK (fx[0].kind == EffectKind::echoform && fx[1].kind == EffectKind::facetEq && fx[2].kind == EffectKind::patcher);
    // the envelopes: on the Echoform's feedback (loop gain 0.2 → 0.8) and on Facet's first band gain (−6 → +6 dB)
    CHECK (song->automations.size() == 2);
    bool feedbackSeen = false, gainSeen = false;
    for (const auto& a : song->automations)
    {
        CHECK (a.target.kind == AutomationTargetKind::effectParam && a.target.index == 2 && a.points.size() == 2);
        if (a.points.size() != 2)
            continue;
        if (a.target.slot == 0 && a.target.param == echosuite::echoform::feedback)
        {
            feedbackSeen = near (echosuite::echoform::feedbackGain (a.points[0].value), 0.2, 1.0e-6) && near (echosuite::echoform::feedbackGain (a.points[1].value), 0.8, 1.0e-6) &&
                           a.name == "Bus Delay Feedback";
        }
        if (a.target.slot == 1 && a.target.param == facet::param (0, 2))
            gainSeen = near (facet::gainDb (a.points[0].value), -6.0, 1.0e-3) && near (facet::gainDb (a.points[1].value), 6.0, 1.0e-3);
    }
    CHECK (feedbackSeen && gainSeen);
    // the envelope with no translated target is still listed
    CHECK (report.unsupported.count ("Automation of a device parameter (Bus)") == 1);
    // the rack: macro 1 → Ember's mix over 20 … 80 %, macro 2 → its drive over 0 … 18 dB; the mapped values follow the macros
    const auto* r = rack::rackOf (fx[2].patcher);
    CHECK (r != nullptr);
    if (r == nullptr)
        return;
    const auto& mixMap = r->macros[0].mappings;
    const auto& driveMap = r->macros[1].mappings;
    CHECK (mixMap.size() == 1 && driveMap.size() == 1 && r->macros[2].mappings.empty());
    if (mixMap.size() == 1 && driveMap.size() == 1)
    {
        CHECK (mixMap[0].target == RackMapping::Target::param && mixMap[0].param == mixsuite::ember::mix && near (mixMap[0].min, 0.2, 1.0e-6) && near (mixMap[0].max, 0.8, 1.0e-6));
        // (190da) Analog Clip runs as Ember's transistor 0.5 dB hotter: Live's 0 … 18 dB → Ember's 0.5 … 18.5 dB
        CHECK (driveMap[0].param == mixsuite::ember::drive && near (mixsuite::ember::driveDb (driveMap[0].min), 0.5, 1.0e-4) && near (mixsuite::ember::driveDb (driveMap[0].max), 18.5, 1.0e-4));
        const auto node = mixMap[0].node;
        CHECK (node >= 0 && node < (int) fx[2].patcher.nodes.size());
        if (node >= 0 && node < (int) fx[2].patcher.nodes.size())
        {
            const auto& ember = fx[2].patcher.nodes[(std::size_t) node].effect;
            // macro 1 at 127 → 80 % wet, macro 2 at 0 → no Live drive (the stored 100 % / 6 dB were the macros' old values)
            CHECK (ember.kind == EffectKind::ember && near (ember.params[mixsuite::ember::mix], 0.8, 1.0e-5) && near (mixsuite::ember::driveDb (ember.params[mixsuite::ember::drive]), 0.5, 1.0e-4));
        }
    }
    CHECK (als190cf::reportHas (report.partialEffects, "Macro mapping onto a setting of a converted Live device that has no LYNX control") &&
           ! als190cf::reportHas (report.partialEffects, "Macro mappings onto a converted Live device (not applied)"));
    CHECK (als190cf::reportHas (report.partialEffects, "Saturator -> Ember Saturator") && als190cf::reportHas (report.partialEffects, "Delay -> Echoform") &&
           report.converted.count ("EQ Eight -> Facet EQ") == 1 && report.droppedEffects.empty());
}

// (round 190cx) The Auto Filter law against Live 12.4's own renders (round 190cw: white noise through a low pass at
// 1 kHz, resonance 0.5, 12 dB; the transfer function against the dry track, 48 kHz): LYNX's Filter as the importer sets
// it, in dB at the same bins. The new tag's render had Output 0.5 (−6.02 dB at every bin); its curve is the older
// tag's within 0.035 dB, so one law serves both.
TEST (als190cx_auto_filter_matches_the_live_renders)
{
    using namespace lynx;
    using als190cf::par;
    using als190cf::flag;
    const double hz[8] { 123.047, 251.953, 498.047, 708.984, 1001.953, 1412.109, 2003.906, 4001.953 };
    const double legacy[8] { 0.12, 0.52, 2.19, 4.83, 7.93, -1.21, -9.98, -23.98 };
    const double newer[8] { -5.90, -5.50, -3.84, -1.20, 1.88, -7.24, -16.00, -30.00 }; // with Output 0.5
    const auto older = als190cf::translate ("<AutoFilter Id=\"6\">" + als190cf::on() + par ("FilterType", 0.0) + flag ("Slope", false) + par ("Cutoff", 83.213) +
                                            par ("Resonance", 0.5) + par ("LfoAmount", 0.0) + "</AutoFilter>");
    const auto af2 = als190cf::translate ("<AutoFilter2 Id=\"5\">" + als190cf::on() + par ("Filter_Type", 0.0) + par ("Filter_Slope", 0.0) + par ("Filter_Frequency", 1000.0) +
                                          par ("Filter_Resonance", 0.5) + par ("Lfo_Amount", 0.0) + par ("Filter_Drive", 0.0) + par ("Filter_Circuit", 0.0) +
                                          par ("DryWet", 1.0) + par ("Envelope_Amount", 0.0) + par ("Output", 0.5) + "</AutoFilter2>");
    CHECK (older.has_value() && af2.has_value());
    if (! older || ! af2)
        return;
    CHECK (als190cf::hasNote (af2->notes.dropped, "output gain -6.0 dB") && ! als190cf::hasNote (older->notes.approximated, "resonance"));
    for (int which = 0; which < 2; ++which)
    {
        const auto& slot = which == 0 ? older->slot : af2->slot;
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        std::vector<float> l (8192, 0.0f), r (8192, 0.0f);
        l[0] = 1.0f;
        for (int at = 0; at < 8192; at += 512)
            fx->process (l.data() + at, r.data() + at, 512, slot.params, 120.0);
        double worst = 0.0;
        for (int k = 0; k < 8; ++k)
        {
            std::complex<double> sum = 0.0;
            for (int n = 0; n < 8192; ++n)
                sum += (double) l[(std::size_t) n] * std::polar (1.0, -2.0 * 3.14159265358979323846 * hz[k] * n / 48000.0);
            const auto live = which == 0 ? legacy[k] : newer[k] + 6.0206; // (LYNX's Filter has no output gain: listed)
            worst = std::max (worst, std::abs (20.0 * std::log10 (std::abs (sum)) - live));
        }
        std::printf ("  Auto Filter %s, LP 1 kHz, resonance 0.5: worst %.3f dB\n", which == 0 ? "(older)" : "(new)", worst);
        CHECK (worst < 0.1);
    }
    // EQ Eight's Q link (automation / macros) keeps a bell's Adaptive Q narrowing: Q 2 at +12 dB → Facet √2 · 3.948
    lynx::xml::Document doc;
    std::string bands;
    for (int b = 0; b < 8; ++b)
        bands += als190bq::eqBand (b, b == 0, 3, 1000.0, b == 0 ? 12.0 : 0.0, 2.0);
    doc.parse ("<Eq8>" + als175a::v ("Mode", "0") + par ("GlobalGain", 0.0) + par ("Scale", 1.0) + bands + flag ("AdaptiveQ", true) + als175a::v ("AdaptiveQFactor", "1.12") + "</Eq8>");
    const auto eq = alsdev::effectFromLiveDevice (doc.root());
    const auto* q = eq ? als190cf::linkOf (*eq, "Bands.0/ParameterA/Q") : nullptr;
    CHECK (q != nullptr && std::abs (facet::q (q->value (2.0)) - 1.41421356 * 3.948) < 0.01);
    // and Utility's Balance has no link: Live's law sits in the matrix (automation / macros on it are listed)
    doc.parse ("<StereoGain>" + par ("Gain", 1.0) + par ("Balance", 0.5) + "</StereoGain>");
    const auto ut = alsdev::effectFromLiveDevice (doc.root());
    CHECK (ut && als190cf::linkOf (*ut, "Balance") == nullptr && als190cf::linkOf (*ut, "Gain") != nullptr);
}

// Round 190da: the importer's devices against Live 12.4.16's own renders of round 190cz (an impulse through one
// device, the transfer function against the dry track, 48 kHz; the numbers are Live's dB at 8 frequencies). LYNX's
// effect is run on an impulse and read at the same frequencies. Saturator: the rms of the 100 Hz 0 dBFS sine and of
// the 1 kHz −12 dBFS sine against Live's.
TEST (als190da_laws_match_the_live_190cz_renders)
{
    using namespace lynx;
    using als190cf::par;
    using als190cf::flag;
    const double hz[8] { 123.047, 251.953, 498.047, 708.984, 1001.953, 1412.109, 2003.906, 4001.953 };
    const auto responseOf = [] (const EffectSlot& slot, double f) {
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        std::vector<float> l (16384, 0.0f), r (16384, 0.0f);
        l[0] = 1.0f;
        for (int at = 0; at < 16384; at += 512)
            fx->process (l.data() + at, r.data() + at, 512, slot.params, 120.0);
        std::complex<double> sum = 0.0;
        for (int n = 0; n < 16384; ++n)
            sum += (double) l[(std::size_t) n] * std::polar (1.0, -2.0 * 3.14159265358979323846 * f * n / 48000.0);
        return 20.0 * std::log10 (std::abs (sum));
    };
    struct Case
    {
        const char* name;
        std::string device;
        double live[8];
        double tolerance;
    };
    const auto legacy = [] (double r, bool steep) {
        return "<AutoFilter Id=\"6\">" + als190cf::on() + par ("FilterType", 0.0) + flag ("Slope", steep) + par ("Cutoff", 83.213) + par ("Resonance", r) +
               par ("LfoAmount", 0.0) + "</AutoFilter>";
    };
    const auto newer = [] (double r, int slope) {
        return "<AutoFilter2 Id=\"5\">" + als190cf::on() + par ("Filter_Type", 0.0) + par ("Filter_Slope", slope) + par ("Filter_Frequency", 1000.0) +
               par ("Filter_Resonance", r) + par ("DryWet", 1.0) + par ("Output", 1.0) + "</AutoFilter2>";
    };
    const auto eq8 = [] (int mode, double q, double gain, bool adaptive, double scale) {
        std::string bands;
        for (int b = 0; b < 8; ++b)
            bands += als190bq::eqBand (b, b == 0, b == 0 ? mode : 3, 1000.0, b == 0 ? gain : 0.0, b == 0 ? q : 0.7071);
        return "<Eq8>" + als175a::v ("Mode", "0") + par ("GlobalGain", 0.0) + par ("Scale", scale) + bands + flag ("AdaptiveQ", adaptive) +
               als175a::v ("AdaptiveQFactor", "1.12") + als175a::v ("AdaptiveQAffectsShelves", "true") + "</Eq8>";
    };
    const Case cases[] = {
        { "Auto Filter (older) r 0", legacy (0.0, false), { -0.13, -0.53, -1.92, -3.53, -6.04, -9.54, -14.07, -24.97 }, 0.05 },
        { "Auto Filter (older) r 0.75", legacy (0.75, false), { 0.13, 0.56, 2.43, 5.85, 16.24, -0.20, -9.73, -23.94 }, 0.05 },
        { "Auto Filter r 0.2", newer (0.2, 0), { 0.06, 0.26, 0.89, 1.24, -0.03, -4.78, -11.26, -24.22 }, 0.05 },
        { "Auto Filter r 0.75", newer (0.75, 0), { 0.13, 0.56, 2.42, 5.85, 16.13, -0.20, -9.73, -23.94 }, 0.05 },
        { "Auto Filter 24 dB r 0.5", newer (0.5, 1), { 0.16, 0.66, 2.52, 4.95, 6.30, -7.10, -21.79, -48.32 }, 0.05 },
        { "Auto Filter (older) 24 dB r 0.25", legacy (0.25, true), { 0.08, 0.32, 0.96, 1.05, -1.79, -10.98, -23.34, -48.64 }, 0.05 },
        { "Auto Filter 24 dB r 0.75", newer (0.75, 1), { 0.18, 0.78, 3.11, 6.65, 15.47, -5.42, -21.20, -48.20 }, 0.05 },
        { "EQ Eight LC48 Q 0.1", eq8 (0, 0.1, 0.0, false, 1.0), { -147.40, -101.24, -61.88, -43.28, -28.07, -19.29, -13.34, -5.13 }, 0.3 },
        { "EQ Eight LC48 Q 10", eq8 (0, 10.0, 0.0, false, 1.0), { -134.69, -94.36, -42.99, -13.26, 15.44, 10.66, 5.50, 1.35 }, 0.3 },
        { "EQ Eight HC48 Q 3", eq8 (7, 3.0, 0.0, false, 1.0), { 0.25, 1.03, 3.77, 6.91, 8.12, -17.18, -44.84, -96.90 }, 0.4 }, // (Facet's analog-matched design near Nyquist)
        { "EQ Eight LS +12 Q 0.1", eq8 (2, 0.1, 12.0, false, 1.0), { 11.24, 9.76, 7.74, 6.80, 6.00, 5.20, 4.26, 2.16 }, 0.1 },
        { "EQ Eight LS +12 Q 18", eq8 (2, 18.0, 12.0, false, 1.0), { 12.17, 12.75, 15.19, 15.84, 5.93, -3.86, -3.18, -0.71 }, 0.1 },
        { "EQ Eight HS +12 Q 4", eq8 (5, 4.0, 12.0, false, 1.0), { -0.14, -0.59, -1.97, -0.99, 6.05, 13.01, 13.97, 12.56 }, 0.1 },
        { "EQ Eight bell +3 Q 2 Adaptive", eq8 (3, 2.0, 3.0, true, 1.0), { 0.02, 0.10, 0.54, 1.50, 3.00, 1.49, 0.54, 0.10 }, 0.1 },
        { "EQ Eight bell +15 Q 2 Adaptive", eq8 (3, 2.0, 15.0, true, 1.0), { 0.01, 0.06, 0.32, 1.32, 14.99, 1.30, 0.32, 0.05 }, 0.1 },
        { "EQ Eight LS +15 Q 0.71 Adaptive", eq8 (2, 0.71, 15.0, true, 1.0), { 15.23, 15.98, 19.10, 17.93, 7.44, -2.97, -4.09, -0.92 }, 0.1 },
        { "EQ Eight HS +6 Q 2 Adaptive", eq8 (5, 2.0, 6.0, true, 1.0), { -0.05, -0.21, -0.58, -0.12, 3.02, 6.13, 6.58, 6.20 }, 0.1 },
        { "EQ Eight bell +12 Q 2 Adaptive Scale 50 %", eq8 (3, 2.0, 12.0, true, 0.5), { 0.03, 0.11, 0.63, 2.05, 6.00, 2.03, 0.62, 0.11 }, 0.1 },
    };
    for (const auto& c : cases)
    {
        const auto t = als190cf::translate (c.device);
        CHECK (t.has_value());
        if (! t)
            continue;
        double worst = 0.0;
        for (int k = 0; k < 8; ++k)
            if (c.live[k] > -60.0)
                worst = std::max (worst, std::abs (responseOf (t->slot, hz[k]) - c.live[k]));
        std::printf ("  %-42s worst %.3f dB\n", c.name, worst);
        CHECK (worst < c.tolerance);
    }

    // old projects: a Filter slot without the appended Slope (as every project saved before round 190da loads it) runs
    // the one cookbook biquad it always ran, sample for sample
    {
        auto old = makeEffectSlot (EffectKind::filter);
        old.params[0] = 0.0f;
        old.params[1] = 0.6f;
        old.params[2] = 0.4f;
        old.params[3] = 0.8f;
        CHECK (old.params[4] == 0.0f);
        auto fx = makeEffect (old);
        fx->prepare (44100.0);
        Biquad ref;
        ref.filter (44100.0, 0, (double) (20.0f * std::pow (1000.0f, 0.6f)), (double) (0.5f * std::pow (24.0f, 0.4f)));
        std::vector<float> l (4096), r (4096);
        for (int n = 0; n < 4096; ++n)
            l[(std::size_t) n] = r[(std::size_t) n] = (float) std::sin (0.013 * n * n * 0.001) * 0.5f;
        auto want = l;
        for (int at = 0; at < 4096; at += 256)
            fx->process (l.data() + at, r.data() + at, 256, old.params, 120.0);
        double worst = 0.0;
        for (int n = 0; n < 4096; ++n)
        {
            const auto x = want[(std::size_t) n];
            worst = std::max (worst, (double) std::abs (l[(std::size_t) n] - (x + (ref.process (x, 0) - x) * 0.8f)));
        }
        std::printf ("  Filter without Slope vs the cookbook biquad: worst %.2e\n", worst);
        CHECK (worst < 2.0e-6);
    }
    // the Reverb laws (190cz: Live's 1 kHz T30 = 0.91 × Decay; high shelf 0.7 in the loop)
    CHECK (std::abs (alsdev::reverbAtriumDecaySec (2.5) - 2.35) < 1e-9 && std::abs (alsdev::reverbAtriumHighMult (0.7, 2.5) - 0.617) < 0.002 &&
           std::abs (alsdev::reverbAtriumHighMult (0.7, 5.0) - 0.447) < 0.002 && std::abs (alsdev::reverbAtriumHighMult (0.7, 1.0) - 0.801) < 0.002 &&
           alsdev::reverbAtriumHighMult (1.0, 5.0) == 1.0);

    // Saturator: rms (dBFS) of the 100 Hz 0 dBFS sine and of the 1 kHz −12 dBFS sine, Live's renders x00 … x61
    struct Sat
    {
        int type;
        double drive, live100, live1k, tolerance;
    };
    // (the fitted curves are within 0.1 … 1.4 dB of these levels; before round 190da up to 11 dB off)
    const Sat sats[] = { { 0, 0, -3.46, -15.05, 0.8 }, { 0, 12, -0.55, -3.48, 0.8 }, { 1, 0, -1.86, -11.30, 0.8 }, { 1, 12, -0.36, -1.87, 0.8 },
                         { 2, 0, -6.36, -15.93, 0.8 }, { 2, 12, -1.31, -6.38, 0.8 }, { 3, 0, -1.14, -9.03, 1.5 }, { 3, 12, -0.25, -1.15, 1.5 },
                         { 4, 0, -0.78, -3.62, 0.8 }, { 4, 12, 0.13, -0.79, 0.8 }, { 6, 0, -3.01, -15.05, 1.1 }, { 6, 12, -0.49, -3.05, 1.1 } };
    for (const auto& s : sats)
    {
        const auto t = als190cf::translate ("<Saturator Id=\"2\">" + als190cf::on() + par ("PreDrive", s.drive) + par ("Type", s.type) + par ("PostDrive", 0.0) +
                                            par ("DryWet", 1.0) + "</Saturator>");
        CHECK (t.has_value());
        if (! t)
            continue;
        const auto rmsOf = [&t] (double f, double amplitude) {
            auto fx = makeEffect (t->slot);
            fx->prepare (48000.0);
            std::vector<float> l (48000), r (48000);
            for (int n = 0; n < 48000; ++n)
                l[(std::size_t) n] = r[(std::size_t) n] = (float) (amplitude * std::sin (2.0 * 3.14159265358979323846 * f * n / 48000.0));
            for (int at = 0; at < 48000; at += 480)
                fx->process (l.data() + at, r.data() + at, 480, t->slot.params, 120.0);
            double e = 0.0;
            for (int n = 24000; n < 48000; ++n)
                e += (double) l[(std::size_t) n] * l[(std::size_t) n];
            return 10.0 * std::log10 (e / 24000.0);
        };
        const auto a = rmsOf (100.0, 1.0), b = rmsOf (1000.0, 0.25);
        std::printf ("  Saturator type %d drive %+.0f: rms 100 Hz %.2f (Live %.2f), 1 kHz %.2f (Live %.2f)\n", s.type, s.drive, a, s.live100, b, s.live1k);
        CHECK (std::abs (a - s.live100) < s.tolerance && std::abs (b - s.live1k) < s.tolerance);
    }

    // Glue with its side-chain EQ on (a high pass at 200 Hz, Q 0.71, as the rendered preset; Live z1 g01 weights its
    // detector as a 2nd-order high pass at 204.5 Hz): a 100 Hz sine at 0 dBFS gets about the gain reduction of a 1 kHz one
    // at −12 dBFS (Live 21.2 vs 21.9 dB)
    const auto glue = als190cf::translate ("<GlueCompressor>" + par ("Threshold", -40.0) + par ("Range", 70.0) + par ("Makeup", 0.0) + par ("Attack", 0) +
                                           par ("Ratio", 2) + par ("Release", 0) + par ("DryWet", 1.0) + "<SideChainEq>" + flag ("On", true) + par ("Mode", 5) +
                                           par ("Freq", 200.0) + par ("Q", 0.7071067691) + par ("Gain", 0.0) + "</SideChainEq></GlueCompressor>");
    CHECK (glue.has_value());
    if (glue)
    {
        const auto grOf = [&glue] (double f, double amplitude) {
            auto fx = makeEffect (glue->slot);
            fx->prepare (48000.0);
            std::vector<float> l (96000), r (96000);
            for (int n = 0; n < 96000; ++n)
                l[(std::size_t) n] = r[(std::size_t) n] = (float) (amplitude * std::sin (2.0 * 3.14159265358979323846 * f * n / 48000.0));
            for (int at = 0; at < 96000; at += 480)
                fx->process (l.data() + at, r.data() + at, 480, glue->slot.params, 120.0);
            double e = 0.0;
            for (int n = 48000; n < 96000; ++n)
                e += (double) l[(std::size_t) n] * l[(std::size_t) n];
            return 10.0 * std::log10 (e / 48000.0) - 20.0 * std::log10 (amplitude / std::sqrt (2.0));
        };
        const auto low = grOf (100.0, 1.0), mid = grOf (1000.0, 0.25);
        std::printf ("  Glue ratio 10, thr -40: GR 100 Hz 0 dBFS %.2f dB (Live -21.16), 1 kHz -12 dBFS %.2f dB (Live -21.94)\n", low, mid);
        CHECK (std::abs ((low - mid) - (-21.16 + 21.94)) < 1.0);
    }
}
