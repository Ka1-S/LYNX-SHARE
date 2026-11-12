// REA_rodada100: LYNX's own reverbs (core/LynxVerbs.h) fitted to black-box measurements of the Valhalla DSP reverbs.
// Tolerances are wide on purpose (decay times ±15–20 %, levels ±1.5 dB): these are measurable behaviours, not
// sample goldens, and they must hold with any compiler.
#include "core/LynxVerbs.h"


namespace verbs100
{
using lynx::EffectKind;

inline lynx::EffectParams defaults (EffectKind kind)
{
    return lynx::makeEffectSlot (kind).params;
}

struct Stereo
{
    std::vector<float> l, r;
};

// the effect's response to a 0.5 impulse on both channels
inline Stereo impulse (EffectKind kind, const lynx::EffectParams& p, double seconds, double sr = 48000.0, int block = 512)
{
    auto fx = lynx::makeEffect (kind);
    fx->prepare (sr);
    const auto n = (std::size_t) (seconds * sr);
    Stereo s { std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f) };
    s.l[0] = s.r[0] = 0.5f;
    for (std::size_t off = 0; off < n; off += (std::size_t) block)
    {
        const auto m = (int) std::min<std::size_t> ((std::size_t) block, n - off);
        fx->process (s.l.data() + off, s.r.data() + off, m, p, 120.0);
    }
    return s;
}

// broadband RT60 from the Schroeder curve of L + R, fitted between −5 and −25 dB (T20); −1 if not reached
inline double rt60 (const Stereo& s, double sr = 48000.0)
{
    const auto n = s.l.size();
    std::vector<double> e (n + 1, 0.0);
    for (std::size_t i = n; i-- > 0;)
    {
        const double x = (double) s.l[i] + (double) s.r[i];
        e[i] = e[i + 1] + x * x;
    }
    if (e[0] <= 0.0)
        return -1.0;
    std::size_t a = 0, b = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto d = 10.0 * std::log10 (std::max (e[i] / e[0], 1e-30));
        if (a == 0 && d <= -5.0)
            a = i;
        if (d <= -25.0)
        {
            b = i;
            break;
        }
    }
    if (a == 0 || b <= a + 10)
        return -1.0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const auto m = (double) (b - a);
    for (std::size_t i = a; i < b; ++i)
    {
        const auto x = (double) i / sr, y = 10.0 * std::log10 (std::max (e[i] / e[0], 1e-30));
        sx += x, sy += y, sxx += x * x, sxy += x * y;
    }
    const auto slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
    return slope < 0.0 ? -60.0 / slope : -1.0;
}

inline double energyDb (const Stereo& s, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (std::size_t i = from; i < std::min (to, s.l.size()); ++i)
        e += (double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i];
    return 10.0 * std::log10 (std::max (e / 0.25, 1e-30));
}

inline std::size_t onset (const Stereo& s)
{
    float peak = 0.0f;
    for (std::size_t i = 0; i < s.l.size(); ++i)
        peak = std::max ({ peak, std::abs (s.l[i]), std::abs (s.r[i]) });
    for (std::size_t i = 0; i < s.l.size(); ++i)
        if (std::max (std::abs (s.l[i]), std::abs (s.r[i])) > peak * 0.01f)
            return i;
    return s.l.size();
}

inline bool near (double v, double want, double rel) { return std::abs (v - want) <= rel * std::abs (want); }
} // namespace verbs100

TEST (lynxverbs100_parameter_laws_follow_the_measured_ones)
{
    using namespace lynx::verbs;
    // [EXEC, REA_rodada100 §0] laws of the reference plugins, read back through set / get of their VST3 parameters
    CHECK (std::abs (epoch::decaySec (0.42048f) - 4.0) < 0.02);   // VintageVerb's default decay "4.00 s"
    CHECK (std::abs (epoch::decaySec (0.5f) - 7.0) < 0.02);
    CHECK (std::abs (epoch::predelayMs (0.25f) - 20.0) < 0.05);
    CHECK (std::abs (epoch::predelayMs (0.5f) - 100.0) < 0.05);
    CHECK (std::abs (foil::decaySec (0.5f) - 3.0) < 1e-9 && std::abs (foil::decaySec (0.875f) - 13.1) < 1e-9);
    CHECK (std::abs (nebula::delayMs (0.5f) - 300.0) < 1e-9 && std::abs (nebula::delayMs (0.8f) - 800.0) < 1e-3);
    CHECK (std::abs (horizon::decaySec (0.2916667f) - 3.0) < 0.01);
    CHECK (std::abs (horizon::echoMs (0.15f) - 30.0) < 1e-3);
    CHECK (std::abs (atrium::decaySec (0.5f) - 2.0) < 0.01 && std::abs (atrium::decayValue (2.0) - 0.5f) < 1e-3);
    const double line[] = { 0, 0, 0.5, 2, 1, 10 };
    CHECK (std::abs (knotLaw (0.75, line, 3) - 6.0) < 1e-12 && std::abs (knotInverse (knotLaw (0.83, line, 3), line, 3) - 0.83) < 1e-12);
    float dry = 0, wet = 0;
    mixGains (0.25f, dry, wet); // equal power (the references: 0.924 / 0.383 at 25 %)
    CHECK (std::abs (dry - 0.92388f) < 1e-4f && std::abs (wet - 0.38268f) < 1e-4f);
    mixGains (1.0f, dry, wet);
    CHECK (dry == 0.0f && wet == 1.0f);
    CHECK (std::string (lynx::formatEffectParam (lynx::EffectKind::epochVerb, 2, 0.42048f, 120.0)) == "4.00 s");
    CHECK (std::string (lynx::formatEffectParam (lynx::EffectKind::epochVerb, 16, 1.0f, 120.0)) == "Vintage Hall");
    CHECK (std::string (lynx::formatEffectParam (lynx::EffectKind::nebula, 3, 0.5f, 120.0)) == "300.0 ms");
    CHECK (std::string (lynx::formatEffectParam (lynx::EffectKind::halo, 1, 1.0f, 120.0)) == "+12.0 st");
}

TEST (lynxverbs100_decay_times_match_the_references)
{
    using namespace verbs100;
    // reference RT60s measured on the plugins with the same settings and the same broadband T20 (48 kHz; REA_rodada100 §1–§6)
    {
        auto p = defaults (EffectKind::epochVerb); // Concert Hall, Decay 2 s, Size 100 %, high cut open, no shelf, bass 1 X
        p[1] = 0.0f, p[2] = (float) lynx::verbs::skewInverse (2.0, 0.2, 70.0, 3.3596), p[5] = 0.5f, p[7] = 1.0f, p[13] = 1.0f, p[15] = 1.0f;
        const auto r = rt60 (impulse (EffectKind::epochVerb, p, 6.0));
        CHECK (near (r, 2.49, 0.15));
        p[16] = 1.0f / 21.0f; // Plate: 1.92 s
        CHECK (near (rt60 (impulse (EffectKind::epochVerb, p, 6.0)), 1.92, 0.15));
        p[2] = (float) lynx::verbs::skewInverse (8.0, 0.2, 70.0, 3.3596); // Plate, 8 s: 6.89 s
        CHECK (near (rt60 (impulse (EffectKind::epochVerb, p, 14.0)), 6.89, 0.15));
    }
    {
        auto p = defaults (EffectKind::atrium); // Large Room, decay 2 s → 1.76 s broadband (2.0 s in the mid bands)
        p[3] = 1.0f;
        CHECK (near (rt60 (impulse (EffectKind::atrium, p, 6.0)), 1.76, 0.15));
        p[2] = lynx::verbs::atrium::decayValue (4.0); // 4 s → 3.50 s
        CHECK (near (rt60 (impulse (EffectKind::atrium, p, 10.0)), 3.50, 0.15));
    }
    {
        auto p = defaults (EffectKind::foilPlate); // decay 2 s → 1.67 s broadband (highs damped)
        p[2] = 0.3f;
        CHECK (near (rt60 (impulse (EffectKind::foilPlate, p, 6.0)), 1.67, 0.2));
    }
    {
        auto p = defaults (EffectKind::nebula); // Twin, 100 ms, feedback 50 %, density 0: √0.5 per pass → 2.05 s
        p[0] = 1.0f, p[3] = (float) (100.0 / 600.0), p[11] = 0.0f;
        CHECK (near (rt60 (impulse (EffectKind::nebula, p, 8.0)), 2.05, 0.2));
        p[5] = 0.75f; // √0.75 per pass → 4.8 s
        CHECK (near (rt60 (impulse (EffectKind::nebula, p, 14.0)), 4.8, 0.2));
    }
    {
        auto p = defaults (EffectKind::horizon); // Room, 2 s, echo off: 1.54 s
        p[0] = 1.0f, p[11] = 0.1875f, p[19] = 0.0f, p[20] = 1.0f, p[22] = 0.0f;
        CHECK (near (rt60 (impulse (EffectKind::horizon, p, 6.0)), 1.54, 0.2));
    }
}

TEST (lynxverbs100_onsets_levels_and_stereo)
{
    using namespace verbs100;
    auto p = defaults (EffectKind::epochVerb);
    p[1] = 0.0f, p[2] = (float) lynx::verbs::skewInverse (2.0, 0.2, 70.0, 3.3596), p[5] = 0.5f, p[7] = 1.0f, p[13] = 1.0f, p[15] = 1.0f;
    const auto hall = impulse (EffectKind::epochVerb, p, 4.0);
    // Concert Hall starts 5.5 ms after the impulse with an energy of −3.1 dB in its first 100 ms (unit impulse) on the
    // classic law; the measured law (round 190bz, the default of new instances) carries the wet-level correction of
    // about 2 dB on top
    CHECK (std::abs ((double) onset (hall) / 48.0 - 5.5) < 1.0);
    {
        auto classic = p;
        classic[(std::size_t) lynx::verbs::epoch::engineLaw] = 0.0f;
        const auto was = energyDb (impulse (EffectKind::epochVerb, classic, 4.0), 0, 4800);
        CHECK (std::abs (was - (-3.1)) < 1.5);
        CHECK (energyDb (hall, 0, 4800) - was > 0.5 && energyDb (hall, 0, 4800) - was < 4.5);
    }
    double lr = 0, ll = 0, rr = 0;
    for (std::size_t i = 0; i < 48000; ++i)
        lr += (double) hall.l[i] * hall.r[i], ll += (double) hall.l[i] * hall.l[i], rr += (double) hall.r[i] * hall.r[i];
    CHECK (std::abs (lr / std::sqrt (ll * rr)) < 0.3); // decorrelated channels (reference: 0.02)
    // pre-delay adds exactly its time
    p[1] = (float) lynx::verbs::skewInverse (50.0, 0.0, 500.0, 2.3219);
    CHECK (std::abs ((double) onset (impulse (EffectKind::epochVerb, p, 1.0)) / 48.0 - 55.5) < 1.0);

    // Mix 0 passes the input untouched, every effect
    for (auto kind : { EffectKind::epochVerb, EffectKind::atrium, EffectKind::foilPlate, EffectKind::nebula, EffectKind::halo, EffectKind::horizon })
    {
        auto q = defaults (kind);
        q[0] = 0.0f;
        const auto s = impulse (kind, q, 0.5);
        CHECK (s.l[0] == 0.5f && s.r[0] == 0.5f);
        float rest = 0.0f;
        for (std::size_t i = 1; i < s.l.size(); ++i)
            rest = std::max ({ rest, std::abs (s.l[i]), std::abs (s.r[i]) });
        CHECK (rest == 0.0f);
    }
}

TEST (lynxverbs100_stable_at_extreme_settings_and_any_rate)
{
    using namespace verbs100;
    // every control at 0 and at 1 (feedback 100 %, shimmer, the longest decays) stays finite and bounded
    for (auto kind : { EffectKind::epochVerb, EffectKind::atrium, EffectKind::foilPlate, EffectKind::nebula, EffectKind::halo, EffectKind::horizon })
        for (float v : { 0.0f, 1.0f })
            for (double sr : { 44100.0, 96000.0 })
            {
                auto p = defaults (kind);
                for (std::size_t i = 0; i < lynx::effectInfo (kind).params.size(); ++i)
                    p[i] = v;
                auto fx = lynx::makeEffect (kind);
                fx->prepare (sr);
                std::vector<float> l (256), r (256);
                float peak = 0.0f;
                bool finite = true;
                for (int b = 0; b < (int) (6.0 * sr / 256); ++b)
                {
                    for (int i = 0; i < 256; ++i)
                        l[(std::size_t) i] = r[(std::size_t) i] = b < 40 ? 0.3f * (float) std::sin (0.05 * (b * 256 + i)) : 0.0f;
                    fx->process (l.data(), r.data(), 256, p, 120.0);
                    for (int i = 0; i < 256; ++i)
                    {
                        finite = finite && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]);
                        peak = std::max ({ peak, std::abs (l[(std::size_t) i]), std::abs (r[(std::size_t) i]) });
                    }
                }
                CHECK (finite && peak < 20.0f);
                if (! finite || peak >= 20.0f)
                    std::printf ("  %s at %g, %g Hz: peak %g\n", lynx::effectInfo (kind).name, (double) v, sr, (double) peak);
            }

    // the decay time does not depend on the sample rate
    auto p = defaults (EffectKind::atrium);
    p[3] = 1.0f;
    const auto at44 = rt60 (impulse (EffectKind::atrium, p, 6.0, 44100.0), 44100.0);
    const auto at96 = rt60 (impulse (EffectKind::atrium, p, 6.0, 96000.0), 96000.0);
    CHECK (near (at44, at96, 0.1));

    // reset() gives the same response again
    auto fx = lynx::makeEffect (EffectKind::halo);
    fx->prepare (48000.0);
    auto q = defaults (EffectKind::halo);
    q[2] = 0.6f;
    std::vector<float> a (4096, 0.0f), b, c (4096, 0.0f), d;
    a[0] = c[0] = 0.5f;
    b = a, d = c;
    fx->process (a.data(), b.data(), 4096, q, 120.0);
    fx->reset();
    fx->process (c.data(), d.data(), 4096, q, 120.0);
    CHECK (a == c && b == d);
}

TEST (lynxverbs100_substitutes_translate_valhalla_states)
{
    using namespace substtest;
    auto translate = [] (const char* name, const std::string& element, int& translated) {
        lynx::ForeignPlugin fp;
        fp.format = lynx::ForeignPlugin::Format::vst3;
        fp.name = name;
        fp.path = std::string ("C:\\Program Files\\Common Files\\VST3\\") + name + ".vst3";
        fp.state = flVst3State (juceXmlComponent (element));
        const auto id = lynx::pluginIdentity (fp);
        const auto* entry = lynx::findPluginSubstitute (id, false);
        translated = -1;
        return entry != nullptr ? std::optional<lynx::EffectSlot> (lynx::makeSubstituteSlot (*entry, &id, &translated)) : std::nullopt;
    };
    int n = 0;
    // VintageVerb: identity laws, the colour and mode by their breakpoints (eighties, Chamber1979 → "Vintage Chamber")
    auto vv = translate ("ValhallaVintageVerb", "<ValhallaVintageVerb Mix=\"0.3\" PreDelay=\"0.1\" Decay=\"0.5\" ColorMode=\"0.6666667\" ReverbMode=\"0.875\"/>", n);
    CHECK (vv && vv->kind == lynx::EffectKind::epochVerb && n == 5 && vv->mix == 1.0f);
    CHECK (vv && std::abs (vv->params[0] - 0.3f) < 1e-6f && std::abs (vv->params[2] - 0.5f) < 1e-6f);
    CHECK (vv && std::abs (vv->params[15] - 0.5f) < 1e-6f && lynx::verbs::choice (vv->params[16], 22) == 20);
    // Room: the linear 0.1..100 s decay through the curve onto LYNX's law (2 s and 10 s), the type by its breakpoints
    auto room = translate ("ValhallaRoom", "<ValhallaRoom mix=\"0.4\" decay=\"0.01901901885867119\" type=\"0.4166667\"/>", n);
    CHECK (room && room->kind == lynx::EffectKind::atrium && n == 3);
    CHECK (room && std::abs (lynx::verbs::atrium::decaySec (room->params[2]) - 2.0) < 0.1 && lynx::verbs::choice (room->params[19], 12) == 4);
    room = translate ("ValhallaRoom", "<ValhallaRoom decay=\"0.0990991\"/>", n);
    CHECK (room && std::abs (lynx::verbs::atrium::decaySec (room->params[2]) - 10.0) < 0.6);
    // Plate type Cobalt (0.125) → the third LYNX plate; Supermassive mode Hydra, synced dotted 1/8
    auto plate = translate ("ValhallaPlate", "<ValhallaPlate Type=\"0.125\" Decay=\"0.5\"/>", n);
    CHECK (plate && plate->kind == lynx::EffectKind::foilPlate && lynx::verbs::choice (plate->params[11], 12) == 2);
    auto sm = translate ("ValhallaSupermassive", "<ValhallaSupermassive Mix=\"0.5\" DelaySync=\"0.75\" DelayNote=\"0.4291667\" Mode=\"0.0833333\"/>", n);
    CHECK (sm && sm->kind == lynx::EffectKind::nebula && n == 4);
    CHECK (sm && lynx::verbs::choice (sm->params[1], 4) == 2 && lynx::verbs::choice (sm->params[2], 7) == 3 && lynx::verbs::choice (sm->params[12], 22) == 1);
    // Shimmer's bare parameterN attributes; FutureVerb's routing / modes
    auto sh = translate ("ValhallaShimmer", "<MYPLUGINSETTINGS parameter0=\"0.5\" parameter1=\"1.0\" parameter10=\"0.35\" parameter11=\"0.7\"/>", n);
    CHECK (sh && sh->kind == lynx::EffectKind::halo && lynx::verbs::choice (sh->params[10], 5) == 1 && lynx::verbs::choice (sh->params[11], 2) == 1);
    auto fv = translate ("ValhallaFutureVerb", "<ValhallaFutureVerb Routing=\"1.0\" EchoMode=\"0.25\" ReverbMode=\"0.125\" Color=\"0.26\"/>", n);
    CHECK (fv && fv->kind == lynx::EffectKind::horizon && lynx::verbs::choice (fv->params[10], 2) == 1);
    CHECK (fv && lynx::verbs::choice (fv->params[21], 12) == 5 && lynx::verbs::choice (fv->params[22], 8) == 2 && lynx::verbs::choice (fv->params[23], 4) == 2);
}

TEST (lynxverbs100_map_shapes_parse_and_apply)
{
    int errors = -1;
    const auto t = lynx::parseSubstituteTable ("[t]\nlynx = Atrium\nmap = xml:a -> Decay | curve 0:0 0.5:0.9 1:1\nmap = xml:b -> Type | steps 0:0 0.3:0.5 0.6:1\n"
                                               "map = xml:c -> Mix | steps 0.5:0.2 0.4:0.1\nmap = xml:d -> Mix | curve 0:1\n", &errors);
    CHECK (t.size() == 1 && errors == 2 && t[0].params.size() == 2); // descending x and a one-point curve are refused
    CHECK (t.size() == 1 && t[0].params[0].shape == lynx::SubstituteParamMap::Shape::curve && t[0].params[1].points.size() == 3);
}
