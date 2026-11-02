// bankaudit: renders LYNX preset-bank candidates with the real engines and prints level / length / spectrum figures
// (round 168p: the sanity check of LYNX's own banks — no silence, no clipping, plausible pitch and length).
//
//   bankaudit <presets.txt> [wavdir]
//
// presets.txt, one per line ("#" comments):
//   <kind>|<category>|<name>|<notes>|<v0,v1,...>
//   kind: fruitkick, plucked, drumpad, sakura  (the values are the engine's parameters, see core/DrumControls)
//   notes: "key:vel:sec;key:vel:sec;..." (start times 0.0, 0.6, 1.2 s apart unless "@t" is given: "60:100:0.5@1.0")
// Prints: name, peak dBFS, RMS dBFS (first 1 s), length to −60 dB, spectral centroid, low-band pitch estimate.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/BankStates.h"
#include "core/Instruments.h"

namespace lynx
{
bool offlineRendering() noexcept { return true; }
} // namespace lynx

namespace
{
std::vector<std::string> split (const std::string& s, char c)
{
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in (s);
    while (std::getline (in, cur, c))
        out.push_back (cur);
    return out;
}

void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int rate)
{
    std::ofstream f (path, std::ios::binary);
    const auto n = (std::uint32_t) l.size();
    auto u32 = [&f] (std::uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&f] (std::uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4); u32 (36 + n * 4); f.write ("WAVEfmt ", 8); u32 (16); u16 (1); u16 (2); u32 ((std::uint32_t) rate);
    u32 ((std::uint32_t) rate * 4); u16 (4); u16 (16); f.write ("data", 4); u32 (n * 4);
    for (std::uint32_t i = 0; i < n; ++i)
        for (const auto x : { l[i], r[i] })
        {
            const auto s = (std::int16_t) std::lround (std::clamp (x, -1.0f, 1.0f) * 32767.0f);
            f.write ((const char*) &s, 2);
        }
}

double centroid (const std::vector<float>& x, int rate)
{
    // average magnitude spectrum of 4096-point Hann frames over the first second
    const int n = 4096;
    std::vector<double> mag (n / 2, 0.0);
    for (int start = 0; start + n <= (int) x.size() && start < rate; start += n / 2)
    {
        std::vector<std::complex<double>> a (n);
        for (int i = 0; i < n; ++i)
            a[(std::size_t) i] = x[(std::size_t) (start + i)] * (0.5 - 0.5 * std::cos (2 * M_PI * i / (n - 1)));
        // iterative radix-2 FFT
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[(std::size_t) i], a[(std::size_t) j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const auto w = std::polar (1.0, -2 * M_PI / len);
            for (int i = 0; i < n; i += len)
            {
                std::complex<double> wk = 1;
                for (int k = 0; k < len / 2; ++k, wk *= w)
                {
                    const auto u = a[(std::size_t) (i + k)], v = a[(std::size_t) (i + k + len / 2)] * wk;
                    a[(std::size_t) (i + k)] = u + v;
                    a[(std::size_t) (i + k + len / 2)] = u - v;
                }
            }
        }
        for (int k = 0; k < n / 2; ++k)
            mag[(std::size_t) k] += std::abs (a[(std::size_t) k]);
    }
    double s = 0, w = 0;
    for (int k = 1; k < n / 2; ++k)
    {
        s += mag[(std::size_t) k];
        w += mag[(std::size_t) k] * k * (double) rate / n;
    }
    return s > 0 ? w / s : 0.0;
}

// strongest spectral peak below 2 kHz in the first 0.3 s (the drum's / note's body pitch)
double bodyPitch (const std::vector<float>& x, int rate)
{
    const int n = std::min<int> ((int) x.size(), (int) (0.3 * rate));
    double best = 0, bestF = 0;
    for (double f = 30; f < 2000; f *= 1.01)
    {
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto ph = 2 * M_PI * f * i / rate;
            re += x[(std::size_t) i] * std::cos (ph);
            im += x[(std::size_t) i] * std::sin (ph);
        }
        const auto m = re * re + im * im;
        if (m > best)
            best = m, bestF = f;
    }
    return bestF;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: bankaudit presets.txt [wavdir]\n");
        return 2;
    }
    const std::string wavDir = argc > 2 ? argv[2] : "";
    std::ifstream in (argv[1]);
    std::string line;
    const int rate = 48000;
    int bad = 0;
    while (std::getline (in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        const auto f = split (line, '|');
        if (f.size() < 5)
            continue;
        std::vector<float> values;
        for (const auto& v : split (f[4], ','))
            if (! v.empty())
                values.push_back (std::stof (v));
        auto def = lynx::bankstates::channelFor (f[0], values);
        if (! def)
        {
            std::printf ("%-28s  (cannot build)\n", f[2].c_str());
            ++bad;
            continue;
        }
        auto inst = lynx::makeInstrument (def->instrument);
        inst->configure (*def, 120.0);
        inst->prepare (rate);
        struct N { int key; float vel; double len, at; };
        std::vector<N> notes;
        double t = 0;
        for (const auto& s : split (f[3], ';'))
        {
            auto parts = split (s, '@');
            const auto kv = split (parts[0], ':');
            N n { std::stoi (kv[0]), kv.size() > 1 ? std::stof (kv[1]) / 127.0f : 100.0f / 127.0f, kv.size() > 2 ? std::stod (kv[2]) : 0.4,
                  parts.size() > 1 ? std::stod (parts[1]) : t };
            notes.push_back (n);
            t = n.at + 0.6;
        }
        double end = 0;
        for (const auto& n : notes)
            end = std::max (end, n.at + n.len);
        const int total = (int) ((end + 4.0) * rate);
        std::vector<float> l ((std::size_t) total, 0.0f), r ((std::size_t) total, 0.0f);
        const int block = 256;
        for (int pos = 0; pos < total; pos += block)
        {
            const double t0 = (double) pos / rate, t1 = (double) (pos + block) / rate;
            for (const auto& n : notes)
            {
                if (n.at >= t0 && n.at < t1)
                    inst->noteOn (n.key, n.vel);
                if (n.at + n.len >= t0 && n.at + n.len < t1)
                    inst->noteOff (n.key);
            }
            inst->render (l.data() + pos, r.data() + pos, std::min (block, total - pos), 1.0f, 1.0f);
        }
        float peak = 0;
        double sum = 0;
        int last = 0;
        bool finite = true;
        for (int i = 0; i < total; ++i)
        {
            const auto a = std::max (std::abs (l[(std::size_t) i]), std::abs (r[(std::size_t) i]));
            finite = finite && std::isfinite (a);
            peak = std::max (peak, a);
            if (i < rate)
                sum += 0.5 * (l[(std::size_t) i] * l[(std::size_t) i] + r[(std::size_t) i] * r[(std::size_t) i]);
        }
        for (int i = 0; i < total; ++i)
            if (std::max (std::abs (l[(std::size_t) i]), std::abs (r[(std::size_t) i])) > peak * 0.001f)
                last = i;
        std::vector<float> mono ((std::size_t) total);
        for (int i = 0; i < total; ++i)
            mono[(std::size_t) i] = 0.5f * (l[(std::size_t) i] + r[(std::size_t) i]);
        double side = 0, mid = 0;
        for (int i = 0; i < total; ++i)
        {
            side += std::pow (0.5 * (l[(std::size_t) i] - r[(std::size_t) i]), 2.0);
            mid += std::pow (mono[(std::size_t) i], 2.0);
        }
        const auto peakDb = 20 * std::log10 (std::max (1e-9f, peak));
        const auto rmsDb = 10 * std::log10 (std::max (1e-18, sum / rate));
        const bool problem = ! finite || peakDb > -0.5 || peakDb < -30.0;
        bad += problem ? 1 : 0;
        std::printf ("%-12s %-28s peak %6.1f  rms %6.1f  len %5.2f s  centroid %6.0f Hz  body %6.0f Hz  width %4.2f%s\n", f[1].c_str(),
                     f[2].c_str(), peakDb, rmsDb, (double) last / rate, centroid (mono, rate), bodyPitch (mono, rate),
                     mid > 0 ? std::sqrt (side / mid) : 0.0, problem ? "  <-- CHECK" : "");
        if (! wavDir.empty())
        {
            auto file = f[0] + "-" + f[2];
            for (auto& c : file)
                if (! std::isalnum ((unsigned char) c) && c != '-')
                    c = '_';
            l.resize ((std::size_t) std::min (total, last + rate / 10));
            r.resize (l.size());
            writeWav (wavDir + "/" + file + ".wav", l, r, rate);
        }
    }
    return bad > 0 ? 1 : 0;
}
