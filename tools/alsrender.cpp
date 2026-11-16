// alsrender: imports a Live set (.als) and renders each MIDI track alone, like Live's "All Individual Tracks" export
// (round 190hs: the LYNX side of the Operator / Tetra quality gate). 48 kHz float WAVs, one per track, named
// "<prefix> <track name>.wav", the song from its start for `seconds`.
// Usage: alsrender set.als out-folder [prefix] [seconds] [track-name-filter]
// LYNX_BLOCK=n: block size (default 256). Prints each channel's instrument kind and the render time per track.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/AlsImport.h"
#include "core/RackRenderer.h"
#include "WavReader.h"

namespace
{
void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int rate)
{
    std::ofstream f (path, std::ios::binary);
    const std::uint32_t frames = (std::uint32_t) l.size(), bytes = frames * 8;
    auto u32 = [&] (std::uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (std::uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4), u32 (36 + bytes), f.write ("WAVEfmt ", 8), u32 (16), u16 (3), u16 (2), u32 ((std::uint32_t) rate),
        u32 ((std::uint32_t) rate * 8), u16 (8), u16 (32), f.write ("data", 4), u32 (bytes);
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        f.write ((const char*) &l[i], 4);
        f.write ((const char*) &r[i], 4);
    }
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: alsrender set.als out-folder [prefix] [seconds] [filter]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const std::string out = argv[2], prefix = argc > 3 ? argv[3] : "t";
    const double seconds = argc > 4 ? std::atof (argv[4]) : 12.0;
    const std::string filter = argc > 5 ? argv[5] : "";
    lynx::AlsImportOptions options;
    lynx::FlpImportReport report;
    options.common.report = &report;
    options.common.fileExists = [] (const std::string& p) { return std::filesystem::exists (p); };
    std::string error;
    auto song = lynx::importAls (bytes, options, &error);
    if (! song)
    {
        std::fprintf (stderr, "error: %s\n", error.c_str());
        return 1;
    }
    for (const auto& n : report.unsupported)
        std::printf ("unsupported: %s (%d)\n", n.first.c_str(), n.second);
    constexpr int rate = 48000;
    const int block = std::getenv ("LYNX_BLOCK") ? std::max (16, std::atoi (std::getenv ("LYNX_BLOCK"))) : 256;
    const auto total = (std::size_t) (seconds * rate);
    std::filesystem::create_directories (out);
    for (std::size_t c = 0; c < song->channels.size(); ++c)
    {
        const auto name = song->channels[c].name;
        if (! filter.empty() && name.find (filter) == std::string::npos)
            continue;
        auto solo = *song;
        for (std::size_t j = 0; j < solo.channels.size(); ++j)
            solo.channels[j].muted = j != c;
        std::int64_t songTicks = 0;
        solo = lynx::makeExportSong (solo, true, (std::int64_t) std::ceil (lynx::ticksPerSample (solo.bpm, rate) * rate), songTicks);
        lynx::RackState::Factories factories;
        factories.samples = [] (const std::string& p) { return lynx::tools::readWav (p); };
        auto state = lynx::RackState::build (solo, nullptr, rate, factories);
        lynx::RackRenderer renderer;
        renderer.prepare (rate, std::max (2048, block));
        std::vector<float> l (total), r (total);
        const auto tps = lynx::ticksPerSample (solo.bpm, rate);
        double tick = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t pos = 0; pos < total; pos += (std::size_t) block)
        {
            const auto n = (int) std::min<std::size_t> ((std::size_t) block, total - pos);
            renderer.process (state.get(), tick < (double) songTicks, tick, n, l.data() + pos, r.data() + pos);
            tick += n * tps;
        }
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("%-44s kind %3d  %.1f ms (%.2f %% of real time)\n", name.c_str(), (int) song->channels[c].instrument, ms,
                     100.0 * ms / (seconds * 1000.0));
        writeWav (out + "/" + prefix + " " + name + ".wav", l, r, rate);
    }
    return 0;
}
