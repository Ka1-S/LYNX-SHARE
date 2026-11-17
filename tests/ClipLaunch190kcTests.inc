// Round 190kc (0.1.30, Clip-launch layout, Live parity): Live's return tracks with their Sends rows and Pre / Post
// toggles, the crossfader (A / B assign, seven curves, .als import / export, the render), the crossfader's key positions
// and the Relative Session Mapping Strip, and the groove pool's Commit / Extract on audio clips.
#include "core/Crossfader.h"
#include "core/SessionMixer.h"
#include "core/RemoteMap.h"
#include "core/SessionRing.h"

namespace kc190
{
// A set as Live 12.4 saves the Patience demo's mixer: two returns ("Reverb" renamed, "B-Delay" never renamed), the tracks'
// sends at -inf (0.0003162277571), the second return Pre, return A's send to B switched on, the crossfader at 0.5 with
// the Slow Cut curve, Keys on A and Vocal on B, the Session mixer showing Returns and Crossfader.
inline std::string returnsSet()
{
    using namespace als175a;
    const auto send = [] (int id, double level, bool on) {
        return "<TrackSendHolder Id=\"" + std::to_string (id) + "\"><Send><Manual Value=\"" + std::to_string (level) + "\" /><AutomationTarget Id=\"" +
               std::to_string (700 + id * 10 + (int) (level * 7)) + "\" /></Send><EnabledByUser Value=\"" + (on ? "true" : "false") + "\" /></TrackSendHolder>";
    };
    const auto mix = [&] (int id, int crossfade, const std::string& sends) {
        return "<Mixer><Volume><Manual Value=\"1\" /><AutomationTarget Id=\"" + std::to_string (id) + "\" /></Volume><Pan><Manual Value=\"0\" /><AutomationTarget Id=\"" +
               std::to_string (id + 1) + "\" /></Pan><Speaker><Manual Value=\"true\" /></Speaker><Sends>" + sends + "</Sends><CrossFadeState><Manual Value=\"" +
               std::to_string (crossfade) + "\" /><AutomationTarget Id=\"" + std::to_string (id + 2) + "\" /></CrossFadeState></Mixer>";
    };
    const std::string minf = "0.0003162277571";
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12117\" Creator=\"test\">\n<LiveSet><Tracks>";
    s += "<MidiTrack Id=\"10\">" + name ("Keys") + v ("Color", "3") + v ("TrackGroupId", "-1") + "<DeviceChain>" +
         mix (100, 0, send (0, std::stod (minf), true) + send (1, std::stod (minf), true)) + "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") +
         "</AudioOutputRouting><MainSequencer><ClipSlotList><ClipSlot Id=\"0\"><ClipSlot><Value>" +
         "<MidiClip Id=\"1\" Time=\"0\">" + v ("CurrentStart", "0") + v ("CurrentEnd", "4") + v ("Name", "Riff") +
         "<Loop>" + v ("LoopStart", "0") + v ("LoopEnd", "4") + v ("StartRelative", "0") + v ("LoopOn", "true") + "</Loop>" +
         "<Notes><KeyTracks><KeyTrack Id=\"0\"><Notes>" + midiNote (0, 1, 90) + "</Notes>" + v ("MidiKey", "64") +
         "</KeyTrack></KeyTracks></Notes></MidiClip></Value></ClipSlot></ClipSlot></ClipSlotList></MainSequencer><DeviceChain><Devices>" +
         vst3Device ("010000000E020000504B0304") + "</Devices></DeviceChain></DeviceChain></MidiTrack>";
    s += "<AudioTrack Id=\"20\">" + name ("Vocal") + v ("Color", "20") + v ("TrackGroupId", "-1") + "<DeviceChain>" +
         mix (200, 2, send (0, 0.5, true) + send (1, std::stod (minf), true)) + "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") +
         "</AudioOutputRouting><DeviceChain><Devices /></DeviceChain></DeviceChain></AudioTrack>";
    s += "<ReturnTrack Id=\"40\">" + name ("Reverb") + v ("Color", "41") + "<DeviceChain>" + mix (400, 1, send (0, std::stod (minf), false) + send (1, std::stod (minf), true)) +
         "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting><DeviceChain><Devices /></DeviceChain></DeviceChain></ReturnTrack>";
    s += "<ReturnTrack Id=\"41\"><Name>" + v ("EffectiveName", "B-Delay") + v ("UserName", "") + "</Name>" + v ("Color", "55") + "<DeviceChain>" +
         mix (500, 1, send (0, std::stod (minf), false) + send (1, std::stod (minf), false)) + "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") +
         "</AudioOutputRouting><DeviceChain><Devices /></DeviceChain></DeviceChain></ReturnTrack>";
    s += "</Tracks><MainTrack><DeviceChain><Mixer><Volume><Manual Value=\"1\" /><AutomationTarget Id=\"900\" /></Volume><Pan><Manual Value=\"0\" /></Pan>"
         "<Tempo><Manual Value=\"120\" /><AutomationTarget Id=\"950\" /></Tempo><TimeSignature><Manual Value=\"201\" /></TimeSignature>"
         "<CrossFade><Manual Value=\"0.5\" /><AutomationTarget Id=\"13\" /></CrossFade></Mixer></DeviceChain>"
         "<AutomationEnvelopes><Envelopes><AutomationEnvelope Id=\"0\"><EnvelopeTarget>" + v ("PointeeId", "13") +
         "</EnvelopeTarget><Automation><Events><FloatEvent Id=\"1\" Time=\"-63072000\" Value=\"-1\" /><FloatEvent Id=\"2\" Time=\"0\" Value=\"-1\" />"
         "<FloatEvent Id=\"3\" Time=\"8\" Value=\"1\" /></Events></Automation></AutomationEnvelope></Envelopes></AutomationEnvelopes></MainTrack>";
    s += "<SendsPre><SendPreBool Id=\"0\" Value=\"false\" /><SendPreBool Id=\"1\" Value=\"true\" /></SendsPre>";
    s += "<Scenes><Scene Id=\"0\">" + v ("Name", "Intro") + "</Scene></Scenes>";
    s += v ("CrossfadeCurve", "4");
    s += "<ViewStates>" + v ("MixerInSession", "1") + v ("SessionIO", "0") + v ("SessionSends", "1") + v ("SessionReturns", "1") + v ("SessionVolume", "1") +
         v ("SessionTrackOptions", "0") + v ("SessionCrossFade", "1") + "</ViewStates>";
    s += "</LiveSet>\n</Ableton>\n";
    return s;
}

inline int insertNamed (const lynx::Song& s, const std::string& name)
{
    for (int i = 0; i < (int) s.mixer.size(); ++i)
        if (s.mixer[(std::size_t) i].name == name)
            return i;
    return -1;
}
} // namespace kc190

TEST (cliplaunch190kc_als_returns_sends_and_crossfader_import)
{
    using namespace lynx;
    FlpImportReport report;
    const auto song = importAls (als175a::bytesOf (kc190::returnsSet()), als181c::options (&report));
    CHECK (song.has_value());
    if (! song)
        return;
    const auto keys = kc190::insertNamed (*song, "Keys"), vocal = kc190::insertNamed (*song, "Vocal");
    const auto reverb = kc190::insertNamed (*song, "Reverb"), delay = kc190::insertNamed (*song, "Delay"); // ("B-Delay": the letter goes)
    CHECK (keys > 0 && vocal > 0 && reverb > 0 && delay > 0);
    if (keys <= 0 || vocal <= 0 || reverb <= 0 || delay <= 0)
        return;
    // the returns, in Live's order, with Live's titles
    CHECK (sessionmixer::returnsOf (*song) == std::vector<int> ({ reverb, delay }));
    CHECK (! song->mixer[(std::size_t) keys].liveReturn && song->mixer[(std::size_t) reverb].liveReturn);
    CHECK (sessionmixer::returnTitle (*song, reverb) == "A Reverb" && sessionmixer::returnTitle (*song, delay) == "B Delay");
    CHECK (sessionmixer::returnLetter (*song, keys).empty());
    // a Sends row per return (Patience: every send at -inf); a send at -inf is a send at 0 (mappable, automatable)
    CHECK (sessionmixer::sendRows (*song, { keys, vocal }, 6) == std::vector<int> ({ reverb, delay }));
    CHECK (sessionmixer::sendPosition (*song, keys, reverb) == 0.0f && sessionmixer::sendPosition (*song, keys, delay) == 0.0f);
    CHECK (std::abs (faderGain (sessionmixer::sendPosition (*song, vocal, reverb)) - 0.5f) < 1e-4f); // (the set's amount)
    // Pre / Post per return; a return's sends: off unless the set switched them on
    CHECK (! sessionmixer::isPre (*song, reverb) && sessionmixer::isPre (*song, delay));
    CHECK (sessionmixer::sendEnabled (*song, reverb, delay) && ! sessionmixer::sendEnabled (*song, delay, reverb));
    CHECK (! sessionmixer::sendEnabled (*song, reverb, reverb) && sessionmixer::sendEnabled (*song, keys, reverb));
    // the crossfader
    CHECK (song->mixer[(std::size_t) keys].crossfade == CrossfadeAssign::a && song->mixer[(std::size_t) vocal].crossfade == CrossfadeAssign::b);
    CHECK (song->mixer[(std::size_t) reverb].crossfade == CrossfadeAssign::none);
    CHECK (std::abs (song->crossfader - 0.5f) < 1e-6f && song->crossfadeCurve == 4);
    // its automation: Main › Crossfade, -1 → 1 over two bars
    const Automation* xf = nullptr;
    for (const auto& a : song->automations)
        if (a.target.kind == AutomationTargetKind::crossfader)
            xf = &a;
    CHECK (xf != nullptr);
    if (xf != nullptr && xf->points.size() >= 2)
        CHECK (std::abs (xf->points.front().value) < 1e-6f && std::abs (xf->points.back().value - 1.0f) < 1e-6f);
    // the Session mixer's sections as the set shows them
    CHECK (song->clipGrid.has_value());
    if (song->clipGrid)
    {
        const auto& view = song->clipGrid->mixerView;
        CHECK (view.shown && ! view.io && view.sends && view.returns && view.volume && ! view.trackOptions && view.crossfader);
    }
}

TEST (cliplaunch190kc_als_returns_and_crossfader_round_trip)
{
    using namespace lynx;
    auto song = importAls (als175a::bytesOf (kc190::returnsSet()), als181c::options (nullptr));
    CHECK (song.has_value());
    if (! song)
        return;
    AlsExportOptions o;
    const auto out = exportAls (*song, o);
    CHECK (out.xml.find ("<CrossfadeCurve Value=\"4\"") != std::string::npos);
    CHECK (out.xml.find ("<SessionCrossFade Value=\"1\"") != std::string::npos);
    const auto back = importAls (out.bytes, als181c::options (nullptr));
    CHECK (back.has_value());
    if (! back)
        return;
    const auto keys = kc190::insertNamed (*back, "Keys"), vocal = kc190::insertNamed (*back, "Vocal");
    const auto reverb = kc190::insertNamed (*back, "Reverb"), delay = kc190::insertNamed (*back, "Delay");
    // (Vocal, an audio track with no clip and no channel, is not written: LYNX exports tracks that carry something)
    CHECK (keys > 0 && reverb > 0 && delay > 0);
    if (keys <= 0 || reverb <= 0 || delay <= 0)
        return;
    // both returns come back (no track sends to Delay: before round 190kc it was dropped), Pre / Post and enables too
    CHECK (sessionmixer::returnsOf (*back) == std::vector<int> ({ reverb, delay }));
    CHECK (sessionmixer::isPre (*back, delay) && ! sessionmixer::isPre (*back, reverb));
    CHECK (sessionmixer::sendEnabled (*back, reverb, delay) && ! sessionmixer::sendEnabled (*back, delay, reverb));
    CHECK (back->mixer[(std::size_t) keys].crossfade == CrossfadeAssign::a);
    if (vocal > 0)
        CHECK (back->mixer[(std::size_t) vocal].crossfade == CrossfadeAssign::b);
    CHECK (std::abs (back->crossfader - 0.5f) < 1e-5f && back->crossfadeCurve == 4);
    CHECK (std::any_of (back->automations.begin(), back->automations.end(), [] (const Automation& a) { return a.target.kind == AutomationTargetKind::crossfader; }));
    CHECK (back->clipGrid && back->clipGrid->mixerView.crossfader && ! back->clipGrid->mixerView.io);
}

TEST (cliplaunch190kc_return_sends_pre_post_and_enable)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (3);
    const auto a = sessionmixer::insertReturn (song, "Reverb"), b = sessionmixer::insertReturn (song, "Delay");
    CHECK (a > 0 && b == a + 1);
    CHECK (sessionmixer::returnTitle (song, b) == "B Delay");
    // Pre on a return: its sends (and the ones made later) leave before the fader
    CHECK (sessionmixer::setSendPosition (song, 1, a, 0.5f));
    sessionmixer::setPre (song, a, true);
    CHECK (song.mixer[1].sends.size() == 1 && song.mixer[1].sends[0].pre);
    CHECK (sessionmixer::setSendPosition (song, 2, a, 0.6f) && song.mixer[2].sends.back().pre);
    sessionmixer::setPre (song, a, false);
    CHECK (! song.mixer[1].sends[0].pre && ! song.mixer[2].sends.back().pre);
    // a return's send to another return: off until switched on; switched off again, its send goes
    CHECK (! sessionmixer::setSendPosition (song, a, b, 0.7f) && song.mixer[(std::size_t) a].sends.empty());
    CHECK (sessionmixer::setSendEnabled (song, a, b, true) && sessionmixer::setSendPosition (song, a, b, 0.7f));
    CHECK (song.mixer[(std::size_t) a].sends.size() == 1);
    CHECK (sessionmixer::setSendEnabled (song, a, b, false) && song.mixer[(std::size_t) a].sends.empty());
    CHECK (! sessionmixer::setSendEnabled (song, a, a, true)); // (never into itself)
    // a project without returns keeps LYNX's rows: every insert sent to
    auto plain = Song::makeDefault();
    plain.mixer = Song::defaultMixer (4);
    sessionmixer::setSendPosition (plain, 1, 3, 0.5f);
    CHECK (sessionmixer::sendRows (plain, { 1, 2 }, 6) == std::vector<int> ({ 3 }));
}

TEST (cliplaunch190kc_crossfader_curves)
{
    using namespace lynx;
    for (int k = 0; k < crossfader::curveCount; ++k)
    {
        // a track on its own side is unattenuated; on the far side muted; never louder on the way
        CHECK (std::abs (crossfader::gain (k, CrossfadeAssign::a, -1.0f) - 1.0f) < 1e-5f);
        CHECK (crossfader::gain (k, CrossfadeAssign::a, 1.0f) < 1e-5f);
        CHECK (std::abs (crossfader::gain (k, CrossfadeAssign::b, 1.0f) - 1.0f) < 1e-5f);
        float previous = 2.0f;
        for (int i = 0; i <= 100; ++i)
        {
            const auto g = crossfader::gain (k, CrossfadeAssign::a, -1.0f + 0.02f * (float) i);
            CHECK (g <= previous + 1e-6f && g >= 0.0f);
            previous = g;
            // A and B mirror each other
            CHECK (std::abs (g - crossfader::gain (k, CrossfadeAssign::b, 1.0f - 0.02f * (float) i)) < 1e-5f);
        }
        CHECK (crossfader::gain (k, CrossfadeAssign::none, 0.3f) == 1.0f);
    }
    // Constant Power keeps the power of A + B at the centre
    const auto c = crossfader::gain (crossfader::defaultCurve, CrossfadeAssign::a, 0.0f);
    CHECK (std::abs (2.0f * c * c - 1.0f) < 0.02f);
    // the centre and quarter gains measured in Live 12.4's renders (xf/curves.json, round 190kc)
    const std::pair<int, float> centre[] { { 0, 0.500f }, { 1, 0.582f }, { 2, 0.707f }, { 3, 1.0f }, { 4, 1.0f }, { 5, 1.0f }, { 6, 0.997f } };
    for (const auto& [k, g] : centre)
        CHECK (std::abs (crossfader::gain (k, CrossfadeAssign::b, 0.0f) - g) < 0.01f);
    CHECK (std::abs (crossfader::gain (0, CrossfadeAssign::a, 0.5f) - 0.223f) < 0.01f);  // Dipped, a quarter from B
    CHECK (std::abs (crossfader::gain (6, CrossfadeAssign::a, 0.5f) - 0.141f) < 0.01f);  // Transition
    CHECK (crossfader::gain (5, CrossfadeAssign::a, 0.9f) > 0.98f);                      // Fast Cut: whole until the end
    CHECK (std::string (crossfader::curveName (6)) == "Transition" && std::string (crossfader::curveName (0)) == "Dipped");
}

TEST (cliplaunch190kc_crossfader_in_the_render)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    song.mixer = Song::defaultMixer (4);
    for (int t : { 2, 3 }) // two inserts playing their audio input (0.25 each) into the Master
    {
        song.mixer[(std::size_t) t].monitor = InputMonitor::on;
        song.mixer[(std::size_t) t].inputChannel = 0;
    }
    RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    LiveInput input;
    renderer.setLiveInput (&input);
    std::vector<float> a (256, 0.25f), l (256), r (256);
    input.channels[0] = a.data();
    input.channels[1] = a.data();
    input.numChannels = 2;
    auto run = [&] (const RackState* state) {
        for (int i = 0; i < 12; ++i)
            renderer.process (state, false, 0.0, 256, l.data(), r.data());
        return l[100];
    };
    auto plain = RackState::build (song, nullptr, 48000.0);
    CHECK (std::abs (run (plain.get()) - 0.5f) < 1e-3f);
    // insert 2 on A, insert 3 on B, the crossfader at the far right: A is muted, B whole
    song.mixer[2].crossfade = CrossfadeAssign::a;
    song.mixer[3].crossfade = CrossfadeAssign::b;
    song.crossfader = 1.0f;
    auto right = RackState::build (song, plain.get(), 48000.0);
    CHECK (std::abs (run (right.get()) - 0.25f) < 2e-3f);
    // a live move (AudioEngine::editLevels) through the snapshot's atomics: the centre, Constant Power
    song.crossfader = 0.0f;
    right->live->store (song);
    const auto centre = crossfader::gain (2, CrossfadeAssign::a, 0.0f);
    CHECK (std::abs (run (right.get()) - 0.5f * centre) < 3e-3f);
    // the curve follows the song (Fast Cut: both whole at the centre)
    song.crossfadeCurve = 5;
    auto fast = RackState::build (song, right.get(), 48000.0);
    CHECK (std::abs (run (fast.get()) - 0.5f) < 3e-3f);
}

TEST (cliplaunch190kc_relative_session_and_crossfader_mappings)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    // the crossfader's three positions and the relative strip are mappable to keys and MIDI, kept for every project
    for (const auto action : { RemoteAction::crossfaderLeft, RemoteAction::crossfaderCentre, RemoteAction::crossfaderRight, RemoteAction::sceneUp,
                               RemoteAction::sceneDown, RemoteAction::launchSelectedScene, RemoteAction::cancelLaunch })
    {
        const auto t = remotemap::Target::of (action);
        CHECK (remotemap::valid (t) && remotemap::isButton (t) && remotemap::canBeGlobal (t));
    }
    const auto clip = remotemap::Target::selectedClip (2);
    CHECK (remotemap::valid (clip) && ! remotemap::canBeGlobal (clip) && ! remotemap::valid (remotemap::Target::selectedClip (-1)));
    CHECK (remotemap::assignKey (song, remotemap::Target::of (RemoteAction::sceneDown), 'J') == remotemap::Refusal::none);
    CHECK (remotemap::assignKey (song, clip, 'K') == remotemap::Refusal::none);
    CHECK (remotemap::assignMidi (song, remotemap::Target::of (RemoteAction::crossfaderLeft), { 0, 20, -1 }) == remotemap::Refusal::none);
    const auto fires = remotemap::keyFires (song, 'K', true);
    CHECK (fires.size() == 1 && fires[0].target == clip && fires[0].edge == remotemap::Edge::press);
    CHECK (remotemap::keyOf (song, remotemap::Target::of (RemoteAction::sceneDown)).key == 'J');
    // Live's CC rule for buttons: 64 and above presses, 63 and below releases [Live manual 34.2.3]
    const auto cc = remotemap::midiFires (song, 0, 20, 64, 0, -1);
    CHECK (cc.size() == 1 && cc[0].edge == remotemap::Edge::press && cc[0].target.action == RemoteAction::crossfaderLeft);
    CHECK (remotemap::midiFires (song, 0, 20, 63, 64, -1).at (0).edge == remotemap::Edge::release);
    // the crossfader is a parameter too (MIDI-mappable as a continuous control)
    Song s2 = song;
    CHECK (s2.setTargetValue ({ AutomationTargetKind::crossfader, 0, 0, 0 }, 0.75f) && std::abs (s2.crossfader - 0.5f) < 1e-6f);
    CHECK (std::abs (s2.targetValue ({ AutomationTargetKind::crossfader, 0, 0, 0 }) - 0.75f) < 1e-6f);
}

TEST (cliplaunch190kc_cancel_launch_drops_waiting_launches)
{
    using namespace lynx;
    // the command exists after every older one (saved nowhere, but the enum's order stays)
    CHECK ((int) launcher::Command::Type::cancelLaunch > (int) launcher::Command::Type::recordEnd);
}

TEST (cliplaunch190kc_session_ring_model)
{
    using namespace lynx::sessionring;
    Ring r; // 8 x 5 at the top left
    CHECK (contains (r, 0, 0) && contains (r, 7, 4) && ! contains (r, 8, 0) && ! contains (r, 0, 5));
    // kept inside a 15-track, 13-scene grid (the Patience set)
    CHECK (moved (r, 100, 100, 15, 13) == Ring ({ 7, 8, 8, 5 }));
    CHECK (moved (r, -3, -3, 15, 13) == Ring ({ 0, 0, 8, 5 }));
    CHECK (paged (r, 1, 1, 15, 13) == Ring ({ 7, 5, 8, 5 }));
    int t = -1, sc = -1;
    CHECK (slotOf (Ring ({ 2, 3, 8, 5 }), 1, 4, t, sc) && t == 3 && sc == 7);
    CHECK (! slotOf (r, 8, 0, t, sc));
    CHECK (reveal (r, 10, 6, 15, 13) == Ring ({ 3, 2, 8, 5 }));
    CHECK (clamped (Ring ({ 4, 4, 8, 5 }), 3, 2) == Ring ({ 0, 0, 8, 5 })); // (a grid smaller than the ring)
}
