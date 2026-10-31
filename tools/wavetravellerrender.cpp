// wavetravellerrender: renders FL's Wave Traveller engine (core/WaveTraveller) from an FL state and a sample, for the
// comparisons against the plugin DLL in our harness (docs/analise/REA_rodada167f-wavetraveller.md §7).
// Usage: wavetravellerrender state.bin sample.f32 channels length rate tempo taps frames out.f32 [events...]
//   sample.f32: planar float32 (the decoder's output); taps 0 = live mode, 64/128/256 = render mode
//   events: on=<frame>,<key>,<finalVol>,<finalPan>,<pitch offset cents>[,<initPan>]   off=<frame>,<note index>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/WaveTraveller.h"

int main (int argc, char** argv)
{
    if (argc < 10)
    {
        std::fprintf (stderr, "usage: %s state.bin sample.f32 channels length rate tempo taps frames out.f32 [events]\n", argv[0]);
        return 2;
    }
    std::ifstream sf (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> bytes { std::istreambuf_iterator<char> (sf), {} };
    lynx::wavetrav::State state;
    if (! lynx::wavetrav::parseState (bytes, state))
    {
        std::fprintf (stderr, "state not parsed\n");
        return 1;
    }
    const auto chans = std::atoi (argv[3]), length = std::atoi (argv[4]), rate = std::atoi (argv[5]);
    const auto tempo = (float) std::atof (argv[6]);
    const auto taps = std::atoi (argv[7]), frames = std::atoi (argv[8]);
    std::vector<float> planar ((std::size_t) chans * (std::size_t) length);
    if (FILE* f = std::fopen (argv[2], "rb"))
    {
        if (std::fread (planar.data(), 4, planar.size(), f) != planar.size())
            return 1;
        std::fclose (f);
    }
    lynx::SampleData data;
    data.left.assign (planar.begin(), planar.begin() + length);
    if (chans > 1)
        data.right.assign (planar.begin() + length, planar.begin() + 2 * length);
    data.sampleRate = 44100.0;
    const auto smp = lynx::wavetrav::sampleFrom (data, (double) rate);
    std::vector<lynx::wavetrav::RefNote> notes;
    for (int i = 10; i < argc; ++i)
    {
        lynx::wavetrav::RefNote n;
        int key = 60;
        float vol = 1, pan = 0, pofs = 0, ipan = 0;
        int id = 0, at = 0;
        const auto k = std::sscanf (argv[i], "on=%d,%d,%f,%f,%f,%f", &at, &key, &vol, &pan, &pofs, &ipan);
        if (k >= 5)
        {
            n.start = at;
            n.note.key = key, n.note.finalVol = vol, n.note.finalPan = pan, n.note.initPan = k == 6 ? ipan : 0.0f;
            n.note.finalPitch = 100.0f * (float) key - 6000.0f + pofs;
            notes.push_back (n);
        }
        else if (std::sscanf (argv[i], "off=%d,%d", &at, &id) == 2 && id >= 0 && id < (int) notes.size())
            notes[(std::size_t) id].release = at;
    }
    const auto out = lynx::wavetrav::renderReference (state, *smp, notes, rate, tempo, taps, frames);
    if (FILE* f = std::fopen (argv[9], "wb"))
    {
        std::fwrite (out.data(), 4, out.size(), f);
        std::fclose (f);
    }
    return 0;
}
