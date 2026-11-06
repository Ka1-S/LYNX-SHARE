#include "engine/Bridge32Test.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/PluginHost.h"
#include "engine/PluginSandbox.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

// LYNX.exe --bridge32test "LYNX Test Synth 32.dll" report.txt (round 176a): the 32-bit plugin bridge end to end, with
// LYNX's own 32-bit test VST2 (tools/testplugin/lynx_test_vst2.c, built for x86). Each step writes key=value lines.

namespace lynx
{
namespace
{
#if JUCE_WINDOWS
struct FoundWindow
{
    DWORD pid;
    juce::String title;
    bool visible = false;
};

BOOL CALLBACK findWindow (HWND hwnd, LPARAM data)
{
    auto& f = *reinterpret_cast<FoundWindow*> (data);
    DWORD pid = 0;
    GetWindowThreadProcessId (hwnd, &pid);
    wchar_t text[256] {};
    GetWindowTextW (hwnd, text, 255);
    if (pid == f.pid && juce::String (text) == f.title && IsWindowVisible (hwnd))
        f.visible = true;
    return TRUE;
}

// a visible top-level window of that process with that title (the plugin window the bridge opens)
bool windowShown (int pid, const juce::String& title)
{
    FoundWindow f { (DWORD) pid, title };
    EnumWindows (findWindow, reinterpret_cast<LPARAM> (&f));
    return f.visible;
}

// CPU time of a process so far (user + kernel), ms
double processCpuMs (int pid)
{
    double ms = 0.0;
    if (auto h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid))
    {
        FILETIME created, ended, kernel, user;
        if (GetProcessTimes (h, &created, &ended, &kernel, &user))
            ms = ((double) (((ULONGLONG) kernel.dwHighDateTime << 32) | kernel.dwLowDateTime)
                  + (double) (((ULONGLONG) user.dwHighDateTime << 32) | user.dwLowDateTime)) / 10000.0;
        CloseHandle (h);
    }
    return ms;
}

double threadCpuMs()
{
    FILETIME created, ended, kernel, user;
    if (! GetThreadTimes (GetCurrentThread(), &created, &ended, &kernel, &user))
        return 0.0;
    return ((double) (((ULONGLONG) kernel.dwHighDateTime << 32) | kernel.dwLowDateTime)
            + (double) (((ULONGLONG) user.dwHighDateTime << 32) | user.dwLowDateTime)) / 10000.0;
}

bool processAlive (int pid)
{
    auto h = OpenProcess (SYNCHRONIZE, FALSE, (DWORD) pid);
    if (h == nullptr)
        return false;
    const auto alive = WaitForSingleObject (h, 0) == WAIT_TIMEOUT;
    CloseHandle (h);
    return alive;
}
#endif

// an environment variable of this process, inherited by the bridge processes it starts afterwards
void setEnv (const char* name, const char* value)
{
   #if JUCE_WINDOWS
    SetEnvironmentVariableA (name, value != nullptr && *value != 0 ? value : nullptr);
   #else
    if (value != nullptr && *value != 0)
        ::setenv (name, value, 1);
    else
        ::unsetenv (name);
   #endif
}

// A fixed position (137 BPM, playing): the test plugin reports the tempo it read through audioMasterGetTime in its state.
struct FixedHead final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (137.0);
        p.setIsPlaying (true);
        p.setPpqPosition (8.0);
        p.setTimeSignature (TimeSignature { 3, 4 });
        return p;
    }
};

// the test plugin's chunk inside a saved state ('LXV2', gain, tempo / 1000)
bool readChunk (const juce::MemoryBlock& state, float& gain, float& tempo)
{
    const auto* b = static_cast<const char*> (state.getData());
    for (std::size_t i = state.getSize() >= 12 ? state.getSize() - 12 + 1 : 0; i-- > 0;) // (the last one: the chunk ends the state)
        if (std::memcmp (b + i, "LXV2", 4) == 0)
        {
            std::memcpy (&gain, b + i + 4, 4);
            std::memcpy (&tempo, b + i + 8, 4);
            return true;
        }
    return false;
}

float peakOf (const std::vector<float>& l, const std::vector<float>& r, std::size_t n)
{
    float peak = 0.0f;
    for (std::size_t k = 0; k < n && k < l.size(); ++k)
        peak = std::max ({ peak, std::abs (l[k]), std::abs (r[k]) });
    return peak;
}

// zero crossings per second of the left channel: the sine's frequency
double frequencyOf (const std::vector<float>& l, std::size_t n, double rate)
{
    int crossings = 0;
    for (std::size_t k = 1; k < n; ++k)
        if ((l[k - 1] < 0.0f) != (l[k] < 0.0f))
            ++crossings;
    return crossings * 0.5 * rate / (double) n;
}

int bitsOf (juce::AudioPluginInstance& p)
{
    const auto& params = p.getParameters();
    return params.size() > 2 ? juce::roundToInt (params[2]->getValue() * 64.0f) : 0;
}
} // namespace

int runBridge32Test (const juce::String& pluginArg, const juce::String& reportPath, const juce::String& comparePath)
{
    const auto pluginPath = juce::File::getCurrentWorkingDirectory().getChildFile (pluginArg).getFullPathName();
    juce::String report;
    const auto write = [&] { juce::File (reportPath).replaceWithText (report); };
    if (! bridge32Supported())
    {
        report << "result=SKIPPED (32-bit plugins are bridged on Windows x64 only)\n";
        write();
        return 0;
    }
    setPluginSandboxAlerts (false);
    auto ok = true;
    const auto fail = [&] (const juce::String& why) { report << "FAILED: " << why << "\n"; ok = false; };

    // 1. the module is 32-bit, the bridge is there; the scan describes it through the bridge
    report << "module_32bit=" << (isModule32Bit (pluginPath) ? 1 : 0) << " bridge=" << bridge32Executable().getFullPathName()
           << " exists=" << (bridge32Executable().existsAsFile() ? 1 : 0) << "\n";
    if (! isModule32Bit (pluginPath) || ! bridge32Executable().existsAsFile())
        fail ("not a 32-bit module, or LYNX-bridge32.exe missing");
    PluginHost host (false);
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    host.addPluginFile (juce::File (pluginPath));
    const auto scanMs = juce::Time::getMillisecondCounterHiRes() - t0;
    const auto types = host.getKnownPlugins().getTypes();
    report << "scan_ms=" << juce::String (scanMs, 0) << " found=" << types.size();
    if (types.isEmpty())
    {
        fail ("the bridge's scan found no plugin");
        report << "result=FAILED\n";
        write();
        return 1;
    }
    const auto description = types.getReference (0);
    report << " name=" << description.name << " format=" << description.pluginFormatName << " instrument=" << (description.isInstrument ? 1 : 0)
           << " is32=" << (isPlugin32Bit (description) ? 1 : 0) << "\n";

    juce::String error;
    std::vector<float> l (96000), r (96000);
    const auto clear = [&] { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); };

    // 2. open (sample rate 48 kHz, LYNX's prepared block 4096): it runs in the bridge, as a 32-bit module
    setEnv ("LYNX_TEST_VST2_LATENCY", "64"); // (the bridge inherits it)
    t0 = juce::Time::getMillisecondCounterHiRes();
    auto remote = createSandboxedPlugin (description, 48000.0, PluginHost::maxBlockSize, {}, error);
    const auto openMs = juce::Time::getMillisecondCounterHiRes() - t0;
    setEnv ("LYNX_TEST_VST2_LATENCY", "0");
    if (remote == nullptr)
    {
        fail ("open: " + error);
        report << "result=FAILED\n";
        write();
        return 1;
    }
    auto& proxy = *remote;
    const auto pid = sandboxedPluginProcessId (proxy);
    report << "open_ms=" << juce::String (openMs, 0) << " bridged32=" << (isBridged32 (proxy) ? 1 : 0) << " plugin_bits=" << bitsOf (proxy)
           << " latency=" << proxy.getLatencySamples() << " params=" << proxy.getParameters().size() << " editor=" << (proxy.hasEditor() ? 1 : 0)
           << " pid=" << pid << "\n";
    if (! isBridged32 (proxy) || bitsOf (proxy) != 32)
        fail ("not running as a 32-bit plugin");
    if (proxy.getLatencySamples() != 64)
        fail ("latency not reported (64 expected)");

    // 3. play a note (1 s, 512-sample blocks as the audio device would ask), timed per block
    PluginInstrument voice (std::move (remote), PluginHost::maxBlockSize);
    FixedHead head;
    proxy.setPlayHead (&head);
    voice.noteOn (69, 1.0f);
    std::vector<double> blockUs;
   #if JUCE_WINDOWS
    const auto childCpu0 = processCpuMs (pid);
    const auto ownCpu0 = threadCpuMs();
   #endif
    t0 = juce::Time::getMillisecondCounterHiRes();
    for (int done = 0; done < 48000; done += 512)
    {
        const auto b0 = juce::Time::getMillisecondCounterHiRes();
        voice.render (l.data() + done, r.data() + done, 512, 1.0f, 1.0f);
        blockUs.push_back ((juce::Time::getMillisecondCounterHiRes() - b0) * 1000.0);
    }
    const auto renderMs = juce::Time::getMillisecondCounterHiRes() - t0;
   #if JUCE_WINDOWS
    const auto childCpu = processCpuMs (pid) - childCpu0;
    const auto ownCpu = threadCpuMs() - ownCpu0;
   #else
    const auto childCpu = 0.0, ownCpu = 0.0;
   #endif
    std::sort (blockUs.begin(), blockUs.end());
    double meanUs = 0.0;
    for (auto u : blockUs)
        meanUs += u / (double) blockUs.size();
    const auto peak = peakOf (l, r, 48000);
    const auto hz = frequencyOf (l, 48000, 48000.0);
    report << "peak=" << peak << " hz=" << juce::String (hz, 1) << " render_1s_ms=" << juce::String (renderMs, 1) << " block_us_mean="
           << juce::String (meanUs, 1) << " block_us_p50=" << juce::String (blockUs[blockUs.size() / 2], 1) << " block_us_p99="
           << juce::String (blockUs[blockUs.size() * 99 / 100], 1) << " block_us_max=" << juce::String (blockUs.back(), 1)
           << " bridge_cpu_ms_per_s=" << juce::String (childCpu, 1) << " lynx_thread_cpu_ms_per_s=" << juce::String (ownCpu, 1) << "\n";
    if (peak < 0.3f || std::abs (hz - 440.0) > 5.0)
        fail ("no / wrong sound (440 Hz sine expected)");

    // 3b. cost per instance (10 s of 256-sample blocks, the device's usual size): the bridged 32-bit plugin, and with
    // --compare <64-bit build of the same plugin>, that one in LYNX's process and in a 64-bit separate process
    {
        const auto timeIt = [&] (Instrument& v, int childPid, const char* label) {
            std::vector<float> a (256), b (256);
           #if JUCE_WINDOWS
            const auto c0 = childPid > 0 ? processCpuMs (childPid) : 0.0;
            const auto o0 = threadCpuMs();
           #endif
            const auto w0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 48000 * 10 / 256; ++i)
                v.render (a.data(), b.data(), 256, 1.0f, 1.0f);
            const auto wall = juce::Time::getMillisecondCounterHiRes() - w0;
           #if JUCE_WINDOWS
            const auto child = childPid > 0 ? processCpuMs (childPid) - c0 : 0.0;
            const auto own = threadCpuMs() - o0;
           #else
            const auto child = 0.0, own = 0.0;
           #endif
            const auto blocks = 48000 * 10 / 256;
            report << "perf_" << label << ": us_per_block=" << juce::String (wall * 1000.0 / blocks, 2) << " cpu_pct_of_realtime="
                   << juce::String ((own + child) / 100.0, 3) << " (lynx " << juce::String (own / 100.0, 3) << " + child " << juce::String (child / 100.0, 3)
                   << ")\n";
            return wall * 1000.0 / blocks;
        };
        const auto bridgedUs = timeIt (voice, pid, "bridge32");
        if (comparePath.isNotEmpty())
        {
            PluginHost local (false);
            local.addPluginFile (juce::File::getCurrentWorkingDirectory().getChildFile (comparePath));
            const auto list = local.getKnownPlugins().getTypes();
            if (! list.isEmpty())
            {
                if (auto direct = createRightInstance (local.getFormats(), list.getReference (0), 48000.0, PluginHost::maxBlockSize, error))
                {
                    preparePlugin (*direct, 48000.0, PluginHost::maxBlockSize);
                    PluginInstrument v (std::move (direct), PluginHost::maxBlockSize);
                    v.noteOn (69, 1.0f);
                    const auto inUs = timeIt (v, 0, "in_process64");
                    report << "bridge32_added_us_per_block=" << juce::String (bridgedUs - inUs, 2) << "\n";
                }
                if (auto sep = createSandboxedPlugin (list.getReference (0), 48000.0, PluginHost::maxBlockSize, {}, error))
                {
                    const auto sepPid = sandboxedPluginProcessId (*sep);
                    PluginInstrument v (std::move (sep), PluginHost::maxBlockSize);
                    v.noteOn (69, 1.0f);
                    timeIt (v, sepPid, "separate_process64");
                }
            }
        }
        report << "bridge32_added_latency_samples=0 (each block is processed in the bridge before the call returns)\n";
    }

    // 4. automation: the gain parameter from 0.5 to 0.25 halves the sound; the transport reached the plugin (137 BPM)
    proxy.getParameters()[0]->setValue (0.25f);
    clear();
    voice.render (l.data(), r.data(), 512, 1.0f, 1.0f); // (carries the change)
    clear();
    voice.render (l.data(), r.data(), 4800, 1.0f, 1.0f);
    const auto peakAutomated = peakOf (l, r, 4800);
    juce::MemoryBlock state;
    proxy.getStateInformation (state);
    float gain = 0.0f, tempo = 0.0f;
    const auto chunk = readChunk (state, gain, tempo);
    report << "automated_peak=" << peakAutomated << " ratio=" << juce::String (peakAutomated / juce::jmax (1e-6f, peak), 3) << " state_bytes="
           << (int) state.getSize() << " state_gain=" << gain << " state_tempo=" << juce::String (tempo * 1000.0f, 2) << "\n";
    if (std::abs (peakAutomated / juce::jmax (1e-6f, peak) - 0.5f) > 0.05f)
        fail ("parameter automation did not reach the plugin");
    if (! chunk || std::abs (gain - 0.25f) > 1e-4f)
        fail ("state does not carry the parameter");
    if (std::abs (tempo * 1000.0f - 137.0f) > 0.01f)
        fail ("transport (tempo) did not reach the plugin");

    // 5. state restore: a second bridged instance at 96 kHz (a sample-rate change re-creates plugins) with that state
    if (auto second = createSandboxedPlugin (description, 96000.0, PluginHost::maxBlockSize, state.toBase64Encoding().toStdString(), error))
    {
        auto& p2 = *second;
        const auto restored = p2.getParameters()[0]->getValue();
        PluginInstrument voice2 (std::move (second), PluginHost::maxBlockSize);
        voice2.noteOn (69, 1.0f);
        clear();
        // odd block sizes (a device buffer change, the renderer's chunks at events)
        int done = 0;
        for (const auto n : { 64, 333, 1024, 4096, 17, 2048 })
        {
            voice2.render (l.data() + done, r.data() + done, n, 1.0f, 1.0f);
            done += n;
        }
        const auto hz96 = frequencyOf (l, (std::size_t) done, 96000.0);
        report << "restored_gain=" << restored << " rate96_hz=" << juce::String (hz96, 1) << " frames=" << done << " peak96=" << peakOf (l, r, (std::size_t) done) << "\n";
        if (std::abs (restored - 0.25f) > 1e-4f)
            fail ("state not restored");
        if (std::abs (hz96 - 440.0) > 6.0)
            fail ("wrong pitch at 96 kHz / odd blocks");
    }
    else
        fail ("second instance: " + error);

    // 6. editor: the plugin's window opens in the bridge while LYNX's stand-in is on screen, and closes with it
   #if JUCE_WINDOWS
    if (! juce::JUCEApplicationBase::getCommandLineParameters().contains ("--no-editor")) // (--no-editor: no window on screen)
    {
        auto holder = std::make_unique<juce::DocumentWindow> ("bridge32test", juce::Colours::black, 0, false);
        holder->setContentOwned (proxy.createEditorIfNeeded(), true);
        holder->setVisible (true);
        holder->addToDesktop (0);
        if (auto* content = holder->getContentComponent()) // (no message loop here: the stand-in is told it is on screen)
        {
            content->setVisible (false);
            content->setVisible (true);
        }
        const auto e0 = juce::Time::getMillisecondCounterHiRes();
        auto shown = false;
        while (! (shown = windowShown (pid, description.name)) && juce::Time::getMillisecondCounterHiRes() - e0 < 5000.0)
            juce::Thread::sleep (20);
        const auto showMs = juce::Time::getMillisecondCounterHiRes() - e0;
        // the bridge keeps processing while its editor is open
        clear();
        voice.render (l.data(), r.data(), 4800, 1.0f, 1.0f);
        const auto peakWithEditor = peakOf (l, r, 4800);
        holder = nullptr; // LYNX's plugin window closed: the stand-in hides the plugin's window
        const auto h0 = juce::Time::getMillisecondCounterHiRes();
        auto hidden = false;
        while (! (hidden = ! windowShown (pid, description.name)) && juce::Time::getMillisecondCounterHiRes() - h0 < 5000.0)
            juce::Thread::sleep (20);
        report << "editor_shown=" << (shown ? 1 : 0) << " editor_show_ms=" << juce::String (showMs, 0) << " editor_hidden=" << (hidden ? 1 : 0)
               << " peak_with_editor=" << peakWithEditor << "\n";
        if (! shown || ! hidden || peakWithEditor < 0.1f)
            fail ("editor window not shown / not hidden, or no sound while it was open");
    }
   #endif

    // 7. a crashing 32-bit plugin (an access violation in its 20th process call) takes only the bridge down
    setEnv ("LYNX_TEST_VST2_CRASH", "process");
    auto crasher = createSandboxedPlugin (description, 48000.0, PluginHost::maxBlockSize, {}, error);
    setEnv ("LYNX_TEST_VST2_CRASH", "");
    if (crasher != nullptr)
    {
        auto& p3 = *crasher;
        const auto pid3 = sandboxedPluginProcessId (p3);
        PluginInstrument voice3 (std::move (crasher), PluginHost::maxBlockSize);
        voice3.noteOn (69, 1.0f);
        const auto c0 = juce::Time::getMillisecondCounterHiRes();
        int blocks = 0;
        while (! isSandboxedPluginDead (p3) && juce::Time::getMillisecondCounterHiRes() - c0 < 5000.0)
        {
            voice3.render (l.data(), r.data(), 512, 1.0f, 1.0f);
            ++blocks;
        }
        const auto crashMs = juce::Time::getMillisecondCounterHiRes() - c0;
        clear();
        voice3.render (l.data(), r.data(), 4096, 1.0f, 1.0f);
        const auto after = peakOf (l, r, 4096);
        // the other bridged instance plays on
        clear();
        voice.render (l.data(), r.data(), 4800, 1.0f, 1.0f);
        const auto others = peakOf (l, r, 4800);
       #if JUCE_WINDOWS
        const auto bridgeGone = ! processAlive (pid3);
       #else
        const auto bridgeGone = true;
       #endif
        report << "crash_dead=" << (isSandboxedPluginDead (p3) ? 1 : 0) << " crash_blocks=" << blocks << " crash_ms=" << juce::String (crashMs, 0)
               << " crash_peak_after=" << after << " other_instance_peak=" << others << " crashed_bridge_gone=" << (bridgeGone ? 1 : 0) << "\n";
        if (! isSandboxedPluginDead (p3) || after != 0.0f || others < 0.1f || ! bridgeGone)
            fail ("crash not isolated");
    }
    else
        fail ("crash instance: " + error);

    report << "result=" << (ok ? "OK" : "FAILED") << "\n";
    write();
    return ok ? 0 : 1;
}
} // namespace lynx
