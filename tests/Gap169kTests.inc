// Round 169k: FL's Playlist picker multi-selection and the render of several picked patterns
// (docs/analise/REA_rodada169k.md §2.1–2.2).

#include "core/PickerSelection.h"
#include "core/QuickRender.h"

TEST (gap169k_picker_selection_like_fl)
{
    using namespace lynx::pickersel;
    Selection s;
    // plain click: only that one (PickerPanelMouseUp, no Ctrl / Shift)
    leftUp (s, 3, false, false, leftDownDeselects (s, 3, false));
    CHECK (s.items == std::vector<int> ({ 3 }));
    leftUp (s, 5, false, false, false);
    CHECK (s.items == std::vector<int> ({ 5 }));
    // Ctrl+click adds, Ctrl+click on a picked one takes it out
    leftUp (s, 1, true, false, leftDownDeselects (s, 1, true));
    CHECK (s.items == std::vector<int> ({ 5, 1 }));
    const auto d = leftDownDeselects (s, 5, true);
    CHECK (d);
    leftUp (s, 5, true, false, d);
    CHECK (s.items == std::vector<int> ({ 1 }));
    // Shift+click: the range from the first picked one to the clicked one, in that order
    leftUp (s, 4, false, true, false);
    CHECK (s.items == std::vector<int> ({ 1, 2, 3, 4 }));
    Selection back;
    back.add (6);
    leftUp (back, 3, false, true, false);
    CHECK (back.items == std::vector<int> ({ 6, 5, 4, 3 }));
    // Shift with nothing picked: just that one
    Selection none;
    leftUp (none, 2, false, true, false);
    CHECK (none.items == std::vector<int> ({ 2 }));
    // right-click on a picked one keeps the selection; on another one (no Ctrl) only that one; Ctrl adds
    rightDown (s, 2, false);
    CHECK (s.items == std::vector<int> ({ 1, 2, 3, 4 }));
    rightDown (s, 7, true);
    CHECK (s.items == std::vector<int> ({ 1, 2, 3, 4, 7 }));
    rightDown (s, 9, false);
    CHECK (s.items == std::vector<int> ({ 9 }));
    // deleted patterns drop out
    Selection c;
    c.items = { 0, 4, 2, 7 };
    clamp (c, 4);
    CHECK (c.items == std::vector<int> ({ 0, 2 }));
}

TEST (gap169k_render_plan_and_warnings)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.patterns.resize (4);
    for (auto& p : song.patterns)
    {
        p.notes.clear();
        p.events.clear();
    }
    song.patterns[0].notes.push_back ({});
    song.patterns[2].notes.push_back ({});
    const auto renderable = [&song] (int p) { return quickrender::patternRenderable (song, p); };
    // picked order is kept, empty ones skipped: "Empty patterns were ignored." (FUN_011884b0)
    auto plan = pickersel::planRender ({ 2, 1, 0, 3 }, renderable);
    CHECK (plan.render == std::vector<int> ({ 2, 0 }));
    CHECK ((plan.skipped) == (2));
    CHECK ((pickersel::skippedWarning (plan)) == (2));
    // one picked and empty: "Pattern is empty, nothing to render."
    plan = pickersel::planRender ({ 1 }, renderable);
    CHECK (plan.render.empty());
    CHECK ((pickersel::skippedWarning (plan)) == (1));
    // all full: no warning
    plan = pickersel::planRender ({ 0, 2 }, renderable);
    CHECK ((pickersel::skippedWarning (plan)) == (0));
    // two renders applied in one go: two new audio clip channels, each after its pattern
    const auto channels = song.channels.size();
    quickrender::applyPatternRender (song, 2, "C:/r/Pattern 3.wav", false);
    quickrender::applyPatternRender (song, 0, "C:/r/Pattern 1.wav", false);
    CHECK ((song.channels.size()) == (channels + 2));
    CHECK (song.channels[channels].instrument == InstrumentKind::audioClip);
    CHECK (song.channels[channels + 1].instrument == InstrumentKind::audioClip);
}
