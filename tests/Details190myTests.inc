// Round details10_190my (0.1.30, details pass 10): the Current track's follow-ups and the round's other rows.

// FL's Current (C) track in the delay compensation (DF-82; REA 190mv R: FUN_010ff3b0 / FUN_010ffca0 add C's latency to
// its target's): C's latent slots make the selected track's output that much later, so the other tracks wait for it;
// with the Master (or C) selected the total grows; with no source (a render without C) nothing changes. The selected
// track's fader acts before C (levelLag and the take's effects latency leave C out).
TEST (details190my_current_track_pdc)
{
    using namespace lynx;
    Song s = latency165::impulses (2, false);
    const auto c = mixerops::addCurrentTrack (s);
    CHECK (c == 3);
    s.mixer[(std::size_t) c].effects.push_back (makeEffectSlot (EffectKind::eq));
    const std::vector<std::vector<int>> slots { {}, {}, {}, { 64 } };

    auto plan = computePdc (s, { 0, 0 }, slots, 48000.0); // (no source)
    CHECK (plan.currentLatency == 64 && plan.total == 0 && plan.outLat[1] == 0 && plan.sendDelay[2][0] == 0);

    plan = computePdc (s, { 0, 0 }, slots, 48000.0, 1); // insert 1 selected
    CHECK (plan.outLat[1] == 64 && plan.inAlign[0] == 64 && plan.total == 64);
    CHECK (plan.sendDelay[1][0] == 0 && plan.sendDelay[2][0] == 64);
    CHECK (plan.levelLag[1] == 0 && insertEffectsLatency (plan, s, 1, 48000.0) == 0);
    CHECK (plan.slotInput[(std::size_t) c].size() == 1 && plan.slotInput[(std::size_t) c][0] == 0);

    plan = computePdc (s, { 0, 0 }, slots, 48000.0, 0); // the Master (or C itself) selected
    CHECK (plan.total == 64 && plan.outLat[1] == 0 && plan.sendDelay[2][0] == 0 && plan.sendDelay[1][0] == 0);

    plan = computePdc (s, { 0, 0 }, slots, 48000.0, c); // (never C on itself)
    CHECK (plan.total == 0);

    // bypassed: no latency
    s.mixer[(std::size_t) c].effects[0].enabled = false;
    plan = computePdc (s, { 0, 0 }, slots, 48000.0, 1);
    CHECK (plan.total == 0 && plan.currentLatency == 0);
}
