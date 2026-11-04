// Round 169l: Drumaxx's level through FL's host pan law, a stateless FL Drumaxx, new generators' default state
// (REA_rodada169l)

#include "core/Drumaxx.h"
#include "core/NativeEngines.h"

TEST (gen169l_probe_new_generator_presets)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    for (const auto& e : natives::instrumentEngines())
    {
        const auto c = natives::presetCandidates (e.presetFolder, true, fl);
        std::string used;
        int tries = 0;
        for (const auto& p : c)
        {
            if (++tries > 60)
                break;
            if (natives::channelFromPresetFile (e.kind, p, fl)) { used = p; break; }
        }
        std::printf ("  PROBE %-18s first=%s used=%s\n", e.name, c.empty() ? "-" : c.front().c_str(), used.c_str());
    }
}
