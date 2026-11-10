#pragma once

// Master looks (round 189k, Simple layout S8): the Finish dock's one-click mastering. A look is a short chain of FL's
// own effects at the end of the Master insert, each slot marked with the look's id (EffectSlot::look):
//   Fruity Parametric EQ 2 -> Fruity Multiband Compressor -> Fruity Stereo Enhancer or Fruity Soft Clipper -> Fruity Limiter
// All four run LYNX's exact engines of those plugins, so the chain plays without FL installed, stays editable in the
// Mixer, and the FL export writes the four plugins with their settings (core/FlStateWrite.h).
// The values of each look are LYNX's own, set at Light (intensity 0.25) and Heavy (0.85) and interpolated; intensity 0
// is a neutral chain. Tone = the EQ's low shelf, mid bell and high shelf (±6 dB). The input gain (Auto level) is the
// EQ's main level. No JUCE.

#include <array>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::looks
{
enum Id : int { none = 0, balanced, loud, clarity, warmTape, gentle, punchy, wide, cinematic };
constexpr int count = 8;     // ids 1…8
constexpr int chainSlots = 4;

const char* name (int id) noexcept;        // English name (the UI translates it)
const char* description (int id) noexcept; // one short line for the tile's tooltip

// The look's four slots for these settings (each slot's `look` = settings.id). Empty for id 0.
std::vector<EffectSlot> chain (const MasterLookSettings& settings);

// Puts the look's chain on the Master insert: an existing look is replaced in place; otherwise the slots go after the
// Master's last effect. False (and nothing changed) when the Master has no room for four more slots.
bool apply (Song& song, const MasterLookSettings& settings);
void remove (Song& song); // takes every look slot off the Master and clears song.masterLook

bool present (const Song& song);         // the Master has look slots
bool custom (const Song& song);          // look slots edited by hand (they differ from chain (song.masterLook))
bool bypassed (const Song& song);        // "Hear without": every look slot is off
void setBypassed (Song& song, bool off); // the A/B switch: all look slots off / on as one

// Auto level: the input gain (dB, −18…+18) that brings a mix measured at `inputLufs` (integrated, before the look) to
// the level the looks are tuned for. Silence (or no measurement, lufs < −70) gives 0.
constexpr double referenceLufs = -18.0;
float autoInputGainDb (double inputLufs) noexcept;
} // namespace lynx::looks
