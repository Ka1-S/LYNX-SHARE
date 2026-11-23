// Round 190pa (research190oe P21 / P14, LYNX extras): automation write modes (core/AutomationModes.h, core/Recorder.h)
// and range edits on chosen tracks (core/RangeEdit.h, core/QuickRender.h planRange). The engine's take with each mode,
// the .lynx round trip, the copies for 0.1.30 / 0.1.28, Capture automation with Latch / Write and undo of the range
// commands are checked in the app's --selftest ("automodes190pa"): they need the engine and ProjectFile.

#include "core/AutomationModes.h"
#include "core/CaptureAutomation.h"
#include "core/QuickRender.h"
#include "core/RangeEdit.h"
#include "core/Recorder.h"

namespace automodes190pa
{
constexpr double ticksPerMs = 120.0 * lynx::ppq / 60000.0; // (120 BPM)
constexpr std::int64_t bar = 4 * lynx::ppq;

// An automation clip over 16 bars on track 0 holding a flat 0.6 for insert 1's volume
inline lynx::Song curveSong()
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.patterns.resize (1);
    Automation a;
    a.name = "Insert 1 volume";
    a.target = { AutomationTargetKind::mixerVolume, 1, 0, 0 };
    a.points = { { 0, 0.6f, 0.0f }, { 16 * bar, 0.6f, 0.0f } };
    s.automations.push_back (a);
    Clip c;
    c.kind = ClipKind::automation;
    c.automation = 0;
    c.track = 0;
    c.start = 0;
    c.length = 16 * bar;
    s.playlist.push_back (c);
    return s;
}

// AudioEngine::updateAutomationRecording's take, frame by frame (33 ms), with the same core calls: the control is grabbed
// at 1000 ms (it jumps to 0.3), moved to 0.9 by 2000 ms and let go there; the take runs from 0 to 4000 ms.
inline lynx::Song recordTake (lynx::automodes::Mode mode)
{
    using namespace lynx;
    auto s = curveSong();
    AutomationWrite w;
    w.target = s.automations[0].target;
    w.name = "Insert 1 volume";
    std::vector<RecordedValue> pending;
    bool touching = false, started = false, held = false;
    double lastMs = 0.0, takeStart = -1.0;
    int countdown = 6;
    auto commit = [&] (bool ended) {
        if (! pending.empty())
            commitAutomationPoints (s, w, std::move (pending), 0);
        pending.clear();
        if (ended)
        {
            if (automodes::glidesBack (mode) && w.automation >= 0)
                glideBackToCurve (s.automations[(std::size_t) w.automation], w, automodes::returnTicks (s.bpm, ppq));
            w.lastLocal = -1;
        }
    };
    for (double now = 0.0; now <= 4000.0; now += 33.0)
    {
        const auto tick = now * ticksPerMs;
        if (takeStart < 0.0)
            takeStart = tick;
        float value = 0.6f;
        if (now >= 1000.0 && now <= 2000.0) // the hand on the control
        {
            value = 0.3f + 0.6f * (float) ((now - 1000.0) / 1000.0);
            touching = held = true;
            lastMs = now;
        }
        else if (now > 2000.0 && held) // let go (tweakReleased)
        {
            held = false;
            lastMs = 2000.0;
        }
        if (now > 2000.0)
            value = 0.9f; // (the control stays where it was left: overridden while touched)
        if (! touching)
            continue;
        if (! automationTakeFrame ((int) mode, held, now - lastMs, tick, value, takeStart, started, pending))
        {
            touching = false;
            commit (true);
            continue;
        }
        if (--countdown <= 0)
        {
            commit (false);
            countdown = 6;
        }
    }
    commit (automodes::glidesBack (mode)); // recording stops (Touch: the gestures still touched end)
    return s;
}

inline float at (const lynx::Song& s, double ms) { return s.automations[0].valueAt (ms * ticksPerMs); }

inline lynx::Clip clipOn (int track, std::int64_t start, std::int64_t length, int pattern = 0)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = pattern;
    c.track = track;
    c.start = start;
    c.length = length;
    return c;
}

// Four tracks with one 16-bar clip each; tracks 1 and 3 each own an automation lane (a ramp 0 -> 1 over 16 bars, with a
// point on it at bar 6)
inline lynx::Song rangeSong()
{
    using namespace lynx;
    Song s;
    s.patterns.resize (4);
    for (int t = 0; t < 4; ++t)
        s.playlist.push_back (clipOn (t, 0, 16 * bar, t));
    s.playlistTracks.resize (4);
    for (int t : { 1, 3 })
    {
        Automation a;
        a.name = "Lane " + std::to_string (t);
        a.target = { AutomationTargetKind::mixerVolume, t + 1, 0, 0 };
        a.points = { { 0, 0.0f, 0.0f }, { 6 * bar, 0.375f, 0.0f }, { 16 * bar, 1.0f, 0.0f } }; // (a point on the ramp at bar 6)
        s.automations.push_back (a);
        s.playlistTracks[(std::size_t) t].lanes.push_back ({ (int) s.automations.size() - 1, 0 });
    }
    return s;
}

inline std::vector<lynx::Clip> clipsOf (const lynx::Song& s, int track)
{
    std::vector<lynx::Clip> out;
    for (const auto& c : s.playlist)
        if (c.track == track)
            out.push_back (c);
    std::sort (out.begin(), out.end(), [] (const lynx::Clip& a, const lynx::Clip& b) { return a.start < b.start; });
    return out;
}
} // namespace automodes190pa

// The modes' rules: Read is FL's gesture (held, or moved within 350 ms), Latch / Write keep writing, ids round trip
TEST (automodes190pa_rules)
{
    using namespace lynx::automodes;
    CHECK (stillWriting (Mode::read, true, 5000.0) && stillWriting (Mode::read, false, 349.0) && ! stillWriting (Mode::read, false, 351.0));
    CHECK (stillWriting (Mode::touch, false, 200.0) && ! stillWriting (Mode::touch, false, 400.0));
    CHECK (stillWriting (Mode::latch, false, 1.0e9) && stillWriting (Mode::write, false, 1.0e9));
    CHECK (glidesBack (Mode::touch) && ! glidesBack (Mode::read) && ! glidesBack (Mode::latch));
    CHECK (holdsToEnd (Mode::latch) && holdsToEnd (Mode::write) && ! holdsToEnd (Mode::touch) && writesFromStart (Mode::write) && ! writesFromStart (Mode::latch));
    for (auto m : { Mode::read, Mode::touch, Mode::latch, Mode::write })
        CHECK (fromId (idOf (m)) == m && fromInt ((int) m) == m);
    CHECK (fromId ("bogus") == Mode::read && fromInt (7) == Mode::read && fromInt (-1) == Mode::read);
    CHECK (returnTicks (120.0, lynx::ppq) == 480); // (250 ms at 120 BPM: an eighth note)
    CHECK (returnTicks (0.0, lynx::ppq) == 480 && returnTicks (120.0, lynx::ppq, 0.0) == 1);

    // one frame: Write adds the take's start once, before the first point
    std::vector<lynx::RecordedValue> pending;
    bool started = false;
    CHECK (lynx::automationTakeFrame ((int) Mode::write, true, 0.0, 1000.0, 0.4f, 100.0, started, pending));
    CHECK (pending.size() == 2 && pending[0].tick == 100.0 && pending[0].value == 0.4f && pending[1].tick == 1000.0 && started);
    CHECK (lynx::automationTakeFrame ((int) Mode::write, false, 9999.0, 1100.0, 0.5f, 100.0, started, pending) && pending.size() == 3);
    pending.clear();
    started = false;
    CHECK (lynx::automationTakeFrame ((int) Mode::read, true, 0.0, 1000.0, 0.4f, 100.0, started, pending) && pending.size() == 1);
    CHECK (! lynx::automationTakeFrame ((int) Mode::read, false, 400.0, 1100.0, 0.4f, 100.0, started, pending) && pending.size() == 1);
}

// The brief's acceptance: a scripted gesture per mode, checked on the curve it leaves (an automation clip; the lanes take
// the same writeRecordedStretch / glide). The old curve is a flat 0.6; the hand grabs at 1 s (0.3), reaches 0.9 at 2 s
// and lets go; recording stops at 4 s.
TEST (automodes190pa_gestures_per_mode)
{
    using namespace automodes190pa;
    using lynx::automodes::Mode;
    const auto read = recordTake (Mode::read);
    const auto touch = recordTake (Mode::touch);
    const auto latch = recordTake (Mode::latch);
    const auto write = recordTake (Mode::write);
    auto near = [] (float a, float b, float tol = 0.02f) { return std::abs (a - b) <= tol; };

    // Read = FL: before the grab the old curve, the gesture, held 350 ms after the release, then back to 0.6 at once
    CHECK (near (at (read, 500.0), 0.6f) && near (at (read, 1500.0), 0.6f, 0.03f) && near (at (read, 2200.0), 0.9f));
    CHECK (near (at (read, 2500.0), 0.6f) && near (at (read, 3500.0), 0.6f) && near (at (read, 4500.0), 0.6f));
    CHECK (read.playlist.size() == 1 && read.automations.size() == 1); // (into the clip that plays there)

    // Touch: the same until the gesture ends, then a 250 ms glide back to 0.6 instead of the jump
    CHECK (near (at (touch, 500.0), 0.6f) && near (at (touch, 2200.0), 0.9f));
    {
        // find the gesture's end: the last tick at 0.9
        const auto& p = touch.automations[0].points;
        std::int64_t end = 0;
        for (const auto& x : p)
            if (near (x.value, 0.9f, 0.001f))
                end = std::max (end, x.tick);
        CHECK (end > (std::int64_t) (2300.0 * ticksPerMs) && end < (std::int64_t) (2450.0 * ticksPerMs));
        const auto half = touch.automations[0].valueAt ((double) end + 240.0);
        CHECK (half > 0.7f && half < 0.8f);                                               // halfway down the glide
        CHECK (near (touch.automations[0].valueAt ((double) end + 480.0), 0.6f, 0.001f)); // back on the curve
        CHECK (read.automations[0].valueAt ((double) end + 240.0) < 0.61f);               // (Read: already back)
    }
    CHECK (near (at (touch, 3500.0), 0.6f));

    // Latch: the last value holds after the release until recording stops, then the old curve plays
    CHECK (near (at (latch, 500.0), 0.6f) && near (at (latch, 3000.0), 0.9f) && near (at (latch, 3900.0), 0.9f));
    CHECK (near (at (latch, 4200.0), 0.6f) && near (at (latch, 6000.0), 0.6f));

    // Write: as Latch, and from the take's start: before the grab the curve holds the first value written (0.3)
    CHECK (near (at (write, 100.0), 0.3f) && near (at (write, 900.0), 0.3f) && near (at (write, 3000.0), 0.9f));
    CHECK (near (at (write, 4200.0), 0.6f));

    // what lies outside the takes is the old curve in every mode
    for (const auto* s : { &read, &touch, &latch, &write })
        CHECK (near (at (*s, 8000.0), 0.6f, 0.0001f) && near (at (*s, 30000.0), 0.6f, 0.0001f));
}

// Touch's glide: nothing to return to on an automation the gesture made itself; a held old curve is returned to
TEST (automodes190pa_glide_back)
{
    using namespace lynx;
    Automation fresh;
    AutomationWrite w;
    writeRecordedStretch (fresh, { { 100, 0.2f, 0.0f }, { 200, 0.8f, 0.0f } }, w);
    CHECK (! w.hadCurve && ! glideBackToCurve (fresh, w, 480) && fresh.points.size() == 2);

    Automation old;
    old.points = { { 0, 0.5f, 0.0f } }; // (a lane's flat line: one point)
    AutomationWrite w2;
    writeRecordedStretch (old, { { 1000, 0.2f, 0.0f }, { 2000, 0.9f, 0.0f } }, w2);
    CHECK (w2.hadCurve && std::abs (w2.returnValue - 0.5f) < 1.0e-6f);
    CHECK (std::abs (old.valueAt (5000.0) - 0.9f) < 1.0e-6f); // (Read leaves the last value: nothing after it)
    CHECK (glideBackToCurve (old, w2, 480));
    CHECK (std::abs (old.valueAt (2240.0) - 0.7f) < 0.01f && std::abs (old.valueAt (5000.0) - 0.5f) < 1.0e-6f);
    CHECK (! glideBackToCurve (old, AutomationWrite {}, 480)); // (no stretch)
}

// Capture automation's play passes (Latch / Write extend a captured gesture to them)
TEST (automodes190pa_capture_passes)
{
    using namespace lynx::captureauto;
    Buffer b;
    CHECK (b.currentPass() == 0 && b.pass (1) == nullptr);
    b.notePlaying (100.0);
    CHECK (b.currentPass() == 1);
    b.notePlaying (500.0);
    b.notePlaying (900.0);
    CHECK (b.pass (1) != nullptr && b.pass (1)->startTick == 100.0 && b.pass (1)->endTick == 900.0);
    b.noteStopped();
    CHECK (b.currentPass() == 0 && b.pass (1)->endTick == 900.0);
    b.notePlaying (50.0); // a new play
    CHECK (b.currentPass() == 2 && b.pass (2)->startTick == 50.0);
    b.notePlaying (40.0); // a jump back: a new pass
    CHECK (b.currentPass() == 3);
    for (int i = 0; i < 300; ++i)
    {
        b.noteStopped();
        b.notePlaying ((double) i);
    }
    CHECK (b.pass (1) == nullptr && b.pass (b.currentPass()) != nullptr); // (the oldest passes are dropped)
}

// The brief's acceptance (P14): a range on tracks 2-3 (indices 1 and 2): Delete removes only their content inside it,
// tracks 1 and 4 (0 and 3) are unchanged; the lane of track 2 is cleared inside, track 4's lane is not
TEST (rangeedit190pa_delete_on_chosen_tracks)
{
    using namespace lynx;
    using namespace automodes190pa;
    auto s = rangeSong();
    const auto before = s;
    const auto r = rangeedit::fromCorners (8 * bar, 4 * bar, 2, 1);
    CHECK (r.from == 4 * bar && r.to == 8 * bar && r.tracks == std::vector<int> ({ 1, 2 }) && r.laneAutomation == -1);
    CHECK (rangeedit::laneAutomations (s, r) == std::vector<int> ({ 0 }));
    CHECK (rangeedit::clearRange (s, r) == 3); // (two clips and one lane)
    CHECK (clipsOf (s, 0) == clipsOf (before, 0) && clipsOf (s, 3) == clipsOf (before, 3));
    CHECK (s.automations[1].points == before.automations[1].points);
    for (int t : { 1, 2 })
    {
        const auto c = clipsOf (s, t);
        CHECK (c.size() == 2);
        if (c.size() == 2)
        {
            CHECK (c[0].start == 0 && c[0].length == 4 * bar && c[0].offset == 0 && c[0].pattern == t);
            CHECK (c[1].start == 8 * bar && c[1].length == 8 * bar && c[1].offset == 8 * bar && c[1].pattern == t);
        }
    }
    // the lane: the ramp outside the range as it was, a straight line between its edge values inside
    const auto& lane = s.automations[0];
    CHECK (std::abs (lane.valueAt (2.0 * bar) - 0.125f) < 1.0e-5f && std::abs (lane.valueAt (12.0 * bar) - 0.75f) < 1.0e-5f);
    CHECK (std::abs (lane.valueAt (6.0 * bar) - 0.375f) < 1.0e-5f);
    CHECK (std::none_of (lane.points.begin(), lane.points.end(), [] (const AutomationPoint& p) { return p.tick > 4 * bar && p.tick < 8 * bar; }));
    // a range that holds nothing changes nothing
    auto t = rangeSong();
    CHECK (rangeedit::clearRange (t, rangeedit::fromCorners (20 * bar, 24 * bar, 0, 3)) == 0 && t.playlist == rangeSong().playlist);
    CHECK (rangeedit::clearRange (t, {}) == 0);
}

// Copy / cut / paste / duplicate keep to the range's tracks; a lane lands only in its own track
TEST (rangeedit190pa_copy_paste_duplicate)
{
    using namespace lynx;
    using namespace automodes190pa;
    auto s = rangeSong();
    const auto r = rangeedit::fromCorners (4 * bar, 8 * bar, 1, 2);
    const auto copy = rangeedit::copyRange (s, r);
    CHECK (copy.length == 4 * bar && copy.trackCount == 2 && copy.clips.size() == 2 && copy.lanes.size() == 1);
    CHECK (copy.clips[0].start == 0 && copy.clips[0].length == 4 * bar && copy.lanes[0].track == 0 && copy.lanes[0].automation == 0);

    // paste at bar 20 onto the same tracks
    int skipped = -1;
    const auto pasted = rangeedit::pasteRange (s, 20 * bar, 1, copy, &skipped);
    CHECK (skipped == 0 && pasted.from == 20 * bar && pasted.to == 24 * bar && pasted.tracks == std::vector<int> ({ 1, 2 }));
    CHECK (clipsOf (s, 1).size() == 2 && clipsOf (s, 1)[1].start == 20 * bar && clipsOf (s, 1)[1].offset == 4 * bar);
    CHECK (clipsOf (s, 0).size() == 1 && clipsOf (s, 3).size() == 1);
    CHECK (std::abs (s.automations[0].valueAt (22.0 * bar) - 0.375f) < 1.0e-5f); // (the copied ramp: 0.25 -> 0.5)

    // pasted one track lower: the clips move to 2-3, the lane of track 1 has no place there
    auto t = rangeSong();
    rangeedit::pasteRange (t, 0, 2, copy, &skipped);
    CHECK (skipped == 1);
    const auto on3 = clipsOf (t, 3);
    CHECK (on3.size() == 2 && on3[0].start == 0 && on3[0].pattern == 2 && on3[0].length == 4 * bar && on3[1].pattern == 3 && on3[1].start == 4 * bar);
    CHECK (t.automations[0].points == rangeSong().automations[0].points);

    // duplicate: repeated right after itself, what lay there replaced, nothing after it moves
    auto d = rangeSong();
    const auto next = rangeedit::duplicateRange (d, r);
    CHECK (next.from == 8 * bar && next.to == 12 * bar && next.tracks == r.tracks);
    const auto on1 = clipsOf (d, 1);
    CHECK (on1.size() == 3 && on1[0].start == 0 && on1[0].length == 8 * bar && on1[1].start == 8 * bar && on1[1].offset == 4 * bar
           && on1[1].length == 4 * bar && on1[2].start == 12 * bar && on1[2].offset == 12 * bar);
    CHECK (clipsOf (d, 0) == clipsOf (rangeSong(), 0) && clipsOf (d, 3) == clipsOf (rangeSong(), 3));
    CHECK (std::abs (d.automations[0].valueAt (10.0 * bar) - 0.375f) < 1.0e-5f && std::abs (d.automations[0].valueAt (14.0 * bar) - 0.875f) < 1.0e-5f);

    // cut = copy + delete
    auto c = rangeSong();
    const auto cut = rangeedit::cutRange (c, r);
    CHECK (cut.clips.size() == 2 && clipsOf (c, 1).size() == 2 && clipsOf (c, 0).size() == 1);
}

// Split at the edges, move (in time keeps lanes along; to other tracks the lanes stay), one lane only, the render plan
TEST (rangeedit190pa_split_move_lane_render)
{
    using namespace lynx;
    using namespace automodes190pa;
    auto s = rangeSong();
    const auto r = rangeedit::fromCorners (4 * bar, 8 * bar, 1, 2);
    CHECK (rangeedit::splitAtRange (s, r) == 4 && clipsOf (s, 1).size() == 3 && clipsOf (s, 0).size() == 1);
    CHECK (rangeedit::splitAtRange (s, r) == 0); // (already cut there)

    auto m = rangeSong();
    const auto moved = rangeedit::moveRange (m, r, 8 * bar, 0);
    CHECK (moved.from == 12 * bar && moved.to == 16 * bar && moved.tracks == r.tracks);
    const auto on2 = clipsOf (m, 2);
    CHECK (on2.size() == 3 && on2[0].length == 4 * bar && on2[1].start == 8 * bar && on2[1].length == 4 * bar && on2[2].start == 12 * bar
           && on2[2].offset == 4 * bar);
    CHECK (std::abs (m.automations[0].valueAt (14.0 * bar) - 0.375f) < 1.0e-5f); // (the lane's piece moved along)

    auto down = rangeSong();
    const auto lower = rangeedit::moveRange (down, r, 0, 1);
    CHECK (lower.tracks == std::vector<int> ({ 2, 3 }));
    CHECK (down.automations[0].points == rangeSong().automations[0].points); // (track 1's lane stays)
    CHECK (clipsOf (down, 1).size() == 2 && clipsOf (down, 3).size() == 3);
    CHECK (rangeedit::moveRange (down, lower, 0, -10).tracks.front() == 0); // (never above the first track)

    // one lane: only its curve, no clip
    auto l = rangeSong();
    auto laneOnly = rangeedit::fromCorners (4 * bar, 8 * bar, 3, 3, 1);
    CHECK (laneOnly.laneAutomation == 1 && rangeedit::fromCorners (4 * bar, 8 * bar, 2, 3, 1).laneAutomation == -1);
    CHECK (rangeedit::clearRange (l, laneOnly) == 1 && l.playlist == rangeSong().playlist && l.automations[0].points == rangeSong().automations[0].points);
    CHECK (rangeedit::splitAtRange (l, laneOnly) == 0);
    const auto laneCopy = rangeedit::copyRange (rangeSong(), laneOnly);
    CHECK (laneCopy.clips.empty() && laneCopy.lanes.size() == 1 && rangeedit::pastedRange (laneCopy, 0, 3).laneAutomation == 1);

    // render: the range's playing clips, cut at its edges into a new track above its top track
    auto q = rangeSong();
    q.playlist[2].muted = true; // (track 2's clip)
    const auto clips = rangeedit::renderClips (q, r);
    CHECK (clips.size() == 1 && q.playlist[(std::size_t) clips[0]].track == 1);
    quickrender::Plan plan;
    CHECK (quickrender::planRange (q, clips, r.top(), r.from, r.to, plan) == quickrender::Result::ok);
    CHECK (plan.timeSelection && plan.start == 4 * bar && plan.end == 8 * bar && plan.topTrack == 1 && plan.clips == clips);
    const auto render = quickrender::makePlanRenderSong (q, plan);
    CHECK (render.loopStart == 4 * bar && render.loopEnd == 8 * bar && render.playlist.size() == 1);
    quickrender::Plan none;
    CHECK (quickrender::planRange (q, {}, 1, r.from, r.to, none) == quickrender::Result::nothingSelected);
}
