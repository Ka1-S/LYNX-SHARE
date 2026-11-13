#pragma once

// Master looks (round 189k, Simple layout S8): the Finish dock's one-click mastering. A look is a short chain of FL's
// own effects at the end of the Master insert, each slot marked with the look's id (EffectSlot::look):
//   Fruity Parametric EQ 2 -> Fruity Multiband Compressor -> Fruity Stereo Enhancer or Fruity Soft Clipper -> Fruity Limiter
//   -> Apex Limiter (true peak, round 190ea)
// The first four run LYNX's exact engines of those plugins, so the chain plays without FL installed, stays editable in the
// Mixer, and the FL export writes the four plugins with their settings (core/FlStateWrite.h). Fruity Limiter holds sample
// peaks only: on bright mixes the peaks between the samples went up to +3.3 dBTP over its ceiling (round 190dv/190ea
// measurements). The fifth slot, LYNX's Apex Limiter with True peak on at 0 dB gain, ends every look at
// trueCeilingDb (dBTP): it only touches those inter-sample overs. FL has no true-peak limiter of its own, so the FL
// export leaves that slot out (and says so in its report). Looks applied before round 190ea keep their four saved
// slots; applying a look again (another look, intensity, tone, Auto level) adds the fifth.
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
constexpr int chainSlots = 5; // (4 before round 190ea: legacySlots)
constexpr int legacySlots = 4;
constexpr float trueCeilingDb = -1.0f;   // every look's true-peak ceiling (dBTP)
constexpr float truePeakMarginDb = 0.05f; // the Apex slot's ceiling sits this much lower (its guard's 16× tolerance)

const char* name (int id) noexcept;        // English name (the UI translates it)
const char* description (int id) noexcept; // one short line for the tile's tooltip

// The true-peak stage that ends every look (an ordinary slot here: `look` 0): Apex Limiter, 0 dB gain, True peak on,
// ceiling trueCeilingDb − truePeakMarginDb. Also the last slot of LYNX's showcase song's Master (core/ShowcaseSong.h).
EffectSlot truePeakStage();

// The look's five slots for these settings (each slot's `look` = settings.id). Empty for id 0.
std::vector<EffectSlot> chain (const MasterLookSettings& settings);

// Puts the look's chain on the Master insert: an existing look is replaced in place (a four-slot look of an older project
// too: its true-peak slot goes right after its limiter when that row is free); otherwise the slots go after the
// Master's last effect. False (and nothing changed) when the Master has no room for five more slots.
bool apply (Song& song, const MasterLookSettings& settings);
void remove (Song& song); // takes every look slot off the Master and clears song.masterLook

bool present (const Song& song);         // the Master has look slots
bool custom (const Song& song);          // look slots edited by hand (they differ from chain (song.masterLook); a
                                         // four-slot look of an older project is compared with the chain's first four)
bool bypassed (const Song& song);        // "Hear without": every look slot is off
void setBypassed (Song& song, bool off); // the A/B switch: all look slots off / on as one

// Auto level: the input gain (dB, −18…+18) that brings a mix measured at `inputLufs` (integrated, before the look) to
// the level the looks are tuned for. Silence (or no measurement, lufs < −70) gives 0.
constexpr double referenceLufs = -18.0;
float autoInputGainDb (double inputLufs) noexcept;
} // namespace lynx::looks
