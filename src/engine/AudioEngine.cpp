#include "engine/AudioEngine.h"
#include "core/VideoPlayer.h"

#include "core/Comping.h"
#include "core/CountIn.h"
#include "core/EdisonFx.h"
#include "core/FlSettings.h"
#include "core/LiveSession.h"

#include "core/SessionIds.h"
#include "core/StandIns.h"
#include "engine/SessionPack.h"

#include "core/ClipOps.h"
#include "core/DetailChain.h"
#include "core/SessionOps.h"
#include "core/MarkerPlan.h"
#include "engine/AudioLogger.h"

#include "core/HardwareInsert.h"
#include "core/Patcher.h"
#include "core/ValueMath.h"
#include "platform/Paths.h"
#include "engine/FlStudio.h"

#include <limits>
#include <map>
#include <thread>

#include "engine/Text.h"
#include "ui/I18n.h"

#include "core/FlpImport.h"
#include "core/AlsImport.h"
#include "core/FlmImport.h"
#include "core/LoadTrace.h"
#include "core/Instruments.h"
#include "core/Denormals.h"
#include "core/Dither.h"
#include "core/ProjectSafety.h"
#include "core/SoftClip.h"
#include "core/Scheduler.h"
#include "engine/ChannelRackProcessor.h"
#include "engine/DllAsioDevice.h"
#include "engine/PipeWireDevice.h"
#include "platform/LinuxRealtime.h"
#include "engine/Metronome.h"
#include "core/NoteTools.h"
#include "core/VfxScript.h"
#include "core/SamplePrecompute.h"
#include "engine/PreviewPlayer.h"
#include "engine/PluginSandbox.h"
#include "core/PluginPresets.h"
#include "flp/Flp.h"

#if JUCE_WINDOWS
 #include <windows.h>
#elif JUCE_MAC
 #include <pthread.h>
 #include <sys/qos.h>
#elif JUCE_LINUX || JUCE_BSD
 #include <pthread.h>
 #include <sched.h>
#endif

namespace lynx
{
using IONode = juce::AudioProcessorGraph::AudioGraphIOProcessor;

// The FL Studio installation (registry, else the usual folders) to resolve %FLStudio...%.
FlPathRoots findFlStudioRoots()
{
    FlPathRoots roots;
    const auto fl = findFlStudio();

    if (fl.installDir.isDirectory())
    {
        roots.factoryData = fl.installDir.getFullPathName().toStdString();
        roots.data = fl.installDir.getChildFile ("Data").getFullPathName().toStdString();
    }

    if (fl.userData.isDirectory())
        roots.userData = fl.userData.getFullPathName().toStdString();
    // FL's Shared data folder and its Downloads (issue #124: FL Cloud sounds and other samples kept there)
    if (fl.sharedData.isDirectory())
    {
        roots.sharedData = fl.sharedData.getFullPathName().toStdString();
        roots.downloadsData = fl.sharedData.getChildFile ("Downloads").getFullPathName().toStdString();
    }

    roots.home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toStdString();
    return roots;
}

const natives::FlAccess& flStudioAccess()
{
    static const natives::FlAccess cached = [] {
        natives::FlAccess access;
        access.roots = findFlStudioRoots();
        access.fileExists = [] (const std::string& path) { return platform::localFile (path).existsAsFile(); };
        access.readFile = [] (const std::string& path) {
            juce::MemoryBlock data;
            platform::localFile (path).loadFileAsData (data);
            const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
            return std::vector<std::uint8_t> (bytes, bytes + data.getSize());
        };
        access.listPresets = [] (const std::string& folder) {
            // round 190gd: a folder's listing is kept for a few seconds. Listing the generators' engines walked FL's whole
            // Plugin database\Generators tree once per engine (~40 walks, ~0.5 s of every start and of every Browser /
            // Add menu refresh); FL's own preset folders change when FL or the user saves a preset, seconds apart at most
            struct Listing
            {
                std::vector<std::string> files;
                juce::uint32 madeMs = 0;
            };
            static std::mutex lock;
            static std::map<std::string, Listing> listings;
            const auto now = juce::Time::getMillisecondCounter();
            {
                const std::scoped_lock l (lock);
                if (const auto it = listings.find (folder); it != listings.end() && now - it->second.madeMs < 5000)
                    return it->second.files;
            }
            std::vector<std::string> out;
            auto files = platform::localFile (folder).findChildFiles (juce::File::findFiles, true, "*.fst;*.tbio;*.gmsynth;*.mrp");
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) {
                return a.getFullPathName().compareNatural (b.getFullPathName()) < 0;
            });
            for (const auto& f : files)
                out.push_back (f.getFullPathName().toStdString());
            const std::scoped_lock l (lock);
            listings[folder] = { out, juce::Time::getMillisecondCounter() };
            return out;
        };
        // (FL Keys' raw wavetables become WAV files of LYNX's settings folder, like an .flp's embedded samples)
        access.saveEmbedded = [] (const std::vector<std::uint8_t>& wav, const std::string& name) -> std::string {
            const auto folder = PluginHost::settingsFolder().getChildFile ("flp-embedded");
            folder.createDirectory();
            const auto hash = juce::String::toHexString ((juce::int64) juce::DefaultHashFunctions::generateHash (
                juce::String::createStringFromData (wav.data(), (int) std::min<std::size_t> (wav.size(), 65536)) + juce::String ((juce::int64) wav.size()),
                0x7FFFFFFF));
            const auto file = folder.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (name.c_str())) + "-" + hash + ".wav");
            if (! file.existsAsFile() && ! file.replaceWithData (wav.data(), wav.size()))
                return {};
            return file.getFullPathName().toStdString();
        };
        return access;
    }();
    return cached;
}

namespace
{
// Audio workers with real-time priority (MMCSS "Pro Audio" on Windows, like the driver; QoS class on macOS; SCHED_FIFO
// on Linux when allowed).
void raiseAudioThreadPriority()
{
   #if JUCE_WINDOWS
    using AvSet = void* (__stdcall*) (const wchar_t*, unsigned long*);
    static const auto avSet = [] () -> AvSet {
        if (auto* avrt = LoadLibraryW (L"avrt.dll"))
            return reinterpret_cast<AvSet> (reinterpret_cast<void*> (GetProcAddress (avrt, "AvSetMmThreadCharacteristicsW")));
        return nullptr;
    }();
    // FL's audio worker threads also raise their MMCSS priority to "high" (FLEngine_x64.dll, REA round 87)
    using AvPriority = int (__stdcall*) (void*, int);
    static const auto avPriority = [] () -> AvPriority {
        if (auto* avrt = LoadLibraryW (L"avrt.dll"))
            return reinterpret_cast<AvPriority> (reinterpret_cast<void*> (GetProcAddress (avrt, "AvSetMmThreadPriority")));
        return nullptr;
    }();
    unsigned long task = 0;

    if (auto* handle = avSet != nullptr ? avSet (L"Pro Audio", &task) : nullptr)
    {
        if (avPriority != nullptr)
            avPriority (handle, 1 /* AVRT_PRIORITY_HIGH */);
    }
    else
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
   #elif JUCE_MAC
    // the highest quality-of-service class: scheduled like the UI's own interactive work, on the performance cores
    pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);
   #elif JUCE_LINUX || JUCE_BSD
    // real-time FIFO scheduling when the user may have it (audio group / rtkit limits, as JACK and PipeWire set up);
    // below the usual JACK/PipeWire audio thread priorities. Without the permission the thread stays as it is.
    sched_param param {};
    param.sched_priority = juce::jlimit (sched_get_priority_min (SCHED_FIFO), sched_get_priority_max (SCHED_FIFO), 70);
    if (pthread_setschedparam (pthread_self(), SCHED_FIFO, &param) != 0)
        rt::noteAudioThread (false); // RealtimeKit / the portal may grant it (round 161)
   #endif
}

int workerCount()
{
    // diagnostics: LYNX_WORKERS=n fixes the number of extra threads
    if (const auto forced = juce::SystemStats::getEnvironmentVariable ("LYNX_WORKERS", {}); forced.isNotEmpty())
        return juce::jlimit (0, 31, forced.getIntValue());

    // the audio thread works too; leave one core free for the UI
    const auto cores = (int) std::thread::hardware_concurrency();
    return juce::jlimit (0, 7, cores - 2);
}
juce::File midiSettingsFile() { return PluginHost::settingsFolder().getChildFile ("midi-settings.txt"); }
juce::File audioDeviceFile() { return PluginHost::settingsFolder().getChildFile ("audio-device.xml"); }
} // namespace

// Saves the audio device setup whenever it changes (Settings › Audio, the setup wizard, a driver's own panel), so
// the next start opens the same device.
struct AudioEngine::DeviceStateSaver final : public juce::ChangeListener
{
    explicit DeviceStateSaver (AudioEngine& e) : engine (e) { engine.deviceManager.addChangeListener (this); }
    ~DeviceStateSaver() override { engine.deviceManager.removeChangeListener (this); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { engine.saveAudioDeviceState(); }
    AudioEngine& engine;
};

// written by fileJobs: the write ends with FlushFileBuffers on Windows, which took 2 s on a busy disk and froze the UI
// after every device change (issue #13). In order, so the newest setup is the one left on disk.
void AudioEngine::saveAudioDeviceState()
{
    if (auto xml = deviceManager.createStateXml())
        fileJobs.post ([text = xml->toString(), file = audioDeviceFile()] { file.replaceWithText (text); });
}

void AudioEngine::restoreAudioDeviceState (const juce::XmlElement* state)
{
    deviceStateSaver = nullptr;
    fileJobs.post ([text = state != nullptr ? state->toString() : juce::String(), restore = state != nullptr, file = audioDeviceFile()] {
        if (restore)
            file.replaceWithText (text);
        else
            file.deleteFile();
    });
    fileJobs.waitIdle(); // (after the saves still queued, and done when this returns)
}

namespace
{
AudioEngine::RecordingSettings loadRecordingSettings();
}

struct AudioEngine::Drainer final : juce::TimeSliceClient
{
    explicit Drainer (AudioEngine& e) : engine (e) {}
    int useTimeSlice() override
    {
        std::vector<std::shared_ptr<Take>> list;
        {
            const juce::ScopedLock lock (engine.drainLock);
            list = engine.draining;
        }
        for (auto& take : list)
        {
            const juce::ScopedLock lock (take->drainMutex);
            take->drain();
        }
        return list.empty() ? 50 : 5;
    }
    AudioEngine& engine;
};


namespace
{
// the engine whose project the plugin hold is for (round 190ap): the hold ends when that engine opens another project
std::atomic<const AudioEngine*> pluginHoldOwner { nullptr };
} // namespace

AudioEngine::AudioEngine (bool openAudioDevice, int workerThreads, bool userPluginList, bool deferDeviceOpen)
    : workers (std::make_unique<WorkerPool> (workerThreads >= 0 ? workerThreads : workerCount(), raiseAudioThreadPriority)),
      plugins (userPluginList),
      song (Song::makeDefault())
{
    boostDeviceThread = juce::SystemStats::getEnvironmentVariable ("LYNX_DEVICE_BOOST", {}) != "0";
    inputSlot.fill (-1);
    outputSlot.fill (-1);
    // VFX Script (round 165d): the audio thread waits a bounded time for the Python thread (exports wait for it)
    vfx::script::setRealtime (openAudioDevice);
    const auto mark = [] (const char* step) { if (startupMark != nullptr) startupMark (step); };
    mark ("  engine: workers + default song");
    if (juce::JUCEApplicationBase::getInstance() != nullptr) // issue #36: retired racks are freed from their own message
        rackExchange.dispose = [this] (const RackState* rack) { rackDisposer.add (rack); };
    publishSong();
    buildInitialGraph();
    mark ("  engine: graph");

    if (openAudioDevice)
    {
        player.setProcessor (&graph);
        // the settings that do not need the device (the window reads them; round 190go: built before the device opens)
        if (rackProcessor != nullptr)
            rackProcessor->getRenderer().setProfiling (true); // per-unit CPU for the CPU meter and the glitch log
        midiPrefs = midiPrefsShared = prefs::MidiPrefs::fromText (midiSettingsFile().loadFileAsString().toStdString());
        performanceMidiChannel.store (midiPrefs.performanceChannel);
        controllerIo.loadProfiles (controllerProfilesFolder());
        recordingSettings = loadRecordingSettings();
        saveGlobalLinks = true;
        devicePending = midiPending = true;
        if (! deferDeviceOpen)
        {
            this->openAudioDevice();
            openMidiDevices();
        }
    }

    startTimerHz (30); // recording and collection of retired snapshots
}

void AudioEngine::openAudioDevice()
{
    if (! std::exchange (devicePending, false))
        return;
    const auto mark = [] (const char* step) { if (startupMark != nullptr) startupMark (step); };

    // LYNX ASIO without installation: lynx-asio.dll next to LYNX.exe, when it is not registered (REA round 106)
    if (auto lynxAsio = makeBuiltInLynxAsioType())
    {
        deviceManager.getAvailableDeviceTypes(); // the standard types first
        deviceManager.addAudioDeviceType (std::move (lynxAsio));
    }
    // LYNX's own PipeWire client on Linux (round 161), listed when a PipeWire daemon answers
    if (auto pipeWire = makePipeWireDeviceType())
    {
        deviceManager.getAvailableDeviceTypes();
        deviceManager.addAudioDeviceType (std::move (pipeWire));
    }
    mark ("  engine: device types");

    // 0 inputs, 2 outputs; the device of the last session (Settings › Audio) if it is still there, else the default.
    const auto saved = juce::XmlDocument::parse (audioDeviceFile());
    // (round 190go) the MIDI ports the saved setup names open with the MIDI devices (openMidiDevices: LYNX's MIDI
    // settings decide which inputs are open anyway), not on the audio device's way: JUCE opened them here (~0.1-0.2 s)
    if (saved != nullptr)
    {
        savedMidiOutput = { saved->getStringAttribute ("defaultMidiOutput"), saved->getStringAttribute ("defaultMidiOutputDevice") };
        saved->removeAttribute ("defaultMidiOutput");
        saved->removeAttribute ("defaultMidiOutputDevice");
        saved->deleteAllChildElementsWithTagName ("MIDIINPUT");
    }
    auto fresh = saved == nullptr;
    if (deviceManager.initialise (0, 2, saved.get(), true).isNotEmpty() && saved != nullptr)
    {
        deviceManager.initialise (0, 2, nullptr, true);
        fresh = true;
    }
    if (fresh)
    {
        choosePreferredDriver(); // Linux: PipeWire, then JACK, then ALSA
        chooseSafeBufferSize(); // no saved setup: a buffer with room for heavy projects (Settings › Audio changes it)
    }
    mark ("  engine: device opened");
    deviceManager.addAudioCallback (this);
    deviceStateSaver = std::make_unique<DeviceStateSaver> (*this);
    prefetcher = std::make_unique<StreamPrefetcher> (transport);
    prefetcher->update (song, samples);
    // a project set while the device was not open yet (round 190go: the start-up song, built with the window): its
    // inputs, FL insert inputs, hardware outputs and inserts open now, as they do when a project is opened
    if (followProjectOnDevice())
        publishSong();
    mark ("  engine: callback + settings");
}

void AudioEngine::prewarmAudioDevice()
{
    prewarmAsioDevice (audioDeviceFile());
}

void AudioEngine::openMidiDevices()
{
    if (! std::exchange (midiPending, false))
        return;
    midiOpen = true;
    // the default MIDI output of the last session (see openAudioDevice), by its id, else by its name (as JUCE does)
    if (savedMidiOutput.name.isNotEmpty() || savedMidiOutput.identifier.isNotEmpty())
        for (const auto& out : juce::MidiOutput::getAvailableDevices())
            if (out.identifier == savedMidiOutput.identifier || (savedMidiOutput.identifier.isEmpty() && out.name == savedMidiOutput.name))
            {
                deviceManager.setDefaultMidiOutputDevice (out.identifier);
                break;
            }
    enableAllMidiInputs();
    midiSync.configure (midiPrefs);
    midiSync.sendAllNotesOff(); // FL's "Send all notes off": on init and after a render
    deviceManager.addMidiInputDeviceCallback ({}, this); // {} = all inputs enabled
    midiDevicesChanged = juce::MidiDeviceListConnection::make ([this] { enableAllMidiInputs(); });
    if (startupMark != nullptr)
        startupMark ("  engine: MIDI inputs");
}

AudioLogger& AudioEngine::getAudioLogger()
{
    if (audioLogger == nullptr)
        audioLogger = std::make_unique<AudioLogger> (meters.logTap);
    return *audioLogger;
}

AudioEngine::~AudioEngine()
{
    if (pluginHoldOwner.load() == this) // (round 190ap: the hold goes with its project's engine)
    {
        pluginHoldOwner.store (nullptr);
        endPluginHold();
    }
    previewAlive->store (false); // (round 190aq: a preview still being prepared ends without touching the engine)
    previewJobs.stop();
    stopTimer();
    rackExchange.dispose = nullptr; // (from here the racks are freed at once)
    rackDisposer.flush();
    audioLogger.reset(); // closes the log file

    // the takes in progress are closed as they are (complete WAVs); their journals stay, so the next start offers to
    // put them on the Playlist (round 163)
    {
        const juce::SpinLock::ScopedLockType lock (takeLock);
        takes.fill (nullptr);
        takeCount = 0;
    }
    {
        const juce::ScopedLock lock (drainLock);
        draining.clear();
    }
    if (drainer != nullptr)
        recordThread.removeTimeSliceClient (drainer.get());
    for (auto& take : openTakes)
    {
        take->stream->finish();
        const juce::ScopedLock lock (take->drainMutex);
        take->drain();
        take->writer.close();
    }
    openTakes.clear();

    recordThread.stopThread (2000);
    prefetcher = nullptr;
    midiDevicesChanged = {};
    deviceStateSaver = nullptr;
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
    player.setProcessor (nullptr);
    graph.clear(); // the nodes reference rackExchange and transport
}

void AudioEngine::buildInitialGraph()
{
    // The output node inherits the graph's channel count at the moment it is created.
    // Without this the graph still has 0 outputs and JUCE silently rejects the connections.
    graph.setPlayConfigDetails (0, 2, transport.sampleRate.load(), 512);

    auto output = graph.addNode (std::make_unique<IONode> (IONode::audioOutputNode));
    auto rack = graph.addNode (std::make_unique<ChannelRackProcessor> (transport, rackExchange, liveEvents, midiEvents, meters, workers.get()));
    rackProcessor = dynamic_cast<ChannelRackProcessor*> (rack->getProcessor());
    if (rackProcessor != nullptr)
    {
        rackProcessor->setLiveInput (&liveInput);
        rackProcessor->setHardwareOutput (&hwOutput);
        rackProcessor->setPerformance (performanceLive.get());
        // FL's own metronome sounds from its install (Data\System), LYNX's stand-ins without it (round 165z-a2)
        const auto& flData = flStudioAccess().roots.data;
        rackProcessor->setMetronome (std::make_unique<Metronome> (transport, flData.empty() ? juce::File() : platform::localFile (flData)));
    }
    auto previewOwned = std::make_unique<PreviewPlayer>();
    preview = previewOwned.get();
    preview->setTransport (&transport); // (round 190aq: "Preview in time" starts on its beats)
    preview->setVolume (controls::previewGain (previewVolume));
    preview->setEditorVolume (controls::previewGain (editorVolume));
    auto previewNode = graph.addNode (std::move (previewOwned));

    for (int channel = 0; channel < 2; ++channel)
    {
        graph.addConnection ({ { rack->nodeID, channel }, { output->nodeID, channel } });
        graph.addConnection ({ { previewNode->nodeID, channel }, { output->nodeID, channel } });
    }

    jassert (graph.getConnections().size() == 4);
}

std::atomic<int>& AudioEngine::offlineRendersRunning() noexcept
{
    static std::atomic<int> count { 0 };
    return count;
}

float AudioEngine::renderOffline (const RenderOptions& options)
{
    offlineRendersRunning().fetch_add (1, std::memory_order_acq_rel);
    const juce::ScopeGuard rendered { [] { offlineRendersRunning().fetch_sub (1, std::memory_order_acq_rel); } };
    const auto seconds = options.seconds;
    const auto sampleRate = options.sampleRate;
    auto* output = options.output;
    const auto& progress = options.progress;
    const auto tailSeconds = options.tailSeconds;
    const auto trimSamples = options.trimSamples;
    std::optional<Dither> dither;

    if (options.ditherBits > 0)
        dither.emplace (options.ditherBits, options.ditherShaping);

    constexpr int blockSize = 512;
    constexpr float silence = 3.0e-5f; // ~ -90 dB
    // "HQ for all plugins" (REA round 134): engines with FL render-quality settings use them (Sytrus) and HQ switches
    // are forced on; without it the export keeps the playback quality
    setOfflineRendering (options.hqAllPlugins);
    setRenderInProgress (true); // (whatever the quality: LYNX Soundfont waits for a font that is still loading, issue #138)
    setHqForAllPlugins (options.hqAllPlugins);
    setPluginsRenderMode (true); // and hosted plugins are told an export is running (FL: "notify about rendering mode")
    transport.sampleRate.store (sampleRate);
    // round 164c: JUCE's graph builds its render sequence on the message thread. Prepared from the Export window's
    // thread it rebuilt later (asynchronously) and rendered silence until then: the first ~0.05–2 s of the song were
    // lost (the transport kept moving). It is prepared on the message thread, and the render waits for that.
    {
        auto prepareGraph = [this, sampleRate] {
            graph.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            graph.prepareToPlay (sampleRate, blockSize);
        };
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        if (mm == nullptr || mm->isThisTheMessageThread())
            prepareGraph();
        else
        {
            struct Handshake
            {
                juce::WaitableEvent done;
                std::atomic<bool> abandoned { false };
            };
            auto handshake = std::make_shared<Handshake>();
            juce::MessageManager::callAsync ([prepareGraph, handshake] {
                if (handshake->abandoned.load())
                    return;
                prepareGraph();
                handshake->done.signal();
            });
            if (! handshake->done.wait (10000)) // (a message thread that is blocked: as before)
            {
                handshake->abandoned = true;
                prepareGraph();
            }
        }
    }

    if (rackProcessor != nullptr)
    {
        rackProcessor->setMetering (false); // nobody looks at the meters during the render
        rackProcessor->setMetronomeAllowed (options.includeMetronome);
    }

    lastRenderStats = {};
    // FL's init list first (play() would do it at the first block), so the plugins settle on those values too
    resetToInitialValues();
    int latency = getLatencySamples();
    if (options.pluginWarmUp)
        lastRenderStats.warmUp = warmUpPlugins (options.warmUp, sampleRate, blockSize, progress, latency);

    play();

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    // PDC trim with the latency the plugins report once they have settled
    const auto trim = (std::int64_t) juce::jmax (0, trimSamples + (options.trimPdc ? latency : 0));
    lastRenderStats.trimmedSamples = (int) trim;
    const auto musicSamples = (std::int64_t) (seconds * sampleRate) + trim;
    const auto totalSamples = musicSamples + (std::int64_t) (juce::jmax (0.0, tailSeconds) * sampleRate);
    std::int64_t quietSamples = 0;
    bool cancelled = lastRenderStats.warmUp.cancelled;

    vfx::script::setSongEnded (false);
    for (std::int64_t done = 0; done < totalSamples && ! cancelled; done += blockSize)
    {
        const auto n = (int) juce::jmin<std::int64_t> (blockSize, totalSamples - done);
        // the tail: the song has ended (VFX Script sees the transport stopped, like FL's render; round 167e)
        if (done >= musicSamples)
            vfx::script::setSongEnded (true);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
        view.clear();
        const auto before = juce::Time::getMillisecondCounterHiRes();
        graph.processBlock (view, midi);
        const auto afterGraph = juce::Time::getMillisecondCounterHiRes();
        lastRenderStats.graphMs += afterGraph - before;

        if (! options.keepOvers) // float WAV and the buffered export (normalize) keep what goes above 0 dBFS, like FL
            for (int ch = 0; ch < view.getNumChannels(); ++ch)
                clipOvers (view.getWritePointer (ch), n);

        const auto blockPeak = view.getMagnitude (0, n);
        peak = juce::jmax (peak, blockPeak);

        if (dither)
            for (int ch = 0; ch < view.getNumChannels(); ++ch)
                dither->process (view.getWritePointer (ch), n, ch);

        // PDC: the start is the latency's silence; the song's end arrives `trim` samples later
        if ((output != nullptr || options.capture) && done + n > trim)
        {
            const auto skip = (int) juce::jmax<std::int64_t> (0, trim - done);
            const auto beforeWrite = juce::Time::getMillisecondCounterHiRes();
            if (output != nullptr)
                output->writeFromAudioSampleBuffer (view, skip, n - skip);
            if (options.capture)
                options.capture (view.getReadPointer (0, skip), view.getReadPointer (1, skip), n - skip);
            lastRenderStats.writeMs += juce::Time::getMillisecondCounterHiRes() - beforeWrite;
        }

        transport.positionTicks.store (transport.positionTicks.load() + n * transport.ticksPerSample());

        // in the tail: half a second of silence ends it
        if (done + n > musicSamples)
        {
            quietSamples = blockPeak < silence ? quietSamples + n : 0;

            if (quietSamples > (std::int64_t) (sampleRate * 0.5))
                break;
        }

        if (progress && (done / blockSize) % 32 == 0 && ! progress ((double) done / (double) juce::jmax<std::int64_t> (1, totalSamples)))
        {
            cancelled = true;
            break;
        }
    }

    stop();
    vfx::script::setSongEnded (false);

    if (rackProcessor != nullptr)
    {
        rackProcessor->setMetering (true);
        rackProcessor->setMetronomeAllowed (true);
    }

    graph.releaseResources();
    setPluginsRenderMode (false);
    setOfflineRendering (false);
    setRenderInProgress (false);
    setHqForAllPlugins (false);
    return cancelled ? -1.0f : peak;
}

warmup::Result AudioEngine::warmUpPlugins (const warmup::Settings& settings, double sampleRate, int blockSize,
                                           const std::function<bool (double)>& progress, int& latency)
{
    // the hosted plugins, collected once: the timer may republish the song meanwhile (the instances stay the same)
    std::vector<juce::AudioPluginInstance*> hosted;
    forEachHostedPlugin ([&hosted] (juce::AudioPluginInstance& p, bool) { hosted.push_back (&p); });
    if (hosted.empty())
        return {}; // LYNX's own engines are ready as soon as they are created: nothing to wait for

    // keeps the units alive whatever the timer publishes during the warm-up
    std::vector<std::shared_ptr<Instrument>> keepInstruments;
    std::vector<std::shared_ptr<Effect>> keepEffects;
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        keepInstruments.push_back (getChannelInstrument ((int) i));
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
            keepEffects.push_back (getEffect ((int) t, (int) s));

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    int blocks = 0;

    // transport stopped at the start position: no note plays, and the automation / pattern events at that position
    // reach the plugins (the rack evaluates them every block), so they settle on the values the song starts with
    const auto result = warmup::run (
        settings, sampleRate,
        [&]() -> int {
            if (progress && ++blocks % 32 == 0 && ! progress (0.0))
                return -1;
            buffer.clear();
            midi.clear();
            graph.processBlock (buffer, midi);
            return blockSize;
        },
        [&hosted] {
            warmup::Hash h;
            for (auto* p : hosted)
            {
                h.add ((std::uint64_t) (std::int64_t) p->getLatencySamples());
                for (auto* param : p->getParameters())
                    h.addValue (param->getValue());
            }
            return h.value;
        },
        [] { return juce::Time::getMillisecondCounterHiRes() * 0.001; },
        [] (double s) { juce::Thread::sleep (juce::jlimit (1, 20, (int) std::lround (s * 1000.0))); });

    // a latency that changed (reported late) gets its PDC plan before the render, on the message thread as the
    // timer does it (renders from the Export window run on a thread of their own)
    auto republish = [this] {
        if (lastPublishedShape.currentLatencies() != lastPublishedShape.latencies)
        {
            ++latencyRepublishes;
            publishSong();
        }
        return getLatencySamples();
    };
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr || mm->isThisTheMessageThread())
        latency = republish();
    else
    {
        struct Reply
        {
            juce::WaitableEvent done;
            std::atomic<int> latency { 0 };
        };
        auto reply = std::make_shared<Reply>();
        reply->latency = getLatencySamples();
        juce::MessageManager::callAsync ([this, reply, republish] {
            reply->latency = republish();
            reply->done.signal();
        });
        reply->done.wait (3000);
        latency = reply->latency.load();
    }

    // LYNX's own units start the render from a clean state, as without the warm-up (free-running LFOs, the delay
    // compensation's lines); the hosted plugins are not reset: they keep what they settled on (a reset re-activates
    // them, which is what they had to settle from)
    if (rackProcessor != nullptr)
        rackProcessor->prepareToPlay (sampleRate, blockSize);
    return result;
}

void AudioEngine::forEachHostedPlugin (const std::function<void (juce::AudioPluginInstance&, bool notifyRender)>& visit) const
{
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument ((int) i).get()); p != nullptr && ! p->hasCrashed())
            visit (p->getInstance(), song.channels[i].plugin.notifyRender);

    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
            if (auto* p = dynamic_cast<PluginEffect*> (getEffect ((int) t, (int) s).get()); p != nullptr && ! p->hasCrashed())
                visit (p->getInstance(), song.mixer[t].effects[s].plugin.notifyRender);

    std::function<void (patcher::Graph*)> inGraph = [&] (patcher::Graph* graph) {
        if (graph == nullptr)
            return;
        for (int n = 0; n < graph->nodeCount(); ++n)
        {
            auto effect = graph->effectAt (n);
            auto instrument = graph->instrumentAt (n);
            if (auto* p = dynamic_cast<PluginEffect*> (effect.get()); p != nullptr && ! p->hasCrashed())
                visit (p->getInstance(), true);
            if (auto* p = dynamic_cast<PluginInstrument*> (instrument.get()); p != nullptr && ! p->hasCrashed())
                visit (p->getInstance(), true);
            inGraph (patcher::graphOf (effect.get()));
            inGraph (patcher::graphOf (instrument.get()));
        }
    };
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        inGraph (patcher::graphOf (getChannelInstrument ((int) i).get()));
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
            inGraph (patcher::graphOf (getEffect ((int) t, (int) s).get()));
}

ProcessTimes* AudioEngine::getProcessTimes() noexcept
{
    if (externalTimes != nullptr)
        return externalTimes;
    return rackProcessor != nullptr ? &rackProcessor->processTimes() : nullptr;
}

bool AudioEngine::previewClipThroughChannel (std::size_t clipIndex)
{
    if (clipIndex >= song.playlist.size())
        return false;
    const auto& c = song.playlist[clipIndex];
    if (c.kind != ClipKind::audio || c.channel < 0 || c.channel >= (int) song.channels.size() || c.length <= 0)
        return false;
    stopClipPreview(); // (the next preview replaces the last)
    const auto rate = transport.sampleRate.load() > 0.0 ? transport.sampleRate.load() : 44100.0;
    auto e = audioClipNote (song, c, clipIndex, ticksPerSample (std::max (song.bpm, 1.0), rate));
    e.sampleOffset = 0;
    if (! liveEvents.push (e))
        return false;
    clipPreviewChannel = c.channel;
    clipPreviewKey = e.key;
    const auto serial = ++clipPreviewSerial;
    // FL stops it at the clip's end (mouse-up and clicks elsewhere don't): the note's off then
    const auto ms = (int) std::ceil ((double) c.length * 60000.0 / (std::max (song.bpm, 1.0) * ppq)) + 10;
    juce::Timer::callAfterDelay (ms, [this, serial, alive = previewAlive] {
        if (alive->load() && serial == clipPreviewSerial)
            stopClipPreview();
    });
    return true;
}

void AudioEngine::stopClipPreview() noexcept
{
    // (the live note queue has one producer, the UI thread: play() / stop() called from the audio or a MIDI thread leave it
    // to the preview's own end)
    if (clipPreviewKey < 0 || ! juce::MessageManager::existsAndIsCurrentThread())
        return;
    NoteEvent e;
    e.type = NoteEvent::Type::off;
    e.channel = clipPreviewChannel;
    e.key = clipPreviewKey;
    liveEvents.push (e);
    clipPreviewChannel = clipPreviewKey = -1;
    ++clipPreviewSerial;
}

void AudioEngine::play() noexcept
{
    stopClipPreview(); // (live check 190ew PL-3: transport start ends a clip preview)
    if (! transport.playing.load())
    {
        resetToInitialValues();
        // Performance mode: the song plays from its Start marker (the zone before it is launched, not played)
        if (const auto zone = song.mode == PlayMode::song ? song.performanceZoneEnd() : 0; zone > 0 && transport.positionTicks.load() < (double) zone)
            seek ((double) zone);
        // FL's "Start" marker: Play from the song's beginning starts at the marker (Song mode)
        if (song.mode == PlayMode::song && transport.seekRequest.load() < 0.0 && transport.positionTicks.load() <= 0.0)
            if (const auto start = transport.markerStart.load(); start > 0)
                transport.seekRequest.store ((double) start);
    }
    transport.playing.store (true);
}

// FL resets its "initialized controls" whenever the song position is set in song mode (playing starts, a jump):
// FUN_011e7470 plays the song's init list (PTR_DAT_015dfc48) before the pattern events (REA round 146).
void AudioEngine::resetToInitialValues() noexcept
{
    if (song.initialValues.empty() || song.mode != PlayMode::song)
        return;
    try
    {
        auto differs = false;
        for (const auto& v : song.initialValues)
        {
            const auto now = currentTargetValue (v.target);
            differs = differs || (now >= 0.0f && std::abs (now - v.value) > 1.0e-6f);
        }
        if (! differs)
            return;
        std::vector<Song::InitialValue> plugins;
        editSong ([&plugins] (Song& s) { s.applyInitialValues ([&plugins] (const AutomationTarget& t, float v) { plugins.push_back ({ t, v }); }); },
                  false);
        for (const auto& v : plugins)
        {
            if (v.target.kind == AutomationTargetKind::channelParam)
                if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument (v.target.index).get()))
                    p->setParameterFromUi (v.target.param, v.value);
            if (v.target.kind == AutomationTargetKind::effectParam)
                if (auto* p = dynamic_cast<PluginEffect*> (getEffect (v.target.index, v.target.slot).get()))
                    p->setParameterFromUi (v.target.param, v.value);
        }
    }
    catch (...)
    {
    }
}

void AudioEngine::stop() noexcept
{
    stopClipPreview(); // (live check 190ew PL-3: and transport stop)
    waitingForInput.store (false);
    transport.countIn.store (0.0);
    transport.playing.store (false);
}

std::vector<char> AudioEngine::lowLatencyTracksNow() const
{
    return recordOptions.lowLatencyRecording ? lowlatency::bypassTracks (song, armedInserts) : std::vector<char> {};
}

void AudioEngine::refreshLowLatency()
{
    if (lowLatencyTracksNow() != publishedLowLatency)
        publishSong();
}

int AudioEngine::lowLatencyBypassedLatency (int track, int slot) const noexcept
{
    const auto& saved = lastPublishedShape.lowLatencySaved;
    return isSlotLowLatencyBypassed (track, slot) && (std::size_t) track < saved.size() && (std::size_t) slot < saved[(std::size_t) track].size()
               ? saved[(std::size_t) track][(std::size_t) slot]
               : 0;
}

int AudioEngine::lowLatencyBypassedCount() const noexcept
{
    int count = 0;
    for (const auto mask : lastPublishedShape.lowLatencySlots)
        for (auto m = (unsigned) mask; m != 0; m &= m - 1)
            ++count;
    return count;
}

void AudioEngine::setRecordOptions (const RecordOptions& options)
{
    recordOptions = options;
    recordOptions.countInBars = juce::jlimit (0, countin::maxBars, options.countInBars);
    recordOptions.countInLength = countin::lengthFor (recordOptions.countInBars, options.countInLength); // (issue #152)
    recordOptions.preRollBars = juce::jlimit (0, 8, options.preRollBars);
    recordOptions.postRollBars = juce::jlimit (0, 8, options.postRollBars);
    recordOptions.catchWindow = juce::jlimit<std::int64_t> (0, ppq, options.catchWindow);
    compensatePlayed.store (options.compensateOutput);
    updateLinearSong();
    refreshLowLatency(); // (round 190aq: the option switched while an insert is live)
    sendChangeMessage();
}

void AudioEngine::startPlayback()
{
    if (! transport.recording.load())
    {
        play();
        return;
    }

    // pre-roll and punch (round 163): playback starts before the range the take keeps; post-roll stops it after
    activePunch.reset();
    punchStopAt = -1;
    auto startAt = (std::int64_t) transport.positionTicks.load();
    if (audioArmed && recordOptions.recordAudio)
    {
        const auto here = (std::int64_t) std::llround (transport.positionTicks.load());
        const auto punching = recordOptions.punchLoopRegion && song.mode == PlayMode::song && song.hasLoop();
        const auto in = punching ? song.loopStart : here;
        const auto out = punching ? song.loopEnd : std::numeric_limits<std::int64_t>::max() / 4;
        // (seconds: the ADR window's pre-roll, round 174d)
        auto secondsBack = [this] (std::int64_t at, double seconds) {
            const auto from = std::max (0.0, song.secondsForTicks (at) - seconds);
            return std::max<std::int64_t> (0, at - (std::int64_t) std::llround (videotrack::ticksForSeconds (song, from)));
        };
        auto secondsOn = [this] (std::int64_t at, double seconds) {
            return std::max<std::int64_t> (0, (std::int64_t) std::llround (videotrack::ticksForSeconds (song, song.secondsForTicks (at) + seconds)) - at);
        };
        const auto pre = recordOptions.preRollSeconds >= 0.0 ? secondsBack (in, recordOptions.preRollSeconds)
                         : recordOptions.preRollBars > 0 ? countInLength (song, recordOptions.preRollBars, std::max<std::int64_t> (0, in - 1)) : 0;
        if (punching || pre > 0)
        {
            activePunch = PunchRange { in, out };
            if (punching)
                punchStopAt = out + (recordOptions.postRollSeconds >= 0.0 ? secondsOn (out, recordOptions.postRollSeconds)
                                     : recordOptions.postRollBars > 0 ? countInLength (song, recordOptions.postRollBars, out) : 0);
            startAt = std::max<std::int64_t> (0, in - pre);
            transport.seekRequest.store ((double) startAt);
        }
        updateLinearSong();
    }

    if (recordOptions.waitForInput)
    {
        waitingForInput.store (true);
        sendChangeMessage();
        return;
    }

    if (recordOptions.countInBars > 0)
    {
        // the notes played during the count-in belong to this take (round 170d)
        const auto length = countInLength (song, recordOptions.countInBars, startAt);
        takeCatch = {};
        takeCatch.start = startAt;
        takeCatch.window = recordOptions.catchWindow;
        takeCatch.songMode = song.mode == PlayMode::song;
        if (! song.patterns.empty())
            takeCatch.wrap = countInWrap (length, song.patterns[(std::size_t) juce::jlimit (0, (int) song.patterns.size() - 1, song.currentPattern)]
                                                      .effectiveLength (song.beatsPerBar));
        transport.countIn.store ((double) length);
        sendChangeMessage();
        return;
    }

    play();
}

void AudioEngine::startOnInput() noexcept
{
    if (waitingForInput.exchange (false))
        transport.playing.store (true);
}

void AudioEngine::setInsertArmed (int track, bool armed)
{
    const auto it = std::find (armedInserts.begin(), armedInserts.end(), track);
    const auto wasArmed = it != armedInserts.end();

    if (armed == wasArmed || track <= 0)
        return;

    if (armed && isInsertRecordSafe (track)) // record safe (round 163): never armed by accident
    {
        notify (InputIssue::recordSafe, track);
        return;
    }

    if (armed)
        armedInserts.push_back (track);
    else
        armedInserts.erase (it);

    if (track < maxMixerTracks)
        liveInput.armed[(std::size_t) track].store (armed);

    if (armed && ! audioArmed)
        setAudioRecordingArmed (true);
    else if (! armed && armedInserts.empty() && audioArmed)
        setAudioRecordingArmed (false);

    refreshLowLatency(); // (round 190aq: "Low latency while recording" follows the arm switches)
    sendChangeMessage();
}

bool AudioEngine::isInsertArmed (int track) const noexcept
{
    return std::find (armedInserts.begin(), armedInserts.end(), track) != armedInserts.end();
}
void AudioEngine::rewind() noexcept { transport.rewindRequested.store (true); }
void AudioEngine::jumpTo (double tick) noexcept
{
    tick = juce::jmax (0.0, tick);
    const auto quantize = quantizeSongJumps.load();
    if (! transport.playing.load() || quantize <= 0 || song.mode != PlayMode::song)
    {
        seek (tick);
        return;
    }
    // the jump waits for the next beat / bar(s) (FL's "Quantize song jumps"); the audio thread makes it there
    const auto at = (double) markers::quantizedJumpTick (song, transport.positionTicks.load(), quantize);
    transport.pendingJumpTo.store (tick);
    transport.pendingJumpAt.store (at);
}

void AudioEngine::jumpToMarker (int index) noexcept
{
    const auto& list = song.markers();
    if (index < 0 || index >= (int) list.size())
        return;
    if (song.mode != PlayMode::song)
        editSong ([] (Song& s) { s.mode = PlayMode::song; }, false);
    jumpTo ((double) list[(std::size_t) index].tick);
}

void AudioEngine::jumpToNextMarker (bool forward) noexcept
{
    // from where the song is (or where a quantized jump is going)
    const auto pending = transport.pendingJumpAt.load() >= 0.0 ? transport.pendingJumpTo.load() : -1.0;
    const auto here = pending >= 0.0 ? pending : transport.positionTicks.load();
    const auto& list = song.markers();
    int best = -1;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto t = (double) list[(std::size_t) i].tick;
        if (forward ? t > here + 0.5 : t < here - 0.5)
        {
            if (forward)
            {
                best = i;
                break;
            }
            best = i;
        }
    }
    if (best >= 0)
        jumpToMarker (best);
    else if (! forward)
        jumpTo (0.0);
}

void AudioEngine::seek (double ticks) noexcept
{
    transport.pendingJumpAt.store (-1.0); // a direct seek replaces a quantized jump still waiting
    if (transport.playing.load())
        resetToInitialValues(); // a jump while the song plays (FL sets the position the same way)
    transport.seekRequest.store (juce::jmax (0.0, ticks));
}

void AudioEngine::copyScope (float* destination, int count) const noexcept
{
    count = juce::jmin (count, scopeSize);
    auto r = (scopeWrite.load (std::memory_order_acquire) - count + scopeSize) % scopeSize;

    for (int i = 0; i < count; ++i)
    {
        destination[i] = scope[(std::size_t) r].load (std::memory_order_relaxed);
        r = (r + 1) % scopeSize;
    }
}

void AudioEngine::previewFile (const juce::File& file)
{
    ++previewSerial; // (a preview still being prepared is dropped)
    previewTiming = {};
    if (preview == nullptr)
        return;
    auto sample = file.existsAsFile() ? samples.load (file.getFullPathName()) : nullptr;
    if (! previewInTime || sample == nullptr)
    {
        preview->playBrowser (std::move (sample)); // FL's preview: the file as it is, at once
        return;
    }

    // "Preview in time" (round 190aq, core/PreviewTempo.h): the file's tempo from its ACID beats or its name (cheap,
    // here), else from LYNX's tempo detection (on the worker); a file that can have none plays as it always did
    const auto path = file.getFullPathName();
    const auto name = file.getFileName().toStdString();
    const auto external = transport.externalBpm.load (std::memory_order_relaxed);
    const auto projectBpm = external > 0.0 ? external : transport.bpm.load (std::memory_order_relaxed);
    auto found = previewtempo::quick (*sample, name);
    auto known = found.bpm > 0.0;
    if (! known)
        if (const auto it = previewTempos.find (path); it != previewTempos.end())
        {
            found = it->second;
            known = true;
        }
    if (! known && ! previewtempo::worthDetecting (*sample))
    {
        preview->playBrowser (std::move (sample));
        return;
    }

    PreviewTiming timing;
    timing.path = path;
    timing.projectBpm = projectBpm;
    timing.fileBpm = found.bpm;
    timing.source = found.source;
    timing.ratio = previewtempo::stretchRatio (found.bpm, projectBpm);
    timing.inTime = found.bpm > 0.0;
    if (known)
    {
        if (found.bpm <= 0.0) // (analysed before: no tempo)
        {
            preview->playBrowser (std::move (sample));
            return;
        }
        if (timing.ratio == 1.0) // already at the project's tempo: only the start waits for the beat
        {
            startPreviewInTime (path, std::move (sample), timing);
            return;
        }
        for (const auto& s : previewStretches)
            if (s.path == path && s.ratio == timing.ratio && s.sample != nullptr)
            {
                startPreviewInTime (path, s.sample, timing);
                return;
            }
    }

    // the tempo detection and / or the stretch, off the message thread; what was playing stops now
    preview->playBrowser (nullptr);
    timing.pending = true;
    previewTiming = timing;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    previewJobs.post ([this, alive = previewAlive, serial = previewSerial, path, sample, timing, known, started]() mutable {
        if (! alive->load())
            return;
        auto found = previewtempo::Found { timing.fileBpm, timing.source };
        if (! known)
            found = previewtempo::detect (*sample);
        timing.fileBpm = found.bpm;
        timing.source = found.source;
        timing.inTime = found.bpm > 0.0;
        timing.ratio = previewtempo::stretchRatio (found.bpm, timing.projectBpm);
        std::shared_ptr<const SampleData> out = sample;
        if (timing.inTime && timing.ratio != 1.0)
            if (auto stretched = timeStretch (*sample, timing.ratio, StretchMethod::general); stretched != nullptr && stretched->frames() > 1)
                out = std::move (stretched);
        timing.prepareMs = juce::Time::getMillisecondCounterHiRes() - started;
        {
            const std::lock_guard<std::mutex> lock (previewResultLock);
            previewResults.push_back ({ serial, path, sample, out, timing, found, ! known });
        }
        juce::MessageManager::callAsync ([this, alive] {
            if (alive->load())
                takePreviewResults();
        });
    });
}

// The prepared previews, on the message thread: the tempo and the stretch are kept for the next click on the same
// file, and the newest one plays unless another file was clicked meanwhile.
void AudioEngine::takePreviewResults()
{
    std::vector<PreviewResult> results;
    {
        const std::lock_guard<std::mutex> lock (previewResultLock);
        results.swap (previewResults);
    }
    for (auto& r : results)
    {
        if (r.detected)
        {
            if (previewTempos.size() > 512)
                previewTempos.clear();
            previewTempos[r.path] = r.found;
        }
        if (r.out != r.sample)
        {
            if (previewStretches.size() >= 6)
                previewStretches.erase (previewStretches.begin());
            previewStretches.push_back ({ r.path, r.timing.ratio, r.out });
        }
        if (r.serial != previewSerial || preview == nullptr)
            continue; // another file was clicked meanwhile (or the preview was stopped)
        lastPreviewPrepareMs = r.timing.prepareMs;
        if (! r.timing.inTime)
        {
            previewTiming = {};
            preview->playBrowser (r.out); // no tempo found: as it is
            continue;
        }
        startPreviewInTime (r.path, r.out, r.timing);
    }
}

void AudioEngine::finishPreviewJobs()
{
    previewJobs.waitIdle();
    takePreviewResults();
}

void AudioEngine::startPreviewInTime (const juce::String& path, std::shared_ptr<const SampleData> sample, const PreviewTiming& timing)
{
    previewTiming = timing;
    previewTiming.path = path;
    previewTiming.pending = false;
    previewTiming.inTime = true;
    previewTiming.frames = sample != nullptr ? sample->frames() : 0;
    previewHandedAtMs = juce::Time::getMillisecondCounterHiRes();
    preview->setSyncLatency (getLatencySamples());
    preview->playBrowser (std::move (sample), 0.8f, true);
}

int AudioEngine::getPreviewBeatStarts() const noexcept { return preview != nullptr ? preview->beatStarts() : 0; }

double AudioEngine::getPreviewInTimeProgress() const noexcept
{
    if (previewTiming.pending)
        return 0.0;
    if (! previewTiming.inTime || preview == nullptr)
        return -1.0;
    const auto position = preview->getPlayPosition();
    if (position < 0.0) // (the audio thread takes the sample at its next block)
        return juce::Time::getMillisecondCounterHiRes() - previewHandedAtMs < 200.0 ? 0.0 : -1.0;
    return previewTiming.frames > 1 ? juce::jlimit (0.0, 1.0, position / (double) (previewTiming.frames - 1)) : 0.0;
}

void AudioEngine::setInputScale (int scale, int root) noexcept
{
    inputScaleRoot.store (root);
    inputScale.store (scale);
}

int AudioEngine::snapInputKey (int key) const noexcept
{
    const auto scale = inputScale.load();
    return scale > 0 ? notetools::snapToScale (key, inputScaleRoot.load(), scale) : key;
}

void AudioEngine::previewSample (std::shared_ptr<const SampleData> sample, bool loop, float gain, bool audioEditor)
{
    if (preview != nullptr)
        preview->play (std::move (sample), loop, gain, audioEditor);
}

void AudioEngine::setEditorVolume (double volume) noexcept
{
    editorVolume = controls::clampUtilityVolume (volume);
    if (preview != nullptr)
        preview->setEditorVolume (controls::previewGain (editorVolume));
}

void AudioEngine::setPreviewVolume (double volume) noexcept
{
    previewVolume = controls::clampUtilityVolume (volume);
    if (preview != nullptr)
        preview->setVolume (controls::previewGain (previewVolume));
}

double AudioEngine::getPreviewPosition() const noexcept
{
    return preview != nullptr ? preview->getPlayPosition() : -1.0;
}

void AudioEngine::markHeldKey (int midiChannel, int key, bool on) noexcept
{
    if (key < 0 || key > 127 || midiChannel < 0 || midiChannel > 15)
        return;
    if (on) // round 190ee: Edison's "Last hit" (the region menu's Trigger note)
        lastHitKey.store (key, std::memory_order_relaxed);
    auto& count = heldKeyCount[midiChannel][(std::size_t) key];
    auto before = count.load();
    std::uint8_t after = 0;
    do
        after = on ? (std::uint8_t) std::min (255, before + 1) : (std::uint8_t) (before > 0 ? before - 1 : 0);
    while (! count.compare_exchange_weak (before, after));
    const auto bit = std::uint64_t { 1 } << (key & 63);
    if (after > 0)
        heldKeyBits[midiChannel][key >> 6].fetch_or (bit);
    else
        heldKeyBits[midiChannel][key >> 6].fetch_and (~bit);
}

void AudioEngine::logPlayedNote (int channel, int key, float velocity, bool on, double tick, bool recorded)
{
    if (channel < 0)
        return;
    const auto ms = juce::Time::getMillisecondCounterHiRes();
    const juce::SpinLock::ScopedLockType lock (scoreLogLock);
    if (on)
        scoreLog.noteOn (ms, channel, key, velocity, tick, recorded);
    else
        scoreLog.noteOff (ms, channel, key);
    if (stepEditOn.load (std::memory_order_relaxed) && stepEditQueue.size() < 512)
        stepEditQueue.push_back ({ channel, key, velocity, on });
}

void AudioEngine::logPlayedNoteForTest (double ms, int channel, int key, float velocity, bool on, double tick)
{
    const juce::SpinLock::ScopedLockType lock (scoreLogLock);
    if (on)
        scoreLog.noteOn (ms, channel, key, velocity, tick, false);
    else
        scoreLog.noteOff (ms, channel, key);
}

void AudioEngine::setStepEdit (bool on) noexcept
{
    stepEditOn.store (on, std::memory_order_relaxed);
    const juce::SpinLock::ScopedLockType lock (scoreLogLock);
    stepEditQueue.clear();
}

std::vector<stepedit::Played> AudioEngine::takeStepEditNotes()
{
    std::vector<stepedit::Played> out;
    const juce::SpinLock::ScopedLockType lock (scoreLogLock);
    out.swap (stepEditQueue);
    return out;
}

std::vector<scorelog::Entry> AudioEngine::scoreLogSnapshot() const
{
    const juce::SpinLock::ScopedLockType lock (scoreLogLock);
    return scoreLog.snapshot();
}

bool AudioEngine::scoreLogEmpty() const
{
    const juce::SpinLock::ScopedLockType lock (scoreLogLock);
    return scoreLog.size() == 0;
}

void AudioEngine::clearScoreLog()
{
    const juce::SpinLock::ScopedLockType lock (scoreLogLock);
    scoreLog.clear();
}

NoteMask AudioEngine::soundingKeys (int channel) const noexcept
{
    return rackProcessor != nullptr ? rackProcessor->getRenderer().soundingKeys (channel) : NoteMask {};
}

std::vector<int> AudioEngine::heldInputKeys() const
{
    std::vector<int> keys;
    std::uint64_t all[2] {};
    for (const auto& channel : heldKeyBits)
        for (int word = 0; word < 2; ++word)
            all[word] |= channel[word].load();
    for (int word = 0; word < 2; ++word)
        for (int b = 0; b < 64; ++b)
            if ((all[word] >> b & 1u) != 0)
                keys.push_back (word * 64 + b);
    return keys;
}

std::array<std::uint64_t, 2> AudioEngine::heldInputKeyBits (int midiChannel) const noexcept
{
    if (midiChannel < 0 || midiChannel > 15)
        return {};
    return { heldKeyBits[midiChannel][0].load (std::memory_order_relaxed), heldKeyBits[midiChannel][1].load (std::memory_order_relaxed) };
}

RollingAudioLog& AudioEngine::getRollingLog() noexcept
{
    return meters.rollingLog;
}

void AudioEngine::sendLiveNote (int channel, int key, float velocity, bool on, bool record)
{
    if (record) // played notes (the PC keyboard) follow the scale snap; previews don't
    {
        markHeldKey (0, key, on); // (FL sends the typing keyboard as MIDI channel 1)
        // the note-off stops the key its note-on started, even if the snap changed in between
        const auto requested = key;
        if (on)
        {
            key = snapInputKey (key);
            playedKeys.on (channel, requested, key);
            startOnInput();
        }
        else
            key = playedKeys.off (channel, requested, snapInputKey (key));
    }

    NoteEvent e;
    e.type = on ? NoteEvent::Type::on : NoteEvent::Type::off;
    e.channel = channel;
    e.key = key;
    e.velocity = velocity;
    liveEvents.push (e);
    // (round 170d: recorded during the count-in too, at the audio clock's position)
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto taking = record && transport.recording.load() && (transport.playing.load() || isCountingIn());
    if (record)
        logPlayedNote (channel, key, velocity, on, transport.playing.load() ? recordTickFor (now) : -1.0, taking);

    if (taking)
        uiRecorded.push_back ({ recordTickFor (now), channel, key, velocity, on });
    if (record && slotTaking.load (std::memory_order_relaxed)) // a slot of the clip grid records (round 190bv)
        uiSlotRecorded.push_back ({ recordTickFor (now), channel, key, velocity, on });
}

void AudioEngine::panic()
{
    NoteEvent e;
    e.type = NoteEvent::Type::choke;
    e.channel = -1; // every channel
    liveEvents.push (e);
    for (auto& channel : heldKeyCount)
        for (auto& c : channel)
            c.store (0);
    for (auto& channel : heldKeyBits)
        for (auto& w : channel)
            w.store (0);
}

void AudioEngine::stopSound()
{
    panic();
    NoteEvent e;
    e.type = NoteEvent::Type::choke;
    e.channel = RackRenderer::stopSoundChannel; // and every effect / plugin reset
    liveEvents.push (e);
}

double AudioEngine::songPlayheadTicks() const noexcept
{
    const auto tick = juce::jmax (0.0, transport.positionTicks.load (std::memory_order_relaxed));
    if (song.loopPlays() || liveInput.linearSong.load (std::memory_order_relaxed))
        return tick;
    return std::fmod (tick, (double) juce::jmax<std::int64_t> (1, song.songLength()));
}

double AudioEngine::playheadAdvanceTicks() const noexcept
{
    if (! transport.playing.load (std::memory_order_relaxed) || ! clockRunning.load (std::memory_order_relaxed))
        return 0.0;
    const auto since = juce::Time::getMillisecondCounterHiRes() - blockTimeMs.load (std::memory_order_acquire);
    return juce::jlimit (0.0, juce::jmax (0.0, blockDurationMs.load (std::memory_order_relaxed)), since) * clockTicksPerMs.load (std::memory_order_relaxed);
}

LiveClock AudioEngine::liveClock() const noexcept
{
    LiveClock c;
    c.timeMs = blockTimeMs.load (std::memory_order_acquire);
    c.tick = clockTick.load (std::memory_order_relaxed);
    c.ticksPerMs = clockTicksPerMs.load (std::memory_order_relaxed);
    c.running = clockRunning.load (std::memory_order_relaxed);
    return c;
}

double AudioEngine::recordTickFor (double eventMs) const noexcept
{
    const auto clock = liveClock();
    // (stopped, or no callback since Play / the count-in started: the transport's own position, as before)
    if (! clock.running)
        return transport.positionTicks.load (std::memory_order_relaxed)
               - (transport.playing.load (std::memory_order_relaxed) ? 0.0 : transport.countIn.load (std::memory_order_relaxed));
    return liveTickAt (clock, eventMs, compensatePlayed.load (std::memory_order_relaxed) ? outputCompensationMs.load (std::memory_order_relaxed) : 0.0);
}

CaptureResult AudioEngine::captureMidi()
{
    CaptureOptions o;
    o.afterMs = lastCaptureMs;
    o.nowMs = juce::Time::getMillisecondCounterHiRes();
    o.gapMs = 2.0 * juce::jmax (1, song.beatsPerBar) * 60000.0 / juce::jmax (1.0, song.bpm); // two bars of silence end a phrase
    o.playhead = (std::int64_t) std::llround (songPlayheadTicks());
    o.mode = recordOptions.mode;
    o.quantize.grid = recordOptions.quantise;
    o.quantize.starts = recordOptions.quantiseStarts;
    o.quantize.end = recordOptions.noteEnd;
    o.quantize.stepSequencer = recordOptions.toStepSequencer;
    o.placeClip = recordOptions.placeSongClip;
    const auto log = scoreLogSnapshot();
    if (capturePhrase (song, log, o).empty()) // (nothing to capture: no undo step)
        return {};
    CaptureResult result;
    editSong ([&] (Song& s) { result = lynx::captureMidi (s, log, o); });
    if (result.newestMs > lastCaptureMs)
        lastCaptureMs = result.newestMs;
    return result;
}

CaptureResult AudioEngine::captureMidiToSlot (int track, int scene, int& placed, bool& into)
{
    placed = -1;
    into = false;
    CaptureOptions o;
    o.afterMs = lastCaptureMs;
    o.nowMs = juce::Time::getMillisecondCounterHiRes();
    o.gapMs = 2.0 * juce::jmax (1, song.beatsPerBar) * 60000.0 / juce::jmax (1.0, song.bpm);
    const auto log = scoreLogSnapshot();
    const auto phrase = capturePhrase (song, log, o);
    if (phrase.empty() || track < 0 || track >= launcher::maxTracks)
        return {};
    CaptureResult result;
    result.played = phrase.front().startTick >= 0.0;
    for (const auto& e : phrase)
        result.newestMs = juce::jmax (result.newestMs, e.startMs);
    const auto channel = detailchain::forTrack (song, track).channel;
    const auto running = transport.playing.load();

    // round 190em: into the clip the track plays (Live writes there, no new clip)
    const auto playingRow = clipLauncher != nullptr ? clipLauncher->tracks[(std::size_t) track].playing.load (std::memory_order_relaxed) : -1;
    if (running && result.played && playingRow >= 0 && song.clipGrid)
        if (const auto* clip = launcher::clipAt (*song.clipGrid, track, playingRow); clip != nullptr && clip->kind == ClipKind::pattern)
        {
            const auto shift = clipLauncher->clockMinusSong.load (std::memory_order_relaxed)
                               - clipLauncher->tracks[(std::size_t) track].anchor.load (std::memory_order_relaxed);
            auto notes = session::clipTimeNotes (phrase, o.nowMs, song.bpm, shift);
            session::setNotesChannel (notes, channel);
            result.notes = (int) notes.size();
            auto ok = false;
            editSong ([&] (Song& s) { ok = session::captureIntoClip (s, track, playingRow, notes); });
            if (ok)
            {
                placed = playingRow;
                into = true;
                if (result.newestMs > lastCaptureMs)
                    lastCaptureMs = result.newestMs;
                return result;
            }
        }

    // a new clip; played while stopped, the tempo comes from the phrase
    const auto bpm = running ? song.bpm : session::phraseTempo (phrase, song.bpm);
    std::int64_t length = 0;
    auto notes = session::phraseNotes (phrase, o.nowMs, bpm, song.beatsPerBar, length);
    session::setNotesChannel (notes, channel);
    result.notes = (int) notes.size();
    const auto row = session::captureSlot (song, track, scene);
    editSong ([&] (Song& s) {
        if (! running)
            s.bpm = bpm;
        if (session::placeRecordedPattern (s, track, row, std::move (notes), length, {}) >= 0)
        {
            placed = row;
            if (auto* slot = launcher::settingsAt (*s.clipGrid, track, row))
                slot->blankName = true; // (Live's captured clip has no name; the pattern keeps LYNX's default one)
        }
    });
    if (placed < 0)
        return {};
    launcherCommand ({ launcher::Command::Type::launch, track, row }); // (launched at once; a stopped transport starts)
    if (result.newestMs > lastCaptureMs)
        lastCaptureMs = result.newestMs;
    return result;
}

double AudioEngine::estimatedPositionNow() const noexcept
{
    // the position only moves block by block; interpolate by the time since the last block
    auto tick = transport.positionTicks.load (std::memory_order_relaxed);

    if (transport.playing.load (std::memory_order_relaxed))
    {
        const auto elapsedMs = juce::jlimit (0.0, 100.0, juce::Time::getMillisecondCounterHiRes() - blockTimeMs.load());
        tick += elapsedMs * 0.001 * transport.sampleRate.load() * transport.ticksPerSample();
    }

    return tick;
}

AudioEngine::LiveRecording AudioEngine::liveRecording() const
{
    LiveRecording live;
    if (! isLiveRecording())
        return live;
    live.punch = activePunch;

    // the note take
    if (recorder.isActive() && song.currentPattern >= 0 && song.currentPattern < (int) song.patterns.size())
    {
        const auto now = transport.playing.load() ? estimatedPositionNow() : lastPlayingTick;
        auto preview = recorder.preview (song, now);
        live.notes = true;
        live.pattern = song.currentPattern;
        live.held = std::move (preview.notes);
        if (song.mode == PlayMode::song && preview.origin >= 0)
        {
            // the take's clip (placed with its first finished note) or, before it, where it will be placed
            const Clip* clip = nullptr;
            for (const auto& c : song.playlist)
                if (c.kind == ClipKind::pattern && c.pattern == song.currentPattern && c.start - c.offset == preview.origin
                    && (clip == nullptr || c.end() > clip->end()))
                    clip = &c;
            live.track = clip != nullptr ? clip->track : patternClipTrack (song, song.barAt (preview.origin).tick);
            live.from = clip != nullptr ? clip->start : preview.origin;
            live.origin = preview.origin;
            auto to = preview.reach;
            for (const auto& n : live.held)
                to = std::max (to, preview.origin + n.start + n.length);
            if (! preview.fromClip && noteTakeLinear)
                to = std::max (to, (std::int64_t) now); // (a linear take: the region follows the playhead)
            live.to = std::max (live.from, to);
        }
    }

    // the audio takes
    const auto startTick = takeStartTick.load();
    const auto frames = takeFrames.load();
    if (! openTakes.empty() && startTick >= 0.0 && frames > 0)
    {
        const auto wraps = takeWrapCount.load();
        const auto lastWrap = song.mode == PlayMode::song && song.loopPlays() && wraps > 0 ? (std::int64_t) takeWraps[(std::size_t) wraps - 1].load() : -1;
        const auto span = liverec::audioSpan (startTick, frames, takeTicksPerSample, takeLatency, lastWrap, song.loopStart);
        if (liveTakeTracks.size() != openTakes.size())
        {
            // the tracks takes::placeTakeClips will pick (each file's insert, one track each)
            liveTakeTracks.clear();
            const auto from = (std::int64_t) span.start, to = from + std::max<std::int64_t> (1, (std::int64_t) span.length);
            for (const auto& take : openTakes)
            {
                auto track = takes::takeTrack (song, take->track > 0 ? take->track : -1, from, to);
                for (const auto used : liveTakeTracks)
                    if (used >= track)
                        track = used + 1;
                liveTakeTracks.push_back (track);
            }
        }
        if (span.valid())
            for (std::size_t k = 0; k < openTakes.size(); ++k)
            {
                LiveRecording::Audio a;
                a.track = liveTakeTracks[k];
                a.span = span;
                a.peaks = openTakes[k]->peaks;
                a.name = openTakes[k]->file.getFileNameWithoutExtension();
                live.audio.push_back (std::move (a));
            }
    }
    return live;
}

void AudioEngine::updateAudioRecording()
{
    // (round 190bv: a slot of the clip grid recording audio runs the armed inserts' takes too)
    const auto wantTake = audioArmed && recordOptions.recordAudio && (transport.recording.load() || slotAudioWanted);
    const auto open = takeCount > 0;

    // the files are ready before Play: the very first block gets recorded
    if (wantTake && ! open && recordingError.isEmpty())
        startAudioTake();

    // post-roll over: playback stops (the take is closed below on the next tick)
    if (open && punchStopAt >= 0 && transport.playing.load() && transport.positionTicks.load() >= (double) punchStopAt)
    {
        punchStopAt = -1;
        stop();
    }

    // the first samples arrived: the journals that let a crash be recovered (round 163)
    if (open && ! journalsWritten && takeStartTick.load() >= 0.0)
        writeJournals();

    // a file that could not be written (disk full, drive gone): say it once, the take stops there
    for (auto& take : openTakes)
        if (take->failed.load() && recordingError.isEmpty())
        {
            recordingError = take->writer.error().empty() ? juce::String ("could not write ") + take->file.getFullPathName()
                                                          : juce::String::fromUTF8 (take->writer.error().c_str()) + ": " + take->file.getFullPathName();
            notify (InputIssue::failed, take->track, recordingError);
        }

    // playback stopped (or recording turned off) with something recorded: close the takes
    if (open && (! wantTake || (! transport.playing.load() && takeFrames.load() > 0)))
        finishAudioTake();
}

bool AudioEngine::Take::drain()
{
    if (stream == nullptr || failed.load())
        return false;
    const auto n = 8192;
    if (scratch.size() < (std::size_t) (2 * n))
        scratch.resize ((std::size_t) (2 * n));
    auto any = false;
    for (;;)
    {
        float* out[] = { scratch.data(), scratch.data() + n };
        const auto got = stream->pull (out, n);
        if (got <= 0)
            break;
        any = true;
        const float* in[] = { out[0], out[1] };
        if (! writer.write (in, got))
        {
            failed.store (true);
            break;
        }
        peaks->append (in, stream->channels(), got);
    }
    return any;
}

namespace
{
juce::File recordingSettingsFile() { return PluginHost::settingsFolder().getChildFile ("recording.txt"); }
juce::File inProgressFile() { return PluginHost::settingsFolder().getChildFile ("recordings-in-progress.txt"); }

AudioEngine::RecordingSettings loadRecordingSettings()
{
    AudioEngine::RecordingSettings s;
    for (const auto& raw : juce::StringArray::fromLines (recordingSettingsFile().loadFileAsString()))
    {
        const auto key = raw.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = raw.fromFirstOccurrenceOf ("=", false, false);
        if (key == "format")
            s.format = value.trim() == "float32" ? takes::SampleFormat::float32 : value.trim() == "pcm16" ? takes::SampleFormat::pcm16 : takes::SampleFormat::pcm24;
        else if (key == "trackTakeNames")
            s.trackTakeNames = value.getIntValue() != 0;
        else if (key == "manualOffset")
            s.manualOffset = juce::jlimit (-48000, 48000, value.getIntValue());
        else if (key == "useMeasured")
            s.useMeasured = value.getIntValue() != 0;
        else if (key == "pingOutput")
            s.pingOutput = juce::jlimit (0, 63, value.getIntValue());
        else if (key == "pingInput")
            s.pingInput = juce::jlimit (0, 63, value.getIntValue());
        else if (key == "measured") // measured=<samples>\t<setup key>
            s.measured[value.fromFirstOccurrenceOf ("\t", false, false).toStdString()] = value.upToFirstOccurrenceOf ("\t", false, false).getIntValue();
    }
    return s;
}
} // namespace

void AudioEngine::setRecordingSettings (const RecordingSettings& settings)
{
    // round 190bj (issue B3): takes whose files are ready but empty (made when the previous take ended, the track still
    // armed) take the new sample format too; one that has samples keeps its own
    if (settings.format != recordingSettings.format)
        for (auto& take : openTakes)
        {
            const juce::ScopedLock lock (take->drainMutex);
            take->writer.changeFormat (settings.format);
        }
    recordingSettings = settings;
    juce::String text;
    text << "format=" << (settings.format == takes::SampleFormat::float32 ? "float32" : settings.format == takes::SampleFormat::pcm16 ? "pcm16" : "pcm24") << "\n"
         << "trackTakeNames=" << (settings.trackTakeNames ? 1 : 0) << "\n"
         << "manualOffset=" << settings.manualOffset << "\n"
         << "useMeasured=" << (settings.useMeasured ? 1 : 0) << "\n"
         << "pingOutput=" << settings.pingOutput << "\n"
         << "pingInput=" << settings.pingInput << "\n";
    for (const auto& [key, samples] : settings.measured)
        text << "measured=" << samples << "\t" << juce::String::fromUTF8 (key.c_str()) << "\n";
    if (deviceStateSaver != nullptr) // (a headless engine — tests, export — keeps the user's file as it is)
        recordingSettingsFile().replaceWithText (text);
    sendChangeMessage();
}

std::string AudioEngine::currentSetupKey() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return {};
    const auto setup = deviceManager.getAudioDeviceSetup();
    return takes::setupKey (device->getTypeName().toStdString(), setup.outputDeviceName.toStdString(), setup.inputDeviceName.toStdString(),
                            device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

takes::Latency AudioEngine::currentLatency() const
{
    takes::Latency l;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        l.driverInput = device->getInputLatencyInSamples();
        l.driverOutput = device->getOutputLatencyInSamples();
    }
    if (recordingSettings.useMeasured)
        if (const auto it = recordingSettings.measured.find (currentSetupKey()); it != recordingSettings.measured.end())
            l.measured = it->second;
    l.manual = recordingSettings.manualOffset;
    l.pdc = getLatencySamples();
    return l;
}

juce::String AudioEngine::startLatencyPing()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return TR_NOOP ("no audio device is open");
    if (transport.playing.load() || isCountingIn())
        return TR_NOOP ("stop playback first");
    if (! hasAudioInput())
        if (const auto r = openAudioInput(); r.issue != InputIssue::none)
            return TR_NOOP ("no audio input is open");
    device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return TR_NOOP ("no audio device is open");
    const auto in = recordingSettings.pingInput, out = recordingSettings.pingOutput;
    if (! device->getActiveInputChannels()[in])
        return TR_NOOP ("the input channel is not open");
    if (! device->getActiveOutputChannels()[out])
        return TR_NOOP ("the output channel is not open");
    // device channel -> the callback's index (only open channels are passed)
    const auto slotOf = [] (const juce::BigInteger& active, int c) {
        if (! active[c])
            return -1;
        int slot = 0;
        for (int k = 0; k < c; ++k)
            slot += active[k] ? 1 : 0;
        return slot;
    };
    pingInput = slotOf (device->getActiveInputChannels(), in);
    pingOutputs = { slotOf (device->getActiveOutputChannels(), out), slotOf (device->getActiveOutputChannels(), out + 1) };
    const auto rate = device->getCurrentSampleRate() > 0.0 ? device->getCurrentSampleRate() : 48000.0;
    ping.start (8, juce::jmax (4096, (int) (rate / 4.0)), 0.5f);
    return {};
}

takes::LatencyPing::Result AudioEngine::finishLatencyPing()
{
    auto r = ping.result();
    if (r.ok)
    {
        auto s = recordingSettings;
        s.measured[currentSetupKey()] = r.samples;
        setRecordingSettings (s);
    }
    return r;
}

void AudioEngine::setInsertRecordSafe (int track, bool safe)
{
    if (track <= 0 || track >= (int) song.mixer.size() || song.mixer[(std::size_t) track].recordSafe == safe)
        return;
    if (safe && isInsertArmed (track))
        setInsertArmed (track, false);
    editSong ([track, safe] (Song& s) { s.mixer[(std::size_t) track].recordSafe = safe; });
}

void AudioEngine::startAudioTake()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    const auto names = getInputChannelNames();
    const auto deviceChannels = (int) names.size();
    const auto active = device->getActiveInputChannels();

    if (deviceChannels == 0 || active.isZero())
    {
        recordingError = "no audio input is open";
        notify (InputIssue::noInputOpen);
        return;
    }

    // one take per armed insert that has an input source; without armed inserts (Settings' "Record input audio"),
    // the first two input channels into a new insert
    // round 168x (issue #41): FL's "Record" position of each insert. "External input only" records the device input;
    // the other positions tap the insert (stereo, after the render) and need no input of their own; LYNX's "also the
    // dry input" adds the device input as a second take
    struct Wanted
    {
        int track, first, channels;
        RecordSource point = RecordSource::externalInput;
        bool dry = false; // the second, dry take of "also record the dry input (LYNX)"
    };
    std::vector<Wanted> wanted;
    std::vector<int> withoutInput;

    for (const auto t : armedInserts)
    {
        if (t <= 0 || t >= (int) song.mixer.size() || (int) wanted.size() >= maxTakes)
            continue;
        const auto& mt = song.mixer[(std::size_t) t];
        const auto [first, count] = inputChannelsOf (mt, deviceChannels);
        if (mt.recordSource != RecordSource::externalInput)
        {
            wanted.push_back ({ t, 0, 2, mt.recordSource, false });
            if (mt.recordDryToo && count > 0 && (int) wanted.size() < maxTakes && (int) wanted.size() < LiveInput::maxTaps)
                wanted.push_back ({ t, first, count, RecordSource::externalInput, true });
            continue;
        }
        if (count == 0)
            withoutInput.push_back (t);
        else
            wanted.push_back ({ t, first, count });
    }

    if (armedInserts.empty())
        wanted.push_back ({ 0, 0, juce::jmin (2, deviceChannels) });

    if (wanted.empty())
    {
        recordingError = "the armed inserts have no audio input";
        notify (InputIssue::armedWithoutInput, withoutInput.empty() ? 0 : withoutInput.front());
        return;
    }

    if (! withoutInput.empty())
        notify (InputIssue::insertWithoutInput, withoutInput.front());

    auto folder = recordingFolder;
    if (! folder.createDirectory()) // e.g. a read-only project folder: the global recordings folder
        folder = defaultRecordingFolder(), folder.createDirectory();
    takeSampleRate = device->getCurrentSampleRate();
    takeLatency = takes::compensation (currentLatency(), recordOptions.latencyCompensation);
    takeTicksPerSample = transport.ticksPerSample();
    const auto format = recordingSettings.format;

    // room on the drive (round 163): under 30 s refuse, under 10 minutes warn
    {
        int channels = 0;
        for (const auto& w : wanted)
            channels += w.channels;
        const auto minutes = takes::minutesLeft (folder.getBytesFreeOnVolume(), takeSampleRate, channels, format);
        if (minutes < 0.5)
        {
            recordingError = "the recordings drive is full";
            notify (InputIssue::lowDisk, 0, juce::String (minutes, 1));
            return;
        }
        if (minutes < 10.0)
            notify (InputIssue::lowDisk, 0, juce::String (minutes, 1));
    }

    const auto now = juce::Time::getCurrentTime();
    std::vector<std::shared_ptr<Take>> made;

    int tapsUsed = 0;
    for (const auto& w : wanted)
    {
        juce::String insert;
        if (w.track > 0)
            insert = song.mixer[(std::size_t) w.track].name.empty() ? "Insert " + juce::String (w.track)
                                                                    : juce::String::fromUTF8 (song.mixer[(std::size_t) w.track].name.c_str());
        if (w.dry)
            insert += " DI"; // the dry take of "also record the dry input (LYNX)"
        juce::File file;
        auto recordTo = w.track > 0 ? juce::String::fromUTF8 (song.mixer[(std::size_t) w.track].recordTo.c_str()) : juce::String();
        if (w.dry && recordTo.isNotEmpty())
            recordTo = juce::File (recordTo).getSiblingFile (juce::File (recordTo).getFileNameWithoutExtension() + " DI.wav").getFullPathName();
        if (recordTo.isNotEmpty() && juce::File::isAbsolutePath (recordTo))
        {
            // FL's "Record <insert> to" file (round 167a): that file; LYNX never overwrites an earlier take, the next
            // ones get "_2", "_3"... (its own choice)
            const auto target = juce::File (recordTo).withFileExtension (".wav");
            target.getParentDirectory().createDirectory();
            const auto unique = safety::uniqueName (target.getFileNameWithoutExtension().toStdString(), [&target, &made] (const std::string& n) {
                const auto f = target.getSiblingFile (juce::String::fromUTF8 (n.c_str()) + ".wav");
                return f.exists() || std::any_of (made.begin(), made.end(), [&f] (const std::shared_ptr<Take>& t) { return t->file == f; });
            });
            file = target.getSiblingFile (juce::String::fromUTF8 (unique.c_str()) + ".wav");
        }
        else if (recordingSettings.trackTakeNames)
        {
            // "<Insert>_Take<N>.wav", N raised until the name is free (round 163)
            for (int n = 1; n < 100000; ++n)
            {
                file = folder.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (takes::takeName (insert.toStdString(), n).c_str())) + ".wav");
                if (! file.exists() && std::none_of (made.begin(), made.end(), [&file] (const std::shared_ptr<Take>& t) { return t->file == file; }))
                    break;
            }
        }
        else
        {
            // FL's name (REA round 134): "<project>_<yyyy-mm-dd hh-mm-ss>_<insert name>", "_2", "_3"... when taken
            const auto name = juce::File::createLegalFileName (juce::String::fromUTF8 (
                safety::recordingName (recordingStem.toStdString(), now.getYear(), now.getMonth() + 1, now.getDayOfMonth(), now.getHours(),
                                       now.getMinutes(), now.getSeconds(), insert.toStdString()).c_str()));
            const auto unique = safety::uniqueName (name.toStdString(), [&folder] (const std::string& n) {
                return folder.getChildFile (juce::String::fromUTF8 (n.c_str()) + ".wav").exists();
            });
            file = folder.getChildFile (juce::String::fromUTF8 (unique.c_str()) + ".wav");
        }

        auto take = std::make_shared<Take>();
        take->track = w.track;
        take->first = w.first;
        take->channels = w.channels;
        take->file = file;
        if (w.point != RecordSource::externalInput && tapsUsed < LiveInput::maxTaps)
        {
            take->tap = tapsUsed++;
            // the effects' latency up to the tap: the slots' (the planned output latency minus the input alignment and
            // the manual track latency, which delays nothing inside the insert)
            if (w.point >= RecordSource::postEffects)
                take->skip = insertEffectsLatency (getPdcPlan(), song, w.track, takeSampleRate);
            auto& slot = liveInput.taps[(std::size_t) take->tap];
            slot.point.store ((int) w.point);
            slot.track.store (w.track);
        }
        // FL (round 190ev, REA_rodada190ev §2): "External input only" plays back through the recording insert, as does any
        // take of an insert a Playlist audio track is linked to; a take that holds the insert's processing (Post EQ, the
        // default) goes where the insert goes (Master, usually). It used to play through a new free insert.
        take->playThrough = takes::playbackInsert (song, w.track, w.point);
        // ~22 s at 48 kHz of room between the audio thread and the disk
        take->stream = std::make_unique<takes::TakeStream> (w.channels, 1 << 20);
        if (! take->writer.open (file.getFullPathName().toStdString(), takeSampleRate, w.channels, format))
        {
            recordingError = utf8 ("could not create ") + file.getFullPathName();
            continue;
        }
        made.push_back (std::move (take));
    }

    if (recordingError.isNotEmpty())
        notify (InputIssue::failed, 0, recordingError);

    takeStartTick.store (-1.0);
    takeFrames.store (0);
    takeTicks.store (0.0);
    takeWrapCount.store (0);
    lostFrames = 0;
    journalsWritten = false;
    openTakes = made;
    {
        const juce::ScopedLock lock (drainLock);
        draining = made;
    }
    if (drainer == nullptr)
        drainer = std::make_unique<Drainer> (*this);
    if (! recordThread.isThreadRunning())
        recordThread.startThread (juce::Thread::Priority::high);
    recordThread.addTimeSliceClient (drainer.get());

    liveInput.tapCount.store (tapsUsed);
    const juce::SpinLock::ScopedLockType lock (takeLock);
    takeCount = 0;
    for (auto& take : made)
        takes[(std::size_t) takeCount++] = take.get();
    updateLinearSong();
}

void AudioEngine::writeJournals()
{
    journalsWritten = true;
    juce::StringArray list (juce::StringArray::fromLines (inProgressFile().loadFileAsString()));
    list.removeEmptyStrings();
    int k = 0;
    for (const auto& take : openTakes)
    {
        takes::Journal j;
        j.wav = take->file.getFullPathName().toStdString();
        j.firstTick = takeStartTick.load();
        j.ticksPerSample = takeTicksPerSample;
        j.sampleRate = takeSampleRate;
        j.latency = takeLatency;
        j.insert = take->playThrough;
        j.playlistTrack = -1;
        j.project = recordingStem.toStdString();
        const auto journal = journalFor (take->file);
        journal.replaceWithText (juce::String::fromUTF8 (takes::journalText (j).c_str()));
        list.addIfNotAlreadyThere (journal.getFullPathName());
        ++k;
    }
    if (deviceStateSaver != nullptr)
        inProgressFile().replaceWithText (list.joinIntoString ("\n"));
}

void AudioEngine::finishAudioTake()
{
    liveTakeTracks.clear();
    {
        const juce::SpinLock::ScopedLockType lock (takeLock);
        takes.fill (nullptr);
        takeCount = 0;
    }
    liveInput.linearSong.store (false);
    liveInput.noLoopRegion.store (false);
    std::vector<std::shared_ptr<Take>> done;
    done.swap (openTakes);
    {
        const juce::ScopedLock lock (drainLock);
        draining.clear();
    }

    // the recording taps are free again (the audio thread let go of the takes under takeLock above)
    liveInput.tapCount.store (0);
    for (auto& tap : liveInput.taps)
        tap.track.store (-1);

    std::vector<std::shared_ptr<Take>> kept;
    for (auto& take : done)
    {
        if (take->tap >= 0 && take->skipped > 0) // a tapped take: the frames it dropped at its start close it
            take->stream->pushSilence (take->skipped);
        take->stream->finish(); // the audio thread let go of it (takeLock above)
        const juce::ScopedLock lock (take->drainMutex);
        take->drain();
        lostFrames += take->stream->lostFrames();
        take->writer.close();
        if (take->file.existsAsFile())
        {
            sessionRecordings.push_back (take->file);
            kept.push_back (take);
        }
    }

    if (recordOptions.disarmOnStop) // LYNX's "Disarm the inserts when recording stops" (not FL's "Disarm on stop")
        for (const auto t : std::vector<int> (armedInserts))
            setInsertArmed (t, false);

    // the journals go once the takes are on the Playlist (or thrown away)
    const auto dropJournals = [this, &done] {
        juce::StringArray list (juce::StringArray::fromLines (inProgressFile().loadFileAsString()));
        for (const auto& take : done)
        {
            journalFor (take->file).deleteFile();
            list.removeString (journalFor (take->file).getFullPathName());
        }
        list.removeEmptyStrings();
        if (deviceStateSaver != nullptr)
        {
            if (list.isEmpty())
                inProgressFile().deleteFile();
            else
                inProgressFile().replaceWithText (list.joinIntoString ("\n"));
        }
    };

    const auto frames = takeFrames.exchange (0);
    const auto startTick = takeStartTick.exchange (-1.0);
    const auto ticks = takeTicks.exchange (0.0);
    const auto keepPunch = activePunch;
    activePunch.reset();
    punchStopAt = -1;
    std::vector<double> wraps;
    for (int k = 0, count = takeWrapCount.exchange (0); k < count; ++k)
        wraps.push_back ((double) takeWraps[(std::size_t) k].load() * takeTicksPerSample);

    // (round 190bv) takes made for slots of the clip grid go into those slots, not onto the Playlist
    if (std::any_of (slotTakes.begin(), slotTakes.end(), [] (const SlotTake& t) { return t.audio; }) && frames > 0 && startTick >= 0.0
        && ! discardAudioTakes)
    {
        std::vector<std::pair<juce::File, int>> files;
        for (const auto& take : kept)
            files.push_back ({ take->file, take->track });
        const auto used = placeSlotAudio (files, takeTicksPerSample, takeLatency);
        if (! used)
            for (auto& take : done)
            {
                take->file.deleteFile();
                forgetRecordings ({ take->file });
            }
        dropJournals();
        return;
    }

    if (frames <= 0 || startTick < 0.0 || discardAudioTakes) // (Cancel recording drops the takes, round 187a)
    {
        for (auto& take : done)
        {
            take->file.deleteFile();
            forgetRecordings ({ take->file });
        }
        dropJournals();
        return;
    }

    if (lostFrames > 0)
        notify (InputIssue::framesLost, 0, juce::String (lostFrames));

    // what came into the interface corresponds to what was heard one latency earlier; the clip lands on that
    // song position to the sample (round 163: sub-tick rest in the source offset)
    const auto placement = takes::place (startTick, ticks, takeTicksPerSample, takeLatency);
    std::vector<takes::Part> parts;
    if (keepPunch.has_value()) // pre-roll / punch in-out: only the range (the rest stays in the file)
        parts = takes::punch (placement, { *keepPunch });
    else if (song.mode == PlayMode::song && song.loopPlays())
        // one part per pass of the loop; FL keeps a pass of any length (REA round 177c: a 1-tick pass got its own track),
        // round 163's option drops one a 1/16 note long or less
        parts = takes::loopPasses (placement, song.loopStart, song.loopEnd, recordOptions.lastLoopPassPlays ? ppq / 4 : 0, wraps);
    else if (recordOptions.recordingMarkers && song.mode == PlayMode::song)
        parts = takes::punch (placement, punchRanges (song.markers())); // FL's "Enable recording markers"
    else
        parts = takes::punch (placement, {});

    if (parts.empty()) // the whole take was outside the ranges
    {
        for (auto& take : done)
        {
            take->file.deleteFile();
            forgetRecordings ({ take->file });
        }
        dropJournals();
        return;
    }

    std::vector<takes::TakeFile> made;
    for (const auto& take : kept)
        made.push_back ({ take->file.getFullPathName().toStdString(), take->file.getFileNameWithoutExtension().toStdString(), take->playThrough,
                          take->track > 0 ? take->track : -1 });

    // FL: Loop record over a loop region, without a punch
    const auto loopRecorded = recordOptions.loopRecord && ! keepPunch.has_value() && song.mode == PlayMode::song && song.loopPlays();
    const takes::TakeOptions takeOptions { loopRecorded, recordOptions.mode == RecordMode::overdub, recordOptions.lastLoopPassPlays };
    editSong ([made, parts, takeOptions] (Song& s) { takes::placeTakeClips (s, made, parts, takeOptions); });
    dropJournals();
}

std::vector<takes::Journal> AudioEngine::recoverInterruptedTakes (const std::vector<juce::File>& folders)
{
    // the journals listed as in progress, and any journal left in the given folders
    juce::StringArray journals (juce::StringArray::fromLines (inProgressFile().loadFileAsString()));
    auto where = folders;
    where.push_back (defaultRecordingFolder());
    for (const auto& folder : where)
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.lynxrec"))
            journals.addIfNotAlreadyThere (f.getFullPathName());
    journals.removeEmptyStrings();

    std::vector<takes::Journal> out;
    for (const auto& path : journals)
    {
        const juce::File journal (path);
        takes::Journal j;
        if (! journal.existsAsFile() || ! takes::parseJournal (journal.loadFileAsString().toStdString(), j))
            continue;
        // (a take being recorded by this very session is not "interrupted")
        if (std::any_of (openTakes.begin(), openTakes.end(), [&journal, this] (const std::shared_ptr<Take>& t) { return journalFor (t->file) == journal; }))
            continue;
        std::int64_t frames = 0;
        if (! takes::repairWav (j.wav, &frames) || frames <= 0)
            continue;
        out.push_back (j);
    }
    return out;
}

int AudioEngine::placeRecoveredTakes (const std::vector<takes::Journal>& recovered)
{
    struct One
    {
        takes::TakeFile file;
        std::vector<takes::Part> parts;
    };
    std::vector<One> placed;
    for (const auto& j : recovered)
    {
        std::int64_t frames = 0;
        if (! takes::repairWav (j.wav, &frames) || frames <= 0)
            continue;
        const auto placement = takes::place (j.firstTick, (double) frames * j.ticksPerSample, j.ticksPerSample, j.latency);
        if (! placement.valid())
            continue;
        const juce::File wav (juce::String::fromUTF8 (j.wav.c_str()));
        placed.push_back ({ { j.wav, wav.getFileNameWithoutExtension().toStdString(), j.insert }, takes::punch (placement, {}) });
    }
    if (! placed.empty())
        editSong ([placed] (Song& s) {
            for (const auto& p : placed)
                takes::placeTakeClips (s, { p.file }, p.parts);
        });
    forgetInterruptedTakes (recovered);
    return (int) placed.size();
}

void AudioEngine::forgetInterruptedTakes (const std::vector<takes::Journal>& recovered)
{
    juce::StringArray list (juce::StringArray::fromLines (inProgressFile().loadFileAsString()));
    for (const auto& j : recovered)
    {
        const auto journal = journalFor (juce::File (juce::String::fromUTF8 (j.wav.c_str())));
        journal.deleteFile();
        list.removeString (journal.getFullPathName());
    }
    list.removeEmptyStrings();
    if (deviceStateSaver != nullptr)
    {
        if (list.isEmpty())
            inProgressFile().deleteFile();
        else
            inProgressFile().replaceWithText (list.joinIntoString ("\n"));
    }
}

juce::File AudioEngine::defaultRecordingFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Recordings");
}

void AudioEngine::setRecordingTarget (const juce::File& folder, const juce::String& projectStem)
{
    recordingFolder = folder == juce::File() ? defaultRecordingFolder() : folder;
    recordingStem = projectStem;
}

void AudioEngine::forgetRecordings (const std::vector<juce::File>& files)
{
    sessionRecordings.erase (std::remove_if (sessionRecordings.begin(), sessionRecordings.end(),
                                             [&files] (const juce::File& f) { return std::find (files.begin(), files.end(), f) != files.end(); }),
                             sessionRecordings.end());
}

std::vector<std::string> AudioEngine::getInputChannelNames() const
{
    std::vector<std::string> names;

    if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr && ! device->getActiveInputChannels().isZero())
        for (const auto& n : device->getInputChannelNames())
            if ((int) names.size() < LiveInput::maxChannels)
                names.push_back (n.toStdString());

    return names;
}

bool AudioEngine::hasAudioInput() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr && ! device->getActiveInputChannels().isZero();
}

void AudioEngine::notify (InputIssue issue, int track, const juce::String& detail)
{
    recordingNotice = { issue, track, detail };
    sendChangeMessage();
}

AudioEngine::RecordingNotice AudioEngine::openAudioInput (const juce::String& deviceName)
{
    auto* type = deviceManager.getCurrentDeviceTypeObject();

    if (type == nullptr || deviceManager.getCurrentAudioDevice() == nullptr)
        return { InputIssue::noDevice, 0, {} };

    auto setup = deviceManager.getAudioDeviceSetup();
    int count = 0;

    if (type->hasSeparateInputsAndOutputs())
    {
        // Windows Audio / DirectSound: the input is a device of its own
        const auto inputs = type->getDeviceNames (true);
        auto name = deviceName.isNotEmpty() ? deviceName : setup.inputDeviceName;
        if (name.isEmpty())
            name = releasedInputDevice; // the one releaseAudioInputIfUnused closed

        if (! inputs.contains (name))
        {
            const auto index = type->getDefaultDeviceIndex (true);
            name = juce::isPositiveAndBelow (index, inputs.size()) ? inputs[index] : juce::String();
        }

        if (name.isEmpty())
            return { InputIssue::noInputDevice, 0, {} };

        auto* current = deviceManager.getCurrentAudioDevice();
        if (name == setup.inputDeviceName && ! current->getActiveInputChannels().isZero())
            count = current->getInputChannelNames().size();
        else if (std::unique_ptr<juce::AudioIODevice> probe (type->createDevice ({}, name)); probe != nullptr)
            count = probe->getInputChannelNames().size();

        setup.inputDeviceName = name;
    }
    else
    {
        // ASIO: one device does both; its input channels come in pairs in the mixer
        count = deviceManager.getCurrentAudioDevice()->getInputChannelNames().size();
        setup.inputDeviceName = setup.outputDeviceName;
    }

    if (count <= 0)
        count = 2;

    count = juce::jmin (count, (int) LiveInput::maxChannels);
    juce::BigInteger wanted;
    wanted.setRange (0, count, true);

    if (! setup.useDefaultInputChannels && setup.inputChannels == wanted && ! deviceManager.getCurrentAudioDevice()->getActiveInputChannels().isZero())
        return {};

    setup.useDefaultInputChannels = false;
    setup.inputChannels = wanted;
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);
    sendChangeMessage();

    if (error.isNotEmpty())
        return { InputIssue::failed, 0, error };
    if (! hasAudioInput())
        return { InputIssue::noInputDevice, 0, {} };
    return {};
}

bool AudioEngine::audioInputNeeded() const
{
    if (audioArmed || ! armedInserts.empty() || hwinsert::returnsFromInput (song))
        return true;
    return std::any_of (song.mixer.begin(), song.mixer.end(), [] (const MixerTrack& t) { return t.inputChannel >= 0 || t.flInput >= 0; });
}

void AudioEngine::releaseAudioInputIfUnused()
{
    if (! hasAudioInput() || audioInputNeeded())
        return;

    auto* type = deviceManager.getCurrentDeviceTypeObject();
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    // a driver with separate input devices (Windows Audio) keeps capturing while an input device is set, even with
    // no channel active: the device goes too (its name is remembered for the next openAudioInput)
    if (type != nullptr && type->hasSeparateInputsAndOutputs() && setup.inputDeviceName.isNotEmpty())
    {
        releasedInputDevice = setup.inputDeviceName;
        setup.inputDeviceName = {};
    }
    deviceManager.setAudioDeviceSetup (setup, true);
    sendChangeMessage();
}

void AudioEngine::setInsertInput (int track, int channel, bool stereo)
{
    if (track < 0 || track >= (int) song.mixer.size()) // (the Master too: REA round 143)
        return;

    editSong ([=] (Song& s) {
        s.mixer[(std::size_t) track].inputChannel = channel;
        s.mixer[(std::size_t) track].inputStereo = stereo;
    });

    recordingError = {};

    if (channel >= 0 && ! hasAudioInput())
        if (const auto result = openAudioInput(); result.issue != InputIssue::none)
            notify (result.issue, track, result.detail);
}

void AudioEngine::setInsertMonitor (int track, InputMonitor mode)
{
    if (track >= 0 && track < (int) song.mixer.size())
        editSong ([=] (Song& s) { s.mixer[(std::size_t) track].monitor = mode; });
}

void AudioEngine::setInsertRecordSource (int track, RecordSource source)
{
    if (track > 0 && track < (int) song.mixer.size() && song.mixer[(std::size_t) track].recordSource != source)
        editSong ([=] (Song& s) { s.mixer[(std::size_t) track].recordSource = source; });
}

void AudioEngine::setInsertRecordDryToo (int track, bool on)
{
    if (track > 0 && track < (int) song.mixer.size() && song.mixer[(std::size_t) track].recordDryToo != on)
        editSong ([=] (Song& s) { s.mixer[(std::size_t) track].recordDryToo = on; });
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    // MIDI thread: queues and atomics only (and a short copy of the device settings)
    prefs::MidiInputPrefs device;
    auto velocityCurve = prefs::VelocityCurve::linear;
    auto locked = -1; // a channel that "receives notes from" this device (FL's channel lock)
    auto jumpChannel = 0;
    const auto deviceName = source != nullptr ? source->getName().toStdString() : std::string();
    {
        const juce::SpinLock::ScopedLockType lock (midiPrefsLock);
        const auto& name = deviceName;
        device = midiPrefsShared.input (name);
        velocityCurve = midiPrefsShared.velocity;
        const auto midiChannel = m.getChannel();
        for (const auto& l : noteLocks)
            if (l.device == name && (l.midiChannel == 0 || l.midiChannel == midiChannel))
            {
                locked = l.channel;
                if (l.midiChannel != 0)
                    break; // a lock on this very MIDI channel wins over one on any channel
            }
        jumpChannel = midiPrefsShared.songMarkerJumpChannel;
    }
    if (locked >= 0)
        device.target = locked;

    if (! device.enabled)
        return;

    // the external clock LYNX follows (round 165f): clock, start / stop / continue, song position
    if (m.getRawDataSize() > 0 && (m.getRawData()[0] >= 0xf2) && source != nullptr && midiSync.handleInput (source->getName(), m))
        return;

    midiActivity.fetch_add (1, std::memory_order_relaxed); // the toolbar's MIDI input light
    if (m.getRawDataSize() > 0 && prefs::midiMeansPlaying (m.getRawData()[0]))
        midiPlayed.fetch_add (1, std::memory_order_relaxed); // what holds a backup back (not clock or active sensing)

    // FL's "Song marker jump (MIDI channel)": notes on that channel move the song to a marker instead of playing
    if (jumpChannel > 0 && m.getChannel() == jumpChannel && m.isNoteOnOrOff())
    {
        if (m.isNoteOn())
            pendingMarkerNote.store (m.getNoteNumber());
        return;
    }

    // FL's playlist live mode channel (round 165m): its notes launch the performance clips instead of playing
    if (const auto pc = performanceMidiChannel.load (std::memory_order_relaxed);
        pc != 0 && m.getChannel() == pc && m.isNoteOnOrOff() && performanceMidiOn.load (std::memory_order_relaxed))
    {
        perf::Command c;
        if (performanceMidi.note (m.getNoteNumber(), m.isNoteOn(), c))
            performanceLive->midiCommands.push (c);
        return;
    }

    // FL's note links (round 190cs; controller number 256 + key): a linked key moves its control instead of playing
    if (m.isNoteOnOrOff())
    {
        const auto key = m.getNoteNumber();
        const auto value = m.isNoteOn() ? (int) m.getVelocity() : 0;
        if ((linkedNotes[(std::size_t) (key >> 6)].load (std::memory_order_relaxed) >> (key & 63) & 1u) != 0
            || mapCaptureNotes.load (std::memory_order_relaxed)) // (round 190ef: MIDI Map Mode takes every key)
        {
            controllerQueue.push ({ linkNoteBase + key, m.getChannel() - 1, value, device.port });
            return;
        }
        if (value > 0) // (and offered to the link window's Auto detect)
            controllerQueue.push ({ linkNoteBase + key, m.getChannel() - 1, value, device.port, true });
    }

    // a controller profile's buttons, knobs and faders (round 190aw): to the message thread, not into the instruments.
    // Controllers linked by hand stay with their links.
    if (source != nullptr && controllerIo.take (deviceName, m.getRawData(), m.getRawDataSize(), linkedCcs))
    {
        if (m.isController()) // (still offered to MIDI learn)
            controllerQueue.push ({ m.getControllerNumber(), m.getChannel() - 1, m.getControllerValue(), device.port });
        return;
    }

    // issue #108: an MPE controller (the device's MPE setting). Its member channels' pitch bend, CC 74 and pressure
    // belong to the notes on them: a sounding note's expression goes to the channels its note on reached, a note on /
    // off carries the note's own values (played, and recorded as the note's fine pitch, Mod X and Mod Y). Everything
    // else (the master channel, other controllers) is handled as from any controller. No channel filter in this mode.
    mpe::InEvent mpeEvents[mpe::Input::maxEvents];
    const mpe::InEvent* mpeNote = nullptr;
    if (device.mpe)
    {
        if (const mpe::Zone zone { true, device.mpeMembers, (float) device.mpeBendRange }; ! (zone == mpeZone))
        {
            mpeZone = zone;
            mpeInput.setZone (zone);
        }
        const auto count = mpeInput.feed (m.getRawData(), m.getRawDataSize(), mpeEvents);
        if (count == 0)
            return; // (the zone's own configuration)
        if (mpeEvents[0].type == mpe::InEvent::Type::expr)
        {
            for (int k = 0; k < count; ++k)
            {
                const auto& x = mpeEvents[k];
                InputHeldNotes::Target to[InputHeldNotes::maxTargets];
                const auto n = midiHeld.targetsOf (x.key, to);
                for (int i = 0; i < n; ++i)
                {
                    NoteEvent e;
                    e.type = NoteEvent::Type::voiceProp;
                    e.expression = true;
                    e.channel = to[i].channel;
                    e.key = to[i].key;
                    e.prop = (std::uint8_t) (x.expr == mpe::Expr::pitch ? vfx::Prop::pitch : x.expr == mpe::Expr::timbre ? vfx::Prop::modX : vfx::Prop::modY);
                    e.velocity = x.expr == mpe::Expr::pressure ? mpe::modYOfPressure (x.value) : x.value;
                    if (e.channel >= 0)
                        midiEvents.push (e);
                }
            }
            return;
        }
        if (mpeEvents[0].type != mpe::InEvent::Type::plain)
            mpeNote = &mpeEvents[0];
    }
    else if (device.midiChannel != 0 && m.getChannel() != device.midiChannel)
        return;

    // the pitch wheel, the mod wheel and the other controllers play into the channels the notes go to (round 162,
    // issue #9: they did nothing unless linked): hosted plugins get the MIDI message, LYNX's instruments bend by the
    // channel's pitch range. A CC with a MIDI link only moves its parameter.
    const auto recordBend = transport.recording.load() && transport.playing.load();
    const auto bendTick = recordBend ? estimatedPositionNow() : 0.0;
    auto playControl = [this, &device, &m, recordBend, bendTick] (int control, float value) {
        NoteEvent e;
        e.type = NoteEvent::Type::control;
        e.key = control;
        e.velocity = value;
        auto send = [&] {
            midiEvents.push (e);
            // recording: the wheel is written too (the channel's pitch bend lane / automation clip, like FL)
            if (recordBend && control == NoteEvent::controlPitchBend)
                bendRecordQueue.push ({ bendTick, e.channel, juce::jlimit (0, 15, m.getChannel() - 1), (value + 1.0f) * 0.5f, true });
        };
        const auto extra = juce::jlimit (0, maxLiveChannels, extraLiveCount.load());
        if (device.target >= 0)
        {
            e.channel = device.target;
            send();
            return;
        }
        for (int i = -1; i < extra; ++i)
        {
            e.channel = i < 0 ? liveChannel.load() : extraLiveChannels[(std::size_t) i].load();
            if (e.channel >= 0)
                send();
        }
    };

    if (m.isController())
    {
        const auto cc = m.getControllerNumber();
        controllerQueue.push ({ cc, m.getChannel() - 1, m.getControllerValue(), device.port });
        // FL's "Foot pedal controls note off" (round 187a [DECOMP FUN_00fdb260]): with it on, CC 64 / CC 66 hold the
        // played channels' notes and don't reach the plugins
        if (const auto mode = PedalHold::modeFor (cc, m.getControllerValue()); mode >= 0 && footPedalNoteOff.load (std::memory_order_relaxed))
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto stamped = m.getTimeStamp() * 1000.0;
            const auto tick = recordTickFor (std::abs (stamped - now) < 1000.0 ? stamped : now);
            const auto taking = transport.recording.load() && (transport.playing.load() || isCountingIn());
            auto pedal = [&] (int channel) {
                pedalHold.setMode (channel, mode, [&] (int key) {
                    NoteEvent e;
                    e.type = NoteEvent::Type::off;
                    e.channel = channel;
                    e.key = key;
                    midiEvents.push (e);
                    logPlayedNote (channel, key, 0.0f, false, transport.playing.load() ? tick : -1.0, taking);
                    if (taking) // (the recorded note lasts until the pedal comes up, as in FL)
                        midiRecordQueue.push ({ tick, channel, key, 0.0f, false });
                });
            };
            if (device.target >= 0)
                pedal (device.target);
            else
            {
                const auto extra = juce::jlimit (0, maxLiveChannels, extraLiveCount.load());
                for (int i = -1; i < extra; ++i)
                    if (const auto channel = i < 0 ? liveChannel.load() : extraLiveChannels[(std::size_t) i].load(); channel >= 0)
                        pedal (channel);
            }
            return;
        }
        // (120…127 are channel mode messages: All Sound / Notes Off, reset…)
        if (cc >= 0 && cc < 120 && (linkedCcs[(std::size_t) (cc >> 6)].load (std::memory_order_relaxed) >> (cc & 63) & 1u) == 0)
            playControl (cc, (float) m.getControllerValue() / 127.0f);
        return;
    }

    if (m.isPitchWheel())
    {
        playControl (NoteEvent::controlPitchBend, juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8192.0f));
        return;
    }

    if (m.isChannelPressure())
    {
        playControl (NoteEvent::controlPressure, (float) m.getChannelPressureValue() / 127.0f);
        return;
    }

    if (m.isProgramChange()) // (round 165c: the selected channel's plugin / VFX Sequencer, as FL passes it on)
    {
        playControl (NoteEvent::controlProgram, (float) m.getProgramChangeNumber() / 127.0f);
        return;
    }

    if (! m.isNoteOnOrOff())
        return;

    const auto on = m.isNoteOn();
    markHeldKey (m.getChannel() - 1, m.getNoteNumber(), on);

    if (on)
        startOnInput();

    // a note-off goes to the channels and key its note-on reached (the selection or the scale snap may have
    // changed while the key was down); a repeated note-on first stops what the previous one started
    InputHeldNotes::Target targets[InputHeldNotes::maxTargets], stale[InputHeldNotes::maxTargets];
    auto count = 0;
    auto staleCount = 0;

    if (on)
    {
        const auto key = snapInputKey (m.getNoteNumber());
        const auto extra = juce::jlimit (0, maxLiveChannels, extraLiveCount.load());

        // the primary selected channel, then the other selected ones (or the device's own channel)
        if (device.target >= 0)
            targets[count++] = { device.target, key };
        else
            for (int i = -1; i < extra && count < InputHeldNotes::maxTargets; ++i)
                targets[count++] = { i < 0 ? liveChannel.load() : extraLiveChannels[(std::size_t) i].load(), key };

        staleCount = midiHeld.noteOn (m.getNoteNumber(), targets, count, stale);
    }
    else
        count = midiHeld.noteOff (m.getNoteNumber(), targets);

    // the time the device stamped the message with (JUCE: getMillisecondCounterHiRes / 1000), not when this thread got
    // to it; a stamp from elsewhere (0, another clock) falls back to now (round 170d)
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto stamped = m.getTimeStamp() * 1000.0;
    const auto eventMs = std::abs (stamped - now) < 1000.0 ? stamped : now;
    const auto taking = transport.recording.load() && (transport.playing.load() || isCountingIn());
    const auto tick = recordTickFor (eventMs);

    auto push = [this, taking, tick, mpeNote] (const InputHeldNotes::Target& t, bool noteOn, float velocity) {
        NoteEvent e;
        e.type = noteOn ? NoteEvent::Type::on : NoteEvent::Type::off;
        e.channel = t.channel;
        e.key = t.key;
        e.velocity = velocity;
        if (mpeNote != nullptr && noteOn) // issue #108: the note starts at its own bend, timbre and pressure
        {
            e.expression = true;
            e.cents = mpeNote->cents;
            e.modX = mpeNote->timbre;
            e.modY = mpe::modYOfPressure (mpeNote->pressure);
        }
        midiEvents.push (e);
        logPlayedNote (e.channel, e.key, e.velocity, noteOn, transport.playing.load() ? tick : -1.0, taking);

        // (round 170d: during the count-in too; the time is the device's, against the audio clock)
        if (taking && mpeNote != nullptr) // (the "on": its bend and timbre; the "off": its peak pressure)
            midiRecordQueue.push ({ tick, e.channel, e.key, e.velocity, noteOn, true, mpe::finePitchOfCents (mpeNote->cents), mpeNote->timbre,
                                    mpe::modYOfPressure (mpeNote->pressure) });
        else if (taking)
            midiRecordQueue.push ({ tick, e.channel, e.key, e.velocity, noteOn });
        if (slotTaking.load (std::memory_order_relaxed)) // a slot of the clip grid records (round 190bv)
            slotRecordQueue.push ({ tick, e.channel, e.key, e.velocity, noteOn });
    };

    for (int i = 0; i < staleCount; ++i)
        push (stale[i], false, 0.0f);

    // round 190ao: a hosted instrument with its own velocity setting answers with it instead of the MIDI settings' curve
    std::uint16_t ownVelocity[InputHeldNotes::maxTargets] {};
    if (on)
    {
        const juce::SpinLock::ScopedLockType lock (midiPrefsLock);
        for (int i = 0; i < count; ++i)
            if (targets[i].channel >= 0 && targets[i].channel < (int) channelVelocity.size())
                ownVelocity[i] = channelVelocity[(std::size_t) targets[i].channel];
    }

    for (int i = 0; i < count; ++i)
    {
        // the foot pedal (round 187a): a held note struck again is released first; a note-off it holds waits for it
        if (on && pedalHold.noteOn (targets[i].channel, targets[i].key))
            push (targets[i], false, 0.0f);
        if (! on && pedalHold.noteOff (targets[i].channel, targets[i].key))
            continue;
        push (targets[i], on, on ? prefs::mapPluginVelocity (m.getFloatVelocity(), ownVelocity[i] & 0xff, ownVelocity[i] >> 8, velocityCurve) : 0.0f);
    }
}

void AudioEngine::enableAllMidiInputs()
{
    // (a device turned off in the MIDI settings is closed, so other programs can use it)
    std::vector<prefs::MidiPort> available;
    std::vector<std::string> open;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        available.push_back ({ device.identifier.toStdString(), device.name.toStdString() });
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
            open.push_back (device.identifier.toStdString());
    }

    const auto plan = prefs::planMidiInputs (midiPrefs, available, lastMidiInputIds, open);
    for (const auto& id : plan.close)
        deviceManager.setMidiInputDeviceEnabled (juce::String (id), false);
    for (const auto& id : plan.reopen) // it went away and came back: the old handle may be dead
        deviceManager.setMidiInputDeviceEnabled (juce::String (id), false);
    for (const auto* list : { &plan.reopen, &plan.open })
        for (const auto& id : *list)
            deviceManager.setMidiInputDeviceEnabled (juce::String (id), true);

    lastMidiInputIds.clear();
    for (const auto& port : available)
        lastMidiInputIds.push_back (port.id);

    // the devices that run a controller profile (round 190aw)
    std::vector<std::string> names;
    for (const auto& port : available)
        names.push_back (port.name);
    controllerIo.configure (midiPrefs, names, true);
}

void AudioEngine::setControllerSurface (controller::Surface* surface) { controllerIo.setSurface (surface); }

juce::File AudioEngine::controllerProfilesFolder() { return PluginHost::settingsFolder().getChildFile ("Controllers"); }

void AudioEngine::reloadControllerProfiles()
{
    controllerIo.loadProfiles (controllerProfilesFolder());
    if (midiOpen)
        enableAllMidiInputs();
    sendChangeMessage();
}

void AudioEngine::reconnectMidiInputs()
{
    if (! midiOpen) // (only with an audio device: the real app, once its MIDI devices are open)
        return;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
            deviceManager.setMidiInputDeviceEnabled (device.identifier, false);
    enableAllMidiInputs();
}

void AudioEngine::setMidiPrefs (const prefs::MidiPrefs& p, bool save)
{
    midiPrefs = p;
    {
        const juce::SpinLock::ScopedLockType lock (midiPrefsLock);
        midiPrefsShared = p;
    }
    performanceMidiChannel.store (p.performanceChannel);
    footPedalNoteOff.store (p.footPedalNoteOff);
    if (! p.pickup)
        pickups.clear();
    if (midiOpen) // (only with an audio device: the real app, once its MIDI devices are open; they open with these)
    {
        enableAllMidiInputs();
        midiSync.configure (p);
    }
    if (save)
        midiSettingsFile().replaceWithText (juce::String (p.toText()));
    sendChangeMessage();
}

void AudioEngine::setUndoLevels (int levels)
{
    maxUndoSteps = (std::size_t) prefs::clampUndoLevels (levels);
    while (undoStack.size() > maxUndoSteps)
        undoStack.pop_front();
}

juce::StringArray AudioEngine::getActiveMidiInputs() const
{
    juce::StringArray names;

    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
            names.add (device.name);

    return names;
}

void AudioEngine::setAudioRecordingArmed (bool armed)
{
    audioArmed = armed;
    recordingError = {};

    if (armed)
    {
        // opens the input of the current driver (every channel, so each insert can pick its own)
        if (! hasAudioInput())
            if (const auto result = openAudioInput(); result.issue != InputIssue::none)
            {
                recordingError = "no audio input";
                notify (result.issue, 0, result.detail);
            }

        if (! recordThread.isThreadRunning())
            recordThread.startThread();
    }

    sendChangeMessage();
}

void AudioEngine::setRecording (bool shouldRecord)
{
    if (shouldRecord && ! transport.recording.load())
        recordingError = {}; // a new attempt
    transport.recording.store (shouldRecord);
    liveInput.recording.store (shouldRecord);
    updateRecording();
    sendChangeMessage();
}

bool AudioEngine::cancelRecording()
{
    if (! transport.recording.load())
        return false;
    // FL [DECOMP 0xf1d7e0]: the recorded-event buffers are emptied, every disk-recording take is dropped and its track
    // re-armed, the record button is released; playback is not stopped. LYNX writes a take into the song as it goes, so
    // the pass's edits are undone instead (one undo step per take; a step the user made while recording goes too).
    updateRecording(); // (what was played up to now, and the start of this pass if the timer hasn't seen it yet)
    const auto armed = armedInserts;
    const auto inPass = recordPassActive && recordPassMarked; // (record on but stopped: the last pass is already kept)
    discardAudioTakes = true;
    setRecording (false); // (the takes close here: the notes still held are committed, the audio files deleted)
    discardAudioTakes = false;
    for (const auto t : armed)
        if (std::find (armedInserts.begin(), armedInserts.end(), t) == armedInserts.end())
            setInsertArmed (t, true);
    if (inPass && ! sessionHistory)
    {
        const auto made = undoStack.size() + recordPassTrimmed;
        const auto steps = made > recordPassUndoBase ? made - recordPassUndoBase : 0;
        for (std::size_t i = 0; i < steps && ! undoStack.empty(); ++i)
        {
            undo();
            if (! redoStack.empty())
                redoStack.pop_back(); // (nothing to redo: the pass is gone)
        }
    }
    recordPassMarked = false;
    sendChangeMessage();
    return true;
}

void AudioEngine::updateRecording()
{
    const auto playing = transport.playing.load();

    // FL's "Disarm on stop" (round 166e): the record switch goes off once playback stops after recording
    if (recordSwitch.update (playing, transport.recording.load(), recordOptions.recordOffOnStop))
    {
        transport.recording.store (false);
        liveInput.recording.store (false);
        sendChangeMessage();
    }
    // (round 170d: a take starts with its count-in, so the notes played during it are kept)
    const auto counting = isCountingIn();
    const auto active = transport.recording.load() && (playing || counting);
    // a recording pass starts: what Cancel recording goes back to (round 187a)
    if (active && ! recordPassActive)
    {
        recordPassMarked = true;
        recordPassUndoBase = undoStack.size();
        recordPassTrimmed = 0;
    }
    recordPassActive = active;

    if (playing)
        lastPlayingTick = estimatedPositionNow();
    else if (counting && takeCatch.start >= 0)
        lastPlayingTick = (double) takeCatch.start;

    // the take starts when playing with recording on, or at the first recorded event
    // (which is only queued in that situation, even if this timer hasn't seen it yet)
    auto beginTake = [this] (double at) {
        if (recorder.isActive())
            return;

        const auto& pattern = std::as_const (song).activePattern();
        recorder.begin (pattern.effectiveLength (song.beatsPerBar));
        // Pattern mode with Loop record off (round 170e5, FL's "never" play end [DECOMP 0x11d4800]): the pattern plays
        // once from the start of this pass and on past its end; the notes stay where they were played, so it grows
        if (recordloop::patternLinear (song.mode, recordOptions.loopRecord))
        {
            const auto origin = recordloop::patternOrigin (at, pattern.effectiveLength (song.beatsPerBar));
            recorder.setPatternOrigin (origin);
            liveInput.patternOrigin.store (origin);
        }
        takeHasUndoStep = false;
        takeClearedChannels.clear();
        // Song mode without a loop region: while recording, the song plays on past its end until recording stops (FL,
        // round 168k [EXEC]: a 1-bar song recorded from bar 1 counted on to bar 8 instead of going back to bar 1), so
        // the notes stay where they were played
        // (Loop record off, round 170e3: through the loop region as well)
        if (recordloop::notesLinear (song.mode, song.loopPlays(), recordOptions.loopRecord))
        {
            noteTakeLinear = true;
            recorder.setLinear (true);
            updateLinearSong();
        }
    };

    if (active)
        beginTake (lastPlayingTick);

    RecordedEvent e;

    // FL's "Enable recording markers" (song mode): a note that starts outside the Start / Stop recording ranges is
    // not recorded (its release finds no held note and is ignored too)
    const auto punch = activePunch.has_value() ? std::vector<PunchRange> { *activePunch } // pre-roll / punch in-out (round 163)
                       : recordOptions.recordingMarkers && song.mode == PlayMode::song ? punchRanges (song.markers()) : std::vector<PunchRange> {};
    auto outsidePunch = [this, &punch] (const RecordedEvent& ev) {
        if (punch.empty() || ! ev.on)
            return false;
        auto tick = ev.tick;
        if (! song.loopPlays() && ! noteTakeLinear)
            tick = std::fmod (std::max (0.0, tick), (double) std::max<std::int64_t> (1, song.songLength()));
        return ! inPunchRanges (punch, tick);
    };

    while (midiRecordQueue.pop (e))
    {
        // (count-in notes: onto the start within the catch window, else at their real time; round 170d)
        if (! recordOptions.recordNotes || ! catchEarlyNote (e, takeCatch) || outsidePunch (e)) // recording filter: notes off
            continue;
        beginTake (std::min (lastPlayingTick, e.tick));
        recorder.add (song, e);
    }

    for (auto ui : uiRecorded)
    {
        if (! recordOptions.recordNotes || ! catchEarlyNote (ui, takeCatch) || outsidePunch (ui))
            continue;
        beginTake (std::min (lastPlayingTick, ui.tick));
        recorder.add (song, ui);
    }

    uiRecorded.clear();

    updateAudioRecording();
    takeRecordedBends();
    updateAutomationRecording (active);

    if (! active && recorder.isActive())
        recorder.end (song, lastPlayingTick);
    if (! active && ! counting)
        takeCatch = {}; // the take is over: its catch with it

    auto notes = recorder.takeFinished();

    if (! recorder.isActive() && std::exchange (noteTakeLinear, false))
        updateLinearSong();
    if (! recorder.isActive() && liveInput.patternOrigin.load (std::memory_order_relaxed) >= 0.0)
        liveInput.patternOrigin.store (-1.0); // the take is over: the pattern loops again

    if (notes.empty())
        return;

    // the whole take is a single undo step: overdub or replace, quantised to the record grid; in song mode the notes
    // go where they were played and a clip of the pattern is placed from the bar of the take's first note (FL; round
    // 168k: they used to fold into the pattern's first bars)
    const auto options = recordOptions;
    editSong ([&] (Song& s) {
        TakeQuantize q;
        q.grid = options.quantise;
        q.starts = options.quantiseStarts;
        q.end = options.noteEnd;
        q.stepSequencer = options.toStepSequencer;
        commitNoteBatch (s, recorder, std::move (notes), options.mode, q, takeClearedChannels, options.placeSongClip, ! takeHasUndoStep);
    }, ! takeHasUndoStep);
    takeHasUndoStep = true;
}

void AudioEngine::launcherCommand (const launcher::Command& command)
{
    if (clipLauncher == nullptr || ! song.clipGrid)
        return;
    using T = launcher::Command::Type;
    const auto starts = command.type == T::launch || command.type == T::launchScene || command.type == T::record;
    if (! transport.playing.load() && ! starts && command.type != T::backToArrangement && command.type != T::reset
        && command.type != T::recordEnd) // (a recordEnd while stopped cancels a recording that never began)
        return;
    if (starts && song.mode != PlayMode::song) // the grid plays with the song
        editSong ([] (Song& s) { s.mode = PlayMode::song; }, false);
    // a scene with its own tempo and bar length: the song takes them when it is launched
    if (command.type == T::launchScene && command.scene >= 0 && command.scene < (int) song.clipGrid->scenes.size())
        if (const auto& scene = song.clipGrid->scenes[(std::size_t) command.scene]; scene.tempo > 0.0 || scene.beatsPerBar > 0)
            editSong ([tempo = scene.tempo, bar = scene.beatsPerBar, unit = scene.beatUnit] (Song& s) {
                if (tempo > 0.0)
                    s.bpm = tempo;
                if (bar > 0)
                    s.beatsPerBar = session::sceneBarBeats (bar, unit); // (round 190em: n/d as the same bar in quarter notes)
            }, false);
    if (starts && ! transport.playing.load() && ! isCountingIn())
    {
        // a recording into a slot counts in first, as the record options say (round 190bv)
        if (command.type == T::record && recordOptions.countInBars > 0)
            transport.countIn.store ((double) countInLength (song, recordOptions.countInBars, (std::int64_t) transport.positionTicks.load()));
        else
            play();
        sendChangeMessage();
    }
    clipLauncher->commands.push (command);
}

void AudioEngine::setSongWithPluginsOff (Song newSong)
{
    holdPlugins();
    pluginHoldOwner.store (nullptr); // (so the setSong below does not end it)
    setSong (std::move (newSong));
    pluginHoldOwner.store (this);
}

bool AudioEngine::pluginsOff() const { return pluginsHeld() && pluginHoldOwner.load() == this; }

std::vector<pluginhold::Use> AudioEngine::hostedPluginUses() const
{
    return pluginhold::uses (song, pluginsOff() ? pluginHoldState() : pluginhold::State());
}

int AudioEngine::switchPluginOn (const std::string& id)
{
    if (! pluginsOff())
        return 0;
    // the plugins already running keep what was set in them since the last save (a Patcher that holds one of them and
    // the plugin switched on now is built again from the project)
    capturePluginStates();
    if (id.empty())
        endPluginHold();
    else
        releasePlugin (id);
    const auto slots = pluginhold::slotsOf (song, id);
    if (! id.empty() && pluginhold::countOff (song, pluginHoldState()) == 0)
        endPluginHold(); // the last one: the mode is over
    if (! pluginsHeld())
        pluginHoldOwner.store (nullptr);
    plugins.clearLoadFailures();
    // this one build creates the plugin's slots anew (they hold the silent placeholders of when it was off)
    rebuildPluginId = id;
    rebuildPlugins = true;
    publishSong();
    rebuildPlugins = false;
    rebuildPluginId.clear();
    return slots;
}

void AudioEngine::setSong (Song newSong)
{
    // round 190ap: another project in the engine that was opened without third-party plugins: the mode ends
    if (pluginHoldOwner.load() == this)
    {
        pluginHoldOwner.store (nullptr);
        endPluginHold();
    }
    if (clipLauncher != nullptr)
        clipLauncher->commands.push ({ launcher::Command::Type::reset });
    undoStack.clear();
    redoStack.clear();
    sessionUndo.clear();
    sessionRedo.clear();
    sessionAfterOpen = false;
    song = std::move (newSong);
    workSince = (double) juce::Time::currentTimeMillis();
    flsettings::forgetSolos(); // (round 190bx: a solo's remembered mute states belong to the project that had it)
    clipops::updateCrossfades (song);
    selectedChannel = 0;
    selectedChannels = { 0 };
    modified = false;
    checkHardwareInserts = true; // (a project measured on another setup: the notice, round 165c)

    // arming isn't saved: a new project starts disarmed; its inserts' audio inputs open the device input
    for (const auto t : std::vector<int> (armedInserts))
        setInsertArmed (t, false);
    followProjectOnDevice();

    publishSong();
}

bool AudioEngine::followProjectOnDevice()
{
    if (prefetcher == nullptr) // (only with an audio device: the real app; see openAudioDevice)
        return false;
    if (! hasAudioInput() && std::any_of (song.mixer.begin(), song.mixer.end(), [] (const MixerTrack& t) { return t.inputChannel >= 0 || t.flInput >= 0; }))
        openAudioInput();
    // an FL project's insert inputs (REA round 157c): FL's list index on the device open now
    auto changed = false;
    if (hasAudioInput())
    {
        auto* device = deviceManager.getCurrentAudioDevice();
        const auto channels = device != nullptr ? device->getInputChannelNames().size() : 0;
        for (auto& t : song.mixer)
        {
            changed = changed || t.flInput >= 0;
            if (t.flInput >= 0 && t.inputChannel < 0)
            {
                const auto source = flInputSource (t.flInput, channels);
                t.inputChannel = source.channel;
                t.inputStereo = source.stereo;
            }
            t.flInput = -1;
        }
    }
    // hardware output targets and hardware inserts: their channels open with the project (the ones the device has)
    openHardwareInsertIo();
    return changed;
}

void AudioEngine::saveUndoStep()
{
    // Dozens of edits arrive during a drag; only the first one of the click saves the state.
    bool samePress = false;

    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        const auto press = juce::Desktop::getInstance().getMainMouseSource().getLastMouseDownTime();
        samePress = press == lastUndoPress;
        lastUndoPress = press;
    }
    else
    {
        lastUndoPress = {};
    }

    if (! samePress)
    {
        undoStack.push_back (song);
        redoStack.clear();

        if (undoStack.size() > maxUndoSteps)
        {
            undoStack.pop_front();
            if (recordPassMarked)
                ++recordPassTrimmed;
        }
        if (sessionHistory)
        {
            closeSessionStep();
            sessionUndo.push_back ({ song, song });
            sessionRedo.clear();
            if (sessionUndo.size() > maxUndoSteps)
                sessionUndo.pop_front();
        }
    }
    if (sessionHistory)
        sessionAfterOpen = true;
}

void AudioEngine::editSong (const std::function<void (Song&)>& edit, bool undoable)
{
    if (undoable)
    {
        saveUndoStep();
        modified = true;
    }

    const auto lengthsBefore = patternLengths (song);
    const auto auditioned = comp::auditionedTrack (song);
    const auto pluginsBefore = detachNewPluginsOn ? freshPluginCounts (song) : std::map<std::string, int>();
    edit (song);
    song.keepOnePattern(); // (round 190ea: what follows, and every listener, may read the current pattern)
    if (detachNewPluginsOn)
        detachNewPlugins (pluginsBefore, song);
    comp::checkAudition (song, auditioned);
    followPatternLengths (lengthsBefore, song);
    clipops::updateCrossfades (song); // FL's automatic crossfades follow every edit (round 166e)
    publishSong();
}

void AudioEngine::editSongPatternOrder (const std::function<void (Song&)>& edit)
{
    saveUndoStep();
    modified = true;
    edit (song);
    clipops::updateCrossfades (song);
    publishSong();
}

void AudioEngine::editLevels (const std::function<void (Song&)>& edit, bool undoable)
{
    // A fader or knob drag (round 171c, discussion #10): every mouse move used to go through editSong, i.e. a whole
    // snapshot rebuild per move. While the song played that stalled the message thread and the fader (and the sound)
    // moved in steps of ~0.2 s. Here the edit changes only levels, so the snapshot in use takes the new values through
    // its live levels; the full publish follows once the mouse is released (timerCallback).
    if (publishedLevels == nullptr || ! publishedLevels->matches (song))
    {
        editSong (edit, undoable);
        return;
    }
    if (undoable)
    {
        saveUndoStep();
        modified = true;
    }
    edit (song);
    if (! publishedLevels->matches (song)) // (an edit that was not only levels)
    {
        publishSong();
        return;
    }
    publishedLevels->store (song);
    ++songStamp;
    levelsPending = true;
    ++levelEdits;
    levelsChanged.sendChangeMessage();
}

void AudioEngine::flushLevels()
{
    if (! levelsPending)
        return;
    levelsPending = false;
    publishSong();
}

void AudioEngine::editSongStructural (const std::function<void (Song&)>& edit)
{
    // (PR #37) the live plugin states go into the Song first: the undo step then holds them, and a plugin that can't be
    // matched to its old instance comes back with its current settings. Round 168s: the rebuild matches channels and
    // effect slots by their instance tag (RackState::build), so a moved plugin, duplicates included, keeps its running
    // instance and is not built again (issue #36: no second instance created and the old one torn down for a move).
    capturePluginStates();
    editSong (edit);
}

void AudioEngine::closeSessionStep()
{
    if (sessionAfterOpen && ! sessionUndo.empty())
        sessionUndo.back().second = song;
    sessionAfterOpen = false;
}

void AudioEngine::sessionStep (bool redo)
{
    closeSessionStep();
    auto& from = redo ? sessionRedo : sessionUndo;
    auto& to = redo ? sessionUndo : sessionRedo;
    if (from.empty())
        return;
    auto step = std::move (from.back());
    from.pop_back();
    std::vector<live::Key> skipped;
    // undo: the step's entities back to "before" (where they still are as "after" left them); redo the other way
    auto next = redo ? live::selectiveRevert (song, step.second, step.first, &skipped) : live::selectiveRevert (song, step.first, step.second, &skipped);
    juce::StringArray names;
    for (const auto& k : skipped)
        names.addIfNotAlreadyThere (juce::String::fromUTF8 (live::describe (k, song, [] (const char* s) { return tr (s).toStdString(); }).c_str()));
    to.push_back (std::move (step));
    song = std::move (next);
    modified = true;
    lastUndoPress = {};
    clipops::updateCrossfades (song);
    publishSong();
    if (! names.isEmpty() && onSessionUndoSkipped)
        onSessionUndoSkipped (names);
}

void AudioEngine::setSessionHistory (bool on)
{
    sessionHistory = on;
    sessionAfterOpen = false;
    sessionUndo.clear();
    sessionRedo.clear();
}

void AudioEngine::applyRemote (Song remote)
{
    closeSessionStep();
    lastUndoPress = {}; // (a drag that goes on after this is a new step: its "before" has the remote change)
    song = std::move (remote);
    clipops::updateCrossfades (song);
    publishSong();
}

void AudioEngine::undo()
{
    if (sessionHistory)
        return sessionStep (false);
    if (undoStack.empty())
        return;

    redoStack.push_back (std::move (song));
    song = std::move (undoStack.back());
    undoStack.pop_back();
    comp::setAudition (song, -1); // (not an edit: whatever an old state had auditioned stays out of it)
    modified = true;
    lastUndoPress = {};
    publishSong();
}

void AudioEngine::redo()
{
    if (sessionHistory)
        return sessionStep (true);
    if (redoStack.empty())
        return;

    undoStack.push_back (std::move (song));
    song = std::move (redoStack.back());
    redoStack.pop_back();
    comp::setAudition (song, -1);
    modified = true;
    lastUndoPress = {};
    publishSong();
}

namespace
{
const Song& historyState (const std::deque<Song>& undo, const Song& current, const std::deque<Song>& redo, int index)
{
    if (index < (int) undo.size())
        return undo[(std::size_t) index];
    if (index == (int) undo.size())
        return current;
    return redo[redo.size() - 1 - (std::size_t) (index - (int) undo.size() - 1)];
}
} // namespace

ChangeLabel AudioEngine::getHistoryLabel (int index) const
{
    if (index <= 0 || index >= getHistorySize())
        return { TR_NOOP ("Start"), {} };
    if (sessionHistory) // (a live session: one's own steps, each named by its own before / after)
    {
        const auto i = (std::size_t) index - 1;
        const auto& step = i < sessionUndo.size() ? sessionUndo[i] : sessionRedo[sessionRedo.size() - 1 - (i - sessionUndo.size())];
        return describeChange (step.first, i + 1 == sessionUndo.size() && sessionAfterOpen ? song : step.second);
    }

    return describeChange (historyState (undoStack, song, redoStack, index - 1), historyState (undoStack, song, redoStack, index));
}

void AudioEngine::jumpToHistory (int index)
{
    index = juce::jlimit (0, getHistorySize() - 1, index);

    if (index == getHistoryPosition())
        return;
    if (sessionHistory)
    {
        while (getHistoryPosition() > index && ! sessionUndo.empty())
            sessionStep (false);
        while (getHistoryPosition() < index && ! sessionRedo.empty())
            sessionStep (true);
        return;
    }

    // move through the stacks, then publish once
    while (getHistoryPosition() > index && ! undoStack.empty())
    {
        redoStack.push_back (std::move (song));
        song = std::move (undoStack.back());
        undoStack.pop_back();
    }

    while (getHistoryPosition() < index && ! redoStack.empty())
    {
        undoStack.push_back (std::move (song));
        song = std::move (redoStack.back());
        redoStack.pop_back();
    }

    modified = true;
    lastUndoPress = {};
    publishSong();
}

void AudioEngine::setSelectedChannel (int channel)
{
    channel = juce::jlimit (0, juce::jmax (0, (int) song.channels.size() - 1), channel);
    setChannelSelection ({ channel }, channel);
}

void AudioEngine::setChannelSelection (std::vector<int> channels, int primary)
{
    const auto count = (int) song.channels.size();
    std::sort (channels.begin(), channels.end());
    channels.erase (std::unique (channels.begin(), channels.end()), channels.end());
    channels.erase (std::remove_if (channels.begin(), channels.end(), [count] (int c) { return c < 0 || c >= count; }), channels.end());
    primary = juce::jlimit (0, juce::jmax (0, count - 1), primary);

    if (std::find (channels.begin(), channels.end(), primary) == channels.end())
        channels.insert (std::lower_bound (channels.begin(), channels.end(), primary), primary);

    if (channels == selectedChannels && primary == selectedChannel)
    {
        publishSelection();
        return;
    }

    selectedChannels = std::move (channels);
    selectedChannel = primary;
    publishSelection();
    sendChangeMessage();
}

void AudioEngine::publishSelection()
{
    liveChannel.store (selectedChannel);
    int extra = 0;

    for (auto c : selectedChannels)
        if (c != selectedChannel && extra < maxLiveChannels)
            extraLiveChannels[(std::size_t) extra++].store (c);

    extraLiveCount.store (extra);
}

namespace
{
// Whether a drive / network share is there, within a time limit: a disconnected network drive can block a file
// query for half a minute, and an open probes many paths under the same root (round 112). One probe per root per open.
bool rootReachable (const std::string& root)
{
    auto result = std::make_shared<std::atomic<int>> (-1);
    std::thread ([result, root] {
        result->store (platform::localFile (root).isDirectory() ? 1 : 0); // FL's "C:\" spelling on macOS/Linux too
    }).detach();
    const auto started = juce::Time::getMillisecondCounter();
    while (result->load() < 0 && juce::Time::getMillisecondCounter() - started < 2000)
        juce::Thread::sleep (2);
    return result->load() == 1; // still unknown after 2 s: treated as missing (the probe thread finishes on its own)
}

std::string userOf (const Song& song, std::size_t track)
{
    return song.mixer[track].name.empty() ? (track == 0 ? std::string ("Master") : "Insert " + std::to_string (track)) : song.mixer[track].name;
}
} // namespace

namespace
{
// Where a Live set's library-relative samples live (Core Library, packs, User Library), newest install first; only
// folders that exist. Read-only: nothing is ever written there.
std::vector<std::string> liveLibraryRoots()
{
    std::vector<std::string> roots;
    auto add = [&roots] (const juce::File& f) {
        if (f.isDirectory())
            roots.push_back (f.getFullPathName().toStdString());
    };
   #if JUCE_WINDOWS
    const juce::File programData ("C:\\ProgramData\\Ableton");
   #elif JUCE_MAC
    const juce::File programData ("/Applications");
   #else
    const juce::File programData;
   #endif
    if (programData.isDirectory())
    {
        auto installs = programData.findChildFiles (juce::File::findDirectories, false, "Live*");
        std::sort (installs.begin(), installs.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
        for (const auto& install : installs)
        {
            add (install.getChildFile ("Resources").getChildFile ("Core Library"));
            add (install.getChildFile ("Contents").getChildFile ("App-Resources").getChildFile ("Core Library"));
        }
    }
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ableton");
    add (docs.getChildFile ("User Library"));
    add (docs.getChildFile ("Factory Packs"));
    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ableton");
    add (music.getChildFile ("User Library"));
    add (music.getChildFile ("Factory Packs"));
    return roots;
}
} // namespace

void AudioEngine::prepareFlpImport (const juce::File& file, const std::map<std::string, std::string>& relocations, SampleLibrary& library,
                                    PreparedFlp& out)
{
    juce::MemoryBlock data;
    {
        const loadtrace::Scope timing ("read_project");
        if (! file.loadFileAsData (data))
        {
            out.error = utf8 ("could not read the file");
            return;
        }
    }

    try
    {
        loadtrace::stage (0.20, 0.30, TR_NOOP ("Reading the project..."));
        const auto started = std::chrono::steady_clock::now();
        const auto* fileBytes = static_cast<const std::uint8_t*> (data.getData());
        // a Live set (.als, round 175a) takes the same pipeline: samples, plugins, report and Missing files window
        const auto liveSet = file.hasFileExtension ("als");
        // an FL Studio Mobile project (.flm, .songtmpl; round 182c) too
        const auto mobileProject = file.hasFileExtension ("flm;songtmpl");
        std::optional<flp::Project> project;
        if (! liveSet && ! mobileProject)
            project = flp::parseProject ({ fileBytes, data.getSize() });
        loadtrace::add ("parse", loadtrace::msSince (started));

        FlpImportOptions options;
        options.roots = findFlStudioRoots();
        options.projectFolder = file.getParentDirectory().getFullPathName().toStdString();
        // samples on a drive that is not there (unplugged external drive, dead network share): one probe per root
        auto roots = std::make_shared<loadtrace::RootProbe> ([] (const std::string& root) {
            const auto t = std::chrono::steady_clock::now();
            const auto ok = rootReachable (root);
            loadtrace::add ("probe_root", loadtrace::msSince (t));
            return ok;
        });
        options.fileExists = [roots] (const std::string& path) {
            if (! roots->rootAvailable (path))
                return false;
            const auto t = std::chrono::steady_clock::now();
            const auto exists = platform::localFile (path).existsAsFile();
            loadtrace::add ("probe_file", loadtrace::msSince (t));
            return exists;
        };
        options.readFile = [roots] (const std::string& path) {
            if (! roots->rootAvailable (path))
                return std::vector<std::uint8_t> {};
            const auto t = std::chrono::steady_clock::now();
            juce::MemoryBlock data;
            platform::localFile (path).loadFileAsData (data);
            const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
            loadtrace::add ("read_aux_file", loadtrace::msSince (t));
            return std::vector<std::uint8_t> (bytes, bytes + data.getSize());
        };
        auto decoded = std::make_shared<std::vector<std::shared_ptr<const SampleData>>>();
        options.sampleFrames = [&library, decoded] (const std::string& path) -> std::int64_t {
            const auto sample = library.load (juce::String::fromUTF8 (path.c_str()));
            if (sample != nullptr)
                decoded->push_back (sample); // kept: the rack uses it next (it was decoded twice before)
            return sample != nullptr ? (std::int64_t) sample->frames() : 0;
        };
        options.sampleSeconds = [&library, decoded] (const std::string& path) -> double {
            const auto sample = library.load (juce::String::fromUTF8 (path.c_str()));
            if (sample == nullptr || sample->sampleRate <= 0.0)
                return 0.0;
            decoded->push_back (sample);
            return (double) sample->frames() / sample->sampleRate;
        };
        // WAV that only exists inside the .flp (Slicex): goes to %APPDATA%\LYNX\flp-embedded, keyed by content
        options.saveEmbedded = [] (const std::vector<std::uint8_t>& wav, const std::string& name) -> std::string {
            const auto folder = PluginHost::settingsFolder().getChildFile ("flp-embedded");
            folder.createDirectory();
            const auto hash = juce::String::toHexString ((juce::int64) juce::DefaultHashFunctions::generateHash (
                juce::String::createStringFromData (wav.data(), (int) std::min<std::size_t> (wav.size(), 65536)) + juce::String ((juce::int64) wav.size()),
                0x7FFFFFFF));
            const auto file = folder.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (name.c_str())) + "-" + hash + ".wav");

            if (! file.existsAsFile() && ! file.replaceWithData (wav.data(), wav.size()))
                return {};

            return file.getFullPathName().toStdString();
        };

        out.report = {};
        options.report = &out.report;
        options.relocations = relocations;
        {
            const auto t = std::chrono::steady_clock::now();
            if (mobileProject)
            {
                FlmImportOptions flmOptions;
                flmOptions.common = options;
                if (! options.roots.factoryData.empty())
                    if (const auto root = flmResourceRootIn (options.roots.factoryData); platform::localFile (root).isDirectory())
                        flmOptions.resourceRoot = root;
                std::string flmError;
                auto song = importFlm (std::vector<std::uint8_t> (fileBytes, fileBytes + data.getSize()), flmOptions, &flmError);
                if (! song)
                {
                    out.error = utf8 (flmError.empty() ? "not a readable FL Studio Mobile project" : flmError.c_str());
                    return;
                }
                out.song = std::move (*song);
            }
            else if (liveSet)
            {
                AlsImportOptions alsOptions;
                alsOptions.common = options;
                alsOptions.libraryRoots = liveLibraryRoots();
                alsOptions.sessionOnPlaylist = alsSessionOnPlaylist.load();
                std::string alsError;
                auto song = importAls (std::vector<std::uint8_t> (fileBytes, fileBytes + data.getSize()), alsOptions, &alsError);
                if (! song)
                {
                    out.error = utf8 (alsError.empty() ? "not a readable Live set" : alsError.c_str());
                    return;
                }
                out.song = std::move (*song);
            }
            else
            {
                out.song = importFlp (*project, options);
            }
            loadtrace::add ("import", loadtrace::msSince (t));
        }
        loadtrace::add ("missing_roots", 0.0, (int) roots->missingRoots().size());
        loadtrace::add ("probe_skipped", 0.0, roots->skipped());

        // the project's third-party plugins, to say which channel/insert used each one that is missing
        for (const auto& c : out.song.channels)
            if (c.plugin.foreign.format != ForeignPlugin::Format::none)
                out.uses.push_back ({ c.plugin.foreign.name, c.name, true });
        for (std::size_t t = 0; t < out.song.mixer.size(); ++t)
            for (const auto& fx : out.song.mixer[t].effects)
                if (fx.kind == EffectKind::plugin && fx.plugin.foreign.format != ForeignPlugin::Format::none)
                    out.uses.push_back ({ fx.plugin.foreign.name, userOf (out.song, t), false });
        // (and the ones inside Patchers, REA_rodada141)
        std::function<void (const PatcherDoc&, const std::string&)> patcherUses = [&] (const PatcherDoc& doc, const std::string& user) {
            for (const auto& n : doc.nodes)
            {
                const auto& ref = n.generator ? n.channel.plugin : n.effect.plugin;
                if (ref.foreign.format != ForeignPlugin::Format::none)
                    out.uses.push_back ({ ref.foreign.name, user + " (Patcher)", n.generator });
                if (n.generator && n.channel.instrument == InstrumentKind::patcher)
                    patcherUses (n.channel.patcher, user);
                if (! n.generator && n.effect.kind == EffectKind::patcher)
                    patcherUses (n.effect.patcher, user);
            }
        };
        for (const auto& c : out.song.channels)
            if (c.instrument == InstrumentKind::patcher)
                patcherUses (c.patcher, c.name);
        for (std::size_t t = 0; t < out.song.mixer.size(); ++t)
            for (const auto& fx : out.song.mixer[t].effects)
                if (fx.kind == EffectKind::patcher)
                    patcherUses (fx.patcher, userOf (out.song, t));

        // the samples, decoded now (in parallel, off the message thread) so building the rack only finds them ready
        std::vector<std::string> paths;
        auto addPath = [&paths] (const std::string& p) {
            if (! p.empty() && std::find (paths.begin(), paths.end(), p) == paths.end())
                paths.push_back (p);
        };
        for (const auto& c : out.song.channels)
        {
            if (c.instrument == InstrumentKind::sampler || c.instrument == InstrumentKind::audioClip || c.instrument == InstrumentKind::granulizer
                || c.instrument == InstrumentKind::wavetraveller || c.instrument == InstrumentKind::fruityslicer
                || c.instrument == InstrumentKind::slicex
                || c.instrument == InstrumentKind::flkeys || c.instrument == InstrumentKind::directwave)
                addPath (c.samplePath);
            for (const auto& z : c.zones)
                addPath (z.path);
        }
        for (const auto& track : out.song.mixer)
            for (const auto& fx : track.effects)
                addPath (fx.samplePath);

        loadtrace::stage (0.30, 0.55, TR_NOOP ("Reading the samples..."), TR_NOOP ("Reading {0}... {1}/{2}"));
        const auto decodeStarted = std::chrono::steady_clock::now();
        std::atomic<std::size_t> next { 0 }, done { 0 };
        std::mutex keep;
        auto worker = [&] {
            for (auto i = next++; i < paths.size(); i = next++)
            {
                const auto t = std::chrono::steady_clock::now();
                auto sample = library.load (juce::String::fromUTF8 (paths[i].c_str()));
                loadtrace::add ("sample_decode", loadtrace::msSince (t));
                {
                    const std::scoped_lock l (keep);
                    if (sample != nullptr)
                        out.decoded.push_back (std::move (sample));
                }
                const auto n = ++done;
                loadtrace::step ((int) n, (int) paths.size(),
                                 juce::File::createFileWithoutCheckingPath (juce::String::fromUTF8 (paths[i].c_str())).getFileName().toStdString());
            }
        };
        const auto threads = (std::size_t) juce::jlimit (1, 6, juce::SystemStats::getNumCpus() - 1);
        std::vector<std::thread> pool;
        for (std::size_t i = 1; i < juce::jmin (threads, paths.size()); ++i)
            pool.emplace_back (worker);
        worker();
        for (auto& t : pool)
            t.join();
        for (auto& d : *decoded)
            out.decoded.push_back (std::move (d));

        // pitch-preserving stretch (audio clips that follow the tempo): the stretched copy too, in parallel, with the
        // same ratio the rack asks for (RackState::build) — it was computed on the message thread while building
        struct Stretch
        {
            std::string path;
            double ratio;
            StretchMethod method;
            double semitones;
            StretchMap map;
            std::string variant; // non-empty: the precomputed span is stretched (RackRenderer, round 180d)
            std::shared_ptr<const SampleData> span;
        };
        std::vector<Stretch> stretches;
        for (const auto& c : out.song.channels)
            if ((c.instrument == InstrumentKind::sampler || c.instrument == InstrumentKind::audioClip) && c.zones.empty() && ! c.samplePath.empty()
                && c.sampler.keepPitch && c.sampler.stretchBeats > 0.0f)
                if (const auto data = library.load (juce::String::fromUTF8 (c.samplePath.c_str())); data != nullptr && data->frames() > 0)
                {
                    const auto target = c.sampler.stretchBeats * 60.0 / std::max (1.0, out.song.bpm);
                    const auto method = c.sampler.stretchMethod;
                    const auto semitones = (double) c.sampler.stretchPitch;
                    // the same stretch as RackRenderer: the precomputed span when there is one (round 180d), else the file
                    const auto clip = c.instrument == InstrumentKind::audioClip;
                    auto settings = c.sampler;
                    if (clip)
                        settings.regionStart = 0.0f, settings.regionEnd = 1.0f;
                    const auto precomputes = clip ? c.sampler.precompute.any() : precompute::needed (c.sampler);
                    if (precomputes && c.sampler.warp.empty())
                    {
                        if (std::shared_ptr<const SampleData> span = precompute::apply (*data, settings); span != nullptr && span->frames() > 0)
                        {
                            const auto ratio = target / ((double) span->frames() / span->sampleRate);
                            auto variant = precompute::variantKey (settings, clip);
                            if (std::none_of (stretches.begin(), stretches.end(), [&] (const Stretch& x) {
                                    return x.path == c.samplePath && x.variant == variant && x.ratio == ratio && x.method == method && x.semitones == semitones; }))
                                stretches.push_back ({ c.samplePath, ratio, method, semitones, {}, std::move (variant), std::move (span) });
                        }
                        continue;
                    }
                    const auto ratio = target / ((double) precompute::stretchedFrames (data, c.sampler, clip) / data->sampleRate);
                    // (the same map the rack asks for: RackState::build)
                    auto map = warpStretchMap (c.sampler.warp, (double) data->frames(), data->sampleRate, (double) std::llround ((double) data->frames() * ratio));
                    if (std::none_of (stretches.begin(), stretches.end(), [&] (const Stretch& x) {
                            return x.path == c.samplePath && x.variant.empty() && x.ratio == ratio && x.method == method && x.semitones == semitones && x.map == map; }))
                        stretches.push_back ({ c.samplePath, ratio, method, semitones, std::move (map), {}, {} });
                }
        if (! stretches.empty())
        {
            const auto stretchStarted = std::chrono::steady_clock::now();
            std::atomic<std::size_t> nextStretch { 0 };
            auto stretchWorker = [&] {
                for (auto i = nextStretch++; i < stretches.size(); i = nextStretch++)
                    if (auto stretched = ! stretches[i].variant.empty()
                                             ? library.loadStretchedSpan (juce::String::fromUTF8 (stretches[i].path.c_str()), stretches[i].variant,
                                                                          stretches[i].span, stretches[i].ratio, stretches[i].method, stretches[i].semitones)
                                             : library.loadStretched (juce::String::fromUTF8 (stretches[i].path.c_str()), stretches[i].ratio, stretches[i].method, stretches[i].semitones, stretches[i].map))
                    {
                        const std::scoped_lock l (keep); // held until the rack is built (the cache drops unused lengths)
                        out.decoded.push_back (std::move (stretched));
                    }
            };
            std::vector<std::thread> helpers;
            for (std::size_t i = 1; i < juce::jmin (threads, stretches.size()); ++i)
                helpers.emplace_back (stretchWorker);
            stretchWorker();
            for (auto& t : helpers)
                t.join();
            loadtrace::add ("stretch_wall", loadtrace::msSince (stretchStarted), (int) stretches.size());
        }
        loadtrace::add ("samples_wall", loadtrace::msSince (decodeStarted), (int) paths.size());
        out.ok = true;
    }
    catch (const flp::Error& e)
    {
        out.error = juce::String::fromUTF8 (e.what());
    }
}

void AudioEngine::adoptPrepared (PreparedFlp& prepared)
{
    if (prepared.adopted)
        return;
    prepared.adopted = true;
    const auto t = std::chrono::steady_clock::now();
    plugins.clearPreloaded();
    plugins.setPreloadRate (transport.sampleRate.load());
    plugins.adoptFlPlugins (prepared.song, &prepared.missing, &prepared.matched, &prepared.withoutState, &prepared.substituted);
    loadtrace::add ("adopt_plugins", loadtrace::msSince (t));

    // the hosted plugins the rack will create, in its order (channels, then inserts)
    for (const auto& c : prepared.song.channels)
        if (c.instrument == InstrumentKind::plugin && ! c.plugin.id.empty())
            prepared.pluginQueue.push_back (c.plugin);
    for (const auto& track : prepared.song.mixer)
        for (std::size_t i = 0; i < track.effects.size() && i < (std::size_t) maxEffectSlots; ++i)
            if (track.effects[i].kind == EffectKind::plugin && ! track.effects[i].plugin.id.empty())
                prepared.pluginQueue.push_back (track.effects[i].plugin);
}

bool AudioEngine::preloadNextPlugin (PreparedFlp& prepared)
{
    adoptPrepared (prepared);
    if (prepared.pluginsDone >= prepared.pluginQueue.size())
        return false;
    const auto total = (int) prepared.pluginQueue.size();
    const auto& ref = prepared.pluginQueue[prepared.pluginsDone];
    auto wanted = 0;
    for (std::size_t i = 0; i <= prepared.pluginsDone; ++i)
        wanted += prepared.pluginQueue[i].id == ref.id && prepared.pluginQueue[i].state == ref.state ? 1 : 0;
    plugins.preload (ref, wanted);
    ++prepared.pluginsDone;
    loadtrace::step ((int) prepared.pluginsDone, total, {});
    return prepared.pluginsDone < prepared.pluginQueue.size();
}

bool AudioEngine::finishFlpImport (PreparedFlp&& prepared, bool asEdit)
{
    if (! prepared.ok)
        return false;
    adoptPrepared (prepared);
    auto& imported = prepared.song;
    importReport = std::move (prepared.report);
    const auto& uses = prepared.uses;

    missingPlugins = prepared.missing;
    matchedPlugins = prepared.matched;
    pluginsWithoutState = prepared.withoutState;
    missingPluginDetails.clear();
    for (const auto& name : prepared.missing)
    {
        MissingPlugin m;
        m.name = name.toStdString();
        for (const auto& u : uses)
            if (u.name == m.name)
            {
                if (std::find (m.usedBy.begin(), m.usedBy.end(), u.user) == m.usedBy.end())
                    m.usedBy.push_back (u.user);
                m.replacement = u.instrument ? "Built-in synth (a preset chosen by the channel name)" : "Nothing: the slot was left out of the mixer";
            }
        missingPluginDetails.push_back (std::move (m));
    }
    // missing, played by a LYNX substitute that keeps the original for when it is installed
    for (std::size_t t = 0; t < imported.mixer.size(); ++t)
        for (const auto& fx : imported.mixer[t].effects)
            if (fx.isSubstitute())
            {
                const auto name = substituteOriginalName (fx);
                auto it = std::find_if (missingPluginDetails.begin(), missingPluginDetails.end(),
                                        [&] (const MissingPlugin& m) { return m.name == name && ! m.replacementArg.empty(); });
                if (it == missingPluginDetails.end())
                    it = missingPluginDetails.insert (missingPluginDetails.end(),
                                                      MissingPlugin { name, {}, TR_NOOP ("LYNX SHARE substitute: {0} (the original plugin is used again once it is installed). FL Studio plays a missing plugin's slot unprocessed: a substitute slot's menu › Bypass all LYNX SHARE substitutes of this project does the same"),
                                                                      effectInfo (fx.kind).name });
                const auto user = userOf (imported, t);
                if (std::find (it->usedBy.begin(), it->usedBy.end(), user) == it->usedBy.end())
                    it->usedBy.push_back (user);
            }
    // found, but the project's settings are in the other format (VST2 <-> VST3): they play their default sound
    for (const auto& line : prepared.withoutState)
        missingPluginDetails.push_back ({ line.toStdString(), {}, TR_NOOP ("Its default settings: the project saved them for the other format (VST2/VST3)") });

    plugins.clearLoadFailures();
    loadtrace::stage (0.90, 0.99, TR_NOOP ("Building the mixer..."));
    {
        const loadtrace::Scope timing ("set_song");
        if (asEdit)
            editSong ([&imported] (Song& s) { s = std::move (imported); });
        else
        {
            stop();
            rewind();
            setSong (std::move (imported));
        }
    }
    plugins.clearPreloaded(); // (any left over: a slot the rack reused, nothing to keep)
    prepared.decoded.clear();

    // installed but failed to load (the rack was just built): silence / bypass, said as loudly as a missing one
    for (const auto& line : plugins.getLoadFailures())
        missingPluginDetails.push_back ({ line.toStdString(), {}, TR_NOOP ("Silence: the plugin is installed but did not load") });
    return true;
}

bool AudioEngine::importFlpFile (const juce::File& file, juce::String& error, const std::map<std::string, std::string>& relocations, bool asEdit)
{
    // a slow plugin module (a Waves shell) starts on its own thread while the samples decode (round 121)
    if (juce::MemoryBlock data; file.loadFileAsData (data))
        try
        {
            PluginHost::newOpen();
            plugins.startModulesFor (flpForeignPlugins (flp::parseProject ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() })));
        }
        catch (const flp::Error&)
        {
        }
    PreparedFlp prepared;
    prepareFlpImport (file, relocations, samples, prepared);
    if (! prepared.ok)
    {
        error = prepared.error;
        return false;
    }
    if (asEdit)
        keepRunningPlugins (prepared);
    loadtrace::stage (0.55, 0.90, TR_NOOP ("Loading plugins..."), TR_NOOP ("Loading {0}... {1}/{2}"));
    while (preloadNextPlugin (prepared))
    {
    }
    return finishFlpImport (std::move (prepared), asEdit);
}

void AudioEngine::keepRunningPlugins (PreparedFlp& prepared)
{
    // The open project imported again (Missing files › Apply): its plugins are already running with these settings,
    // and the rack keeps a slot whose plugin did not change. Creating them all again first (round 151: 71 plugins,
    // 25 s with the window frozen, then a crash inside a plugin as the second copies replaced the first) is skipped
    // for every plugin the song already has with the same settings.
    adoptPrepared (prepared);
    std::vector<const PluginRef*> running;
    for (const auto& c : song.channels)
        if (c.instrument == InstrumentKind::plugin && ! c.plugin.id.empty())
            running.push_back (&c.plugin);
    for (const auto& track : song.mixer)
        for (const auto& fx : track.effects)
            if (fx.kind == EffectKind::plugin && ! fx.plugin.id.empty())
                running.push_back (&fx.plugin);
    auto& queue = prepared.pluginQueue;
    queue.erase (std::remove_if (queue.begin(), queue.end(),
                                 [&running] (const PluginRef& ref) {
                                     const auto it = std::find_if (running.begin(), running.end(), [&ref] (const PluginRef* r) { return r != nullptr && *r == ref; });
                                     if (it == running.end())
                                         return false;
                                     *it = nullptr; // each running plugin stands for one queued copy
                                     return true;
                                 }),
                 queue.end());
}

const AutomationDisplay& AudioEngine::getAutomationDisplay()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    // (editors ask from their own frames: once per display frame is enough; round 174e: 5 ms, i.e. every frame at 144
    // Hz, unless a big project's automation takes long to evaluate, then 12 ms as before)
    if (now - automationDisplayAtMs >= (automationDisplayCostMs > 0.5 ? 12.0 : 5.0))
    {
        automationDisplayAtMs = now;
        const auto playing = transport.playing.load (std::memory_order_relaxed);
        automationDisplay.update (song, songStamp, estimatedPositionNow(), playing);
        automationDisplayCostMs = juce::Time::getMillisecondCounterHiRes() - now;
    }
    return automationDisplay;
}

RackState::Factories AudioEngine::rackFactories()
{
    RackState::Factories factories;
    factories.samples = [this] (const std::string& path) {
        const loadtrace::Scope timing ("rack_samples");
        return samples.load (juce::String::fromUTF8 (path.c_str()));
    };
    factories.stretchedSamples = [this] (const std::string& path, double ratio, StretchMethod method, double semitones, const StretchMap& map) {
        const loadtrace::Scope timing ("rack_stretch");
        return samples.loadStretched (juce::String::fromUTF8 (path.c_str()), ratio, method, semitones, map);
    };
    factories.stretchedSpans = [this] (const std::string& path, const std::string& variant, const std::shared_ptr<const SampleData>& span,
                                       double ratio, StretchMethod method, double semitones) {
        const loadtrace::Scope timing ("rack_stretch");
        return samples.loadStretchedSpan (juce::String::fromUTF8 (path.c_str()), variant, span, ratio, method, semitones);
    };
    factories.pluginInstruments = [this] (const PluginRef& ref, double sr) { return plugins.makeInstrument (ref, sr); };
    factories.pluginEffects = [this] (const PluginRef& ref, double sr) { return plugins.makeEffect (ref, sr); };
    return factories;
}

bool AudioEngine::auditionSound (const ChannelDef& channel, const std::vector<int>& keys, double* seconds)
{
    auto audio = lynx::renderAudition (channel, keys, 0.32, 0.9, song.bpm, transport.sampleRate.load(), rackFactories());
    if (seconds != nullptr)
        *seconds = audio.sampleRate > 0.0 ? (double) audio.left.size() / audio.sampleRate : 0.0;
    if (audio.left.empty())
        return false;
    auto sample = std::make_shared<SampleData>();
    sample->sampleRate = audio.sampleRate;
    sample->left = std::move (audio.left);
    sample->right = std::move (audio.right);
    previewSample (std::move (sample), false, 1.0f);
    return true;
}

ClipAudio AudioEngine::renderClipAudio (std::size_t clipIndex)
{
    return lynx::renderClipAudio (song, clipIndex, transport.sampleRate.load(), rackFactories());
}

void AudioEngine::publishSong()
{
    // Issue #134: a hosted plugin may run the message loop while it is created (a licence or trial box, COM, a splash
    // window), and what that loop dispatches can ask for a publish while this one is still building its rack: the
    // engine's own timer (after a device change to another sample rate it sees the old rate until this publish ends, so
    // it started a second rebuild of every plugin inside the first one's, and another inside each of those, as deep as
    // the plugins kept running the loop), or an edit. The nested one waits: when this publish is done, one more runs
    // with the song as it then is.
    if (publishing)
    {
        publishAgain = true;
        ++nestedPublishes;
        return;
    }
    publishing = true;
    const juce::ScopeGuard published { [this] {
        publishing = false;
        if (std::exchange (publishAgain, false))
            publishSong();
    } };
    ++songStamp;
    ++publishCount;
    // round 190ea: FL's invariant, at the one place every song state passes (new / opened / imported project, edit, undo,
    // redo, a live session's remote change): at least one pattern and the current one in range. A Song built without a
    // pattern used to crash the docked Piano roll (its listener read the current pattern of an empty list).
    song.keepOnePattern();
    // Online Sessions (round 168a): every entity has its stable id after any edit, import or load
    uids::normalize (song);
    // round 174d A3: the Video Player's clock follows the song's tempo map (FL: the song's real time)
    if (std::any_of (song.channels.begin(), song.channels.end(), [] (const ChannelDef& c) { return c.instrument == InstrumentKind::videoplayer; }))
        video::publishTempo (song);
    // pasted / duplicated channels and effect slots get their own instance (round 168s: instances follow their channel)
    song.uniqueInstances();
    // diagnostics (round 149): with LYNX_TRACE_EDITS set, each publish's time goes to %APPDATA%\LYNX\edits.log
    static const bool traceEdits = std::getenv ("LYNX_TRACE_EDITS") != nullptr;
    const auto publishStart = juce::Time::getMillisecondCounterHiRes();
    const juce::ScopeGuard traceEnd { [&] {
        if (traceEdits)
            platform::settingsFile ("edits.log").appendText (juce::String (juce::Time::getMillisecondCounterHiRes() - publishStart, 1) + " ms publish\n");
    } };
    // the Master's hardware output target, read by the device callback
    masterOutChannel.store (song.mixer.empty() ? outputDefault : song.mixer[0].outputChannel, std::memory_order_relaxed);
    masterOutMode.store (song.mixer.empty() ? 0 : (int) song.mixer[0].outputMode, std::memory_order_relaxed);
    selectedChannel = juce::jlimit (0, juce::jmax (0, (int) song.channels.size() - 1), selectedChannel);
    {
        const auto count = (int) song.channels.size();
        selectedChannels.erase (std::remove_if (selectedChannels.begin(), selectedChannels.end(), [count] (int c) { return c >= count; }),
                                selectedChannels.end());
        if (std::find (selectedChannels.begin(), selectedChannels.end(), selectedChannel) == selectedChannels.end())
            selectedChannels.insert (std::lower_bound (selectedChannels.begin(), selectedChannels.end(), selectedChannel), selectedChannel);
    }
    publishSelection();
    {
        // the channels locked to a MIDI input ("Receive notes from"): built here, swapped in under the lock
        std::vector<NoteLock> locks;
        for (std::size_t c = 0; c < song.channels.size(); ++c)
            if (! song.channels[c].receiveFrom.empty())
                locks.push_back ({ song.channels[c].receiveFrom, song.channels[c].receiveMidiChannel, (int) c });
        // round 190ao: the hosted instruments with their own velocity setting
        std::vector<std::uint16_t> velocities;
        for (std::size_t c = 0; c < song.channels.size(); ++c)
            if (song.channels[c].instrument == InstrumentKind::plugin && song.channels[c].plugin.velocityMode != prefs::pluginVelocityFollow)
            {
                velocities.resize (c + 1, 0);
                velocities[c] = (std::uint16_t) (prefs::clampPluginVelocityMode (song.channels[c].plugin.velocityMode)
                                                 | (prefs::clampFixedVelocity (song.channels[c].plugin.fixedVelocity) << 8));
            }
        const juce::SpinLock::ScopedLockType lock (midiPrefsLock);
        noteLocks.swap (locks);
        channelVelocity.swap (velocities);
    }
    transport.bpm.store (song.bpm);
    transport.beatsPerBar.store (song.beatsPerBar);

    {
        // time-signature table for the metronome: the start (beatsPerBar/4) and each signature change
        transport.signatureVersion.fetch_add (1, std::memory_order_acq_rel); // odd: writing
        int count = 0;
        transport.signatureTick[0].store (0, std::memory_order_relaxed);
        transport.signatureLength[0].store ((std::int64_t) ppq * juce::jmax (1, song.beatsPerBar), std::memory_order_relaxed);
        ++count;

        for (const auto& m : song.markers())
            if (m.isSignature() && count < Transport::maxSignatures)
            {
                const auto length = juce::jmax<std::int64_t> (1, (std::int64_t) ppq * 4 * m.numerator / juce::jmax (1, m.denominator));
                const auto slot = m.tick <= 0 ? 0 : count++; // at position 0 it replaces the default
                transport.signatureTick[(std::size_t) slot].store (juce::jmax<std::int64_t> (0, m.tick), std::memory_order_relaxed);
                transport.signatureLength[(std::size_t) slot].store (length, std::memory_order_relaxed);
            }

        transport.signatureCount.store (count, std::memory_order_relaxed);
        transport.signatureVersion.fetch_add (1, std::memory_order_acq_rel); // even: ready
    }
    transport.songMode.store (song.mode == PlayMode::song);
    transport.loopStart.store (song.loopPlays() ? (double) song.loopStart : 0.0); // (round 190cg: the loop switch off = the range stays in the Song, playback runs through)
    transport.loopEnd.store (song.loopPlays() ? (double) song.loopEnd : 0.0);
    transport.songEnd.store ((double) song.songLength());
    transport.publishMarkers (makeMarkerPlan (song)); // FL's marker actions (loop, skip, pause, start...)
    transport.songStart.store ((double) song.performanceZoneEnd());

    const loadtrace::Scope buildTiming ("rack_build");
    auto factories = rackFactories();
    // the song's own rack (not the offline clip render, which shares rackFactories()) tells the UI a plugin is being created
    factories.pluginInstruments = [this] (const PluginRef& ref, double sr) {
        if (duringPluginCreation)
            duringPluginCreation();
        return plugins.makeInstrument (ref, sr);
    };
    factories.pluginEffects = [this] (const PluginRef& ref, double sr) {
        if (duringPluginCreation)
            duringPluginCreation();
        return plugins.makeEffect (ref, sr);
    };
    if (rebuildPlugins) // round 190ap: a plugin switched back on (switchPluginOn)
        factories.rebuildPlugin = [this] (const PluginRef& ref) { return rebuildPluginId.empty() || ref.id == rebuildPluginId; };

    const auto buildStart = juce::Time::getMillisecondCounterHiRes();
    const auto tracePhases = traceEdits && ! loadtrace::active(); // the build's phases (an open traces its own)
    if (tracePhases)
        loadtrace::begin ("edit");
    // the parts whose third-party plugin is missing here play their stand-in renders (core/StandIns.h): the audio
    // engine gets that projection of the song, the UI keeps the song itself
    // (no copy of the song for the almost every project that has none)
    std::optional<Song> projectedSong;
    standins::Projected projected;
    // Freeze (round 190ay): a frozen channel whose audio is not on disk (the project moved without it, a freeze brought
    // back by Undo after its files went) plays its instrument: out of date beats silent
    std::optional<Song> thawedSong;
    frozenMissing.clear();
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        if (const auto& f = song.channels[i].freeze; f.on())
        {
            const auto* whole = standins::find (f.stems, 0, false);
            if (whole != nullptr && juce::File (juce::String::fromUTF8 (whole->path.c_str())).existsAsFile())
                continue;
            if (! thawedSong)
                thawedSong = song;
            thawedSong->channels[i].freeze = {};
            frozenMissing.push_back ((int) i);
        }
    const Song& unprojected = thawedSong ? *thawedSong : song;
    if (standins::hasAny (unprojected))
        projectedSong = standins::project (unprojected, [this] (const PluginRef& ref) { return ! plugins.isInstalled (ref); }, &projected);
    const Song& played = projectedSong ? *projectedSong : unprojected;
    {
        // round 190ap: the slots whose plugin is switched off ("Open without third-party plugins"), for their badges
        const auto hold = pluginsOff() || pluginHoldOwner.load() == nullptr ? pluginHoldState() : pluginhold::State();
        channelsOff = pluginhold::channelsOff (song, hold);
        insertsOff = pluginhold::insertsOff (song, hold);
    }
    standInChannels = std::move (projected.channels);
    standInInserts = std::move (projected.inserts);
    if (played.clipGrid)
    {
        if (clipLauncher == nullptr)
            clipLauncher = std::make_unique<launcher::Runtime>();
        factories.clipLauncher = clipLauncher.get();
    }
    // "Low latency while recording" (round 190aq): the live inserts' paths, for this snapshot only (not the song)
    publishedLowLatency = lowLatencyTracksNow();
    factories.lowLatencyTracks = publishedLowLatency;
    auto state = RackState::build (played, &lastPublishedShape, transport.sampleRate.load(), factories);
    if (traceEdits)
        platform::settingsFile ("edits.log").appendText (juce::String (juce::Time::getMillisecondCounterHiRes() - buildStart, 1) + " ms rack build\n"
                                                         + (tracePhases ? juce::String (loadtrace::reportLines()) : juce::String()));
    if (tracePhases)
        loadtrace::end();

    // what the next build needs to reuse instruments and effects
    lastPublishedShape.song.channels = played.channels;
    lastPublishedShape.song.mixer = played.mixer;
    // the tempo too (round 149): a channel that plays a time-stretched copy is kept when its stretch and the tempo
    // did not change; without it every edit rebuilt them (sample reloaded and stretch looked up, ~0.5 s on Thief)
    lastPublishedShape.song.bpm = song.bpm;
    lastPublishedShape.instruments = state->instruments;
    lastPublishedShape.serial = state->serial; // (round 168s: the renderer moves held notes only along this chain)
    lastPublishedShape.effects = state->effects;
    lastPublishedShape.sampleRate = state->sampleRate;
    lastPublishedShape.latencies = state->latencies;
    lastPublishedShape.lowLatencySlots = state->lowLatencySlots; // (currentLatencies() reads them)
    lastPublishedShape.lowLatencySaved = state->lowLatencySaved;
    lastPublishedShape.pdc = state->pdc;
    lastPublishedShape.channelDelays = state->channelDelays;
    lastPublishedShape.sendDelays = state->sendDelays;

    const auto mark = [&] (const char* what) {
        if (traceEdits)
            platform::settingsFile ("edits.log").appendText (juce::String (juce::Time::getMillisecondCounterHiRes() - publishStart, 1) + " ms " + what + "\n");
    };
    mark ("built");
    levelsPending = false;
    publishedLevels = projectedSong ? nullptr : state->live; // (stand-ins: their projection, edits rebuild)
    rackExchange.publish (std::move (state));
    mark ("exchanged");

    if (prefetcher != nullptr)
        prefetcher->update (played, samples);
    mark ("prefetch updated");

    sendChangeMessage();
}

std::vector<MidiLink> AudioEngine::getGlobalLinks() const
{
    std::vector<MidiLink> out;
    for (const auto& l : midiPrefs.globalLinks)
    {
        MidiLink link { l.cc, l.channel, { (AutomationTargetKind) l.kind, l.index, l.slot, l.param } };
        link.port = l.port;
        link.formula = l.formula;
        link.smooth = l.smooth;
        link.smoothing = l.smoothing;
        link.mode = l.mode;
        out.push_back (std::move (link));
    }
    return out;
}

std::optional<MidiLink> AudioEngine::globalLinkOf (const AutomationTarget& target) const
{
    for (const auto& l : getGlobalLinks())
        if (l.target == target)
            return l;
    return std::nullopt;
}

void AudioEngine::learnGlobalLink (const AutomationTarget& target)
{
    learning = target;
    learningGlobal = true;
    sendChangeMessage();
}

void AudioEngine::removeGlobalLink (const AutomationTarget& target)
{
    auto p = midiPrefs;
    p.globalLinks.erase (std::remove_if (p.globalLinks.begin(), p.globalLinks.end(),
                                         [&] (const prefs::GlobalLink& l) {
                                             return l.kind == (int) target.kind && l.index == target.index && l.slot == target.slot
                                                 && l.param == target.param;
                                         }),
                         p.globalLinks.end());
    setMidiPrefs (p, saveGlobalLinks);
}

void AudioEngine::setTargetLinks (const AutomationTarget& target, std::vector<MidiLink> links, bool removeConflicts)
{
    editSong ([&] (Song& s) { lynx::setTargetLinks (s.midiLinks, target, std::move (links), removeConflicts); });
    linkSmoothing.clear();
    sendChangeMessage();
}

void AudioEngine::setGlobalLink (const AutomationTarget& target, const std::optional<MidiLink>& link)
{
    auto p = midiPrefs;
    p.globalLinks.erase (std::remove_if (p.globalLinks.begin(), p.globalLinks.end(),
                                         [&] (const prefs::GlobalLink& l) {
                                             return l.kind == (int) target.kind && l.index == target.index && l.slot == target.slot
                                                 && l.param == target.param;
                                         }),
                         p.globalLinks.end());
    if (link && link->cc >= 0)
    {
        prefs::GlobalLink g { link->cc, link->channel, (int) target.kind, target.index, target.slot, target.param };
        g.port = link->port;
        g.formula = link->formula;
        g.smooth = link->smooth;
        g.smoothing = link->smoothing;
        g.mode = link->mode;
        p.globalLinks.push_back (std::move (g));
    }
    setMidiPrefs (p, saveGlobalLinks);
    linkSmoothing.clear();
    sendChangeMessage();
}

void AudioEngine::setMapMode (remotemap::Mode mode)
{
    mapMode = mode;
    mapCaptured.reset();
    mapCaptureNotes.store (mode == remotemap::Mode::midi, std::memory_order_relaxed);
}

void AudioEngine::injectControllerForTest (int cc, int channel, int value, int port)
{
    controllerQueue.push ({ cc, channel, value, port });
    applyControllers();
}

// The smoothing filters of the links being moved (FL's per-link "Smoothing"): each one walks towards the position its
// controller last gave, at the timer's pace, until it has arrived.
void AudioEngine::stepLinkSmoothing (std::vector<std::pair<AutomationTarget, float>>& changes)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = linkSmoothingClock > 0.0 ? juce::jlimit (0.0, 250.0, now - linkSmoothingClock) : 0.0;
    linkSmoothingClock = now;
    for (auto& s : linkSmoothing)
    {
        const auto v = s.filter.step (s.target, elapsed, s.link.smoothing);
        const auto mixerTrack = selectedMixerTrackForLinks ? selectedMixerTrackForLinks() : -1;
        changes.push_back ({ linkOmniTarget (s.link, selectedChannel, mixerTrack), v });
    }
    linkSmoothing.erase (std::remove_if (linkSmoothing.begin(), linkSmoothing.end(), [] (const LinkSmoothing& s) { return s.filter.settled (s.target); }),
                         linkSmoothing.end());
    if (linkSmoothing.empty())
        linkSmoothingClock = 0.0;
}

void AudioEngine::setMultilink (bool on)
{
    if (on)
        multilink.emplace();
    else
        multilink.reset();
    sendChangeMessage();
}

void AudioEngine::multilinkToControllers (bool global)
{
    if (! multilink)
        return;
    multilink->collecting = false;
    multilink->global = global;
    sendChangeMessage();
}

void AudioEngine::learnMidiController (const AutomationTarget& target)
{
    learning = target;
    learningGlobal = false;
    sendChangeMessage();
}

// --- last tweaked parameters and automation recording (REA round 143) --------------------------------------

void AudioEngine::noteTweaked (const AutomationTarget& target, bool held)
{
    if (target.kind == AutomationTargetKind::none)
        return;

    if (multilink) // round 177b-4: FL's Multilink queues the moved controls in order
        multilink->add (target);

    // FL [EXEC round 185b, issue #125]: a moved control leaves its automation until playback starts again (stopped: the
    // value stays; playing: the clip no longer drives it). While recording automation the touch rule applies instead.
    const auto recordingAutomation = transport.recording.load() && transport.playing.load() && recordOptions.recordAutomation;
    if (! recordingAutomation && std::find (releasedTargets.begin(), releasedTargets.end(), target) == releasedTargets.end())
    {
        releasedTargets.push_back (target);
        publishReleasedTargets();
    }

    if (! (lastTweaked[0] == target))
    {
        lastTweaked[1] = lastTweaked[0];
        lastTweaked[0] = target;
    }

    const auto now = juce::Time::getMillisecondCounterHiRes();
    auto it = std::find_if (touches.begin(), touches.end(), [&] (const Touch& t) { return t.target == target; });
    if (it == touches.end())
    {
        touches.push_back ({ target, now, held, {} });
        return;
    }
    it->lastMs = now;
    it->held = it->held || held;
}

void AudioEngine::publishReleasedTargets()
{
    if (rackProcessor == nullptr)
        return;
    std::vector<int> slots, one;
    for (const auto& t : releasedTargets)
    {
        RackState::slotsOfTarget (song, t, one);
        for (const auto a : one) // (a link to an internal controller keeps driving it)
            if (a >= (int) song.automations.size() || ! song.automations[(std::size_t) a].isLive())
                slots.push_back (a);
    }
    rackProcessor->getRenderer().setReleasedSlots (slots);
}

void AudioEngine::tweakReleased (const AutomationTarget& target)
{
    for (auto& t : touches)
        if (t.target == target)
        {
            t.held = false;
            t.lastMs = juce::Time::getMillisecondCounterHiRes();
        }
}

std::optional<AutomationTarget> AudioEngine::getLastTweaked (int which) const
{
    if (which < 0 || which > 1 || lastTweaked[(std::size_t) which].kind == AutomationTargetKind::none
        || currentTargetValue (lastTweaked[(std::size_t) which]) < 0.0f)
        return std::nullopt;
    return lastTweaked[(std::size_t) which];
}

float AudioEngine::currentTargetValue (const AutomationTarget& t) const
{
    // a controller's pitch wheel being recorded (any instrument: LYNX's own have no such parameter)
    if (t.kind == AutomationTargetKind::channelParam && midictl::isPitchBend (t.param))
        if (const auto it = liveBends.find (t.index); it != liveBends.end())
            return it->second;
    const auto base = song.targetValue (t);
    if (base < 0.0f || (t.kind != AutomationTargetKind::channelParam && t.kind != AutomationTargetKind::effectParam))
        return base;

    juce::AudioPluginInstance* plugin = nullptr;
    float midiValue = -1.0f;
    if (t.kind == AutomationTargetKind::channelParam)
        if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument (t.index).get()))
        {
            plugin = &p->getInstance();
            midiValue = midictl::isMidiParam (t.param) ? p->midiControlValue (t.param) : -1.0f;
        }
    if (t.kind == AutomationTargetKind::effectParam)
        if (auto* p = dynamic_cast<PluginEffect*> (getEffect (t.index, t.slot).get()))
        {
            plugin = &p->getInstance();
            midiValue = midictl::isMidiParam (t.param) ? p->midiControlValue (t.param) : -1.0f;
        }

    if (midiValue >= 0.0f)
        return midiValue;
    if (plugin != nullptr && t.param >= 0 && t.param < plugin->getParameters().size())
        return plugin->getParameters()[t.param]->getValue();
    return base;
}

std::optional<AutomationTarget> AudioEngine::targetOfPlugin (const juce::AudioPluginInstance& plugin, int param) const
{
    for (int c = 0; c < (int) song.channels.size(); ++c)
        if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument (c).get()); p != nullptr && &p->getInstance() == &plugin)
            return AutomationTarget { AutomationTargetKind::channelParam, c, 0, param };

    for (int t = 0; t < (int) song.mixer.size(); ++t)
        for (int s = 0; s < (int) song.mixer[(std::size_t) t].effects.size(); ++s)
            if (auto* p = dynamic_cast<PluginEffect*> (getEffect (t, s).get()); p != nullptr && &p->getInstance() == &plugin)
                return AutomationTarget { AutomationTargetKind::effectParam, t, s, param };

    return std::nullopt;
}

void AudioEngine::takeRecordedBends()
{
    RecordedEvent e;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    while (bendRecordQueue.pop (e))
    {
        if (! recordOptions.recordAutomation || e.channel < 0 || e.channel >= (int) song.channels.size())
            continue;
        const AutomationTarget target { AutomationTargetKind::channelParam, e.channel, 0, midictl::pitchBendBase + e.key };
        liveBends[e.channel] = e.velocity;
        // the wheel's own moves, at the times they were played (not only the UI's sampling)
        auto it = std::find_if (touches.begin(), touches.end(), [&] (const Touch& t) { return t.target == target; });
        if (it == touches.end())
        {
            touches.push_back ({ target, now, false, {} });
            it = touches.end() - 1;
        }
        it->lastMs = now;
        it->pending.push_back ({ e.tick, e.velocity });
    }
    if (! transport.recording.load() || ! transport.playing.load())
        liveBends.clear();
}

void AudioEngine::updateAutomationRecording (bool active)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto recording = active && recordOptions.recordAutomation;
    const auto grid = recordOptions.quantiseAutomation ? recordOptions.quantise : std::int64_t { 0 };

    auto writeFor = [this] (const AutomationTarget& target) -> AutomationWrite& {
        auto it = std::find_if (takeWrites.begin(), takeWrites.end(), [&] (const AutomationWrite& w) { return w.target == target; });
        if (it != takeWrites.end())
            return *it;
        AutomationWrite w;
        w.target = target;
        w.name = nameAutomation ? nameAutomation (target) : std::string ("Automation");
        takeWrites.push_back (w);
        return takeWrites.back();
    };

    // writes what was sampled (one undo step for the whole take); `ended` = the gestures that are over
    auto commit = [&] (const std::vector<AutomationTarget>& ended) {
        if (std::none_of (touches.begin(), touches.end(), [] (const Touch& t) { return ! t.pending.empty(); }))
            return;
        const auto songMode = song.mode == PlayMode::song;
        editSong ([&] (Song& s) {
            for (auto& t : touches)
                if (! t.pending.empty())
                {
                    auto& w = writeFor (t.target);
                    recordedAutomationPoints += (int) t.pending.size();
                    if (songMode)
                        commitAutomationPoints (s, w, std::move (t.pending), grid, automationLinear || noteTakeLinear);
                    else
                        commitEventPoints (s, automationPattern, w, std::move (t.pending), automationLoopLength, grid);
                    t.pending.clear();
                }
        }, ! automationTakeHasUndoStep);
        automationTakeHasUndoStep = true;
        for (const auto& target : ended)
            writeFor (target).lastLocal = -1; // the next gesture on it starts a new stretch
    };

    // FL: while recording, the moved control wins over its playing automation at once (REA round 146); the
    // automation of a touched target does not drive it until the gesture ends and its points are written.
    auto overrideTouched = [this] {
        if (rackProcessor == nullptr)
            return;
        std::vector<int> slots, one;
        for (const auto& t : touches)
        {
            RackState::slotsOfTarget (song, t.target, one);
            slots.insert (slots.end(), one.begin(), one.end());
        }
        if (slots.empty() && ! automationOverrideActive)
            return;
        rackProcessor->getRenderer().setOverriddenSlots (slots);
        automationOverrideActive = ! slots.empty();
    };

    // the take's own clips reach the playhead (FL: the clip keeps growing until recording stops, holding the last value)
    auto growClips = [this] (std::int64_t songTick, bool finished) {
        if (song.mode != PlayMode::song || songTick < 0)
            return;
        const auto needed = std::any_of (takeWrites.begin(), takeWrites.end(), [&] (const AutomationWrite& w) {
            if (! w.createdByTake || w.automation < 0)
                return false;
            for (const auto& c : song.playlist)
                if (c.kind == ClipKind::automation && c.automation == w.automation)
                    return finished || c.end() < songTick;
            return false;
        });
        if (! needed)
            return;
        editSong ([&] (Song& s) {
            for (const auto& w : takeWrites)
                growTakeClip (s, w, songTick, finished);
        }, ! automationTakeHasUndoStep);
        automationTakeHasUndoStep = true;
    };

    if (! recording)
    {
        commit ({});
        touches.clear();
        overrideTouched();
        if (! active)
        {
            growClips (lastAutomationTick, true);
            takeWrites.clear();
            automationTakeHasUndoStep = false;
            automationLoopLength = 0;
            lastAutomationTick = -1;
            if (std::exchange (automationLinear, false))
                updateLinearSong();
        }
        return;
    }

    if (automationLoopLength <= 0)
    {
        automationPattern = song.currentPattern;
        automationLoopLength = song.patterns.empty() ? ppq * 4
                                                     : song.patterns[(std::size_t) juce::jlimit (0, (int) song.patterns.size() - 1, song.currentPattern)]
                                                           .effectiveLength (song.beatsPerBar);
    }

    const auto tick = estimatedPositionNow();

    // FL's "Record automation" of an internal-controller link (round 190cs): the controller's output, as the link maps
    // it, is written like a hand move for as long as automation is recorded
    for (const auto& a : song.automations)
    {
        if (! a.isLive() || ! a.recordLive)
            continue;
        auto output = -1.0f;
        if (a.source.channel >= 0)
        {
            if (const auto instrument = getChannelInstrument (a.source.channel))
                output = instrument->controlOutput (a.source.output);
        }
        else if (const auto effect = getEffect (a.source.track, a.source.slot))
            output = effect->controlOutput (a.source.output);
        if (output < 0.0f || std::isnan (output))
            continue;
        auto it = std::find_if (touches.begin(), touches.end(), [&] (const Touch& t) { return t.target == a.target; });
        if (it == touches.end())
        {
            touches.push_back ({ a.target, now, false, {} });
            it = touches.end() - 1;
        }
        it->lastMs = now;
        it->liveValue = juce::jlimit (0.0f, 1.0f, a.mapLive (juce::jlimit (0.0f, 1.0f, output)));
    }

    std::vector<AutomationTarget> ended;
    for (auto& t : touches)
    {
        const auto alive = t.held || now - t.lastMs < 350.0;
        const auto value = t.liveValue >= 0.0f ? t.liveValue : currentTargetValue (t.target);
        if (value < 0.0f)
        {
            ended.push_back (t.target);
            continue;
        }
        if (alive)
            t.pending.push_back ({ tick, value });
        else
            ended.push_back (t.target);
    }

    // song mode without a loop region: from the first move on, the song plays on past its end until recording stops
    if (! takeWrites.empty() || ! touches.empty())
        if (! automationLinear && song.mode == PlayMode::song && ! song.loopPlays())
        {
            automationLinear = true;
            updateLinearSong();
        }
    if (song.mode == PlayMode::song)
        lastAutomationTick = songTickForRecorded (song, tick, automationLinear || noteTakeLinear);

    if (! ended.empty() || --automationCommitCountdown <= 0)
    {
        growClips (lastAutomationTick, false);
        commit (ended);
        automationCommitCountdown = 6; // about 5 writes per second while a control keeps moving
    }

    touches.erase (std::remove_if (touches.begin(), touches.end(),
                                   [&] (const Touch& t) { return std::find (ended.begin(), ended.end(), t.target) != ended.end(); }),
                   touches.end());
    overrideTouched();
}

void AudioEngine::applyControllers()
{
    ControllerEvent e;
    // this frame's events in their order (round 190cs: a key or button link in Latch / Inc / Stay mode acts on each
    // press, so presses are not merged); a plain controller still moves its control once, to its last value
    std::vector<ControllerEvent> events;
    std::map<std::tuple<int, int, int>, std::size_t> last; // (number, channel, port) -> its last event this frame
    controllerLearnTick = learning.kind != AutomationTargetKind::none || multilink.has_value();

    while (controllerQueue.pop (e))
    {
        // Auto detect of the Remote control settings window: controllers, and keys as they are pressed
        if (! isNoteLink (e.cc) || e.value > 0)
            controllerSeen = { e.port, e.channel, e.cc, e.value, controllerSeen.serial + 1 };
        // round 190ef: in MIDI Map Mode a controller moved or a key pressed is caught for the selected control; nothing
        // acts ("MIDI-mappings won't function while you're in MIDI Map Mode", the reference's lesson)
        if (mapMode == remotemap::Mode::midi)
        {
            if (! isNoteLink (e.cc) || e.value > 0)
                mapCaptured = remotemap::MidiControl { e.channel, e.cc, e.port };
            continue;
        }
        if (e.seenOnly)
            continue;
        if (isNoteLink (e.cc)) // (MIDI learn and Multilink take controllers, as before)
        {
            last[{ e.cc, e.channel, e.port }] = events.size();
            events.push_back (e);
            continue;
        }
        if (learning.kind != AutomationTargetKind::none && learningGlobal)
        {
            // "Override global link...": the link goes to the settings, for every project (REA round 146)
            auto p = midiPrefs;
            prefs::GlobalLink g { e.cc, e.channel, (int) learning.kind, learning.index, learning.slot, learning.param };
            g.port = e.port; // (round 190bt: the device's port number, as FL's Auto detect fills it)
            p.globalLinks.erase (std::remove_if (p.globalLinks.begin(), p.globalLinks.end(),
                                                 [&] (const prefs::GlobalLink& l) {
                                                     return l.kind == g.kind && l.index == g.index && l.slot == g.slot && l.param == g.param;
                                                 }),
                                 p.globalLinks.end());
            p.globalLinks.push_back (g);
            learning = {};
            learningGlobal = false;
            setMidiPrefs (p, saveGlobalLinks);
        }
        else if (learning.kind == AutomationTargetKind::none && multilink)
        {
            // FL's Multilink (round 177b-4): the first message of each controller links the next queued control
            if (const auto link = multilink->controller (e.cc, e.channel))
            {
                if (multilink->global) // "Override global links": as "Override global link...", for every project
                {
                    auto p = midiPrefs;
                    prefs::GlobalLink g { link->cc, link->channel, (int) link->target.kind, link->target.index, link->target.slot,
                                          link->target.param };
                    g.port = e.port;
                    p.globalLinks.erase (std::remove_if (p.globalLinks.begin(), p.globalLinks.end(),
                                                         [&] (const prefs::GlobalLink& l) {
                                                             return l.kind == g.kind && l.index == g.index && l.slot == g.slot && l.param == g.param;
                                                         }),
                                         p.globalLinks.end());
                    p.globalLinks.push_back (g);
                    setMidiPrefs (p, saveGlobalLinks);
                }
                else
                    editSong ([l = *link, port = e.port] (Song& s) {
                        s.midiLinks.erase (std::remove_if (s.midiLinks.begin(), s.midiLinks.end(), [&] (const MidiLink& m) { return m.target == l.target; }),
                                           s.midiLinks.end());
                        MidiLink made { l.cc, l.channel, l.target };
                        made.port = port;
                        s.midiLinks.push_back (std::move (made));
                    });
                if (multilink->complete())
                    multilink.reset(); // FL: "the process will auto-close once the number of hardware controls tweaked
                                       // equals the number of software targets"
                sendChangeMessage();
            }
        }
        else if (learning.kind != AutomationTargetKind::none)
        {
            // MIDI learn: replace the old link for the same target with the new one
            editSong ([t = learning, e] (Song& s) {
                s.midiLinks.erase (std::remove_if (s.midiLinks.begin(), s.midiLinks.end(), [&] (const MidiLink& l) { return l.target == t; }),
                                   s.midiLinks.end());
                MidiLink made { e.cc, e.channel, t };
                made.port = e.port;
                s.midiLinks.push_back (std::move (made));
            });
            learning = {};
        }

        last[{ e.cc, e.channel, e.port }] = events.size();
        events.push_back (e);
    }

    const auto links = linksWithGlobal (song.midiLinks, getGlobalLinks());
    {
        std::uint64_t bits[2] {}, notes[2] {};
        for (const auto& link : links)
            if (link.cc >= 0 && link.cc < 128)
                bits[link.cc >> 6] |= std::uint64_t { 1 } << (link.cc & 63);
            else if (isNoteLink (link.cc))
                notes[(link.cc - linkNoteBase) >> 6] |= std::uint64_t { 1 } << ((link.cc - linkNoteBase) & 63);
        remotemap::usedNumbers (song, bits, notes); // (round 190ef: the MIDI mappings of slots, scenes, transport)
        for (int w = 0; w < 2; ++w)
        {
            linkedCcs[(std::size_t) w].store (bits[w], std::memory_order_relaxed);
            linkedNotes[(std::size_t) w].store (notes[w], std::memory_order_relaxed);
        }
    }
    if (mapMode == remotemap::Mode::midi)
        return;

    // round 190ef: the MIDI mappings of clip slots, scenes, track stop buttons and transport switches (presses and releases)
    if (! song.remoteMappings.empty())
        for (const auto& ev : events)
        {
            auto& previous = remoteValues.try_emplace ({ ev.cc, ev.channel, ev.port }, -1).first->second;
            const auto fires = remotemap::midiFires (song, ev.channel, ev.cc, ev.value, previous, ev.port);
            previous = ev.value;
            if (onRemoteFire != nullptr)
                for (const auto& f : fires)
                    onRemoteFire (f);
        }

    if ((events.empty() || links.empty()) && linkSmoothing.empty())
        return;

    // one snapshot per frame, outside the undo history (like turning the knob)
    std::vector<std::pair<AutomationTarget, float>> changes;
    const auto mixerTrack = selectedMixerTrackForLinks ? selectedMixerTrackForLinks() : -1;

    std::map<std::tuple<int, int, int, int>, float> moved; // a control's position after this frame's events so far

    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const auto& ev = events[i];
        const auto lastOfKey = last[{ ev.cc, ev.channel, ev.port }] == i;
        for (const auto& link : links)
        {
            // (round 190bt: a link follows the devices with exactly its port number, as FL's controller id does)
            if (link.cc != ev.cc || ! (link.channel < 0 || link.channel == ev.channel) || link.port != ev.port)
                continue;
            const auto absolute = linkformula::linkModeIsAbsolute (link.mode);
            if (absolute && ! lastOfKey)
                continue; // (a controller's earlier values this frame: only the last one moves the control)
            static const linkformula::Formula oneToOne;
            const auto* formula = &oneToOne;
            if (! link.formula.empty()) // FL's mapping formula: Input = the controller's position, the result limited to 0..1
            {
                auto found = linkFormulas.find (link.formula);
                if (found == linkFormulas.end())
                {
                    if (linkFormulas.size() > 256)
                        linkFormulas.clear();
                    found = linkFormulas.emplace (link.formula, linkformula::Formula::compile (link.formula)).first;
                }
                formula = &found->second;
            }
            const auto t = linkOmniTarget (link, selectedChannel, mixerTrack); // "Omni": the selected channel / track
            const std::tuple<int, int, int, int> id { (int) t.kind, t.index, t.slot, t.param };
            // FL's Mode (round 190cs): Latch and Inc start from where the control is now
            auto current = 0.0f;
            if (! absolute)
            {
                if (const auto m = moved.find (id); m != moved.end())
                    current = m->second;
                else if (const auto now = currentTargetValue (t); now >= 0.0f)
                    current = now;
            }
            const auto mapped = linkformula::linkModeValue (link.mode, *formula, (float) ev.value / 127.0f, current);
            if (! mapped)
                continue; // (a release in Latch / Inc / Stay)
            // round 190ef: the mapping's Min / Max (the reference's range; 0 / 1 keeps the position as it was)
            const auto v = absolute ? remotemap::applyRange (*mapped, link.min, link.max) : *mapped;
            // takeover: only parameters the song holds (a plugin's own parameters have no value to compare here), and
            // only for links that follow the controller's position
            const auto songHeld = t.kind != AutomationTargetKind::channelParam && t.kind != AutomationTargetKind::effectParam;
            if (midiPrefs.pickup && songHeld && absolute)
            {
                auto& state = pickups[{ ev.cc, ev.channel, (int) t.kind, t.index, t.slot, t.param }];
                if (! prefs::pickupUpdate (state, song.targetValue (t), v))
                    continue;
            }
            moved[id] = v;
            noteTweaked (t); // a linked controller's move is a tweak (Last tweaked, automation recording)
            if (link.smooth && link.smoothing != 0)
            {
                auto s = std::find_if (linkSmoothing.begin(), linkSmoothing.end(), [&] (const LinkSmoothing& x) { return x.link == link; });
                if (s == linkSmoothing.end())
                {
                    // starts from where the control is (a control the song does not hold starts at the new position)
                    LinkSmoothing fresh { link, v, {} };
                    if (const auto now = currentTargetValue (t); now >= 0.0f)
                        fresh.filter.step (now, 0.0, link.smoothing);
                    linkSmoothing.push_back (std::move (fresh));
                }
                else
                    s->target = v;
                continue;
            }
            changes.push_back ({ t, v });
        }
    }

    stepLinkSmoothing (changes);
    applyTargetValues (changes);
}

void AudioEngine::applyTargetValues (const std::vector<std::pair<AutomationTarget, float>>& changes)
{
    std::vector<std::pair<AutomationTarget, float>> pluginChanges;

    editSong ([&] (Song& s) {
        for (const auto& [target, v] : changes)
            if (! s.setTargetValue (target, v))
                pluginChanges.push_back ({ target, v });
    }, false);

    // plugin parameters live in the plugin
    for (const auto& [target, v] : pluginChanges)
    {
        if (target.kind == AutomationTargetKind::channelParam)
        {
            if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument (target.index).get()))
                p->setParameterFromUi (target.param, v);
            // a MIDI Out's controls: the plugins on its port (REA_rodada133 §4)
            if (target.index >= 0 && target.index < (int) song.channels.size()
                && song.channels[(std::size_t) target.index].instrument == InstrumentKind::midiOut)
            {
                const auto port = midictl::parseMidiOut (song.channels[(std::size_t) target.index].pluginState).port;
                for (int c = 0; c < (int) song.channels.size(); ++c)
                    if (song.channels[(std::size_t) c].instrument == InstrumentKind::plugin && song.channels[(std::size_t) c].plugin.midiInPort == port)
                        if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument (c).get()))
                            p->setParameterFromUi (target.param, v);
            }
        }

        if (target.kind == AutomationTargetKind::effectParam)
            if (auto* p = dynamic_cast<PluginEffect*> (getEffect (target.index, target.slot).get()))
                p->setParameterFromUi (target.param, v);
    }
}

void AudioEngine::timerCallback()
{
    // MIDI inputs: open the enabled devices that appeared (or were not ready) and reopen the ones that came back
    if (midiOpen && ++midiCheckTicks >= 60) // every 2 s
    {
        midiCheckTicks = 0;
        enableAllMidiInputs();
    }

    updateRealtimeStats();
    if (++hwTicks >= 5) // (6 times a second)
    {
        hwTicks = 0;
        pumpEdisonRecordings(); // (round 190cd)
        applyHardwareInsertPings();
    }
    rt::promotePending(); // Linux: real-time priority for the audio threads seen since the last tick (round 161)
    updateAudition(); // (round 190ao: the preset audition's note starts and ends here)
    applyControllers();
    controllerIo.pump (controllerLearnTick); // (controller profiles, round 190aw)
    // playback started: the moved controls follow their automation again (the renderer cleared its own mask)
    if (const auto playing = transport.playing.load(); playing != std::exchange (releasedWasPlaying, playing) && playing)
        releasedTargets.clear();
    else if (! releasedTargets.empty())
        publishReleasedTargets(); // (the slots follow the song: an automation added or removed renumbers them)
    if (const auto note = pendingMarkerNote.exchange (-1); note >= 0)
        jumpToMarker (note); // FL's "Song marker jump": note 0 = the first marker
    updateRecording();
    updatePerformanceRecording();
    if (clipLauncher != nullptr)
    {
        updateLauncherRecording();
        updateSlotRecording();
    }
    rackExchange.collectGarbage();
    if (levelsPending && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        flushLevels(); // a level drag ended: the full snapshot catches up (round 171c)

    // Plugins are prepared at a fixed rate; if the device changed rate, recreate them.
    // A plugin whose latency changed (preset change, lookahead) requests a new PDC plan.
    const auto rateChanged = std::abs (transport.sampleRate.load() - lastPublishedShape.sampleRate) > 0.5;
    const auto latencies = lastPublishedShape.currentLatencies();
    if (! publishing && (rateChanged || latencies != lastPublishedShape.latencies)) // (not inside a publish: see publishSong)
    {
        // the plugins made for the new rate start from what the running ones hold now (issue #134: they started from
        // the state of the last save or export, so a device at another rate undid every change made in their windows)
        if (rateChanged)
            capturePluginStates();
        // (diagnostics: which unit's latency moved)
        if (! rateChanged)
            for (std::size_t i = 0; i < latencies.size() && i < lastPublishedShape.latencies.size(); ++i)
                if (latencies[i] != lastPublishedShape.latencies[i])
                {
                    lastLatencyChange = juce::String ((int) i) + ": " + juce::String (lastPublishedShape.latencies[i]) + " -> " + juce::String (latencies[i]);
                    break;
                }
        ++latencyRepublishes;
        publishSong();
    }
}

// Performance mode's recording (FL's "Clips" filter, REA round 146 B5: a launched clip is written to its playlist
// track): every pass of a launched clip that ended while recording becomes a copy of that clip in the arrangement,
// on the same track, where the song position was when the pass began.
void AudioEngine::updatePerformanceRecording()
{
    auto& live = *performanceLive;
    performanceMidiOn.store (song.performanceMode && song.mode == PlayMode::song && song.performanceZoneEnd() > 0, std::memory_order_relaxed);
    const auto on = transport.recording.load() && transport.playing.load() && song.performanceMode && song.mode == PlayMode::song
                    && recordOptions.recordClips;
    if (on && ! live.recording.load (std::memory_order_relaxed))
        perfTakeHasUndoStep = false;
    live.recording.store (on, std::memory_order_relaxed);

    std::vector<Clip> clips;
    perf::RecordedPass pass;
    while (live.recorded.pop (pass))
        if (Clip c; perf::recordedClip (song, pass, c) && c.start >= song.performanceZoneEnd())
            clips.push_back (c);
    if (clips.empty())
        return;
    editSong ([&clips] (Song& s) {
        for (const auto& c : clips)
        {
            // the take replaces what was under it on that track (like FL's recorded clips)
            const auto end = c.end();
            for (auto& o : s.playlist)
                if (o.track == c.track && o.start < end && o.end() > c.start && o.start >= s.performanceZoneEnd())
                {
                    if (o.start >= c.start)
                        o.length = 0; // removed below
                    else
                        o.length = c.start - o.start;
                }
            s.playlist.erase (std::remove_if (s.playlist.begin(), s.playlist.end(), [] (const Clip& o) { return o.length <= 0; }),
                              s.playlist.end());
            s.playlist.push_back (c);
        }
    }, ! perfTakeHasUndoStep);
    perfTakeHasUndoStep = true;
}

// The clip grid's recording (round 181a): while recording, what is launched is written into the Playlist. Every pass that
// ended becomes a copy of its clip on its track, where the song was when the pass began, replacing what was under it.
void AudioEngine::updateLauncherRecording()
{
    auto& runtime = *clipLauncher;
    const auto on = transport.recording.load() && transport.playing.load() && song.clipGrid && song.mode == PlayMode::song && recordOptions.recordClips;
    if (on && ! runtime.recording.load (std::memory_order_relaxed))
        launcherTakeHasUndoStep = false;
    runtime.recording.store (on, std::memory_order_relaxed);

    std::vector<Clip> clips;
    launcher::RecordedPass pass;
    while (runtime.recorded.pop (pass))
        if (Clip c; song.clipGrid && launcher::recordedClip (*song.clipGrid, pass, c))
            clips.push_back (c);
    if (clips.empty())
        return;
    editSong ([&clips] (Song& s) {
        for (const auto& c : clips)
        {
            const auto end = c.end();
            for (auto& o : s.playlist)
                if (o.track == c.track && o.start < end && o.end() > c.start)
                {
                    if (o.start >= c.start)
                        o.length = 0; // removed below
                    else
                        o.length = c.start - o.start;
                }
            s.playlist.erase (std::remove_if (s.playlist.begin(), s.playlist.end(), [] (const Clip& o) { return o.length <= 0; }),
                              s.playlist.end());
            s.playlist.push_back (c);
        }
    }, ! launcherTakeHasUndoStep);
    launcherTakeHasUndoStep = true;
}

void AudioEngine::choosePreferredDriver()
{
   #if JUCE_LINUX || JUCE_BSD
    std::vector<std::pair<std::string, bool>> types;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        types.emplace_back (type->getTypeName().toStdString(), ! type->getDeviceNames (false).isEmpty());
    }
    const auto best = juce::String (prefs::preferredLinuxDriver (types));
    if (best.isNotEmpty() && best != deviceManager.getCurrentAudioDeviceType())
    {
        deviceManager.setCurrentAudioDeviceType (best, true);
        if (deviceManager.getCurrentAudioDevice() == nullptr) // it would not open: back to what worked
            deviceManager.initialise (0, 2, nullptr, true);
    }
   #endif
}

void AudioEngine::chooseSafeBufferSize()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;
    std::vector<int> sizes;
    for (auto b : device->getAvailableBufferSizes())
        sizes.push_back (b);
    const auto current = device->getCurrentBufferSizeSamples();
    const auto target = prefs::safeBufferSize (sizes, current, device->getCurrentSampleRate());
    if (target > 0 && target != current)
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.bufferSize = target;
        deviceManager.setAudioDeviceSetup (setup, true);
    }
}

bool AudioEngine::showDriverPanel (juce::AudioDeviceManager& devices)
{
    auto* device = devices.getCurrentAudioDevice();
    if (device == nullptr || ! device->hasControlPanel())
        return false;
    // JUCE's ASIO device: true when the panel stayed open long enough to have changed something (its own selector
    // then closes and restarts the device the same way)
    if (! device->showControlPanel())
        return false;
    auto setup = devices.getAudioDeviceSetup();
    devices.closeAudioDevice(); // deletes `device`
    device = nullptr;
    // 0 = the new device's default, i.e. the preferred size the driver now reports (ASIO4ALL / FL ASIO: its panel's
    // buffer); a driver that keeps no preferred size gets LYNX's usual default
    setup.bufferSize = 0;
    devices.setAudioDeviceSetup (setup, true);
    return true;
}

namespace
{
// A driver with its own panel, like ASIO4ALL / FL ASIO: the panel sets the preferred buffer size the next device
// object reads. Every live device is tracked, so a call on a deleted one is caught instead of crashing.
struct PanelTestState
{
    std::vector<const void*> alive;
    int preferred = 256;
    int panelResult = 64; // the size the panel picks; 0 = the panel changes nothing (returns false)
    bool calledDeleted = false;
};
PanelTestState& panelTest()
{
    static PanelTestState state;
    return state;
}

struct PanelTestDevice final : juce::AudioIODevice
{
    PanelTestDevice() : juce::AudioIODevice ("Panel test device", "Panel test"), preferred (panelTest().preferred) { panelTest().alive.push_back (this); }
    ~PanelTestDevice() override
    {
        close();
        auto& alive = panelTest().alive;
        alive.erase (std::remove (alive.begin(), alive.end(), (const void*) this), alive.end());
    }
    bool isAlive() const { const auto& a = panelTest().alive; return std::find (a.begin(), a.end(), (const void*) this) != a.end(); }
    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 64, 128, 256, 272, 512 }; }
    int getDefaultBufferSize() override { return preferred; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger& outs, double rate, int size) override
    {
        rateNow = rate;
        sizeNow = size;
        outsNow = outs;
        opened = true;
        return {};
    }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback* cb) override
    {
        if (cb != nullptr)
            cb->audioDeviceAboutToStart (this);
        callback = cb;
    }
    void stop() override
    {
        if (auto* cb = std::exchange (callback, nullptr))
            cb->audioDeviceStopped();
    }
    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return sizeNow; }
    double getCurrentSampleRate() override { return rateNow; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return outsNow; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
    bool hasControlPanel() const override { return true; }
    bool showControlPanel() override
    {
        if (! isAlive())
        {
            panelTest().calledDeleted = true;
            return false;
        }
        if (panelTest().panelResult == 0)
            return false;
        panelTest().preferred = panelTest().panelResult; // what the driver now reports to a new device object
        return true;
    }

    const int preferred;
    double rateNow = 44100.0;
    int sizeNow = 0;
    juce::BigInteger outsNow;
    bool opened = false;
    juce::AudioIODeviceCallback* callback = nullptr;
};

struct PanelTestType final : juce::AudioIODeviceType
{
    PanelTestType() : juce::AudioIODeviceType ("Panel test") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool input) const override { return input ? juce::StringArray() : juce::StringArray ("Panel test device"); }
    int getDefaultDeviceIndex (bool input) const override { return input ? -1 : 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool input) const override { return d != nullptr && ! input ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& out, const juce::String&) override
    {
        return out == "Panel test device" ? new PanelTestDevice() : nullptr;
    }
};
} // namespace

juce::String AudioEngine::driverPanelSelfTest()
{
    auto& state = panelTest();
    state = {};
    juce::AudioDeviceManager devices;
    devices.addAudioDeviceType (std::make_unique<PanelTestType>());
    devices.setCurrentAudioDeviceType ("Panel test", true);
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.outputDeviceName = "Panel test device";
    setup.sampleRate = 44100.0;
    setup.bufferSize = 272; // chosen in LYNX's own buffer box
    setup.useDefaultOutputChannels = true;
    if (const auto error = devices.setAudioDeviceSetup (setup, true); error.isNotEmpty())
        return "open: " + error;
    auto* before = devices.getCurrentAudioDevice();
    if (before == nullptr || before->getCurrentBufferSizeSamples() != 272)
        return "open: buffer " + juce::String (before != nullptr ? before->getCurrentBufferSizeSamples() : -1);

    // a panel that changes nothing: the same device keeps running with LYNX's buffer
    state.panelResult = 0;
    if (showDriverPanel (devices) || devices.getCurrentAudioDevice() != before || before->getCurrentBufferSizeSamples() != 272)
        return "unchanged panel reopened the device";

    // the panel picks 64 samples (issue #13's ASIO4ALL setting): reopened, the panel's buffer kept, never the old object
    state.panelResult = 64;
    if (! showDriverPanel (devices))
        return "changed panel did not reopen";
    auto* after = devices.getCurrentAudioDevice();
    if (state.calledDeleted)
        return "called the deleted device";
    if (after == nullptr || ! after->isOpen())
        return "no device after the panel";
    if (after->getCurrentBufferSizeSamples() != 64)
        return "buffer " + juce::String (after->getCurrentBufferSizeSamples()) + " after the panel chose 64";
    if (state.alive.size() != 1)
        return juce::String ((int) state.alive.size()) + " devices alive";
    // and the reopened device can show its panel again (the button pressed twice)
    state.panelResult = 128;
    if (! showDriverPanel (devices) || devices.getCurrentAudioDevice() == nullptr || devices.getCurrentAudioDevice()->getCurrentBufferSizeSamples() != 128
        || state.calledDeleted)
        return "second panel";
    devices.closeAudioDevice();
    return "OK";
}

juce::File AudioEngine::glitchLogFile() { return PluginHost::settingsFolder().getChildFile ("audio-glitches.log"); }

void AudioEngine::updateRealtimeStats()
{
    // the message thread's pulse: this 30 Hz timer arriving late means the UI is saturated
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (lastUiPulseMs > 0.0)
            uiPulse.maxGapMs = juce::jmax (uiPulse.maxGapMs, now - lastUiPulseMs);
        lastUiPulseMs = now;
        ++uiPulse.calls;
    }

    // every ~2 s: the share of real time each channel / insert effect took in that window
    if (rackProcessor != nullptr && juce::Time::getMillisecondCounterHiRes() - lastUnitsWindowMs >= 2000.0)
    {
        lastUnitsWindowMs = juce::Time::getMillisecondCounterHiRes();
        auto& renderer = rackProcessor->getRenderer();
        const auto& units = renderer.unitTimes();
        const auto processed = units.processedSamples.load (std::memory_order_relaxed);
        const auto rate = juce::jmax (1.0, transport.sampleRate.load());
        const auto audioNs = (double) (processed - std::min (processed, lastProcessedSamples)) / rate * 1.0e9;
        lastProcessedSamples = processed;
        lastChannelNs.resize (units.channelNs.size(), 0);
        lastEffectNs.resize (units.effectNs.size(), 0);
        std::vector<UnitLoad> loads;

        auto take = [&] (const std::atomic<std::uint64_t>& total, std::uint64_t& last, std::atomic<std::uint64_t>& longest, auto&& name) {
            const auto now = total.load (std::memory_order_relaxed);
            const auto delta = now - std::min (now, last);
            last = now;
            const auto longestNs = const_cast<std::atomic<std::uint64_t>&> (longest).exchange (0, std::memory_order_relaxed);
            if (delta > 0 && audioNs > 0.0)
                loads.push_back ({ name(), (double) delta / audioNs, (double) longestNs * 1.0e-6 });
        };
        auto& mutableUnits = const_cast<RackRenderer::UnitTimes&> (units);
        for (std::size_t c = 0; c < units.channelNs.size(); ++c)
            take (units.channelNs[c], lastChannelNs[c], mutableUnits.channelMaxNs[c], [&] {
                return c < song.channels.size() ? juce::String::fromUTF8 (song.channels[c].name.c_str()) : "Channel " + juce::String ((int) c + 1);
            });
        for (std::size_t k = 0; k < units.effectNs.size(); ++k)
            take (units.effectNs[k], lastEffectNs[k], mutableUnits.effectMaxNs[k], [&] {
                const auto t = k / (std::size_t) maxEffectSlots, slot = k % (std::size_t) maxEffectSlots;
                juce::String effect;
                if (t < song.mixer.size() && slot < song.mixer[t].effects.size())
                {
                    const auto& fx = song.mixer[t].effects[slot];
                    effect = juce::String::fromUTF8 (fx.plugin.name.empty() ? effectInfo (fx.kind).name : fx.plugin.name.c_str());
                }
                const auto track = t == 0 ? juce::String ("Master") : (t < song.mixer.size() && ! song.mixer[t].name.empty() ? juce::String::fromUTF8 (song.mixer[t].name.c_str())
                                                                                                               : "Insert " + juce::String ((int) t));
                return track + " / " + juce::String ((int) slot + 1) + " " + effect;
            });
        std::sort (loads.begin(), loads.end(), [] (const UnitLoad& a, const UnitLoad& b) { return a.load > b.load; });
        if (loads.size() > 8)
            loads.resize (8);
        if (audioNs > 0.0)
            heaviestUnits = std::move (loads);
    }

    // a line in the glitch log when callbacks ran over their deadline or the device called late
    const auto stats = rtStats.snapshot();
    if ((stats.overBudget > loggedOver || stats.lateCalls > loggedLate) && juce::Time::getMillisecondCounterHiRes() - lastLogMs > 2000.0)
    {
        // (appended by fileJobs: this runs on the message thread, and at a small buffer glitches come every 2 s)
        const auto bar = transport.positionTicks.load() / (double) (ppq * juce::jmax (1, song.beatsPerBar)) + 1.0;
        auto line = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + "  over deadline +" + juce::String ((juce::int64) (stats.overBudget - loggedOver))
                    + ", late calls +" + juce::String ((juce::int64) (stats.lateCalls - loggedLate)) + ", at bar " + juce::String (bar, 2)
                    + (transport.playing.load() ? "" : " (stopped)") + "\n" + describeRealtime() + "\n";
        fileJobs.post ([line = std::move (line), file = glitchLogFile()] {
            if (file.getSize() > 1024 * 1024)
                file.moveFileTo (file.withFileExtension ("old.log"));
            file.appendText (line, false, false, "\r\n");
        });
        loggedOver = stats.overBudget;
        loggedLate = stats.lateCalls;
        lastLogMs = juce::Time::getMillisecondCounterHiRes();
    }
}

juce::String AudioEngine::describeRealtime (bool withUnits) const
{
    juce::String text;
    if (auto* device = const_cast<LynxDeviceManager&> (deviceManager).getCurrentAudioDevice())
    {
        const auto rate = device->getCurrentSampleRate();
        const auto buffer = device->getCurrentBufferSizeSamples();
        text << "  device: " << device->getTypeName() << " / " << device->getName() << ", " << (int) rate << " Hz, buffer " << buffer
             << " samples (deadline " << juce::String (prefs::bufferLatencyMs (buffer, rate), 2) << " ms), output latency "
             << device->getOutputLatencyInSamples() << " samples, driver dropouts " << device->getXRunCount() << "\n";
       #if JUCE_LINUX
        text << "  audio thread: " << (rt::status() == rt::Status::unknown ? juce::String ("not seen yet") : rt::describe()) << "\n";
       #endif
       #if JUCE_WINDOWS
        if (const auto before = deviceThreadPriorityBefore.load (std::memory_order_relaxed); before > -100)
            text << "  device thread priority " << before << (deviceThreadPriorityAfter.load (std::memory_order_relaxed) != before
                                                                ? " -> " + juce::String (deviceThreadPriorityAfter.load (std::memory_order_relaxed)) + " (raised to MMCSS Pro Audio)"
                                                                : juce::String (boostDeviceThread ? " (kept)" : " (kept: LYNX_DEVICE_BOOST=0)")) << "\n";
       #endif
    }
    else
        text << "  device: none\n";
    const auto s = rtStats.snapshot();
    text << "  callbacks " << (juce::int64) s.callbacks << ": over deadline " << (juce::int64) s.overBudget << ", 80-100% " << (juce::int64) s.nearBudget
         << ", late device calls " << (juce::int64) s.lateCalls << ", average load " << juce::String (s.averageLoad * 100.0, 1) << "%, max "
         << juce::String (s.maxLoad * 100.0, 0) << "%, workers " << (workers != nullptr ? workers->workers() : 0) << "\n";
    if (latencyRepublishes > 0)
        text << "  latency changes re-planned PDC " << latencyRepublishes << " times (last: unit " << lastLatencyChange << ")\n";
    if (withUnits && heaviestUnits.empty())
        text << "  heaviest: not measured yet\n";
    if (withUnits && ! heaviestUnits.empty())
    {
        text << "  heaviest:";
        for (std::size_t i = 0; i < heaviestUnits.size() && i < 5; ++i)
            text << (i > 0 ? ";" : "") << " " << heaviestUnits[i].name << " " << juce::String (heaviestUnits[i].load * 100.0, 1) << "% (peak "
                 << juce::String (heaviestUnits[i].longestMs, 1) << " ms)";
        text << "\n";
    }
    return text;
}

bool AudioEngine::freezeOutOfDate (int channel)
{
    if (channel < 0 || channel >= (int) song.channels.size() || ! song.channels[(std::size_t) channel].freeze.on())
        return false;
    if (freezeChecks.size() < song.channels.size())
        freezeChecks.resize (song.channels.size());
    auto& check = freezeChecks[(std::size_t) channel];
    if (check.stamp != songStamp)
    {
        check.stamp = songStamp;
        check.outOfDate = sessionpack::freezeSource (song, channel, transport.sampleRate.load()) != song.channels[(std::size_t) channel].freeze.source;
    }
    return check.outOfDate;
}

std::shared_ptr<Instrument> AudioEngine::getChannelInstrument (int channel) const
{
    if (channel < 0 || channel >= (int) lastPublishedShape.instruments.size())
        return nullptr;

    return lastPublishedShape.instruments[(std::size_t) channel];
}

std::shared_ptr<Effect> AudioEngine::getEffect (int track, int slot) const
{
    if (externalEffect)
        return externalEffect();

    const auto& effects = lastPublishedShape.effects;

    if (track < 0 || track >= (int) effects.size() || slot < 0 || slot >= (int) effects[(std::size_t) track].size())
        return nullptr;

    return effects[(std::size_t) track][(std::size_t) slot];
}

void AudioEngine::setPluginsRenderMode (bool offline)
{
    vfx::script::setRealtime (! offline); // (an export waits for every VFX Script answer: exact and repeatable)
    if (! offline)
        midiSync.sendAllNotesOff(); // FL's "Send all notes off": after a render
    // FL's "Notify about rendering mode" off: that plugin is not told (round 149); "Reset plugin when FL Studio
    // resets": reset as the export starts (round 151)
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument ((int) i).get()))
        {
            if (song.channels[i].plugin.notifyRender)
                p->setRenderMode (offline);
            if (offline)
                p->resetForExport();
        }

    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
            if (auto* p = dynamic_cast<PluginEffect*> (getEffect ((int) t, (int) s).get()))
            {
                if (song.mixer[t].effects[s].plugin.notifyRender)
                    p->setRenderMode (offline);
                if (offline)
                    p->resetForExport();
            }

    // the plugins inside Patchers (REA_rodada141)
    std::function<void (patcher::Graph*)> graphMode = [&] (patcher::Graph* graph) {
        if (graph == nullptr)
            return;
        for (int n = 0; n < graph->nodeCount(); ++n)
        {
            auto effect = graph->effectAt (n);
            auto instrument = graph->instrumentAt (n);
            if (auto* p = dynamic_cast<PluginEffect*> (effect.get()))
                p->setRenderMode (offline);
            if (auto* p = dynamic_cast<PluginInstrument*> (instrument.get()))
                p->setRenderMode (offline);
            graphMode (patcher::graphOf (effect.get()));
            graphMode (patcher::graphOf (instrument.get()));
        }
    };
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        graphMode (patcher::graphOf (getChannelInstrument ((int) i).get()));
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
            graphMode (patcher::graphOf (getEffect ((int) t, (int) s).get()));
}

void AudioEngine::stampWorkTime()
{
    const auto now = (double) juce::Time::currentTimeMillis();
    if (workSince > 0.0 && now > workSince)
        song.info.workSeconds += (now - workSince) / 1000.0;
    workSince = now;
}

double AudioEngine::workSecondsNow() const
{
    const auto now = (double) juce::Time::currentTimeMillis();
    return song.info.workSeconds + (workSince > 0.0 && now > workSince ? (now - workSince) / 1000.0 : 0.0);
}

// FL's Project info › "Reset working time" (round 190bx; its handler stores "now" as the project's start and restarts
// the clock, without a question): the project counts from this moment on. Not an undo step, as the clock itself.
void AudioEngine::resetWorkTime()
{
    const auto now = (double) juce::Time::currentTimeMillis();
    lynx::resetWorkTime (song.info, now / 1000.0);
    workSince = now;
    modified = true;
    sendChangeMessage(); // (the Info page; nothing the audio thread plays changed)
}

void AudioEngine::markCreatedNow()
{
    if (song.info.created <= 0.0)
        song.info.created = (double) juce::Time::currentTimeMillis() / 1000.0;
}

void AudioEngine::pumpEdisonRecordings()
{
    bool finished = false;
    for (const auto& track : song.mixer)
        for (const auto& fx : track.effects)
            if (fx.kind == EffectKind::edison)
                if (const auto session = edisonfx::findSession (fx.instance.id); session != nullptr && session->recording() && ! session->pump())
                    finished = true; // (the maximum recording time, or its effect is gone)
    if (finished)
        flushEdisonAudio();
}

void AudioEngine::flushEdisonAudio()
{
    struct Change
    {
        std::uint64_t instance;
        std::string path;
        bool clearState;
    };
    std::vector<Change> changes;
    for (const auto& track : song.mixer)
        for (const auto& fx : track.effects)
        {
            if (fx.kind != EffectKind::edison)
                continue;
            const auto session = edisonfx::findSession (fx.instance.id);
            if (session == nullptr || session->recording())
                continue;
            const auto wasDirty = session->dirty();
            if (wasDirty)
            {
                if (session->document().audio.empty())
                    session->save ({});
                else
                {
                    auto folder = recordingFolder;
                    if (! folder.createDirectory()) // e.g. a read-only project folder: the global recordings folder
                        folder = defaultRecordingFolder(), folder.createDirectory();
                    const auto name = (recordingStem.isNotEmpty() ? recordingStem + "_" : juce::String())
                                      + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + "_Edison";
                    const auto file = folder.getNonexistentChildFile (name, ".wav", false);
                    if (! session->save (file.getFullPathName().toStdString()))
                    {
                        recordingError = tr ("Could not write {0}", { file.getFullPathName() });
                        continue; // (it stays unsaved: the next flush tries again)
                    }
                    sessionRecordings.push_back (file); // (a file this project may end up not using, like a take)
                }
            }
            // the slot follows its session: after a write here, or one by another engine of the same project
            if (wasDirty || (! session->path().empty() && session->path() != fx.samplePath))
                changes.push_back ({ fx.instance.id, session->path(), wasDirty });
        }
    if (changes.empty())
        return;
    auto apply = [&changes] (Song& s) {
        for (auto& track : s.mixer)
            for (auto& fx : track.effects)
                if (fx.kind == EffectKind::edison)
                    for (const auto& c : changes)
                        if (c.instance == fx.instance.id)
                        {
                            fx.samplePath = c.path;
                            if (c.clearState)
                                fx.state.clear(); // (an imported slot: FL's state held the audio that was replaced)
                        }
    };
    const auto differs = std::any_of (changes.begin(), changes.end(), [this] (const Change& c) {
        for (const auto& track : song.mixer)
            for (const auto& fx : track.effects)
                if (fx.kind == EffectKind::edison && fx.instance.id == c.instance)
                    return fx.samplePath != c.path || (c.clearState && ! fx.state.empty());
        return false;
    });
    if (differs)
        editSong (apply, false);
}

void AudioEngine::capturePluginStates()
{
    stampWorkTime();
    flushEdisonAudio(); // (round 190cd: an Edison slot's audio is a file of the project)
    // Changes only the saved state (recreates nothing and doesn't enter the undo history).
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument ((int) i).get()))
            if (song.channels[i].plugin.playedBy == 0) // (P5-A1: a plugin shared in a live session keeps no state, only values)
                if (auto state = p->captureState()) // (a plugin that crashed keeps the state saved before)
                    song.channels[i].plugin.state = std::move (*state);

    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
            if (auto* p = dynamic_cast<PluginEffect*> (getEffect ((int) t, (int) s).get()))
                if (song.mixer[t].effects[s].plugin.playedBy == 0)
                    if (auto state = p->captureState())
                        song.mixer[t].effects[s].plugin.state = std::move (*state);

    // the plugins inside Patchers (REA_rodada141), nested ones too
    std::function<void (PatcherDoc&, patcher::Graph*)> captureGraph = [&] (PatcherDoc& doc, patcher::Graph* graph) {
        if (graph == nullptr)
            return;
        for (int n = 0; n < (int) doc.nodes.size() && n < graph->nodeCount(); ++n)
        {
            auto& node = doc.nodes[(std::size_t) n];
            if (node.generator)
            {
                auto instrument = graph->instrumentAt (n);
                if (auto* p = dynamic_cast<PluginInstrument*> (instrument.get()))
                    if (auto state = p->captureState())
                        node.channel.plugin.state = std::move (*state);
                if (node.channel.instrument == InstrumentKind::patcher)
                    captureGraph (node.channel.patcher, patcher::graphOf (instrument.get()));
            }
            else
            {
                auto effect = graph->effectAt (n);
                if (auto* p = dynamic_cast<PluginEffect*> (effect.get()))
                    if (auto state = p->captureState())
                        node.effect.plugin.state = std::move (*state);
                if (node.effect.kind == EffectKind::patcher)
                    captureGraph (node.effect.patcher, patcher::graphOf (effect.get()));
            }
        }
    };
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        if (song.channels[i].instrument == InstrumentKind::patcher)
            captureGraph (song.channels[i].patcher, patcher::graphOf (getChannelInstrument ((int) i).get()));
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
            if (song.mixer[t].effects[s].kind == EffectKind::patcher)
                captureGraph (song.mixer[t].effects[s].patcher, patcher::graphOf (getEffect ((int) t, (int) s).get()));

    lastPublishedShape.song.channels = song.channels;
    lastPublishedShape.song.mixer = song.mixer;
}

bool AudioEngine::auditionPreset (const juce::AudioPluginInstance& plugin)
{
    auto channel = -1;
    for (std::size_t i = 0; i < song.channels.size() && channel < 0; ++i)
        if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument ((int) i).get()); p != nullptr && &p->getInstance() == &plugin && ! p->hasCrashed())
            channel = (int) i;
    if (! pluginpresets::auditionPlays (true, channel >= 0, transport.playing.load(), transport.recording.load()))
        return false;
    if (auditionChannel >= 0 && auditionSounding) // the last one still sounds: it ends now
        sendLiveNote (auditionChannel, pluginpresets::auditionKey, 0.0f, false);
    auditionChannel = channel;
    auditionSounding = false;
    auditionAtMs = juce::Time::getMillisecondCounterHiRes() + pluginpresets::auditionDelayMs;
    return true;
}

bool AudioEngine::auditionChannelSound (int channel)
{
    if (channel < 0 || channel >= (int) song.channels.size()
        || ! pluginpresets::auditionPlays (true, true, transport.playing.load(), transport.recording.load()))
        return false;
    if (auditionChannel >= 0 && auditionSounding) // the last one still sounds: it ends now
        sendLiveNote (auditionChannel, pluginpresets::auditionKey, 0.0f, false);
    auditionChannel = channel;
    auditionSounding = false;
    auditionAtMs = juce::Time::getMillisecondCounterHiRes() + pluginpresets::auditionDelayMs;
    return true;
}

void AudioEngine::updateAudition()
{
    if (auditionChannel < 0 || juce::Time::getMillisecondCounterHiRes() < auditionAtMs)
        return;
    if (auditionSounding)
    {
        sendLiveNote (auditionChannel, pluginpresets::auditionKey, 0.0f, false);
        auditionChannel = -1;
        auditionSounding = false;
        return;
    }
    // (the song started in between, or the channel went: nothing plays)
    if (transport.playing.load() || auditionChannel >= (int) song.channels.size())
    {
        auditionChannel = -1;
        return;
    }
    sendLiveNote (auditionChannel, pluginpresets::auditionKey, pluginpresets::auditionVelocity, true);
    auditionSounding = true;
    auditionAtMs = juce::Time::getMillisecondCounterHiRes() + pluginpresets::auditionLengthMs;
}

namespace
{
bool pluginIsDown (Instrument* instrument)
{
    auto* p = dynamic_cast<PluginInstrument*> (instrument);
    return p != nullptr && (p->hasCrashed() || isSandboxedPluginDead (p->getInstance()));
}

bool pluginIsDown (Effect* effect)
{
    auto* p = dynamic_cast<PluginEffect*> (effect);
    return p != nullptr && (p->hasCrashed() || isSandboxedPluginDead (p->getInstance()));
}
} // namespace

bool AudioEngine::hostedPluginDown (HostedSlot where) const
{
    return where.track < 0 ? pluginIsDown (getChannelInstrument (where.channel).get()) : pluginIsDown (getEffect (where.track, where.slot).get());
}

std::vector<AudioEngine::HostedSlot> AudioEngine::downHostedPlugins (const std::string& named) const
{
    std::vector<HostedSlot> out;
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        if (song.channels[i].instrument == InstrumentKind::plugin && (named.empty() || song.channels[i].plugin.name == named)
            && hostedPluginDown ({ (int) i, -1, -1 }))
            out.push_back ({ (int) i, -1, -1 });
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
            if (const auto& fx = song.mixer[t].effects[s];
                fx.kind == EffectKind::plugin && (named.empty() || fx.plugin.name == named) && hostedPluginDown ({ -1, (int) t, (int) s }))
                out.push_back ({ -1, (int) t, (int) s });
    return out;
}

int AudioEngine::reloadHostedPlugins (const std::vector<HostedSlot>& which)
{
    // the running plugins' current settings (a reloaded one that still runs starts from them); this also brings the
    // last published shape in line with the song
    capturePluginStates();
    std::vector<HostedSlot> asked;
    for (const auto& w : which)
        if (w.track < 0 ? forgetHostedPlugin (lastPublishedShape, w.channel) : forgetHostedPlugin (lastPublishedShape, w.track, w.slot))
            asked.push_back (w);
    if (asked.empty())
        return 0;
    publishSong();
    auto running = 0;
    for (const auto& w : asked)
    {
        const auto loaded = w.track < 0 ? dynamic_cast<PluginInstrument*> (getChannelInstrument (w.channel).get()) != nullptr
                                        : dynamic_cast<PluginEffect*> (getEffect (w.track, w.slot).get()) != nullptr;
        running += loaded && ! hostedPluginDown (w) ? 1 : 0;
    }
    return running;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext& context)
{
    // FTZ/DAZ for the whole callback (round 160): the rack had it, but the metronome, the preview player and the
    // hosted plugins' graph nodes around it ran without
    const ScopedFlushDenormals noDenormals;
    rt::noteAudioThread(); // Linux: the message thread asks for real-time priority for this thread once
   #if JUCE_WINDOWS
    // round 174e: a new device thread (first callback, a device restart) gets the workers' real-time class unless the
    // driver already runs it time-critical: the callback waits on the workers, and a normal-priority callback thread
    // lost the CPU to the UI's frames (more late calls while the screen was busy)
    if (const auto id = (unsigned long) GetCurrentThreadId(); id != deviceThreadId.load (std::memory_order_relaxed))
    {
        deviceThreadId.store (id, std::memory_order_relaxed);
        const auto before = GetThreadPriority (GetCurrentThread());
        deviceThreadPriorityBefore.store (before, std::memory_order_relaxed);
        if (boostDeviceThread && before < THREAD_PRIORITY_TIME_CRITICAL)
            raiseAudioThreadPriority();
        deviceThreadPriorityAfter.store (GetThreadPriority (GetCurrentThread()), std::memory_order_relaxed);
    }
   #endif
    // real-time health: how long this callback took against its deadline (the buffer's duration)
    const auto callbackStart = juce::Time::getMillisecondCounterHiRes();
    struct Measure
    {
        ~Measure()
        {
            const auto rate = engine.transport.sampleRate.load (std::memory_order_relaxed);
            if (rate > 0.0)
                engine.rtStats.record (start, juce::Time::getMillisecondCounterHiRes() - start, 1000.0 * samples / rate);
        }
        AudioEngine& engine;
        double start;
        int samples;
    } measure { *this, callbackStart, numSamples };

    // an export renders (round 168s): playback stops and nothing is processed until it ends (not while recording)
    if (offlineRendersRunning().load (std::memory_order_acquire) > 0 && ! transport.recording.load (std::memory_order_relaxed))
    {
        if (transport.playing.load (std::memory_order_relaxed))
            stop();
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
        return;
    }

    if (transport.rewindRequested.exchange (false))
    {
        transport.positionTicks.store (0.0);
        transport.userJumps.fetch_add (1, std::memory_order_relaxed);
        if (transport.resetPluginsOnTransport.load (std::memory_order_relaxed))
            midiSync.requestTransportNotesOff(); // FL: CC 123 to the "Send all notes off" outputs (REA 167l §6)
    }

    if (const auto target = transport.seekRequest.exchange (-1.0); target >= 0.0)
    {
        transport.positionTicks.store (target);
        transport.userJumps.fetch_add (1, std::memory_order_relaxed);
        if (transport.resetPluginsOnTransport.load (std::memory_order_relaxed))
            midiSync.requestTransportNotesOff();
    }

    transport.readMarkers (rtMarkers, rtMarkerVersion); // FL's marker actions (a new table when the song changed)
    // an external MIDI clock moves the transport (round 165f)
    midiSync.beginBlock (transport, callbackStart, numSamples);

    // With the loop region on, the block is split at the loop end and the position jumps back to
    // its start (the nodes see a jump and release their notes, as in FL).
    // the audio editor's input recording (nothing happens unless it chose the input)
    meters.capture.push (AudioCapture::input, numInputChannels > 0 ? inputChannelData[0] : nullptr, numInputChannels > 1 ? inputChannelData[1] : nullptr,
                         numSamples);

    std::array<const float*, 64> inputs {};
    std::array<float*, 64> outputs {};
    numInputChannels = juce::jmin (numInputChannels, (int) inputs.size());
    numOutputChannels = juce::jmin (numOutputChannels, (int) outputs.size());

    // count-in: the metronome counts the pre-roll down, then playback starts — to the sample (round 170d: the song
    // started at the next block boundary and the count-in's clicks ran a block early, so the downbeat flammed ~5 ms
    // after the last count-in click and the take's notes were late against it): the block is split where the count-in
    // ends, the part before it counts in (the metronome's clock = the start minus what is left at that sample)
    const auto countInAtStart = transport.countIn.load (std::memory_order_relaxed);
    const auto countingIn = countInAtStart > 0.0 && ! transport.playing.load (std::memory_order_relaxed);
    const auto countInSamples = ! countingIn ? 0
                                : (int) juce::jmin ((double) numSamples, std::ceil (countInAtStart / juce::jmax (1.0e-9, transport.ticksPerSample()) - 1.0e-6));

    auto countInNow = countInAtStart;

    for (int done = 0; done < numSamples;)
    {
        auto n = numSamples - done;
        if (countInSamples > done)
            n = juce::jmin (n, countInSamples - done);
        const auto playing = transport.playing.load (std::memory_order_relaxed);
        const auto tps = transport.ticksPerSample();
        // Song mode: the loop region, or else the whole song (back to the start at its end, like FL)
        // (while audio takes record in Song mode without a loop region, the song plays on past its end instead of
        // starting over: the take stays one piece, as in Pro Tools / Nuendo; round 163)
        // (punching on the loop region: the region is not looped either, playback runs on into the post-roll)
        const auto linear = liveInput.linearSong.load (std::memory_order_relaxed);
        const auto throughRegion = liveInput.noLoopRegion.load (std::memory_order_relaxed);
        const auto loop = playbackLoop (transport.songMode.load (std::memory_order_relaxed),
                                        throughRegion ? 0.0 : transport.loopStart.load (std::memory_order_relaxed),
                                        throughRegion ? 0.0 : transport.loopEnd.load (std::memory_order_relaxed),
                                        linear ? 0.0 : transport.songEnd.load (std::memory_order_relaxed),
                                        transport.songStart.load (std::memory_order_relaxed));
        const auto loopStart = loop.start, loopEnd = loop.end;
        const auto looping = playing && loop.active() && tps > 0.0;

        // without the loop region, the song's end goes back to FL's "Loop" marker (else the start)
        const auto regionOn = ! throughRegion && transport.loopEnd.load (std::memory_order_relaxed) > transport.loopStart.load (std::memory_order_relaxed);
        const auto songWrap = regionOn ? loopStart : juce::jlimit (loopStart, juce::jmax (loopStart, loopEnd - 1.0), (double) rtMarkers.wrapTo);

        if (looping)
        {
            // before the loop, play normally until reaching it; past the end, jump back to the start
            auto position = transport.positionTicks.load (std::memory_order_relaxed);

            if (transportReached (position, loopEnd))
            {
                position = songWrap;
                transport.positionTicks.store (position, std::memory_order_relaxed);
            }

            n = transportSamplesUntil (position, loopEnd, tps, n);
        }

        // FL's marker actions (Song mode): the block ends where the position reaches a marker jump / pause, or a
        // quantized jump the user asked for; it is made after the block (below), like the loop's wrap
        const MarkerJump* markerJump = nullptr;
        const auto pendingAt = transport.pendingJumpAt.load (std::memory_order_relaxed);
        auto pendingDue = false;
        if (playing && tps > 0.0 && transport.songMode.load (std::memory_order_relaxed))
        {
            const auto step = markerStep (rtMarkers, transport.positionTicks.load (std::memory_order_relaxed), n, tps, pendingAt);
            n = step.samples;
            markerJump = step.jump;
            pendingDue = step.userJump;
        }

        for (int ch = 0; ch < numInputChannels; ++ch)
            inputs[(std::size_t) ch] = inputChannelData[ch] != nullptr ? inputChannelData[ch] + done : nullptr;

        // the block's input by device channel, for the inserts (monitoring, meters) and the takes
        liveInput.numChannels = 0;
        for (int c = 0; c < LiveInput::maxChannels; ++c)
        {
            const auto slot = inputSlot[(std::size_t) c];
            liveInput.channels[(std::size_t) c] = slot >= 0 && slot < numInputChannels ? inputs[(std::size_t) slot] : nullptr;
            if (liveInput.channels[(std::size_t) c] != nullptr)
                liveInput.numChannels = c + 1;
        }

        // audio recording: this block's input goes to the takes' streams (without waiting on a lock). Every take gets
        // every block (silence for an input channel missing now), so all of them keep the same timeline (round 163).
        // The lock is held through the render when takes are tapped inside their inserts (round 168x): their part of
        // this block is pushed after the render, and every take must get every block.
        auto takesLocked = false;
        auto tappedTakes = false;
        if (playing && (transport.recording.load (std::memory_order_relaxed) || slotAudioCapture.load (std::memory_order_relaxed)))
        {
            takesLocked = takeLock.tryEnter();

            if (takesLocked && takeCount > 0)
            {
                for (int k = 0; k < takeCount; ++k)
                {
                    auto* take = takes[(std::size_t) k];
                    if (take->tap >= 0) // filled by the renderer: silence until it writes the tap
                    {
                        tappedTakes = true;
                        if (n <= LiveInput::tapCapacity)
                            for (auto& d : liveInput.taps[(std::size_t) take->tap].data)
                                std::fill (d.begin(), d.begin() + n, 0.0f);
                        continue;
                    }
                    const std::array<const float*, 2> data { liveInput.channel (take->first, 0), liveInput.channel (take->first + take->channels - 1, 0) };
                    if (data[0] == nullptr || data[1] == nullptr)
                        take->stream->pushSilence (n);
                    else
                        take->stream->push (data.data(), n);
                }

                if (takeStartTick.load (std::memory_order_relaxed) < 0.0)
                {
                    takeStartTick.store (transport.positionTicks.load (std::memory_order_relaxed));
                    // (round 190bv) the clip grid's clock at this sample: its own while it runs, else it starts at the song position
                    takeStartClock.store (clipLauncher != nullptr && clipLauncher->running.load (std::memory_order_relaxed)
                                              ? clipLauncher->clock
                                              : transport.positionTicks.load (std::memory_order_relaxed),
                                          std::memory_order_relaxed);
                }
                takeFrames.fetch_add (n, std::memory_order_relaxed);
                takeTicks.store (takeTicks.load (std::memory_order_relaxed) + n * tps, std::memory_order_relaxed);
            }
            if (takesLocked && ! tappedTakes)
            {
                takeLock.exit();
                takesLocked = false;
            }
        }

        for (int ch = 0; ch < numOutputChannels; ++ch)
            outputs[(std::size_t) ch] = outputChannelData[ch] != nullptr ? outputChannelData[ch] + done : nullptr;

        // master sync out (round 165f): this part's clock / MTC messages, time-stamped for the sender thread
        midiSync.subBlock (transport.positionTicks.load (std::memory_order_relaxed), tps, n, done, playing);

        player.audioDeviceIOCallbackWithContext (inputs.data(), numInputChannels, outputs.data(), numOutputChannels, n, context);
        routeOutputs (outputs.data(), numOutputChannels, n);

        if (takesLocked) // the tapped takes' part of this block, now rendered (round 168x)
        {
            for (int k = 0; k < takeCount; ++k)
                if (auto* take = takes[(std::size_t) k]; take->tap >= 0)
                {
                    if (n > LiveInput::tapCapacity)
                    {
                        take->stream->pushSilence (n);
                        continue;
                    }
                    auto& tap = liveInput.taps[(std::size_t) take->tap];
                    const auto drop = juce::jmin (n, take->skip - take->skipped); // the effects' latency at the start
                    take->skipped += drop;
                    if (drop < n)
                    {
                        const std::array<const float*, 2> data { tap.data[0].data() + drop, tap.data[1].data() + drop };
                        take->stream->push (data.data(), n - drop);
                    }
                }
            takeLock.exit();
        }

        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputs[(std::size_t) ch] != nullptr)
                clipOvers (outputs[(std::size_t) ch], n);

        // oscilloscope: average of both sides into the ring buffer
        if (numOutputChannels > 0 && outputs[0] != nullptr)
        {
            auto w = scopeWrite.load (std::memory_order_relaxed);
            const auto* right = numOutputChannels > 1 && outputs[1] != nullptr ? outputs[1] : outputs[0];

            for (int i = 0; i < n; ++i)
            {
                scope[(std::size_t) w].store (0.5f * (outputs[0][i] + right[i]), std::memory_order_relaxed);
                w = (w + 1) % scopeSize;
            }

            scopeWrite.store (w, std::memory_order_release);
        }

        if (playing)
        {
            auto position = transport.positionTicks.load (std::memory_order_relaxed) + n * tps;

            if (pendingDue && transportReached (position, pendingAt))
            {
                position = transport.pendingJumpTo.load (std::memory_order_relaxed);
                transport.pendingJumpAt.store (-1.0, std::memory_order_relaxed);
                markerEvents.fetch_add (1, std::memory_order_relaxed);
                transport.userJumps.fetch_add (1, std::memory_order_relaxed);
            }
            else if (markerJump != nullptr && transportReached (position, (double) markerJump->at))
            {
                markerEvents.fetch_add (1, std::memory_order_relaxed);
                if (markerJump->kind == MarkerJump::Kind::pause)
                {
                    // FL's "Marker pause": playback stops at the marker; Play goes on from there
                    position = (double) markerJump->at;
                    transport.positionTicks.store (position, std::memory_order_relaxed);
                    transport.playing.store (false, std::memory_order_relaxed); // the rest of the block plays stopped
                }
                position = (double) markerJump->to;
                transport.userJumps.fetch_add (1, std::memory_order_relaxed);
            }
            else if (looping && transportReached (position, loopEnd))
            {
                position = songWrap;
                // loop recording: where this pass ends in the takes (round 163)
                if (const auto frames = takeFrames.load (std::memory_order_relaxed); frames > 0 && takeStartTick.load (std::memory_order_relaxed) >= 0.0)
                    if (const auto k = takeWrapCount.load (std::memory_order_relaxed); k < (int) takeWraps.size())
                    {
                        takeWraps[(std::size_t) k].store (frames, std::memory_order_relaxed);
                        takeWrapCount.store (k + 1, std::memory_order_release);
                    }
            }

            transport.positionTicks.store (position, std::memory_order_relaxed);
        }

        // the count-in's part of the block is over: what is left of it, or playback from here
        // (a compare-exchange: a Stop pressed meanwhile set it to 0 and must win)
        if (countInSamples > done)
        {
            const auto rawLeft = countInAtStart - (done + n) * transport.ticksPerSample();
            const auto left = juce::jmax (0.0, rawLeft);
            const auto ends = done + n >= countInSamples && rawLeft <= 1.0e-6;
            auto expected = countInNow;
            if (transport.countIn.compare_exchange_strong (expected, ends ? 0.0 : left, std::memory_order_relaxed))
            {
                countInNow = ends ? 0.0 : left;
                if (ends)
                    transport.playing.store (true, std::memory_order_relaxed);
            }
        }

        done += n;
    }

    // the latency ping (round 163): only its clicks go out (no feedback through a monitored input), listened for on
    // its input
    if (ping.isRunning())
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
        std::array<float*, 2> clickOut {};
        for (std::size_t k = 0; k < 2; ++k)
            if (pingOutputs[k] >= 0 && pingOutputs[k] < numOutputChannels)
                clickOut[k] = outputChannelData[pingOutputs[k]];
        const auto* in = pingInput >= 0 && pingInput < numInputChannels ? inputChannelData[pingInput] : nullptr;
        ping.process (in, clickOut.data(), 2, numSamples);
    }

    // the block is done: the workers sleep until the next one instead of spinning (REA round 116)
    if (workers != nullptr)
        workers->park();

    // the live clock (round 170d): the next block's musical position — during the count-in the metronome's count before
    // the start (the start minus the count-in left)
    {
        const auto tps = transport.ticksPerSample();
        const auto nowPlaying = transport.playing.load (std::memory_order_relaxed);
        const auto left = transport.countIn.load (std::memory_order_relaxed);
        const auto counting = left > 0.0 && ! nowPlaying;
        const auto position = transport.positionTicks.load (std::memory_order_relaxed);
        clockTick.store (counting ? position - left : position, std::memory_order_relaxed);
        clockTicksPerMs.store (tps * transport.sampleRate.load (std::memory_order_relaxed) * 0.001, std::memory_order_relaxed);
        clockRunning.store (nowPlaying || counting, std::memory_order_relaxed);
    }
    if (const auto rate = transport.sampleRate.load (std::memory_order_relaxed); rate > 0.0)
        blockDurationMs.store (1000.0 * numSamples / rate, std::memory_order_relaxed);
    blockTimeMs.store (juce::Time::getMillisecondCounterHiRes(), std::memory_order_release);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    rtStats.deviceRestarted();
    rt::resetForNewDevice();
    transport.sampleRate.store (device->getCurrentSampleRate());
    meters.rollingLog.setSampleRate (device->getCurrentSampleRate()); // (the audio thread isn't running yet: it may allocate)
    midiSync.deviceStarted (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(), device->getOutputLatencyInSamples());
    // what is heard comes out one output buffer + the driver's output latency after it was rendered (round 170d, as
    // the MIDI clock output counts it)
    outputCompensationMs.store (1000.0 * (device->getCurrentBufferSizeSamples() + juce::jmax (0, device->getOutputLatencyInSamples()))
                                / juce::jmax (1.0, device->getCurrentSampleRate()));
    // FL's fixed size buffers: 2 ms, or the device buffer with "Process maximum size buffers" (REA round 134)
    setFixedPluginBlock (device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());

    // the callback gets the open input channels only, in order: map device channel -> callback index
    const auto active = device->getActiveInputChannels();
    int slot = 0;
    for (int c = 0; c < LiveInput::maxChannels; ++c)
        inputSlot[(std::size_t) c] = active[c] ? slot++ : -1;
    // the same for the outputs (hardware output targets, REA round 156)
    const auto activeOut = device->getActiveOutputChannels();
    slot = 0;
    for (int c = 0; c < HardwareOutput::maxChannels; ++c)
        outputSlot[(std::size_t) c] = activeOut[c] ? slot++ : -1;
    hwOutput.clear();
    hwOutput.active.store (true);
    player.audioDeviceAboutToStart (device);
}

void AudioEngine::audioDeviceStopped()
{
    hwOutput.active.store (false);
    player.audioDeviceStopped();
}

// The Master's own target (its default: the graph's output as it is, on the first open pair) and the inserts'
// hardware outputs, added into the device channels they chose. A channel that is not open drops its sound.
void AudioEngine::routeOutputs (float* const* outputs, int numOutputs, int n) noexcept
{
    const auto slotOf = [this] (int c) { return c >= 0 && c < HardwareOutput::maxChannels ? outputSlot[(std::size_t) c] : -1; };

    if (const auto channel = masterOutChannel.load (std::memory_order_relaxed); channel != outputDefault && numOutputs > 0 && n <= HardwareOutput::capacity)
    {
        auto* l = masterScratch.data();
        auto* r = masterScratch.data() + HardwareOutput::capacity;
        const auto* srcR = numOutputs > 1 && outputs[1] != nullptr ? outputs[1] : outputs[0];
        if (outputs[0] == nullptr)
            return;
        std::copy (outputs[0], outputs[0] + n, l);
        std::copy (srcR, srcR + n, r);
        for (int k = 0; k < std::min (numOutputs, 2); ++k)
            if (outputs[k] != nullptr)
                std::fill (outputs[k], outputs[k] + n, 0.0f);

        const auto mode = (OutputMode) masterOutMode.load (std::memory_order_relaxed);
        const auto [first, count] = outputChannelsOf (channel, mode);
        if (count > 0)
        {
            const auto a = slotOf (first), b = count > 1 ? slotOf (first + 1) : -1;
            if (a >= 0 && a < numOutputs)
                addToOutput (l, r, mode, outputs[a], b >= 0 && b < numOutputs ? outputs[b] : nullptr, n);
        }
    }

    hwOutput.drainInto (outputs, numOutputs, slotOf, n);
}

std::vector<std::string> AudioEngine::getOutputChannelNames() const
{
    std::vector<std::string> names;

    if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr)
        for (const auto& n : device->getOutputChannelNames())
            if ((int) names.size() < HardwareOutput::maxChannels)
                names.push_back (n.toStdString());

    return names;
}

bool AudioEngine::isOutputChannelOpen (int channel) const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr && channel >= 0 && device->getActiveOutputChannels()[channel];
}

void AudioEngine::setTrackOutput (int track, int channel, OutputMode mode)
{
    if (track < 0 || track >= (int) song.mixer.size())
        return;

    if (track > 0 && channel < 0) // an insert without a target: the default (the Master only), nothing saved
        channel = outputDefault;

    editSong ([=] (Song& s) {
        s.mixer[(std::size_t) track].outputChannel = channel;
        s.mixer[(std::size_t) track].outputMode = mode;
    });

    openTargetedOutputs();
}

void AudioEngine::openHardwareInsertIo()
{
    openTargetedOutputs();
    if (hwinsert::returnsFromInput (song) && ! hasAudioInput())
        if (const auto result = openAudioInput(); result.issue != InputIssue::none)
            notify (result.issue, 0, result.detail);
}

void AudioEngine::openTargetedOutputs()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    const auto available = juce::jmin ((int) HardwareOutput::maxChannels, device->getOutputChannelNames().size());
    auto setup = deviceManager.getAudioDeviceSetup();
    auto wanted = device->getActiveOutputChannels();
    const auto before = wanted;

    for (const auto& t : song.mixer)
    {
        const auto [first, count] = outputChannelsOf (t.outputChannel, t.outputMode);
        for (int c = first; c < first + count; ++c)
            if (c < available)
                wanted.setBit (c);
    }
    // the hardware inserts' sends (round 164d; inside Patchers too, round 165c)
    hwinsert::forEachHardwareInsert (song, [&wanted, available] (const EffectSlot& fx, bool) {
        const auto [from, n] = outputChannelsOf (hwinsert::channelOf (fx.params[hwinsert::output]), hwinsert::outputModeOf (fx.params));
        for (int c = from; c < from + n; ++c)
            if (c < available)
                wanted.setBit (c);
    });

    if (wanted == before)
        return;

    setup.useDefaultOutputChannels = false;
    setup.outputChannels = wanted;
    deviceManager.setAudioDeviceSetup (setup, true);
    sendChangeMessage();
}

// --- hardware inserts anywhere in the song (round 165c) ------------------------------------------------------------

namespace
{
template <typename F>
void walkHardwareInserts (const PatcherDoc& doc, AudioEngine::HardwareInsertRef& ref, F&& f)
{
    for (int n = 0; n < (int) doc.nodes.size(); ++n)
    {
        const auto& node = doc.nodes[(std::size_t) n];
        ref.nodes.push_back (n);
        if (node.generator ? node.channel.instrument == InstrumentKind::patcher : node.effect.kind == EffectKind::patcher)
            walkHardwareInserts (node.generator ? node.channel.patcher : node.effect.patcher, ref, f);
        else if (! node.generator && node.effect.kind == EffectKind::hardwareInsert)
            f (ref);
        ref.nodes.pop_back();
    }
}
} // namespace

std::vector<AudioEngine::HardwareInsertRef> AudioEngine::getHardwareInserts() const
{
    std::vector<HardwareInsertRef> out;
    auto add = [&out] (const HardwareInsertRef& r) { out.push_back (r); };
    for (int t = 0; t < (int) song.mixer.size(); ++t)
        for (int s = 0; s < (int) song.mixer[(std::size_t) t].effects.size(); ++s)
        {
            const auto& fx = song.mixer[(std::size_t) t].effects[(std::size_t) s];
            HardwareInsertRef ref { false, t, s, {} };
            if (fx.kind == EffectKind::hardwareInsert)
                out.push_back (ref);
            else if (fx.kind == EffectKind::patcher)
                walkHardwareInserts (fx.patcher, ref, add);
        }
    for (int c = 0; c < (int) song.channels.size(); ++c)
        if (song.channels[(std::size_t) c].instrument == InstrumentKind::patcher)
        {
            HardwareInsertRef ref { true, c, 0, {} };
            walkHardwareInserts (song.channels[(std::size_t) c].patcher, ref, add);
        }
    return out;
}

EffectSlot* AudioEngine::locateHardwareInsert (Song& s, const HardwareInsertRef& ref)
{
    PatcherDoc* doc = nullptr;
    if (ref.channel)
    {
        if (ref.index < 0 || ref.index >= (int) s.channels.size() || s.channels[(std::size_t) ref.index].instrument != InstrumentKind::patcher)
            return nullptr;
        doc = &s.channels[(std::size_t) ref.index].patcher;
    }
    else
    {
        if (ref.index < 0 || ref.index >= (int) s.mixer.size() || ref.slot < 0 || ref.slot >= (int) s.mixer[(std::size_t) ref.index].effects.size())
            return nullptr;
        auto& fx = s.mixer[(std::size_t) ref.index].effects[(std::size_t) ref.slot];
        if (ref.nodes.empty())
            return fx.kind == EffectKind::hardwareInsert ? &fx : nullptr;
        if (fx.kind != EffectKind::patcher)
            return nullptr;
        doc = &fx.patcher;
    }
    for (std::size_t k = 0; k < ref.nodes.size(); ++k)
    {
        const auto n = ref.nodes[k];
        if (doc == nullptr || n < 0 || n >= (int) doc->nodes.size())
            return nullptr;
        auto& node = doc->nodes[(std::size_t) n];
        if (k + 1 == ref.nodes.size())
            return ! node.generator && node.effect.kind == EffectKind::hardwareInsert ? &node.effect : nullptr;
        doc = node.generator ? (node.channel.instrument == InstrumentKind::patcher ? &node.channel.patcher : nullptr)
                             : (node.effect.kind == EffectKind::patcher ? &node.effect.patcher : nullptr);
    }
    return nullptr;
}

std::shared_ptr<Effect> AudioEngine::runningHardwareInsert (const HardwareInsertRef& ref) const
{
    patcher::Graph* graph = nullptr;
    if (ref.channel)
        graph = patcher::graphOf (getChannelInstrument (ref.index).get());
    else
    {
        auto fx = getEffect (ref.index, ref.slot);
        if (ref.nodes.empty())
            return fx;
        graph = patcher::graphOf (fx.get());
    }
    for (std::size_t k = 0; k < ref.nodes.size() && graph != nullptr; ++k)
    {
        const auto n = ref.nodes[k];
        if (n < 0 || n >= graph->nodeCount())
            return nullptr;
        if (k + 1 == ref.nodes.size())
            return graph->effectAt (n);
        auto* next = patcher::graphOf (graph->effectAt (n).get());
        graph = next != nullptr ? next : patcher::graphOf (graph->instrumentAt (n).get());
    }
    return nullptr;
}

std::vector<AudioEngine::HardwareInsertRef> AudioEngine::staleHardwareInserts() const
{
    std::vector<HardwareInsertRef> out;
    const auto key = currentSetupKey();
    if (key.empty())
        return out;
    for (const auto& ref : getHardwareInserts())
        if (const auto* fx = locateHardwareInsert (const_cast<Song&> (song), ref))
            if (fx->enabled && hwinsert::isRouted (fx->params))
                if (const auto measured = hwinsert::measuredSetup (*fx); ! measured.empty() && measured != key)
                    out.push_back (ref);
    return out;
}

void AudioEngine::pingHardwareInserts (const std::vector<HardwareInsertRef>& refs)
{
    if (refs.empty())
        return;
    editSong ([refs] (Song& s) {
        for (const auto& ref : refs)
            if (auto* fx = locateHardwareInsert (s, ref))
                fx->params[hwinsert::ping] = fx->params[hwinsert::ping] >= 0.5f ? 0.0f : 1.0f; // (a momentary value: any change)
    }, false);
}

void AudioEngine::applyHardwareInsertPings()
{
    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return;
    const auto refs = getHardwareInserts();
    const auto key = currentSetupKey();

    // the device setup changed: the inserts measured for the old one (or, measured before LYNX kept the setup, for the
    // one that was running) want a new ping — a notice for the UI, nothing pinged by itself
    if (key != lastSetupKey)
    {
        const auto previous = std::exchange (lastSetupKey, key);
        if (! previous.empty() && ! refs.empty())
        {
            editSong ([refs, previous] (Song& s) {
                for (const auto& ref : refs)
                    if (auto* fx = locateHardwareInsert (s, ref); fx != nullptr && hwinsert::measuredSetup (*fx).empty()
                                                                  && hwinsert::isRouted (fx->params) && hwinsert::roundTripOf (fx->params[hwinsert::roundTrip]) > 0)
                        hwinsert::setMeasuredSetup (*fx, previous);
            }, false);
            hwNotice = staleHardwareInserts();
            if (! hwNotice.empty())
                sendChangeMessage();
        }
    }

    if (std::exchange (checkHardwareInserts, false) && hwNotice.empty())
    {
        hwNotice = staleHardwareInserts();
        if (! hwNotice.empty())
            sendChangeMessage();
    }

    // ping results: each one becomes its slot's round trip once (undoable, saved with the project), with this setup
    for (const auto& ref : refs)
    {
        const auto fx = runningHardwareInsert (ref);
        if (fx == nullptr)
            continue;
        const auto serial = juce::roundToInt (fx->controlOutput (hwinsert::pingSerial) * 1024.0f);
        const auto [it, fresh] = hwPingSerials.try_emplace (fx.get(), serial);
        if (fresh || it->second == serial)
            continue;
        it->second = serial;
        if (std::abs (fx->controlOutput (hwinsert::pingState) - 0.5f) >= 0.01f)
            continue; // (failed: the editor says why; the old round trip stays)
        const auto samples = hwinsert::roundTripOf (fx->controlOutput (hwinsert::pingSamples));
        editSong ([ref, samples, key] (Song& s) {
            if (auto* slot = locateHardwareInsert (s, ref))
            {
                slot->params[hwinsert::roundTrip] = hwinsert::roundTripValue (samples);
                hwinsert::setMeasuredSetup (*slot, key);
            }
        });
    }
    if (hwPingSerials.size() > 4 * refs.size() + 16) // (instances rebuilt meanwhile)
        hwPingSerials.clear();
}

// --- real-time bounce (round 165c) ----------------------------------------------------------------------------------

juce::String AudioEngine::startRealtimeBounce (double startTick)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr || transport.sampleRate.load() <= 0.0)
        return tr ("No audio device is open");
    if (transport.recording.load() || takeCount > 0)
        return tr ("Stop recording first");
    if (ping.isRunning())
        return tr ("A latency ping is running");
    if (bouncing)
        return tr ("A real-time bounce is already running");

    stop();
    bounceReturnTick = transport.positionTicks.load();
    bounceRestoreMode = song.mode != PlayMode::song;
    if (bounceRestoreMode)
        editSong ([] (Song& s) { s.mode = PlayMode::song; }, false);
    bouncing = true;
    updateLinearSong(); // (plays on past the song's end and through the loop region, like a linear recording)
    bounceStartLatency = getLatencySamples();
    bounceStartXruns = device->getXRunCount();
    meters.bounceTap.setSource (0); // (pushed only while the song plays: the first frame is the start's)
    transport.positionTicks.store (juce::jmax (0.0, startTick));
    transport.seekRequest.store (juce::jmax (0.0, startTick));
    play();
    sendChangeMessage();
    return {};
}

int AudioEngine::bounceDropouts() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    const auto xruns = device != nullptr ? juce::jmax (0, device->getXRunCount() - bounceStartXruns) : 0;
    return xruns + (meters.bounceTap.droppedFrames() > 0 ? 1 : 0);
}

void AudioEngine::finishRealtimeBounce()
{
    if (! bouncing)
        return;
    stop();
    meters.bounceTap.setSource (AudioCapture::off);
    bouncing = false;
    updateLinearSong();
    if (std::exchange (bounceRestoreMode, false))
        editSong ([] (Song& s) { s.mode = PlayMode::pattern; }, false);
    seek (bounceReturnTick);
    sendChangeMessage();
}
} // namespace lynx
