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

namespace lynx
{
namespace
{
void hostedStateClears (const juce::var& before, const juce::var& after)
{
    // Clearing a vendor blob does not have a portable in-place reset meaning. Use the
    // explicit catalog replacement operations for defaults, rather than silently keeping
    // a running device's old state. This walk also covers branches, Patchers and bus heads.
    std::map<juce::String, int> oldEmpty, newEmpty;
    std::set<juce::String> saved;
    const auto collect = [] (const juce::var& root, std::map<juce::String, int>& empty, std::set<juce::String>* nonempty) {
        std::function<void (const juce::var&)> visit;
        visit = [&] (const juce::var& value) {
            if (const auto* object = value.getDynamicObject())
            {
                if (value["id"].isString())
                {
                    const auto key = value["id"].toString();
                    if (value["state"].toString().isEmpty()) ++empty[key];
                    else if (nonempty != nullptr) nonempty->insert (key);
                }
                for (const auto& property : object->getProperties()) visit (property.value);
            }
            else if (const auto* array = value.getArray())
                for (const auto& item : *array) visit (item);
        };
        visit (root);
    };
    collect (before, oldEmpty, &saved);
    collect (after, newEmpty, nullptr);
    for (const auto& key : saved)
        if (newEmpty[key] > oldEmpty[key])
            remote_json::invalid ("Project edits cannot clear hosted device state; use a catalog replacement operation to reset it");
    std::function<void (const juce::var&, const juce::var&)> compare;
    compare = [&] (const juce::var& old, const juce::var& next) {
        if (const auto* object = old.getDynamicObject())
        {
            if (next.getDynamicObject() == nullptr) return;
            if (old["id"].isString() && old["state"].isString() && old["state"].toString().isNotEmpty()
                && next["id"] == old["id"] && next["state"].toString().isEmpty())
                remote_json::invalid ("Project edits cannot clear hosted device state; use a catalog replacement operation to reset it");
            for (const auto& property : object->getProperties()) compare (property.value, next[property.name]);
        }
        else if (const auto* array = old.getArray())
            if (const auto* other = next.getArray())
                for (int i = 0; i < std::min (array->size(), other->size()); ++i) compare ((*array)[i], (*other)[i]);
    };
    compare (before, after);
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
    hostedStateClears (before, candidate);
    for (const auto* key : { "format", "version", "projectId", "nextUid", "session" })
        if (before.hasProperty (key) != candidate.hasProperty (key) || ! remote_json::equal (before[key], candidate[key]))
            remote_json::invalid ("Project format, lineage and collaboration identity are read-only");
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
