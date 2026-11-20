// LYNX Model's presets (round 190ni): one per model type, each starting from its own fitted table's values (the
// recordings' mallet, strike point and damping), plus two variations. The first preset is the Marimba, so a new
// LYNX Model channel plays the marimba.

#include <utility>

#include "core/LynxBanks.h"
#include "core/physmod/LynxModel.h"

namespace lynx::banks
{
namespace
{
Preset model (const char* name, const char* category, int type, std::vector<std::string> tags,
              std::vector<std::pair<int, double>> changes = {})
{
    return { name, category, std::move (tags), [type, changes] (ChannelDef& c) {
                const auto v = lynxModelPreset (type);
                c.modelParams.assign (v.begin(), v.end());
                for (const auto& [index, value] : changes)
                    c.modelParams[(std::size_t) index] = (float) value;
            } };
}
} // namespace

std::vector<Preset> lynxModelBank()
{
    namespace p = lm::p;
    return {
        model ("Marimba", "Mallets", lm::marimba, { "marimba", "mallet", "wood", "physical model" }),
        model ("Vibraphone", "Mallets", lm::vibraphone, { "vibraphone", "vibes", "mallet", "metal", "physical model" }),
        model ("Xylophone", "Mallets", lm::xylophone, { "xylophone", "mallet", "wood", "physical model" }),
        model ("Glockenspiel", "Mallets", lm::glockenspiel, { "glockenspiel", "bells", "mallet", "metal", "physical model" }),
        model ("Tubular Bells", "Bells", lm::tubularBells, { "tubular bells", "chimes", "bells", "metal", "physical model" }),
        model ("Kalimba", "Pluck", lm::kalimba, { "kalimba", "mbira", "thumb piano", "pluck", "physical model" }),
        model ("Vibraphone Motor", "Mallets", lm::vibraphone, { "vibraphone", "vibes", "tremolo", "motor", "physical model" },
               { { p::motor, 5.5 }, { p::resonator, 0.6 } }),
        model ("Marimba Soft Mallets", "Mallets", lm::marimba, { "marimba", "soft", "mallet", "wood", "physical model" },
               { { p::hardness, 0.25 }, { p::noise, -6.0 } }),
    };
}
} // namespace lynx::banks
