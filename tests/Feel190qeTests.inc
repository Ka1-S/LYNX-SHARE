// Round 190qe (TOP-NEXT R1, "Feel 1"): core/EditorFeel.h — the edge scroll (FL's measured law in the Pattern layout, the
// Live / Logic-like one elsewhere), the view stack, the zoom glide, the zoom anchor rule, the selection history and FL's
// "Keep labels on screen" placement.

#include "core/EditorFeel.h"

namespace feel190qe
{
inline bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }
} // namespace feel190qe

TEST (feel190qe_fl_edge_law)
{
    using namespace lynx::edgescroll;
    using feel190qe::near;
    // FL Studio 2026's measured speeds (verify_190qc/REA_rodada190qc.md, Playlist left edge): 3 px inside nothing, 1 px
    // inside ~10-20, at the edge 20, 20 px out 223, 80 px out 775, 250 px out ~1900
    CHECK (flSpeed (-3.0) == 0.0);
    CHECK (flSpeed (-2.0) == 0.0);
    CHECK (flSpeed (-1.0) > 5.0 && flSpeed (-1.0) < 21.0);
    CHECK (near (flSpeed (0.0), 20.0, 0.01));
    CHECK (near (flSpeed (20.0), 223.0, 0.01));
    CHECK (near (flSpeed (80.0), 775.0, 0.01));
    CHECK (near (flSpeed (250.0), 1927.0, 0.01));
    // ≈ 10 px/s per px past the edge up to 80 px (FL's fit)
    for (double d = 5.0; d <= 80.0; d += 5.0)
        CHECK (flSpeed (d) / (d + 2.0) > 8.5 && flSpeed (d) / (d + 2.0) < 13.5);
    // the curve only grows, and goes on past the last measured point (FL showed no cap)
    for (double d = -2.0; d < 600.0; d += 0.5)
        CHECK (flSpeed (d + 0.5) >= flSpeed (d));
    CHECK (flSpeed (500.0) > flSpeed (400.0));
    // both edges alike, signed; "1 px inside" counts from the view's last pixel
    CHECK (axisSpeed (Law::fl, 100.0, 100.0, 1100.0) < 0.0);                       // on the first pixel: back
    CHECK (near (axisSpeed (Law::fl, 1099.0, 100.0, 1100.0), 20.0, 0.01));          // on the last pixel: on
    CHECK (axisSpeed (Law::fl, 1096.0, 100.0, 1100.0) == 0.0);                      // 3 px inside: still
    CHECK (axisSpeed (Law::fl, 600.0, 100.0, 1100.0) == 0.0);                       // the middle: still
    CHECK (near (axisSpeed (Law::fl, 1119.0, 100.0, 1100.0), 223.0, 0.01));         // 20 px past the right edge
    CHECK (near (axisSpeed (Law::fl, 80.0, 100.0, 1100.0), -223.0, 0.01));          // 20 px past the left edge
    // in screen pixels: the speed does not depend on the view's width (or the zoom)
    CHECK (axisSpeed (Law::fl, 1119.0, 100.0, 1100.0) == axisSpeed (Law::fl, 419.0, 100.0, 400.0));
}

TEST (feel190qe_smooth_edge_law)
{
    using namespace lynx::edgescroll;
    using feel190qe::near;
    // Clip-launch / Simple: a 24 px zone; 0.5 screen/s where it starts, faster with depth, at most 4 screens/s
    const double view = 1000.0;
    CHECK (smoothSpeed (-30.0, view) == 0.0);
    CHECK (smoothSpeed (-24.0, view) == 0.0);
    CHECK (near (smoothSpeed (-23.9, view), 500.0, 1.0));
    CHECK (smoothSpeed (0.0, view) > 500.0 && smoothSpeed (0.0, view) < 4000.0);
    CHECK (near (smoothSpeed (48.0, view), 4000.0, 0.01));
    CHECK (near (smoothSpeed (400.0, view), 4000.0, 0.01));
    for (double d = -23.0; d < 60.0; d += 1.0)
        CHECK (smoothSpeed (d + 1.0, view) >= smoothSpeed (d, view));
    // in screens: a view twice as wide runs twice as many px
    CHECK (near (smoothSpeed (10.0, 2.0 * view), 2.0 * smoothSpeed (10.0, view), 1e-9));
    // the zone is inside the view: 10 px inside the right edge already scrolls (FL's law would not)
    CHECK (axisSpeed (Law::smooth, 1089.0, 100.0, 1100.0) > 0.0);
    CHECK (axisSpeed (Law::fl, 1089.0, 100.0, 1100.0) == 0.0);
    // a view narrower than two zones: the nearer edge
    CHECK (axisSpeed (Law::smooth, 105.0, 100.0, 130.0) < 0.0);
    CHECK (axisSpeed (Law::smooth, 128.0, 100.0, 130.0) > 0.0);
}

TEST (feel190qe_scroller)
{
    using namespace lynx::edgescroll;
    using feel190qe::near;
    Scroller s;
    // inside: nothing, not running
    auto st = s.update (0.0, 500.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl);
    CHECK (! st.running && st.dx == 0.0 && st.dy == 0.0);
    // 20 px past the right edge: the first frame starts the clock, then ~223 px/s, flat (no acceleration over time)
    st = s.update (1000.0, 1119.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl);
    CHECK (st.running && st.dx == 0.0);
    double moved = 0.0;
    for (int f = 1; f <= 144; ++f) // one second at 144 Hz
    {
        st = s.update (1000.0 + f * 1000.0 / 144.0, 1119.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl);
        CHECK (st.dx == std::floor (st.dx)); // whole px
        moved += st.dx;
    }
    CHECK (near (moved, 223.0, 1.5));
    double second = 0.0;
    for (int f = 145; f <= 288; ++f)
        second += s.update (1000.0 + f * 1000.0 / 144.0, 1119.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl).dx;
    CHECK (near (second, moved, 1.5));
    // a stall: one late frame counts as 50 ms at most
    st = s.update (1000.0 + 288 * 1000.0 / 144.0 + 2000.0, 1119.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl);
    CHECK (st.dx <= 223.0 * 0.05 + 1.0);
    // a quantum of 2 (150 %): even steps only
    Scroller q;
    q.update (0.0, 1119.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, false, Law::fl, 2.0);
    double sum = 0.0;
    for (int f = 1; f <= 60; ++f)
    {
        const auto d = q.update (f * 1000.0 / 60.0, 1119.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, false, Law::fl, 2.0).dx;
        CHECK (std::fmod (d, 2.0) == 0.0);
        sum += d;
    }
    CHECK (near (sum, 223.0, 2.5));
    // vertical only where the drag allows it: past the bottom with canY false, nothing
    Scroller v;
    v.update (0.0, 500.0, 700.0, 100.0, 50.0, 1100.0, 650.0, true, false, Law::fl);
    CHECK (! v.update (100.0, 500.0, 700.0, 100.0, 50.0, 1100.0, 650.0, true, false, Law::fl).running);
    v.update (200.0, 500.0, 700.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl);
    CHECK (v.update (300.0, 500.0, 700.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl).dy > 0.0);
    // Esc holds it until the pointer is back inside; then it runs again on the next visit
    Scroller e;
    e.update (0.0, 60.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl);
    CHECK (e.update (100.0, 60.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl).dx < 0.0);
    e.hold();
    CHECK (! e.update (200.0, 60.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl).running);
    CHECK (! e.update (300.0, 30.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl).running);
    CHECK (! e.update (400.0, 500.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl).running && ! e.isHeld());
    e.update (500.0, 60.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl);
    CHECK (e.update (600.0, 60.0, 300.0, 100.0, 50.0, 1100.0, 650.0, true, true, Law::fl).dx < 0.0);
    // the release
    e.stop();
    CHECK (! e.isRunning());
    // the whole-px quantum of a UI scale
    CHECK (wholeQuantum (1.0) == 1 && wholeQuantum (2.0) == 1 && wholeQuantum (1.5) == 2 && wholeQuantum (1.25) == 4 && wholeQuantum (1.75) == 4);
    // the dragged item stays at the view's edge
    CHECK (clampInto (40.0, 100.0, 1100.0) == 100.0 && clampInto (1500.0, 100.0, 1100.0) == 1099.0 && clampInto (500.0, 100.0, 1100.0) == 500.0);
}

TEST (feel190qe_view_stack)
{
    using namespace lynx::viewstack;
    Stack s;
    const View wide { 0.0, 0.05, 0.0 }, sel { 3840.0, 0.4, 120.0 }, other { 1000.0, 0.1, 0.0 };
    // the first press zooms to the selection, the same key again goes back
    CHECK (same (s.toggle (wide, sel), sel) && s.depth() == 1);
    CHECK (same (s.toggle (sel, sel), wide) && s.depth() == 0);
    // the view moved in between (a scroll): the key zooms to the selection again (pushing the moved view)
    s.toggle (wide, sel);
    CHECK (same (s.toggle (other, sel), sel) && s.depth() == 2);
    // "Previous view" goes back whatever the view is now
    CHECK (same (*s.back(), other) && same (*s.back(), wide) && ! s.back().has_value());
    // bounded
    for (int i = 0; i < 40; ++i)
        s.toggle (View { (double) i, 1.0, 0.0 }, sel);
    CHECK (s.depth() == Stack::maxDepth);
}

TEST (feel190qe_glide)
{
    using namespace lynx;
    using feel190qe::near;
    // ease-out: 0 → 1, faster at the start
    CHECK (glide::easeOut (0.0) == 0.0 && glide::easeOut (1.0) == 1.0 && glide::easeOut (2.0) == 1.0);
    CHECK (glide::easeOut (0.25) > 0.25 && glide::easeOut (0.5) > 0.5);
    for (double t = 0.0; t < 1.0; t += 0.05)
        CHECK (glide::easeOut (t + 0.05) >= glide::easeOut (t));
    CHECK (glide::zoomMs == 150.0);
    // a zoom glide: both visible ends in a straight line; the centre of a centred zoom stays the centre
    const double w = 1000.0;
    const viewstack::View a { 1000.0, 0.5, 0.0 }; // ticks 1000..3000
    const viewstack::View b { 1500.0, 1.0, 40.0 }; // ticks 1500..2500
    glide::Glide g;
    g.start (a, b, 0.0, w);
    CHECK (g.running && ! g.done (100.0) && g.done (150.0));
    for (double t = 0.0; t <= 150.0; t += 10.0)
    {
        const auto v = g.at (t);
        CHECK (near (v.scroll + 0.5 * w / v.pixelsPerUnit, 2000.0, 1e-6)); // (the centre tick)
        CHECK (v.pixelsPerUnit >= 0.5 - 1e-9 && v.pixelsPerUnit <= 1.0 + 1e-9);
    }
    CHECK (viewstack::same (g.at (150.0), b) && viewstack::same (g.at (0.0), a));
    CHECK (near (g.at (75.0).scrollY, 40.0 * glide::easeOut (0.5), 1e-9));
    // nothing to glide: not running, the target at once
    g.start (b, b, 0.0, w);
    CHECK (! g.running && g.done (0.0));
}

TEST (feel190qe_zoom_anchor)
{
    using namespace lynx;
    using zoomanchor::Source;
    const double w = 1200.0;
    // the wheel and pinch: the pointer, always
    CHECK (zoomanchor::anchorPx (Source::pointer, 333.0, w, true, 100.0, 700.0) == 333.0);
    // keys: the playhead while playing and on screen
    CHECK (zoomanchor::anchorPx (Source::keys, 333.0, w, true, 100.0, 700.0) == 100.0);
    // not playing, or the playhead off screen: the selection's centre on screen
    CHECK (zoomanchor::anchorPx (Source::keys, 333.0, w, false, 100.0, 700.0) == 700.0);
    CHECK (zoomanchor::anchorPx (Source::keys, 333.0, w, true, -50.0, 700.0) == 700.0);
    CHECK (zoomanchor::anchorPx (Source::keys, 333.0, w, true, 1500.0, std::nullopt) == 600.0);
    // else the view's centre (a selection off screen too)
    CHECK (zoomanchor::anchorPx (Source::keys, 333.0, w, false, std::nullopt, std::nullopt) == 600.0);
    CHECK (zoomanchor::anchorPx (Source::keys, 333.0, w, false, std::nullopt, -400.0) == 600.0);
    // the anchor moves < 1 px across a zoom step (in and out, every anchor), as gesture::zoomAnchored keeps it
    for (double anchor : { 0.0, 100.0, 600.0, 1199.0 })
        for (double factor : { 1.25, 1.0 / 1.25, 2.0, 0.5 })
        {
            const double scroll = 96000.0, ppu = 0.05;
            const auto unit = scroll + anchor / ppu;
            const auto newPpu = ppu * factor;
            const auto s2 = zoomanchor::scrollFor (scroll, ppu, newPpu, anchor);
            CHECK (std::abs ((unit - s2) * newPpu - anchor) < 1.0);
            CHECK (std::abs (s2 - gesture::zoomAnchored (scroll, ppu, newPpu, anchor)) < 1e-9);
        }
    // never before the song's start
    CHECK (zoomanchor::scrollFor (10.0, 1.0, 0.1, 600.0) == 0.0);
}

TEST (feel190qe_selection_history)
{
    using H = lynx::selhistory::History<int>;
    H h;
    h.changed ({}, { 1, 2 });         // (nothing was selected: nothing to remember)
    CHECK (h.size() == 0);
    h.changed ({ 1, 2 }, { 1, 2 });  // (no change)
    CHECK (h.size() == 0);
    h.changed ({ 1, 2 }, {});        // a click deselected
    h.changed ({ 5 }, { 7 });
    CHECK (h.size() == 2);
    // newest first, and never the selection that is there now
    CHECK (*h.restore ({ 7 }) == std::vector<int> ({ 5 }));
    CHECK (*h.restore ({ 5 }) == std::vector<int> ({ 1, 2 }));
    CHECK (! h.restore ({ 1, 2 }).has_value());
    // the last 8 only
    for (int i = 0; i < 20; ++i)
        h.changed ({ i }, { i + 100 });
    CHECK (h.size() == H::keep);
    CHECK (*h.restore ({}) == std::vector<int> ({ 19 }));
    // the editor applying a restored selection does not remember it again
    h.restoring = true;
    h.changed ({ 42 }, { 43 });
    h.restoring = false;
    CHECK (h.size() == H::keep - 1);
}

TEST (feel190qe_keep_labels)
{
    using namespace lynx::keeplabels;
    const auto saved = on;
    CHECK (on); // (FL's default)
    // a visible start: the label at the clip
    auto p = place (300.0, 600.0, 120.0);
    CHECK (! p.held && p.left == 300.0 && p.width == 300.0);
    // the start scrolled off: held at the view's left edge, as wide as what is still in view
    p = place (40.0, 600.0, 120.0);
    CHECK (p.held && p.left == 120.0 && p.width == 480.0);
    // the clip ending just after the edge: a short box (the label shortened to it)
    p = place (-900.0, 150.0, 120.0);
    CHECK (p.held && p.width == 30.0);
    // the whole clip off to the left: nothing held
    p = place (-900.0, 110.0, 120.0);
    CHECK (! p.held);
    // the option off: the label goes with the clip's start (FL's off)
    p = place (40.0, 600.0, 120.0, false);
    CHECK (! p.held && p.left == 40.0);
    CHECK (inset == 3.0);
    on = saved;
}
