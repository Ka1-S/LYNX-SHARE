// Round 190jd, part A (live check 190iw Q2 + Q7): DirectWave's zone map gestures as FL's ZoneEditor, Slicex's deck Snap
// menu, and the Slicex play line the editor follows. The paint is checked by the --snapshot pairs; these are the parts
// without JUCE. (Uses slicex190h::Rig from Slicex190hTests.inc.)

#include "core/DirectWaveZoneDrag.h"
#include "core/SlicexDeckSnap.h"
#include <cmath>

TEST (livefix190jd_directwave_zone_drag_as_fl)
{
    using namespace lynx;
    dw::Zone z;
    z.lowKey = 40, z.highKey = 45, z.root = 42, z.lowVel = 30, z.highVel = 80;
    // a plain (or Ctrl) drag moves the keys only: 40–45 → 44–49, root 42 kept
    auto a = dw::dragZone (z, dw::ZoneHandle::body, 4, 0, false);
    CHECK (a.lowKey == 44 && a.highKey == 49 && a.root == 42 && a.lowVel == 30 && a.highVel == 80);
    // Alt (and Ctrl+Alt) moves the root with the keys: −4 keys, root 42 → 38
    auto b = dw::dragZone (z, dw::ZoneHandle::body, -4, 0, true);
    CHECK (b.lowKey == 36 && b.highKey == 41 && b.root == 38);
    // up / down moves the velocity range, keeping its size: 30–80 → 52–102
    auto c = dw::dragZone (z, dw::ZoneHandle::body, 0, 22, false);
    CHECK (c.lowVel == 52 && c.highVel == 102 && c.lowKey == 40 && c.highKey == 45);
    // ... and stops at the ends without changing the size
    auto d = dw::dragZone (z, dw::ZoneHandle::body, 200, 200, true);
    CHECK (d.highKey == 127 && d.lowKey == 122 && d.highVel == 127 && d.lowVel == 77 && d.root == 124);
    // the lower-right square sets H-Key + H-Vel (45 → 48, 127 → 80 in FL's take)
    dw::Zone e = z;
    e.highVel = 127;
    auto f = dw::dragZone (e, dw::ZoneHandle::highCorner, 3, -47, false);
    CHECK (f.highKey == 48 && f.highVel == 80 && f.lowKey == 40 && f.lowVel == 30 && f.root == 42);
    // the upper-left square sets L-Key + L-Vel (40 → 38, 0 → 30)
    dw::Zone g = z;
    g.lowVel = 0;
    auto h = dw::dragZone (g, dw::ZoneHandle::lowCorner, -2, 30, false);
    CHECK (h.lowKey == 38 && h.lowVel == 30 && h.highKey == 45 && h.highVel == 80);
    // a corner never crosses the other one
    auto i = dw::dragZone (z, dw::ZoneHandle::lowCorner, 50, 90, false);
    CHECK (i.lowKey == 45 && i.lowVel == 80);
}

TEST (livefix190jd_slicex_deck_snap_modes)
{
    using namespace lynx::slicex;
    // FL's menu: five modes, grid first (the default)
    CHECK (deckSnapCount == 5 && (int) DeckSnap::grid == 0 && (int) DeckSnap::pitchPeriod == 4);
    // a 441 Hz sine at 44.1 kHz: a period of 100 frames, rising crossings at 0, 100, 200 …, falling ones at 50, 150 …
    std::vector<float> s (44100);
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = 0.5f * (float) std::sin (6.283185307179586 * 441.0 * (double) i / 44100.0);
    DeckWave w { s.data(), nullptr, (std::int64_t) s.size(), 44100.0 };
    const std::vector<std::int64_t> edges { 1000, 5000 };
    // grid: to the pixel, as FL's measured deck drags (REA_rodada190aj §2)
    CHECK (snapDeckFrame (DeckSnap::grid, 1234.4, w, 4.0, edges) == 1234.4);
    // samples: whole frames only
    CHECK (snapDeckFrame (DeckSnap::samples, 1234.4, w, 4.0, edges) == 1234.0);
    // zero-crossing: the nearest one either way (1250, falling)
    CHECK (std::abs (snapDeckFrame (DeckSnap::zeroCrossing, 1234.0, w, 4.0, edges) - 1250.0) <= 1.0);
    // pitch period: the nearest rising one (1200), the period found by the autocorrelation
    CHECK (std::abs ((double) localPitchPeriod (w, 1234) - 100.0) <= 1.0);
    CHECK (std::abs (snapDeckFrame (DeckSnap::pitchPeriod, 1234.0, w, 4.0, edges) - 1200.0) <= 1.0);
    // regions: an edge within 12 px
    CHECK (snapDeckFrame (DeckSnap::regions, 1100.0, w, 10.0, edges) == 1000.0);
    CHECK (snapDeckFrame (DeckSnap::regions, 1200.0, w, 10.0, edges) == 1200.0);
    // silence has no pitch: the nearest crossing within the zero-crossing radius, else where it was
    std::vector<float> quiet (44100, 0.0f);
    DeckWave q { quiet.data(), nullptr, (std::int64_t) quiet.size(), 44100.0 };
    CHECK (localPitchPeriod (q, 2000) == 0);
}

TEST (livefix190jd_slicex_play_line_follows_the_newest_voice)
{
    // what the editor's red line and lit Play button follow: the deck and frame the newest voice reads, none when idle
    slicex190h::Rig rig;
    rig.load();
    CHECK (rig.engine.playMark().deck == -1);
    CHECK (rig.play (61) >= 0); // region 1: frames 11025 … 22049
    rig.run (4096);
    auto m = rig.engine.playMark();
    CHECK (m.deck == 0);
    CHECK (m.frame >= 11025.0 + 4000.0 && m.frame < 11025.0 + 4200.0);
    // a newer note takes the line
    CHECK (rig.play (63) >= 0); // region 3: 33075 …
    rig.run (1024);
    m = rig.engine.playMark();
    CHECK (m.deck == 0 && m.frame >= 33075.0 && m.frame < 33075.0 + 1200.0);
    // both regions run out (and their declicks): no line
    rig.run (30000);
    CHECK (rig.engine.idle() && rig.engine.playMark().deck == -1);
    // killAll clears it at once
    CHECK (rig.play (60) >= 0);
    rig.run (512);
    CHECK (rig.engine.playMark().deck == 0);
    rig.engine.killAll();
    CHECK (rig.engine.playMark().deck == -1);
}
