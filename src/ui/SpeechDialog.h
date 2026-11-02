#pragma once

// "Speech properties" (round 168b): FL's speech synthesizer form, interpreted in LYNX's look (docs/analise/
// REA_rodada168b.md §A). The text, a Listen button, the voice character (Personality, Style, Word rate, Mode) and a
// keyboard for the voice pitch. As in FL it opens when you load a .speech file into a channel: Accept saves your
// changes as a new .speech (FL's suggested name: the text in CamelCase + "_" + the personality) and loads that one;
// Cancel, or Accept without changes, loads the file as it was. Each change is heard at once, like FL's form.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Speech.h"

namespace lynx
{
class AudioEngine;

namespace speechui
{
// LYNX's own speech presets folder (FL's "Presets\Speech" in its user data).
juce::File userSpeechFolder();

// Shows the dialog for `file` (a .speech) and then calls `then` with the file to load: the saved copy after Accept
// with changes, else `file` itself. Any other file: `then (file)` at once.
void editThenLoad (AudioEngine& engine, const juce::File& file, std::function<void (const juce::File&)> then);

// The panel alone (tests, --snapshot): `onDone (accepted)`.
std::unique_ptr<juce::Component> makePanel (AudioEngine& engine, const speech::Settings& settings, std::function<void (bool, const speech::Settings&)> onDone);
} // namespace speechui
} // namespace lynx
