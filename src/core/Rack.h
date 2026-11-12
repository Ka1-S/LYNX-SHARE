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
#include <map>
#include <memory>
#include <optional>
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
// Live's Ungroup (Ctrl+Shift+G) on insert `track`'s slot `slot`: a one-chain Audio Effect Rack's devices take its place,
// in order. Each native parameter a macro moves keeps the value the macro gives it now (a plugin keeps its own state).
// Automation of a macro with one full-range mapping onto a device parameter moves onto that parameter, so ungrouping
// what Group made gives the old slots back (LYNX: Live drops the macro automation); other macro, selector and morph
// automation is dropped, as the macros go. Later slots' targets shift. Returns the first device's slot (the rack's slot
// for an empty rack, which just goes), or −1 with the song untouched.
int ungroupSlot (Song& song, int track, int slot);

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

// ---------------------------------------------------------------------------------------------------------------
// Drum Rack (round 190ec, docs/analise/REA_rodada190ec.md; Live's DrumGroupDevice): an Instrument Rack whose chains are
// pads. A pad's chain gets the notes of one key (its Receive note, or every note), turned into its Play note, so a
// Simpler on any pad plays at its own pitch; a note into a pad of a choke group cuts the other pads of that group; a
// chain's audio goes to the rack's output or into one of its return chains, and it sends to the return chains.
// Live's defaults [DATA: every shipped kit and the Slicing defaults]: Play C3 (60), the first pad C1 (36), no choke.

constexpr int drumPadCount = 128, drumPadColumns = 4, drumPadRows = 32;
constexpr int drumFirstPad = 36, drumPlayNote = 60, drumChokeGroups = 16;
constexpr int drumDefaultScroll = 19; // (the pad view's top row from the top: C1 at the bottom left)

// A new Drum Rack: no pads yet (Live's empty rack: "Drop an instrument or sample here"), 8 macros shown, the pads visible
PatcherDoc makeDrumRack();
bool isDrumRack (const PatcherDoc& doc) noexcept;
bool isDrumRack (const ChannelDef& channel) noexcept;
// The pad view's row (from the top) of a key, and the first key of a row
int padRow (int key) noexcept;
int rowFirstKey (int row) noexcept;

// Chain slots: 0 … chains − 1 are the chains, chains … chains + returns − 1 the return chains (null outside)
int chainSlots (const RackDoc& rack) noexcept;
bool isReturnSlot (const RackDoc& rack, int slot) noexcept;
RackChain* chainAt (RackDoc& rack, int slot) noexcept;
const RackChain* chainAt (const RackDoc& rack, int slot) noexcept;

// The chain playing pad `key` (the first one receiving that note), or −1
int padChain (const RackDoc& rack, int key) noexcept;
// What the pad of `key` shows: its chain's name; "" for an empty pad
std::string padName (const RackDoc& rack, int key);
// Every named pad: key → its chain's name (the Piano roll's key column, Live's pad names in the note editor)
std::map<int, std::string> padNames (const RackDoc& rack);
// A pad chain on `key` (Receive key, Play 60) named `name`, holding `instrument` when given (as the first device of its
// kind). Returns the chain's index, or −1 (not a Drum Rack). A key that has a pad gets another chain on it (Live layers
// chains that receive the same note).
int addPad (PatcherDoc& doc, int key, const std::string& name, std::optional<PatchNode> instrument = {});
// Live's drag of a pad onto another: the chains of the two keys trade their Receive notes (also onto an empty pad)
bool swapPads (PatcherDoc& doc, int fromKey, int toKey);
// The I/O section (each recompiles): Receive (0…127, −1 all notes), Play (0…127), Choke (0 none, 1…16), Audio To
// (−1 the rack's output, k return chain k), a send (return k: linear gain and on)
void setReceive (PatcherDoc& doc, int chain, int key);
void setPlay (PatcherDoc& doc, int chain, int key);
void setChoke (PatcherDoc& doc, int chain, int group);
void setAudioTo (PatcherDoc& doc, int chain, int target);
void setSend (PatcherDoc& doc, int chain, int returnChain, float level, bool on = true);
// Return chains: a new one (its sends start at −inf on every chain), and removing one (chains that played into it go to
// the rack's output; their sends to it go)
int addReturn (PatcherDoc& doc, const std::string& name = {});
bool removeReturn (PatcherDoc& doc, int returnChain);

// Slice to Drum Rack / Slice to New MIDI Track (Live's Simpler › Slice to Drum Rack and the audio clip's Slice to New
// MIDI Track; LYNX's own slicing templates, after the facts of Live's Slicing defaults [DATA]): one pad per slice from C1
// up (at most 92: C1…G8), each a Sampler on that slice of the sample.
enum class SliceTemplate : std::uint8_t
{
    gated,   // Live's "Default" facts: every pad in choke group 1, the slice plays while the note is held (50 ms release)
    oneShot, // every slice plays to its end, pads overlap (no choke group)
};
constexpr int maxSlicePads = drumPadCount - drumFirstPad;
// `slices`: the slice starts as fractions of the sample (sorted, the first usually 0); the slice ends at the next start
// (the last at `end`). `baseName`: the pads are "<baseName> 1", "<baseName> 2"…
ChannelDef sliceToDrumRack (const std::string& samplePath, const std::vector<float>& slices, const std::string& baseName,
                            SliceTemplate preset = SliceTemplate::gated, float end = 1.0f);
} // namespace lynx::rack
