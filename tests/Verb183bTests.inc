// Round 183b: Horizon (the ValhallaFutureVerb role), measured black-box on the reference (refcap verb183b: 62 extra
// renders of every control, the impulse at 0 and −20 dB, music at 0 and −20 dB) and read against its manual.
// Laws pinned here: the echo is the reverb's pre-delay (Echo → Reverb sends the echo, not the dry input, and Level
// only sets the echo heard), Low EQ cuts once, High EQ sets the high decay, Nonlin's length follows Size, the mode
// steps switch at the reference's (k + 1) / 24 points, and loud programme is compressed gently.
#include "core/Biquad.h"

namespace verb183b
{
using lynx::EffectKind;

inline lynx::EffectParams horizon()
{
    auto p = verbs100::defaults (EffectKind::horizon);
    p[0] = 1.0f; // 100 % wet
    return p;
}

// power (dB) of one channel of `x` at `hz`, averaged over ±5 Hz (a plain DFT over the given span)
inline double powerAt (const std::vector<float>& x, double hz, std::size_t from, std::size_t to, double sr = 48000.0)
{
    double sum = 0.0;
    for (double f = hz - 5.0; f <= hz + 5.0; f += 1.0)
    {
        double re = 0.0, im = 0.0;
        for (std::size_t i = from; i < std::min (to, x.size()); ++i)
        {
            const auto w = 2.0 * 3.14159265358979 * f * (double) i / sr;
            re += x[i] * std::cos (w);
            im -= x[i] * std::sin (w);
        }
        sum += re * re + im * im;
    }
    return 10.0 * std::log10 (sum / 11.0 + 1e-30);
}

// energy (dB) of the response above 6 kHz in [from, to) samples
inline double highBandDb (const verbs100::Stereo& s, std::size_t from, std::size_t to)
{
    lynx::Biquad hp;
    hp.filter (48000.0, 1, 6000.0, 0.7071);
    auto l = s.l, r = s.r;
    hp.processBlock (l.data(), r.data(), (int) l.size());
    double e = 0.0;
    for (std::size_t i = from; i < std::min (to, l.size()); ++i)
        e += (double) l[i] * l[i] + (double) r[i] * r[i];
    return 10.0 * std::log10 (e + 1e-30);
}
} // namespace verb183b

TEST (horizon_echo_is_the_predelay_verb183b)
{
    using namespace verbs100;
    // the default echo (30 ms, Level 0, Feedback 0, Echo → Reverb) delays the whole reverb by its time
    auto p = verb183b::horizon();
    const auto withEcho = (double) onset (impulse (EffectKind::horizon, p, 0.5)) / 48.0;
    p[4] = 0.0f; // Echo time 0 ms
    const auto noEcho = (double) onset (impulse (EffectKind::horizon, p, 0.5)) / 48.0;
    CHECK (std::abs (withEcho - noEcho - 30.0) < 2.0);
    // Chamber's own onset after the echo: the reference's first arrival is 13 ms behind it (±5)
    CHECK (noEcho > 3.0 && noEcho < 20.0);
    // Level sets only the echo heard: a 300 ms echo at Level 0 is silent before the reverb starts; at Level 100 % it is
    // there at 300 ms
    for (float v = 0.0f; v <= 1.0f; v += 0.0005f)
        if (lynx::verbs::horizon::echoMs (v) >= 300.0)
        {
            p[4] = v;
            break;
        }
    p[9] = 0.0f;
    const auto quiet = energyDb (impulse (EffectKind::horizon, p, 0.5), 14160, 14760); // 295 … 307.5 ms
    p[9] = 1.0f;
    const auto heard = energyDb (impulse (EffectKind::horizon, p, 0.5), 14160, 14760);
    CHECK (quiet < -60.0);
    CHECK (heard > -20.0);
}

TEST (horizon_eq_laws_verb183b)
{
    using namespace verbs100;
    // Low EQ is one −6 dB/oct high-pass on the reverb's path (Echo → Reverb): 100 Hz → 400 Hz takes ~11.7 dB at 30 Hz
    // (the reference: −11.5 dB; cut twice it would be −23)
    auto p = verb183b::horizon();
    const auto at100 = impulse (EffectKind::horizon, p, 2.0);
    p[19] = 0.6f; // ~409 Hz
    const auto at400 = impulse (EffectKind::horizon, p, 2.0);
    const auto d = verb183b::powerAt (at400.l, 30.0, 0, 96000) - verb183b::powerAt (at100.l, 30.0, 0, 96000);
    CHECK (d < -8.0 && d > -16.0);
    // High EQ sets the high decay: from 1 kHz to 20 kHz the highs of the late tail (1 … 2 s against 0.1 … 0.5 s) gain
    // well over 10 dB (the reference: RT(8 kHz) 1.4 s → 3.0 s at Decay 3 s)
    p = verb183b::horizon();
    p[20] = 0.0f; // 1 kHz
    const auto dark = impulse (EffectKind::horizon, p, 2.0);
    p[20] = 1.0f; // 20 kHz
    const auto bright = impulse (EffectKind::horizon, p, 2.0);
    const auto fall = [] (const Stereo& s) { return verb183b::highBandDb (s, 48000, 96000) - verb183b::highBandDb (s, 4800, 24000); };
    CHECK (fall (bright) - fall (dark) > 10.0);
}

TEST (horizon_nonlin_length_follows_size_verb183b)
{
    using namespace verbs100;
    // the reference's Nonlin gates after ~0.33 s at Size 50 % and ~1.05 s at Size 100 %, whatever the Decay
    auto p = verb183b::horizon();
    p[22] = 1.0f; // the eighth mode (the reference's Nonlin)
    const auto mid = impulse (EffectKind::horizon, p, 2.0);
    p[12] = 1.0f;
    const auto big = impulse (EffectKind::horizon, p, 2.0);
    // (the echo's 30 ms pre-delay comes first)
    CHECK (energyDb (mid, (std::size_t) (0.6 * 48000), 96000) - energyDb (mid, 0, 96000) < -30.0);
    CHECK (energyDb (big, (std::size_t) (0.6 * 48000), (std::size_t) (0.9 * 48000)) - energyDb (big, 0, 96000) > -12.0);
}

TEST (horizon_mode_steps_and_loud_programme_verb183b)
{
    // the reference's reverb modes sit at (k + 1) / 24 and switch exactly there: 0.0833 still plays Room (measured)
    auto translate = [] (const std::string& element) {
        lynx::ForeignPlugin fp;
        fp.format = lynx::ForeignPlugin::Format::vst3;
        fp.name = "ValhallaFutureVerb";
        fp.path = "C:\\Program Files\\Common Files\\VST3\\ValhallaFutureVerb.vst3";
        fp.state = substtest::flVst3State (substtest::juceXmlComponent (element));
        const auto id = lynx::pluginIdentity (fp);
        const auto* entry = lynx::findPluginSubstitute (id, false);
        int translated = -1;
        return entry != nullptr ? lynx::verbs::choice (lynx::makeSubstituteSlot (*entry, &id, &translated).params[22], 8) : -1;
    };
    CHECK (translate ("<ValhallaFutureVerb ReverbMode=\"0.0833\"/>") == 0);
    CHECK (translate ("<ValhallaFutureVerb ReverbMode=\"0.0833333358168602\"/>") == 1);
    CHECK (translate ("<ValhallaFutureVerb ReverbMode=\"0.2916666567325592\"/>") == 6);
    CHECK (translate ("<ValhallaFutureVerb ReverbMode=\"0.3333333432674408\"/>") == 7);

    // loud programme is compressed gently: a 1 kHz tone at −6 dBFS comes out ~4 dB below the same tone at −40 dBFS
    // scaled up (the reference: 1 … 2 dB on music at −15 dBFS RMS, more on a loud burst)
    auto wetDb = [] (float amp) {
        auto fx = lynx::makeEffect (lynx::EffectKind::horizon);
        fx->prepare (48000.0);
        auto p = verb183b::horizon();
        const int n = 96000;
        std::vector<float> l ((std::size_t) n), r;
        for (int i = 0; i < n; ++i)
            l[(std::size_t) i] = amp * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / 48000.0);
        r = l;
        for (int i = 0; i < n; i += 512)
            fx->process (l.data() + i, r.data() + i, std::min (512, n - i), p, 120.0);
        double e = 0.0;
        for (int i = 48000; i < n; ++i)
            e += (double) l[(std::size_t) i] * l[(std::size_t) i];
        return 10.0 * std::log10 (e + 1e-30) - 20.0 * std::log10 (amp);
    };
    const auto squeeze = wetDb (0.01f) - wetDb (0.5f);
    CHECK (squeeze > 2.5 && squeeze < 6.0);
}
