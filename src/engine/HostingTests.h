#pragma once

#include <juce_core/juce_core.h>

// Round 190jg (BUILD-LIST 11 + 18): command-line checks of grouped plugin processes and the CLAP / VST3 hosting work.
// Each returns the process exit code (0 = passed) and writes a report; no window, no audio device.
namespace lynx
{
// --sandboxgrouptest <plugin> report.txt [--crash <crash plugin>]: plugins in two process groups and one in LYNX's
// process; one group's process is crashed (the test message, then a real crashing plugin when given): its plugins go
// silent together and are reported, the other group and the in-process plugin keep playing, a new plugin of the
// crashed group starts a new process.
int runSandboxGroupTest (const juce::String& pluginPath, const juce::String& reportPath, const juce::String& crashPluginPath);

// --sandboxbench <plugin> report.txt [--plugins N] [--block B] [--seconds S]: N instances in one chain on a real-time
// paced audio thread (48 kHz, block B), in LYNX's process, each in its own process, and all in one process: the chain's
// time per block (mean, 99th percentile, misses of the block's deadline), the IPC cost per plugin and block, and the CPU
// of LYNX and its plugin processes together.
int runSandboxBench (const juce::String& pluginPath, const juce::String& reportPath, int plugins, int block, double seconds);

// --clapext <plugin.clap> report.txt: what each plugin in the file offers and what LYNX makes of it (extensions,
// presets, remote pages, voice info, tail, thread pool, state contexts, render mode). With LYNX's own test plugin
// (tools/testplugin/lynx_test_ext.c) every answer is checked.
int runClapExtTest (const juce::String& pluginPath, const juce::String& reportPath);

// --automationtimingtest <plugin> report.txt: a song whose automation steps a parameter of the plugin (on the master
// insert) at a known sample; rendered with sample-accurate automation on and off, the sample where the plugin's output
// steps (LYNX's test plugins put the parameter on their output: tools/testplugin/lynx_test_ext.c, lynx_test_param_vst3.cpp)
int runAutomationTimingTest (const juce::String& pluginPath, const juce::String& reportPath);

// Round 190of: --pluginbench <plugin | list.txt> report.tsv [--block B] [--rate R] [--seconds S] [--types N]
// [--names a,b] [--editors]: what each plugin costs when LYNX hosts it, one row per plugin type (list.txt: one file per
// line, optionally a tab and a name filter). Scan, load and prepare times and the memory they take; the processing of a
// test signal (an effect: a 110 Hz tone with harmonics; an instrument: a held 4-note chord) on a thread of its own with
// FTZ/DAZ as LYNX's audio threads: work per block (instructions on Linux, cycles on Windows), wall time, the share of a
// core, spikes (blocks over 4x the median) and blocks over half the block's time; on Linux, voluntary context switches
// and page faults on that thread, and with LD_PRELOAD=tools/pluginbench/libbenchcount.so its allocations and mutex
// locks; then silence (notes released / input zero): when the output falls under -96 dB, what steady silence costs
// against the signal (a plugin that keeps working in silence gains from smart disable), and the same with
// flush-to-zero off (its sensitivity to denormals); LYNX's own adapter around the same instance (PluginEffect /
// PluginInstrument) against the bare processBlock; with --editors the editor's open time and memory. No audio device.
int runPluginBench (const juce::String& listOrPlugin, const juce::String& reportPath, int block, double rate, double seconds, int typesPerFile,
                    const juce::String& nameFilter, bool editors);
} // namespace lynx
