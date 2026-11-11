#pragma once

// A pad envelope of FL's FPC (volume or pan) as the plugin plays it (round 190z, docs/analise/REA_rodada190z.md):
// Image-Line's articulator curve (the library Fruity Love Philter and Slicex share, core/FlCurves.inc) with FPC's
// envelope class — the decay, loop and sustain points kept, the ATT / DEC / SUS / REL knobs on the segment times and
// the sustain level, a release that leaves from the level reached, no arpeggio points. The volume envelope's level
// is a gain (0…1), the pan envelope's level is centred (0.5 = centre). FPC steps both once per tick of
// `ticksFor (rate)` samples, counted from the voice's start; a time unit of the points is half a second.
// Clean-room: our own code from the decompiled behaviour.

#include <memory>
#include <vector>

#include "core/Fpc.h"

namespace lynx
{
class FpcEnvelope
{
public:
    FpcEnvelope() = default;
    // `pan`: the pan envelope (its level is read around 0.5)
    FpcEnvelope (const fpc::Envelope& stored, bool pan);

    bool enabled() const noexcept; // the envelope's switch (and a readable curve)

    // FPC's tick at a sample rate: 48 samples at 44.1 kHz, max(4, 4·round(48·rate/44100/4)) else; and the time
    // scale the plugin derives from it (2·tick/rate, computed the plugin's way)
    static int ticksFor (double rate) noexcept;
    static double scaleFor (double rate) noexcept;

    struct State
    {
        int stage = 9, idx = 0, sub = 9;
        double phase = 0.0, rate = 0.0;
        float start = 0.0f, delta = 0.0f, shift = 0.0f, out = 0.0f;
        float sus = 0.0f;                       // the S knob's lift once the decay point is passed (0 without sustain)
        float kA = 1.0f, kD = 1.0f, kS = 0.0f, kR = 1.0f;
        bool hold = false;

        bool finished() const noexcept { return stage > 8; }
    };

    void start (State& s, double scale) const noexcept;
    void step (State& s, double scale, double ticks) const noexcept;
    void release (State& s) const noexcept;

    // for drawing: the points (time after the previous one in FPC's units, level 0…1), the segment's shape (0…1 of
    // the way from the previous point's level, at x 0…1 of the segment), the decay / loop / sustain points (−1 = none)
    int pointCount() const noexcept;
    double pointTime (int i) const noexcept;
    double pointLevel (int i) const noexcept;
    float shape (int i, double x) const noexcept;
    int decayPoint() const noexcept;
    int loopPoint() const noexcept;
    int sustainPoint() const noexcept;
    // the knobs as factors: ATT, DEC, REL on times, SUS on levels (S/128)
    float attack() const noexcept;
    float decay() const noexcept;
    float sustain() const noexcept;
    float releaseTime() const noexcept;

    // The envelope as it plays: its level for each tick from the note's start, the note released after `heldTicks`
    // ticks (< 0: never), until it ends or `maxTicks`. For the editor's drawing.
    std::vector<float> trace (double rate, int heldTicks, int maxTicks) const;

    struct Curve;

private:
    std::shared_ptr<const Curve> curve;
};
} // namespace lynx
