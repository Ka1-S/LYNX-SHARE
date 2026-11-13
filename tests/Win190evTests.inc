// Round 190ev (issue #152, 0.1.26 on Windows 11: "flickering windows; selecting the window behind does not bring it over
// the one in front; the Channel rack almost never comes to the front"): the stacking rules of the windows docked in the
// main window (core/DockStack.h). The native side (child windows, the click hook) is checked by --plugwintest's dock mode.
#include "core/DockStack.h"

#include <set>
#include <string>

TEST (win190ev_a_click_in_a_plugin_view_belongs_to_its_docked_window)
{
    using lynx::dockstack::clickedWindow;
    const std::set<std::string> docked { "frame", "editor" };
    auto isDocked = [&docked] (const std::string& w) { return docked.count (w) > 0; };
    // the plugin's own view, two levels under the docked frame, under the main window
    CHECK (clickedWindow (std::vector<std::string> { "knob", "view", "frame", "main" }, isDocked) == 2);
    // a click on the docked window's own native window: index 0 (JUCE's own mouse path takes it)
    CHECK (clickedWindow (std::vector<std::string> { "editor", "main" }, isDocked) == 0);
    // a click on a panel or on the main window: no docked window
    CHECK (clickedWindow (std::vector<std::string> { "rack", "main" }, isDocked) == -1);
    CHECK (clickedWindow (std::vector<std::string> {}, isDocked) == -1);
}

TEST (win190ev_a_clicked_docked_window_rises_above_panels_but_not_above_overlays)
{
    using lynx::dockstack::needsRaise;
    // bottom to top: Playlist, Channel rack, a channel's settings, Mixer, a dancer that keeps in front, the wizard
    const std::vector<std::string> stack { "playlist", "rack", "settings", "mixer", "dancer", "wizard" };
    const std::set<std::string> inFront { "dancer", "wizard" };
    auto staysAbove = [&inFront] (const std::string& w) { return inFront.count (w) > 0; };
    CHECK (needsRaise (stack, std::string ("settings"), staysAbove)); // the Mixer covers it
    CHECK (needsRaise (stack, std::string ("rack"), staysAbove));
    CHECK (! needsRaise (stack, std::string ("mixer"), staysAbove)); // only the dancer and the wizard above it
    CHECK (! needsRaise (stack, std::string ("wizard"), staysAbove));
    CHECK (! needsRaise (stack, std::string ("gone"), staysAbove)); // not among the siblings
    // a hidden sibling above covers nothing
    const std::set<std::string> hiddenToo { "dancer", "wizard", "mixer" };
    CHECK (! needsRaise (stack, std::string ("settings"), [&hiddenToo] (const std::string& w) { return hiddenToo.count (w) > 0; }));
}

TEST (win190ev_the_desktop_list_is_reordered_only_when_it_differs)
{
    using lynx::dockstack::listMatches;
    // JUCE's list of desktop windows: the main window, then the docked children, then the owned windows
    std::vector<std::string> list { "main", "playlist", "rack", "plugin", "dialog" };
    auto indexOf = [&list] (const std::string& w) {
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i] == w)
                return (int) i;
        return -1;
    };
    const std::vector<std::string> screen { "playlist", "rack", "plugin", "dialog" };
    CHECK (listMatches (std::string ("main"), screen, indexOf));
    // the main window came to the front of JUCE's list (a click in it): the list must be put back
    list = { "playlist", "rack", "plugin", "dialog", "main" };
    CHECK (! listMatches (std::string ("main"), screen, indexOf));
    // two children swapped in the list against the screen
    list = { "main", "rack", "playlist", "plugin", "dialog" };
    CHECK (! listMatches (std::string ("main"), screen, indexOf));
    // a window missing from the list
    list = { "main", "playlist", "plugin", "dialog" };
    CHECK (! listMatches (std::string ("main"), screen, indexOf));
}

TEST (win190ev_docked_panels_do_not_fade_where_the_window_alpha_flashes)
{
    using lynx::dockstack::panelFades;
    // Windows: a docked panel shows and hides at once (the alpha fade flashed it twice); a plain panel still fades
    CHECK (! panelFades (true, true, true));
    CHECK (panelFades (true, false, true));
    // elsewhere round 190i's fade stays; Animations off: never
    CHECK (panelFades (true, true, false));
    CHECK (! panelFades (false, true, false) && ! panelFades (false, false, true));
}
