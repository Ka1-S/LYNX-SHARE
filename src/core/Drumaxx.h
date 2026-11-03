#pragma once

// FL Studio's Drumaxx rebuilt as the engine decoded in docs/analise/REA_rodada67.md §6–§9 (clean-room; checked against
// the installation's Drumaxx_x64.dll + engine_x64.dll run unmodified).
//
// 16 pads, each a physical drum model running at 38 kHz (§7): a mallet (an exponentially decaying impulse plus noise
// from the engine's LCG) drives a bank of up to 128 four-lane delay lines whose lane lengths step by one sample (the
// "membrane", size and shape choose the lines, tension/phase/material the output weights), each lane with its own
// one-pole low-pass and feedback; then the "SOF" resonator, the LOW high-pass, the HIGH shelf, the enveloped MID peak,
// a clipper, the hold/release envelope and the lo-fi quantiser; linear resampling to the host rate with the per-key
// pitch (Final.Pitch) and smoothed left/right gains (pad volume and pan × Final.Vol). The 16 pads mix into the master
// with the 8-band EQ, the limiter and the master level (§8).
//
// The channel keeps the plugin's raw state ("SSLF" stream, engine versions DMS3…DMS5) in ChannelDef::pluginState.
//
// Not modelled (§9): the pattern sequencer (patterns start on their trigger key, 100 in every installation state) and
// swing; the outputs to separate mixer tracks (every pad plays on the master); MIDI controllers; FL's demo-mode
// silences. The CRT's powf/sinf/cosf and vector sine follow its FMA paths (§8); its double sinh/pow are the C library's.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the engine can read this Drumaxx state
bool drumaxxStateUsable (const std::vector<std::uint8_t>& state);

// The kit name stored in the state (empty if none)
std::string drumaxxKitName (const std::vector<std::uint8_t>& state);
// a pad's name (the kit's 16 names; empty when the state has none)
std::string drumaxxPadName (const std::vector<std::uint8_t>& state, int pad);

// Parameter `index` (0…719: 16 pads × 44, then the master; REA_rodada67 §6) of the state, 0…1 (NaN if unusable) / set it
float drumaxxParam (const std::vector<std::uint8_t>& state, int index);
void drumaxxSetParam (std::vector<std::uint8_t>& state, int index, float value);

std::unique_ptr<Instrument> makeDrumaxxInstrument();

namespace drumaxx
{
struct RefNote
{
    int start = 0, key = 36;
    double initVol = 1.0;      // Init.Vol (the plugin's MIDI velocity: 127·ln(10·v + 1)/ln 21)
    double finalVol = 1.0;     // Final.Vol (scales the pad's output)
    double pitchOffset = 0.0;  // Final.Pitch − 100·(key − 60), cents
};

// Renders `notes` from a raw state like the round-67 EXEC harness drives the DLL (TriggerVoice at block starts, the
// plugin's output gain starting from 0 at the first block). Interleaved stereo (2·length); empty if unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 512);
} // namespace drumaxx
} // namespace lynx
