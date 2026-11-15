// Round 190gg (0.1.28, from round 190gc's complaints study, slices B1 / B4 / B5): LYNX-own robustness.
// B5: dropouts counted and explained (core/Dropouts.h: the count, when, and whether the processing or the driver /
//     system caused it, with the slowest call of that block from the renderer).
// B4: the crash guard also covers the calls the UI makes into a plugin (its editor, presets, programs, spare state) and,
//     on Linux / macOS, faults on the plugin's own threads (the app only: no static TLS in the exported plugins).
// B1: after a crash, "Always run <plugin> in a separate process" in the crash prompt, the plugin window's crash notice
//     and the Plugin manager's "Separate process" column.
// The crash simulations with the test plugins run in the app: LYNX --crashguardtest <plugin> report thread | ui.

#include "core/Dropouts.h"

namespace stability190gg
{
inline lynx::dropouts::Poll poll (double ms, std::uint64_t over, std::uint64_t late, int xruns, double window, std::uint64_t slowest = 0,
                                  double overLoad = 0.0)
{
    lynx::dropouts::Poll p;
    p.nowMs = ms;
    p.overBudget = over;
    p.lateCalls = late;
    p.deviceXruns = xruns;
    p.windowMaxLoad = window;
    p.lastOverSlowest = slowest;
    p.lastOverLoad = overLoad;
    return p;
}
} // namespace stability190gg

TEST (stability190gg_slowest_call_packs_and_keeps_the_longest)
{
    using namespace lynx::dropouts;
    const auto v = pack (4'200'000, effectUnit (3 * 10 + 1));
    CHECK (nanosecondsOf (v) == 4'200'000);
    CHECK (unitOf (v) == effectBase + 31);
    CHECK (! isChannel (unitOf (v)) && effectKeyOf (unitOf (v)) == 31);
    CHECK (isChannel (channelUnit (0)) && channelOf (channelUnit (0)) == 0 && channelOf (channelUnit (1023)) == 1023);
    CHECK (unitOf (pack (1, 0)) == 0); // 0 = none
    CHECK (nanosecondsOf (pack (~std::uint64_t (0), 5)) == (std::uint64_t (1) << 40) - 1); // (saturates, the unit survives)
    CHECK (unitOf (pack (~std::uint64_t (0), 5)) == 5);
    std::atomic<std::uint64_t> slot { 0 };
    raiseSlowest (slot, 1000, channelUnit (2));
    raiseSlowest (slot, 500, channelUnit (3)); // shorter: kept out
    raiseSlowest (slot, 3000, effectUnit (7));
    raiseSlowest (slot, 3000, effectUnit (8)); // equal: the first stays
    CHECK (nanosecondsOf (slot.load()) == 3000 && unitOf (slot.load()) == effectUnit (7));
}

TEST (stability190gg_rtstats_keeps_the_slowest_of_the_late_block)
{
    lynx::RtStats stats;
    stats.record (10.0, 2.0, 10.0, lynx::dropouts::pack (900'000, lynx::dropouts::channelUnit (1)));   // 20 %: fine
    CHECK (stats.snapshot().lastOverSlowest == 0);
    stats.record (20.0, 12.5, 10.0, lynx::dropouts::pack (9'000'000, lynx::dropouts::effectUnit (12))); // 125 %: over
    stats.record (30.0, 3.0, 10.0, lynx::dropouts::pack (100, lynx::dropouts::channelUnit (4)));        // fine again
    const auto s = stats.snapshot();
    CHECK (s.overBudget == 1);
    CHECK (lynx::dropouts::unitOf (s.lastOverSlowest) == lynx::dropouts::effectUnit (12));
    CHECK (std::abs (s.lastOverLoad - 1.25) < 0.002);
    // the dropout log's window is its own: the CPU meter taking its window leaves it alone
    CHECK (std::abs (stats.takeWindowMaxLoad() - 1.25) < 0.002);
    CHECK (std::abs (stats.takeDropoutWindowMaxLoad() - 1.25) < 0.002);
    CHECK (stats.takeDropoutWindowMaxLoad() == 0.0);
    stats.reset();
    CHECK (stats.snapshot().lastOverSlowest == 0 && stats.snapshot().lastOverLoad == 0.0);
}

TEST (stability190gg_dropouts_counted_with_their_cause)
{
    using namespace lynx::dropouts;
    using stability190gg::poll;
    Log log;
    CHECK (log.count() == 0 && log.secondsSince (5000.0) == -1 && log.cause() == Cause::none);
    CHECK (log.update (poll (100, 0, 0, -1, 0.2)) == 0);
    // a block over its deadline (no driver count): one dropout, the processing's, naming the slowest call
    const auto slow = pack (12'000'000, effectUnit (21));
    CHECK (log.update (poll (200, 1, 0, -1, 1.3, slow, 1.3)) == 1);
    CHECK (log.count() == 1 && log.processing() == 1 && log.system() == 0);
    CHECK (log.cause() == Cause::processing && log.slowest() == slow && log.load() >= 1.29);
    CHECK (log.secondsSince (200.0) == 0 && log.secondsSince (12'400.0) == 12);
    // nothing new: nothing added
    CHECK (log.update (poll (300, 1, 0, -1, 0.3)) == 0);
    CHECK (log.update (poll (400, 1, 0, -1, 0.3)) == 0);
    // late device calls while the load stayed low: the driver's / system's, no plugin named
    CHECK (log.update (poll (500, 1, 2, -1, 0.25)) == 2);
    CHECK (log.count() == 3 && log.system() == 2 && log.cause() == Cause::system && log.slowest() == 0);
    CHECK (std::abs (log.load() - 0.3) < 1e-9); // (the higher of this window and the one before)
    CHECK (log.lastAt() == 500.0);
}

TEST (stability190gg_dropouts_follow_the_drivers_count)
{
    using namespace lynx::dropouts;
    using stability190gg::poll;
    Log log;
    // the driver reports its own count (FL's underrun counter shows it): LYNX's estimate is not added on top
    CHECK (log.update (poll (100, 0, 0, 0, 0.1)) == 0);
    CHECK (log.update (poll (200, 0, 3, 0, 0.1)) == 0); // (late calls the driver did not count as dropouts)
    CHECK (log.update (poll (300, 0, 3, 1, 0.2)) == 1);
    CHECK (log.cause() == Cause::system);
    // a driver count that arrives a poll after a heavy block is still the processing's
    CHECK (log.update (poll (400, 0, 3, 1, 0.95)) == 0);
    CHECK (log.update (poll (500, 0, 3, 2, 0.2)) == 1);
    CHECK (log.cause() == Cause::processing && log.slowest() == 0 && log.load() >= 0.95);
    // over-deadline blocks the poll before: the processing's too, with that block's slowest call
    const auto slow = pack (7'000'000, channelUnit (5));
    CHECK (log.update (poll (600, 1, 3, 2, 1.2, slow, 1.2)) == 0);
    CHECK (log.update (poll (700, 1, 3, 3, 0.1, slow, 1.2)) == 1);
    CHECK (log.cause() == Cause::processing && log.slowest() == slow);
    // a new device: its count starts again at 0; the total never goes back, and counts the new device's from there
    CHECK (log.update (poll (800, 1, 3, 0, 0.1)) == 0);
    CHECK (log.count() == 3);
    CHECK (log.update (poll (900, 1, 3, 2, 0.1)) == 2);
    CHECK (log.count() == 5 && log.processing() == 2 && log.system() == 3);
    // dropouts seen before the first poll count as well (the log starts after the device)
    Log late;
    CHECK (late.update (poll (50, 0, 0, 4, 0.1)) == 4 && late.count() == 4 && late.cause() == Cause::system);
    // "low load" is under 70 % of the deadline
    Log edge;
    CHECK (edge.update (poll (10, 0, 0, 0, 0.1)) == 0);
    CHECK (edge.update (poll (20, 0, 0, 1, 0.69)) == 1 && edge.cause() == Cause::system);
    Log edge2;
    CHECK (edge2.update (poll (10, 0, 0, 0, 0.1)) == 0);
    CHECK (edge2.update (poll (20, 0, 0, 1, 0.70)) == 1 && edge2.cause() == Cause::processing);
}

TEST (stability190gg_renderer_names_the_slowest_call_of_a_block)
{
    // the render path marks each channel / effect call: the block's slowest is taken once per device callback
    auto song = lynx::Song::makeDefault();
    constexpr int rate = 48000, block = 256;
    auto state = lynx::RackState::build (song, nullptr, rate);
    lynx::RackRenderer renderer;
    renderer.prepare (rate, block);
    renderer.setProfiling (true);
    std::vector<float> l (block), r (block);
    double tick = 0.0;
    std::uint64_t seen = 0;
    for (int i = 0; i < 400 && seen == 0; ++i)
    {
        renderer.process (state.get(), true, tick, block, l.data(), r.data());
        tick += block * lynx::ticksPerSample (song.bpm, rate);
        seen = renderer.takeBlockSlowest();
    }
    CHECK (seen != 0);
    const auto unit = lynx::dropouts::unitOf (seen);
    CHECK (lynx::dropouts::isChannel (unit) ? lynx::dropouts::channelOf (unit) < (int) song.channels.size()
                                            : lynx::dropouts::effectKeyOf (unit) < (std::size_t) lynx::maxMixerTracks * lynx::maxEffectSlots);
    CHECK (renderer.takeBlockSlowest() == 0); // (taken)
    // profiling off (offline renders): nothing is measured, nothing is kept
    renderer.setProfiling (false);
    for (int i = 0; i < 20; ++i)
    {
        renderer.process (state.get(), true, tick, block, l.data(), r.data());
        tick += block * lynx::ticksPerSample (song.bpm, rate);
    }
    CHECK (renderer.takeBlockSlowest() == 0);
}

TEST (stability190gg_dropouts_reach_the_cpu_meter_and_the_monitor)
{
    using namespace i18ntest;
    const auto engine = readFile ("src/engine/AudioEngine.cpp");
    // the callback hands the block's slowest call to RtStats; the engine's timer keeps the log
    CHECK (engine.find ("engine.rackProcessor->getRenderer().takeBlockSlowest()") != std::string::npos);
    CHECK (engine.find ("dropoutLog.update (poll)") != std::string::npos);
    CHECK (engine.find ("rtStats.takeDropoutWindowMaxLoad()") != std::string::npos);
    const auto main = readFile ("src/ui/MainComponent.cpp");
    CHECK (main.find ("perfMeter.setDropouts (describeDropouts (engine))") != std::string::npos);
    const auto monitor = readFile ("src/ui/PluginMonitorWindow.cpp");
    CHECK (monitor.find ("setDropoutText (describeDropouts (engine))") != std::string::npos);
    const auto meter = readFile ("src/ui/Toolbar.cpp");
    CHECK (meter.find ("h.text << \"  \" << dropoutText") != std::string::npos);
    // the troubleshooting page explains the causes
    const auto wiki = readFile ("website/_src/wiki/troubleshooting.md");
    CHECK (wiki.find ("### The sound crackles or drops out") != std::string::npos);
    CHECK (wiki.find ("Driver or system latency") != std::string::npos);
}

TEST (stability190gg_ui_calls_into_plugins_are_guarded)
{
    using namespace i18ntest;
    const auto window = readFile ("src/ui/PluginWindow.cpp");
    // the editor is made under the guard before any window, in PluginWindows::open
    const auto open = window.find ("void open (std::shared_ptr<void> owner, juce::AudioPluginInstance& plugin");
    CHECK (open != std::string::npos);
    const auto guarded = window.find ("callPluginFromUi (plugin, \"making its editor\", [&plugin] { plugin.createEditorIfNeeded(); })", open);
    const auto host = window.find ("open[&plugin] = makeHost", open);
    CHECK (guarded != std::string::npos && host != std::string::npos && guarded < host);
    // the spare state, presets and programs
    CHECK (window.find ("callPluginFromUi (plugin, \"loading its spare state\"") != std::string::npos);
    CHECK (window.find ("plugin.setStateInformation (load->data(), (int) load->size());\n            engine") == std::string::npos);
    const auto presets = readFile ("src/ui/PluginPresetMenu.cpp");
    CHECK (presets.find ("callPluginFromUi (plugin, \"loading a preset\"") != std::string::npos);
    CHECK (presets.find ("callPluginFromUi (plugin, \"loading a program\"") != std::string::npos);
    // a CLAP editor's view is put in its window from a timer: guarded, and never torn down after a fault there
    const auto clap = readFile ("src/engine/ClapFormat.cpp");
    CHECK (clap.find ("callPluginFromUi (owner, \"opening its editor\"") != std::string::npos);
    CHECK (clap.find ("if (created && ! faulted)") != std::string::npos);
    // every adapter registers its plugin (a UI crash switches that adapter off) and leaves as it goes
    const auto hostCpp = readFile ("src/engine/PluginHost.cpp");
    std::size_t registrations = 0, at = 0;
    while ((at = hostCpp.find ("registerAdapter (*plugin, crashed, name);", at)) != std::string::npos)
        ++registrations, ++at;
    CHECK (registrations == 2);
    std::size_t leaves = 0;
    at = 0;
    while ((at = hostCpp.find ("forgetAdapter (plugin.get());", at)) != std::string::npos)
        ++leaves, ++at;
    CHECK (leaves == 2);
    // the module of each plugin loaded inside LYNX is watched for faults on its own threads
    CHECK (hostCpp.find ("crashguard::watchPluginModule (description->fileOrIdentifier.toStdString());") != std::string::npos);
    CHECK (readFile ("src/ui/MainComponent.cpp").find ("switchOffPluginsOfThreadFaults();") != std::string::npos);
}

TEST (stability190gg_thread_guard_is_app_only_and_adds_no_static_tls)
{
    using namespace i18ntest;
    const auto guard = readFile ("src/platform/CrashGuard.cpp");
    // round 190fp's rule: one initial-exec variable at most, in the app-only branch
    std::size_t count = 0, at = 0;
    while ((at = guard.find ("tls_model (", at)) != std::string::npos)
        ++count, ++at;
    CHECK (count == 1); // (the app's frame pointer only: round 190fp's test checks where it sits)
    CHECK (guard.find ("thread_local", guard.find ("LYNX_PLUGIN_THREAD_GUARD")) == std::string::npos);
    // the plugin-thread guard is compiled only where LYNX_EXPORT_VERSION is not (the exported plugins' runtime)
    const auto def = guard.find ("#if ! defined(LYNX_EXPORT_VERSION)\n #define LYNX_PLUGIN_THREAD_GUARD 1\n#endif");
    CHECK (def != std::string::npos);
    // ...and it keeps no state that allocates in the handler: fixed tables and atomics
    CHECK (guard.find ("WatchedModule watched[watchedCapacity];") != std::string::npos);
    CHECK (guard.find ("ThreadFaultSlot threadFaultSlots[threadFaultCapacity];") != std::string::npos);
    // a thread is parked only when no frame is LYNX's and the walk reached the thread's start
    const auto park = guard.find ("bool parkPluginThread (int sig, const void* context) noexcept");
    CHECK (park != std::string::npos);
    const auto body = guard.substr (park, guard.find ("\n}\n", park) - park);
    CHECK (body.find ("if (frame.dli_fbase == ownImage)") != std::string::npos);
    CHECK (body.find ("! runtimeLibrary (last.dli_fname)") != std::string::npos);
    CHECK (body.find ("watchedModuleOf (frame.dli_fname)") != std::string::npos);
    // only faults outside every guarded call get there
    CHECK (guard.find ("else if (currentFrame() == nullptr && synchronousFault (info))\n        parkPluginThread (sig, context);") != std::string::npos);
    // the self test checks LYNX's own threads still crash (LYNX's executable watched as if it were a plugin's module)
    const auto main = readFile ("src/Main.cpp");
    CHECK (main.find ("{ \"plain\", \"guarded\", \"guarded-libc\", \"thread\", \"thread-libc\" }") != std::string::npos);
}

TEST (stability190gg_always_separate_offered_after_a_crash)
{
    using namespace i18ntest;
    const auto main = readFile ("src/ui/MainComponent.cpp");
    const auto prompt = main.find ("const auto prompt = PluginWindows::crashPrompt (crashedName, onLoad);");
    CHECK (prompt != std::string::npos);
    CHECK (main.find ("else if (prompt.separate && result == (prompt.reload ? 2 : 1))", prompt) != std::string::npos);
    CHECK (main.find ("PluginWindows::alwaysRunSeparately (safe->engine, crashedName);", prompt) != std::string::npos);
    // the editor-crash prompt (Windows) uses the same action
    CHECK (main.find ("PluginWindows::alwaysRunSeparately (safe->engine, plugin);") != std::string::npos);
    const auto window = readFile ("src/ui/PluginWindow.cpp");
    // the prompt's buttons: Reload (not after a crash on its saved settings), the choice when it can be made, Close
    const auto build = window.find ("CrashPrompt crashPrompt (const juce::String& plugin, bool onLoad)");
    CHECK (build != std::string::npos);
    CHECK (window.find ("p.separate = canAlwaysRunSeparately (plugin);", build) != std::string::npos);
    CHECK (window.find ("p.buttons.add (tr (\"Always run {0} in a separate process\", { plugin }));", build) != std::string::npos);
    CHECK (window.find ("p.message << \"\\n\\n\" << alwaysSeparateHint();", build) != std::string::npos);
    // the crash notice in the plugin's window: a second button, with the hint about docking
    CHECK (window.find ("notice.setSeparate (std::move (separate), pluginName);") != std::string::npos);
    CHECK (window.find ("separate.setTooltip (PluginWindows::alwaysSeparateHint());") != std::string::npos);
    // the action: the list by name (survives rescans: its own file), then every instance and the down ones
    const auto action = window.find ("void alwaysRunSeparately (AudioEngine& engine, const juce::String& plugin)");
    CHECK (action != std::string::npos);
    CHECK (window.find ("setPluginAlwaysSeparate (plugin, true);", action) != std::string::npos);
    CHECK (window.find ("r->separateProcess = true;", action) != std::string::npos);
    // not offered when everything already runs separately, or when the plugin is already on the list
    const auto can = window.find ("bool canAlwaysRunSeparately (const juce::String& plugin)");
    CHECK (window.find ("! pluginSandboxEnabled() && ! pluginAlwaysSeparate (plugin)", can) != std::string::npos);
    // the list is its own file (separate-process.txt), not the scan's database: a rescan keeps it
    const auto sandbox = readFile ("src/engine/PluginSandbox.cpp");
    CHECK (sandbox.find ("getChildFile (\"separate-process.txt\")") != std::string::npos);
    const auto host = readFile ("src/engine/PluginHost.cpp");
    const auto forgetAll = host.find ("void PluginHost::forgetAll()");
    CHECK (forgetAll == std::string::npos || host.substr (forgetAll, 1200).find ("separate-process") == std::string::npos);
    // the creation path uses the list (Reload of a down plugin starts it in its own process)
    CHECK (host.find ("if (bits32 || pluginSandboxEnabled() || ref.separateProcess || pluginAlwaysSeparate (description->name))") != std::string::npos);
    // the Plugin manager's column: a tick per plugin, not for 32-bit ones (always in the bridge)
    const auto manager = readFile ("src/ui/PluginManager.cpp");
    CHECK (manager.find ("header.addColumn (tr (\"Separate process\"), separateColumn") != std::string::npos);
    CHECK (manager.find ("r.separate = separate.contains (t.name.trim(), true);") != std::string::npos);
    CHECK (manager.find ("PluginWindows::alwaysRunSeparately (engine, name);") != std::string::npos);
}
