#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

// First-run setup (small and skippable; Options > Setup wizard reopens it): language, audio device with a test
// sound, plugin folders (the standard VST3/CLAP folders plus the ones FL Studio uses on this PC), and a plugin
// scan that runs in the background (it keeps going after the window closes): "Scan now", or Finish with "Look for new
// plugins in the background at every start" ticked; unticked, Finish scans nothing (core/AutoScanPolicy.h).
// It is shown as an overlay on top of the main window (`makeSetupWizardOverlay`: the caller adds it over its whole
// area and deletes it, asynchronously, when `onClose` runs — after Skip, Finish or Escape).
// `onLanguage(id)` runs when another language was chosen (the caller saves it and offers a restart).
// Round 184a S17 (design D21): the wizard also offers the three layout styles (core/LayoutStyle.h), with `layoutStyle`
// (the current one; Pattern workflow on a first start) selected; `onLayoutStyle(style)` runs on Finish when another
// one was chosen. Skip changes nothing.
std::unique_ptr<juce::Component> makeSetupWizardOverlay (AudioEngine& engine, std::function<void (const juce::String&)> onLanguage,
                                                         std::function<void()> onClose, int layoutStyle = 0,
                                                         std::function<void (int)> onLayoutStyle = {});

// The wizard's panel alone (its natural size), for --snapshot mode.
std::unique_ptr<juce::Component> makeSetupWizardContent (AudioEngine& engine, std::function<void (const juce::String&)> onLanguage,
                                                         std::function<void()> onClose, int layoutStyle = 0,
                                                         std::function<void (int)> onLayoutStyle = {});

// Whether the wizard was finished or skipped once (%APPDATA%\LYNX\setup-done.txt).
bool setupWizardDone();
void markSetupWizardDone();

// "Scan for new plugins at every start" (on by default; set in the wizard).
bool backgroundScanAtStart();
void setBackgroundScanAtStart (bool on);
} // namespace lynx
