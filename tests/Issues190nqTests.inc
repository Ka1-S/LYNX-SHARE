// Round 190nq: issues #225 (Mixer Left / Right select), #227 (ruler click with every tool, LYNX extra), #228 (the action
// search, LYNX extra), #230 (FL's "Show fade previews").

#include "core/ClipOps.h"
#include "core/KeyMap.h"
#include "core/LynxExtras.h"
#include "core/MixerOps.h"
#include "core/RulerGestures.h"

TEST (issues190nq_225_mixer_left_right_select_the_neighbour)
{
    using namespace lynx;
    auto noneHidden = [] (int) { return false; };
    CHECK (mixerops::neighbourTrack (10, 3, 1, noneHidden) == 4);
    CHECK (mixerops::neighbourTrack (10, 3, -1, noneHidden) == 2);
    CHECK (mixerops::neighbourTrack (10, 0, -1, noneHidden) == 0);  // Master: stays
    CHECK (mixerops::neighbourTrack (10, 9, 1, noneHidden) == 9);   // the last: stays
    CHECK (mixerops::neighbourTrack (10, 3, -2, noneHidden) == 1);
    // "Show only used inserts": hidden ones are skipped; none shown that way: stays
    auto hidden = [] (int t) { return t == 4 || t == 5 || t == 9; };
    CHECK (mixerops::neighbourTrack (10, 3, 1, hidden) == 6);
    CHECK (mixerops::neighbourTrack (10, 6, -1, hidden) == 3);
    CHECK (mixerops::neighbourTrack (10, 8, 1, hidden) == 8);

    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    // FL 2026: Left / Right, also with Ctrl or Shift, select; Alt moves (unchanged)
    for (const auto mods : { (std::uint8_t) keys::none, (std::uint8_t) keys::ctrl, (std::uint8_t) keys::shift })
    {
        CHECK (map.find (keys::Chord { keys::left, mods }, Context::mixer, false) == "mixer.selectPrevious");
        CHECK (map.find (keys::Chord { keys::right, mods }, Context::mixer, false) == "mixer.selectNext");
    }
    CHECK (map.find (keys::Chord { keys::left, keys::alt }, Context::mixer, false) == "mixer.moveLeft");
    CHECK (map.find (keys::Chord { keys::right, keys::alt }, Context::mixer, false) == "mixer.moveRight");
    // the other panels keep their arrows
    CHECK (map.find (keys::Chord { keys::left, keys::ctrl }, Context::playlist, false) == "playlist.timePrevious");
    CHECK (map.find (keys::Chord { keys::left, keys::none }, Context::browser, false) == "browser.closeFolder");
}
