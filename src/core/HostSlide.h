#pragma once

// FL's slide notes on a native generator (issue #14). A slide note does not start a voice: FL's host moves the pitch
// of the channel's sounding voices to the slide note's pitch, linearly in cents, over the slide note's length; each
// voice keeps its key (its own note off still ends it) and stays at the new pitch afterwards. One HostSlide per voice:
// the engine starts it from Instrument::slideTo and adds `cents` to the voice's pitch while it renders.
//
// [INFERENCE, REA_rodada166a §4] FL's voice model (the host owns each voice's Final pitch, the generator reads it every
// tick) and FL 2026's what's new (16250 "MiniSynth: piano roll slides don't work", 18004 / 17811 "Repeated notes don't
// slide correctly"): slides are a host feature every native generator follows. LYNX's sampler and synth glide the same way.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>

namespace lynx
{
struct HostSlide
{
    float cents = 0.0f;  // how far the voice has moved from its note's pitch
    float target = 0.0f; // where it is going (cents from its note's pitch)
    float left = 0.0f;   // samples still to go
    float step = 0.0f;   // cents per sample
    // FL's channel PITCH knob and main pitch, live (round 167a): a sounding voice follows them. `liveRef` is what the
    // voice took at its note on (knob + fine tune + main pitch, cents; NaN = not tracked), `liveApplied` how far the
    // voice has moved since, `jump` a move the next advance() applies at once.
    float liveRef = NAN;
    float liveApplied = 0.0f;
    float jump = 0.0f;

    // FL's channel porta (round 168l, ChannelPoly.h): the rack starts a note `fromCents` away from its pitch (the note's
    // cents already hold it) and sets this around the engine's noteOn; the voice that note starts glides back to its own
    // pitch over `samples` (host samples). Audio thread only: one per thread, set and cleared by the rack.
    struct GlideIn
    {
        float fromCents = 0.0f;
        float samples = 0.0f;
        bool taken = false; // (a voice took it: tests check every engine that says it follows the channel porta)
    };
    static GlideIn& pendingGlide() noexcept
    {
        static thread_local GlideIn glide;
        return glide;
    }

    // FL's slide notes step (round 190jv, REA_rodada190hg §1.1, REA_rodada190jv §3): on FL's native generators the host
    // moves a sounding voice's Final pitch once per tick of the project (PPQ 96), in n + 1 equal steps for a slide note
    // n ticks long — the first one with the slide's start, the last one at its end — not in a straight glide. The rack
    // sets this around Instrument::slideTo for a slide note on an FL native generator (n ticks, one tick as a fraction of
    // the slide's length) and splits its render calls on those ticks; start() then steps instead of gliding. A porta
    // glide and every other engine keep the straight glide.
    struct Steps
    {
        int ticks = 0;         // n (0: glide)
        float fraction = 0.0f; // one tick / the slide's length
    };
    static Steps& pendingSteps() noexcept
    {
        static thread_local Steps steps;
        return steps;
    }

    // FL's slide note over a chord or over notes of other colours (round 190pi, Song::slideNoteProps; FL 2026 /R renders of
    // Sampler, 3x Osc, DirectWave, SimSynth, Harmless, Harmor, MiniSynth and Sytrus, REA_rodada190pi §2): the host moves the
    // held notes of the slide note's colour only, each by the same interval — from the highest of them to the slide note's
    // key —, not every voice to the slide note's key. The rack sets this around Instrument::slideTo when that is not the
    // plain case (one held key, of the slide's colour); start() then takes `cents` (the move from the voice's own note
    // pitch) for a voice whose key is in `keys` and leaves the others as they are. The voice's key is read back from the
    // engine's own `toCents` (the slide note's key minus it, in semitones). Audio thread only, set and cleared by the rack.
    struct Shift
    {
        bool on = false;
        float cents = 0.0f;
        int slideKey = 60;
        std::array<std::uint64_t, 2> keys {};
        bool moves (int key) const noexcept { return key >= 0 && key < 128 && ((keys[(std::size_t) (key >> 6)] >> (key & 63)) & 1u) != 0; }
    };
    static Shift& pendingShift() noexcept
    {
        static thread_local Shift shift;
        return shift;
    }

    // the note on: the channel's pitch the voice was started with (knob + fine tune + main pitch, cents); `rate` =
    // the engine's samples per host sample (an engine that runs at its own rate)
    void anchor (float ref, float rate = 1.0f) noexcept
    {
        liveRef = ref;
        liveApplied = 0.0f;
        if (auto& g = pendingGlide(); g.samples > 0.0f)
        {
            jump = 0.0f;
            cents = 0.0f;
            start (-g.fromCents, g.samples * rate);
            g.taken = true;
        }
    }
    // the channel's pitch now: the voice jumps to it (applied at the next advance())
    void follow (float now) noexcept
    {
        if (std::isnan (liveRef) || ! std::isfinite (now))
            return;
        if (const auto want = now - liveRef; want != liveApplied)
        {
            jump += want - liveApplied;
            liveApplied = want;
        }
    }

    // glide from where the voice is now to `toCents` (from its note's pitch) over `samples`
    // (`toCents` already holds the channel's pitch now: a pending live jump moves where the glide starts, not its end)
    void start (float toCents, float samples) noexcept
    {
        if (const auto& shift = pendingShift(); shift.on) // (a chord, or other colours: round 190pi)
        {
            if (! shift.moves (shift.slideKey - (int) std::lround (toCents / 100.0f)))
                return; // (a note of another colour: it keeps its pitch, and a glide in progress goes on)
            toCents = shift.cents + (std::isnan (liveRef) ? 0.0f : liveApplied);
        }
        target = toCents - jump;
        left = std::max (1.0f, samples);
        if (const auto& s = pendingSteps(); s.ticks > 0) // (FL's stepped slide note)
        {
            stepsLeft = s.ticks + 1;
            stepSize = (target - cents) / (float) stepsLeft;
            stepEvery = (double) left * (double) s.fraction;
            stepDue = 0.0; // (the first step at once: at the next advance, even advance (0))
            step = 0.0f;
            return;
        }
        stepsLeft = 0;
        step = (target - cents) / left;
    }
    // `n` samples went by: the new offset (cents)
    float advance (float n) noexcept
    {
        if (jump != 0.0f) // (a live knob move: at once, a glide in progress keeps its length and moves with it)
        {
            cents += jump;
            target += jump;
            jump = 0.0f;
        }
        if (left <= 0.0f)
            return cents;
        if (stepsLeft > 0) // (FL's steps: each one at its tick; a step due within a hair of the end of `n` counts)
        {
            auto remaining = (double) n;
            const auto tolerance = 1.0e-3 * stepEvery + 1.0e-6;
            while (stepsLeft > 0 && remaining + tolerance >= stepDue)
            {
                remaining -= stepDue;
                stepDue = stepEvery;
                cents += stepSize;
                --stepsLeft;
            }
            if (stepsLeft == 0)
            {
                cents = target;
                left = 0.0f;
            }
            else
                stepDue -= remaining;
            return cents;
        }
        if (n >= left)
        {
            cents = target;
            left = 0.0f;
        }
        else
        {
            cents += step * n;
            left -= n;
        }
        return cents;
    }
    // For an engine that moves the pitch before it renders `n` samples (it reads the offset for them first): a straight
    // glide as advance (n) always did; a stepped slide gives the offset at the start of those samples (the steps due by
    // now) and keeps the ones due inside them for the next call, which the rack starts on the tick.
    float advanceFirst (float n) noexcept
    {
        if (stepsLeft <= 0)
            return advance (n);
        const auto now = advance (0.0f);
        if (stepsLeft > 0)
            stepDue -= (double) n;
        return now;
    }
    bool moving() const noexcept { return left > 0.0f || jump != 0.0f; }
    bool stepping() const noexcept { return stepsLeft > 0; }

    int stepsLeft = 0;     // steps of a stepped slide still to come
    float stepSize = 0.0f; // cents each
    double stepEvery = 0.0, stepDue = 0.0; // samples between two steps; until the next one
};

// What an engine keeps of Instrument::setChannelPitchNow (round 167a): the channel's pitch now, for its voices' anchor()
// at note on and follow() every render. NaN until the rack has told it (an engine driven alone does not follow).
struct LiveChannelPitch
{
    float knob = NAN, rest = NAN;

    void set (float k, float r) noexcept { knob = k, rest = r; }
    float now() const noexcept { return knob + rest; }
    // a voice started with `knobUsed` (the engine's configured knob) + this block's fine tune and main pitch
    float ref (float knobUsed) const noexcept { return knobUsed + rest; }
};
} // namespace lynx
