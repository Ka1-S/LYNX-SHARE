#include "engine/RemotePatternControl.h"
#include "engine/AudioEngine.h"
#include "engine/RemoteError.h"
#include "engine/RemotePatternEdits.h"
#include "core/SessionIds.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lynx
{
namespace
{
[[noreturn]] void invalid (const juce::String& why) { throw std::runtime_error (why.toStdString()); }
juce::String text (const std::string& value) { return juce::String::fromUTF8 (value.c_str()); }
void fields (const juce::var& value, std::initializer_list<const char*> allowed)
{
    const auto* source = value.getDynamicObject();
    if (source == nullptr) invalid ("Expected an object");
    for (const auto& property : source->getProperties())
        if (std::none_of (allowed.begin(), allowed.end(), [&] (const char* key) { return property.name == juce::StringRef (key); }))
            invalid ("Unknown field: " + property.name.toString());
}
int integer (const juce::var& args, const char* key, int lo, int hi)
{
    const auto& value = args[key];
    if (! (value.isInt() || value.isInt64() || value.isDouble())) invalid (juce::String (key) + " must be an integer");
    const auto n = (double) value;
    if (! std::isfinite (n) || std::floor (n) != n || n < lo || n > hi) invalid (juce::String (key) + " is out of range");
    return (int) n;
}
std::optional<std::string> name (const juce::var& args)
{
    if (! args.hasProperty ("name")) return {};
    if (! args["name"].isString()) invalid ("name must be a string");
    const auto value = args["name"].toString();
    if (value.trim().isEmpty() || value.length() > 256) invalid ("name must contain visible text and be at most 256 characters");
    for (const auto c : value)
        if (c < 32 || (c >= 127 && c <= 159)) invalid ("name must not contain control characters");
    return value.toStdString();
}
}
bool isRemotePatternOperation (const juce::String& operation)
{
    return operation == "pattern.create" || operation == "pattern.clone" || operation == "pattern.set";
}
juce::var dispatchRemotePattern (AudioEngine& engine, const juce::String& operation, const juce::var& args,
                              const std::function<bool()>& cancelled)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (! isRemotePatternOperation (operation)) invalid ("Unsupported pattern operation");
    if (engine.getTransport().playing.load() || engine.isRecording()) invalid ("Stop playback and recording before editing patterns");
    if (operation == "pattern.clone") fields (args, { "index", "name", "expectedProjectId", "expectedRevision", "expectedEpoch" });
    else fields (args, { "index", "name", "lengthTicks", "expectedProjectId", "expectedRevision", "expectedEpoch" });
    const auto create = operation == "pattern.create";
    const auto clone = operation == "pattern.clone";
    const auto count = (int) engine.getSong().patterns.size();
    const auto index = create && ! args.hasProperty ("index") ? count : integer (args, "index", 0, create ? count : count - 1);
    const auto nextName = name (args);
    const auto length = args.hasProperty ("lengthTicks")
        ? std::optional<std::int64_t> (integer (args, "lengthTicks", 0, (int) remote_patterns::maxLength)) : std::nullopt;
    bool changed = true;
    if (create) remote_patterns::validateCreate (engine.getSong(), index, nextName, length.value_or (0));
    else if (clone) remote_patterns::validateClone (engine.getSong(), index, nextName);
    else changed = remote_patterns::metadataChanged (engine.getSong(), index, nextName, length);
    if (cancelled && cancelled()) invalid ("Pattern operation cancelled before committing changes");
    // A no-op must not capture hosted state, add an undo step or advance the revision.
    if (changed)
        changed = engine.editSongRemoteTransaction ([=] (Song& current) {
            auto staged = create ? remote_patterns::create (current, index, nextName, length.value_or (0))
                : clone ? remote_patterns::clone (current, index, nextName) : remote_patterns::set (current, index, nextName, length);
            current = std::move (staged.song);
        }, cancelled);
    try
    {
        const auto resultIndex = clone ? index + 1 : index;
        const auto& pattern = engine.getSong().patterns[(std::size_t) resultIndex];
        juce::var result (new juce::DynamicObject());
        auto* properties = result.getDynamicObject();
        properties->setProperty ("index", resultIndex);
        properties->setProperty ("patternId", text (uids::toHex (pattern.uid)));
        properties->setProperty ("name", text (pattern.name));
        properties->setProperty ("lengthTicks", (juce::int64) pattern.lengthTicks);
        properties->setProperty ("effectiveLengthTicks", (juce::int64) pattern.effectiveLength (engine.getSong().meter()));
        properties->setProperty ("patternCount", (int) engine.getSong().patterns.size());
        properties->setProperty ("changed", changed);
        return result;
    }
    catch (...)
    {
        if (changed)
            throw RemoteError ("uncertain", "The pattern edit ran but its response could not be built; inspect before retrying", true);
        throw;
    }
}
}
