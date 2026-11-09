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
    CHECK (clipops::flCrossfadePair (clips[0], clips[1])); // FL's FUN_00cf7580 now sees a crossfade
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

// --- Round 189o: FL's audio clip menu › Fades › "Crossfade selected" (Tag 100; [DECOMP] FUN_012447a0 -> FUN_00d01bc0
// action 0x15 -> FUN_00d02580 with handle kind 0; shown per FUN_01236e30 / FUN_00cfc9b0) ----
TEST (playlist189o_crossfade_selected_follows_fl)
{
    using namespace lynx;
    // 0: 0..1000, 1: 800..1800 (overlaps 0 by 200), 2: 1700..2300 (overlaps 1 by 100), 3: 5000..5500 (alone)
    std::vector<Clip> clips { audio189n (0, 1000), audio189n (800, 1000), audio189n (1700, 600), audio189n (5000, 500) };
    // shown only when the clicked clip is selected...
    CHECK (! clipops::showCrossfadeSelected (clips, 1, {}));
    CHECK (! clipops::showCrossfadeSelected (clips, 1, { 0, 2 }));
    CHECK (clipops::showCrossfadeSelected (clips, 1, { 0, 1, 2 }));
    // ...and a selected clip has a selected neighbour: alone, or next to unselected clips only, it is hidden
    CHECK (! clipops::showCrossfadeSelected (clips, 3, { 3 }));
    CHECK (! clipops::showCrossfadeSelected (clips, 0, { 0 }));
    // FL looks before first: clip 1's neighbour before is 0 (unselected), so 1 alone doesn't count; 2's (1) does
    CHECK (! clipops::canCreateCrossfade (clips, { 1 }, -1, { 1, 2 }));
    CHECK (clipops::showCrossfadeSelected (clips, 1, { 1, 2 }));
    CHECK (clipops::showCrossfadeSelected (clips, 0, { 0, 1 })); // 0 has none before, 1 after it is selected

    // the action: every selected clip with the clip after it, else the one before it
    CHECK (clipops::createCrossfades (clips, 1, 0, { 0, 1, 2 }) == 3); // pairs 0|1, 1|2 and (2 has none after) 1|2 again
    CHECK (clips[0].fadeOut == 200 && clips[1].fadeIn == 200);
    CHECK (clips[1].fadeOut == 100 && clips[2].fadeIn == 100);
    CHECK (clips[3].fadeIn == 0 && clips[3].fadeOut == 0);
}

// --- Round 189o: FL's General › "Monophonic painting in piano roll" (PRPaintMode, off; [DECOMP] FUN_00e5cd20 picks
// "mono paint" when Shift != the option) ----
#include "core/NoteEdit.h"
TEST (pianoroll189o_monophonic_painting_option_inverts_shift)
{
    using namespace lynx;
    CHECK (! noteedit::monophonicPaint (false, false)); // FL's default: plain strokes layer notes
    CHECK (noteedit::monophonicPaint (true, false));    // Shift paints monophonically
    CHECK (noteedit::monophonicPaint (false, true));    // the option on: plain strokes are monophonic
    CHECK (! noteedit::monophonicPaint (true, true));   // and Shift layers
}

// --- Round 189o: FL's General › "Limit the number of open plugin editors" (LimitEditors, on; [DECOMP] FUN_012baae0:
// past 25 open editors, the oldest by stamp closes) ----
#include "core/EditorLimit.h"
TEST (editors189o_limit_closes_the_oldest_past_25)
{
    using namespace lynx;
    std::vector<std::pair<int, std::uint64_t>> open;
    for (int i = 0; i < 25; ++i)
        open.push_back ({ i, (std::uint64_t) (100 - i) }); // window 24 is the oldest
    CHECK (editorlimit::oldestBeyond (open).empty());
    open.push_back ({ 25, 1000 });
    const auto victims = editorlimit::oldestBeyond (open);
    CHECK (victims.size() == 1 && victims[0] == 24);
    CHECK (editorlimit::oldestBeyond (open, 23).size() == 3);

    // the registry: 26 windows open one after the other, the first closes; touching one keeps it
    editorlimit::entries().clear();
    const auto saved = editorlimit::enabled();
    editorlimit::enabled() = true;
    static int keys[30];
    std::vector<int> closed;
    for (int i = 0; i < 25; ++i)
        editorlimit::opened (&keys[i], [&closed, i] { closed.push_back (i); editorlimit::forget (&keys[i]); });
    CHECK (closed.empty() && editorlimit::entries().size() == 25);
    editorlimit::touch (&keys[0]); // window 0 used again: window 1 is now the oldest
    editorlimit::opened (&keys[25], [&closed] { closed.push_back (25); editorlimit::forget (&keys[25]); });
    CHECK (closed.size() == 1 && closed[0] == 1 && editorlimit::entries().size() == 25);
    // off: no limit
    editorlimit::enabled() = false;
    editorlimit::opened (&keys[26], [&closed] { closed.push_back (26); });
    CHECK (closed.size() == 1 && editorlimit::entries().size() == 26);
    editorlimit::entries().clear();
    editorlimit::enabled() = saved;
}
