#pragma once

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

// Plugins that always run in a separate process, by name (round 157a): e.g. one whose editor crashed LYNX (a GPU
// driver failing inside the plugin's OpenGL window takes the host down with it; in its own process only the plugin
// goes). File separate-process.txt in the settings folder, one name per line.
bool pluginAlwaysSeparate (const juce::String& name);
void setPluginAlwaysSeparate (const juce::String& name, bool on);

// 32-bit plugins (round 176a, Windows x64 only): LYNX is 64-bit and can't load a 32-bit module, so a 32-bit VST2 / VST3
// always runs in the 32-bit bridge LYNX-bridge32.exe (next to LYNX.exe) — the same separate-process protocol as "Make
// bridged", as FL runs them in its System\Tools\Bridge\32bit\ilbridge.exe. The scan describes them there too.
bool bridge32Supported();               // this build can run them (Windows x64); elsewhere the option is hidden
juce::File bridge32Executable();        // LYNX-bridge32.exe beside LYNX.exe (it may be missing)
bool isModule32Bit (const juce::String& fileOrIdentifier); // a 32-bit x86 VST2 DLL / VST3 bundle or file
bool isPlugin32Bit (const juce::PluginDescription& description);
// A sandbox proxy running in the 32-bit bridge.
bool isBridged32 (const juce::AudioPluginInstance& plugin);
// Any sandbox proxy (Options' separate process, "Make bridged" or the 32-bit bridge): its editor is a window of another
// process, so it can't be docked inside the main window (issue #116).
bool isSandboxedPlugin (const juce::AudioPluginInstance& plugin);

// Opens the plugin in a child process, already prepared (rate, block, base64 state). nullptr + `error` on
// failure (child didn't start, plugin didn't load, timeout).
std::unique_ptr<juce::AudioPluginInstance> createSandboxedPlugin (const juce::PluginDescription& description, double sampleRate,
                                                                  int blockSize, const std::string& stateBase64, juce::String& error);

// Separate-process plugins whose process died this session (round 176a; newest last; UI thread): the plugin's name, and
// whether it crashed (else it stopped responding) and ran in the 32-bit bridge. `count` only grows, so the UI notices a
// new one by comparing it (MainComponent shows round 174a's "Plugin crashed" prompt).
struct SandboxDeath
{
    juce::String plugin;
    bool crashed = false, bits32 = false;
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
