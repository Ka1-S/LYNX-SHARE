#pragma once

// Channel arpeggiator (like the one in FL's Channel settings): takes the notes being
// held on the channel and plays one at a time, on the transport grid. No allocation: state
// and fixed-size buffers, created outside the audio thread.

#include <array>

#include "core/Scheduler.h"
#include "core/Song.h"

namespace lynx
{
class Arpeggiator
{
public:
    static constexpr int maxHeld = 32;
    static constexpr int maxEvents = 512;

    // Processes a span: `in` are the channel events (on/off) with offsets within the span; the output
    // (arp on/off, plus everything that isn't on/off) goes to `out`, in order.
    void process (int channel, const NoteEvent* in, int inCount, double startTick, double tps, int numSamples,
                  const ArpSettings& settings, NoteEvent* out, int& outCount) noexcept;

    void reset() noexcept;

    std::array<NoteEvent, maxEvents> input {}, output {};

private:
    // what an arpeggiated note takes from the held note it plays (issue #63: the note's pan, fine pitch, Mod X / Y,
    // colour and level, as FL's arpeggiator keeps them; the velocity was already kept)
    struct Expression
    {
        float pan = 0.0f, cents = 0.0f, modX = 0.5f, modY = 0.5f, gain = 1.0f;
        std::int8_t colour = 0;
    };
    static Expression expressionOf (const NoteEvent& e) noexcept { return { e.pan, e.cents, e.modX, e.modY, e.gain, e.colour }; }
    static void apply (const Expression& x, NoteEvent& e) noexcept
    {
        e.pan = x.pan, e.cents = x.cents, e.modX = x.modX, e.modY = x.modY, e.gain = x.gain, e.colour = x.colour;
    }

    void hold (const NoteEvent& on) noexcept;
    void release (int key) noexcept;
    int sequenceLength (const ArpSettings& settings) const noexcept;
    int keyAt (const ArpSettings& settings, int index) const noexcept; // a position of the sequence → its key
    int nextIndex (const ArpSettings& settings) noexcept; // within the sequence

    std::array<int, maxHeld> heldKeys {};
    std::array<float, maxHeld> heldVelocity {};
    std::array<Expression, maxHeld> heldExpression {};
    int heldCount = 0;
    int step = 0;          // position in the sequence
    int repeatCount = 0;
    int sounding = -1;     // key currently sounding (-1 = none)
    // FL's Repeat 0 (round 149): each step plays the whole chord; the keys sounding together (sounding = the first)
    std::array<int, maxHeld> soundingKeys {};
    int soundingCount = 0;
    double offTick = -1.0; // when the sounding key is released (gate)
    double nextStep = -1.0; // FL: the steps count from the first held note (−1 = start at the next note)
    bool sliding = false;   // a slide-mode note is sounding (the next steps glide it)
    std::uint32_t random = 0x2545F491u;
};
} // namespace lynx
