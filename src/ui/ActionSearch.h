#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ActionSearch.h"
#include "core/KeyMap.h"

namespace lynx
{
// One row of the command palette: what it shows (core/ActionSearch.h) and what Enter does (false: it can't run now; the
// box says so and stays).
struct PaletteItem
{
    actionsearch::Entry entry;
    std::function<bool()> run;
};

// #228 (round 190nq), a LYNX extra: the action search ("?"). Round 190on (research190oe P2): the command palette — the same
// box over the main window, with the key map's actions, the main menus' items, the LYNX extras' switches, the Settings
// pages and the recent projects, each with its place and its keys. Up / Down pick, Enter or a double click runs it, Esc
// or a click beside the box closes it.
class ActionSearchOverlay final : public juce::Component
{
public:
    ActionSearchOverlay (std::vector<PaletteItem> items);
    ~ActionSearchOverlay() override;

    void setQuery (const juce::String& text); // (--snapshot / tests)
    int matchCount() const;
    std::string selectedId() const;
    bool runSelected();
    int itemCount() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void parentSizeChanged() override;

    std::function<void()> onClose; // the owner removes it (called once)

    // the ids run from the palette lately, the latest first (kept in the settings folder; they go first in the list)
    static std::vector<std::string>& recentIds();

private:
    class Panel;
    std::unique_ptr<Panel> panel;
};

// The entries for every action of `map` (translated, with the keys the user has now).
std::vector<actionsearch::Entry> actionSearchEntries (const keys::KeyMap& map);

// The items of `menu` (its submenus too), as palette items: "File > Export > MP3 file...", their shortcut text (after a
// tab or in the item's shortcut column), ticked = "on", greyed when disabled. Items without an action, headers and
// separators are left out; so are the items under a section heading named `skipSection` (the File menu's recent
// projects: the palette lists them itself).
void addMenuItems (std::vector<PaletteItem>& out, const juce::PopupMenu& menu, const juce::String& path, const juce::String& skipSection = {});
} // namespace lynx
