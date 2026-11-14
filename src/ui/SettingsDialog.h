#pragma once

#include <functional>
#include <memory>

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
} // namespace lynx
