// Round 169k: FL's Plugin picker view (F8; docs/analise/REA_rodada169k.md §2.5).

#include "core/PickerView.h"

TEST (gap169k_picker_tabs_and_wheel_like_fl)
{
    using namespace lynx::pickerview;
    Tabs tabs;
    tabs.generators = { "Synth", "Sampler", "Drum" };
    tabs.effects = { "Delay & reverb", "Distortion", "Dynamics", "EQ & filter" };
    CHECK (tabs.count() == 8);
    CHECK (tabs.all() == 3 && tabs.name (3) == "All");
    CHECK (tabs.isGenerator (0) && tabs.isGenerator (2) && ! tabs.isGenerator (3));
    CHECK (tabs.isEffect (4) && tabs.isEffect (7) && ! tabs.isEffect (8));
    CHECK (tabs.name (5) == "Distortion");
    // FL: 3 notches down from All → the third effect category; it stops at both ends
    CHECK (stepTab (tabs, tabs.all(), 3) == 6);
    CHECK (stepTab (tabs, tabs.all(), 30) == 7);
    CHECK (stepTab (tabs, 1, -5) == 0);
    // F8 is the picker, the quick-add palette has no default key any more
    lynx::keys::KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ lynx::keys::f8 }, lynx::keys::Context::global, false) == "view.pluginPicker");
}

TEST (gap169k_picker_tiles_fit)
{
    using namespace lynx::pickerview;
    // few tiles: the biggest size
    auto f = fitTiles (4, 900, 600, 10, 70, 190, 0.55);
    CHECK (f.tileWidth == 190 && f.columns == 4 && f.rows == 1);
    // many tiles: smaller until they fit the height
    f = fitTiles (120, 900, 800, 10, 70, 190, 0.55);
    CHECK (f.tileWidth < 190 && f.tileWidth >= 70);
    CHECK (f.rows * f.tileHeight + (f.rows - 1) * 10 <= 800);
    CHECK (f.columns * f.tileWidth + (f.columns - 1) * 10 <= 900);
    // nothing to show
    CHECK (fitTiles (0, 900, 600, 10, 70, 190, 0.55).tileWidth == 0);
}
