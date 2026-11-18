// Round 190kx: the loop analysis accuracy table — key and tempo detection over LYNX's own synthetic corpus
// (tests/LoopCorpus190kx.h). Build (from the repo root; the linker flag skips the parts of AudioEditTools it never calls):
//   g++ -std=c++20 -O2 -Isrc -Itests tools/loop_accuracy.cpp src/core/KeyFollow.cpp src/core/TempoDetect.cpp \
//       src/core/LoopAnalysis.cpp src/core/AudioEditTools.cpp src/core/NoteTools.cpp src/core/AutoWarp.cpp \
//       -Wl,--unresolved-symbols=ignore-in-object-files -o loop_accuracy
// Run: loop_accuracy [--legacy] [--list] [--filter=drums-rock] [--sweep | --tempo-sweep]
//   --legacy: keyfollow::detectKey and tempodetect::detect, the detectors before round 190kx
//   --sweep / --tempo-sweep: key / tempo settings tried (a "!" marks a class where a setting is worse than the legacy)
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>

#include "LoopCorpus190kx.h"
#include "core/KeyFollow.h"
#include "core/LoopAnalysis.h"
#include "core/TempoDetect.h"

namespace
{
const char* tempoClass (double got, double want)
{
    if (! (got > 0.0))
        return "none";
    const auto r = got / want;
    const auto near = [r] (double x) { return std::abs (r / x - 1.0) < 0.01; };
    if (near (1.0))
        return "exact";
    if (near (2.0) || near (0.5))
        return "octave";
    if (near (1.5) || near (2.0 / 3.0) || near (3.0) || near (1.0 / 3.0) || near (0.75) || near (4.0 / 3.0))
        return "third";
    return "other";
}

const char* keyClass (int gotRoot, bool gotMinor, bool got, int root, bool minor)
{
    if (root < 0)
        return got ? "false" : "none-ok";
    if (! got)
        return "none";
    if (gotRoot == root && gotMinor == minor)
        return "exact";
    const auto rel = [] (int r, bool m) { return (r + (m ? 3 : 0)) % 12; };
    if (rel (gotRoot, gotMinor) == rel (root, minor))
        return "relative";
    if (gotMinor == minor && ((gotRoot - root + 12) % 12 == 7 || (gotRoot - root + 12) % 12 == 5))
        return "fifth";
    if (gotRoot == root)
        return "parallel";
    return "other";
}
} // namespace

int main (int argc, char** argv)
{
    bool legacy = false, list = false, sweep = false, tempoSweep = false;
    std::string filter;
    for (int i = 1; i < argc; ++i)
    {
        legacy |= std::strcmp (argv[i], "--legacy") == 0, list |= std::strcmp (argv[i], "--list") == 0, sweep |= std::strcmp (argv[i], "--sweep") == 0,
        tempoSweep |= std::strcmp (argv[i], "--tempo-sweep") == 0;
        if (std::strncmp (argv[i], "--filter=", 9) == 0)
            filter = argv[i] + 9;
    }
    auto items = loopcorpus::corpus (false, filter);
    if (! filter.empty())
        items.erase (std::remove_if (items.begin(), items.end(), [&] (const loopcorpus::Item& it) { return it.name.find (filter) == std::string::npos; }), items.end());
    if (tempoSweep)
    {
        // per class: the legacy detector's exact count, then each setting's (a "!" marks a class worse than legacy)
        std::map<std::string, int> legacyExact;
        for (const auto& it : items)
            if (it.bpm > 0.0)
            {
                const auto r = lynx::tempodetect::detect (it.audio->leftData(), it.audio->isStereo() ? it.audio->rightData() : nullptr, it.audio->frames(), it.audio->sampleRate);
                legacyExact[(it.name.substr (0, it.name.find ('-', it.name.find ('-') + 1)))] += std::string (tempoClass (r.bpm, it.bpm)) == "exact";
            }
        int legacyTotal = 0;
        for (auto& [k, v] : legacyExact)
            legacyTotal += v;
        std::printf ("legacy total %d\n", legacyTotal);
        for (double share : { 0.6, 0.75 })
            for (double sigma : { 0.8 })
                for (double ratio : { 1.5 })
                    for (double level : { 2.0 })
                    {
                        auto& o = lynx::loopanalysis::tempoOptions();
                        o.loopShare = share, o.priorSigma = sigma, o.backbeatRatio = ratio, o.kickRatio = level;
                        std::map<std::string, int> exact;
                        int total = 0;
                        for (const auto& it : items)
                            if (it.bpm > 0.0)
                            {
                                const auto t = lynx::loopanalysis::detectTempo (*it.audio);
                                const auto ok = t.confidence >= o.minConfidence && std::string (tempoClass (t.bpm, it.bpm)) == "exact";
                                exact[(it.name.substr (0, it.name.find ('-', it.name.find ('-') + 1)))] += ok;
                                total += ok;
                            }
                        std::string worse;
                        for (auto& [k, v] : legacyExact)
                            if (exact[k] < v)
                                worse += " !" + k + " " + std::to_string (exact[k]) + "<" + std::to_string (v);
                        std::printf ("share %.2f sigma %.1f ratio %.2f kick %.1f | exact %d%s\n", share, sigma, ratio, level, total, worse.c_str());
                        std::fflush (stdout);
                    }
        return 0;
    }
    if (sweep)
    {
        // key settings tried on the tonal and mixed loops (and the drums for false keys): one line each
        struct Try
        {
            const char* label;
            lynx::loopanalysis::KeyOptions o;
        };
        std::vector<Try> tries;
        for (int frames : { 1, 2 })
            for (double startWeight : { 2.0 })
                for (double startSeconds : { 0.5 })
                    for (double margin : { 0.05 })
                    {
                        lynx::loopanalysis::KeyOptions o;
                        o.hpssFrames = frames;
                        o.tonicStart = startWeight, o.startSeconds = startSeconds, o.minMargin = margin;
                        tries.push_back ({ "", o });
                    }
        // the legacy detector's exact keys per class (a "!" marks a setting worse than it in a class)
        std::map<std::string, int> legacyExact;
        for (const auto& it : items)
            if (it.keyRoot >= 0)
            {
                const auto d = lynx::keyfollow::detectKey (*it.audio);
                legacyExact[it.name.substr (0, it.name.find ('-', it.name.find ('-') + 1))] += d.key && d.key->root == it.keyRoot && d.key->minor == it.minor;
            }
        for (const auto& t : tries)
        {
            lynx::loopanalysis::keyOptions() = t.o;
            int exact = 0, relative = 0, fifth = 0, other = 0, none = 0, falseKey = 0, keyed = 0, unkeyed = 0;
            std::map<std::string, int> classExact;
            for (const auto& it : items)
            {
                const auto d = lynx::loopanalysis::detectKey (*it.audio);
                const auto c = std::string (keyClass (d.key ? d.key->root : -1, d.key && d.key->minor, d.key.has_value(), it.keyRoot, it.minor));
                if (it.keyRoot >= 0)
                    classExact[it.name.substr (0, it.name.find ('-', it.name.find ('-') + 1))] += c == "exact";
                if (it.keyRoot >= 0)
                    ++keyed, exact += c == "exact", relative += c == "relative", fifth += c == "fifth", other += c == "other" || c == "parallel", none += c == "none";
                else
                    ++unkeyed, falseKey += c == "false";
            }
            std::string worse;
            for (auto& [k, v] : legacyExact)
                if (classExact[k] < v)
                    worse += " !" + k + " " + std::to_string (classExact[k]) + "<" + std::to_string (v);
            std::printf ("hpss %d start %.1f x %.2f s margin %.2f | exact %3d/%d rel %3d fifth %2d other %3d none %3d | false %3d/%d%s\n", t.o.hpssFrames, t.o.tonicStart,
                         t.o.startSeconds, t.o.minMargin, exact, keyed, relative, fifth, other, none, falseKey, unkeyed, worse.c_str());
            std::fflush (stdout);
        }
        return 0;
    }
    // per class of loop (drums style / tonal part / mixed style / one-shot): counts per result class
    std::map<std::string, std::map<std::string, int>> keyTable, tempoTable, wholeTable;
    double seconds = 0.0, audio = 0.0;
    for (const auto& it : items)
    {
        const auto t0 = std::chrono::steady_clock::now();
        int kr = -1;
        bool km = false, kg = false;
        double bpm = 0.0;
        bool whole = false;
        if (legacy)
        {
            const auto d = lynx::keyfollow::detectKey (*it.audio);
            if (d.key)
                kr = d.key->root, km = d.key->minor, kg = true;
            const auto r = lynx::tempodetect::detect (it.audio->leftData(), it.audio->isStereo() ? it.audio->rightData() : nullptr, it.audio->frames(),
                                                            it.audio->sampleRate);
            bpm = r.bpm;
            whole = r.beats >= 3.9 && std::abs (r.beats - std::round (r.beats)) <= 0.04 && r.confidence >= 0.35;
        }
        else
        {
            const auto a = lynx::loopanalysis::analyse (*it.audio, it.name + ".wav", false);
            if (a.key)
                kr = a.key->root, km = a.key->minor, kg = true;
            bpm = a.bpm;
            whole = a.wholeBeats;
        }
        seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        audio += (double) it.audio->frames() / it.audio->sampleRate;
        const auto group = (it.kind == loopcorpus::Kind::drums ? "drums " : it.kind == loopcorpus::Kind::tonal ? "tonal " : it.kind == loopcorpus::Kind::mixed ? "mixed " : "1shot ") + it.style;
        const auto kc = keyClass (kr, km, kg, it.keyRoot, it.minor);
        keyTable[group][kc]++, keyTable["ALL"][kc]++;
        if (it.bpm > 0.0)
        {
            const auto tc = tempoClass (bpm, it.bpm);
            tempoTable[group][tc]++, tempoTable["ALL"][tc]++;
            const auto ok = whole && std::abs (bpm * it.beats / it.bpm - std::round (bpm * it.beats / it.bpm)) < 0.05;
            wholeTable[group][ok ? "whole-ok" : "whole-miss"]++, wholeTable["ALL"][ok ? "whole-ok" : "whole-miss"]++;
        }
        else
        {
            wholeTable[group][whole ? "whole-false" : "whole-none-ok"]++;
        }
        if (list)
            std::printf ("%-36s bpm %7.2f (want %6.1f) %-6s | key %-3s%s (want %s%s) %s\n", it.name.c_str(), bpm, it.bpm, tempoClass (bpm, it.bpm > 0 ? it.bpm : 1),
                         kg ? loopcorpus::noteName (kr) : "-", kg && km ? "m" : "", it.keyRoot >= 0 ? loopcorpus::noteName (it.keyRoot) : "-",
                         it.minor ? "m" : "", kc);
    }
    const auto print = [] (const char* title, std::map<std::string, std::map<std::string, int>>& t, std::vector<std::string> cols) {
        std::printf ("\n%s\n%-18s", title, "class");
        for (auto& c : cols)
            std::printf (" %9s", c.c_str());
        std::printf ("\n");
        for (auto& [g, row] : t)
        {
            int n = 0;
            for (auto& [c, v] : row)
                n += v;
            std::printf ("%-18s", g.c_str());
            for (auto& c : cols)
                std::printf (" %9s", row.count (c) ? (std::to_string (row[c]) + "/" + std::to_string (n)).c_str() : "-");
            std::printf ("\n");
        }
    };
    print ("KEY", keyTable, { "exact", "relative", "fifth", "parallel", "other", "none", "none-ok", "false" });
    print ("TEMPO", tempoTable, { "exact", "octave", "third", "other", "none" });
    print ("WHOLE BEATS", wholeTable, { "whole-ok", "whole-miss", "whole-none-ok", "whole-false" });
    std::printf ("\n%zu items, %.1f s of audio analysed in %.2f s (%.1f ms per second of audio)\n", items.size(), audio, seconds, 1000.0 * seconds / audio);
}
