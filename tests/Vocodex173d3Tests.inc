// Round 173d3 (gap173d-4): Vocodex's 13 maps can be read and written in FL's layout, and curved map segments read as
// FL's envelope library reads them (REA_rodada35 §12). The goldens come from FL's own Vocodex_x64.dll run headless on
// the states and signals built here (the round-35 EXEC harness).

namespace vcx173d3
{
// A new FL instance's state (FL's defaults, every map as FL's constructor makes it) with 24 bands, Draft off, the
// modulator on side input 1 and the carrier on side input 2
inline std::vector<std::uint8_t> flState()
{
    std::vector<std::uint8_t> d;
    auto put = [&d] (const void* p, std::size_t n) { d.insert (d.end(), (const std::uint8_t*) p, (const std::uint8_t*) p + n); };
    auto i32 = [&] (std::int32_t v) { put (&v, 4); };
    i32 ((8 << 16) | 3);
    for (int m = 0; m < 13; ++m)
    {
        const auto pts = lynx::vocodex::defaultMap (m);
        i32 (3), i32 (3), i32 ((std::int32_t) pts.size());
        double prev = 0.0;
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            const double dx = pts[i].x - prev;
            prev = pts[i].x;
            put (&dx, 8), put (&pts[i].y, 8);
            const std::uint8_t rest[8] { 0, 0, 0, 0, 0, 0, 0, (std::uint8_t) (i == 0 ? 0 : 2) };
            put (rest, 8);
        }
        i32 (0), i32 (-1), i32 (-1), i32 (-1), i32 (-1);
    }
    for (const std::int32_t v : { 1000, 0, 0, 55000, 0, 28000, 0, 55000, 1666, 1115, 0, 3500, 0, 0, 349, 100, 2000, 0, 250, 128 })
        i32 (v);
    for (const std::int32_t v : { 0, 2, 0, 24, 1, 1, 2 })
        i32 (v);
    const std::uint8_t flags[8] { 0, 1, 1, 1, 1, 1, 0, 1 };
    put (flags, 8);
    std::uint8_t axis[13] {};
    axis[3] = axis[4] = 1;
    put (axis, 13);
    d.push_back (0); // no tone
    i32 (1), i32 (60);
    return d;
}

// the subtractive flag (+0x190) of an FL state
inline std::vector<std::uint8_t> withSubtractive (std::vector<std::uint8_t> d, bool on)
{
    const auto s = lynx::vocodex::parseState (d);
    d[s->paramsAt + 80 + 32] = on ? 1 : 0;
    return d;
}

using P = lynx::vocodex::MapPoint;

// the golden cases: (map, points), applied in order to flState(), and the subtractive flag
struct Case
{
    const char* name;
    std::vector<std::pair<int, std::vector<P>>> maps;
    bool subtractive = true;
};
inline std::vector<Case> cases()
{
    using namespace lynx::vocodex;
    std::vector<P> every { { 0.0, 0.3 } };
    for (int m = 0; m < 13; ++m)
        every.push_back ({ (m + 1) / 13.0, m % 2 == 0 ? 0.8 : 0.2, m % 2 == 0 ? 0.4f : -0.6f, (std::uint8_t) m });
    const std::vector<P> gate { { 0.0, 0.05 }, { 0.6, 0.45, 0.7f, curveSingle }, { 1.0, 0.2, -0.5f, curveSingle3 } };
    return {
        { "curves on the gain, pan and band width maps",
          { { 0, { { 0.0, 0.2 }, { 0.5, 0.9, 0.6f, curveSingle }, { 1.0, 0.3, -0.4f, curveDouble } } },
            { 1, { { 0.0, 0.2 }, { 0.4, 0.8, 0.5f, curveHalfSine }, { 1.0, 0.5, -0.3f, curveSingle2 } } },
            { 9, { { 0.0, 0.3 }, { 0.5, 0.7, 0.5f, curveSingle2 }, { 1.0, 0.5, -0.5f, curveDouble2 } } } } },
        { "every curve mode on the gain map", { { 0, every } } },
        { "a curved gate map, subtractive", { { 3, gate } }, true },
        { "a curved gate map, not subtractive", { { 3, gate } }, false },
        { "curved band distribution, pitch and hold / attack / release maps",
          { { 8, { { 0.0, 0.14293710887432098 }, { 1.0, 0.9917563796043396, 0.5f, curveSingle3 } } },
            { 10, { { 0.0, 0.5 }, { 0.4, 0.62, 0.8f, curveHalfSine }, { 1.0, 0.45, -0.3f, curveSmooth } } },
            { 5, { { 0.0, 0.2 }, { 1.0, 0.8, 0.6f, curveDouble2 } } },
            { 6, { { 0.0, 0.7 }, { 1.0, 0.2, -0.6f, curveDouble3 } } },
            { 7, { { 0.0, 0.3 }, { 0.5, 0.9, 0.9f, curveStairs }, { 1.0, 0.4, -0.2f, curveSmoothStairs } } } } },
        { "a smooth saturation curve, saturation mix 0.6",
          { { 12, { { 0.0, 0.0 }, { 0.3, 0.6, 0.2f, curveSmooth }, { 0.7, 0.8, -0.4f, curveSmooth }, { 1.0, 1.0, 0.3f, curveSmooth } } },
            { 11, { { 0.0, 0.6 }, { 1.0, 0.6 } } } } },
    };
}

inline std::vector<std::uint8_t> caseState (const Case& c)
{
    auto d = withSubtractive (flState(), c.subtractive);
    for (const auto& [m, pts] : c.maps)
        d = lynx::vocodex::withMap (d, m, pts);
    return d;
}

constexpr int frames = 22050, block = 512;

// modulator (noise in bursts plus a buzz) and carrier (four saws), interleaved stereo, exactly reproducible
inline void signals (std::vector<float>& mod, std::vector<float>& car)
{
    mod.assign (2 * frames, 0.0f);
    car.assign (2 * frames, 0.0f);
    std::uint32_t seed = 12345;
    for (int i = 0; i < frames; ++i)
    {
        const bool on = (i / 2756) % 2 == 0;
        for (int c = 0; c < 2; ++c)
        {
            seed = seed * 1664525u + 1013904223u;
            const float noise = (float) (seed >> 8) * (1.0f / 16777216.0f) - 0.5f;
            const float buzz = (float) (2.0 * std::fmod (i * 180.0 / 44100.0, 1.0) - 1.0);
            mod[(std::size_t) (2 * i + c)] = on ? noise * 0.4f + buzz * 0.1f : buzz * 0.02f;
        }
        double s = 0.0;
        for (const double hz : { 110.0, 165.0, 220.5, 330.0 })
            s += 2.0 * std::fmod (i * hz / 44100.0, 1.0) - 1.0;
        car[(std::size_t) (2 * i)] = (float) (s * 0.15);
        car[(std::size_t) (2 * i + 1)] = (float) (s * 0.12);
    }
}

// LYNX's engine on a case: interleaved stereo out
inline std::vector<float> render (const std::vector<std::uint8_t>& state)
{
    std::vector<float> mod, car, out ((std::size_t) (2 * frames)), zero ((std::size_t) (2 * block), 0.0f);
    signals (mod, car);
    const auto s = lynx::vocodex::parseState (state);
    lynx::vocodex::Engine engine;
    engine.prepare (*s, 44100.0, block, false);
    for (int n0 = 0; n0 < frames; n0 += block)
    {
        const int n = std::min (block, frames - n0);
        engine.process (zero.data(), mod.data() + 2 * n0, car.data() + 2 * n0, out.data() + 2 * n0, n);
    }
    return out;
}

inline std::uint64_t fnv (const std::vector<std::uint8_t>& d)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const auto b : d)
        h = (h ^ b) * 1099511628211ull;
    return h;
}
} // namespace vcx173d3

TEST (vocodex173d3_maps_read_and_write_in_fl_layout)
{
    using namespace lynx::vocodex;
    const auto fl = vcx173d3::flState();
    CHECK (parseState (fl).has_value());
    // a slot state carries LYNX's install-data trailer after FL's state: kept as it is
    InstallData data;
    data.sgDat = { 1, 2, 3, 4, 5 };
    const auto slot = appendInstallData (fl, data);
    const auto maps = readMaps (slot);
    CHECK (maps.has_value());
    if (! maps)
        return;
    for (int m = 0; m < 13; ++m)
        CHECK ((*maps)[(std::size_t) m] == defaultMap (m));
    CHECK (std::string (mapInfo (3).name) == "Modulator noise level" && ! mapInfo (3).centred && mapInfo (10).centred);

    const std::vector<MapPoint> gate { { 0.0, 0.1 }, { 0.35, 0.6, 0.45f, curveSingle }, { 0.7, 0.25, -0.8f, curveDouble3 },
                                       { 1.0, 0.9, 0.0f, curveHold } };
    const auto edited = withMap (slot, 3, gate);
    const auto back = readMaps (edited);
    CHECK (back.has_value());
    if (! back)
        return;
    for (int m = 0; m < 13; ++m)
        if (m != 3)
            CHECK ((*back)[(std::size_t) m] == (*maps)[(std::size_t) m]);
    const auto& g = (*back)[3];
    CHECK (g.size() == 4);
    for (std::size_t i = 0; i < g.size() && i < gate.size(); ++i)
        CHECK (std::abs (g[i].x - gate[i].x) < 1e-15 && g[i].y == (double) (float) gate[i].y && g[i].tension == gate[i].tension
               && g[i].mode == gate[i].mode);
    // the rest of FL's state and the trailer are untouched
    const auto [flEdited, dataEdited] = splitInstallData (edited);
    CHECK (dataEdited.sgDat == data.sgDat);
    const auto a = parseState (fl), b = parseState (flEdited);
    CHECK (a->params == b->params && a->bands == b->bands && a->mapAxis == b->mapAxis && a->subtractiveGate == b->subtractiveGate
           && a->notes == b->notes && fl.size() - a->paramsAt == flEdited.size() - b->paramsAt);
    CHECK (std::equal (fl.begin() + (std::ptrdiff_t) a->paramsAt, fl.end(), flEdited.begin() + (std::ptrdiff_t) b->paramsAt));
    // FL's point bytes: mode, then the sign FL's loader computes (2 = straight; −1/1 = the tension's sign)
    const auto at = [&] (int map) {
        std::size_t o = 4;
        for (int m = 0; m < map; ++m)
        {
            std::int32_t n = 0;
            std::memcpy (&n, flEdited.data() + o + 8, 4);
            o += 12 + 24 * (std::size_t) n + 20;
        }
        return o + 12;
    };
    const auto p3 = at (3);
    CHECK (flEdited[p3 + 24 + 20] == curveSingle && flEdited[p3 + 24 + 23] == 1);
    CHECK (flEdited[p3 + 48 + 20] == curveDouble3 && flEdited[p3 + 48 + 23] == 0xff);
    CHECK (flEdited[p3 + 72 + 20] == curveHold && flEdited[p3 + 72 + 23] == 0);
    const auto p0 = at (0);
    CHECK (flEdited[p0 + 24 + 23] == 2 && flEdited[p0 + 23] == 0);

    // the editor's points are tidied as FL's editor keeps them: sorted, ends at 0 and 1, levels in 0…1
    const auto tidy = readMaps (withMap (fl, 5, { { 0.8, 1.7 }, { 0.2, 0.4, 3.0f, 40 }, { 0.5, -2.0 } }));
    CHECK (tidy && (*tidy)[5].size() == 3);
    if (tidy && (*tidy)[5].size() == 3)
    {
        const auto& t = (*tidy)[5];
        CHECK (t[0].x == 0.0 && t[0].y == (double) 0.4f && t[0].tension == 0.0f && t[0].mode == curveSingle);
        CHECK (t[1].x == 0.5 && t[1].y == 0.0 && t[2].x == 1.0 && t[2].y == 1.0);
    }
    CHECK (withMap (fl, 13, gate) == fl && withMap ({ 1, 2, 3 }, 3, gate) == std::vector<std::uint8_t> ({ 1, 2, 3 }));

    // "Follows modulator pitch shift" per map
    const auto follow = withMapFollowsPitch (slot, 9, true);
    CHECK (parseState (splitInstallData (follow).first)->mapAxis[9] == 1 && splitInstallData (follow).second.sgDat == data.sgDat);
}

TEST (vocodex173d3_map_reads_as_fl)
{
    using namespace lynx::vocodex;
    // straight segments: linear interpolation; levels are FL's floats; centred maps read level − 0.5
    Map line;
    line.x = { 0.0, 1.0 }, line.y = { 0.0, 0.5 }, line.tension = { 0.0f, 0.0f }, line.mode = { 0, 0 };
    CHECK (std::abs (mapValue (line, 0.25) - 0.125) < 1e-15 && mapValue (line, 0.0) == 0.0 && mapValue (line, 1.0) == 0.5);
    CHECK (std::abs (mapValue (line, 0.5, true) + 0.25) < 1e-15);
    line.y = { 0.1, 0.1 };
    CHECK (mapValue (line, 0.3) == (double) 0.1f);
    // a single curve bends toward the end point's side; the hold mode keeps the level until the point
    Map bent = line;
    bent.y = { 0.0, 1.0 }, bent.tension = { 0.0f, 0.5f };
    CHECK (mapValue (bent, 0.5) > 0.9);
    bent.tension[1] = -0.5f;
    CHECK (mapValue (bent, 0.5) < 0.1);
    bent.mode[1] = curveHold;
    CHECK (mapValue (bent, 0.999) == 0.0 && mapValue (bent, 1.0) == 1.0);
    // a first point after 0 gets one at 0; the map is stretched to span 0…1
    Map shifted;
    shifted.x = { 0.5, 2.0 }, shifted.y = { 0.2, 0.8 };
    CHECK (mapValue (shifted, 0.1) == (double) 0.2f && std::abs (mapValue (shifted, 0.625) - 0.5) < 1e-7);
}

TEST (vocodex173d3_curved_maps_sound_as_fl)
{
    // goldens: FL's Vocodex_x64.dll on these states and signals (rms over each quarter and a few samples)
    struct Golden
    {
        std::uint64_t state;
        std::array<double, 4> rms;
        std::array<float, 4> samples; // at frames 3000, 9000, 15000, 21000 (left)
    };
    static const Golden goldens[] {
        { 15946570036355901964uLL, { 0.41697350372581704, 0.40250024664787848, 0.41079888802134185, 0.38672088401054927 }, { -0.33589157462120056f, 0.6732884049415588f, -0.4243902862071991f, 0.28848084807395935f } },
        { 8516061042520654654uLL, { 0.43923508731425526, 0.42150361284147569, 0.42225131201870147, 0.4055286433496692 }, { -0.47705158591270447f, 0.46338701248168945f, -0.5948057174682617f, 0.08836816996335983f } },
        { 12647735120715589970uLL, { 0.27014621678241224, 0.25782503824061404, 0.26106825246308674, 0.24747458379883669 }, { -0.30332496762275696f, 0.3792390823364258f, -0.3252479135990143f, 0.12093021720647812f } },
        { 1354245273333509063uLL, { 0.28636205947729265, 0.27425236154523353, 0.27726687029153879, 0.26310255748134664 }, { -0.3207556903362274f, 0.40440285205841064f, -0.3398796319961548f, 0.10561837255954742f } },
        { 3544612616161804085uLL, { 0.177896366880338, 0.18621166326653524, 0.17985351197522506, 0.17320155816502233 }, { -0.22764179110527039f, -0.13280075788497925f, -0.1532435268163681f, -0.035218797624111176f } },
        { 7922560855850245719uLL, { 0.60176180234467125, 0.564071801672552, 0.57265390282346618, 0.54054125590555424 }, { -0.670004665851593f, 0.8169585466384888f, -0.7954597473144531f, 0.301006555557251f } },
    };
    const auto cs = vcx173d3::cases();
    CHECK (std::size (goldens) == cs.size());
    std::vector<float> plain;
    for (std::size_t c = 0; c < cs.size() && c < std::size (goldens); ++c)
    {
        const auto state = vcx173d3::caseState (cs[c]);
        const auto& g = goldens[c];
        CHECK (vcx173d3::fnv (state) == g.state); // the state the DLL ran
        const auto out = vcx173d3::render (state);
        for (int q = 0; q < 4; ++q)
        {
            double e = 0.0, d = 0.0;
            const int from = q * vcx173d3::frames / 4, to = (q + 1) * vcx173d3::frames / 4;
            for (int i = 2 * from; i < 2 * to; ++i)
                e += (double) out[(std::size_t) i] * out[(std::size_t) i];
            e = std::sqrt (e / (2.0 * (to - from)));
            d = std::abs (e - g.rms[(std::size_t) q]);
            CHECK (d <= 1e-6 * g.rms[(std::size_t) q] + 1e-9);
        }
        const int at[4] { 3000, 9000, 15000, 21000 };
        for (int k = 0; k < 4; ++k)
            CHECK (std::abs (out[(std::size_t) (2 * at[k])] - g.samples[(std::size_t) k]) <= 2e-6f * (float) g.rms[3] + 1e-7f);
        if (c == 2)
            plain = out;
        if (c == 3) // the subtractive switch changes what the gate map does
        {
            double diff = 0.0;
            for (std::size_t i = 0; i < out.size(); ++i)
                diff += std::abs ((double) out[i] - plain[i]);
            CHECK (diff > 1.0);
        }
    }
}
