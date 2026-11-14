#include "engine/RemoteProjectControl.h"
#include "engine/AudioEngine.h"
#include "engine/RemoteJsonPatch.h"
#include "engine/RemoteError.h"
#include <juce_events/juce_events.h>

namespace lynx
{
namespace
{
using Field = std::pair<const char*, juce::var>;
juce::var object (std::initializer_list<Field> fields)
{
    auto* value = new juce::DynamicObject();
    for (const auto& [key, item] : fields) value->setProperty (key, item);
    return value;
}
void fields (const juce::var& args, std::initializer_list<const char*> allowed)
{
    const auto* value = args.getDynamicObject();
    if (value == nullptr) remote_json::invalid ("Project API arguments must be an object");
    for (const auto& field : value->getProperties())
        if (std::none_of (allowed.begin(), allowed.end(), [&] (const char* key) { return field.name.toString() == key; }))
            remote_json::invalid ("Unknown project API argument");
}
std::size_t integer (const juce::var& args, const char* key, std::size_t fallback, std::size_t maximum)
{
    if (! args.hasProperty (key)) return fallback;
    const auto& value = args[key];
    if (! remote_json::number (value)) remote_json::invalid ("Project page offset/limit must be integers");
    const auto n = (double) value;
    if (! std::isfinite (n) || std::floor (n) != n || n < 0 || n > (double) maximum)
        remote_json::invalid ("Project page offset/limit is out of range");
    return (std::size_t) n;
}
bool lowerHex (const juce::String& value, int count)
{
    if (value.length() != count) return false;
    for (const auto c : value) if (! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
void checkCancelled (const std::function<bool()>& cancelled)
{
    if (cancelled && cancelled()) remote_json::invalid ("Project operation cancelled before commit");
}
} // namespace

bool isRemoteProjectOperation (const juce::String& op) { return op == "project.read" || op == "project.edit"; }

juce::var RemoteProjectControl::dispatch (const juce::String& operation, const juce::var& args, const std::function<bool()>& cancelled)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    checkCancelled (cancelled);
    if (engine.getTransport().playing.load() || engine.isRecording())
        throw RemoteError ("busy", "Stop playback and recording before full project snapshots or edits");
    const auto revision = engine.getSongStamp();
    const auto project = engine.getSong().projectId;
    snapshots.erase (std::remove_if (snapshots.begin(), snapshots.end(), [&] (const auto& snapshot) {
        return snapshot.revision != revision || snapshot.project != project;
    }), snapshots.end());
    if (operation == "project.read")
    {
        fields (args, { "snapshotId", "expectedDocumentHash", "offset", "limit" });
        const auto offset = integer (args, "offset", 0, remote_document::maxBytes);
        const auto limit = integer (args, "limit", 32768, remote_document::maxPage);
        if (limit == 0) remote_json::invalid ("Project page limit must be in 1..65536");
        if (! args.hasProperty ("snapshotId"))
        {
            if (offset != 0 || args.hasProperty ("expectedDocumentHash")) remote_json::invalid ("A continuation requires its snapshotId and document hash");
            auto document = remote_document::bytes (engine.snapshotForRemoteControl());
            std::size_t held = 0;
            for (const auto& snapshot : snapshots) held += snapshot.document.size();
            while (! snapshots.empty() && (snapshots.size() >= 4 || held + document.size() > remote_document::maxBytes))
            {
                held -= snapshots.front().document.size();
                snapshots.pop_front();
            }
            const auto id = juce::Uuid().toString().removeCharacters ("-");
            const auto digest = remote_document::hash (document);
            snapshots.push_back ({ id, project, std::move (document), digest, revision });
        }
        const Snapshot* snapshot = nullptr;
        if (args.hasProperty ("snapshotId"))
        {
            if (! args["snapshotId"].isString() || ! lowerHex (args["snapshotId"].toString(), 32)
                || ! args["expectedDocumentHash"].isString()) remote_json::invalid ("Page requires the exact snapshot ID and document hash");
            for (const auto& item : snapshots) if (item.id == args["snapshotId"].toString()) snapshot = &item;
        }
        else snapshot = &snapshots.back();
        if (snapshot == nullptr || (args.hasProperty ("snapshotId") && snapshot->digest != args["expectedDocumentHash"].toString().toStdString()))
            throw RemoteError ("changed", "Project snapshot expired, changed or has the wrong hash; start a new read");
        if (offset > snapshot->document.size()) remote_json::invalid ("Project page offset is past the document end");
        const auto count = std::min (limit, snapshot->document.size() - offset);
        auto result = object ({ { "snapshotId", snapshot->id }, { "documentHash", juce::String (snapshot->digest) },
            { "encoding", "base64" }, { "data", juce::Base64::toBase64 (snapshot->document.data() + offset, count) },
            { "offset", (juce::int64) offset }, { "totalBytes", (juce::int64) snapshot->document.size() },
            { "revision", (juce::int64) snapshot->revision }, { "projectId", juce::String (snapshot->project) } });
        if (offset + count < snapshot->document.size()) result.getDynamicObject()->setProperty ("nextOffset", (juce::int64) (offset + count));
        return result;
    }
    if (operation != "project.edit") remote_json::invalid ("Unsupported project operation");
    fields (args, { "snapshotId", "expectedDocumentHash", "patch", "expectedProjectId", "expectedEpoch", "expectedRevision" });
    if (! args["snapshotId"].isString() || ! lowerHex (args["snapshotId"].toString(), 32)
        || ! args["expectedDocumentHash"].isString() || ! lowerHex (args["expectedDocumentHash"].toString(), 64))
        remote_json::invalid ("Project edit requires its snapshot ID and SHA-256 document hash");
    const auto found = std::find_if (snapshots.begin(), snapshots.end(), [&] (const auto& snapshot) {
        return snapshot.id == args["snapshotId"].toString() && snapshot.digest == args["expectedDocumentHash"].toString().toStdString();
    });
    if (found == snapshots.end()) throw RemoteError ("changed", "Project snapshot expired or changed; read before planning another edit");
    const auto digest = found->digest;
    const auto changed = engine.editSongRemoteTransaction ([&] (Song& current) {
        checkCancelled (cancelled);
        if (remote_document::hash (remote_document::bytes (current)) != digest)
            throw RemoteError ("changed", "Project or live plugin state changed after the snapshot; nothing was committed");
        auto staged = remote_document::stage (current, args["patch"]);
        checkCancelled (cancelled);
        current = std::move (staged);
    }, cancelled, true);
    if (changed) snapshots.clear();
    std::string afterHash = digest;
    if (changed)
    {
        try { afterHash = remote_document::hash (remote_document::bytes (engine.snapshotForRemoteControl())); }
        catch (...)
        {
            throw RemoteError ("uncertain", "Project edit committed, but its final document could not be inspected; read before retrying", true);
        }
    }
    return object ({ { "changed", changed }, { "operationCount", args["patch"].size() },
        { "documentHash", juce::String (afterHash) } });
}
} // namespace lynx
