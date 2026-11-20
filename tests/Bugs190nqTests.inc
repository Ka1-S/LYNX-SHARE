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
