#include "engine/RemoteProjectControl.h"
#include "engine/RemoteJsonPatch.h"
#include "engine/RemoteFileSecurity.h"
#include "engine/ProjectFile.h"
#include "core/SessionIds.h"
#include "core/PluginSubstitutes.h"
#include "core/FlExtraFx.h"
#include "core/VideoPlayer.h"
#include "core/VisualFx.h"
#include "core/Vfx.h"
#include <cstdio>
#include <stdexcept>

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
    bool rejected = false;
    try { operation(); }
    catch (const std::runtime_error& error) { rejected = std::string (error.what()).find (reason) != std::string::npos; }
    check (rejected, "Expected I/O policy rejection did not occur");
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
    song.swing = 0.123456791f;
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
    pattern.notes.push_back (note);
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
           && loaded->playlist == source.playlist && loaded->swing == source.swing,
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
           && staged.swing == source.swing && staged.projectId == source.projectId && staged.nextUid == source.nextUid,
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
    refusesWith ([&] { remote_document::stage (source, replacement (changed, "channels")); }, "clear hosted device state");
    refusesWith ([&] { remote_document::stage (source, json (R"([{"op":"remove","path":"/channels/0/plugin/state"}])")); },
                 "clear hosted device state");
    changed = source;
    changed.mixer[1].effects[0].plugin.state.clear();
    refusesWith ([&] { remote_document::stage (source, replacement (changed, "mixer")); }, "clear hosted device state");
    changed = source;
    changed.mixer[1].effects[1].branches[0].chain[0].plugin.state.clear();
    refusesWith ([&] { remote_document::stage (source, replacement (changed, "mixer")); }, "clear hosted device state");
    changed = source;
    changed.channels[1].patcher.nodes[0].effect.plugin.state.clear();
    refusesWith ([&] { remote_document::stage (source, replacement (changed, "channels")); }, "clear hosted device state");
    changed = source;
    changed.channels[1].patcher.nodes[0].busHeads[0].head->plugin.state.clear();
    refusesWith ([&] { remote_document::stage (source, replacement (changed, "channels")); }, "clear hosted device state");
    check (source.channels[0].plugin.state == saved && source.mixer[1].effects[0].plugin.state == saved,
           "Clear-state rejection changed its input");

    // Ordinary metadata edits and deliberate nonempty state replacement remain supported.
    const auto renamed = remote_document::stage (source, json (R"([{"op":"replace","path":"/channels/0/name","value":"Renamed"}])"));
    check (renamed.channels[0].name == "Renamed" && renamed.channels[0].plugin.state == saved, "Metadata edit lost hosted state");
    changed = source;
    const auto other = juce::MemoryBlock ("new saved state", 15).toBase64Encoding().toStdString();
    changed.channels[0].plugin.state = other;
    const auto stateEdit = remote_document::stage (source, replacement (changed, "channels"));
    check (stateEdit.channels[0].plugin.state == other, "Nonempty hosted state edit was refused or discarded");
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
    try
    {
        patchSemantics();
        exactDocuments();
        transactionValidation();
        documentIoPolicy();
        hostedStateClearIsRejectedBeforeCommit();
        knownNativeStatePolicy();
        discoveryCleanup();
        std::puts ("Remote JSON Patch, exact project documents, atomic staging and discovery cleanup tests passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf (stderr, "%s\n", error.what());
        return 1;
    }
}
