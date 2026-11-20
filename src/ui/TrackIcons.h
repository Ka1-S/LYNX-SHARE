#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/TrackIcons.h"

// LYNX's own icons for channels, mixer inserts and Playlist tracks (round 168h): original line drawings on the 24x24
// grid of ui/Icons.h (one family with the toolbar icons), never FL's artwork. The model keeps FL's icon index
// (core/TrackIcons.h maps it to these).
namespace lynx::ui
{
// Draws `icon` fitted (centred, square) into `area`.
void drawTrackIcon (juce::Graphics& g, trackicons::Icon icon, juce::Rectangle<float> area, juce::Colour colour);

// The icon picker (FL's "Change icon..." of mixer tracks, channels and Playlist tracks; FL's rename dialog's F5 icon
// menu): a grid of LYNX's icons in a pop-up at `screenArea`, the current one (`currentFlIndex`, FL's index) marked,
// with the search box (round 190ng) taking the keys.
// `onPick` gets the chosen icon's FL index (0 = none).
void showTrackIconPicker (juce::Rectangle<int> screenArea, int currentFlIndex, std::function<void (int)> onPick);

// The picker itself (a TrackIconPicker of 10 columns), for --snapshot mode and tests.
std::unique_ptr<juce::Component> makeTrackIconGrid (int currentFlIndex, std::function<void (int)> onPick);

// Issue #220 (round 190ng): the picker with a search box over the grid. Typing narrows the grid to the icons whose names
// (English or the user's language) the typed words start (core/TrackIcons.h search); Enter picks the first one shown.
// The pop-up picker and the "Rename, color and icon" dialog use it; `onPick` gets FL's index (0 = none).
class IconGrid;
class TrackIconPicker final : public juce::Component,
                              private juce::KeyListener
{
public:
    TrackIconPicker (int currentFlIndex, int columns, std::function<void (int)> onPick);
    ~TrackIconPicker() override;
    void resized() override;
    void paint (juce::Graphics& g) override;

    void setQuery (const juce::String& query);
    int shownCount() const;
    int shownFlIndex (int k) const; // FL's index of the k-th icon shown (-1 past the end)
    bool pickFirstShown();
    void pick (int flIndex);
    int pickedFlIndex() const noexcept { return picked; }
    void focusSearch();
    juce::TextEditor& searchBox() noexcept { return search; }

    static constexpr int searchHeight = 26, gap = 4;

private:
    // Enter in the search box picks the first icon shown at once (before a dialog's OK, which the same key presses)
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override;
    using juce::Component::keyPressed;
    int picked = 0;
    juce::TextEditor search;
    std::unique_ptr<IconGrid> grid;
};
} // namespace lynx::ui
