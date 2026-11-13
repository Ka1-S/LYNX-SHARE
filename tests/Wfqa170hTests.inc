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
    auto added = ne::duplicateBars (p, { 0, 1 }, lynx::Meter (4, 4));
    CHECK (added.size() == 2);
    CHECK (p.notes[(std::size_t) added[0]].start == bar);
    CHECK (p.notes[(std::size_t) added[1]].start == bar + 6 * step);

    // the four notes now span 1.5 bars (steps 1-24): the next copy starts at bar 3
    added = ne::duplicateBars (p, { 0, 1, 2, 3 }, lynx::Meter (4, 4));
    CHECK (added.size() == 4);
    std::int64_t first = std::int64_t { 1 } << 62;
    for (auto i : added)
        first = std::min (first, p.notes[(std::size_t) i].start);
    CHECK (first == 2 * bar);

    // a riff whose last note ends before the bar line keeps its place in the bar (was: right after the last note)
    lynx::Pattern q;
    q.notes = { { 0, 2 * step, 0, 60 }, { 8 * step, 2 * step, 0, 63 } }; // ends at step 10
    added = ne::duplicateBars (q, { 0, 1 }, lynx::Meter (4, 4));
    CHECK (q.notes[(std::size_t) added[0]].start == bar && q.notes[(std::size_t) added[1]].start == bar + 8 * step);

    // a 3/4 bar is three beats
    lynx::Pattern w;
    w.notes = { { 0, step, 0, 60 } };
    added = ne::duplicateBars (w, { 0 }, lynx::Meter (3, 4));
    CHECK (w.notes[(std::size_t) added[0]].start == (std::int64_t) lynx::ppq * 3);

    // nothing selected: nothing added
    CHECK (ne::duplicateBars (w, {}, lynx::Meter (4, 4)).empty());
}

// Driven LYNX (h-13/h-16): "Sidechain to this track" first, then add the compressor as the box said — the
// compressor's side input stayed "None". Now the added dynamics effect listens to the sole sidechain sender, and
// "Sidechain to this track" also links an existing Fruity Limiter / Fruity Compressor (FL users sidechain with these).
TEST (wfqa170h_sidechain_link_then_add_compressor)
{
    namespace mo = lynx::mixerops;
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (6);

    // the link with nothing to listen yet: a level-0 send 1 -> 5
    CHECK (mo::sidechainTo (song, 1, 5) == 0);
    CHECK (song.routes (1, 5) && mo::soleSidechainSender (song, 5) == 1);

    // the compressor added afterwards listens to Insert 1
    lynx::EffectSlot comp;
    comp.kind = lynx::EffectKind::compressor;
    song.mixer[5].effects.push_back (comp);
    CHECK (mo::linkAddedSidechain (song, 5, 0) && song.mixer[5].effects[0].sidechain == 1);
    // a side input already chosen is kept
    CHECK (! mo::linkAddedSidechain (song, 5, 0));

    // Fruity Limiter added: linked too; a reverb is not
    lynx::EffectSlot lim;
    lim.kind = lynx::EffectKind::fruityLimiter;
    song.mixer[5].effects.push_back (lim);
    CHECK (mo::linkAddedSidechain (song, 5, 1) && song.mixer[5].effects[1].sidechain == 1);
    lynx::EffectSlot rev;
    rev.kind = lynx::EffectKind::reverb;
    song.mixer[5].effects.push_back (rev);
    CHECK (! mo::linkAddedSidechain (song, 5, 2) && song.mixer[5].effects[2].sidechain < 0);

    // a vocoder's side input (its modulator) is not chosen for the user
    lynx::EffectSlot voc;
    voc.kind = lynx::EffectKind::vocoder;
    song.mixer[5].effects.push_back (voc);
    CHECK (! mo::linkAddedSidechain (song, 5, 3));

    // an ordinary send (level > 0) is not a sidechain link; two sidechain senders: ambiguous, nothing linked
    song.setRoute (2, 4, true);
    CHECK (mo::soleSidechainSender (song, 4) == -1);
    CHECK (mo::sidechainTo (song, 3, 5) >= 0 && mo::soleSidechainSender (song, 5) == -1);
    lynx::EffectSlot comp2;
    comp2.kind = lynx::EffectKind::fruityCompressor;
    song.mixer[5].effects.push_back (comp2);
    CHECK (! mo::linkAddedSidechain (song, 5, 4));

    // "Sidechain to this track" links an existing Fruity Limiter / Fruity Compressor
    auto s2 = lynx::Song::makeDefault();
    s2.mixer = lynx::Song::defaultMixer (3);
    s2.mixer[2].effects = { lim, comp2 };
    CHECK (mo::sidechainTo (s2, 1, 2) == 2 && s2.mixer[2].effects[0].sidechain == 1 && s2.mixer[2].effects[1].sidechain == 1);
}
