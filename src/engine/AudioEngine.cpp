#include "engine/AudioEngine.h"
#include <cstring> // (round 190nw: the module import's sample files)
#include "core/AutomationModes.h" // round 190pa (P21)
#include "core/AutoLook.h"
#include "core/NewClipColours.h"
#include "core/SessionMixer.h"

#include "core/TransistorBass.h"
#include "core/AraRegions.h"
#include "core/RackOps.h"
#include "core/FlPluginDb.h"
#include "core/HarmorResynth.h"
#include "core/BrowserSend.h" // round 190iz (FL's preview length)
#include "core/LiveNote.h"
#include "core/TrackClips.h"
#include "core/TrackOnly.h" // round 190iu
#include "core/VideoPlayer.h"
#include "core/TempoMap.h"
#include "engine/ProjectFile.h"
#include "engine/RemoteError.h"
#include "engine/RemoteHistory.h"
#include "core/Sha256.h"

#include "core/Comping.h"
#include "core/CountIn.h"
#include "core/EdisonFx.h"
#include "core/FlSettings.h"
#include "core/MixerOps.h" // (round 190mv: keepCurrentLast)
#include "core/LiveSession.h"

#include "core/SessionIds.h"
#include "core/StandIns.h"
#include "engine/SessionPack.h"

#include "core/ClipOps.h"
#include "core/PlaylistOps.h"
#include "core/LayoutStyle.h"
#include "core/DetailChain.h"
#include "core/SessionOps.h"
#include "core/MarkerPlan.h"
#include "core/MidiOut.h"
#include "engine/AudioLogger.h"

#include "core/HardwareInsert.h"
#include "core/Patcher.h"
#include "core/ValueMath.h"
#include "platform/Paths.h"
#include "engine/FlStudio.h"

#include <limits>
#include <map>
#include <thread>
#if defined(__GLIBC__)
 #include <malloc.h>
#endif

#include "engine/Text.h"
#include "ui/I18n.h"

#include "core/FlpImport.h"
#include "core/AlsImport.h"
#include "core/DawProject.h" // round 190jf (B22)
#include "core/GeneratorPresets.h"
#include "core/FlmImport.h"
#include "core/ModuleImport.h"
#include "core/BandImport.h" // round 190ic: GarageBand projects
#include "core/BlxImport.h"  // round 190jr: BandLab projects (.blx)
#include "engine/ProjectPackage.h"
#include "core/LoadTrace.h"
#include "core/Instruments.h"
#include "core/Denormals.h"
#include "core/Dither.h"
#include "core/GrossBeat.h"
#include "core/ProjectSafety.h"
#include "core/SoftClip.h"
#include "core/Scheduler.h"
#include "core/TrackStates.h"
#include "engine/ChannelRackProcessor.h"
#include "engine/DllAsioDevice.h"
#include "engine/PipeWireDevice.h"
#include "platform/LinuxRealtime.h"
#include "engine/PowerHints.h"
#include "platform/PowerThrottling.h"
#include "engine/Metronome.h"
#include "core/NoteTools.h"
#include "core/VfxScript.h"
#include "core/SamplePrecompute.h"
#include "engine/PreviewPlayer.h"
#include "engine/RenderStream.h"
#include "engine/PluginSandbox.h"
#include "core/PluginPresets.h"
#include "flp/Flp.h"

#if JUCE_WINDOWS
 #include <windows.h>
#elif JUCE_MAC
 #include <mach/mach.h>
 #include <mach/mach_time.h>
 #include <mach/thread_policy.h>
 #include <pthread.h>
 #include <sys/qos.h>
 #include <sys/sysctl.h>
#elif JUCE_LINUX || JUCE_BSD
 #include <pthread.h>
 #include <sched.h>
#endif

namespace lynx
{
namespace
{
std::unordered_set<std::uint64_t> remoteFreshTargets (const Song& before, const Song& after)
{
    std::unordered_set<std::uint64_t> existing, fresh;
    for (const auto& channel : before.channels) existing.insert (channel.instance.id);
    for (const auto& track : before.mixer)
        for (const auto& effect : track.effects) existing.insert (effect.instance.id);
    for (const auto& channel : after.channels)
        if (! existing.contains (channel.instance.id)) fresh.insert (channel.instance.id);
    for (const auto& track : after.mixer)
        for (const auto& effect : track.effects)
            if (! existing.contains (effect.instance.id)) fresh.insert (effect.instance.id);
    return fresh;
}

void adoptCapturedRemoteDevices (Song& model, RackState& published, Song&& captured)
{
    // Capturing changes only saved hosted-state bytes. Keep the model and reuse baseline equal to the verified
    // running devices without publishing, changing history or capturing a vendor a second time.
    remote_history::mirrorCapturedHostedStates (captured, published.song);
    model.channels = std::move (captured.channels);
    model.mixer = std::move (captured.mixer);
}
}

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

// --- engine/PowerHints.h (rollup 0.1.28, REA round 190hb actions 3 and 4) -------------------------------------------
namespace powerhints
{
std::uint32_t keepAwakeBegin() noexcept
{
   #if JUCE_WINDOWS
    return (std::uint32_t) SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
   #else
    return 0;
   #endif
}

void keepAwakeEnd (std::uint32_t previous) noexcept
{
   #if JUCE_WINDOWS
    // (the state before, or plain ES_CONTINUOUS when there was none: the system may sleep again)
    SetThreadExecutionState (previous != 0 ? (EXECUTION_STATE) previous : ES_CONTINUOUS);
   #else
    (void) previous;
   #endif
}

int lowerCurrentThread() noexcept
{
   #if JUCE_WINDOWS
    const auto before = GetThreadPriority (GetCurrentThread());
    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    return before == THREAD_PRIORITY_ERROR_RETURN ? THREAD_PRIORITY_NORMAL : before;
   #else
    return 0;
   #endif
}

void restoreCurrentThread (int previous) noexcept
{
   #if JUCE_WINDOWS
    SetThreadPriority (GetCurrentThread(), previous);
   #else
    (void) previous;
   #endif
}

void lowerWorkerThread()
{
    (void) lowerCurrentThread();
}

void enableCompositorMmcss() noexcept
{
   #if JUCE_WINDOWS
    if (juce::SystemStats::getEnvironmentVariable ("LYNX_DWM_MMCSS", {}) == "0")
        return;
    using EnableMmcss = HRESULT (WINAPI*) (BOOL);
    if (auto* dwm = LoadLibraryW (L"dwmapi.dll"))
        if (auto enable = (EnableMmcss) (void*) GetProcAddress (dwm, "DwmEnableMMCSS"))
            enable (TRUE);
   #endif
}
} // namespace powerhints

namespace
{
#if JUCE_WINDOWS
// what the audio threads reached (round toolchain190gt, REA 190hc: LYNX's were seen at dynamic priority 15 in some runs
// and 24-25 in others, Live's always at 26): threads raised, how many joined MMCSS at CRITICAL, their thread priority
std::atomic<int> rtThreadsRaised { 0 }, rtThreadsMmcss { 0 }, rtThreadsCritical { 0 };
std::atomic<int> rtPriorityMin { 1000 }, rtPriorityMax { -1000 };

// The process priority class (round toolchain190gt, REA 190hb): Windows' WinMM driver (wdmaud2.drv) raises a process
// that opens a WinMM device to HIGH (and asks REALTIME), FL's and LYNX's alike, so the UI, decoders, prefetch and plugin
// scans ran above every normal program; FL itself asks ABOVE_NORMAL. LYNX_PRIORITY_CLASS=above asks ABOVE_NORMAL when a
// device starts (and again 2 s later, after the MIDI ports), relying on MMCSS for the audio threads. Default: left as
// Windows sets it — measured with --rtstress (Exoplanet, 10 ms buffer, 3 x 3000 callbacks each, loaded PC): HIGH 0
// callbacks over the deadline, ABOVE_NORMAL 1, the same average load; a 64-sample run was not possible on this PC.
std::atomic<unsigned long> priorityClassFound { 0 }, priorityClassNow { 0 };
void choosePriorityClass() noexcept
{
    static const auto keep = juce::SystemStats::getEnvironmentVariable ("LYNX_PRIORITY_CLASS", {}) != "above";
    const auto found = GetPriorityClass (GetCurrentProcess());
    if (priorityClassFound.load (std::memory_order_relaxed) == 0 || found != ABOVE_NORMAL_PRIORITY_CLASS)
        priorityClassFound.store (found, std::memory_order_relaxed);
    if (! keep && found != ABOVE_NORMAL_PRIORITY_CLASS)
        SetPriorityClass (GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    priorityClassNow.store (GetPriorityClass (GetCurrentProcess()), std::memory_order_relaxed);
}
juce::String priorityClassName (unsigned long c)
{
    switch (c)
    {
        case IDLE_PRIORITY_CLASS: return "idle";
        case BELOW_NORMAL_PRIORITY_CLASS: return "below normal";
        case NORMAL_PRIORITY_CLASS: return "normal";
        case ABOVE_NORMAL_PRIORITY_CLASS: return "above normal";
        case HIGH_PRIORITY_CLASS: return "high";
        case REALTIME_PRIORITY_CLASS: return "realtime";
        default: return c == 0 ? juce::String ("not seen") : juce::String::toHexString ((juce::int64) c);
    }
}
#endif

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
    // FL's audio worker threads raise their MMCSS priority to "high" (FLEngine_x64.dll, REA round 87); Live's to "critical"
    // (REA round 190hc: its 22 audio threads sat at dynamic priority 26 in every snapshot), which LYNX uses since round
    // toolchain190gt
    using AvPriority = int (__stdcall*) (void*, int);
    static const auto avPriority = [] () -> AvPriority {
        if (auto* avrt = LoadLibraryW (L"avrt.dll"))
            return reinterpret_cast<AvPriority> (reinterpret_cast<void*> (GetProcAddress (avrt, "AvSetMmThreadPriority")));
        return nullptr;
    }();
    unsigned long task = 0;

    if (auto* handle = avSet != nullptr ? avSet (L"Pro Audio", &task) : nullptr)
    {
        rtThreadsMmcss.fetch_add (1, std::memory_order_relaxed);
        if (avPriority != nullptr && avPriority (handle, 2 /* AVRT_PRIORITY_CRITICAL */) != 0)
            rtThreadsCritical.fetch_add (1, std::memory_order_relaxed);
        else if (avPriority != nullptr)
            avPriority (handle, 1 /* AVRT_PRIORITY_HIGH */);
    }
    else
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    // and full speed while LYNX is in the background (no EcoQoS: platform/PowerThrottling.h, round toolchain190gt)
    power::optOutOfPowerThrottling();
    const auto reached = GetThreadPriority (GetCurrentThread());
    rtThreadsRaised.fetch_add (1, std::memory_order_relaxed);
    for (auto low = rtPriorityMin.load(); reached < low && ! rtPriorityMin.compare_exchange_weak (low, reached);) {}
    for (auto high = rtPriorityMax.load(); reached > high && ! rtPriorityMax.compare_exchange_weak (high, reached);) {}
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

#if JUCE_MAC
// The calling thread becomes a time-constraint (real-time) thread with the device's buffer as its period: about half of
// it to compute, the whole of it as the deadline. Apple's audio workgroups take only such threads (os_workgroup_join
// fails with EINVAL before; research round 190gs B1).
void becomeTimeConstraintThread (double blockMs) noexcept
{
    mach_timebase_info_data_t timebase {};
    if (mach_timebase_info (&timebase) != KERN_SUCCESS || timebase.numer == 0)
        return;
    const auto ticks = [&] (double ms) { return (uint32_t) (ms * 1.0e6 * (double) timebase.denom / (double) timebase.numer); };
    const auto period = juce::jlimit (0.5, 50.0, blockMs); // (above 50 ms the kernel refuses the policy)
    thread_time_constraint_policy_data_t policy { ticks (period), ticks (0.5 * period), ticks (period), 1 };
    thread_policy_set (pthread_mach_thread_np (pthread_self()), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t) &policy,
                       THREAD_TIME_CONSTRAINT_POLICY_COUNT);
}
#endif

// The render-ahead thread (round 190gq, core/RenderAhead.h): the workers' priority, except on macOS, where it is not
// deadline work and must never join the device's Audio Workgroup (round 190gt joins the real-time workers): the
// interactive QoS class only.
void raiseRenderAheadThreadPriority()
{
   #if JUCE_MAC
    pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);
   #else
    raiseAudioThreadPriority();
   #endif
}

// Round 190gq (190gs Q7): the cores that give a block its full speed — physical cores (SMT siblings share one core's
// units), and on hybrid CPUs only the performance class (a block waits for its slowest worker). 0 = unknown.
int performanceCores()
{
   #if JUCE_WINDOWS
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx (RelationProcessorCore, nullptr, &bytes);
    if (bytes == 0)
        return 0;
    std::vector<std::uint8_t> buffer (bytes);
    if (! GetLogicalProcessorInformationEx (RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX> (buffer.data()), &bytes))
        return 0;
    int best = -1, count = 0;
    for (DWORD at = 0; at < bytes;)
    {
        const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*> (buffer.data() + at);
        if (info->Relationship == RelationProcessorCore)
        {
            const auto cls = (int) info->Processor.EfficiencyClass; // (higher = faster; all 0 on a non-hybrid CPU)
            if (cls > best)
            {
                best = cls;
                count = 0;
            }
            if (cls == best)
                ++count;
        }
        if (info->Size == 0)
            break;
        at += info->Size;
    }
    return count;
   #elif JUCE_MAC
    int perf = 0;
    auto size = sizeof (perf);
    if (sysctlbyname ("hw.perflevel0.physicalcpu", &perf, &size, nullptr, 0) == 0 && perf > 0)
        return perf;
    int physical = 0;
    size = sizeof (physical);
    return sysctlbyname ("hw.physicalcpu", &physical, &size, nullptr, 0) == 0 ? physical : 0;
   #elif JUCE_LINUX || JUCE_BSD
    // sysfs: one (package, core) pair per physical core; on an Intel hybrid CPU only the cpu_core PMU's CPUs
    std::vector<std::pair<int, int>> cores;
    const juce::File root ("/sys/devices/system/cpu");
    juce::String hybrid = juce::File ("/sys/devices/cpu_core/cpus").loadFileAsString().trim(); // e.g. "0-15"
    auto inHybridSet = [&hybrid] (int cpu) {
        if (hybrid.isEmpty())
            return true;
        for (const auto& range : juce::StringArray::fromTokens (hybrid, ",", {}))
        {
            const auto from = range.upToFirstOccurrenceOf ("-", false, false).getIntValue();
            const auto to = range.contains ("-") ? range.fromFirstOccurrenceOf ("-", false, false).getIntValue() : from;
            if (cpu >= from && cpu <= to)
                return true;
        }
        return false;
    };
    for (int cpu = 0; cpu < 1024; ++cpu)
    {
        const auto dir = root.getChildFile ("cpu" + juce::String (cpu));
        if (! dir.isDirectory())
            break;
        const auto core = dir.getChildFile ("topology/core_id").loadFileAsString().trim();
        const auto package = dir.getChildFile ("topology/physical_package_id").loadFileAsString().trim();
        if (core.isEmpty() || ! inHybridSet (cpu))
            continue;
        const std::pair<int, int> id { package.getIntValue(), core.getIntValue() };
        if (std::find (cores.begin(), cores.end(), id) == cores.end())
            cores.push_back (id);
    }
    return (int) cores.size();
   #else
    return 0;
   #endif
}

int workerCount()
{
    // diagnostics: LYNX_WORKERS=n fixes the number of extra threads
    if (const auto forced = juce::SystemStats::getEnvironmentVariable ("LYNX_WORKERS", {}); forced.isNotEmpty())
        return juce::jlimit (0, 31, forced.getIntValue());

    // round 190gq (190gs Q7): the audio thread and the workers on the performance cores, one per physical core, the
    // audio thread's own included (the SMT siblings and the slower cores are left to the UI and the rest). Before:
    // logical CPUs − 2 (7 workers on 6 cores / 12 threads). Unknown topology: as before.
    if (const auto cores = performanceCores(); cores > 0)
        return juce::jlimit (0, 7, cores - 1);
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
    // (an engine without an audio device renders offline — exports, bounces, freezes: its workers run below normal
    // priority as FL's export threads do, rollup 0.1.28 / REA 190hb; the live engine's workers get the audio priority)
    : workers (std::make_unique<WorkerPool> (workerThreads >= 0 ? workerThreads : workerCount(),
                                             openAudioDevice ? std::function<void()> (raiseAudioThreadPriority)
                                                             : std::function<void()> (powerhints::lowerWorkerThread))),
      plugins (userPluginList),
      song (Song::makeDefault())
{
    // Harmor's resynthesis (round natives190kj): the sample a part names, found as FL finds it (path variables, then the
    // file re-rooted to this PC's FL folders) and decoded by the core (WAV, FL's Ogg-in-WAV, Ogg Vorbis, WavPack)
    harmor::setSampleLoader ([] (const std::string& saved, harmor::SampleData& out) {
        const auto& fl = flStudioAccess();
        const auto path = locateFlSample (saved, fl.roots, {}, fl.fileExists);
        return ! path.empty() && fl.readFile && harmor::readWavMono (fl.readFile (path), out);
    });
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
            rackProcessor->setProfiling (true); // per-unit CPU for the CPU meter and the glitch log (both renderers: 190gq)
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
    deviceManager.getAvailableDeviceTypes(); // the standard types first (round 190hd: its own trace line)
    mark ("  engine: standard device types (the prewarmed ASIO device handed over)");
    if (auto lynxAsio = makeBuiltInLynxAsioType())
        deviceManager.addAudioDeviceType (std::move (lynxAsio));
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
    auto failed = deviceManager.initialise (0, 2, saved.get(), true).isNotEmpty();
    // (round 190hd) a device made ahead without JUCE's start / stop at init that then failed: once more the usual way
    if (failed && saved != nullptr && asioPrewarmQuickInit())
        failed = deviceManager.initialise (0, 2, saved.get(), true).isNotEmpty();
    if (failed && saved != nullptr)
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
    configureMidiOutputs();
    midiSync.sendAllNotesOff(); // FL's "Send all notes off": on init and after a render
    if (! platform::testMode()) // (round 190gl: Link on as it was left; never in a test run, which must not join a session)
        applyLinkPrefs();
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
    if (rackProcessor != nullptr) // (round 190gq: the render-ahead thread stops before the snapshots are freed)
        rackProcessor->haltRenderAhead();
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
    auto rack = graph.addNode (std::make_unique<ChannelRackProcessor> (transport, rackExchange, liveEvents, midiEvents, meters, workers.get(),
                                                                       raiseRenderAheadThreadPriority));
    rackProcessor = dynamic_cast<ChannelRackProcessor*> (rack->getProcessor());
    if (rackProcessor != nullptr)
    {
        rackProcessor->setRenderAheadTargets (&renderAheadTargets); // (round 190gq)
        rackProcessor->setLiveInput (&liveInput);
        rackProcessor->setHardwareOutput (&hwOutput);
        rackProcessor->setPerformance (performanceLive.get());
        rackProcessor->getRenderer().reserveFor (lastPublishedShape); // (round 190gn: a song published before the graph)
        rackProcessor->getRenderer().setHardwareMidiPorts (hardwareMidiPorts); // (round 190jl)
        rackProcessor->getRenderer().setOwnNoteChannels (ownNoteChannels); // (round 190ks)
        rackProcessor->getRenderer().setCurrentSource (currentSource); // (round 190mv)
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

RenderStream::Setup AudioEngine::streamSetup (const RenderOptions& options, int latency)
{
    RenderStream::Setup setup;
    setup.trim = (std::int64_t) juce::jmax (0, options.trimSamples + (options.trimPdc ? latency : 0));
    setup.musicSamples = (std::int64_t) (options.seconds * options.sampleRate) + setup.trim;
    // (round 190gh: an exact length renders that much after the trim, the tail's silence does not end it)
    setup.totalSamples = options.exactSamples > 0 ? setup.trim + options.exactSamples
                                                  : setup.musicSamples + (std::int64_t) (juce::jmax (0.0, options.tailSeconds) * options.sampleRate);
    setup.exact = options.exactSamples > 0;
    setup.sampleRate = options.sampleRate;
    setup.keepOvers = options.keepOvers;
    setup.ditherBits = options.ditherBits;
    setup.ditherShaping = options.ditherShaping;
    setup.output = options.output;
    setup.capture = options.capture;
    return setup;
}

float AudioEngine::renderOffline (const RenderOptions& options)
{
    // round 190po: the render's thread (the main thread of `LYNX --export`, an Export window's or a bounce's own thread)
    // at the x87's 64-bit precision, as the rack's workers: the same samples whichever thread renders (core/Denormals.h)
    const ScopedExtendedPrecision precision;
    offlineRendersRunning().fetch_add (1, std::memory_order_acq_rel);
    const juce::ScopeGuard rendered { [] { offlineRendersRunning().fetch_sub (1, std::memory_order_acq_rel); } };
    // (round 190mp: FL's Half speed is a listening state: an export renders at the song's own speed)
    const auto speedBefore = transport.playSpeed.exchange (1.0, std::memory_order_relaxed);
    const juce::ScopeGuard speedBack { [this, speedBefore] { transport.playSpeed.store (speedBefore, std::memory_order_relaxed); } };
    const auto sampleRate = options.sampleRate;
    const auto& progress = options.progress;
    auto* shared = options.shared;

    constexpr int blockSize = renderBlockSize;
    // "HQ for all plugins" (REA round 134): engines with FL render-quality settings use them (Sytrus) and HQ switches
    // are forced on; without it the export keeps the playback quality
    setOfflineRendering (options.hqAllPlugins);
    setRenderInProgress (true); // (whatever the quality: LYNX Soundfont waits for a font that is still loading, issue #138)
    setHqForAllPlugins (options.hqAllPlugins);
    setPluginsRenderMode (true); // and hosted plugins are told an export is running (FL: "notify about rendering mode")
    transport.sampleRate.store (sampleRate);
    // Round 190ou: a render plays from the song's start at the song's tempo, as on a new engine. A second render on the
    // same engine started where the first had stopped (the transport kept its end position, past a pattern's end: a
    // silent file) and at the tempo the song's tempo automation had reached. The engine's position comes back after.
    const auto positionBefore = transport.positionTicks.exchange (0.0);
    transport.bpm.store (song.bpm);
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
    // round 190kx: an ARA plugin renders what it has analysed: the render waits (at most 3 minutes) until its analysis of
    // the clips is done (a live engine's has long been; a fresh export engine's instances analyse now)
    if (araHost != nullptr)
    {
        const auto onMessageThread = juce::MessageManager::existsAndIsCurrentThread();
        const auto start = juce::Time::getMillisecondCounterHiRes();
        juce::Thread::sleep (onMessageThread ? 0 : 300); // (the heartbeat's first look)
        while (araHost->analysing() && juce::Time::getMillisecondCounterHiRes() - start < 180000.0)
            juce::Thread::sleep (50);
        lastRenderStats.araWaitMs = juce::Time::getMillisecondCounterHiRes() - start;
    }
    int latency = getLatencySamples();
    if (options.pluginWarmUp)
        lastRenderStats.warmUp = warmUpPlugins (options.warmUp, sampleRate, blockSize, progress, latency);

    play();

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    // what is done after the graph (round 190gj: RenderStream, the same code as the single-pass stems' files), with the
    // PDC trim of the latency the plugins report once they have settled
    auto setup = streamSetup (options, latency);
    lastRenderStats.trimmedSamples = (int) setup.trim;
    const auto musicSamples = setup.musicSamples;
    const auto totalSamples = setup.totalSamples;
    if (shared != nullptr)
    {
        setup.output = nullptr;
        setup.capture = nullptr;
    }
    RenderStream stream (std::move (setup));
    bool cancelled = lastRenderStats.warmUp.cancelled;
    if (shared != nullptr && ! cancelled)
        shared->start (latency);

    vfx::script::setSongEnded (false);
    for (std::int64_t done = 0; ! cancelled;)
    {
        const auto n = shared != nullptr ? juce::jmin (blockSize, shared->nextBlock (done)) : stream.blockAt (done, blockSize);
        if (n <= 0)
            break;
        // the tail: the song has ended (VFX Script sees the transport stopped, like FL's render; round 167e)
        if (done >= musicSamples)
            vfx::script::setSongEnded (true);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
        view.clear();
        const auto before = juce::Time::getMillisecondCounterHiRes();
        graph.processBlock (view, midi);
        const auto afterGraph = juce::Time::getMillisecondCounterHiRes();
        lastRenderStats.graphMs += afterGraph - before;

        if (shared != nullptr)
            shared->block (view, done, n);
        else
            stream.block (view, done, n);

        transport.positionTicks.store (transport.positionTicks.load() + n * transport.ticksPerSample());

        // in the tail: half a second of silence ends it
        if (stream.endedBySilence())
            break;

        // (a shared render reports its own progress, and is cancelled by ending its blocks)
        if (shared == nullptr && progress && (done / blockSize) % 32 == 0
            && ! progress ((double) done / (double) juce::jmax<std::int64_t> (1, totalSamples)))
        {
            cancelled = true;
            break;
        }
        done += n;
        if (stream.hasEnded())
            break;
    }
    lastRenderStats.writeMs = stream.getWriteMs();
    const auto peak = stream.getPeak();

    stop();
    vfx::script::setSongEnded (false);
    // Round 190ou: the rack sees the stop, as playback's next block shows it (the notes released, the note tools reset):
    // a render after this one starts playing from a stopped transport, as on a new engine (one discarded block)
    {
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, blockSize);
        view.clear();
        midi.clear();
        graph.processBlock (view, midi);
    }
    transport.positionTicks.store (positionBefore);
    transport.bpm.store (song.bpm);

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

    // Issue #138 (round 190hn): a hosted instrument gets at least `hostedInstrumentSeconds` of audio. The export's
    // instances are new ones (FL renders with the instances that have been playing since the project opened), and a
    // hardware emulator boots its firmware in the audio it processes, not in wall time: gearmulator's JE-8086 holds MIDI
    // back for its first ~0.8 s of emulated time ("the firmware drops what arrives while it boots") and then plays the
    // restored patch in at MIDI speed. A second of warm-up left a short note at tick 0 meeting a synth still loading its
    // patch: a silent export, the same note a bar later fine. (Plugins that are ready at once only cost the time it
    // takes them to process the extra silence, a few ms for most.)
    constexpr double hostedInstrumentSeconds = 3.0;
    auto wait = settings;
    // Round 190jb: a unit lent by the live engine (it renders with the instances that play, as FL) has booted long ago;
    // when every hosted plugin of the render is one, the warm-up only lets the start's values settle (FL has none)
    auto lent = [this] (const void* unit) { return std::find (lentUnits.begin(), lentUnits.end(), unit) != lentUnits.end(); };
    auto allLent = true;
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        if (const auto kind = song.channels[i].instrument; kind == InstrumentKind::plugin || kind == InstrumentKind::patcher)
            allLent = allLent && lent (getChannelInstrument ((int) i).get());
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
            if (const auto kind = song.mixer[t].effects[s].kind; kind == EffectKind::plugin || kind == EffectKind::patcher)
                allLent = allLent && lent (getEffect ((int) t, (int) s).get());
    if (allLent)
    {
        wait.minAudioSeconds = 0.0;
        wait.minWallSeconds = 0.0;
        wait.stableWallSeconds = juce::jmin (wait.stableWallSeconds, 0.05);
        wait.maxWallSeconds = juce::jmin (wait.maxWallSeconds, 2.0);
    }
    else if (std::any_of (song.channels.begin(), song.channels.end(), [] (const ChannelDef& c) { return c.instrument == InstrumentKind::plugin; }))
        wait.minAudioSeconds = juce::jmax (wait.minAudioSeconds, hostedInstrumentSeconds);

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
        wait, sampleRate,
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

void AudioEngine::getPublishedLatencies (std::vector<int>& channels, std::vector<std::vector<int>>& slots) const
{
    // RackState::latencies: the instruments, then the slots of every insert in order (buildPdc's layout)
    const auto& shape = lastPublishedShape;
    channels.clear();
    slots.assign (shape.effects.size(), {});
    auto next = shape.latencies.begin();
    for (std::size_t c = 0; c < shape.instruments.size() && next != shape.latencies.end(); ++c)
        channels.push_back (*next++);
    for (std::size_t t = 0; t < shape.effects.size(); ++t)
        for (std::size_t s = 0; s < shape.effects[t].size() && next != shape.latencies.end(); ++s)
            slots[t].push_back (*next++);
}

void AudioEngine::setRenderTaps (RackRenderer::RenderTaps* taps) noexcept
{
    if (rackProcessor != nullptr)
        rackProcessor->getRenderer().setRenderTaps (taps);
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
    preparePlay();
    transport.playing.store (true);
}

void AudioEngine::preparePlay() noexcept
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
    transport.linkWaiting.store (false); // (round 190gl: a start waiting for the Link session's phase is cancelled)
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
    waitFilter.store ((options.recordAutomation ? 1 : 0) | (options.recordNotes ? 2 : 0) | (options.recordAudio ? 4 : 0) | (options.recordClips ? 8 : 0));
    waitRespects.store (options.waitRespectFilter);
    updateLinearSong();
    refreshLowLatency(); // (round 190aq: the option switched while an insert is live)
    sendChangeMessage();
}

void AudioEngine::startPlayback()
{
    if (! transport.recording.load())
    {
        // FL's Wait for input with "Only when recording" off (round 190ld): Play alone waits for the first note too
        if (recordOptions.waitForInput && ! recordOptions.waitOnlyWhenRecording && ! transport.playing.load())
        {
            preparePlay();
            waitingForInput.store (true);
            sendChangeMessage();
            return;
        }
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
        const auto punching = recordOptions.punchLoopRegion && song.mode == PlayMode::song && song.hasLoop()
                              && (recordOptions.punchIn || recordOptions.punchOut);
        const auto ends = recordloop::punchEnds (here, song.loopStart, song.loopEnd, punching, recordOptions.punchIn, recordOptions.punchOut,
                                                 std::numeric_limits<std::int64_t>::max() / 4); // (round 190lx: in / out each on its own)
        const auto in = ends.in, out = ends.out;
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
            if (ends.stopsAtOut)
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

    // (round 190gl: as the reference DAW, no count-in while Link is on: the start waits for the session's phase instead)
    if (recordOptions.countInBars > 0 && ! abletonLink.isEnabled())
    {
        // the notes played during the count-in belong to this take (round 170d)
        const auto length = countInLength (song, recordOptions.countInBars, startAt);
        takeCatch = {};
        takeCatch.start = startAt;
        takeCatch.window = recordOptions.catchWindow;
        takeCatch.songMode = song.mode == PlayMode::song;
        if (! song.patterns.empty())
            takeCatch.wrap = countInWrap (length, song.patterns[(std::size_t) juce::jlimit (0, (int) song.patterns.size() - 1, song.currentPattern)]
                                                      .effectiveLength (song.meter()));
        transport.countIn.store ((double) length);
        sendChangeMessage();
        return;
    }

    play();
}

void AudioEngine::startOnInput (WaitInput kind) noexcept
{
    if (! waitingForInput.load (std::memory_order_relaxed))
        return;
    // FL's "Respect recording filter" (round 190ld; REA 190oa R4): only while recording; Audio in the filter lets any
    // input start it, the tapper always does. (FL's own test for clips never matches — a slip in FL; LYNX follows the
    // filter's Clips switch.)
    if (waitRespects.load (std::memory_order_relaxed) && transport.recording.load (std::memory_order_relaxed) && kind != WaitInput::tapper)
    {
        const auto filter = waitFilter.load (std::memory_order_relaxed);
        if ((filter & 4) == 0 && (filter & (int) kind) == 0)
            return;
    }
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

void AudioEngine::copyScopeSide (float* destination, int count) const noexcept
{
    count = juce::jmin (count, scopeSize);
    auto r = (scopeWrite.load (std::memory_order_acquire) - count + scopeSize) % scopeSize;
    for (int i = 0; i < count; ++i)
    {
        destination[i] = scopeSide[(std::size_t) r].load (std::memory_order_relaxed);
        r = (r + 1) % scopeSize;
    }
}

void AudioEngine::previewFile (const juce::File& file)
{
    previewFile (file, 0.0, true);
}

void AudioEngine::setPreviewRange (const SampleData* sample)
{
    if (preview == nullptr)
        return;
    if (sample == nullptr || (previewFull && previewStartFraction <= 0.0))
    {
        preview->setBrowserRange (0, 0, false);
        return;
    }
    const auto r = browserpreview::range ((std::int64_t) sample->frames(), sample->sampleRate, previewStartFraction, previewFull);
    preview->setBrowserRange (r.start, r.capped ? r.end : 0, r.capped);
}

void AudioEngine::previewFile (const juce::File& file, double startFraction, bool full)
{
    previewStartFraction = startFraction; // (round 190iz: kept for the in-time preview prepared on the worker)
    previewFull = full || fullSamplePreview;
    ++previewSerial; // (a preview still being prepared is dropped)
    previewTiming = {};
    if (preview == nullptr)
        return;
    auto sample = file.existsAsFile() ? samples.load (file.getFullPathName()) : nullptr;
    if (! previewInTime || sample == nullptr)
    {
        setPreviewRange (sample.get());
        preview->playBrowser (std::move (sample)); // FL's preview: the file as it is, at once
        return;
    }

    // "Preview in time" (round 190aq, core/PreviewTempo.h): the file's tempo from its ACID beats or its name (cheap,
    // here), else from LYNX's tempo detection (on the worker); a file that can have none plays as it always did
    const auto path = file.getFullPathName();
    const auto name = file.getFileName().toStdString();
    const auto external = transport.overrideBpm(); // (an external clock's or the Link session's tempo)
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
        setPreviewRange (sample.get());
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
            setPreviewRange (sample.get());
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
            setPreviewRange (r.out.get());
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
    setPreviewRange (sample.get());
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

void AudioEngine::previewScrub (std::shared_ptr<const SampleData> sample, std::int64_t startFrame)
{
    if (preview != nullptr)
        preview->playScrub (std::move (sample), startFrame, 1.0f);
}

void AudioEngine::setPreviewScrubSpeed (double speed) noexcept
{
    if (preview != nullptr)
        preview->setScrubSpeed (speed);
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

void AudioEngine::markLiveNote (int key, int velocity127, bool on) noexcept
{
    if (key < 0 || key > 127)
        return;
    auto& v = liveNoteVelocities[(std::size_t) key];
    if (on)
        v.store ((std::uint8_t) std::clamp (velocity127, 0, 127), std::memory_order_relaxed);
    liveNote.store (livenote::pack (liveNote.load (std::memory_order_relaxed), key, (int) v.load (std::memory_order_relaxed), on), std::memory_order_relaxed);
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
    return rackProcessor != nullptr ? rackProcessor->soundingKeys (channel) : NoteMask {};
}

int AudioEngine::voicesSounding() const noexcept
{
    return rackProcessor != nullptr ? rackProcessor->voicesSounding() : 0;
}

std::uint32_t AudioEngine::noteStrikes (int channel) const noexcept
{
    return rackProcessor != nullptr ? rackProcessor->noteStrikes (channel) : 0u;
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
        markLiveNote (key, juce::roundToInt (velocity * 127.0f), on); // (the hint bar's "Note on", round 190ih)
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

void AudioEngine::sendGuiNote (int channel, int key, float velocity, bool on)
{
    key = juce::jlimit (0, 127, key);
    if (on)
        startOnInput(); // ("Wait for input": a pad hit starts the take, as a played key does)
    markLiveNote (key, juce::roundToInt (velocity * 127.0f), on);
    NoteEvent e;
    e.type = on ? NoteEvent::Type::on : NoteEvent::Type::off;
    e.channel = channel;
    e.key = key;
    e.velocity = velocity;
    liveEvents.push (e);
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto taking = transport.recording.load() && (transport.playing.load() || isCountingIn());
    logPlayedNote (channel, key, velocity, on, transport.playing.load() ? recordTickFor (now) : -1.0, taking);
    if (taking)
        uiRecorded.push_back ({ recordTickFor (now), channel, key, velocity, on });
    if (slotTaking.load (std::memory_order_relaxed))
        uiSlotRecorded.push_back ({ recordTickFor (now), channel, key, velocity, on });
}

void AudioEngine::panic()
{
    NoteEvent e;
    e.type = NoteEvent::Type::choke;
    e.channel = -1; // every channel
    liveEvents.push (e);
    midiSync.requestPortNotesOff(); // (round 190jl: the hardware MIDI outputs' notes end too)
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
    midiSync.requestTransportNotesOff(); // (FL's "Stop sound": CC 123 to the "Send all notes off" outputs; REA midiout190jl, 0xf1de20)
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

double AudioEngine::heardLatencyMs() const noexcept
{
    const auto rate = transport.sampleRate.load (std::memory_order_relaxed);
    return outputCompensationMs.load (std::memory_order_relaxed)
           + (rate > 0.0 ? 1000.0 * std::max (0, masterPdcSamples.load (std::memory_order_relaxed)) / rate : 0.0);
}

double AudioEngine::recordTickFor (double eventMs) const noexcept
{
    const auto clock = liveClock();
    // (stopped, or no callback since Play / the count-in started: the transport's own position, as before)
    if (! clock.running)
        return transport.positionTicks.load (std::memory_order_relaxed)
               - (transport.playing.load (std::memory_order_relaxed) ? 0.0 : transport.countIn.load (std::memory_order_relaxed));
    // (round 190hq: the Master's delay compensation too — a note is recorded where the song was heard)
    return liveTickAt (clock, eventMs, compensatePlayed.load (std::memory_order_relaxed) ? heardLatencyMs() : 0.0);
}

CaptureResult AudioEngine::captureMidi()
{
    CaptureOptions o;
    o.afterMs = lastCaptureMs;
    o.nowMs = juce::Time::getMillisecondCounterHiRes();
    o.gapMs = 2.0 * (double) song.barTicks() / ppq * 60000.0 / juce::jmax (1.0, song.bpm); // two bars of silence end a phrase
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
    o.gapMs = 2.0 * (double) song.barTicks() / ppq * 60000.0 / juce::jmax (1.0, song.bpm);
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
    auto notes = session::phraseNotes (phrase, o.nowMs, bpm, song.meter(), length);
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
    {
        // Settings' / Options' "Record input audio" with no insert armed: the first two input channels. Round 190ir
        // (Discord report 2026-11-15, "how do i stop it from creating new inserts every time i record"): each take used
        // to get a new free insert (and so a new track). They now go through one insert: the first that listens to the
        // device's first input already (the one an earlier take of this switch was given, or the user's), else a free
        // insert that gets that input.
        auto target = -1;
        for (int t = 1; t < (int) song.mixer.size() && target < 0; ++t)
            if (song.mixer[(std::size_t) t].inputChannel == 0 && ! song.mixer[(std::size_t) t].recordSafe)
                target = t;
        if (target < 0)
            if (const auto free = song.nextFreeInsert(); free > 0 && free < (int) song.mixer.size())
            {
                setInsertInput (free, 0, deviceChannels > 1);
                target = free;
            }
        if (target > 0)
        {
            // the device input itself, as before (the insert is not armed, so it may not be processing its input): the
            // clip plays through that insert and its track becomes the insert's audio track, so the next take lands there
            const auto [first, count] = inputChannelsOf (song.mixer[(std::size_t) target], deviceChannels);
            if (count > 0)
                wanted.push_back ({ target, first, count });
        }
        if (wanted.empty()) // (no insert could take it: the old way, a new insert for the take)
            wanted.push_back ({ 0, 0, juce::jmin (2, deviceChannels) });
    }

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

    // round 190ir (Discord report: "it isnt picking up my audio input"): takes that hold nothing but digital silence
    // (exact zeros: not a quiet room, which always has some noise) mean the input delivers nothing: another input
    // channel than the one the microphone is on, a muted interface, or the OS's microphone privacy (Windows) /
    // permission (macOS). Said once the take is placed, so the user knows why the clip is flat.
    if (! kept.empty() && std::all_of (kept.begin(), kept.end(), [] (const std::shared_ptr<Take>& t) { return t->peaks == nullptr || t->peaks->maxPeak() <= 0.0f; }))
        notify (InputIssue::silentTake, kept.front()->track);

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
    handleMidiInput (source, source != nullptr ? source->getName().toStdString() : std::string(), m);
}

void AudioEngine::handleMidiInput (juce::MidiInput* source, const std::string& deviceName, const juce::MidiMessage& m)
{
    // MIDI thread: queues and atomics only (and a short copy of the device settings)
    prefs::MidiInputPrefs device;
    auto velocityCurve = prefs::VelocityCurve::linear;
    auto locked = -1; // a channel that "receives notes from" this device (FL's channel lock)
    auto jumpChannel = 0;
    std::uint8_t inputSource = 0; // round 190jl: NoteEvent::source (0 = unknown)
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
        for (std::size_t k = 0; k < midiSourceNames.size(); ++k) // (round 190jl: the input's source number)
            if (midiSourceNames[k] == name)
            {
                inputSource = (std::uint8_t) (k + 1);
                break;
            }
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
            // MPE stage 2 (issue #108): while a take records, each change also goes into the held note's curves
            const auto taking = transport.recording.load() && (transport.playing.load() || isCountingIn());
            auto tick = 0.0;
            if (taking)
            {
                const auto now = juce::Time::getMillisecondCounterHiRes();
                const auto stamped = m.getTimeStamp() * 1000.0;
                tick = recordTickFor (std::abs (stamped - now) < 1000.0 ? stamped : now);
            }
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
                    if (taking && e.channel >= 0)
                    {
                        RecordedEvent r { tick, e.channel, e.key, 0.0f, false };
                        r.lane = (std::int8_t) (x.expr == mpe::Expr::pitch ? NoteCurveLane::pitch : x.expr == mpe::Expr::timbre ? NoteCurveLane::timbre
                                                                                                                                  : NoteCurveLane::pressure);
                        r.value = x.value;
                        midiRecordQueue.push (r);
                    }
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
    auto playControl = [this, &device, &m, recordBend, bendTick, inputSource] (int control, float value) {
        NoteEvent e;
        e.source = inputSource;
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
                    e.source = inputSource;
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
    markLiveNote (m.getNoteNumber(), (int) m.getVelocity(), on);

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

    auto push = [this, taking, tick, mpeNote, inputSource] (const InputHeldNotes::Target& t, bool noteOn, float velocity) {
        NoteEvent e;
        e.source = inputSource;
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
                                    mpe::modYOfPressure (mpeNote->pressure), -1, mpeNote->cents });
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

std::function<juce::Array<juce::MidiDeviceInfo> (bool inputs)>& AudioEngine::midiDeviceListOverride()
{
    static std::function<juce::Array<juce::MidiDeviceInfo> (bool)> lists;
    return lists;
}

void AudioEngine::enableAllMidiInputs()
{
    const auto& stand = midiDeviceListOverride();
    const auto inputs = stand ? stand (true) : juce::MidiInput::getAvailableDevices();
    const auto outputs = stand ? stand (false) : juce::MidiOutput::getAvailableDevices();

    // (a device turned off in the MIDI settings is closed, so other programs can use it)
    std::vector<prefs::MidiPort> available;
    std::vector<std::string> open;
    for (const auto& device : inputs)
    {
        available.push_back ({ device.identifier.toStdString(), device.name.toStdString() });
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
            open.push_back (device.identifier.toStdString());
    }
    // round 190gh (B8): an input that was open and is no longer listed (unplugged) is closed at once
    for (const auto& id : lastMidiInputIds)
        if (std::none_of (available.begin(), available.end(), [&id] (const prefs::MidiPort& p) { return p.id == id; })
            && deviceManager.isMidiInputDeviceEnabled (juce::String (id)))
            open.push_back (id);

    const auto plan = prefs::planMidiInputs (midiPrefs, available, lastMidiInputIds, open);
    for (const auto* list : { &plan.close, &plan.gone })
        for (const auto& id : *list)
            deviceManager.setMidiInputDeviceEnabled (juce::String (id), false);
    for (const auto& id : plan.reopen) // it went away and came back: the old handle may be dead
        deviceManager.setMidiInputDeviceEnabled (juce::String (id), false);
    for (const auto* list : { &plan.reopen, &plan.open })
        for (const auto& id : *list)
            deviceManager.setMidiInputDeviceEnabled (juce::String (id), true);

    lastMidiInputIds.clear();
    for (const auto& port : available)
        lastMidiInputIds.push_back (port.id);
    {
        // (round 190jl: each input's source number, for the MIDI Out channels' echo guard; the outputs learn a new one
        // from configureMidiOutputs, which follows every list change)
        const juce::SpinLock::ScopedLockType lock (midiPrefsLock);
        for (const auto& port : available)
            if (midiSourceNames.size() < 63 && std::find (midiSourceNames.begin(), midiSourceNames.end(), port.name) == midiSourceNames.end())
                midiSourceNames.push_back (port.name);
    }

    // round 190gh (B8): what was plugged in or out since the last check (inputs and outputs)
    std::vector<prefs::MidiPort> ports;
    for (const auto& port : available)
        ports.push_back ({ "in:" + port.id, port.name });
    for (const auto& device : outputs)
        ports.push_back ({ "out:" + device.identifier.toStdString(), device.name.toStdString() });
    const auto change = prefs::diffMidiDevices (lastMidiPorts, ports);
    const auto firstList = ! std::exchange (midiPortsKnown, true);
    lastMidiPorts = std::move (ports);

    // the devices that run a controller profile (round 190aw)
    std::vector<std::string> names;
    for (const auto& port : available)
        names.push_back (port.name);
    controllerIo.configure (midiPrefs, names, true);

    if (change.changed && ! firstList)
    {
        ++midiDeviceListVersion;
        controllerIo.devicesChanged(); // (feedback ports: an output that came back under another identifier)
        configureMidiOutputs(); // (sync / notes-off / port outputs: closed when gone, opened again by name)
        juce::StringArray added, removed;
        for (const auto& n : change.added)
            added.add (juce::String::fromUTF8 (n.c_str()));
        for (const auto& n : change.removed)
            removed.add (juce::String::fromUTF8 (n.c_str()));
        if (onMidiDevicesChanged && (! added.isEmpty() || ! removed.isEmpty()))
            onMidiDevicesChanged (added, removed);
        sendChangeMessage();
    }
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

void AudioEngine::configureMidiOutputs()
{
    std::vector<std::string> sources;
    {
        const juce::SpinLock::ScopedLockType lock (midiPrefsLock);
        sources = midiSourceNames;
    }
    midiSync.configure (midiPrefs, true, false, sources);
    // round 190jl: the MIDI Out channels on these ports send to the devices (FL's Options › MIDI › Output "Port")
    hardwareMidiPorts = midiSync.hardwarePorts();
    if (rackProcessor != nullptr)
        rackProcessor->getRenderer().setHardwareMidiPorts (hardwareMidiPorts);
    publishRenderAheadTargets(); // (those channels render live: their notes are timed with the device's blocks)
}

void AudioEngine::setMidiPrefs (const prefs::MidiPrefs& p, bool save)
{
    midiPrefs = p;
    {
        const juce::SpinLock::ScopedLockType lock (midiPrefsLock);
        midiPrefsShared = p;
    }
    publishRenderAheadTargets(); // (round 190gq: a MIDI input's own target channel is played live)
    performanceMidiChannel.store (p.performanceChannel);
    footPedalNoteOff.store (p.footPedalNoteOff);
    if (! p.pickup)
        pickups.clear();
    if (midiOpen) // (only with an audio device: the real app, once its MIDI devices are open; they open with these)
    {
        enableAllMidiInputs();
        configureMidiOutputs();
        applyLinkPrefs();
    }
    if (save)
        midiSettingsFile().replaceWithText (juce::String (p.toText()));
    sendChangeMessage();
}

// --- Ableton Link (round 190gl) -----------------------------------------------------------------------------------

void AudioEngine::applyLinkPrefs()
{
    abletonLink.setStartStopSync (midiPrefs.linkStartStopSync);
    if (midiPrefs.link == abletonLink.isEnabled())
        return;
    if (! midiPrefs.link && abletonLink.tempoChangedByPeer())
    {
        // the reference DAW has one tempo: leaving the session, the project keeps the tempo a peer set last (not over
        // tempo automation, which sets it anyway)
        const auto session = abletonLink.sessionTempo();
        if (session > 0.0 && std::abs (session - song.bpm) > 1.0e-6 && ! transport.songTempoMap.load())
            editSong ([session] (Song& s) { s.bpm = session; });
    }
    abletonLink.setEnabled (midiPrefs.link, song.bpm);
}

void AudioEngine::setLinkEnabled (bool on)
{
    if (midiPrefs.link == on && abletonLink.isEnabled() == on)
        return;
    auto p = midiPrefs;
    p.link = on;
    setMidiPrefs (p);
}

void AudioEngine::setLinkStartStopSync (bool on)
{
    if (midiPrefs.linkStartStopSync == on)
        return;
    auto p = midiPrefs;
    p.linkStartStopSync = on;
    setMidiPrefs (p);
}

int AudioEngine::restretchAll()
{
    // (round 190mp) the file read again and its stretched copies dropped, then the rack built anew: the copies come from
    // the file as it is now (an edit made elsewhere included), at the tempo of the song now
    const auto count = lynx::restretchableChannels (song);
    if (count == 0)
        return 0;
    for (const auto& c : song.channels)
        if ((c.instrument == InstrumentKind::sampler || c.instrument == InstrumentKind::audioClip) && ! c.samplePath.empty()
            && c.sampler.keepPitch && c.sampler.stretchBeats > 0.0f)
            samples.forget (juce::String::fromUTF8 (c.samplePath.c_str()));
    lastPublishedShape.song.bpm = -1.0; // (no stretched channel is kept from the last rack: each one is built again)
    publishSong();
    return count;
}

double AudioEngine::shownTempo() const noexcept
{
    const auto session = abletonLink.isEnabled() ? abletonLink.sessionTempo() : 0.0;
    return session > 0.0 ? session : song.bpm;
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
    // (rollup 0.1.28, REA 190hb action 3: no sleep in the middle of a take; this timer runs on the message thread)
    if (active && ! awakeForRecording)
        awakeForRecording = powerhints::keepAwakeBegin();
    else if (! active && awakeForRecording)
        powerhints::keepAwakeEnd (*std::exchange (awakeForRecording, std::nullopt));

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
        recorder.begin (pattern.effectiveLength (song.meter()));
        // Pattern mode with Loop record off (round 170e5, FL's "never" play end [DECOMP 0x11d4800]): the pattern plays
        // once from the start of this pass and on past its end; the notes stay where they were played, so it grows
        if (recordloop::patternLinear (song.mode, recordOptions.loopRecord))
        {
            const auto origin = recordloop::patternOrigin (at, pattern.effectiveLength (song.meter()));
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
        if (e.lane >= 0) // (an MPE note's expression: into its held note, issue #108)
        {
            if (recordOptions.recordNotes)
                recorder.add (song, e);
            continue;
        }
        // (count-in notes: onto the start within the catch window, else at their real time; round 170d)
        if (! recordOptions.recordNotes || ! catchEarlyNote (e, takeCatch) || outsidePunch (e)) // recording filter: notes off
            continue;
        beginTake (std::min (lastPlayingTick, e.tick));
        recorder.add (song, e);
    }

    takeRecordedPluginNotes(); // GitHub #198 (round 190ks): the notes plugin windows played
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
    if (starts)
        startOnInput (WaitInput::clips); // (round 190oa, DF-10: FL's clip trigger starts a wait for input)
    if (! transport.playing.load() && ! starts && command.type != T::backToArrangement && command.type != T::trackBackToArrangement && command.type != T::reset
        && command.type != T::recordEnd) // (a recordEnd while stopped cancels a recording that never began)
        return;
    if (starts && song.mode != PlayMode::song) // the grid plays with the song
        editSong ([] (Song& s) { s.mode = PlayMode::song; }, false);
    // a scene with its own tempo and bar length: the song takes them when it is launched
    if (command.type == T::launchScene && command.scene >= 0 && command.scene < (int) song.clipGrid->scenes.size())
        if (const auto& scene = song.clipGrid->scenes[(std::size_t) command.scene]; scene.tempo > 0.0 || scene.beatsPerBar > 0)
            editSong ([tempo = scene.tempo, bar = scene.beatsPerBar, unit = scene.beatUnit] (Song& s) {
                if (tempo > 0.0)
                {
                    s.bpm = tempo;
                    // round 190hq (Live-exact): the scene's tempo wins over the Playlist's tempo automation / tempo
                    // markers until Back to Playlist
                    s.sceneTempoHold = tempomap::tempoAutomation (s) >= 0 || tempomap::hasTempoEvents (s);
                }
                if (bar > 0)
                {
                    // round 190fg: the song takes the scene's meter as it is (6 / 8: six eighths a bar; it was 3/4)
                    s.beatsPerBar = bar;
                    s.beatUnit = session::sceneBeatUnit (unit);
                }
            }, false);
    if (command.type == T::backToArrangement && song.sceneTempoHold) // (round 190hq: the tempo map plays again)
        editSong ([] (Song& s) { s.sceneTempoHold = false; }, false);
    if (starts && ! transport.playing.load() && ! isCountingIn())
    {
        // a recording into a slot counts in first, as the record options say (round 190bv)
        if (command.type == T::record && recordOptions.countInBars > 0 && ! abletonLink.isEnabled())
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

namespace
{
// the sample files a song's channels and effects read (the instruments that load their channel's sample, zones,
// effects with a sample): what RackState::build asks the sample provider for
std::vector<std::string> songSamplePaths (const Song& song)
{
    std::vector<std::string> paths;
    auto addPath = [&paths] (const std::string& p) {
        if (! p.empty() && std::find (paths.begin(), paths.end(), p) == paths.end())
            paths.push_back (p);
    };
    for (const auto& c : song.channels)
    {
        if (c.instrument == InstrumentKind::sampler || c.instrument == InstrumentKind::audioClip || c.instrument == InstrumentKind::granulizer
            || c.instrument == InstrumentKind::wavetraveller || c.instrument == InstrumentKind::fruityslicer
            || c.instrument == InstrumentKind::slicex
            || c.instrument == InstrumentKind::flkeys || c.instrument == InstrumentKind::directwave)
            addPath (c.samplePath);
        for (const auto& z : c.zones)
            addPath (z.path);
    }
    for (const auto& track : song.mixer)
        for (const auto& fx : track.effects)
            addPath (fx.samplePath);
    return paths;
}

// Round perfgate190id (community slice B12, the big-project gate): the samples of a song decoded in parallel (as an
// .flp import does, round 112), so the rack build that follows finds them decoded instead of decoding them one by one
// on the message thread. Keep the result alive until the song is published.
std::vector<std::shared_ptr<const SampleData>> decodeSamplesInParallel (SampleLibrary& samples, const Song& forSong)
{
    std::vector<std::shared_ptr<const SampleData>> decoded;
    const auto paths = songSamplePaths (forSong);
    if (paths.size() < 2) // (one file: the rack build decodes it as fast)
        return decoded;
    const loadtrace::Scope timing ("samples_parallel");
    std::atomic<std::size_t> next { 0 };
    std::mutex keep;
    auto worker = [&] {
        for (auto i = next++; i < paths.size(); i = next++)
            if (auto sample = samples.load (juce::String::fromUTF8 (paths[i].c_str())))
            {
                const std::scoped_lock l (keep);
                decoded.push_back (std::move (sample));
            }
    };
    const auto threads = (std::size_t) juce::jlimit (1, 6, juce::SystemStats::getNumCpus() - 1);
    std::vector<std::thread> pool;
    for (std::size_t i = 1; i < juce::jmin (threads, paths.size()); ++i)
        pool.emplace_back (worker);
    worker();
    for (auto& t : pool)
        t.join();
#if defined(__GLIBC__)
    malloc_trim (0); // (the workers' malloc arenas keep their freed decode buffers otherwise: +27 MB on big200)
#endif
    return decoded;
}
} // namespace

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
    if (araHost != nullptr) // round 190kx: another project: its own ARA documents (made again from its archives)
        araHost->reset();
    undoStack.clear();
    redoStack.clear();
    sessionUndo.clear();
    sessionRedo.clear();
    sessionAfterOpen = false;
    song = std::move (newSong);
    ++openCount; // (round 190oo: getOpenCount)
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

    // (round perfgate190id: a .lynx with 24 one-minute takes froze the window for 5-50 s while the rack build decoded
    // them one after the other into the stream cache)
    const auto decoded = parallelSampleDecode ? decodeSamplesInParallel (samples, song) : std::vector<std::shared_ptr<const SampleData>> {};
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
    const auto remote = nextRemoteHistoryStep;
    nextRemoteHistoryStep = false;
    // Dozens of edits arrive during a drag; only the first one of the click saves the state.
    bool samePress = false;
    const auto buttonDown = juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown();
    if (gestureOpen && gestureRealMouse && ! buttonDown && ! gestureEnding)
        gestureOpen = false; // (its mouseUp never came: the gesture is over)

    if (gestureOpen) // (round 190jv: one step per gesture, whatever input source drives it)
    {
        samePress = gestureSaved;
        gestureSaved = true;
        lastUndoPress = buttonDown ? juce::Desktop::getInstance().getMainMouseSource().getLastMouseDownTime() : juce::Time();
    }
    else if (buttonDown)
    {
        const auto press = juce::Desktop::getInstance().getMainMouseSource().getLastMouseDownTime();
        samePress = press == lastUndoPress;
        lastUndoPress = press;
    }
    else
    {
        lastUndoPress = {};
        gestureSaved = false;
    }

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (! samePress && tweakSkipsStep (buttonDown, nowMs)) // (round 190mv: "Undo knob tweaks" off)
        samePress = true;

    if (! samePress)
    {
        undoStack.push_back ({ song, remote });
        lastPushSize = undoStack.size();
        lastPushMs = nowMs;
        lastPushPress = buttonDown ? juce::Desktop::getInstance().getMainMouseSource().getLastMouseDownTime() : juce::Time();
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
            sessionUndo.push_back ({ song, song, remote });
            sessionRedo.clear();
            if (sessionUndo.size() > maxUndoSteps)
                sessionUndo.pop_front();
        }
    }
    if (sessionHistory)
        sessionAfterOpen = true;
}

// round 190mv (DF-134): FL's "Undo knob tweaks" off. The tweak and its edit come within one press (a drag) or one
// event (a wheel step, a click on a switch), in either order.
bool AudioEngine::tweakSkipsStep (bool buttonDown, double nowMs) const noexcept
{
    if (flsettings::options().undoKnobTweaks.load() || sessionHistory)
        return false;
    if (buttonDown)
        return tweakPress != juce::Time() && tweakPress == juce::Desktop::getInstance().getMainMouseSource().getLastMouseDownTime();
    return nowMs - tweakMs < 60.0;
}

float AudioEngine::tapperMuteGain (double left, double total) noexcept
{
    if (! (left > 0.0) || ! (total > 0.0))
        return 1.0f;
    const auto c = std::cos (juce::MathConstants<double>::halfPi * juce::jlimit (0.0, 1.0, left / total));
    return (float) (0.01 + 0.99 * std::pow (c, 18.0));
}

void AudioEngine::setCurrentTrackSource (int track)
{
    const auto changed = track != currentSource;
    currentSource = track;
    if (rackProcessor != nullptr)
        rackProcessor->getRenderer().setCurrentSource (track);
    // round 190my (DF-82, REA 190mv: FUN_010ff3b0 adds C's latency to its target's): a Current track with latency moves
    // that latency to the newly selected track's PDC — the plan is made again (nothing to do while C adds none)
    if (changed && lastPublishedShape.pdc.currentLatency > 0)
        publishSong();
}

void AudioEngine::controlTweakedForUndo()
{
    if (flsettings::options().undoKnobTweaks.load() || sessionHistory)
        return;
    const auto buttonDown = juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown();
    const auto press = buttonDown ? juce::Desktop::getInstance().getMainMouseSource().getLastMouseDownTime() : juce::Time();
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    tweakPress = press;
    tweakMs = nowMs;
    // the edit came first and saved a step for this press / event: take it back (the project stays changed)
    const auto samePush = buttonDown ? (press != juce::Time() && lastPushPress == press) : (lastPushPress == juce::Time() && nowMs - lastPushMs < 60.0);
    if (samePush && lastPushSize > 0 && undoStack.size() == lastPushSize)
    {
        undoStack.pop_back();
        lastPushSize = 0;
        modified = true;
        sendChangeMessage(); // (the Edit menu / history show one step less)
    }
}

void AudioEngine::beginUndoGesture() noexcept
{
    // (a gesture that is still open lost its mouseUp: this press starts a new one)
    gestureOpen = true;
    gestureSaved = false;
    gestureEnding = false;
    gestureRealMouse = juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown();
}

void AudioEngine::endUndoGesture() noexcept
{
    gestureOpen = false;
    gestureSaved = false;
    gestureEnding = false;
}

void AudioEngine::editSong (const std::function<void (Song&)>& edit, bool undoable)
{
    if (undoable)
    {
        saveUndoStep();
        modified = true;
    }

    auto lengthsBefore = patternLengths (song);
    const auto auditioned = comp::auditionedTrack (song);
    const auto pluginsBefore = detachNewPluginsOn ? freshPluginCounts (song) : std::map<std::string, int>();
    // LYNX's "Link Playlist tracks to mixer tracks" (rounds 190gz, 190ib): each instrument's home track and the linked
    // tracks' names before the edit
    const auto linkBefore = autolink::snapshot (song);
    const auto namesBefore = trackmodes::linkedNames (song); // (round 190ns, issue #235: FL's audio track ⇄ mixer track)
    const auto channelsBefore = song.channels.size(); // (round 190iu: Track-only mode, core/TrackOnly.h)
    const auto looksBefore = autolook::names (song); // (issue #239: the user's colour / icon rules; nothing when off)
    const auto automationsBefore = song.automations.size(); // (round 190od: FL's "clips use note colors")
    edit (song);
    song.keepOnePattern(); // (round 190ea: what follows, and every listener, may read the current pattern)
    newclipcolours::colourNew (song, channelsBefore, automationsBefore); // FL's theme option: new clips take note colours
    mixerops::keepCurrentLast (song); // (round 190mv: FL's Current track stays the last mixer track)
    // round 190ne (core/TrackClips.h): a track clip goes with its last clip, in this undo step (ordinary patterns stay, as
    // in FL; Song::removePattern keeps one pattern and the current one in range)
    if (std::vector<int> collected; trackclips::collect (song, &collected) > 0)
        for (auto it = collected.rbegin(); it != collected.rend(); ++it) // (followPatternLengths compares by index)
            if (*it < (int) lengthsBefore.size())
                lengthsBefore.erase (lengthsBefore.begin() + *it);
    if (trackonly::active() && song.channels.size() > channelsBefore) // each new instrument gets its own instrument track
        trackonly::follow (song, channelsBefore);
    trackmodes::followLinkedNames (song, namesBefore); // FL: an audio track and its mixer track keep one name and colour
    if (song.linkTracksToMixer && linkBefore.on) // new content goes through its track's insert; renames follow the link
        autolink::followNewContent (song, linkBefore);
    autolook::follow (song, looksBefore); // issue #239: an item named by this edit takes its rule's colour and icon
    if (detachNewPluginsOn)
        detachNewPlugins (pluginsBefore, song);
    comp::checkAudition (song, auditioned);
    followPatternLengths (lengthsBefore, song);
    clipops::updateCrossfades (song); // FL's automatic crossfades follow every edit (round 166e)
    publishSong();
}

bool AudioEngine::setChannelAra (int channel, const std::string& pluginId, const std::string& pluginName)
{
    if (channel < 0 || channel >= (int) song.channels.size() || song.channels[(std::size_t) channel].instrument != InstrumentKind::audioClip)
        return false;
    editSong ([&] (Song& s) {
        auto& c = s.channels[(std::size_t) channel];
        c.ara = pluginId.empty() ? AraLink {} : AraLink { pluginId, pluginName };
        if (pluginId.empty() && ! ara::anyAraChannel (s) && ! ara::anyAraInsert (s)) // (round 190oq: nor an ARA insert)
            s.araDocuments.clear(); // (the last one: no document is kept)
    });
    return true;
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
    renderAheadTargets.tweaked(); // (round 190gq: the part rendered ahead hears a drag within about a block)
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
    if (from.empty()) return;
    const auto remote = from.back().remote;
    bool movementMayHaveBegun = false;
    try
    {
        std::optional<Song> before;
        if (remote)
        {
            before = song;
            capturePluginStatesInto (*before, false);
            if (remoteHistoryApiStep && ! checkRemoteHistory (from.back().deviceHash, *before)) return;
        }
        // Prepare the pending after-state, selective result and all conflict labels before touching either stack.
        auto step = from.back();
        if (remote)
        {
            // API calls passed the device fence; native history intentionally restores without that refusal.
            // Capture this endpoint while keeping metadata for selective conflicts in the remaining entities.
            auto& outgoing = redo ? step.first : step.second;
            outgoing.channels = before->channels;
            outgoing.mixer = before->mixer;
        }
        const auto& current = before ? *before : song;
        std::vector<live::Key> skipped;
        auto next = redo ? live::selectiveRevert (current, step.second, step.first, &skipped)
                         : live::selectiveRevert (current, step.first, step.second, &skipped);
        const auto savedTargetHash = remote ? remoteDeviceStateHashOf (next) : juce::String();
        juce::StringArray names;
        for (const auto& key : skipped)
            names.addIfNotAlreadyThere (juce::String::fromUTF8 (live::describe (key, song, [] (const char* s) { return tr (s).toStdString(); }).c_str()));
        const auto fresh = remote ? remoteFreshTargets (*before, next) : std::unordered_set<std::uint64_t>();
        const juce::ScopedValueSetter<std::unordered_set<std::uint64_t>> freshPolicy (remoteFreshInstances, fresh);
        if (remoteHistoryApiStep && remoteHistoryCancelled && remoteHistoryCancelled())
        {
            failRemoteHistory (false);
            remoteHistoryError = tr ("Remote history operation cancelled before changing history.");
            return;
        }
        movementMayHaveBegun = true;
        to.push_back (std::move (step));
        from.pop_back();
        sessionAfterOpen = false;
        song = std::move (next);
        modified = true;
        lastUndoPress = {};
        gestureSaved = false;
        clipops::updateCrossfades (song);
        levelsPending = false;
        publishSong();
        const auto fence = finishRemoteHistory (before);
        if (remote && ! remoteHistoryFailed)
        {
            auto& incoming = redo ? to.back().second : to.back().first;
            incoming.channels = song.channels;
            incoming.mixer = song.mixer;
            remote_history::acceptRestoredState (to.back(), from.empty() ? nullptr : &from.back(), savedTargetHash, fence);
        }
        if (! names.isEmpty() && onSessionUndoSkipped) onSessionUndoSkipped (names);
    }
    catch (...)
    {
        if (! remote) throw;
        failRemoteHistory (movementMayHaveBegun);
    }
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
    gestureSaved = false;
    song = std::move (remote);
    clipops::updateCrossfades (song);
    publishSong();
}

void AudioEngine::historyStep (bool redo)
{
    auto& from = redo ? redoStack : undoStack;
    auto& to = redo ? undoStack : redoStack;
    if (from.empty()) return;
    const auto remote = from.back().remote;
    bool movementMayHaveBegun = false;
    try
    {
        std::optional<Song> before;
        if (remote)
        {
            before = song;
            capturePluginStatesInto (*before, false);
            if (remoteHistoryApiStep && ! checkRemoteHistory (from.back().deviceHash, *before)) return;
        }
        const auto savedTargetHash = remote ? remoteDeviceStateHashOf (from.back().song) : juce::String();
        // The model's saved blob can differ from the running vendor's normalized blob. Redo must restore the
        // actual state we are leaving, captured before any history movement, rather than the stale model bytes.
        std::optional<HistoryEntry> inverse;
        if (remote) inverse.emplace (HistoryEntry { *before, true });
        const auto fresh = remote ? remoteFreshTargets (*before, from.back().song) : std::unordered_set<std::uint64_t>();
        const juce::ScopedValueSetter<std::unordered_set<std::uint64_t>> freshPolicy (remoteFreshInstances, fresh);
        if (remoteHistoryApiStep && remoteHistoryCancelled && remoteHistoryCancelled())
        {
            failRemoteHistory (false);
            remoteHistoryError = tr ("Remote history operation cancelled before changing history.");
            return;
        }
        // Moving song into a temporary may already change the live model even if push_back throws.
        movementMayHaveBegun = true;
        if (remote) to.push_back (std::move (*inverse));
        else to.push_back ({ std::move (song), false });
        song = std::move (from.back().song);
        from.pop_back();
        comp::setAudition (song, -1); // an old history state must not resume a comp audition
        modified = true;
        levelsPending = false;
        lastUndoPress = {};
        gestureSaved = false;
        publishSong();
        const auto fence = finishRemoteHistory (before);
        if (remote && ! remoteHistoryFailed)
            remote_history::acceptRestoredState (to.back(), from.empty() ? nullptr : &from.back(),
                                                 savedTargetHash, fence);
    }
    catch (...)
    {
        if (! remote) throw;
        failRemoteHistory (movementMayHaveBegun);
    }
}

void AudioEngine::undo()
{
    remoteHistoryFailed = false;
    remoteHistoryUncertain = false;
    remoteHistoryError.clear();
    remoteHistoryCode = "changed";
    if (sessionHistory) sessionStep (false); else historyStep (false);
}

void AudioEngine::redo()
{
    remoteHistoryFailed = false;
    remoteHistoryUncertain = false;
    remoteHistoryError.clear();
    remoteHistoryCode = "changed";
    if (sessionHistory) sessionStep (true); else historyStep (true);
}

namespace
{
template <typename Entries>
const Song& historyState (const Entries& undo, const Song& current, const Entries& redo, int index)
{
    if (index < (int) undo.size())
        return undo[(std::size_t) index].song;
    if (index == (int) undo.size())
        return current;
    return redo[redo.size() - 1 - (std::size_t) (index - (int) undo.size() - 1)].song;
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
        while (getHistoryPosition() != index)
        {
            const auto previous = getHistoryPosition();
            if (previous > index) undo(); else redo();
            if (remoteHistoryFailed || getHistoryPosition() == previous) return;
        }
        return;
    }

    bool remote = false;
    const auto position = getHistoryPosition();
    if (index < position)
        for (int i = index; i < position; ++i) remote = remote || undoStack[(std::size_t) i].remote;
    else
        for (int i = 0; i < index - position; ++i) remote = remote || redoStack[redoStack.size() - 1 - (std::size_t) i].remote;
    if (remote)
    {
        while (getHistoryPosition() > index)
        {
            const auto previous = getHistoryPosition();
            undo();
            if (remoteHistoryFailed || getHistoryPosition() == previous) return;
        }
        while (getHistoryPosition() < index)
        {
            const auto previous = getHistoryPosition();
            redo();
            if (remoteHistoryFailed || getHistoryPosition() == previous) return;
        }
        return;
    }
    // move through the stacks, then publish once
    while (getHistoryPosition() > index && ! undoStack.empty())
    {
        redoStack.push_back ({ std::move (song), undoStack.back().remote });
        song = std::move (undoStack.back().song);
        undoStack.pop_back();
    }

    while (getHistoryPosition() < index && ! redoStack.empty())
    {
        undoStack.push_back ({ std::move (song), redoStack.back().remote });
        song = std::move (redoStack.back().song);
        redoStack.pop_back();
    }

    modified = true;
    lastUndoPress = {};
    gestureSaved = false;
    levelsPending = false;
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
    publishRenderAheadTargets();
}

// Render-ahead (round 190gq, core/RenderAhead.h): the channels notes are played into live stay real-time — the selected
// channels (the typing keyboard, a MIDI input on "the selected channels"), each MIDI input's own target channel, the
// channels locked to a MIDI input ("Receive notes from")
void AudioEngine::publishRenderAheadTargets()
{
    std::array<int, renderahead::Targets::maxChannels> channels {};
    int count = 0;
    auto add = [&] (int c) {
        if (c < 0 || count >= (int) channels.size() || std::find (channels.begin(), channels.begin() + count, c) != channels.begin() + count)
            return;
        channels[(std::size_t) count++] = c;
    };
    add (selectedChannel);
    for (const auto c : selectedChannels)
        add (c);
    for (const auto& input : midiPrefs.inputs)
        if (input.enabled)
            add (input.target);
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        if (! song.channels[c].receiveFrom.empty())
            add ((int) c);
    // round 190jl: a MIDI Out channel (or a plugin's output port) that sends to a hardware MIDI output
    if ((hardwareMidiPorts[0] | hardwareMidiPorts[1] | hardwareMidiPorts[2] | hardwareMidiPorts[3]) != 0)
        for (std::size_t c = 0; c < song.channels.size(); ++c)
        {
            const auto& ch = song.channels[c];
            const auto port = ch.instrument == InstrumentKind::midiOut ? midiout::stateOf (ch.pluginState).port
                              : ch.instrument == InstrumentKind::plugin ? ch.plugin.midiOutPort : -1;
            if (port >= 0 && port < 256 && (hardwareMidiPorts[(std::size_t) port >> 6] >> (port & 63) & 1u) != 0)
                add ((int) c);
        }
    // hosted plugin editors on screen: their channel or insert
    std::array<int, renderahead::Targets::maxTracks> tracks {};
    int trackCount = 0;
    for (const auto& e : openEditors)
    {
        if (e.track >= 0 && trackCount < (int) tracks.size())
            tracks[(std::size_t) trackCount++] = e.track;
        if (e.find)
            if (const auto* ref = e.find (song))
                for (std::size_t c = 0; c < song.channels.size(); ++c)
                    if (&song.channels[c].plugin == ref)
                        add ((int) c);
    }
    renderAheadTargets.publish (channels.data(), count, tracks.data(), trackCount);
    publishOwnNoteChannels(); // (round 190ks: the same editors)
}

// GitHub #198 (round 190ks): the channels whose hosted plugin's window is open — what it plays from there is recorded
// (core/RackRenderer.h ownNotes). Their channel index is looked up at each publish, as the render-ahead's.
void AudioEngine::publishOwnNoteChannels()
{
    std::array<std::uint64_t, RackRenderer::ownNoteWords> mask {};
    for (const auto& e : openEditors)
        if (e.track < 0 && e.find)
            if (const auto* ref = e.find (song))
                for (std::size_t c = 0; c < song.channels.size() && c < (std::size_t) RackRenderer::maxChannels; ++c)
                    if (&song.channels[c].plugin == ref && song.channels[c].instrument == InstrumentKind::plugin
                        && song.channels[c].plugin.recordOwnNotes)
                        mask[c >> 6] |= std::uint64_t { 1 } << (c & 63);
    if (mask == ownNoteChannels)
        return;
    ownNoteChannels = mask;
    if (rackProcessor != nullptr)
        rackProcessor->getRenderer().setOwnNoteChannels (ownNoteChannels);
}

void AudioEngine::takePluginNotes (int done) noexcept
{
    if (rackProcessor == nullptr)
        return;
    auto& rack = rackProcessor->getRenderer();
    const auto count = rack.takeOwnNotes (ownNotesTaken.data(), (int) ownNotesTaken.size());
    if (count == 0)
        return;
    // (as a played note: a take while recording and playing or counting in; a clip slot that records)
    const auto take = transport.recording.load (std::memory_order_relaxed)
                      && (transport.playing.load (std::memory_order_relaxed) || isCountingIn());
    const auto slot = slotTaking.load (std::memory_order_relaxed);
    if (! take && ! slot)
        return;
    const auto clock = liveClock();
    const auto rate = juce::jmax (1.0, transport.sampleRate.load (std::memory_order_relaxed));
    for (int k = 0; k < count; ++k)
    {
        const auto& o = ownNotesTaken[(std::size_t) k];
        // the click reached the plugin by this block: recorded where the song was heard then, as a played note
        const auto eventMs = clock.timeMs + 1000.0 * (double) (done + o.sample) / rate;
        PluginRecorded r;
        r.event = { recordTickFor (eventMs), (int) o.channel, (int) o.key, o.velocity, o.velocity > 0.0f };
        r.take = take;
        r.slot = slot;
        pluginRecordQueue.push (r);
    }
}

void AudioEngine::takeRecordedPluginNotes()
{
    PluginRecorded r;
    while (pluginRecordQueue.pop (r))
    {
        if (r.take)
            uiRecorded.push_back (r.event); // (with the typing keyboard's: the same filter, punch and count-in catch)
        if (r.slot)
            uiSlotRecorded.push_back (r.event);
    }
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
// Round 190fn (O1): the FL Studio Mobile resources folders of Settings › Files (flm-resources.txt, one per line), each
// taken down to the folder "@R@" paths start from; only folders that exist.
std::vector<std::string> flmResourceFolders()
{
    std::vector<std::string> out;
    const auto file = PluginHost::settingsFolder().getChildFile ("flm-resources.txt");
    if (! file.existsAsFile())
        return out;
    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());
    const auto exists = [] (const std::string& p) { return platform::localFile (p).exists(); };
    for (auto line : lines)
    {
        line = line.trim();
        if (line.isEmpty() || ! platform::localFile (line.toStdString()).isDirectory())
            continue;
        out.push_back (flmResourceRootFrom (line.toStdString(), exists));
    }
    return out;
}

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

// round 190og (the .flm export, ui/FlmExportDialog.h): FL Studio Mobile's factory content on this computer, the user's own
// "Resources" folders first, then FL Studio's copy, as the .flm import looks for it
std::vector<std::string> flmFactoryRoots()
{
    auto out = flmResourceFolders();
    const auto roots = findFlStudioRoots();
    if (! roots.factoryData.empty())
        if (const auto root = flmResourceRootIn (roots.factoryData); ! root.empty() && platform::localFile (root).isDirectory())
            out.push_back (root);
    return out;
}

void AudioEngine::prepareFlpImport (const juce::File& file, const std::map<std::string, std::string>& relocations, SampleLibrary& library,
                                    PreparedFlp& out)
{
    juce::MemoryBlock data;
    // a GarageBand project (round 190ic): a .band package folder, read file by file by its import
    const auto bandFolder = ProjectPackage::garageBandPackage (file);
    {
        const loadtrace::Scope timing ("read_project");
        if (bandFolder == juce::File() && ! file.loadFileAsData (data))
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
        const auto garageBand = bandFolder != juce::File();
        // a DAWproject (.dawproject, round 190jf B22) too
        const auto dawProject = file.hasFileExtension ("dawproject");
        // a BandLab project (.blx, round 190jr): its revision JSON, the samples beside it
        const auto bandLab = file.hasFileExtension ("blx");
        // a tracker module (.mod, .xm, .s3m, .it; round 190nw, core/ModuleImport.h) too
        const auto module = file.hasFileExtension (moduleExtensions());
        std::optional<flp::Project> project;
        if (! liveSet && ! mobileProject && ! garageBand && ! dawProject && ! bandLab && ! module)
            project = flp::parseProject ({ fileBytes, data.getSize() });
        loadtrace::add ("parse", loadtrace::msSince (started));

        FlpImportOptions options;
        options.roots = findFlStudioRoots();
        options.projectFolder = file.getParentDirectory().getFullPathName().toStdString();
        options.flmResourceRoots = flmResourceFolders(); // round 190fn (O1): FL Studio Mobile Rack generators in an .flp
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
                // round 190fn (O1): the user's own FL Studio Mobile "Resources" folders (Settings › Files), searched first
                flmOptions.userResourceRoots = options.flmResourceRoots;
                std::string flmError;
                auto song = importFlm (std::vector<std::uint8_t> (fileBytes, fileBytes + data.getSize()), flmOptions, &flmError);
                if (! song)
                {
                    out.error = utf8 (flmError.empty() ? "not a readable FL Studio Mobile project" : flmError.c_str());
                    return;
                }
                out.song = std::move (*song);
            }
            else if (garageBand)
            {
                BandImportOptions bandOptions;
                bandOptions.common = options;
                bandOptions.packageFolder = bandFolder.getFullPathName().toStdString();
                std::string bandError;
                auto song = importBandPackage (bandOptions, &bandError);
                if (! song)
                {
                    out.error = utf8 (bandError.empty() ? "not a readable GarageBand project" : bandError.c_str());
                    return;
                }
                out.song = std::move (*song);
            }
            else if (dawProject)
            {
                DawProjectImportOptions dawOptions;
                dawOptions.common = options;
                // LYNX's own devices with every field (the project file's JSON); a file from the core writer still opens
                dawOptions.nativeInstrument = [] (const std::string&, const std::vector<std::uint8_t>& bytes) -> std::optional<ChannelDef> {
                    if (auto c = ProjectFile::channelFromStateBytes (bytes))
                        return c;
                    if (const auto preset = genpresets::fromText (std::string (bytes.begin(), bytes.end())))
                        return genpresets::newChannel (*preset);
                    return std::nullopt;
                };
                dawOptions.nativeEffect = [] (const std::string&, const std::vector<std::uint8_t>& bytes) -> std::optional<EffectSlot> {
                    if (auto e = ProjectFile::effectFromStateBytes (bytes))
                        return e;
                    return dawproject::effectFromStateText (std::string (bytes.begin(), bytes.end()));
                };
                std::string dawError;
                auto song = importDawProject (std::vector<std::uint8_t> (fileBytes, fileBytes + data.getSize()), dawOptions, &dawError);
                if (! song)
                {
                    out.error = utf8 (dawError.empty() ? "not a readable DAWproject file" : dawError.c_str());
                    return;
                }
                out.song = std::move (*song);
            }
            else if (module)
            {
                // the samples become WAV files in "<module> samples" beside the module (as an FL Studio Mobile share's
                // samples sit beside its .flm); where that folder can't be written, in LYNX's settings folder
                ModuleImportOptions moduleOptions;
                moduleOptions.report = &out.report;
                auto folder = file.getParentDirectory().getChildFile (file.getFileNameWithoutExtension() + " samples");
                if (! folder.createDirectory() || ! folder.hasWriteAccess())
                {
                    const auto hash = juce::String::toHexString ((juce::int64) juce::DefaultHashFunctions::generateHash (
                        file.getFullPathName() + juce::String (file.getSize()), 0x7FFFFFFF));
                    folder = PluginHost::settingsFolder().getChildFile ("module-samples").getChildFile (juce::File::createLegalFileName (file.getFileNameWithoutExtension()) + "-" + hash);
                    folder.createDirectory();
                }
                moduleOptions.saveSample = [folder] (const std::vector<std::uint8_t>& wav, const std::string& name) -> std::string {
                    const auto target = folder.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (name.c_str())));
                    juce::MemoryBlock existing;
                    if (target.existsAsFile() && target.loadFileAsData (existing) && existing.getSize() == wav.size()
                        && std::memcmp (existing.getData(), wav.data(), wav.size()) == 0)
                        return target.getFullPathName().toStdString(); // (imported before: the same file)
                    if (! target.replaceWithData (wav.data(), wav.size()))
                        return {};
                    return target.getFullPathName().toStdString();
                };
                std::string moduleError;
                if (data.getSize() > (std::size_t) 256 * 1024 * 1024)
                {
                    out.error = utf8 ("the module is larger than 256 MB");
                    return;
                }
                auto song = importModule (std::vector<std::uint8_t> (fileBytes, fileBytes + data.getSize()), moduleOptions, &moduleError);
                if (! song)
                {
                    out.error = utf8 (moduleError.empty() ? "not a readable module" : moduleError.c_str());
                    return;
                }
                if (song->info.title.empty())
                    song->info.title = file.getFileNameWithoutExtension().toStdString();
                out.song = std::move (*song);
            }
            else if (bandLab)
            {
                BlxImportOptions blxOptions;
                blxOptions.common = options;
                const auto folder = file.getParentDirectory();
                blxOptions.folder = folder.getFullPathName().toStdString();
                // the files beside the .blx (its Audio / MIDI folders), for the samples by id or name; a .blx left in a
                // big folder (Downloads, the Desktop) is not walked past a few thousand files
                {
                    juce::RangedDirectoryIterator it (folder, true, "*", juce::File::findFiles);
                    int seen = 0;
                    for (const auto& entry : it)
                    {
                        if (++seen > 5000)
                            break;
                        blxOptions.files.push_back (entry.getFile().getRelativePathFrom (folder).replaceCharacter ('\\', '/').toStdString());
                    }
                }
                std::string blxError;
                auto song = importBlxBytes (std::vector<std::uint8_t> (fileBytes, fileBytes + data.getSize()), blxOptions, &blxError);
                if (! song)
                {
                    out.error = utf8 (blxError.empty() ? "not a readable BandLab project" : blxError.c_str());
                    return;
                }
                if (song->info.title.empty())
                    song->info.title = file.getFileNameWithoutExtension().toStdString();
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
            StretchOptions options; // (round 190fm)
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
                    const auto options = stretchOptionsOf (c.sampler);
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
                                    return x.path == c.samplePath && x.variant == variant && x.ratio == ratio && x.method == method && x.semitones == semitones && x.options == options; }))
                                stretches.push_back ({ c.samplePath, ratio, method, semitones, {}, std::move (variant), std::move (span), options });
                        }
                        continue;
                    }
                    const auto ratio = target / ((double) precompute::stretchedFrames (data, c.sampler, clip) / data->sampleRate);
                    // (the same map the rack asks for: RackState::build)
                    auto map = warpStretchMap (c.sampler.warp, (double) data->frames(), data->sampleRate, (double) std::llround ((double) data->frames() * ratio));
                    if (std::none_of (stretches.begin(), stretches.end(), [&] (const Stretch& x) {
                            return x.path == c.samplePath && x.variant.empty() && x.ratio == ratio && x.method == method && x.semitones == semitones && x.map == map && x.options == options; }))
                        stretches.push_back ({ c.samplePath, ratio, method, semitones, std::move (map), {}, {}, options });
                }
        if (! stretches.empty())
        {
            const auto stretchStarted = std::chrono::steady_clock::now();
            std::atomic<std::size_t> nextStretch { 0 };
            auto stretchWorker = [&] {
                for (auto i = nextStretch++; i < stretches.size(); i = nextStretch++)
                    if (auto stretched = ! stretches[i].variant.empty()
                                             ? library.loadStretchedSpan (juce::String::fromUTF8 (stretches[i].path.c_str()), stretches[i].variant,
                                                                          stretches[i].span, stretches[i].ratio, stretches[i].method, stretches[i].semitones,
                                                                          stretches[i].options)
                                             : library.loadStretched (juce::String::fromUTF8 (stretches[i].path.c_str()), stretches[i].ratio, stretches[i].method, stretches[i].semitones, stretches[i].map,
                                                                      stretches[i].options))
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

bool AudioEngine::readOtherProject (const juce::File& file, OtherProject& out, juce::String& error)
{
    if (file.hasFileExtension (juce::String (ProjectFile::extension).substring (1)))
    {
        auto song = ProjectFile::load (file, error);
        if (! song)
            return false;
        out.song = std::move (*song);
        return true;
    }
    PreparedFlp prepared;
    prepareFlpImport (file, {}, samples, prepared);
    if (! prepared.ok)
    {
        error = prepared.error;
        return false;
    }
    adoptPrepared (prepared); // (hosted plugins found by name, FL's own and missing ones substituted, as an open does)
    plugins.clearPreloaded();
    out.song = std::move (prepared.song);
    out.report = std::move (prepared.report);
    out.missingPlugins = prepared.missing;
    out.decoded = std::move (prepared.decoded);
    return true;
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
        automationDisplay.update (song, songStamp, estimatedPositionNow(), playing, &releasedTargets); // (round 190qp: moved by hand)
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
    factories.stretchedSamples = [this] (const std::string& path, double ratio, StretchMethod method, double semitones, const StretchMap& map,
                                         const StretchOptions& options) {
        const loadtrace::Scope timing ("rack_stretch");
        return samples.loadStretched (juce::String::fromUTF8 (path.c_str()), ratio, method, semitones, map, options);
    };
    factories.stretchedSpans = [this] (const std::string& path, const std::string& variant, const std::shared_ptr<const SampleData>& span,
                                       double ratio, StretchMethod method, double semitones, const StretchOptions& options) {
        const loadtrace::Scope timing ("rack_stretch");
        return samples.loadStretchedSpan (juce::String::fromUTF8 (path.c_str()), variant, span, ratio, method, semitones, options);
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

void AudioEngine::setMainVolume (float position)
{
    position = juce::jlimit (0.0f, 1.0f, position);
    if (song.mainVolume != position)
    {
        song.mainVolume = position;
        if (! modified)
        {
            modified = true; // (FL: the project keeps it; no undo step)
            sendChangeMessage();
        }
    }
    mainVolumeGain.store (faderGain (position), std::memory_order_relaxed);
}

void AudioEngine::publishSong()
{
    mainVolumeGain.store (faderGain (song.mainVolume), std::memory_order_relaxed); // (round 190oa: a loaded / undone song's main volume)
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
    if (! openEditors.empty()) // (round 190ks: a channel moved or its option changed: its index / mark follows)
        publishOwnNoteChannels();
    // round 190jl: a MIDI Out channel added, or moved to a port a hardware output has, renders live
    if ((hardwareMidiPorts[0] | hardwareMidiPorts[1] | hardwareMidiPorts[2] | hardwareMidiPorts[3]) != 0)
        publishRenderAheadTargets();
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
    hwOutput.cueGain.store (sessionmixer::cueGain (sessionmixer::cueOf (song).volume), std::memory_order_relaxed); // (round 190nc)
    if (preview != nullptr) // round 190nc (DL-110): Browser previews to the Cue Out while Cue mode is in effect
    {
        const auto cueOn = sessionmixer::cueOf (song).cueMode && sessionmixer::cueActive (song, outputChannelCount());
        const auto& cue = sessionmixer::cueOf (song);
        preview->setCueOutput (&hwOutput, cueOn ? sessionmixer::cueOutChannels (song).first : -1, cue.out == outputDefault ? OutputMode::stereo : cue.outMode);
    }
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
    transport.beatUnit.store (meterUnit (song.beatUnit));
    transport.projectBpm.store (song.bpm); // (round 190gl: what the Link session sees as the project's own tempo)
    transport.linkQuantum.store (linksync::quantumBeats (song.clipGrid.has_value() ? song.clipGrid->globalQuant : LaunchQuant::bar, song.meter()));

    {
        // time-signature table for the metronome: the start (the song's meter) and each signature change
        transport.signatureVersion.fetch_add (1, std::memory_order_acq_rel); // odd: writing
        int count = 0;
        transport.signatureTick[0].store (0, std::memory_order_relaxed);
        transport.signatureLength[0].store (song.barTicks(), std::memory_order_relaxed);
        transport.signatureBeat[0].store (song.beatTicks(), std::memory_order_relaxed);
        ++count;

        for (const auto& m : song.markers())
            if (m.isSignature() && count < Transport::maxSignatures)
            {
                const auto length = juce::jmax<std::int64_t> (1, (std::int64_t) ppq * 4 * m.numerator / juce::jmax (1, m.denominator));
                const auto beat = juce::jmax<std::int64_t> (1, length / juce::jmax (1, m.numerator)); // (round 190fg: its own beats)
                const auto slot = m.tick <= 0 ? 0 : count++; // at position 0 it replaces the default
                transport.signatureTick[(std::size_t) slot].store (juce::jmax<std::int64_t> (0, m.tick), std::memory_order_relaxed);
                transport.signatureLength[(std::size_t) slot].store (length, std::memory_order_relaxed);
                transport.signatureBeat[(std::size_t) slot].store (beat, std::memory_order_relaxed);
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
    factories.freshInstances = std::exchange (remoteFreshInstances, {});
    factories.liveInstrument = liveInstrumentSource; // (round 190jb: an export engine renders with the live plugins)
    factories.liveEffect = liveEffectSource;
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
    // round 190op (P4, LYNX extra): deactivated channels and inserts are not loaded; the engine plays silent placeholders
    // in their places (core/TrackStates.h deactivate::project). Nothing changes for a project without any.
    {
        auto off = deactivate::effective (unprojected);
        if (off.any)
        {
            if (! projectedSong)
                projectedSong = unprojected;
            deactivate::project (*projectedSong, off);
        }
        deactivatedChannels = std::move (off.channels);
        deactivatedInserts = std::move (off.inserts);
    }
    const Song& playedBeforeAra = projectedSong ? *projectedSong : unprojected;
    // round 190oq (issue #243): an ARA plugin on a mixer insert plays the audio clip channels routed there (their ARA link
    // in the song the rack plays; the slot itself is left empty: core/AraRegions.h projectInserts). Only songs with one.
    std::optional<Song> araInsertSong;
    if (AraHost::available() && ara::anyAraInsert (playedBeforeAra)) // (an export engine too: it borrows the live ARA instances)
    {
        araInsertSong = playedBeforeAra;
        ara::projectInserts (*araInsertSong);
    }
    const Song& played = araInsertSong ? *araInsertSong : playedBeforeAra;
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
    // round 190kx: ARA. The host is made the first time the song has an audio clip channel edited in an ARA plugin (a song
    // without one never makes it); an export engine that renders with the live plugins borrows the live instances instead
    if ((araHost != nullptr || ara::anyAraChannel (played)) && ! liveInstrumentSource && AraHost::available())
    {
        if (araHost == nullptr)
        {
            araHost = std::make_unique<AraHost> (plugins, [this] (const std::string& path) { return samples.load (juce::String::fromUTF8 (path.c_str())); });
            araHost->onPlaybackRequest = [this] (int request, double seconds) {
                if (request == 0)
                    play();
                else if (request == 1)
                    stop();
                else // (round 190oq: through the tempo map)
                    seek (std::max (0.0, ara::timeMap (song).ticksAt (seconds)));
            };
        }
        araHost->sync (played, transport.sampleRate.load());
        factories.araInstrument = [this] (const ChannelDef& c, double rate) { return araHost->instrumentFor (c, rate); };
    }
    factories.currentSource = currentSource; // (round 190my: FL's Current track's latency in the PDC plan)
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
    lastPublishedShape.sideLines = state->sideLines; // (round 190hq: the side inputs' lines carry over as the sends' do)
    // round 190hq (B13): what the Master hears is this much later than what the rack renders (the clocks LYNX sends and
    // follows, Link and played notes count it, as they count the output buffer)
    masterPdcSamples.store (state->pdc.total, std::memory_order_relaxed);
    lastPublishedShape.shifterStock = state->shifterStock; // (round 190gn: the next build keeps the rack's shifter stock)

    const auto mark = [&] (const char* what) {
        if (traceEdits)
            platform::settingsFile ("edits.log").appendText (juce::String (juce::Time::getMillisecondCounterHiRes() - publishStart, 1) + " ms " + what + "\n");
    };
    mark ("built");
    levelsPending = false;
    publishedLevels = projectedSong ? nullptr : state->live; // (stand-ins: their projection, edits rebuild)
    // round 190gn (A6): the renderer's channel / insert buffers hold this song before it can play it
    if (rackProcessor != nullptr)
        rackProcessor->getRenderer().reserveFor (*state);
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
        link.min = l.min;
        link.max = l.max;
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
        g.min = link->min;
        g.max = link->max;
        p.globalLinks.push_back (std::move (g));
    }
    setMidiPrefs (p, saveGlobalLinks);
    linkSmoothing.clear();
    sendChangeMessage();
}

bool AudioEngine::moveMappingScope (const remotemap::Row& row, bool toEveryProject)
{
    auto p = midiPrefs;
    if (! remotemap::canMove (song, p, row, toEveryProject))
        return false;
    auto moved = false;
    editSong ([&] (Song& s) { moved = toEveryProject ? remotemap::moveToEveryProject (s, p, row) : remotemap::moveToThisProject (s, p, row); });
    if (moved)
        setMidiPrefs (p, saveGlobalLinks);
    linkSmoothing.clear();
    sendChangeMessage();
    return moved;
}

bool AudioEngine::editGlobalMappingRow (const remotemap::Row& row, const std::function<bool (prefs::MidiPrefs&)>& edit)
{
    if (! row.global)
        return false;
    auto p = midiPrefs;
    if (! edit (p))
        return false;
    setMidiPrefs (p, saveGlobalLinks);
    return true;
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
    startOnInput (WaitInput::automation); // (round 190oa, DF-10: FL's moved parameter starts a wait for input)

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

void AudioEngine::setLiveAutomationOverride (bool liveOverride) noexcept
{
    liveAutomationOverride = liveOverride;
    if (rackProcessor != nullptr)
        rackProcessor->setKeepReleasedOnPlay (liveOverride);
}

bool AudioEngine::automationOverridden() const
{
    std::vector<int> one;
    for (const auto& t : releasedTargets)
    {
        RackState::slotsOfTarget (song, t, one);
        if (! one.empty())
            return true;
    }
    return false;
}

void AudioEngine::reEnableAutomation()
{
    if (releasedTargets.empty())
        return;
    releasedTargets.clear();
    if (rackProcessor != nullptr)
        rackProcessor->setReleasedSlots ({});
    sendChangeMessage();
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
    rackProcessor->setReleasedSlots (slots);
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
    // round 190mx (GitHub #198, Live's Session automation): Session Record (a clip slot recording, or overdubbing the
    // playing clips) writes the moved parameters into those clips' envelopes (updateSlotRecording places them)
    const auto sessionMode = ! active && transport.playing.load() && isSessionRecording();
    const auto recording = (active || sessionMode) && recordOptions.recordAutomation;
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
    // round 190pa (P21, Touch): the ended gestures written into an automation clip or a lane glide back to the curve
    const auto writeMode = automodes::fromInt (song.automationWriteMode);
    auto glideTargets = [&] (const std::vector<AutomationTarget>& ended, bool toSession) {
        std::vector<AutomationTarget> out;
        if (! automodes::glidesBack (writeMode) || toSession)
            return out;
        const auto songMode = song.mode == PlayMode::song;
        const auto style = (int) layoutstyle::current();
        for (const auto& target : ended)
            if (std::any_of (takeWrites.begin(), takeWrites.end(), [&] (const AutomationWrite& w) {
                    return w.target == target && w.lastLocal >= 0 && w.automation >= 0 && w.hadCurve
                           && recordedAutomationDestination (songMode, style, target) != AutomationDestination::patternEvents;
                }))
                out.push_back (target);
        return out;
    };

    auto commit = [&] (const std::vector<AutomationTarget>& ended, bool toSession) {
        const auto glides = glideTargets (ended, toSession);
        if (glides.empty() && std::none_of (touches.begin(), touches.end(), [] (const Touch& t) { return ! t.pending.empty(); }))
            return;
        if (toSession) // (round 190mx: the session's clips take them when they exist; updateSlotRecording)
        {
            for (auto& t : touches)
                if (! t.pending.empty())
                {
                    recordedAutomationPoints += (int) t.pending.size();
                    routeSessionAutomation (t.target, std::move (t.pending));
                    t.pending.clear();
                }
            return;
        }
        const auto songMode = song.mode == PlayMode::song;
        // round 190mx (GitHub #198): where Song mode writes depends on the layout. Clip-launch (Live's Arrangement
        // automation): the parameter's lane under its track. Pattern (FL, live check 190mx): a generator's own parameter
        // goes into the current pattern as event data, as in Pattern mode; the other targets make automation clips.
        const auto style = layoutstyle::current();
        editSong ([&] (Song& s) {
            for (auto& t : touches)
                if (! t.pending.empty())
                {
                    auto& w = writeFor (t.target);
                    recordedAutomationPoints += (int) t.pending.size();
                    switch (recordedAutomationDestination (songMode, (int) style, t.target))
                    {
                        case AutomationDestination::lane:
                            commitLanePoints (s, w, std::move (t.pending), grid, automationLinear || noteTakeLinear);
                            break;
                        case AutomationDestination::automationClip:
                            commitAutomationPoints (s, w, std::move (t.pending), grid, automationLinear || noteTakeLinear);
                            break;
                        case AutomationDestination::patternEvents:
                            commitEventPoints (s, automationPattern, w, std::move (t.pending), automationLoopLength, grid);
                            break;
                    }
                    t.pending.clear();
                }
            if (! glides.empty()) // (round 190pa: Touch's return glide, Pro Tools' AutoMatch)
            {
                const auto ticks = automodes::returnTicks (s.bpm, ppq);
                for (const auto& target : glides)
                    for (const auto& w : takeWrites)
                        if (w.target == target && w.automation >= 0 && w.automation < (int) s.automations.size())
                            glideBackToCurve (s.automations[(std::size_t) w.automation], w, ticks);
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
        rackProcessor->setOverriddenSlots (slots);
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
        {
            // round 190pa (P21): a take that stops while controls are touched ends their gestures (Touch glides back)
            std::vector<AutomationTarget> stopping;
            if (automodes::glidesBack (writeMode) && ! takeWrites.empty())
                for (const auto& t : touches)
                    stopping.push_back (t.target);
            commit (stopping, std::exchange (sessionAutomationPass, false));
        }
        // round 190pa (P21): Capture automation's play passes (Latch / Write extend a captured gesture to them)
        if (transport.playing.load() && ! active)
            automationCapture.notePlaying (estimatedPositionNow());
        else
            automationCapture.noteStopped();
        // round 190op (P7): playing without recording, the moved controls go into Capture automation's buffer
        if (transport.playing.load() && ! active && ! touches.empty())
        {
            const auto songTick = estimatedPositionNow();
            for (const auto& t : touches)
            {
                const auto value = currentTargetValue (t.target);
                if (value < 0.0f)
                    continue;
                captureauto::Entry e;
                e.target = t.target;
                e.ms = now;
                e.tick = songTick;
                e.value = juce::jlimit (0.0f, 1.0f, value);
                e.songMode = song.mode == PlayMode::song;
                e.pass = automationCapture.currentPass(); // (round 190pa)
                if (! sessionClipOf (t.target, e.clipPattern, e.clipTick, e.clipEnd))
                    e.clipPattern = -1;
                automationCapture.add (e);
            }
        }
        touches.clear();
        overrideTouched();
        if (! active)
        {
            growClips (lastAutomationTick, true);
            takeWrites.clear();
            automationTakeStart = -1.0; // (round 190pa)
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
                                                           .effectiveLength (song.meter());
    }

    const auto tick = sessionMode ? slotClockNow() : estimatedPositionNow(); // (the session's clips run on the launcher's clock)
    if (automationTakeStart < 0.0) // round 190pa (P21, Write): where the take started
        automationTakeStart = tick;

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
        const auto value = t.liveValue >= 0.0f ? t.liveValue : currentTargetValue (t.target);
        if (value < 0.0f)
        {
            ended.push_back (t.target);
            continue;
        }
        // FL's gesture rule (held, or moved within 350 ms) in Read; Latch / Write keep writing until the take stops, Write
        // from the take's start (round 190pa, core/AutomationModes.h)
        if (! automationTakeFrame (song.automationWriteMode, t.held, now - t.lastMs, tick, value, automationTakeStart, t.started, t.pending))
            ended.push_back (t.target);
    }

    // song mode without a loop region: from the first move on, the song plays on past its end until recording stops
    if (! sessionMode && (! takeWrites.empty() || ! touches.empty()))
        if (! automationLinear && song.mode == PlayMode::song && ! song.loopPlays())
        {
            automationLinear = true;
            updateLinearSong();
        }
    if (song.mode == PlayMode::song && ! sessionMode)
        lastAutomationTick = songTickForRecorded (song, tick, automationLinear || noteTakeLinear);

    if (! ended.empty() || --automationCommitCountdown <= 0)
    {
        if (! sessionMode)
            growClips (lastAutomationTick, false);
        commit (ended, sessionMode);
        automationCommitCountdown = 6; // about 5 writes per second while a control keeps moving
    }

    touches.erase (std::remove_if (touches.begin(), touches.end(),
                                   [&] (const Touch& t) { return std::find (ended.begin(), ended.end(), t.target) != ended.end(); }),
                   touches.end());
    overrideTouched();
    sessionAutomationPass = sessionMode;
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
    // round 190hp: the Map Mode's mappings kept for every project that act here (the project's own win)
    const auto globalMappings = midiPrefs.globalMappings.empty() ? std::vector<RemoteMapping> {} : remotemap::activeGlobalMappings (song, midiPrefs);
    {
        std::uint64_t bits[2] {}, notes[2] {};
        for (const auto& link : links)
            if (link.cc >= 0 && link.cc < 128)
                bits[link.cc >> 6] |= std::uint64_t { 1 } << (link.cc & 63);
            else if (isNoteLink (link.cc))
                notes[(link.cc - linkNoteBase) >> 6] |= std::uint64_t { 1 } << ((link.cc - linkNoteBase) & 63);
        remotemap::usedNumbers (song, bits, notes); // (round 190ef: the MIDI mappings of slots, scenes, transport)
        remotemap::usedNumbers (globalMappings, bits, notes); // (round 190hp: and those for every project)
        for (int w = 0; w < 2; ++w)
        {
            linkedCcs[(std::size_t) w].store (bits[w], std::memory_order_relaxed);
            linkedNotes[(std::size_t) w].store (notes[w], std::memory_order_relaxed);
        }
    }
    if (mapMode == remotemap::Mode::midi)
        return;

    // round 190ef: the MIDI mappings of clip slots, scenes, track stop buttons and transport switches (presses and releases)
    if (! song.remoteMappings.empty() || ! globalMappings.empty())
        for (const auto& ev : events)
        {
            auto& previous = remoteValues.try_emplace ({ ev.cc, ev.channel, ev.port }, -1).first->second;
            const auto fires = remotemap::midiFires (song, globalMappings, ev.channel, ev.cc, ev.value, previous, ev.port);
            previous = ev.value;
            if (! fires.empty()) // round 190ld: the MIDI light's "handled" colour
                ++midiProjectLinked;
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
            // round 190ld (FL's Sync indicator): a project link (the list's first part) or a global one took it
            if (&link - links.data() < (std::ptrdiff_t) song.midiLinks.size())
                ++midiProjectLinked;
            else
                ++midiGlobalLinked;
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
    // round 190gn (A1): the racks' pitch-shifter stocks back to their reserve (a voice took one: a live keyboard on a
    // keep-pitch channel); nothing to do, and no allocation, when none was taken
    shifterstock::topUpAll();
    if (++hwTicks >= 5) // (6 times a second)
    {
        hwTicks = 0;
        pumpEdisonRecordings(); // (round 190cd)
        applyHardwareInsertPings();
    }
    rt::promotePending(); // Linux: real-time priority for the audio threads seen since the last tick (round 161)
    updateAudition(); // (round 190ao: the preset audition's note starts and ends here)
    applyControllers();
    pumpGrossBeatPicks();
    controllerIo.pump (controllerLearnTick); // (controller profiles, round 190aw)
    // playback started: the moved controls follow their automation again (the renderer cleared its own mask)
    if (const auto playing = transport.playing.load(); playing != std::exchange (releasedWasPlaying, playing) && playing
                                                       && ! liveAutomationOverride) // (round 190oa: Live keeps them until Re-Enable)
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
    // (round 190of: compared in place; the list is made only when it changed)
    if (! publishing && (rateChanged || lastPublishedShape.latenciesDiffer (lastPublishedShape.latencies))) // (not inside a publish: see publishSong)
    {
        const auto latencies = lastPublishedShape.currentLatencies();
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
        // round 190ok: the safe-overload guard's view of the UI, and FL's Settings › Audio › "Safe overloads"
        overloadGuard.uiPulse (now);
        overloadGuard.setEnabled (flsettings::options().safeOverloads.load (std::memory_order_relaxed));
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

    // round 190gg (B5): the dropout log (how many, the last one's time and cause)
    const auto stats = rtStats.snapshot();
    {
        dropouts::Poll poll;
        poll.nowMs = juce::Time::getMillisecondCounterHiRes();
        poll.overBudget = stats.overBudget;
        poll.lateCalls = stats.lateCalls;
        auto* device = deviceManager.getCurrentAudioDevice();
        poll.deviceXruns = device != nullptr ? device->getXRunCount() : -1;
        poll.windowMaxLoad = rtStats.takeDropoutWindowMaxLoad();
        poll.lastOverSlowest = stats.lastOverSlowest;
        poll.lastOverLoad = stats.lastOverLoad;
        dropoutLog.update (poll);
    }

    // a line in the glitch log when callbacks ran over their deadline or the device called late
    if ((stats.overBudget > loggedOver || stats.lateCalls > loggedLate) && juce::Time::getMillisecondCounterHiRes() - lastLogMs > 2000.0)
    {
        // (appended by fileJobs: this runs on the message thread, and at a small buffer glitches come every 2 s)
        const auto bar = transport.positionTicks.load() / (double) song.barTicks() + 1.0;
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

juce::String AudioEngine::unitDisplayName (std::uint32_t unit) const
{
    if (unit == 0)
        return {};
    if (dropouts::isChannel (unit))
    {
        const auto c = (std::size_t) dropouts::channelOf (unit);
        return c < song.channels.size() ? juce::String::fromUTF8 (song.channels[c].name.c_str()) : "Channel " + juce::String ((int) c + 1);
    }
    const auto k = dropouts::effectKeyOf (unit);
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
}

AudioEngine::DropoutInfo AudioEngine::dropoutInfo() const
{
    DropoutInfo d;
    d.count = dropoutLog.count();
    d.processing = dropoutLog.processing();
    d.system = dropoutLog.system();
    d.secondsAgo = dropoutLog.secondsSince (juce::Time::getMillisecondCounterHiRes());
    d.cause = dropoutLog.cause();
    d.load = dropoutLog.load();
    if (const auto slowest = dropoutLog.slowest(); slowest != 0)
    {
        d.slowest = unitDisplayName (dropouts::unitOf (slowest));
        d.slowestMs = (double) dropouts::nanosecondsOf (slowest) * 1.0e-6;
    }
    if (auto* device = const_cast<LynxDeviceManager&> (deviceManager).getCurrentAudioDevice())
        if (device->getCurrentSampleRate() > 0.0)
            d.bufferMs = 1000.0 * device->getCurrentBufferSizeSamples() / device->getCurrentSampleRate();
    return d;
}

const renderahead::Stats* AudioEngine::renderAheadStats() const noexcept
{
    return rackProcessor != nullptr ? &rackProcessor->renderAheadStats() : nullptr;
}

void AudioEngine::setEditorOpen (const void* key, std::function<PluginRef* (Song&)> find, int track, bool open)
{
    openEditors.erase (std::remove_if (openEditors.begin(), openEditors.end(), [key] (const OpenEditor& e) { return e.key == key; }), openEditors.end());
    if (open)
        openEditors.push_back ({ key, std::move (find), track });
    publishRenderAheadTargets();
}

void AudioEngine::setRenderAheadAlways (bool always) noexcept
{
    if (rackProcessor != nullptr)
        rackProcessor->setRenderAheadAlways (always);
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
        if (const auto raised = rtThreadsRaised.load (std::memory_order_relaxed); raised > 0)
            text << "  audio threads raised " << raised << ": MMCSS Pro Audio " << rtThreadsMmcss.load (std::memory_order_relaxed)
                 << " (critical " << rtThreadsCritical.load (std::memory_order_relaxed) << "), thread priority "
                 << rtPriorityMin.load (std::memory_order_relaxed) << ".." << rtPriorityMax.load (std::memory_order_relaxed) << "\n";
        text << "  process priority class " << priorityClassName (priorityClassNow.load (std::memory_order_relaxed)) << " (found "
             << priorityClassName (priorityClassFound.load (std::memory_order_relaxed)) << ")\n";
       #endif
    }
    else
        text << "  device: none\n";
    const auto s = rtStats.snapshot();
    text << "  callbacks " << (juce::int64) s.callbacks << ": over deadline " << (juce::int64) s.overBudget << ", 80-100% " << (juce::int64) s.nearBudget
         << ", late device calls " << (juce::int64) s.lateCalls << ", average load " << juce::String (s.averageLoad * 100.0, 1) << "%, max "
         << juce::String (s.maxLoad * 100.0, 0) << "%, workers " << (workers != nullptr ? workers->workers() : 0) << "\n";
    if (const auto d = dropoutInfo(); d.count > 0) // (round 190gg)
        text << "  dropouts " << (juce::int64) d.count << " (processing " << (juce::int64) d.processing << ", driver / system " << (juce::int64) d.system
             << "), the last " << d.secondsAgo << " s ago: " << (d.cause == dropouts::Cause::system ? "driver / system late" : "processing")
             << ", load " << juce::String (d.load * 100.0, 0) << "%"
             << (d.slowest.isNotEmpty() ? ", slowest " + d.slowest + " " + juce::String (d.slowestMs, 2) + " ms" : juce::String()) << "\n";
    if (overloadGuard.timesEngaged() > 0 || overloadGuard.engaged()) // (round 190ok: Settings › Audio › "Safe overloads")
        text << "  safe overloads: engaged " << (juce::int64) overloadGuard.timesEngaged() << " times" << (overloadGuard.engaged() ? " (now)" : "")
             << ", the audio gave up " << juce::String (overloadGuard.yieldedMs(), 0) << " ms to keep the screen responsive\n";
    if (latencyRepublishes > 0)
        text << "  latency changes re-planned PDC " << latencyRepublishes << " times (last: unit " << lastLatencyChange << ")\n";
    if (rackProcessor != nullptr) // (round 190gq: Options › Audio settings › "Render ahead")
    {
        const auto& r = rackProcessor->renderAheadStats();
        auto n = [] (const std::atomic<std::uint64_t>& v) { return juce::String ((juce::int64) v.load (std::memory_order_relaxed)); };
        text << "  render ahead: " << (renderAheadOption().load() ? "on" : "off") << ", " << (r.engaged.load() ? "running" : "not running")
             << ", window " << r.window.load() << " samples; blocks " << n (r.blocks) << ": from the ring " << n (r.fromRing) << ", waited "
             << n (r.waited) << ", rendered in the callback " << n (r.inline_) << "; dropped " << n (r.flushes) << ", drained " << n (r.drains)
             << ", started " << n (r.engagements) << ", states taken at once " << n (r.statesSwapped) << ", channels joined live " << n (r.promoted)
             << ", partition changes " << n (r.partitionChanges) << ", boundary mismatches " << n (r.splitMismatches) << "; ahead "
             << r.aheadChannels.load() << " channels / " << r.aheadTracks.load() << " inserts, boundary " << r.boundary.load() << "\n";
        // (perfgate190id) the engagement rule's probes: how often, what they kept, and the last one's measurements
        text << "  render ahead probes " << n (r.probes) << ": kept on " << n (r.keptOn) << " (for fewer overruns " << n (r.keptForOverruns)
             << "), kept off " << n (r.keptOff) << "; last probe CPU " << juce::String (r.probeCpuOff.load(), 1) << "% off / "
             << juce::String (r.probeCpuOn.load(), 1) << "% on, overruns " << juce::String (r.probeOverOff.load(), 0) << " / "
             << juce::String (r.probeOverOn.load(), 0) << " per minute\n";
    }
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

bool AudioEngine::holdForExport (int timeoutMs)
{
    if (exportHold.load (std::memory_order_acquire))
        return true;
    // (a recording or a real-time export plays on through the device: their plugins can't be lent)
    if (transport.recording.load() || bouncing || isCountingIn())
        return false;
    // FL stops playback for its render (REA_rodada155 / 190jb)
    if (transport.playing.load())
        stop();
    exportHold.store (true, std::memory_order_seq_cst);
    // the block in progress ends; every later one outputs silence without the rack
    const auto until = juce::Time::getMillisecondCounterHiRes() + juce::jmax (0, timeoutMs);
    while (callbackBusy.load (std::memory_order_seq_cst))
    {
        if (juce::Time::getMillisecondCounterHiRes() > until)
        {
            exportHold.store (false, std::memory_order_seq_cst);
            return false;
        }
        juce::Thread::sleep (1);
    }
    // the part rendered ahead stops too (its thread may be in a block of its own)
    if (rackProcessor != nullptr)
        rackProcessor->haltRenderAhead();
    return true;
}

void AudioEngine::releaseAfterExport()
{
    if (! exportHold.load (std::memory_order_acquire))
        return;
    if (rackProcessor != nullptr)
        rackProcessor->resumeRenderAhead();
    exportHold.store (false, std::memory_order_seq_cst);
    // the export's rack gave the lent units its own routes (a plugin's outputs, Connections, its side input): this
    // song's come back with a build of it
    publishSong();
}

void AudioEngine::setLiveUnits (LiveInstrumentSource instrument, LiveEffectSource effect)
{
    lentUnits.clear();
    if (instrument)
        liveInstrumentSource = [this, instrument = std::move (instrument)] (const ChannelDef& c, double rate) {
            auto unit = instrument (c, rate);
            if (unit != nullptr)
                lentUnits.push_back (unit.get());
            return unit;
        };
    else
        liveInstrumentSource = nullptr;
    if (effect)
        liveEffectSource = [this, effect = std::move (effect)] (const EffectSlot& e, double rate) {
            auto unit = effect (e, rate);
            if (unit != nullptr)
                lentUnits.push_back (unit.get());
            return unit;
        };
    else
        liveEffectSource = nullptr;
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

void AudioEngine::markCreatedFrom (const juce::File& file)
{
    if (song.info.created <= 0.0)
        if (const auto born = platform::fileBirthSeconds (file); born > 0.0)
            song.info.created = born;
}

void AudioEngine::pumpGrossBeatPicks()
{
    std::vector<std::pair<AutomationTarget, float>> changes;
    const auto& effects = lastPublishedShape.effects;
    for (std::size_t t = 0; t < song.mixer.size() && t < effects.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size() && s < effects[t].size(); ++s)
        {
            const auto& fx = song.mixer[t].effects[s];
            if (fx.kind != EffectKind::grossBeat)
                continue;
            auto* picks = grossbeat::midiPicksOf (effects[t][s].get());
            if (picks == nullptr)
                continue;
            const auto serial = picks->serial.load (std::memory_order_acquire);
            if (serial == picks->taken.load (std::memory_order_relaxed))
                continue;
            picks->taken.store (serial, std::memory_order_relaxed);
            for (const auto& [param, value] : grossbeat::paramsForPicks (*picks, fx.params))
            {
                AutomationTarget target;
                target.kind = AutomationTargetKind::effectParam;
                target.index = (int) t;
                target.slot = (int) s;
                target.param = param;
                changes.push_back ({ target, value });
            }
        }
    if (! changes.empty())
        applyTargetValues (changes);
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

void AudioEngine::capturePluginStatesInto (Song& target, bool requireAll)
{
    // round 190kx: the ARA plugins' documents (their edits) as they are now; a document not made here (its plugin is
    // missing) keeps what the project had
    if (araHost != nullptr && (ara::anyAraChannel (target) || ara::anyAraInsert (target))) // (round 190oq: ARA inserts too)
        for (auto& doc : araHost->archives())
        {
            auto it = std::find_if (target.araDocuments.begin(), target.araDocuments.end(), [&doc] (const AraDocumentArchive& a) { return a.pluginId == doc.pluginId; });
            if (it != target.araDocuments.end())
                *it = std::move (doc);
            else
                target.araDocuments.push_back (std::move (doc));
        }

    // round 190ox: a modulated parameter of a hosted plugin is saved at its base (the user's value), not mid-swing
    const auto basesOf = [&target] (AutomationTargetKind kind, int index, int slot) {
        std::vector<std::pair<int, float>> bases;
        for (const auto& m : target.modulators)
            for (const auto& r : m.routes)
                if (r.target.kind == kind && r.target.index == index && (kind != AutomationTargetKind::effectParam || r.target.slot == slot)
                    && r.base >= 0.0f && r.depth != 0.0f && r.target.param >= 0)
                    bases.emplace_back (r.target.param, std::clamp (r.base, 0.0f, 1.0f));
        return bases;
    };
    const auto capture = [requireAll] (auto* plugin, const std::vector<std::pair<int, float>>& bases = {}) {
        auto state = bases.empty() ? plugin->captureState() : plugin->captureState (bases);
        if (! state && requireAll) throw RemoteError ("failed", "Cannot capture a live device; no project changes were made");
        return state;
    };
    // round 190mc (discussion #202): a Transistor Bass whose notes chose another program (its sequencer on, FL's AF)
    // saves that program, as FL does, whether or not its page showed it
    for (std::size_t i = 0; i < target.channels.size(); ++i)
        if (target.channels[i].instrument == InstrumentKind::transistorbass)
            if (const auto instrument = getChannelInstrument ((int) i))
                if (const auto selected = instrument->selectedProgram(); selected >= 0)
                    if (const auto prog = transistorBassProgram (target.channels[i].pluginState); prog && prog->program != selected)
                        transistorBassSelectProgram (target.channels[i].pluginState, selected);

    // Changes only the saved state (recreates nothing and doesn't enter the undo history).
    for (std::size_t i = 0; i < target.channels.size(); ++i)
        if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument ((int) i).get()))
            if (target.channels[i].plugin.playedBy == 0) // (P5-A1: a plugin shared in a live session keeps no state, only values)
                if (auto state = capture (p, basesOf (AutomationTargetKind::channelParam, (int) i, 0))) // (a plugin that crashed keeps the state saved before)
                    target.channels[i].plugin.state = std::move (*state);

    for (std::size_t t = 0; t < target.mixer.size(); ++t)
        for (std::size_t s = 0; s < target.mixer[t].effects.size(); ++s)
            if (auto* p = dynamic_cast<PluginEffect*> (getEffect ((int) t, (int) s).get()))
                if (target.mixer[t].effects[s].plugin.playedBy == 0)
                    if (auto state = capture (p, basesOf (AutomationTargetKind::effectParam, (int) t, (int) s)))
                        target.mixer[t].effects[s].plugin.state = std::move (*state);

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
                    if (auto state = capture (p))
                        node.channel.plugin.state = std::move (*state);
                if (node.channel.instrument == InstrumentKind::patcher)
                    captureGraph (node.channel.patcher, patcher::graphOf (instrument.get()));
            }
            else
            {
                auto effect = graph->effectAt (n);
                if (auto* p = dynamic_cast<PluginEffect*> (effect.get()))
                    if (auto state = capture (p))
                        node.effect.plugin.state = std::move (*state);
                if (node.effect.kind == EffectKind::patcher)
                    captureGraph (node.effect.patcher, patcher::graphOf (effect.get()));
            }
        }
    };
    for (std::size_t i = 0; i < target.channels.size(); ++i)
        if (target.channels[i].instrument == InstrumentKind::patcher)
            captureGraph (target.channels[i].patcher, patcher::graphOf (getChannelInstrument ((int) i).get()));
    for (std::size_t t = 0; t < target.mixer.size(); ++t)
        for (std::size_t s = 0; s < target.mixer[t].effects.size(); ++s)
            if (target.mixer[t].effects[s].kind == EffectKind::patcher)
                captureGraph (target.mixer[t].effects[s].patcher, patcher::graphOf (getEffect ((int) t, (int) s).get()));

}

void AudioEngine::setDeactivated (const std::vector<int>& channels, const std::vector<int>& inserts, bool off)
{
    if (off)
        capturePluginStates(); // (their running plugins' settings go into the song before the instances are released)
    editSong ([&] (Song& s) {
        deactivate::setChannels (s, channels, off);
        deactivate::setInserts (s, inserts, off);
    });
}

void AudioEngine::capturePluginStates()
{
    stampWorkTime();
    flushEdisonAudio(); // (round 190cd: an Edison slot's audio is a file of the project)
    capturePluginStatesInto (song, false);
    lastPublishedShape.song.channels = song.channels;
    lastPublishedShape.song.mixer = song.mixer;
    // round 190op: the shape keeps the deactivated units' placeholders it was built with (a later build compares with it)
    if (deactivate::Effective off { deactivatedChannels, deactivatedInserts, true }; ! deactivatedChannels.empty() || ! deactivatedInserts.empty())
        deactivate::projectUnits (lastPublishedShape.song.channels, lastPublishedShape.song.mixer, off);
}

Song AudioEngine::snapshotForRemoteControl()
{
    if (getTransport().playing.load() || isRecording())
        throw RemoteError ("busy", "Stop playback and recording before capturing opaque device state");
    // The model belongs to the message thread. Live state capture runs beside processing (no lock, as on JUCE's own
    // hosts); only a restore holds a hosted plugin's callback lock. Do not stop the audio callback while copying.
    auto snapshot = song;
    capturePluginStatesInto (snapshot, false);
    return snapshot;
}

juce::String AudioEngine::remoteDeviceStateHash()
{
    return remoteDeviceStateHashOf (snapshotForRemoteControl());
}

juce::String AudioEngine::remoteDeviceStateHashOf (const Song& snapshot)
{
    Song devices;
    devices.channels = snapshot.channels;
    devices.mixer = snapshot.mixer;
    const auto json = ProjectFile::toJsonExact (devices).toStdString();
    return juce::String (sha256Hex (reinterpret_cast<const std::uint8_t*> (json.data()), json.size()));
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
void preserveRemoteTags (const Song& before, Song& after)
{
    std::function<void (const PatcherDoc&, PatcherDoc&)> graph;
    graph = [&] (const PatcherDoc& old, PatcherDoc& next) {
        for (std::size_t i = 0; i < std::min (old.nodes.size(), next.nodes.size()); ++i)
        {
            const auto& prior = old.nodes[i];
            auto& node = next.nodes[i];
            if (node.generator != prior.generator) continue;
            if (node.generator)
            {
                node.channel.instance = prior.channel.instance;
                graph (prior.channel.patcher, node.channel.patcher);
            }
            else
            {
                node.effect.instance = prior.effect.instance;
                graph (prior.effect.patcher, node.effect.patcher);
            }
        }
    };
    for (auto& channel : after.channels)
        if (channel.uid != 0)
            for (const auto& prior : before.channels)
                if (channel.uid == prior.uid && channel.instrument == prior.instrument && channel.plugin.id == prior.plugin.id
                    && ! (channel.plugin.state.empty() && ! prior.plugin.state.empty()))
                {
                    channel.instance = prior.instance;
                    graph (prior.patcher, channel.patcher);
                    break;
                }
    for (std::size_t t = 0; t < std::min (before.mixer.size(), after.mixer.size()); ++t)
    {
        const auto& old = before.mixer[t].effects;
        auto& next = after.mixer[t].effects;
        std::vector<bool> used (old.size(), false);
        std::vector<int> match (next.size(), -1);
        // Keep tags already carried through an edit; parsed project documents need value matching.
        for (std::size_t i = 0; i < next.size(); ++i)
            for (std::size_t j = 0; j < old.size(); ++j)
                if (! used[j] && next[i].instance.id == old[j].instance.id)
                { match[i] = (int) j; used[j] = true; break; }
        for (std::size_t i = 0; i < next.size(); ++i)
            if (match[i] < 0)
                for (std::size_t j = 0; j < old.size(); ++j)
                    if (! used[j] && next[i] == old[j])
                    { match[i] = (int) j; used[j] = true; break; }
        for (std::size_t i = 0; i < next.size(); ++i)
            if (match[i] < 0 && i < old.size() && ! used[i] && next[i].kind == old[i].kind && next[i].plugin.id == old[i].plugin.id
                && ! (next[i].plugin.state.empty() && ! old[i].plugin.state.empty()))
            { match[i] = (int) i; used[i] = true; }
        for (std::size_t i = 0; i < next.size(); ++i)
            if (match[i] >= 0)
            {
                const auto& prior = old[(std::size_t) match[i]];
                next[i].instance = prior.instance;
                graph (prior.patcher, next[i].patcher);
            }
    }
}

void validateRemoteStates (const Song& song)
{
    const auto check = [] (const PluginRef& ref) {
        if (ref.state.empty()) return;
        juce::MemoryBlock bytes;
        if (! bytes.fromBase64Encoding (juce::String::fromUTF8 (ref.state.c_str())))
            throw RemoteError ("invalid", "Hosted device state must use the native base64 encoding");
    };
    std::function<void (const PatcherDoc&)> graph;
    graph = [&] (const PatcherDoc& doc) {
        for (const auto& node : doc.nodes)
            if (node.generator)
            {
                if (node.channel.instrument == InstrumentKind::plugin) check (node.channel.plugin);
                graph (node.channel.patcher);
            }
            else
            {
                if (node.effect.kind == EffectKind::plugin) check (node.effect.plugin);
                graph (node.effect.patcher);
            }
    };
    for (const auto& channel : song.channels)
    {
        if (channel.instrument == InstrumentKind::plugin) check (channel.plugin);
        graph (channel.patcher);
    }
    for (const auto& track : song.mixer)
        for (const auto& effect : track.effects)
        {
            if (effect.kind == EffectKind::plugin) check (effect.plugin);
            graph (effect.patcher);
        }
}
}

void AudioEngine::syncRemotePluginStates (const Song& before)
{
    std::map<std::uint64_t, std::string> previous;
    std::function<void (const PatcherDoc&)> collectGraph;
    collectGraph = [&] (const PatcherDoc& doc) {
        for (const auto& node : doc.nodes)
            if (node.generator)
            {
                previous[node.channel.instance.id] = node.channel.plugin.state;
                collectGraph (node.channel.patcher);
            }
            else
            {
                previous[node.effect.instance.id] = node.effect.plugin.state;
                collectGraph (node.effect.patcher);
            }
    };
    for (const auto& channel : before.channels)
    {
        previous[channel.instance.id] = channel.plugin.state;
        collectGraph (channel.patcher);
    }
    for (const auto& track : before.mixer)
        for (const auto& effect : track.effects)
        {
            previous[effect.instance.id] = effect.plugin.state;
            collectGraph (effect.patcher);
        }
    const auto apply = [&] (auto* plugin, std::uint64_t identity, const std::string& desired) {
        const auto old = previous.find (identity);
        // A new instance was loaded with its target state by the normal factory. An unchanged
        // instance needs no state call: saving bytes must never reset unrelated GUI state.
        if (old == previous.end() || old->second == desired) return;
        const auto current = plugin->captureState();
        if (! current || (*current != desired && ! plugin->restoreState (desired)))
            throw RemoteError ("uncertain", "The project changed but a running device could not restore its state; inspect it before further history actions", true);
    };
    std::function<void (const PatcherDoc&, patcher::Graph*)> graph;
    graph = [&] (const PatcherDoc& doc, patcher::Graph* live) {
        if (live == nullptr) return;
        for (int i = 0; i < (int) doc.nodes.size() && i < live->nodeCount(); ++i)
        {
            const auto& node = doc.nodes[(std::size_t) i];
            if (node.generator)
            {
                auto instance = live->instrumentAt (i);
                if (auto* p = dynamic_cast<PluginInstrument*> (instance.get())) apply (p, node.channel.instance.id, node.channel.plugin.state);
                if (node.channel.instrument == InstrumentKind::patcher) graph (node.channel.patcher, patcher::graphOf (instance.get()));
            }
            else
            {
                auto instance = live->effectAt (i);
                if (auto* p = dynamic_cast<PluginEffect*> (instance.get())) apply (p, node.effect.instance.id, node.effect.plugin.state);
                if (node.effect.kind == EffectKind::patcher) graph (node.effect.patcher, patcher::graphOf (instance.get()));
            }
        }
    };
    for (std::size_t i = 0; i < song.channels.size(); ++i)
    {
        auto instance = getChannelInstrument ((int) i);
        if (auto* p = dynamic_cast<PluginInstrument*> (instance.get())) apply (p, song.channels[i].instance.id, song.channels[i].plugin.state);
        if (song.channels[i].instrument == InstrumentKind::patcher) graph (song.channels[i].patcher, patcher::graphOf (instance.get()));
    }
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t i = 0; i < song.mixer[t].effects.size(); ++i)
        {
            auto instance = getEffect ((int) t, (int) i);
            if (auto* p = dynamic_cast<PluginEffect*> (instance.get())) apply (p, song.mixer[t].effects[i].instance.id, song.mixer[t].effects[i].plugin.state);
            if (song.mixer[t].effects[i].kind == EffectKind::patcher) graph (song.mixer[t].effects[i].patcher, patcher::graphOf (instance.get()));
        }
}

void AudioEngine::failRemoteHistory (bool uncertain)
{
    remoteHistoryFailed = true;
    remoteHistoryUncertain = uncertain;
    remoteHistoryCode = uncertain ? "uncertain" : "failed";
    remoteHistoryError = uncertain
        ? tr ("The history operation did not finish; the project or its history may have changed. Inspect before further undo/redo.")
        : tr ("The history operation could not be prepared. No history step was moved; inspect the device before retrying.");
    try { juce::Logger::writeToLog (remoteHistoryError); } catch (...) {}
}

bool AudioEngine::checkRemoteHistory (const juce::String& expected, const Song& current)
{
    if (expected.isNotEmpty() && expected == remoteDeviceStateHashOf (current)) return true;
    remoteHistoryFailed = true;
    remoteHistoryUncertain = false;
    remoteHistoryCode = "changed";
    remoteHistoryError = tr ("A device changed after this remote edit. Undo/redo was refused to preserve those changes.");
    juce::Logger::writeToLog (remoteHistoryError);
    return false;
}

juce::String AudioEngine::finishRemoteHistory (const std::optional<Song>& before)
{
    if (! before) return {};
    try
    {
        syncRemotePluginStates (*before);
        auto current = song;
        capturePluginStatesInto (current, false);
        const auto fence = remoteDeviceStateHashOf (current);
        adoptCapturedRemoteDevices (song, lastPublishedShape, std::move (current));
        return fence;
    }
    catch (...)
    {
        failRemoteHistory (true);
        return {};
    }
}

bool AudioEngine::editSongRemoteTransaction (const std::function<void (Song&)>& stage, const std::function<bool()>& cancelled, bool preserveRuntimeIdentity)
{
    if (getTransport().playing.load() || isRecording() || juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
        throw RemoteError ("busy", "Stop playback/recording and finish the current gesture before remote edits");
    // Staging, serialization and publication stay off the audio-device lock. A hosted state restore holds only that
    // plugin's callback lock (the audio side skips the block meanwhile); capture takes no lock.
    auto before = snapshotForRemoteControl();
    auto next = before;
    if (cancelled && cancelled()) throw RemoteError ("failed", "Remote operation cancelled before committing changes");
    stage (next); // all parsing, validation and allocations happen before history is changed
    if (cancelled && cancelled()) throw RemoteError ("failed", "Remote operation cancelled before committing changes");
    if (ProjectFile::toJsonExact (before) == ProjectFile::toJsonExact (next)) return false;
    validateRemoteStates (next);
    if (preserveRuntimeIdentity) preserveRemoteTags (before, next);
    const auto capturedBefore = before;
    const auto fresh = remoteFreshTargets (capturedBefore, next);
    try
    {
        song = std::move (before); // capture live state in the owned undo step, without adding a separate step
        remote_history::mirrorCapturedHostedStates (song, lastPublishedShape.song);
        lastUndoPress = {};
        gestureSaved = false;
        levelsPending = false;
        const juce::ScopedValueSetter<bool> remoteStep (nextRemoteHistoryStep, true);
        const juce::ScopedValueSetter<std::unordered_set<std::uint64_t>> freshPolicy (remoteFreshInstances, fresh);
        editSong ([&] (Song& current) { current = std::move (next); });
        syncRemotePluginStates (capturedBefore);
        auto committed = song;
        capturePluginStatesInto (committed, false);
        const auto fence = remoteDeviceStateHashOf (committed);
        adoptCapturedRemoteDevices (song, lastPublishedShape, std::move (committed));
        if (! undoStack.empty()) undoStack.back().deviceHash = fence;
        if (sessionHistory && ! sessionUndo.empty()) sessionUndo.back().deviceHash = fence;
        remoteHistoryError.clear();
    }
    catch (...)
    {
        throw RemoteError ("uncertain", "The project edit ran but its device state could not be verified; inspect before retrying", true);
    }
    return true;
}

void AudioEngine::undoRemoteControl (const std::function<bool()>& cancelled)
{
    if (getTransport().playing.load() || isRecording() || juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
        throw RemoteError ("busy", "Stop playback/recording and finish the current gesture before remote undo");
    if (cancelled && cancelled()) throw RemoteError ("failed", "Remote undo cancelled before changing history");
    const juce::ScopedValueSetter<bool> apiStep (remoteHistoryApiStep, true);
    const juce::ScopedValueSetter<std::function<bool()>> cancellation (remoteHistoryCancelled, cancelled);
    try { undo(); }
    catch (...) { throw RemoteError ("uncertain", "Native undo did not finish; its history or project may have changed. Inspect before retrying", true); }
    if (remoteHistoryFailed) throw RemoteError (remoteHistoryCode, remoteHistoryError.toStdString(), remoteHistoryUncertain);
}

void AudioEngine::redoRemoteControl (const std::function<bool()>& cancelled)
{
    if (getTransport().playing.load() || isRecording() || juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
        throw RemoteError ("busy", "Stop playback/recording and finish the current gesture before remote redo");
    if (cancelled && cancelled()) throw RemoteError ("failed", "Remote redo cancelled before changing history");
    const juce::ScopedValueSetter<bool> apiStep (remoteHistoryApiStep, true);
    const juce::ScopedValueSetter<std::function<bool()>> cancellation (remoteHistoryCancelled, cancelled);
    try { redo(); }
    catch (...) { throw RemoteError ("uncertain", "Native redo did not finish; its history or project may have changed. Inspect before retrying", true); }
    if (remoteHistoryFailed) throw RemoteError (remoteHistoryCode, remoteHistoryError.toStdString(), remoteHistoryUncertain);
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
        WorkerPool::nameCurrentThread ("LYNX audio device"); // (round 190kb: per-thread CPU lists)
        const auto before = GetThreadPriority (GetCurrentThread());
        deviceThreadPriorityBefore.store (before, std::memory_order_relaxed);
        if (boostDeviceThread && before < THREAD_PRIORITY_TIME_CRITICAL)
            raiseAudioThreadPriority();
        else
            power::optOutOfPowerThrottling(); // (a driver's own time-critical thread too: no EcoQoS, round toolchain190gt)
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
            // round 190gg: with the block's slowest channel / effect call (named if the block missed its deadline)
            const auto slowest = engine.rackProcessor != nullptr ? engine.rackProcessor->getRenderer().takeBlockSlowest() : 0;
            if (rate > 0.0)
            {
                const auto end = juce::Time::getMillisecondCounterHiRes();
                engine.rtStats.record (start, end - start, 1000.0 * samples / rate, slowest);
                // round 190ok (FL's "Safe overloads", TOP25 P24): in a real overload with the UI starving, a slice of the
                // CPU is given up after the block's output is written (core/OverloadGuard.h; round 190os: never right
                // after the transport stops, so the tails render at full speed)
                if (const auto yield = engine.overloadGuard.afterCallback (end, end - start, 1000.0 * samples / rate,
                                                                           engine.transport.playing.load (std::memory_order_relaxed));
                    yield > 0.0)
                    OverloadGuard::sleepFor (yield);
            }
        }
        AudioEngine& engine;
        double start;
        int samples;
    } measure { *this, callbackStart, numSamples };

    // round 190jb: an export may hold this engine (holdForExport: it renders with these plugins). The flag below says the
    // callback is inside its block; the hold is read after it is set (both sequentially consistent), so holdForExport,
    // which sets the hold and then waits for the flag to drop, knows no block of this engine runs the rack any more.
    callbackBusy.store (true, std::memory_order_seq_cst);
    struct LeaveBlock
    {
        std::atomic<bool>& busy;
        ~LeaveBlock() { busy.store (false, std::memory_order_seq_cst); }
    } leaveBlock { callbackBusy };

    // an export renders (round 168s): playback stops and nothing is processed until it ends (not while recording; with
    // the export hold of round 190jb, whatever happens)
    if (exportHold.load (std::memory_order_seq_cst)
        || (offlineRendersRunning().load (std::memory_order_acquire) > 0 && ! transport.recording.load (std::memory_order_relaxed)))
    {
        if (transport.playing.load (std::memory_order_relaxed))
            stop();
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
        return;
    }

    // the position the UI asked for (Rewind, a seek). Round 190hn: `playing` is read BEFORE the requests are taken. A
    // start from the UI stores its position request first and `playing` after it (seek, then play(); the real-time
    // export's startRealtimeBounce). When the start landed between this check and the loop's `playing` check below, the
    // first block played from the old position and the request was taken one block later, as a jump back to the start:
    // a real-time export one block late (the first block twice; the 0.1.29 macOS release self-test), a stutter at the
    // start of playback. If `playing` turns on after this check, the requests are taken again before the first block
    // that plays (the loop below).
    auto requestsTakenWhilePlaying = transport.playing.load (std::memory_order_acquire);
    auto takePositionRequests = [this] {
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
    };
    takePositionRequests();
    // (round 190hn, the export parity self-test only: the UI's start lands here, after the requests were taken)
    if (auto* hook = afterPositionRequests.load (std::memory_order_acquire); hook != nullptr)
        hook (afterPositionRequestsContext);

    transport.readMarkers (rtMarkers, rtMarkerVersion); // FL's marker actions (a new table when the song changed)
    // an external MIDI clock moves the transport (round 165f)
    midiSync.setRackLatencyMs (heardLatencyMs() - outputCompensationMs.load (std::memory_order_relaxed)); // (round 190hq)
    midiSync.beginBlock (transport, callbackStart, numSamples);
    // Ableton Link (round 190gl): the session's tempo and phase move the transport; a Play waiting for the session's
    // phase starts at `linkStart` (a sample of this block; -1 = none), to the sample like the count-in's end
    const auto linkStart = abletonLink.beginBlock (transport, numSamples, heardLatencyMs()); // (round 190hq: + the Master's PDC)

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
        if (linkStart > done) // (the part before the Link start plays stopped)
            n = juce::jmin (n, linkStart - done);
        else if (linkStart >= 0 && done == linkStart)
            transport.playing.store (true, std::memory_order_relaxed);
        const auto playing = transport.playing.load (std::memory_order_acquire);
        // (round 190hn) playback started after the requests were taken: the start's position request (stored before
        // `playing`, so it is visible now) applies before this part plays, not one block late
        if (playing && ! std::exchange (requestsTakenWhilePlaying, true))
            takePositionRequests();
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

        // round 190ke: the Simple tuner's input (only while the tuner shows: one relaxed load otherwise)
        if (const auto tf = tunerFirst.load (std::memory_order_acquire); tf >= 0 && tf < LiveInput::maxChannels && n > 0)
        {
            const auto* a = liveInput.channels[(std::size_t) tf];
            const auto* b = tunerCount.load (std::memory_order_relaxed) > 1 && tf + 1 < LiveInput::maxChannels ? liveInput.channels[(std::size_t) tf + 1] : nullptr;
            if (a != nullptr)
            {
                tunerTapData->sampleRate.store (transport.sampleRate.load (std::memory_order_relaxed), std::memory_order_relaxed);
                std::array<float, 256> mono;
                for (int at = 0; at < n; at += (int) mono.size())
                {
                    const auto count = juce::jmin ((int) mono.size(), n - at);
                    for (int i = 0; i < count; ++i)
                        mono[(std::size_t) i] = b != nullptr ? 0.5f * (a[at + i] + b[at + i]) : a[at + i];
                    tunerTapData->write (mono.data(), mono.data(), count);
                }
            }
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

        // (round 190gq: render-ahead follows the device callback's blocks while the rack runs inside it)
        transport.callbackSamples.store (numSamples, std::memory_order_relaxed);
        transport.callbackOffset.store (done, std::memory_order_relaxed);
        transport.callbackPlaying.store (playing, std::memory_order_relaxed); // (round 190hn: the rack plays what this part plays)
        transport.inDeviceCallback.store (true, std::memory_order_relaxed);
        player.audioDeviceIOCallbackWithContext (inputs.data(), numInputChannels, outputs.data(), numOutputChannels, n, context);
        transport.inDeviceCallback.store (false, std::memory_order_relaxed);
        routeOutputs (outputs.data(), numOutputChannels, n);
        // round 190jl: what the MIDI Out channels on a hardware output's port played in this part, timed like the clock
        if (rackProcessor != nullptr && midiSync.hasPorts())
        {
            const auto count = rackProcessor->getRenderer().takeHardwareMidi (hardwareMidiTaken.data(), (int) hardwareMidiTaken.size());
            for (int k = 0; k < count; ++k)
            {
                const auto& h = hardwareMidiTaken[(std::size_t) k];
                midiSync.channelMessage (h.port, done + h.sample, h.bytes.data(), h.size, h.source);
            }
            // the transport stopped: every note the port devices still sound ends ("Send all notes off": CC 123 too).
            // Round 190mh: from the state this part was PLAYED with (`playing`, what the rack saw), not a fresh read of
            // the transport: a stop that lands while the rack renders a part used to ask here already, so the notes-off
            // marker went into the queue (next block's beginBlock) BEFORE the rack's own note off of the next, stopped
            // part, and the held note got two note offs (the macOS CI flake "extra notes after the stop")
            if (hardwareMidiWasPlaying && ! playing)
                midiSync.requestPortNotesOff();
        }
        hardwareMidiWasPlaying = playing;
        takePluginNotes (done); // GitHub #198 (round 190ks): what plugin windows played in this part

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

        // round 190ld: the Finish dock's loudness-matched "Hear without" (the device's output only)
        if (const auto want = listenGain.load (std::memory_order_relaxed) * mainVolumeGain.load (std::memory_order_relaxed);
            want != 1.0f || listenGainNow != 1.0f)
        {
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (auto* out = outputs[(std::size_t) ch]; out != nullptr)
                {
                    if (want == listenGainNow)
                        juce::FloatVectorOperations::multiply (out, want, n);
                    else
                        for (int i = 0; i < n; ++i)
                            out[i] *= listenGainNow + (want - listenGainNow) * (float) (i + 1) / (float) n;
                }
            listenGainNow = want;
        }

        // round 190my (DF-17): the tempo tapper's mute (FL's curve, counted in song ticks while playing; stop ends it)
        if (const auto left = muteTicksLeft.load (std::memory_order_relaxed); left > 0.0)
        {
            auto expected = left; // (the UI may have started a new count meanwhile: it wins)
            if (! transport.playing.load (std::memory_order_relaxed))
                muteTicksLeft.compare_exchange_strong (expected, 0.0, std::memory_order_relaxed);
            else
            {
                const auto total = juce::jmax (left, muteTicksTotal.load (std::memory_order_relaxed));
                const auto tpsNow = transport.ticksPerSample();
                // (the gain moves along the curve within the block: from its start to its end, linearly)
                const auto rest = juce::jmax (0.0, left - n * tpsNow);
                const auto g0 = tapperMuteGain (left, total), g1 = tapperMuteGain (rest, total);
                for (int ch = 0; ch < numOutputChannels; ++ch)
                    if (auto* out = outputs[(std::size_t) ch]; out != nullptr)
                        for (int i = 0; i < n; ++i)
                            out[i] *= g0 + (g1 - g0) * (float) i / (float) n;
                muteTicksLeft.compare_exchange_strong (expected, rest, std::memory_order_relaxed);
            }
        }

        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputs[(std::size_t) ch] != nullptr)
                clipOvers (outputs[(std::size_t) ch], n);

        // oscilloscope: average of both sides into the ring buffer
        if (numOutputChannels > 0 && outputs[0] != nullptr)
        {
            auto w = scopeWrite.load (std::memory_order_relaxed);
            const auto* right = numOutputChannels > 1 && outputs[1] != nullptr ? outputs[1] : outputs[0];

            float peakL = 0.0f, peakR = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                scope[(std::size_t) w].store (0.5f * (outputs[0][i] + right[i]), std::memory_order_relaxed);
                scopeSide[(std::size_t) w].store (0.5f * (outputs[0][i] - right[i]), std::memory_order_relaxed);
                w = (w + 1) % scopeSize;
                peakL = juce::jmax (peakL, std::abs (outputs[0][i]));
                peakR = juce::jmax (peakR, std::abs (right[i]));
            }

            scopeWrite.store (w, std::memory_order_release);
            // the toolbar's stereo bars (round 190kn): the loudest sample of each side since the UI's last read
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto v = ch == 0 ? peakL : peakR;
                auto& slot = outputPeak[(std::size_t) ch];
                auto current = slot.load (std::memory_order_relaxed);
                while (v > current && ! slot.compare_exchange_weak (current, v, std::memory_order_relaxed))
                {
                }
            }
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

    // the block is done: the workers sleep until the next one instead of spinning (REA round 116) — unless render-ahead
    // runs: its thread hands them blocks between the callbacks and parks them itself (round 190gq)
    if (workers != nullptr && (rackProcessor == nullptr || ! rackProcessor->renderAheadOwnsWorkers()))
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
    // (round 190ok: LYNX_SAFE_OVERLOADS="share=…,max=…,window=…,late=…,ui=0|1": the guard's numbers for an A/B)
    if (const auto spec = juce::SystemStats::getEnvironmentVariable ("LYNX_SAFE_OVERLOADS", {}); spec.isNotEmpty())
        overloadGuard.configure (spec.toRawUTF8());
    overloadGuard.restart();
    rt::resetForNewDevice();
    transport.sampleRate.store (device->getCurrentSampleRate());
    meters.rollingLog.setSampleRate (device->getCurrentSampleRate()); // (the audio thread isn't running yet: it may allocate)
    midiSync.deviceStarted (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(), device->getOutputLatencyInSamples());
    abletonLink.deviceStarted (device->getCurrentSampleRate()); // (round 190gl: the Link session's sample clock starts over)
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
    joinDeviceWorkgroup (device);
    if (! processFullSpeed) // (Windows: the process stays out of EcoQoS while a device is open, round toolchain190gt)
    {
        processFullSpeed = true;
        power::holdProcessFullSpeed (true);
    }
   #if JUCE_WINDOWS
    choosePriorityClass();
    juce::Timer::callAfterDelay (2000, [] { choosePriorityClass(); }); // (after the MIDI ports: WinMM raises it again)
   #endif
}

void AudioEngine::audioDeviceStopped()
{
    hwOutput.active.store (false);
    player.audioDeviceStopped();
    joinDeviceWorkgroup (nullptr);
    if (processFullSpeed)
    {
        processFullSpeed = false;
        power::holdProcessFullSpeed (false);
    }
}

// macOS (round toolchain190gt): the render workers join the device's audio workgroup (os_workgroup, macOS 11+), so the
// scheduler runs them with the device's I/O thread as one real-time job with the buffer's deadline: on Apple silicon,
// on the performance cores. Each worker joins on its own thread (WorkerPool::setThreadRefresh) the next time it wakes,
// first becoming a time-constraint thread. A stopped device: the workers leave. LYNX_WORKGROUP=0 turns it off.
// Elsewhere this does nothing (Windows: MMCSS "Pro Audio" and no EcoQoS; Linux: SCHED_FIFO / RealtimeKit, round 161).
struct AudioEngine::DeviceWorkgroup
{
    juce::SpinLock lock;
    juce::AudioWorkgroup workgroup; // (empty: no device, or one without a workgroup)
    double blockMs = 0.0;
};

void AudioEngine::joinDeviceWorkgroup ([[maybe_unused]] juce::AudioIODevice* device)
{
   #if JUCE_MAC
    static const auto off = juce::SystemStats::getEnvironmentVariable ("LYNX_WORKGROUP", {}) == "0";
    if (off || workers == nullptr || workers->workers() == 0)
        return;
    auto wanted = device != nullptr ? device->getWorkgroup() : juce::AudioWorkgroup {};
    if (deviceWorkgroup == nullptr)
    {
        if (! wanted)
            return;
        deviceWorkgroup = std::make_shared<DeviceWorkgroup>();
        // (the pool keeps the holder alive as long as its workers can run the refresh)
        workers->setThreadRefresh ([holder = deviceWorkgroup] {
            struct Joined
            {
                juce::AudioWorkgroup workgroup; // (retained while joined: the token leaves it before it is released)
                juce::WorkgroupToken token;
            };
            thread_local Joined joined;
            juce::AudioWorkgroup next;
            double blockMs = 0.0;
            {
                const juce::SpinLock::ScopedLockType sl (holder->lock);
                next = holder->workgroup;
                blockMs = holder->blockMs;
            }
            if (next == joined.workgroup && (! next || static_cast<bool> (joined.token)))
                return;
            joined.token.reset();
            joined.workgroup = next;
            if (! next)
                return;
            becomeTimeConstraintThread (blockMs);
            next.join (joined.token);
        });
    }
    {
        const juce::SpinLock::ScopedLockType sl (deviceWorkgroup->lock);
        deviceWorkgroup->workgroup = std::move (wanted);
        deviceWorkgroup->blockMs = device != nullptr ? 1000.0 * device->getCurrentBufferSizeSamples() / juce::jmax (1.0, device->getCurrentSampleRate())
                                                     : 0.0;
    }
    workers->requestThreadRefresh();
   #endif
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

void AudioEngine::setCueOutput (int channel, OutputMode mode)
{
    editSong ([=] (Song& s) {
        if (! s.clipGrid)
            s.clipGrid.emplace();
        s.clipGrid->cue.out = channel < 0 ? outputDefault : channel;
        s.clipGrid->cue.outMode = mode;
    });
    openTargetedOutputs();
}

void AudioEngine::setCueMode (bool cue)
{
    editSong ([cue] (Song& s) {
        if (! s.clipGrid)
            s.clipGrid.emplace();
        s.clipGrid->cue.cueMode = cue;
    });
    openTargetedOutputs();
}

void AudioEngine::setCueVolume (float position, bool undoable)
{
    editSong ([p = juce::jlimit (0.0f, 1.0f, position)] (Song& s) {
        if (! s.clipGrid)
            s.clipGrid.emplace();
        s.clipGrid->cue.volume = p;
    },
              undoable);
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
    // round 190nc (DL-110): the Cue Out while Cue mode is on
    if (sessionmixer::cueOf (song).cueMode)
    {
        const auto [first, count] = sessionmixer::cueOutChannels (song);
        for (int c = first; c < first + count; ++c)
            if (c >= 0 && c < available)
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
