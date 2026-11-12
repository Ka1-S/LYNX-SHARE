#pragma once

// One-knob effects (round 190bu, Simple layout S13; design: simple184a/DESIGN.md §3.3). A goal is a plain English word
// for a result ("Brighter", "Room"); its knob drives several parameters of ONE ordinary LYNX effect through the table
// in GoalKnobs.cpp. The slot stays that effect with two small fields (EffectSlot::goal, goalAmount), so the Mixer, the
// other layouts and the exports see a normal effect and its full editor is one click away.
//
//   Brighter  Aether            the two dynamic air bands
//   Echo      Echoform          mix, feedback, darker repeats; tempo-synced
//   Punch     Strike Shaper     attack up, sustain a little down, guard on
//   Room      Atrium            mix and decay; lows kept out
//   Bass      Facet EQ          a low shelf up, a little cut above it
//   Level     Glow Leveler      peak reduction with auto make-up
//   Dirty     Ember Saturator   hard drive, level-matched
//   Warm      Ember Saturator   gentle drive, softer highs
//
// Every knob at 0 leaves the sound as it was (wet 0, 0 dB or no drive). The laws are piecewise linear over the
// effect's own 0..1 parameter values; the values are LYNX's own, tuned by measurement (round 190bu: loudness on pink
// noise; round 190dg's gate: at every position on five test sounds the level stays within −6 … +3 dB of the clean
// sound, moves at most 1.5 dB per eighth of a turn, peaks rise at most 3 dB, and a knob turned while a sound plays makes
// no click — RackRenderer glides it; tests/SimpleGate190dgTests.inc). No JUCE.

#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::goals
{
enum Id : int { none = 0, brighter, echo, punch, room, bass, level, dirty, warm };
constexpr int count = 8; // ids 1…8

const char* name (int id) noexcept;        // English name (the UI translates it); "" for a bad id
const char* description (int id) noexcept; // one short line
EffectKind kindOf (int id) noexcept;       // the effect a goal's slot holds
float defaultAmount (int id) noexcept;     // where the knob starts when the effect is added
int idOfName (const std::string& name) noexcept; // 0 when no goal has that name

struct Move
{
    int param = 0;
    float value = 0.0f;
    bool operator== (const Move&) const = default;
};
// The parameters goal `id` writes with its knob at `amount` (0..1), in the table's order. Empty for a bad id.
std::vector<Move> mapping (int id, float amount);

// The same written into `params` (the effect's other parameters untouched). No allocation: the renderer glides a
// turned knob through it on the audio thread (round 190dg).
void writeParams (int id, float amount, EffectParams& params) noexcept;

// A new slot of the goal's effect with the knob at `amount` (< 0: the goal's default). An ordinary slot (kind eq, goal
// 0) for a bad id.
EffectSlot make (int id, float amount = -1.0f);
// Turns the knob: writes the mapped parameters and goalAmount. Nothing happens to a slot without a goal, or whose
// effect is not the goal's.
void setAmount (EffectSlot& slot, float amount);
// The slot is still a one-knob effect: it has a goal, holds the goal's effect, and the mapped parameters are where the
// knob put them (a parameter moved by hand in the full editor makes it an ordinary slot again; the fields stay, so
// turning the knob once more takes it back).
bool active (const EffectSlot& slot) noexcept; // (no allocation)
} // namespace lynx::goals
