#include "engine/RemoteProjectControl.h"
#include "engine/ProjectFile.h"
#include "engine/RemoteJsonPatch.h"
#include "engine/RemoteError.h"
#include "core/SessionIds.h"
#include "core/Sha256.h"
#include "core/BassDrum.h"
#include "core/FlExtraFx.h"
#include "core/Gms.h"
#include "core/Morphine.h"
#include "core/Scratcher.h"
#include "core/SoundFont.h"
#include "core/VideoPlayer.h"
#include "core/VisualFx.h"
#include "core/Vfx.h"
#include "core/WaveTraveller.h"
#include <set>
#include <tuple>
#include <map>

namespace lynx
{
namespace
{
enum class DocumentShape
{
    data, readOnly, root, info, channel, plugin, origin, arp, misc, layer, synth, sampler, precomputed,
    keyLabels, mixer, effect, branch, link, patcher, node, busHead, standIn, pattern, events, automation,
    modulator, vca, video, videoTrack, videoClip, adr, adrSettings, cue, take, grid, scene, gridClip, arrangement
};
struct DocumentField
{
    DocumentShape shape = DocumentShape::data;
    bool array = false;
};
using DocumentFields = std::map<juce::String, DocumentField>;
const DocumentFields& editableDocumentFields (DocumentShape shape)
{
    // This is an API capability list, independent of the native file loader. Every changed
    // object must contain only reviewed fields, even if an unreviewed member is unchanged;
    // an entirely unchanged subtree is preserved. Parent/array replacement follows the same
    // rule. New save-format fields therefore require review before their containing object
    // is edited. Positional musical data is still validated by
    // the exact native round trip and the reference/bounds checks below.
    static const auto schemas = [] {
        using S = DocumentShape;
        std::map<S, DocumentFields> result;
        const auto add = [&] (S owner, const char* names, S child = S::data, bool array = false) {
            for (const auto& name : juce::StringArray::fromTokens (names, " ", ""))
                result[owner].emplace (name, DocumentField { child, array });
        };
        add (S::root, "bpm beatsPerBar swing chanceSeed channelGroups masterPitch substitutesBypassed cleanMixer performanceMode "
             "autoCrossfades compCrossfadeMs playTruncatedNotes fastDeclick panLaw autoPdc compensateAutomations timeSettings "
             "scaleHighlight dataFolder currentPattern playlist patternBlocks blocks playlistTracks playlistTrackHeights "
             "playlistTrackColours playlistTrackGroups playlistCompTracks playlistTrackLocks playlistTrackLanes "
             "playlistTrackModes playlistTrackPerformance playlistTrackIcons currentArrangement mode loop arrangementUids midiLinks initialValues");
        add (S::root, "format version savedBy projectId nextUid session clipFadesPerClip", S::readOnly);
        add (S::root, "info", S::info);
        add (S::root, "channels", S::channel, true);
        add (S::root, "patterns", S::pattern, true);
        add (S::root, "automations", S::automation, true);
        add (S::root, "modulators", S::modulator, true);
        add (S::root, "vcas", S::vca, true);
        add (S::root, "mixer", S::mixer, true);
        add (S::root, "video", S::video);
        add (S::root, "adr", S::adr);
        add (S::root, "clipGrid", S::grid);
        add (S::root, "arrangements", S::arrangement, true);
        add (S::info, "title author genre comments");
        add (S::info, "url showOnOpen created workSeconds", S::readOnly);
        add (S::channel, "name instrument volume pan muted ghostHidden soloLock sample controllerState pluginState modelParams zones "
             "mixer maxPolyphony colour group swingMix arp cut keyRegion receiveFrom icon flWrapperFlags loopStarter uid");
        add (S::channel, "plugin", S::plugin);
        add (S::channel, "flOrigin", S::origin);
        add (S::channel, "keyLabels", S::keyLabels);
        add (S::channel, "arpFl", S::arp);
        add (S::channel, "misc", S::misc);
        add (S::channel, "layer", S::layer);
        add (S::channel, "synthSettings", S::synth);
        add (S::channel, "samplerSettings", S::sampler);
        add (S::channel, "patcher", S::patcher);
        add (S::channel, "standIns", S::standIn, true);
        add (S::plugin, "id name state foreign fixedBuffers separateProcess threaded maxSizeBuffers allowSmartDisable hostMaxBuffer "
             "midiInPort midiOutPort notifyRender latencyMs releaseVelocity sendBendRange bendRange modXAftertouch "
             "allNotesOffOnStop resetOnFlush sendLoopPosition allowHostKeys hostKeysFirst bitSize detached outputs");
        add (S::origin, "plugin state print");
        add (S::arp, "chord chordSustain slide octaveRepeat");
        add (S::misc, "shift gate fullPorta mono porta portaTime echo velocityTracking keyTracking pitchRange mainPitch rootNote addToKey fineTune");
        add (S::layer, "children random crossfade sequential fade");
        add (S::synth, "osc spectrum spectrumPhase spectrumDecay spectrumTarget adsr filter ladder chorus stereoTremolo vibratoCents "
             "mono porta portaTime am ring keyTrack bands sweep sweepTime fm filterEnv pitchEnv lfo");
        add (S::sampler, "pitch attack decay sustain release stretchBeats keepPitch stretchMethod flStretchMode flStretchPitch stretchPitch "
             "warp region startOffset slices reverse cut porta portaTime filter gainDb useLoopPoints flVelocityLaw declickMode "
             "flVolumeEnvelope flChannel filterEnv pitchEnv lfo");
        add (S::sampler, "precomputed", S::precomputed);
        add (S::precomputed, "removeDc normalize reversePolarity fadeStereo swapStereo pingPong fadeIn fadeOut crossfade trim "
             "boost eq ringMix ringFreq stereoDelay cutoff resonance reverb reverbType pogo clip");
        add (S::mixer, "name volume pan muted solo soloSources threaded separator folded dock toMaster masterLevel colour stereo "
             "invertPhase swap input inputMono monitor recordSafe recordTo recordSource recordDryToo latencyMs latencyBypass "
             "compensateMaster icon output outputMode eq eqBypassed sends lostSends");
        add (S::mixer, "effects", S::effect, true);
        add (S::mixer, "standIns", S::standIn, true);
        add (S::effect, "kind on params sendSpan mix smartDisable row sidechain sidechain2 sendTransient sendSustain state sample preset abOther abB");
        add (S::effect, "plugin substituteFor", S::plugin);
        add (S::effect, "flOrigin", S::origin);
        add (S::effect, "patcher", S::patcher);
        add (S::effect, "branches", S::branch, true);
        add (S::effect, "links", S::link, true);
        add (S::branch, "gain");
        add (S::branch, "chain", S::effect, true);
        add (S::link, "branch source target param curve");
        add (S::patcher, "wires io published outputTracks fromMidiPorts toMidiPorts fromNotesHidden surface surfaceAt");
        add (S::patcher, "nodes", S::node, true);
        add (S::node, "label x y paramIns paramOuts");
        add (S::node, "channel", S::channel);
        add (S::node, "effect", S::effect);
        add (S::node, "busHeads", S::busHead, true);
        add (S::busHead, "bus gain");
        add (S::busHead, "head", S::effect);
        add (S::standIn, "pattern loop path hash length plugin by");
        add (S::pattern, "name uid length colour loopOn loopStarterGenre loopMode channelLoops markers notes");
        add (S::pattern, "events", S::events, true);
        add (S::events, "target events");
        add (S::automation, "name uid target points source curve lfo lfoFl");
        add (S::modulator, "name shape sync beats hz phase unipolar seed routes");
        add (S::vca, "name colour level muted members parent");
        add (S::video, "rate startTimecode rateFromVideo timecodeRuler burnIn");
        add (S::video, "tracks", S::videoTrack, true);
        add (S::videoTrack, "name muted height colour");
        add (S::videoTrack, "clips", S::videoClip, true);
        add (S::videoClip, "path start offset length duration uid");
        add (S::adr, "current");
        add (S::adr, "settings", S::adrSettings);
        add (S::adr, "cues", S::cue, true);
        add (S::adrSettings, "preRoll postRoll beeps beepFrequency streamer streamerLength streamerColour punch cycle guideInsert headphoneOutput recordInsert takeTrack");
        add (S::cue, "id in out character line notes status selected comp compClips");
        add (S::cue, "takes", S::take, true);
        add (S::take, "number name clip rating circled note aligned");
        add (S::grid, "quant backOnStop followActions");
        add (S::grid, "scenes", S::scene, true);
        add (S::grid, "clips", S::gridClip, true);
        add (S::scene, "name colour tempo beatsPerBar");
        add (S::gridClip, "scene name colour mode quant legato loop noStop follow clip");
        add (S::arrangement, "name clips blocks loop markers");
        return result;
    }();
    static const DocumentFields none;
    const auto found = schemas.find (shape);
    return found == schemas.end() ? none : found->second;
}

void editableDocument (const juce::var& before, const juce::var& after, DocumentField field = { DocumentShape::root })
{
    using S = DocumentShape;
    if (remote_json::equal (before, after)) return;
    if (field.shape == S::readOnly)
        remote_json::invalid ("Project field is read-only through document edits");
    if (field.array)
    {
        if (after.isVoid()) return;
        const auto* list = after.getArray();
        if (list == nullptr) remote_json::invalid ("Editable project collection must remain an array");
        const auto* old = before.getArray();
        for (int i = 0; i < list->size(); ++i)
            editableDocument (old != nullptr && i < old->size() ? (*old)[i] : juce::var(), (*list)[i], { field.shape });
        return;
    }
    if (field.shape == S::data)
    {
        if (after.getDynamicObject() != nullptr)
            remote_json::invalid ("Project field does not allow editable object members");
        if (const auto* list = after.getArray())
            for (const auto& item : *list) editableDocument ({}, item, { S::data });
        return;
    }
    if (! after.isVoid() && after.getDynamicObject() == nullptr)
        remote_json::invalid ("Editable project object must remain an object");
    const auto* old = before.getDynamicObject();
    const auto* next = after.getDynamicObject();
    if (field.shape == S::keyLabels)
    {
        if (next != nullptr)
            for (const auto& property : next->getProperties())
            {
                const auto key = property.name.toString();
                if (key != juce::String (key.getIntValue()) || key.getIntValue() < 0 || key.getIntValue() > 127 || ! property.value.isString())
                    remote_json::invalid ("Editable key labels must name MIDI keys 0..127 and contain text");
            }
        return;
    }
    const auto& fields = editableDocumentFields (field.shape);
    const auto check = [&] (const juce::Identifier& key) {
        const auto found = fields.find (key.toString());
        if (found == fields.end()) remote_json::invalid ("Project field is not on the editable document allowlist");
        editableDocument (old != nullptr ? old->getProperty (key) : juce::var(),
                          next != nullptr ? next->getProperty (key) : juce::var(), found->second);
    };
    if (next != nullptr) for (const auto& property : next->getProperties()) check (property.name);
    if (old != nullptr) for (const auto& property : old->getProperties())
        if (next == nullptr || ! next->hasProperty (property.name)) check (property.name);
}

void hostedStatePolicy (const juce::var& before, const juce::var& after)
{
    using Device = std::tuple<juce::String, juce::String, juce::String>;
    std::map<juce::String, Device> known;
    const auto visit = [] (const juce::var& root, const auto& device) {
        std::function<void (const juce::var&, const juce::String&)> walk;
        walk = [&] (const juce::var& value, const juce::String& path) {
            if (const auto* object = value.getDynamicObject())
            {
                for (const auto& property : object->getProperties())
                {
                    const auto key = property.name.toString();
                    const auto childPath = path + "/" + key;
                    if ((key == "plugin" || key == "substituteFor") && property.value.getDynamicObject() != nullptr)
                        device (childPath, Device { property.value["id"].toString(), property.value["foreign"].toString(), property.value["state"].toString() });
                    else walk (property.value, childPath);
                }
            }
            else if (const auto* array = value.getArray())
                for (int i = 0; i < array->size(); ++i)
                {
                    const auto& item = (*array)[i];
                    // Channel UIDs keep a device stable when top-level channels are reordered.
                    // Unidentified nested devices/effects remain bound to their exact container slot.
                    const auto key = item["uid"].isString() ? "uid:" + item["uid"].toString() : juce::String (i);
                    walk (item, path + "/" + key);
                }
        };
        walk (root, {});
    };
    visit (before, [&] (const auto& path, const Device& device) { known.emplace (path, device); });
    visit (after, [&] (const auto& path, const Device& device) {
        const auto old = known.find (path);
        if (old == known.end() || std::get<0> (old->second) != std::get<0> (device) || std::get<1> (old->second) != std::get<1> (device))
        {
            if (std::get<2> (device).isNotEmpty())
                remote_json::invalid ("Hosted plugin state is read-only; use a catalog replacement operation to create or reset a device");
        }
        else if (std::get<2> (old->second) != std::get<2> (device))
            remote_json::invalid ("Hosted plugin state is read-only; use native parameter operations or the plugin editor");
    });
}

void references (Song& song)
{
    const auto in = [] (int index, std::size_t count, bool none = true) {
        return (none && index == -1) || (index >= 0 && (std::size_t) index < count);
    };
    if (song.channels.size() > 100000 || song.patterns.size() > 100000 || song.automations.size() > 100000
        || song.mixer.size() > (std::size_t) maxMixerTracks || song.playlistTracks.size() > 500)
        remote_json::invalid ("Project collection exceeds the remote API limit");
    refs::forEachChannelRef (song, [&] (int& index) {
        if (! in (index, song.channels.size())) remote_json::invalid ("Project contains a dangling channel reference");
    });
    refs::forEachPatternRef (song, [&] (int& index) {
        if (! in (index, song.patterns.size(), false)) remote_json::invalid ("Project contains a dangling pattern reference");
    });
    refs::forEachAutomationRef (song, [&] (int& index) {
        if (! in (index, song.automations.size(), false)) remote_json::invalid ("Project contains a dangling automation reference");
    });
    refs::forEachInsertRef (song, [&] (int& index) {
        if (! in (index, song.mixer.size())) remote_json::invalid ("Project contains a dangling mixer reference");
    });
    refs::forEachRowRef (song, [&] (int& index) {
        if (index < 0 || index >= 500) remote_json::invalid ("Playlist rows must be in 0..499");
    });
    for (const auto& pattern : song.patterns)
        for (const auto& note : pattern.notes)
            if (! in (note.channel, song.channels.size(), false) || note.start < 0 || note.length < 1
                || note.start > 1000000000 || note.length > 1000000000 - note.start)
                remote_json::invalid ("Project contains an invalid note or note end");
    song.forEachClipList ([&] (std::vector<Clip>& clips) {
        for (const auto& clip : clips)
            if (clip.start < 0 || clip.start > 1000000000 || clip.length < 1 || clip.length > 1000000000 - clip.start || clip.offset < 0)
                remote_json::invalid ("Project contains invalid Playlist clip bounds");
    });
    song.forEachTarget ([&] (AutomationTarget& target) {
        if (target.kind == AutomationTargetKind::effectParam || target.kind == AutomationTargetKind::effectMix)
            if (! in (target.index, song.mixer.size(), false)
                || ! in (target.slot, song.mixer[(std::size_t) target.index].effects.size(), false))
                remote_json::invalid ("Project contains a dangling effect target");
    });
    // The native mixer ignores cyclic sends. Refuse them instead of accepting a document
    // whose audible result differs from its declared routing.
    std::vector<int> visiting (song.mixer.size());
    std::function<void (int)> visit = [&] (int track) {
        if (visiting[(std::size_t) track] == 1) remote_json::invalid ("Project mixer routing contains a cycle");
        if (visiting[(std::size_t) track] == 2) return;
        visiting[(std::size_t) track] = 1;
        for (const auto& send : song.mixer[(std::size_t) track].sends) visit (send.target);
        visiting[(std::size_t) track] = 2;
    };
    for (int i = 0; i < (int) song.mixer.size(); ++i) visit (i);
}
template <class Item> void identities (const std::vector<Item>& before, const std::vector<Item>& after)
{
    std::set<std::uint64_t> known, seen;
    for (const auto& item : before) if (item.uid != 0) known.insert (item.uid);
    for (const auto& item : after)
        if (item.uid != 0 && (! known.contains (item.uid) || ! seen.insert (item.uid).second))
            remote_json::invalid ("New entities must omit uid; existing identities must be unique and retained");
}
void identityAndLocks (const Song& before, Song& after)
{
    identities (before.channels, after.channels);
    identities (before.patterns, after.patterns);
    identities (before.automations, after.automations);
    identities (before.arrangements, after.arrangements);
    std::vector<Clip> oldClips, newClips;
    auto collect = [] (const Song& song, std::vector<Clip>& clips) {
        song.forEachClipList ([&] (const std::vector<Clip>& list) {
            clips.insert (clips.end(), list.begin(), list.end());
        });
    };
    collect (before, oldClips);
    collect (after, newClips);
    identities (oldClips, newClips);
    for (int row = 0; row < (int) before.playlistTracks.size(); ++row)
        if (before.playlistTracks[(std::size_t) row].lockedToContent
            && row < (int) after.playlistTracks.size() && after.playlistTracks[(std::size_t) row].lockedToContent)
        {
            const auto onRow = [row] (const std::vector<Clip>& clips) {
                std::vector<Clip> result;
                for (const auto& clip : clips) if (clip.track == row) result.push_back (clip);
                return result;
            };
            if (onRow (oldClips) != onRow (newClips)) remote_json::invalid ("Unlock the Playlist row explicitly before changing its clips");
        }
}

using ForeignLocator = std::tuple<ForeignPlugin::Format, std::uint32_t, std::array<std::uint8_t, 16>, bool,
                                  std::string, std::string, std::string>;
struct IoReferences
{
    std::set<std::string> assets, plugins;
    std::set<ForeignLocator> foreign;
    std::set<std::pair<std::string, std::string>> nativeResources;
    std::set<std::string> scripts;
};
IoReferences ioReferences (const Song& song)
{
    IoReferences result;
    const auto path = [&] (const std::string& value) { if (! value.empty()) result.assets.insert (value); };
    const auto nativeResource = [&] (const char* role, const std::string& value) {
        if (! value.empty()) result.nativeResources.insert ({ role, value });
    };
    const auto plugin = [&] (const PluginRef& value) {
        if (! value.id.empty()) result.plugins.insert (value.id);
        const auto& f = value.foreign;
        if (f.format != ForeignPlugin::Format::none)
            result.foreign.insert ({ f.format, f.vst2Id, f.classId, f.hasClassId, f.clapId, f.name, f.path });
    };
    std::function<void (const PatcherDoc&)> patcher;
    std::function<void (const ChannelDef&)> channel;
    std::function<void (const EffectSlot&)> effect;
    channel = [&] (const ChannelDef& value) {
        // samplePath also names engine data folders / imported engine DLLs for some native generators.
        path (value.samplePath);
        for (const auto& zone : value.zones) path (zone.path);
        for (const auto& standIn : value.standIns) path (standIn.path);
        if (value.instrument == InstrumentKind::plugin) plugin (value.plugin);
        // Known native encoded state is not an opaque vendor blob: inspect the resource fields the native
        // runtime/editors know how to open. Roles are separate so a sample path cannot become a launcher URL.
        if (value.instrument == InstrumentKind::videoplayer)
            if (const auto player = video::read (value.pluginState)) nativeResource ("video", player->path);
        if (value.instrument == InstrumentKind::wavetraveller)
        {
            wavetrav::State state;
            if (wavetrav::parseState (value.pluginState, state)) nativeResource ("wave-traveller", state.samplePath);
        }
        if (value.instrument == InstrumentKind::soundfont)
        {
            sf::FlPlayerState state;
            if (sf::parseFlPlayerState (value.pluginState, state)) nativeResource ("soundfont", state.path);
        }
        if (value.instrument == InstrumentKind::gms)
            for (const auto& name : gmsWaveNames (value.pluginState)) nativeResource ("gms-wave", name);
        if (value.instrument == InstrumentKind::bassdrum) nativeResource ("bassdrum-noise", bassDrumNoiseName (value.pluginState));
        if (value.instrument == InstrumentKind::morphine) nativeResource ("morphine-noise", morphineNoiseName (value.pluginState));
        if (value.instrument == InstrumentKind::vfx)
            if (const auto state = vfx::readState (value.pluginState); state && state->kind == vfx::Kind::script)
            {
                Sha256Stream hash;
                hash.update (value.pluginState.data(), value.pluginState.size());
                result.scripts.insert (hash.hex());
            }
        if (value.instrument == InstrumentKind::patcher || value.instrument == InstrumentKind::dashboard)
            patcher (value.patcher);
    };
    effect = [&] (const EffectSlot& value) {
        path (value.samplePath);
        if (value.kind == EffectKind::htmlNotebook)
            nativeResource ("html-notebook", flextra::htmlnotebook::address (value.state));
        if (value.kind == EffectKind::visualizer)
            if (const auto scene = visualfx::scene::fromState (value.state))
                for (const auto& layer : scene->layers) nativeResource ("visualizer", layer.path);
        if (value.kind == EffectKind::scratcher)
            if (const auto state = scratcher::parse (value.state)) nativeResource ("scratcher", state->samplePath);
        if (value.kind == EffectKind::plugin) plugin (value.plugin);
        else plugin (value.substituteFor);
        for (const auto& branch : value.branches)
            for (const auto& child : branch.chain) effect (child);
        if (value.kind == EffectKind::patcher || value.kind == EffectKind::controlSurface) patcher (value.patcher);
    };
    patcher = [&] (const PatcherDoc& value) {
        for (const auto& node : value.nodes)
        {
            if (node.generator) channel (node.channel); else effect (node.effect);
            for (const auto& bus : node.busHeads) if (bus.head) effect (*bus.head);
        }
    };
    for (const auto& value : song.channels) channel (value);
    for (const auto& track : song.mixer)
    {
        for (const auto& standIn : track.standIns) path (standIn.path);
        for (const auto& value : track.effects) effect (value);
    }
    for (const auto& track : song.videoTracks)
        for (const auto& clip : track.clips) nativeResource ("video", clip.path);
    return result;
}
void ioPolicy (const Song& before, const Song& after)
{
    // Musical document edits may reuse existing resources, but do not authorize new filesystem/network locations.
    // Exact spelling is intentional: no normalization can turn a new UNC / device path into an old local asset.
    const auto known = ioReferences (before), requested = ioReferences (after);
    for (const auto& path : requested.assets)
        if (! known.assets.contains (path)) remote_json::invalid ("Project edits cannot introduce asset paths; import assets in LYNX first");
    for (const auto& id : requested.plugins)
        if (! known.plugins.contains (id)) remote_json::invalid ("Project edits cannot introduce plugin loader IDs; use a catalog instrument/effect operation");
    for (const auto& locator : requested.foreign)
        if (! known.foreign.contains (locator)) remote_json::invalid ("Project edits cannot introduce foreign plugin locations or identities");
    for (const auto& resource : requested.nativeResources)
        if (! known.nativeResources.contains (resource))
            remote_json::invalid ("Project edits cannot introduce native media, page or resource locations");
    for (const auto& payload : requested.scripts)
        if (! known.scripts.contains (payload)) remote_json::invalid ("Project edits cannot add or change VFX Script payloads");
    if (before.dataFolder != after.dataFolder) remote_json::invalid ("Project output/data folders are read-only through document edits");
    for (std::size_t i = 0; i < std::max (before.mixer.size(), after.mixer.size()); ++i)
    {
        const auto oldPath = i < before.mixer.size() ? before.mixer[i].recordTo : std::string();
        const auto newPath = i < after.mixer.size() ? after.mixer[i].recordTo : std::string();
        if (oldPath != newPath) remote_json::invalid ("Mixer recording output paths are read-only through document edits");
    }
}
} // namespace

namespace remote_document
{
std::string bytes (const Song& song)
{
    auto result = ProjectFile::toJsonExact (song).toStdString();
    if (result.size() > maxBytes) throw RemoteError ("limit", "Project document exceeds the 64 MiB remote API limit");
    return result;
}
std::string hash (const std::string& document)
{
    Sha256Stream digest;
    digest.update (reinterpret_cast<const std::uint8_t*> (document.data()), document.size());
    return digest.hex();
}
Song stage (const Song& source, const juce::var& patch)
{
    juce::var before;
    if (juce::JSON::parse (juce::String::fromUTF8 (bytes (source).c_str()), before).failed())
        remote_json::invalid ("Cannot decode the native project document");
    const auto candidate = remote_json::apply (before, patch);
    if (candidate.getDynamicObject() == nullptr) remote_json::invalid ("Project root must remain an object");
    hostedStatePolicy (before, candidate);
    for (const auto* key : { "format", "version", "projectId", "nextUid", "session" })
        if (before.hasProperty (key) != candidate.hasProperty (key) || ! remote_json::equal (before[key], candidate[key]))
            remote_json::invalid ("Project format, lineage and collaboration identity are read-only");
    editableDocument (before, candidate);
    const auto json = juce::JSON::toString (candidate, true, 17);
    if (json.getNumBytesAsUTF8() > maxBytes) remote_json::invalid ("Edited project exceeds the 64 MiB remote API limit");
    juce::String error;
    auto loaded = ProjectFile::fromJson (json, error);
    if (! loaded) remote_json::invalid ("Edited document is not a supported native project");
    juce::var roundtrip;
    if (juce::JSON::parse (ProjectFile::toJsonExact (*loaded), roundtrip).failed() || ! remote_json::equal (candidate, roundtrip, true))
        remote_json::invalid ("Edited document has unsupported fields, invalid types or values the native format would discard or clamp");
    references (*loaded);
    identityAndLocks (source, *loaded);
    ioPolicy (source, *loaded);
    uids::normalize (*loaded);
    (void) bytes (*loaded); // server-assigned identities must also fit before committing
    return std::move (*loaded);
}
} // namespace remote_document

} // namespace lynx
