// Round 168t (the YouTube review, 168d #4/#5/#9/#10/#15): FL's modifier roles on macOS (core/ModifierMap.h), FL's
// ruler gestures (core/RulerGestures.h) and the Channel Rack's built-in views (core/RackFilter.h).
// Evidence: docs/analise/REA_rodada168t.md. Included from tests.cpp.

#include "core/ModifierMap.h"
#include "core/RackFilter.h"
#include "core/RulerGestures.h"

#include <filesystem>
#include <fstream>
#include <sstream>

TEST (sel168t_windows_linux_keep_juce_meaning)
{
    using namespace lynx::modmap;
    for (auto os : { Os::windows, Os::linux })
    {
        Raw r;
        r.control = true;
        r.left = true;
        CHECK (map (r, os).ctrl);
        CHECK (! map (r, os).popup);
        r = {};
        r.command = true; // no Command key there (JUCE: isCommandDown() == isCtrlDown()), never set alone
        r.left = true;
        CHECK (! map (r, os).ctrl);
        r = {};
        r.right = true;
        CHECK (map (r, os).popup);
        CHECK (! map (r, os).ctrl);
    }
}

TEST (sel168t_mac_cmd_is_fl_ctrl_and_control_click_is_no_menu)
{
    using namespace lynx::modmap;
    Raw r;
    r.command = true;
    r.left = true;
    CHECK (map (r, Os::mac).ctrl);  // FL for Mac: Cmd where Windows has Ctrl
    CHECK (! map (r, Os::mac).popup);

    r = {};
    r.control = true; // JUCE on a Mac: isPopupMenu() is true here — the bug of the review
    r.left = true;
    CHECK (map (r, Os::mac).ctrl);  // still FL's Ctrl gesture (the hint said "Ctrl")
    CHECK (! map (r, Os::mac).popup); // FL 2026 for Mac: Control+click is a left click
    CHECK (map (r, Os::mac).left);

    r = {};
    r.right = true; // two-finger click / right button
    CHECK (map (r, Os::mac).popup);
    r.command = true;
    CHECK (map (r, Os::mac).ctrl && map (r, Os::mac).right); // Cmd+right = FL's Ctrl+right (zoom on the ruler)
}

TEST (sel168t_mac_key_names)
{
    using namespace lynx::modmap;
    const std::string cmd = "\xe2\x8c\x98", opt = "\xe2\x8c\xa5";
    CHECK (keyNames ("Ctrl+B", Os::windows) == "Ctrl+B");
    CHECK (keyNames ("Ctrl+B", Os::linux) == "Ctrl+B");
    CHECK (keyNames ("Ctrl+B", Os::mac) == cmd + "+B");
    CHECK (keyNames ("Ctrl+Alt+Z", Os::mac) == cmd + "+" + opt + "+Z");
    CHECK (keyNames ("Ctrl", Os::mac) == cmd); // a hint key cap
    CHECK (keyNames ("Shift", Os::mac) == "Shift");
    CHECK (keyNames ("[Strg][LMB] Ziehen", Os::mac) == "[" + cmd + "][LMB] Ziehen");
    CHECK (keyNames ("[Ctrl] drag: select", Os::mac) == "[" + cmd + "] drag: select");
    // whole words only
    CHECK (keyNames ("Alternate Control Ctrls Salt", Os::mac) == "Alternate Control Ctrls Salt");
    CHECK (keyNames ("Arraste com Ctrl: seleção", Os::mac) == "Arraste com " + cmd + ": seleção");
}

namespace
{
lynx::ruler::Press rulerPress (bool left, bool right, bool ctrl, bool shift, int clicks = 1)
{
    lynx::ruler::Press p;
    p.mods.left = left;
    p.mods.right = right;
    p.mods.ctrl = ctrl;
    p.mods.shift = shift;
    p.mods.popup = right;
    p.clicks = clicks;
    return p;
}
} // namespace

TEST (sel168t_ruler_left_gestures_follow_fl)
{
    using namespace lynx::ruler;
    using lynx::EditTool;
    CHECK (press (rulerPress (true, false, false, false)) == Action::seek);
    CHECK (press (rulerPress (true, false, true, false)) == Action::select);           // Ctrl+drag
    CHECK (press (rulerPress (true, false, false, false, 2)) == Action::select);        // double click held
    CHECK (press (rulerPress (true, false, false, true)) == Action::select);           // Shift, no selection yet
    auto p = rulerPress (true, false, false, false);
    p.tool = EditTool::lasso; // FL's Select tool: a plain drag selects time
    CHECK (press (p) == Action::select);
    p.tool = EditTool::zoom;
    CHECK (press (p) == Action::zoomRange);
    p = rulerPress (true, false, false, true);
    p.hasSelection = true;
    CHECK (press (p) == Action::moveSelection); // FL: Shift+drag moves the selection, inside or not
    p.mods.ctrl = true;
    CHECK (press (p) == Action::moveSelection);
    p.insideSelection = true;
    CHECK (press (p) == Action::moveWithClips); // LYNX's extra: Ctrl+Shift inside moves the clips too
    p = rulerPress (true, false, false, false);
    p.onMarkerTag = true;
    CHECK (press (p) == Action::marker);
    p.mods.ctrl = true;
    CHECK (press (p) == Action::markerSpan);
}

TEST (sel168t_ruler_right_gestures_follow_fl)
{
    using namespace lynx::ruler;
    using lynx::EditTool;
    // right-drag = a time selection (before 168t: always the ruler menu, and on a Mac Ctrl+drag became it too)
    CHECK (press (rulerPress (false, true, false, false)) == Action::select);
    auto p = rulerPress (false, true, false, false);
    p.hasSelection = true;
    CHECK (press (p) == Action::adjust);
    p.mods.ctrl = true;
    CHECK (press (p) == Action::zoomRange);
    p = rulerPress (false, true, false, false);
    p.onMarkerTag = true;
    CHECK (press (p) == Action::markerMenu);
    p.tool = EditTool::lasso; // FL: the Select / Zoom tools never open the marker menu
    CHECK (press (p) == Action::select);
    p.tool = EditTool::zoom;
    CHECK (press (p) == Action::zoomRange);
}

TEST (sel168t_ruler_mac_ctrl_drag_makes_a_time_selection)
{
    // the review's gesture on a Mac, through the whole chain: Control+drag and Cmd+drag on the ruler
    using namespace lynx;
    for (auto key : { 0, 1 })
    {
        modmap::Raw r;
        r.left = true;
        (key == 0 ? r.control : r.command) = true;
        ruler::Press p;
        p.mods = modmap::map (r, modmap::Os::mac);
        CHECK (ruler::press (p) == ruler::Action::select);
    }
}

TEST (sel168t_ruler_adjust_and_move_maths)
{
    using namespace lynx::ruler;
    // the nearer edge follows: the farther one is the anchor
    CHECK (adjustAnchor (960, 3840, 1200) == 3840);
    CHECK (adjustAnchor (960, 3840, 3500) == 960);
    CHECK (adjustAnchor (960, 3840, 100) == 3840);
    CHECK (adjustAnchor (960, 3840, 9000) == 960);
    // moving keeps the length and never goes before the start
    CHECK (moved (960, 3840, 480).from == 1440 && moved (960, 3840, 480).to == 4320);
    CHECK (moved (960, 3840, -5000).from == 0 && moved (960, 3840, -5000).to == 2880);
    CHECK (isClick (1, 1));
    CHECK (! isClick (4, 0));
}

TEST (sel168t_rack_builtin_views)
{
    using namespace lynx;
    Song s;
    s.channels.resize (5);
    s.channels[1].instrument = InstrumentKind::audioClip;
    s.channelGroups = { "Drums", "Audio clips", "Unsorted" };
    s.channels[2].group = 0;      // Drums
    s.channels[3].group = 1;      // FL's "Audio clips" group (an imported audio channel)
    s.channels[4].group = 2;      // FL's "Unsorted" group
    s.channels[0].instrument = InstrumentKind::sampler;

    CHECK (rackfilter::shows (s, 1, rackfilter::audioClips));
    CHECK (rackfilter::shows (s, 3, rackfilter::audioClips));
    CHECK (! rackfilter::shows (s, 0, rackfilter::audioClips));
    CHECK (! rackfilter::shows (s, 2, rackfilter::audioClips));

    CHECK (rackfilter::shows (s, 0, rackfilter::unsorted));
    CHECK (rackfilter::shows (s, 4, rackfilter::unsorted));
    CHECK (! rackfilter::shows (s, 1, rackfilter::unsorted)); // audio clips are hidden there, as in FL
    CHECK (! rackfilter::shows (s, 3, rackfilter::unsorted));
    CHECK (! rackfilter::shows (s, 2, rackfilter::unsorted));

    for (int c = 0; c < 5; ++c)
        CHECK (rackfilter::shows (s, c, rackfilter::all));
    CHECK (rackfilter::shows (s, 2, 0) && ! rackfilter::shows (s, 0, 0));

    CHECK (rackfilter::clamp (s, 7) == rackfilter::all);
    CHECK (rackfilter::clamp (s, rackfilter::unsorted) == rackfilter::unsorted);
    CHECK (rackfilter::groupForNewChannel (rackfilter::audioClips) == -1);
    CHECK (rackfilter::groupForNewChannel (0) == 0);
    CHECK (rackfilter::isBuiltinName ("Audio clips") && rackfilter::isBuiltinName ("Unsorted") && ! rackfilter::isBuiltinName ("Drums"));
}

TEST (sel168t_editors_ask_fl_roles_not_juce_popup)
{
    // the editors with FL Ctrl gestures must not ask JUCE's isPopupMenu() / isCtrlDown() (macOS: Control+click =
    // popup, Cmd ignored); they use ui/FlMods.h. Skipped when the sources are not next to the tests.
    const auto root = std::filesystem::path (__FILE__).parent_path().parent_path() / "src" / "ui";
    for (const char* file : { "PlaylistComponent.cpp", "PianoRollComponent.cpp", "PianoRollComponent.h", "ChannelRackComponent.cpp",
                              "MixerComponent.cpp", "EventEditor.cpp", "AutomationUi.cpp", "EditorTools.cpp" })
    {
        std::ifstream in (root / file);
        if (! in)
            continue;
        std::stringstream text;
        text << in.rdbuf();
        const auto s = text.str();
        CHECK (s.find (".isPopupMenu()") == std::string::npos);
        CHECK (s.find (".isCtrlDown()") == std::string::npos);
    }
}
