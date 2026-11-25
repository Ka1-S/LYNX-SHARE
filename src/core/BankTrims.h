#pragma once

// How much louder or quieter than its design a bank's preset is written, in dB, so that a bank's presets are about as loud
// (round 190rh: measured on LYNX's own renders of a phrase, BS.1770, aiming at -16 LUFS and peaks under full scale).

#include <cstring>

namespace lynx::banks
{
inline double trimDb (const char* engine, const char* preset)
{
    struct Row
    {
        const char *engine, *preset;
        double db;
    };
    static const Row rows[] = {
#include "core/BankTrims.inc"
        { "", "", 0.0 },
    };
    for (const auto& r : rows)
        if (std::strcmp (r.engine, engine) == 0 && std::strcmp (r.preset, preset) == 0)
            return r.db;
    return 0.0;
}
} // namespace lynx::banks
