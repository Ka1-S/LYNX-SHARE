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

TEST (picker171c_tiles_share_one_scale_like_fl)
{
    using namespace lynx::pickerview;
    // a big editor is a big tile: every picture at the same scale, the largest that fits
    const std::vector<Box> sizes { { 0, 0, 1000, 700 }, { 0, 0, 500, 350 }, { 0, 0, 400, 200 }, { 0, 0, 300, 600 } };
    const auto p = packRows (sizes, 600, 400, 10, 0.05, 0.45);
    CHECK (p.boxes.size() == 4 && p.height <= 400 && p.scale > 0.05);
    CHECK (p.boxes.size() == 4 && std::abs (p.boxes[0].w - 2 * p.boxes[1].w) <= 1); // the same scale for all
    for (const auto& b : p.boxes)
        CHECK (b.x >= 0 && b.x + b.w <= 600);
    // the largest scale: a bigger area never makes the tiles smaller, and maxScale caps them
    const auto roomy = packRows (sizes, 2000, 2000, 10, 0.05, 0.45);
    CHECK (roomy.scale == 0.45 && roomy.rowSlack.size() == 1);
    // rows: tiles centred in their row's height, and a row's slack for centring
    const auto rows = packAt ({ { 0, 0, 100, 100 }, { 0, 0, 100, 50 }, { 0, 0, 100, 100 } }, 220, 10, 1.0);
    CHECK (rows.rowOf.size() == 3 && rows.rowOf[0] == 0 && rows.rowOf[1] == 0 && rows.rowOf[2] == 1);
    CHECK (rows.boxes.size() == 3 && rows.boxes[1].y == 25 && rows.boxes[2].y == 110 && rows.boxes[2].x == 0);
    CHECK (rows.rowSlack.size() == 2 && rows.rowSlack[0] == 10 && rows.rowSlack[1] == 120);
    CHECK (rows.height == 210);
    // too many: the smallest scale (the view scrolls)
    std::vector<Box> many (400, Box { 0, 0, 500, 300 });
    const auto tight = packRows (many, 600, 300, 10, 0.06, 0.45);
    CHECK (tight.scale == 0.06 && tight.height > 300);
    CHECK (packRows ({}, 600, 300, 10, 0.06, 0.45).boxes.empty());
}
