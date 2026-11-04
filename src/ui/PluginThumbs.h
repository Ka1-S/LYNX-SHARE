#pragma once

// The Plugin picker's thumbnails (round 171c, discussion #10): a small picture of every plugin's editor, cached on disk.
//
// LYNX's own generators and effects are drawn from their own editors (their themed art, rounds 170j/170k) by a
// background copy of LYNX (`LYNX --pluginthumbs <folder>`), so the picker never builds an editor on the message
// thread: it opens at once with plain tiles and loads each picture when it is shown. Third-party VST / CLAP plugins get
// a tile with their maker's name until their editor has been opened once: PluginWindow then captures it (Windows) and
// the picture is kept. Nothing of FL Studio's pictures is used.

#include <functional>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
}

namespace lynx::pluginthumbs
{
// The largest picture kept (pixels): enough for a 190-px tile on a 200 % display.
constexpr int maxWidth = 384, maxHeight = 288;

// <cache>/plugin-thumbs/<LYNX version>: the pictures of LYNX's own plugins (redrawn by each version: the art changes)
juce::File nativeFolder();
// <cache>/plugin-thumbs/plugins: the captured editors of third-party plugins (kept across versions)
juce::File capturedFolder();
// A file name for a plugin key ("lynx:fx:Facet EQ" → "fx-facet-eq-1a2b3c4d.png")
juce::String fileNameFor (const std::string& key);
// Where the picture of `key` is (it may not exist yet)
juce::File fileFor (const std::string& key, bool native);

// `image` scaled down (never up) to fit maxWidth × maxHeight, high-quality resampling
juce::Image shrink (const juce::Image& image);

// --pluginthumbs <folder> [key,key...]: draws the editor of every LYNX generator and effect (or only those keys) into
// `folder`, skipping the pictures that exist. `progress` gets one line per plugin. Returns the number drawn.
int renderNative (const juce::File& folder, const juce::StringArray& only, const std::function<void (const juce::String&)>& progress);

// Starts the background drawing (a second LYNX process) when pictures are missing; at most one at a time, and only
// once per run unless `again` (after a version change of the art, say). Returns whether one is running.
bool ensureRendering (bool again = false);
bool rendering();

// A third-party plugin's editor that is open and drawn: its picture is captured once (when none is kept yet) and kept
// under capturedFolder(). `editor` = the component holding the plugin's editor (its area of the window is captured,
// with the plugin's own native drawing). No-op off Windows.
void captureEditor (const std::string& key, juce::Component& editor);
} // namespace lynx::pluginthumbs
