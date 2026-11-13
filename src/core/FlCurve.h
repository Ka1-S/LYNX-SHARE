#pragma once

// FL Studio's envelope / curve point law (round 165, docs/analise/REA_rodada165c-dash165.md §4): the shape of the
// segment that ends at a point, from that point's mode and tension (−1…1). The same library evaluates FL's automation
// clips, plugin envelopes and the X-Y-Z Controller's input / output curves [DECOMP Fruity X-Y-Z Controller_x64.dll
// 0x782750 (preparation), 0x784140 (count), 0x7858b0 (evaluation); REA_rodada23 §1.2, REA_rodada27 §3.2].
// Clean-room: our own code from the decompiled behaviour.

namespace lynx::flcurve
{
enum Mode : int
{
    singleCurve = 0,
    doubleCurve = 1,
    hold = 2,
    stairs = 3,
    smoothStairs = 4,
    pulse = 5,
    wave = 6,
    singleCurve2 = 7,
    doubleCurve2 = 8,
    halfSine = 9,
    smooth = 10, // (needs its neighbours: prepareSmooth; `prepare` alone gives a straight segment)
    singleCurve3 = 11,
    doubleCurve3 = 12,
    modeCount = 13
};

// The segment's prepared form (FL computes it once per point).
struct Segment
{
    int mode = singleCurve;
    int sign = 2;        // −1, 0, +1 (the tension's sign); 2 = straight
    float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f;
};

// FL's step / period count of modes 3…6: round (0.001 (1000001^(1 − 0.6 |t|) − 1)) (+ 1 for stairs with t ≥ 0).
int count (int mode, float tension) noexcept;
Segment prepare (int mode, float tension) noexcept;
// The shape 0..1 at x 0..1 (0 = the previous point's value, 1 = this point's).
double evaluate (const Segment& s, double x) noexcept;
inline double shape (int mode, float tension, double x) noexcept { return evaluate (prepare (mode, tension), x); }

// Mode 10, "Smooth" (round 190bw; the same shared envelope library, REA_rodada51 §2.4): a cubic Hermite segment from
// y0 to y1 over the time dx whose end slopes come from the neighbouring segments, and only from neighbours that are
// "Smooth" themselves. `before`: the segment that ends where this one starts (its length and its start level);
// `after`: the segment that starts where this one ends (its length and its end level). A neighbour that is missing or
// not smooth gives a flat end. The tension pulls the curve back to the straight line (|t| = 1: straight).
struct SmoothSide
{
    bool smooth = false; // that neighbour segment exists and is in Smooth mode
    double dx = 0.0;     // its length (any unit, the same as the segment's)
    double y = 0.0;      // before: the level where it starts; after: the level where it ends
    bool hasY = true;    // before only: false when there is no point before the neighbour's start
};
Segment prepareSmooth (float tension, double dx, double y0, double y1, const SmoothSide& before, const SmoothSide& after) noexcept;

const char* modeName (int mode) noexcept; // "Single curve", "Stairs"…
} // namespace lynx::flcurve
