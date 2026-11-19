// Round 190lx (details pass 4): Live's slot-block clipboard (DL-29) and its separate Punch-In / Punch-Out switches
// (DL-61).
#include "core/RecordLoop.h"
#include "core/SessionOps.h"
#include "core/MeterScale.h"
#include "core/NoteTools.h"

namespace details190lx
{
using namespace lynx;

inline Clip clipOf (int pattern)
{
    Clip c;
    c.pattern = pattern;
    c.length = 4 * ppq;
    return c;
}

inline Song gridSong()
{
    Song s;
    s.patterns.resize (4);
    s.playlistTracks.resize (6);
    return s;
}
} // namespace details190lx

// Live's Copy / Paste / Duplicate of a slot rectangle: the block's clips at their offsets, holes left alone, the parts
// past the grid dropped; Duplicate puts the copy right under the rectangle
TEST (details190lx_slot_block_copy_paste_duplicate)
{
    using namespace lynx;
    using namespace details190lx;
    auto s = gridSong();
    launcher::setClip (s, 0, 0, clipOf (0));
    launcher::setClip (s, 1, 1, clipOf (1)); // (0,1) and (1,0) stay empty: holes
    launcher::setClip (s, 3, 3, clipOf (2)); // under the paste's hole: must stay
    auto patternAt = [&s] (int t, int r) {
        const auto* c = launcher::clipAt (*s.clipGrid, t, r);
        return c != nullptr ? c->pattern : -1;
    };

    const auto block = session::copyBlock (s, 1, 1, 0, 0); // (corners in either order)
    CHECK (block.tracks == 2 && block.scenes == 2 && block.clips.size() == 2);
    CHECK (block.clips[0].track == 0 && block.clips[0].scene == 0 && block.clips[1].track == 1 && block.clips[1].scene == 1);

    CHECK (session::pasteBlock (s, 2, 2, block) == 2);
    CHECK (patternAt (2, 2) == 0 && patternAt (3, 3) == 1 && patternAt (3, 2) == -1 && patternAt (2, 3) == -1);
    // a hole does not clear what lies under it
    launcher::setClip (s, 4, 5, clipOf (3));
    CHECK (session::pasteBlock (s, 3, 4, block) == 2);
    CHECK (patternAt (3, 4) == 0 && patternAt (4, 5) == 1);
    // past the last track / scene: dropped
    CHECK (session::pasteBlock (s, launcher::maxTracks - 1, 0, block) == 1);
    CHECK (session::pasteBlock (s, 0, -1, block) == 0);

    // Duplicate: the rectangle's copy goes under it
    CHECK (session::duplicateBlock (s, 0, 0, 1, 1) == 2);
    CHECK (patternAt (0, 2) == 0 && patternAt (1, 3) == 1);
    // an empty rectangle: nothing
    CHECK (session::duplicateBlock (s, 5, 0, 5, 1) == -1);

    // Cut's second half
    session::clearBlock (s, 2, 2, 3, 3);
    CHECK (patternAt (2, 2) == -1 && patternAt (3, 3) == -1 && patternAt (3, 4) == 0);
}

// Live's Punch-In / Punch-Out switches: in alone starts the take at the loop region and runs on (no post-roll stop), out
// alone records from where recording starts to the region's end; both = round 163's punch; off: neither end
TEST (details190lx_punch_in_and_out_each_on_its_own)
{
    using lynx::recordloop::punchEnds;
    constexpr std::int64_t here = 100, from = 1000, to = 5000, open = 1 << 30;
    auto e = punchEnds (here, from, to, true, true, true, open);
    CHECK (e.in == from && e.out == to && e.stopsAtOut);
    e = punchEnds (here, from, to, true, true, false, open);
    CHECK (e.in == from && e.out == open && ! e.stopsAtOut);
    e = punchEnds (here, from, to, true, false, true, open);
    CHECK (e.in == here && e.out == to && e.stopsAtOut);
    e = punchEnds (here, from, to, false, true, true, open);
    CHECK (e.in == here && e.out == open && ! e.stopsAtOut);
}

// FL's "Alternate meter scale": off = LYNX's straight -60 … +6 dB axis; on = FL's measured law, rising, and the hint's
// reading under the pointer is its inverse
TEST (details190lx_alternate_meter_scale_law)
{
    using namespace lynx::meterscale;
    CHECK (std::abs (positionOfDb (-60.0f, false)) < 1.0e-6f && std::abs (positionOfDb (6.0f, false) - 1.0f) < 1.0e-6f);
    CHECK (std::abs (dbAtPosition (0.5f, false) + 27.0f) < 1.0e-4f);
    // FL's Master meter (capture 190lx A1, H = 346 px, bottom 555): 0 dB at y 262, -6 dB at y 370; hint readings
    CHECK (std::abs (positionOfDb (0.0f, true) - (555.0f - 262.0f) / 346.0f) < 0.004f);
    CHECK (std::abs (positionOfDb (-6.0f, true) - (555.0f - 370.0f) / 346.0f) < 0.004f);
    CHECK (std::abs (dbAtPosition ((555.0f - 304.0f) / 346.0f, true) + 2.14f) < 0.1f);
    CHECK (std::abs (dbAtPosition ((555.0f - 480.0f) / 346.0f, true) + 15.79f) < 0.3f);
    float last = -1.0f;
    for (float db = -60.0f; db <= 2.0f; db += 0.5f)
    {
        const auto p = positionOfDb (db, true);
        CHECK (p > last && p >= 0.0f && p <= 1.0f);
        CHECK (std::abs (dbAtPosition (p, true) - db) < 0.01f);
        last = p;
    }
    CHECK (std::abs (position (1.0f, false) - positionOfDb (0.0f, false)) < 1.0e-5f);
    CHECK (position (0.0f, true) == 0.0f && std::abs (position (1.0f, true) - positionOfDb (0.0f, true)) < 1.0e-5f);
}

// FL's Channel rack graph tabs Note and Shift (DE-30): Note is the key over the lane, Shift the start offset in the step
// (100 % = 99 % of a step); the old lanes read and write as before
TEST (details190lx_graph_note_and_shift_lanes)
{
    using namespace lynx;
    using namespace lynx::notetools;
    Note n;
    n.start = 3 * ticksPerStep;
    n.key = 60;
    CHECK (std::abs (graphValue (n, graphNote, ticksPerStep) - 60.0f / 127.0f) < 1.0e-5f);
    setGraphValue (n, graphNote, 1.0f, ticksPerStep);
    CHECK (n.key == 127);
    CHECK (graphValue (n, graphShift, ticksPerStep) == 0.0f);
    setGraphValue (n, graphShift, 1.0f, ticksPerStep);
    CHECK (n.start > 3 * ticksPerStep && n.start < 4 * ticksPerStep);
    CHECK (std::abs (graphValue (n, graphShift, ticksPerStep) - 1.0f) < 0.01f);
    setGraphValue (n, graphShift, 0.0f, ticksPerStep);
    CHECK (n.start == 3 * ticksPerStep);
    setGraphValue (n, (int) Level::velocity, 0.25f, ticksPerStep);
    CHECK (std::abs (n.velocity - 0.25f) < 1.0e-6f && std::abs (graphValue (n, (int) Level::velocity, ticksPerStep) - 0.25f) < 1.0e-6f);
    CHECK (graphIsBipolar ((int) Level::pan) && ! graphIsBipolar (graphNote) && ! graphIsBipolar (graphShift));
    CHECK (graphDefault (graphShift) == 0.0f && std::abs (graphDefault (graphNote) - 60.0f / 127.0f) < 1.0e-5f);
    // (rollup 0.1.30 step 11: round 190ma's lanes — FL's ninth, Repeat, after Shift)
    CHECK (graphTabOrder[0] == graphNote && graphTabOrder[graphLaneCount - 2] == graphShift && graphTabOrder[graphLaneCount - 1] == graphRepeat);
}
