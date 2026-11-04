// Round 170h: end-to-end "make a beat" workflow QA (docs/analise/REA_rodada170h.md)

// FL 2026 (driven, snap on Step): Ctrl+B in the Piano roll puts the copy whole bars later, the selection's span
// rounded up to bars, not right after the last note
TEST (wfqa170h_piano_roll_duplicate_goes_whole_bars_later)
{
    namespace ne = lynx::noteedit;
    const auto step = (std::int64_t) lynx::ticksPerStep;
    const auto bar = (std::int64_t) lynx::ppq * 4;

    // two notes over steps 1-8 (C5 at step 1, D5 at step 7, one step each): the copy starts at bar 2
    lynx::Pattern p;
    p.notes = { { 0, step, 0, 60 }, { 6 * step, step, 0, 62 } };
    auto added = ne::duplicateBars (p, { 0, 1 }, 4);
    CHECK (added.size() == 2);
    CHECK (p.notes[(std::size_t) added[0]].start == bar);
    CHECK (p.notes[(std::size_t) added[1]].start == bar + 6 * step);

    // the four notes now span 1.5 bars (steps 1-24): the next copy starts at bar 3
    added = ne::duplicateBars (p, { 0, 1, 2, 3 }, 4);
    CHECK (added.size() == 4);
    std::int64_t first = std::int64_t { 1 } << 62;
    for (auto i : added)
        first = std::min (first, p.notes[(std::size_t) i].start);
    CHECK (first == 2 * bar);

    // a riff whose last note ends before the bar line keeps its place in the bar (was: right after the last note)
    lynx::Pattern q;
    q.notes = { { 0, 2 * step, 0, 60 }, { 8 * step, 2 * step, 0, 63 } }; // ends at step 10
    added = ne::duplicateBars (q, { 0, 1 }, 4);
    CHECK (q.notes[(std::size_t) added[0]].start == bar && q.notes[(std::size_t) added[1]].start == bar + 8 * step);

    // a 3/4 bar is three beats
    lynx::Pattern w;
    w.notes = { { 0, step, 0, 60 } };
    added = ne::duplicateBars (w, { 0 }, 3);
    CHECK (w.notes[(std::size_t) added[0]].start == (std::int64_t) lynx::ppq * 3);

    // nothing selected: nothing added
    CHECK (ne::duplicateBars (w, {}, 4).empty());
}
