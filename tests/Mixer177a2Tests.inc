// --- Round 177a slice 2: folded mixer groups (LYNX's addition to FL's separator groups) --------------------------------

#include "core/MixerOps.h"

TEST (mixer177a2_groups_fold_to_their_bus_or_a_tab)
{
    using namespace lynx;
    using mixerops::FoldShow;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (10);

    // no separator anywhere: nothing can fold (the whole block is not a group to fold)
    CHECK (! mixerops::canFold (song, 2) && ! mixerops::toggleFold (song, 2));
    const auto plain = mixerops::foldView (song);
    CHECK (std::all_of (plain.begin(), plain.end(), [] (FoldShow f) { return f == FoldShow::strip; }));

    // drums 2-4 summed into 5 by "Create submix" (a group with its bus); 7-8 a plain group
    CHECK (mixerops::createSubmix (song, { 2, 3, 4 }, 5, "Drums") == 3);
    mixerops::createGroup (song, { 7, 8 }, 7);
    CHECK ((mixerops::groupOf (song, 3) == std::vector<int> { 2, 3, 4, 5 }));
    CHECK (mixerops::canFold (song, 3) && mixerops::canFold (song, 8));
    CHECK (mixerops::groupBus (song, mixerops::groupOf (song, 3)) == 5);
    CHECK (mixerops::groupBus (song, mixerops::groupOf (song, 7)) == -1);

    // folding from any member sets the flag on the group's first strip
    CHECK (mixerops::toggleFold (song, 4) && song.mixer[2].folded && mixerops::isFolded (song, 5));
    auto view = mixerops::foldView (song);
    CHECK (view[2] == FoldShow::hidden && view[3] == FoldShow::hidden && view[4] == FoldShow::hidden && view[5] == FoldShow::strip);
    CHECK (view[1] == FoldShow::strip && view[6] == FoldShow::strip && view[7] == FoldShow::strip);

    CHECK (mixerops::toggleFold (song, 8)); // a group without a bus: a tab on its first strip
    view = mixerops::foldView (song);
    CHECK (view[7] == FoldShow::tab && view[8] == FoldShow::hidden && view[9] == FoldShow::strip);

    // a member with a second output: no longer a summing group, the drums fold to a tab
    song.setRoute (3, 9, true);
    CHECK (mixerops::groupBus (song, mixerops::groupOf (song, 3)) == -1);
    view = mixerops::foldView (song);
    CHECK (view[2] == FoldShow::tab && view[3] == FoldShow::hidden && view[5] == FoldShow::hidden);

    // unfold all / fold all
    CHECK (mixerops::foldAll (song, false) == 2 && ! song.mixer[2].folded && ! song.mixer[7].folded);
    CHECK (mixerops::foldAll (song, true) == 3); // (9-10: the inserts after the 7-8 group's closing separator)
    CHECK (mixerops::foldAll (song, true) == 0);
    // the fold follows its group when inserts move (the flag is the first strip's)
    std::vector<int> selection { 2, 3, 4, 5 };
    CHECK (mixerops::moveTracks (song, selection, -1));
    CHECK (song.mixer[1].folded && mixerops::isFolded (song, 4));
}
