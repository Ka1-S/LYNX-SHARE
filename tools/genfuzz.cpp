// genfuzz: crash and stuck-note fuzzer for LYNX's built-in generators (round 170i2; the effects' twin is fxfuzz).
//
// Builds every instrument the way the rack does (RackState::build) and plays it hard: note storms over the whole key
// range with extreme and random velocities, chords, pitch bend, the next note's pan / cents / porta, Mod X / Y,
// release velocities, channel pitch moves, cuts, all-notes-off, odd block sizes (1…4096) and rates 22.05…192 kHz.
// Sources of channels:
//   - every FL engine a native project can add (natives::instrumentEngines(), from the FL installation's Default
//     preset when --fl is given, LYNX's own defaults otherwise)
//   - every channel of every .flp under a directory (importFlp, --dir)
// Reports non-finite output (NaN / inf), output that stays loud after every note was released and the tail had 20 s
// (a stuck note; engines with a sustain loop in their release still decay because all notes are cut at the end) and,
// built with -fsanitize=address,undefined (scripts/genfuzz-asan.sh), any memory error.
//
// Usage: genfuzz [--seed n] [--rounds n] [--fl <FL Studio's Data folder>] [--dir <folder with .flp>]... [--only <name>]
//               [--verbose]
// Exit code: 0 = clean, 1 = non-finite output or a stuck note found (crashes abort on their own).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "core/FlpImport.h"
#include "core/NativeEngines.h"
#include "core/RackRenderer.h"
#include "flp/Flp.h"
#include "WavReader.h"

namespace
{
namespace fs = std::filesystem;
int problems = 0, channelsTested = 0;
std::mt19937 rng { 170 };
int rounds = 3;
bool verbose = false;

float uni() { return std::uniform_real_distribution<float> (0.0f, 1.0f) (rng); }
int pick (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); }

std::vector<std::uint8_t> readFile (const fs::path& p)
{
    std::ifstream f (p, std::ios::binary);
    return { std::istreambuf_iterator<char> (f), {} };
}

// an FL installation path (backslashes, any case) on this file system
std::string local (std::string p)
{
    std::replace (p.begin(), p.end(), '\\', '/');
    return lynx::existingPathIgnoringCase (p);
}

lynx::natives::FlAccess flAccess (const std::string& data)
{
    lynx::natives::FlAccess fl;
    if (data.empty())
        return fl;
    fl.roots.data = fs::path (data).string();
    fl.roots.factoryData = fs::path (data).parent_path().string();
    fl.fileExists = [] (const std::string& p) { return fs::is_regular_file (local (p)); };
    fl.readFile = [] (const std::string& p) { return readFile (local (p)); };
    fl.listPresets = [] (const std::string& folder) {
        std::vector<std::string> out;
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator (local (folder), ec))
            if (e.is_regular_file())
                out.push_back (e.path().string());
        std::sort (out.begin(), out.end());
        return out;
    };
    fl.saveEmbedded = [] (const std::vector<std::uint8_t>& wav, const std::string& name) {
        const auto path = (fs::temp_directory_path() / ("lynx-genfuzz-" + std::to_string (wav.size()) + "-" + std::to_string (name.size()) + ".wav")).string();
        std::ofstream (path, std::ios::binary).write ((const char*) wav.data(), (std::streamsize) wav.size());
        return path;
    };
    return fl;
}

bool finite (const std::vector<float>& l, const std::vector<float>& r, int n)
{
    for (int i = 0; i < n; ++i)
        if (! std::isfinite (l[(std::size_t) i]) || ! std::isfinite (r[(std::size_t) i]))
            return false;
    return true;
}

double peak (const std::vector<float>& l, const std::vector<float>& r, int n)
{
    double p = 0.0;
    for (int i = 0; i < n; ++i)
        p = std::max ({ p, (double) std::abs (l[(std::size_t) i]), (double) std::abs (r[(std::size_t) i]) });
    return p;
}

// one channel alone in a rack: every round at another rate, a storm of events, then the release check
void fuzzChannel (const lynx::ChannelDef& channel, const std::string& label)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    auto c = channel;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    auto samples = [] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> { return lynx::tools::readWav (local (path)); };
    const double rates[] { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 };
    ++channelsTested;
    for (int round = 0; round < rounds; ++round)
    {
        const double rate = rates[(std::size_t) pick (0, 4)];
        auto state = lynx::RackState::build (s, nullptr, rate, samples);
        if (state == nullptr || state->instruments.empty() || state->instruments[0] == nullptr)
            return;
        auto& in = *state->instruments[0];
        in.prepare (rate);
        std::vector<float> l (4096), r (4096);
        bool bad = false;
        std::vector<int> held;
        const int steps = 400;
        for (int step = 0; step < steps && ! bad; ++step)
        {
            switch (pick (0, 11))
            {
                case 0: case 1: case 2: // a note: any key, extreme or random velocity, with an expression
                {
                    const int key = pick (0, 2) == 0 ? (pick (0, 1) == 0 ? 0 : 127) : pick (0, 127);
                    const float vel = pick (0, 3) == 0 ? (float) pick (0, 1) : uni();
                    in.setNextNoteExpression (uni() * 2.0f - 1.0f, (uni() - 0.5f) * 2400.0f, pick (0, 5) == 0);
                    in.setNextNoteModulation (uni(), uni());
                    in.noteOn (key, vel);
                    held.push_back (key);
                    break;
                }
                case 3: // a chord
                    for (int k = 0, root = pick (0, 115); k < 4; ++k)
                    {
                        in.noteOn (root + 3 * k, uni());
                        held.push_back (root + 3 * k);
                    }
                    break;
                case 4: case 5:
                    if (! held.empty())
                    {
                        const auto i = (std::size_t) pick (0, (int) held.size() - 1);
                        in.setNextNoteRelease (uni());
                        in.noteOff (held[i]);
                        held.erase (held.begin() + (long) i);
                    }
                    break;
                case 6: in.pitchBend ((uni() - 0.5f) * 96.0f); break;
                case 7: in.setChannelPitchNow ((uni() - 0.5f) * 4800.0f, (uni() - 0.5f) * 200.0f); break;
                case 8: in.controlChange (pick (0, 127), uni()); break;
                case 9: if (pick (0, 7) == 0) in.cut(); break;
                case 10: if (pick (0, 15) == 0) { in.allNotesOff(); held.clear(); } break;
                default: in.slideTo (pick (0, 127), (uni() - 0.5f) * 200.0f, (float) pick (1, 20000)); break;
            }
            const int n = pick (0, 9) == 0 ? pick (1, 8) : pick (1, 4096);
            std::fill (l.begin(), l.begin() + n, 0.0f), std::fill (r.begin(), r.begin() + n, 0.0f);
            in.render (l.data(), r.data(), n, 1.0f, 1.0f);
            if (! finite (l, r, n))
            {
                std::printf ("NON-FINITE  %s  rate %.0f  round %d step %d (block %d)\n", label.c_str(), rate, round, step, n);
                ++problems, bad = true;
            }
        }
        if (bad)
            continue;
        // every note released, then cut: 20 s later the instrument must be silent (a stuck note otherwise)
        for (auto k : held)
            in.noteOff (k);
        in.allNotesOff();
        double last = 0.0;
        for (int done = 0; done < (int) (20.0 * rate); done += 4096)
        {
            std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
            in.render (l.data(), r.data(), 4096, 1.0f, 1.0f);
            if (! finite (l, r, 4096))
            {
                std::printf ("NON-FINITE  %s  rate %.0f  round %d in the release\n", label.c_str(), rate, round);
                ++problems, bad = true;
                break;
            }
            last = peak (l, r, 4096);
        }
        if (! bad && last > 1e-3) // −60 dBFS after 20 s of release
        {
            std::printf ("STUCK       %s  rate %.0f  round %d: peak %.4f after 20 s of release\n", label.c_str(), rate, round, last);
            ++problems;
        }
        else if (verbose)
            std::printf ("  ok %s  rate %.0f  round %d (release peak %.2g)\n", label.c_str(), rate, round, last);
    }
}
} // namespace

int main (int argc, char** argv)
{
    std::string flData, only;
    std::vector<std::string> dirs;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };
        if (a == "--seed")
            rng.seed ((unsigned) std::atoi (next().c_str()));
        else if (a == "--rounds")
            rounds = std::max (1, std::atoi (next().c_str()));
        else if (a == "--fl")
            flData = next();
        else if (a == "--dir")
            dirs.push_back (next());
        else if (a == "--only")
            only = next();
        else if (a == "--verbose")
            verbose = true;
        else
        {
            std::printf ("usage: genfuzz [--seed n] [--rounds n] [--fl <FL Data folder>] [--dir <folder>]... [--only <name>] [--verbose]\n");
            return 2;
        }
    }
    const auto fl = flAccess (flData);
    for (const auto& e : lynx::natives::instrumentEngines())
    {
        if (! only.empty() && only != e.name)
            continue;
        if (const auto c = lynx::natives::makeChannel (e.kind, fl))
            fuzzChannel (*c, e.name);
        else if (verbose)
            std::printf ("  (%s: not available)\n", e.name);
    }
    for (const auto& dir : dirs)
    {
        std::error_code ec;
        for (const auto& f : fs::recursive_directory_iterator (dir, ec))
        {
            if (! f.is_regular_file() || f.path().extension() != ".flp")
                continue;
            try
            {
                lynx::FlpImportOptions options;
                if (! flData.empty())
                {
                    options.roots = { fs::path (flData).parent_path().string(), flData, "" };
                    options.fileExists = [] (const std::string& p) { return fs::is_regular_file (local (p)); };
                    options.readFile = [] (const std::string& p) { return readFile (local (p)); };
                }
                const auto song = lynx::importFlp (flp::parseProject (readFile (f.path())), options);
                for (const auto& c : song.channels)
                    if (only.empty() || only == c.name)
                        fuzzChannel (c, f.path().filename().string() + ": " + c.name);
            }
            catch (const std::exception& ex)
            {
                if (verbose)
                    std::printf ("  (%s: %s)\n", f.path().string().c_str(), ex.what());
            }
        }
    }
    std::printf ("genfuzz: %d channels, %d problems\n", channelsTested, problems);
    return problems > 0 ? 1 : 0;
}
