#include <complex>
#include "core/HarmorResynth.h"
// Round natives190kj: Harmor's resynthesis (docs/analise/REA_rodada190kj-harmor.md) — the sample analysis against the
// planes FL's Harmor_x64.dll builds for the same file [EXEC], the voice, and the engine revision of old projects.

namespace
{
// the round's test file rs_sine.wav: 220 Hz, 2 s at 44.1 kHz, 10 ms / 50 ms ramps, 0.7 peak, 16-bit
std::vector<std::uint8_t> natives190kjSineWav (double hz = 220.0, int channels = 1)
{
    const int fs = 44100, n = 2 * fs;
    std::vector<double> y ((std::size_t) n);
    double mx = 0.0;
    for (int i = 0; i < n; ++i)
        y[(std::size_t) i] = std::sin (2.0 * 3.141592653589793 * hz * i / fs), mx = std::max (mx, std::fabs (y[(std::size_t) i]));
    const double tEnd = (double) (n - 1) / fs;
    std::vector<std::uint8_t> b;
    auto u32 = [&b] (std::uint32_t v) { for (int k = 0; k < 4; ++k) b.push_back ((std::uint8_t) (v >> (8 * k))); };
    auto u16 = [&b] (std::uint16_t v) { b.push_back ((std::uint8_t) v), b.push_back ((std::uint8_t) (v >> 8)); };
    const auto bytes = (std::uint32_t) (n * 2 * channels);
    b.insert (b.end(), { 'R', 'I', 'F', 'F' }), u32 (36 + bytes), b.insert (b.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    u32 (16), u16 (1), u16 ((std::uint16_t) channels), u32 (fs), u32 ((std::uint32_t) (fs * 2 * channels)), u16 ((std::uint16_t) (2 * channels)), u16 (16);
    b.insert (b.end(), { 'd', 'a', 't', 'a' }), u32 (bytes);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        const double env = std::min (1.0, t / 0.01) * std::min (1.0, (tEnd - t) / 0.05);
        const auto v = (std::int16_t) std::lround (env * y[(std::size_t) i] * 0.7 / mx * 32767.0);
        for (int c = 0; c < channels; ++c)
            u16 ((std::uint16_t) (c == 1 ? (std::int16_t) (v / 2) : v));
    }
    return b;
}

std::string natives190kjTempWav (const std::string& name, const std::vector<std::uint8_t>& bytes)
{
    const auto p = (std::filesystem::temp_directory_path() / name).string();
    std::ofstream o (p, std::ios::binary);
    o.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    return p;
}

// dominant frequency (Hz) of a stretch of the left channel (Hann window, parabolic peak)
double natives190kjPeakHz (const std::vector<float>& y, int a, int len)
{
    std::vector<std::complex<double>> X ((std::size_t) len);
    for (int k = 0; k < len; ++k)
        X[(std::size_t) k] = (double) y[2 * (std::size_t) (a + k)] * (0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * k / len));
    std::vector<double> m ((std::size_t) len / 2);
    for (std::size_t f = 1; f < m.size(); ++f)
    {
        std::complex<double> s = 0.0;
        if (f > 400) // (≤ 400 bins × 44100/len Hz is enough here)
            break;
        for (int k = 0; k < len; ++k)
            s += X[(std::size_t) k] * std::polar (1.0, -2.0 * 3.141592653589793 * (double) f * k / len);
        m[f] = std::abs (s);
    }
    std::size_t best = 1;
    for (std::size_t f = 1; f < 400 && f + 1 < m.size(); ++f)
        if (m[f] > m[best])
            best = f;
    const auto a1 = m[best - 1], a2 = m[best], a3 = m[best + 1];
    const auto d = 0.5 * (a1 - a3) / (a1 - 2.0 * a2 + a3);
    return ((double) best + d) * 44100.0 / len;
}
} // namespace

TEST (natives190kj_resynthesis_analysis_matches_fl)
{
    // FUN_00794510 + FUN_00795720 on rs_sine.wav: geometry, pixels, codes and the frequency plane as FL's DLL built them
    lynx::harmor::SampleData s;
    CHECK (lynx::harmor::readWavMono (natives190kjSineWav(), s));
    CHECK (s.rate == 44100.0 && s.mono.size() == 88200);
    const auto P = lynx::harmor::analyseResynthesis (s.mono.data(), s.mono.size(), s.rate);
    CHECK (P != nullptr);
    if (P == nullptr)
        return;
    CHECK (P->cols == 347 && P->rows == 255 && P->stride == 256 && P->hop == 194);
    CHECK (std::fabs (P->rate - 33488.072358476631) < 1e-6);
    CHECK (std::fabs (P->k148 + 3600.0) < 1e-6);
    CHECK (std::fabs (P->msCol - 5.7931074062223225) < 1e-9);
    CHECK (std::fabs (P->k104 - 1.31958759f) < 1e-6f);
    auto g = [&P] (int c, int i) { return P->gain[(std::size_t) c * 256 + (std::size_t) i]; };
    auto ph = [&P] (int c, int i) { return P->phase[(std::size_t) c * 256 + (std::size_t) i]; };
    auto fr = [&P] (int c, int i) { return P->freq[(std::size_t) c * 256 + (std::size_t) i]; };
    // [EXEC] the DLL's plane 0 at columns 100 / 5 / 340
    CHECK (std::fabs (g (100, 2) - 254.14006f) < 0.01f && std::fabs (g (100, 3) - 221.802399f) < 0.01f);
    CHECK (std::fabs (g (100, 1) - 0.4928951f) < 0.01f && g (100, 0) == 0.0f);
    CHECK (std::fabs (g (5, 2) - 254.150314f) < 0.01f && std::fabs (g (5, 1) - 0.276983738f) < 0.01f);
    CHECK (std::fabs (g (340, 2) - 232.246658f) < 0.01f && std::fabs (g (340, 3) - 202.144318f) < 0.01f);
    // plane 2: peaks (524) on every second bin, the other bins coded 2 + their peak, the peak phases
    const int codes[12] = { 4, 4, 524, 4, 4, 8, 524, 8, 524, 10, 524, 12 };
    for (int i = 0; i < 12; ++i)
        CHECK ((int) std::nearbyint (ph (100, i)) == codes[i]);
    CHECK (std::fabs ((ph (100, 2) - 524.0f) - 0.2239990f) < 1e-3f);
    // column 0: every bin an onset (522)
    for (int i = 0; i < 12; ++i)
        CHECK ((int) std::nearbyint (ph (0, i)) == 522);
    // plane 1 (the frequency deviation) on the sounding bins
    CHECK (std::fabs (fr (100, 2) - 0.27539062f) < 1e-3f && std::fabs (fr (100, 3) + 0.48228267f) < 1e-3f);
    CHECK (std::fabs (fr (5, 2) - 0.27441406f) < 1e-3f && std::fabs (fr (5, 3) + 0.48667339f) < 1e-3f);
    // the second setting (part misc +0x20 = 1, FUN_00794260): harmonics of C1, 511 rows
    lynx::harmor::ResynthOptions o;
    o.mode = 1;
    const auto Q = lynx::harmor::analyseResynthesis (s.mono.data(), s.mono.size(), s.rate, o);
    CHECK (Q != nullptr && Q->rows == 511 && Q->stride == 512 && std::fabs (Q->k148 + 4800.0) < 1e-6);
}

TEST (natives190kj_resynthesis_reader_mixes_channels_as_fl)
{
    // stereo files are analysed as the mean of their channels; float WAVs read as they are
    lynx::harmor::SampleData m, st;
    CHECK (lynx::harmor::readWavMono (natives190kjSineWav (220.0, 1), m));
    CHECK (lynx::harmor::readWavMono (natives190kjSineWav (220.0, 2), st));
    CHECK (m.mono.size() == st.mono.size());
    bool ok = true;
    for (std::size_t i = 0; i < m.mono.size(); i += 997)
    {
        const auto v = (std::int16_t) std::lround (m.mono[i] * 32768.0f);
        ok = ok && std::fabs (st.mono[i] - (m.mono[i] + (float) (std::int16_t) (v / 2) / 32768.0f) * 0.5f) < 1e-7f;
    }
    CHECK (ok);
    lynx::harmor::SampleData none;
    CHECK (! lynx::harmor::readWavMono (std::vector<std::uint8_t> { 'R', 'I', 'F', 'F' }, none));
}

TEST (natives190kj_resynthesis_plays_the_sample)
{
    // a part with the sine loaded plays 220 Hz on C5 (the sample's own pitch) and an octave up on C6, at a steady level
    const auto path = natives190kjTempWav ("lynx_natives190kj_sine.wav", natives190kjSineWav());
    const auto st = harmorTestState (false, false, 0, path);
    CHECK (lynx::harmorFallbacks (st).empty());
    const auto y5 = lynx::harmor::renderReference (st, { { 0, 60, 1.0, 1.0, 0.0, -1 } }, 44100.0, 30000);
    const auto y6 = lynx::harmor::renderReference (st, { { 0, 72, 1.0, 1.0, 0.0, -1 } }, 44100.0, 30000);
    CHECK (y5.size() == 60000 && y6.size() == 60000);
    CHECK (std::fabs (natives190kjPeakHz (y5, 8192, 8192) - 220.0) < 2.0);
    CHECK (std::fabs (natives190kjPeakHz (y6, 8192, 8192) - 440.0) < 4.0);
    double e1 = 0.0, e2 = 0.0;
    for (int k = 6000; k < 12000; ++k)
        e1 += (double) y5[2 * (std::size_t) k] * y5[2 * (std::size_t) k];
    for (int k = 20000; k < 26000; ++k)
        e2 += (double) y5[2 * (std::size_t) k] * y5[2 * (std::size_t) k];
    std::printf ("  natives190kj resynthesis level: %.4g → %.4g\n", e1, e2);
    CHECK (e1 > 1e-3 && e2 > e1 * 0.01); // still sounding (the part's volume envelope decays to its sustain)
    std::filesystem::remove (path);
}

TEST (natives190kj_engine_revision_keeps_old_projects)
{
    // engineRev 0 (a project saved before this round): the resynthesis part plays its timbres exactly as before —
    // bit-identical to the same state whose sample cannot be found (LYNX's former path) — while revision 1 plays the
    // sample; states without a resynthesis image render identically under both revisions
    const auto path = natives190kjTempWav ("lynx_natives190kj_rev.wav", natives190kjSineWav());
    const auto st = harmorTestState (false, false, 0, path);
    const auto missing = harmorTestState (false, false, 0, path + ".missing");
    const std::vector<lynx::harmor::RefNote> notes { { 0, 60, 0.8, 1.0, 0.0, 12000 }, { 4000, 67, 0.6, 1.0, 0.0, 16000 } };
    const auto old0 = lynx::harmor::renderReference (st, notes, 44100.0, 20000, 512, {}, 120.0, {}, 0);
    const auto gone = lynx::harmor::renderReference (missing, notes, 44100.0, 20000, 512, {}, 120.0, {}, 1);
    const auto now1 = lynx::harmor::renderReference (st, notes, 44100.0, 20000, 512, {}, 120.0, {}, 1);
    CHECK (! old0.empty() && old0 == gone);
    CHECK (now1 != old0);
    for (int features : { 0, 1, 16 })
    {
        const auto plain = harmorTestState (false, false, features);
        CHECK (lynx::harmor::renderReference (plain, notes, 44100.0, 8000, 512, {}, 120.0, {}, 0)
               == lynx::harmor::renderReference (plain, notes, 44100.0, 8000, 512, {}, 120.0, {}, 1));
    }
    const auto image = harmorTestState (false, true);
    CHECK (lynx::harmor::renderReference (image, notes, 44100.0, 8000, 512, {}, 120.0, {}, 0)
           == lynx::harmor::renderReference (image, notes, 44100.0, 8000, 512, {}, 120.0, {}, 1));
    lynx::ChannelDef fresh;
    CHECK (fresh.engineRev == 1);
    std::filesystem::remove (path);
}

TEST (natives190kj_resynthesis_cpu_report)
{
    // analysis time of a 2 s sample and the render-time factor of one resynthesised voice (report only)
    lynx::harmor::SampleData s;
    lynx::harmor::readWavMono (natives190kjSineWav(), s);
    const auto t0 = std::chrono::steady_clock::now();
    const auto P = lynx::harmor::analyseResynthesis (s.mono.data(), s.mono.size(), s.rate);
    const auto t1 = std::chrono::steady_clock::now();
    const auto path = natives190kjTempWav ("lynx_natives190kj_cpu.wav", natives190kjSineWav());
    const auto st = harmorTestState (false, false, 0, path);
    lynx::harmorFallbacks (st); // (analysis cached)
    const auto t2 = std::chrono::steady_clock::now();
    const auto y = lynx::harmor::renderReference (st, { { 0, 60, 1.0, 1.0, 0.0, -1 } }, 44100.0, 88200);
    const auto t3 = std::chrono::steady_clock::now();
    const auto plain = harmorTestState (false, false, 0);
    const auto t4 = std::chrono::steady_clock::now();
    const auto z = lynx::harmor::renderReference (plain, { { 0, 60, 1.0, 1.0, 0.0, -1 } }, 44100.0, 88200);
    const auto t5 = std::chrono::steady_clock::now();
    const auto ms = [] (auto a, auto b) { return std::chrono::duration<double, std::milli> (b - a).count(); };
    std::printf ("  natives190kj resynthesis: analysis of 2 s %.1f ms; one voice RTF %.4f (the same part without the sample %.4f)\n",
                 ms (t0, t1), ms (t2, t3) / 2000.0, ms (t4, t5) / 2000.0);
    CHECK (P != nullptr && ! y.empty() && ! z.empty());
    std::filesystem::remove (path);
}
