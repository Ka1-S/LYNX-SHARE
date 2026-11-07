#include "core/ConvolverFx.h"

// Fruity Convolver, exact (REA_rodada60): goldens of "Fruity Convolver_x64.dll" (the installation's plugin, run
// unmodified) on states of our own — impulse responses generated here as embedded WAVs — and this test's input, rendered
// with the rate set before the state, blocks of 512. The plugin convolves with float FFTs and LYNX with its own
// partitioned convolution, so the outputs agree to about −120 dB rather than bit for bit (tolerance below).
TEST (convolver_engine)
{
    auto lcg = [] (std::uint32_t& s) {
        s = (s * 1103515245u + 12345u) & 0x7fffffffu;
        return s;
    };
    // a decaying noise impulse response as 16-bit integers (the same generator as the golden script)
    auto irPcm16 = [&] (int frames, int channels, std::uint32_t seed, int lead) {
        std::vector<int> v;
        for (int i = 0; i < frames; ++i)
        {
            const int amp = i < lead ? 0 : 12000 * (frames - i) / frames;
            for (int c = 0; c < channels; ++c)
            {
                const auto r = lcg (seed);
                v.push_back (amp > 0 ? (int) ((r >> 8) % (std::uint32_t) (2 * amp + 1)) - amp : 0);
            }
        }
        return v;
    };
    auto put = [] (std::vector<std::uint8_t>& b, const void* p, std::size_t n) {
        const auto* q = (const std::uint8_t*) p;
        b.insert (b.end(), q, q + n);
    };
    auto putI = [&] (std::vector<std::uint8_t>& b, std::int32_t v) { put (b, &v, 4); };
    auto putD = [&] (std::vector<std::uint8_t>& b, double v) { put (b, &v, 8); };
    auto putF = [&] (std::vector<std::uint8_t>& b, float v) { put (b, &v, 4); };
    auto wav = [&] (const std::vector<int>& s, int channels, int rate, bool asFloat) {
        std::vector<std::uint8_t> data;
        for (int v : s)
        {
            if (asFloat)
                putF (data, (float) (v / 32768.0));
            else
            {
                const auto x = (std::int16_t) v;
                put (data, &x, 2);
            }
        }
        const int bits = asFloat ? 32 : 16;
        std::vector<std::uint8_t> fmt;
        const std::uint16_t f16[2] = { (std::uint16_t) (asFloat ? 3 : 1), (std::uint16_t) channels };
        put (fmt, f16, 4);
        putI (fmt, rate);
        putI (fmt, rate * channels * bits / 8);
        const std::uint16_t g16[2] = { (std::uint16_t) (channels * bits / 8), (std::uint16_t) bits };
        put (fmt, g16, 4);
        std::vector<std::uint8_t> body { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' };
        putI (body, 16);
        body.insert (body.end(), fmt.begin(), fmt.end());
        put (body, "data", 4);
        putI (body, (std::int32_t) data.size());
        body.insert (body.end(), data.begin(), data.end());
        std::vector<std::uint8_t> w { 'R', 'I', 'F', 'F' };
        putI (w, (std::int32_t) body.size());
        w.insert (w.end(), body.begin(), body.end());
        return w;
    };
    auto env = [&] (std::vector<std::uint8_t>& b, int version, double t1, double y0, double y1) {
        putI (b, version);
        putI (b, 3);
        putI (b, 2);
        putD (b, 0.0), putD (b, y0), putF (b, 0.0f), putI (b, 0);
        putD (b, t1), putD (b, y1), putF (b, 0.0f), putI (b, 0x02000000);
        putI (b, 0);
        for (int k = 0; k < 4; ++k)
            putI (b, -1);
    };
    // the EQ curve's map with points of our own: Δx, level, tension, mode (the sign byte as FL writes it)
    using EqPts = std::vector<lynx::convolver::EnvPoint>;
    auto eqMap = [&] (std::vector<std::uint8_t>& b, const EqPts& pts) {
        putI (b, 3);
        putI (b, 3);
        putI (b, (std::int32_t) pts.size());
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            const auto& p = pts[i];
            putD (b, p.dt), putD (b, p.y), putF (b, p.tension);
            const std::uint8_t sign = i == 0 ? 0 : (p.tension == 0.0f ? 2 : (p.tension > 0.0f ? 1 : 0xff));
            b.push_back (p.mode), b.push_back (0), b.push_back (0), b.push_back (sign);
        }
        putI (b, 0);
        for (int k = 0; k < 4; ++k)
            putI (b, -1);
    };
    // version-15 state with an embedded WAV
    auto state = [&] (const std::vector<std::uint8_t>& w, int frames, int rate, std::array<int, 4> params, std::array<int, 5> knobs,
                      bool normalise, double eq0, double eq1, const EqPts& curve = {}) {
        std::vector<std::uint8_t> b;
        putI (b, 0xf0003);
        for (int v : params)
            putI (b, v);
        b.push_back (0), b.push_back (0);
        putI (b, (std::int32_t) w.size());
        b.insert (b.end(), w.begin(), w.end());
        const double secs = (double) frames / rate;
        putD (b, -1.0), putD (b, -1.0), putD (b, 0.0), putD (b, secs), putF (b, 0.02f);
        for (int k = 0; k < 4; ++k)
            env (b, 6, secs, 0.5, 0.5);
        if (curve.empty())
            env (b, 3, 1.0, eq0, eq1);
        else
            eqMap (b, curve);
        b.push_back (0), b.push_back (0), b.push_back (1), b.push_back (normalise ? 1 : 0);
        putI (b, 0);
        for (int v : knobs)
            putI (b, v);
        return b;
    };

    constexpr int N = 12000;
    std::vector<float> inL (N), inR (N);
    {
        std::uint32_t seed = 12345;
        for (int i = 0; i < N; ++i)
        {
            const auto s = lcg (seed);
            const auto nz = (float) ((double) s / 2147483648.0 - 0.5);
            const auto tri = (float) (((i % 200) - 100) / 100.0);
            const float on = i >= 1024 && i < 9000 ? 1.0f : 0.0f;
            inL[(std::size_t) i] = 0.5f * tri * on + (i >= 3000 && i < 3200 ? nz * 0.6f : 0.0f);
            inR[(std::size_t) i] = 0.25f * tri * on;
        }
    }
    struct Golden
    {
        int i;
        float l, r;
    };
    auto run = [&] (const std::vector<std::uint8_t>& st, double rate, const std::vector<Golden>& gold) {
        const auto slot = lynx::convolverFromFl (st);
        CHECK (slot.has_value() && slot->kind == lynx::EffectKind::convolver);
        if (! slot)
            return;
        auto fx = lynx::makeConvolver (slot->state);
        fx->prepare (rate);
        fx->reset();
        std::vector<float> l (inL), r (inR);
        for (int s = 0; s < N; s += 512)
            fx->process (l.data() + s, r.data() + s, std::min (512, N - s), slot->params, 120.0);
        for (const auto& g : gold)
            CHECK (std::abs (l[(std::size_t) g.i] - g.l) < 2e-5f && std::abs (r[(std::size_t) g.i] - g.r) < 2e-5f);
    };

    // 16-bit stereo IR with 10 silent frames, normalised; dry 700, wet 900
    const auto irA = wav (irPcm16 (2000, 2, 7, 10), 2, 44100, false);
    const auto stA = state (irA, 2000, 44100, { 700, 900, 0, 0 }, { 0, 0, 0, 0, 1000 }, true, 0.5, 0.5);
    run (stA, 44100.0,
         { { 1500, 0.838246703f, 0.158409148f }, { 3100, 1.42356706f, -0.303102672f }, { 4444, -0.27890569f, -0.213456571f },
           { 8999, -0.0308878124f, 0.618085802f }, { 9500, 0.10672576f, -0.353644371f }, { 11999, 0.0f, 0.0f } });
    // the same at 48 kHz: the IR resampled, the wet gain scaled by 44100 / 48000
    run (stA, 48000.0,
         { { 1500, 0.518590748f, -0.0824978724f }, { 3100, -0.361366481f, 0.588894129f }, { 4444, 0.765420735f, -0.716362238f },
           { 8999, 0.0080665946f, -0.371479809f }, { 9500, -0.979989707f, 0.590659916f }, { 11999, 0.0f, 0.0f } });
    // mono float IR at 48 kHz: predelay 120, blend 300, speed −250, flat EQ level 0.4, input / wet separation 400 / −600
    const auto irB = wav (irPcm16 (3000, 1, 99, 0), 1, 48000, true);
    const auto stB = state (irB, 3000, 48000, { 250, 1000, 400, -600 }, { 0, 120, 300, -250, 1000 }, false, 0.4, 0.4);
    run (stB, 44100.0,
         { { 1500, 0.0f, 0.0f }, { 3100, -0.96503818f, -0.408105195f }, { 4444, 0.308390796f, 0.269355118f },
           { 8999, 1.28672683f, 0.538172305f }, { 9500, -0.755565524f, -0.313999802f }, { 11999, -0.00642539468f, -0.00267019751f } });

    // shaped EQ curves (REA_rodada60 §7 C.7): a linear-phase FIR from the curve, mixed by the EQ amount. Curve of our
    // own with single-curve, double-curve, hold and smooth segments; 16-bit stereo IR, EQ amount 600
    const EqPts curveC { { 0.0, 0.3, 0.0f, 0 },   { 0.1, 0.7, 0.4f, 0 },   { 0.2, 0.25, -0.3f, 1 }, { 0.15, 0.6, 0.0f, 0 },
                         { 0.15, 0.85, 0.2f, 10 }, { 0.1, 0.5, 0.0f, 2 },   { 0.3, 0.4, -0.5f, 0 } };
    const auto irC = wav (irPcm16 (2500, 2, 21, 0), 2, 44100, false);
    const auto stC = state (irC, 2500, 44100, { 500, 1000, 0, 0 }, { 0, 0, 0, 0, 600 }, false, 0.5, 0.5, curveC);
    run (stC, 44100.0,
         { { 1500, -0.959381282f, 0.295649499f }, { 3100, 1.69355786f, 0.29091081f }, { 4444, -0.879927754f, 1.65361476f },
           { 8999, -0.462104529f, 0.386536717f }, { 9500, 2.71528888f, 0.0113559086f }, { 11999, 0.0f, 0.0f } });
    run (stC, 48000.0,
         { { 1500, -0.936118662f, -0.0532133915f }, { 3100, 0.351017892f, 0.319867849f }, { 4444, 0.0732205659f, -0.149969548f },
           { 8999, 1.69761217f, -0.415878028f }, { 9500, 0.303037286f, 0.440511107f }, { 11999, 2.73808833e-08f, 1.06956577e-08f } });
    // mono float IR at 48 kHz played at 44.1 kHz: EQ amount 1000 after speed +150 and before predelay 60 (the curve's
    // Δx sum to 2: FL scales them to 1)
    const EqPts curveD { { 0.0, 0.5, 0.0f, 0 }, { 0.3, 0.2, -0.25f, 0 }, { 0.6, 0.95, 0.35f, 0 }, { 0.5, 0.45, 0.0f, 0 }, { 0.6, 0.65, 0.1f, 1 } };
    const auto irD = wav (irPcm16 (3000, 1, 5, 12), 1, 48000, true);
    const auto stD = state (irD, 3000, 48000, { 0, 1000, 0, 0 }, { 0, 60, 0, 150, 1000 }, false, 0.5, 0.5, curveD);
    run (stD, 44100.0,
         { { 1500, -0.0890971571f, -0.0445485786f }, { 3100, 0.956276f, 0.478138f }, { 4444, -7.81696129f, -4.04534912f },
           { 8999, -1.6380682f, -0.8190341f }, { 9500, 0.876178622f, 0.438089311f }, { 11999, 0.063020736f, 0.031510368f } });
    // with the EQ amount at 0 a shaped curve does nothing (not even the flat curve's gain)
    {
        const auto plain = lynx::convolver::parseState (stC);
        auto off = *plain;
        off.knobs[4] = 0;
        const auto ir = lynx::convolver::decodeAudio (irC);
        const auto k0 = lynx::convolver::buildKernel (off, *ir, 44100);
        CHECK (k0.has_value() && k0->l.size() == 2500 && k0->l[100] == ir->l[100] && k0->r[2000] == ir->r[2000]);
    }

    // the editor's API: the curve read from and written back into a slot blob, its levels, the frequency axis and the
    // level law
    {
        const auto slot = lynx::convolverFromFl (stC);
        CHECK (slot.has_value());
        auto blob = slot->state;
        const auto got = lynx::convolver::readEqCurve (blob);
        CHECK (got.has_value() && got->size() == curveC.size() && (*got)[4].mode == 10 && (*got)[2].tension == -0.3f && (*got)[1].y == 0.7);
        CHECK (lynx::convolver::writeEqCurve (blob, curveD));
        std::vector<std::uint8_t> fl, file;
        CHECK (lynx::convolver::unpackBlob (blob, fl, file));
        // the same bytes as a state written with that curve, and the rest of the state unchanged
        CHECK (fl == state (irC, 2500, 44100, { 500, 1000, 0, 0 }, { 0, 0, 0, 0, 600 }, false, 0.5, 0.5, curveD));
        const auto back = lynx::convolver::readEqCurve (blob);
        CHECK (back.has_value() && back->size() == curveD.size() && (*back)[2].y == 0.95);
        const auto lv = lynx::convolver::eqCurveLevels (curveC, 11);
        CHECK (lv.size() == 11 && std::abs (lv[0] - 0.3f) < 1e-6f && std::abs (lv[10] - 0.4f) < 1e-6f && std::abs (lv[1] - 0.7f) < 1e-6f);
        CHECK (std::abs (lynx::convolver::eqFrequencyToX (22050.0) - 1.0) < 1e-12 && lynx::convolver::eqFrequencyToX (0.0) == 0.0);
        CHECK (std::abs (lynx::convolver::eqXToFrequency (lynx::convolver::eqFrequencyToX (1000.0)) - 1000.0) < 1e-9);
        CHECK (std::abs (lynx::convolver::eqXToFrequency (lynx::convolver::eqFrequencyToX (60.0)) - 60.0) < 1e-9);
        CHECK (lynx::convolver::eqLevelGain (0.5) == 1.0f && lynx::convolver::eqLevelGain (1.0) == 4.0f && lynx::convolver::eqLevelGain (0.0) == 0.0f);
    }

    // what is not decoded falls back to the approximation: an installation file without a reader, a truncated or
    // unknown state
    {
        std::vector<std::uint8_t> f;
        putI (f, 0xf0003);
        for (int k = 0; k < 4; ++k)
            putI (f, 0);
        f.push_back (1);
        const std::string path = "%FLStudioFactoryData%\\Data\\Patches\\Impulses\\Default.wav";
        f.push_back ((std::uint8_t) path.size());
        f.insert (f.end(), path.begin(), path.end());
        const auto rest = std::vector<std::uint8_t> (stA.begin() + (std::ptrdiff_t) (4 + 16 + 2 + 4 + irA.size()), stA.end());
        f.insert (f.end(), rest.begin(), rest.end());
        CHECK (lynx::convolver::parseState (f).has_value() && lynx::convolver::parseState (f)->fromFile);
        CHECK (! lynx::convolverFromFl (f).has_value());
        std::string asked;
        const auto withFile = lynx::convolverFromFl (f, [&asked, &irA] (const std::string& rel) { asked = rel; return irA; });
        CHECK (withFile.has_value() && asked == "Data\\Patches\\Impulses\\Default.wav");
    }
    CHECK (! lynx::convolverFromFl (std::vector<std::uint8_t> (stA.begin(), stA.begin() + 100)).has_value());
    CHECK (! lynx::convolver::parseState ({ 9, 0, 0, 0 }).has_value());

    // parameters map back to FL's integers; texts in FL's units
    lynx::EffectParams p {};
    lynx::convolver::toParams ({ 321, 999, -384, 1000 }, p);
    CHECK ((lynx::convolver::fromParams (p) == std::array<int, 4> { 321, 999, -384, 1000 }));
    CHECK (lynx::convolverParamText (0, 0.5f) == "-12.7dB" && lynx::convolverParamText (1, 0.25f) == "-46.7dB");
    CHECK (lynx::convolverParamText (0, 1.0f) == "0.0dB" && lynx::convolverParamText (0, 0.0f) == "-INFdB");
    CHECK (lynx::convolverParamText (2, 0.5f) == "Original" && lynx::convolverParamText (2, 0.308f) == "38% separated");
    CHECK (lynx::convolverParamText (3, 1.0f) == "100% merged");
    // the slot blob keeps the FL state and the file
    std::vector<std::uint8_t> a, b;
    CHECK (lynx::convolver::unpackBlob (lynx::convolver::packBlob ({ 1, 2, 3 }, { 4, 5 }), a, b) && a.size() == 3 && b.size() == 2);
}

TEST (convolver_long_kernel_matches_direct_convolution)
{
    // a sparse 100k-tap kernel reaches every partition level (128, 1024, 8192, 16384); the larger levels compute a
    // block's output during the previous block (REA round 87), which must not change the convolution
    lynx::convolver::Kernel k;
    k.l.assign (100001, 0.0f);
    k.r.assign (100001, 0.0f);
    const std::vector<std::pair<int, float>> taps { { 0, 0.5f }, { 77, -0.25f }, { 300, 0.2f }, { 1500, -0.15f }, { 2100, 0.1f }, { 9000, 0.3f },
                                                    { 16500, -0.2f }, { 20000, 0.12f }, { 70000, -0.3f }, { 100000, 0.25f } };
    for (const auto& [i, v] : taps)
        k.l[(std::size_t) i] = v, k.r[(std::size_t) i] = -v;
    k.tail = 200000;
    lynx::convolver::Engine e;
    e.setRate (44100);
    e.setKernel (k);
    e.setParams ({ 0, 1000, 0, 0 }); // no dry, full wet, no separation
    e.reset (false);
    const int total = 130000;
    std::vector<float> x (total), l (total), r (total);
    std::uint32_t seed = 5;
    for (auto& v : x)
        v = (float) ((seed = seed * 1664525u + 1013904223u) >> 8) / 16777216.0f - 0.5f;
    l = x, r = x;
    for (int done = 0; done < total; done += 512)
        e.process (l.data() + done, r.data() + done, std::min (512, total - done));
    const double wet = (double) l[0] / ((double) x[0] * 0.5); // the wet gain's float rounding
    double worst = 0.0;
    for (int i = 0; i < total; i += 7)
    {
        double y = 0.0;
        for (const auto& [t, v] : taps)
            if (i >= t)
                y += (double) v * (double) x[(std::size_t) (i - t)];
        worst = std::max ({ worst, std::abs (y * wet - (double) l[(std::size_t) i]), std::abs (-y * wet - (double) r[(std::size_t) i]) });
    }
    CHECK (std::abs (wet - 1.0) < 1e-3);
    CHECK (worst < 2e-5);
}
