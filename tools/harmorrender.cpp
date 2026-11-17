// Renders a Harmor state with LYNX's engine exactly like our round-34 reference model drives it (verification
// harness, REA_rodada34 §1.25): harmorrender <state.bin> <out.f32> <length> <mt.txt|-> <rate> [blk=N] [bpm=X]
//   [comp=<Harmor System\Compression\n.fst>] [note=start,key,velocity,finalVol,finalPan,release ...]. Writes interleaved stereo float32; mt.txt holds the
// DLL's Mersenne-Twister state (index, then 624 words) or "-" for LYNX's fixed seed.
#include <cctype>
#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/HarmlessFx.h"
#include "core/Harmor.h"
#include "core/HarmorResynth.h"

// resynthesis samples (round natives190kj): FL's path variables and Windows drives mapped to this machine's mounts
// (HR_FLDATA, default /mnt/c/Program Files/Image-Line/FL Studio 2026/Data); non-WAV files decoded with ffmpeg
static bool loadSample (const std::string& saved, lynx::harmor::SampleData& out)
{
    std::string p = saved;
    const std::string var = "%FLStudioData%", fac = "%FLStudioFactoryData%";
    if (p.compare (0, var.size(), var) == 0)
    {
        const char* e = std::getenv ("HR_FLDATA");
        p = std::string (e != nullptr ? e : "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data") + p.substr (var.size());
    }
    else if (p.compare (0, fac.size(), fac) == 0)
        p = std::string ("/mnt/c/Program Files/Image-Line/FL Studio 2026") + p.substr (fac.size());
    else if (p.size() > 2 && p[1] == ':')
        p = std::string ("/mnt/") + (char) std::tolower ((unsigned char) p[0]) + p.substr (2);
    for (auto& c : p)
        if (c == '\\')
            c = '/';
    const auto dot = p.find_last_of ('.');
    const auto ext = dot == std::string::npos ? std::string() : p.substr (dot + 1);
    // WAV, FL's Ogg-in-WAV, Ogg Vorbis and WavPack are decoded by the core reader (FL's own Ogg decoding); others by ffmpeg
    if (ext == "wav" || ext == "WAV" || ext == "ogg" || ext == "OGG" || ext == "wv" || ext == "WV")
        return lynx::harmor::readWavMono (p, out);
    const std::string tmp = "/tmp/claude-1000/hr_dec_" + std::to_string ((long long) getpid()) + ".wav";
    const std::string cmd = "ffmpeg -v quiet -y -i \"" + p + "\" -c:a pcm_f32le \"" + tmp + "\"";
    if (std::system (cmd.c_str()) != 0)
        return false;
    const bool ok = lynx::harmor::readWavMono (tmp, out);
    std::remove (tmp.c_str());
    return ok;
}

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "usage: harmorrender state.bin out.f32 length mt.txt|- rate [blk=N] [bpm=X] [note=s,k,v,fv,fp,off ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    std::vector<std::uint32_t> mt;
    if (std::strcmp (argv[4], "-") != 0)
    {
        std::ifstream m (argv[4]);
        unsigned long long v = 0;
        while (m >> v)
            mt.push_back ((std::uint32_t) v);
    }
    const double rate = std::atof (argv[5]);
    lynx::harmor::setSampleLoader (loadSample);
    int blk = 512, rev = 1;
    double bpm = 120.0;
    std::vector<lynx::harmor::RefNote> notes;
    std::vector<std::uint8_t> comp;
    for (int i = 6; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("rev=", 0) == 0)
            rev = std::atoi (a.c_str() + 4);
        else if (a.rfind ("blk=", 0) == 0)
            blk = std::atoi (a.c_str() + 4);
        else if (a.rfind ("comp=", 0) == 0)
        {
            std::ifstream f (a.substr (5), std::ios::binary);
            const std::vector<std::uint8_t> fst ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
            if (auto st = lynx::hlfx::maximusStateFromFst (fst))
                comp = *st;
        }
        else if (a.rfind ("bpm=", 0) == 0)
            bpm = std::atof (a.c_str() + 4);
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::harmor::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%lf,%lf,%lf,%d", &n.start, &n.key, &n.velocity, &n.finalVol, &n.finalPan, &n.release) >= 5)
                notes.push_back (n);
        }
    }
    const auto out = lynx::harmor::renderReference (state, notes, rate, length, blk, mt, bpm, comp, rev);
    if (out.empty())
    {
        std::fprintf (stderr, "state not usable\n");
        return 1;
    }
    std::FILE* f = std::fopen (argv[2], "wb");
    if (f == nullptr)
        return 1;
    std::fwrite (out.data(), sizeof (float), out.size(), f);
    std::fclose (f);
    for (const auto& s : lynx::harmorFallbacks (state))
        std::printf ("fallback: %s\n", s.c_str());
    return 0;
}
