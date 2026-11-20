// Round 190lb: black-box study of licence-protected plug-ins (owner, 2026-11-17: never break the encryption; behaviour
// from outside + public sources; /mnt/e/lynx-work/blackbox190lb/HANDOFF.md, docs/analise/BLACKBOX_rodada190lb.md).
// - SSL 4K G: its default EQ colour "Pink" gets its own measured EQ in Ridge Strip's 4K voicing (hidden k4Pink, with
//   its ÷3 / ×3 switches), and both colours its own filters and 1.1108 output ceiling (hidden k4gLaw). Set only by the 4K G
//   translation: new Ridge Strips and every project saved before keep the old sound bit for bit (lab/gold.sh).
#include <cmath>

namespace parity190lb
{
inline lynx::EffectParams k4g (const std::string& params) { return tp172c::sslSlot ("SSL 4K G", params).params; }
// peak |sample| of a 0.5 impulse through a Ridge Strip with these parameters
inline double impulsePeak (const lynx::EffectParams& p, double sr)
{
    auto s = lynx::makeEffectSlot (lynx::EffectKind::ridgeStrip);
    s.params = p;
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
    l[100] = r[100] = 0.5f;
    for (int off = 0; off < 4096; off += 256)
        fx->process (l.data() + off, r.data() + off, 256, s.params, 120.0);
    double m = 0.0;
    for (const auto v : l)
        m = std::max (m, (double) std::abs (v));
    return m;
}
} // namespace parity190lb

TEST (parity190lb_k4_pink_and_ceiling_are_hidden_and_off_for_new_and_other_strips)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    const auto fresh = makeEffectSlot (EffectKind::ridgeStrip).params;
    CHECK (fresh[r::k4Pink] == 0.0f && fresh[r::k4LmfDiv3] == 0.0f && fresh[r::k4HmfMult3] == 0.0f && fresh[r::k4gLaw] == 0.0f);
    // the hidden ones sit after the named parameters (saved only while they hold a value)
    CHECK ((int) r::k4Pink > (int) r::compAttackFine && (int) r::k4gLaw < maxEffectParams);
    for (const char* other : { "SSL 4K E", "SSL 4K B", "SSL Native Channel Strip 2" })
    {
        const auto p = tp172c::sslSlot (other, "<PARAM id=\"EqType\" value=\"0.0\"/><PARAM id=\"LowEqGain\" value=\"6.0\"/>").params;
        CHECK (p[r::k4Pink] == 0.0f && p[r::k4gLaw] == 0.0f);
    }
}

TEST (parity190lb_ssl_4k_g_pink_state_translates_to_the_pink_colour)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    // EqType 0 = Pink (the original's default): its mid dials before ÷3 / ×3, the switches with them
    auto p = parity190lb::k4g ("<PARAM id=\"EqType\" value=\"0.0\"/><PARAM id=\"LowMidEqFreq\" value=\"1.5\"/><PARAM id=\"LowMidEqDiv3\" value=\"1.0\"/>"
                               "<PARAM id=\"HighMidEqFreq\" value=\"2.0\"/><PARAM id=\"HighMidEqMult3\" value=\"1.0\"/>");
    CHECK (r::k4Voicing (p) && p[r::k4Pink] == 1.0f && p[r::k4gLaw] == 1.0f);
    CHECK (p[r::k4LmfDiv3] == 1.0f && p[r::k4HmfMult3] == 1.0f);
    CHECK (std::abs (r::lmfHz (p[r::lmfFreq]) - 1500.0) < 0.5 && std::abs (r::hmfHz (p[r::hmfFreq]) - 2000.0) < 1.0);
    // no EqType at all: the default, Pink
    CHECK (parity190lb::k4g ("<PARAM id=\"LowEqGain\" value=\"3.0\"/>")[r::k4Pink] == 1.0f);
    // Black: the old path plus the ceiling; ÷3 / ×3 leave the dial (round 190ho, K4G_ir: they do nothing in the G's Black)
    p = parity190lb::k4g ("<PARAM id=\"EqType\" value=\"1.0\"/><PARAM id=\"LowMidEqFreq\" value=\"1.5\"/><PARAM id=\"LowMidEqDiv3\" value=\"1.0\"/>");
    CHECK (p[r::k4Pink] == 0.0f && p[r::k4gLaw] == 1.0f && std::abs (r::lmfHz (p[r::lmfFreq]) - 1500.0) < 0.5);
}

TEST (parity190lb_pink_curves_follow_the_reference_grid)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    const double sr = 48000.0;
    // the reference's LF shelf, 200 Hz +18 dB (K4G_ir PiLFSf2g5): +18.5 at 50 Hz, +13.8 at 200 Hz; its ~1 dB dip after the
    // shelf (-1.2 dB at 3.2 kHz) is beyond one second-order section (the fit's residual): LYNX stays within 1.5 dB there
    auto p = parity190lb::k4g ("<PARAM id=\"EqType\" value=\"0.0\"/><PARAM id=\"LowEqFreq\" value=\"200.0\"/><PARAM id=\"LowEqGain\" value=\"18.0\"/>");
    CHECK (std::abs (r::responseDb (p, sr, 50.0) - 18.5) < 0.8);
    CHECK (std::abs (r::responseDb (p, sr, 200.0) - 13.8) < 0.8);
    CHECK (std::abs (r::responseDb (p, sr, 3200.0) + 1.2) < 1.5);
    // the Bell switch does nothing in Pink (its LF / HF bands are shelves only)
    auto bell = parity190lb::k4g ("<PARAM id=\"EqType\" value=\"0.0\"/><PARAM id=\"LowEqFreq\" value=\"200.0\"/><PARAM id=\"LowEqGain\" value=\"18.0\"/>"
                                  "<PARAM id=\"LowEqBell\" value=\"1.0\"/>");
    CHECK (std::abs (r::responseDb (bell, sr, 50.0) - r::responseDb (p, sr, 50.0)) < 1e-9);
    // LMF 1 kHz, Q 1.5, +22 dB (PiLMFf2q1gM): its peak ~21.2 dB near 1.08 kHz; at -6.6 dB the centre sits lower (~920 Hz)
    p = parity190lb::k4g ("<PARAM id=\"EqType\" value=\"0.0\"/><PARAM id=\"LowMidEqFreq\" value=\"1.0\"/><PARAM id=\"LowMidEqQ\" value=\"1.5\"/>"
                          "<PARAM id=\"LowMidEqGain\" value=\"22.0\"/>");
    CHECK (std::abs (r::responseDb (p, sr, 1080.0) - 21.2) < 0.6);
    p = parity190lb::k4g ("<PARAM id=\"EqType\" value=\"0.0\"/><PARAM id=\"LowMidEqFreq\" value=\"1.0\"/><PARAM id=\"LowMidEqQ\" value=\"1.5\"/>"
                          "<PARAM id=\"LowMidEqGain\" value=\"-6.6\"/>");
    CHECK (r::responseDb (p, sr, 920.0) < r::responseDb (p, sr, 1100.0) - 0.05 && std::abs (r::responseDb (p, sr, 920.0) + 5.8) < 0.5);
    // ÷3: the 1 kHz dial lands at ~262 Hz (PiLMFd3q0)
    p = parity190lb::k4g ("<PARAM id=\"EqType\" value=\"0.0\"/><PARAM id=\"LowMidEqFreq\" value=\"1.0\"/><PARAM id=\"LowMidEqQ\" value=\"3.0\"/>"
                          "<PARAM id=\"LowMidEqGain\" value=\"22.0\"/><PARAM id=\"LowMidEqDiv3\" value=\"1.0\"/>");
    CHECK (r::responseDb (p, sr, 262.0) > 21.0 && r::responseDb (p, sr, 1000.0) < 15.0);
}

TEST (parity190lb_ssl_4k_g_clips_at_its_ceiling)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    // the reference's HF shelf +20 dB at 8 kHz drives a 0.5 impulse to its 1.1108 ceiling (PiHFSf2g5)
    auto p = parity190lb::k4g ("<PARAM id=\"EqType\" value=\"0.0\"/><PARAM id=\"HighEqFreq\" value=\"8.0\"/><PARAM id=\"HighEqGain\" value=\"20.0\"/>"
                               "");
    const auto clipped = parity190lb::impulsePeak (p, 48000.0);
    CHECK (clipped <= 1.1108 + 1e-6 && clipped > 1.1);
    p[r::k4gLaw] = 0.0f;
    CHECK (parity190lb::impulsePeak (p, 48000.0) > 1.2);
}

TEST (parity190lb_d16_repeater_opens_as_echoform_with_the_delay_role_preset)
{
    using namespace lynx;
    // spec-based, unscored: no capture and no saved state of the original exist (its trial expired, its code is
    // PACE-encrypted); the delay role's quarter-note preset, at the role's 25 % mix
    auto slot = p190ca::substituted (p190ca::vst3Named ("Repeater"));
    CHECK (slot.kind == EffectKind::echoform && slot.isSubstitute());
    CHECK (std::abs (slot.params[(std::size_t) p190ca::paramNamed (EffectKind::echoform, "Feedback")] - 0.125f) < 1e-6f);
}

TEST (parity190lb_ssl_4k_g_filters_are_its_own)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    const double sr = 48000.0;
    // HPF dial 146.7 Hz (K4G_ir BlHPF3): -8.0 dB at 100 Hz, -20.6 at 60 Hz, -38.5 at 30 Hz (18 dB/oct)
    auto p = parity190lb::k4g ("<PARAM id=\"EqType\" value=\"1.0\"/><PARAM id=\"HighPassFreq\" value=\"146.7\"/>");
    CHECK (std::abs (r::responseDb (p, sr, 100.0) + 8.0) < 0.5 && std::abs (r::responseDb (p, sr, 60.0) + 20.6) < 0.7);
    CHECK (std::abs (r::responseDb (p, sr, 30.0) + 38.5) < 1.0);
    // LPF dial 3.0 kHz (BlLPF6): -5.4 dB at 4 kHz, -20.1 at 10 kHz, -31.1 at 20 kHz (no cramping toward Nyquist)
    p = parity190lb::k4g ("<PARAM id=\"EqType\" value=\"1.0\"/><PARAM id=\"LowPassFreq\" value=\"3.0\"/>");
    CHECK (std::abs (r::responseDb (p, sr, 4000.0) + 5.4) < 0.5 && std::abs (r::responseDb (p, sr, 10000.0) + 20.1) < 0.7);
    CHECK (std::abs (r::responseDb (p, sr, 20000.0) + 31.1) < 1.5);
    // out: 10 Hz / 35 kHz
    p = parity190lb::k4g ("<PARAM id=\"EqType\" value=\"1.0\"/><PARAM id=\"HighPassFreq\" value=\"10.0\"/><PARAM id=\"LowPassFreq\" value=\"35.0\"/>");
    CHECK (p[r::hpf] == 0.0f && p[r::lpf] == 0.0f && std::abs (r::responseDb (p, sr, 30.0)) < 0.05 && std::abs (r::responseDb (p, sr, 18000.0)) < 0.05);
}

TEST (parity190lb_ssl_4k_e_black_and_orange_high_pass_is_18_db_per_octave)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    const double sr = 48000.0;
    // K4E_ir BlHPF3 (dial 146.7 Hz): -7.8 dB at 100 Hz, -20.2 at 60 Hz; Brown (BrHPF3) stays 12 dB/oct: -2.7 / -9.5
    auto bl = tp172c::sslSlot ("SSL 4K E", "<PARAM id=\"EqColour\" value=\"1.0\"/><PARAM id=\"HighPassFreq\" value=\"146.7\"/>").params;
    CHECK (bl[r::k4eHpLaw] == 1.0f && bl[r::k4gLaw] == 0.0f && bl[r::k4Pink] == 0.0f);
    CHECK (std::abs (r::responseDb (bl, sr, 100.0) + 7.8) < 0.6 && std::abs (r::responseDb (bl, sr, 60.0) + 20.2) < 0.8);
    auto br = tp172c::sslSlot ("SSL 4K E", "<PARAM id=\"EqColour\" value=\"0.0\"/><PARAM id=\"HighPassFreq\" value=\"146.7\"/>").params;
    CHECK (br[r::k4eHpLaw] == 0.0f && r::responseDb (br, sr, 60.0) > -12.0);
    // the same Ridge parameters without the hidden law (a project saved before) keep the old 12 dB/oct curve
    bl[r::k4eHpLaw] = 0.0f;
    CHECK (r::responseDb (bl, sr, 60.0) > -12.0);
}
