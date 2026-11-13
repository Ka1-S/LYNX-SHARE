#pragma once

// The main window's layout style (View › Layout style, Settings › General › Workflow): FL's Pattern workflow (the
// default), the Clip-launch workflow (round 181a) and the Simple workflow (round 184a, design in
// /mnt/e/lynx-work/simple184a/DESIGN.md: one window with the tracks, the timeline and a dock for the selected track).
// A style is a preference of the user, not part of a project: switching moves panels only. No JUCE here.

#include <atomic>
#include <string>
#include <string_view>

namespace lynx::layoutstyle
{
enum class Style : int
{
    pattern,    // FL's channel rack, patterns and Playlist (the default)
    clipLaunch, // a clip grid with scenes beside the Playlist
    simple      // one window: the tracks, the timeline and a dock for the selected track
};

// The style the main window shows now (round 190cm), for the panels and tool windows whose keys depend on it
// (core/SessionKeys.h). The main window sets it when the style changes; Pattern until then.
namespace detail
{
inline std::atomic<int> shown { (int) Style::pattern };
}
inline Style current() noexcept { return (Style) detail::shown.load (std::memory_order_relaxed); }
inline void setCurrent (Style s) noexcept { detail::shown.store ((int) s, std::memory_order_relaxed); }

constexpr int count = 3;

// The id saved in the UI settings ("layoutStyle=..."); stable, never translated.
inline const char* id (Style s) noexcept
{
    switch (s)
    {
        case Style::clipLaunch: return "clipLaunch";
        case Style::simple: return "simple";
        case Style::pattern: break;
    }
    return "pattern";
}

// The style of a saved id; anything unknown (or empty) is the default, so a newer settings file opens in Pattern workflow.
inline Style fromId (std::string_view text) noexcept
{
    if (text == "clipLaunch")
        return Style::clipLaunch;
    if (text == "simple")
        return Style::simple;
    return Style::pattern;
}

// The workspace layout file kept for each style, so going back to a style restores its panels exactly. The first two
// are round 181a's names.
inline const char* workspaceFile (Style s) noexcept
{
    switch (s)
    {
        case Style::clipLaunch: return "workspace-cliplaunch.txt";
        case Style::simple: return "workspace-simple.txt";
        case Style::pattern: break;
    }
    return "workspace-pattern.txt";
}

// The value written for round 181a's older key "clipLaunchWorkflow" (kept, so a downgrade still opens the right style:
// an older version reads only that key and opens Simple workflow as Pattern workflow).
inline bool legacyClipLaunchFlag (Style s) noexcept { return s == Style::clipLaunch; }

// Settings › General's combo box: ids 1… in this order (0 is "nothing selected" for juce::ComboBox).
inline int comboId (Style s) noexcept { return (int) s + 1; }
inline Style fromComboId (int comboId) noexcept { return comboId == 2 ? Style::clipLaunch : comboId == 3 ? Style::simple : Style::pattern; }

// Reads the two settings keys whatever their order in the file: "layoutStyle" wins over round 181a's
// "clipLaunchWorkflow", which is read only when no "layoutStyle" line was seen.
struct SettingsReader
{
    Style style = Style::pattern;
    bool sawStyle = false;

    void legacyFlag (bool clipLaunch) noexcept
    {
        if (! sawStyle)
            style = clipLaunch ? Style::clipLaunch : Style::pattern;
    }
    void styleId (std::string_view text) noexcept
    {
        style = fromId (text);
        sawStyle = true;
    }
};
} // namespace lynx::layoutstyle
