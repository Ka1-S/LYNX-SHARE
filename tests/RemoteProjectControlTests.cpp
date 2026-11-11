#include "engine/RemoteProjectControl.h"
#include "engine/RemoteJsonPatch.h"
#include "engine/RemoteFileSecurity.h"
#include "engine/ProjectFile.h"
#include "engine/RemoteNotes.h"
#include "core/SessionIds.h"
#include "core/ClipLauncher.h"
#include "core/PluginSubstitutes.h"
#include "core/FlExtraFx.h"
#include "core/VideoPlayer.h"
#include "core/VisualFx.h"
#include "core/Vfx.h"
#include <cstdio>
#include <stdexcept>
#include <limits>

using namespace lynx;
namespace
{
void check (bool condition, const char* message)
{
    if (! condition) throw std::runtime_error (message);
}
juce::var json (const char* text)
{
    juce::var result;
    check (juce::JSON::parse (text, result).wasOk(), "Bad test JSON");
    return result;
}
template <class Fn> void refuses (Fn&& operation)
{
    bool rejected = false;
    try { operation(); } catch (const std::runtime_error&) { rejected = true; }
    check (rejected, "Invalid transaction was accepted");
}
template <class Fn> void refusesWith (Fn&& operation, const char* reason)
{
    std::string actual = "operation was accepted";
    try { operation(); }
    catch (const std::runtime_error& error)
    {
        actual = error.what();
        if (actual.find (reason) != std::string::npos) return;
    }
    throw std::runtime_error (std::string ("Expected rejection containing '") + reason + "', got: " + actual);
}
juce::var addValue (const char* path, const juce::var& value)
{
    auto* operation = new juce::DynamicObject();
    operation->setProperty ("op", "add");
    operation->setProperty ("path", path);
    operation->setProperty ("value", value);
    return juce::Array<juce::var> { juce::var (operation) };
}
juce::String encoded (const std::vector<std::uint8_t>& bytes)
{
    return juce::Base64::toBase64 (bytes.data(), bytes.size());
}
Song fixture()
{
    Song song;
    ChannelDef channel;
    channel.name = "Lead";
    channel.synth.cutoff = 2432.12353515625f;
    channel.synth.resonance = 0.123456791f;
    channel.modelParams = { 0.123456791f, 1.23456788e-12f, 1.23456789e20f };
    channel.synth.spectrum[0] = 0.123456791f;
    channel.synth.spectrum[1] = 1.23456788e-12f;
    channel.synth.spectrumPhase[0] = 0.234567896f;
    channel.synth.spectrumDecay[0] = 2.12345672f;
    channel.synth.spectrumTarget[0] = 0.345678925f;
    channel.volume = 0.73123455f;
    song.channels.push_back (channel);
    song.mixer = Song::defaultMixer (2);
    song.mixer[1].volume = 0.812345683f;
    song.mixer[1].pan = -0.123456791f;
    song.mixer[1].stereo = 0.234567896f;
    song.mixer[1].sends.push_back ({ 2, 0.345678925f });
    song.mixer[1].lostSends.push_back ({ "Deleted insert", 0.456789017f });
    song.swing = 0.123456791f;
    song.chanceSeed = 0x89abcdefu;
    song.bpm = 130.12345678901235;
    Pattern pattern;
    pattern.name = "Lead";
    pattern.lengthTicks = 3840;
    Note note;
    note.channel = 0;
    note.start = 13;
    note.length = 1001;
    note.velocity = 0.73123455f;
    note.pan = 0.123456791f;
    note.finePitch = 4.56789112f;
    note.release = 0.456789017f;
    note.modX = 0.89123404f;
    note.modY = 0.345678925f;
    note.chance = 0.73123455f;
    note.velocityRange = 0.123456791f;
    note.chanceGroup = 7;
    note.chanceOne = true;
    pattern.notes.push_back (note);
    note.start = 1920;
    note.chance = 1.0f;
    note.velocityRange = 0.0f;
    note.chanceGroup = 0;
    pattern.notes.push_back (note); // Exact documents also preserve the inactive play-one flag.
    PatternEvents lane;
    lane.target = { AutomationTargetKind::channelVolume, 0 };
    lane.events.push_back ({ 17, 0.765432119f });
    pattern.events.push_back (lane);
    song.patterns.push_back (pattern);
    Automation automation;
    automation.name = "Level";
    automation.target = { AutomationTargetKind::channelVolume, 0 };
    automation.points.push_back ({ 0, 0.765432119f, 0.456789017f });
    song.automations.push_back (automation);
    Clip clip;
    clip.pattern = 0;
    clip.length = 3840;
    clip.gain = 0.123456791f;
    clip.offsetFraction = 0.234567896f;
    clip.pan = -0.345678925f;
    clip.pitch = 0.456789017f;
    clip.stretch = 1.123456789012345;
    song.playlist.push_back (clip);
    uids::normalize (song);
    return song;
}
#include "RemoteNotesTests.inc"

void patchSemantics()
{
    const auto source = json (R"({"a/b":{"~name":[1,2]},"x":1})");
    const auto result = remote_json::apply (source, json (R"([
        {"op":"test","path":"/x","value":1.0},
        {"op":"add","path":"/a~1b/~0name/1","value":3},
        {"op":"replace","path":"/x","value":4},
        {"op":"remove","path":"/a~1b/~0name/0"},
        {"op":"add","path":"/a~1b/~0name/-","value":5}
    ])"));
    check (remote_json::equal (result, json (R"({"a/b":{"~name":[3,2,5]},"x":4})")), "JSON Patch ordering/escaping differs");
    check (remote_json::equal (source, json (R"({"a/b":{"~name":[1,2]},"x":1})")), "Patching mutated its source");
    refuses ([&] { remote_json::apply (source, json (R"([{"op":"replace","path":"/x","value":3},{"op":"test","path":"/x","value":2}])")); });
    check (source["x"] == juce::var (1), "A failed later operation leaked an earlier change");
    refuses ([&] { remote_json::apply (source, json (R"([{"op":"test","path":"/x","value":1.00000001}])")); });
    refuses ([&] { remote_json::apply (source, json (R"([{"op":"add","path":"/a~1b/~0name/01","value":0}])")); });
    refuses ([&] { remote_json::apply (source, json (R"([{"op":"add","path":"/a~1b/~0name/4","value":0}])")); });
    refuses ([&] { remote_json::apply (source, json (R"([{"op":"replace","path":"/bad~2path","value":0}])")); });
    refuses ([&] { remote_json::apply (source, json (R"([{"op":"replace","path":"/missing","value":0}])")); });
    refuses ([&] { remote_json::apply (source, json (R"([{"op":"move","path":"/x","value":0}])")); });
    refuses ([&] { remote_json::apply (source, json (R"([{"op":"remove","path":"/x","value":0}])")); });
    refuses ([&] { remote_json::apply (source, json (R"([{"op":"replace","path":"/x","value":0,"extra":1}])")); });
}
void exactDocuments()
{
    const auto source = fixture();
    const auto ordinary = ProjectFile::toJson (source);
    const auto exact = ProjectFile::toJsonExact (source);
    check (exact != ordinary, "Exact snapshots still use rounded save numbers");
    check (ProjectFile::toJson (source) == ordinary, "Exact serialization changed later ordinary saves");
    juce::String error;
    const auto loaded = ProjectFile::fromJson (exact, error);
    check (loaded.has_value(), "Exact project did not load");
    check (loaded->patterns == source.patterns && loaded->automations == source.automations
           && loaded->channels == source.channels && loaded->mixer == source.mixer
           && loaded->playlist == source.playlist && loaded->swing == source.swing
           && loaded->chanceSeed == source.chanceSeed,
           "Exact snapshots lost native float bits or unrequested data");
    const auto staged = remote_document::stage (source, json (R"([
        {"op":"test","path":"/bpm","value":130.12345678901235},
        {"op":"replace","path":"/bpm","value":142},
        {"op":"replace","path":"/patterns/0/name","value":"Verse"}
    ])"));
    auto expected = source;
    expected.bpm = 142;
    expected.patterns[0].name = "Verse";
    check (staged.bpm == 142 && staged.patterns == expected.patterns && staged.channels == source.channels
           && staged.mixer == source.mixer && staged.automations == source.automations && staged.playlist == source.playlist
           && staged.swing == source.swing && staged.chanceSeed == source.chanceSeed
           && staged.projectId == source.projectId && staged.nextUid == source.nextUid,
           "Metadata transaction changed unrelated native state");
    check (source.bpm == 130.12345678901235 && source.patterns[0].name == "Lead", "Staging mutated the live input");
    const auto onlyName = remote_document::stage (source, json (R"([{"op":"replace","path":"/patterns/0/name","value":"Only name"}])"));
    check (onlyName.bpm == source.bpm && onlyName.playlist[0].stretch == source.playlist[0].stretch,
           "Metadata-only edit changed unrelated double precision");
    check (remote_document::hash (remote_document::bytes (source)).size() == 64, "Document hash malformed");
}
void transactionValidation()
{
    const auto source = fixture();
    refuses ([&] { remote_document::stage (source, json (R"([{"op":"replace","path":"/bpm","value":1200}])")); });
    refuses ([&] { remote_document::stage (source, json (R"([{"op":"replace","path":"/bpm","value":"fast"}])")); });
    refuses ([&] { remote_document::stage (source, json (R"([{"op":"add","path":"/unknown","value":1}])")); });
    refuses ([&] { remote_document::stage (source, json (R"([{"op":"replace","path":"/projectId","value":"different"}])")); });
    refuses ([&] { remote_document::stage (source, json (R"([{"op":"replace","path":"/patterns/0/notes/0/0","value":1.5}])")); });
    refuses ([&] { remote_document::stage (source, json (R"([{"op":"replace","path":"/patterns/0/notes/0/2","value":4}])")); });
    refuses ([&] { remote_document::stage (source, json (R"([{"op":"replace","path":"/patterns/0/uid","value":"ffffffffffffffff"}])")); });
    refuses ([&] { remote_document::stage (source, json (R"([{"op":"remove","path":"/patterns/0"}])")); });
    refuses ([&] { remote_document::stage (source, json (R"([{"op":"replace","path":"/bpm","value":140},{"op":"test","path":"/bpm","value":130}])")); });
    auto locked = source;
    locked.playlistTracks.resize (1);
    locked.playlistTracks[0].lockedToContent = true;
    refuses ([&] { remote_document::stage (locked, json (R"([{"op":"replace","path":"/playlist/0/2","value":960}])")); });
    const auto changed = remote_document::stage (locked, json (R"([
        {"op":"remove","path":"/playlistTrackLocks"},
        {"op":"replace","path":"/playlist/0/2","value":960}
    ])"));
    check ((changed.playlistTracks.empty() || ! changed.playlistTracks[0].lockedToContent)
           && changed.playlist[0].start == 960, "Explicit unlock/edit transaction failed");
    check (source.bpm == 130.12345678901235 && source.patterns[0].notes[0].channel == 0, "Rejected transactions changed their input");
}
void currentModelDocuments()
{
    auto source = fixture();
    source.playlist[0].fadeInTension = 0.123456791f;
    source.playlist[0].fadeOutTension = -0.234567896f;
    source.playlist[0].linearFades = 1;
    source.playlist[0].flFadeFlags = 0x12345678;
    source.playlist[0].fadeCurve = 2;
    source.automations[0].lfo.on = true;
    source.automations[0].lfo.range = 0.345678925f;
    source.automations[0].lfo.amp = { { 0, 0.456789017f, -0.123456791f }, { 960, 0.765432119f, 0.234567896f } };
    Modulator mod;
    mod.name = "Preserve LFO";
    mod.routes.push_back ({ { AutomationTargetKind::mixerPan, 1, 0, 0 }, 0.234567896f });
    source.modulators.push_back (mod);
    source.playlistTracks.resize (1);
    source.playlistTracks[0].lanes.push_back ({ 0, 72 });
    source.playlistTracks[0].lanesFolded = true;
    source.clipGrid.emplace();
    source.clipGrid->scenes.emplace_back();
    source.clipGrid->slots.emplace_back();
    auto gridClip = source.playlist[0];
    gridClip.uid = 0;
    gridClip.start = 0;
    gridClip.gridSlot = 0;
    source.clipGrid->clips.push_back (gridClip);
    VideoTrack videoTrack;
    videoTrack.name = "Picture";
    videoTrack.clips.push_back ({ "/videos/existing.mp4", 0.123456789012345, 0.234567890123456, 3.345678901234567, 10.0, 99 });
    source.videoTracks.push_back (videoTrack);
    uids::normalize (source);

    const auto changed = remote_document::stage (source, addValue ("/patterns/0/name", "Preserved"));
    check (changed.playlist == source.playlist && changed.automations == source.automations
           && changed.modulators == source.modulators && changed.playlistTracks == source.playlistTracks
           && changed.clipGrid == source.clipGrid && changed.videoTracks == source.videoTracks,
           "A metadata edit changed clip fades, LFOs, lanes, grid clips or video tracks");
    refusesWith ([&] {
        remote_document::stage (source, addValue ("/video/tracks/0/clips/0/path", "\\\\server\\share\\new.mp4"));
    }, "native media, page or resource locations");
    refusesWith ([&] {
        remote_document::stage (source, json (R"([{"op":"replace","path":"/clipGrid/clips/0/clip/0/16","value":"ffffffffffffffff"}])"));
    }, "existing identities");
    auto locked = source;
    locked.playlistTracks[0].lockedToContent = true;
    refusesWith ([&] {
        remote_document::stage (locked, json (R"([{"op":"replace","path":"/clipGrid/clips/0/clip/0/3","value":1920}])"));
    }, "Unlock the Playlist row");
    check (locked.clipGrid == source.clipGrid && source.videoTracks[0].clips[0].path == "/videos/existing.mp4",
           "A refused grid or video edit changed the input");

    auto stopMarker = fixture();
    launcher::setStopButton (stopMarker, 0, 0, false);
    const auto unchanged = remote_document::bytes (stopMarker);
    refuses ([&] { remote_document::stage (stopMarker, addValue ("/patterns/0/name", "No data loss")); });
    check (remote_document::bytes (stopMarker) == unchanged,
           "A grid stop marker lost by the native loader was committed or changed its input");
}
void documentIoPolicy()
{
    const auto source = fixture();
    refusesWith ([&] { remote_document::stage (source, addValue ("/mixer/1/recordTo", "/tmp/remote-output.wav")); },
                 "recording output paths");
    refusesWith ([&] { remote_document::stage (source, addValue ("/dataFolder", "\\\\server\\share\\output")); },
                 "output/data folders");
    refusesWith ([&] { remote_document::stage (source, addValue ("/channels/0/sample", "\\\\server\\share\\sample.wav")); },
                 "introduce asset paths");
    auto nested = source;
    nested.channels[0].instrument = InstrumentKind::patcher;
    PatchNode node;
    node.generator = true;
    node.channel.name = "Nested";
    node.channel.instrument = InstrumentKind::sampler;
    nested.channels[0].patcher.nodes.push_back (node);
    refusesWith ([&] {
        remote_document::stage (nested, addValue ("/channels/0/patcher/nodes/0/channel/sample", "\\\\server\\share\\nested.wav"));
    }, "introduce asset paths");
    auto hosted = source;
    hosted.channels[0].instrument = InstrumentKind::plugin;
    hosted.channels[0].plugin.id = "existing-catalog-id";
    hosted.channels[0].plugin.name = "Existing";
    refusesWith ([&] { remote_document::stage (hosted, addValue ("/channels/0/plugin/id", "VST3-\\\\server\\share\\new.dll")); },
                 "introduce plugin loader IDs");
    ForeignPlugin foreign;
    foreign.format = ForeignPlugin::Format::vst2;
    foreign.name = "External";
    foreign.path = "\\\\server\\share\\external.dll";
    refusesWith ([&] {
        remote_document::stage (hosted, addValue ("/channels/0/plugin/foreign", juce::String (foreignToText (foreign))));
    }, "introduce foreign plugin locations");
    auto assets = source;
    assets.channels[0].samplePath = "/samples/existing.wav";
    const auto document = json (ProjectFile::toJsonExact (assets).toRawUTF8());
    auto copy = document["channels"][0].clone();
    copy.getDynamicObject()->removeProperty ("uid");
    const auto cloned = remote_document::stage (assets, addValue ("/channels/-", copy));
    check (cloned.channels.size() == 2 && cloned.channels[1].samplePath == assets.channels[0].samplePath
           && cloned.channels[1].uid != assets.channels[0].uid, "Existing assets could not be reused by a fresh entity");
    copy.getDynamicObject()->setProperty ("sample", "\\\\server\\share\\new.wav");
    refusesWith ([&] { remote_document::stage (assets, addValue ("/channels/-", copy)); }, "introduce asset paths");
    check (source.mixer[1].recordTo.empty() && source.channels[0].samplePath.empty(), "I/O rejection changed its source");
}
void knownNativeStatePolicy()
{
    auto notebook = fixture();
    EffectSlot html;
    html.kind = EffectKind::htmlNotebook;
    html.state = flextra::htmlnotebook::stateFor ("/pages/existing.html");
    notebook.mixer[1].effects.push_back (html);
    for (const auto* address : { "\\\\server\\share\\page.html", "/tmp/program.exe", "https://example.invalid/page" })
        refusesWith ([&] {
            remote_document::stage (notebook, addValue ("/mixer/1/effects/0/state", encoded (flextra::htmlnotebook::stateFor (address))));
        }, "native media, page or resource locations");
    const auto renamed = remote_document::stage (notebook, json (R"([{"op":"replace","path":"/patterns/0/name","value":"Same page"}])"));
    check (renamed.mixer[1].effects[0].state == html.state, "Metadata edit did not preserve an existing notebook page");

    auto player = fixture();
    player.channels[0].instrument = InstrumentKind::videoplayer;
    video::Settings videoSettings;
    videoSettings.path = "/videos/existing.mp4";
    player.channels[0].pluginState = video::write (videoSettings);
    auto incoming = videoSettings;
    incoming.path = "\\\\server\\share\\video.mp4";
    refusesWith ([&] {
        remote_document::stage (player, addValue ("/channels/0/pluginState", encoded (video::write (incoming))));
    }, "native media, page or resource locations");
    auto volumeOnly = videoSettings;
    volumeOnly.volume = 64;
    const auto adjustedVideo = remote_document::stage (player, addValue ("/channels/0/pluginState", encoded (video::write (volumeOnly))));
    check (video::readOrDefault (adjustedVideo.channels[0].pluginState).volume == 64
           && video::readOrDefault (adjustedVideo.channels[0].pluginState).path == videoSettings.path,
           "Allowlisted native state edit changed or refused an existing media resource");
    const auto playerDocument = json (ProjectFile::toJsonExact (player).toRawUTF8());
    auto newPlayer = playerDocument["channels"][0].clone();
    newPlayer.getDynamicObject()->removeProperty ("uid");
    newPlayer.getDynamicObject()->setProperty ("pluginState", encoded (video::write (incoming)));
    refusesWith ([&] { remote_document::stage (player, addValue ("/channels/-", newPlayer)); }, "native media, page or resource locations");

    auto visualizer = fixture();
    EffectSlot visual;
    visual.kind = EffectKind::visualizer;
    auto scene = visualfx::scene::defaultScene();
    visualfx::scene::Layer image;
    image.type = visualfx::scene::LayerType::image;
    image.path = "/images/existing.png";
    scene.layers = { image };
    visual.state = visualfx::scene::toState (scene);
    visualizer.mixer[1].effects.push_back (visual);
    scene.layers[0].path = "\\\\server\\share\\image.png";
    refusesWith ([&] {
        remote_document::stage (visualizer, addValue ("/mixer/1/effects/0/state", encoded (visualfx::scene::toState (scene))));
    }, "native media, page or resource locations");

    auto script = vfx::defaultScript();
    script.setSource ("while True:\n    pass\n");
    vfx::State state;
    state.kind = vfx::Kind::script;
    state.fl = vfx::writeScript (script);
    const auto payload = vfx::writeState (state);
    auto noteProcessor = fixture();
    vfx::makeChannel (noteProcessor.channels[0], vfx::Kind::keyMapper);
    uids::normalize (noteProcessor);
    refusesWith ([&] {
        remote_document::stage (noteProcessor, addValue ("/channels/0/pluginState", encoded (payload)));
    }, "VFX Script payloads");
    auto processorCopy = json (ProjectFile::toJsonExact (noteProcessor).toRawUTF8())["channels"][0].clone();
    processorCopy.getDynamicObject()->removeProperty ("uid");
    processorCopy.getDynamicObject()->setProperty ("pluginState", encoded (payload));
    refusesWith ([&] { remote_document::stage (noteProcessor, addValue ("/channels/-", processorCopy)); }, "VFX Script payloads");
    auto nested = fixture();
    nested.channels[0].instrument = InstrumentKind::patcher;
    PatchNode node;
    node.generator = true;
    vfx::makeChannel (node.channel, vfx::Kind::keyMapper);
    nested.channels[0].patcher.nodes.push_back (node);
    refusesWith ([&] {
        remote_document::stage (nested, addValue ("/channels/0/patcher/nodes/0/channel/pluginState", encoded (payload)));
    }, "VFX Script payloads");
}
void hostedStateClearIsRejectedBeforeCommit()
{
    auto source = fixture();
    const auto saved = juce::MemoryBlock ("saved state", 11).toBase64Encoding().toStdString();
    source.channels[0].instrument = InstrumentKind::plugin;
    source.channels[0].plugin.id = "Known hosted instrument";
    source.channels[0].plugin.state = saved;
    EffectSlot hosted;
    hosted.kind = EffectKind::plugin;
    hosted.plugin.id = "Known hosted effect";
    hosted.plugin.state = saved;
    source.mixer[1].effects.push_back (hosted);
    EffectSlot parallel;
    parallel.kind = EffectKind::parallel;
    parallel.branches.resize (1);
    parallel.branches[0].chain.push_back (hosted);
    source.mixer[1].effects.push_back (parallel);
    ChannelDef patcher;
    patcher.instrument = InstrumentKind::patcher;
    PatchNode node;
    node.generator = false;
    node.effect = hosted;
    node.busHeads.push_back ({ 1, hosted, 1.0f });
    patcher.patcher.nodes.push_back (node);
    source.channels.push_back (std::move (patcher));
    uids::normalize (source);

    const auto replacement = [&] (const Song& changed, const char* field) {
        juce::var document;
        check (juce::JSON::parse (ProjectFile::toJsonExact (changed), document).wasOk(), "Cannot build clear-state fixture");
        auto* operation = new juce::DynamicObject();
        operation->setProperty ("op", "replace");
        operation->setProperty ("path", juce::String ("/") + field);
        operation->setProperty ("value", document[field]);
        return juce::var (juce::Array<juce::var> { juce::var (operation) });
    };
    auto changed = source;
    changed.channels[0].plugin.state.clear();
    refusesWith ([&] { remote_document::stage (source, replacement (changed, "channels")); }, "Hosted plugin state is read-only");
    refusesWith ([&] { remote_document::stage (source, json (R"([{"op":"remove","path":"/channels/0/plugin/state"}])")); },
                 "Hosted plugin state is read-only");
    changed = source;
    changed.mixer[1].effects[0].plugin.state.clear();
    refusesWith ([&] { remote_document::stage (source, replacement (changed, "mixer")); }, "Hosted plugin state is read-only");
    changed = source;
    changed.mixer[1].effects[1].branches[0].chain[0].plugin.state.clear();
    refusesWith ([&] { remote_document::stage (source, replacement (changed, "mixer")); }, "Hosted plugin state is read-only");
    changed = source;
    changed.channels[1].patcher.nodes[0].effect.plugin.state.clear();
    refusesWith ([&] { remote_document::stage (source, replacement (changed, "channels")); }, "Hosted plugin state is read-only");
    changed = source;
    changed.channels[1].patcher.nodes[0].busHeads[0].head->plugin.state.clear();
    refusesWith ([&] { remote_document::stage (source, replacement (changed, "channels")); }, "Hosted plugin state is read-only");
    check (source.channels[0].plugin.state == saved && source.mixer[1].effects[0].plugin.state == saved,
           "Clear-state rejection changed its input");

    // Metadata and typed wrapper routing remain editable without passing a vendor blob to a device.
    const auto renamed = remote_document::stage (source, json (R"([{"op":"replace","path":"/channels/0/name","value":"Renamed"}])"));
    check (renamed.channels[0].name == "Renamed" && renamed.channels[0].plugin.state == saved, "Metadata edit lost hosted state");
    changed = source;
    const auto other = juce::MemoryBlock ("new saved state", 15).toBase64Encoding().toStdString();
    changed.channels[0].plugin.state = other;
    refusesWith ([&] { remote_document::stage (source, replacement (changed, "channels")); }, "Hosted plugin state is read-only");
    for (const auto* path : { "/channels/0/plugin/state", "/mixer/1/effects/0/plugin/state",
                             "/mixer/1/effects/1/branches/0/chain/0/plugin/state",
                             "/channels/1/patcher/nodes/0/effect/plugin/state",
                             "/channels/1/patcher/nodes/0/busHeads/0/head/plugin/state" })
        refusesWith ([&] { remote_document::stage (source, addValue (path, juce::String (other))); }, "Hosted plugin state is read-only");

    auto document = json (ProjectFile::toJsonExact (source).toRawUTF8());
    auto copy = document["channels"][0].clone();
    copy.getDynamicObject()->removeProperty ("uid");
    refusesWith ([&] { remote_document::stage (source, addValue ("/channels/-", copy)); }, "Hosted plugin state is read-only");
    copy = document["mixer"][1]["effects"][0].clone();
    refusesWith ([&] { remote_document::stage (source, addValue ("/mixer/2/effects", juce::Array<juce::var> { copy })); },
                 "Hosted plugin state is read-only");
    changed = source;
    changed.channels[0].plugin.state = other;
    refusesWith ([&] { remote_document::stage (source, addValue ("", json (ProjectFile::toJsonExact (changed).toRawUTF8()))); },
                 "Hosted plugin state is read-only");
    auto substitute = source;
    substitute.mixer[1].effects[0].kind = EffectKind::utility;
    substitute.mixer[1].effects[0].substituteFor = hosted.plugin;
    refusesWith ([&] { remote_document::stage (substitute, addValue ("/mixer/1/effects/0/substituteFor/state", juce::String (other))); },
                 "Hosted plugin state is read-only");
    auto empty = source;
    empty.channels[0].plugin.state.clear();
    refusesWith ([&] { remote_document::stage (empty, addValue ("/channels/0/plugin/state", juce::String (saved))); }, "Hosted plugin state is read-only");

    const auto routed = remote_document::stage (source, addValue ("/channels/0/plugin/outputs", json ("[[1,1]]")));
    check (routed.channels[0].plugin.outputs.size() == 1 && routed.channels[0].plugin.outputs[0].offset == 1
           && routed.channels[0].plugin.state == saved, "Allowlisted wrapper output routing changed or refused hosted state");
    // Stable channel identities permit reordering without treating their saved states as new data.
    auto reordered = source;
    std::swap (reordered.channels[0], reordered.channels[1]);
    const auto moved = remote_document::stage (source, replacement (reordered, "channels"));
    check (moved.channels[1].plugin.state == saved, "Reordering stable channel identities lost hosted state");
    auto sameId = source;
    auto second = sameId.channels[0];
    second.uid = 0;
    second.plugin.state.clear();
    sameId.channels.push_back (second);
    uids::normalize (sameId);
    auto swapped = sameId;
    std::swap (swapped.channels[0], swapped.channels[2]);
    const auto sameIdMoved = remote_document::stage (sameId, replacement (swapped, "channels"));
    check (sameIdMoved.channels[0].plugin.state.empty() && sameIdMoved.channels[2].plugin.state == saved,
           "Reordering same-ID channels confused their stable device states");
    swapped = sameId;
    std::swap (swapped.channels[0].plugin.state, swapped.channels[2].plugin.state);
    refusesWith ([&] { remote_document::stage (sameId, replacement (swapped, "channels")); }, "Hosted plugin state is read-only");
    check (source.channels[0].plugin.state == saved && source.mixer[1].effects[0].plugin.state == saved,
           "Hosted-state rejection changed its input");
}

void documentFieldAllowlist()
{
    auto source = fixture();
    source.info.title = "Original";
    source.info.url = "https://existing.example/project";
    source.info.showOnOpen = true;
    source.info.created = 1234.0;
    source.info.workSeconds = 42.0;
    const auto original = remote_document::bytes (source);
    const auto named = remote_document::stage (source, json (R"([
        {"op":"replace","path":"/info/title","value":"New title"},
        {"op":"add","path":"/info/author","value":"Producer"},
        {"op":"add","path":"/info/genre","value":"Electronic"},
        {"op":"add","path":"/info/comments","value":"Session notes"},
        {"op":"add","path":"/channels/0/synthSettings/chorus","value":1},
        {"op":"replace","path":"/patterns/0/events/0/events/0/1","value":0.5},
        {"op":"replace","path":"/automations/0/points/0/1","value":0.5}
    ])"));
    check (named.info.title == "New title" && named.info.author == "Producer" && named.info.genre == "Electronic"
           && named.info.comments == "Session notes" && named.info.url == source.info.url
           && named.info.showOnOpen && named.info.created == source.info.created && named.info.workSeconds == source.info.workSeconds
           && named.channels[0].synth.chorus == 1 && named.patterns[0].events[0].events[0].value == 0.5f
           && named.automations[0].points[0].value == 0.5f, "Allowlisted musical or project metadata edit failed");
    for (const auto* path : { "/info/url", "/info/showOnOpen", "/info/created", "/info/workSeconds" })
        refusesWith ([&] { remote_document::stage (source, addValue (path, juce::String (path) == "/info/url" ? juce::var ("file:///tmp/program") : juce::var (false))); },
                     "read-only");
    refusesWith ([&] { remote_document::stage (source, json (R"([{"op":"remove","path":"/info"}])")); }, "read-only");
    auto changed = source;
    changed.info.url = "https://new.example/launch";
    refusesWith ([&] { remote_document::stage (source, addValue ("/info", json (ProjectFile::toJsonExact (changed).toRawUTF8())["info"])); },
                 "read-only");
    refusesWith ([&] { remote_document::stage (source, addValue ("", json (ProjectFile::toJsonExact (changed).toRawUTF8()))); }, "read-only");
    refusesWith ([&] { remote_document::stage (fixture(), addValue ("/info", json (R"({"title":"Injected","url":"file:///tmp/program","showOnOpen":true})"))); },
                 "read-only");

    for (const auto* path : { "/futureLoader", "/channels/0/futureLoader", "/channels/0/synthSettings/futureLoader",
                             "/patterns/0/events/0/futureLoader", "/automations/0/futureLoader", "/mixer/1/futureLoader" })
        refusesWith ([&] { remote_document::stage (source, addValue (path, "file:///tmp/program")); }, "editable document allowlist");
    auto document = json (ProjectFile::toJsonExact (source).toRawUTF8());
    auto channel = document["channels"][0].clone();
    channel.getDynamicObject()->removeProperty ("uid");
    channel["synthSettings"].getDynamicObject()->setProperty ("futureLoader", "/tmp/program");
    refusesWith ([&] { remote_document::stage (source, addValue ("/channels/-", channel)); }, "editable document allowlist");
    refusesWith ([&] { remote_document::stage (source, addValue ("/patterns/0/notes/0/0", json (R"({"futureLoader":"/tmp/program"})"))); },
                 "does not allow editable object members");
    const auto labeled = remote_document::stage (source, addValue ("/channels/0/keyLabels", json (R"({"60":"Middle C","127":"Top"})")));
    check (labeled.channels[0].keyLabels.at (60) == "Middle C", "MIDI key-label allowlist rejected a valid dictionary");
    refusesWith ([&] { remote_document::stage (source, addValue ("/channels/0/keyLabels", json (R"({"futureLoader":"/tmp/program"})"))); }, "MIDI keys");
    check (remote_document::bytes (source) == original, "Allowlist rejection changed its input");
}

void richDocumentAllowlist()
{
    auto source = fixture();
    VideoTrack existingVideo;
    existingVideo.name = "Existing picture";
    existingVideo.clips.push_back ({ "/videos/existing.mp4", 0.0, 0.0, 2.0, 2.0, 99 });
    source.videoTracks.push_back (existingVideo);
    auto rich = source;
    rich.channelGroups = { "Instruments" };
    rich.channels[0].group = 0;
    rich.channels[0].keyLabels[60] = "Middle C";
    rich.channels[0].arp.direction = 1;
    rich.channels[0].misc.gateBeats = 0.5f;
    rich.channels[0].sampler.pitch = 3.0f;
    rich.channels[0].sampler.warp = { { 0.0, 0.0 }, { 1.0, 2.0 } };
    rich.channels[0].sampler.precompute.normalize = true;
    ChannelDef layered;
    layered.instrument = InstrumentKind::layer;
    layered.layer.children = { 0 };
    rich.channels.push_back (layered);
    EffectSlot parallel;
    parallel.kind = EffectKind::parallel;
    parallel.branches.resize (1);
    parallel.branches[0].chain.emplace_back();
    rich.mixer[1].effects.push_back (parallel);
    ChannelDef patcher;
    patcher.instrument = InstrumentKind::patcher;
    PatchNode node;
    node.effect.kind = EffectKind::utility;
    node.label = "Bus processor";
    node.paramIns = { 0 };
    node.busHeads.push_back ({ 1, node.effect, 0.5f });
    patcher.patcher.nodes.push_back (node);
    rich.channels.push_back (patcher);
    Modulator modulator;
    modulator.name = "LFO";
    modulator.routes.push_back ({ { AutomationTargetKind::mixerPan, 1, 0, 0 }, 0.25f });
    rich.modulators.push_back (modulator);
    Vca vca;
    vca.name = "Band";
    vca.members = { 1, 2 };
    rich.vcas.push_back (vca);
    rich.clipGrid.emplace();
    rich.clipGrid->scenes.emplace_back();
    rich.clipGrid->scenes[0].name = "Scene";
    rich.clipGrid->slots.emplace_back();
    auto gridClip = source.playlist[0];
    gridClip.uid = 0;
    gridClip.start = 0;
    gridClip.gridSlot = 0;
    rich.clipGrid->clips.push_back (gridClip);
    VideoTrack video;
    video.name = "Picture";
    video.clips.push_back ({ "/videos/existing.mp4", 0.0, 0.0, 2.0, 2.0, 0 });
    rich.videoTracks.push_back (video);
    AdrCue cue;
    cue.id = "101";
    cue.out = 2.0;
    cue.line = "Dialogue";
    cue.takes.emplace_back();
    cue.takes[0].clip = source.playlist[0].uid;
    cue.takes[0].note = "First take";
    rich.adr.cues.push_back (cue);
    auto document = json (ProjectFile::toJsonExact (rich).toRawUTF8());
    const auto accepted = remote_document::stage (source, addValue ("", document));
    check (accepted.channels.size() == 3 && accepted.channels[0].keyLabels.at (60) == "Middle C"
           && accepted.channels[0].sampler.warp.size() == 2 && accepted.channels[1].layer.children == layered.layer.children
           && accepted.channels[2].patcher.nodes[0].busHeads.size() == 1
           && accepted.mixer[1].effects[0].branches[0].chain.size() == 1 && accepted.modulators.size() == 1
           && accepted.vcas.size() == 1 && accepted.clipGrid->slots.size() == 1 && accepted.videoTracks.size() == 2
           && accepted.adr.cues[0].takes[0].note == "First take", "Allowlist discarded a supported rich native object/tuple workflow");
    for (const auto* path : { "/channels/2/patcher/nodes/0/futureLoader",
                             "/channels/2/patcher/nodes/0/busHeads/0/head/futureLoader",
                             "/mixer/1/effects/0/branches/0/chain/0/futureLoader",
                             "/clipGrid/clips/0/futureLoader", "/video/tracks/0/clips/0/futureLoader",
                             "/adr/cues/0/takes/0/futureLoader" })
        refusesWith ([&] { remote_document::stage (accepted, addValue (path, "file:///tmp/program")); }, "editable document allowlist");
}

void discoveryCleanup()
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
        .getChildFile ("lynx-control-test-" + remote_files::randomToken());
    struct Cleanup
    {
        juce::File directory;
        ~Cleanup() { directory.deleteRecursively(); }
    } cleanup { directory };
    const auto file = directory.getChildFile ("connection.json");
    const juce::String record = R"({"token":"owned"})";
    remote_files::writeDiscovery (file, record);
    remote_files::removeDiscoveryIfOwned (file, "another process");
    check (file.existsAsFile(), "Cleanup removed another process's discovery record");
    remote_files::removeDiscoveryIfOwned (file, "owned");
    check (! file.exists(), "Cleanup kept its own discovery record");

    // The original crash payload must also be harmless during disable/shutdown.
    for (const auto size : { 16384, 100000 })
    {
        remote_files::writeDiscovery (file, juce::String (std::string ((std::size_t) size, '[')));
        remote_files::removeDiscoveryIfOwned (file, "owned");
        check (file.existsAsFile(), "Cleanup removed a malformed discovery record");
    }
    // JUCE's release parser accepts overlong UTF-8 as recursive array brackets.
    // The raw-byte gate must reject it before parsing even with a matching token.
    std::string invalidUtf8 = R"({"token":"owned","deep":)";
    for (int i = 0; i < 32; ++i) invalidUtf8 += "\xc1\x9b";
    invalidUtf8 += "0";
    for (int i = 0; i < 32; ++i) invalidUtf8 += "\xc1\x9d";
    invalidUtf8 += "}";
    // Keep malformed bytes outside JUCE strings: release-build conversion itself
    // can corrupt memory before JSON parsing, so the raw gate must run first.
    check (! remote_json::withinNestingLimit (invalidUtf8, 8), "Noncanonical UTF-8 passed the pre-conversion gate");
    {
        auto output = file.createOutputStream();
        check (output != nullptr && output->setPosition (0) && output->truncate().wasOk()
               && output->write (invalidUtf8.data(), invalidUtf8.size()), "Could not write malformed UTF-8 discovery fixture");
        output->flush();
    }
    remote_files::removeDiscoveryIfOwned (file, "owned");
    check (file.existsAsFile(), "Cleanup parsed a noncanonical UTF-8 discovery record");
    const juce::String unicodeRecord = juce::String::fromUTF8 ("{\"token\":\"owned\",\"name\":\""
        "\xe9\x9f\xb3\xe6\xa8\x82\xf0\x9f\x8e\xb5\"}");
    remote_files::writeDiscovery (file, unicodeRecord);
    remote_files::removeDiscoveryIfOwned (file, "owned");
    check (! file.exists(), "Cleanup rejected a canonical Unicode discovery record");
    // An oversized record with a valid prefix must be rejected rather than parsed
    // from a truncated buffer. A record at the byte limit remains valid.
    remote_files::writeDiscovery (file, record + juce::String::repeatedString (" ", 16385 - record.length()));
    remote_files::removeDiscoveryIfOwned (file, "owned");
    check (file.existsAsFile(), "Cleanup accepted an oversized discovery record");
    remote_files::writeDiscovery (file, record + juce::String::repeatedString (" ", 16384 - record.length()));
    remote_files::removeDiscoveryIfOwned (file, "owned");
    check (! file.exists(), "Cleanup rejected a discovery record at the byte limit");
}
}
int main()
{
    const char* activeTest = "initialization";
    const auto run = [&] (const char* name, auto test) { activeTest = name; test(); };
    try
    {
        run ("patchSemantics", patchSemantics);
        run ("exactDocuments", exactDocuments);
        run ("remoteNotesRoundtrip", remoteNotesRoundtrip);
        run ("transactionValidation", transactionValidation);
        run ("currentModelDocuments", currentModelDocuments);
        run ("documentIoPolicy", documentIoPolicy);
        run ("hostedStateClearIsRejectedBeforeCommit", hostedStateClearIsRejectedBeforeCommit);
        run ("documentFieldAllowlist", documentFieldAllowlist);
        run ("richDocumentAllowlist", richDocumentAllowlist);
        run ("knownNativeStatePolicy", knownNativeStatePolicy);
        run ("discoveryCleanup", discoveryCleanup);
        std::puts ("Remote JSON Patch, exact project documents, atomic staging and discovery cleanup tests passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf (stderr, "%s: %s\n", activeTest, error.what());
        return 1;
    }
}
