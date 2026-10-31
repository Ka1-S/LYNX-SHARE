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

    // the note on: the channel's pitch the voice was started with (knob + fine tune + main pitch, cents)
    void anchor (float ref) noexcept
    {
        liveRef = ref;
        liveApplied = 0.0f;
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
        target = toCents - jump;
        left = std::max (1.0f, samples);
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
    bool moving() const noexcept { return left > 0.0f || jump != 0.0f; }
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
