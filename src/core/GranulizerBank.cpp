// LYNX's own Fruity Granulizer bank (round 168q). Designed for LYNX — never converted from FL's preset files. The
// granulizer plays the channel's own sample (core/Granulizer.h), so these presets are settings only: they keep the
// channel's sample and its channel-settings snapshot. Grain spacing against wave spacing is the playback speed
// (equal = the sample's own speed, smaller wave spacing = stretched, negative = backwards; REA_rodada67 §2).

#include <utility>
#include <vector>

#include "core/Granulizer.h"
#include "core/LynxBanks.h"

namespace lynx::banks
{
namespace
{
enum P { attack, hold, grainSpacing, waveSpacing, depth, speed, randomness, stereo, loop, freeze, start, transientHold };

struct Grains
{
    std::vector<std::pair<int, int>> params;   // parameter, value
    int slices = 0, keyTo = 0, tempoLock = 0;  // the editor's modes
};

Preset make (const char* name, const char* category, std::vector<std::string> tags, Grains g)
{
    Preset p;
    p.name = name, p.category = category, p.tags = std::move (tags);
    p.apply = [g] (ChannelDef& c) {
        auto s = granulizerNormalisedState (c.pluginState, {});
        if (s.empty())
            s = granulizerDefaultState();
        // every preset starts from the plugin's defaults, so the knobs it leaves out do not keep the last preset's
        const auto fresh = granulizerDefaultState();
        for (int k = 0; k < granulizerParamCount(); ++k)
            granulizerSetParam (s, k, *granulizerParam (fresh, k));
        for (const auto& [k, v] : g.params)
            granulizerSetParam (s, k, v);
        granulizerSetMode (s, 0, g.slices);
        granulizerSetMode (s, 1, g.keyTo);
        granulizerSetMode (s, 2, g.tempoLock);
        c.pluginState = std::move (s);
    };
    return p;
}
} // namespace

std::vector<Preset> granulizerBank()
{
    std::vector<Preset> bank;
    bank.push_back (make ("Init Grains", "Init", { "default" }, {}));
    bank.push_back (make ("Smooth Stretch", "Stretch", { "slow", "smooth" },
                          { { { attack, 380 }, { hold, 160 }, { grainSpacing, 420 }, { waveSpacing, 140 }, { stereo, 24 }, { loop, 1 } } }));
    bank.push_back (make ("Half Time", "Stretch", { "half speed" },
                          { { { attack, 260 }, { hold, 120 }, { grainSpacing, 600 }, { waveSpacing, 300 }, { loop, 1 } } }));
    bank.push_back (make ("Double Time", "Stretch", { "fast" },
                          { { { attack, 120 }, { hold, 60 }, { grainSpacing, 480 }, { waveSpacing, 960 } } }));
    bank.push_back (make ("Frozen Pad", "Pad", { "freeze", "drone" },
                          { { { attack, 500 }, { hold, 300 }, { grainSpacing, 350 }, { waveSpacing, 40 }, { randomness, 60 },
                              { stereo, 48 }, { freeze, 1 }, { loop, 1 } } }));
    bank.push_back (make ("Shimmer Cloud", "Pad", { "random", "wide" },
                          { { { attack, 440 }, { hold, 200 }, { grainSpacing, 300 }, { waveSpacing, 220 }, { depth, 160 }, { speed, 40 },
                              { randomness, 200 }, { stereo, 64 }, { loop, 1 } } }));
    bank.push_back (make ("Tape Wobble", "Texture", { "wobble", "lo-fi" },
                          { { { attack, 300 }, { hold, 150 }, { grainSpacing, 500 }, { waveSpacing, 500 }, { depth, 300 }, { speed, 16 },
                              { stereo, 12 }, { loop, 1 } } }));
    bank.push_back (make ("Backwards", "Texture", { "reverse" },
                          { { { attack, 300 }, { hold, 120 }, { grainSpacing, 611 }, { waveSpacing, -611 }, { stereo, 16 }, { start, 4000 },
                              { loop, 1 } } }));
    bank.push_back (make ("Stutter Glitch", "Rhythm", { "stutter", "glitch" },
                          { { { attack, 50 }, { hold, 300 }, { grainSpacing, 1000 }, { waveSpacing, 250 }, { randomness, 120 },
                              { stereo, 40 } } }));
    bank.push_back (make ("Grain Spray", "Texture", { "spray", "random" },
                          { { { attack, 80 }, { hold, 0 }, { grainSpacing, 160 }, { waveSpacing, 120 }, { randomness, 256 }, { stereo, 64 },
                              { loop, 1 } } }));
    bank.push_back (make ("Key Scrub", "Keys", { "scrub", "percent" },
                          { { { attack, 220 }, { hold, 100 }, { grainSpacing, 400 }, { waveSpacing, 60 }, { stereo, 20 }, { loop, 1 } },
                            0, 1, 0 }));
    bank.push_back (make ("Slice Player", "Rhythm", { "slices", "transients" },
                          { { { attack, 60 }, { hold, 60 }, { grainSpacing, 611 }, { waveSpacing, 611 }, { transientHold, 320 } }, 2, 3, 0 }));
    return bank;
}
} // namespace lynx::banks
