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

TEST (picker171c_one_cell_box_for_every_tile)
{
    using namespace lynx::pickerview;
    // both halves fit, at the largest cell; the half with more plugins gets more columns
    const auto g = split (50, 110, 1800, 800, 10, 40, 1.6, 72, 300);
    CHECK (g.fits && g.cellW >= 72 && g.cellW <= 300 && std::abs (g.cellW - (int) (g.cellH * 1.6 + 0.5)) <= 1);
    CHECK (g.rightCols > g.leftCols);
    CHECK (gridHeight (50, g.leftCols, g.cellH, 10) <= 800 && gridHeight (110, g.rightCols, g.cellH, 10) <= 800);
    CHECK (gridWidth (g.leftCols, g.cellW, 10) + gridWidth (g.rightCols, g.cellW, 10) + 40 <= 1800);
    // one more pixel of cell would not fit (the largest)
    const auto bigger = split (50, 110, 1800, 800, 10, 40, 1.6, g.cellW + 1, g.cellW + 1);
    CHECK (! bigger.fits || bigger.cellW == g.cellW);
    // the grid fills most of the room: no big empty zone
    const auto usedW = gridWidth (g.leftCols, g.cellW, 10) + gridWidth (g.rightCols, g.cellW, 10) + 40;
    CHECK (usedW * 10 >= 1800 * 8);
    // a few plugins: capped at maxW; too many: the smallest cells, scrolled
    const auto few = split (3, 0, 1800, 800, 10, 0, 1.6, 72, 300);
    CHECK (few.fits && few.cellW == 300 && few.leftCols == 3 && few.rightCols == 0);
    const auto many = split (2000, 0, 600, 300, 10, 0, 1.6, 72, 300);
    CHECK (! many.fits && many.cellW == 72);
    // cells: rows left to right, the last row centred
    const auto cells = gridCells (5, 3, 100, 60, 10);
    CHECK (cells.size() == 5 && cells[2].x == 220 && cells[3].y == 70 && cells[3].x == 55 && cells[4].x == 165);
    CHECK (gridCells (3, 0, 100, 60, 10).empty());
}

TEST (picker171c_picture_fitted_inside_its_cell)
{
    using namespace lynx::pickerview;
    // a tall, narrow editor is letterboxed (never stretched, never bigger than the cell)
    const auto tall = fitInside ({ 0, 0, 160, 100 }, 206, 700);
    CHECK (tall.h == 100 && tall.w < 40 && tall.x > 60 && tall.y == 0);
    const auto wide = fitInside ({ 10, 20, 160, 100 }, 900, 100);
    CHECK (wide.w == 160 && wide.h < 20 && wide.x == 10 && wide.y > 60);
    const auto same = fitInside ({ 0, 0, 160, 100 }, 320, 200);
    CHECK (same.w == 160 && same.h == 100);
}
