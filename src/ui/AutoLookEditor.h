#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/AutoLook.h"

// Issue #239 (round 190od, LYNX extra): Settings › LYNX extras › "Automatic colors and icons": the user's rules
// (core/AutoLook.h) — the switch, one row per rule (how the name is compared, the text, the colour, the icon, what it
// colours, move up / down, remove), Add rule and Apply to this project. Every change is saved at once (autolookui::save).
namespace lynx
{
class AudioEngine;

namespace autolookui
{
juce::File file(); // <settings>/auto-look.txt
void load();       // the file into autolook::current() (start-up)
void save();       // autolook::current() into the file
} // namespace autolookui

class AutoLookEditor final : public juce::Component
{
public:
    AutoLookEditor (AudioEngine& engine, bool persistent);
    ~AutoLookEditor() override;

    int heightFor (int width) const;
    void resized() override;
    void refresh(); // the rows again from autolook::current()

    std::function<void()> onHeightChange; // the page lays itself out again (a row added or removed)

    // tests / --issuesshot190od
    int rowCount() const noexcept { return (int) rows.size(); }
    juce::ToggleButton& switchButton() noexcept { return enable; }

private:
    struct Row;
    void changed (bool relayout = false);
    void addRow (int index);
    void moveRule (int index, int by); // round 190qo: up (−1) / down (+1) the list
    int noteHeight (int width) const;

    AudioEngine& engine;
    bool persistent;
    juce::ToggleButton enable;
    juce::Label note, status;
    juce::TextButton addButton, applyButton;
    std::vector<std::unique_ptr<Row>> rows;
    static constexpr int rowHeight = 30, gap = 6, noteIndent = 27;
};
} // namespace lynx
