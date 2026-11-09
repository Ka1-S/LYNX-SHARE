// --- Round 184a: the three layout styles (core/LayoutStyle.h): Pattern (default), Clip-launch and Simple workflow.

#include "core/LayoutStyle.h"

TEST (simple184a_layout_style_ids_round_trip)
{
    using namespace lynx::layoutstyle;
    for (auto s : { Style::pattern, Style::clipLaunch, Style::simple })
    {
        CHECK (fromId (id (s)) == s);
        CHECK (fromComboId (comboId (s)) == s);
    }
    CHECK (std::string (id (Style::pattern)) == "pattern");
    CHECK (std::string (id (Style::clipLaunch)) == "clipLaunch");
    CHECK (std::string (id (Style::simple)) == "simple");
    // unknown or empty: the default, so a settings file from a newer version opens in Pattern workflow
    CHECK (fromId ("") == Style::pattern);
    CHECK (fromId ("somethingNew") == Style::pattern);
    CHECK (fromComboId (0) == Style::pattern);
    CHECK (fromComboId (9) == Style::pattern);
    CHECK (comboId (Style::pattern) == 1 && comboId (Style::clipLaunch) == 2 && comboId (Style::simple) == 3); // 181a's ids kept
}

TEST (simple184a_layout_style_workspace_files)
{
    using namespace lynx::layoutstyle;
    // round 181a's two file names are kept, so the users' saved layouts still load
    CHECK (std::string (workspaceFile (Style::pattern)) == "workspace-pattern.txt");
    CHECK (std::string (workspaceFile (Style::clipLaunch)) == "workspace-cliplaunch.txt");
    CHECK (std::string (workspaceFile (Style::simple)) == "workspace-simple.txt");
}

TEST (simple184a_layout_style_settings_keys)
{
    using namespace lynx::layoutstyle;
    // a 181a settings file: only the old key
    {
        SettingsReader r;
        r.legacyFlag (true);
        CHECK (r.style == Style::clipLaunch);
    }
    {
        SettingsReader r;
        CHECK (r.style == Style::pattern); // nothing saved: the default
        r.legacyFlag (false);
        CHECK (r.style == Style::pattern);
    }
    // both keys, in either order: "layoutStyle" wins
    {
        SettingsReader r;
        r.legacyFlag (false);
        r.styleId ("simple");
        CHECK (r.style == Style::simple);
    }
    {
        SettingsReader r;
        r.styleId ("simple");
        r.legacyFlag (false); // written as 0 for Simple, so an older version opens Pattern workflow
        CHECK (r.style == Style::simple);
    }
    {
        SettingsReader r;
        r.styleId ("clipLaunch");
        r.legacyFlag (true);
        CHECK (r.style == Style::clipLaunch);
    }
    // what is written for the old key
    CHECK (legacyClipLaunchFlag (Style::clipLaunch));
    CHECK (! legacyClipLaunchFlag (Style::simple));
    CHECK (! legacyClipLaunchFlag (Style::pattern));
}
