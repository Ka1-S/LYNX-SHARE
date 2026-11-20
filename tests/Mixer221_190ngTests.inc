// Round 190ng (issue #221): "Show only used inserts" after Del (core/MixerOps.h revealedAfterDelete / selectionAfterDelete).

#include "core/MixerOps.h"

TEST (mixer221_190ng_kept_inserts_follow_the_delete)
{
    using namespace lynx::mixerops;
    // the deleted number goes, the later ones move left as their inserts did (FL keeps the count)
    CHECK ((revealedAfterDelete ({ 3, 19 }, { 19 }) == std::set<int> { 3 }));
    CHECK ((revealedAfterDelete ({ 3, 19, 20 }, { 19 }) == std::set<int> { 3, 19 }));
    CHECK ((revealedAfterDelete ({ 2, 5, 7, 9 }, { 5, 3 }) == std::set<int> { 2, 5, 7 }));
    CHECK ((revealedAfterDelete ({ 4 }, {}) == std::set<int> { 4 }));
}

TEST (mixer221_190ng_selection_after_delete)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (20);
    song.channels.clear();
    for (const auto t : { 8, 18 })
    {
        ChannelDef c;
        c.mixerTrack = t;
        song.channels.push_back (c);
    }
    CHECK (song.insertInUse (18) && ! song.insertInUse (19));
    // FL (every insert shown): the same number
    CHECK (mixerops::selectionAfterDelete (song, {}, 19, false) == 19);
    // only the used ones shown: the blank under 19 is not shown, the nearest shown one before it is 18
    CHECK (mixerops::selectionAfterDelete (song, {}, 19, true) == 18);
    // kept (brought back with "+") under that number: it shows, so it is the one selected
    CHECK (mixerops::selectionAfterDelete (song, { 19 }, 19, true) == 19);
    // nothing shown before it: the Master
    CHECK (mixerops::selectionAfterDelete (song, {}, 5, true) == 0);
    CHECK (mixerops::selectionAfterDelete (song, {}, 99, false) == 20);
}
