// fxrender: renders WAV files through one of LYNX's built-in effects, offline (round tp172b).
//
// The LYNX side of the black-box parity harness (tools/fxmeasure renders the originals): the same stimulus through
// a LYNX effect at matched settings, so scripts/fxparity.py can score the pair like FL's own render tests.
//
//   fxrender <effect name | kind index> [options] <in.wav> <out.wav> [<in.wav> <out.wav> ...]
//     --rate <hz>              sample rate the effect is prepared at (default: the input file's)
//     --block <n>              block size (default 512)
//     --ff <translator> <file> a FabFilter state (raw component or JUCE host state) translated as LYNX loads one
//                              (core/FabFilterStates.h: pro-c3, pro-mb, pro-g, saturn2, pro-q4, pro-ds, pro-l2)
//     --sub <name> <file>      the substitute LYNX opens for the plugin named <name> (core/PluginSubstitutes.h), built
//                              from that plugin's saved component state <file>; the <effect> argument is then
//                              ignored (any name, e.g. "-") and replaced by the substitute's effect
//     --set <i>=<v>            parameter i to the normalized value v
//     --text <i>=<value>       parameter i to the value whose text (formatEffectParam) matches: a number = the
//                              closest numeric text, otherwise the same text (case-insensitive)
//     --songpos <samples>      the song position before each block (<samples> at the file's first sample, 120 BPM)
//     --ticks <ppq> [mode]     also split blocks at song ticks (a host that cuts its buffers on a <ppq> grid; mode 0 floor,
//                              1 round, 2 ceil of the tick's sample position)
//     --settle <s>             silence processed before each file (default 0.5 s, after a reset)
//     --list                   print the parameters (index, name, value, text) and exit
//     --dump <file>            write the parameters ("index value" lines; LYNX --parityshot reads them)
// Every input is processed from a reset; the output is a 32-bit float stereo WAV, not latency-compensated (the
// latency is printed). Inputs: 32-bit float or 16 / 24-bit PCM WAV, mono or stereo.
// Build (with the core, like the other tools): see scripts/build-local.sh, or
//   $CXX $FLAGS $CORE $WAVPACK tools/fxrender.cpp -o build-local/fxrender

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/Effects.h"
#include "core/FabFilterStates.h"
#include "core/PluginSubstitutes.h"

using namespace lynx;

namespace
{
struct Audio
{
    int rate = 48000;
    std::vector<float> l, r;
};

bool readWav (const std::string& path, Audio& a)
{
    std::ifstream f (path, std::ios::binary);
    std::vector<std::uint8_t> d ((std::istreambuf_iterator<char> (f)), {});
    if (d.size() < 12 || std::memcmp (d.data(), "RIFF", 4) != 0)
        return false;
    int format = 0, channels = 0, bits = 0;
    for (std::size_t i = 12; i + 8 <= d.size();)
    {
        std::uint32_t size;
        std::memcpy (&size, d.data() + i + 4, 4);
        if (std::memcmp (d.data() + i, "fmt ", 4) == 0)
        {
            std::uint16_t fmt, ch, bps;
            std::uint32_t sr;
            std::memcpy (&fmt, d.data() + i + 8, 2);
            std::memcpy (&ch, d.data() + i + 10, 2);
            std::memcpy (&sr, d.data() + i + 12, 4);
            std::memcpy (&bps, d.data() + i + 22, 2);
            format = fmt == 0xFFFE ? (bps == 32 ? 3 : 1) : fmt;
            channels = ch;
            bits = bps;
            a.rate = (int) sr;
        }
        else if (std::memcmp (d.data() + i, "data", 4) == 0 && channels > 0)
        {
            const auto* p = d.data() + i + 8;
            const std::size_t bytes = std::min<std::size_t> (size, d.size() - i - 8);
            const int width = bits / 8;
            const std::size_t frames = bytes / (std::size_t) (width * channels);
            a.l.resize (frames);
            a.r.resize (frames);
            for (std::size_t n = 0; n < frames; ++n)
                for (int c = 0; c < std::min (channels, 2); ++c)
                {
                    const auto* s = p + (n * (std::size_t) channels + (std::size_t) c) * (std::size_t) width;
                    float v = 0.0f;
                    if (format == 3 && width == 4)
                        std::memcpy (&v, s, 4);
                    else if (width == 2)
                        v = (float) (std::int16_t) (s[0] | (s[1] << 8)) / 32768.0f;
                    else if (width == 3)
                        v = (float) ((std::int32_t) ((std::uint32_t) s[0] << 8 | (std::uint32_t) s[1] << 16 | (std::uint32_t) s[2] << 24) >> 8) / 8388608.0f;
                    else if (width == 4)
                        v = (float) ((double) (std::int32_t) (s[0] | (s[1] << 8) | (s[2] << 16) | ((std::uint32_t) s[3] << 24)) / 2147483648.0);
                    (c == 0 ? a.l : a.r)[n] = v;
                }
            if (channels == 1)
                a.r = a.l;
            return true;
        }
        i += 8 + size + (size & 1);
    }
    return false;
}

bool writeWav (const std::string& path, const Audio& a)
{
    std::ofstream f (path, std::ios::binary);
    const std::uint32_t frames = (std::uint32_t) a.l.size(), data = frames * 8;
    const auto u32 = [&] (std::uint32_t v) { f.write ((const char*) &v, 4); };
    const auto u16 = [&] (std::uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4); u32 (36 + data); f.write ("WAVEfmt ", 8); u32 (16); u16 (3); u16 (2); u32 ((std::uint32_t) a.rate);
    u32 ((std::uint32_t) a.rate * 8); u16 (8); u16 (32); f.write ("data", 4); u32 (data);
    for (std::uint32_t n = 0; n < frames; ++n)
    {
        f.write ((const char*) &a.l[n], 4);
        f.write ((const char*) &a.r[n], 4);
    }
    return (bool) f;
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

bool numericText (const std::string& t, double& v)
{
    const char* s = t.c_str();
    while (*s == ' ' || *s == '+')
        ++s;
    char* end = nullptr;
    v = std::strtod (s, &end);
    if (end == s)
        return false;
    // "1.2 k" / "1.2 kHz": thousands
    while (*end == ' ')
        ++end;
    if (*end == 'k' || *end == 'K')
        v *= 1000.0;
    return true;
}

float valueForText (EffectKind kind, int param, const std::string& want, const EffectParams& context)
{
    double target = 0.0;
    const bool numeric = numericText (want, target);
    float best = -1.0f;
    double bestErr = 1e300;
    constexpr int steps = 20000;
    for (int k = 0; k <= steps; ++k)
    {
        const float v = (float) k / steps;
        const auto text = formatEffectParam (kind, param, v, 120.0, context);
        double got = 0.0;
        if (numeric)
        {
            if (numericText (text, got))
            {
                const double err = std::abs (got - target);
                if (err < bestErr)
                    bestErr = err, best = v;
            }
        }
        else if (lower (text) == lower (want))
            return v;
    }
    if (best < 0.0f)
        std::fprintf (stderr, "warning: no value of parameter %d reads \"%s\"\n", param, want.c_str());
    return std::max (best, 0.0f);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: fxrender <effect> [--rate hz] [--block n] [--ff translator file] [--set i=v] [--text i=value] [--list] in.wav out.wav ...\n");
        return 2;
    }
    const std::string want = argv[1];
    int kindIndex = -1;
    for (int k = 0; k <= (int) lastEffectKind; ++k)
        if (lower (effectInfo ((EffectKind) k).name) == lower (want) || std::to_string (k) == want)
            kindIndex = k;
    const bool sub = std::any_of (argv + 2, argv + argc, [] (const char* a) { return std::strcmp (a, "--sub") == 0; });
    if (kindIndex < 0 && sub)
        kindIndex = 0;
    if (kindIndex < 0)
    {
        std::fprintf (stderr, "unknown effect: %s\n", want.c_str());
        return 2;
    }
    auto kind = (EffectKind) kindIndex;
    auto slot = makeEffectSlot (kind);
    double rate = 0.0, settle = 0.5;
    bool songPos = false;
    double songOffset = 0.0; // samples
    int block = 512;
    double tickPpq = 0.0;
    int tickMode = 0;
    bool list = false;
    std::string dump;
    std::vector<std::string> files;
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--rate" && i + 1 < argc)
            rate = std::atof (argv[++i]);
        else if (a == "--block" && i + 1 < argc)
            block = std::max (1, std::atoi (argv[++i]));
        else if (a == "--songpos" && i + 1 < argc)
            songPos = true, songOffset = std::atof (argv[++i]); // round 188i: the song position (from 0 at the file's first sample) before each block, as a host gives it
        else if (a == "--ticks" && i + 2 < argc)
            tickPpq = std::atof (argv[++i]), tickMode = std::atoi (argv[++i]);
        else if (a == "--settle" && i + 1 < argc)
            settle = std::atof (argv[++i]);
        else if (a == "--list")
            list = true;
        else if (a == "--dump" && i + 1 < argc)
            dump = argv[++i];
        else if (a == "--ff" && i + 2 < argc)
        {
            const auto translate = fabfilter::translatorNamed (argv[++i]);
            std::ifstream f (argv[++i], std::ios::binary);
            const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (f)), {});
            const auto state = fabfilter::parseState (bytes);
            const int n = translate != nullptr && state ? translate (*state, slot) : 0;
            std::printf ("translated %d parameters (state %s, %u values)\n", n, state ? "read" : "not read", state ? state->count : 0u);
            if (n == 0) // as a substitute slot that cannot read the state: the effect's defaults
                std::printf ("warning: state not translated, the effect's defaults are used\n");
        }
        else if (a == "--sub" && i + 2 < argc)
        {
            PluginIdentity id;
            id.format = ForeignPlugin::Format::vst3;
            id.name = argv[++i];
            std::ifstream f (argv[++i], std::ios::binary);
            id.state.assign (std::istreambuf_iterator<char> (f), {});
            const auto* entry = findPluginSubstitute (id, false);
            if (entry == nullptr)
            {
                std::fprintf (stderr, "no substitute for: %s\n", id.name.c_str());
                return 2;
            }
            int n = 0;
            slot = makeSubstituteSlot (*entry, &id, &n);
            kind = slot.kind;
            std::printf ("substitute %s, translated %d parameters (state %zu bytes)\n", effectInfo (kind).name, n, id.state.size());
        }
        else if ((a == "--set" || a == "--text") && i + 1 < argc)
        {
            const std::string s = argv[++i];
            const auto eq = s.find ('=');
            if (eq == std::string::npos)
                return 2;
            const int p = std::atoi (s.substr (0, eq).c_str());
            if (p < 0 || p >= maxEffectParams)
                return 2;
            slot.params[(std::size_t) p] = a == "--set" ? (float) std::atof (s.substr (eq + 1).c_str())
                                                        : valueForText (kind, p, s.substr (eq + 1), slot.params);
        }
        else
            files.push_back (a);
    }
    const auto& info = effectInfo (kind);
    if (list)
    {
        for (std::size_t p = 0; p < info.params.size(); ++p)
            std::printf ("%zu\t%s\t%.6f\t%s\n", p, info.params[p].name, slot.params[p],
                         formatEffectParam (kind, (int) p, slot.params[p], 120.0, slot.params).c_str());
        return 0;
    }
    if (! dump.empty())
        if (FILE* f = std::fopen (dump.c_str(), "w"))
        {
            for (std::size_t p = 0; p < info.params.size(); ++p)
                std::fprintf (f, "%zu %.9g\n", p, slot.params[p]);
            std::fclose (f);
        }
    for (std::size_t p = 0; p < info.params.size(); ++p)
        if (std::abs (slot.params[p] - info.params[p].defaultValue) > 1e-6f)
            std::printf ("param %zu %s = %.6f (%s)\n", p, info.params[p].name, slot.params[p],
                         formatEffectParam (kind, (int) p, slot.params[p], 120.0, slot.params).c_str());
    for (std::size_t f = 0; f + 1 < files.size(); f += 2)
    {
        Audio in;
        if (! readWav (files[f], in))
        {
            std::fprintf (stderr, "cannot read %s\n", files[f].c_str());
            return 1;
        }
        const double sr = rate > 0.0 ? rate : (double) in.rate;
        auto fx = makeEffect (slot);
        fx->prepare (sr);
        fx->reset();
        std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
        for (int left = (int) (settle * sr); left > 0; left -= block)
        {
            const int n = std::min (block, left);
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            fx->process (l.data(), r.data(), n, slot.params, 120.0);
        }
        Audio out;
        out.rate = (int) sr;
        out.l = in.l;
        out.r = in.r;
        const double tickLen = tickPpq > 0.0 ? sr * 0.5 / tickPpq : 0.0; // 120 BPM
        const auto tickAt = [&] (long long k) {
            const double t = (double) k * tickLen - songOffset;
            return (long long) (tickMode == 0 ? std::floor (t) : tickMode == 1 ? std::floor (t + 0.5) : std::ceil (t));
        };
        for (std::size_t at = 0, n = 0; at < out.l.size(); at += n)
        {
            n = std::min<std::size_t> ((std::size_t) block, out.l.size() - at);
            if (tickLen > 0.0)
            {
                long long k = (long long) std::floor (((double) at + songOffset) / tickLen);
                while (tickAt (k) <= (long long) at)
                    ++k;
                n = std::min<std::size_t> (n, (std::size_t) (tickAt (k) - (long long) at));
            }
            if (songPos)
                fx->setSongPosition (((double) at + songOffset) / sr * 2.0 * (double) ppq); // 120 BPM: two beats a second
            fx->process (out.l.data() + at, out.r.data() + at, (int) n, slot.params, 120.0);
        }
        bool finite = true;
        for (std::size_t n = 0; n < out.l.size(); ++n)
            finite = finite && std::isfinite (out.l[n]) && std::isfinite (out.r[n]);
        writeWav (files[f + 1], out);
        std::printf ("processed %zu samples -> %s latency=%d%s\n", out.l.size(), files[f + 1].c_str(), fx->latencySamples(),
                     finite ? "" : " NON-FINITE");
    }
    return 0;
}
