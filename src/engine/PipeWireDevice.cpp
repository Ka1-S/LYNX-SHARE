#include "engine/PipeWireDevice.h"

#if JUCE_LINUX

 #include "core/Preferences.h"

 #include <algorithm>
 #include <array>
 #include <atomic>
 #include <cerrno>
 #include <cstring>
 #include <dlfcn.h>
 #include <map>
 #include <tuple>
 #include <unistd.h>
 #include <vector>

 // PipeWire / SPA headers (MIT, third_party/pipewire): only declarations, inline helpers and constants are used; every
 // libpipewire function is called through a pointer resolved with dlsym, so LYNX starts without PipeWire installed.
 #if defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wpedantic"
  #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  #pragma GCC diagnostic ignored "-Wold-style-cast"
  #pragma GCC diagnostic ignored "-Wcast-qual"
  #pragma GCC diagnostic ignored "-Wsign-conversion"
  #pragma GCC diagnostic ignored "-Wconversion"
  #pragma GCC diagnostic ignored "-Wshadow"
  #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
  #pragma GCC diagnostic ignored "-Wunused-parameter"
  #pragma GCC diagnostic ignored "-Wswitch-enum"
  #pragma GCC diagnostic ignored "-Wcast-align"
 #endif
 #include <pipewire/pipewire.h>
 #include <pipewire/extensions/metadata.h>
 #include <spa/param/audio/format-utils.h>
 #include <spa/node/io.h>
 #if defined(__GNUC__)
  #pragma GCC diagnostic pop
 #endif

namespace lynx
{
namespace
{
// --- libpipewire, loaded at run time ------------------------------------------------------------------------------
struct Api
{
    void* library = nullptr;
    int major = 0, minor = 0, micro = 0;

    decltype (&::pw_init) init = nullptr;
    decltype (&::pw_get_library_version) getLibraryVersion = nullptr;
    decltype (&::pw_thread_loop_new) threadLoopNew = nullptr;
    decltype (&::pw_thread_loop_destroy) threadLoopDestroy = nullptr;
    decltype (&::pw_thread_loop_start) threadLoopStart = nullptr;
    decltype (&::pw_thread_loop_stop) threadLoopStop = nullptr;
    decltype (&::pw_thread_loop_lock) threadLoopLock = nullptr;
    decltype (&::pw_thread_loop_unlock) threadLoopUnlock = nullptr;
    decltype (&::pw_thread_loop_signal) threadLoopSignal = nullptr;
    decltype (&::pw_thread_loop_timed_wait) threadLoopTimedWait = nullptr;
    decltype (&::pw_thread_loop_get_loop) threadLoopGetLoop = nullptr;
    decltype (&::pw_context_new) contextNew = nullptr;
    decltype (&::pw_context_destroy) contextDestroy = nullptr;
    decltype (&::pw_context_connect) contextConnect = nullptr;
    decltype (&::pw_core_disconnect) coreDisconnect = nullptr;
    decltype (&::pw_proxy_destroy) proxyDestroy = nullptr;
    decltype (&::pw_properties_new) propertiesNew = nullptr;
    decltype (&::pw_properties_set) propertiesSet = nullptr;
    decltype (&::pw_stream_new) streamNew = nullptr;
    decltype (&::pw_stream_destroy) streamDestroy = nullptr;
    decltype (&::pw_stream_add_listener) streamAddListener = nullptr;
    decltype (&::pw_stream_connect) streamConnect = nullptr;
    decltype (&::pw_stream_disconnect) streamDisconnect = nullptr;
    decltype (&::pw_stream_set_active) streamSetActive = nullptr;
    decltype (&::pw_stream_dequeue_buffer) streamDequeueBuffer = nullptr;
    decltype (&::pw_stream_queue_buffer) streamQueueBuffer = nullptr;
    decltype (&::pw_stream_get_time_n) streamGetTimeN = nullptr; // since 0.3.50 (optional)

    bool atLeast (int ma, int mi, int mc) const
    {
        return std::tie (major, minor, micro) >= std::tie (ma, mi, mc);
    }

    static Api* get()
    {
        static Api* api = [] () -> Api* {
            if (juce::SystemStats::getEnvironmentVariable ("LYNX_NO_PIPEWIRE", {}).isNotEmpty())
                return nullptr;
            auto* lib = dlopen ("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
            if (lib == nullptr)
                return nullptr;
            auto* a = new Api();
            a->library = lib;
            auto ok = true;
            auto load = [&] (auto& fn, const char* name, bool required = true) {
                fn = reinterpret_cast<std::remove_reference_t<decltype (fn)>> (dlsym (lib, name));
                if (fn == nullptr && required)
                    ok = false;
            };
            load (a->init, "pw_init");
            load (a->getLibraryVersion, "pw_get_library_version");
            load (a->threadLoopNew, "pw_thread_loop_new");
            load (a->threadLoopDestroy, "pw_thread_loop_destroy");
            load (a->threadLoopStart, "pw_thread_loop_start");
            load (a->threadLoopStop, "pw_thread_loop_stop");
            load (a->threadLoopLock, "pw_thread_loop_lock");
            load (a->threadLoopUnlock, "pw_thread_loop_unlock");
            load (a->threadLoopSignal, "pw_thread_loop_signal");
            load (a->threadLoopTimedWait, "pw_thread_loop_timed_wait");
            load (a->threadLoopGetLoop, "pw_thread_loop_get_loop");
            load (a->contextNew, "pw_context_new");
            load (a->contextDestroy, "pw_context_destroy");
            load (a->contextConnect, "pw_context_connect");
            load (a->coreDisconnect, "pw_core_disconnect");
            load (a->proxyDestroy, "pw_proxy_destroy");
            load (a->propertiesNew, "pw_properties_new");
            load (a->propertiesSet, "pw_properties_set");
            load (a->streamNew, "pw_stream_new");
            load (a->streamDestroy, "pw_stream_destroy");
            load (a->streamAddListener, "pw_stream_add_listener");
            load (a->streamConnect, "pw_stream_connect");
            load (a->streamDisconnect, "pw_stream_disconnect");
            load (a->streamSetActive, "pw_stream_set_active");
            load (a->streamDequeueBuffer, "pw_stream_dequeue_buffer");
            load (a->streamQueueBuffer, "pw_stream_queue_buffer");
            load (a->streamGetTimeN, "pw_stream_get_time_n", false);
            if (! ok)
            {
                dlclose (lib);
                delete a;
                return nullptr;
            }
            std::sscanf (a->getLibraryVersion(), "%d.%d.%d", &a->major, &a->minor, &a->micro);
            a->init (nullptr, nullptr);
            return a;
        }();
        return api;
    }
};

const char* lookup (const spa_dict* props, const char* key)
{
    return props != nullptr ? spa_dict_lookup (props, key) : nullptr;
}

std::string text (const char* s) { return s != nullptr ? std::string (s) : std::string(); }

// --- one connection to the daemon: the device list, the defaults, and the streams' core -----------------------------
struct Connection
{
    struct Node
    {
        std::string name, description;
        bool sink = false, source = false;
    };
    struct Port
    {
        std::uint32_t node = 0;
        bool input = false, monitor = false;
        std::string channel;
        int index = -1; // port.id: the port's place on its node (the registry ids can come in any order)
    };

    Api& api;
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    spa_hook coreListener {}, registryListener {};
    struct BoundMetadata
    {
        std::uint32_t id = 0;
        std::string name;
        pw_metadata* proxy = nullptr;
        spa_hook listener {};
        Connection* owner = nullptr;
    };
    std::vector<std::unique_ptr<BoundMetadata>> metadata;

    // guarded by the loop lock
    std::map<std::uint32_t, Node> nodes;
    std::map<std::uint32_t, Port> ports;
    std::string defaultSink, defaultSource;
    int clockRate = 0;
    int pendingSeq = -1;
    bool synced = false;
    std::atomic<bool> broken { false };
    std::atomic<std::uint32_t> generation { 0 }; // bumps when sinks / sources / defaults change
    std::atomic<int> lastBlock { 0 };             // the block size the last device opened with

    explicit Connection (Api& a) : api (a) {}

    bool connect()
    {
        loop = api.threadLoopNew ("lynx-pipewire", nullptr);
        if (loop == nullptr)
            return false;
        context = api.contextNew (api.threadLoopGetLoop (loop), nullptr, 0);
        if (context == nullptr || api.threadLoopStart (loop) < 0)
            return false;

        api.threadLoopLock (loop);
        core = api.contextConnect (context, api.propertiesNew (PW_KEY_APP_NAME, "LYNX", nullptr), 0);
        if (core == nullptr)
        {
            api.threadLoopUnlock (loop);
            return false;
        }
        static const pw_core_events coreEvents = [] {
            pw_core_events e {};
            e.version = PW_VERSION_CORE_EVENTS;
            e.done = [] (void* data, std::uint32_t id, int seq) {
                auto& c = *static_cast<Connection*> (data);
                if (id == PW_ID_CORE && seq == c.pendingSeq)
                {
                    c.synced = true;
                    c.api.threadLoopSignal (c.loop, false);
                }
            };
            e.error = [] (void* data, std::uint32_t id, int, int res, const char*) {
                auto& c = *static_cast<Connection*> (data);
                if (id == PW_ID_CORE && res == -EPIPE)
                {
                    c.broken = true;
                    ++c.generation;
                    c.api.threadLoopSignal (c.loop, false);
                }
            };
            return e;
        }();
        pw_core_add_listener (core, &coreListener, &coreEvents, this);

        static const pw_registry_events registryEvents = [] {
            pw_registry_events e {};
            e.version = PW_VERSION_REGISTRY_EVENTS;
            e.global = [] (void* data, std::uint32_t id, std::uint32_t, const char* type, std::uint32_t, const spa_dict* props) {
                static_cast<Connection*> (data)->added (id, type, props);
            };
            e.global_remove = [] (void* data, std::uint32_t id) { static_cast<Connection*> (data)->removed (id); };
            return e;
        }();
        registry = pw_core_get_registry (core, PW_VERSION_REGISTRY, 0);
        pw_registry_add_listener (registry, &registryListener, &registryEvents, this);

        // a round trip: every existing object has been announced (and the metadata read) when it comes back
        const auto ok = sync (3);
        api.threadLoopUnlock (loop);
        return ok;
    }

    // with the loop locked
    bool sync (int seconds)
    {
        synced = false;
        pendingSeq = pw_core_sync (core, PW_ID_CORE, 0);
        const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) seconds * 1000;
        while (! synced && ! broken && juce::Time::getMillisecondCounter() < until)
            api.threadLoopTimedWait (loop, 1);
        if (synced) // a second round trip lets the bound metadata report its properties
        {
            synced = false;
            pendingSeq = pw_core_sync (core, PW_ID_CORE, 0);
            while (! synced && ! broken && juce::Time::getMillisecondCounter() < until)
                api.threadLoopTimedWait (loop, 1);
        }
        return synced && ! broken;
    }

    ~Connection()
    {
        if (loop == nullptr)
            return;
        api.threadLoopLock (loop);
        for (auto& m : metadata)
            if (m->proxy != nullptr)
                api.proxyDestroy (reinterpret_cast<pw_proxy*> (m->proxy));
        metadata.clear();
        if (registry != nullptr)
            api.proxyDestroy (reinterpret_cast<pw_proxy*> (registry));
        if (core != nullptr)
            api.coreDisconnect (core);
        api.threadLoopUnlock (loop);
        api.threadLoopStop (loop);
        if (context != nullptr)
            api.contextDestroy (context);
        api.threadLoopDestroy (loop);
    }

    void lock() { api.threadLoopLock (loop); }
    void unlock() { api.threadLoopUnlock (loop); }

    void added (std::uint32_t id, const char* type, const spa_dict* props)
    {
        if (std::strcmp (type, PW_TYPE_INTERFACE_Node) == 0)
        {
            const auto mediaClass = text (lookup (props, PW_KEY_MEDIA_CLASS));
            Node n;
            n.sink = mediaClass == "Audio/Sink" || mediaClass == "Audio/Duplex";
            n.source = mediaClass == "Audio/Source" || mediaClass == "Audio/Duplex" || mediaClass == "Audio/Source/Virtual";
            if (! n.sink && ! n.source)
                return;
            n.name = text (lookup (props, PW_KEY_NODE_NAME));
            n.description = text (lookup (props, PW_KEY_NODE_DESCRIPTION));
            if (n.description.empty())
                n.description = text (lookup (props, PW_KEY_NODE_NICK));
            if (n.description.empty())
                n.description = n.name;
            nodes[id] = n;
            ++generation;
        }
        else if (std::strcmp (type, PW_TYPE_INTERFACE_Port) == 0)
        {
            Port p;
            p.node = (std::uint32_t) std::atoi (text (lookup (props, PW_KEY_NODE_ID)).c_str());
            p.input = text (lookup (props, PW_KEY_PORT_DIRECTION)) == "in";
            p.monitor = text (lookup (props, PW_KEY_PORT_MONITOR)) == "true";
            p.channel = text (lookup (props, PW_KEY_AUDIO_CHANNEL));
            if (const auto index = text (lookup (props, "port.id")); ! index.empty())
                p.index = std::atoi (index.c_str());
            ports[id] = p;
        }
        else if (std::strcmp (type, PW_TYPE_INTERFACE_Metadata) == 0)
        {
            const auto name = text (lookup (props, PW_KEY_METADATA_NAME));
            if (name != "default" && name != "settings")
                return;
            auto m = std::make_unique<BoundMetadata>();
            m->id = id;
            m->name = name;
            m->owner = this;
            m->proxy = static_cast<pw_metadata*> (pw_registry_bind (registry, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
            if (m->proxy == nullptr)
                return;
            static const pw_metadata_events metadataEvents = [] {
                pw_metadata_events e {};
                e.version = PW_VERSION_METADATA_EVENTS;
                e.property = [] (void* data, std::uint32_t subject, const char* key, const char*, const char* value) -> int {
                    auto& bound = *static_cast<BoundMetadata*> (data);
                    bound.owner->metadataChanged (bound.name, subject, key, value);
                    return 0;
                };
                return e;
            }();
            pw_metadata_add_listener (m->proxy, &m->listener, &metadataEvents, m.get());
            metadata.push_back (std::move (m));
        }
    }

    void removed (std::uint32_t id)
    {
        if (nodes.erase (id) > 0)
            ++generation;
        ports.erase (id);
        for (auto it = metadata.begin(); it != metadata.end(); ++it)
            if ((*it)->id == id)
            {
                api.proxyDestroy (reinterpret_cast<pw_proxy*> ((*it)->proxy));
                metadata.erase (it);
                break;
            }
    }

    void metadataChanged (const std::string& which, std::uint32_t subject, const char* key, const char* value)
    {
        if (subject != PW_ID_CORE)
            return;
        if (which == "default")
        {
            if (key == nullptr) // everything cleared
            {
                defaultSink.clear();
                defaultSource.clear();
            }
            else if (std::strcmp (key, "default.audio.sink") == 0)
                defaultSink = prefs::pipewireDefaultName (text (value));
            else if (std::strcmp (key, "default.audio.source") == 0)
                defaultSource = prefs::pipewireDefaultName (text (value));
            else
                return;
            ++generation;
        }
        else if (which == "settings" && key != nullptr && std::strcmp (key, "clock.rate") == 0)
            clockRate = std::atoi (text (value).c_str());
    }

    // the channel positions of a node's ports in one direction (playback: the sink's inputs; capture: the source's outputs)
    std::vector<std::string> channelsOf (std::uint32_t node, bool sinkInputs) const
    {
        // in the node's own port order (port.id), not the registry's: a 16-channel source whose ports were announced
        // out of order gave In 1 = AUX8, In 2 = AUX9... (round 190rj)
        std::vector<std::tuple<int, std::uint32_t, std::string>> found;
        for (const auto& [id, p] : ports)
            if (p.node == node && p.input == sinkInputs && ! p.monitor)
                found.emplace_back (p.index, id, p.channel);
        std::sort (found.begin(), found.end());
        std::vector<std::string> out;
        for (auto& f : found)
            out.push_back (std::get<2> (f));
        return out;
    }
};

// --- what the device list offers ----------------------------------------------------------------------------------
constexpr const char* defaultDeviceName = "System default";

struct Endpoint
{
    juce::String name;                 // as the device list shows it
    std::string node;                  // node.name to connect to; empty = follow the system default
    std::vector<std::string> channels; // channel positions ("FL", "FR"...)
};

struct DeviceList
{
    std::vector<Endpoint> outputs, inputs;

    static DeviceList read (Connection& c)
    {
        DeviceList list;
        c.lock();
        std::vector<std::string> sinkNames, sourceNames;
        std::vector<std::uint32_t> sinks, sources;
        for (const auto& [id, n] : c.nodes)
        {
            if (n.sink)
            {
                sinks.push_back (id);
                sinkNames.push_back (n.description);
            }
            if (n.source)
            {
                sources.push_back (id);
                sourceNames.push_back (n.description);
            }
        }
        const std::vector<std::string> stereo { "FL", "FR" };
        list.outputs.push_back ({ defaultDeviceName, {}, stereo });
        list.inputs.push_back ({ defaultDeviceName, {}, stereo });
        const auto outNames = prefs::uniqueDeviceNames (sinkNames);
        const auto inNames = prefs::uniqueDeviceNames (sourceNames);
        for (std::size_t i = 0; i < sinks.size(); ++i)
        {
            auto channels = c.channelsOf (sinks[i], true);
            list.outputs.push_back ({ juce::String::fromUTF8 (outNames[i].c_str()), c.nodes[sinks[i]].name, channels.empty() ? stereo : channels });
        }
        for (std::size_t i = 0; i < sources.size(); ++i)
        {
            auto channels = c.channelsOf (sources[i], false);
            list.inputs.push_back ({ juce::String::fromUTF8 (inNames[i].c_str()), c.nodes[sources[i]].name, channels.empty() ? stereo : channels });
        }
        c.unlock();
        return list;
    }

    static const Endpoint* find (const std::vector<Endpoint>& list, const juce::String& name)
    {
        for (const auto& e : list)
            if (e.name == name)
                return &e;
        return nullptr;
    }
};

std::uint32_t channelPosition (const std::string& p, std::size_t index)
{
    static const std::pair<const char*, std::uint32_t> known[] = {
        { "MONO", SPA_AUDIO_CHANNEL_MONO }, { "FL", SPA_AUDIO_CHANNEL_FL }, { "FR", SPA_AUDIO_CHANNEL_FR },
        { "FC", SPA_AUDIO_CHANNEL_FC }, { "LFE", SPA_AUDIO_CHANNEL_LFE }, { "SL", SPA_AUDIO_CHANNEL_SL },
        { "SR", SPA_AUDIO_CHANNEL_SR }, { "FLC", SPA_AUDIO_CHANNEL_FLC }, { "FRC", SPA_AUDIO_CHANNEL_FRC },
        { "RC", SPA_AUDIO_CHANNEL_RC }, { "RL", SPA_AUDIO_CHANNEL_RL }, { "RR", SPA_AUDIO_CHANNEL_RR },
        { "TC", SPA_AUDIO_CHANNEL_TC }, { "TFL", SPA_AUDIO_CHANNEL_TFL }, { "TFC", SPA_AUDIO_CHANNEL_TFC },
        { "TFR", SPA_AUDIO_CHANNEL_TFR }, { "TRL", SPA_AUDIO_CHANNEL_TRL }, { "TRC", SPA_AUDIO_CHANNEL_TRC },
        { "TRR", SPA_AUDIO_CHANNEL_TRR }
    };
    for (const auto& [name, id] : known)
        if (p == name)
            return id;
    if (p.rfind ("AUX", 0) == 0 && p.size() > 3)
        return SPA_AUDIO_CHANNEL_AUX0 + (std::uint32_t) std::atoi (p.c_str() + 3);
    return SPA_AUDIO_CHANNEL_AUX0 + (std::uint32_t) index;
}

// --- the device ---------------------------------------------------------------------------------------------------
class PipeWireDevice;

// The devices a type created and that still exist (message thread), so a restarted daemon can take them back
struct DeviceRegistry
{
    std::vector<PipeWireDevice*> devices;
};

class PipeWireDevice final : public juce::AudioIODevice
{
public:
    PipeWireDevice (std::shared_ptr<Connection> c, std::shared_ptr<DeviceRegistry> r, const juce::String& outName, const juce::String& inName,
                    Endpoint out, Endpoint in, bool hasOutput, bool hasInput)
        : juce::AudioIODevice (outName.isNotEmpty() ? outName : inName, pipeWireTypeName),
          connection (std::move (c)), registry (std::move (r)), output (std::move (out)), input (std::move (in)), wantOutput (hasOutput), wantInput (hasInput)
    {
        registry->devices.push_back (this);
    }

    ~PipeWireDevice() override
    {
        close();
        std::erase (registry->devices, this);
    }

    juce::StringArray getOutputChannelNames() override { return names (wantOutput ? output.channels : std::vector<std::string>()); }
    juce::StringArray getInputChannelNames() override { return names (wantInput ? input.channels : std::vector<std::string>()); }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 32, 64, 128, 256, 512, 1024, 2048 }; }
    // the last block size opened (when a chosen device is unplugged, the device manager falls back to "System default"
    // with the default size: it keeps the user's buffer that way)
    int getDefaultBufferSize() override { return connection->lastBlock > 0 ? connection->lastBlock.load() : 256; }

    juce::String open (const juce::BigInteger& inputChannels, const juce::BigInteger& outputChannels, double sampleRate, int bufferSizeSamples) override
    {
        close();
        lastError.clear();
        if (connection->broken)
            return lastError = "The PipeWire daemon is not running any more";

        connection->lock();
        const auto graphRate = connection->clockRate;
        connection->unlock();
        rate = sampleRate > 0 ? (int) sampleRate : (graphRate > 0 ? graphRate : 48000);
        block = bufferSizeSamples > 0 ? bufferSizeSamples : getDefaultBufferSize();
        block = juce::jlimit (16, 8192, block);
        connection->lastBlock = block;
        xruns = 0;

        activeOut.clear();
        activeIn.clear();
        for (int i = 0; wantOutput && i < (int) output.channels.size(); ++i)
            if (outputChannels[i])
                activeOut.setBit (i);
        for (int i = 0; wantInput && i < (int) input.channels.size(); ++i)
            if (inputChannels[i])
                activeIn.setBit (i);

        lastError = openStreams();
        if (lastError.isNotEmpty())
        {
            close();
            return lastError;
        }
        isOpenFlag = true;
        return {};
    }

    void close() override
    {
        stop();
        destroyStreams();
        isOpenFlag = false;
    }

    // The daemon restarted: the same streams again on the new connection, playing on (message thread)
    void reconnect (std::shared_ptr<Connection> fresh)
    {
        if (! isOpenFlag || fresh == connection)
            return;
        destroyStreams(); // the old core is gone: only the client-side objects are freed
        connection = std::move (fresh);
        connection->lastBlock = block;
        lastError = openStreams();
        if (lastError.isEmpty() && callback != nullptr)
            setActive (true);
    }

    bool usesBrokenConnection() const { return isOpenFlag && connection->broken; }

    bool isOpen() override { return isOpenFlag; }

    void start (juce::AudioIODeviceCallback* cb) override
    {
        if (! isOpenFlag || cb == nullptr || cb == callback)
            return;
        stop();
        cb->audioDeviceAboutToStart (this);
        {
            const juce::SpinLock::ScopedLockType sl (callbackLock);
            callback = cb;
        }
        setActive (true);
    }

    void stop() override
    {
        juce::AudioIODeviceCallback* old = nullptr;
        {
            const juce::SpinLock::ScopedLockType sl (callbackLock);
            old = callback;
            callback = nullptr;
        }
        if (old != nullptr)
        {
            setActive (false);
            old->audioDeviceStopped();
        }
    }

    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return lastError; }
    int getCurrentBufferSizeSamples() override { return block; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOut; }
    juce::BigInteger getActiveInputChannels() const override { return activeIn; }
    int getOutputLatencyInSamples() override { return outputDelay.load(); }
    int getInputLatencyInSamples() override { return inputDelay.load() + (capture.stream != nullptr ? block : 0); }
    int getXRunCount() const noexcept override { return xruns.load(); }

private:
    static constexpr int maxFrames = 8192;
    static constexpr std::size_t fifoFrames = 32768;

    struct Stream
    {
        PipeWireDevice* owner = nullptr;
        bool isPlayback = true;
        pw_stream* stream = nullptr;
        spa_hook listener {};
        pw_stream_state state = PW_STREAM_STATE_UNCONNECTED;
        juce::String error;
        std::atomic<spa_io_position*> position { nullptr };
        int timeCounter = 0;
    };

    static juce::StringArray names (const std::vector<std::string>& channels)
    {
        juce::StringArray out;
        for (std::size_t i = 0; i < channels.size(); ++i)
            out.add (juce::String::fromUTF8 (prefs::pipewireChannelName (channels[i], (int) i).c_str()));
        return out;
    }

    juce::String openStreams()
    {
        auto& api = connection->api;
        // scratch for the engine's blocks: the input FIFO, the input block
        const auto inCount = (std::size_t) activeIn.countNumberOfSetBits();
        fifo.assign (inCount, std::vector<float> (fifoFrames, 0.0f));
        inBlock.assign (inCount, std::vector<float> ((std::size_t) maxFrames, 0.0f));
        fifoWrite = fifoRead = 0;
        captureStarted = false;

        juce::String error;
        connection->lock();
        if (! activeOut.isZero())
            makeStream (true);
        if (! activeIn.isZero())
            makeStream (false);
        // wait until PipeWire has accepted the streams (or failed)
        const auto until = juce::Time::getMillisecondCounter() + 3000;
        auto pending = [this] { return (playback.stream != nullptr && playback.state == PW_STREAM_STATE_CONNECTING)
                                       || (capture.stream != nullptr && capture.state == PW_STREAM_STATE_CONNECTING); };
        while (pending() && ! connection->broken && juce::Time::getMillisecondCounter() < until)
            api.threadLoopTimedWait (connection->loop, 1);
        for (auto* st : { &playback, &capture })
            if (st->stream != nullptr && st->state == PW_STREAM_STATE_ERROR)
                error = "PipeWire: " + st->error;
        if ((! activeOut.isZero() && playback.stream == nullptr) || (! activeIn.isZero() && capture.stream == nullptr))
            error = "PipeWire: the stream could not be created";
        connection->unlock();
        return error;
    }

    void destroyStreams()
    {
        if (playback.stream == nullptr && capture.stream == nullptr)
            return;
        auto& api = connection->api;
        connection->lock();
        for (auto* st : { &playback, &capture })
            if (st->stream != nullptr)
            {
                api.streamDisconnect (st->stream);
                api.streamDestroy (st->stream);
                st->stream = nullptr;
                st->position = nullptr;
            }
        connection->unlock();
    }

    // with the loop locked
    Stream& makeStream (bool isPlayback)
    {
        auto& api = connection->api;
        auto& s = isPlayback ? playback : capture;
        const auto& endpoint = isPlayback ? output : input;
        s.owner = this;
        s.isPlayback = isPlayback;
        s.state = PW_STREAM_STATE_CONNECTING;
        s.error.clear();
        s.position = nullptr;

        auto* props = api.propertiesNew (PW_KEY_MEDIA_TYPE, "Audio",
                                         PW_KEY_MEDIA_CATEGORY, isPlayback ? "Playback" : "Capture",
                                         PW_KEY_MEDIA_ROLE, "Production",
                                         PW_KEY_APP_NAME, "LYNX",
                                         PW_KEY_NODE_NAME, "LYNX",
                                         PW_KEY_NODE_DESCRIPTION, isPlayback ? "LYNX" : "LYNX (input)",
                                         nullptr);
        api.propertiesSet (props, PW_KEY_NODE_LATENCY, prefs::pipewireLatency (block, rate).c_str());
        api.propertiesSet (props, "node.rate", ("1/" + std::to_string (rate)).c_str());
        // the input and output streams run in the same graph cycle, on the same driver (as pw-loopback does)
        api.propertiesSet (props, "node.group", ("lynx." + std::to_string ((long) getpid())).c_str());
        if (! endpoint.node.empty())
            api.propertiesSet (props, "target.object", endpoint.node.c_str());

        s.stream = api.streamNew (connection->core, isPlayback ? "LYNX output" : "LYNX input", props);
        if (s.stream == nullptr)
        {
            s.state = PW_STREAM_STATE_ERROR;
            s.error = "pw_stream_new failed";
            return s;
        }

        static const pw_stream_events events = [] {
            pw_stream_events e {};
            e.version = PW_VERSION_STREAM_EVENTS;
            e.state_changed = [] (void* data, pw_stream_state, pw_stream_state state, const char* error) {
                auto& st = *static_cast<Stream*> (data);
                st.state = state;
                if (state == PW_STREAM_STATE_ERROR)
                    st.error = juce::String::fromUTF8 (error != nullptr ? error : "error");
                st.owner->connection->api.threadLoopSignal (st.owner->connection->loop, false);
            };
            e.io_changed = [] (void* data, std::uint32_t id, void* area, std::uint32_t) {
                if (id == SPA_IO_Position)
                    static_cast<Stream*> (data)->position = static_cast<spa_io_position*> (area);
            };
            e.process = [] (void* data) {
                auto& st = *static_cast<Stream*> (data);
                if (st.isPlayback)
                    st.owner->processPlayback();
                else
                    st.owner->processCapture();
            };
            return e;
        }();
        api.streamAddListener (s.stream, &s.listener, &events, &s);

        spa_audio_info_raw info {};
        info.format = SPA_AUDIO_FORMAT_F32P;
        info.rate = (std::uint32_t) rate;
        info.channels = (std::uint32_t) endpoint.channels.size();
        for (std::size_t i = 0; i < endpoint.channels.size() && i < SPA_AUDIO_MAX_CHANNELS; ++i)
            info.position[i] = channelPosition (endpoint.channels[i], i);
        std::uint8_t podBuffer[2048];
        spa_pod_builder builder {};
        builder.data = podBuffer;
        builder.size = sizeof (podBuffer);
        const spa_pod* params[1] = { spa_format_audio_raw_build (&builder, SPA_PARAM_EnumFormat, &info) };

        const auto flags = (pw_stream_flags) (PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS
                                              | PW_STREAM_FLAG_INACTIVE);
        if (api.streamConnect (s.stream, isPlayback ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT, PW_ID_ANY, flags, params, 1) < 0)
        {
            s.state = PW_STREAM_STATE_ERROR;
            s.error = "pw_stream_connect failed";
        }
        return s;
    }

    void setActive (bool active)
    {
        auto& api = connection->api;
        connection->lock();
        for (auto* s : { &playback, &capture })
            if (s->stream != nullptr)
                api.streamSetActive (s->stream, active);
        connection->unlock();
    }

    // How many frames this cycle wants (PipeWire's suggestion, else the graph quantum at our rate)
    std::uint32_t framesWanted (const Stream& s, const pw_buffer* b, std::uint32_t capacity) const noexcept
    {
        std::uint32_t n = 0;
        if (connection->api.atLeast (0, 3, 50) && b->requested > 0)
            n = (std::uint32_t) b->requested;
        else if (auto* pos = s.position.load (std::memory_order_relaxed))
        {
            const auto graphRate = pos->clock.rate.denom > 0 ? pos->clock.rate.denom : (std::uint32_t) rate;
            n = (std::uint32_t) ((std::uint64_t) pos->clock.duration * (std::uint64_t) rate / graphRate);
        }
        if (n == 0)
            n = (std::uint32_t) block;
        return std::min ({ n, capacity, (std::uint32_t) maxFrames });
    }

    void noteDelay (Stream& s, std::atomic<int>& into) noexcept
    {
        if (connection->api.streamGetTimeN == nullptr || (s.timeCounter++ & 63) != 0)
            return;
        pw_time t {};
        if (connection->api.streamGetTimeN (s.stream, &t, sizeof (t)) == 0 && t.rate.denom > 0)
            into = (int) juce::jmax<std::int64_t> (0, t.delay * rate * (std::int64_t) t.rate.num / (std::int64_t) t.rate.denom);
    }

    // PipeWire's real-time data thread
    void processCapture() noexcept
    {
        auto& api = connection->api;
        auto* b = api.streamDequeueBuffer (capture.stream);
        if (b == nullptr)
            return;
        auto* buf = b->buffer;
        const auto channels = fifo.size();
        if (buf->n_datas > 0 && buf->datas[0].chunk != nullptr)
        {
            const auto n = std::min<std::uint32_t> (buf->datas[0].chunk->size / sizeof (float), (std::uint32_t) maxFrames);
            const auto w = fifoWrite.load (std::memory_order_relaxed);
            const auto r = fifoRead.load (std::memory_order_acquire);
            if (w - r + n > fifoFrames)
                ++xruns; // the engine stopped reading: drop this block
            else
            {
                std::size_t slot = 0;
                for (int ch = 0; ch < (int) input.channels.size() && slot < channels; ++ch)
                {
                    if (! activeIn[ch])
                        continue;
                    const auto* src = (std::uint32_t) ch < buf->n_datas && buf->datas[ch].data != nullptr && buf->datas[ch].chunk != nullptr
                                          ? reinterpret_cast<const float*> (static_cast<const std::uint8_t*> (buf->datas[ch].data) + buf->datas[ch].chunk->offset)
                                          : nullptr;
                    auto& ring = fifo[slot++];
                    for (std::uint32_t i = 0; i < n; ++i)
                        ring[(w + i) % fifoFrames] = src != nullptr ? src[i] : 0.0f;
                }
                fifoWrite.store (w + n, std::memory_order_release);
                captureStarted = true;
            }
        }
        noteDelay (capture, inputDelay);
        api.streamQueueBuffer (capture.stream, b);
    }

    void processPlayback() noexcept
    {
        auto& api = connection->api;
        auto* b = api.streamDequeueBuffer (playback.stream);
        if (b == nullptr)
        {
            ++xruns; // no free buffer: this cycle is lost
            return;
        }
        auto* buf = b->buffer;
        const auto channels = (int) output.channels.size();
        if ((int) buf->n_datas < channels || buf->datas[0].data == nullptr)
        {
            api.streamQueueBuffer (playback.stream, b);
            return;
        }
        const auto n = framesWanted (playback, b, buf->datas[0].maxsize / (std::uint32_t) sizeof (float));

        std::array<float*, 64> outs {};
        int outCount = 0;
        for (int ch = 0; ch < channels && ch < 64; ++ch)
        {
            auto* dst = static_cast<float*> (buf->datas[ch].data);
            if (activeOut[ch] && dst != nullptr && buf->datas[ch].maxsize >= n * sizeof (float))
                outs[(std::size_t) outCount++] = dst;
            else if (dst != nullptr)
                std::memset (dst, 0, std::min<std::size_t> (n * sizeof (float), buf->datas[ch].maxsize));
        }

        render (outs.data(), outCount, (int) n);

        for (int ch = 0; ch < channels; ++ch)
            if (auto* chunk = buf->datas[ch].chunk)
            {
                chunk->offset = 0;
                chunk->stride = sizeof (float);
                chunk->size = n * sizeof (float);
            }
        b->size = n;
        noteDelay (playback, outputDelay);
        api.streamQueueBuffer (playback.stream, b);
    }

    void render (float* const* outs, int outCount, int n) noexcept
    {
        // the input for this cycle from the capture stream's FIFO
        const auto inCount = (int) inBlock.size();
        std::array<const float*, 64> ins {};
        if (inCount > 0)
        {
            const auto r = fifoRead.load (std::memory_order_relaxed);
            const auto w = fifoWrite.load (std::memory_order_acquire);
            auto available = w - r;
            auto from = r;
            if (available > (std::size_t) n * 3) // drifted ahead (a stall): keep the newest block
            {
                from = w - (std::size_t) n;
                available = (std::size_t) n;
            }
            const auto take = std::min<std::size_t> (available, (std::size_t) n);
            if (take < (std::size_t) n && captureStarted)
                ++xruns;
            for (int c = 0; c < inCount && c < 64; ++c)
            {
                auto& dst = inBlock[(std::size_t) c];
                for (std::size_t i = 0; i < take; ++i)
                    dst[i] = fifo[(std::size_t) c][(from + i) % fifoFrames];
                std::fill (dst.begin() + (std::ptrdiff_t) take, dst.begin() + n, 0.0f);
                ins[(std::size_t) c] = dst.data();
            }
            fifoRead.store (from + take, std::memory_order_release);
        }

        const juce::SpinLock::ScopedTryLockType sl (callbackLock);
        if (! sl.isLocked() || callback == nullptr)
        {
            for (int c = 0; c < outCount; ++c)
                std::memset (outs[c], 0, (std::size_t) n * sizeof (float));
            return;
        }
        // in the engine's block size (PipeWire may run a larger quantum than asked when another client wants it)
        std::array<float*, 64> o {};
        std::array<const float*, 64> in {};
        for (int done = 0; done < n;)
        {
            const auto m = juce::jmin (block, n - done);
            for (int c = 0; c < outCount; ++c)
                o[(std::size_t) c] = outs[c] + done;
            for (int c = 0; c < inCount && c < 64; ++c)
                in[(std::size_t) c] = ins[(std::size_t) c] + done;
            callback->audioDeviceIOCallbackWithContext (in.data(), juce::jmin (inCount, 64), o.data(), outCount, m, {});
            done += m;
        }
    }

    std::shared_ptr<Connection> connection;
    std::shared_ptr<DeviceRegistry> registry;
    Endpoint output, input;
    bool wantOutput, wantInput;
    bool isOpenFlag = false;
    int rate = 48000, block = 256;
    juce::BigInteger activeOut, activeIn;
    juce::String lastError;

    juce::SpinLock callbackLock;
    juce::AudioIODeviceCallback* callback = nullptr;
    Stream playback, capture;

    std::vector<std::vector<float>> fifo, inBlock;
    std::atomic<std::size_t> fifoWrite { 0 }, fifoRead { 0 };
    std::atomic<bool> captureStarted { false };
    std::atomic<int> xruns { 0 }, outputDelay { 0 }, inputDelay { 0 };
};

std::shared_ptr<Connection> connectToDaemon();

// --- the device type ----------------------------------------------------------------------------------------------
class PipeWireDeviceType final : public juce::AudioIODeviceType,
                                 private juce::Timer
{
public:
    explicit PipeWireDeviceType (std::shared_ptr<Connection> c) : juce::AudioIODeviceType (pipeWireTypeName), connection (std::move (c))
    {
        scanForDevices(); // the device manager scans only the types it created itself
        startTimer (500); // hot-plug: sinks and sources that come and go
    }

    void scanForDevices() override
    {
        list = DeviceList::read (*connection);
        seenGeneration = connection->generation.load();
        scanned = true;
    }

    juce::StringArray getDeviceNames (bool wantInputNames) const override
    {
        juce::StringArray out;
        for (const auto& e : wantInputNames ? list.inputs : list.outputs)
            out.add (e.name);
        return out;
    }

    int getDefaultDeviceIndex (bool) const override { return 0; }

    int getIndexOfDevice (juce::AudioIODevice* device, bool asInput) const override
    {
        if (device == nullptr)
            return -1;
        return getDeviceNames (asInput).indexOf (device->getName());
    }

    bool hasSeparateInputsAndOutputs() const override { return true; }

    juce::AudioIODevice* createDevice (const juce::String& outputDeviceName, const juce::String& inputDeviceName) override
    {
        if (! scanned)
            scanForDevices();
        const auto* out = DeviceList::find (list.outputs, outputDeviceName);
        const auto* in = DeviceList::find (list.inputs, inputDeviceName);
        if (out == nullptr && in == nullptr)
            return nullptr;
        return new PipeWireDevice (connection, registry, outputDeviceName, inputDeviceName, out != nullptr ? *out : Endpoint {},
                                   in != nullptr ? *in : Endpoint {}, out != nullptr, in != nullptr);
    }

private:
    void timerCallback() override
    {
        // the daemon went away (restart, update): reconnect every 2 s and take the open devices along
        if (connection->broken)
        {
            if (++reconnectTicks % 4 != 0)
                return;
            auto fresh = connectToDaemon();
            if (fresh == nullptr)
                return;
            connection = std::move (fresh);
            for (auto* d : registry->devices)
                if (d->usesBrokenConnection())
                    d->reconnect (connection);
            seenGeneration = 0; // the list may have changed meanwhile
        }
        if (! scanned || connection->generation.load() == seenGeneration)
            return;
        const auto before = std::make_pair (getDeviceNames (false), getDeviceNames (true));
        scanForDevices();
        if (std::make_pair (getDeviceNames (false), getDeviceNames (true)) != before)
            callDeviceChangeListeners();
    }

    std::shared_ptr<Connection> connection;
    std::shared_ptr<DeviceRegistry> registry = std::make_shared<DeviceRegistry>();
    DeviceList list;
    std::uint32_t seenGeneration = 0;
    int reconnectTicks = 0;
    bool scanned = false;
};

std::shared_ptr<Connection> connectToDaemon()
{
    auto* api = Api::get();
    if (api == nullptr)
        return nullptr;
    auto c = std::make_shared<Connection> (*api);
    if (! c->connect())
        return nullptr;
    return c;
}
} // namespace

std::unique_ptr<juce::AudioIODeviceType> makePipeWireDeviceType()
{
    auto c = connectToDaemon();
    if (c != nullptr)
        return std::make_unique<PipeWireDeviceType> (std::move (c));
    return nullptr;
}

juce::String describePipeWire()
{
    auto* api = Api::get();
    if (api == nullptr)
        return "PipeWire: libpipewire-0.3.so.0 not found";
    juce::String s;
    s << "PipeWire: library " << api->getLibraryVersion();
    auto c = connectToDaemon();
    if (c == nullptr)
        return s + ", no daemon answered";
    c->lock();
    s << ", " << (int) c->nodes.size() << " audio devices, default output \"" << c->defaultSink << "\", default input \"" << c->defaultSource
      << "\", graph rate " << c->clockRate;
    c->unlock();
    return s;
}
} // namespace lynx

#else

namespace lynx
{
std::unique_ptr<juce::AudioIODeviceType> makePipeWireDeviceType() { return nullptr; }
juce::String describePipeWire() { return "PipeWire: Linux only"; }
} // namespace lynx

#endif
