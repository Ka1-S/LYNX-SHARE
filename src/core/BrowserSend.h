#pragma once

// Round 190iz: FL's "Send to selected channel or focused plugin" and FL's preview length. No JUCE: tested in isolation
// (tests/Browser190izTests.inc); docs/analise/REA_rodada190iz.md §4 and §5.

#include <algorithm>
#include <cstdint>

namespace lynx::browsersend
{
// What a Browser item is to the send.
enum class Item
{
    sample,          // an audio file
    soundFont,       // .sf2 / .sf3 / .sfz
    pluginPreset,    // a hosted plugin's preset (.lynxpreset of a plugin, .fxp, .fxb, .vstpreset, FL's .fst of one)
    generatorPreset, // a preset of one of LYNX's generators, or an FL synth preset LYNX converts (.fst of 3x Osc...)
    generator,       // a Plugin database generator
    effect,          // a Plugin database effect
    other            // projects, MIDI files, mixer presets: not something a channel takes
};

// The selected channel, as the send sees it.
enum class Channel
{
    none,
    playsSample, // a Sampler (or the drum generators that become one, the Granulizer, Wave Traveller)
    soundFont,   // a LYNX Soundfont channel
    plugin,      // a hosted plugin
    other        // any other generator
};

enum class Action
{
    none,
    sampleIntoSelected,     // the channel's sample is replaced
    sampleNewChannel,       // the selected channel can't take a sample: a new Sampler channel, as Enter does
    fontIntoSelected,       // the SoundFont replaces the selected Soundfont channel's
    fontNewChannel,
    presetToFocusedPlugin,  // the focused plugin window's plugin (else the selected channel's plugin) loads the preset
    generatorPresetChannel, // LYNX's generator presets open as they do on a double-click (a channel with that sound)
    replaceSelected,        // a Plugin database generator replaces the selected channel's generator (FL's Enter)
    newChannel,             // ... or becomes a new channel when no channel is selected
    effectToInsert          // a Plugin database effect: into the selected mixer insert
};

// FL manual browser.htm › File menu: "Send to selected channel or focused plugins - Content will be sent to the selected
// Channel or Plugin as noted"; › Actions: "Enter: Replaces the channel with the selected item". `focusedPlugin`: a
// plugin window was the last one focused (PluginWindows::openPlugins puts it first).
constexpr Action decide (Item item, Channel selected, bool focusedPlugin) noexcept
{
    switch (item)
    {
        case Item::sample: return selected == Channel::playsSample ? Action::sampleIntoSelected : Action::sampleNewChannel;
        case Item::soundFont: return selected == Channel::soundFont ? Action::fontIntoSelected : Action::fontNewChannel;
        case Item::pluginPreset: return focusedPlugin || selected == Channel::plugin ? Action::presetToFocusedPlugin : Action::none;
        case Item::generatorPreset: return Action::generatorPresetChannel;
        case Item::generator: return selected == Channel::none ? Action::newChannel : Action::replaceSelected;
        case Item::effect: return Action::effectToInsert;
        case Item::other: return Action::none;
    }
    return Action::none;
}
} // namespace lynx::browsersend

namespace lynx::browserpreview
{
// FL's preview length (manual browser.htm › Browser Menu: "By default audio files clicked in the Browser will play for
// 5 seconds" unless "Full sample preview" is on; › Preview Player: Alt+Click plays to the end, Shift+Click "starts
// playback from the mouse position"; the Play button plays it all). Owner decision F2 (details190if): FL's 5 s default.
constexpr double defaultSeconds = 5.0;

struct Range
{
    std::int64_t start = 0; // first frame
    std::int64_t end = 0;   // one past the last frame played
    bool capped = false;    // ends before the file does (a short fade-out there, no click)
};

// The part of a file of `frames` frames at `sampleRate` a preview plays: from `startFraction` (0..1, Shift+Click / a
// click on the strip's waveform) for `seconds` of the file's time, or to the end when `full` (the option, Alt+Click,
// the Play button) or the file is shorter.
inline Range range (std::int64_t frames, double sampleRate, double startFraction, bool full, double seconds = defaultSeconds)
{
    Range r;
    if (frames <= 0)
        return r;
    const auto clamped = std::clamp (startFraction, 0.0, 1.0);
    r.start = std::min<std::int64_t> (frames - 1, (std::int64_t) ((double) frames * clamped));
    r.end = frames;
    if (! full && sampleRate > 0.0 && seconds > 0.0)
    {
        const auto limit = (std::int64_t) (seconds * sampleRate + 0.5);
        if (r.start + limit < frames)
        {
            r.end = r.start + limit;
            r.capped = true;
        }
    }
    return r;
}

// The fade-out at a capped end, in device samples (10 ms), and the gain of a sample `left` samples before the end.
inline int fadeSamples (double deviceRate) noexcept { return std::max (1, (int) (deviceRate * 0.01)); }
inline float fadeGain (double left, int fade) noexcept { return left >= (double) fade ? 1.0f : (float) std::max (0.0, left / (double) fade); }
} // namespace lynx::browserpreview
