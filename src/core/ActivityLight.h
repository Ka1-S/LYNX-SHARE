#pragma once

// The Channel rack's activity light (round 190ih, PLAN R4: DC-8, DE-6). FL's manual (Channel Rack, "Channel Selector
// (outer border) / Activity indicator (inner region)") and the 190if capture of FL 2026
// (shots/fl-rack-selector-typingkey-before-during-after.png): a typing-keyboard note lights the channel's selector
// brightly at note-on, it dims while the note is held and goes dark after the release. The light follows every note
// the channel plays (song, pattern, typing keyboard, MIDI, Piano roll preview), not only the steps.
//
// One step per UI frame (the rack's 30 Hz timer). `strikes` is the engine's per-channel counter of blocks that struck a
// note (AudioEngine::noteStrikes): a change is a new note-on, even one shorter than a frame. With Settings ›
// Animations off the light is steady: on while a note sounds (or for the one frame of a short note), off otherwise.
// The decay constants are LYNX's reading of the capture (FL's exact timing is REA question Q-3, still open).

#include <cstdint>

namespace lynx::activity
{
constexpr float peak = 1.0f;        // at note-on
constexpr float held = 0.4f;        // a held note dims to this
constexpr float holdDecay = 0.78f;  // per frame, towards `held` (≈ 0.13 s to settle at 30 Hz)
constexpr float releaseDecay = 0.55f; // per frame after the release (dark in ≈ 0.15 s)
constexpr float off = 0.04f;        // below this the light is out

struct Light
{
    float level = 0.0f;
    std::uint32_t seen = 0;
    bool primed = false; // the first look only learns the counter (opening the rack lights nothing)
};

// advances one frame; returns the new level (0 … 1)
inline float step (Light& light, std::uint32_t strikes, bool sounding, bool fade) noexcept
{
    const auto struck = light.primed && strikes != light.seen;
    light.seen = strikes;
    light.primed = true;
    if (! fade)
        light.level = struck || sounding ? peak : 0.0f;
    else if (struck)
        light.level = peak;
    else if (sounding)
        light.level = light.level > held ? held + (light.level - held) * holdDecay : light.level;
    else
        light.level = light.level * releaseDecay < off ? 0.0f : light.level * releaseDecay;
    return light.level;
}

// the drawn shade (a repaint only when it changes)
inline int shade (float level) noexcept { return (int) (level * 24.0f + 0.5f); }
} // namespace lynx::activity
