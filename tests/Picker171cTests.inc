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

TEST (picker171c_search_every_word_any_field)
{
    using lynx::pickerview::matches;
    const std::vector<std::string> limiter { "Fruity Limiter", "built-in FL engine", "Dynamics", "LYNX SHARE" };
    CHECK (matches ("", limiter));
    CHECK (matches ("lim", limiter));
    CHECK (matches ("LIMITER dyn", limiter));          // two words, two fields, any case
    CHECK (matches ("  fruity   lim ", limiter));      // extra spaces
    CHECK (! matches ("limiter reverb", limiter));     // every word must be found
    CHECK (matches ("fabfilter", { "Pro-Q 3", "FabFilter · VST3", "Installed", "FabFilter" }));
}

TEST (picker171c_keyboard_moves_like_a_grid)
{
    using lynx::pickerview::Box;
    using lynx::pickerview::moveSelection;
    // 3 + 2 tiles in two rows; index 5 is not shown
    const std::vector<Box> boxes { { 0, 0, 100, 60 }, { 110, 0, 100, 60 }, { 220, 0, 100, 60 }, { 0, 70, 100, 60 }, { 110, 70, 100, 60 }, {} };
    CHECK (moveSelection (boxes, -1, 1, 0) == 0);  // nothing selected: the first
    CHECK (moveSelection (boxes, 0, 1, 0) == 1);
    CHECK (moveSelection (boxes, 2, 1, 0) == 3);   // reading order: the end of a row goes on to the next
    CHECK (moveSelection (boxes, 3, -1, 0) == 2);
    CHECK (moveSelection (boxes, 0, -1, 0) == 0);  // stops at the ends
    CHECK (moveSelection (boxes, 4, 1, 0) == 4);
    CHECK (moveSelection (boxes, 1, 0, 1) == 4);   // down: the nearest centre in the next row
    CHECK (moveSelection (boxes, 2, 0, 1) == 4);
    CHECK (moveSelection (boxes, 3, 0, -1) == 0);  // up
    CHECK (moveSelection (boxes, 0, 0, -1) == 0);  // no row above: stays
    CHECK (moveSelection ({}, 3, 1, 0) == 3);      // nothing shown: stays
}
