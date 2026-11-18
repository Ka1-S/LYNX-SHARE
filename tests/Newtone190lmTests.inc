// Rollup 0.1.30 step 8 (round 190lm, owner decision 2026-11-18): a NEW Newtone slot, which renders with LYNX's natural
// engine (round 190li), is analysed with "Separate close notes" (round 190kx) on. FL imports, older projects and every
// other analysis keep FL's analysis, and their saved states stay byte for byte as before. (Uses ara190kx::sung.)

TEST (rollup030_step8_new_newtone_slot_separates_close_notes)
{
    using namespace lynx;
    // the defaults: FL's analysis for an analysis as such, the fine split for a new slot (natural engine)
    CHECK (! newtonefx::AnalyseOptions {}.fineSplit);
    CHECK (newtonefx::latestEngine >= 1 && newtonefx::newSlotAnalyseOptions().fineSplit);

    // round 190kx's off-tune line (45 cents apart, 20-cent vibrato): FL's analysis joins two notes, a new slot keeps five
    const auto offtune = ara190kx::sung ({ { 60.3, 0.5 }, { 61.75, 0.5 }, { 64.4, 0.5 }, { 64.85, 0.5 }, { 67.2, 1.0 } }, 20.0);
    const auto fl = newtonefx::analyseNotes (offtune);
    const auto fresh = newtonefx::analyseNewSlot (offtune);
    CHECK (ara190kx::voicedNotes (fl) == 4 && ! fl.fineSplit && fl.engine == 0);
    CHECK (ara190kx::voicedNotes (fresh) == 5 && fresh.fineSplit && fresh.engine == newtonefx::latestEngine);
    // the same notes as the explicit fine analysis
    newtonefx::AnalyseOptions fine;
    fine.fineSplit = true;
    const auto explicitFine = newtonefx::analyseNotes (offtune, fine);
    CHECK (explicitFine.pitches.size() == fresh.pitches.size());
    for (std::size_t i = 0; i < fresh.pitches.size() && i < explicitFine.pitches.size(); ++i)
        CHECK (fresh.pitches[i].origStart == explicitFine.pitches[i].origStart && fresh.pitches[i].origPitch == explicitFine.pitches[i].origPitch);

    // saved states: FL's analysis as before (layout 3; with the natural engine layout 4, as round 190li), the new slot's
    // layout 5 with the flag, which comes back; a 0.1.28 copy (layout 2) drops it
    const auto flState = newtonefx::writeNative (fl);
    CHECK (newtonefx::nativeVersionOf (flState) == 3);
    auto flNatural = fl;
    flNatural.engine = newtonefx::latestEngine;
    CHECK (newtonefx::nativeVersionOf (newtonefx::writeNative (flNatural)) == 4);
    const auto freshState = newtonefx::writeNative (fresh);
    CHECK (newtonefx::nativeVersionOf (freshState) == 5);
    const auto back = newtonefx::parseNative (freshState);
    CHECK (back && back->fineSplit && back->engine == newtonefx::latestEngine && back->pitches.size() == fresh.pitches.size());
    const auto old = newtonefx::parseNative (newtonefx::writeNative (fresh, 2u));
    CHECK (old && ! old->fineSplit && old->engine == 0 && old->pitches.size() == fresh.pitches.size());
    // an older state (layouts 3 / 4) reads without the flag: the editor shows FL's analysis, the notes are untouched
    const auto v4 = newtonefx::parseNative (newtonefx::writeNative (flNatural));
    CHECK (v4 && ! v4->fineSplit && v4->pitches.size() == fl.pitches.size());
    // and the fine split itself is not written into FL's layouts: a track written as layout 4 is the same bytes with or
    // without the flag
    auto flagged = flNatural;
    flagged.fineSplit = true;
    CHECK (newtonefx::writeNative (flagged, 4u) == newtonefx::writeNative (flNatural, 4u));
}
