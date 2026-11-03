// Renders every preset of LYNX's own banks (Toxic Biohazard and PoiZone, round 168n; Harmless, Fruity DX10 and SimSynth,
// round 168o) to WAV files and prints their
// level and brightness, to check the banks by ear and by numbers: bankrender <out dir> [rate]
//   peak / RMS (dBFS) over the phrase, the spectral centroid (Hz) of the held part, and the tail level after the release.
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "core/Dx10.h"
#include "core/Harmless.h"
#include "core/LynxBanks.h"
#include "core/SimSynth.h"
#include "core/PoiZone.h"
#include "core/Toxic.h"

namespace
{
void writeWav (const std::string& path, const std::vector<float>& interleaved, int rate)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&f] (std::uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&f] (std::uint16_t v) { f.write ((const char*) &v, 2); };
    const auto bytes = (std::uint32_t) (interleaved.size() * 2);
    f.write ("RIFF", 4), u32 (36 + bytes), f.write ("WAVEfmt ", 8), u32 (16), u16 (1), u16 (2), u32 ((std::uint32_t) rate),
        u32 ((std::uint32_t) rate * 4), u16 (4), u16 (16), f.write ("data", 4), u32 (bytes);
    for (const auto v : interleaved)
    {
        const auto s = (std::int16_t) std::lround (std::max (-1.0f, std::min (1.0f, v)) * 32767.0f);
        f.write ((const char*) &s, 2);
    }
}

double centroid (const std::vector<float>& interleaved, int from, int rate)
{
    const int n = 16384;
    if ((int) interleaved.size() / 2 < from + n)
        return 0.0;
    std::vector<std::complex<double>> x ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        x[(std::size_t) i] = 0.5 * (interleaved[2 * (std::size_t) (from + i)] + interleaved[2 * (std::size_t) (from + i) + 1])
                             * (0.5 - 0.5 * std::cos (6.283185307179586 * i / n));
    for (int i = 1, j = 0; i < n; ++i) // radix-2 FFT
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (x[(std::size_t) i], x[(std::size_t) j]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const auto w = std::polar (1.0, -6.283185307179586 / len);
        for (int i = 0; i < n; i += len)
        {
            std::complex<double> wk (1.0);
            for (int k = 0; k < len / 2; ++k, wk *= w)
            {
                const auto u = x[(std::size_t) (i + k)], v = x[(std::size_t) (i + k + len / 2)] * wk;
                x[(std::size_t) (i + k)] = u + v;
                x[(std::size_t) (i + k + len / 2)] = u - v;
            }
        }
    }
    double num = 0.0, den = 0.0;
    for (int k = 1; k < n / 2; ++k)
    {
        const auto m = std::abs (x[(std::size_t) k]);
        num += m * k * rate / (double) n;
        den += m;
    }
    return den > 0.0 ? num / den : 0.0;
}

// an engine without a reference renderer: its Instrument, the notes held for `held` samples, interleaved stereo
std::vector<float> renderInstrument (lynx::InstrumentKind kind, const lynx::ChannelDef& c, const std::vector<int>& keys, int rate, int held, int length)
{
    auto inst = kind == lynx::InstrumentKind::harmless ? lynx::makeHarmlessInstrument()
                : kind == lynx::InstrumentKind::dx10   ? lynx::makeDx10Instrument()
                                                       : lynx::makeSimSynthInstrument();
    inst->configure (c, 120.0);
    inst->prepare ((double) rate);
    inst->settleNew();
    for (const auto k : keys)
        inst->noteOn (k, 0.78f);
    std::vector<float> L ((std::size_t) length, 0.0f), R ((std::size_t) length, 0.0f), out;
    for (int pos = 0; pos < length; pos += 256)
    {
        const auto n = std::min (256, length - pos);
        if (pos <= held && pos + n > held)
            for (const auto k : keys)
                inst->noteOff (k);
        inst->render (L.data() + pos, R.data() + pos, n, 1.0f, 1.0f);
    }
    out.reserve (2 * (std::size_t) length);
    for (int i = 0; i < length; ++i)
        out.push_back (L[(std::size_t) i]), out.push_back (R[(std::size_t) i]);
    return out;
}

const char* engineName (lynx::InstrumentKind kind)
{
    switch (kind)
    {
        case lynx::InstrumentKind::toxic: return "Toxic";
        case lynx::InstrumentKind::poizone: return "PoiZone";
        case lynx::InstrumentKind::harmless: return "Harmless";
        case lynx::InstrumentKind::dx10: return "DX10";
        default: return "SimSynth";
    }
}

double db (double v) { return v > 1e-12 ? 20.0 * std::log10 (v) : -240.0; }
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: bankrender <out dir> [rate]\n");
        return 2;
    }
    const std::string dir = argv[1];
    const int rate = argc > 2 ? std::atoi (argv[2]) : 48000;
    const int held = 2 * rate, length = 3 * rate;
    std::printf ("%-8s %-18s %-6s %8s %8s %9s %9s\n", "engine", "preset", "cat", "peak dB", "rms dB", "centroid", "tail dB");
    for (const auto kind : { lynx::InstrumentKind::toxic, lynx::InstrumentKind::poizone, lynx::InstrumentKind::harmless, lynx::InstrumentKind::dx10,
                             lynx::InstrumentKind::simsynth })
    {
        const auto& bank = lynx::banks::bankOf (kind);
        for (int i = 0; i < (int) bank.size(); ++i)
        {
            lynx::ChannelDef c;
            c.instrument = kind;
            const auto loaded = lynx::banks::withPreset (c, i);
            if (! loaded)
                continue;
            std::vector<float> out;
            const bool mono = bank[(std::size_t) i].category == "Bass" || bank[(std::size_t) i].category == "Lead";
            const int low = bank[(std::size_t) i].category == "Bass" ? 36 : 60; // a bass at C2, a lead at C4
            if (kind == lynx::InstrumentKind::toxic)
            {
                std::vector<lynx::toxic::RefNote> notes;
                for (const auto key : { 48, 52, 55 })
                    if (! mono || key == 48)
                        notes.push_back ({ 0, mono ? low : key, 1.0, 1.0, 0.0, 0.0, held });
                out = lynx::toxic::renderReference (loaded->pluginState, notes, rate, length, "");
            }
            else if (kind != lynx::InstrumentKind::poizone)
            {
                std::vector<int> keys;
                for (const auto key : { 48, 52, 55 })
                    if (! mono || key == 48)
                        keys.push_back (mono ? 36 : key);
                out = renderInstrument (kind, *loaded, keys, rate, held, length);
            }
            else
            {
                std::vector<lynx::poizone::RefNote> notes;
                for (const auto key : { 48, 52, 55 })
                    if (! mono || key == 48)
                        notes.push_back ({ 0, mono ? low : key, 1.0, 1.0, 0.0, 0.0, held });
                out = lynx::poizone::renderReference (loaded->pluginState, notes, rate, length);
            }
            float peak = 0.0f;
            double sum = 0.0, tail = 0.0;
            for (std::size_t s = 0; s < out.size(); ++s)
            {
                peak = std::max (peak, std::fabs (out[s]));
                sum += (double) out[s] * out[s];
                if (s >= 2 * (std::size_t) (held + rate / 2))
                    tail = std::max (tail, (double) std::fabs (out[s]));
            }
            const auto name = std::string (engineName (kind));
            std::printf ("%-8s %-18s %-6s %8.1f %8.1f %9.0f %9.1f\n", name.c_str(), bank[(std::size_t) i].name.c_str(),
                         bank[(std::size_t) i].category.c_str(), db (peak), db (std::sqrt (sum / std::max<std::size_t> (1, out.size()))),
                         centroid (out, rate / 2, rate), db (tail));
            writeWav (dir + "/" + name + " - " + bank[(std::size_t) i].category + " - " + bank[(std::size_t) i].name + ".wav", out, rate);
        }
    }
    return 0;
}
