// --- Round 190jv: FL's Mixer menu › "Trim unused tracks" (REA FUN_012a6920 / FUN_012a65c0 / FUN_0127bcc0 / FUN_012a5280,
// docs/analise/REA_rodada190jv.md) ----------------------------------------------------------------------------------

#include "core/MixerOps.h"

namespace trim190jv
{
// Master + 10 inserts:
//  1 a channel plays into it            (linked)
//  2 fader, pan, mute, colour, input    (FL ignores them: unlinked, empty)
//  3 named "Drums bus"                  (unlinked, non-empty)
//  4 a Fruity Send to 6                 (unlinked, non-empty; 6 is side-routed: linked in LYNX)
//  5 routes to 6                        (linked, and 6 too)
//  7 a compressor on 8 side-chains from 7 (7 linked in LYNX; 8 unlinked, non-empty)
//  9 a Playlist audio track             (linked)
// 10 default, automated volume          (FL ignores automation: unlinked, empty)
inline lynx::Song song()
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.mixer = Song::defaultMixer (10);
    s.channels.resize (2);
    s.channels[0].mixerTrack = 1;
    s.channels[1].mixerTrack = 0;
    auto& two = s.mixer[2];
    two.volume = 0.3f;
    two.pan = 0.5f;
    two.muted = true;
    two.colour = 0xff336699;
    two.inputChannel = 0;
    s.mixer[3].name = "Drums bus";
    EffectSlot send;
    send.kind = EffectKind::fruitySend;
    send.params[3] = 6.0f / (float) (maxMixerTracks - 1);
    s.mixer[4].effects.push_back (send);
    s.mixer[5].sends = { { 6, 0.8f } };
    EffectSlot comp;
    comp.kind = EffectKind::compressor;
    comp.sidechain = 7;
    s.mixer[8].effects.push_back (comp);
    s.playlistTracks.resize (2);
    s.playlistTracks[0].mode = PlaylistTrackMode::audio;
    s.playlistTracks[0].mixerTrack = 9;
    s.automations.clear();
    Automation on9, on10;
    on9.target = { AutomationTargetKind::mixerVolume, 9, 0, 0 };
    on10.target = { AutomationTargetKind::mixerVolume, 10, 0, 0 };
    s.automations.push_back (on9);
    s.automations.push_back (on10);
    return s;
}
} // namespace trim190jv

TEST (trim190jv_which_tracks_count_as_unused)
{
    using namespace lynx;
    using mixerops::TrimUnused;
    const auto s = trim190jv::song();
    CHECK (mixerops::insertLinked (s, 0)); // the Master never goes
    for (int t : { 1, 5, 6, 7, 9 })
        CHECK (mixerops::insertLinked (s, t));
    for (int t : { 2, 3, 4, 8, 10 })
        CHECK (! mixerops::insertLinked (s, t));
    CHECK (mixerops::insertNonEmpty (s, 3) && mixerops::insertNonEmpty (s, 4) && mixerops::insertNonEmpty (s, 8));
    CHECK (! mixerops::insertNonEmpty (s, 2) && ! mixerops::insertNonEmpty (s, 10)); // (a moved fader / colour is not "non-empty")
    CHECK ((mixerops::unusedTracks (s, TrimUnused::unlinkedOnly) == std::vector<int> { 2, 10 }));
    CHECK ((mixerops::unusedTracks (s, TrimUnused::unlinkedAndNonEmpty) == std::vector<int> { 2, 3, 4, 8, 10 }));

    // a level-0 (sidechain) route links both ends; the route to the Master does not count
    auto r = s;
    r.mixer[10].sends = { { 2, 0.0f } };
    CHECK (mixerops::insertLinked (r, 10) && mixerops::insertLinked (r, 2));
    r.mixer[10].sends.clear();
    r.mixer[10].toMaster = false;
    CHECK (! mixerops::insertLinked (r, 10));
}

TEST (trim190jv_unlinked_only_removes_and_renumbers_every_reference)
{
    using namespace lynx;
    auto s = trim190jv::song();
    CHECK (mixerops::trimUnusedTracks (s, mixerops::TrimUnused::unlinkedOnly) == 2);
    // 2 and 10 went: the mixer is shorter (unlike Delete, which keeps the count); 3…9 moved down by one
    CHECK (s.mixer.size() == 9);
    CHECK (s.mixer[0].name == "Master");
    CHECK (s.mixer[2].name == "Drums bus");
    CHECK (s.mixer[4].name == "Insert 4" && s.mixer[8].name == "Insert 8"); // (default names follow the number)
    CHECK (s.channels[0].mixerTrack == 1 && s.channels[1].mixerTrack == 0);
    CHECK (fruitySendTarget (s.mixer[3].effects.at (0)) == 5); // the Fruity Send follows insert 6 → 5
    CHECK (s.mixer[4].sends.size() == 1 && s.mixer[4].sends[0].target == 5);
    CHECK (s.mixer[7].effects.at (0).sidechain == 6);
    CHECK (s.playlistTracks[0].mixerTrack == 8 && s.playlistTracks[0].mode == PlaylistTrackMode::audio);
    CHECK (s.automations[0].target.kind == AutomationTargetKind::mixerVolume && s.automations[0].target.index == 8);
    CHECK (s.automations[1].target.kind == AutomationTargetKind::none); // its insert went: no target (FL: -1)
    // nothing left to trim in that mode
    CHECK (mixerops::unusedTracks (s, mixerops::TrimUnused::unlinkedOnly).empty());
    CHECK (mixerops::trimUnusedTracks (s, mixerops::TrimUnused::unlinkedOnly) == 0 && s.mixer.size() == 9);
}

TEST (trim190jv_unlinked_and_non_empty_also_removes_effects_and_names)
{
    using namespace lynx;
    auto s = trim190jv::song();
    CHECK (mixerops::trimUnusedTracks (s, mixerops::TrimUnused::unlinkedAndNonEmpty) == 5);
    // left: Master, 1, 5, 6, 7, 9 → 0…5
    CHECK (s.mixer.size() == 6);
    CHECK (s.mixer[2].sends.size() == 1 && s.mixer[2].sends[0].target == 3);
    CHECK (s.playlistTracks[0].mixerTrack == 5);
    CHECK (s.automations[0].target.index == 5);
    for (std::size_t t = 1; t < s.mixer.size(); ++t)
        CHECK (s.mixer[t].effects.empty() && s.mixer[t].name == "Insert " + std::to_string (t));
}

TEST (trim190jv_one_edit_master_kept_and_empty_project)
{
    using namespace lynx;
    // the menu runs trimUnusedTracks inside one editSongStructural (one undo step): the result is the same as removing
    // the listed tracks at once, and the state before is untouched by the listing
    const auto before = trim190jv::song();
    auto a = before, b = before;
    const auto list = mixerops::unusedTracks (before, mixerops::TrimUnused::unlinkedAndNonEmpty);
    CHECK (mixerops::trimTracks (a, list) == (int) list.size());
    CHECK (mixerops::trimUnusedTracks (b, mixerops::TrimUnused::unlinkedAndNonEmpty) == (int) list.size());
    CHECK (a.mixer.size() == b.mixer.size());
    for (std::size_t t = 0; t < a.mixer.size(); ++t)
        CHECK (a.mixer[t].name == b.mixer[t].name && a.mixer[t].sends.size() == b.mixer[t].sends.size());
    CHECK (before.mixer.size() == 11);
    // the Master, duplicates and out-of-range numbers are never removed
    auto c = before;
    CHECK (mixerops::trimTracks (c, { 0, 10, 10, 99, -1 }) == 1 && c.mixer.size() == 10 && c.mixer[0].name == "Master");

    // nothing linked at all: everything but the Master goes
    auto empty = Song::makeDefault();
    empty.mixer = Song::defaultMixer (5);
    empty.channels.clear();
    empty.playlistTracks.clear();
    CHECK (mixerops::trimUnusedTracks (empty, mixerops::TrimUnused::unlinkedOnly) == 5 && empty.mixer.size() == 1);
    CHECK (mixerops::trimUnusedTracks (empty, mixerops::TrimUnused::unlinkedAndNonEmpty) == 0);
}
