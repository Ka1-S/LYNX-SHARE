#include "engine/ClapFormat.h"
#include "engine/JsfxFormat.h"
#include "engine/Vst2Format.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <juce_gui_extra/juce_gui_extra.h>

#include <clap/clap.h>

#include "core/Mpe.h"
#include "core/WorkerPool.h"
#include "engine/PluginHost.h"
#include "engine/TimedParameters.h"
#include "platform/PowerThrottling.h"
#include "platform/CrashGuard.h"
#include "platform/NativeEmbed.h"
#include "platform/Paths.h"

namespace lynx
{
namespace
{
// --- module (the .clap DLL) --------------------------------------------------
// clap_entry.init/deinit apply once per DLL: a loaded module is shared by
// all instances and unloaded when the last one goes away.
class ClapModule
{
public:
    // The file to load: the .clap itself (Windows DLL, Linux .so), or on macOS the executable inside the .clap bundle
    // (Contents/MacOS/<name>); clap_entry.init still gets the bundle path, as the CLAP spec asks.
    static juce::String binaryOf (const juce::String& path)
    {
       #if JUCE_MAC
        const juce::File bundle (path);
        if (bundle.isDirectory())
        {
            const auto macos = bundle.getChildFile ("Contents/MacOS");
            if (const auto named = macos.getChildFile (bundle.getFileNameWithoutExtension()); named.existsAsFile())
                return named.getFullPathName();
            for (const auto& f : macos.findChildFiles (juce::File::findFiles, false))
                return f.getFullPathName();
        }
       #endif
        return path;
    }

    static std::shared_ptr<ClapModule> open (const juce::String& path)
    {
        static std::mutex mutex;
        static std::map<juce::String, std::weak_ptr<ClapModule>> modules;
        const std::lock_guard<std::mutex> lock (mutex);

        if (auto alive = modules[path].lock())
            return alive;

        auto module = std::shared_ptr<ClapModule> (new ClapModule());

        if (! module->library.open (binaryOf (path)))
            return nullptr;

        const auto* entry = static_cast<const clap_plugin_entry_t*> (module->library.getFunction ("clap_entry"));

        if (entry == nullptr || ! clap_version_is_compatible (entry->clap_version) || ! entry->init (path.toRawUTF8()))
            return nullptr;

        module->entry = entry;
        module->factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));

        if (module->factory == nullptr)
            return nullptr;

        modules[path] = module;
        return module;
    }

    ~ClapModule()
    {
        if (entry != nullptr)
            entry->deinit();

        library.close();
    }

    const clap_plugin_factory_t* factory = nullptr;

    // another factory of the module (round 190jg: the preset discovery factory), or nullptr
    const void* getFactory (const char* id) const { return entry != nullptr ? entry->get_factory (id) : nullptr; }

private:
    ClapModule() = default;

    juce::DynamicLibrary library;
    const clap_plugin_entry_t* entry = nullptr;
};

bool hasFeature (const clap_plugin_descriptor_t& d, const char* feature)
{
    for (auto* f = d.features; f != nullptr && *f != nullptr; ++f)
        if (std::strcmp (*f, feature) == 0)
            return true;

    return false;
}

int idHash (const char* pluginId)
{
    return juce::String::fromUTF8 (pluginId).hashCode();
}

juce::PluginDescription describe (const clap_plugin_descriptor_t& d, const juce::File& file, int pluginsInFile)
{
    juce::PluginDescription p;
    p.name = juce::String::fromUTF8 (d.name);
    p.descriptiveName = juce::String::fromUTF8 (d.description != nullptr ? d.description : d.name);
    p.pluginFormatName = ClapPluginFormat::formatName;
    p.manufacturerName = juce::String::fromUTF8 (d.vendor != nullptr ? d.vendor : "");
    p.version = juce::String::fromUTF8 (d.version != nullptr ? d.version : "");
    p.fileOrIdentifier = file.getFullPathName();
    p.lastFileModTime = file.getLastModificationTime();
    p.lastInfoUpdateTime = juce::Time::getCurrentTime();
    p.uniqueId = p.deprecatedUid = idHash (d.id);
    p.isInstrument = hasFeature (d, CLAP_PLUGIN_FEATURE_INSTRUMENT);
    p.numInputChannels = p.isInstrument ? 0 : 2;
    p.numOutputChannels = 2;
    p.hasSharedContainer = pluginsInFile > 1;

    juce::StringArray features;
    for (auto* f = d.features; f != nullptr && *f != nullptr; ++f)
        features.add (juce::String::fromUTF8 (*f));
    p.category = features.joinIntoString ("|");
    return p;
}

// --- input event list (no allocation on the audio thread) -------------------------
class EventList
{
public:
    EventList()
    {
        pool.resize (64 * 1024);
        offsets.reserve (4096);
        list.ctx = this;
        list.size = [] (const clap_input_events_t* l) { return (uint32_t) static_cast<EventList*> (l->ctx)->offsets.size(); };
        list.get = [] (const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* {
            auto* self = static_cast<EventList*> (l->ctx);
            return i < self->offsets.size() ? reinterpret_cast<const clap_event_header_t*> (self->pool.data() + self->offsets[i])
                                            : nullptr;
        };
    }

    void clear() noexcept
    {
        offsets.clear();
        used = 0;
    }

    template <typename Event>
    void add (const Event& e) noexcept
    {
        const auto size = (sizeof (Event) + 7) & ~(std::size_t) 7;

        if (used + size > pool.size() || offsets.size() == offsets.capacity())
            return; // full: drop it (never allocates)

        std::memcpy (pool.data() + used, &e, sizeof (Event));
        offsets.push_back ((uint32_t) used);
        used += size;
    }

    const clap_input_events_t* get() const noexcept { return &list; }

    // in time order, keeping the order of events at the same time (an insertion sort: no allocation, and the list is
    // already in order unless timed parameter changes came before the MIDI)
    void sortByTime() noexcept
    {
        const auto timeOf = [this] (uint32_t at) { return reinterpret_cast<const clap_event_header_t*> (pool.data() + at)->time; };
        for (std::size_t i = 1; i < offsets.size(); ++i)
        {
            const auto item = offsets[i];
            const auto t = timeOf (item);
            auto j = i;
            for (; j > 0 && timeOf (offsets[j - 1]) > t; --j)
                offsets[j] = offsets[j - 1];
            offsets[j] = item;
        }
    }

private:
    std::vector<std::uint8_t> pool;
    std::vector<uint32_t> offsets;
    std::size_t used = 0;
    clap_input_events_t list {};
};

template <typename Event>
Event makeEvent (uint16_t type, uint32_t time)
{
    Event e {};
    e.header.size = sizeof (Event);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = type;
    e.header.flags = 0;
    return e;
}

class ClapInstance;

// --- parameter ---------------------------------------------------------------------
class ClapParameter final : public juce::HostedAudioProcessorParameter
{
public:
    ClapParameter (ClapInstance& o, const clap_param_info_t& info, double current)
        : owner (o),
          id (info.id),
          cookie (info.cookie),
          minimum (info.min_value),
          maximum (info.max_value),
          defaultValue (info.default_value),
          flags (info.flags),
          name (juce::String::fromUTF8 (info.name))
    {
        normalised.store (toNormalised (current));
    }

    juce::String getParameterID() const override { return juce::String ((juce::int64) id); }
    float getValue() const override { return normalised.load(); }
    void setValue (float v) override;
    float getDefaultValue() const override { return toNormalised (defaultValue); }
    juce::String getName (int maximumLength) const override { return name.substring (0, maximumLength); }
    juce::String getLabel() const override { return {}; }
    float getValueForText (const juce::String& text) const override { return toNormalised (text.getDoubleValue()); }
    juce::String getText (float v, int) const override;
    bool isAutomatable() const override { return (flags & CLAP_PARAM_IS_AUTOMATABLE) != 0; }
    bool isDiscrete() const override { return (flags & CLAP_PARAM_IS_STEPPED) != 0; }
    int getNumSteps() const override
    {
        return isDiscrete() ? juce::jmax (2, (int) (maximum - minimum) + 1) : AudioProcessorParameter::getNumSteps();
    }

    double toPlain (float v) const noexcept { return minimum + (maximum - minimum) * (double) v; }
    float toNormalised (double plain) const noexcept
    {
        return maximum > minimum ? (float) juce::jlimit (0.0, 1.0, (plain - minimum) / (maximum - minimum)) : 0.0f;
    }

    void setFromPlugin (double plain) noexcept { normalised.store (toNormalised (plain)); }

    ClapInstance& owner;
    const clap_id id;
    void* const cookie;

private:
    const double minimum, maximum, defaultValue;
    const uint32_t flags;
    const juce::String name;
    std::atomic<float> normalised { 0.0f };
};

// --- plugin UI window ----------------------------------------------------------------
class ClapEditor;

// --- what outlives the instance ---------------------------------------------------------
// CLAP's threading rules (issue #46): deactivate and destroy belong on the main thread, stop_processing on an audio
// thread, and a strict plugin (u-he's) aborts the whole process when a host breaks them. A ClapInstance can be
// deleted on any thread (an export's thread drops the last reference to its rack), and the message thread may be
// busy or blocked just then, for as long as it likes: it may even be waiting for that very thread. So what the plugin
// needs until it is destroyed lives here, not in the instance: the plugin, its module, the clap_host_t it was created
// with (the plugin may call it from deactivate / destroy) and the processing handshake. The instance's destructor
// hands this block to the message thread and waits a moment for it; when the message thread does not answer, the
// instance goes anyway and the plugin is deactivated and destroyed whenever the message thread is free again, or by
// finishPendingTeardowns() when the application closes. Nothing here is ever called on another thread instead.
class ClapInstance;

struct ClapLife
{
    const clap_plugin_t* plugin = nullptr;
    std::shared_ptr<ClapModule> module; // (released after destroy, on the message thread: the DLL's deinit too)
    clap_host_t host {};
    std::atomic<ClapInstance*> owner { nullptr }; // nullptr once the instance is gone
    std::atomic<bool> active { false };            // (atomic: prepareToPlay reads it on the export's thread)
    std::atomic<std::thread::id> audioThread {};
    // gate: held by the block in progress, or by the main thread while it (de)activates (holdProcessing);
    // processing: start_processing succeeded and stop_processing was not called yet
    std::atomic<bool> restartPending { false }, gate { false }, stopRequest { false }, processing { false };
    std::atomic<juce::uint32> lastBlockMs { 0 };
    juce::WaitableEvent tornDown { true };

    static bool isMessageThread()
    {
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        return mm == nullptr || mm->isThisTheMessageThread();
    }

    // CLAP's threading rules (issue #46: u-he plugins abort when they are broken): activate / deactivate on the main
    // thread, start_processing / stop_processing on the audio thread, and never deactivate while processing. Main
    // thread side: holds every block off (they output silence), has the audio thread call stop_processing at its next
    // block when blocks are flowing, and waits for the block in progress to end. When no block comes (playback
    // stopped, the export finished, the plugin is being deleted), the audio thread is idle: a short-lived thread then
    // stands in for it (thread_check reports it as the audio thread while it calls stop_processing). Returns false,
    // still holding the blocks off, when a block never ends (the plugin crashed inside process).
    bool holdProcessing()
    {
        restartPending.store (true);
        if (processing.load())
        {
            const auto now = juce::Time::getMillisecondCounter();
            // blocks are flowing (on another thread: an offline render on this one is over): the next one stops it
            if (now - lastBlockMs.load() < 250 && audioThread.load() != std::this_thread::get_id())
            {
                stopRequest.store (true);
                while (processing.load() && juce::Time::getMillisecondCounter() - now < 500)
                    std::this_thread::yield();
            }
        }

        const auto start = juce::Time::getMillisecondCounter();
        while (gate.exchange (true))
        {
            if (juce::Time::getMillisecondCounter() - start > 2000)
            {
                stopRequest.store (false);
                return false;
            }
            std::this_thread::yield();
        }
        stopRequest.store (false);

        if (processing.load()) // no block came: stop it from a stand-in audio thread
        {
            const auto previous = audioThread.load();
            std::thread standIn ([this] {
                audioThread.store (std::this_thread::get_id());
                plugin->stop_processing (plugin);
            });
            standIn.join();
            audioThread.store (previous);
            processing.store (false);
        }
        return true;
    }

    // The message thread: stop_processing (by the stand-in audio thread), deactivate, destroy, then the module.
    // (A plugin stuck inside process is not deactivated, as before: it is left alone.)
    void tearDown()
    {
        if (plugin != nullptr)
        {
            if (active.load() && holdProcessing())
                plugin->deactivate (plugin);
            active.store (false);
            plugin->destroy (plugin);
            plugin = nullptr;
        }
        module = nullptr;
        tornDown.signal();
    }

    // --- the teardowns waiting for the message thread --------------------------------------
    struct Pending
    {
        std::mutex lock;
        std::vector<std::shared_ptr<ClapLife>> list;
        std::atomic<int> waitMs { 10000 };
        std::atomic<int> deferred { 0 }; // how many the instance's destructor did not see finished (diagnostics, tests)
    };
    static Pending& pending()
    {
        static auto* p = new Pending(); // (never destroyed: a teardown may be queued while the process ends)
        return *p;
    }

    // Runs every queued teardown; on the message thread only (anywhere else it does nothing).
    static void drain()
    {
        if (! isMessageThread())
            return;
        for (;;)
        {
            std::vector<std::shared_ptr<ClapLife>> now;
            {
                const std::lock_guard<std::mutex> l (pending().lock);
                now.swap (pending().list);
            }
            if (now.empty())
                return;
            for (auto& life : now)
                life->tearDown();
        }
    }

    // The end of an instance, on whatever thread deletes it.
    static void finish (std::shared_ptr<ClapLife> life)
    {
        life->owner.store (nullptr);
        if (isMessageThread())
        {
            life->tearDown();
            return;
        }
        {
            const std::lock_guard<std::mutex> l (pending().lock);
            pending().list.push_back (life);
        }
        // (not posted: the message loop has ended, the application is closing and finishPendingTeardowns() runs it)
        if (! juce::MessageManager::callAsync ([] { drain(); }) || ! life->tornDown.wait (pending().waitMs.load()))
            ++pending().deferred;
    }
};

// --- instance ------------------------------------------------------------------------
class ClapInstance final : public juce::AudioPluginInstance
{
public:
    ClapInstance (std::shared_ptr<ClapModule> m, const clap_plugin_t* p, const juce::PluginDescription& d)
        : AudioPluginInstance (busesFor (d)),
          plugin (p),
          description (d)
    {
        life->module = std::move (m);
        life->owner.store (this);
        life->host = makeHost();
    }

    // Second phase (needs `this` ready as host_data): init + extensions + parameters.
    bool initialise()
    {
        life->plugin = plugin;
        if (! plugin->init (plugin))
            return false;

        initialised = true;
        params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
        gui = static_cast<const clap_plugin_gui_t*> (plugin->get_extension (plugin, CLAP_EXT_GUI));
        latency = static_cast<const clap_plugin_latency_t*> (plugin->get_extension (plugin, CLAP_EXT_LATENCY));
        audioPorts = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));
        // round 190jg (BUILD-LIST 18): the further extensions LYNX hosts
        tailExt = static_cast<const clap_plugin_tail_t*> (plugin->get_extension (plugin, CLAP_EXT_TAIL));
        renderExt = static_cast<const clap_plugin_render_t*> (plugin->get_extension (plugin, CLAP_EXT_RENDER));
        voiceInfoExt = static_cast<const clap_plugin_voice_info_t*> (plugin->get_extension (plugin, CLAP_EXT_VOICE_INFO));
        stateContextExt = static_cast<const clap_plugin_state_context_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE_CONTEXT));
        threadPoolExt = static_cast<const clap_plugin_thread_pool_t*> (plugin->get_extension (plugin, CLAP_EXT_THREAD_POOL));
        remoteControlsExt = static_cast<const clap_plugin_remote_controls_t*> (plugin->get_extension (plugin, CLAP_EXT_REMOTE_CONTROLS));
        if (remoteControlsExt == nullptr)
            remoteControlsExt = static_cast<const clap_plugin_remote_controls_t*> (plugin->get_extension (plugin, CLAP_EXT_REMOTE_CONTROLS_COMPAT));
        presetLoadExt = static_cast<const clap_plugin_preset_load_t*> (plugin->get_extension (plugin, CLAP_EXT_PRESET_LOAD));
        if (presetLoadExt == nullptr)
            presetLoadExt = static_cast<const clap_plugin_preset_load_t*> (plugin->get_extension (plugin, CLAP_EXT_PRESET_LOAD_COMPAT));

        if (const auto* notes = static_cast<const clap_plugin_note_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_NOTE_PORTS)))
        {
            clap_note_port_info_t info {};

            if (notes->count (plugin, true) > 0 && notes->get (plugin, 0, true, &info))
                midiDialect = (info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) == 0;
            hasNoteOutput = notes->count (plugin, false) > 0; // (GitHub #198, round 190ks: a plugin that sends notes)
        }

        inputChannels = mainPortChannels (true, description.isInstrument ? 0 : 2);
        outputChannels = juce::jmax (1, mainPortChannels (false, 2));

        if (gui != nullptr && ! gui->is_api_supported (plugin, platform::NativeEmbed::clapWindowApi(), false))
            gui = nullptr;

        if (params != nullptr)
        {
            const auto count = params->count (plugin);

            for (uint32_t i = 0; i < count; ++i)
            {
                clap_param_info_t info {};

                if (! params->get_info (plugin, i, &info) || (info.flags & CLAP_PARAM_IS_HIDDEN) != 0)
                    continue;

                double value = info.default_value;
                params->get_value (plugin, info.id, &value);
                auto parameter = std::make_unique<ClapParameter> (*this, info, value);
                byId[info.id] = parameter.get();
                byIndex.push_back (parameter.get());
                addHostedParameter (std::move (parameter));
            }
        }
        readRemoteControls();
        readVoiceInfo();

        return true;
    }

    ~ClapInstance() override
    {
        *alive = false;
        timers.clear();
       #if JUCE_LINUX || JUCE_BSD
        for (const auto fd : fds)
            juce::LinuxEventLoop::unregisterFdCallback (fd);
        fds.clear();
       #endif

        // (stop_processing on an audio thread, never here: issue #46; a plugin stuck inside process is left alone).
        // Deleted on another thread: deactivate and destroy go to the message thread, which this waits a moment for;
        // they are never done here instead (see ClapLife).
        life->plugin = plugin;
        ClapLife::finish (std::move (life));
    }

    static bool isMessageThread() { return ClapLife::isMessageThread(); }

    static const clap_host_t* hostFor (ClapInstance* self) { return &self->life->host; }

    // --- AudioProcessor -------------------------------------------------------------
    const juce::String getName() const override { return description.name; }

    void fillInPluginDescription (juce::PluginDescription& d) const override { d = description; }

    void prepareToPlay (double sampleRate, int blockSize) override
    {
        // already active with these settings: nothing to do. (LYNX prepares a plugin again to switch it to offline
        // rendering, on the export's thread; a CLAP has no process mode in activate, and activate / deactivate belong
        // on the main thread: issue #46)
        if (active && sampleRate == activeRate && juce::jmax (1, blockSize) == maxBlock)
            return;
        onMessageThreadFor ([this, sampleRate, blockSize] { reactivate (sampleRate, blockSize); });
    }

    // Runs fn here on the message thread; from another thread (an export at another rate or block size) on the
    // message thread, waiting up to 5 s for it. A message thread that doesn't answer in time is busy: fn then runs
    // when it gets there; until then the plugin keeps its current activation. With no message loop any more (the
    // application is closing) fn does not run at all: activate / deactivate are never called on another thread
    // (issue #46), and the teardown deactivates the plugin on the message thread.
    void onMessageThreadFor (std::function<void()> fn)
    {
        if (isMessageThread())
        {
            fn();
            return;
        }
        auto done = std::make_shared<juce::WaitableEvent>();
        if (juce::MessageManager::callAsync ([alive = alive, fn = std::move (fn), done] {
                if (*alive)
                    fn();
                done->signal();
            }))
            done->wait (5000);
    }

    // deactivates (when active) and activates the plugin again
    void reactivate (double sampleRate, int blockSize)
    {
        if (! holdProcessing())
            return; // (a process call never returned: the plugin crashed inside it, leave it alone)

        if (active)
            plugin->deactivate (plugin);
        active = false;

        if (plugin->activate (plugin, sampleRate, 1, (uint32_t) juce::jmax (1, blockSize)))
        {
            active = true;
            activeRate = sampleRate;
            maxBlock = juce::jmax (1, blockSize);
            scratch.setSize (juce::jmax (2, outputChannels), maxBlock);

            if (latency != nullptr)
                setLatencySamples ((int) latency->get (plugin));
            if (tailExt != nullptr) // (round 190jg; main or audio thread)
                tailSamples.store (tailExt->get (plugin));
            readVoiceInfo();
        }

        releaseProcessing();
    }

    void releaseResources() override
    {
        if (! active)
            return;
        onMessageThreadFor ([this] {
            if (! active || ! holdProcessing())
                return;
            plugin->deactivate (plugin);
            active = false;
            releaseProcessing();
        });
    }

    bool holdProcessing() { return life->holdProcessing(); } // (the handshake itself: ClapLife)

    void releaseProcessing()
    {
        gate.store (false);
        restartPending.store (false);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const auto n = buffer.getNumSamples();

        // the restart handshake (request_restart, holdProcessing): no block starts while the main thread holds them off
        if (gate.exchange (true))
        {
            buffer.clear();
            if (hasNoteOutput)
                midi.clear(); // (round 190ks: it sent nothing)
            return;
        }
        audioThread.store (std::this_thread::get_id());
        lastBlockMs.store (juce::Time::getMillisecondCounter());

        if (stopRequest.load() && processing.load())
        {
            plugin->stop_processing (plugin); // (on this, the audio thread: issue #46)
            processing.store (false);
        }

        if (restartPending.load() || ! active || n > maxBlock)
        {
            buffer.clear();
            if (hasNoteOutput)
                midi.clear(); // (round 190ks: it sent nothing)
            gate.store (false);
            return;
        }

        if (! processing.load())
            processing.store (plugin->start_processing (plugin));
        if (tailExt != nullptr && tailChanged.exchange (false)) // (round 190jg: clap_host_tail.changed)
            tailSamples.store (tailExt->get (plugin));

        events.clear();

        // parameters changed by the host (automation, UI)
        PendingParam change;
        while (pendingParams.pop (change))
        {
            auto e = makeEvent<clap_event_param_value_t> (CLAP_EVENT_PARAM_VALUE, 0);
            e.param_id = change.id;
            e.cookie = change.cookie;
            e.note_id = -1;
            e.port_index = e.channel = e.key = -1;
            e.value = change.value;
            events.add (e);
        }

        // round 190jg (sample-accurate automation, engine/TimedParameters.h): the automation's changes inside this block
        for (int k = 0; k < juce::lynxTimedParamCount; ++k)
        {
            const auto index = juce::lynxTimedParamIndices[k];
            if (index < 0 || index >= (int) byIndex.size())
                continue;
            auto* parameter = byIndex[(std::size_t) index];
            const auto value = juce::jlimit (0.0f, 1.0f, juce::lynxTimedParamValues[k]);
            auto e = makeEvent<clap_event_param_value_t> (CLAP_EVENT_PARAM_VALUE, (uint32_t) juce::jlimit (0, n - 1, juce::lynxTimedParamOffsets[k]));
            e.param_id = parameter->id;
            e.cookie = parameter->cookie;
            e.note_id = -1;
            e.port_index = e.channel = e.key = -1;
            e.value = parameter->toPlain (value);
            events.add (e);
            parameter->setFromPlugin (e.value);
        }

        for (const auto m : midi)
            addMidi (m.getMessage(), (uint32_t) juce::jlimit (0, n - 1, m.samplePosition));
        events.sortByTime(); // (CLAP: a block's input events in time order)
        // GitHub #198 (round 190ks): with a note output port, the buffer comes back with what the plugin sent (JUCE's
        // processBlock rule: after the call, the MIDI buffer holds the plugin's output)
        outMidi = hasNoteOutput ? &midi : nullptr;
        if (outMidi != nullptr)
            midi.clear();

        std::array<float*, 16> inputs {};
        std::array<float*, 16> outputs {};

        for (int ch = 0; ch < inputChannels && ch < (int) inputs.size(); ++ch)
            inputs[(std::size_t) ch] = buffer.getWritePointer (juce::jmin (ch, buffer.getNumChannels() - 1));

        for (int ch = 0; ch < outputChannels && ch < (int) outputs.size(); ++ch)
        {
            outputs[(std::size_t) ch] = scratch.getWritePointer (ch);
            juce::FloatVectorOperations::clear (outputs[(std::size_t) ch], n);
        }

        clap_audio_buffer_t in {};
        in.data32 = inputs.data();
        in.channel_count = (uint32_t) inputChannels;
        clap_audio_buffer_t out {};
        out.data32 = outputs.data();
        out.channel_count = (uint32_t) outputChannels;

        clap_output_events_t outEvents {};
        outEvents.ctx = this;
        outEvents.try_push = [] (const clap_output_events_t* list, const clap_event_header_t* e) {
            auto* self = static_cast<ClapInstance*> (list->ctx);

            if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE)
            {
                const auto* v = reinterpret_cast<const clap_event_param_value_t*> (e);

                if (auto it = self->byId.find (v->param_id); it != self->byId.end())
                    it->second->setFromPlugin (v->value);
            }
            // GitHub #198 (round 190ks): the notes it sends (its note output port), as MIDI at their sample
            if (self->outMidi != nullptr && e->space_id == CLAP_CORE_EVENT_SPACE_ID)
            {
                const auto at = (int) e->time;
                if (e->type == CLAP_EVENT_NOTE_ON || e->type == CLAP_EVENT_NOTE_OFF)
                {
                    const auto* note = reinterpret_cast<const clap_event_note_t*> (e);
                    if (note->key >= 0 && note->key < 128)
                    {
                        const auto ch = note->channel >= 0 && note->channel < 16 ? note->channel + 1 : 1;
                        const auto velocity = (float) juce::jlimit (0.0, 1.0, note->velocity);
                        self->outMidi->addEvent (e->type == CLAP_EVENT_NOTE_ON && velocity > 0.0f
                                                     ? juce::MidiMessage::noteOn (ch, note->key, velocity)
                                                     : juce::MidiMessage::noteOff (ch, note->key, velocity),
                                                 at);
                    }
                }
                else if (e->type == CLAP_EVENT_MIDI)
                {
                    const auto* m = reinterpret_cast<const clap_event_midi_t*> (e);
                    if (m->data[0] >= 0x80 && m->data[0] < 0xF0)
                        self->outMidi->addEvent (m->data, juce::MidiMessage::getMessageLengthFromFirstByte (m->data[0]), at);
                }
            }

            return true;
        };

        clap_process_t process {};
        process.steady_time = steadyTime;
        process.frames_count = (uint32_t) n;
        process.audio_inputs = inputChannels > 0 ? &in : nullptr;
        process.audio_inputs_count = inputChannels > 0 ? 1u : 0u;
        process.audio_outputs = &out;
        process.audio_outputs_count = 1;
        process.in_events = events.get();
        process.out_events = &outEvents;

        plugin->process (plugin, &process);
        steadyTime += n;
        outMidi = nullptr;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.copyFrom (ch, 0, scratch, juce::jmin (ch, outputChannels - 1), 0, n);
        gate.store (false);
    }

    // round 190jg: clap.tail (UINT32_MAX = an infinite tail); 0 without the extension, as before
    double getTailLengthSeconds() const override
    {
        if (tailExt == nullptr)
            return 0.0;
        const auto samples = tailSamples.load();
        if (samples == UINT32_MAX)
            return std::numeric_limits<double>::infinity();
        const auto rate = activeRate.load();
        return rate > 0.0 ? samples / rate : 0.0;
    }

    // round 190jg: clap.render — an export renders offline (the plugin may use its slower, better algorithms); a plugin
    // with a hard real-time requirement (it plays hardware) stays real-time
    void setNonRealtime (bool offline) noexcept override
    {
        AudioPluginInstance::setNonRealtime (offline);
        if (renderExt == nullptr)
            return;
        onMessageThreadFor ([this, offline] {
            if (renderExt->has_hard_realtime_requirement != nullptr && renderExt->has_hard_realtime_requirement (plugin) && offline)
                return;
            renderExt->set (plugin, offline ? CLAP_RENDER_OFFLINE : CLAP_RENDER_REALTIME);
        });
    }
    bool acceptsMidi() const override { return description.isInstrument; }
    bool producesMidi() const override { return hasNoteOutput; } // (round 190ks)

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return gui != nullptr; }

    // Round 190jg: the plugin's presets (preset-discovery factory + preset-load) as JUCE programs, which LYNX's plugin
    // window lists in its preset bar (the wrapper's preset menu and arrows). Found on the message thread the first time
    // they are asked for there; one program (none) until then and without the extensions, as before.
    int getNumPrograms() override
    {
        const auto& list = presets();
        return list.empty() ? 1 : (int) list.size();
    }
    int getCurrentProgram() override { return juce::jmax (0, currentPreset); }
    void setCurrentProgram (int index) override
    {
        const auto& list = presets();
        if (presetLoadExt == nullptr || index < 0 || index >= (int) list.size() || ! isMessageThread())
            return;
        const auto& p = list[(std::size_t) index];
        if (presetLoadExt->from_location (plugin, p.kind, p.kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN ? nullptr : p.location.toRawUTF8(),
                                          p.loadKey.isEmpty() ? nullptr : p.loadKey.toRawUTF8()))
        {
            currentPreset = index;
            refreshParameterValues();
        }
    }
    const juce::String getProgramName (int index) override
    {
        const auto& list = presets();
        return index >= 0 && index < (int) list.size() ? list[(std::size_t) index].name : juce::String();
    }
    void changeProgramName (int, const juce::String&) override {}

    // what discovery found for this plugin (the tests, --clapext)
    struct Preset
    {
        juce::String name, location, loadKey;
        uint32_t kind = CLAP_PRESET_DISCOVERY_LOCATION_FILE;
    };
    const std::vector<Preset>& presets();

    void getStateInformation (juce::MemoryBlock& data) override
    {
        if (state == nullptr)
            return;

        juce::MemoryOutputStream out (data, false);
        clap_ostream_t stream {};
        stream.ctx = &out;
        stream.write = [] (const clap_ostream_t* s, const void* buffer, uint64_t size) -> int64_t {
            return static_cast<juce::MemoryOutputStream*> (s->ctx)->write (buffer, (size_t) size) ? (int64_t) size : -1;
        };
        // round 190jg: clap.state-context — a preset or a duplicate may leave out what belongs to the project
        if (const auto context = clapStateContext(); stateContextExt != nullptr && context != CLAP_STATE_CONTEXT_FOR_PROJECT)
            stateContextExt->save (plugin, &stream, context);
        else
            state->save (plugin, &stream);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (state == nullptr)
            return;

        juce::MemoryInputStream in (data, (size_t) size, false);
        clap_istream_t stream {};
        stream.ctx = &in;
        stream.read = [] (const clap_istream_t* s, void* buffer, uint64_t bytes) -> int64_t {
            return static_cast<juce::MemoryInputStream*> (s->ctx)->read (buffer, (int) juce::jmin<uint64_t> (bytes, 1 << 30));
        };

        const auto context = clapStateContext();
        const auto loaded = stateContextExt != nullptr && context != CLAP_STATE_CONTEXT_FOR_PROJECT ? stateContextExt->load (plugin, &stream, context)
                                                                                                    : state->load (plugin, &stream);
        if (loaded)
            refreshParameterValues();
    }

    void refreshParameterValues()
    {
        if (params != nullptr)
            for (auto& [paramId, parameter] : byId)
            {
                double value = 0.0;

                if (params->get_value (plugin, paramId, &value))
                    parameter->setFromPlugin (value);
            }
    }

    // --- round 190jg: remote controls, voice info, what the plugin offers --------------------------------------------
    std::vector<ClapRemotePage> remotePages; // (message thread)
    void readRemoteControls()
    {
        remotePages.clear();
        if (remoteControlsExt == nullptr)
            return;
        std::map<clap_id, int> indexOf;
        for (std::size_t i = 0; i < byIndex.size(); ++i)
            indexOf[byIndex[i]->id] = (int) i;
        const auto count = juce::jmin (64u, remoteControlsExt->count (plugin));
        for (uint32_t i = 0; i < count; ++i)
        {
            clap_remote_controls_page_t page {};
            if (! remoteControlsExt->get (plugin, i, &page))
                continue;
            ClapRemotePage out;
            out.section = juce::String::fromUTF8 (page.section_name);
            out.name = juce::String::fromUTF8 (page.page_name);
            out.forPreset = page.is_for_preset;
            for (int k = 0; k < CLAP_REMOTE_CONTROLS_COUNT; ++k)
            {
                const auto it = indexOf.find (page.param_ids[k]);
                out.params[(std::size_t) k] = page.param_ids[k] != CLAP_INVALID_ID && it != indexOf.end() ? it->second : -1;
            }
            remotePages.push_back (out);
        }
    }

    ClapVoiceInfo voiceInfo; // (message thread)
    void readVoiceInfo()
    {
        voiceInfo = {};
        if (voiceInfoExt == nullptr || ! isMessageThread())
            return;
        clap_voice_info_t info {};
        if (voiceInfoExt->get (plugin, &info))
            voiceInfo = { true, (int) info.voice_count, (int) info.voice_capacity, (info.flags & CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES) != 0 };
    }

    juce::StringArray extensionsOffered() const
    {
        static const char* const ids[] = { CLAP_EXT_PARAMS, CLAP_EXT_STATE, CLAP_EXT_STATE_CONTEXT, CLAP_EXT_GUI, CLAP_EXT_LATENCY, CLAP_EXT_TAIL,
                                           CLAP_EXT_RENDER, CLAP_EXT_AUDIO_PORTS, CLAP_EXT_AUDIO_PORTS_CONFIG, CLAP_EXT_NOTE_PORTS, CLAP_EXT_NOTE_NAME,
                                           CLAP_EXT_VOICE_INFO, CLAP_EXT_THREAD_POOL, CLAP_EXT_TIMER_SUPPORT, CLAP_EXT_POSIX_FD_SUPPORT,
                                           CLAP_EXT_REMOTE_CONTROLS, CLAP_EXT_REMOTE_CONTROLS_COMPAT, CLAP_EXT_PRESET_LOAD, CLAP_EXT_PRESET_LOAD_COMPAT,
                                           CLAP_EXT_PARAM_INDICATION, CLAP_EXT_CONTEXT_MENU, CLAP_EXT_TRACK_INFO, CLAP_EXT_AMBISONIC, CLAP_EXT_SURROUND,
                                           CLAP_EXT_CONFIGURABLE_AUDIO_PORTS, CLAP_EXT_AUDIO_PORTS_ACTIVATION };
        juce::StringArray out;
        for (const auto* id : ids)
            if (plugin->get_extension (plugin, id) != nullptr)
                out.add (id);
        return out;
    }
    std::pair<int, int> threadPoolUse() const { return { threadPoolRequests.load(), threadPoolTasks.load() }; }
    // the presets are still being found (round 190jg: on their own thread; the first presets() call starts it)
    bool presetsPending()
    {
        if (presetLoadExt == nullptr || presetsFound)
            return false;
        presets();
        return ! presetsFound;
    }
    bool declaresTail() const noexcept { return tailExt != nullptr; }
    const std::shared_ptr<ClapModule>& moduleOf() const { return life->module; }
    const juce::PluginDescription& describedAs() const { return description; }

    // --- used by the parameters and the editor ---------------------------------------------
    void queueParam (clap_id id, void* cookie, double value) { pendingParams.push ({ id, cookie, value }); }

    juce::String paramText (clap_id id, double value) const
    {
        char text[256] {};

        if (params != nullptr && juce::MessageManager::existsAndIsCurrentThread() && params->value_to_text (plugin, id, value, text, sizeof (text)))
            return juce::String::fromUTF8 (text);

        return juce::String (value, 3);
    }

    const clap_plugin_t* plugin;
    const clap_plugin_gui_t* gui = nullptr;
    juce::Component::SafePointer<juce::Component> editor;

private:
    struct PendingParam
    {
        clap_id id = 0;
        void* cookie = nullptr;
        double value = 0.0;
    };

    // single-producer queue (UI thread) to the audio thread
    class ParamQueue
    {
    public:
        bool push (const PendingParam& p) noexcept
        {
            const auto w = write.load();
            const auto next = (w + 1) % items.size();

            if (next == read.load())
                return false;

            items[w] = p;
            write.store (next);
            return true;
        }

        bool pop (PendingParam& p) noexcept
        {
            const auto r = read.load();

            if (r == write.load())
                return false;

            p = items[r];
            read.store ((r + 1) % items.size());
            return true;
        }

    private:
        std::array<PendingParam, 1024> items {};
        std::atomic<std::size_t> write { 0 }, read { 0 };
    };

    static BusesProperties busesFor (const juce::PluginDescription& d)
    {
        BusesProperties buses;

        if (! d.isInstrument)
            buses = buses.withInput ("Input", juce::AudioChannelSet::stereo(), true);

        return buses.withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    }

    int mainPortChannels (bool input, int fallback) const
    {
        if (audioPorts == nullptr)
            return fallback;

        if (audioPorts->count (plugin, input) == 0)
            return 0;

        clap_audio_port_info_t info {};
        return audioPorts->get (plugin, 0, input, &info) ? juce::jmin (16, (int) info.channel_count) : fallback;
    }

public:
    // issue #108: see setClapNoteExpressions (engine/ClapFormat.h)
    bool setNoteExpressions (float bendRange) noexcept
    {
        expressionRange.store (midiDialect ? 0.0f : juce::jmax (0.0f, bendRange), std::memory_order_relaxed);
        return ! midiDialect;
    }

private:
    void addMidi (const juce::MidiMessage& m, uint32_t time)
    {
        const auto channel = (int16_t) juce::jlimit (0, 15, m.getChannel() - 1);

        if (! midiDialect && (m.isNoteOn() || m.isNoteOff()))
        {
            const auto on = m.isNoteOn();
            auto e = makeEvent<clap_event_note_t> (on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF, time);
            e.note_id = -1;
            e.port_index = 0;
            e.channel = channel;
            e.key = (int16_t) m.getNoteNumber();
            e.velocity = on ? m.getFloatVelocity() : 0.0;
            events.add (e);
            heldNotes[(std::size_t) channel * 128 + (std::size_t) m.getNoteNumber()] = on;
            return;
        }

        if (! midiDialect && (m.isAllNotesOff() || m.isAllSoundOff()))
        {
            // CLAP has no "all notes off": release each held note
            for (std::size_t i = 0; i < heldNotes.size(); ++i)
                if (heldNotes[i])
                {
                    auto e = makeEvent<clap_event_note_t> (m.isAllSoundOff() ? CLAP_EVENT_NOTE_CHOKE : CLAP_EVENT_NOTE_OFF, time);
                    e.note_id = -1;
                    e.port_index = 0;
                    e.channel = (int16_t) (i / 128);
                    e.key = (int16_t) (i % 128);
                    events.add (e);
                    heldNotes[i] = false;
                }
            return;
        }

        // issue #108: a member channel's bend / CC 74 / pressure as a note expression for the notes on that channel
        const auto range = expressionRange.load (std::memory_order_relaxed);
        if (mpe::NoteExpression x; ! midiDialect && range > 0.0f && mpe::toNoteExpression (m.getRawData(), m.getRawDataSize(), range, x))
        {
            auto e = makeEvent<clap_event_note_expression_t> (CLAP_EVENT_NOTE_EXPRESSION, time);
            e.expression_id = x.expr == mpe::Expr::pitch ? CLAP_NOTE_EXPRESSION_TUNING
                              : x.expr == mpe::Expr::timbre ? CLAP_NOTE_EXPRESSION_BRIGHTNESS : CLAP_NOTE_EXPRESSION_PRESSURE;
            e.note_id = -1;
            e.port_index = 0;
            e.channel = (int16_t) x.channel;
            e.key = -1;
            e.value = x.value;
            events.add (e);
            return;
        }

        if (m.getRawDataSize() <= 3)
        {
            auto e = makeEvent<clap_event_midi_t> (CLAP_EVENT_MIDI, time);
            e.port_index = 0;
            std::memcpy (e.data, m.getRawData(), (std::size_t) m.getRawDataSize());
            events.add (e);
        }
    }

    // --- the host as seen by the plugin -----------------------------------------------------
    // host_data is the ClapLife: the plugin may call its host until it is destroyed, which can be after the instance
    // is gone (from: nullptr then; what needs no instance, the extension tables and thread_check, keeps answering)
    static ClapLife* lifeOf (const clap_host_t* h) { return static_cast<ClapLife*> (h->host_data); }
    static ClapInstance* from (const clap_host_t* h) { return lifeOf (h)->owner.load(); }

    void onMainThread (std::function<void()> fn)
    {
        juce::MessageManager::callAsync ([flag = alive, fn = std::move (fn)] {
            if (*flag)
                fn();
        });
    }

    clap_host_t makeHost()
    {
        clap_host_t h {};
        h.clap_version = CLAP_VERSION;
        h.host_data = life.get();
        h.name = "LYNX";
        h.vendor = "LYNX";
        h.url = "";
        h.version = JUCE_APPLICATION_VERSION_STRING;
        // (the tables belong to no instance: a plugin keeps the pointers it got for as long as it lives)
        h.get_extension = [] (const clap_host_t*, const char* id) -> const void* {
            if (std::strcmp (id, CLAP_EXT_LOG) == 0) return &hostLog();
            if (std::strcmp (id, CLAP_EXT_THREAD_CHECK) == 0) return &hostThreadCheck();
            if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &hostParams();
            if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &hostState();
            if (std::strcmp (id, CLAP_EXT_LATENCY) == 0) return &hostLatency();
            if (std::strcmp (id, CLAP_EXT_GUI) == 0) return &hostGui();
            if (std::strcmp (id, CLAP_EXT_TIMER_SUPPORT) == 0) return &hostTimers();
            // round 190jg (BUILD-LIST 18)
            if (std::strcmp (id, CLAP_EXT_THREAD_POOL) == 0) return &hostThreadPool();
            if (std::strcmp (id, CLAP_EXT_TAIL) == 0) return &hostTail();
            if (std::strcmp (id, CLAP_EXT_VOICE_INFO) == 0) return &hostVoiceInfo();
            if (std::strcmp (id, CLAP_EXT_REMOTE_CONTROLS) == 0 || std::strcmp (id, CLAP_EXT_REMOTE_CONTROLS_COMPAT) == 0) return &hostRemoteControls();
            if (std::strcmp (id, CLAP_EXT_PRESET_LOAD) == 0 || std::strcmp (id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0) return &hostPresetLoad();
           #if JUCE_LINUX || JUCE_BSD
            if (std::strcmp (id, CLAP_EXT_POSIX_FD_SUPPORT) == 0) return &hostFds();
           #endif
            return nullptr;
        };
        h.request_restart = [] (const clap_host_t* hh) {
            // restart on the UI thread with the same settings (new latency, ports...)
            auto* self = from (hh);
            if (self == nullptr)
                return;
            self->onMainThread ([self] {
                // the rack calls processBlock directly (no callback lock), so suspendProcessing alone would not
                // stop it: reactivate holds new blocks off, has the audio thread stop processing and waits for the
                // block in progress before deactivating (holdProcessing)
                if (self->active)
                    self->reactivate (self->activeRate, self->maxBlock);
            });
        };
        h.request_process = [] (const clap_host_t*) {};
        h.request_callback = [] (const clap_host_t* hh) {
            if (auto* self = from (hh))
                self->onMainThread ([self] { self->plugin->on_main_thread (self->plugin); });
        };
        return h;
    }

    juce::PluginDescription description;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::shared_ptr<ClapLife> life = std::make_shared<ClapLife>(); // (the host the plugin sees: outlives this)

    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const clap_plugin_latency_t* latency = nullptr;
    const clap_plugin_audio_ports_t* audioPorts = nullptr;
    // round 190jg
    const clap_plugin_tail_t* tailExt = nullptr;
    const clap_plugin_render_t* renderExt = nullptr;
    const clap_plugin_voice_info_t* voiceInfoExt = nullptr;
    const clap_plugin_state_context_t* stateContextExt = nullptr;
    const clap_plugin_thread_pool_t* threadPoolExt = nullptr;
    const clap_plugin_remote_controls_t* remoteControlsExt = nullptr;
    const clap_plugin_preset_load_t* presetLoadExt = nullptr;
    std::atomic<uint32_t> tailSamples { 0 };
    std::atomic<bool> tailChanged { false };
    std::vector<ClapParameter*> byIndex; // the parameters in getParameters() order
    int currentPreset = -1;
    bool presetsFound = false;
    std::vector<Preset> presetList;
    std::atomic<int> threadPoolRequests { 0 }, threadPoolTasks { 0 }; // (diagnostics)
    friend struct ClapHostThreadPool;
    bool midiDialect = false;
    bool hasNoteOutput = false;           // (round 190ks) a note output port: its notes come back in processBlock's buffer
    juce::MidiBuffer* outMidi = nullptr;  // (round 190ks) that buffer while process() runs
    std::atomic<float> expressionRange { 0.0f }; // issue #108: > 0 = member channels' MPE data go out as note expressions
    bool initialised = false;

    std::atomic<bool>& active = life->active;
    std::atomic<double> activeRate { 0.0 }; // (atomic: prepareToPlay reads them on the export's thread)
    std::atomic<int> maxBlock { 0 };
    int inputChannels = 0, outputChannels = 2;
    int64_t steadyTime = 0;
    juce::AudioBuffer<float> scratch;
    EventList events;
    ParamQueue pendingParams;
    std::unordered_map<clap_id, ClapParameter*> byId;
    std::array<bool, 16 * 128> heldNotes {};
    // the processing handshake (ClapLife: the teardown needs it after this instance)
    std::atomic<std::thread::id>& audioThread = life->audioThread;
    std::atomic<bool>& restartPending = life->restartPending;
    std::atomic<bool>& gate = life->gate;
    std::atomic<bool>& stopRequest = life->stopRequest;
    std::atomic<bool>& processing = life->processing;
    std::atomic<juce::uint32>& lastBlockMs = life->lastBlockMs;

    static const clap_host_log_t& hostLog()
    {
        static const clap_host_log_t e { [] (const clap_host_t*, clap_log_severity, const char* msg) { juce::ignoreUnused (msg); DBG ("CLAP: " << msg); } };
        return e;
    }
    static const clap_host_thread_check_t& hostThreadCheck()
    {
        static const clap_host_thread_check_t e {
            [] (const clap_host_t*) { return juce::MessageManager::existsAndIsCurrentThread(); },
            [] (const clap_host_t* hh) { return lifeOf (hh)->audioThread.load() == std::this_thread::get_id(); }
        };
        return e;
    }
    static const clap_host_params_t& hostParams()
    {
        static const clap_host_params_t e {
            [] (const clap_host_t*, clap_param_rescan_flags) {},
            [] (const clap_host_t*, clap_id, clap_param_clear_flags) {},
            [] (const clap_host_t*) {}
        };
        return e;
    }
    static const clap_host_state_t& hostState()
    {
        static const clap_host_state_t e { [] (const clap_host_t*) {} };
        return e;
    }
    // timer-support: plugin GUIs (on Linux above all) repaint and poll from host timers, on the main thread
    struct PluginTimer final : juce::Timer
    {
        PluginTimer (ClapInstance& o, clap_id i) : owner (o), id (i) {}
        void timerCallback() override
        {
            if (const auto* ext = static_cast<const clap_plugin_timer_support_t*> (owner.plugin->get_extension (owner.plugin, CLAP_EXT_TIMER_SUPPORT)))
                ext->on_timer (owner.plugin, id);
        }
        ClapInstance& owner;
        clap_id id;
    };
    std::map<clap_id, std::unique_ptr<PluginTimer>> timers;
    clap_id nextTimer = 1;
    static const clap_host_timer_support_t& hostTimers()
    {
        static const clap_host_timer_support_t e {
            [] (const clap_host_t* hh, uint32_t periodMs, clap_id* timerId) {
                auto* self = from (hh);
                if (self == nullptr || timerId == nullptr || ! juce::MessageManager::existsAndIsCurrentThread())
                    return false;
                const auto id = self->nextTimer++;
                auto timer = std::make_unique<PluginTimer> (*self, id);
                timer->startTimer ((int) juce::jlimit<uint32_t> (1, 10000, periodMs));
                self->timers[id] = std::move (timer);
                *timerId = id;
                return true;
            },
            [] (const clap_host_t* hh, clap_id timerId) {
                auto* self = from (hh); // (gone: its timers went with it)
                return self != nullptr && self->timers.erase (timerId) > 0;
            }
        };
        return e;
    }
   #if JUCE_LINUX || JUCE_BSD
    // posix-fd-support: X11/Wayland plugin GUIs watch their own sockets through the host's event loop
    std::vector<int> fds;
    static short pollMask (clap_posix_fd_flags_t flags)
    {
        return (short) (((flags & CLAP_POSIX_FD_READ) != 0 ? 0x001 /*POLLIN*/ : 0) | ((flags & CLAP_POSIX_FD_WRITE) != 0 ? 0x004 /*POLLOUT*/ : 0)
                        | ((flags & CLAP_POSIX_FD_ERROR) != 0 ? 0x008 /*POLLERR*/ : 0));
    }
    static bool watchFd (ClapInstance* self, int fd, clap_posix_fd_flags_t flags)
    {
        if (self == nullptr)
            return false;
        juce::LinuxEventLoop::unregisterFdCallback (fd);
        juce::LinuxEventLoop::registerFdCallback (fd, [self, flags] (int f) {
            if (const auto* ext = static_cast<const clap_plugin_posix_fd_support_t*> (self->plugin->get_extension (self->plugin, CLAP_EXT_POSIX_FD_SUPPORT)))
                ext->on_fd (self->plugin, f, flags);
        }, pollMask (flags));
        if (std::find (self->fds.begin(), self->fds.end(), fd) == self->fds.end())
            self->fds.push_back (fd);
        return true;
    }
    static const clap_host_posix_fd_support_t& hostFds()
    {
        static const clap_host_posix_fd_support_t e {
            [] (const clap_host_t* hh, int fd, clap_posix_fd_flags_t flags) { return watchFd (from (hh), fd, flags); },
            [] (const clap_host_t* hh, int fd, clap_posix_fd_flags_t flags) { return watchFd (from (hh), fd, flags); },
            [] (const clap_host_t* hh, int fd) {
                auto* self = from (hh);
                if (self == nullptr) // (gone: its descriptors were unregistered with it)
                    return false;
                juce::LinuxEventLoop::unregisterFdCallback (fd);
                const auto it = std::find (self->fds.begin(), self->fds.end(), fd);
                if (it == self->fds.end())
                    return false;
                self->fds.erase (it);
                return true;
            }
        };
        return e;
    }
   #endif

    // --- round 190jg (BUILD-LIST 18) ---------------------------------------------------------------------------------
    // thread-pool: the plugin's tasks of one process call run on LYNX's own worker threads (a pool kept for CLAP plugins:
    // the rack's pool may be running the very block that called). While another plugin's tasks hold it, or off the
    // audio thread, they run here one after the other (still complete when request_exec returns, as the spec asks).
    static const clap_host_thread_pool_t& hostThreadPool()
    {
        static const clap_host_thread_pool_t e { [] (const clap_host_t* hh, uint32_t tasks) {
            auto* life = lifeOf (hh);
            auto* self = life->owner.load();
            if (self == nullptr || self->threadPoolExt == nullptr || life->audioThread.load() != std::this_thread::get_id())
                return false;
            ++self->threadPoolRequests;
            self->threadPoolTasks += (int) tasks;
            runClapTasks (*self->plugin, *self->threadPoolExt, tasks);
            return true;
        } };
        return e;
    }
    static void runClapTasks (const clap_plugin_t& p, const clap_plugin_thread_pool_t& ext, uint32_t tasks) noexcept
    {
        static std::atomic<bool> busy { false };
        static WorkerPool* pool = [] {
            const auto n = juce::jlimit (1, 7, (int) std::thread::hardware_concurrency() - 1);
            return new WorkerPool (n, [] { power::becomeProAudioThread(); }); // (never destroyed: used until exit)
        }();
        auto task = [&p, &ext] (int i) { ext.exec (&p, (uint32_t) i); };
        if (tasks > 1 && ! busy.exchange (true))
        {
            pool->parallelFor ((int) tasks, task);
            pool->park();
            busy.store (false);
            return;
        }
        for (uint32_t i = 0; i < tasks; ++i)
            task ((int) i);
    }
    // tail: the plugin's tail changed (audio or main thread): read again at the next block
    static const clap_host_tail_t& hostTail()
    {
        static const clap_host_tail_t e { [] (const clap_host_t* hh) {
            if (auto* self = from (hh))
                self->tailChanged.store (true);
        } };
        return e;
    }
    static const clap_host_voice_info_t& hostVoiceInfo()
    {
        static const clap_host_voice_info_t e { [] (const clap_host_t* hh) {
            if (auto* self = from (hh))
                self->onMainThread ([self] { self->readVoiceInfo(); });
        } };
        return e;
    }
    static const clap_host_remote_controls_t& hostRemoteControls()
    {
        static const clap_host_remote_controls_t e {
            [] (const clap_host_t* hh) {
                if (auto* self = from (hh))
                    self->onMainThread ([self] { self->readRemoteControls(); });
            },
            [] (const clap_host_t*, clap_id) {} // (a suggested page: LYNX's controller pages follow the knob banks)
        };
        return e;
    }
    static const clap_host_preset_load_t& hostPresetLoad()
    {
        static const clap_host_preset_load_t e {
            [] (const clap_host_t*, uint32_t, const char*, const char*, int32_t, const char* msg) {
                juce::ignoreUnused (msg);
                DBG ("CLAP preset load failed: " << msg);
            },
            [] (const clap_host_t* hh, uint32_t, const char*, const char*) {
                if (auto* self = from (hh))
                    self->onMainThread ([self] { self->refreshParameterValues(); });
            }
        };
        return e;
    }

    static const clap_host_latency_t& hostLatency()
    {
        static const clap_host_latency_t e { [] (const clap_host_t* hh) {
            auto* self = from (hh);
            if (self == nullptr)
                return;
            self->onMainThread ([self] {
                if (self->latency != nullptr && self->active)
                    self->setLatencySamples ((int) self->latency->get (self->plugin));
            });
        } };
        return e;
    }
    static const clap_host_gui_t& hostGui()
    {
        static const clap_host_gui_t e {
            [] (const clap_host_t*) {},
            [] (const clap_host_t* hh, uint32_t w, uint32_t h) {
                auto* self = from (hh);
                if (self == nullptr)
                    return false;
                self->onMainThread ([self, w, h] {
                    if (auto* e = self->editor.getComponent())
                    {
                        const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;
                        e->setSize (juce::roundToInt (w / scale), juce::roundToInt (h / scale));
                    }
                });
                return true;
            },
            [] (const clap_host_t*) { return false; },
            [] (const clap_host_t*) { return false; },
            [] (const clap_host_t*, bool) {}
        };
        return e;
    }
};

// --- round 190jg: preset discovery ---------------------------------------------------------------------------------
namespace
{
// What one discovery run found for one module: every preset with the plugin ids it is for (empty: the provider named
// none). A module is crawled once per session (the spec: indexing must be fast and not interactive).
struct FoundPreset
{
    ClapInstance::Preset preset;
    juce::StringArray pluginIds;
};

struct Crawl
{
    std::vector<clap_preset_discovery_location_t> locations;
    std::vector<juce::String> locationNames, locationPaths; // (owned copies of the strings)
    juce::StringArray extensions;                          // declared file types ("" = any)
    std::vector<FoundPreset> found;
    // the file / location being read now
    uint32_t kind = CLAP_PRESET_DISCOVERY_LOCATION_FILE;
    juce::String path;
};

std::atomic<bool> discoveryStop { false }; // (the application is closing: a crawl stops between files)

std::vector<FoundPreset> discoverPresets (const ClapModule& module)
{
    std::vector<FoundPreset> result;
    auto* factory = static_cast<const clap_preset_discovery_factory_t*> (module.getFactory (CLAP_PRESET_DISCOVERY_FACTORY_ID));
    if (factory == nullptr)
        factory = static_cast<const clap_preset_discovery_factory_t*> (module.getFactory (CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT));
    if (factory == nullptr)
        return result;

    constexpr std::size_t maxPresets = 8192, maxFiles = 20000;
    for (uint32_t i = 0; i < factory->count (factory); ++i)
    {
        const auto* descriptor = factory->get_descriptor (factory, i);
        if (descriptor == nullptr || descriptor->id == nullptr)
            continue;
        Crawl crawl;
        clap_preset_discovery_indexer_t indexer {};
        indexer.clap_version = CLAP_VERSION;
        indexer.name = "LYNX SHARE";
        indexer.vendor = "LYNX";
        indexer.url = "";
        indexer.version = JUCE_APPLICATION_VERSION_STRING;
        indexer.indexer_data = &crawl;
        indexer.declare_filetype = [] (const clap_preset_discovery_indexer_t* x, const clap_preset_discovery_filetype_t* t) {
            if (t != nullptr)
                static_cast<Crawl*> (x->indexer_data)->extensions.add (juce::String::fromUTF8 (t->file_extension != nullptr ? t->file_extension : ""));
            return true;
        };
        indexer.declare_location = [] (const clap_preset_discovery_indexer_t* x, const clap_preset_discovery_location_t* l) {
            if (l == nullptr)
                return false;
            auto* c = static_cast<Crawl*> (x->indexer_data);
            c->locations.push_back (*l);
            c->locationNames.push_back (juce::String::fromUTF8 (l->name != nullptr ? l->name : ""));
            c->locationPaths.push_back (juce::String::fromUTF8 (l->location != nullptr ? l->location : ""));
            return true;
        };
        indexer.declare_soundpack = [] (const clap_preset_discovery_indexer_t*, const clap_preset_discovery_soundpack_t*) { return true; };
        indexer.get_extension = [] (const clap_preset_discovery_indexer_t*, const char*) -> const void* { return nullptr; };

        const auto* provider = factory->create (factory, &indexer, descriptor->id);
        if (provider == nullptr)
            continue;
        if (provider->init (provider))
        {
            clap_preset_discovery_metadata_receiver_t receiver {};
            receiver.receiver_data = &crawl;
            receiver.on_error = [] (const clap_preset_discovery_metadata_receiver_t*, int32_t, const char*) {};
            receiver.begin_preset = [] (const clap_preset_discovery_metadata_receiver_t* r, const char* name, const char* key) {
                auto* c = static_cast<Crawl*> (r->receiver_data);
                if (c->found.size() >= maxPresets)
                    return false;
                FoundPreset p;
                p.preset.name = juce::String::fromUTF8 (name != nullptr ? name : "");
                p.preset.loadKey = juce::String::fromUTF8 (key != nullptr ? key : "");
                p.preset.location = c->path;
                p.preset.kind = c->kind;
                if (p.preset.name.isEmpty())
                    p.preset.name = juce::File (c->path).getFileNameWithoutExtension();
                c->found.push_back (std::move (p));
                return true;
            };
            receiver.add_plugin_id = [] (const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* id) {
                auto* c = static_cast<Crawl*> (r->receiver_data);
                if (id != nullptr && id->id != nullptr && ! c->found.empty() && (id->abi == nullptr || std::strcmp (id->abi, "clap") == 0))
                    c->found.back().pluginIds.add (juce::String::fromUTF8 (id->id));
            };
            receiver.set_soundpack_id = [] (const clap_preset_discovery_metadata_receiver_t*, const char*) {};
            receiver.set_flags = [] (const clap_preset_discovery_metadata_receiver_t*, uint32_t) {};
            receiver.add_creator = [] (const clap_preset_discovery_metadata_receiver_t*, const char*) {};
            receiver.set_description = [] (const clap_preset_discovery_metadata_receiver_t*, const char*) {};
            receiver.set_timestamps = [] (const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {};
            receiver.add_feature = [] (const clap_preset_discovery_metadata_receiver_t*, const char*) {};
            receiver.add_extra_info = [] (const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {};

            std::size_t files = 0;
            for (std::size_t l = 0; l < crawl.locations.size(); ++l)
            {
                if (crawl.locations[l].kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN)
                {
                    crawl.kind = CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN;
                    crawl.path = {};
                    provider->get_metadata (provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &receiver);
                    continue;
                }
                const juce::File where (crawl.locationPaths[l]);
                crawl.kind = CLAP_PRESET_DISCOVERY_LOCATION_FILE;
                juce::Array<juce::File> list;
                if (where.existsAsFile())
                    list.add (where);
                else if (where.isDirectory())
                    for (const auto& entry : juce::RangedDirectoryIterator (where, true, "*", juce::File::findFiles))
                    {
                        const auto f = entry.getFile();
                        auto wanted = crawl.extensions.isEmpty();
                        for (const auto& e : crawl.extensions)
                            wanted = wanted || e.isEmpty() || f.getFileExtension().equalsIgnoreCase ("." + e.trimCharactersAtStart ("."));
                        if (wanted)
                            list.add (f);
                        if (list.size() >= (int) maxFiles)
                            break;
                    }
                for (const auto& f : list)
                {
                    if (++files > maxFiles || crawl.found.size() >= maxPresets || discoveryStop.load())
                        break;
                    crawl.path = f.getFullPathName();
                    provider->get_metadata (provider, CLAP_PRESET_DISCOVERY_LOCATION_FILE, crawl.path.toRawUTF8(), &receiver);
                }
            }
        }
        provider->destroy (provider);
        for (auto& p : crawl.found)
            result.push_back (std::move (p));
    }
    return result;
}

// The crawls (round 190jg, measured on Windows: Surge XT's 2970 presets took 6.6 s): on a thread of their own, as the
// CLAP spec encourages (its factory is thread-safe); until one is done its plugin has no programs. One per module and
// session; at exit the running ones are told to stop (discoveryStop) and are waited for (they hold their module).
struct Discoveries
{
    std::mutex lock;
    std::map<juce::String, std::shared_ptr<const std::vector<FoundPreset>>> done;
    std::set<juce::String> running;
    std::vector<std::thread> threads;
    ~Discoveries()
    {
        discoveryStop.store (true);
        for (auto& t : threads)
            if (t.joinable())
                t.join();
    }
};
Discoveries& discoveries()
{
    static Discoveries d;
    return d;
}

thread_local ClapStateContext stateContextNow = ClapStateContext::project;
} // namespace

const std::vector<ClapInstance::Preset>& ClapInstance::presets()
{
    static const std::vector<Preset> none;
    if (presetLoadExt == nullptr)
        return none;
    if (! presetsFound)
    {
        std::shared_ptr<const std::vector<FoundPreset>> found;
        const auto key = description.fileOrIdentifier;
        {
            auto& d = discoveries();
            const std::lock_guard<std::mutex> l (d.lock);
            if (const auto it = d.done.find (key); it != d.done.end())
                found = it->second;
            else if (d.running.insert (key).second && ! discoveryStop.load())
                d.threads.emplace_back ([key, module = life->module] {
                    juce::Thread::setCurrentThreadName ("LYNX CLAP presets");
                    auto list = std::make_shared<const std::vector<FoundPreset>> (discoverPresets (*module));
                    auto& dd = discoveries();
                    const std::lock_guard<std::mutex> ll (dd.lock);
                    dd.done[key] = std::move (list);
                    dd.running.erase (key);
                });
        }
        if (found == nullptr)
            return none; // (still being found: the preset bar asks again when its menu opens)
        presetsFound = true;
        const auto ownId = juce::String::fromUTF8 (plugin->desc != nullptr && plugin->desc->id != nullptr ? plugin->desc->id : "");
        for (const auto& p : *found)
            if (p.pluginIds.isEmpty() || p.pluginIds.contains (ownId))
                presetList.push_back (p.preset);
    }
    return presetList;
}

void ClapParameter::setValue (float v)
{
    normalised.store (v);
    owner.queueParam (id, cookie, toPlain (v));
}

juce::String ClapParameter::getText (float v, int) const
{
    return owner.paramText (id, toPlain (v));
}

// The plugin's native UI inside a native child view (platform/NativeEmbed: HWND, NSView or X11 window).
class ClapEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit ClapEditor (ClapInstance& p)
        : AudioProcessorEditor (p),
          owner (p)
    {
        setSize (480, 320);

        if (owner.gui != nullptr && owner.gui->create (owner.plugin, platform::NativeEmbed::clapWindowApi(), false))
        {
            created = true;
            fitToPlugin();
        }

        addAndMakeVisible (embed);
        owner.editor = this;
        startTimer (20);
    }

    ~ClapEditor() override
    {
        stopTimer();

        if (created && ! faulted) // (round 190gg: under the crash guard; after a fault in it, never called again)
            crashguard::run ([this] {
                owner.gui->hide (owner.plugin);
                owner.gui->destroy (owner.plugin);
            });
    }

    void resized() override { embed.setBounds (getLocalBounds()); }

    // round 190oh (P3): FL's wrapper Settings › GUI marked this editor (platform::setEditorScaling): told its scale and
    // sized again
    void setScaleFactor (float) override
    {
        if (created && ! faulted)
            fitToPlugin();
    }

    void paint (juce::Graphics& g) override
    {
        if (! created)
        {
            g.fillAll (juce::Colours::black);
            g.setColour (juce::Colours::white);
            g.drawText (juce::String::fromUTF8 ("This plugin has no UI of its own"), getLocalBounds(), juce::Justification::centred);
        }
    }

private:
    // the scale the plugin is told and its reported size in this window's units (platform::editorContentScale /
    // editorSizeScale, round 190oh: the primary display's scale for an editor FL's GUI options did not mark)
    void fitToPlugin()
    {
        owner.gui->set_scale (owner.plugin, platform::editorContentScale (*this));
        uint32_t w = 0, h = 0;
        const auto scale = platform::editorSizeScale (*this);
        if (owner.gui->get_size (owner.plugin, &w, &h) && w > 0 && h > 0)
            setSize (juce::roundToInt (w / scale), juce::roundToInt (h / scale));
    }

    void timerCallback() override
    {
        // the native view needs a real window as parent: wait until on screen
        if (! created)
        {
            stopTimer();
            return;
        }
        if (! embed.create())
            return;

        stopTimer();
        clap_window_t window {};
        window.api = platform::NativeEmbed::clapWindowApi();
       #if JUCE_LINUX || JUCE_BSD
        window.x11 = embed.x11Window();
       #else
        window.ptr = embed.nativeHandle();
       #endif
        // round 190gg (B4): this runs from the message loop, outside every other guard: a plugin that crashes building
        // its view in its new window is switched off (its window shows the crash notice) instead of ending LYNX
        if (! callPluginFromUi (owner, "opening its editor", [this, &window] {
                owner.gui->set_parent (owner.plugin, &window);
                owner.gui->show (owner.plugin);
            }))
            faulted = true;
    }

    ClapInstance& owner;
    platform::NativeEmbed embed;
    bool created = false, faulted = false;
};

juce::AudioProcessorEditor* ClapInstance::createEditor()
{
    return gui != nullptr ? new ClapEditor (*this) : nullptr;
}
} // namespace

ScopedClapStateContext::ScopedClapStateContext (ClapStateContext context) noexcept : previous (stateContextNow)
{
    stateContextNow = context;
}

ScopedClapStateContext::~ScopedClapStateContext() { stateContextNow = previous; }

std::uint32_t clapStateContext() noexcept { return (std::uint32_t) stateContextNow; }

std::vector<ClapRemotePage> clapRemoteControls (juce::AudioPluginInstance& plugin)
{
    if (auto* clap = dynamic_cast<ClapInstance*> (&plugin))
        return clap->remotePages;
    return {};
}

ClapVoiceInfo clapVoiceInfo (juce::AudioPluginInstance& plugin)
{
    if (auto* clap = dynamic_cast<ClapInstance*> (&plugin))
    {
        clap->readVoiceInfo();
        return clap->voiceInfo;
    }
    return {};
}

bool clapDeclaresTail (const juce::AudioPluginInstance& plugin)
{
    const auto* clap = dynamic_cast<const ClapInstance*> (&plugin);
    return clap != nullptr && clap->declaresTail();
}

std::pair<int, int> clapThreadPoolUse (juce::AudioPluginInstance& plugin)
{
    if (auto* clap = dynamic_cast<ClapInstance*> (&plugin))
        return clap->threadPoolUse();
    return {};
}

juce::String describeClapPlugin (juce::AudioPluginInstance& plugin)
{
    auto* clap = dynamic_cast<ClapInstance*> (&plugin);
    if (clap == nullptr)
        return "not a CLAP plugin\n";
    juce::String out;
    out << "plugin=" << plugin.getName() << " id=" << juce::String::fromUTF8 (clap->plugin->desc != nullptr ? clap->plugin->desc->id : "?") << "\n";
    out << "extensions=" << clap->extensionsOffered().joinIntoString (" ") << "\n";
    out << "parameters=" << plugin.getParameters().size() << "\n";
    out << "latency=" << plugin.getLatencySamples() << " tail_seconds=" << plugin.getTailLengthSeconds() << "\n";
    const auto voices = clapVoiceInfo (plugin);
    out << "voice_info=" << (voices.valid ? juce::String (voices.count) + "/" + juce::String (voices.capacity) + (voices.overlappingNotes ? " overlapping" : "") : juce::String ("none")) << "\n";
    const auto pages = clapRemoteControls (plugin);
    out << "remote_pages=" << (int) pages.size();
    for (std::size_t i = 0; i < pages.size() && i < 4; ++i)
        out << (i == 0 ? " [" : ", ") << pages[i].section << "/" << pages[i].name;
    out << (pages.empty() ? "" : "]") << "\n";
    // (the crawl runs on its own thread: a test waits for it, up to 60 s)
    const auto crawlStart = juce::Time::getMillisecondCounterHiRes();
    while (clap->presetsPending() && juce::Time::getMillisecondCounterHiRes() - crawlStart < 60000.0)
        juce::Thread::sleep (20);
    out << "preset_crawl_ms=" << juce::String (juce::Time::getMillisecondCounterHiRes() - crawlStart, 0) << " (background thread)\n";
    const auto& presetList = clap->presets();
    out << "presets=" << (int) presetList.size();
    for (std::size_t i = 0; i < presetList.size() && i < 5; ++i)
        out << (i == 0 ? " [" : ", ") << presetList[i].name;
    out << (presetList.empty() ? "" : "]") << "\n";
    return out;
}


// --- format ----------------------------------------------------------------------------

void ClapPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& path)
{
    if (! fileMightContainThisPluginType (path))
        return;

    const auto module = ClapModule::open (path);

    if (module == nullptr)
        return;

    const auto count = module->factory->get_plugin_count (module->factory);

    for (uint32_t i = 0; i < count; ++i)
        if (const auto* d = module->factory->get_plugin_descriptor (module->factory, i))
            results.add (new juce::PluginDescription (describe (*d, juce::File (path), (int) count)));
}

bool ClapPluginFormat::fileMightContainThisPluginType (const juce::String& path)
{
    const juce::File f (path);
    return f.hasFileExtension (".clap") && f.exists();
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier (const juce::String& path)
{
    return juce::File (path).getFileNameWithoutExtension();
}

bool ClapPluginFormat::pluginNeedsRescanning (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).getLastModificationTime() != d.lastFileModTime;
}

bool ClapPluginFormat::doesPluginStillExist (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).exists();
}

juce::StringArray ClapPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& directories, bool recursive, bool)
{
    juce::StringArray found;

    for (int i = 0; i < directories.getNumPaths(); ++i)
        for (const auto& f : directories[i].findChildFiles (juce::File::findFilesAndDirectories, recursive, "*.clap"))
            found.addIfNotAlreadyThere (f.getFullPathName());

    return found;
}

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    // the CLAP folders among this OS's standard plugin folders (platform/Paths)
    juce::FileSearchPath paths;
    const auto all = platform::standardPluginFolders();
    for (int i = 0; i < all.getNumPaths(); ++i)
        if (all[i].getFileName().equalsIgnoreCase ("CLAP") || all[i].getFileName() == ".clap" || all[i].getFileName() == "clap")
            paths.add (all[i]);
    return paths;
}

void ClapPluginFormat::createPluginInstance (const juce::PluginDescription& d, double sampleRate, int blockSize,
                                             PluginCreationCallback callback)
{
    auto module = ClapModule::open (d.fileOrIdentifier);

    if (module == nullptr)
    {
        callback (nullptr, juce::String::fromUTF8 ("could not load ") + d.fileOrIdentifier);
        return;
    }

    const auto count = module->factory->get_plugin_count (module->factory);

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* desc = module->factory->get_plugin_descriptor (module->factory, i);

        if (desc == nullptr || idHash (desc->id) != d.uniqueId)
            continue;

        // the host must exist before create_plugin; the instance is created with the plugin afterwards
        auto instance = std::unique_ptr<ClapInstance> (new ClapInstance (module, nullptr, d));
        const auto* plugin = module->factory->create_plugin (module->factory, ClapInstance::hostFor (instance.get()), desc->id);

        if (plugin == nullptr)
            break;

        instance->plugin = plugin;

        if (! instance->initialise())
        {
            callback (nullptr, juce::String::fromUTF8 ("the plugin refused to start"));
            return;
        }

        instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
        callback (std::move (instance), {});
        return;
    }

    callback (nullptr, juce::String::fromUTF8 ("plugin not found in file ") + d.fileOrIdentifier);
}

// (a format goes with its plugin host: when that is on the message thread, nothing stays queued behind it)
ClapPluginFormat::~ClapPluginFormat() { finishPendingTeardowns(); }

void ClapPluginFormat::finishPendingTeardowns() { ClapLife::drain(); }

int ClapPluginFormat::pendingTeardowns()
{
    const std::lock_guard<std::mutex> l (ClapLife::pending().lock);
    return (int) ClapLife::pending().list.size();
}

int ClapPluginFormat::deferredTeardowns() { return ClapLife::pending().deferred.load(); }

void ClapPluginFormat::setTeardownWaitMs (int ms) { ClapLife::pending().waitMs.store (juce::jmax (0, ms)); }

void addPluginFormats (juce::AudioPluginFormatManager& manager)
{
    manager.addDefaultFormats();
    manager.addFormat (new ClapPluginFormat());
    manager.addFormat (new Vst2PluginFormat());
    manager.addFormat (new JsfxPluginFormat()); // (round 190pj, issue #241)
}

std::string clapPluginId (const juce::PluginDescription& description)
{
    if (description.pluginFormatName != ClapPluginFormat::formatName)
        return {};
    auto module = ClapModule::open (description.fileOrIdentifier);
    if (module == nullptr || module->factory == nullptr)
        return {};
    const auto count = module->factory->get_plugin_count (module->factory);
    for (std::uint32_t i = 0; i < count; ++i)
        if (const auto* d = module->factory->get_plugin_descriptor (module->factory, i); d != nullptr && d->id != nullptr && idHash (d->id) == description.uniqueId)
            return d->id;
    return {};
}
bool setClapNoteExpressions (juce::AudioPluginInstance& plugin, float bendRange)
{
    if (auto* clap = dynamic_cast<ClapInstance*> (&plugin))
        return clap->setNoteExpressions (bendRange);
    return false;
}
} // namespace lynx
