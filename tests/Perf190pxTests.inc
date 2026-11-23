// Round 190px (0.1.32 perf round 7): exact speed-ups and memory cuts — the AVX half-band oversampler (mixsuite), the
// kept pole sections of the minimum-phase band filters (Prism's dynamic bands), FL's Ogg samples decoded without spare
// room, and the "Safe overloads" guard's view of a starving UI that paces render-ahead's catch-up.
// (Echoform's lazily grown delay lines: the gold renders and perf190px/s/linebench, the old ring against the new.)

#include "core/CpuFeatures.h"
#include "core/MinPhaseBand.h"
#include "core/MixSuite.h"
#include "core/OggVorbis.h"
#include "core/OverloadGuard.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
std::uint64_t fnv190px (const std::vector<std::vector<float>>& channels)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (const auto& v : channels)
    {
        const auto* p = reinterpret_cast<const unsigned char*> (v.data());
        for (std::size_t i = 0; i < v.size() * sizeof (float); ++i)
            h = (h ^ p[i]) * 1099511628211ULL;
    }
    return h;
}
} // namespace

TEST (perf190px_oversampler_avx_renders_the_sse_samples_bit_for_bit)
{
    using namespace lynx;
    std::vector<float> in (5000);
    std::uint32_t s = 0x2468aceu;
    for (auto& x : in)
    {
        s = s * 1664525u + 1013904223u;
        x = (float) ((double) (s >> 8) / 16777216.0 * 2.0 - 1.0);
    }
    const auto curve = [] (float v) noexcept { return v - 0.25f * v * v * v; };
    for (int factor : { 2, 4, 8 })
        // (blocks of every length the stages split into: whole AVX groups, the SSE and scalar remainders)
        for (int block : { 1, 7, 8, 15, 16, 17, 64, 100, 256 })
        {
            std::vector<float> a = in, b = in;
            mixsuite::Oversampler oa, ob;
            oa.setFactor (factor);
            ob.setFactor (factor);
            cpu::setLevelForTest (cpu::sse41);
            for (int at = 0; at < (int) a.size(); at += block)
                oa.process (a.data() + at, std::min (block, (int) a.size() - at), curve);
            cpu::setLevelForTest (-1); // (the hardware's: AVX where there is one)
            for (int at = 0; at < (int) b.size(); at += block)
                ob.process (b.data() + at, std::min (block, (int) b.size() - at), curve);
            CHECK (std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
        }
    cpu::setLevelForTest (-1);
}

TEST (perf190px_minphase_band_kept_poles_design_as_a_fresh_band)
{
    using namespace lynx::minphase;
    // a gain sweep as Prism's dynamic bands make it (every step a new design of the same band), and the crossovers moved
    // half way: each design against a band that never designed before, response and samples bit for bit
    struct Case { double wl, wh; int ml, mh; };
    const Case cases[] { { 0.0, 0.31, 0, 4 }, { 0.12, 0.0, 4, 0 }, { 0.05, 0.4, 4, 4 }, { 0.05, 0.4, 2, 8 }, { 0.2, 0.9, 8, 8 }, { 0.08, 0.6, 2, 2 } };
    for (const auto& c : cases)
    {
        Band kept;
        for (int step = 0; step < 60; ++step)
        {
            const double g = std::pow (10.0, (-24.0 + 0.8 * step) / 20.0);
            const auto wl = step < 30 ? c.wl : c.wl * 1.1, wh = step < 30 ? c.wh : c.wh * 0.9;
            Band fresh;
            kept.designM (wl, wh, c.ml, c.mh, g);
            fresh.designM (wl, wh, c.ml, c.mh, g);
            bool same = true;
            for (double w : { 0.01, 0.1, 0.5, 1.0, 2.0, 3.0 })
            {
                const auto a = kept.response (w), b = fresh.response (w);
                same = same && a.real() == b.real() && a.imag() == b.imag();
            }
            Band k2 = kept, f2 = fresh;
            k2.reset();
            f2.reset();
            for (int i = 0; i < 64; ++i)
            {
                const double x = i == 0 ? 1.0 : std::sin (0.37 * i);
                same = same && k2.process (x) == f2.process (x);
            }
            CHECK (same);
        }
    }
}

TEST (perf190px_guard_sees_the_ui_starving)
{
    lynx::OverloadGuard g;
    CHECK (! g.uiStarving (1000.0)); // (no pulse yet)
    g.uiPulse (1000.0);
    CHECK (! g.uiStarving (1049.0));
    CHECK (g.uiStarving (1051.0));
    g.setEnabled (false); // ("Safe overloads" off: never)
    CHECK (! g.uiStarving (1200.0));
    g.setEnabled (true);
    g.uiPulse (1200.0);
    CHECK (! g.uiStarving (1220.0));
    // ("ralate": render-ahead's own threshold, for an A/B)
    lynx::OverloadGuard h;
    h.configure ("ralate=30");
    h.uiPulse (1000.0);
    CHECK (! h.uiStarving (1029.0) && h.uiStarving (1031.0));
    g.notePaced (1.5);
    g.notePaced (0.5);
    CHECK (g.timesPaced() == 2 && std::abs (g.pacedMs() - 2.0) < 1e-9);
}

TEST (perf190px_fl_ogg_samples_decode_the_same_floats_without_spare_room)
{
    if (realFlpDir.empty())
        return;
    const auto packs = std::filesystem::path (realFlpDir) / "Patches" / "Packs" / "Drums";
    // the samples' FNV-1a 64 of the planar floats, as the decoder before round 190px made them (perf190px/perf/ogg-base.txt)
    const std::pair<const char*, std::uint64_t> files[] { { "Cymbals/Broken Crash.wav", 0xa9dbd74be1a16257ull },
                                                          { "Kicks/Grv Kick 27.wav", 0x08919d4b317dd9c5ull },
                                                          { "Snares/808 Snare.wav", 0xc410014a2627fb33ull } };
    for (const auto& [name, hash] : files)
    {
        std::ifstream in (packs / name, std::ios::binary);
        const std::vector<std::uint8_t> bytes { std::istreambuf_iterator<char> (in), {} };
        lynx::oggvorbis::Decoded d;
        if (bytes.empty())
            continue;
        CHECK (lynx::oggvorbis::decode (bytes.data(), bytes.size(), d) && ! d.data.empty());
        CHECK (fnv190px (d.data) == hash);
        for (const auto& v : d.data)
            CHECK (v.capacity() - v.size() <= 16384);
    }
}
