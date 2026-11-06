// ctlwake (round 173d2): every editor control of the FL-native generators, and for the ones the plain chord pattern
// doesn't move, the context that wakes them (tools/CtlWake.h). Usage:
//   ctlwake --fl <FL Data> [--only <engine>] [--tsv <out>] [--known <tsv>] [--known-only] [--controls a,b]

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <chrono>
#include <map>
#include <set>

#include "CtlWake.h"
#include "WavReader.h"
#include "core/FlpImport.h"
#include "core/NativeEngines.h"

namespace fs = std::filesystem;

namespace
{
std::vector<std::uint8_t> readFile (const fs::path& p)
{
    std::ifstream f (p, std::ios::binary);
    return { std::istreambuf_iterator<char> (f), {} };
}

std::string local (std::string p)
{
    std::replace (p.begin(), p.end(), '\\', '/');
    return lynx::existingPathIgnoringCase (p);
}

lynx::natives::FlAccess flAccess (const std::string& data)
{
    lynx::natives::FlAccess fl;
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
        const auto path = (fs::temp_directory_path() / ("lynx-ctlaudit-" + std::to_string (wav.size()) + "-" + std::to_string (name.size()) + ".wav")).string();
        std::ofstream (path, std::ios::binary).write ((const char*) wav.data(), (std::streamsize) wav.size());
        return path;
    };
    return fl;
}

} // namespace

int main (int argc, char** argv)
{
    std::string flData, only, tsvPath, knownPath;
    bool knownOnly = false, unreadable = false, listHints = false; // --hints: each engine's hints (engine, id, note, pattern) // --unreadable: list the controls the new channel's state can't read or write
    double budget = 0.0; // seconds of search per engine (0 = none)
    std::vector<std::string> pick;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--fl" && i + 1 < argc) flData = argv[++i];
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a == "--tsv" && i + 1 < argc) tsvPath = argv[++i];
        else if (a == "--known" && i + 1 < argc) knownPath = argv[++i];
        else if (a == "--known-only") knownOnly = true;
        else if (a == "--controls" && i + 1 < argc) pick = lynx::ctlwake::split (argv[++i], ',');
        else if (a == "--budget" && i + 1 < argc) budget = std::atof (argv[++i]);
        else if (a == "--unreadable") unreadable = true;
        else if (a == "--hints") listHints = true;
    }
    // engine → control → key, from an earlier --tsv
    std::map<std::string, std::map<std::string, std::string>> known;
    if (! knownPath.empty())
    {
        std::ifstream f (knownPath);
        std::string line;
        std::getline (f, line);
        while (std::getline (f, line))
        {
            const auto c = lynx::ctlwake::split (line, '\t');
            if (c.size() >= 6 && ! c[5].empty())
                known[c[0]][c[2]] = c[5];
        }
    }
    const auto fl = flAccess (flData);
    lynx::ctlaudit::flAccess = &fl;
    const lynx::ctlaudit::Samples samples = [] (const std::string& p) { return lynx::tools::readWav (local (p)); };
    std::FILE* tsv = tsvPath.empty() ? nullptr : std::fopen (tsvPath.c_str(), "w");
    if (tsv != nullptr)
        std::fprintf (tsv, "engine\tindex\tcontrol\tplain_db\tstatus\tkey\tbest_db\tnote\n");
    int total = 0, dead = 0, dependent = 0;
    for (const auto& e : lynx::natives::instrumentEngines())
    {
        if (! only.empty() && only != e.name)
            continue;
        auto c = lynx::natives::makeChannel (e.kind, fl);
        if (c && e.kind == lynx::InstrumentKind::granulizer && c->samplePath.empty())
            c->samplePath = (fs::path (flData) / "Patches" / "Packs" / "Loops" / "DL Pendulus.wav").string();
        if (! c)
        {
            std::printf ("== %s: no channel\n", e.name);
            continue;
        }
        const auto controls = lynx::ctlaudit::editorControls (*c);
        if (controls.empty())
        {
            std::printf ("== %s: no editor controls\n", e.name);
            continue;
        }
        if (listHints)
        {
            std::set<std::string> ids;
            for (const auto& h : lynx::ctlwake::hintsFor (c->instrument, controls))
            {
                const auto id = lynx::ctlwake::hintId (h.note);
                std::printf ("%s\t%s\t%s\t%s%s\n", e.name, id.c_str(), h.note.c_str(), h.pattern.c_str(), ids.insert (id).second ? "" : "\tDUPLICATE");
            }
            continue;
        }
        if (unreadable)
        {
            int bad = 0;
            for (const auto& k : controls)
            {
                auto d = *c;
                const auto v = k.get (*c);
                const bool w = v && (k.set (d, k.low) || k.set (d, k.high));
                if (! v || ! w)
                    std::printf ("  %s | %s | %s\n", e.name, k.name.c_str(), ! v ? "unreadable" : "unwritable"), ++bad;
            }
            std::printf ("== %s: %zu controls, %d unreadable or unwritable\n", e.name, controls.size(), bad);
            continue;
        }
        std::vector<int> sub;
        for (int i = 0; i < (int) controls.size(); ++i)
            if (! pick.empty() && std::find (pick.begin(), pick.end(), controls[(std::size_t) i].name) != pick.end())
                sub.push_back (i);
        if (! pick.empty() && sub.empty())
            continue;
        const auto t0 = std::chrono::steady_clock::now();
        const auto it = known.find (e.name);
        const auto verdicts = lynx::ctlwake::wake (*c, controls, samples, it != known.end() ? it->second : std::map<std::string, std::string> {},
                                                   ! knownOnly, sub, budget);
        int d = 0, dep = 0;
        for (const auto& v : verdicts)
            d += v.best <= lynx::ctlwake::wakeDb, dep += v.best > lynx::ctlwake::wakeDb && ! v.key.empty();
        total += (int) verdicts.size(), dead += d, dependent += dep;
        std::printf ("== %s: %zu controls, %d dependent, %d dead (%.0f s)\n", e.name, verdicts.size(), dep, d,
                     std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        for (const auto& v : verdicts)
        {
            const auto& k = controls[(std::size_t) v.control];
            const char* st = v.best <= lynx::ctlwake::wakeDb ? "DEAD" : v.key.empty() ? "ok" : "dependent-ok";
            if (v.best <= lynx::ctlwake::wakeDb || ! v.key.empty())
                std::printf ("  %4d %-30s %-12s plain %5.0f  best %5.0f  %s\n", k.index, k.name.c_str(), st, v.plain, v.best, v.note.c_str());
            if (tsv != nullptr)
                std::fprintf (tsv, "%s\t%d\t%s\t%.1f\t%s\t%s\t%.1f\t%s\n", e.name, k.index, k.name.c_str(), v.plain, st, v.key.c_str(), v.best,
                              v.note.c_str());
        }
        std::fflush (stdout);
        if (tsv != nullptr)
            std::fflush (tsv);
    }
    std::printf ("\n%d controls: %d dependent, %d dead\n", total, dependent, dead);
    if (tsv != nullptr)
        std::fclose (tsv);
}
