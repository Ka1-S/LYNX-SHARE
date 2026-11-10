// Round 190r: Slicex region keys (FL's trigger list) — read and written as FL does
#include "core/Slicex.h"
#include "core/SlicexEngine.h"

// FL's trigger list is a chunk of its own (a project FL saved after "Assign current region to this key"); one inside a
// LIST is not read, as FL ignores it
TEST (slicex190r_region_keys_are_read_and_written_as_fl_does)
{
    using namespace lynx;
    const auto wav = slicex190h::wavWith (1000, { { 1, 0, 0, "Kick", -1 }, { 2, 250, 0, "Snare", -1 }, { 3, 500, 0, "", -1 } });
    std::int64_t frames = 0;
    auto regions = slicex::wavRegions (wav, frames);
    CHECK (regions.size() == 3 && regions[0].key == -1 && regions[1].key == -1);

    const auto keyed = slicex::withRegionKeys (wav, { 61, -1, 61 });
    CHECK (keyed.size() == wav.size() + 8 + 4 + 2 * 24);
    regions = slicex::wavRegions (keyed, frames);
    CHECK (frames == 1000 && regions.size() == 3);
    CHECK (regions[0].key == 61 && regions[1].key == -1 && regions[2].key == 61);
    CHECK (regions[0].name == "Kick" && regions[1].start == 250);
    // the chunk: "tlst", size, count, then {'cue ', id − 1, 1, FF key 90 00, 0, 0} — the bytes FL wrote for one key
    const std::uint8_t expect[] = { 't', 'l', 's', 't', 52, 0, 0, 0, 2, 0, 0, 0, 'c', 'u', 'e', ' ', 0, 0, 0, 0, 1, 0, 0, 0, 0xff, 61, 0x90, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    const auto found = std::search (keyed.begin(), keyed.end(), std::begin (expect), std::end (expect));
    CHECK (found != keyed.end());
    // it sits after LIST/adtl, and the RIFF size follows
    const std::uint8_t adtl[] = { 'a', 'd', 't', 'l' };
    CHECK (std::search (keyed.begin(), keyed.end(), std::begin (adtl), std::end (adtl)) < found);
    CHECK ((std::size_t) (keyed[4] | (keyed[5] << 8) | (keyed[6] << 16) | (keyed[7] << 24)) == keyed.size() - 8);

    // rewriting replaces the list; all keys off removes it and gives the first file back
    const auto again = slicex::withRegionKeys (keyed, { -1, 72, -1 });
    regions = slicex::wavRegions (again, frames);
    CHECK (regions[0].key == -1 && regions[1].key == 72 && regions[2].key == -1);
    CHECK (slicex::withRegionKeys (keyed, { -1, -1, -1 }) == wav);
    CHECK (slicex::withRegionKeys ({ 1, 2, 3 }, { 60 }).empty());

    // the key map: two regions with the same key share the note
    const auto map = slicex::keyMap (0, slicex::wavRegions (keyed, frames), {}, true);
    CHECK (map[61].size() == 2);

    // a trigger list wrapped in a LIST chunk (what round 190l made by hand) is not FL's: no keys
    auto wrapped = wav;
    const std::uint8_t list[] = { 'L', 'I', 'S', 'T', 32, 0, 0, 0, 't', 'l', 's', 't', 1, 0, 0, 0, 'c', 'u', 'e', ' ', 0, 0, 0, 0, 1, 0, 0, 0, 0xff, 61, 0x90, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    wrapped.insert (wrapped.end(), std::begin (list), std::end (list));
    regions = slicex::wavRegions (wrapped, frames);
    CHECK (regions.size() == 3 && regions[0].key == -1);
}

// ---- the editor's helpers (curve shapes for the articulator graph)
TEST (slicex190r_curve_levels_follow_the_points)
{
    using namespace lynx;
    slicex::Curve c;
    c.enabled = -1;
    slicex::Point a, b, d;
    a.y = 0.0;
    b.dt = 1.0;
    b.y = 1.0;
    d.dt = 1.0;
    d.y = 0.5;
    c.points = { a, b, d };
    const auto v = slicex::curveLevels (c, false, 201);
    CHECK (v.size() == 201);
    CHECK (std::abs (v.front() - 0.0f) < 1.0e-4f);
    CHECK (std::abs (v[100] - 1.0f) < 0.02f);  // the second point, half way
    CHECK (std::abs (v[50] - 0.5f) < 0.02f);   // a straight segment (tension 0)
    CHECK (std::abs (v[200] - 0.5f) < 0.02f);
    // a "hold" segment keeps the previous level up to its point
    c.points[1].mode = 2;
    const auto h = slicex::curveLevels (c, false, 201);
    CHECK (std::abs (h[50] - 0.0f) < 1.0e-4f);
    // a map is shown across x = 0…1 whatever its points' spacing
    c.points[1].mode = 0;
    c.points[2].dt = 3.0;
    const auto m = slicex::curveLevels (c, true, 101);
    CHECK (std::abs (m[25] - 1.0f) < 0.03f);
    // one point, or none: a flat line, no crash
    c.points.resize (1);
    CHECK (slicex::curveLevels (c, false, 16).size() == 16);
    c.points.clear();
    CHECK (slicex::curveLevels (c, false, 16).size() == 16);
    CHECK (slicex::curveLevels (c, false, 1).size() == 1);
}

TEST (slicex190r_lfo_cycle_is_one_bipolar_period)
{
    using namespace lynx;
    const auto st = slicex::fresh();
    const auto w = slicex::lfoCycle (st.groups[0].art[slicex::volume], 64);
    CHECK (w.size() == 64);
    float lo = 1.0f, hi = -1.0f;
    for (const auto v : w)
    {
        CHECK (std::isfinite (v) && v >= -1.0001f && v <= 1.0001f);
        lo = std::min (lo, v);
        hi = std::max (hi, v);
    }
    CHECK (hi > 0.9f && lo < -0.9f);
}

