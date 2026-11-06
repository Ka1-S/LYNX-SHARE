// Round 171c (discussion #10, masoudnp): the Plugin picker's category tabs, search and keyboard (core/PickerView.h).

#include "core/PickerView.h"
#include "core/PluginCatalog.h"

TEST (picker171c_tabs_lynx_first_az_installed_last)
{
    using lynx::pickerview::orderTabs;
    const auto fx = orderTabs ({ "Misc", "Installed", "Delay reverb", "LYNX SHARE", "dynamics", "Controller", "Misc" }, "LYNX SHARE", "Installed");
    CHECK (fx.size() == 6);
    CHECK (fx.size() == 6 && fx[0] == "LYNX SHARE" && fx[1] == "Controller" && fx[2] == "Delay reverb" && fx[3] == "dynamics" && fx[4] == "Misc"
           && fx[5] == "Installed");
    CHECK (orderTabs ({}, "LYNX SHARE", "Installed").empty());
}

TEST (picker171c_categories_are_round_170b_folders)
{
    // the tabs come from FL's Plugin database folders (round 170b): FL's plugins under FL's folder, LYNX's own first
    using namespace lynx::plugincatalog;
    const auto folders = withDefaultCategories ({}, false,
                                                { { "lynx:fx:Fruity Limiter", "Fruity Limiter", true, true },
                                                  { "lynx:fx:Fruity Delay 3", "Fruity Delay 3", true, true },
                                                  { "lynx:fx:Facet EQ", "Facet EQ", false, false } });
    CHECK (folders.of.at ("lynx:fx:Fruity Limiter") == "Dynamics");
    CHECK (folders.of.at ("lynx:fx:Fruity Delay 3") == "Delay reverb");
    CHECK (folders.of.at ("lynx:fx:Facet EQ") == lynxCategory);
}

TEST (picker171c_search_like_fl)
{
    using namespace lynx::pickerview;
    // FL (REA_rodada171c §2): the text anywhere in the name, ignoring case; the others are dimmed
    CHECK (nameMatches ("del", "Multiband Delay") && nameMatches ("DEL", "Fruity Delay 3") && ! nameMatches ("del", "Fruity Limiter"));
    CHECK (nameMatches ("", "anything"));
    // the pointed-at tile: the first name that starts with the text, else the last one that contains it (FL's "del" →
    // Multiband Delay when no name starts with "del")
    const std::vector<std::string> names { "Fruity Delay 2", "Fruity Delay 3", "Fruity Limiter", "Multiband Delay" };
    CHECK (searchJump ("del", names) == 3);
    CHECK (searchJump ("fruity", names) == 0);
    CHECK (searchJump ("LIM", names) == 2);
    CHECK (searchJump ("xyz", names) == -1 && searchJump ("", names) == -1);
    // Ctrl+Backspace: back to the previous space
    CHECK (dropLastWord ("fruity del") == "fruity ");
    CHECK (dropLastWord ("fruity del  ") == "fruity ");
    CHECK (dropLastWord ("fruity") == "" && dropLastWord ("") == "");
}

TEST (picker171c_up_down_wrap_and_skip_dimmed)
{
    using lynx::pickerview::stepWrapping;
    const std::vector<char> usable { 1, 0, 1, 1 };
    CHECK (stepWrapping (usable, -1, 1) == 0);  // nothing pointed at: the first
    CHECK (stepWrapping (usable, -1, -1) == 3); // or the last
    CHECK (stepWrapping (usable, 0, 1) == 2);   // a dimmed one is skipped
    CHECK (stepWrapping (usable, 3, 1) == 0);   // wraps around
    CHECK (stepWrapping (usable, 0, -1) == 3);
    CHECK (stepWrapping (usable, 0, 3) == 0);   // three Ctrl+wheel notches
    CHECK (stepWrapping ({ 0, 0 }, 0, 1) == -1);
    CHECK (stepWrapping ({}, 2, 1) == 2);
}
