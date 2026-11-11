// Round 190r: Slicex region keys (FL's trigger list) — read and written as FL does
#include "core/Slicex.h"

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
