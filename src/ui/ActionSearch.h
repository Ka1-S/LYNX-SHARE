#pragma once

#include <functional>
#include <memory>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ActionSearch.h"
#include "core/KeyMap.h"

namespace lynx
{
// #228 (round 190nq), a LYNX extra: the action search ("?"). A box over the main window: a search field and the actions
// of the key map that match, with their panel and keys. Up / Down pick, Enter or a double click runs it (`run` returns
// false where the action can't run now: the box says so and stays), Esc or a click beside the box closes it.
class ActionSearchOverlay final : public juce::Component
{
public:
    ActionSearchOverlay (const keys::KeyMap& map, std::function<bool (const std::string&)> run);
    ~ActionSearchOverlay() override;

    void setQuery (const juce::String& text); // (--snapshot / tests)
    int matchCount() const;
    std::string selectedId() const;
    bool runSelected();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void parentSizeChanged() override;

    std::function<void()> onClose; // the owner removes it (called once)

private:
    class Panel;
    std::unique_ptr<Panel> panel;
};

// The entries for every action of `map` (translated, with the keys the user has now).
std::vector<actionsearch::Entry> actionSearchEntries (const keys::KeyMap& map);
} // namespace lynx
