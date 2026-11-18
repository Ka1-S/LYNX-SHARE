// Round 190kz (0.1.30, perf-first, codegen): runtime CPU dispatch. The release builds keep the x86-64 baseline (SSE2);
// the hottest loops also have AVX2 variants picked at run time (src/core/CpuFeatures.h). Each variant must render
// exactly what the SSE2 loops render: these tests run the same work at the SSE2 level and at the CPU's own level
// (cpu::setLevelForTest) and compare every output bit. On a CPU without AVX2 both runs take the SSE2 loops (the test
// then only checks the level logic).

#include "core/CpuFeatures.h"

TEST (codegen190kz_cpu_level_caps_and_restores)
{
    using namespace lynx::cpu;
    const auto hw = hardwareLevel();
    std::printf ("  CPU dispatch level: hardware %s, in use %s\n", levelName (hw), levelName (level()));
    CHECK (level() <= hw);
    setLevelForTest (sse2);
    CHECK (level() == (hw < sse2 ? hw : sse2));
    CHECK (! hasAvx2());
    setLevelForTest (avx2 + 5); // never above the hardware
    CHECK (level() <= hw);
    setLevelForTest (-1);
    CHECK (level() <= hw);
#if LYNX_CPU_DISPATCH
    CHECK (hw >= sse2); // every x86-64 CPU has SSE2
#endif
}

namespace codegen190kz
{
// one render at a capped dispatch level (-1: the CPU's own)
template <typename F>
std::uint64_t atLevel (int cap, F&& render)
{
    lynx::cpu::setLevelForTest (cap);
    const auto h = render();
    lynx::cpu::setLevelForTest (-1);
    return h;
}
} // namespace codegen190kz

// Signalsmith Stretch's spectral loops and FFT passes (third_party/signalsmith: eight bands / values at a time with
// AVX2): the real-time pitch shifter at factor 1 (the identity map: Thief's follow-tempo shifters), transpositions
// down and up (the mapped and the random paths), mono and stereo, both analysis blocks; and the offline stretch.
TEST (codegen190kz_signalsmith_avx2_renders_the_sse2_bits)
{
    using lynx::StretchMethod;
    struct Case
    {
        double rate;
        StretchMethod method;
        bool mono;
        double factor;
    };
    const Case cases[] = {
        { 44100.0, StretchMethod::general, false, 1.0 },
        { 44100.0, StretchMethod::general, true, 1.0 },
        { 48000.0, StretchMethod::transients, false, 1.0 },
        { 44100.0, StretchMethod::general, true, 0.5 },
        { 44100.0, StretchMethod::general, false, 0.9438743126816935 },
        { 44100.0, StretchMethod::general, false, 1.0594630943592953 },
        { 44100.0, StretchMethod::general, true, 1.4983070768766815 },
        { 44100.0, StretchMethod::general, false, 2.378414230005442 },
        { 48000.0, StretchMethod::transients, false, 0.7491535384383408 },
        { 48000.0, StretchMethod::transients, true, 3.174802103936399 },
    };
    int same = 0, total = 0;
    for (const auto& c : cases)
    {
        double e0 = 0.0, e1 = 0.0;
        const auto a = codegen190kz::atLevel (lynx::cpu::sse2, [&] { return perf190kr::shifterRender (c.rate, c.method, c.mono, c.factor, e0); });
        const auto b = codegen190kz::atLevel (-1, [&] { return perf190kr::shifterRender (c.rate, c.method, c.mono, c.factor, e1); });
        ++total;
        if (a == b)
            ++same;
        else
            std::printf ("  shifter %.0f Hz %s factor %.4f: sse2 %016llx, %s %016llx\n", c.rate, c.mono ? "mono" : "stereo", c.factor,
                         (unsigned long long) a, lynx::cpu::levelName (lynx::cpu::level()), (unsigned long long) b);
        CHECK (e0 > 1.0 && std::isfinite (e0));
    }
    for (const auto& [mono, ratio] : { std::pair { false, 1.37 }, std::pair { true, 0.6 }, std::pair { false, 2.6 } })
    {
        double e0 = 0.0, e1 = 0.0;
        const auto a = codegen190kz::atLevel (lynx::cpu::sse2, [&] { return perf190kr::offlineRender (mono, ratio, e0); });
        const auto b = codegen190kz::atLevel (-1, [&] { return perf190kr::offlineRender (mono, ratio, e1); });
        ++total;
        same += a == b ? 1 : 0;
    }
    std::printf ("  Signalsmith Stretch: %d of %d renders identical at sse2 and %s\n", same, total,
                 lynx::cpu::levelName (lynx::cpu::level()));
    CHECK (same == total);
}

// Parametric EQ 2's band cascade and Maximus's biquad cascades (the AVX2 copies: the same SSE2 operations, VEX-encoded)
// against round 190kr's plain-loop references, at the SSE2 level and at the CPU's own level
TEST (codegen190kz_peq2_and_maximus_cascades_avx2_match_the_plain_loops)
{
    int ok[2] = { 0, 0 };
    constexpr int seeds = 60;
    for (int lv = 0; lv < 2; ++lv)
    {
        lynx::cpu::setLevelForTest (lv == 0 ? lynx::cpu::sse2 : -1);
        for (unsigned seed = 1; seed <= (unsigned) seeds; ++seed)
            ok[lv] += (lynx::peqfl::stagesMatchReference (seed * 2246822519u) ? 1 : 0)
                      + (lynx::hlfx::kernelsMatchReference (seed * 2654435761u) ? 1 : 0);
        lynx::cpu::setLevelForTest (-1);
    }
    std::printf ("  PEQ2 / Maximus cascades: sse2 %d / %d, %s %d / %d match the plain loops\n", ok[0], 2 * seeds,
                 lynx::cpu::levelName (lynx::cpu::level()), ok[1], 2 * seeds);
    CHECK (ok[0] == 2 * seeds);
    CHECK (ok[1] == 2 * seeds);
}
