#pragma once

// Channel states built from parameter values for LYNX's own preset banks of FL's drum and physical-model engines
// (round 168p): the same state layouts LYNX reads from FL projects (Fruit Kick version 0, Plucked!'s five integers,
// Drumpad / Sakura model parameters, Drumaxx's "SSLF" + DMS4 kit stream, DrumSynth Live's 120 patches), filled with
// LYNX's own values — no FL preset is copied or converted. No JUCE.

#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::bankstates
{
std::vector<std::uint8_t> fruitKickState (const std::vector<int>& sixParams);
std::vector<std::uint8_t> pluckedState (const std::vector<int>& fiveParams);

// a Drumaxx pad: its Drumpad-style voice (Drumpad's 38 parameters: Drumaxx keeps them at pad offset 5…42, the morph
// targets at 35…38) and the pad's own settings
struct DrumaxxPad
{
    std::string name;
    std::vector<float> voice; // 38 values as Drumpad's (index 0/1: retriggers, 30…33: morph targets, raw)
    float volume = 0.8f, pan = 0.5f;
    int key = 36;
    bool mute = false;
};
// A Drumaxx kit state (DMS4 stream: name, 720 parameters, 16 empty patterns with trigger key 100, pad names, cut
// groups, output buses); unused pads stay silent (volume 0). Master level, EQ (8 band gains, 0.5 = 0 dB) and limiter.
std::vector<std::uint8_t> drumaxxState (const std::string& kitName, const std::vector<DrumaxxPad>& pads, float master = 0.55f,
                                        bool limiter = true, float limiterDrive = 0.3f, const std::vector<float>& eq = {});

// DrumSynth Live: a kit of patches on keys (each 38 raw integers, as FL's patch layout) → modelParams (patch slots for
// the keys, the others silent; the editor key = the first patch's key)
struct DrumSynthPatch
{
    int key = 48;
    std::vector<int> values; // 38 raw integers
};
std::vector<float> drumSynthParams (const std::vector<DrumSynthPatch>& patches);

// A channel of `kind` ("fruitkick", "plucked", "drumpad", "sakura") from a list of parameter values (the bank audit
// tool's text format); nullopt for another kind or too few values.
std::optional<ChannelDef> channelFor (const std::string& kind, const std::vector<float>& values);
} // namespace lynx::bankstates
