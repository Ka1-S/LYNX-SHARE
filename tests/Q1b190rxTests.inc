// Round 190rx (0.1.32 Q1 part B: Mixer, Browser, plugins, automation, recording, audio editor, Settings, Online
// sessions, Remote Control): one test per defect found by driving the app and fixed.

#include "core/TypedValue.h"

namespace q1b190rx
{
using namespace lynx;

std::string between (const std::string& text, const std::string& from, const std::string& to)
{
    const auto a = text.find (from);
    if (a == std::string::npos)
        return {};
    const auto b = text.find (to, a + from.size());
    return text.substr (a, b == std::string::npos ? std::string::npos : b - a);
}

Song mixerSong (int inserts)
{
    Song s;
    s.mixer.resize ((std::size_t) inserts + 1);
    for (int t = 0; t <= inserts; ++t)
        s.mixer[(std::size_t) t].name = t == 0 ? "Master" : "Insert " + std::to_string (t);
    return s;
}
} // namespace q1b190rx

// An automation point's "Type in value...": a number is read as the editor shows it (percent, or BPM for a tempo clip);
// text with no number leaves the point alone (it set it to 0 %, a tempo clip to its lowest tempo).
TEST (q1b190rx_typed_automation_value)
{
    using namespace lynx;
    CHECK (typedAutomationValue ("25", false).has_value() && std::abs (*typedAutomationValue ("25", false) - 0.25f) < 1e-6f);
    CHECK (std::abs (*typedAutomationValue (" 50 %", false) - 0.5f) < 1e-6f);
    CHECK (std::abs (*typedAutomationValue ("12,5", false) - 0.125f) < 1e-6f);
    CHECK (*typedAutomationValue ("150", false) == 1.0f);
    CHECK (*typedAutomationValue ("-5", false) == 0.0f);
    CHECK (std::abs (*typedAutomationValue ("120,5", true) - valueFromTempo (120.5)) < 1e-6f);
    CHECK (! typedAutomationValue ("abc", false).has_value());
    CHECK (! typedAutomationValue ("", true).has_value());
    CHECK (! typedAutomationValue ("-", false).has_value());
    CHECK (! typedAutomationValue (".", true).has_value());
}

// In the automation editor's LFO mode the points shown are the LFO's amplitude points: "Type in value..." on one of them
// showed and wrote the clip point with the same number (driven: 190rx logs/au2*).
TEST (q1b190rx_lfo_type_in_writes_the_lfo_amplitude)
{
    const auto src = i18ntest::readFile ("src/ui/AutomationUi.cpp");
    const auto typeValue = q1b190rx::between (src, "    void typeValue (int point)", "    juce::PopupMenu curveMenu()");
    CHECK (typeValue.find ("typeAutomationPointValue (engine, automation, point, a != nullptr && a->lfo.on)") != std::string::npos);
    const auto fn = q1b190rx::between (src, "void typeAutomationPointValue (AudioEngine& engine, int automation, int point, bool lfoAmplitude)",
                                       "juce::String describeAutomationTarget");
    CHECK (fn.find ("const auto& points = lfoAmplitude ? a.lfo.amp : a.points;") != std::string::npos);
    CHECK (fn.find ("auto& pts = lfoAmplitude ? x.lfo.amp : x.points;") != std::string::npos);
    CHECK (fn.find ("typedAutomationValue (") != std::string::npos && fn.find ("if (! v)") != std::string::npos);
    CHECK (fn.find ("retainCharacters") == std::string::npos);
}

// Mixer › Insert multiple... at insert 3 renumbers the inserts the channels play into: the undo step was named after the
// first channel ("Channel: Hat"); it is named for the inserts.
TEST (q1b190rx_insert_multiple_undo_label)
{
    using namespace lynx;
    auto before = q1b190rx::mixerSong (4);
    before.channels.resize (2);
    before.channels[0].name = "Kick";
    before.channels[0].mixerTrack = 1;
    before.channels[1].name = "Hat";
    before.channels[1].mixerTrack = 4;
    auto after = before;
    CHECK (mixerops::insertTracks (after, 3, 5) == 5);
    CHECK (after.channels[1].mixerTrack == 9); // (the channel follows its insert)
    CHECK (std::string (describeChange (before, after).text) == "Add insert");
    CHECK (std::string (describeChange (after, before).text) == "Remove insert");
}

// A route that would make a loop is found before the edit, so the Mixer can say why and skip the empty undo step
// (driven: Track routing › Route selected to this track did nothing, said nothing, and left an "Edit" step).
TEST (q1b190rx_looping_routes)
{
    using namespace lynx;
    auto s = q1b190rx::mixerSong (5);
    s.setRoute (2, 4, true);
    s.setRoute (4, 5, true);
    const auto loops = mixerops::loopingRoutes (s, { 4 }, { 2 });
    CHECK (loops.size() == 1 && loops[0].first == 4 && loops[0].second == 2);
    CHECK (mixerops::loopingRoutes (s, { 5 }, { 2 }).size() == 1);  // (5 is fed by 2 through 4)
    CHECK (mixerops::loopingRoutes (s, { 2 }, { 4 }).empty());      // (the route is there)
    CHECK (mixerops::loopingRoutes (s, { 3 }, { 2 }).empty());      // (a fine route)
    CHECK (mixerops::loopingRoutes (s, { 4 }, { 0 }).empty());      // (the Master is never a loop)
    CHECK (mixerops::loopingRoutes (s, { 4, 3, 5 }, { 2 }).size() == 2);
    CHECK (mixerops::routeSelectedTo (s, { 4 }, 2, mixerops::RouteKind::route) == 0); // (still refused by the edit itself)
    const auto ui = i18ntest::readFile ("src/ui/MixerComponent.cpp");
    CHECK (ui.find ("LoopCheck explainLoopingRoutes (const Song& song") != std::string::npos);
    CHECK (ui.find ("if (explainLoopingRoutes (engine.getSong(), others, { t }).nothingToDo())") != std::string::npos);
    CHECK (ui.find ("if (explainLoopingRoutes (engine.getSong(), { t }, others).nothingToDo())") != std::string::npos);
    CHECK (ui.find ("if (! on && t > 0 && explainLoopingRoutes (engine.getSong(), sources, { t }).nothingToDo())") != std::string::npos);
    CHECK (ui.find ("if (t > 0 && explainLoopingRoutes (engine.getSong(), selection, { t }).nothingToDo())") != std::string::npos);
}

// Online sessions: a guest (or the host) whose running session the relay lost was told "...or the code is wrong" (the
// branch with the right words came after the plain 404 one and never ran); a code that can't be an invite code is said
// before "Save changes?" lets the open project go.
TEST (q1b190rx_live_session_messages)
{
    const auto engine = i18ntest::readFile ("src/engine/LiveSessions.cpp");
    const auto running404 = engine.find ("else if (e.code == 404 && running)");
    const auto plain404 = engine.find ("else if (e.code == 404)\n");
    CHECK (running404 != std::string::npos && plain404 != std::string::npos && running404 < plain404);
    const auto ui = i18ntest::readFile ("src/ui/LiveSessionUi.cpp");
    const auto parse = ui.find ("if (! livecrypto::parseCode (code.toStdString()))");
    const auto discard = ui.find ("c.confirmDiscard ([c, name, code] {");
    CHECK (parse != std::string::npos && discard != std::string::npos && parse < discard);
}

// "Link to MIDI controller (move a knob on the controller)" waited for ever when no controller answered (none
// connected): Esc ends it, and the hint says so (and that no MIDI input is open).
TEST (q1b190rx_midi_learn_can_be_cancelled)
{
    const auto main = i18ntest::readFile ("src/ui/MainComponent.cpp");
    CHECK (main.find ("&& engine.isLearningMidi())\n    {\n        engine.learnMidiController ({});") != std::string::npos);
    CHECK (main.find ("tr (\"Move a knob/fader on the MIDI controller to link it to the parameter... (Esc cancels)\")") != std::string::npos);
    const auto engine = i18ntest::readFile ("src/engine/AudioEngine.cpp");
    const auto learn = q1b190rx::between (engine, "void AudioEngine::learnMidiController (const AutomationTarget& target)", "\n}\n");
    CHECK (learn.find ("learning = target;") != std::string::npos); // (an empty target ends the learn: isLearningMidi)
}

// Discord (sidthenugget, 11-26, Windows): File › Open project "sometimes" showed LYNX's built-in browser in the install
// folder. A native dialog closed within a second (Esc right after Ctrl+O) was taken for one that never showed (issue
// #72's Linux fallback ran on every platform) and every later chooser of the session was the built-in one. The fallback
// is Linux / BSD only; Windows and macOS always get their own dialogs; no chooser bypasses ui/FileChoosers.h.
TEST (q1b190rx_native_file_dialogs_stay_native)
{
    const auto h = i18ntest::readFile ("src/ui/FileChoosers.h");
    const auto gate = q1b190rx::between (h, "#if JUCE_LINUX || JUCE_BSD\nconstexpr bool quickFailMeansBroken = true;", "#endif");
    CHECK (gate.find ("#else\nconstexpr bool quickFailMeansBroken = false;") != std::string::npos);
    CHECK (h.find ("if (! info.native || forced || ! quickFailMeansBroken)\n        return callback;") != std::string::npos);
    const auto native = q1b190rx::between (h, "inline bool nativeDialogs()", "\n}\n");
    const auto broken = native.find ("brokenThisSession()");
    const auto linuxOnly = native.find ("#if JUCE_LINUX || JUCE_BSD");
    CHECK (broken != std::string::npos && linuxOnly != std::string::npos && linuxOnly < broken);
    int direct = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (std::filesystem::path ("src")))
        if (entry.is_regular_file() && entry.path().filename() != "FileChoosers.h")
        {
            const auto text = i18ntest::readFile (entry.path());
            if (text.find ("std::make_unique<juce::FileChooser>") != std::string::npos || text.find ("std::make_shared<juce::FileChooser>") != std::string::npos)
                ++direct;
        }
    CHECK (direct == 0);
}

// Settings › Audio's sample rate changed while recording (48 -> 44.1 kHz, driven on a PipeWire loop node): the take
// kept its 48 kHz file and went on at 44.1 kHz, so everything after the change played sped up (440 Hz -> 479 Hz) and
// nothing was said. The device restart now ends the recording before a frame at the new rate is written; the take up
// to the change is kept and the input notice says why.
TEST (q1b190rx_sample_rate_change_ends_the_take)
{
    const auto engine = i18ntest::readFile ("src/engine/AudioEngine.cpp");
    const auto start = q1b190rx::between (engine, "void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)",
                                          "transport.sampleRate.store (device->getCurrentSampleRate());");
    CHECK (start.find ("if (takeCount > 0 && std::abs (device->getCurrentSampleRate() - takeSampleRate) > 0.5)") != std::string::npos);
    CHECK (start.find ("transport.recording.store (false);") != std::string::npos);
    const auto update = q1b190rx::between (engine, "void AudioEngine::updateAudioRecording()", "// the files are ready before Play");
    const auto finish = update.find ("finishAudioTake();");
    const auto stopRec = update.find ("setRecording (false);");
    CHECK (finish != std::string::npos && stopRec != std::string::npos && finish < stopRec); // (no take open when setRecording re-enters)
    CHECK (update.find ("notify (InputIssue::failed, 0,") != std::string::npos);
}
