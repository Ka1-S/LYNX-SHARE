// ctlaudit (round 170i3): which controls of the FL-native generators' LYNX editors change the sound — through a new
// instrument (state), the running one (live: the editor's path) and automation (setParameter). See tools/CtlAudit.h.
// Usage: ctlaudit --fl <FL Studio's Data folder> [--only <engine name>] [--all]   (--all: list every control, not only
// the ones that don't move)

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "CtlAudit.h"
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

const char* mark (double db, double noise) { return lynx::ctlaudit::moved (db, noise) ? "  ok " : " DEAD"; }
} // namespace

int main (int argc, char** argv)
{
    std::string flData, only;
    bool all = false;
    std::vector<std::pair<int, float>> sets; // --set index=raw (a context before the audit)
    std::vector<int> pick;                   // --controls i,j,… (only these)
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--fl" && i + 1 < argc)
            flData = argv[++i];
        else if (a == "--only" && i + 1 < argc)
            only = argv[++i];
        else if (a == "--all")
            all = true;
        else if (a == "--set" && i + 1 < argc)
        {
            const std::string v = argv[++i];
            const auto eq = v.find ('=');
            if (eq != std::string::npos)
                sets.push_back ({ std::atoi (v.substr (0, eq).c_str()), (float) std::atof (v.substr (eq + 1).c_str()) });
        }
        else if (a == "--controls" && i + 1 < argc)
        {
            std::string v = argv[++i];
            for (std::size_t at = 0; at < v.size();)
            {
                const auto comma = v.find (',', at);
                pick.push_back (std::atoi (v.substr (at, comma == std::string::npos ? std::string::npos : comma - at).c_str()));
                at = comma == std::string::npos ? v.size() : comma + 1;
            }
        }
    }
    const auto fl = flAccess (flData);
    const lynx::ctlaudit::Samples samples = [] (const std::string& p) { return lynx::tools::readWav (local (p)); };
    for (const auto& e : lynx::natives::instrumentEngines())
    {
        if (! only.empty() && only != e.name)
            continue;
        auto c = lynx::natives::makeChannel (e.kind, fl);
        // a new Granulizer has no sample (silent, as in FL): give it one of FL's loops
        if (c && e.kind == lynx::InstrumentKind::granulizer && c->samplePath.empty())
            c->samplePath = (fs::path (flData) / "Patches" / "Packs" / "Loops" / "DL Pendulus.wav").string();
        if (! c)
        {
            std::printf ("== %s: no channel\n", e.name);
            continue;
        }
        auto controls = lynx::ctlaudit::editorControls (*c);
        for (const auto& [index, raw] : sets)
            for (const auto& k : controls)
                if (k.index == index)
                    k.set (*c, raw);
        if (! pick.empty())
            controls.erase (std::remove_if (controls.begin(), controls.end(),
                                            [&pick] (const lynx::ctlaudit::Control& k) { return std::find (pick.begin(), pick.end(), k.index) == pick.end(); }),
                            controls.end());
        if (controls.empty())
        {
            std::printf ("== %s: no editor controls\n", e.name);
            continue;
        }
        const auto p = lynx::ctlaudit::patternFor (e.kind);
        const auto rep = lynx::ctlaudit::audit (*c, controls, p, samples);
        int dead = 0;
        for (const auto& r : rep.results)
            dead += ! lynx::ctlaudit::moved (r.state, rep.noise) || ! lynx::ctlaudit::moved (r.live, rep.noise)
                    || (r.automatable && ! lynx::ctlaudit::moved (r.automation, rep.noise));
        int rebuilt = 0;
        for (const auto& r : rep.results)
            rebuilt += r.rebuilt;
        std::printf ("== %s: %zu controls, peak %.3f, noise %.0f dB, %d with a dead path, %d live edits rebuild the instrument\n", e.name,
                     rep.results.size(), rep.basePeak, rep.noise, dead, rebuilt);
        for (const auto& r : rep.results)
        {
            const bool bad = ! lynx::ctlaudit::moved (r.state, rep.noise) || ! lynx::ctlaudit::moved (r.live, rep.noise)
                             || (r.automatable && ! lynx::ctlaudit::moved (r.automation, rep.noise));
            if (bad || all)
                std::printf ("  %4d %-28s %9.3f -> %9.3f  state%s %6.0f  live%s %6.0f  auto%s %6.0f\n", r.index, r.name.c_str(), r.from, r.to,
                             mark (r.state, rep.noise), r.state, mark (r.live, rep.noise), r.live,
                             r.automatable ? mark (r.automation, rep.noise) : "  -  ", r.automation);
            if (r.rebuilt && (bad || all))
                std::printf ("       (the live edit rebuilt the instrument)\n");
        }
        // the controls whose flip moved nothing: in random settings of the others (a section switched on)
        std::vector<int> silent;
        for (std::size_t i = 0; i < controls.size(); ++i)
            for (const auto& r : rep.results)
                if (r.index == controls[i].index && r.name == controls[i].name && ! lynx::ctlaudit::moved (r.state, rep.noise))
                    silent.push_back ((int) i);
        if (! silent.empty())
        {
            const auto left = lynx::ctlaudit::stillDead (*c, controls, silent, p, samples);
            std::printf ("  context pass: %zu of %zu silent controls move with other settings; never moved:", silent.size() - left.size(), silent.size());
            for (const auto i : left)
                std::printf (" %d %s;", controls[(std::size_t) i].index, controls[(std::size_t) i].name.c_str());
            std::printf ("\n");
        }
        std::fflush (stdout);
    }
}
