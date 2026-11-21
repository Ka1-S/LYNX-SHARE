#include "engine/HostingTests.h"
#include "platform/PrefaultMemory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "core/Denormals.h"
#include "core/FlCurve.h"
#include "core/Instruments.h"
#include "core/Song.h"
#include "engine/AudioEngine.h"
#include "engine/ClapFormat.h"
#include "engine/PluginHost.h"
#include "engine/PluginSandbox.h"
#include "platform/PowerThrottling.h"

#include "platform/ThreadInstructions.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <psapi.h>
#else
 #include <dlfcn.h>
 #include <sys/resource.h>
 #include <unistd.h>
#endif
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
 #include <xmmintrin.h>
#endif

// Round 190jg: see engine/HostingTests.h.
namespace lynx
{
namespace
{
void setEnv (const char* name, const juce::String& value)
{
   #if JUCE_WINDOWS
    _putenv_s (name, value.toRawUTF8());
   #else
    if (value.isEmpty())
        unsetenv (name);
    else
        setenv (name, value.toRawUTF8(), 1);
   #endif
}

std::optional<juce::PluginDescription> firstType (PluginHost& host, const juce::String& path, bool preferEffect = false)
{
    host.addPluginFile (juce::File (path));
    std::optional<juce::PluginDescription> any, effect;
    for (const auto& t : host.getKnownPlugins().getTypes())
        if (juce::File (t.fileOrIdentifier) == juce::File (path) || t.fileOrIdentifier == path)
        {
            if (! any)
                any = t;
            if (! t.isInstrument && ! effect)
                effect = t;
        }
    return preferEffect && effect ? effect : any;
}

// One block through a plugin: an instrument gets `notes` held from its first block; an effect a 440 Hz tone
struct Feeder
{
    double phase = 0.0;
    bool started = false;
    void fill (juce::AudioPluginInstance& p, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, double rate)
    {
        buffer.clear();
        midi.clear();
        if (p.getTotalNumInputChannels() == 0 || p.acceptsMidi())
        {
            if (! started)
                for (const auto key : { 48, 55, 60, 64 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), 0);
            started = true;
        }
        if (p.getTotalNumInputChannels() > 0)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto v = 0.25f * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 440.0 / rate;
                for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
                    buffer.setSample (c, i, v);
            }
    }
};

float runBlocks (juce::AudioPluginInstance& p, Feeder& feed, int blocks, int blockSize, double rate)
{
    juce::AudioBuffer<float> buffer (juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), blockSize);
    juce::MidiBuffer midi;
    auto peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        feed.fill (p, buffer, midi, rate);
        p.processBlock (buffer, midi);
        peak = juce::jmax (peak, buffer.getMagnitude (0, 0, blockSize));
    }
    return peak;
}

// CPU time of a process (pid 0: this one), seconds
double cpuSeconds (int pid)
{
   #if JUCE_WINDOWS
    const auto handle = pid == 0 ? GetCurrentProcess() : OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid);
    if (handle == nullptr)
        return 0.0;
    FILETIME created, exited, kernel, user;
    double seconds = 0.0;
    if (GetProcessTimes (handle, &created, &exited, &kernel, &user))
    {
        const auto ticks = [] (const FILETIME& t) { return (double) (((unsigned long long) t.dwHighDateTime << 32) | t.dwLowDateTime); };
        seconds = (ticks (kernel) + ticks (user)) * 1.0e-7;
    }
    if (pid != 0)
        CloseHandle (handle);
    return seconds;
   #else
    const auto text = juce::File ("/proc/" + (pid == 0 ? juce::String ("self") : juce::String (pid)) + "/stat").loadFileAsString();
    // fields after the ") " that ends the command name: state is field 3; utime 14, stime 15
    const auto rest = juce::StringArray::fromTokens (text.fromLastOccurrenceOf (")", false, false).trim(), " ", "");
    if (rest.size() < 13)
        return 0.0;
    const auto hz = (double) sysconf (_SC_CLK_TCK);
    return (rest[11].getDoubleValue() + rest[12].getDoubleValue()) / juce::jmax (1.0, hz);
   #endif
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
int runSandboxGroupTest (const juce::String& pluginPath, const juce::String& reportPath, const juce::String& crashPluginPath)
{
    juce::String report;
    auto failures = 0;
    auto check = [&] (bool ok, const juce::String& what) {
        report << (ok ? "ok   " : "FAIL ") << what << "\n";
        if (! ok)
            ++failures;
    };
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    setPluginSandboxAlerts (true); // (records the deaths; no window opens in a test)
    {
        PluginHost host (false);
        const auto type = firstType (host, pluginPath);
        check (type.has_value(), "plugin found: " + (type ? type->name + " (" + type->pluginFormatName + ")" : pluginPath));
        if (! type)
        {
            juce::File (reportPath).replaceWithText (report + "result=FAILED\n");
            return 1;
        }
        // the modes' group keys (Settings › Plugins › Plugin processes)
        check (sandboxGroupFor (*type, SandboxMode::perPlugin).isEmpty() && sandboxGroupFor (*type, SandboxMode::inProcess).isEmpty(),
               "each instance alone: no group");
        check (sandboxGroupFor (*type, SandboxMode::perVendor) == "vendor:" + type->manufacturerName.trim().toLowerCase()
                   || (type->manufacturerName.trim().isEmpty() && sandboxGroupFor (*type, SandboxMode::perVendor).startsWith ("vendor:folder:")),
               "per vendor: " + sandboxGroupFor (*type, SandboxMode::perVendor));
        check (sandboxGroupFor (*type, SandboxMode::perPluginType).startsWith ("plugin:") && sandboxGroupFor (*type, SandboxMode::allTogether) == "all",
               "per plugin: " + sandboxGroupFor (*type, SandboxMode::perPluginType) + "; all together: all");
        juce::String error;
        auto open = [&] (const juce::String& group) {
            auto p = createSandboxedPlugin (*type, rate, block, {}, error, group);
            if (p == nullptr)
                report << "open failed (" << group << "): " << error << "\n";
            return p;
        };
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto a = open ("vendor:group-a");
        const auto firstMs = juce::Time::getMillisecondCounterHiRes() - t0;
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        auto b = open ("vendor:group-a");
        const auto joinMs = juce::Time::getMillisecondCounterHiRes() - t1;
        auto c = open ("vendor:group-b");
        auto own = open ({});
        auto local = createRightInstance (host.getFormats(), *type, rate, block, error);
        if (local != nullptr)
            preparePlugin (*local, rate, block);
        check (a && b && c && own && local, "five instances: two in group A, one in group B, one alone, one in LYNX's process");
        if (! (a && b && c && own && local))
        {
            juce::File (reportPath).replaceWithText (report + "result=FAILED\n");
            return 1;
        }
        report << "open_ms first_in_group=" << juce::String (firstMs, 1) << " joining_group=" << juce::String (joinMs, 1) << "\n";
        const auto pa = sandboxedPluginProcessId (*a), pb = sandboxedPluginProcessId (*b), pc = sandboxedPluginProcessId (*c),
                   po = sandboxedPluginProcessId (*own);
        report << "pids a=" << pa << " b=" << pb << " c=" << pc << " own=" << po << "\n";
        check (pa != 0 && pa == pb, "A and B share one process");
        check (pc != 0 && pc != pa && po != 0 && po != pa && po != pc, "group B and the lone plugin have processes of their own");
        check (sandboxGroupOf (*a) == "vendor:group-a" && sandboxGroupOf (*own).isEmpty(), "groups reported");

        Feeder fa, fb, fc, fo, fl;
        const auto peakA = runBlocks (*a, fa, 40, block, rate), peakB = runBlocks (*b, fb, 40, block, rate),
                   peakC = runBlocks (*c, fc, 40, block, rate), peakO = runBlocks (*own, fo, 40, block, rate),
                   peakL = runBlocks (*local, fl, 40, block, rate);
        report << "peaks before a=" << peakA << " b=" << peakB << " c=" << peakC << " own=" << peakO << " local=" << peakL << "\n";
        // rollup 0.1.30: after a block the buffer holds what the plugin sent out, in a separate process as in LYNX's own
        // (SharedBlock::midiOut; before, the sandboxed buffer kept what went in whatever the plugin did with it)
        {
            auto midiAfter = [&] (juce::AudioPluginInstance& p) {
                juce::AudioBuffer<float> buffer (juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), block);
                buffer.clear();
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (2, 61, (juce::uint8) 90), 17);
                midi.addEvent (juce::MidiMessage::controllerEvent (2, 7, 100), 40);
                p.processBlock (buffer, midi);
                juce::String text;
                for (const auto m : midi)
                    text << m.samplePosition << ":" << juce::String::toHexString (m.data, m.numBytes) << " ";
                return text.trim();
            };
            const auto inProcess = midiAfter (*local), separate = midiAfter (*a);
            report << "midi after a block: in LYNX's process [" << inProcess << "], separate [" << separate << "]\n";
            check (inProcess == separate, "a separate process gives back the plugin's MIDI output as LYNX's process does");
        }
        check (peakA > 1.0e-4f && peakB > 1.0e-4f && peakC > 1.0e-4f && peakO > 1.0e-4f && peakL > 1.0e-4f, "all five play");

        // group A's process crashes (the worker aborts, as a crashing plugin does)
        const auto deathsBefore = pluginSandboxDeathCount();
        crashSandboxedPluginForTest (*a);
        const auto crashStart = juce::Time::getMillisecondCounterHiRes();
        while (! (isSandboxedPluginDead (*a) && isSandboxedPluginDead (*b)) && juce::Time::getMillisecondCounterHiRes() - crashStart < 5000.0)
        {
            runBlocks (*c, fc, 1, block, rate);
            juce::Thread::sleep (5);
        }
        const auto crashMs = juce::Time::getMillisecondCounterHiRes() - crashStart;
        check (isSandboxedPluginDead (*a) && isSandboxedPluginDead (*b), "group A's two plugins are down together (" + juce::String (crashMs, 0) + " ms)");
        check (! isSandboxedPluginDead (*c) && ! isSandboxedPluginDead (*own), "group B and the lone plugin are alive");
        const auto afterA = runBlocks (*a, fa, 4, block, rate), afterB = runBlocks (*b, fb, 4, block, rate);
        const auto afterC = runBlocks (*c, fc, 40, block, rate), afterO = runBlocks (*own, fo, 40, block, rate),
                   afterL = runBlocks (*local, fl, 40, block, rate);
        report << "peaks after a=" << afterA << " b=" << afterB << " c=" << afterC << " own=" << afterO << " local=" << afterL << "\n";
        check (afterA == 0.0f && afterB == 0.0f, "the crashed group is silent");
        check (afterC > 1.0e-4f && afterO > 1.0e-4f && afterL > 1.0e-4f, "the others keep playing");
        juce::Thread::sleep (100);
        const auto deaths = pluginSandboxDeaths();
        const auto newDeaths = pluginSandboxDeathCount() - deathsBefore;
        report << "deaths_recorded=" << newDeaths;
        for (int i = juce::jmax (0, (int) deaths.size() - newDeaths); i < (int) deaths.size(); ++i)
            report << " [" << deaths[(std::size_t) i].plugin << " group=" << deaths[(std::size_t) i].group
                   << (deaths[(std::size_t) i].alone ? " alone" : " with others") << (deaths[(std::size_t) i].crashed ? " crashed" : " hung") << "]";
        report << "\n";
        check (newDeaths == 2 && ! deaths.back().alone && deaths.back().group == "vendor:group-a", "both deaths reported, as one group's");

        // Reload: a new plugin of group A starts a new process
        auto again = open ("vendor:group-a");
        check (again != nullptr && sandboxedPluginProcessId (*again) != pa && ! isSandboxedPluginDead (*again), "a reloaded plugin of group A gets a new process");
        if (again != nullptr)
        {
            Feeder fr;
            check (runBlocks (*again, fr, 40, block, rate) > 1.0e-4f, "and plays");
        }

        // a real crashing plugin in a group (round 190dt's test plugin: a null write in its 20th block)
        if (crashPluginPath.isNotEmpty())
        {
            setEnv ("LYNX_TEST_CRASH", "process");
            setEnv ("LYNX_TEST_CRASH_AFTER", "20");
            PluginHost crashHost (false);
            const auto crashType = firstType (crashHost, crashPluginPath);
            check (crashType.has_value(), "crash plugin found");
            if (crashType)
            {
                auto crasher = createSandboxedPlugin (*crashType, rate, block, {}, error, "vendor:crashing");
                auto companion = open ("vendor:crashing");
                auto other = open ("vendor:safe");
                check (crasher && companion && other, "crash plugin + a companion in one group, another group beside");
                if (crasher && companion && other)
                {
                    Feeder f1, f2, f3;
                    float otherPeak = 0.0f;
                    for (int i = 0; i < 60; ++i)
                    {
                        runBlocks (*crasher, f1, 1, block, rate);
                        runBlocks (*companion, f2, 1, block, rate);
                        otherPeak = juce::jmax (otherPeak, runBlocks (*other, f3, 1, block, rate));
                    }
                    const auto waitStart = juce::Time::getMillisecondCounterHiRes();
                    while (! isSandboxedPluginDead (*companion) && juce::Time::getMillisecondCounterHiRes() - waitStart < 3000.0)
                        juce::Thread::sleep (10);
                    check (isSandboxedPluginDead (*crasher) && isSandboxedPluginDead (*companion), "the real crash took its group down");
                    const auto otherAfter = runBlocks (*other, f3, 40, block, rate);
                    report << "real crash: other group peak before=" << otherPeak << " after=" << otherAfter << "\n";
                    check (otherAfter > 1.0e-4f && ! isSandboxedPluginDead (*other), "the other group keeps playing; LYNX is alive");
                }
            }
            setEnv ("LYNX_TEST_CRASH", {});
        }

        // teardown: no wait on the freeing thread (issue #159)
        const auto freeStart = juce::Time::getMillisecondCounterHiRes();
        a.reset();
        b.reset();
        c.reset();
        own.reset();
        again.reset();
        const auto freeMs = juce::Time::getMillisecondCounterHiRes() - freeStart;
        const auto ended = finishSandboxTeardowns (15000);
        report << "free_ms=" << juce::String (freeMs, 1) << " teardowns_done=" << (ended ? 1 : 0) << "\n";
        check (freeMs < 500.0 && ended, "freeing is quick and every process ends");
    }
    report << "result=" << (failures == 0 ? "OK" : "FAILED") << "\n";
    juce::File (reportPath).replaceWithText (report);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
int runSandboxBench (const juce::String& pluginPath, const juce::String& reportPath, int plugins, int block, double seconds)
{
    constexpr double rate = 48000.0;
    plugins = juce::jlimit (1, 64, plugins);
    block = juce::jlimit (16, 4096, block);
    juce::String report;
    report << "plugin=" << pluginPath << " plugins=" << plugins << " block=" << block << " rate=48000 seconds=" << seconds << "\n";
   #if JUCE_WINDOWS
    report << "os=Windows";
   #elif JUCE_LINUX
    report << "os=Linux";
   #else
    report << "os=other";
   #endif
    report << " cores=" << juce::SystemStats::getNumCpus() << "\n";
    setPluginSandboxAlerts (false);

    PluginHost host (false);
    const auto type = firstType (host, pluginPath, true);
    if (! type)
    {
        juce::File (reportPath).replaceWithText (report + "plugin not found\nresult=FAILED\n");
        return 1;
    }
    report << "type=" << type->name << " (" << type->pluginFormatName << (type->isInstrument ? ", instrument" : ", effect") << ")\n";
    report << "load average at the start: " << juce::File ("/proc/loadavg").loadFileAsString().upToFirstOccurrenceOf (" ", false, false) << "\n";
    report << "mode | chain us/block median | mean | p99 | max | deadline misses | IPC us per plugin-block (median) | CPU % of one core (all processes) | processes\n";

    struct Result
    {
        double mean = 0, p50 = 0, p99 = 0, max = 0, cpu = 0;
        int misses = 0, processes = 0;
        bool ok = false;
    };
    std::map<juce::String, Result> results;
    const juce::String modes[] = { "in-process", "per-plugin", "one-group", "two-groups" };
    for (const auto& mode : modes)
    {
        std::vector<std::unique_ptr<juce::AudioPluginInstance>> chain;
        juce::String error;
        for (int i = 0; i < plugins; ++i)
        {
            std::unique_ptr<juce::AudioPluginInstance> p;
            if (mode == "in-process")
            {
                p = createRightInstance (host.getFormats(), *type, rate, block, error);
                if (p != nullptr)
                    preparePlugin (*p, rate, block);
            }
            else
                p = createSandboxedPlugin (*type, rate, block, {}, error,
                                           mode == "per-plugin" ? juce::String() : mode == "one-group" ? juce::String ("vendor:bench") : "vendor:bench" + juce::String (i % 2));
            if (p == nullptr)
                break;
            chain.push_back (std::move (p));
        }
        if ((int) chain.size() != plugins)
        {
            report << mode << " | could not open the plugins: " << error << "\n";
            continue;
        }
        std::set<int> pids;
        for (auto& p : chain)
            if (const auto pid = sandboxedPluginProcessId (*p); pid != 0)
                pids.insert (pid);

        // warm up (first blocks allocate, plugins settle), then the paced run on an audio thread of our own
        std::vector<Feeder> feeds ((std::size_t) plugins);
        for (std::size_t i = 0; i < chain.size(); ++i)
            runBlocks (*chain[i], feeds[i], 20, block, rate);

        const auto blocks = juce::jmax (100, (int) (seconds * rate / block));
        std::vector<double> times ((std::size_t) blocks);
        double cpuBefore = 0, cpuAfter = 0;
        double wallSeconds = 0;
        std::thread audio ([&] {
            const lynx::ScopedFlushDenormals noDenormals;
            power::becomeProAudioThread();
            juce::AudioBuffer<float> buffer (juce::jmax (2, chain[0]->getTotalNumInputChannels(), chain[0]->getTotalNumOutputChannels()), block);
            juce::MidiBuffer midi;
            const auto period = std::chrono::duration<double> (block / rate);
            cpuBefore = cpuSeconds (0);
            for (const auto pid : pids)
                cpuBefore += cpuSeconds (pid);
            const auto start = std::chrono::steady_clock::now();
            auto next = start;
            for (int b = 0; b < blocks; ++b)
            {
                const auto t0 = std::chrono::steady_clock::now();
                for (std::size_t i = 0; i < chain.size(); ++i)
                {
                    feeds[i].fill (*chain[i], buffer, midi, rate);
                    chain[i]->processBlock (buffer, midi);
                }
                times[(std::size_t) b] = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
                next += std::chrono::duration_cast<std::chrono::steady_clock::duration> (period);
                std::this_thread::sleep_until (next);
            }
            wallSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            cpuAfter = cpuSeconds (0);
            for (const auto pid : pids)
                cpuAfter += cpuSeconds (pid);
        });
        audio.join();

        Result r;
        r.ok = true;
        r.processes = (int) pids.size();
        auto sorted = times;
        std::sort (sorted.begin(), sorted.end());
        for (const auto t : times)
            r.mean += t / blocks;
        r.p50 = sorted[(std::size_t) ((blocks - 1) / 2)];
        r.p99 = sorted[(std::size_t) ((blocks - 1) * 0.99)];
        r.max = sorted.back();
        const auto deadline = block / rate * 1.0e6;
        for (const auto t : times)
            r.misses += t > deadline ? 1 : 0;
        r.cpu = wallSeconds > 0 ? (cpuAfter - cpuBefore) / wallSeconds * 100.0 : 0.0;
        for (auto& p : chain)
            if (isSandboxedPluginDead (*p))
                r.ok = false;
        results[mode] = r;
        chain.clear();
        finishSandboxTeardowns (15000);
    }

    const auto base = results.count ("in-process") > 0 ? results["in-process"] : Result {};
    for (const auto& mode : modes)
        if (const auto it = results.find (mode); it != results.end())
        {
            const auto& r = it->second;
            const auto ipc = mode == "in-process" ? 0.0 : (r.p50 - base.p50) / plugins;
            report << mode << " | " << juce::String (r.p50, 1) << " | " << juce::String (r.mean, 1) << " | " << juce::String (r.p99, 1) << " | " << juce::String (r.max, 1) << " | "
                   << r.misses << " | " << juce::String (ipc, 1) << " | " << juce::String (r.cpu, 2) << " (+" << juce::String (r.cpu - base.cpu, 2)
                   << ") | " << r.processes << (r.ok ? "" : " (a plugin died)") << "\n";
        }
    report << "(block deadline " << juce::String (block / rate * 1.0e6, 0) << " us; CPU = LYNX + its plugin processes during the run, "
           << "the bench's own pacing included; +x = more than in-process)\n";
    report << "result=" << (results.size() == 4 ? "OK" : "PARTIAL") << "\n";
    juce::File (reportPath).replaceWithText (report);
    return results.size() == 4 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
int runClapExtTest (const juce::String& pluginPath, const juce::String& reportPath)
{
    const auto logFile = juce::File (reportPath).getSiblingFile (juce::File (reportPath).getFileNameWithoutExtension() + "-plugin.txt");
    logFile.deleteFile();
    setEnv ("LYNX_TEST_EXT_LOG", logFile.getFullPathName());
    juce::String report;
    auto failures = 0;
    auto check = [&] (bool ok, const juce::String& what) {
        report << (ok ? "ok   " : "FAIL ") << what << "\n";
        if (! ok)
            ++failures;
    };
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    auto ours = false;
    {
        PluginHost host (false);
        host.addPluginFile (juce::File (pluginPath));
        std::vector<juce::PluginDescription> types;
        for (const auto& t : host.getKnownPlugins().getTypes())
            if (t.pluginFormatName == "CLAP" && juce::File (t.fileOrIdentifier) == juce::File (pluginPath))
                types.push_back (t);
        check (! types.empty(), "CLAP plugins in the file: " + juce::String ((int) types.size()));
        for (const auto& type : types)
        {
            juce::String error;
            const auto created = juce::Time::getMillisecondCounterHiRes();
            auto p = createRightInstance (host.getFormats(), type, rate, block, error);
            if (p == nullptr)
            {
                report << "--- " << type.name << ": could not open (" << error << ")\n";
                continue;
            }
            preparePlugin (*p, rate, block);
            report << "--- " << type.name << " (open " << juce::String (juce::Time::getMillisecondCounterHiRes() - created, 0) << " ms)\n";
            Feeder feed;
            const auto peak = runBlocks (*p, feed, 40, block, rate);
            const auto described = juce::Time::getMillisecondCounterHiRes();
            report << describeClapPlugin (*p);
            report << "describe_ms=" << juce::String (juce::Time::getMillisecondCounterHiRes() - described, 0) << " (presets found here)\n";
            const auto pool = clapThreadPoolUse (*p);
            report << "peak=" << peak << " thread_pool_requests=" << pool.first << " tasks=" << pool.second << "\n";

            if (type.name != "LYNX Test Ext")
                continue;
            ours = true;
            const auto text = describeClapPlugin (*p);
            for (const auto* ext : { "clap.thread-pool", "clap.tail", "clap.render", "clap.voice-info", "clap.state-context/2", "clap.remote-controls/2", "clap.preset-load/2" })
                check (text.contains (ext), juce::String ("the plugin offers ") + ext);
            check (std::abs (p->getTailLengthSeconds() - 0.1) < 1.0e-9, "tail 4800 samples = 0.1 s");
            const auto voices = clapVoiceInfo (*p);
            check (voices.valid && voices.count == 3 && voices.capacity == 16 && voices.overlappingNotes, "voice info 3 of 16, overlapping notes");
            const auto pages = clapRemoteControls (*p);
            check (pages.size() == 1 && pages[0].name == "Main" && pages[0].params[0] == 1 && pages[0].params[1] == 0 && pages[0].params[2] == -1,
                   "remote page Test / Main: Width, Level, empty");
            check (p->getNumPrograms() == 3 && p->getProgramName (2) == "High", "three presets found (Low, Mid, High)");
            p->setCurrentProgram (2);
            check (std::abs (p->getParameters()[0]->getValue() - 0.75f) < 1.0e-4f && p->getCurrentProgram() == 2, "preset High loaded: Level 0.75");
            check (pool.first >= 1 && pool.second >= 8, "the plugin's thread pool request ran on LYNX's workers");
            {
                juce::MemoryBlock state;
                {
                    const ScopedClapStateContext forPreset (ClapStateContext::preset);
                    p->getStateInformation (state);
                    p->setStateInformation (state.getData(), (int) state.getSize());
                }
                juce::MemoryBlock project;
                p->getStateInformation (project);
            }
            p->setNonRealtime (true);
            p->setNonRealtime (false);
        }
    }
    const auto log = logFile.loadFileAsString();
    if (ours)
    {
        check (log.contains ("pool ok=1 tasks=8"), "plugin log: its 8 tasks ran (" + log.fromFirstOccurrenceOf ("pool ", false, false).upToFirstOccurrenceOf ("\n", false, false) + ")");
        check (log.contains ("save context=1") && log.contains ("load context=1 saved_with=1"), "plugin log: a preset state saved and loaded in the preset context");
        check (log.contains ("save context=0"), "plugin log: the project's state without a context (clap.state)");
        check (log.contains ("render offline") && log.contains ("render realtime"), "plugin log: render mode offline, then real-time");
        check (log.contains ("preset high level=0.75"), "plugin log: preset loaded through preset-load");
    }
    report << "--- plugin log ---\n" << log;
    report << "result=" << (failures == 0 ? "OK" : "FAILED") << "\n";
    juce::File (reportPath).replaceWithText (report);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
int runAutomationTimingTest (const juce::String& pluginPath, const juce::String& reportPath)
{
    const auto logFile = juce::File (reportPath).getSiblingFile (juce::File (reportPath).getFileNameWithoutExtension() + "-plugin.txt");
    juce::String report;
    auto failures = 0;
    auto check = [&] (bool ok, const juce::String& what) {
        report << (ok ? "ok   " : "FAIL ") << what << "\n";
        if (! ok)
            ++failures;
    };
    constexpr double rate = 44100.0, bpm = 120.0;
    // two steps of a Hold automation: 0.2 -> 0.8 at tick t1, 0.8 -> 0.35 at tick t2 (samples not on any 16/128 grid)
    constexpr std::int64_t t1 = ppq + 7, t2 = 2 * ppq + 5;
    const auto tps = bpm / 60.0 * ppq / rate;
    const auto expected1 = (long) std::ceil ((double) t1 / tps - 1.0e-6), expected2 = (long) std::ceil ((double) t2 / tps - 1.0e-6);
    report << "plugin=" << pluginPath << "\nexpected step samples: " << expected1 << " and " << expected2 << " (44.1 kHz, 120 BPM, ticks "
           << (int) t1 << " / " << (int) t2 << ")\n";

    struct Run
    {
        long first = -1, second = -1;
        bool rendered = false;
    };
    auto render = [&] (bool sampleAccurate, bool separateProcess) {
        Run run;
        // (the plugin in a separate process: Settings › Plugin processes, put back afterwards)
        const auto modeBefore = pluginSandboxMode();
        setPluginSandboxMode (separateProcess ? SandboxMode::perPlugin : SandboxMode::inProcess);
        const juce::ScopeGuard modeBack { [modeBefore] { setPluginSandboxMode (modeBefore); } };
        logFile.deleteFile();
        setEnv ("LYNX_TEST_EXT_LOG", logFile.getFullPathName());
        setEnv ("LYNX_TEST_PARAM_LOG", logFile.getFullPathName());
        const auto wavFile = juce::File (reportPath).getSiblingFile (juce::File (reportPath).getFileNameWithoutExtension() + (sampleAccurate ? "-on" : "-off")
                                                                     + (separateProcess ? "-sep.wav" : ".wav"));
        {
            AudioEngine offline (false);
            auto& host = offline.getPluginHost();
            const auto type = firstType (host, pluginPath, true);
            if (! type)
            {
                report << "plugin not found\n";
                return run;
            }
            auto index = 0;
            {
                juce::String error;
                if (auto probe = createRightInstance (host.getFormats(), *type, rate, 512, error))
                    for (int i = 0; i < probe->getParameters().size(); ++i)
                        if (probe->getParameters()[i]->getName (32) == "Level")
                            index = i;
            }
            Song s;
            s.mixer = Song::defaultMixer (2);
            s.bpm = bpm;
            s.mode = PlayMode::song;
            EffectSlot slot;
            slot.kind = EffectKind::plugin;
            slot.smartDisable = false;
            slot.plugin.id = type->createIdentifierString().toStdString();
            slot.plugin.name = type->name.toStdString();
            s.mixer[0].effects.push_back (slot);
            Automation a;
            a.name = "Level";
            a.target = { AutomationTargetKind::effectParam, 0, 0, index };
            AutomationPoint p0, p1, p2;
            p0.tick = 0;
            p0.value = 0.2f;
            p0.mode = (std::uint8_t) flcurve::hold;
            p1.tick = t1;
            p1.value = 0.8f;
            p1.mode = (std::uint8_t) flcurve::hold;
            p2.tick = t2;
            p2.value = 0.35f;
            p2.mode = (std::uint8_t) flcurve::hold;
            a.points = { p0, p1, p2 };
            s.automations.push_back (a);
            Clip c;
            c.kind = ClipKind::automation;
            c.automation = 0;
            c.start = 0;
            c.length = 4 * ppq;
            c.track = 0;
            s.playlist.push_back (c);
            offline.setSong (std::move (s));
            setSampleAccurateAutomation (sampleAccurate);
            wavFile.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (wavFile.createOutputStream().release(), rate, 2, 32, {}, 0));
            if (writer != nullptr)
                offline.renderOffline (1.5, rate, writer.get());
            writer.reset();
            setSampleAccurateAutomation (true);
        }
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wavFile));
        if (reader == nullptr)
        {
            report << "no render\n";
            return run;
        }
        juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
        reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
        reader.reset();
        wavFile.deleteFile();
        run.rendered = true;
        const auto* x = audio.getReadPointer (0);
        const auto n = audio.getNumSamples();
        // the plateaus' levels, then the first sample past half of each step
        const auto at = [&] (long i) { return i >= 0 && i < n ? x[i] : 0.0f; };
        const auto v0 = at (expected1 - 2000), v1 = at ((expected1 + expected2) / 2), v2 = at (expected2 + 4000);
        report << (sampleAccurate ? "on " : "off") << (separateProcess ? " (separate process)" : "") << " levels " << v0 << " / " << v1 << " / " << v2 << "\n";
        for (long i = expected1 - 1500; i < expected2 - 500 && i < n; ++i)
            if (std::abs (x[i] - v0) > std::abs (v1 - v0) / 2)
            {
                run.first = i;
                break;
            }
        for (long i = expected2 - 1500; i < n; ++i)
            if (std::abs (x[i] - v1) > std::abs (v2 - v1) / 2)
            {
                run.second = i;
                break;
            }
        return run;
    };

    const auto on = render (true, false);
    report << "--- plugin log (sample-accurate on; parameter points around the steps) ---\n";
    for (const auto& line : juce::StringArray::fromLines (logFile.loadFileAsString()))
        if (line.startsWith ("P ") && ! line.startsWith ("P 0 "))
            report << line << "\n";
    const auto off = render (false, false);
    const auto separate = render (true, true);
    finishSandboxTeardowns (15000);
    report << "sample-accurate on:  steps at " << on.first << " and " << on.second << "\n";
    report << "sample-accurate off: steps at " << off.first << " and " << off.second << " (the next 128-sample piece; "
           << (off.first >= 0 ? juce::String (off.first - expected1) : juce::String ("?")) << " and "
           << (off.second >= 0 ? juce::String (off.second - expected2) : juce::String ("?")) << " samples late)\n";
    report << "sample-accurate on, plugin in a separate process: steps at " << separate.first << " and " << separate.second << "\n";
    check (on.rendered && off.rendered && separate.rendered, "rendered three times");
    check (separate.first == expected1 && separate.second == expected2, "in a separate process too (the changes go with the block)");
    check (on.first == expected1 && on.second == expected2, "with sample-accurate automation the plugin steps at the automation's own samples");
    check (off.first >= expected1 && off.first % 128 == 0 && off.second >= expected2 && off.second % 128 == 0,
           "without it, at the start of the next 128-sample piece (as before)");
    report << "result=" << (failures == 0 ? "OK" : "FAILED") << "\n";
    juce::File (reportPath).replaceWithText (report);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Round 190of: --pluginbench (see engine/HostingTests.h)
namespace
{
// the process's private memory (Windows: private bytes; Linux: resident anonymous + file pages, from statm)
double processMemoryMb()
{
   #if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS_EX counters {};
    if (GetProcessMemoryInfo (GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*) &counters, sizeof (counters)))
        return (double) counters.PrivateUsage / 1048576.0;
    return 0.0;
   #else
    const auto fields = juce::StringArray::fromTokens (juce::File ("/proc/self/statm").loadFileAsString(), " ", "");
    return fields.size() > 1 ? fields[1].getDoubleValue() * (double) sysconf (_SC_PAGESIZE) / 1048576.0 : 0.0;
   #endif
}

// the calling thread's work counter: instructions retired in user space (Linux) or its CPU cycles (Windows)
struct WorkCounter
{
    platform::ThreadInstructions instructions;
    long long now() const
    {
       #if JUCE_WINDOWS
        ULONG64 cycles = 0;
        QueryThreadCycleTime (GetCurrentThread(), &cycles);
        return (long long) cycles;
       #else
        return instructions.now();
       #endif
    }
    static const char* unit()
    {
       #if JUCE_WINDOWS
        return "cycles";
       #else
        return "instructions";
       #endif
    }
};

// Linux: LD_PRELOAD=tools/pluginbench/libbenchcount.so counts malloc-family calls and mutex locks made by a thread
// while it has counting on (lynxbench_counts); without it these columns are "-"
using CountFn = void (*) (int on, unsigned long* allocations, unsigned long* locks);
CountFn countHook()
{
   #if JUCE_WINDOWS
    return nullptr;
   #else
    static const auto fn = (CountFn) dlsym (RTLD_DEFAULT, "lynxbench_counts");
    return fn;
   #endif
}

struct ThreadSwitches
{
    long voluntary = 0, minorFaults = 0;
    static ThreadSwitches now()
    {
        ThreadSwitches s;
       #if defined(RUSAGE_THREAD)
        rusage u {};
        if (getrusage (RUSAGE_THREAD, &u) == 0)
        {
            s.voluntary = u.ru_nvcsw;
            s.minorFaults = u.ru_minflt;
        }
       #endif
        return s;
    }
};

struct Phase
{
    std::vector<double> work, wallUs; // per block
    std::vector<float> peak;          // per block, the output's peak
    unsigned long allocations = 0, locks = 0;
    long switches = 0, faults = 0;
    double median (bool wall = false) const
    {
        auto v = wall ? wallUs : work;
        if (v.empty())
            return 0.0;
        std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) (v.size() / 2), v.end());
        return v[v.size() / 2];
    }
    double mean (bool wall = false) const
    {
        const auto& v = wall ? wallUs : work;
        double sum = 0.0;
        for (const auto x : v)
            sum += x;
        return v.empty() ? 0.0 : sum / (double) v.size();
    }
    double quantile (double q, bool wall = false) const
    {
        auto v = wall ? wallUs : work;
        if (v.empty())
            return 0.0;
        std::sort (v.begin(), v.end());
        return v[(std::size_t) ((double) (v.size() - 1) * q)];
    }
    double max (bool wall = false) const
    {
        const auto& v = wall ? wallUs : work;
        return v.empty() ? 0.0 : *std::max_element (v.begin(), v.end());
    }
    // blocks whose work exceeds `factor` x the median (a spike: something the plugin does now and then)
    int spikes (double factor) const
    {
        const auto m = median();
        int n = 0;
        for (const auto x : work)
            n += m > 0.0 && x > factor * m ? 1 : 0;
        return n;
    }
};

enum class Feed { signal, silence };

// Runs `blocks` blocks of `process` on its own thread (FTZ/DAZ as the audio threads have it, unless `denormals`)
template <typename Process>
Phase runPhase (int blocks, bool denormals, Process&& process)
{
    Phase phase;
    phase.work.resize ((std::size_t) blocks);
    phase.wallUs.resize ((std::size_t) blocks);
    phase.peak.resize ((std::size_t) blocks);
    std::thread audio ([&] {
        const lynx::ScopedFlushDenormals noDenormals;
       #if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
        if (denormals)
            _mm_setcsr (_mm_getcsr() & ~0x8040u);
       #else
        juce::ignoreUnused (denormals);
       #endif
        WorkCounter counter;
        const auto hook = countHook();
        const auto before = ThreadSwitches::now();
        if (hook != nullptr)
            hook (1, nullptr, nullptr);
        for (int b = 0; b < blocks; ++b)
        {
            const auto t0 = std::chrono::steady_clock::now();
            const auto w0 = counter.now();
            phase.peak[(std::size_t) b] = process (b);
            const auto w1 = counter.now();
            phase.wallUs[(std::size_t) b] = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
            phase.work[(std::size_t) b] = (double) (w1 - w0);
        }
        if (hook != nullptr)
            hook (0, &phase.allocations, &phase.locks);
        const auto after = ThreadSwitches::now();
        phase.switches = after.voluntary - before.voluntary;
        phase.faults = after.minorFaults - before.minorFaults;
    });
    audio.join();
    return phase;
}

// the first block from which the output stays under −96 dB (FL's smart-disable silence), −1 = never
int silentFrom (const Phase& p)
{
    int from = -1;
    for (int b = 0; b < (int) p.peak.size(); ++b)
        if (p.peak[(std::size_t) b] > 1.5849e-5f)
            from = -1;
        else if (from < 0)
            from = b;
    return from;
}

juce::String num (double v, int decimals = 0)
{
    return juce::String (v, decimals);
}
} // namespace

int runPluginBench (const juce::String& listOrPlugin, const juce::String& reportPath, int block, double rate, double seconds, int typesPerFile,
                    const juce::String& nameFilter, bool editors)
{
    block = juce::jlimit (16, 4096, block);
    rate = juce::jlimit (8000.0, 384000.0, rate);
    seconds = juce::jlimit (0.5, 120.0, seconds);
    setPluginSandboxAlerts (false);
    juce::StringArray lines;
    const juce::File list (listOrPlugin);
    if (list.existsAsFile() && (list.hasFileExtension ("txt") || list.hasFileExtension ("lst")))
        lines.addLines (list.loadFileAsString());
    else
        lines.add (listOrPlugin);
    const auto filters = juce::StringArray::fromTokens (nameFilter, ",", "");

    juce::String out;
    out << "# LYNX --pluginbench: block " << block << ", " << rate << " Hz, " << seconds << " s per phase; work = " << WorkCounter::unit()
        << " per block on the bench thread\n";
    out << "# load average at the start: " << juce::File ("/proc/loadavg").loadFileAsString().upToFirstOccurrenceOf (" ", false, false) << "\n";
    out << "file\tplugin\tformat\tkind\tscan_ms\tload_ms\tprepare_ms\tram_load_mb\tram_prepare_mb\tlatency\ttail_s\t"
           "work_med\twork_p99\twork_max\twall_med_us\twall_p99_us\twall_max_us\tcpu_pct\tpeak_pct\tspikes4x\tover_half\t"
           "allocs\tlocks\tswitches\tfaults\tsilent_after_ms\tsilence_work_med\tsilence_ratio\tdenormal_ratio\twrap_work_med\twrap_overhead_pct\t"
           "editor_ms\tram_editor_mb\trelease_ms\tnote\n";
    const auto budgetUs = block / rate * 1.0e6;
    const auto signalBlocks = juce::jmax (50, (int) (seconds * rate / block));
    juce::String errors;

    for (auto line : lines)
    {
        line = line.trim();
        if (line.isEmpty() || line.startsWithChar ('#'))
            continue;
        const auto path = line.upToFirstOccurrenceOf ("\t", false, false).trim();
        const auto only = line.containsChar ('\t') ? line.fromFirstOccurrenceOf ("\t", false, false).trim() : juce::String();
        PluginHost host (false);
        const auto s0 = juce::Time::getMillisecondCounterHiRes();
        host.addPluginFile (juce::File (path));
        const auto scanMs = juce::Time::getMillisecondCounterHiRes() - s0;
        std::vector<juce::PluginDescription> types;
        for (const auto& t : host.getKnownPlugins().getTypes())
            if (juce::File (t.fileOrIdentifier) == juce::File (path) || t.fileOrIdentifier == path)
            {
                auto wanted = only.isEmpty() || t.name.containsIgnoreCase (only);
                if (wanted && ! filters.isEmpty())
                {
                    wanted = false;
                    for (const auto& f : filters)
                        wanted = wanted || t.name.containsIgnoreCase (f.trim());
                }
                if (wanted)
                    types.push_back (t);
            }
        if (types.empty())
        {
            out << path << "\t-\t-\t-\t" << num (scanMs) << "\t" << juce::String::repeatedString ("\t-", 29) << "\tnot found or not loadable\n";
            juce::File (reportPath).replaceWithText (out);
            continue;
        }
        if ((int) types.size() > typesPerFile)
            types.resize ((std::size_t) typesPerFile);

        for (const auto& type : types)
        {
            juce::String note;
            const auto mem0 = processMemoryMb();
            // round 190ok: LYNX_PB_PREFAULT=1 pre-faults the memory the plugin mapped while it loaded and prepared
            // (platform/PrefaultMemory.h), to compare its audio thread's page faults with and without
            const auto prefaultOn = juce::SystemStats::getEnvironmentVariable ("LYNX_PB_PREFAULT", "0") == "1";
            const auto mapsBefore = prefaultOn ? prefault::mappings() : std::vector<prefault::Region> {};
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            juce::String error;
            auto instance = createRightInstance (host.getFormats(), type, rate, block, error);
            const auto loadMs = juce::Time::getMillisecondCounterHiRes() - t0;
            if (instance == nullptr)
            {
                out << path << "\t" << type.name << "\t" << type.pluginFormatName << "\t-\t" << num (scanMs) << "\t" << num (loadMs)
                    << juce::String::repeatedString ("\t-", 29) << "\tload failed: " << error.replaceCharacters ("\t\n", "  ") << "\n";
                juce::File (reportPath).replaceWithText (out);
                continue;
            }
            const auto mem1 = processMemoryMb();
            const auto p0 = juce::Time::getMillisecondCounterHiRes();
            preparePlugin (*instance, rate, block);
            if (prefaultOn)
            {
                const auto pf = prefault::populateNew (mapsBefore);
                note << "prefault " << (juce::int64) pf.regions << " regions / " << juce::String ((double) pf.bytes / 1048576.0, 1) << " MB"
                     << (pf.supported ? "" : " (not supported)") << "; ";
            }
            const auto prepareMs = juce::Time::getMillisecondCounterHiRes() - p0;
            const auto mem2 = processMemoryMb();
            const auto instrument = type.isInstrument || instance->getTotalNumInputChannels() == 0;
            auto& p = *instance;

            juce::AudioBuffer<float> buffer (juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), block);
            juce::MidiBuffer midi;
            double phaseAngle = 0.0;
            const int keys[] = { 48, 55, 60, 64 };
            auto feed = [&] (Feed f, bool first, bool release) {
                buffer.clear();
                midi.clear();
                if (instrument || p.acceptsMidi())
                {
                    if (first && f == Feed::signal)
                        for (const auto key : keys)
                            midi.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), 0);
                    if (release)
                        for (const auto key : keys)
                            midi.addEvent (juce::MidiMessage::noteOff (1, key), 0);
                }
                if (! instrument && f == Feed::signal)
                    for (int i = 0; i < block; ++i)
                    {
                        // a 110 Hz saw-ish tone with harmonics (a compressor / EQ / reverb sees a full band)
                        const auto v = 0.2f * (float) (std::sin (phaseAngle) + 0.5 * std::sin (3.0 * phaseAngle) + 0.25 * std::sin (7.0 * phaseAngle));
                        phaseAngle += juce::MathConstants<double>::twoPi * 110.0 / rate;
                        for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
                            buffer.setSample (c, i, v);
                    }
            };
            auto peakOut = [&] {
                auto peak = 0.0f;
                for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
                    peak = juce::jmax (peak, buffer.getMagnitude (c, 0, block));
                return peak;
            };

            // warm-up (the first blocks allocate, plugins settle), then signal, then silence (notes released / input zero)
            for (int b = 0; b < 20; ++b)
            {
                feed (Feed::signal, b == 0, false);
                p.processBlock (buffer, midi);
            }
            auto signal = runPhase (signalBlocks, false, [&] (int) {
                feed (Feed::signal, false, false);
                p.processBlock (buffer, midi);
                return peakOut();
            });
            // round 190ok: LYNX_PB_BLOCKS=<file> appends the signal phase's work per block (finding a period in spikes)
            if (const auto dump = juce::SystemStats::getEnvironmentVariable ("LYNX_PB_BLOCKS", {}); dump.isNotEmpty())
            {
                juce::String rows;
                for (std::size_t b = 0; b < signal.work.size(); ++b)
                    rows << type.name << "\t" << (int) b << "\t" << juce::String (signal.work[b], 0) << "\t" << juce::String (signal.wallUs[b], 1) << "\n";
                juce::File (dump).appendText (rows);
            }
            auto silence = runPhase (signalBlocks, false, [&] (int b) {
                feed (Feed::silence, false, b == 0);
                p.processBlock (buffer, midi);
                return peakOut();
            });
            // denormals: the same signal / silence with flush-to-zero off (what a plugin meets when nothing sets it)
            for (int b = 0; b < (int) (0.5 * rate / block); ++b)
            {
                feed (Feed::signal, b == 0, false);
                p.processBlock (buffer, midi);
            }
            auto silenceDenormal = runPhase (signalBlocks, true, [&] (int b) {
                feed (Feed::silence, false, b == 0);
                p.processBlock (buffer, midi);
                return peakOut();
            });
            const auto latency = p.getLatencySamples();
            const auto tail = p.getTailLengthSeconds();

            // LYNX's wrapper (the effect slot / channel instrument adapter) around the same instance: what LYNX adds
            double wrapMedian = 0.0, releaseMs = 0.0;
            {
                std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
                double angle = 0.0;
                const auto timedRelease = [&releaseMs] (auto& owner) {
                    const auto r0 = juce::Time::getMillisecondCounterHiRes();
                    owner = nullptr;
                    releaseMs = juce::Time::getMillisecondCounterHiRes() - r0;
                };
                if (instrument)
                {
                    auto wrapped = std::make_unique<PluginInstrument> (std::move (instance), block);
                    for (const auto key : keys)
                        wrapped->noteOn (key, 0.8f);
                    for (int b = 0; b < 20; ++b)
                        wrapped->render (l.data(), r.data(), block, 1.0f, 1.0f);
                    const auto wrap = runPhase (signalBlocks, false, [&] (int) {
                        std::fill (l.begin(), l.end(), 0.0f);
                        std::fill (r.begin(), r.end(), 0.0f);
                        wrapped->render (l.data(), r.data(), block, 1.0f, 1.0f);
                        return 0.0f;
                    });
                    wrapMedian = wrap.median();
                    wrapped->allNotesOff();
                    timedRelease (wrapped);
                }
                else
                {
                    auto wrapped = std::make_unique<PluginEffect> (std::move (instance), block);
                    EffectParams params {};
                    auto fill = [&] {
                        for (int i = 0; i < block; ++i)
                        {
                            const auto v = 0.2f * (float) (std::sin (angle) + 0.5 * std::sin (3.0 * angle) + 0.25 * std::sin (7.0 * angle));
                            angle += juce::MathConstants<double>::twoPi * 110.0 / rate;
                            l[(std::size_t) i] = r[(std::size_t) i] = v;
                        }
                    };
                    for (int b = 0; b < 20; ++b)
                    {
                        fill();
                        wrapped->process (l.data(), r.data(), block, params, 120.0);
                    }
                    const auto wrap = runPhase (signalBlocks, false, [&] (int) {
                        fill();
                        wrapped->process (l.data(), r.data(), block, params, 120.0);
                        return 0.0f;
                    });
                    wrapMedian = wrap.median();
                    timedRelease (wrapped);
                }
            }

            // the editor (optional): opened on a new instance, shown in a window of its own, then closed
            double editorMs = -1.0, editorMb = 0.0;
            if (editors)
            {
                auto second = createRightInstance (host.getFormats(), type, rate, block, error);
                if (second != nullptr && second->hasEditor())
                {
                    preparePlugin (*second, rate, block);
                    const auto before = processMemoryMb();
                    const auto e0 = juce::Time::getMillisecondCounterHiRes();
                    std::unique_ptr<juce::AudioProcessorEditor> editor (second->createEditorIfNeeded());
                    if (editor != nullptr)
                    {
                        editor->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
                        editor->setVisible (true);
                        editorMs = juce::Time::getMillisecondCounterHiRes() - e0;
                        editorMb = processMemoryMb() - before;
                        editor->setVisible (false);
                        editor->removeFromDesktop();
                        second->editorBeingDeleted (editor.get());
                        editor = nullptr;
                    }
                }
                if (second != nullptr)
                    second->releaseResources();
            }

            const auto silentAt = silentFrom (silence);
            const auto silentMs = silentAt < 0 ? -1.0 : silentAt * budgetUs / 1000.0;
            // the cost of steady silence: the last half of the silence phase
            Phase tailHalf;
            tailHalf.work.assign (silence.work.begin() + (std::ptrdiff_t) (silence.work.size() / 2), silence.work.end());
            Phase tailHalfDen;
            tailHalfDen.work.assign (silenceDenormal.work.begin() + (std::ptrdiff_t) (silenceDenormal.work.size() / 2), silenceDenormal.work.end());
            const auto sigMed = signal.median();
            const auto overHalf = (int) std::count_if (signal.wallUs.begin(), signal.wallUs.end(), [budgetUs] (double us) { return us > 0.5 * budgetUs; });
            const auto hook = countHook() != nullptr;
            if (signal.peak.empty() || *std::max_element (signal.peak.begin(), signal.peak.end()) < 1.0e-6f)
                note << "silent output with the test signal; ";
            out << path << "\t" << type.name << "\t" << type.pluginFormatName << "\t" << (instrument ? "instrument" : "effect") << "\t" << num (scanMs) << "\t"
                << num (loadMs, 1) << "\t" << num (prepareMs, 1) << "\t" << num (mem1 - mem0, 1) << "\t" << num (mem2 - mem1, 1) << "\t" << latency << "\t"
                << num (tail, 2) << "\t" << num (sigMed) << "\t" << num (signal.quantile (0.99)) << "\t" << num (signal.max()) << "\t"
                << num (signal.median (true), 1) << "\t" << num (signal.quantile (0.99, true), 1) << "\t" << num (signal.max (true), 1) << "\t"
                << num (signal.mean (true) / budgetUs * 100.0, 2) << "\t" << num (signal.max (true) / budgetUs * 100.0, 1) << "\t" << signal.spikes (4.0) << "\t"
                << overHalf << "\t" << (hook ? juce::String ((juce::int64) signal.allocations) : juce::String ("-")) << "\t"
                << (hook ? juce::String ((juce::int64) signal.locks) : juce::String ("-")) << "\t" << (int) signal.switches << "\t" << (int) signal.faults << "\t"
                << num (silentMs, 0) << "\t" << num (tailHalf.median()) << "\t" << num (sigMed > 0 ? tailHalf.median() / sigMed : 0.0, 2) << "\t"
                << num (tailHalf.median() > 0 ? tailHalfDen.median() / tailHalf.median() : 0.0, 2) << "\t" << num (wrapMedian) << "\t"
                << num (sigMed > 0 ? (wrapMedian - sigMed) / sigMed * 100.0 : 0.0, 1) << "\t" << num (editorMs, 1) << "\t" << num (editorMb, 1) << "\t"
                << num (releaseMs, 1) << "\t" << note << "\n";
            juce::File (reportPath).replaceWithText (out); // (what was reached, if a plugin crashes the run)
        }
    }
    juce::File (reportPath).replaceWithText (out + "result=OK\n");
    return 0;
}
} // namespace lynx
