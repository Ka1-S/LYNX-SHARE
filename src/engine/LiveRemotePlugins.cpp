// Online Sessions P5-A1 (docs/design/ONLINE_SESSIONS.md "P5-A1 as built", round 190ai): remote plugins — the part of
// LiveSessionManager that lets a guest use a plugin only the host has. core/RemotePlugins.h has the rules.
//
// The wire adds two messages, both host -> guest:
//   shared {plugins: [{id, name, vendor, format, instrument, params: [[index, name, unit, low, high, default, steps,
//           [step names]], ...]}]}   what the host shares (an empty list for "listen only" guests)
//   rbusy  {keys: [...]}             the parts the host is rendering for their owners right now ("updating...")
// A guest needs no new message: using a shared plugin is an ordinary edit of its own channel / insert (a PluginRef
// with the host's plugin id, playedBy = the host, no state, parameter values), judged by the host's Authority like any
// other edit after remoteplug::sanitize and the per-guest rate limit.

#include "engine/LiveSessions.h"

#include <algorithm>
#include <cmath>

#include "core/StandIns.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "engine/SessionPack.h"
#include "ui/I18n.h"

namespace lynx
{
namespace
{
constexpr double stemRate = 44100.0;       // (the live stand-ins' rate, as in LiveSessions.cpp)
constexpr juce::int64 hostedTickMs = 250;
constexpr float sameValue = 0.002f;        // a value read back from the plugin within this of the song's is the song's

juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
std::string utf8 (const juce::String& s) { return s.toStdString(); }

juce::var object() { return juce::var (new juce::DynamicObject()); }
juce::var& set (juce::var& o, const char* name, const juce::var& value)
{
    o.getDynamicObject()->setProperty (name, value);
    return o;
}

juce::var sharedToVar (const std::vector<remoteplug::Shared>& list)
{
    juce::Array<juce::var> plugins;
    for (const auto& s : list)
    {
        auto o = object();
        set (o, "id", str (s.id));
        set (o, "name", str (s.name));
        set (o, "vendor", str (s.vendor));
        set (o, "format", str (s.format));
        set (o, "instrument", s.instrument);
        juce::Array<juce::var> params;
        for (const auto& p : s.params)
        {
            juce::Array<juce::var> row;
            row.add (p.index);
            row.add (str (p.name));
            row.add (str (p.unit));
            row.add (str (p.low));
            row.add (str (p.high));
            row.add ((double) p.def);
            row.add (p.steps);
            juce::Array<juce::var> names;
            for (const auto& n : p.stepNames)
                names.add (str (n));
            row.add (names);
            params.add (row);
        }
        set (o, "params", params);
        plugins.add (o);
    }
    return plugins;
}

std::vector<remoteplug::Shared> sharedFromVar (const juce::var& v)
{
    std::vector<remoteplug::Shared> out;
    const auto* plugins = v.getArray();
    if (plugins == nullptr)
        return out;
    for (const auto& o : *plugins)
    {
        if (out.size() >= remoteplug::maxShared)
            break;
        remoteplug::Shared s;
        s.id = utf8 (o["id"].toString().substring (0, 512));
        s.name = utf8 (o["name"].toString().substring (0, 128));
        s.vendor = utf8 (o["vendor"].toString().substring (0, 128));
        s.format = utf8 (o["format"].toString().substring (0, 16));
        s.instrument = (bool) o["instrument"];
        if (const auto* params = o["params"].getArray())
            for (const auto& row : *params)
            {
                const auto* r = row.getArray();
                if (r == nullptr || r->size() < 8 || s.params.size() >= remoteplug::maxParams)
                    continue;
                remoteplug::Param p;
                p.index = (int) (*r)[0];
                p.name = utf8 ((*r)[1].toString().substring (0, 128));
                p.unit = utf8 ((*r)[2].toString().substring (0, 32));
                p.low = utf8 ((*r)[3].toString().substring (0, 64));
                p.high = utf8 ((*r)[4].toString().substring (0, 64));
                p.def = (float) (double) (*r)[5];
                p.steps = (int) (*r)[6];
                if (const auto* names = (*r)[7].getArray())
                    for (int i = 0; i < names->size() && i < remoteplug::maxSteps; ++i)
                        p.stepNames.push_back (utf8 ((*names)[i].toString().substring (0, 64)));
                s.params.push_back (std::move (p));
            }
        out.push_back (std::move (s));
    }
    remoteplug::clean (out);
    return out;
}

juce::var keysToVar (const std::set<live::Key>& keys)
{
    juce::Array<juce::var> out;
    for (const auto& k : keys)
        out.add (juce::String (live::toString (k)));
    return out;
}

std::set<live::Key> keysFromVar (const juce::var& v)
{
    std::set<live::Key> out;
    if (const auto* a = v.getArray())
        for (const auto& item : *a)
            if (auto k = live::keyFromString (item.toString().toStdString()); k && (k->kind == live::Kind::channel || k->kind == live::Kind::insert) && out.size() < 4096)
                out.insert (*k);
    return out;
}

int channelOf (const Song& song, std::uint64_t uid)
{
    for (int i = 0; i < (int) song.channels.size(); ++i)
        if (song.channels[(std::size_t) i].uid == uid && uid != 0)
            return i;
    return -1;
}

// the running plugin of a hosted reference on this computer (nullptr: none) and what sets its parameters
struct Running
{
    std::shared_ptr<void> keep;
    juce::AudioPluginInstance* plugin = nullptr;
    std::function<void (int, float)> setParameter;
};

Running runningAt (AudioEngine& engine, const remoteplug::Place& place)
{
    Running r;
    const auto& song = engine.getSong();
    if (place.slot < 0)
    {
        const auto c = channelOf (song, place.part.a);
        if (c < 0)
            return r;
        auto instrument = engine.getChannelInstrument (c);
        if (auto* p = dynamic_cast<PluginInstrument*> (instrument.get()); p != nullptr && ! p->hasCrashed())
        {
            r.plugin = &p->getInstance();
            r.setParameter = [p] (int i, float v) { p->setParameterFromUi (i, v); };
            r.keep = instrument;
        }
        return r;
    }
    auto effect = engine.getEffect ((int) place.part.a, place.slot);
    if (auto* p = dynamic_cast<PluginEffect*> (effect.get()); p != nullptr && ! p->hasCrashed())
    {
        r.plugin = &p->getInstance();
        r.setParameter = [p] (int i, float v) { p->setParameterFromUi (i, v); };
        r.keep = effect;
    }
    return r;
}
} // namespace

// --- the host: sharing ----------------------------------------------------------------------------------------------

bool LiveSessionManager::sharePlugin (const std::string& pluginId, bool on, juce::String* error)
{
    if (! host || ! isActive())
    {
        if (error != nullptr)
            *error = tr ("Only the host of a live session can share plugins.");
        return false;
    }
    const auto it = std::find_if (shared.begin(), shared.end(), [&] (const remoteplug::Shared& s) { return s.id == pluginId; });
    if (! on)
    {
        if (it == shared.end())
            return true;
        shared.erase (it);
        hostedStateCache.clear();
        sendShared (0);
        if (onChange)
            onChange();
        return true;
    }
    if (it != shared.end())
        return true;
    if (shared.size() >= remoteplug::maxShared)
    {
        if (error != nullptr)
            *error = tr ("You can share up to {0} plugins at once.", { juce::String ((int) remoteplug::maxShared) });
        return false;
    }
    juce::String why;
    auto described = engine.getPluginHost().describeForSharing (pluginId, &why);
    if (! described)
    {
        if (error != nullptr)
            *error = why;
        return false;
    }
    shared.push_back (std::move (*described));
    hostedStateCache.clear();
    sendShared (0);
    lastRenderCheckMs = 0; // (parts that were waiting for it render now)
    if (onChange)
        onChange();
    return true;
}

void LiveSessionManager::sendShared (int to)
{
    if (! host)
        return;
    // "listen only" guests cannot use shared plugins: they are not told about them
    juce::var full, none;
    for (const auto& [n, p] : peers)
    {
        if (! p.accepted || (to != 0 && n != to))
            continue;
        auto& wire = p.perms.listener ? none : full;
        if (wire.isVoid())
        {
            wire = object();
            set (wire, "t", "shared");
            set (wire, "plugins", p.perms.listener ? juce::var (juce::Array<juce::var>()) : sharedToVar (shared));
        }
        sendJson (n, wire);
    }
    if (to != 0 && ! hostedBusy.empty())
    {
        auto m = object();
        set (m, "t", "rbusy");
        set (m, "keys", keysToVar (hostedBusy));
        sendJson (to, m);
    }
}

juce::StringArray LiveSessionManager::sharedPluginUsers (const std::string& pluginId) const
{
    juce::StringArray out;
    const auto& song = engine.getSong();
    for (const auto& use : remoteplug::uses (song, me))
        if (use.id == pluginId)
            out.add (nameOf (use.owner) + ": " + str (live::describe (use.place.part, song, [] (const char* s) { return tr (s).toStdString(); })));
    return out;
}

juce::String LiveSessionManager::playedByName (const PluginRef& ref) const
{
    if (! isActive() || ref.playedBy == 0 || ref.playedBy == me)
        return {};
    return nameOf (ref.playedBy);
}

// --- the host: a guest's proposal ------------------------------------------------------------------------------------

bool LiveSessionManager::admitHosted (std::uint32_t tag, live::Proposal& proposal)
{
    // no state and only clean values for hosted plugins, whatever the guest's computer sent
    remoteplug::sanitize (proposal.patch.carrier, session::hostTag);
    const auto edits = remoteplug::valueEdits (authority->confirmed(), proposal.patch);
    if (valueLimiter.allow (tag, juce::Time::getMillisecondCounterHiRes(), edits))
        return true;
    ++stats.valueEditsRefused;
    return false;
}

// --- the host: rendering other people's parts ------------------------------------------------------------------------

void LiveSessionManager::addHostedRenders (std::vector<live::Key>& keys)
{
    if (! host || shared.empty())
        return;
    const auto mine = remoteplug::toRender (
        engine.getSong(), allNeed, me, [this] (const PluginRef& r) { return missingHere (r); },
        [this] (const std::string& id) { return remoteplug::find (shared, id) != nullptr; });
    for (const auto& k : mine)
        if (std::find (keys.begin(), keys.end(), k) == keys.end())
            keys.push_back (k);
}

void LiveSessionManager::setHostedBusy (std::set<live::Key> keys)
{
    // only the parts rendered for someone else are news to them
    const auto& song = engine.getSong();
    for (auto it = keys.begin(); it != keys.end();)
        it = live::ownerOf (*it, song) == me ? keys.erase (it) : std::next (it);
    if (keys == hostedBusy)
        return;
    hostedBusy = std::move (keys);
    hostedStateCache.clear();
    if (host)
    {
        auto m = object();
        set (m, "t", "rbusy");
        set (m, "keys", keysToVar (hostedBusy));
        sendToGuests (m);
    }
    if (onChange)
        onChange();
}

// --- a guest: what the host says -------------------------------------------------------------------------------------

bool LiveSessionManager::handleRemotePlugins (int, const juce::String& type, const juce::var& m)
{
    if (host)
        return false;
    if (type == "shared")
    {
        auto list = sharedFromVar (m["plugins"]);
        if (list != shared)
        {
            shared = std::move (list);
            hostedStateCache.clear();
            if (onChange)
                onChange();
        }
        return true;
    }
    if (type == "rbusy")
    {
        hostedBusy = keysFromVar (m["keys"]);
        hostedStateCache.clear();
        if (onChange)
            onChange();
        return true;
    }
    return false;
}

LiveSessionManager::HostedState LiveSessionManager::hostedState (const remoteplug::Place& place) const
{
    const auto& song = engine.getSong();
    const auto* ref = remoteplug::refAt (song, place);
    if (ref == nullptr || ref->playedBy == 0 || ! isActive())
        return HostedState::none;
    if (state != State::live)
        return HostedState::hostAway;
    if (remoteplug::find (shared, ref->id) == nullptr)
        return HostedState::notShared;
    if (hostedBusy.count (place.part) > 0)
        return HostedState::updating;
    const auto stamp = engine.getSongStamp();
    if (auto it = hostedStateCache.find (place); it != hostedStateCache.end() && it->second.first == stamp)
        return it->second.second;
    auto result = HostedState::upToDate;
    if (place.slot >= 0)
    {
        // an insert's stem depends on everything that plays into it: its hash is the renderer's to compute
        result = standins::find (song.mixer[(std::size_t) place.part.a].standIns, 0, false) != nullptr ? HostedState::upToDate : HostedState::updating;
    }
    else if (const auto c = channelOf (song, place.part.a); c >= 0)
    {
        const auto& channel = song.channels[(std::size_t) c];
        bool notes = false;
        for (const auto& p : song.patterns)
            notes = notes || std::any_of (p.notes.begin(), p.notes.end(), [&] (const Note& n) { return n.channel == c; });
        if (! notes)
            result = HostedState::noNotes;
        else if (standins::needsTimeline (song, c))
            result = standins::find (channel.standIns, 0, false) != nullptr ? HostedState::upToDate : HostedState::updating;
        else
            for (int p = 0; p < (int) song.patterns.size() && result == HostedState::upToDate; ++p)
            {
                const auto& pattern = song.patterns[(std::size_t) p];
                if (std::none_of (pattern.notes.begin(), pattern.notes.end(), [&] (const Note& n) { return n.channel == c; }))
                    continue;
                // (every computer computes the same hash from the same wire state: a stale render is known here too)
                const auto* stem = standins::find (channel.standIns, pattern.uid, false);
                if (stem == nullptr || stem->sourceHash != sessionpack::sourceHash (song, false, c, p, stemRate))
                    result = HostedState::updating;
            }
    }
    hostedStateCache[place] = { stamp, result };
    return result;
}

// --- the host: the running instances ---------------------------------------------------------------------------------

void LiveSessionManager::captureHostedValuesAt (const remoteplug::Place& place)
{
    const auto* ref = remoteplug::refAt (engine.getSong(), place);
    if (ref == nullptr || ref->playedBy != me)
        return;
    const auto running = runningAt (engine, place);
    if (running.plugin == nullptr)
        return;
    auto values = PluginHost::readValues (*running.plugin, remoteplug::find (shared, ref->id));
    // (a value the plugin rounded a little is still the one the song has)
    for (auto& [index, value] : values)
        if (const auto had = remoteplug::valueOf (*ref, index, -1.0f); had >= 0.0f && std::abs (had - value) < sameValue)
            value = had;
    if (values == ref->values)
        return;
    pushedValues[place] = values; // (the instance has them already)
    engine.editSong ([&] (Song& s) {
                         if (auto* r = remoteplug::refAt (s, place))
                             r->values = values;
                     },
                     false);
}

void LiveSessionManager::captureHostedValues()
{
    if (! host || ! isActive())
        return;
    std::vector<remoteplug::Place> places;
    remoteplug::forEachRef (engine.getSong(), [&] (const PluginRef& ref, const remoteplug::Place& place) {
        if (ref.playedBy == me)
            places.push_back (place);
    });
    for (const auto& place : places)
        captureHostedValuesAt (place);
}

void LiveSessionManager::hostedTick()
{
    engine.getPluginHost().setSessionTag (isActive() ? me : 0);
    const auto now = juce::Time::currentTimeMillis();
    if (! host || now - lastHostedMs < hostedTickMs)
        return;
    lastHostedMs = now;
    std::vector<std::pair<remoteplug::Place, PluginRef>> mine;
    remoteplug::forEachRef (engine.getSong(), [&] (const PluginRef& ref, const remoteplug::Place& place) {
        if (ref.playedBy == me)
            mine.push_back ({ place, ref });
    });
    std::set<remoteplug::Place> seen;
    std::vector<remoteplug::Place> closed;
    for (const auto& [place, ref] : mine)
    {
        seen.insert (place);
        const auto running = runningAt (engine, place);
        if (running.plugin == nullptr)
        {
            pushedValues.erase (place);
            continue;
        }
        // the values the session has for it go onto the running instance (the ones that changed since the last look);
        // only for a plugin that is shared: an un-shared one keeps playing as it is
        if (remoteplug::find (shared, ref.id) != nullptr)
        {
            auto& pushed = pushedValues[place];
            if (pushed != ref.values)
            {
                const auto count = running.plugin->getParameters().size();
                for (const auto& [index, value] : ref.values)
                {
                    const auto had = std::lower_bound (pushed.begin(), pushed.end(), index, [] (const auto& p, int i) { return p.first < i; });
                    if (index < count && (had == pushed.end() || had->first != index || had->second != value))
                        running.setParameter (index, value);
                }
                pushed = ref.values;
            }
        }
        // the host changed it in the plugin's own window: when that closes, what it changed becomes values
        if (hostedEditorOpen)
        {
            const bool open = hostedEditorOpen (*running.plugin);
            auto& was = hostedOpen[place];
            if (was && ! open)
                closed.push_back (place);
            was = open;
        }
    }
    for (auto it = pushedValues.begin(); it != pushedValues.end();)
        it = seen.count (it->first) == 0 ? pushedValues.erase (it) : std::next (it);
    for (auto it = hostedOpen.begin(); it != hostedOpen.end();)
        it = seen.count (it->first) == 0 ? hostedOpen.erase (it) : std::next (it);
    for (const auto& place : closed)
        captureHostedValuesAt (place);
}

// --- the end ---------------------------------------------------------------------------------------------------------

void LiveSessionManager::endHosted (bool wasHost)
{
    shared.clear();
    hostedBusy.clear();
    pushedValues.clear();
    hostedOpen.clear();
    hostedStateCache.clear();
    valueLimiter = remoteplug::Limiter();
    bool any = false;
    remoteplug::forEachRef (engine.getSong(), [&] (const PluginRef& ref, const remoteplug::Place&) { any = any || ref.playedBy != 0 || ! ref.values.empty(); });
    if (any && ! abandoned)
    {
        if (wasHost)
        {
            // the host's project keeps the plugins as they sound now: their real state replaces the session's values
            engine.editSong ([] (Song& s) { remoteplug::endSession (s, false); }, false);
            engine.capturePluginStates();
        }
        // (a guest keeps the audio stand-ins only: the plugin and its settings stayed on the host)
        engine.editSong ([] (Song& s) { remoteplug::endSession (s, true); }, false);
    }
    engine.getPluginHost().setSessionTag (0);
}
} // namespace lynx
