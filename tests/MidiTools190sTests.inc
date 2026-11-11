// Round 190s: the Quantize tool of the MIDI Tools panel (core/MidiTools.h). Live 12.4's panel: Grid / 1/4 / 1/8 /
// 1/16 / 1/32, Triplets, Adjust Note Start / End, Amount [LIVE: the controls and defaults; MAN: what they do].

#include "core/MidiTools.h"

namespace mt190s
{
inline lynx::Note at (std::int64_t start, std::int64_t length)
{
    lynx::Note n;
    n.start = start;
    n.length = length;
    return n;
}
} // namespace mt190s

TEST (mt190s_quantize_moves_starts_and_keeps_lengths)
{
    using namespace lynx;
    const auto grid = (std::int64_t) ppq / 4; // 1/16 = 240
    miditools::QuantizeOptions o;             // the defaults: Grid, Start, 100 %
    CHECK (o.step == 0 && o.start && ! o.end && ! o.triplets && o.amount == 1.0f);
    auto out = miditools::quantize ({ mt190s::at (10, 200), mt190s::at (130, 300), mt190s::at (119, 50), mt190s::at (120, 50) }, o, grid);
    CHECK (out.size() == 4);
    CHECK (out[0].start == 0 && out[0].length == 200);
    CHECK (out[1].start == 240 && out[1].length == 300);
    CHECK (out[2].start == 0);   // just under half a step: back
    CHECK (out[3].start == 240); // half a step: forward

    // 50 %: half of the way
    o.amount = 0.5f;
    out = miditools::quantize ({ mt190s::at (100, 200) }, o, grid);
    CHECK (out[0].start == 50 && out[0].length == 200);

    // a fixed 1/4 instead of the grid, then its triplet (two thirds: 640 ticks)
    o = {};
    o.step = ppq;
    out = miditools::quantize ({ mt190s::at (500, 100) }, o, grid);
    CHECK (out[0].start == 960);
    o.triplets = true;
    out = miditools::quantize ({ mt190s::at (500, 100), mt190s::at (1000, 100) }, o, grid);
    CHECK (out[0].start == 640 && out[1].start == 1280);
}

TEST (mt190s_quantize_note_ends)
{
    using namespace lynx;
    const auto grid = (std::int64_t) ppq / 4;
    miditools::QuantizeOptions o;
    o.start = false;
    o.end = true;
    auto out = miditools::quantize ({ mt190s::at (10, 200), mt190s::at (240, 100) }, o, grid);
    CHECK (out[0].start == 10 && out[0].length == 230);  // the end 210 goes to 240
    CHECK (out[1].start == 240 && out[1].length == 1);   // the end 340 goes to 240, the note's own start: one tick stays

    // both: the note moves, then its end goes to a line
    o.start = true;
    out = miditools::quantize ({ mt190s::at (10, 200) }, o, grid);
    CHECK (out[0].start == 0 && out[0].length == 240);
    // an end that would land on the start keeps one tick
    out = miditools::quantize ({ mt190s::at (230, 20) }, o, grid);
    CHECK (out[0].start == 240 && out[0].length >= 1);

    // nothing chosen, or no step: unchanged
    o.start = o.end = false;
    out = miditools::quantize ({ mt190s::at (10, 200) }, o, grid);
    CHECK (out[0].start == 10 && out[0].length == 200);
    out = miditools::quantize ({ mt190s::at (10, 200) }, {}, 0);
    CHECK (out[0].start == 10);
}
