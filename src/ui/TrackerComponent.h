#pragma once

// The tracker editor (round 190nw, 0.1.31 queue item 2, stage 1 "M1"; design: study190nd/tracker/DESIGN-tracker.md and
// its wireframe). A view of a pattern's own notes, as the Piano roll and the Channel rack are (core/TrackerGrid.h): time
// runs down the rows, each channel (or track group) is a track, cells show Note / Ins / Vol / Pan / Dly / Fx. Keyboard
// first: FL's typing-keyboard rows enter notes (with Edit on), 1 = OFF, hex digits the values, Del clears, Insert /
// Backspace shift rows, octave and edit step, block selection, copy / paste (also as text), transpose, interpolate,
// follow playback. Every edit is one undo step through AudioEngine::editSong.
//
// Where it lives (LYNX's own look in each): a tool window in the Pattern layout (View › Tracker, the pattern and channel
// menus), "Edit in tracker" on a clip of the Clip-launch grid, and a tab of the Simple dock (one track's pattern).
// Painting: only the visible rows and tracks, the text through ui/TextCache; while playing only the old and new playhead
// rows repaint (a FrameTicker that detaches when the transport stops: no timer while stopped).

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/TrackerGrid.h"

namespace lynx
{
class AudioEngine;

class TrackerComponent final : public juce::Component, private juce::ChangeListener, private juce::ScrollBar::Listener
{
public:
    // compact: the Simple dock's tab (no pattern box, no side panel)
    explicit TrackerComponent (AudioEngine& engine, bool compact = false);
    ~TrackerComponent() override;

    // The pattern shown: -1 follows the song's current pattern (the tool window), else that one (a clip's, a track's)
    void showPattern (int pattern);
    int shownPattern() const noexcept;
    // Only these channels (a Simple track's); nullopt = every channel with notes, and the selected one
    void setOnlyChannels (std::optional<std::vector<int>> channels);
    // The cursor to the track of `channel` (when it has one), scrolled into view (round 190ob).
    void showChannel (int channel);

    // User settings (ui-options.txt: trackerHex, trackerOctave, trackerStep, trackerFollow)
    static inline bool hex = true;
    static inline int octave = 4;   // 0..9: the lower typing row starts at C<octave> (FL's octave numbers)
    static inline int editStep = 1; // 0..16 rows the cursor moves after an entry
    static inline bool follow = true;
    static constexpr int minOctave = 0, maxOctave = 9, maxEditStep = 16;
    static inline std::function<void()> onSettingsChanged; // (the main window saves ui-options.txt)

    // --- tests and --tracker190nwshot ---
    void setCursorForCheck (const tracker::Cursor& c);
    tracker::Cursor cursorForCheck() const noexcept { return cursor; }
    void setSelectionForCheck (const tracker::Cursor& from, const tracker::Cursor& to);
    void setEditForCheck (bool on);
    bool keyForCheck (const juce::KeyPress& key) { return keyPressed (key); }
    void setPlayRowForSnapshot (std::int64_t row);
    const tracker::Layout& layoutForCheck() const noexcept { return layout; }
    double paintBenchMs (int frames);
    double paintRowsBenchMs (int frames); // a playhead step: two rows
    int rowsRepaintedForCheck() const noexcept { return lastRowsRepainted; }
    // round 190ob: the bench's frames, each painted whole into a 1:1 image: the median wall time and the median of the
    // instructions the thread retires (-1: no counter), and the last frame's pixels (a hash)
    enum class BenchFrame
    {
        same,     // the same frame again (nothing moved)
        scroll,   // one row further down each frame (wheel / follow scrolling)
        uncached, // the row-chunk cache emptied before each frame (the first frame of a view, fractional scales)
        edit,     // one track's chunks dropped before each frame (what an edit in that track repaints)
        playhead  // the two rows of a playhead step (not a whole frame)
    };
    struct BenchResult
    {
        double ms = 0.0;
        long long instructions = -1;
        std::uint64_t pixels = 0;
    };
    BenchResult paintBench (BenchFrame kind, int frames);
    // one whole frame's pixels (a hash) with the row-chunk cache used or bypassed: the two must be identical
    std::uint64_t framePixelsForCheck (bool cached);
    // the same frame as an image (round 190pb: the Windows check compares pixels within a tolerance, see
    // framesDifferForCheck)
    juce::Image frameForCheck (bool cached);
    // pixels of `a` and `b` (same size) whose colour differs by more than `tolerance` levels in a channel; the largest
    // difference in `largest` and the pixels that differ at all in `anyDifference`
    static int framesDifferForCheck (const juce::Image& a, const juce::Image& b, int tolerance, int& largest, int& anyDifference);
    void scrollToRowForCheck (std::int64_t row);
    size_t cachedChunksForCheck() const noexcept;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void visibilityChanged() override;
    void focusLost (FocusChangeType) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    AudioEngine& engine;
    bool compact = false;
    tracker::Layout layout;
    tracker::Cursor cursor, anchor;
    bool selecting = false, hasSelection = false;
    std::int64_t firstRow = 0; // the top visible row
    int scrollX = 0;
    std::int64_t playRow = -1;
    int lastRowsRepainted = 0;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    friend struct Impl;
};

// The tool window (one): opens it, or brings it to the front, on `pattern` (-1: the current pattern, which it follows);
// `channel` >= 0: the cursor goes to that channel's track (round 190ob: the Channel rack's "Edit in tracker").
void openTracker (AudioEngine& engine, int pattern = -1, int channel = -1);
void closeTracker();
bool isTrackerOpen();
// The window's editor on its own (for --snapshot style captures).
std::unique_ptr<TrackerComponent> makeTracker (AudioEngine& engine, bool compact = false);
} // namespace lynx
