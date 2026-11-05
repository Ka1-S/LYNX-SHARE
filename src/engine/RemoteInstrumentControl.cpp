#include "engine/RemoteInstrumentControl.h"
#include "engine/AudioEngine.h"
#include "engine/GeneratorCatalog.h"
#include "engine/RemoteInstrumentEdits.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lynx
{
namespace
{
using Field = std::pair<const char*, juce::var>;
juce::var object (std::initializer_list<Field> fields)
{
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields) result->setProperty (key, value);
    return result;
}
juce::String text (const std::string& value) { return juce::String::fromUTF8 (value.c_str()); }
[[noreturn]] void invalid (const juce::String& why) { throw std::runtime_error (why.toStdString()); }
void fields (const juce::var& value, std::initializer_list<const char*> allowed)
{
    const auto* source = value.getDynamicObject();
    if (source == nullptr) invalid ("Expected an object");
    for (const auto& property : source->getProperties())
        if (std::none_of (allowed.begin(), allowed.end(), [&] (const char* key) { return property.name == juce::StringRef (key); }))
            invalid ("Unknown field: " + property.name.toString());
}
juce::String string (const juce::var& args, const char* key, int lo, int hi)
{
    if (! args[key].isString()) invalid (juce::String (key) + " must be a string");
    const auto value = args[key].toString();
    if (value.length() < lo || value.length() > hi) invalid (juce::String (key) + " has an invalid length");
    for (const auto c : value)
        if (c < 32 || c == 127) invalid (juce::String (key) + " must not contain control characters");
    return value;
}
int integer (const juce::var& args, const char* key, int lo, int hi)
{
    const auto& value = args[key];
    if (! (value.isInt() || value.isInt64() || value.isDouble())) invalid (juce::String (key) + " must be an integer");
    const auto n = (double) value;
    if (! std::isfinite (n) || std::floor (n) != n || n < lo || n > hi) invalid (juce::String (key) + " is out of range");
    return (int) n;
}
juce::String instrumentId (const generators::Entry& entry)
{
    return (entry.kind == InstrumentKind::plugin ? "plugin:" : "") + text (entry.key);
}
void checkCommit (AudioEngine& engine, const juce::var& args, const std::function<bool()>& cancelled)
{
    if (cancelled && cancelled()) invalid ("Instrument operation cancelled before committing changes");
    if (args["expectedProjectId"].toString() != text (engine.getSong().projectId)
        || ! (args["expectedRevision"].isInt() || args["expectedRevision"].isInt64())
        || (juce::int64) args["expectedRevision"] < 0
        || (std::uint64_t) (juce::int64) args["expectedRevision"] != engine.getSongStamp())
        invalid ("The project changed while preparing the instrument; inspect it again");
    if (engine.getTransport().playing.load() || engine.isRecording())
        invalid ("Stop playback and recording before editing instruments");
}
juce::var catalog (AudioEngine& engine, const juce::var& args)
{
    fields (args, { "query", "offset", "limit" });
    const auto query = args.hasProperty ("query") ? string (args, "query", 0, 256) : juce::String();
    const auto offset = args.hasProperty ("offset") ? integer (args, "offset", 0, 100000) : 0;
    const auto limit = args.hasProperty ("limit") ? integer (args, "limit", 1, 512) : 256;
    juce::Array<juce::var> all;
    for (const auto& entry : generators::entries (engine, true))
    {
        if (query.isNotEmpty() && ! entry.label.containsIgnoreCase (query) && ! entry.manufacturer.containsIgnoreCase (query)) continue;
        auto value = object ({ { "instrumentId", instrumentId (entry) }, { "name", entry.label },
                              { "type", entry.kind == InstrumentKind::plugin ? "hosted" : "native" }, { "available", entry.available } });
        auto* properties = value.getDynamicObject();
        if (entry.unavailableReason.isNotEmpty()) properties->setProperty ("reason", entry.unavailableReason);
        if (entry.format.isNotEmpty()) properties->setProperty ("format", entry.format);
        if (entry.manufacturer.isNotEmpty()) properties->setProperty ("manufacturer", entry.manufacturer);
        all.add (std::move (value));
    }
    juce::Array<juce::var> selected;
    for (int i = offset; i < all.size() && i < offset + limit; ++i) selected.add (all[i]);
    auto result = object ({ { "instruments", selected }, { "count", all.size() }, { "offset", offset } });
    if (offset + limit < all.size()) result.getDynamicObject()->setProperty ("nextOffset", offset + limit);
    return result;
}
juce::var edit (AudioEngine& engine, const juce::String& operation, const juce::var& args, const std::function<bool()>& cancelled)
{
    const auto replace = operation == "instrument.replace";
    if (replace) fields (args, { "instrumentId", "channelIndex", "name", "expectedProjectId", "expectedRevision", "expectedEpoch" });
    else fields (args, { "instrumentId", "channelIndex", "mixerIndex", "name", "expectedProjectId", "expectedRevision", "expectedEpoch" });
    const auto id = string (args, "instrumentId", 1, 4096);
    const auto count = (int) engine.getSong().channels.size();
    if (! replace && count >= 100000) invalid ("The Channel Rack has reached the control channel limit");
    const auto at = replace || args.hasProperty ("channelIndex") ? integer (args, "channelIndex", 0, replace ? count - 1 : count) : count;
    std::optional<int> mixer;
    if (args.hasProperty ("mixerIndex")) mixer = integer (args, "mixerIndex", 0, (int) engine.getSong().mixer.size() - 1);
    const auto name = args.hasProperty ("name") ? std::optional<juce::String> (string (args, "name", 1, 256)) : std::nullopt;
    const auto entries = generators::entries (engine, true);
    const auto found = std::find_if (entries.begin(), entries.end(), [&] (const auto& entry) { return instrumentId (entry) == id; });
    if (found == entries.end()) invalid ("Unknown instrumentId; choose an instrument returned by instrument.catalog");
    if (! found->available) invalid ("The selected instrument is unavailable: " + found->unavailableReason);
    checkCommit (engine, args, cancelled);
    auto generator = found->make();
    if (! generator) invalid ("The selected instrument could not be prepared; no project changes made");
    if (name) generator->name = name->toStdString();
    const auto hosted = generator->instrument == InstrumentKind::plugin;
    const auto preparedRef = generator->plugin;
    const auto rate = engine.getTransport().sampleRate.load();
    const juce::ScopeGuard discardPrepared { [&] { if (hosted) engine.getPluginHost().discardPreloaded (preparedRef, rate); } };
    if (hosted)
    {
        juce::String error;
        if (! engine.getPluginHost().preloadChecked (preparedRef, rate, error))
            invalid ("Cannot load the selected instrument: " + error.substring (0, 512));
    }
    checkCommit (engine, args, cancelled);
    if (hosted && engine.getTransport().sampleRate.load() != rate)
        invalid ("The audio device changed while preparing the instrument; inspect it again");
    // The structural path captures live hosted state once, only after every
    // preparation/cancellation guard has passed. Build from that fresh model;
    // never replace it with a copy made before plugin-state capture.
    engine.editSongRemoteTransaction ([replace, at, mixer, generator = std::move (*generator)] (Song& current) mutable {
        auto staged = replace ? remote_instruments::replace (current, at, std::move (generator))
                              : remote_instruments::create (current, std::move (generator), at, mixer);
        current = std::move (staged.song);
    }, cancelled);
    const auto& channel = engine.getSong().channels[(std::size_t) at];
    return object ({ { "channelIndex", at }, { "instrumentId", id }, { "mixerIndex", channel.mixerTrack },
                    { "name", text (channel.name) }, { "channelCount", (int) engine.getSong().channels.size() } });
}
}
bool isRemoteInstrumentOperation (const juce::String& operation)
{
    return operation == "instrument.catalog" || operation == "channel.create" || operation == "instrument.replace";
}
juce::var dispatchRemoteInstrument (AudioEngine& engine, const juce::String& operation, const juce::var& args,
                                 const std::function<bool()>& cancelled)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (operation == "instrument.catalog") return catalog (engine, args);
    if (engine.getTransport().playing.load() || engine.isRecording()) invalid ("Stop playback and recording before editing instruments");
    if (operation == "channel.create" || operation == "instrument.replace") return edit (engine, operation, args, cancelled);
    invalid ("Unsupported instrument operation");
}
}
