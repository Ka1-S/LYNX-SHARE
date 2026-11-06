#include "RemotePluginControl.h"
#include "AudioEngine.h"
#include "core/Effects.h"
#include "core/MixerOps.h"
#include "core/PluginLayout.h"
#include "core/StateParams.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace lynx
{
namespace
{
using Field = std::pair<const char*, juce::var>;
juce::var object (std::initializer_list<Field> fields)
{
    auto* result = new juce::DynamicObject();
    for (const auto& [name, value] : fields) result->setProperty (name, value);
    return result;
}
juce::String text (const std::string& value) { return juce::String::fromUTF8 (value.c_str()); }
[[noreturn]] void invalid (const juce::String& message) { throw std::runtime_error (message.toStdString()); }
void checkCancelled (const std::function<bool()>& cancelled)
{
    if (cancelled && cancelled()) invalid ("Plugin operation cancelled before committing changes");
}
void checkCommit (AudioEngine& engine, const juce::var& args, const std::function<bool()>& cancelled)
{
    checkCancelled (cancelled);
    if (args["expectedProjectId"].toString() != text (engine.getSong().projectId)
        || ! (args["expectedRevision"].isInt() || args["expectedRevision"].isInt64())
        || (juce::int64) args["expectedRevision"] < 0
        || (std::uint64_t) (juce::int64) args["expectedRevision"] != engine.getSongStamp())
        invalid ("The project changed while preparing the plugin; inspect it again");
    if (engine.getTransport().playing.load() || engine.isRecording()) invalid ("Stop playback and recording before editing plugins");
}
void fields (const juce::var& value, std::initializer_list<const char*> allowed)
{
    const auto* source = value.getDynamicObject();
    if (source == nullptr) invalid ("Expected an object");
    for (const auto& property : source->getProperties())
        if (std::none_of (allowed.begin(), allowed.end(), [&] (const char* name) { return property.name == juce::StringRef (name); }))
            invalid ("Unknown field: " + property.name.toString());
}
double number (const juce::var& value, const char* key, double lo, double hi)
{
    const auto& n = value[key];
    if (! n.isInt() && ! n.isInt64() && ! n.isDouble()) invalid (juce::String (key) + " must be numeric");
    const auto result = (double) n;
    if (! std::isfinite (result) || result < lo || result > hi) invalid (juce::String (key) + " is out of range");
    return result;
}
int index (const juce::var& value, const char* key, int count)
{
    if (count <= 0) invalid (juce::String (key) + " refers to an empty collection");
    const auto n = number (value, key, 0, count - 1);
    if (std::floor (n) != n) invalid (juce::String (key) + " must be an integer");
    return (int) n;
}
int paging (const juce::var& args, const char* key, int fallback, int lo, int hi)
{
    if (! args.hasProperty (key)) return fallback;
    const auto n = number (args, key, lo, hi);
    if (std::floor (n) != n) invalid (juce::String (key) + " must be an integer");
    return (int) n;
}

struct Target
{
    bool instrument = false;
    int channel = -1, mixer = -1, effect = -1;
    juce::var value() const
    {
        return instrument ? object ({ { "kind", "instrument" }, { "channelIndex", channel } })
                          : object ({ { "kind", "effect" }, { "mixerIndex", mixer }, { "effectIndex", effect } });
    }
};
Target target (const Song& song, const juce::var& value)
{
    if (! value["kind"].isString()) invalid ("target.kind must be instrument or effect");
    Target t;
    if (value["kind"].toString() == "instrument")
    {
        fields (value, { "kind", "channelIndex" });
        t.instrument = true;
        t.channel = index (value, "channelIndex", (int) song.channels.size());
        if (song.channels[(std::size_t) t.channel].instrument != InstrumentKind::plugin)
            invalid ("This instrument is native; this endpoint controls hosted instruments and mixer effects");
    }
    else if (value["kind"].toString() == "effect")
    {
        fields (value, { "kind", "mixerIndex", "effectIndex" });
        t.mixer = index (value, "mixerIndex", (int) song.mixer.size());
        t.effect = index (value, "effectIndex", (int) song.mixer[(std::size_t) t.mixer].effects.size());
    }
    else invalid ("target.kind must be instrument or effect");
    return t;
}
const PluginRef& pluginRef (const Song& song, const Target& t)
{
    return t.instrument ? song.channels[(std::size_t) t.channel].plugin
                        : song.mixer[(std::size_t) t.mixer].effects[(std::size_t) t.effect].plugin;
}
PluginRef& pluginRef (Song& song, const Target& t)
{
    return t.instrument ? song.channels[(std::size_t) t.channel].plugin
                        : song.mixer[(std::size_t) t.mixer].effects[(std::size_t) t.effect].plugin;
}
bool hosted (const Song& song, const Target& t)
{
    return t.instrument || song.mixer[(std::size_t) t.mixer].effects[(std::size_t) t.effect].kind == EffectKind::plugin;
}
struct Hosted
{
    std::shared_ptr<Instrument> instrumentOwner;
    std::shared_ptr<Effect> effectOwner;
    PluginInstrument* instrument = nullptr;
    PluginEffect* effect = nullptr;
    juce::AudioPluginInstance& instance() const { return instrument != nullptr ? instrument->getInstance() : effect->getInstance(); }
    bool crashed() const { return instrument != nullptr ? instrument->hasCrashed() : effect->hasCrashed(); }
    void set (int parameter, float value) const
    {
        if (instrument != nullptr) instrument->setParameterFromUi (parameter, value);
        else effect->setParameterFromUi (parameter, value);
    }
    void flushStagedParameters (double bpm) const
    {
        // A VST3 controller receives UI edits immediately, but its component may
        // consume them only in process(). Capturing before that block can save
        // the old component state. Process one bounded, silent staging buffer;
        // this instance is never attached to the device or the live song.
        std::array<float, PluginHost::maxBlockSize> left {}, right {};
        if (instrumentOwner != nullptr)
            instrumentOwner->render (left.data(), right.data(), PluginHost::maxBlockSize, 0.0f, 0.0f);
        else
            effectOwner->process (left.data(), right.data(), PluginHost::maxBlockSize, {}, bpm);
    }
    std::optional<std::string> capture() const { return instrument != nullptr ? instrument->captureState() : effect->captureState(); }
};
Hosted running (AudioEngine& engine, const Target& t)
{
    Hosted result;
    if (t.instrument)
    {
        result.instrumentOwner = engine.getChannelInstrument (t.channel);
        result.instrument = dynamic_cast<PluginInstrument*> (result.instrumentOwner.get());
    }
    else
    {
        result.effectOwner = engine.getEffect (t.mixer, t.effect);
        result.effect = dynamic_cast<PluginEffect*> (result.effectOwner.get());
    }
    if ((result.instrument == nullptr && result.effect == nullptr) || result.crashed())
        invalid ("The hosted plugin is unavailable or has crashed");
    return result;
}
Hosted staging (AudioEngine& engine, const Target& t, const PluginRef& ref)
{
    Hosted result;
    juce::String error;
    const auto rate = engine.getTransport().sampleRate.load();
    if (t.instrument)
    {
        result.instrumentOwner = engine.getPluginHost().makeInstrument (ref, rate, &error);
        result.instrument = dynamic_cast<PluginInstrument*> (result.instrumentOwner.get());
    }
    else
    {
        result.effectOwner = engine.getPluginHost().makeEffect (ref, rate, &error);
        result.effect = dynamic_cast<PluginEffect*> (result.effectOwner.get());
    }
    if ((result.instrument == nullptr && result.effect == nullptr) || result.crashed())
        invalid ("Cannot stage plugin changes: " + error.substring (0, 512));
    return result;
}
bool readOnly (juce::AudioProcessorParameter& p)
{
    return ((int) p.getCategory() >> 16) == 2; // JUCE meter categories
}
juce::var hostedParameter (juce::AudioProcessorParameter& p, int i)
{
    auto* stable = dynamic_cast<juce::HostedAudioProcessorParameter*> (&p);
    auto result = object ({ { "index", i }, { "parameterId", stable != nullptr ? stable->getParameterID() : juce::String (i) },
                           { "name", p.getName (256) }, { "value", p.getValue() }, { "minimum", 0.0 }, { "maximum", 1.0 },
                           { "defaultValue", p.getDefaultValue() }, { "displayValue", p.getText (p.getValue(), 256) },
                           { "label", p.getLabel() }, { "readOnly", readOnly (p) }, { "automatable", p.isAutomatable() },
                           { "numSteps", p.getNumSteps() } });
    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (&p))
    {
        const auto& range = ranged->getNormalisableRange();
        result.getDynamicObject()->setProperty ("nativeMinimum", range.start);
        result.getDynamicObject()->setProperty ("nativeMaximum", range.end);
        result.getDynamicObject()->setProperty ("nativeValue", range.convertFrom0to1 (p.getValue()));
    }
    return result;
}
float nativeValue (const EffectSlot& slot, int parameter)
{
    return plugui::stateIndex (slot.kind, parameter) >= 0 ? plugui::stateValue01 (slot.kind, parameter, slot.state)
                                                        : slot.params[(std::size_t) parameter];
}
juce::var nativeParameter (const EffectSlot& slot, int i, double bpm)
{
    const auto stateIndex = plugui::stateIndex (slot.kind, i);
    const auto value = nativeValue (slot, i);
    juce::String display;
    auto result = object ({ { "index", i }, { "parameterId", "native:" + juce::String (i) },
                           { "name", plugui::paramName (slot.kind, i) }, { "value", value }, { "minimum", 0.0 },
                           { "maximum", 1.0 }, { "readOnly", false }, { "label", "" } });
    if (stateIndex >= 0)
    {
        const auto* table = stateparams::tableFor (slot.kind);
        const auto& description = table->params[(std::size_t) stateIndex];
        const auto raw = table->get (slot.state, stateIndex).value_or (description.defaultValue);
        display = text (table->text (slot.state, stateIndex, raw));
        result.getDynamicObject()->setProperty ("nativeMinimum", description.minimum);
        result.getDynamicObject()->setProperty ("nativeMaximum", description.maximum);
        result.getDynamicObject()->setProperty ("nativeValue", raw);
    }
    else
    {
        display = text (formatEffectParam (slot.kind, i, value, bpm, slot.params));
        result.getDynamicObject()->setProperty ("defaultValue", effectInfo (slot.kind).params[(std::size_t) i].defaultValue);
    }
    result.getDynamicObject()->setProperty ("displayValue", display);
    return result;
}
juce::var describe (AudioEngine& engine, const Target& t, bool withParameters, int offset, int limit)
{
    const auto& song = engine.getSong();
    auto result = object ({ { "target", t.value() }, { "valueScale", "normalized 0..1; displayValue and label give native units" } });
    juce::Array<juce::var> parameters;
    int count = 0;
    if (hosted (song, t))
    {
        const auto& ref = pluginRef (song, t);
        result.getDynamicObject()->setProperty ("name", text (ref.name));
        result.getDynamicObject()->setProperty ("pluginId", text (ref.id));
        result.getDynamicObject()->setProperty ("type", "hosted");
        try
        {
            auto live = running (engine, t);
            const auto& list = live.instance().getParameters();
            count = list.size();
            if (withParameters)
                for (int i = offset; i < count && i < offset + limit; ++i)
                    parameters.add (hostedParameter (*list[i], i));
            result.getDynamicObject()->setProperty ("available", true);
        }
        catch (const std::exception&)
        {
            if (withParameters) throw;
            result.getDynamicObject()->setProperty ("available", false);
        }
    }
    else
    {
        const auto& slot = song.mixer[(std::size_t) t.mixer].effects[(std::size_t) t.effect];
        result.getDynamicObject()->setProperty ("name", effectInfo (slot.kind).name);
        result.getDynamicObject()->setProperty ("pluginId", "native:" + juce::String ((int) slot.kind));
        result.getDynamicObject()->setProperty ("type", "native");
        result.getDynamicObject()->setProperty ("available", true);
        count = plugui::paramCount (slot.kind);
        if (withParameters)
            for (int i = offset; i < count && i < offset + limit; ++i) parameters.add (nativeParameter (slot, i, song.bpm));
    }
    result.getDynamicObject()->setProperty ("parameterCount", count);
    if (withParameters)
    {
        result.getDynamicObject()->setProperty ("parameters", parameters);
        result.getDynamicObject()->setProperty ("offset", offset);
        if (offset + limit < count) result.getDynamicObject()->setProperty ("nextOffset", offset + limit);
    }
    return result;
}
juce::var inspect (AudioEngine& engine, const juce::var& args)
{
    fields (args, { "target", "offset", "limit" });
    const auto offset = paging (args, "offset", 0, 0, 1000000), limit = paging (args, "limit", 128, 1, 256);
    if (args.hasProperty ("target")) return describe (engine, target (engine.getSong(), args["target"]), true, offset, limit);
    juce::Array<juce::var> devices;
    const auto& song = engine.getSong();
    for (int c = 0; c < (int) song.channels.size(); ++c)
        if (song.channels[(std::size_t) c].instrument == InstrumentKind::plugin)
            devices.add (describe (engine, { true, c, -1, -1 }, false, 0, 0));
    for (int m = 0; m < (int) song.mixer.size(); ++m)
        for (int e = 0; e < (int) song.mixer[(std::size_t) m].effects.size(); ++e)
            devices.add (describe (engine, { false, -1, m, e }, false, 0, 0));
    return object ({ { "devices", devices }, { "scope", "top-level hosted instruments and hosted/native mixer effects" } });
}
juce::var catalog (AudioEngine& engine, const juce::var& args)
{
    fields (args, { "query", "offset", "limit" });
    juce::String query;
    if (args.hasProperty ("query"))
    {
        if (! args["query"].isString() || args["query"].toString().length() > 256) invalid ("query must be at most 256 characters");
        query = args["query"].toString();
    }
    const auto offset = paging (args, "offset", 0, 0, 1000000), limit = paging (args, "limit", 256, 1, 512);
    juce::Array<juce::var> all;
    for (int k = 0; k <= (int) lastEffectKind; ++k)
    {
        const auto kind = (EffectKind) k;
        if (kind == EffectKind::plugin) continue;
        const juce::String name = effectInfo (kind).name;
        if (name.isNotEmpty() && (query.isEmpty() || name.containsIgnoreCase (query)))
            all.add (object ({ { "effectId", "native:" + juce::String (k) }, { "name", name }, { "type", "native" } }));
    }
    for (const auto& description : engine.getPluginHost().getKnownPlugins().getTypes())
        if (! description.isInstrument && (query.isEmpty() || description.name.containsIgnoreCase (query)
                || description.manufacturerName.containsIgnoreCase (query)))
            all.add (object ({ { "effectId", "plugin:" + description.createIdentifierString() }, { "name", description.name },
                              { "type", "hosted" }, { "format", description.pluginFormatName }, { "manufacturer", description.manufacturerName } }));
    juce::Array<juce::var> selected;
    for (int i = offset; i < all.size() && i < offset + limit; ++i) selected.add (all[i]);
    auto result = object ({ { "effects", selected }, { "count", all.size() }, { "offset", offset } });
    if (offset + limit < all.size()) result.getDynamicObject()->setProperty ("nextOffset", offset + limit);
    return result;
}
struct Change { int index = 0; float value = 0, expected = 0; };
juce::var setParameters (AudioEngine& engine, const juce::var& args, const std::function<bool()>& cancelled)
{
    fields (args, { "target", "parameters", "expectedProjectId", "expectedRevision", "expectedEpoch" });
    const auto t = target (engine.getSong(), args["target"]);
    const auto* list = args["parameters"].getArray();
    if (list == nullptr || list->isEmpty() || list->size() > 64) invalid ("parameters must contain 1..64 changes");
    std::set<int> seen;
    std::vector<Change> changes;
    const auto isHosted = hosted (engine.getSong(), t);
    auto live = isHosted ? std::optional<Hosted> (running (engine, t)) : std::nullopt;
    const auto& song = engine.getSong();
    const auto* slot = ! t.instrument ? &song.mixer[(std::size_t) t.mixer].effects[(std::size_t) t.effect] : nullptr;
    const auto count = isHosted ? live->instance().getParameters().size() : plugui::paramCount (slot->kind);
    for (const auto& entry : *list)
    {
        fields (entry, { "index", "value", "expectedValue" });
        Change change { index (entry, "index", count), (float) number (entry, "value", 0, 1), (float) number (entry, "expectedValue", 0, 1) };
        if (! seen.insert (change.index).second) invalid ("A parameter index may occur only once");
        const auto value = isHosted ? live->instance().getParameters()[change.index]->getValue() : nativeValue (*slot, change.index);
        if (isHosted && readOnly (*live->instance().getParameters()[change.index])) invalid ("A read-only meter parameter cannot be changed");
        if (! std::isfinite (value) || std::abs (value - change.expected) > 0.000001f)
            invalid ("A parameter changed since it was read; inspect the plugin again");
        changes.push_back (change);
    }
    checkCancelled (cancelled);
    if (isHosted)
    {
        auto ref = pluginRef (song, t);
        const auto before = live->capture();
        if (! before) invalid ("Cannot capture the plugin's current state; no changes made");
        ref.state = *before;
        auto staged = staging (engine, t, ref);
        checkCancelled (cancelled);
        if (staged.instance().getParameters().size() != count) invalid ("Staged plugin exposes different parameters; no changes made");
        for (const auto& change : changes)
            if (std::abs (staged.instance().getParameters()[change.index]->getValue() - change.expected) > 0.000001f)
                invalid ("Plugin state did not reproduce the observed parameters; no changes made");
        for (const auto& change : changes)
        {
            staged.set (change.index, change.value);
            if (staged.crashed()) invalid ("The plugin rejected staged parameter changes; no project changes made");
        }
        staged.flushStagedParameters (song.bpm);
        if (staged.crashed()) invalid ("The plugin could not process staged parameter changes; no project changes made");
        checkCancelled (cancelled);
        std::vector<float> desired;
        desired.reserve (changes.size());
        for (const auto& change : changes)
        {
            const auto* parameter = staged.instance().getParameters()[change.index];
            const auto value = parameter->getValue();
            if (! std::isfinite (value)) invalid ("The staged plugin returned an invalid parameter value");
            const auto steps = parameter->getNumSteps();
            const auto tolerance = steps > 1 && steps < 1000000 ? 0.5f / (float) (steps - 1) + 0.000001f : 0.000001f;
            if (std::abs (value - change.value) > tolerance)
                invalid ("The plugin did not apply the staged parameter value; no project changes made");
            desired.push_back (value);
        }
        const auto after = staged.capture();
        if (! after) invalid ("Cannot capture staged plugin state; no project changes made");
        ref.state = *after;
        juce::String error;
        const auto verified = [&] (juce::AudioPluginInstance& prepared, juce::String& why) {
            const auto& parameters = prepared.getParameters();
            if (parameters.size() != count)
            {
                why = "Restored plugin exposes different parameters";
                return false;
            }
            for (std::size_t i = 0; i < changes.size(); ++i)
                if (! std::isfinite (parameters[changes[i].index]->getValue())
                    || std::abs (parameters[changes[i].index]->getValue() - desired[i]) > 0.000001f)
                {
                    why = "Plugin state did not retain staged parameter changes";
                    return false;
                }
            return true;
        };
        if (! engine.getPluginHost().preloadChecked (ref, engine.getTransport().sampleRate.load(), error, verified))
            invalid ("Cannot prepare changed plugin state: " + error.substring (0, 512));
        const auto rate = engine.getTransport().sampleRate.load();
        const juce::ScopeGuard discardPrepared { [&] { engine.getPluginHost().discardPreloaded (ref, rate); } };
        checkCommit (engine, args, cancelled);
        const auto stillBefore = live->capture();
        if (! stillBefore || *stillBefore != *before) invalid ("The plugin changed while preparing the edit; inspect it again");
        engine.editSongRemoteTransaction ([t, ref] (Song& edited) { pluginRef (edited, t).state = ref.state; }, cancelled);
    }
    else
    {
        auto replacement = *slot;
        for (const auto& change : changes)
            if (const auto s = plugui::stateIndex (replacement.kind, change.index); s >= 0)
            {
                const auto* table = stateparams::tableFor (replacement.kind);
                replacement.state = table->with (replacement.state, s, plugui::stateRaw (replacement.kind, change.index, change.value));
                if (! table->get (replacement.state, s)) invalid ("This state-backed parameter cannot be edited in this slot");
            }
            else replacement.params[(std::size_t) change.index] = change.value;
        checkCommit (engine, args, cancelled);
        engine.editSongRemoteTransaction ([t, replacement = std::move (replacement)] (Song& edited) {
            auto& effect = edited.mixer[(std::size_t) t.mixer].effects[(std::size_t) t.effect];
            effect.params = replacement.params;
            effect.state = replacement.state;
        }, cancelled);
    }
    return object ({ { "target", t.value() }, { "changed", (int) changes.size() } });
}
EffectSlot effectFor (AudioEngine& engine, const juce::String& id)
{
    if (id.startsWith ("native:"))
        for (int k = 0; k <= (int) lastEffectKind; ++k)
            if (id == "native:" + juce::String (k) && (EffectKind) k != EffectKind::plugin)
                return makeEffectSlot ((EffectKind) k);
    if (id.startsWith ("plugin:"))
    {
        const auto key = id.substring (7);
        for (const auto& description : engine.getPluginHost().getKnownPlugins().getTypes())
            if (! description.isInstrument && description.createIdentifierString() == key)
            {
                auto effect = makeEffectSlot (EffectKind::plugin);
                effect.plugin.id = key.toStdString();
                effect.plugin.name = description.name.toStdString();
                return effect;
            }
    }
    invalid ("Unknown effectId; choose an effect returned by effect.catalog");
}
juce::var load (AudioEngine& engine, const juce::var& args, const std::function<bool()>& cancelled)
{
    fields (args, { "mixerIndex", "effectId", "effectIndex", "expectedProjectId", "expectedRevision", "expectedEpoch" });
    const auto mixer = index (args, "mixerIndex", (int) engine.getSong().mixer.size());
    const auto size = (int) engine.getSong().mixer[(std::size_t) mixer].effects.size();
    if (size >= maxEffectSlots) invalid ("The mixer effect chain is full");
    const auto at = paging (args, "effectIndex", size, 0, size);
    if (! args["effectId"].isString() || args["effectId"].toString().length() > 4096) invalid ("effectId must be a catalog ID");
    auto fx = effectFor (engine, args["effectId"].toString());
    if (fx.kind == EffectKind::plugin)
    {
        juce::String error;
        if (! engine.getPluginHost().preloadChecked (fx.plugin, engine.getTransport().sampleRate.load(), error))
            invalid ("Cannot load the selected effect: " + error.substring (0, 512));
    }
    const auto preparedRef = fx.plugin;
    const auto rate = engine.getTransport().sampleRate.load();
    const juce::ScopeGuard discardPrepared { [&] {
        if (fx.kind == EffectKind::plugin) engine.getPluginHost().discardPreloaded (preparedRef, rate);
    } };
    checkCommit (engine, args, cancelled);
    engine.editSongRemoteTransaction ([mixer, at, fx = std::move (fx)] (Song& song) mutable {
        auto& effects = song.mixer[(std::size_t) mixer].effects;
        const auto rows = mixerops::effectRows (effects);
        int row = 0;
        while (std::find (rows.begin(), rows.end(), row) != rows.end()) ++row;
        const auto inserted = mixerops::insertEffectAtRow (song, mixer, std::move (fx), row);
        if (inserted < 0) invalid ("No free effect slot");
        if (inserted != at) mixerops::moveEffectSlot (song, mixer, inserted, at);
        mixerops::linkAddedSidechain (song, mixer, at);
    }, cancelled);
    return object ({ { "target", Target { false, -1, mixer, at }.value() }, { "loaded", true } });
}
juce::var remove (AudioEngine& engine, const juce::var& args, const std::function<bool()>& cancelled)
{
    fields (args, { "mixerIndex", "effectIndex", "expectedProjectId", "expectedRevision", "expectedEpoch" });
    const auto mixer = index (args, "mixerIndex", (int) engine.getSong().mixer.size());
    const auto effect = index (args, "effectIndex", (int) engine.getSong().mixer[(std::size_t) mixer].effects.size());
    checkCommit (engine, args, cancelled);
    engine.editSongRemoteTransaction ([mixer, effect] (Song& song) { mixerops::removeEffectKeepRows (song, mixer, effect); }, cancelled);
    return object ({ { "removed", true }, { "mixerIndex", mixer }, { "effectIndex", effect } });
}
}

bool isRemotePluginOperation (const juce::String& operation)
{
    return operation == "plugin.inspect" || operation == "plugin.parameters_set" || operation == "effect.catalog"
        || operation == "effect.load" || operation == "effect.remove";
}
juce::var dispatchRemotePlugin (AudioEngine& engine, const juce::String& operation, const juce::var& args,
                              const std::function<bool()>& cancelled)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (operation == "plugin.inspect") return inspect (engine, args);
    if (operation == "effect.catalog") return catalog (engine, args);
    if (engine.getTransport().playing.load() || engine.isRecording()) invalid ("Stop playback and recording before editing plugins");
    if (operation == "plugin.parameters_set") return setParameters (engine, args, cancelled);
    if (operation == "effect.load") return load (engine, args, cancelled);
    if (operation == "effect.remove") return remove (engine, args, cancelled);
    invalid ("Unsupported plugin operation");
}
}
