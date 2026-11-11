#pragma once

// FL's Slicex voice engine (round 190h, docs/analise/REA_rodada190h.md Parts 2 and 3): what a note plays. A note picks
// the regions mapped to its key on a deck (note colour: even = deck A, odd = deck B; colours 14 and 15 play reversed),
// layers them by the layering mode and crossfade, and each region becomes a voice: FL's sample resampler on the
// region, the region's pan / volume / pitch / delay / start, the six articulators of its groups (envelope, LFO and
// maps on pan, volume, cutoff, resonance, speed, start) stepped once per sub-block, the group's filter (1…3 biquad
// stages), FL's declick (4 ms attack DC cancel, 20 ms release) and the 4-gain pan matrix with ramped gains.
//
// A Program is everything prepared from a state and the decks' audio (off the audio thread); the Engine plays it
// without allocating.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/Slicex.h"

namespace lynx::slicex
{
// A deck's audio: planar float frames (right == nullptr: mono) kept alive by `owner`, and its regions
struct DeckAudio
{
    std::shared_ptr<const void> owner;
    const float* left = nullptr;
    const float* right = nullptr;
    std::int64_t frames = 0;
    double sampleRate = 44100.0;
    std::vector<WavRegion> regions;
};
// Regions for a sample that only has marker positions (its cue points): each runs to the next one
std::vector<WavRegion> regionsFromMarkers (const std::vector<std::int64_t>& starts, std::int64_t frames);

struct Program;
std::shared_ptr<Program> makeProgram (const State& state, DeckAudio deckA, DeckAudio deckB);

// What FL's host hands the plugin with a note
struct Note
{
    int key = 60;
    float velocity = 0.78125f;  // 0…1 (the velocity map's input)
    int colour = 0;             // 0…15
    float vol = 1.0f;           // the voice's final volume: channel volume × the note's level (FL's Init.Vol law, by the host)
    float pan = 0.0f;           // final pan −1…1 (channel + note)
    float pitchCents = 0.0f;    // final pitch relative to C5: 100·(key − 60) + channel pitch + the note's fine pitch
    float modX = 0.0f, modY = 0.0f; // the note's Mod X / Y, −1…1 (0 = none)
};

class Engine
{
public:
    static constexpr int maxVoices = 96;

    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    // (audio thread; the previous program is returned to be freed elsewhere)
    std::shared_ptr<Program> setProgram (std::shared_ptr<Program> program) noexcept;
    void setRate (int sampleRate, bool rendering) noexcept; // FL's sample-rate message (with the host's rendering flag)
    void setTempo (float bpm) noexcept;
    void setInterpolation (int quality) noexcept;            // FL's interpolation index (core/FlResampler.h)

    int trigger (const Note& note) noexcept;                 // → the note's id (−1: nothing mapped to the key)
    void release (int id) noexcept;
    void releaseAll() noexcept;
    void killAll() noexcept;
    // the host's final levels of a sounding note change (channel volume, pan, pitch)
    void setNoteLevels (int id, float vol, float pan, float pitchCents) noexcept;
    bool noteActive (int id) const noexcept;
    bool idle() const noexcept;
    int voiceCount() const noexcept;

    // adds `frames` stereo frames to left / right
    void render (float* left, float* right, int frames) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace lynx::slicex
