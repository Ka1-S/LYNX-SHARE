// --- Round 190hr (DV-2, Live's Detail View residue): the Start marker inside the loop (detail-016), the clip's own scale
// (detail-022), Transpose ±48 st (detail-072) and the clip editor's grid (detail-055). Core logic and .als round trips; the panels are checked by the app's shot route.

#include "core/LiveGrid.h"
#include "core/LiveScales.h"

namespace dv190hr
{
using namespace lynx;
using grid181a::bar;
using grid181a::closeTo;

// sessionSet's MIDI clips with their own scales: "Riff A" D (Name 0) in key, "Hit B" E (Name 1) in key
inline std::string scaledSet()
{
    auto s = als181c::sessionSet();
    const auto add = [&s] (const std::string& name, int root, int index, bool inKey) {
        const auto tag = "<Name Value=\"" + name + "\" />";
        const auto at = s.find (tag);
        CHECK (at != std::string::npos);
        if (at != std::string::npos)
            s.insert (at + tag.size(), "<ScaleInformation>" + als175a::v ("Root", std::to_string (root)) + als175a::v ("Name", std::to_string (index)) +
                                           "</ScaleInformation>" + als175a::v ("IsInKey", inKey ? "true" : "false"));
    };
    add ("Riff A", 2, 0, true);
    add ("Hit B", 4, 1, true);
    return s;
}
} // namespace dv190hr

TEST (dv190hr_start_marker_inside_the_loop_plays_from_it_then_loops)
{
    using namespace lynx;
    using namespace dv190hr;
    auto s = grid181a::patternSong();
    Note second;
    second.key = 70;
    second.start = 2 * ppq;
    second.length = ppq / 2;
    s.patterns[0].notes.push_back (second);
    const auto id = launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    CHECK (id >= 0);
    s.clipGrid->slots[(std::size_t) id].startMarker = 2 * ppq; // Live's StartRelative 2 beats into a 1-bar loop
    grid181a::Run run (s);
    run.launch (0, 0);
    run.until (bar * 2.0);
    const auto c = run.onsOf (60), d = run.onsOf (70);
    // the first pass starts at the Start marker (beat 3: key 70 at once), then the whole loop from its start
    CHECK (d.size() >= 2 && c.size() >= 1);
    if (d.size() >= 2 && c.size() >= 1)
    {
        CHECK (closeTo (d[0], 0.0));
        CHECK (closeTo (c[0], 2.0 * ppq));
        CHECK (closeTo (d[1], 4.0 * ppq));
    }
}

TEST (dv190hr_start_lead_is_only_for_looping_clips_and_stays_in_the_loop)
{
    using namespace lynx;
    ClipGridSlot slot;
    slot.startMarker = 3 * ppq;
    CHECK (launcher::startLead (slot, 4 * ppq) == 3.0 * ppq);
    CHECK (launcher::startLead (slot, 2 * ppq) == 2.0 * ppq - 1.0); // kept inside the loop
    slot.loop = false;
    CHECK (launcher::startLead (slot, 4 * ppq) == 0.0); // Loop off: Start is the clip's start
    CHECK (launcher::startLead (ClipGridSlot {}, 4 * ppq) == 0.0); // every clip saved before
}

TEST (dv190hr_start_marker_round_trips_through_live_session_view)
{
    using namespace lynx;
    auto set = als181c::sessionSet();
    const std::string zero = "<StartRelative Value=\"0.000000\" />";
    const auto at = set.find (zero);
    CHECK (at != std::string::npos);
    set.replace (at, zero.size(), "<StartRelative Value=\"1.5\" />");
    const auto song = importAls (als175a::bytesOf (set), als181c::options());
    CHECK (song.has_value());
    if (! song)
        return;
    const ClipGridSlot* a = nullptr;
    CHECK (als181c::clipNamed (*song, "Riff A", &a) != nullptr && a != nullptr && a->startMarker == 3 * ppq / 2);
    const auto out = exportAls (*song, AlsExportOptions {});
    CHECK (out.xml.find ("<StartRelative Value=\"1.5\"") != std::string::npos);
    const auto back = importAls (out.bytes, als181c::options());
    const ClipGridSlot* b = nullptr;
    CHECK (back.has_value() && als181c::clipNamed (*back, "Riff A", &b) != nullptr && b != nullptr && b->startMarker == 3 * ppq / 2);
}

TEST (dv190hr_clip_scale_round_trips_through_live_session_view)
{
    using namespace lynx;
    const auto song = importAls (als175a::bytesOf (dv190hr::scaledSet()), als181c::options());
    CHECK (song.has_value());
    if (! song)
        return;
    // the set has no scale of its own: the first in-key clip's (D, Name 0) is the project's key; Hit B keeps its own
    const auto project = projectkey::get (*song);
    CHECK (project.has_value() && project->root == 2);
    const ClipGridSlot* a = nullptr;
    const ClipGridSlot* b = nullptr;
    CHECK (als181c::clipNamed (*song, "Riff A", &a) != nullptr && a != nullptr && ! a->ownScale && a->scaleOn);
    CHECK (als181c::clipNamed (*song, "Hit B", &b) != nullptr && b != nullptr && b->ownScale && b->scaleOn && b->scaleRoot == 4);
    const auto expected = livescales::keyFor (4, 1);
    CHECK (expected.has_value());
    if (b != nullptr && expected)
    {
        CHECK (b->scaleIndex == expected->flScale);
        CHECK (projectkey::clipKey (*b, *song) == expected);
    }
    const auto out = exportAls (*song, AlsExportOptions {});
    const auto back = importAls (out.bytes, als181c::options());
    CHECK (back.has_value());
    if (! back)
        return;
    const ClipGridSlot* b2 = nullptr;
    CHECK (als181c::clipNamed (*back, "Hit B", &b2) != nullptr && b2 != nullptr && b2->ownScale && b2->scaleRoot == 4 && b2->scaleIndex == b->scaleIndex);
}

TEST (dv190hr_clip_scale_mode_off_gives_the_tools_no_key)
{
    using namespace lynx;
    Song s;
    projectkey::set (s, projectkey::Key { 0, projectkey::commonFirst().front() });
    ClipGridSlot slot;
    CHECK (projectkey::clipKey (slot, s) == projectkey::get (s)); // follows the project's key, as before
    slot.scaleOn = false;
    CHECK (! projectkey::clipKey (slot, s).has_value());
    slot.scaleOn = true;
    slot.ownScale = true;
    slot.scaleRoot = 9;
    slot.scaleIndex = (std::int16_t) projectkey::commonFirst().front();
    CHECK (projectkey::clipKey (slot, s).has_value() && projectkey::clipKey (slot, s)->root == 9);
}

TEST (dv190hr_transpose_reaches_48_semitones_in_and_out)
{
    using namespace lynx;
    auto set = als181c::sessionSet();
    const std::string mode = "<WarpMode Value=\"0\" />";
    const auto at = set.find (mode);
    CHECK (at != std::string::npos);
    set.replace (at, mode.size(), "<WarpMode Value=\"4\" /><PitchCoarse Value=\"30\" /><PitchFine Value=\"-25\" />");
    const auto song = importAls (als175a::bytesOf (set), als181c::options());
    CHECK (song.has_value());
    if (! song)
        return;
    const auto* beat = als181c::clipNamed (*song, "Beat loop");
    CHECK (beat != nullptr && std::abs (beat->pitch - 29.75f) < 1e-4f); // (±12 before round 190hr)
    const auto out = exportAls (*song, AlsExportOptions {});
    CHECK (out.xml.find ("<PitchCoarse Value=\"29\"") != std::string::npos || out.xml.find ("<PitchCoarse Value=\"30\"") != std::string::npos);
}

TEST (dv190hr_live_grid_steps_and_keys)
{
    using namespace lynx;
    using namespace lynx::livegrid;
    Grid g; // DefaultLiveSet: adaptive, Medium, 1/16, snap on
    CHECK (! g.fixed && g.density == mediumDensity && g.snap && ! g.triplet);
    g.fixed = true;
    CHECK (stepTicks (g, 0.1) == ppq / 4);
    CHECK (stepText (stepTicks (g, 0.1), false) == "1/16");
    g.triplet = true;
    CHECK (stepTicks (g, 0.1) == ppq / 6);
    CHECK (stepText (stepTicks (g, 0.1), true) == "1/16T");
    g.triplet = false;
    CHECK (narrower (g) && g.fixedIndex == 8 && ! narrower (g)); // 1/32 is the narrowest fixed value
    while (wider (g)) {}
    CHECK (g.fixedIndex == 0 && stepText (stepTicks (g, 0.1), false) == "8 Bars");
    // adaptive: the finest step whose lines stay `densityPixels` apart; Narrower halves it at the same zoom
    Grid a;
    const auto ppt = 20.0 / (ppq / 4.0); // 1/16 = 20 px
    CHECK (stepTicks (a, ppt) == ppq / 4);
    CHECK (narrower (a) && stepTicks (a, ppt) == ppq / 8);
    CHECK (wider (a) && wider (a) && stepTicks (a, ppt) == ppq / 2);
    CHECK (stepText (4 * ppq, false) == "1 Bar" && stepText (8 * ppq, false) == "2 Bars");
    // .als Grid: 1/8 fixed triplet, 10 px
    const auto f = fromLive (1, 8, 10, 3, false, true);
    CHECK (f.fixed && f.triplet && ! f.snap && f.fixedIndex == 6 && f.density == 3);
}
