// modrender: renders a tracker module (.mod .xm .s3m .it) with libxmp-lite's own player (round 190nw), the reference the
// module import's renders are compared with (scripts/flref_compare.py pair <this.wav> <LYNX's export.wav>).
// Usage: modrender <module> <out.wav> [--rate 44100] [--mix 100] [--interp nearest|linear|spline]
// 16-bit stereo PCM; the song once (stops where it would loop), as LYNX's export plays it.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef LIBXMP_STATIC
 #define LIBXMP_STATIC 1
#endif
#include "xmp.h"

namespace
{
void put16 (std::FILE* f, int v) { std::fputc (v & 255, f), std::fputc ((v >> 8) & 255, f); }
void put32 (std::FILE* f, std::uint32_t v) { for (int i = 0; i < 4; ++i) std::fputc ((int) ((v >> (8 * i)) & 255), f); }
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: modrender <module> <out.wav> [--rate N] [--mix 0..100] [--interp nearest|linear|spline]\n");
        return 2;
    }
    int rate = 44100, mix = 100, interp = XMP_INTERP_SPLINE;
    for (int i = 3; i + 1 < argc; i += 2)
    {
        const std::string a = argv[i], v = argv[i + 1];
        if (a == "--rate")
            rate = std::atoi (v.c_str());
        else if (a == "--mix")
            mix = std::atoi (v.c_str());
        else if (a == "--interp")
            interp = v == "nearest" ? XMP_INTERP_NEAREST : v == "linear" ? XMP_INTERP_LINEAR : XMP_INTERP_SPLINE;
    }
    auto* ctx = xmp_create_context();
    if (xmp_load_module (ctx, argv[1]) != 0)
    {
        std::fprintf (stderr, "cannot load %s\n", argv[1]);
        return 1;
    }
    if (xmp_start_player (ctx, rate, 0) != 0)
        return 1;
    xmp_set_player (ctx, XMP_PLAYER_MIX, mix);
    xmp_set_player (ctx, XMP_PLAYER_INTERP, interp);
    std::vector<std::int16_t> pcm;
    xmp_frame_info fi {};
    while (xmp_play_frame (ctx) == 0)
    {
        xmp_get_frame_info (ctx, &fi);
        if (fi.loop_count > 0)
            break;
        const auto* b = static_cast<const std::int16_t*> (fi.buffer);
        pcm.insert (pcm.end(), b, b + fi.buffer_size / 2);
        if (pcm.size() > (std::size_t) rate * 2 * 3600)
            break;
    }
    xmp_end_player (ctx);
    xmp_release_module (ctx);
    xmp_free_context (ctx);
    auto* f = std::fopen (argv[2], "wb");
    if (f == nullptr)
        return 1;
    const auto bytes = (std::uint32_t) (pcm.size() * 2);
    std::fwrite ("RIFF", 1, 4, f);
    put32 (f, 36 + bytes);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    put32 (f, 16);
    put16 (f, 1);
    put16 (f, 2);
    put32 (f, (std::uint32_t) rate);
    put32 (f, (std::uint32_t) rate * 4);
    put16 (f, 4);
    put16 (f, 16);
    std::fwrite ("data", 1, 4, f);
    put32 (f, bytes);
    std::fwrite (pcm.data(), 2, pcm.size(), f);
    std::fclose (f);
    std::printf ("%s: %.2f s\n", argv[2], (double) pcm.size() / 2.0 / rate);
    return 0;
}
