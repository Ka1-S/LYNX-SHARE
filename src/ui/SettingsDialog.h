#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "ui/SettingsPages.h"

namespace lynx
{
// The Settings window (FL Studio's Settings pages, interpreted): Audio (device, driver, sample rate, buffer, status),
// MIDI (input devices, velocity, controllers), General (language, display, startup, editing, backup, plugins and
// shortcuts), Files (the folders LYNX searches for missing samples, the Browser's folders and the plugin folders),
// Project (tempo, time signature, swing, master pitch) and Info (title, author, genre, web link, comments).
// Project and Info edit the song: every change is an undo step. The others edit the app's settings at once.
enum class SettingsPage { project, info, files, general, audio, midi, theme, extras, link }; // (extras: round 190am, LYNX extras; link: round 190gl, Ableton Link)

void showSettings (AudioEngine& engine, SettingsPage page, SettingsHooks hooks = {});

// The open Settings window (F10 / F11 toggle it like FL's, issue #23): its page as an int (SettingsPage), or -1 when it
// is not open; switch it to another page in place (false: not open); close it.
int shownSettingsPage();
bool switchSettingsPage (SettingsPage page);
void closeSettings();

// The content, for --snapshot mode (`persistent` false: sample folders and devices, nothing saved).
std::unique_ptr<juce::Component> makeSettingsPanel (AudioEngine& engine, SettingsPage page, bool persistent = false, SettingsHooks hooks = {});

// Issue #247 (round 190oq, shots / tests): types `query` into the Settings panel's find bar and steps `steps` matches on
// (negative: back); what it found
struct SettingsFindState
{
    int matches = 0, current = -1, page = -1; // page: SettingsPage of the tab shown
    juce::String text;                          // the match's caption
    juce::Component* target = nullptr;          // the matched control
    juce::Component* ring = nullptr;            // the outline (nullptr: none)
    juce::TextEditor* field = nullptr;          // the find box
};
SettingsFindState settingsFindForSnapshot (juce::Component& panel, const juce::String& query, int steps = 0);

// Round 190qx (the owner's "make the options easy"): every option of every Settings page, for the command palette. Read
// once per language from the pages themselves (a Settings panel made off screen): each check box, button and row caption,
// with its page and the section it is under. `english` holds the search words the control carries ("lynxSearchText").
struct SettingsOption
{
    SettingsPage page = SettingsPage::general;
    juce::String text, section, english;
};
const std::vector<SettingsOption>& settingsOptions (AudioEngine& engine, const SettingsHooks& hooks);
double settingsOptionsBuildMs(); // how long the last reading took (the selftest bounds it)
// Shows the open Settings window on `page` with the option `caption` outlined (its find box showing the caption).
bool revealInSettings (SettingsPage page, const juce::String& caption);
// The page's name as its tab shows it
juce::String settingsPageName (SettingsPage page);

// Round 190qx: "Reset this page" (the Settings bar's right end) on a panel made by makeSettingsPanel: whether the page shown
// can be reset, and reset it as the button and its Reset do (true: done); then undo it as the bar's Undo does.
bool settingsCanResetForSnapshot (juce::Component& panel);
bool settingsResetForSnapshot (juce::Component& panel, bool confirm = true);
bool settingsUndoResetForSnapshot (juce::Component& panel);
juce::Component* settingsResetBarForSnapshot (juce::Component& panel);
bool settingsShowPageForSnapshot (juce::Component& panel, SettingsPage page);
} // namespace lynx
