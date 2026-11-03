#pragma once

// FL Studio's Maximus and Soundgoodizer as LYNX mixer effects, on the exact engine of core/HarmlessFx (rebuilt in
// docs/analise/REA_rodada36.md and REA_rodada40.md; clean-room). The effect slot carries everything in
// EffectSlot::state as LYNX's own small container:
//
//   "LXMX", u8 container version (1), u8 build (0 = Maximus / Shared SG_x64.dll, 1 = Soundgoodizer_x64.dll's copy),
//   i32 band-mix parameter (−1 = the state's own parameter 5), then the Maximus state bytes.
//
// A Maximus slot holds FL's own saved state; a Soundgoodizer slot holds the preset file a…d.dat read at import from
// the user's FL installation (never from this repository) and its Amount as the band mix.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Effects.h"
#include "core/HarmlessFx.h"

namespace lynx::maximusfx
{
struct Blob
{
    hlfx::MaximusBuild build = hlfx::MaximusBuild::shared;
    int mix = -1; // parameter 5; −1 = the state's own value
    std::vector<std::uint8_t> state;
};

std::vector<std::uint8_t> pack (const Blob& blob);
std::optional<Blob> unpack (const std::vector<std::uint8_t>& bytes);

// The engine's preset for a container (nullptr when the state is not supported: see makeMaximusPreset)
std::shared_ptr<const hlfx::MaximusPreset> presetOf (const Blob& blob);

// Soundgoodizer's state (0xD5, 12 bytes): version, preset 0…3 (A…D → Data\Presets\a…d.dat), amount 0…1000
struct SoundgoodizerState
{
    int preset = 0, amount = 600;
};
std::optional<SoundgoodizerState> parseSoundgoodizer (const std::vector<std::uint8_t>& state);
const char* soundgoodizerPresetFile (int preset); // "a.dat" … "d.dat"

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& slotState);

// --- editing a Maximus slot (the native editor's page) -------------------------------------------------------
// The 59 parameters of the Maximus state (REA_rodada8 §1.2: 7 globals, then 13 per band low / mid / high / master),
// raw integer ranges and defaults as the plugin stores them. Index = the Maximus parameter number.
struct StateParam
{
    const char* name;
    int minimum, maximum, defaultValue;
};
const std::vector<StateParam>& stateParams();
int stateParamIndex (const char* name);                                     // −1 = no such parameter
std::optional<int> stateParam (const std::vector<std::uint8_t>& slotState, int index); // raw value
// the slot state with one parameter replaced (parameter 5 also updates the container's band mix when it is set)
std::vector<std::uint8_t> withStateParam (const std::vector<std::uint8_t>& slotState, int index, int raw);
std::string stateParamText (int index, int raw);                            // the value as the editor shows it
// The drawn compression curve of band 0…3 (cumulative x, y; both 0…1; x = log₃(1 + 2·level)/2, so 0.5 = 0 dBFS)
std::vector<std::pair<float, float>> curvePoints (const std::vector<std::uint8_t>& slotState, int band);
// --- the compression curve editor (round 168x, issue #40: FL's envelope editor gestures on the In/Out graph) ---
// A point of band 0…3's curve at its absolute position: x = input 0…1 (cumulative Δx), y = output 0…1, the tension
// and FL's segment type of the segment that ends here. The first point stays at x = 0 and the last at x = 1.
struct CurveEditPoint
{
    double x = 0.0;
    float y = 0.0f, tension = 0.0f;
    std::uint8_t type = 0;
};
std::vector<CurveEditPoint> curveForEditing (const std::vector<std::uint8_t>& slotState, int band);
// the slot state with the band's curve replaced (FL's layout; Δx of the points that did not move are kept bit-exact)
std::vector<std::uint8_t> withCurve (const std::vector<std::uint8_t>& slotState, int band, const std::vector<CurveEditPoint>& points);
// edits; each returns the index of the point it touched (−1 = nothing done). A point can't pass its neighbours; the
// first and last points only move up and down and can't be removed.
int addCurvePoint (std::vector<CurveEditPoint>& points, double x, float y, float tension = 0.0f, std::uint8_t type = 0);
int moveCurvePoint (std::vector<CurveEditPoint>& points, int index, double x, float y);
bool removeCurvePoint (std::vector<CurveEditPoint>& points, int index);
// the curve as the engine plays it, n + 1 samples over x 0…1 (empty: a segment type LYNX's engine doesn't play)
std::vector<float> curveShape (const std::vector<CurveEditPoint>& points, int n);
// the curve's axes as levels: 0.5 = 0 dBFS, 1 = +9.5 dB (level = (3^(2v) − 1) / 2); "-inf" at 0
std::string curveLevelText (double v);

// The band selected in FL's editor when the state was saved (UI block +0x1E0), 0…3
int selectedBand (const std::vector<std::uint8_t>& slotState);
} // namespace lynx::maximusfx
