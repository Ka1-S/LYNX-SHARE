// Round 183b slice 5: Atrium (the ValhallaRoom role), measured black-box on the reference (tp172d's settings and 13
// new captures with the impulse at −20 dB; the reference is linear). Its level barely moves from early only to late
// only, holds over Decay and over Late size.
TEST (atrium_level_laws_verb183b)
{
    using namespace verbs100;
    using lynx::EffectKind;
    auto total = [] (lynx::EffectParams p) { const auto s = impulse (EffectKind::atrium, p, 12.0); return energyDb (s, 0, s.l.size()); };
    auto p = defaults (EffectKind::atrium);
    p[0] = 1.0f;
    p[4] = 0.0f;
    const auto early = total (p);
    p[4] = 0.5f;
    const auto mid = total (p);
    p[4] = 1.0f;
    const auto late = total (p);
    // the reference: −28.7 / −27.2 / −26.1 dB (2.6 dB from early only to late only; the old linear fade moved 10.7)
    CHECK (std::abs (late - early) < 5.0 && mid > early - 1.0 && mid < late + 1.0);
    // Decay 2.1 s → 8.6 s: the reference −27.2 → −26.8 dB
    p[4] = 0.5f;
    p[2] = lynx::verbs::atrium::decayValue (2.1);
    const auto shortRoom = total (p);
    p[2] = lynx::verbs::atrium::decayValue (8.6);
    const auto longRoom = total (p);
    CHECK (std::abs (longRoom - shortRoom) < 2.5);
    // Late size 0 / 100 %: the reference holds its level (−26.9 / −27.2 dB)
    p = defaults (EffectKind::atrium);
    p[0] = 1.0f;
    p[5] = 0.0f;
    const auto small = total (p);
    p[5] = 1.0f;
    const auto big = total (p);
    CHECK (std::abs (big - small) < 2.0);
}
