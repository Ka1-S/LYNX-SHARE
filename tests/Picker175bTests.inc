// Round 175b: the Plugin picker packs its tiles like FL (core/PickerView.h, docs/analise/REA_rodada175b.md). The
// expected positions are measured on FL Studio 2026's own picker (shots in E:\lynx-work\picker175b\fl): FL's bitmaps
// (Plugin database, 192 dpi) shown at half size + a 3 px shadow around them.

#include "core/PickerView.h"

namespace
{
std::vector<lynx::pickerview::PackTile> flTiles (const std::vector<std::pair<int, int>>& pngs, bool generators)
{
    std::vector<lynx::pickerview::PackTile> out;
    for (const auto& [w, h] : pngs)
        out.push_back ({ lynx::pickerview::roundFl (w / 2.0) + 6, lynx::pickerview::roundFl (h / 2.0) + 6, true, generators });
    return out;
}
} // namespace

TEST (picker175b_one_row_centred_with_32px_gaps_like_fl)
{
    using namespace lynx::pickerview;
    // FL's Dynamics tab at 1920 × 1032 (work area): one row, gaps capped at 32, centred; the pictures' left edges as FL
    const auto tiles = flTiles ({ { 257, 127 }, { 256, 59 }, { 257, 54 }, { 209, 100 }, { 258, 50 }, { 133, 64 }, { 208, 128 }, { 70, 98 }, { 182, 106 } }, false);
    const auto r = packRows (tiles, { 32, 56, 1888, 1032 - 96 });
    const int lefts[] = { 312, 488, 664, 840, 992, 1168, 1282, 1435, 1519 };
    CHECK (r.rows == 1);
    for (std::size_t i = 0; i < tiles.size(); ++i)
        CHECK (r.tiles[i].x + 4 == lefts[i]);
    CHECK (r.tiles[0].y + 4 == 464); // (the tallest picture's top: the row centred in the box)
}

TEST (picker175b_rows_spread_over_the_width_like_fl)
{
    using namespace lynx::pickerview;
    // FL's Synth classic tab at 1280 × 672: two rows, both spread over the width (the second one too)
    const auto tiles = flTiles ({ { 184, 128 }, { 230, 128 }, { 178, 113 }, { 255, 103 }, { 191, 128 }, { 241, 128 }, { 204, 44 }, { 194, 128 },
                                  { 210, 128 }, { 185, 128 }, { 187, 125 }, { 242, 128 }, { 196, 128 }, { 149, 128 } }, true);
    const auto r = packRows (tiles, { 32, 56, 1248, 672 - 96 });
    CHECK (r.rows == 2);
    const int lefts[] = { 36, 147, 282, 389, 536, 652, 791, 912, 1028, 1152, 36, 404, 798, 1170 };
    for (std::size_t i = 0; i < tiles.size(); ++i)
        CHECK (r.tiles[i].x + 4 == lefts[i]);
    CHECK (r.tiles[0].y + 4 == 240 && r.tiles[10].y + 4 == 329); // (16 px between the rows)
    // at 1920 the same 14 fit in one row, spread edge to edge
    const auto wide = packRows (tiles, { 32, 56, 1888, 936 });
    CHECK (wide.rows == 1 && wide.tiles[0].x == 32 && wide.tiles[13].x + wide.tiles[13].w == 1888);
}

TEST (picker175b_single_tile_rows_and_overflow)
{
    using namespace lynx::pickerview;
    // a lone tile: centred when it is the only row
    auto one = packRows ({ { 100, 50, true, false } }, { 0, 0, 1000, 600 });
    CHECK (one.tiles[0].x == 449 && one.tiles[0].y == 274);
    // a lone tile in a later row: an effect stays left, a generator goes right (FL)
    const std::vector<PackTile> fx { { 600, 50 }, { 600, 50 } };
    auto f = packRows (fx, { 0, 0, 1000, 600 });
    CHECK (f.rows == 2 && f.tiles[1].x == 0);
    auto gens = fx;
    for (auto& t : gens)
        t.generator = true;
    auto g = packRows (gens, { 0, 0, 1000, 600 });
    CHECK (g.tiles[1].x + g.tiles[1].w == 1000);
    // hidden tiles take no room
    auto hidden = packRows ({ { 100, 50, false }, { 100, 50 } }, { 0, 0, 1000, 600 });
    CHECK (! hidden.tiles[0].shown && hidden.tiles[1].x == 449);
    // too many rows for the box: they overlap (negative gap) and the block starts at the top, never scrolled
    std::vector<PackTile> many (40, PackTile { 400, 100 });
    auto m = packRows (many, { 0, 0, 1000, 600 });
    CHECK (m.rows == 20 && m.tiles[0].y == -7 && m.tiles[2].y < m.tiles[0].y + 102 && m.tiles[39].y + m.tiles[39].h <= 600 + 102);
}

TEST (picker175b_tabs_and_picture_budget)
{
    using namespace lynx::pickerview;
    // the labels' font: 19 when they fit, smaller down to 12 (FL at 1280 × 720: 13 for its 9 generator tabs, 12 for its 11 effect tabs)
    auto widthAt = [] (int total19) { return [total19] (int size) { return total19 * size / 19; }; };
    CHECK (tabFontSize (widthAt (514), 9, 960) == 19);
    CHECK (tabFontSize (widthAt (514), 9, 640) == 14);
    CHECK (tabFontSize (widthAt (536), 11, 640) == 12);
    CHECK (tabFontSize (widthAt (5000), 11, 640) == 12);
    // the strip: under the tiles, at most the form's bottom (FL: tab centre 998 at 1032 high)
    CHECK (tabCentreY (2000, 1032, 26, 1.0) == 1032 - 13 - 21);
    CHECK (tabCentreY (500, 1032, 26, 1.0) == 580 - 13 - 21);
    // a picture: half the editor, within 258 × 128
    CHECK (pictureSize (418, 200) == std::make_pair (209, 100));
    CHECK (pictureSize (900, 600) == std::make_pair (192, 128));
    CHECK (pictureSize (1200, 200) == std::make_pair (258, 43));
    CHECK (pictureSize (0, 10) == std::make_pair (0, 0));
}
