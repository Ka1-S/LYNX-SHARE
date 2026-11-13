// Round 190ft (issues #163 / #164): FL Studio's Playlist track-header rules, core/TrackHeaderGestures.h, and FL's row
// size law (core/EditorPolish.h) with its .flp field. Evidence: docs/analise/REA_rodada190ft.md.

#include "core/EditorPolish.h"
#include "core/FlpExport.h"
#include "core/TrackHeaderGestures.h"

TEST (issues190ft_header_selection_follows_fl)
{
    using namespace lynx::trackhdr;
    // FUN_012e83e0's modes
    std::vector<int> sel { 2, 5 };
    applySelection (sel, 3, SelMode::only);
    CHECK ((sel == std::vector<int> { 3 }));
    applySelection (sel, 1, SelMode::toggle);
    applySelection (sel, 7, SelMode::toggle);
    CHECK ((sel == std::vector<int> { 1, 3, 7 }));
    applySelection (sel, 3, SelMode::toggle);
    CHECK ((sel == std::vector<int> { 1, 7 }));
    applySelection (sel, 7, SelMode::add);
    applySelection (sel, 4, SelMode::add);
    CHECK ((sel == std::vector<int> { 1, 4, 7 }));
    applySelection (sel, 4, SelMode::remove);
    applySelection (sel, 9, SelMode::remove);
    CHECK ((sel == std::vector<int> { 1, 7 }));

    // what a press does: click = alone; Ctrl = alone with its clips and a drag adds; Ctrl+Shift = add / remove (the drag
    // follows the result); Alt or Ctrl+Alt = alone; Shift = LYNX's range
    const auto plain = planPress (false, false, false);
    CHECK (plain.mode == SelMode::only && plain.clips == ClipPick::none && dragAfter (plain, true) == RangeDrag::none && ! plain.lynxShiftRange);
    const auto c = planPress (true, false, false);
    CHECK (c.mode == SelMode::only && c.clips == ClipPick::replace && dragAfter (c, true) == RangeDrag::select);
    const auto cs = planPress (true, true, false);
    CHECK (cs.mode == SelMode::toggle && cs.clips == ClipPick::add);
    CHECK (dragAfter (cs, true) == RangeDrag::select && dragAfter (cs, false) == RangeDrag::deselect);
    CHECK (planPress (false, false, true).mode == SelMode::only && planPress (true, false, true).clips == ClipPick::none);
    CHECK (planPress (false, true, false).lynxShiftRange);

    // the drag across the headers (FUN_012e8740 on every move): the range only grows
    std::vector<int> r { 0 };
    applyRange (r, 0, 3, true);
    CHECK ((r == std::vector<int> { 0, 1, 2, 3 }));
    applyRange (r, 0, 1, true); // (moving back leaves 2 and 3 selected)
    CHECK ((r == std::vector<int> { 0, 1, 2, 3 }));
    applyRange (r, 3, 2, false);
    CHECK ((r == std::vector<int> { 0, 1 }));
}

TEST (issues190ft_resize_band_and_presses)
{
    using namespace lynx::trackhdr;
    // a quarter of the row, 4 … 14 px, at its bottom; none on rows of 6 px or less
    CHECK (resizeBand (38) == 9 && resizeBand (8) == 4 && resizeBand (200) == 14);
    CHECK (inResizeBand (38, 37) && inResizeBand (38, 29) && ! inResizeBand (38, 28) && ! inResizeBand (38, 38));
    CHECK (! inResizeBand (6, 5) && inResizeBand (16, 15) && ! inResizeBand (16, 11));
    // presses on the band
    CHECK (bandPress (false, false, false) == BandPress::resize);
    CHECK (bandPress (false, false, true) == BandPress::reset);
    CHECK (bandPress (false, true, false) == BandPress::toggleLock);
    CHECK (bandPress (true, false, false) == BandPress::toggleLock && bandPress (true, false, true) == BandPress::toggleLock);
    // FL's quick lock flip negates the locked size: never locked (−1) → 1 = the smallest row, and back
    CHECK (flippedLock (-1) == 1 && flippedLock (1) == -1 && flippedLock (60) == -60 && flippedLock (-60) == 60 && flippedLock (0) == 1);
}

TEST (issues190ft_resize_heights)
{
    using namespace lynx;
    using namespace lynx::trackhdr;
    // FL's row limits (FUN_00ea0b30): 16 … 528 px, even
    CHECK (polish::minTrackHeight == 16 && polish::maxTrackHeight == 528);
    CHECK (draggedHeight (38, 40) == 78 && draggedHeight (38, 41) == 78 && draggedHeight (38, -100) == 16 && draggedHeight (38, 5000) == 528);
    // the dragged track is one of the selected ones: the distance from the first one's top shared down to it
    const std::vector<int> sel { 1, 2, 3 };
    CHECK (selectedHeight (sel, 2, 100, 200) == 50);
    CHECK (selectedHeight (sel, 3, 100, 250) == 50);
    CHECK (selectedHeight (sel, 1, 100, 160) == 60);
    CHECK (selectedHeight (sel, 2, 100, 132) == 0); // 16 px each: not above the smallest row, sizes stay
    CHECK (selectedHeight (sel, 9, 100, 150) == 50); // (a track outside the list counts as the first)
    // past the top of a row at its smallest
    CHECK (groupGesture (16, 68, 100) && ! groupGesture (16, 69, 100) && ! groupGesture (40, 0, 100));
}

TEST (issues190ft_fl_size_factor_law_and_flp)
{
    using namespace lynx;
    // a row = 16 px + size × (default − 16): FL's Size menu on LYNX's 38 px default
    CHECK (polish::heightForFlSize (1.0) == 38 && polish::heightForFlSize (0.0) == 16 && polish::heightForFlSize (2.0) == 60);
    CHECK (polish::heightForFlSize (1.5) == 48 && polish::heightForFlSize (0.5) == 26 && polish::heightForFlSize (1.0 / 3.0) == 22);
    CHECK (polish::heightForFlSize (100.0) == 528 && polish::heightForFlSize (-1.0) == 38);
    CHECK (std::abs (polish::flSizeForHeight (60) - 2.0f) < 1.0e-6f && std::abs (polish::flSizeForHeight (16)) < 1.0e-6f
           && std::abs (polish::flSizeForHeight (0) - 1.0f) < 1.0e-6f);
    for (int h = 16; h <= 528; h += 2)
        CHECK (polish::heightForFlSize (polish::flSizeForHeight (h)) == h);

    // .flp: the f32 size at byte 13 of the 0xEE track event is written and read back with the same law
    auto song = Song::makeDefault();
    song.playlistTracks.resize (4);
    song.playlistTracks[1].height = 60;  // FL 200 %
    song.playlistTracks[2].height = 300;
    song.playlistTracks[3].lockedHeight = 80;
    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    float sizes[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
    int locked3 = 0;
    for (const auto& a : project.arrangements)
        for (const auto& t : a.tracks)
            if (t.index >= 0 && t.index < 4)
            {
                sizes[t.index] = t.heightScale;
                if (t.index == 3)
                    locked3 = t.lockedHeight;
            }
    CHECK (std::abs (sizes[0] - 1.0f) < 1.0e-6f && std::abs (sizes[1] - 2.0f) < 1.0e-6f
           && std::abs (sizes[2] - polish::flSizeForHeight (300)) < 1.0e-6f && locked3 == 80);
    const auto back = importFlp (project);
    CHECK (back.playlistTracks.size() >= 4);
    if (back.playlistTracks.size() >= 4)
        CHECK (back.playlistTracks[0].height == 0 && back.playlistTracks[1].height == 60 && back.playlistTracks[2].height == 300
               && back.playlistTracks[3].lockedHeight == 80);
}
