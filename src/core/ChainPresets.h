#pragma once

// Chain presets, "sounds" for a track (round 190bu, Simple layout S14; design: simple184a/DESIGN.md §3.4). A preset is
// a short chain of LYNX's own effects — mostly one-knob effects (core/GoalKnobs.h), with a few of LYNX's plain effects —
// and a Blend, the dry/wet of the whole chain (MixerTrack::chainBlend). Choosing one replaces the insert's slots, so
// the Mixer and every other layout show the same effects in the same slots. Names, categories and values are LYNX's
// own. The user's own presets are mixer track states (.lynxtrack, engine/ProjectFile.h) in a folder of their own.
// No JUCE.

#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::chains
{
// One slot of a preset: a one-knob effect (`goal` and its knob), or a plain effect (`kind`) with some of its
// parameters named and set (0..1 values) and its slot mix.
struct Step
{
    int goal = 0;
    float amount = 0.5f;
    EffectKind kind = EffectKind::eq;
    float mix = 1.0f;
    std::vector<std::pair<const char*, float>> params;
};

struct Preset
{
    const char* name;        // English; the UI shows it as it is
    const char* category;    // "Voice", "Guitar", "Bass", "Drums", "Keys", "Lo-fi", "Space" (the UI translates it)
    const char* description; // one short line
    float blend;             // 0..1, the chain's dry/wet
    std::vector<Step> steps;
};

const std::vector<Preset>& factory();
const std::vector<const char*>& categories(); // in menu order
const Preset* find (const std::string& name); // nullptr when no factory preset has that name

// The preset's slots, in rows 0, 1, 2… (a named parameter the effect doesn't have is skipped)
std::vector<EffectSlot> slots (const Preset& preset);

// Puts the preset on insert `track`: its slots replace the insert's, Blend and the preset's name are set. False (and
// nothing changes) for a track the song doesn't have.
bool apply (Song& song, int track, const Preset& preset);
// The same for any chain (a user's preset): the slots as they are (at most FL's 10), rows kept when they are valid.
bool applyChain (Song& song, int track, std::vector<EffectSlot> chain, float blend, const std::string& name);

// The insert still holds what its preset put there: a factory preset of that name whose slots and Blend match (the
// "edited" mark of the preset box). False without a preset name; true for a name that is not a factory preset (a
// user's preset: its file is not read here).
bool unchanged (const Song& song, int track);

// What a user preset file keeps of an insert: its slots, Blend and preset name on an otherwise new track (no level,
// pan, EQ or routing), named `name`.
MixerTrack stateOf (const MixerTrack& track, const std::string& name);
} // namespace lynx::chains
