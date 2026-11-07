// Round 177b: one-click sidechain. The "Sidechain from" picker of an effect makes FL's sidechain link itself: a
// level-0 send when there is no route (the source keeps its Master send), the slot listens; a loop changes nothing.
TEST (sc177b_sidechain_picker_makes_the_route)
{
    namespace mo = lynx::mixerops;
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (5);
    lynx::EffectSlot comp;
    comp.kind = lynx::EffectKind::fruityCompressor;
    song.mixer[3].effects.push_back (comp);

    // Insert 1 (the kick) → slot 0 of Insert 3: a level-0 send, the kick still on the Master
    CHECK (song.mixer[1].toMaster && ! song.routes (1, 3));
    CHECK (mo::sidechainFrom (song, 3, 0, 1) == mo::SidechainPick::linked);
    CHECK (song.routes (1, 3) && song.mixer[1].toMaster && song.mixer[3].effects[0].sidechain == 1);
    CHECK (song.mixer[1].sends.size() == 1 && song.mixer[1].sends[0].level == 0.0f);
    // FL's way sees it as its own sidechain link
    CHECK (mo::soleSidechainSender (song, 3) == 1);

    // an existing route keeps its level
    song.setRoute (2, 3, true, 0.7f);
    CHECK (mo::sidechainFrom (song, 3, 0, 2) == mo::SidechainPick::linked);
    CHECK (song.mixer[3].effects[0].sidechain == 2 && song.mixer[2].sends[0].level == 0.7f);

    // None: the slot listens to nothing, the routing stays
    CHECK (mo::sidechainFrom (song, 3, 0, -1) == mo::SidechainPick::cleared);
    CHECK (song.mixer[3].effects[0].sidechain == -1 && song.routes (1, 3) && song.routes (2, 3));

    // a loop: Insert 3 feeds Insert 4, so 4 cannot sidechain into 3
    song.setRoute (3, 4, true);
    CHECK (mo::sidechainFrom (song, 3, 0, 4) == mo::SidechainPick::cycle);
    CHECK (song.mixer[3].effects[0].sidechain == -1 && ! song.routes (4, 3));

    // the Master's effects need no route; bad slots / itself are invalid
    song.mixer[0].effects.push_back (comp);
    CHECK (mo::sidechainFrom (song, 0, 0, 2) == mo::SidechainPick::linked && song.mixer[0].effects[0].sidechain == 2);
    CHECK (mo::sidechainFrom (song, 3, 5, 1) == mo::SidechainPick::invalid);
    CHECK (mo::sidechainFrom (song, 3, 0, 3) == mo::SidechainPick::invalid);

    // the vocoder's second input (carrier) and its separate-inputs switch
    lynx::EffectSlot voc;
    voc.kind = lynx::EffectKind::vocoder;
    song.mixer[2].effects.push_back (voc);
    CHECK (mo::sidechainFrom (song, 2, 0, 1, true) == mo::SidechainPick::linked);
    CHECK (song.mixer[2].effects[0].sidechain2 == 1 && song.mixer[2].effects[0].sidechain == -1 && song.mixer[2].effects[0].params[16] == 1.0f);
}

// Every engine that reads a side input offers the picker; effects without one do not (no dead control).
TEST (sc177b_accepts_sidechain_matches_the_engines)
{
    namespace mo = lynx::mixerops;
    using K = lynx::EffectKind;
    for (const auto k : { K::compressor, K::fruityLimiter, K::fruityCompressor, K::vocoder, K::vocodex, K::facetEq, K::apexLimiter,
                          K::hushDeEsser, K::crestCompressor, K::prismMultiband, K::sentryGate, K::luxeVerb, K::patcher,
                          K::transmitter, K::flTransporter })
        CHECK (mo::acceptsSidechain (k));
    for (const auto k : { K::reverb, K::delay, K::eq, K::parametricEq, K::plugin })
        CHECK (! mo::acceptsSidechain (k));

}
