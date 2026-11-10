// Round 183b slice 6: Epoch Verb (the ValhallaVintageVerb role), measured black-box on the reference (tp172d's
// settings and 12 new captures with the impulse at −20 dB; the reference is linear). Its first 80 ms hold more of the
// energy at its default 4 s decay than this tank's did (−3.8 dB of the total; ours −7.2); at 2 s round 100's
// −3.1 dB in the first 100 ms still holds (lynxverbs100_onsets_levels_and_stereo).
TEST (epoch_early_share_verb183b)
{
    using namespace verbs100;
    using lynx::EffectKind;
    auto p = defaults (EffectKind::epochVerb);
    p[0] = 1.0f;
    const auto s = impulse (EffectKind::epochVerb, p, 4.0);
    const auto start = onset (s);
    const auto early = energyDb (s, start, start + 3840) - energyDb (s, start, s.l.size());
    CHECK (early > -6.5 && early < -2.0);
}
