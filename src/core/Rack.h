#pragma once

// Racks (round 183c, docs/analise/REA_rodada183c.md): Live's Instrument, Audio Effect and MIDI Effect Racks as a Patcher
// in rack mode. The RackDoc (core/Song.h) holds the chains, zones, macros and variations; the Patcher's nodes are the
// rack's devices plus one "rack chain mixer" node per chain (and an input gate per Audio Effect Rack chain). compile()
// builds the Patcher's cables, Surface and published parameters from the RackDoc, so a rack runs on patcher::Graph
// (plugins, instruments, VFX note effects, nested racks, delay compensation) with nothing of its own on the audio
// thread except the note zones (Graph) and the chain mixer (an effect).
//
// Behaviour from Live (clean-room: manual, Live's own set XML, measured renders; REA §1–5):
//  - every chain gets the same input; the chains' outputs are summed;
//  - zones: lo ≤ fadeLo ≤ fadeHi ≤ hi; key and velocity fades scale the velocity of the notes entering the chain; the
//    chain-select fade scales a MIDI Effect Rack's velocities and an Instrument / Audio Effect Rack's chain output;
//  - a macro moves each mapped parameter linearly in its 0..1 range from `min` to `max`; one macro per parameter;
//  - Randomize sets every mapped macro not excluded; a variation stores macro values and its launch leaves excluded
//    macros and those it holds no value for alone.
// The fade laws, the chain pan law and the volume taper are measured on Live 12.4 renders (REA §8).

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
class Effect;
}

namespace lynx::rack
{
// The Surface of a compiled rack: outputs 0…15 are the macros, 16 the chain selector, 17 the morph (LYNX's own).
// The published parameters (automation) have the same indices.
constexpr int selectorOutput = rackMacroCount;
constexpr int morphOutput = rackMacroCount + 1;
constexpr int surfaceOutputs = rackMacroCount + 2;

// The chain mixer node's parameters (EffectKind::rackMixer)
constexpr int mixerVolume = 0, mixerPan = 1, mixerSelect = 2;
constexpr float unityVolume = 0.85f; // 0 dB on Live's chain volume taper

// The "rack chain mixer" effect (EffectKind::rackMixer): volume × chain-select gain, then pan, ramped over each block
std::unique_ptr<Effect> makeChainMixer();

// Live's chain-volume taper (REA §4 [DATA]: 40 dB per unit above −18 dB, 0 dB at 0.85, +6 dB at 1; below, Live's stored
// points down to −70 dB at the bottom, Live's floor): 0..1 → linear gain (0 = silence), and back.
float volumeGain (float v) noexcept;
float volumeValue (float gain) noexcept;
// The chain mixer's pan law: −1..1 → left / right gains (Live's, measured: equal power, +3 dB at the ends)
void panGains (float pan, float& left, float& right) noexcept;

// A zone's note fade at x (0…127): 1 on [fadeLo, fadeHi], 0 outside [lo, hi], linear in the fades counting both ends
// ((x − lo + 1) / (fadeLo − lo + 1)); it scales the velocity of the notes entering the chain.
float fadeGain (const RackZone& zone, float x) noexcept;
bool inZone (const RackZone& zone, float x) noexcept;
// Whether the zone fades on the side x is on (outside it): Live attenuates an Instrument / Audio Effect Rack chain to
// silence outside a zone only on a side that ends in a fade (REA §3).
bool fadesTowards (const RackZone& zone, float x) noexcept;
// The chain-select gain of a chain's output (Instrument and Audio Effect Racks) at selector value x: sin (π/2 · fade)
float selectOutputGain (const RackZone& zone, float x) noexcept;
// Whether an Audio Effect Rack chain's input is open at selector value x (the zone without a fade closes the input,
// so the chain's tails ring out)
bool selectInputOpen (const RackZone& zone, float x) noexcept;
// A note's velocity through a chain's zones (0 = it doesn't enter): key and velocity on the note, the chain-select
// zone at selector value `selector` (it scales velocity only in a MIDI Effect Rack). Velocity 0..1.
float zoneVelocity (const PatchZone& zone, int key, float velocity, float selector) noexcept;
// The selector value a compiled zone reads, from its Surface value (0..1) through its mapping range
float zoneSelector (const PatchZone& zone, float surfaceValue) noexcept;

// ---------------------------------------------------------------------------------------------------------------
// Documents

// A new rack: one empty chain, 8 macros shown, the views Live opens a new rack with
PatcherDoc makeRack (RackDoc::Kind kind);
// The mixer-slot effect / Channel Rack channel holding a new rack
EffectSlot makeRackSlot (RackDoc::Kind kind = RackDoc::Kind::audioEffect);
const RackDoc* rackOf (const PatcherDoc& doc) noexcept;
RackDoc* rackOf (PatcherDoc& doc) noexcept;
bool isRack (const EffectSlot& slot) noexcept;
bool isRack (const ChannelDef& channel) noexcept;

// Builds the cables, Surface, published parameters, note zones and node places from the RackDoc (a plain Patcher is
// left untouched). Called after every edit; cheap.
void compile (PatcherDoc& doc);

// ---------------------------------------------------------------------------------------------------------------
// Chains and devices (each recompiles)

int addChain (PatcherDoc& doc, const std::string& name = {});
bool removeChain (PatcherDoc& doc, int chain);
int duplicateChain (PatcherDoc& doc, int chain);
bool moveChain (PatcherDoc& doc, int from, int to);
// A device into chain `chain` at `position` (−1 = last; an Instrument Rack keeps MIDI effects, the instrument, then
// audio effects in that order: the node goes to the first place that keeps it). Returns its node index, or −1.
int addDevice (PatcherDoc& doc, int chain, PatchNode node, int position = -1);
bool removeDevice (PatcherDoc& doc, int chain, int position);
bool moveDevice (PatcherDoc& doc, int chain, int from, int to);
// The device nodes of a chain, in order (without its mixer and gate)
const std::vector<int>& devices (const RackDoc& rack, int chain);
// Every node the rack uses for chain bookkeeping (mixers, gates): not user devices
bool isChainNode (const RackDoc& rack, int node) noexcept;

void setSolo (PatcherDoc& doc, int chain, bool solo);
void setActive (PatcherDoc& doc, int chain, bool active);
void setChainVolume (PatcherDoc& doc, int chain, float gain);
void setChainPan (PatcherDoc& doc, int chain, float pan);
void setSelector (PatcherDoc& doc, float value);

// Group: effect slots into a new Audio Effect Rack with one chain holding them (Live's Group, Ctrl+G). Ungroup: a
// one-chain rack's devices back as slots (empty when the rack has several chains or isn't an effect rack).
EffectSlot group (const std::vector<EffectSlot>& slots);
// A plain Patcher of effects in series (what Patcherize makes, core/Patcherize.h) turned into an Audio Effect Rack with one
// chain: its published parameters 0…15 become macros 0…15 on the same parameters (full range), so automation and
// links that drove them keep working. False (document untouched) for a side-chain, a generator, a branch, more than 16
// published parameters or a curved one. A hosted plugin's parameter needs its current value (`pluginValue (node,
// param)`, 0..1; −1 = unknown → false), so the macro starts where the plugin is and grouping changes no sound.
bool fromPatcher (PatcherDoc& doc, const std::function<float (int node, int param)>& pluginValue = {});
std::vector<EffectSlot> ungroup (const EffectSlot& rackSlot);

// ---------------------------------------------------------------------------------------------------------------
// Macros

// Maps macro `macro` onto a target (a target belongs to one macro: an earlier mapping of it is removed). False if the
// macro or the target is invalid.
bool map (PatcherDoc& doc, int macro, const RackMapping& mapping);
bool unmap (PatcherDoc& doc, int macro, int mapping);
bool invertRange (PatcherDoc& doc, int macro, int mapping);
// The macro mapping a target (−1: none), and which of its mappings
int macroOf (const RackDoc& rack, const RackMapping& target, int* mappingIndex = nullptr) noexcept;
bool sameTarget (const RackMapping& a, const RackMapping& b) noexcept;
// Sets a macro (0…127) and what it drives in the RackDoc (chain volume / pan, selector, device parameters)
void setMacro (PatcherDoc& doc, int macro, float value);
// The value a mapping gives its target for a macro at `value` (0…127)
float mappedValue (const RackMapping& m, float value) noexcept;
// The macro's shown name: its own, else the one mapped parameter's (`paramName`), else "Macro N"
std::string macroName (const RackDoc& rack, int macro, const std::string& paramName = {});
// Randomize (Live's Rand): every mapped macro not excluded gets a random value; `seed` advances. Returns how many.
int randomize (PatcherDoc& doc, std::uint32_t& seed);
// The shown-macro counts Live offers (1, 2, 4 … 16): the next one up / down
int moreMacros (int shown) noexcept;
int fewerMacros (int shown) noexcept;

// ---------------------------------------------------------------------------------------------------------------
// Variations (Live's Macro Variations) and LYNX's morph (decision D6, off by default)

int storeVariation (PatcherDoc& doc);                // New: "Variation N"
bool overwriteVariation (PatcherDoc& doc, int index);
bool launchVariation (PatcherDoc& doc, int index);   // instant (Live), unless the morph's glide is set (the UI glides)
bool renameVariation (PatcherDoc& doc, int index, const std::string& name);
int duplicateVariation (PatcherDoc& doc, int index);
bool deleteVariation (PatcherDoc& doc, int index);
// The macro values of the morph between variations A and B at `amount` (0..1): A's value moving linearly to B's for
// every macro both hold and that isn't excluded; the others keep `current`.
std::array<float, rackMacroCount> morphValues (const RackDoc& rack, float amount) noexcept;
void setMorph (PatcherDoc& doc, float amount);
} // namespace lynx::rack
