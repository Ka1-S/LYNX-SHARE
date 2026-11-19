// Round details6_190mk (0.1.30, details pass 6): Live's insert marker, the Channel rack's zipped channels, more.

#include "core/InsertMark.h"
#include "core/RackZip.h"

#include <cmath>

// Live's insert marker (capture 190mf B1): cyan -> white -> cyan in 1200 ms, never off; Live's status bar texts
TEST (details190mk_insert_mark_pulse_and_texts)
{
    using namespace lynx::insertmark;
    CHECK (colourAt (0.0) == cyan);
    CHECK (colourAt (600.0) == white);
    CHECK (colourAt (1200.0) == cyan);
    CHECK (colourAt (2400.0) == cyan);
    const auto mid = colourAt (300.0); // halfway: (129,225,234)
    CHECK (((mid >> 16) & 0xff) == 129 && ((mid >> 8) & 0xff) == 225 && (mid & 0xff) == 234);
    CHECK (std::abs (pulse (900.0) - 0.5) < 1.0e-9);
    CHECK (positionText (63, 0, 960, 240) == "63.1.1");
    CHECK (positionText (5, 960 * 2 + 240 * 3, 960, 240) == "5.3.4");
    CHECK (timeText (89.102) == "1:29:102");
    CHECK (timeText (0.0) == "0:00:000");
    CHECK (timeText (600.5) == "10:00:500");
}

// FL's zipped channels (capture 190mf A4): neighbours share one half-row strip, bars side by side
TEST (details190mk_rack_zip_layout)
{
    using namespace lynx::rackzip;
    // rows: 0 normal, 1 + 2 zipped (one strip), 3 normal, 4 zipped alone, add row
    const std::vector<bool> zipped { false, true, true, false, true };
    const std::vector<bool> shown (5, true);
    const auto l = layout (zipped, shown, 30);
    CHECK (l.top[0] == 4 && l.height[0] == 30);
    CHECK (l.top[1] == 34 && l.height[1] == 15 && l.slot[1] == 0);
    CHECK (l.top[2] == 34 && l.slot[2] == 1 && l.first[2] == 1);
    CHECK (l.top[3] == 49 && l.height[3] == 30);
    CHECK (l.top[4] == 79 && l.slot[4] == 0 && l.first[4] == 4);
    CHECK (l.top[5] == 94 && l.total == 94 + 30 - 4);
    CHECK (rowAt (l, 40, 30) == 1);
    CHECK (rowAt (l, 50, 30) == 3);
    CHECK (rowAt (l, 100, 30) == 5);
    CHECK (rowAt (l, 2, 30) == -1);
    // bars: 150 wide from x 100, 14 apart
    CHECK (barAt (l, 1, 100, 100, 150) == 1);
    CHECK (barAt (l, 1, 249, 100, 150) == 1);
    CHECK (barAt (l, 1, 255, 100, 150) == -1); // (the gap)
    CHECK (barAt (l, 1, 264, 100, 150) == 2);
    CHECK (barAt (l, 1, 430, 100, 150) == -1); // (no third bar)
    CHECK (barAt (l, 3, 100, 100, 150) == -1); // (not a strip)
    // a hidden row (filter) between two zipped rows does not split their strip
    const auto h = layout ({ true, false, true }, { true, false, true }, 30);
    CHECK (h.first[2] == 0 && h.slot[2] == 1 && h.top[3] == 4 + 15);
    // no zipped row: FL's 30 px rows, as before
    const auto plain = layout ({ false, false }, { true, true }, 30);
    CHECK (plain.top[1] == 34 && plain.top[2] == 64);
    CHECK (barBottom (0xff9c332c) == 0xff7c130c); // FL's red (156,51,44) -> (124,19,12)
}

#include "core/GrossBeat.h"

// Gross Beat's point clipboard on "Basic 4" (capture 190mk A1.4: P0 0 / level 0, P1 1.5 / -1.5, P2 3 / 0, P3 3.5 / -0.75,
// end 4 / -0.75; level L shows as y = 1 + L / 8)
TEST (details190mk_grossbeat_point_clipboard)
{
    using namespace lynx::grossbeat;
    const std::vector<EditPoint> basic4 { { 0.0, 1.0f, 0.0f, 2 }, { 1.5, 0.8125f, 0.0f, 2 }, { 3.0, 1.0f, 0.0f, 2 },
                                          { 3.5, 0.90625f, 0.0f, 2 }, { 4.0, 0.90625f, 0.0f, 2 } };
    const auto clip = copyPoints (basic4, { 1, 2 });
    CHECK (clip.points.size() == 2 && clip.length() == 1.5 && clip.points[0].x == 0.0);
    // Paste (insert): right after the selection (x 3), later points pushed by 1.5 (past the bar: gone), the end kept
    {
        const auto [pts, sel] = pasteInsert (basic4, { 1, 2 }, clip, 2.0);
        CHECK (pts.size() == 5);
        CHECK (pts[3].x == 3.0 && pts[3].y == 0.8125f);
        CHECK (pts.back().x == 4.0 && pts.back().y == 0.90625f);
        CHECK (sel == std::vector<int> ({ 1, 2, 3 }));
    }
    // Paste (replace) of P2..P3 over P1..P2: at 1.5 and 2.0, old P3 at 2.5 (FL: 973 -> 842 px), the end stays at 4
    {
        const auto [pts, sel] = pasteReplace (basic4, { 1, 2 }, copyPoints (basic4, { 2, 3 }));
        CHECK (pts.size() == 5);
        CHECK (pts[1].x == 1.5 && pts[1].y == 1.0f && pts[2].x == 2.0 && pts[2].y == 0.90625f);
        CHECK (std::abs (pts[3].x - 2.5) < 1.0e-12 && pts[4].x == 4.0);
        CHECK (sel == std::vector<int> ({ 1, 2 }));
    }
    // Duplicate: the copy after the selection, the copy selected
    {
        const auto [pts, sel] = duplicatePoints (basic4, { 1, 2 });
        CHECK (pts[3].x == 3.0 && sel == std::vector<int> ({ 3 }));
    }
    // Cut: the gap closes (P3 onto P0's time), the end stays at the bar's end in LYNX
    {
        const auto pts = cutPoints (basic4, { 1, 2 });
        CHECK (pts.size() == 3 && pts[1].x == 0.0 && pts[1].y == 0.90625f && pts[2].x == 4.0);
    }
    // Prepare for smooth editing: a smooth point per beat, the end at the start's level
    {
        Slot slot;
        setPoints (slot, basic4);
        const auto pts = prepareForSmoothEditing (slot);
        CHECK (pts.size() == 5);
        for (std::size_t i = 0; i < pts.size(); ++i)
            CHECK (pts[i].x == (double) i && pts[i].type == 10);
        CHECK (pts[2].y == 0.8125f && pts[3].y == 1.0f && pts[4].y == pts[0].y);
    }
}

// FL's envelope state file (*.fnv): "Env filter - default" (a triangle over 1.0, stretched to the bar); write / read round trip
TEST (details190mk_grossbeat_envelope_file)
{
    using namespace lynx::grossbeat;
    std::vector<std::uint8_t> fnv;
    const auto put32 = [&fnv] (std::int32_t v) { for (int i = 0; i < 4; ++i) fnv.push_back ((std::uint8_t) ((std::uint32_t) v >> (8 * i))); };
    const auto putD = [&fnv] (double v) { std::uint8_t b[8]; std::memcpy (b, &v, 8); fnv.insert (fnv.end(), b, b + 8); };
    put32 (3); put32 (2); put32 (3);
    const double pts[3][2] = { { 0.0, 0.0 }, { 0.5, 1.0 }, { 0.5, 0.0 } };
    for (const auto& p : pts)
    {
        putD (p[0]); putD (p[1]); put32 (0); put32 (0);
    }
    put32 (0); put32 (-1); put32 (-1); put32 (-1); put32 (-1);
    const auto read = readEnvelopeFile (fnv.data(), fnv.size());
    CHECK (read.has_value() && read->size() == 3);
    if (read)
    {
        CHECK ((*read)[1].x == 2.0 && (*read)[1].y == 1.0f && (*read)[2].x == 4.0 && (*read)[2].y == 0.0f);
        Slot slot;
        setPoints (slot, *read);
        const auto written = writeEnvelopeFile (slot);
        const auto again = readEnvelopeFile (written.data(), written.size());
        CHECK (again.has_value() && again->size() == 3 && (*again)[1].x == 2.0);
    }
    CHECK (! readEnvelopeFile (fnv.data(), 8).has_value());
    // Analyze audio file: loud = top, silence = bottom; dense where it moves, at most the asked number of points
    std::vector<float> peaks (1024, 0.0f);
    for (int i = 0; i < 1024; ++i)
        peaks[(std::size_t) i] = std::exp (-(float) (i % 64) / 10.0f);
    const auto env = envelopeFromLevels (peaks, 110);
    CHECK (env.size() >= 16 && env.size() <= 110 && env.front().x == 0.0 && env.back().x == 4.0);
    CHECK (env.front().y == 1.0f && env.front().type == 0);
}
