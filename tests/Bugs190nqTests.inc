// Round 190nq: bug fixes for issues #229 (a narrow Channel rack's title tools), #193 (video lanes in the Playlist's
// cached picture), #226 (automation of a deleted mixer track), and the follow-ups of #218 (Enter in the Browser) and
// #222 (the big track icon beside the insert effects column).

#include "core/MixerOps.h"
#include "core/TrackIconScale.h"
#include "core/Workspace.h"

TEST (bugs190nq_229_narrow_title_bar_drops_the_caption_then_hides_tools)
{
    using namespace lynx::workspace;
    // wide: caption and every tool
    CHECK (titleCaptionFits (130, 460, 700));
    CHECK (titleToolsRoom (130, 460, 700) == 460);
    // narrower: the caption goes first (FL's Channel rack), the tools keep their full width
    CHECK (! titleCaptionFits (130, 460, 520));
    CHECK (titleToolsRoom (32, 460, 520) == 460);
    // narrower still: the tools get only the room before the buttons, never more
    CHECK (titleToolsRoom (32, 460, 400) == 368);
    CHECK (titleToolsRoom (32, 460, 20) == 0);
    // a panel without title tools always shows its caption
    CHECK (titleCaptionFits (900, 0, 300));
}

TEST (bugs190nq_226_automation_of_a_deleted_insert_says_so)
{
    using namespace lynx;
    Song song;
    song.mixer.resize (6);
    for (int t = 1; t < 6; ++t)
        song.mixer[(std::size_t) t].name = "Insert " + std::to_string (t);
    song.mixer[1].name = "BEAT BUS";
    song.mixer[2].name = "VERB";
    song.mixer[1].sends.push_back ({ 2, 0.5f });
    Automation send;
    send.name = "BEAT BUS \xe2\x86\x92 VERB - Send level";
    send.target = { AutomationTargetKind::mixerSend, 1, 2, 0 };
    send.points = { { 0, 0.2f }, { 960, 0.8f } };
    Automation volume;
    volume.name = "Insert 4 - Volume";
    volume.target = { AutomationTargetKind::mixerVolume, 4, 0, 0 };
    song.automations = { send, volume };

    CHECK (mixerops::deleteTrack (song, 2)); // the reverb track goes
    CHECK (song.automations[0].target.kind == AutomationTargetKind::none);
    CHECK (song.automations[0].name == "BEAT BUS \xe2\x86\x92 VERB - Send level (target deleted)");
    CHECK (song.automations[0].points.size() == 2); // (its points stay: undo or a new target brings it back)
    // the other clip follows its insert (4 -> 3) and keeps its name
    CHECK (song.automations[1].target.kind == AutomationTargetKind::mixerVolume && song.automations[1].target.index == 3);
    CHECK (song.automations[1].name == "Insert 4 - Volume");
    // deleting another track later doesn't add the mark twice
    CHECK (mixerops::deleteTrack (song, 1));
    CHECK (song.automations[0].name == "BEAT BUS \xe2\x86\x92 VERB - Send level (target deleted)");
    CHECK (mixerops::targetDeletedName ("A (target deleted)") == "A (target deleted)");
}

TEST (bugs190nq_222_big_icon_centred_beside_the_insert_effects_column)
{
    using namespace lynx::trackiconscale;
    // no column: the whole plate
    CHECK ((bigIconArea (4.0f, 200.0f, 0.0f) == std::pair<float, float> { 4.0f, 200.0f }));
    // with the LYNX extra's insert effects column (104 px at the plate's right): the part left of it
    const auto [x, w] = bigIconArea (4.0f, 300.0f, 104.0f);
    CHECK (x == 4.0f && w == 196.0f);
    CHECK (x + w * 0.5f == 102.0f); // the icon's centre: the middle of the name / controls part
    CHECK (bigIconArea (4.0f, 50.0f, 104.0f).second == 0.0f);
    CHECK (bigIconSize (112, (int) w) == 56);
}
