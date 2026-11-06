// fxaudit (round 173d, issue #77): does every control of LYNX's built-in effects change the sound, and does any
// setting break the output? See tools/FxAudit.h.
// Usage: fxaudit [--fl <FL Studio's Data folder>] [--only <effect name>[,<name>…]] [--control <name>] [--quick]
//                [--segment <s>] [--tail <s>] [--contexts <n>] [--budget <n>] [--tsv <file>] [--all] [--from <n>] [--to <n>]
//   the regression test's settings (tests/FxAudit173dTests.inc): --quick --segment 0.25 --tail 0.15 --contexts 4 --budget 60
//   --all: print every control (not only the dead / dependent ones);  --tsv: one line per control and per finding

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "FxAudit.h"
#include "core/FlpImport.h"
#include "core/NativeEngines.h"

namespace fs = std::filesystem;
using namespace lynx;

namespace
{
std::string local (std::string p)
{
    std::replace (p.begin(), p.end(), '\\', '/');
    return existingPathIgnoringCase (p);
}

natives::FlAccess flAccess (const std::string& data)
{
    natives::FlAccess fl;
    if (data.empty())
        return fl;
    fl.roots.data = fs::path (data).string();
    fl.roots.factoryData = fs::path (data).parent_path().string();
    fl.fileExists = [] (const std::string& p) { return fs::is_regular_file (local (p)); };
    fl.readFile = [] (const std::string& p) {
        std::ifstream f (local (p), std::ios::binary);
        return std::vector<std::uint8_t> { std::istreambuf_iterator<char> (f), {} };
    };
    fl.listPresets = [] (const std::string& folder) {
        std::vector<std::string> out;
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator (local (folder), ec))
            if (e.is_regular_file())
                out.push_back (e.path().string());
        std::sort (out.begin(), out.end());
        return out;
    };
    fl.saveEmbedded = [] (const std::vector<std::uint8_t>&, const std::string&) { return std::string(); };
    return fl;
}

std::vector<std::string> split (const std::string& s)
{
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= s.size())
    {
        const auto c = s.find (',', at);
        out.push_back (s.substr (at, c == std::string::npos ? std::string::npos : c - at));
        if (c == std::string::npos)
            break;
        at = c + 1;
    }
    return out;
}

std::string clean (std::string s)
{
    for (auto& c : s)
        if (c == '\t' || c == '\n' || c == '|')
            c = ' ';
    return s;
}
} // namespace

int main (int argc, char** argv)
{
    std::string flData, tsvPath;
    std::vector<std::string> only;
    fxaudit::Options opt;
    bool all = false;
    double segment = 0.6, tail = 0.25;
    int from = 0, to = (int) lastEffectKind, shard = 0, shards = 1;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--fl" && i + 1 < argc)
            flData = argv[++i];
        else if (a == "--only" && i + 1 < argc)
            only = split (argv[++i]);
        else if (a == "--control" && i + 1 < argc)
            opt.only = split (argv[++i]);
        else if (a == "--quick")
            opt.quick = true;
        else if (a == "--noclicks")
            opt.clicks = false;
        else if (a == "--all")
            all = true;
        else if (a == "--segment" && i + 1 < argc)
            segment = std::atof (argv[++i]);
        else if (a == "--tail" && i + 1 < argc)
            tail = std::atof (argv[++i]);
        else if (a == "--contexts" && i + 1 < argc)
            opt.contexts = std::atoi (argv[++i]);
        else if (a == "--budget" && i + 1 < argc)
            opt.searchBudget = std::atoi (argv[++i]);
        else if (a == "--tsv" && i + 1 < argc)
            tsvPath = argv[++i];
        else if (a == "--from" && i + 1 < argc)
            from = std::atoi (argv[++i]);
        else if (a == "--shard" && i + 1 < argc)
        {
            const std::string v = argv[++i];
            shard = std::atoi (v.c_str()), shards = std::atoi (v.substr (v.find ('/') + 1).c_str());
        }
        else if (a == "--to" && i + 1 < argc)
            to = std::atoi (argv[++i]);
    }
    const auto fl = flAccess (flData);
    const auto prog = fxaudit::programme (segment, tail);
    std::ofstream tsv;
    if (! tsvPath.empty())
    {
        tsv.open (tsvPath);
        tsv << "kind\teffect\tcontrol\tparam\ttype\tlayout\tstatus\tbest_db\tprerequisite\tnew_instance_only\tvalues\tkey\tbest_default_db\n";
    }
    int totalControls = 0, totalDead = 0, totalDep = 0, kinds = 0;
    for (int k = from; k <= to; ++k)
    {
        if (shards > 1 && k % shards != shard)
            continue;
        const auto kind = (EffectKind) k;
        const std::string name = effectInfo (kind).name;
        if (! only.empty() && std::find (only.begin(), only.end(), name) == only.end())
            continue;
        if (const char* why = fxaudit::skipReason (kind))
        {
            std::printf ("== %d %s: skipped (%s)\n", k, name.c_str(), why);
            if (tsv)
                tsv << k << '\t' << name << "\t-\t-\t-\t-\tskipped\t\t" << why << "\t\t\n";
            continue;
        }
        const auto slot = natives::newEffect (kind, fl);
        if (! slot)
        {
            std::printf ("== %d %s: skipped (needs FL Studio's data)\n", k, name.c_str());
            if (tsv)
                tsv << k << '\t' << name << "\t-\t-\t-\t-\tskipped\t\tneeds FL Studio's data\t\t\n";
            continue;
        }
        const auto t0 = std::chrono::steady_clock::now();
        const auto rep = fxaudit::audit (*slot, prog, opt);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        ++kinds;
        totalControls += (int) rep.controls.size();
        totalDead += rep.dead();
        totalDep += rep.dependent();
        std::printf ("== %d %s: %zu controls (of %d), peak %.3f, noise %.0f dB, %d dead, %d dependent, %zu findings (%.1f s)\n", k, name.c_str(),
                     rep.controls.size(), rep.listed, rep.peak, rep.noise, rep.dead(), rep.dependent(), rep.broken.size(), secs);
        for (const auto& c : rep.controls)
        {
            const bool show = all || c.status != fxaudit::Status::ok;
            std::string vals;
            for (const auto& ch : c.changes)
            {
                char b[200];
                std::snprintf (b, sizeof b, "%s%s=%.0f/%.1fdB/st%.0f", vals.empty() ? "" : "; ", clean (ch.text).c_str(), fxaudit::moveOf (ch), ch.band, ch.stereo);
                vals += b;
            }
            if (show)
                std::printf ("  %4d %-30s %-10s %-4s %-12s best %6.0f dB %s%s  [%s]\n", c.control.param, c.control.name.c_str(),
                             c.control.type == fxaudit::Type::continuous ? "knob" : c.control.type == fxaudit::Type::toggle ? "toggle" : "choice",
                             c.control.inLayout ? "ui" : "hid", fxaudit::statusText (c.status), c.best, c.prerequisite.c_str(),
                             c.onlyNewInstance ? " (moves only in a new instance)" : "", vals.c_str());
            if (tsv)
                tsv << k << '\t' << name << '\t' << clean (c.control.name) << '\t' << c.control.param << '\t'
                    << (c.control.type == fxaudit::Type::continuous ? "knob" : c.control.type == fxaudit::Type::toggle ? "toggle" : "choice") << '\t'
                    << (c.control.inLayout ? "ui" : "hidden") << '\t' << fxaudit::statusText (c.status) << '\t' << (int) std::lround (c.best) << '\t'
                    << clean (c.prerequisite) << '\t' << (c.onlyNewInstance ? "yes" : "") << '\t' << vals << '\t' << c.prereqKey << '\t' << (int) std::lround (c.status == fxaudit::Status::dependent && ! c.prereqKey.empty() ? c.bestDefault : c.best) << '\n';
        }
        for (const auto& f : rep.broken)
        {
            std::printf ("  BROKEN %-6s %s = %s (%.3g)\n", f.what.c_str(), f.control.c_str(), f.text.c_str(), f.value);
            if (tsv)
                tsv << k << '\t' << name << '\t' << clean (f.control) << "\t\t\t\tBROKEN-" << f.what << '\t' << f.value << '\t' << clean (f.text) << "\t\t\n";
        }
        std::fflush (stdout);
    }
    std::printf ("\n%d effects, %d controls: %d dead, %d dependent\n", kinds, totalControls, totalDead, totalDep);
}
