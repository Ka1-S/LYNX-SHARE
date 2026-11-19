#pragma once

#include <functional>
#include <optional>

#include <memory>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace lynx
{
// Optional out-of-process plugins ("sandbox"): each plugin runs in a child LYNX.exe
// (`--plugin-host`). Audio, MIDI and parameters go through shared memory every block; state and
// editor window, through messages. If the child hangs or exits, the plugin goes silent and LYNX keeps running.
// Windows, Linux (round 161) and macOS.

constexpr const char* pluginSandboxId = "lynx-plugin-host";

// On/off (file sandbox.txt in %APPDATA%\LYNX; default: off).
bool pluginSandboxEnabled();
void setPluginSandboxEnabled (bool enabled);

// Round 190jg: how plugins run when the option is on, as Bitwig's plugin hosting modes ("Individually", "By plug-in",
// "By manufacturer", "Together"). A crash takes down only the process it happened in: the instance alone (perPlugin),
// every instance of that plugin (perPluginType), its vendor's plugins (perVendor) or every sandboxed plugin
// (allTogether), never LYNX. More plugins per process = fewer processes (memory, start-up), less isolation.
enum class SandboxMode { inProcess = 0, perPlugin = 1, perVendor = 2, allTogether = 3, perPluginType = 4 };
SandboxMode pluginSandboxMode();
void setPluginSandboxMode (SandboxMode mode);
// The group a plugin joins under `mode` (the key of its process; empty = a process of its own)
juce::String sandboxGroupFor (const juce::PluginDescription& description, SandboxMode mode);

// Plugins that always run in a separate process, by name (round 157a): e.g. one whose editor crashed LYNX (a GPU
// driver failing inside the plugin's OpenGL window takes the host down with it; in its own process only the plugin
// goes). File separate-process.txt in the settings folder, one name per line.
bool pluginAlwaysSeparate (const juce::String& name);
void setPluginAlwaysSeparate (const juce::String& name, bool on);
// The whole list (round 190gg: the Plugin manager's "Separate process" column reads it once per refresh)
juce::StringArray pluginsAlwaysSeparate();

// 32-bit plugins (round 176a, Windows x64 only): LYNX is 64-bit and can't load a 32-bit module, so a 32-bit VST2 / VST3
// always runs in the 32-bit bridge LYNX-bridge32.exe (next to LYNX.exe) — the same separate-process protocol as "Make
// bridged", as FL runs them in its System\Tools\Bridge\32bit\ilbridge.exe. The scan describes them there too.
bool bridge32Supported();               // this build can run them (Windows x64); elsewhere the option is hidden
juce::File bridge32Executable();        // LYNX-bridge32.exe beside LYNX.exe (it may be missing)
bool isModule32Bit (const juce::String& fileOrIdentifier); // a 32-bit x86 VST2 DLL / VST3 bundle or file
bool isPlugin32Bit (const juce::PluginDescription& description);
// Issue #209: the answer without touching the plugin's file — what this run or an earlier one found (kept in
// plugin-bits.txt in the settings folder), nullopt when nobody looked yet. Opening hundreds of plugin modules to read
// their headers froze the Plugin picker's first opening (each first read of a DLL waits for the virus scanner:
// 99 s for 486 modules on the test PC, 0.4 s the second time). Lists use this and refreshPluginBitness().
std::optional<bool> knownPlugin32Bit (const juce::PluginDescription& description);
// Looks at the plugins whose answer is missing or whose file changed, on a background thread (one at a time), keeps
// the answers in plugin-bits.txt and then calls `changed` on the message thread when any answer is new.
void refreshPluginBitness (const juce::Array<juce::PluginDescription>& types, std::function<void()> changed);
// Tests: wait for a running refresh (at most `ms`); true when none is running
bool waitForPluginBitness (int ms);
// A sandbox proxy running in the 32-bit bridge.
bool isBridged32 (const juce::AudioPluginInstance& plugin);
// Any sandbox proxy (Options' separate process, "Make bridged" or the 32-bit bridge): its editor is a window of another
// process, so it can't be docked inside the main window (issue #116).
bool isSandboxedPlugin (const juce::AudioPluginInstance& plugin);

// Opens the plugin in a child process, already prepared (rate, block, base64 state). nullptr + `error` on
// failure (child didn't start, plugin didn't load, timeout). `group` (round 190jg, sandboxGroupFor): the plugin joins
// the running process of that group, else starts it; empty = a process of its own.
std::unique_ptr<juce::AudioPluginInstance> createSandboxedPlugin (const juce::PluginDescription& description, double sampleRate,
                                                                  int blockSize, const std::string& stateBase64, juce::String& error,
                                                                  const juce::String& group = {});
// Running group processes (tests), and a proxy's group ("" for a process of its own; for the 32-bit bridge "32|"...)
int sandboxProcessCount();
juce::String sandboxGroupOf (const juce::AudioPluginInstance& plugin);

// Separate-process plugins whose process died this session (round 176a; newest last; UI thread): the plugin's name, and
// whether it crashed (else it stopped responding) and ran in the 32-bit bridge. `count` only grows, so the UI notices a
// new one by comparing it (MainComponent shows round 174a's "Plugin crashed" prompt).
struct SandboxDeath
{
    juce::String plugin;
    bool crashed = false, bits32 = false;
    juce::String group; // round 190jg: the group of its process ("" = a process of its own)
    bool alone = true;  // the only plugin in its process when it died (else the others in it died with it)
};
int pluginSandboxDeathCount() noexcept;
std::vector<SandboxDeath> pluginSandboxDeaths();

// True if the plugin is a sandbox proxy and its process died (hung, exited or stopped responding).
bool isSandboxedPluginDead (const juce::AudioPluginInstance& plugin);

// A proxy ends its child process on a thread of its own (issue #159: JUCE's pipe waits up to 10 s for a dead child,
// which froze the window whenever the rack freed such a plugin). At exit: waits up to `timeoutMs` for the ones still
// ending; false if some are left (the process may end anyway: their children end when the pipe closes).
bool finishSandboxTeardowns (int timeoutMs);
int sandboxTeardownsRunning(); // (tests)

// Child mode: if the command line belongs to a sandbox process, connects to the parent and returns true (the app
// must not open a window or audio). The process exits on its own when the parent goes away.
bool runPluginSandboxWorker (const juce::String& commandLine);
void shutdownPluginSandboxWorker();

// Tests (--sandboxtest): no alert box when a plugin dies; and a request that makes the child
// hang on its next audio call (to check the timeout).
void setPluginSandboxAlerts (bool show);
void hangSandboxedPluginForTest (juce::AudioPluginInstance& plugin);
// Test (--bridge32test): the child's process id (0 when unknown).
int sandboxedPluginProcessId (const juce::AudioPluginInstance& plugin);
// Test: makes the child process crash (abort) as a misbehaving plugin would; LYNX must keep running.
void crashSandboxedPluginForTest (juce::AudioPluginInstance& plugin);
} // namespace lynx
