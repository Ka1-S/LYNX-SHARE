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
// FL's picture budget (its Plugin database bitmaps, round 175b): half the editor's size, at most 258 × 128 pixels, kept
// as "192 dpi" pictures that the picker shows at half their size on a 100 % display (a quarter of the editor).
constexpr float pictureScale = 0.5f;
constexpr int maxWidth = 258, maxHeight = 128;

// <cache>/plugin-thumbs/<LYNX version>: the pictures of LYNX's own plugins (redrawn by each version: the art changes)
juce::File nativeFolder();
// <cache>/plugin-thumbs/plugins: the captured editors of third-party plugins (kept across versions)
juce::File capturedFolder();
// A file name for a plugin key ("lynx:fx:Facet EQ" → "fx-facet-eq-1a2b3c4d.png")
juce::String fileNameFor (const std::string& key);
// Where the picture of `key` is (it may not exist yet)
juce::File fileFor (const std::string& key, bool native);

// `image` (drawn at `pixelsPerUnit` pixels per logical pixel) scaled to pictureScale, at most maxWidth × maxHeight,
// high-quality resampling
juce::Image shrink (const juce::Image& image, float pixelsPerUnit = 1.0f);

// --pluginthumbs <folder> [key,key...]: draws the editor of every LYNX generator and effect (or only those keys) into
// `folder`, skipping the pictures that exist. `progress` gets one line per plugin. Returns the number drawn.
// `fullScale` > 0 (round 190bi: --shot-scale / LYNX_SHOT_SCALE given): the whole editor at that many pixels per logical
// pixel, trimmed to its drawn part but not shrunk to the picker's 258 × 128 budget (the website's plugin pictures).
int renderNative (const juce::File& folder, const juce::StringArray& only, const std::function<void (const juce::String&)>& progress,
                  float fullScale = 0.0f);

// Starts the background drawing (a second LYNX process) when pictures are missing; at most one at a time, and only
// once per run unless `again` (after a version change of the art, say). Returns whether one is running.
bool ensureRendering (bool again = false);
bool rendering();

// A third-party plugin's editor that is open and drawn: its picture is captured once (when none is kept yet) and kept
// under capturedFolder(). `editor` = the component holding the plugin's editor (its area of the window is captured,
// with the plugin's own native drawing). No-op off Windows.
void captureEditor (const std::string& key, juce::Component& editor);
} // namespace lynx::pluginthumbs
