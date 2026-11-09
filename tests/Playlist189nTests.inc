// --- Round 189n: FL's Create crossfade (fade handle menu Tag 100) and Normalize / Normalize selection as a group (gain
// handle menu Tags 110 / 111). FLEngine 2026 [DECOMP] FUN_00d02410 / FUN_00d02310 / FUN_00cf9930 / FUN_00cfd680 /
// FUN_00cfaf80 and FUN_00d029b0 / FUN_01064dc0 / FUN_00d026f0; notes in /mnt/e/lynx-work/flgap189n ----

#include "core/ClipOps.h"

namespace
{
lynx::Clip audio189n (std::int64_t start, std::int64_t length, int track = 0)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::audio;
    c.start = start;
    c.length = length;
    c.track = track;
    return c;
}
} // namespace

TEST (playlist189n_crossfade_neighbour_follows_fl)
{
    using namespace lynx;
    // 0: 0..1000, 1: 800..1800 (overlaps 0 by 200), 2: 1800..2400 (touches 1), 3: other track, 4: 3000..3500 (alone)
    std::vector<Clip> clips { audio189n (0, 1000), audio189n (800, 1000), audio189n (1800, 600), audio189n (900, 500, 1), audio189n (3000, 500) };
    CHECK (clipops::crossfadeNeighbour (clips, 1, 0) == 0);
    CHECK (clipops::crossfadeNeighbour (clips, 0, 1) == 1);
    CHECK (clipops::crossfadeNeighbour (clips, 0, 0) == -1);
    // touching without fades: FL's FUN_00cf7580 counts it as a crossfade pair (the menu item is enabled, nothing changes)
    CHECK (clipops::crossfadeNeighbour (clips, 1, 1) == 2 && clipops::crossfadeNeighbour (clips, 2, 0) == 1);
    CHECK (clipops::crossfadeNeighbour (clips, 4, 0) == -1 && clipops::crossfadeNeighbour (clips, 4, 1) == -1);
    CHECK (clipops::crossfadeNeighbour (clips, 3, 0) == -1); // another track never counts
    // a manual fade out on 1 breaks the touching pair
    clipops::setManualFade (clips[1], 1, true);
    CHECK (clipops::crossfadeNeighbour (clips, 1, 1) == -1);
}

TEST (playlist189n_create_crossfade_sets_the_overlap_on_both_sides)
{
    using namespace lynx;
    std::vector<Clip> clips { audio189n (0, 1000), audio189n (800, 1000), audio189n (1800, 600) };
    clips[0].fadeIn = 300;
    clips[1].fadeOut = 100;
    clipops::setManualFade (clips[0], 1, true);
    clipops::setManualFade (clips[1], 0, true);
    clips[0].fadeOut = 50;
    clips[1].fadeIn = 70;

    // clicked clip 1's fade in, not selected: crosses with the clip before it
    CHECK (clipops::createCrossfades (clips, 1, 0, {}) == 1);
    CHECK (clips[0].fadeOut == 200 && clips[1].fadeIn == 200);
    CHECK (! clipops::manualFade (clips[0], 1) && ! clipops::manualFade (clips[1], 0));
    CHECK (clips[0].fadeIn == 300 && clips[1].fadeOut == 100); // the other sides stay
    CHECK (clipops::crossfadedSide (clips, 1, 0) && clipops::crossfadedSide (clips, 0, 1));
    // touching clips (overlap 0): nothing to do
    CHECK (clipops::createCrossfades (clips, 1, 1, {}) == 0 && clips[2].fadeIn == 0);

    // the other fade keeps priority: a's fade in 900 of 1000 leaves 100 for the crossfade's fade out
    std::vector<Clip> tight { audio189n (0, 1000), audio189n (700, 1000) };
    tight[0].fadeIn = 900;
    CHECK (clipops::createCrossfades (tight, 0, 1, {}) == 1);
    CHECK (tight[0].fadeOut == 100 && tight[1].fadeIn == 300);

    // an overlap longer than the shorter clip (a clip inside another): no crossfade
    std::vector<Clip> inside { audio189n (0, 1000), audio189n (200, 300) };
    CHECK (clipops::createCrossfades (inside, 1, 0, {}) == 0 && inside[0].fadeOut == 0 && inside[1].fadeIn == 0);

    // pattern clips get no fades
    std::vector<Clip> mixed { audio189n (0, 1000), audio189n (800, 1000) };
    mixed[0].kind = ClipKind::pattern;
    CHECK (clipops::createCrossfades (mixed, 1, 0, {}) == 0 && mixed[0].fadeOut == 0 && mixed[1].fadeIn == 0);
}

TEST (playlist189n_create_crossfade_on_a_selection)
{
    using namespace lynx;
    // three overlapping clips, all selected: each crosses with the one after it, the last with the one before it
    std::vector<Clip> clips { audio189n (0, 1000), audio189n (900, 1000), audio189n (1700, 1000) };
    const std::vector<int> selection { 0, 1, 2 };
    CHECK (clipops::canCreateCrossfade (clips, selection, 0, selection));
    CHECK (clipops::createCrossfades (clips, 1, 0, selection) == 3);
    CHECK (clips[0].fadeOut == 100 && clips[1].fadeIn == 100 && clips[1].fadeOut == 200 && clips[2].fadeIn == 200);

    // enabled only when the neighbour is as selected as the clip
    std::vector<Clip> two { audio189n (0, 1000), audio189n (900, 1000) };
    CHECK (! clipops::canCreateCrossfade (two, { 1 }, 0, { 1 }));
    CHECK (clipops::canCreateCrossfade (two, { 1 }, 0, {}));
    CHECK (! clipops::canCreateCrossfade (two, { 1 }, 1, {}));
}

TEST (playlist189n_normalize_gains_follow_fl)
{
    using namespace lynx;
    CHECK (std::abs (clipops::normalizeGain (0.5f, 1.0f) - 2.0f) < 1e-6f);
    CHECK (clipops::normalizeGain (0.0f, 0.7f) == 0.7f); // silent: unchanged
    CHECK (clipops::normalizeGain (1.0e-6f, 1.0f) == clipops::normalizeGainMax); // +36 dB at most (LYNX had +24)
    CHECK (clipops::normalizeGain (1.0e6f, 1.0f) == clipops::normalizeGainMin);
    CHECK (std::abs (20.0 * std::log10 (clipops::normalizeGainMax) - 36.0) < 0.001);
    CHECK (std::abs (20.0 * std::log10 (clipops::normalizeGainMin) + 96.1) < 0.01);
    // group: one gain from the loudest clip, no limit
    CHECK (std::abs (clipops::groupNormalizeGain ({ 0.25f, 0.5f, 0.1f }) - 2.0f) < 1e-6f);
    CHECK (clipops::groupNormalizeGain ({ 0.0f, 0.0f }) == 0.0f);
    CHECK (std::abs (clipops::groupNormalizeGain ({ 1.0e-6f }) - 1.0e6f) < 1.0f);
}
