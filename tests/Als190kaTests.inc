// --- Live set (.als) automation and video clips, round als190ka (issue #193). Synthetic sets written by the tests (only
// the elements the importer reads; the shapes follow Live 12.4's own saves of the round's probe sets).

#include "core/AlsImport.h"
#include "core/VideoTrack.h"

#define ALS190KA_NEAR(a, b, tol) CHECK (std::abs ((double) (a) - (double) (b)) <= (double) (tol))

namespace als190ka
{
using als175a::bytesOf;
using als175a::name;
using als175a::v;

inline std::string fe (double t, double value, const std::string& extra = {})
{
    return "<FloatEvent Id=\"0\" Time=\"" + std::to_string (t) + "\" Value=\"" + std::to_string (value) + "\"" + extra + " />";
}
inline std::string be (double t, bool on) { return "<BoolEvent Id=\"0\" Time=\"" + std::to_string (t) + "\" Value=\"" + (on ? "true" : "false") + "\" />"; }

inline std::string envelope (const std::string& pointee, const std::string& events)
{
    return "<AutomationEnvelope Id=\"0\"><EnvelopeTarget>" + v ("PointeeId", pointee) + "</EnvelopeTarget><Automation><Events>" + events +
           "</Events></Automation></AutomationEnvelope>";
}

// a Utility (StereoGain) with its On switch (target `onId`) and Gain (target `gainId`)
inline std::string utility (int onId, int gainId, bool on = true)
{
    return "<StereoGain Id=\"0\"><On><Manual Value=\"" + std::string (on ? "true" : "false") + "\" /><AutomationTarget Id=\"" + std::to_string (onId) +
           "\" /></On><Gain><Manual Value=\"1\" /><AutomationTarget Id=\"" + std::to_string (gainId) + "\" /></Gain>" + v ("LegacyMode", "false") + "</StereoGain>";
}

inline std::string audioClip (double at, double lengthBeats, const std::string& path, bool warped, double loopStart, double loopEnd)
{
    return "<AudioClip Id=\"0\" Time=\"" + std::to_string (at) + "\">" + v ("CurrentStart", std::to_string (at)) +
           v ("CurrentEnd", std::to_string (at + lengthBeats)) + v ("Name", "clip") + v ("IsWarped", warped ? "true" : "false") + v ("WarpMode", "4") +
           "<Loop>" + v ("LoopStart", std::to_string (loopStart)) + v ("LoopEnd", std::to_string (loopEnd)) + v ("StartRelative", "0") +
           v ("LoopOn", "false") + v ("OutMarker", std::to_string (loopEnd)) + "</Loop>" +
           "<WarpMarkers><WarpMarker Id=\"0\" SecTime=\"0\" BeatTime=\"0\" /><WarpMarker Id=\"1\" SecTime=\"1\" BeatTime=\"2\" /></WarpMarkers>" +
           "<SampleRef><FileRef>" + v ("RelativePathType", "0") + v ("RelativePath", "") + v ("Path", path) + "</FileRef></SampleRef></AudioClip>";
}

// a set at `bpm`: audio tracks (mixer ids from 100·(k+1)), one return (Id 9, mixer ids 900), the Main track (ids 950)
// with `mainDevices`; `tracks[k]` = {devices, envelopes, clips, sends}
struct Track
{
    std::string devices, envelopes, clips;
    std::vector<double> sends { 0.000316 };
};
inline std::string set (const std::vector<Track>& tracks, double bpm = 100.0, const std::string& mainDevices = {}, const std::string& mainEnvelopes = {},
                        const std::string& returnEnvelopes = {})
{
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12402\" Creator=\"test\">\n<LiveSet><Tracks>";
    for (std::size_t k = 0; k < tracks.size(); ++k)
    {
        const auto id = 100 * (int) (k + 1);
        s += "<AudioTrack Id=\"" + std::to_string (k + 1) + "\">" + name ("T" + std::to_string (k + 1)) + v ("TrackGroupId", "-1") + "<DeviceChain>" +
             als175a::mixer (id, 1.0, 0.0, true, tracks[k].sends) + "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting>" +
             "<MainSequencer><VolumeModulationTarget Id=\"" + std::to_string (id + 50) + "\" /><Sample><ArrangerAutomation><Events>" + tracks[k].clips +
             "</Events></ArrangerAutomation></Sample></MainSequencer><DeviceChain><Devices>" + tracks[k].devices +
             "</Devices></DeviceChain></DeviceChain><AutomationEnvelopes><Envelopes>" + tracks[k].envelopes + "</Envelopes></AutomationEnvelopes></AudioTrack>";
    }
    s += "<ReturnTrack Id=\"9\">" + name ("A-Return") + "<DeviceChain>" + als175a::mixer (900, 1.0, 0.0, true, { 0.000316 }) + "<AudioOutputRouting>" +
         v ("Target", "AudioOut/Main") + "</AudioOutputRouting><DeviceChain><Devices /></DeviceChain></DeviceChain><AutomationEnvelopes><Envelopes>" +
         returnEnvelopes + "</Envelopes></AutomationEnvelopes></ReturnTrack>";
    s += "</Tracks><MainTrack><DeviceChain>" +
         als175a::mixer (950, 1.0, 0.0, true, {}, "<Tempo><Manual Value=\"" + std::to_string (bpm) + "\" /><AutomationTarget Id=\"990\" /></Tempo>") +
         "<DeviceChain><Devices>" + mainDevices + "</Devices></DeviceChain></DeviceChain><AutomationEnvelopes><Envelopes>" + mainEnvelopes +
         "</Envelopes></AutomationEnvelopes></MainTrack></LiveSet>\n</Ableton>\n";
    return s;
}

inline std::optional<lynx::Song> load (const std::string& text, lynx::FlpImportReport* report = nullptr, std::vector<std::string> files = {})
{
    lynx::AlsImportOptions o;
    o.common.projectFolder = "E:/sets";
    o.common.fileExists = [files] (const std::string& p) { return std::find (files.begin(), files.end(), p) != files.end(); };
    o.common.report = report;
    std::string error;
    return lynx::importAls (bytesOf (text), o, &error);
}

inline const lynx::Automation* automationFor (const lynx::Song& s, lynx::AutomationTargetKind kind, int index, int slot = 0)
{
    for (const auto& a : s.automations)
        if (a.target.kind == kind && a.target.index == index && a.target.slot == slot)
            return &a;
    return nullptr;
}

// the value an automation has at `beats` (its clip starts at the first event)
inline float valueAt (const lynx::Song& s, const lynx::Automation& a, double beats)
{
    for (const auto& c : s.playlist)
        if (c.kind == lynx::ClipKind::automation && &s.automations[(std::size_t) c.automation] == &a)
            return lynx::automationCurveAt (a.points, beats * lynx::ppq - (double) c.start);
    return -1.0f;
}

inline double dbOf (float faderPos)
{
    const auto g = lynx::faderGain (faderPos);
    return g > 0.0f ? 20.0 * std::log10 ((double) g) : -200.0;
}
} // namespace als190ka

// A send that rests at −inf but has an envelope (a throw) gets its route, so the envelope has something to move
TEST (als190ka_automated_send_at_minus_inf_has_a_route)
{
    using namespace als190ka;
    als190ka::Track t;
    t.sends = { 0.000316 }; // −inf
    t.envelopes = envelope ("110", fe (-63072000, 0.000316) + fe (0, 0.000316) + fe (8, 1.0) + fe (8, 1.0) + fe (12, 0.000316));
    als190ka::Track quiet; // a send at −inf without an envelope: since round 190kc (rollup 0.1.30 step 6) a send at 0 too
    lynx::FlpImportReport report;
    const auto s = load (set ({ t, quiet }), &report);
    CHECK (s.has_value());
    if (! s)
        return;
    const auto ret = (int) s->mixer.size() - 1; // the return is the last insert
    CHECK (s->routes (1, ret));
    CHECK (s->routes (2, ret)); // (190kc, its owner decision 1: an enabled send at −inf stays a send at 0, as Live's knob is there)
    bool zero = false;
    for (const auto& send : s->mixer[1].sends)
        zero = zero || (send.target == ret && send.level == 0.0f);
    CHECK (zero);
    const auto* a = automationFor (*s, lynx::AutomationTargetKind::mixerSend, 1, ret);
    CHECK (a != nullptr);
    if (a != nullptr)
    {
        ALS190KA_NEAR (dbOf (valueAt (*s, *a, 8.0)), 0.0, 0.05);
        CHECK (valueAt (*s, *a, 0.0) == 0.0f);
        CHECK (valueAt (*s, *a, 13.0) == 0.0f);
    }
}

// Jumps: two events at one time keep the set's order (the value before, then after)
TEST (als190ka_jump_keeps_the_sets_order)
{
    using namespace als190ka;
    als190ka::Track t;
    t.envelopes = envelope ("100", fe (-63072000, 1.0) + fe (4, 1.0) + fe (4, 0.1) + fe (8, 0.1));
    const auto s = load (set ({ t }));
    CHECK (s.has_value());
    if (! s)
        return;
    const auto* a = automationFor (*s, lynx::AutomationTargetKind::mixerVolume, 1);
    CHECK (a != nullptr);
    if (a != nullptr)
    {
        ALS190KA_NEAR (dbOf (valueAt (*s, *a, 3.99)), 0.0, 0.05);
        ALS190KA_NEAR (dbOf (valueAt (*s, *a, 4.01)), -20.0, 0.05);
    }
}

// A device's on / off envelope moves the slot's mix (off = the dry signal, Live's bypass); a device saved off but
// switched on by its envelope gets its slot switched on
TEST (als190ka_device_on_envelope_moves_the_slot_mix)
{
    using namespace als190ka;
    als190ka::Track t;
    t.devices = utility (300, 301, false);
    t.envelopes = envelope ("300", be (-63072000, false) + be (4, true) + be (8, false));
    const auto s = load (set ({ t }));
    CHECK (s.has_value());
    if (! s)
        return;
    CHECK (s->mixer[1].effects.size() == 1);
    if (s->mixer[1].effects.empty())
        return;
    CHECK (s->mixer[1].effects[0].enabled);
    const auto* a = automationFor (*s, lynx::AutomationTargetKind::effectMix, 1, 0);
    CHECK (a != nullptr);
    if (a != nullptr)
    {
        CHECK (valueAt (*s, *a, 2.0) == 0.0f);
        ALS190KA_NEAR (valueAt (*s, *a, 6.0), s->mixer[1].effects[0].mix, 1.0e-6);
        CHECK (valueAt (*s, *a, 3.999) == 0.0f); // a step, not a ramp
        CHECK (valueAt (*s, *a, 9.0) == 0.0f);
    }
}

// The Main track's devices go onto the Master insert, with their envelopes
TEST (als190ka_main_track_devices_on_the_master)
{
    using namespace als190ka;
    const auto s = load (set ({ als190ka::Track {} }, 100.0, utility (960, 961), envelope ("961", fe (-63072000, 1.0) + fe (0, 1.0) + fe (8, 0.5))));
    CHECK (s.has_value());
    if (! s)
        return;
    CHECK (s->mixer[0].effects.size() == 1);
    CHECK (automationFor (*s, lynx::AutomationTargetKind::effectParam, 0, 0) != nullptr);
}

// The track's on / off switch under an envelope is named on its own (LYNX's mute is not automated)
TEST (als190ka_track_on_off_automation_is_named)
{
    using namespace als190ka;
    // als175a::mixer gives Speaker no target: add one by hand
    auto text = set ({ als190ka::Track {} });
    const auto at = text.find ("<Speaker><Manual Value=\"true\" /></Speaker>");
    CHECK (at != std::string::npos);
    text.replace (at, std::strlen ("<Speaker><Manual Value=\"true\" /></Speaker>"), "<Speaker><Manual Value=\"true\" /><AutomationTarget Id=\"177\" /></Speaker>");
    const auto close = text.find ("</Envelopes></AutomationEnvelopes></AudioTrack>");
    text.insert (close, envelope ("177", be (-63072000, true) + be (4, false)));
    lynx::FlpImportReport report;
    const auto s = load (text, &report);
    CHECK (s.has_value());
    bool named = false, generic = false;
    for (const auto& [what, n] : report.notReproduced) // (round qafix190kq: automation lines left `unsupported`)
    {
        named = named || what.rfind ("Track on / off automation", 0) == 0;
        generic = generic || what.rfind ("Automation of a device parameter", 0) == 0;
    }
    CHECK (named);
    CHECK (! generic);
}

// A Live video clip (an arrangement audio clip whose file is a video) → a LYNX video track with Live's trim, in
// seconds; its sound stays an audio clip on the track's insert
TEST (als190ka_video_clip_keeps_its_trim)
{
    using namespace als190ka;
    als190ka::Track t;
    // unwarped: the loop is in seconds; at 100 bpm beat 4 = 2.4 s, 6.6667 beats = 4 s; file seconds 5 … 9
    t.clips = audioClip (4.0, 4.0 * 100.0 / 60.0, "E:/sets/clock.mp4", false, 5.0, 9.0);
    lynx::FlpImportReport report;
    const auto s = load (set ({ t }), &report, { "E:/sets/clock.mp4" });
    CHECK (s.has_value());
    if (! s)
        return;
    CHECK (s->videoTracks.size() == 1);
    if (s->videoTracks.size() != 1 || s->videoTracks[0].clips.size() != 1)
        return;
    const auto& c = s->videoTracks[0].clips[0];
    CHECK (c.path == "E:/sets/clock.mp4");
    ALS190KA_NEAR (c.start, 2.4, 1.0e-6);
    ALS190KA_NEAR (c.offset, 5.0, 1.0e-6);
    ALS190KA_NEAR (c.length, 4.0, 1.0e-6);
    CHECK (c.uid != 0);
    int audio = 0;
    for (const auto& p : s->playlist)
        audio += p.kind == lynx::ClipKind::audio;
    CHECK (audio == 1);
    CHECK (lynx::videotrack::isVideoPath ("C:\\a\\b.MOV") && ! lynx::videotrack::isVideoPath ("x.wav") && ! lynx::videotrack::isVideoPath ("mp4.d/x"));
}

// Live's take lanes → LYNX's take lanes: muted rows folded under the track, whose row becomes the comp track
TEST (als190ka_take_lanes_become_lynx_take_lanes)
{
    using namespace als190ka;
    als190ka::Track t;
    t.clips = audioClip (0.0, 4.0, "E:/sets/take2.wav", true, 0.0, 4.0);
    auto text = set ({ t, als190ka::Track {} });
    const std::string lanes = "<TakeLanes><TakeLanes><TakeLane Id=\"0\">" + v ("Name", "Take 1") + "<ClipAutomation><Events>" +
                              audioClip (0.0, 4.0, "E:/sets/take1.wav", true, 0.0, 4.0) + "</Events></ClipAutomation></TakeLane>" +
                              "<TakeLane Id=\"1\">" + v ("Name", "") + "<ClipAutomation><Events>" + audioClip (0.0, 4.0, "E:/sets/take2.wav", true, 0.0, 4.0) +
                              "</Events></ClipAutomation></TakeLane></TakeLanes></TakeLanes>";
    const auto at = text.find ("<AutomationEnvelopes>"); // (the first track's)
    text.insert (at, lanes);
    lynx::FlpImportReport report;
    const auto s = load (text, &report, { "E:/sets/take1.wav", "E:/sets/take2.wav" });
    CHECK (s.has_value());
    if (! s)
        return;
    CHECK (s->playlistTracks.size() == 4); // T1, its 2 lanes, T2
    if (s->playlistTracks.size() != 4)
        return;
    CHECK (s->playlistTracks[0].compTrack && ! s->playlistTracks[0].grouped && ! s->playlistTracks[0].muted);
    CHECK (s->playlistTracks[1].grouped && s->playlistTracks[1].muted && s->playlistTracks[1].name == "Take 1");
    CHECK (s->playlistTracks[2].grouped && s->playlistTracks[2].muted && s->playlistTracks[2].name == "T1 2");
    CHECK (! s->playlistTracks[3].grouped && s->playlistTracks[3].name == "T2");
    int onRow[4] = {};
    for (const auto& c : s->playlist)
        if (c.kind == lynx::ClipKind::audio && c.track >= 0 && c.track < 4)
            ++onRow[c.track];
    CHECK (onRow[0] == 1 && onRow[1] == 1 && onRow[2] == 1 && onRow[3] == 0);
    CHECK (s->playlistTracks[1].mixerTrack == s->playlistTracks[0].mixerTrack);
}

// .als export of automation (coverage arrangement-0115): a send's envelope goes to its return's send knob, and a LYNX
// curve (a bent segment) comes back from Live's straight pieces within 0.1 dB
TEST (als190ka_send_and_curve_automation_round_trip)
{
    using namespace lynx;
    Song s = Song::makeDefault();
    s.setRoute (1, 5, true);
    s.mixer[1].sends.back().level = 0.0f; // a throw: the send rests at −inf
    Automation send;
    send.name = "send";
    send.target = { AutomationTargetKind::mixerSend, 1, 5, 0 };
    send.points = { { 0, 0.0f, 0.0f }, { 4 * ppq, faderPosition (1.0f), 0.0f }, { 8 * ppq, 0.0f, 0.0f } };
    Automation vol;
    vol.name = "volume";
    vol.target = { AutomationTargetKind::mixerVolume, 1, 0, 0 };
    vol.points = { { 0, 0.8f, 0.0f }, { 8 * ppq, 0.2f, 0.0f } };
    vol.points[0].tension = 0.6f; // a bent segment
    vol.points[0].mode = 0;       // (FL's Single curve)
    for (auto* a : { &send, &vol })
    {
        s.automations.push_back (*a);
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = (int) s.automations.size() - 1;
        c.track = (int) s.automations.size() + 2;
        c.start = 2 * ppq;
        c.length = 8 * ppq;
        s.playlist.push_back (c);
    }
    const auto out = exportAls (s);
    CHECK (std::none_of (out.notes.begin(), out.notes.end(), [] (const std::string& n) { return n.find ("Automation not exported") != std::string::npos; }));
    AlsImportOptions o;
    o.common.projectFolder = "E:/sets";
    o.common.fileExists = [] (const std::string&) { return true; };
    const auto back = importAls (out.bytes, o);
    CHECK (back.has_value());
    if (! back)
        return;
    const Automation* sendBack = nullptr;
    const Automation* volBack = nullptr;
    for (const auto& a : back->automations)
    {
        if (a.target.kind == AutomationTargetKind::mixerSend)
            sendBack = &a;
        if (a.target.kind == AutomationTargetKind::mixerVolume && a.target.index > 0)
            volBack = &a;
    }
    CHECK (sendBack != nullptr && volBack != nullptr);
    if (sendBack == nullptr || volBack == nullptr)
        return;
    double worstSend = 0.0, worstVol = 0.0;
    for (double beat = 2.25; beat < 9.9; beat += 0.25)
    {
        const auto want = [&] (const Automation& a) { return als190ka::dbOf (automationCurveAt (a.points, (beat - 2.0) * ppq)); };
        const auto got = [&] (const Automation& a) { return als190ka::dbOf (als190ka::valueAt (*back, a, beat)); };
        const auto clampDb = [] (double d) { return std::max (d, -60.0); };
        worstSend = std::max (worstSend, std::abs (clampDb (want (send)) - clampDb (got (*sendBack))));
        worstVol = std::max (worstVol, std::abs (clampDb (want (vol)) - clampDb (got (*volBack))));
    }
    CHECK (worstSend < 0.2);
    CHECK (worstVol < 0.2);
}

// Envelopes follow Live's own curve between points: straight along Live's fader travel (volume, sends) and bent by Live's
// curve controls on the event that starts the segment. The numbers are Live 12.4's renders of the round's probe sets
// (renders/live: a1 v1, v3, v4; renders/live2: a2 send A), 1 kHz sine, dB relative to the unautomated track.
TEST (als190ka_envelopes_follow_lives_curves)
{
    using namespace als190ka;
    const auto curve = [] (double x1, double y1, double x2, double y2) {
        return " CurveControl1X=\"" + std::to_string (x1) + "\" CurveControl1Y=\"" + std::to_string (y1) + "\" CurveControl2X=\"" + std::to_string (x2) +
               "\" CurveControl2Y=\"" + std::to_string (y2) + "\"";
    };
    const auto neg = 0.0003162277571;
    als190ka::Track v1, v3, v4, s1;
    v1.envelopes = envelope ("100", fe (-63072000, neg) + fe (0, neg) + fe (16, 1.99526231));
    // (Live leaves out the controls equal to 0.5: (0.5, 0, 1, 0.5) is saved as 1Y = 0 and 2X = 1)
    v3.envelopes = envelope ("200", fe (-63072000, 1.0) + fe (0, 1.0, " CurveControl1Y=\"0\" CurveControl2X=\"1\"") + fe (8, 0.0316227749) +
                                        fe (16, 1.0, " CurveControl1X=\"0\" CurveControl2Y=\"1\"") + fe (20, 1.0) + fe (28, 0.0316227749));
    v4.envelopes = envelope ("300", fe (-63072000, 1.0) + fe (0, 1.0, curve (0.9, 0.1, 1.0, 0.0)) + fe (8, 0.0316227749) +
                                        fe (12, 0.0316227749, curve (0.0, 1.0, 0.1, 1.0)) + fe (20, 1.0));
    s1.sends = { neg, neg };
    s1.envelopes = envelope ("410", fe (-63072000, neg) + fe (0, neg) + fe (16, 1.0));
    const auto s = load (set ({ v1, v3, v4, s1 }, 120.0));
    CHECK (s.has_value());
    if (! s)
        return;
    struct Case
    {
        lynx::AutomationTargetKind kind;
        int index, slot;
        std::vector<std::pair<double, double>> measured; // (beat, dB in Live's render)
    };
    const auto ret = (int) s->mixer.size() - 1;
    const std::vector<Case> cases {
        { lynx::AutomationTargetKind::mixerVolume, 1, 0, { { 1, -54.92 }, { 2, -44.64 }, { 4, -28.8 }, { 8, -14.0 }, { 12, -4.0 }, { 14, 1.0 } } },
        { lynx::AutomationTargetKind::mixerVolume, 2, 0, { { 2, -1.01 }, { 4, -3.94 }, { 6, -9.3 }, { 10, -18.38 }, { 14, -6.11 } } },
        { lynx::AutomationTargetKind::mixerVolume, 3, 0, { { 2, -0.63 }, { 4, -1.29 }, { 6, -2.75 }, { 7, -4.92 }, { 13, -4.1 }, { 14, -1.71 }, { 16, -0.31 } } },
        { lynx::AutomationTargetKind::mixerSend, 4, ret, { { 2, -50.64 }, { 4, -34.8 }, { 8, -20.0 }, { 12, -10.0 } } },
    };
    for (const auto& c : cases)
    {
        const auto* a = automationFor (*s, c.kind, c.index, c.slot);
        CHECK (a != nullptr);
        if (a == nullptr)
            continue;
        for (const auto& [beat, db] : c.measured)
            ALS190KA_NEAR (dbOf (valueAt (*s, *a, beat)), db, 0.25);
    }
    // a bent segment of a parameter in its own units (pan): the same curve in the target's 0…1
    als190ka::Track p;
    p.envelopes = envelope ("101", fe (-63072000, -1.0) + fe (0, -1.0, curve (0.9, 0.1, 1.0, 0.0)) + fe (16, 1.0));
    const auto sp = load (set ({ p }, 120.0));
    CHECK (sp.has_value());
    if (sp)
        if (const auto* a = automationFor (*sp, lynx::AutomationTargetKind::mixerPan, 1); a != nullptr)
        {
            // y(x) of the Bézier (0.9, 0.1), (1, 0): at x = 0.5 the curve is still near the start
            const auto v = valueAt (*sp, *a, 8.0);
            CHECK (v > 0.0f && v < 0.2f);
            ALS190KA_NEAR (valueAt (*sp, *a, 16.0), 1.0, 1.0e-6);
        }
}

// Under a one-event tempo envelope Live plays the envelope's tempo, not Tempo/Manual (Live's default set: 100 vs 120)
TEST (als190ka_tempo_envelope_value_wins)
{
    using namespace als190ka;
    const auto s = load (set ({ als190ka::Track {} }, 100.0, {}, envelope ("990", fe (-63072000, 120.0))));
    CHECK (s.has_value());
    if (s)
        ALS190KA_NEAR (s->bpm, 120.0, 1.0e-9);
}
